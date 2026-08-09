#include "multistream-dock.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QCheckBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QWidget>
#include <QFont>
#include <QColor>
#include <QToolButton>
#include <QInputDialog>
#include <QMessageBox>
#include <QStringList>
#include <QFileDialog>
#include <QFile>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>
#include <QIcon>
#include <QSize>
#include <QDesktopServices>
#include <QUrl>
#include <algorithm>
#include <functional>
#include <cmath>

#include <obs.h>
#include <obs.hpp>

#include "multistream-manager.h"
#include "platform-presets.h"
#include "encoder-settings-dialog.h"
#include "chat-link-dialog.h"
#include "settings-store.h"
#include "password-crypto.h"
#include "chat-diagnostics-dialog.h"

enum Column {
	ColEnabled = 0,
	ColPlatform,
	ColStatus,
	ColBitrate,
	ColServer,
	ColStreamKey,
	ColEncoder,
	ColChat,
	ColRemove,
	ColCount,
};

namespace {
// Status text colors, matched loosely to the semantics streamers already
// read off OBS's own stream indicator (green = good, red = bad, yellow =
// in-between) so the row list scans at a glance without reading every word.
const char *StatusColor(OutputState state)
{
	switch (state) {
	case OutputState::Live:
		return "#2ecc71";
	case OutputState::Connecting:
	case OutputState::Reconnecting:
		return "#f1c40f";
	case OutputState::Error:
		return "#e74c3c";
	case OutputState::Idle:
	case OutputState::Stopped:
	default:
		return "#9aa0a6";
	}
}

// Small square icon buttons for the Server/Stream Key cells, colored to
// signal "configured" vs "missing" at a glance without needing the full
// value visible in the row (stream keys especially shouldn't sit exposed
// in plaintext in a busy dock).
QToolButton *MakeFieldButton(QWidget *parent, const QString &glyph)
{
	auto *btn = new QToolButton(parent);
	btn->setText(glyph);
	btn->setIconSize(QSize(16, 16));
	btn->setFixedSize(28, 28);
	btn->setCursor(Qt::PointingHandCursor);
	return btn;
}

// Font-rendered emoji glyphs turned out unreliable in this Qt build's font
// fallback -- some (🔗/🔑) draw fine as color emoji, others (⚙/💬/✕) came
// out as blank boxes depending on what's installed system-side. Drawing
// the icon ourselves sidesteps font/emoji-font availability entirely.
QIcon MakeVectorIcon(QColor color, const std::function<void(QPainter &, int)> &draw, int size = 16)
{
	QPixmap pix(size, size);
	pix.fill(Qt::transparent);
	QPainter p(&pix);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(QPen(color, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	p.setBrush(color);
	draw(p, size);
	return QIcon(pix);
}

QIcon MakeCheckIcon(QColor color)
{
	return MakeVectorIcon(color, [](QPainter &p, int s) {
		p.setBrush(Qt::NoBrush);
		QPolygon poly;
		poly << QPoint(s * 2 / 10, s * 5 / 10) << QPoint(s * 4 / 10, s * 7 / 10) << QPoint(s * 8 / 10, s * 3 / 10);
		p.drawPolyline(poly);
	});
}

QIcon MakeXIcon(QColor color)
{
	return MakeVectorIcon(color, [](QPainter &p, int s) {
		p.setBrush(Qt::NoBrush);
		int m = s * 3 / 10;
		p.drawLine(m, m, s - m, s - m);
		p.drawLine(s - m, m, m, s - m);
	});
}

QIcon MakePlayIcon(QColor color)
{
	return MakeVectorIcon(color, [](QPainter &p, int s) {
		p.setPen(Qt::NoPen);
		QPolygon tri;
		tri << QPoint(s * 3 / 10, s * 2 / 10) << QPoint(s * 3 / 10, s * 8 / 10) << QPoint(s * 8 / 10, s * 5 / 10);
		p.drawPolygon(tri);
	});
}

QIcon MakeSaveIcon(QColor color)
{
	return MakeVectorIcon(color, [](QPainter &p, int s) {
		p.setBrush(Qt::NoBrush);
		p.drawRoundedRect(s * 2 / 10, s * 2 / 10, s * 6 / 10, s * 6 / 10, 2, 2);
		p.drawRect(s * 35 / 100, s * 2 / 10, s * 3 / 10, s * 2 / 10);
	});
}

QIcon MakeUpArrowIcon(QColor color)
{
	return MakeVectorIcon(color, [](QPainter &p, int s) {
		p.setBrush(Qt::NoBrush);
		p.drawLine(s / 2, s * 8 / 10, s / 2, s * 2 / 10);
		p.drawLine(s / 2, s * 2 / 10, s * 3 / 10, s * 4 / 10);
		p.drawLine(s / 2, s * 2 / 10, s * 7 / 10, s * 4 / 10);
	});
}

QIcon MakeDownArrowIcon(QColor color)
{
	return MakeVectorIcon(color, [](QPainter &p, int s) {
		p.setBrush(Qt::NoBrush);
		p.drawLine(s / 2, s * 2 / 10, s / 2, s * 8 / 10);
		p.drawLine(s / 2, s * 8 / 10, s * 3 / 10, s * 6 / 10);
		p.drawLine(s / 2, s * 8 / 10, s * 7 / 10, s * 6 / 10);
	});
}

QIcon MakeGearIcon(QColor color)
{
	return MakeVectorIcon(color, [color](QPainter &p, int s) {
		p.setPen(QPen(color, 2));
		p.setBrush(Qt::NoBrush);
		int c = s / 2, r = s * 3 / 10;
		p.drawEllipse(QPoint(c, c), r, r);
		p.setBrush(color);
		p.setPen(Qt::NoPen);
		for (int i = 0; i < 6; i++) {
			double a = i * 3.14159 / 3.0;
			int x = c + (int)((r + 2) * cos(a));
			int y = c + (int)((r + 2) * sin(a));
			p.drawEllipse(QPoint(x, y), 1, 1);
		}
	});
}

QIcon MakeDiagnosticsIcon(QColor color)
{
	// A simple pulse/EKG line -- reads as "connection health" at a glance.
	return MakeVectorIcon(color, [](QPainter &p, int s) {
		p.setBrush(Qt::NoBrush);
		QPolygon poly;
		poly << QPoint(s * 1 / 10, s * 5 / 10) << QPoint(s * 3 / 10, s * 5 / 10)
		     << QPoint(s * 4 / 10, s * 2 / 10) << QPoint(s * 6 / 10, s * 8 / 10)
		     << QPoint(s * 7 / 10, s * 5 / 10) << QPoint(s * 9 / 10, s * 5 / 10);
		p.drawPolyline(poly);
	});
}

QIcon MakeChatIcon(QColor color)
{
	return MakeVectorIcon(color, [color](QPainter &p, int s) {
		p.setPen(QPen(color, 1.5));
		p.setBrush(Qt::NoBrush);
		p.drawRoundedRect(s * 15 / 100, s * 2 / 10, s * 7 / 10, s * 5 / 10, 2, 2);
		QPolygon tail;
		tail << QPoint(s * 3 / 10, s * 7 / 10) << QPoint(s * 3 / 10, s * 9 / 10) << QPoint(s * 5 / 10, s * 7 / 10);
		p.setBrush(color);
		p.setPen(Qt::NoPen);
		p.drawPolygon(tail);
	});
}

QIcon MakeVerticalIcon(QColor color)
{
	return MakeVectorIcon(color, [color](QPainter &p, int s) {
		p.setPen(QPen(color, 1.5));
		p.setBrush(Qt::NoBrush);
		p.drawRoundedRect(s * 3 / 10, s * 1 / 10, s * 4 / 10, s * 8 / 10, 2, 2);
	});
}

// Tags a target's name cell so vertical targets are distinguishable from
// horizontal ones at a glance in the row list, without touching the cell's
// text (which doubles as the editable rename field for custom targets).
void UpdateNameOrientationBadge(QTableWidgetItem *nameItem, const OutputConfig &config)
{
	bool isCustom = (nameItem->flags() & Qt::ItemIsEditable) != 0;
	QString base = isCustom ? "Double-click to rename" : QString();

	if (config.orientation == CanvasOrientation::Vertical) {
		nameItem->setIcon(MakeVerticalIcon(QColor("#5dade2")));
		nameItem->setToolTip(base.isEmpty() ? "Vertical (9:16) target" : base + " — Vertical (9:16) target");
	} else {
		nameItem->setIcon(QIcon());
		nameItem->setToolTip(base);
	}
}

QIcon MakeHeartIcon(QColor color)
{
	return MakeVectorIcon(color, [color](QPainter &p, int s) {
		p.setPen(Qt::NoPen);
		p.setBrush(color);
		QPainterPath path;
		path.moveTo(s * 5 / 10, s * 8 / 10);
		path.cubicTo(s * -1 / 10, s * 4 / 10, s * 2 / 10, s * 1 / 10, s * 5 / 10, s * 3 / 10);
		path.cubicTo(s * 8 / 10, s * 1 / 10, s * 11 / 10, s * 4 / 10, s * 5 / 10, s * 8 / 10);
		p.drawPath(path);
	});
}

void StyleFieldButton(QToolButton *btn, bool isSet)
{
	const char *bg = isSet ? "#20402c" : "#402424";
	const char *border = isSet ? "#2ecc71" : "#e74c3c";
	btn->setStyleSheet(QString("QToolButton { background-color: %1; border: 1px solid %2; border-radius: 4px; "
				    "font-size: 13px; }"
				    "QToolButton:hover { background-color: %2; }")
				    .arg(bg, border));
}

void StyleNeutralButton(QToolButton *btn)
{
	btn->setStyleSheet("QToolButton { background-color: #2d2d33; border: 1px solid #55555c; border-radius: 4px; "
			    "font-size: 13px; }"
			    "QToolButton:hover { background-color: #55555c; }");
}

// RTMP(S) is the only scheme obs_output "rtmp_output" can actually connect
// with; anything else (http links, a pasted stream key instead of a URL,
// typos) would fail opaquely at stream-start time, so catch it at entry
// instead.
bool IsPlausibleRtmpUrl(const QString &url)
{
	QString lower = url.toLower();
	return lower.startsWith("rtmp://") || lower.startsWith("rtmps://");
}

void UpdateBitrateWarning(QSpinBox *spin, const OutputConfig &config)
{
	const PlatformPreset *preset = FindPreset(config.platform);
	bool overCeiling = preset && preset->maxRecommendedBitrateKbps > 0 &&
			   config.videoBitrateKbps > preset->maxRecommendedBitrateKbps;
	if (overCeiling) {
		spin->setStyleSheet("QSpinBox { border: 1px solid #e74c3c; }");
		spin->setToolTip(QString("Exceeds %1's recommended max of %2 kb/s — ingest may reject or "
					  "transcode this down")
					  .arg(QString::fromStdString(config.name))
					  .arg(preset->maxRecommendedBitrateKbps));
	} else {
		spin->setStyleSheet("");
		spin->setToolTip("");
	}
}

// Export/import serialization, deliberately separate from SettingsStore's
// on-disk format: this one keeps the stream key in plaintext (fine here --
// the whole blob gets password-encrypted by PasswordCrypto before it ever
// touches disk) and encodes platform as a raw int rather than SettingsStore's
// private string mapping, since this format only ever has to round-trip
// with itself.
void SerializeConfigPlain(obs_data_t *item, const OutputConfig &c)
{
	obs_data_set_int(item, "platform", (int)c.platform);
	obs_data_set_string(item, "name", c.name.c_str());
	obs_data_set_bool(item, "enabled", c.enabled);
	obs_data_set_string(item, "server", c.server.c_str());
	obs_data_set_string(item, "channel_name", c.channelName.c_str());
	obs_data_set_string(item, "stream_key", c.streamKey.c_str());
	obs_data_set_string(item, "video_encoder_id", c.videoEncoderId.c_str());
	obs_data_set_int(item, "video_bitrate_kbps", c.videoBitrateKbps);
	obs_data_set_int(item, "keyframe_interval_sec", c.keyframeIntervalSec);
	obs_data_set_string(item, "rate_control", c.rateControl.c_str());
	obs_data_set_string(item, "preset", c.preset.c_str());
	obs_data_set_string(item, "audio_encoder_id", c.audioEncoderId.c_str());
	obs_data_set_int(item, "audio_bitrate_kbps", c.audioBitrateKbps);
	obs_data_set_bool(item, "auto_reconnect", c.autoReconnect);
	obs_data_set_int(item, "reconnect_max_retries", c.reconnectMaxRetries);
	obs_data_set_int(item, "reconnect_base_delay_ms", c.reconnectBaseDelayMs);
	obs_data_set_int(item, "reconnect_max_delay_ms", c.reconnectMaxDelayMs);
}

OutputConfig DeserializeConfigPlain(obs_data_t *item)
{
	OutputConfig c;
	c.platform = (PlatformId)obs_data_get_int(item, "platform");
	c.name = obs_data_get_string(item, "name");
	c.enabled = obs_data_get_bool(item, "enabled");
	c.server = obs_data_get_string(item, "server");
	c.channelName = obs_data_get_string(item, "channel_name");
	c.streamKey = obs_data_get_string(item, "stream_key");
	c.videoEncoderId = obs_data_get_string(item, "video_encoder_id");
	c.videoBitrateKbps = (int)obs_data_get_int(item, "video_bitrate_kbps");
	c.keyframeIntervalSec = (int)obs_data_get_int(item, "keyframe_interval_sec");
	c.rateControl = obs_data_get_string(item, "rate_control");
	c.preset = obs_data_get_string(item, "preset");
	c.audioEncoderId = obs_data_get_string(item, "audio_encoder_id");
	c.audioBitrateKbps = (int)obs_data_get_int(item, "audio_bitrate_kbps");
	c.autoReconnect = obs_data_get_bool(item, "auto_reconnect");
	c.reconnectMaxRetries = (int)obs_data_get_int(item, "reconnect_max_retries");
	c.reconnectBaseDelayMs = (int)obs_data_get_int(item, "reconnect_base_delay_ms");
	c.reconnectMaxDelayMs = (int)obs_data_get_int(item, "reconnect_max_delay_ms");
	return c;
}

QString FormatDuration(int64_t seconds)
{
	if (seconds <= 0)
		return {};
	int64_t h = seconds / 3600;
	int64_t m = (seconds % 3600) / 60;
	int64_t s = seconds % 60;
	if (h > 0)
		return QString(" · %1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
	return QString(" · %1:%2").arg(m).arg(s, 2, 10, QChar('0'));
}

constexpr double kDroppedFrameWarnPercent = 5.0;

// "platformId:name" identity used both here and in MultistreamManager's own
// target matching -- stable enough to key a saved preset against, without
// needing a separate unique-ID field on OutputConfig.
QString PresetMemberId(const OutputConfig &c)
{
	return QString("%1:%2").arg((int)c.platform).arg(QString::fromStdString(c.name));
}
} // namespace

MultistreamDock::MultistreamDock(QWidget *parent) : QWidget(parent)
{
	BuildUi();

	// A small generated dot icon -- there's no bundled app icon asset, and
	// QSystemTrayIcon needs some icon to show balloon notifications at all.
	QPixmap pix(16, 16);
	pix.fill(Qt::transparent);
	{
		QPainter p(&pix);
		p.setRenderHint(QPainter::Antialiasing);
		p.setBrush(QColor("#2ecc71"));
		p.setPen(Qt::NoPen);
		p.drawEllipse(0, 0, 16, 16);
	}
	trayIcon_ = new QSystemTrayIcon(QIcon(pix), this);
	trayIcon_->setToolTip("Multistream");
	trayIcon_->setVisible(true);

	MultistreamManager::Get().LoadFromStore();
	PopulateFromManager();

	statusTimer_ = new QTimer(this);
	connect(statusTimer_, &QTimer::timeout, this, &MultistreamDock::RefreshStatus);
	statusTimer_->start(1000);
}

MultistreamDock::~MultistreamDock()
{
	// Belt-and-suspenders: every edit path already auto-saves on change, but
	// save once more on teardown in case OBS is closed mid-edit (e.g. a spin
	// box that hasn't lost focus yet, so its valueChanged/editingFinished
	// signal hasn't fired).
	ApplyRowsToManager();
	MultistreamManager::Get().SaveToStore();
}

void MultistreamDock::BuildUi()
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(10, 10, 10, 10);
	layout->setSpacing(8);

	// Controls condensed into one row at the top, table stretched to fill
	// whatever's left below -- the table is what benefits from extra
	// vertical space (more rows visible at once), not the controls.
	addTargetBtn_ = new QPushButton("+ Add Target", this);
	addTargetBtn_->setStyleSheet("QPushButton { padding: 3px 8px; font-size: 11px; border-radius: 3px; }");
	connect(addTargetBtn_, &QPushButton::clicked, this, &MultistreamDock::OnAddCustomTargetClicked);

	presetCombo_ = new QComboBox(this);
	presetCombo_->setMinimumWidth(90);
	presetCombo_->setStyleSheet("QComboBox { padding: 2px 6px; font-size: 11px; }");

	applyPresetBtn_ = MakeFieldButton(this, QString());
	applyPresetBtn_->setIcon(MakePlayIcon(QColor("#dcdde1")));
	StyleNeutralButton(applyPresetBtn_);
	applyPresetBtn_->setToolTip("Apply selected preset");

	savePresetBtn_ = MakeFieldButton(this, QString());
	savePresetBtn_->setIcon(MakeSaveIcon(QColor("#dcdde1")));
	StyleNeutralButton(savePresetBtn_);
	savePresetBtn_->setToolTip("Save current selection as a preset");

	exportBtn_ = MakeFieldButton(this, QString());
	exportBtn_->setIcon(MakeUpArrowIcon(QColor("#dcdde1")));
	StyleNeutralButton(exportBtn_);
	exportBtn_->setToolTip("Export config (password-encrypted backup)");

	importBtn_ = MakeFieldButton(this, QString());
	importBtn_->setIcon(MakeDownArrowIcon(QColor("#dcdde1")));
	StyleNeutralButton(importBtn_);
	importBtn_->setToolTip("Import config from a backup file");

	// Go Live / Stop condensed to small colored icon buttons (check / X)
	// rather than full-width text, matching the compact icon-button
	// language already used for the table's own row actions. Drawn as
	// vector icons rather than font glyphs -- see MakeVectorIcon.
	goLiveBtn_ = new QPushButton(this);
	goLiveBtn_->setIcon(MakeCheckIcon(QColor("#10190f")));
	goLiveBtn_->setIconSize(QSize(16, 16));
	goLiveBtn_->setFixedSize(30, 26);
	goLiveBtn_->setToolTip("Go Live (all enabled)");
	goLiveBtn_->setStyleSheet(
		"QPushButton { background-color: #2ecc71; border-radius: 4px; }"
		"QPushButton:hover { background-color: #3ee081; }"
		"QPushButton:pressed { background-color: #27ae60; }");

	stopBtn_ = new QPushButton(this);
	stopBtn_->setIcon(MakeXIcon(QColor("#1a0d0a")));
	stopBtn_->setIconSize(QSize(16, 16));
	stopBtn_->setFixedSize(30, 26);
	stopBtn_->setToolTip("Stop All");
	stopBtn_->setStyleSheet("QPushButton { background-color: #e74c3c; border-radius: 4px; }"
				 "QPushButton:hover { background-color: #ec6255; }"
				 "QPushButton:pressed { background-color: #c0392b; }");

	tipBtn_ = MakeFieldButton(this, QString());
	tipBtn_->setIcon(MakeHeartIcon(QColor("#e0245e")));
	StyleNeutralButton(tipBtn_);
	tipBtn_->setToolTip("Support development on Ko-fi");
	connect(tipBtn_, &QToolButton::clicked, this,
		[]() { QDesktopServices::openUrl(QUrl("https://ko-fi.com/pickledmoth")); });

	diagBtn_ = MakeFieldButton(this, QString());
	diagBtn_->setIcon(MakeDiagnosticsIcon(QColor("#dcdde1")));
	StyleNeutralButton(diagBtn_);
	diagBtn_->setToolTip("Chat Diagnostics -- inspect Twitch/YouTube connection state");
	connect(diagBtn_, &QToolButton::clicked, this, &MultistreamDock::OnDiagnosticsClicked);

	auto *btnRow = new QHBoxLayout();
	btnRow->setSpacing(6);
	btnRow->addWidget(addTargetBtn_);
	btnRow->addWidget(presetCombo_);
	btnRow->addWidget(applyPresetBtn_);
	btnRow->addWidget(savePresetBtn_);
	btnRow->addStretch();
	btnRow->addWidget(tipBtn_);
	btnRow->addWidget(diagBtn_);
	btnRow->addWidget(exportBtn_);
	btnRow->addWidget(importBtn_);
	btnRow->addWidget(goLiveBtn_);
	btnRow->addWidget(stopBtn_);
	layout->addLayout(btnRow);

	connect(goLiveBtn_, &QPushButton::clicked, this, &MultistreamDock::OnGoLiveClicked);
	connect(stopBtn_, &QPushButton::clicked, this, &MultistreamDock::OnStopClicked);
	connect(savePresetBtn_, &QToolButton::clicked, this, &MultistreamDock::OnSavePresetClicked);
	connect(applyPresetBtn_, &QToolButton::clicked, this, &MultistreamDock::OnApplyPresetClicked);
	connect(exportBtn_, &QToolButton::clicked, this, &MultistreamDock::OnExportConfigClicked);
	connect(importBtn_, &QToolButton::clicked, this, &MultistreamDock::OnImportConfigClicked);

	summaryLabel_ = new QLabel("Not live", this);
	QFont summaryFont = summaryLabel_->font();
	summaryFont.setBold(true);
	summaryLabel_->setFont(summaryFont);
	layout->addWidget(summaryLabel_);

	table_ = new QTableWidget(0, ColCount, this);
	table_->setHorizontalHeaderLabels({"On", "Platform", "Status", "Video kb/s", "URL", "Key", "", "", ""});
	table_->horizontalHeader()->setSectionResizeMode(ColEnabled, QHeaderView::ResizeToContents);
	table_->horizontalHeader()->setSectionResizeMode(ColPlatform, QHeaderView::ResizeToContents);
	table_->horizontalHeader()->setSectionResizeMode(ColServer, QHeaderView::ResizeToContents);
	table_->horizontalHeader()->setSectionResizeMode(ColStreamKey, QHeaderView::ResizeToContents);
	table_->horizontalHeader()->setSectionResizeMode(ColBitrate, QHeaderView::ResizeToContents);
	table_->horizontalHeader()->setSectionResizeMode(ColEncoder, QHeaderView::ResizeToContents);
	table_->horizontalHeader()->setSectionResizeMode(ColChat, QHeaderView::ResizeToContents);
	table_->horizontalHeader()->setSectionResizeMode(ColRemove, QHeaderView::ResizeToContents);
	table_->horizontalHeader()->setSectionResizeMode(ColStatus, QHeaderView::Stretch);
	table_->horizontalHeader()->setHighlightSections(false);
	table_->verticalHeader()->setVisible(false);
	table_->verticalHeader()->setDefaultSectionSize(34);
	table_->setShowGrid(false);
	table_->setAlternatingRowColors(true);
	table_->setSelectionMode(QAbstractItemView::NoSelection);
	table_->setFocusPolicy(Qt::NoFocus);
	table_->setEditTriggers(QAbstractItemView::AllEditTriggers);
	layout->addWidget(table_, /*stretch=*/1);

	// Only custom targets' name cells are made editable (see AddPlatformRow),
	// so this only ever fires for user-added rows renaming themselves.
	connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
		int row = item->row();
		if (item->column() != ColPlatform || row < 0 || row >= (int)workingConfigs_.size())
			return;
		QString name = item->text().trimmed();
		if (name.isEmpty()) {
			item->setText(QString::fromStdString(workingConfigs_[row].name));
			return;
		}
		workingConfigs_[row].name = name.toStdString();
		ApplyRowsToManager();
		MultistreamManager::Get().SaveToStore();
	});
}

