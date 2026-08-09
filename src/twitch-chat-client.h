#pragma once

#include <QObject>
#include <QSslSocket>
#include <QTimer>
#include <QByteArray>
#include <QDateTime>
#include <QStringList>

class QNetworkAccessManager;
class QNetworkReply;

// Anonymous, read-only Twitch chat client using IRC-over-TLS
// (irc.chat.twitch.tv:6697) -- Twitch allows anonymous read access to any
// channel's chat with a throwaway "justinfanNNNNN" nick, no OAuth/API key
// needed. This is the only platform in the combined chat view that doesn't
// require the user to create developer credentials first.
//
// Viewer count is a separate, lower-frequency concern: Twitch's official
// Helix API requires a registered app + OAuth even for public data like
// this, so it's fetched the same way IRC chat is read -- anonymously,
// against Twitch's own web client's GraphQL endpoint (gql.twitch.tv), using
// the public Client-Id its website itself uses (not a developer credential;
// the same value Streamlink/Chatterino use for the same reason).
class TwitchChatClient : public QObject {
	Q_OBJECT

public:
	explicit TwitchChatClient(QObject *parent = nullptr);
	~TwitchChatClient() override;

	// Reconnects to a different channel if it differs from the current one;
	// no-op if already connected to this channel. Empty channel disconnects.
	void JoinChannel(const QString &channel);

	QString CurrentChannel() const { return channel_; }
	bool IsConnected() const;

	// Diagnostic state, surfaced by ChatDiagnosticsDialog -- lets "why isn't
	// chat showing up" be answered from a running instance instead of
	// re-deriving it from raw logs each time.
	QStringList AckedCapabilities() const { return ackedCapabilities_; }
	QDateTime LastMessageAt() const { return lastMessageAt_; }
	int MessagesReceivedCount() const { return messagesReceivedCount_; }
	QString LastStatus() const { return lastStatus_; }

signals:
	// "text" is a pre-escaped, safe-to-render HTML fragment, not plain
	// text -- callers must NOT re-escape or re-sanitize it. Plain message
	// content is HTML-escaped internally before this is emitted; Twitch
	// emotes (identified via the "emotes" IRCv3 tag) are rendered inline
	// as <img> tags pointing at Twitch's CDN. Only "username" is plain
	// text.
	void MessageReceived(const QString &username, const QString &text);
	void StatusChanged(const QString &status);
	// -1 means "unknown/offline", not "zero viewers".
	void ViewerCountChanged(int count);

private slots:
	void OnEncrypted();
	void OnReadyRead();
	void OnErrorOccurred();
	void OnDisconnected();
	void OnReconnectTimer();
	void PollViewerCount();
	void OnViewerCountFinished();

private:
	void Disconnect();
	void ConnectToTwitch();
	void ProcessLine(const QString &line);

	QSslSocket *socket_ = nullptr;
	QByteArray buffer_;
	QString channel_;
	QTimer *reconnectTimer_ = nullptr;

	QNetworkAccessManager *nam_ = nullptr;
	QTimer *viewerCountTimer_ = nullptr;

	QStringList ackedCapabilities_;
	QDateTime lastMessageAt_;
	int messagesReceivedCount_ = 0;
	QString lastStatus_ = "Not connected";
};
