#include "twitch-chat-client.h"

#include <obs.h>
#include <obs.hpp>
#include <QRandomGenerator>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <algorithm>

namespace {
constexpr int kTwitchIrcPort = 6697;
constexpr const char *kTwitchIrcHost = "irc.chat.twitch.tv";
constexpr int kViewerCountPollMs = 30000; // not chat-latency-sensitive, no need to poll often

struct EmoteRange {
	QString id;
	int start; // inclusive, UTF-16 code unit index into the message text
	int end;   // inclusive
};

// Parses the IRCv3 "emotes" tag value: "id:start-end,start-end/id2:start-end"
// -- each id maps to one or more character ranges in the message where that
// emote's plain-text code (e.g. "Kappa") appears, so it can be swapped for
// the actual image.
QList<EmoteRange> ParseEmotesTag(const QString &value)
{
	QList<EmoteRange> ranges;
	if (value.isEmpty())
		return ranges;

	for (const QString &idGroup : value.split('/', Qt::SkipEmptyParts)) {
		int colonIdx = idGroup.indexOf(':');
		if (colonIdx == -1)
			continue;
		QString id = idGroup.left(colonIdx);
		for (const QString &range : idGroup.mid(colonIdx + 1).split(',', Qt::SkipEmptyParts)) {
			int dashIdx = range.indexOf('-');
			if (dashIdx == -1)
				continue;
			bool okStart = false, okEnd = false;
			int start = range.left(dashIdx).toInt(&okStart);
			int end = range.mid(dashIdx + 1).toInt(&okEnd);
			if (okStart && okEnd)
				ranges.append({id, start, end});
		}
	}

	std::sort(ranges.begin(), ranges.end(), [](const EmoteRange &a, const EmoteRange &b) {
		return a.start < b.start;
	});
	return ranges;
}

// Builds a safe HTML fragment for the message: plain text is HTML-escaped
// (this is untrusted, user-typed chat content), emote ranges are replaced
// with an <img> pointing at Twitch's own CDN. 2.0 scale (~56px) downscaled
// via CSS reads clearly inline without being oversized.
QString RenderMessageHtml(const QString &text, const QList<EmoteRange> &emotes)
{
	if (emotes.isEmpty())
		return text.toHtmlEscaped();

	QString html;
	int pos = 0;
	for (const EmoteRange &e : emotes) {
		if (e.start > pos)
			html += text.mid(pos, e.start - pos).toHtmlEscaped();
		// Qt's rich-text engine (used by the dock) only sizes <img> via
		// the width/height HTML attributes, not CSS -- a "style=height:"
		// like platform-icons.cpp avoids is silently ignored there (a
		// real browser, i.e. the on-stream overlay, would honor either,
		// but the attributes work everywhere so there's no reason to
		// rely on CSS here at all).
		html += QString("<img src=\"https://static-cdn.jtvnw.net/emoticons/v2/%1/default/dark/2.0\" "
				 "width=\"20\" height=\"20\" style=\"vertical-align:middle;\">")
				.arg(e.id);
		pos = e.end + 1;
	}
	if (pos < text.size())
		html += text.mid(pos).toHtmlEscaped();

	return html;
}

// Twitch's own website's public GraphQL Client-Id -- identifies "the
// Twitch website" as the caller (the same way a browser visiting twitch.tv
// does), not a per-developer credential. Used the same way by Streamlink
// and Chatterino to read public data anonymously.
constexpr const char *kTwitchGqlClientId = "kimne78kx3ncx6brgo4mv6wki5h1ko";

// Accepts either a bare channel login name or any twitch.tv URL shape
// (channel page, /popout/<ch>/chat, /embed/<ch>/chat, with or without
// query string) and returns just the channel name -- IRC JOIN needs a
// bare name, but it's natural to paste in whatever URL you already have
// open (e.g. the same popout URL used for a Custom Browser Dock).
QString ExtractTwitchChannel(const QString &input)
{
	QString s = input.trimmed();
	int idx = s.indexOf("twitch.tv/", 0, Qt::CaseInsensitive);
	if (idx == -1)
		return s.toLower();

	s = s.mid(idx + QStringLiteral("twitch.tv/").size());
	if (s.startsWith("popout/", Qt::CaseInsensitive))
		s = s.mid(7);
	else if (s.startsWith("embed/", Qt::CaseInsensitive))
		s = s.mid(6);

	int end = -1;
	for (QChar terminator : {QChar('/'), QChar('?'), QChar('#')}) {
		int pos = s.indexOf(terminator);
		if (pos != -1 && (end == -1 || pos < end))
			end = pos;
	}
	if (end != -1)
		s = s.left(end);

	s = s.toLower();

	// Twitch login names are only ever [a-z0-9_] -- s is spliced directly
	// into a raw "JOIN #<name>\r\n" IRC line below with no further
	// encoding, so anything outside that set (most importantly \r\n
	// itself) has to be stripped here, not just trimmed off the ends.
	// Without this, a crafted value (typed into the Chat Link dialog, or a
	// tampered config file) containing an embedded \r\n could inject
	// arbitrary additional IRC commands into the connection.
	QString safe;
	safe.reserve(s.size());
	for (QChar c : s) {
		if (c.isLetterOrNumber() || c == '_')
			safe.append(c);
	}
	return safe;
}
} // namespace