void MultistreamDock::RefreshFromManager()
{
	PopulateFromManager();
}

void MultistreamDock::PopulateFromManager()
{
	workingConfigs_ = MultistreamManager::Get().GetConfigs();
	table_->setRowCount(0);
	for (auto &c : workingConfigs_)
		AddPlatformRow(c);
	lastStates_.assign(workingConfigs_.size(), OutputState::Idle);
	RefreshPresetCombo();
}

void MultistreamDock::AddPlatformRow(const OutputConfig &config)
{
	int row = table_->rowCount();
	table_->insertRow(row);

	auto *enabledCheck = new QCheckBox(this);
	enabledCheck->setChecked(config.enabled);
	connect(enabledCheck, &QCheckBox::toggled, this, [this](bool) {
		ApplyRowsToManager();
		MultistreamManager::Get().SaveToStore();
	});
	auto *enabledContainer = new QWidget(this);
	auto *enabledLayout = new QHBoxLayout(enabledContainer);
	enabledLayout->addWidget(enabledCheck);
	enabledLayout->setAlignment(Qt::AlignCenter);
	enabledLayout->setContentsMargins(0, 0, 0, 0);
	table_->setCellWidget(row, ColEnabled, enabledContainer);

	bool isCustom = config.platform == PlatformId::Custom;

	auto *nameItem = new QTableWidgetItem(QString::fromStdString(config.name));
	if (isCustom)
		nameItem->setFlags(nameItem->flags() | Qt::ItemIsEditable);
	else
		nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
	if (isCustom)
		nameItem->setToolTip("Double-click to rename");
	QFont nameFont = nameItem->font();
	nameFont.setBold(true);
	nameItem->setFont(nameFont);
	table_->setItem(row, ColPlatform, nameItem);
	UpdateNameOrientationBadge(nameItem, config);

	auto *serverBtn = MakeFieldButton(this, QStringLiteral("🔗"));
	StyleFieldButton(serverBtn, !config.server.empty());
	serverBtn->setToolTip(config.server.empty() ? "URL not set — click to enter"
						     : QString::fromStdString(config.server));
	connect(serverBtn, &QToolButton::clicked, this, [this, row, serverBtn]() {
		bool ok = false;
		QString current = QString::fromStdString(workingConfigs_[row].server);
		QString value = QInputDialog::getText(this, "URL",
						       QString("RTMP(S) server URL for %1:")
							       .arg(QString::fromStdString(workingConfigs_[row].name)),
						       QLineEdit::Normal, current, &ok);
		if (!ok)
			return;
		if (!value.isEmpty() && !IsPlausibleRtmpUrl(value)) {
			QMessageBox::warning(this, "Invalid URL",
					      "This doesn't look like an RTMP(S) URL -- it should start with "
					      "\"rtmp://\" or \"rtmps://\". Copy the ingest server URL from the "
					      "platform's stream dashboard, not the stream key or a web link.");
			return;
		}
		workingConfigs_[row].server = value.toStdString();
		StyleFieldButton(serverBtn, !value.isEmpty());
		serverBtn->setToolTip(value.isEmpty() ? "URL not set — click to enter" : value);
		ApplyRowsToManager();
		MultistreamManager::Get().SaveToStore();
	});
	table_->setCellWidget(row, ColServer, serverBtn);

	auto *keyBtn = MakeFieldButton(this, QStringLiteral("🔑"));
	StyleFieldButton(keyBtn, !config.streamKey.empty());
	keyBtn->setToolTip(config.streamKey.empty() ? "Key not set — click to enter" : "Key set — click to change");
	connect(keyBtn, &QToolButton::clicked, this, [this, row, keyBtn]() {
		bool ok = false;
		QString current = QString::fromStdString(workingConfigs_[row].streamKey);
		QString value = QInputDialog::getText(this, "Key",
						       QString("Stream key for %1:")
							       .arg(QString::fromStdString(workingConfigs_[row].name)),
						       QLineEdit::Password, current, &ok);
		if (!ok)
			return;
		workingConfigs_[row].streamKey = value.toStdString();
		StyleFieldButton(keyBtn, !value.isEmpty());
		keyBtn->setToolTip(value.isEmpty() ? "Key not set — click to enter" : "Key set — click to change");
		ApplyRowsToManager();
		MultistreamManager::Get().SaveToStore();
	});
	table_->setCellWidget(row, ColStreamKey, keyBtn);

	// Wrapped in a padded container rather than set directly as the cell
	// widget: bare it stretched flush to the cell's edges, looking cramped
	// next to the fixed-size, naturally-inset icon buttons in neighboring
	// columns (URL/Key/⚙/💬/✕).
	auto *bitrateSpin = new QSpinBox(this);
	bitrateSpin->setRange(500, 60000);
	bitrateSpin->setSingleStep(250);
	bitrateSpin->setValue(config.videoBitrateKbps);
	UpdateBitrateWarning(bitrateSpin, config);
	auto *bitrateContainer = new QWidget(this);
	auto *bitrateLayout = new QHBoxLayout(bitrateContainer);
	bitrateLayout->addWidget(bitrateSpin);
	bitrateLayout->setContentsMargins(4, 2, 4, 2);
	connect(bitrateSpin, &QSpinBox::editingFinished, this, [this, row, bitrateSpin]() {
		workingConfigs_[row].videoBitrateKbps = bitrateSpin->value();
		UpdateBitrateWarning(bitrateSpin, workingConfigs_[row]);
		ApplyRowsToManager();
		MultistreamManager::Get().SaveToStore();
	});
	table_->setCellWidget(row, ColBitrate, bitrateContainer);

	auto *encoderBtn = MakeFieldButton(this, QString());
	encoderBtn->setIcon(MakeGearIcon(QColor("#dcdde1")));
	StyleNeutralButton(encoderBtn);
	encoderBtn->setToolTip("Encoder settings");
	connect(encoderBtn, &QToolButton::clicked, this, [this, row]() {
		EncoderSettingsDialog dlg(workingConfigs_[row], this);
		if (dlg.exec() != QDialog::Accepted)
			return;
		workingConfigs_[row] = dlg.Result();
		if (auto *nameItem = table_->item(row, ColPlatform))
			UpdateNameOrientationBadge(nameItem, workingConfigs_[row]);
		if (auto *bitrateContainer = table_->cellWidget(row, ColBitrate)) {
			if (auto *bitrateSpin = bitrateContainer->findChild<QSpinBox *>()) {
				bitrateSpin->setValue(workingConfigs_[row].videoBitrateKbps);
				UpdateBitrateWarning(bitrateSpin, workingConfigs_[row]);
			}
		}
		ApplyRowsToManager();
		MultistreamManager::Get().SaveToStore();
	});
	table_->setCellWidget(row, ColEncoder, encoderBtn);

	auto *chatBtn = MakeFieldButton(this, QString());
	chatBtn->setIcon(MakeChatIcon(QColor("#dcdde1")));
	if (isCustom) {
		chatBtn->setEnabled(false);
		StyleNeutralButton(chatBtn);
		chatBtn->setToolTip("No generic chat link for custom targets");
	} else {
		StyleNeutralButton(chatBtn);
		chatBtn->setToolTip("Get a chat-embed link for OBS's Custom Browser Docks");
		connect(chatBtn, &QToolButton::clicked, this, [this, row]() {
			ChatLinkDialog dlg(workingConfigs_[row].platform, workingConfigs_[row].name,
					    workingConfigs_[row].channelName, this);
			dlg.exec();
			workingConfigs_[row].channelName = dlg.ChannelOrVideo();
			ApplyRowsToManager();
			MultistreamManager::Get().SaveToStore();
		});
	}
	table_->setCellWidget(row, ColChat, chatBtn);

	auto *removeBtn = MakeFieldButton(this, QString());
	removeBtn->setIcon(MakeXIcon(isCustom ? QColor("#e74c3c") : QColor("#6b6b70")));
	if (isCustom) {
		StyleFieldButton(removeBtn, false);
		removeBtn->setToolTip("Remove this custom target");
		connect(removeBtn, &QToolButton::clicked, this, [this, row]() { RemoveRow(row); });
	} else {
		removeBtn->setEnabled(false);
		StyleNeutralButton(removeBtn);
		removeBtn->setToolTip("Built-in platforms can't be removed — disable with the checkbox instead");
	}
	table_->setCellWidget(row, ColRemove, removeBtn);

	auto *statusItem = new QTableWidgetItem("Idle");
	statusItem->setFlags(statusItem->flags() & ~Qt::ItemIsEditable);
	statusItem->setForeground(QColor(StatusColor(OutputState::Idle)));
	table_->setItem(row, ColStatus, statusItem);
}

