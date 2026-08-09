#include "youtube-chat-client.h"
#include "multistream-manager.h"

#include <obs.h>
#include <obs.hpp>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

namespace {
// YouTube's response tells us how long to wait before polling again
// (timeoutMs) -- it's a suggestion from its own web client, not a hard
// requirement, and other InnerTube-based tools commonly poll faster than
// it asks for. kMaxPollMs caps how much of that suggestion we honor:
// conservative enough to stay well clear of rate-limiting/IP blocks (which
// reportedly start becoming a risk under ~1s), but well under the ~10s
// YouTube sometimes asks for on a quiet chat.
constexpr int kMinPollMs = 2000;
constexpr int kMaxPollMs = 4000;
constexpr int kNotLiveRetryMs = 30000;      // no user-configurable quota anymore, so this can be short
constexpr int kContinuationRetryMs = 10000; // live but chat page not ready / transient fetch failure
constexpr int kMaxContinuationRetries = 5;
constexpr int kLivenessCheckMs = 1000; // local check only, no network -- cheap to poll often
constexpr int kViewerCountPollMs = 30000; // not chat-latency-sensitive, no need to poll often

// YouTube's own web client's public InnerTube key -- baked into every
// youtube.com page load (view source on any video, it's right there in
// ytcfg.set(...)). This is not a per-developer credential like a Data API
// key; it identifies "the YouTube website" as the caller, the same way a
// browser visiting youtube.com does, which is why it isn't quota-limited
// against a Google Cloud project the way the official Data API is.
constexpr const char *kInnertubeKey = "AIzaSyAO_FJ2SlqU8Q4STEHLGCilw_Y9_11qcW8";
constexpr const char *kInnertubeClientVersion = "2.20240101.00.00";
constexpr const char *kDesktopUserAgent =
	"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Safari/537.36";

// Accepts a raw "UC..." channel ID, a full channel URL
// (.../channel/UCxxxx), or a handle (@name or .../@name) and returns the
// URL path segment to fetch (e.g. "channel/UCxxxx" or "@name").
QString ChannelUrlPath(const QString &input)
{
	QString s = input.trimmed();

	int idx = s.indexOf("/channel/");
	if (idx != -1) {
		QString rest = s.mid(idx + 9);
		int end = rest.indexOf(QRegularExpression("[/?#]"));
		return "channel/" + (end == -1 ? rest : rest.left(end));
	}

	idx = s.indexOf("/@");
	if (idx != -1)
		s = s.mid(idx + 1);

	if (!s.startsWith('@')) {
		if (s.startsWith("UC") && s.size() >= 20)
			return "channel/" + s;
		s = "@" + s;
	}

	int end = s.indexOf(QRegularExpression("[/?#]"));
	return end == -1 ? s : s.left(end);
}

// If the input is already a specific video/chat -- a live_chat popout URL
// (.../live_chat?v=ID), a watch URL (.../watch?v=ID), a youtu.be short
// link, or a bare 11-char video ID -- return that ID directly so callers
// can skip the channel-live-search step entirely. Returns empty for
// anything that looks like a channel handle/URL/ID instead.
QString DirectVideoId(const QString &input)
{
	QString s = input.trimmed();

	QRegularExpression queryIdRe(R"([?&]v=([\w-]{11}))");
	QRegularExpressionMatch m = queryIdRe.match(s);
	if (m.hasMatch())
		return m.captured(1);

	QRegularExpression shortLinkRe(R"(youtu\.be/([\w-]{11}))");
	m = shortLinkRe.match(s);
	if (m.hasMatch())
		return m.captured(1);

	QRegularExpression bareIdRe(R"(^[\w-]{11}$)");
	if (bareIdRe.match(s).hasMatch())
		return s;

	return {};
}

QNetworkRequest MakeHtmlRequest(const QUrl &url)
{
	QNetworkRequest req(url);
	req.setHeader(QNetworkRequest::UserAgentHeader, QString(kDesktopUserAgent));
	req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	// Without this, a fresh cookie-less session (which every request here
	// is, since there's no persistent login) can get served YouTube's EU
	// cookie-consent interstitial instead of the actual channel/live page,
	// which has none of the markers this client looks for -- indistinguishable
	// from "not live" otherwise. This is the standard bypass value other
	// InnerTube-based scrapers (yt-dlp, pytchat) use.
	req.setRawHeader("Cookie", "CONSENT=YES+1");
	req.setRawHeader("Accept-Language", "en-US,en;q=0.9");
	return req;
}
} // namespace

