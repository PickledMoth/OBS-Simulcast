#include "chat-view-dock.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextBrowser>
#include <QLabel>
#include <QTextCursor>
#include <QScrollBar>
#include <QPushButton>
#include <QLineEdit>
#include <QMessageBox>
#include <QApplication>
#include <QClipboard>
#include <QMenu>
#include <QTextBlock>
#include <QUrl>

#include <obs.h>
#include <obs.hpp>
#include <obs-frontend-api.h>

#include "multistream-manager.h"
#include "platform-icons.h"

namespace {
constexpr int kMaxMessages = 500;
constexpr int kTrimTo = 400;
constexpr quint16 kOverlayPort = 19813;
} // namespace

ChatViewDock::ChatViewDock(QWidget *parent) : QWidget(parent)
{
	BuildUi();

	twitchClient_ = new TwitchChatClient(this);
	connect(twitchClient_, &TwitchChatClient::MessageReceived, this, &ChatViewDock::OnTwitchMessage);
	connect(twitchClient_, &TwitchChatClient::StatusChanged, this, &ChatViewDock::OnTwitchStatus);
	connect(twitchClient_, &TwitchChatClient::ViewerCountChanged, this, &ChatViewDock::OnTwitchViewerCount);

	youtubeClient_ = new YouTubeChatClient(this);
	connect(youtubeClient_, &YouTubeChatClient::MessageReceived, this, &ChatViewDock::OnYouTubeMessage);
	connect(youtubeClient_, &YouTubeChatClient::StatusChanged, this, &ChatViewDock::OnYouTubeStatus);
	connect(youtubeClient_, &YouTubeChatClient::ViewerCountChanged, this, &ChatViewDock::OnYouTubeViewerCount);

	overlayServer_ = new ChatOverlayServer(this);
	overlayServer_->Start(kOverlayPort);

	pollTimer_ = new QTimer(this);
	connect(pollTimer_, &QTimer::timeout, this, &ChatViewDock::PollConfiguredChannels);
	pollTimer_->start(5000);
	PollConfiguredChannels();
}

ChatViewDock::~ChatViewDock()
{
	// floatingOverlay_ is deliberately parentless (a real top-level window,
	// not a child dialog) so Qt's parent-child ownership won't clean it up
	// on its own.
	delete floatingOverlay_;
}

void ChatViewDock::BuildUi()
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(6, 6, 6, 6);
	layout->setSpacing(4);

	auto *filterRow = new QHBoxLayout();
	filterEdit_ = new QLineEdit(this);
	filterEdit_->setPlaceholderText("Filter (username or text)…");
	highlightEdit_ = new QLineEdit(this);
	highlightEdit_->setPlaceholderText("Highlight keyword (e.g. your name)…");
	connect(filterEdit_, &QLineEdit::textChanged, this, &ChatViewDock::OnFilterChanged);
	connect(highlightEdit_, &QLineEdit::textChanged, this, &ChatViewDock::OnHighlightChanged);
	filterRow->addWidget(filterEdit_, /*stretch=*/1);
	filterRow->addWidget(highlightEdit_, /*stretch=*/1);
	layout->addLayout(filterRow);

	view_ = new RemoteImageTextBrowser(this);
	view_->setOpenExternalLinks(false);
	view_->document()->setMaximumBlockCount(kMaxMessages);
	// QTextEdit/QTextBrowser (a QAbstractScrollArea) intercepts context-menu
	// events itself -- via QAbstractScrollArea::viewportEvent(), which calls
	// this->contextMenuEvent() directly and never even consults the
	// viewport widget's own context-menu policy. So the policy has to live
	// on view_ itself, not view_->viewport(), or this handler never fires
	// and you just get Qt's plain default menu.
	view_->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(view_, &QWidget::customContextMenuRequested, this, &ChatViewDock::OnChatContextMenu);
	layout->addWidget(view_);

	twitchStatusLabel_ = new QLabel(this);
	twitchStatusLabel_->setTextFormat(Qt::RichText);
	twitchStatusLabel_->setStyleSheet("color: #9aa0a6; font-size: 11px;");
	// Lets the platform icon act as a link (opened via the OS default
	// browser) without turning the whole label into an editable/selectable
	// text field.
	twitchStatusLabel_->setTextInteractionFlags(Qt::TextBrowserInteraction);
	twitchStatusLabel_->setOpenExternalLinks(true);
	layout->addWidget(twitchStatusLabel_);

	youtubeStatusLabel_ = new QLabel(this);
	youtubeStatusLabel_->setTextFormat(Qt::RichText);
	youtubeStatusLabel_->setStyleSheet("color: #9aa0a6; font-size: 11px;");
	youtubeStatusLabel_->setTextInteractionFlags(Qt::TextBrowserInteraction);
	youtubeStatusLabel_->setOpenExternalLinks(true);
	layout->addWidget(youtubeStatusLabel_);

	auto *overlayRow = new QHBoxLayout();
	overlayLinkBtn_ = new QPushButton("Overlay Link", this);
	overlayLinkBtn_->setStyleSheet("QPushButton { padding: 2px 8px; font-size: 11px; }");
	overlayLinkBtn_->setToolTip("Add this same merged chat feed as a Browser Source in the current scene");
	connect(overlayLinkBtn_, &QPushButton::clicked, this, &ChatViewDock::OnOverlayLinkClicked);
	overlayRow->addWidget(overlayLinkBtn_);

	floatingOverlayBtn_ = new QPushButton("Desktop Overlay", this);
	floatingOverlayBtn_->setStyleSheet("QPushButton { padding: 2px 8px; font-size: 11px; }");
	floatingOverlayBtn_->setToolTip("Show a small always-on-top chat window on your desktop -- useful if you "
					 "only have one monitor and can't fit this dock next to a fullscreen game.");
	connect(floatingOverlayBtn_, &QPushButton::clicked, this, &ChatViewDock::OnFloatingOverlayClicked);
	overlayRow->addWidget(floatingOverlayBtn_);
	layout->addLayout(overlayRow);

	RefreshTwitchLabel();
	RefreshYouTubeLabel();
}