void MultistreamDock::ApplyRowsToManager()
{
	// Server/stream key are updated directly into workingConfigs_ by their
	// edit-dialog callbacks in AddPlatformRow, so only the checkbox and
	// bitrate spinner need to be read back from live widgets here.
	for (int row = 0; row < table_->rowCount() && row < (int)workingConfigs_.size(); row++) {
		auto *enabledContainer = table_->cellWidget(row, ColEnabled);
		auto *enabledCheck = enabledContainer->findChild<QCheckBox *>();
		auto *bitrateContainer = table_->cellWidget(row, ColBitrate);
		auto *bitrateSpin = bitrateContainer ? bitrateContainer->findChild<QSpinBox *>() : nullptr;

		workingConfigs_[row].enabled = enabledCheck && enabledCheck->isChecked();
		workingConfigs_[row].videoBitrateKbps = bitrateSpin ? bitrateSpin->value() : workingConfigs_[row].videoBitrateKbps;
	}

	MultistreamManager::Get().SetConfigs(workingConfigs_);
}

void MultistreamDock::OnGoLiveClicked()
{
	ApplyRowsToManager();
	MultistreamManager::Get().SaveToStore();

	QStringList incomplete;
	for (auto &c : workingConfigs_) {
		if (c.enabled && (c.server.empty() || c.streamKey.empty()))
			incomplete << QString::fromStdString(c.name);
	}
	if (!incomplete.isEmpty()) {
		QMessageBox::warning(this, "Missing URL/Key",
				      QString("These enabled platforms are missing a URL or key and won't start: "
					      "%1\n\nSet them (🔗/🔑) or disable the platform, then try again.")
					      .arg(incomplete.join(", ")));
		return;
	}

	MultistreamManager::Get().StartAll();
}