YouTubeChatClient::YouTubeChatClient(QObject *parent) : QObject(parent)
{
	nam_ = new QNetworkAccessManager(this);
	pollTimer_ = new QTimer(this);
	pollTimer_->setSingleShot(true);
	connect(pollTimer_, &QTimer::timeout, this, &YouTubeChatClient::PollTick);
	obs_frontend_add_event_callback(OnFrontendEvent, this);

	livenessTimer_ = new QTimer(this);
	connect(livenessTimer_, &QTimer::timeout, this, &YouTubeChatClient::CheckStreamingActive);
	livenessTimer_->start(kLivenessCheckMs);

	viewerCountTimer_ = new QTimer(this);
	connect(viewerCountTimer_, &QTimer::timeout, this, &YouTubeChatClient::PollViewerCount);

	connect(this, &YouTubeChatClient::StatusChanged, this, [this](const QString &s) { lastStatus_ = s; });
}

YouTubeChatClient::~YouTubeChatClient()
{
	obs_frontend_remove_event_callback(OnFrontendEvent, this);
}

void YouTubeChatClient::OnFrontendEvent(enum obs_frontend_event event, void *data)
{
	// Just a fast-path for the OBS-native-streaming case -- CheckStreamingActive()
	// below is what actually covers this plugin's own Go Live button, since
	// that never fires an OBS frontend event at all.
	if (event == OBS_FRONTEND_EVENT_EXIT) {
		auto *self = static_cast<YouTubeChatClient *>(data);
		self->streamingActive_ = false;
		self->Stop();
	}
}

void YouTubeChatClient::CheckStreamingActive()
{
	bool active = obs_frontend_streaming_active() || MultistreamManager::Get().IsAnyActive();
	if (active == streamingActive_)
		return;

	streamingActive_ = active;
	if (streamingActive_) {
		if (!channelInput_.isEmpty())
			FindLiveVideo();
	} else {
		Stop();
		emit StatusChanged("Not connected (stream not live)");
	}
}

void YouTubeChatClient::Configure(const QString &channel)
{
	QString chan = channel.trimmed();
	if (chan == channelInput_)
		return;

	channelInput_ = chan;
	Stop();

	if (channelInput_.isEmpty()) {
		emit StatusChanged("Not connected");
		return;
	}

	emit StatusChanged(streamingActive_ ? "Not connected" : "Not connected (stream not live)");
	if (streamingActive_)
		FindLiveVideo();
}

void YouTubeChatClient::Stop()
{
	pollTimer_->stop();
	viewerCountTimer_->stop();
	emit ViewerCountChanged(-1);
	videoId_.clear();
	continuation_.clear();
	retryCount_ = 0;
	lastMessageAt_ = QDateTime();
	messagesReceivedCount_ = 0;
}

void YouTubeChatClient::PollTick()
{
	if (!streamingActive_ || channelInput_.isEmpty())
		return;
	if (videoId_.isEmpty())
		FindLiveVideo();
	else if (continuation_.isEmpty())
		FetchInitialContinuation();
	else
		PollMessages();
}

void YouTubeChatClient::FindLiveVideo()
{
	QString directId = DirectVideoId(channelInput_);
	if (!directId.isEmpty()) {
		videoId_ = directId;
		retryCount_ = 0;
		FetchInitialContinuation();
		return;
	}

	emit StatusChanged("Checking for a live stream…");

	QUrl url("https://www.youtube.com/" + ChannelUrlPath(channelInput_) + "/live");
	QNetworkReply *reply = nam_->get(MakeHtmlRequest(url));
	connect(reply, &QNetworkReply::finished, this, &YouTubeChatClient::OnFindLiveVideoFinished);
}