void ChatViewDock::OnOverlayLinkClicked()
{
	QString url = overlayServer_->Url();
	if (url.isEmpty()) {
		QMessageBox::warning(this, "Chat Overlay",
				      QString("Couldn't start the local overlay server on port %1 (already in "
					      "use?). Restart OBS and try again.")
					      .arg(kOverlayPort));
		return;
	}

	OBSSourceAutoRelease sceneSource = obs_frontend_get_current_scene();
	if (!sceneSource) {
		QMessageBox::warning(this, "Chat Overlay", "No scene is currently selected -- pick a scene first.");
		return;
	}
	obs_scene_t *scene = obs_scene_from_source(sceneSource);

	static const char *kSourceName = "Multistream Chat Overlay";

	// Already sitting in this exact scene -- nothing to add, just confirm.
	if (obs_scene_find_source(scene, kSourceName)) {
		QMessageBox::information(this, "Chat Overlay",
					  QString("\"%1\" is already in this scene.").arg(kSourceName));
		return;
	}

	OBSSourceAutoRelease existing = obs_get_source_by_name(kSourceName);
	if (existing) {
		// A browser source with this name already exists (added to a
		// different scene earlier) -- reuse it as a new scene item rather
		// than creating a second independent source pointed at the same
		// URL, so any crop/filter/size tweaks stay meaningful in one place.
		if (!obs_scene_add(scene, existing)) {
			QMessageBox::warning(this, "Chat Overlay", "Couldn't add the overlay source to this scene.");
			return;
		}
		QMessageBox::information(this, "Chat Overlay",
					  QString("Added the existing \"%1\" source to this scene.").arg(kSourceName));
		return;
	}

	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, "url", url.toUtf8().constData());
	obs_data_set_int(settings, "width", 800);
	obs_data_set_int(settings, "height", 600);
	obs_data_set_bool(settings, "shutdown", true); // stop rendering while not visible in the output

	OBSSourceAutoRelease source = obs_source_create("browser_source", kSourceName, settings, nullptr);
	if (!source) {
		// obs-browser isn't bundled with every OBS build/distro -- fall
		// back to the old copy-the-URL path rather than just failing.
		QApplication::clipboard()->setText(url);
		QMessageBox::warning(this, "Chat Overlay",
				      "Couldn't create a Browser Source (the obs-browser plugin doesn't seem to be "
				      "available). Copied the URL to the clipboard instead -- add it manually via "
				      "Sources -> + -> Browser Source.");
		return;
	}

	if (!obs_scene_add(scene, source)) {
		QMessageBox::warning(this, "Chat Overlay", "Couldn't add the overlay source to this scene.");
		return;
	}

	QMessageBox::information(this, "Chat Overlay",
				  QString("Added \"%1\" to the current scene -- same merged Twitch+YouTube feed as "
					  "this dock, with a transparent background. Resize/reposition it like any "
					  "other source.")
					  .arg(kSourceName));
}

