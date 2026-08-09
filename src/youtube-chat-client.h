#pragma once

#include <obs-frontend-api.h>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QDateTime>

class QNetworkAccessManager;
class QNetworkReply;

// Reads YouTube live chat via the same private "InnerTube" endpoint
// youtube.com's own web player uses to render chat -- not the official
// YouTube Data API v3. This mirrors how TwitchChatClient talks to Twitch's
// anonymous IRC directly rather than through a quota-limited official API:
// no user-supplied API key, no Google Cloud project setup, and no daily
// quota to burn through or rate-limit against (see the 429s the old
// search.list-based implementation used to hit).
//
// Tradeoff: InnerTube is YouTube's internal/undocumented API. It's what
// pytchat, YTLiveChat, and similar long-running open-source projects use in
// production, but YouTube could change its shape without notice, unlike the
// official Data API's stable support contract.
//
// Configured with a *channel* (handle like "@name", a full channel URL, or
// a raw "UC..." channel ID): fetches "<channel>/live" to find whatever
// video is currently live, scrapes that video's live_chat page for its
// initial continuation token, then polls get_live_chat using the
// server-supplied continuation + timeoutMs it hands back each time (the
// same mechanism the InnerTube-based libraries above use).
class YouTubeChatClient : public QObject {
	Q_OBJECT

public:
	explicit YouTubeChatClient(QObject *parent = nullptr);
	~YouTubeChatClient() override;

	// Re-resolves/reconnects only if the channel actually changed from
	// what's currently active. Empty channel stops polling.
	void Configure(const QString &channel);

	// The currently-live video, re-discovered each broadcast. Empty if
	// nothing's live yet.
	QString CurrentVideoId() const { return videoId_; }

	// Diagnostic state, surfaced by ChatDiagnosticsDialog.
	QDateTime LastMessageAt() const { return lastMessageAt_; }
	int MessagesReceivedCount() const { return messagesReceivedCount_; }
	QString LastStatus() const { return lastStatus_; }

signals:
	// "text" is a pre-escaped, safe-to-render HTML fragment, not plain
	// text -- callers must NOT re-escape or re-sanitize it. Plain message
	// content is HTML-escaped internally before this is emitted; custom
	// channel emoji are rendered inline as <img> tags pointing at
	// YouTube's own CDN, and standard Unicode emoji are inserted as-is
	// (no HTML metacharacters, safe by construction). Only "username" is
	// plain text.
	void MessageReceived(const QString &username, const QString &text);
	void StatusChanged(const QString &status);
	// -1 means "unknown/not live", not "zero viewers".
	void ViewerCountChanged(int count);

private slots:
	void OnFindLiveVideoFinished();
	void OnFetchContinuationFinished();
	void OnPollFinished();
	void PollTick();
	void PollViewerCount();
	void OnViewerCountFinished();

private:
	void Stop();
	static void OnFrontendEvent(enum obs_frontend_event event, void *data);
	void FindLiveVideo();
	void FetchInitialContinuation();
	void PollMessages();

	QNetworkAccessManager *nam_ = nullptr;
	QTimer *pollTimer_ = nullptr;
	QTimer *viewerCountTimer_ = nullptr;

	QString channelInput_; // whatever the user typed: handle, URL, or raw ID
	QString videoId_;      // currently-live video, re-discovered each broadcast
	QString continuation_; // InnerTube's opaque "keep polling from here" token
	int retryCount_ = 0;

	QDateTime lastMessageAt_;
	int messagesReceivedCount_ = 0;
	QString lastStatus_ = "Not connected";

	// Only search-for-a-live-video and chat polling actually hit the
	// network, and there's no reason to do either outside an active
	// stream. "Active" means either OBS's own Start/Stop Streaming button
	// or this plugin's own Go Live button (MultistreamManager) -- the
	// latter is the actual primary way this plugin is used, and it never
	// touches OBS's native streaming output/state, so relying on OBS's
	// frontend event alone misses it entirely. Checked on a lightweight
	// timer since neither source pushes a "just went live" notification.
	void CheckStreamingActive();
	QTimer *livenessTimer_ = nullptr;
	bool streamingActive_ = false;
};