TwitchChatClient::TwitchChatClient(QObject *parent) : QObject(parent)
{
	socket_ = new QSslSocket(this);
	connect(socket_, &QSslSocket::encrypted, this, &TwitchChatClient::OnEncrypted);
	connect(socket_, &QSslSocket::readyRead, this, &TwitchChatClient::OnReadyRead);
	connect(socket_, &QSslSocket::errorOccurred, this, &TwitchChatClient::OnErrorOccurred);
	connect(socket_, &QSslSocket::disconnected, this, &TwitchChatClient::OnDisconnected);

	reconnectTimer_ = new QTimer(this);
	reconnectTimer_->setSingleShot(true);
	connect(reconnectTimer_, &QTimer::timeout, this, &TwitchChatClient::OnReconnectTimer);

	nam_ = new QNetworkAccessManager(this);
	viewerCountTimer_ = new QTimer(this);
	connect(viewerCountTimer_, &QTimer::timeout, this, &TwitchChatClient::PollViewerCount);

	connect(this, &TwitchChatClient::StatusChanged, this, [this](const QString &s) { lastStatus_ = s; });
}

TwitchChatClient::~TwitchChatClient()
{
	Disconnect();
}

bool TwitchChatClient::IsConnected() const
{
	return socket_->state() == QAbstractSocket::ConnectedState;
}

void TwitchChatClient::JoinChannel(const QString &channel)
{
	QString normalized = ExtractTwitchChannel(channel);
	if (normalized == channel_)
		return;

	channel_ = normalized;
	Disconnect();
	viewerCountTimer_->stop();
	emit ViewerCountChanged(-1);
	ackedCapabilities_.clear();
	lastMessageAt_ = QDateTime();
	messagesReceivedCount_ = 0;

	if (channel_.isEmpty()) {
		emit StatusChanged("Not connected");
		return;
	}

	ConnectToTwitch();
	PollViewerCount();
	viewerCountTimer_->start(kViewerCountPollMs);
}

void TwitchChatClient::Disconnect()
{
	reconnectTimer_->stop();
	if (socket_->state() != QAbstractSocket::UnconnectedState) {
		socket_->disconnectFromHost();
		if (socket_->state() != QAbstractSocket::UnconnectedState)
			socket_->waitForDisconnected(1000);
	}
	buffer_.clear();
}

void TwitchChatClient::ConnectToTwitch()
{
	if (channel_.isEmpty())
		return;
	blog(LOG_INFO, "[OBS-Simulcast] Twitch IRC: connecting to %s:%d for #%s", kTwitchIrcHost, kTwitchIrcPort,
	     channel_.toUtf8().constData());
	emit StatusChanged(QString("Connecting to #%1…").arg(channel_));
	socket_->connectToHostEncrypted(kTwitchIrcHost, kTwitchIrcPort);
}