void YouTubeChatClient::OnFindLiveVideoFinished()
{
	auto *reply = qobject_cast<QNetworkReply *>(sender());
	if (!reply)
		return;
	reply->deleteLater();

	if (reply->error() != QNetworkReply::NoError) {
		emit StatusChanged(QString("Error checking live status: %1").arg(reply->errorString()));
		pollTimer_->start(kNotLiveRetryMs);
		return;
	}

	QString foundId;

	// Signal 1: when actually live, YouTube's redirect chain for
	// "/<channel>/live" lands on the specific watch URL -- check the final
	// URL first since it doesn't depend on parsing page content at all.
	QRegularExpression urlIdRe(R"([?&]v=([\w-]{11}))");
	QRegularExpressionMatch urlMatch = urlIdRe.match(reply->url().toString());
	if (urlMatch.hasMatch())
		foundId = urlMatch.captured(1);

	QString html = QString::fromUtf8(reply->readAll());

	// Signal 2: the page's canonical link, when it points at a watch URL
	// rather than back at the channel itself.
	if (foundId.isEmpty()) {
		QRegularExpression canonicalRe(
			R"(rel="canonical"\s+href="https://www\.youtube\.com/watch\?v=([\w-]{11}))");
		QRegularExpressionMatch m = canonicalRe.match(html);
		if (m.hasMatch())
			foundId = m.captured(1);
	}

	// Signal 3: ytInitialData/player response embed an explicit live flag
	// next to the video ID for channels where the above two don't apply
	// (e.g. members-only or unlisted broadcasts still reachable via /live).
	if (foundId.isEmpty() && html.contains("\"isLive\":true")) {
		QRegularExpression videoIdRe(R"re("videoId":"([\w-]{11})")re");
		QRegularExpressionMatch m = videoIdRe.match(html);
		if (m.hasMatch())
			foundId = m.captured(1);
	}

	if (foundId.isEmpty()) {
		emit StatusChanged("Not currently live -- checking again soon");
		pollTimer_->start(kNotLiveRetryMs);
		return;
	}

	videoId_ = foundId;
	retryCount_ = 0;
	FetchInitialContinuation();
}

void YouTubeChatClient::FetchInitialContinuation()
{
	emit StatusChanged("Connecting to live chat…");

	QUrl url("https://www.youtube.com/live_chat");
	QUrlQuery q;
	q.addQueryItem("v", videoId_);
	q.addQueryItem("is_popout", "1");
	url.setQuery(q);

	QNetworkReply *reply = nam_->get(MakeHtmlRequest(url));
	connect(reply, &QNetworkReply::finished, this, &YouTubeChatClient::OnFetchContinuationFinished);
}

void YouTubeChatClient::OnFetchContinuationFinished()
{
	auto *reply = qobject_cast<QNetworkReply *>(sender());
	if (!reply)
		return;
	reply->deleteLater();

	auto retry = [this]() {
		if (++retryCount_ > kMaxContinuationRetries) {
			// Broadcast may have ended between finding it and here, or
			// its chat is disabled -- start over from scratch rather
			// than retrying a specific video forever.
			videoId_.clear();
			retryCount_ = 0;
			viewerCountTimer_->stop();
			emit ViewerCountChanged(-1);
			emit StatusChanged("Couldn't connect to chat -- checking for a live stream again");
			pollTimer_->start(kNotLiveRetryMs);
			return;
		}
		pollTimer_->start(kContinuationRetryMs);
	};

	if (reply->error() != QNetworkReply::NoError) {
		emit StatusChanged(QString("Error connecting to chat: %1").arg(reply->errorString()));
		retry();
		return;
	}

	QString html = QString::fromUtf8(reply->readAll());
	QRegularExpression continuationRe(R"re("continuation":"([^"]+)")re");
	QRegularExpressionMatch m = continuationRe.match(html);
	if (!m.hasMatch()) {
		emit StatusChanged("Live chat not available for this stream yet -- retrying");
		retry();
		return;
	}

	continuation_ = m.captured(1);
	retryCount_ = 0;
	emit StatusChanged("Connected");
	PollMessages();

	PollViewerCount();
	viewerCountTimer_->start(kViewerCountPollMs);
}

void YouTubeChatClient::PollViewerCount()
{
	if (videoId_.isEmpty())
		return;

	QUrl url("https://www.youtube.com/watch");
	QUrlQuery q;
	q.addQueryItem("v", videoId_);
	url.setQuery(q);

	QNetworkReply *reply = nam_->get(MakeHtmlRequest(url));
	connect(reply, &QNetworkReply::finished, this, &YouTubeChatClient::OnViewerCountFinished);
}

void YouTubeChatClient::OnViewerCountFinished()
{
	auto *reply = qobject_cast<QNetworkReply *>(sender());
	if (!reply)
		return;
	reply->deleteLater();

	if (reply->error() != QNetworkReply::NoError)
		return; // transient -- next 30s tick will retry; not worth surfacing as a chat error

	QString html = QString::fromUtf8(reply->readAll());

	// videoDetails.viewCount is NOT the concurrent viewer count -- it's the
	// page's total historical view count (interactionCount) even while a
	// broadcast is live, which is why viewer counts were showing numbers
	// far larger than who's actually watching. The real live "watching
	// now" figure lives nested under videoViewCountRenderer as either
	// {"simpleText":"1,234 watching"} or {"runs":[{"text":"1,234"}, ...]}
	// -- an object, not a bare quoted digit string, so the old regex could
	// never match it and silently fell through to videoDetails instead.
	QRegularExpression liveViewCountRe(
		R"re("videoViewCountRenderer":\{"viewCount":\{(?:"simpleText"|"runs":\[\{"text")\s*:\s*"([\d,]+)")re");
	QRegularExpressionMatch m = liveViewCountRe.match(html);
	if (!m.hasMatch()) {
		emit ViewerCountChanged(-1);
		return;
	}
	QString digits = m.captured(1);
	digits.remove(',');
	emit ViewerCountChanged(digits.toInt());
}

void YouTubeChatClient::PollMessages()
{
	QUrl url("https://www.youtube.com/youtubei/v1/live_chat/get_live_chat");
	QUrlQuery q;
	q.addQueryItem("key", kInnertubeKey);
	url.setQuery(q);

	OBSDataAutoRelease body = obs_data_create();
	OBSDataAutoRelease context = obs_data_create();
	OBSDataAutoRelease client = obs_data_create();
	obs_data_set_string(client, "clientName", "WEB");
	obs_data_set_string(client, "clientVersion", kInnertubeClientVersion);
	obs_data_set_obj(context, "client", client);
	obs_data_set_obj(body, "context", context);
	obs_data_set_string(body, "continuation", continuation_.toUtf8().constData());

	QNetworkRequest req(url);
	req.setHeader(QNetworkRequest::UserAgentHeader, QString(kDesktopUserAgent));
	req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

	QNetworkReply *reply = nam_->post(req, QByteArray(obs_data_get_json(body)));
	connect(reply, &QNetworkReply::finished, this, &YouTubeChatClient::OnPollFinished);
}

void YouTubeChatClient::OnPollFinished()
{
	auto *reply = qobject_cast<QNetworkReply *>(sender());
	if (!reply)
		return;
	reply->deleteLater();

	if (reply->error() != QNetworkReply::NoError) {
		// Continuation likely expired (broadcast ended, or the token
		// simply timed out) -- start over rather than hammering a dead
		// continuation.
		emit StatusChanged(QString("Error: %1 -- reconnecting…").arg(reply->errorString()));
		videoId_.clear();
		continuation_.clear();
		viewerCountTimer_->stop();
		emit ViewerCountChanged(-1);
		pollTimer_->start(kNotLiveRetryMs);
		return;
	}

	OBSDataAutoRelease root = obs_data_create_from_json(reply->readAll().constData());
	if (!root) {
		pollTimer_->start(kMinPollMs);
		return;
	}

	OBSDataAutoRelease continuationContents = obs_data_get_obj(root, "continuationContents");
	OBSDataAutoRelease liveChatContinuation = continuationContents ? obs_data_get_obj(continuationContents, "liveChatContinuation") : nullptr;
	if (!liveChatContinuation) {
		// No continuationContents at all usually means the stream ended.
		emit StatusChanged("Live broadcast ended -- checking again soon");
		videoId_.clear();
		continuation_.clear();
		viewerCountTimer_->stop();
		emit ViewerCountChanged(-1);
		pollTimer_->start(kNotLiveRetryMs);
		return;
	}

	OBSDataArrayAutoRelease actions = obs_data_get_array(liveChatContinuation, "actions");
	size_t count = actions ? obs_data_array_count(actions) : 0;
	for (size_t i = 0; i < count; i++) {
		OBSDataAutoRelease action = obs_data_array_item(actions, i);
		OBSDataAutoRelease addItem = obs_data_get_obj(action, "addChatItemAction");
		OBSDataAutoRelease item = addItem ? obs_data_get_obj(addItem, "item") : nullptr;
		OBSDataAutoRelease textMsg = item ? obs_data_get_obj(item, "liveChatTextMessageRenderer") : nullptr;
		if (!textMsg)
			continue;

		OBSDataAutoRelease authorName = obs_data_get_obj(textMsg, "authorName");
		const char *name = authorName ? obs_data_get_string(authorName, "simpleText") : nullptr;
		if (!name || !*name)
			continue;

		OBSDataAutoRelease message = obs_data_get_obj(textMsg, "message");
		OBSDataArrayAutoRelease runs = message ? obs_data_get_array(message, "runs") : nullptr;
		size_t runCount = runs ? obs_data_array_count(runs) : 0;
		QString text;
		for (size_t r = 0; r < runCount; r++) {
			OBSDataAutoRelease run = obs_data_array_item(runs, r);

			const char *t = obs_data_get_string(run, "text");
			if (t && *t) {
				// Plain text is untrusted, user-typed chat content --
				// escape it now since downstream renderers no longer
				// re-escape "text" (it needs to stay raw HTML so the
				// <img> tags below render instead of showing as
				// literal angle-bracket text).
				text += QString::fromUtf8(t).toHtmlEscaped();
				continue;
			}

			// Emoji (including plain Unicode ones typed by chatters, not
			// just YouTube's custom channel emoji) arrive as a separate
			// run shape -- {"emoji": {...}} -- with no "text" field at
			// all, so they were silently dropped without this.
			OBSDataAutoRelease emoji = obs_data_get_obj(run, "emoji");
			if (!emoji)
				continue;

			bool isCustom = obs_data_get_bool(emoji, "isCustomEmoji");
			const char *emojiId = obs_data_get_string(emoji, "emojiId");
			if (!isCustom && emojiId && *emojiId) {
				// For standard emoji, emojiId *is* the literal Unicode
				// character(s) (e.g. "\U0001F389") -- no HTML metachars
				// in it, safe to use directly.
				text += QString::fromUtf8(emojiId);
				continue;
			}

			// Custom (channel-specific) emoji have no Unicode form --
			// render the actual image from YouTube's own CDN, using the
			// largest available thumbnail. Falls back to the text
			// shortcut (e.g. ":tada:") if the image URL is ever missing.
			OBSDataAutoRelease image = obs_data_get_obj(emoji, "image");
			OBSDataArrayAutoRelease thumbnails = image ? obs_data_get_array(image, "thumbnails") : nullptr;
			size_t thumbCount = thumbnails ? obs_data_array_count(thumbnails) : 0;
			const char *imageUrl = nullptr;
			if (thumbCount > 0) {
				OBSDataAutoRelease lastThumb = obs_data_array_item(thumbnails, thumbCount - 1);
				imageUrl = obs_data_get_string(lastThumb, "url");
			}

			if (imageUrl && *imageUrl) {
				// width/height attributes, not CSS -- Qt's rich-text
				// engine (the dock) only sizes <img> via the HTML
				// attributes, silently ignoring CSS-only sizing.
				text += QString("<img src=\"%1\" width=\"20\" height=\"20\" style=\"vertical-align:middle;\">")
						.arg(QString::fromUtf8(imageUrl).toHtmlEscaped());
				continue;
			}

			OBSDataArrayAutoRelease shortcuts = obs_data_get_array(emoji, "shortcuts");
			if (shortcuts && obs_data_array_count(shortcuts) > 0) {
				OBSDataAutoRelease shortcutItem = obs_data_array_item(shortcuts, 0);
				const char *shortcut = obs_data_get_string(shortcutItem, "value");
				if (shortcut && *shortcut)
					text += QString::fromUtf8(shortcut).toHtmlEscaped();
			}
		}
		if (!text.isEmpty()) {
			lastMessageAt_ = QDateTime::currentDateTime();
			messagesReceivedCount_++;
			emit MessageReceived(QString::fromUtf8(name), text);
		}
	}

	// Continuation for the next poll: InnerTube hands back a fresh token
	// (and how long to wait) under whichever continuation type applies --
	// try them in the order the live chat UI itself does.
	OBSDataArrayAutoRelease continuations = obs_data_get_array(liveChatContinuation, "continuations");
	if (!continuations || obs_data_array_count(continuations) == 0) {
		emit StatusChanged("Live broadcast ended -- checking again soon");
		videoId_.clear();
		continuation_.clear();
		viewerCountTimer_->stop();
		emit ViewerCountChanged(-1);
		pollTimer_->start(kNotLiveRetryMs);
		return;
	}

	OBSDataAutoRelease nextCont = obs_data_array_item(continuations, 0);
	int64_t timeoutMs = kMinPollMs;
	const char *nextToken = nullptr;
	for (const char *key : {"invalidationContinuationData", "timedContinuationData", "reloadContinuationData"}) {
		OBSDataAutoRelease data = obs_data_get_obj(nextCont, key);
		if (!data)
			continue;
		const char *tok = obs_data_get_string(data, "continuation");
		if (tok && *tok) {
			nextToken = tok;
			int64_t t = obs_data_get_int(data, "timeoutMs");
			if (t > 0)
				timeoutMs = t;
			break;
		}
	}

	if (!nextToken) {
		emit StatusChanged("Live broadcast ended -- checking again soon");
		videoId_.clear();
		continuation_.clear();
		viewerCountTimer_->stop();
		emit ViewerCountChanged(-1);
		pollTimer_->start(kNotLiveRetryMs);
		return;
	}

	continuation_ = QString::fromUtf8(nextToken);
	if (timeoutMs < kMinPollMs)
		timeoutMs = kMinPollMs;
	else if (timeoutMs > kMaxPollMs)
		timeoutMs = kMaxPollMs;

	emit StatusChanged("Connected");
	pollTimer_->start((int)timeoutMs);
}
