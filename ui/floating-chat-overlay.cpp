#include "floating-chat-overlay.h"
#include "remote-image-text-browser.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSizeGrip>
#include <QScrollBar>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTextDocument>
#include <QTimer>
#include <QSettings>
#include <QMenu>
#include <QWidgetAction>
#include <QSlider>
#include <QPainter>
#include <QPainterPath>
#include <QIcon>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <cmath>

namespace {
constexpr int kMaxMessages = 100;
constexpr int kCornerRadius = 8;
constexpr int kTitleFadeDelayMs = 4000;
constexpr int kTitleFadeDurationMs = 1200;
constexpr double kTitleFadedOpacity = 0.12;

// Font-rendered symbol glyphs (⚙/✕) are unreliable in this Qt build's font
// fallback -- they've come out as blank boxes elsewhere in this plugin (see
// the same note in multistream-dock.cpp), which is exactly why these two
// buttons were invisible. Drawing the icons ourselves sidesteps font/emoji
// availability entirely.
QIcon MakeGearIcon(QColor color)
{
	QPixmap pix(16, 16);
	pix.fill(Qt::transparent);
	QPainter p(&pix);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(QPen(color, 2));
	p.setBrush(Qt::NoBrush);
	p.drawEllipse(QPoint(8, 8), 4, 4);
	p.setBrush(color);
	p.setPen(Qt::NoPen);
	for (int i = 0; i < 6; i++) {
		double a = i * 3.14159 / 3.0;
		int x = 8 + (int)(7 * std::cos(a));
		int y = 8 + (int)(7 * std::sin(a));
		p.drawEllipse(QPoint(x, y), 1, 1);
	}
	return QIcon(pix);
}

QIcon MakeCloseIcon(QColor color)
{
	QPixmap pix(16, 16);
	pix.fill(Qt::transparent);
	QPainter p(&pix);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(QPen(color, 2, Qt::SolidLine, Qt::RoundCap));
	p.drawLine(4, 4, 12, 12);
	p.drawLine(12, 4, 4, 12);
	return QIcon(pix);
}
} // namespace

FloatingChatOverlay::FloatingChatOverlay(QWidget *parent) : QWidget(parent)
{
	// Qt::Tool keeps it off the taskbar/alt-tab (it's a HUD, not a real
	// window); WindowStaysOnTopHint is what makes it survive over a
	// fullscreen/borderless game; WA_TranslucentBackground is required for
	// per-pixel transparency on a frameless window. Nothing here paints its
	// own background via stylesheets -- paintEvent() below draws ONE
	// rounded panel behind everything (title bar and chat area alike), so
	// there's no seam between differently-colored child widgets and no
	// separate corner-rounding to keep in sync across them.
	setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
	setAttribute(Qt::WA_TranslucentBackground);
	setAttribute(Qt::WA_ShowWithoutActivating);
	resize(380, 420);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	auto *titleBar = new QWidget(this);
	titleBar->setAttribute(Qt::WA_TranslucentBackground);
	auto *titleLayout = new QHBoxLayout(titleBar);
	titleLayout->setContentsMargins(8, 2, 4, 2);

	titleLabel_ = new QLabel("Chat Overlay — drag to move", titleBar);
	titleLabel_->setStyleSheet("color: rgba(255,255,255,190); font-size: 10px; background: transparent;");
	titleOpacityEffect_ = new QGraphicsOpacityEffect(titleLabel_);
	titleOpacityEffect_->setOpacity(1.0);
	titleLabel_->setGraphicsEffect(titleOpacityEffect_);

	auto *gearBtn = new QPushButton(titleBar);
	gearBtn->setIcon(MakeGearIcon(QColor(255, 255, 255)));
	gearBtn->setFixedSize(18, 18);
	gearBtn->setIconSize(QSize(14, 14));
	gearBtn->setToolTip("Background opacity");
	gearBtn->setStyleSheet("QPushButton { background: transparent; border: none; } "
				"QPushButton:hover { background: rgba(255,255,255,30); border-radius: 3px; }");
	connect(gearBtn, &QPushButton::clicked, this, &FloatingChatOverlay::ShowOpacityMenu);

	auto *closeBtn = new QPushButton(titleBar);
	closeBtn->setIcon(MakeCloseIcon(QColor(255, 255, 255)));
	closeBtn->setFixedSize(18, 18);
	closeBtn->setIconSize(QSize(14, 14));
	closeBtn->setStyleSheet("QPushButton { background: transparent; border: none; } "
				 "QPushButton:hover { background: rgba(255,80,80,60); border-radius: 3px; }");
	connect(closeBtn, &QPushButton::clicked, this, [this]() {
		hide();
		emit Closed();
	});

	titleLayout->addWidget(titleLabel_, /*stretch=*/1);
	titleLayout->addWidget(gearBtn);
	titleLayout->addWidget(closeBtn);
	layout->addWidget(titleBar);

	view_ = new RemoteImageTextBrowser(this);
	view_->setFrameShape(QFrame::NoFrame);
	view_->viewport()->setAttribute(Qt::WA_TranslucentBackground);
	// This is a passive HUD, not an interactive text field -- disabling
	// interaction/focus means clicks land on the drag logic below instead
	// of starting a text selection.
	view_->setTextInteractionFlags(Qt::NoTextInteraction);
	view_->setFocusPolicy(Qt::NoFocus);
	view_->document()->setMaximumBlockCount(kMaxMessages);
	// No background here -- paintEvent() supplies the one shared panel
	// behind this. Text color is left fully opaque on purpose: the
	// background opacity slider must never wash out chat text.
	view_->setStyleSheet("QTextBrowser { background: transparent; color: #fff; border: none; "
			      "font-family: 'Segoe UI'; font-size: 14px; padding: 6px; }");
	layout->addWidget(view_, /*stretch=*/1);

	// A plain child of `this` positioned manually (see resizeEvent()) rather
	// than laid out in its own row, so there's no extra unpainted strip
	// below the chat view for it to sit in.
	grip_ = new QSizeGrip(this);
	grip_->resize(grip_->sizeHint());

	titleBar->installEventFilter(this);
	view_->viewport()->installEventFilter(this);

	titleFadeTimer_ = new QTimer(this);
	titleFadeTimer_->setSingleShot(true);
	connect(titleFadeTimer_, &QTimer::timeout, this, [this]() {
		auto *anim = new QPropertyAnimation(titleOpacityEffect_, "opacity", this);
		anim->setDuration(kTitleFadeDurationMs);
		anim->setStartValue(titleOpacityEffect_->opacity());
		anim->setEndValue(kTitleFadedOpacity);
		anim->start(QAbstractAnimation::DeleteWhenStopped);
	});

	QSettings settings("OBS-Simulcast", "FloatingChatOverlay");
	SetOpacityPercent(settings.value("floatingOverlayOpacityPercent", 90).toInt());
}

