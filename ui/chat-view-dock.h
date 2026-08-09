#pragma once

#include <QWidget>
#include <QTimer>
#include <QString>
#include <vector>

#include "twitch-chat-client.h"
#include "youtube-chat-client.h"
#include "chat-overlay-server.h"
#include "remote-image-text-browser.h"
#include "floating-chat-overlay.h"

class QLabel;
class QPushButton;
class QLineEdit;

// Merged, single scrolling chat feed tagged by platform: Twitch (anonymous
// IRC) and YouTube (InnerTube, YouTube's own web client's private API -- no
// API key needed, see SETUP.md). Both platforms read their channel/video
// value from the main dock's 💬 chat-link button for that row -- the same
// field already used for OBS's own Custom Browser Dock chat links -- rather
// than duplicating a second place to configure it here. Both platforms'
// status labels also show a live viewer
// count, fetched anonymously the same way chat itself is read (Twitch via
// its web client's GraphQL endpoint, YouTube via the watch page) -- Kick
// has no equivalent public source for either and stays unsupported. Also
// supports filtering the feed by text and highlighting messages containing
// a configured keyword (e.g. your own name).
//
// The dock itself is a native Qt widget, which OBS can't capture into a
// scene -- ChatOverlayServer mirrors this same merged feed as a small
// localhost-only web page so it can be added as a Browser Source and
// actually appear in the stream output.
class ChatViewDock : public QWidget {
	Q_OBJECT

public:
	explicit ChatViewDock(QWidget *parent = nullptr);
	~ChatViewDock() override;

	// Read-only access for ChatDiagnosticsDialog -- kept as live pointers
	// rather than a copied snapshot struct so "Refresh" in that dialog
	// always reflects current state with no separate sync path to forget.
	TwitchChatClient *TwitchClient() const { return twitchClient_; }
	YouTubeChatClient *YouTubeClient() const { return youtubeClient_; }
	QString OverlayUrl() const { return overlayServer_ ? overlayServer_->Url() : QString(); }
	int TwitchViewerCount() const { return twitchViewerCount_; }
	int YouTubeViewerCount() const { return youtubeViewerCount_; }

private slots:
	void OnTwitchMessage(const QString &username, const QString &text);
	void OnTwitchStatus(const QString &status);
	void OnTwitchViewerCount(int count);
	void OnYouTubeMessage(const QString &username, const QString &text);
	void OnYouTubeStatus(const QString &status);
	void OnYouTubeViewerCount(int count);
	void OnOverlayLinkClicked();
	void OnFloatingOverlayClicked();
	void PollConfiguredChannels();
	void OnFilterChanged();
	void OnHighlightChanged();
	void OnChatContextMenu(const QPoint &pos);

private:
	void BuildUi();
	void AddMessage(const QString &platform, const QString &platformColor, const QString &username,
			 const QString &text);
	void RenderMessage(const QString &platform, const QString &platformColor, const QString &username,
			    const QString &text, qint64 id);
	static QString BuildMessageHtml(const QString &platform, const QString &platformColor,
					 const QString &username, const QString &text);
	void RerenderAll();
	void DeleteMessage(qint64 id);
	void RefreshTwitchLabel();
	void RefreshYouTubeLabel();

	struct ChatMsg {
		qint64 id;
		QString platform;
		QString color;
		QString username;
		QString text;
	};

	RemoteImageTextBrowser *view_ = nullptr;
	QLineEdit *filterEdit_ = nullptr;
	QLineEdit *highlightEdit_ = nullptr;
	QLabel *twitchStatusLabel_ = nullptr;
	QLabel *youtubeStatusLabel_ = nullptr;
	QPushButton *overlayLinkBtn_ = nullptr;
	QPushButton *floatingOverlayBtn_ = nullptr;
	TwitchChatClient *twitchClient_ = nullptr;
	YouTubeChatClient *youtubeClient_ = nullptr;
	ChatOverlayServer *overlayServer_ = nullptr;
	FloatingChatOverlay *floatingOverlay_ = nullptr; // lazily created on first use
	QTimer *pollTimer_ = nullptr;
	std::vector<ChatMsg> messages_;
	qint64 nextMsgId_ = 1;

	// Parallels the message blocks currently rendered in view_, in order --
	// only covers what's actually displayed (post-filter), so a right-click
	// can map a clicked block back to which message to delete.
	std::vector<qint64> displayedIds_;

	// Status text and viewer count arrive as separate signals -- cached so
	// one can be re-rendered into the label without clobbering the other.
	QString twitchStatusText_ = "Not connected";
	int twitchViewerCount_ = -1;
	QString youtubeStatusText_ = "Not connected";
	int youtubeViewerCount_ = -1;
};
