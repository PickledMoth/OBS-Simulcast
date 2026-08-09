#pragma once

#include <QObject>
#include <QString>
#include <QList>

class QTcpServer;
class QTcpSocket;

// Serves the merged Twitch+YouTube chat feed as a small local web page
// (localhost-only, no auth needed since it never leaves the machine) so it
// can be added as an OBS Browser Source and actually show up in the stream
// output -- unlike the native Qt dock, which OBS can't capture into a
// scene. Uses Server-Sent Events (plain HTTP, kept open) rather than
// WebSockets, since QtWebSockets isn't a dependency this plugin otherwise
// needs.
class ChatOverlayServer : public QObject {
	Q_OBJECT

public:
	explicit ChatOverlayServer(QObject *parent = nullptr);
	~ChatOverlayServer() override;

	// Binds to 127.0.0.1:<port>. Returns false (and leaves Url() empty) if
	// the port couldn't be bound -- caller should surface that rather than
	// silently pretending the overlay is available.
	bool Start(quint16 port);

	// Empty if Start() hasn't succeeded.
	QString Url() const;

	// Broadcasts to all currently-connected overlay pages and keeps a
	// small backlog so a Browser Source added/reloaded mid-stream isn't
	// blank until the next message arrives. `id` must match the id later
	// passed to DeleteMessage() to remove this same message.
	void PushMessage(qint64 id, const QString &platform, const QString &platformColor, const QString &username,
			  const QString &text);

	// Removes a previously-pushed message: strips it from the backlog (so
	// clients connecting after the delete never see it) and tells
	// already-open overlay pages to remove it from the DOM immediately.
	void DeleteMessage(qint64 id);

private slots:
	void OnNewConnection();
	void OnClientReadyRead();
	void OnClientDisconnected();

private:
	void HandleRequest(QTcpSocket *socket, const QString &requestLine);
	void ServeIndexPage(QTcpSocket *socket);
	void ServeEventStream(QTcpSocket *socket);

	struct BacklogEntry {
		qint64 id;
		QString frame; // pre-formatted "data: {...}\n\n" SSE frame
	};

	QTcpServer *server_ = nullptr;
	quint16 port_ = 0;
	QList<QTcpSocket *> sseClients_;
	QList<BacklogEntry> backlog_;
};