void TwitchChatClient::OnEncrypted()
{
	// Anonymous read-only login: any "justinfanNNNNN" nick is accepted by
	// Twitch for chat viewing without an OAuth token. Requesting the tags
	// capability gets each PRIVMSG an "emotes" tag identifying which words
	// in the message are Twitch emotes (Kappa, sub emotes, etc.) and where
	// -- without it, those just arrive as their plain-text code with no
	// way to tell them apart from a chatter literally typing that word.
	int suffix = QRandomGenerator::global()->bounded(10000, 99999);
	// "SCHMOOPIIE" + only the "tags" capability is old community folklore
	// that stopped actually relaying PRIVMSG somewhere along the way (JOIN
	// still succeeded, but zero chat messages ever followed it -- confirmed
	// from raw IRC logs). gempir/go-twitch-irc, an actively-maintained
	// library real bots currently use for anonymous read access, instead
	// uses PASS "oauth:59301" and requests both twitch.tv/tags AND
	// twitch.tv/commands -- matching that exactly here since it's the
	// combination known to still work today.
	QByteArray login = "CAP REQ :twitch.tv/tags twitch.tv/commands\r\nCAP END\r\nPASS oauth:59301\r\nNICK justinfan" +
			    QByteArray::number(suffix) + "\r\nJOIN #" + channel_.toUtf8() + "\r\n";
	blog(LOG_INFO, "[OBS-Simulcast] Twitch IRC: TLS handshake done, sending login as justinfan%d, joining #%s",
	     suffix, channel_.toUtf8().constData());
	socket_->write(login);
	// This is optimistic (sent, not server-confirmed) -- ProcessLine() logs
	// the actual server responses below, which is what confirms the JOIN
	// really succeeded.
	emit StatusChanged(QString("Connected to #%1").arg(channel_));
}

void TwitchChatClient::OnReadyRead()
{
	buffer_.append(socket_->readAll());

	int idx;
	while ((idx = buffer_.indexOf("\r\n")) != -1) {
		QByteArray lineBytes = buffer_.left(idx);
		buffer_.remove(0, idx + 2);
		ProcessLine(QString::fromUtf8(lineBytes));
	}
}

void TwitchChatClient::ProcessLine(const QString &rawLine)
{
	if (rawLine.isEmpty())
		return;

	// Temporary, intentionally verbose: logs every raw line the server
	// sends so a connection that *looks* fine in the status label (JOIN
	// sent, no error/disconnect) but never renders chat can actually be
	// diagnosed from the log instead of guessed at -- this shows exactly
	// what Twitch is sending, or confirms nothing's arriving at all.
	blog(LOG_INFO, "[OBS-Simulcast] Twitch IRC <<%s", rawLine.toUtf8().constData());

	if (rawLine.startsWith("PING")) {
		socket_->write("PONG :tmi.twitch.tv\r\n");
		return;
	}

	// "<prefix> CAP <nick> ACK :<space-separated capabilities>"
	int capAckIdx = rawLine.indexOf(" CAP * ACK :");
	if (capAckIdx != -1) {
		// Twitch may ACK requested capabilities across more than one line --
		// accumulate rather than overwrite.
		QString caps = rawLine.mid(capAckIdx + QStringLiteral(" CAP * ACK :").size());
		for (const QString &cap : caps.split(' ', Qt::SkipEmptyParts)) {
			if (!ackedCapabilities_.contains(cap))
				ackedCapabilities_.append(cap);
		}
		return;
	}

	// With the tags capability requested, most lines now start with
	// "@key=value;key2=value2 " before the usual ":nick!... " prefix.
	QString line = rawLine;
	QString emotesTag;
	if (line.startsWith('@')) {
		int spaceIdx = line.indexOf(' ');
		if (spaceIdx == -1)
			return;
		QString tags = line.left(spaceIdx);
		line = line.mid(spaceIdx + 1);

		int emotesIdx = tags.indexOf(";emotes=");
		int start = -1;
		if (tags.startsWith("@emotes=")) {
			start = 8;
		} else if (emotesIdx != -1) {
			start = emotesIdx + 8;
		}
		if (start != -1) {
			int end = tags.indexOf(';', start);
			emotesTag = end == -1 ? tags.mid(start) : tags.mid(start, end - start);
		}
	}

	// :nick!nick@nick.tmi.twitch.tv PRIVMSG #channel :message text
	if (!line.startsWith(':'))
		return;

	int privmsgIdx = line.indexOf(" PRIVMSG #");
	if (privmsgIdx == -1)
		return;

	QString prefix = line.mid(1, privmsgIdx - 1); // "nick!nick@nick.tmi.twitch.tv"
	int bangIdx = prefix.indexOf('!');
	QString username = bangIdx == -1 ? prefix : prefix.left(bangIdx);

	int msgStart = line.indexOf(" :", privmsgIdx + 1);
	if (msgStart == -1)
		return;
	QString text = line.mid(msgStart + 2);

	QString html = RenderMessageHtml(text, ParseEmotesTag(emotesTag));
	lastMessageAt_ = QDateTime::currentDateTime();
	messagesReceivedCount_++;
	emit MessageReceived(username, html);
}