void MultistreamDock::OnStopClicked()
{
	MultistreamManager::Get().StopAll();
}

void MultistreamDock::OnAddCustomTargetClicked()
{
	int customCount = 0;
	for (auto &c : workingConfigs_) {
		if (c.platform == PlatformId::Custom)
			customCount++;
	}

	const PlatformPreset *preset = FindPreset(PlatformId::Custom);
	OutputConfig c;
	c.platform = PlatformId::Custom;
	c.name = "Custom " + std::to_string(customCount + 1);
	c.enabled = false;
	if (preset) {
		c.videoBitrateKbps = preset->defaultVideoBitrateKbps;
		c.audioBitrateKbps = preset->defaultAudioBitrateKbps;
	}
	workingConfigs_.push_back(std::move(c));

	// Not ApplyRowsToManager(): that re-reads live widgets by row index,
	// which is unsafe here since workingConfigs_ has grown but the table
	// hasn't been rebuilt yet. Every edit path already keeps workingConfigs_
	// itself authoritative, so push it straight through.
	MultistreamManager::Get().SetConfigs(workingConfigs_);
	MultistreamManager::Get().SaveToStore();
	PopulateFromManager();
}

void MultistreamDock::RemoveRow(int row)
{
	if (row < 0 || row >= (int)workingConfigs_.size())
		return;
	if (workingConfigs_[row].platform != PlatformId::Custom)
		return;

	auto reply = QMessageBox::question(this, "Remove Target",
					    QString("Remove \"%1\"? This can't be undone.")
						    .arg(QString::fromStdString(workingConfigs_[row].name)));
	if (reply != QMessageBox::Yes)
		return;

	workingConfigs_.erase(workingConfigs_.begin() + row);
	// See OnAddCustomTargetClicked: skip ApplyRowsToManager's live-widget
	// re-read here too, for the same reason -- row indices no longer line
	// up between workingConfigs_ and the (not-yet-rebuilt) table.
	MultistreamManager::Get().SetConfigs(workingConfigs_);
	MultistreamManager::Get().SaveToStore();
	PopulateFromManager();
}

