#pragma once

#include <QWidget>
#include <QPoint>
#include <QColor>

class RemoteImageTextBrowser;
class QSizeGrip;
class QLabel;
class QGraphicsOpacityEffect;
class QTimer;

// A frameless, always-on-top desktop window mirroring the merged chat feed --
// for single-monitor streamers who can't fit the dock next to a fullscreen
// game and don't want to alt-tab out just to glance at chat. Distinct from
// ChatOverlayServer's Browser Source overlay: that one is composited into
// the *stream output*; this one is a real top-level window rendered by
// Windows on top of everything (including exclusive/borderless fullscreen
// apps), visible only to the streamer, never to viewers.
class FloatingChatOverlay : public QWidget {
	Q_OBJECT

public:
	explicit FloatingChatOverlay(QWidget *parent = nullptr);

	// Appends one pre-rendered message line (same HTML markup used by the
	// dock/on-stream overlay) and auto-scrolls if already at the bottom.
	void AppendMessage(const QString &html);

signals:
	// Emitted when closed via the window's own × button, so the dock's
	// toggle button can be kept in sync without polling isVisible().
	void Closed();

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void paintEvent(QPaintEvent *event) override;
	void showEvent(QShowEvent *event) override;

private:
	void ShowOpacityMenu();
	void SetOpacityPercent(int percent);

	RemoteImageTextBrowser *view_ = nullptr;
	QSizeGrip *grip_ = nullptr;
	QLabel *titleLabel_ = nullptr;
	QGraphicsOpacityEffect *titleOpacityEffect_ = nullptr;
	QTimer *titleFadeTimer_ = nullptr;
	QPoint dragOffset_;
	bool dragging_ = false;

	// Alpha (0-255) of the single background panel painted in paintEvent()
	// -- this is what the opacity slider actually controls. Deliberately
	// NOT done via QWidget::setWindowOpacity(), which dims the whole
	// composited window uniformly and made chat text look washed-out grey
	// at lower settings; painting just the background with variable alpha
	// keeps all text/icons fully vibrant regardless of this setting.
	int backgroundAlpha_ = 230;
};