void TwitchChatClient::OnErrorOccurred()
{
	blog(LOG_WARNING, "[OBS-Simulcast] Twitch IRC: socket error: %s",
	     socket_->errorString().toUtf8().constData());
	emit StatusChanged(QString("Error: %1").arg(socket_->errorString()));
	// QSslSocket emits disconnected() after an error too, which schedules
	// the reconnect -- avoid double-scheduling here.
}

void TwitchChatClient::OnDisconnected()
{
	buffer_.clear();
	if (channel_.isEmpty())
		return;
	blog(LOG_WARNING, "[OBS-Simulcast] Twitch IRC: disconnected from #%s, reconnecting in 5s",
	     channel_.toUtf8().constData());
	emit StatusChanged(QString("Disconnected, retrying…"));
	reconnectTimer_->start(5000);
}

void TwitchChatClient::OnReconnectTimer()
{
	if (!channel_.isEmpty() && socket_->state() == QAbstractSocket::UnconnectedState)
		ConnectToTwitch();
}

void TwitchChatClient::PollViewerCount()
{
	if (channel_.isEmpty())
		return;

	OBSDataAutoRelease body = obs_data_create();
	std::string query = "query{user(login:\"" + channel_.toStdString() + "\"){stream{viewersCount}}}";
	obs_data_set_string(body, "query", query.c_str());

	QNetworkRequest req(QUrl("https://gql.twitch.tv/gql"));
	req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	req.setRawHeader("Client-Id", kTwitchGqlClientId);

	QNetworkReply *reply = nam_->post(req, QByteArray(obs_data_get_json(body)));
	connect(reply, &QNetworkReply::finished, this, &TwitchChatClient::OnViewerCountFinished);
}

void TwitchChatClient::OnViewerCountFinished()
{
	auto *reply = qobject_cast<QNetworkReply *>(sender());
	if (!reply)
		return;
	reply->deleteLater();

	if (reply->error() != QNetworkReply::NoError)
		return; // transient -- next 30s tick will retry; not worth surfacing as a chat error

	OBSDataAutoRelease root = obs_data_create_from_json(reply->readAll().constData());
	OBSDataAutoRelease data = root ? obs_data_get_obj(root, "data") : nullptr;
	OBSDataAutoRelease user = data ? obs_data_get_obj(data, "user") : nullptr;
	OBSDataAutoRelease stream = user ? obs_data_get_obj(user, "stream") : nullptr;

	// A null "stream" means the channel is offline -- not an error, just
	// nothing to report a count for.
	emit ViewerCountChanged(stream ? (int)obs_data_get_int(stream, "viewersCount") : -1);
}