void MultistreamDock::NotifyStateChange(const QString &platformName, OutputState oldState, OutputState newState)
{
	if (!trayIcon_)
		return;
	// Only flag *unexpected* transitions: a user-initiated Stop takes Live
	// straight to Stopped (see OutputTarget::Stop), never through
	// Reconnecting, so that path never fires a toast here.
	if (oldState == OutputState::Live &&
	    (newState == OutputState::Reconnecting || newState == OutputState::Error)) {
		trayIcon_->showMessage("Multistream", QString("%1 disconnected").arg(platformName),
					QSystemTrayIcon::Warning, 5000);
	} else if ((oldState == OutputState::Reconnecting || oldState == OutputState::Connecting) &&
		   newState == OutputState::Live) {
		trayIcon_->showMessage("Multistream", QString("%1 reconnected").arg(platformName),
					QSystemTrayIcon::Information, 4000);
	}
}

void MultistreamDock::RefreshStatus()
{
	auto statuses = MultistreamManager::Get().GetStatusSnapshot();
	int liveCount = 0;
	int64_t maxLiveSeconds = 0;

	if (lastStates_.size() != statuses.size())
		lastStates_.assign(statuses.size(), OutputState::Idle);

	for (int row = 0; row < table_->rowCount() && row < (int)statuses.size(); row++) {
		bool dropWarning = statuses[row].state == OutputState::Live &&
				    statuses[row].droppedPercent >= kDroppedFrameWarnPercent;
		QString text;
		switch (statuses[row].state) {
		case OutputState::Idle:
			text = "Idle";
			break;
		case OutputState::Connecting:
			text = "Connecting…";
			break;
		case OutputState::Live:
			text = QString("Live · %1 kb/s%2")
				       .arg(statuses[row].bitrateKbps)
				       .arg(FormatDuration(statuses[row].liveSeconds));
			if (dropWarning)
				text += QString(" ⚠ %1% dropped").arg(statuses[row].droppedPercent, 0, 'f', 1);
			liveCount++;
			maxLiveSeconds = std::max(maxLiveSeconds, statuses[row].liveSeconds);
			break;
		case OutputState::Reconnecting:
			text = "Reconnecting…";
			break;
		case OutputState::Error:
			text = "Error";
			break;
		case OutputState::Stopped:
			text = "Stopped";
			break;
		}
		if (auto *item = table_->item(row, ColStatus)) {
			item->setText(text);
			item->setForeground(QColor(dropWarning ? "#f39c12" : StatusColor(statuses[row].state)));
		}

		NotifyStateChange(QString::fromStdString(statuses[row].name), lastStates_[row], statuses[row].state);
		lastStates_[row] = statuses[row].state;
	}

	if (liveCount > 0) {
		summaryLabel_->setText(
			QString("● Live on %1 platform(s)%2").arg(liveCount).arg(FormatDuration(maxLiveSeconds)));
		summaryLabel_->setStyleSheet("color: #2ecc71;");
	} else {
		summaryLabel_->setText("Not live");
		summaryLabel_->setStyleSheet("color: #9aa0a6;");
	}
}