void ChatViewDock::OnFloatingOverlayClicked()
{
	if (!floatingOverlay_) {
		floatingOverlay_ = new FloatingChatOverlay(); // no parent -- a real top-level window, not a child dialog
		connect(floatingOverlay_, &FloatingChatOverlay::Closed, this,
			[this]() { floatingOverlayBtn_->setText("Desktop Overlay"); });
	}

	if (floatingOverlay_->isVisible()) {
		floatingOverlay_->hide();
		floatingOverlayBtn_->setText("Desktop Overlay");
	} else {
		floatingOverlay_->show();
		floatingOverlayBtn_->setText("Hide Desktop Overlay");
	}
}

void ChatViewDock::PollConfiguredChannels()
{
	auto configs = MultistreamManager::Get().GetConfigs();

	QString twitchChannel;
	QString youtubeVideo;
	for (auto &c : configs) {
		if (c.platform == PlatformId::Twitch && !c.channelName.empty())
			twitchChannel = QString::fromStdString(c.channelName);
		else if (c.platform == PlatformId::YouTube && !c.channelName.empty())
			youtubeVideo = QString::fromStdString(c.channelName);
	}

	// Both read the same channel/video value set via the main dock's 💬
	// chat-link button for that row -- Configure()/JoinChannel() are each
	// no-ops if the value hasn't actually changed since the last poll.
	twitchClient_->JoinChannel(twitchChannel);
	youtubeClient_->Configure(youtubeVideo);

	// Also refreshes the popout-chat links on this same 5s cadence -- the
	// Twitch channel and (especially) the YouTube video ID can change
	// between StatusChanged emissions, and these are cheap to rebuild.
	RefreshTwitchLabel();
	RefreshYouTubeLabel();
}

void ChatViewDock::OnTwitchMessage(const QString &username, const QString &text)
{
	AddMessage("Twitch", "#a970ff", username, text);
}

void ChatViewDock::OnTwitchStatus(const QString &status)
{
	twitchStatusText_ = status;
	RefreshTwitchLabel();
}

void ChatViewDock::OnTwitchViewerCount(int count)
{
	twitchViewerCount_ = count;
	RefreshTwitchLabel();
}

void ChatViewDock::RefreshTwitchLabel()
{
	static const QString icon = IconToImgTag(MakeTwitchIcon());

	QString iconPart = icon;
	// twitchClient_ isn't constructed yet the first time this runs --
	// BuildUi() (which calls this once at the end) runs before the
	// constructor creates twitchClient_/youtubeClient_.
	QString channel = twitchClient_ ? twitchClient_->CurrentChannel() : QString();
	if (!channel.isEmpty()) {
		QString url = QString("https://www.twitch.tv/popout/%1/chat")
				      .arg(QString::fromUtf8(QUrl::toPercentEncoding(channel)));
		iconPart = QString("<a href=\"%1\" title=\"Open Twitch popout chat in your browser\">%2</a>")
				   .arg(url, icon);
	}

	QString text = QString("%1 %2").arg(iconPart, twitchStatusText_);
	if (twitchViewerCount_ >= 0)
		text += QString(" · %1 viewers").arg(twitchViewerCount_);
	twitchStatusLabel_->setText(text);
}

void ChatViewDock::OnYouTubeMessage(const QString &username, const QString &text)
{
	AddMessage("YouTube", "#ff0000", username, text);
}

void ChatViewDock::OnYouTubeStatus(const QString &status)
{
	youtubeStatusText_ = status;
	RefreshYouTubeLabel();
}

void ChatViewDock::OnYouTubeViewerCount(int count)
{
	youtubeViewerCount_ = count;
	RefreshYouTubeLabel();
}

void ChatViewDock::RefreshYouTubeLabel()
{
	static const QString icon = IconToImgTag(MakeYouTubeIcon());

	QString iconPart = icon;
	QString videoId = youtubeClient_ ? youtubeClient_->CurrentVideoId() : QString();
	if (!videoId.isEmpty()) {
		QString url = QString("https://www.youtube.com/live_chat?is_popout=1&v=%1")
				      .arg(QString::fromUtf8(QUrl::toPercentEncoding(videoId)));
		iconPart = QString("<a href=\"%1\" title=\"Open YouTube popout chat in your browser\">%2</a>")
				   .arg(url, icon);
	}

	QString text = QString("%1 %2").arg(iconPart, youtubeStatusText_);
	if (youtubeViewerCount_ >= 0)
		text += QString(" · %1 viewers").arg(youtubeViewerCount_);
	youtubeStatusLabel_->setText(text);
}

void ChatViewDock::OnFilterChanged()
{
	RerenderAll();
}

void ChatViewDock::OnHighlightChanged()
{
	RerenderAll();
}

