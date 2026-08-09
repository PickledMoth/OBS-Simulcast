#include "chat-overlay-server.h"
#include "platform-icons.h"

#include <obs.h>
#include <obs.hpp>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>

namespace {
constexpr int kMaxBacklog = 50;

// IconToImgTag() returns a full <img> tag; only the base64 payload is
// wanted here since the overlay page builds its own <img> per message.
QString ExtractBase64(const QString &imgTag)
{
	int start = imgTag.indexOf("base64,") + 7;
	int end = imgTag.indexOf('"', start);
	return (start >= 7 && end > start) ? imgTag.mid(start, end - start) : QString();
}
} // namespace

ChatOverlayServer::ChatOverlayServer(QObject *parent) : QObject(parent)
{
	server_ = new QTcpServer(this);
	connect(server_, &QTcpServer::newConnection, this, &ChatOverlayServer::OnNewConnection);
}

ChatOverlayServer::~ChatOverlayServer()
{
	server_->close();
}

bool ChatOverlayServer::Start(quint16 port)
{
	if (server_->isListening())
		return true;

	// 127.0.0.1 only -- this is a read-only broadcast of already-public
	// chat with no auth, which is fine as long as it never leaves the
	// machine; binding any other interface would make it reachable over
	// the LAN, which it has no business being.
	if (!server_->listen(QHostAddress::LocalHost, port))
		return false;

	port_ = port;
	return true;
}

QString ChatOverlayServer::Url() const
{
	return server_->isListening() ? QString("http://127.0.0.1:%1/").arg(port_) : QString();
}

void ChatOverlayServer::OnNewConnection()
{
	while (QTcpSocket *socket = server_->nextPendingConnection()) {
		connect(socket, &QTcpSocket::readyRead, this, &ChatOverlayServer::OnClientReadyRead);
		connect(socket, &QTcpSocket::disconnected, this, &ChatOverlayServer::OnClientDisconnected);
	}
}

void ChatOverlayServer::OnClientReadyRead()
{
	auto *socket = qobject_cast<QTcpSocket *>(sender());
	if (!socket)
		return;

	// Only the request line is needed to route "/" vs "/events" -- headers
	// and body (there isn't one, this only ever serves GET) are ignored.
	QByteArray data = socket->peek(2048);
	int lineEnd = data.indexOf("\r\n");
	QString requestLine = QString::fromLatin1(lineEnd == -1 ? data : data.left(lineEnd));
	socket->readAll(); // drop whatever was buffered now that we've peeked it

	HandleRequest(socket, requestLine);
}

void ChatOverlayServer::HandleRequest(QTcpSocket *socket, const QString &requestLine)
{
	if (requestLine.startsWith("GET /events"))
		ServeEventStream(socket);
	else
		ServeIndexPage(socket);
}