void FloatingChatOverlay::showEvent(QShowEvent *event)
{
	QWidget::showEvent(event);
	// Reset the "drag to move" hint to fully visible each time the overlay
	// is (re)shown, then let it fade again after a few seconds -- only this
	// label fades; chat messages never do.
	titleOpacityEffect_->setOpacity(1.0);
	titleFadeTimer_->start(kTitleFadeDelayMs);
}

void FloatingChatOverlay::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	if (grip_)
		grip_->move(width() - grip_->width(), height() - grip_->height());

	// Clips the whole composited window (title bar included) to one rounded
	// rectangle -- everything outside it simply isn't painted or
	// hit-testable, regardless of what any child widget draws.
	QPainterPath path;
	path.addRoundedRect(rect(), kCornerRadius, kCornerRadius);
	setMask(QRegion(path.toFillPolygon().toPolygon()));
}

void FloatingChatOverlay::paintEvent(QPaintEvent *event)
{
	Q_UNUSED(event);
	// The single background panel for the whole window -- title bar and
	// chat area are painted as one continuous shape here rather than each
	// child widget drawing its own (mismatched) background, which is what
	// left a visible seam between them before.
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(15, 15, 18, backgroundAlpha_));
	p.drawRoundedRect(rect(), kCornerRadius, kCornerRadius);
}

void FloatingChatOverlay::AppendMessage(const QString &html)
{
	bool atBottom = view_->verticalScrollBar()->value() >= view_->verticalScrollBar()->maximum() - 4;
	view_->append(html);
	if (atBottom)
		view_->verticalScrollBar()->setValue(view_->verticalScrollBar()->maximum());
}

void FloatingChatOverlay::ShowOpacityMenu()
{
	QMenu menu(this);
	menu.setStyleSheet("QMenu { background-color: rgb(30,30,34); border: 1px solid rgb(60,60,66); }");

	auto *container = new QWidget(&menu);
	auto *vbox = new QVBoxLayout(container);
	vbox->setContentsMargins(10, 6, 10, 6);
	vbox->setSpacing(4);

	int currentPercent = backgroundAlpha_ * 100 / 255;
	auto *label = new QLabel(QString("Background opacity: %1%").arg(currentPercent), container);
	label->setStyleSheet("color: #ddd; font-size: 11px;");

	auto *slider = new QSlider(Qt::Horizontal, container);
	slider->setRange(20, 100); // floored at 20% so the panel never fades past "unfindable"
	slider->setValue(currentPercent);
	slider->setFixedWidth(160);
	connect(slider, &QSlider::valueChanged, this, [this, label](int v) {
		SetOpacityPercent(v);
		label->setText(QString("Background opacity: %1%").arg(v));
	});

	vbox->addWidget(label);
	vbox->addWidget(slider);

	auto *sliderAction = new QWidgetAction(&menu);
	sliderAction->setDefaultWidget(container);
	menu.addAction(sliderAction);

	menu.exec(mapToGlobal(QPoint(qMax(0, width() - 190), 24)));
}

void FloatingChatOverlay::SetOpacityPercent(int percent)
{
	percent = qBound(20, percent, 100);
	backgroundAlpha_ = percent * 255 / 100;
	update();

	QSettings settings("OBS-Simulcast", "FloatingChatOverlay");
	settings.setValue("floatingOverlayOpacityPercent", percent);
}

bool FloatingChatOverlay::eventFilter(QObject *watched, QEvent *event)
{
	Q_UNUSED(watched); // only titleBar/view_->viewport() are ever filtered -- see the installEventFilter calls above

	if (event->type() == QEvent::MouseButtonPress) {
		auto *me = static_cast<QMouseEvent *>(event);
		if (me->button() == Qt::LeftButton) {
			dragging_ = true;
			dragOffset_ = me->globalPosition().toPoint() - frameGeometry().topLeft();
			return true;
		}
	} else if (event->type() == QEvent::MouseMove) {
		auto *me = static_cast<QMouseEvent *>(event);
		if (dragging_ && (me->buttons() & Qt::LeftButton)) {
			move(me->globalPosition().toPoint() - dragOffset_);
			return true;
		}
	} else if (event->type() == QEvent::MouseButtonRelease) {
		dragging_ = false;
	}

	return QWidget::eventFilter(watched, event);
}