void ChatViewDock::AddMessage(const QString &platform, const QString &platformColor, const QString &username,
			       const QString &text)
{
	qint64 id = nextMsgId_++;
	messages_.push_back({id, platform, platformColor, username, text});
	if ((int)messages_.size() > kMaxMessages)
		messages_.erase(messages_.begin(), messages_.begin() + (messages_.size() - kTrimTo));

	// The on-stream overlay and desktop overlay are separate audiences from
	// this dock's local view -- your own filter box shouldn't hide messages
	// from them.
	overlayServer_->PushMessage(id, platform, platformColor, username, text);
	if (floatingOverlay_)
		floatingOverlay_->AppendMessage(BuildMessageHtml(platform, platformColor, username, text));

	QString filter = filterEdit_->text().trimmed();
	if (!filter.isEmpty() && !username.contains(filter, Qt::CaseInsensitive) &&
	    !text.contains(filter, Qt::CaseInsensitive))
		return; // doesn't match the active filter -- stored, just not shown

	bool atBottom = view_->verticalScrollBar()->value() >= view_->verticalScrollBar()->maximum() - 4;
	RenderMessage(platform, platformColor, username, text, id);
	if (atBottom)
		view_->verticalScrollBar()->setValue(view_->verticalScrollBar()->maximum());
}

QString ChatViewDock::BuildMessageHtml(const QString &platform, const QString &platformColor,
					const QString &username, const QString &text)
{
	// Computed once and reused rather than re-encoding a PNG on every
	// message -- the icon set is fixed.
	static const QString twitchIconTag = IconToImgTag(MakeTwitchIcon());
	static const QString youtubeIconTag = IconToImgTag(MakeYouTubeIcon());
	const QString &iconTag = platform == "Twitch" ? twitchIconTag : youtubeIconTag;

	// "text" arrives pre-escaped as safe HTML from the platform clients
	// (plain content escaped, emotes/emoji rendered as trusted <img> tags)
	// -- must NOT be re-escaped here, or the emote images would show up
	// as literal "&lt;img..." text instead of rendering.
	return QString("%1 <b style=\"color:%2;\">%3</b>: %4")
		.arg(iconTag, platformColor, username.toHtmlEscaped(), text);
}

void ChatViewDock::RenderMessage(const QString &platform, const QString &platformColor, const QString &username,
				  const QString &text, qint64 id)
{
	QString highlight = highlightEdit_->text().trimmed();
	bool isMention = !highlight.isEmpty() &&
			  (username.contains(highlight, Qt::CaseInsensitive) || text.contains(highlight, Qt::CaseInsensitive));

	QString line = BuildMessageHtml(platform, platformColor, username, text);

	if (isMention)
		line = QString("<div style=\"background-color:#4a3d0a; padding:2px 4px; border-radius:2px;\">%1</div>")
			       .arg(line);

	view_->append(line);
	displayedIds_.push_back(id);
}

void ChatViewDock::RerenderAll()
{
	view_->clear();
	displayedIds_.clear();
	QString filter = filterEdit_->text().trimmed();

	for (auto &m : messages_) {
		if (!filter.isEmpty() && !m.username.contains(filter, Qt::CaseInsensitive) &&
		    !m.text.contains(filter, Qt::CaseInsensitive))
			continue;
		RenderMessage(m.platform, m.color, m.username, m.text, m.id);
	}

	view_->verticalScrollBar()->setValue(view_->verticalScrollBar()->maximum());
}

void ChatViewDock::DeleteMessage(qint64 id)
{
	for (auto it = messages_.begin(); it != messages_.end(); ++it) {
		if (it->id == id) {
			messages_.erase(it);
			break;
		}
	}
	RerenderAll();
	overlayServer_->DeleteMessage(id);
}

void ChatViewDock::OnChatContextMenu(const QPoint &pos)
{
	// pos arrives relative to view_ itself (see BuildUi's comment on why the
	// policy/signal live there) -- cursorForPosition()/createStandardContextMenu()
	// both expect viewport-relative coordinates instead.
	QPoint viewportPos = view_->viewport()->mapFrom(view_, pos);
	int blockNumber = view_->cursorForPosition(viewportPos).blockNumber();
	bool hasMessage = blockNumber >= 0 && blockNumber < (int)displayedIds_.size();

	QMenu *menu = view_->createStandardContextMenu(viewportPos);
	if (hasMessage) {
		menu->addSeparator();
		QAction *deleteAction = menu->addAction("Delete Message");
		qint64 id = displayedIds_[blockNumber];
		connect(deleteAction, &QAction::triggered, this, [this, id]() { DeleteMessage(id); });
	}
	menu->exec(view_->mapToGlobal(pos));
	menu->deleteLater();
}