void ChatOverlayServer::ServeIndexPage(QTcpSocket *socket)
{
	static const QString twitchIconB64 = ExtractBase64(IconToImgTag(MakeTwitchIcon(16)));
	static const QString youtubeIconB64 = ExtractBase64(IconToImgTag(MakeYouTubeIcon(16)));

	QString html = QString(R"HTML(<!doctype html>
<html><head><meta charset="utf-8">
<style>
  html, body { background: transparent; margin: 0; padding: 0; overflow: hidden; }
  body { font-family: "Segoe UI", sans-serif; font-size: 16px; color: #fff;
         text-shadow: 0 1px 3px rgba(0,0,0,0.9); }
  #feed { display: flex; flex-direction: column; justify-content: flex-end;
          min-height: 100vh; padding: 8px; box-sizing: border-box; }
  .msg { margin: 2px 0; line-height: 1.4; word-wrap: break-word; }
  .msg img { vertical-align: middle; margin-right: 4px; width: 16px; height: 16px; }
  .name { font-weight: 700; }
</style></head>
<body>
<div id="feed"></div>
<script>
const ICONS = { Twitch: "data:image/png;base64,%1", YouTube: "data:image/png;base64,%2" };
const COLORS = { Twitch: "#a970ff", YouTube: "#ff0000" };
const feed = document.getElementById("feed");
const MAX_ON_SCREEN = 20;

function addMessage(m) {
    const div = document.createElement("div");
    div.className = "msg";
    div.dataset.id = m.id;
    const icon = ICONS[m.platform] || "";
    const color = COLORS[m.platform] || "#fff";
    // m.text arrives pre-escaped as safe HTML from the plugin (plain
    // content escaped server-side, emotes/emoji as trusted <img> tags) --
    // only m.username still needs escaping here, since it's plain text.
    div.innerHTML = '<img src="' + icon + '"><span class="name" style="color:' + color + '">' +
        escapeHtml(m.username) + '</span>: ' + m.text;
    feed.appendChild(div);
    while (feed.children.length > MAX_ON_SCREEN)
        feed.removeChild(feed.firstChild);
}

function removeMessage(id) {
    const el = feed.querySelector('[data-id="' + id + '"]');
    if (el)
        el.remove();
}

function escapeHtml(s) {
    const d = document.createElement("div");
    d.innerText = s;
    return d.innerHTML;
}

const source = new EventSource("/events");
source.onmessage = (e) => {
    const m = JSON.parse(e.data);
    if (m.deleted)
        removeMessage(m.id);
    else
        addMessage(m);
};
</script>
</body></html>)HTML")
				.arg(twitchIconB64, youtubeIconB64);

	QByteArray body = html.toUtf8();
	QByteArray response = "HTTP/1.1 200 OK\r\n"
			       "Content-Type: text/html; charset=utf-8\r\n"
			       "Content-Length: " +
			       QByteArray::number(body.size()) +
			       "\r\n"
			       "Connection: close\r\n\r\n" +
			       body;
	socket->write(response);
	socket->disconnectFromHost();
}

void ChatOverlayServer::ServeEventStream(QTcpSocket *socket)
{
	// No Access-Control-Allow-Origin here on purpose: the only legitimate
	// consumer is this same server's own index page, opened as a same-origin
	// EventSource -- that needs no CORS header at all. Adding "*" would only
	// let some unrelated website's background JS read this feed (channel
	// name, chat content, viewer activity) if the streamer happens to have
	// it open in a browser tab while OBS is running.
	socket->write("HTTP/1.1 200 OK\r\n"
		      "Content-Type: text/event-stream\r\n"
		      "Cache-Control: no-cache\r\n"
		      "Connection: keep-alive\r\n\r\n");

	for (const BacklogEntry &entry : backlog_)
		socket->write(entry.frame.toUtf8());

	sseClients_.append(socket);
}

void ChatOverlayServer::OnClientDisconnected()
{
	auto *socket = qobject_cast<QTcpSocket *>(sender());
	if (!socket)
		return;
	sseClients_.removeAll(socket);
	socket->deleteLater();
}

void ChatOverlayServer::PushMessage(qint64 id, const QString &platform, const QString &platformColor,
				     const QString &username, const QString &text)
{
	OBSDataAutoRelease obj = obs_data_create();
	obs_data_set_int(obj, "id", id);
	obs_data_set_string(obj, "platform", platform.toUtf8().constData());
	obs_data_set_string(obj, "color", platformColor.toUtf8().constData());
	obs_data_set_string(obj, "username", username.toUtf8().constData());
	obs_data_set_string(obj, "text", text.toUtf8().constData());

	QString frame = QString("data: %1\n\n").arg(QString::fromUtf8(obs_data_get_json(obj)));

	backlog_.append({id, frame});
	while (backlog_.size() > kMaxBacklog)
		backlog_.removeFirst();

	QByteArray bytes = frame.toUtf8();
	for (QTcpSocket *client : std::as_const(sseClients_))
		client->write(bytes);
}

void ChatOverlayServer::DeleteMessage(qint64 id)
{
	// Strip it out of the backlog too -- otherwise a Browser Source
	// reloaded after the delete would just replay the deleted message
	// right back in from the backlog.
	for (int i = backlog_.size() - 1; i >= 0; --i) {
		if (backlog_[i].id == id)
			backlog_.removeAt(i);
	}

	OBSDataAutoRelease obj = obs_data_create();
	obs_data_set_int(obj, "id", id);
	obs_data_set_bool(obj, "deleted", true);

	QByteArray bytes = QString("data: %1\n\n").arg(QString::fromUtf8(obs_data_get_json(obj))).toUtf8();
	for (QTcpSocket *client : std::as_const(sseClients_))
		client->write(bytes);
}