void MultistreamDock::RefreshPresetCombo()
{
	QString current = presetCombo_->currentText();
	presetCombo_->clear();
	for (auto &[name, ids] : SettingsStore::Get().LoadPresets()) {
		(void)ids;
		presetCombo_->addItem(QString::fromStdString(name));
	}
	int idx = presetCombo_->findText(current);
	if (idx >= 0)
		presetCombo_->setCurrentIndex(idx);
}

void MultistreamDock::OnSavePresetClicked()
{
	ApplyRowsToManager();

	bool ok = false;
	QString name = QInputDialog::getText(this, "Save Preset", "Preset name (e.g. \"Twitch only\"):",
					      QLineEdit::Normal, presetCombo_->currentText(), &ok);
	name = name.trimmed();
	if (!ok || name.isEmpty())
		return;

	std::vector<std::string> enabledIds;
	for (auto &c : workingConfigs_) {
		if (c.enabled)
			enabledIds.push_back(PresetMemberId(c).toStdString());
	}

	auto presets = SettingsStore::Get().LoadPresets();
	std::string nameStd = name.toStdString();
	bool replaced = false;
	for (auto &[pname, ids] : presets) {
		if (pname == nameStd) {
			ids = enabledIds;
			replaced = true;
			break;
		}
	}
	if (!replaced)
		presets.push_back({nameStd, enabledIds});

	SettingsStore::Get().SavePresets(presets);
	RefreshPresetCombo();
	presetCombo_->setCurrentText(name);
}

void MultistreamDock::OnApplyPresetClicked()
{
	QString name = presetCombo_->currentText();
	if (name.isEmpty())
		return;

	for (auto &[pname, ids] : SettingsStore::Get().LoadPresets()) {
		if (pname != name.toStdString())
			continue;

		for (auto &c : workingConfigs_) {
			QString id = PresetMemberId(c);
			c.enabled = std::find(ids.begin(), ids.end(), id.toStdString()) != ids.end();
		}

		MultistreamManager::Get().SetConfigs(workingConfigs_);
		MultistreamManager::Get().SaveToStore();
		PopulateFromManager();
		return;
	}
}

void MultistreamDock::OnExportConfigClicked()
{
	ApplyRowsToManager();

	bool ok = false;
	QString pw1 = QInputDialog::getText(this, "Export Config",
					     "Choose a password to encrypt this backup. Anyone with the file and "
					     "this password can see your stream keys, so pick something real:",
					     QLineEdit::Password, "", &ok);
	if (!ok || pw1.isEmpty())
		return;
	QString pw2 = QInputDialog::getText(this, "Export Config", "Confirm password:", QLineEdit::Password, "",
					     &ok);
	if (!ok || pw2 != pw1) {
		QMessageBox::warning(this, "Export Config", "Passwords didn't match -- nothing exported.");
		return;
	}

	QString filePath = QFileDialog::getSaveFileName(this, "Export Config", "OBS-Simulcast-backup.json.enc",
							  "Encrypted backup (*.json.enc)");
	if (filePath.isEmpty())
		return;

	OBSDataAutoRelease root = obs_data_create();
	OBSDataArrayAutoRelease arr = obs_data_array_create();
	for (auto &c : workingConfigs_) {
		OBSDataAutoRelease item = obs_data_create();
		SerializeConfigPlain(item, c);
		obs_data_array_push_back(arr, item);
	}
	obs_data_set_array(root, "targets", arr);

	std::string json = obs_data_get_json(root);
	std::string encrypted = PasswordCrypto::Encrypt(json, pw1.toStdString());
	if (encrypted.empty()) {
		QMessageBox::critical(this, "Export Config", "Encryption failed -- nothing was written.");
		return;
	}

	QFile file(filePath);
	if (!file.open(QIODevice::WriteOnly)) {
		QMessageBox::critical(this, "Export Config", "Couldn't write to that location.");
		return;
	}
	file.write(encrypted.data(), (qint64)encrypted.size());
	file.close();
	QMessageBox::information(this, "Export Config", "Backup saved.");
}

void MultistreamDock::OnImportConfigClicked()
{
	QString filePath = QFileDialog::getOpenFileName(this, "Import Config", QString(),
							  "Encrypted backup (*.json.enc);;All files (*)");
	if (filePath.isEmpty())
		return;

	QFile file(filePath);
	if (!file.open(QIODevice::ReadOnly)) {
		QMessageBox::critical(this, "Import Config", "Couldn't read that file.");
		return;
	}
	QByteArray data = file.readAll();
	file.close();

	bool ok = false;
	QString pw = QInputDialog::getText(this, "Import Config", "Password for this backup:", QLineEdit::Password,
					    "", &ok);
	if (!ok)
		return;

	std::string json = PasswordCrypto::Decrypt(std::string(data.constData(), (size_t)data.size()),
						    pw.toStdString());
	if (json.empty()) {
		QMessageBox::critical(this, "Import Config", "Wrong password, or the file is corrupted/not a "
							       "OBS-Simulcast backup.");
		return;
	}

	OBSDataAutoRelease root = obs_data_create_from_json(json.c_str());
	if (!root) {
		QMessageBox::critical(this, "Import Config", "Backup contents are corrupted.");
		return;
	}

	std::vector<OutputConfig> imported;
	OBSDataArrayAutoRelease arr = obs_data_get_array(root, "targets");
	if (arr) {
		size_t count = obs_data_array_count(arr);
		for (size_t i = 0; i < count; i++) {
			OBSDataAutoRelease item = obs_data_array_item(arr, i);
			imported.push_back(DeserializeConfigPlain(item));
		}
	}

	if (imported.empty()) {
		QMessageBox::warning(this, "Import Config", "Backup has no targets in it.");
		return;
	}

	if (QMessageBox::question(this, "Import Config",
				   QString("Replace all current targets with the %1 from this backup? This can't "
					   "be undone.")
					   .arg(imported.size())) != QMessageBox::Yes)
		return;

	MultistreamManager::Get().SetConfigs(imported);
	MultistreamManager::Get().SaveToStore();

	PopulateFromManager();
	QMessageBox::information(this, "Import Config", "Backup imported.");
}

void MultistreamDock::OnDiagnosticsClicked()
{
	ChatDiagnosticsDialog dlg(chatDock_, this);
	dlg.exec();
}
