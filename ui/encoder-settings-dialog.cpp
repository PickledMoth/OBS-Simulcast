#include "encoder-settings-dialog.h"

#include <obs.h>
#include <cstring>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>
#include <QFrame>
#include <QFont>
#include <QTcpSocket>
#include <QUrl>

EncoderSettingsDialog::EncoderSettingsDialog(OutputConfig config, QWidget *parent)
	: QDialog(parent),
	  config_(std::move(config))
{
	setWindowTitle(QString("Encoder & Reconnect Settings — %1").arg(QString::fromStdString(config_.name)));
	BuildUi();
}

void EncoderSettingsDialog::PopulateVideoEncoders()
{
	// Enumerate what's actually registered in this libobs instance rather
	// than hardcoding IDs, so NVENC/QSV/AMF only show up when the matching
	// GPU/driver support is actually present -- picking one that isn't
	// available would just fail to create at stream-start time.
	const char *id = nullptr;
	for (size_t i = 0; obs_enum_encoder_types(i, &id); i++) {
		if (obs_get_encoder_type(id) != OBS_ENCODER_VIDEO)
			continue;
		const char *codec = obs_get_encoder_codec(id);
		if (!codec || strcmp(codec, "h264") != 0)
			continue; // h264 only: universally accepted by RTMP ingest on all three platforms

		const char *displayName = obs_encoder_get_display_name(id);
		videoEncoderCombo_->addItem(displayName ? displayName : id, QString(id));
	}

	if (videoEncoderCombo_->count() == 0)
		videoEncoderCombo_->addItem("x264 (software)", QString("obs_x264"));

	int idx = videoEncoderCombo_->findData(QString::fromStdString(config_.videoEncoderId));
	videoEncoderCombo_->setCurrentIndex(idx >= 0 ? idx : 0);
}

void EncoderSettingsDialog::BuildUi()
{
	auto *layout = new QVBoxLayout(this);
	auto *form = new QFormLayout();

	orientationCombo_ = new QComboBox(this);
	orientationCombo_->addItem("Horizontal (main scene)", QVariant((int)CanvasOrientation::Horizontal));
	orientationCombo_->addItem("Vertical (9:16)", QVariant((int)CanvasOrientation::Vertical));
	orientationCombo_->setCurrentIndex(config_.orientation == CanvasOrientation::Vertical ? 1 : 0);
	form->addRow("Orientation:", orientationCombo_);

	auto *orientationNote = new QLabel(
		"Vertical targets encode from a separate 1080x1920 scene you compose yourself (View menu → "
		"Docks, or see SETUP.md) -- they don't crop or letterbox your main scene.",
		this);
	orientationNote->setWordWrap(true);
	orientationNote->setStyleSheet("color: #9aa0a6; font-size: 11px;");
	form->addRow("", orientationNote);

	videoEncoderCombo_ = new QComboBox(this);
	PopulateVideoEncoders();
	form->addRow("Video encoder:", videoEncoderCombo_);
	connect(videoEncoderCombo_, &QComboBox::currentIndexChanged, this, &EncoderSettingsDialog::OnVideoEncoderChanged);

	rateControlCombo_ = new QComboBox(this);
	rateControlCombo_->addItems({"CBR", "VBR", "CQP"});
	int rcIdx = rateControlCombo_->findText(QString::fromStdString(config_.rateControl), Qt::MatchFixedString);
	rateControlCombo_->setCurrentIndex(rcIdx >= 0 ? rcIdx : 0);
	form->addRow("Rate control:", rateControlCombo_);

	presetEdit_ = new QLineEdit(QString::fromStdString(config_.preset), this);
	presetEdit_->setPlaceholderText("e.g. veryfast (x264), hq (NVENC), quality (QSV/AMF)");
	form->addRow("Preset:", presetEdit_);

	keyframeIntervalSpin_ = new QSpinBox(this);
	keyframeIntervalSpin_->setRange(1, 10);
	keyframeIntervalSpin_->setSuffix(" s");
	keyframeIntervalSpin_->setValue(config_.keyframeIntervalSec);
	form->addRow("Keyframe interval:", keyframeIntervalSpin_);

	audioBitrateSpin_ = new QSpinBox(this);
	audioBitrateSpin_->setRange(64, 320);
	audioBitrateSpin_->setSingleStep(32);
	audioBitrateSpin_->setSuffix(" kb/s");
	audioBitrateSpin_->setValue(config_.audioBitrateKbps);
	form->addRow("Audio bitrate:", audioBitrateSpin_);

	layout->addLayout(form);

	auto *note = new QLabel(
		"Keyframe interval should usually match across all platforms and be 2s or a divisor of your "
		"segment length -- mismatched intervals are a common cause of transcoding/ABR issues on some "
		"platforms.",
		this);
	note->setWordWrap(true);
	note->setStyleSheet("color: #9aa0a6; font-size: 11px;");
	layout->addWidget(note);

	auto *divider = new QFrame(this);
	divider->setFrameShape(QFrame::HLine);
	divider->setStyleSheet("color: #444;");
	layout->addWidget(divider);

	auto *reconnectLabel = new QLabel("Reconnect", this);
	QFont boldFont = reconnectLabel->font();
	boldFont.setBold(true);
	reconnectLabel->setFont(boldFont);
	layout->addWidget(reconnectLabel);

	auto *reconnectForm = new QFormLayout();

	autoReconnectCheck_ = new QCheckBox("Automatically reconnect on drop", this);
	autoReconnectCheck_->setChecked(config_.autoReconnect);
	reconnectForm->addRow(autoReconnectCheck_);

	maxRetriesSpin_ = new QSpinBox(this);
	maxRetriesSpin_->setRange(1, 100);
	maxRetriesSpin_->setValue(config_.reconnectMaxRetries);
	reconnectForm->addRow("Max retries:", maxRetriesSpin_);

	baseDelaySpin_ = new QSpinBox(this);
	baseDelaySpin_->setRange(1, 60);
	baseDelaySpin_->setSuffix(" s");
	baseDelaySpin_->setValue(config_.reconnectBaseDelayMs / 1000);
	reconnectForm->addRow("Initial retry delay:", baseDelaySpin_);

	maxDelaySpin_ = new QSpinBox(this);
	maxDelaySpin_->setRange(1, 300);
	maxDelaySpin_->setSuffix(" s");
	maxDelaySpin_->setValue(config_.reconnectMaxDelayMs / 1000);
	reconnectForm->addRow("Max retry delay:", maxDelaySpin_);

	layout->addLayout(reconnectForm);

	auto *reconnectNote = new QLabel(
		"Delay doubles after each failed attempt (exponential backoff), capped at the max above, so a "
		"flaky connection doesn't hammer the platform's ingest server with rapid retries.",
		this);
	reconnectNote->setWordWrap(true);
	reconnectNote->setStyleSheet("color: #9aa0a6; font-size: 11px;");
	layout->addWidget(reconnectNote);

	auto *testDivider = new QFrame(this);
	testDivider->setFrameShape(QFrame::HLine);
	testDivider->setStyleSheet("color: #444;");
	layout->addWidget(testDivider);

	auto *testRow = new QHBoxLayout();
	testConnectionBtn_ = new QPushButton("Test Connection", this);
	testConnectionResultLabel_ = new QLabel("", this);
	testConnectionResultLabel_->setStyleSheet("color: #9aa0a6; font-size: 11px;");
	testRow->addWidget(testConnectionBtn_);
	testRow->addWidget(testConnectionResultLabel_, /*stretch=*/1);
	layout->addLayout(testRow);
	connect(testConnectionBtn_, &QPushButton::clicked, this, &EncoderSettingsDialog::OnTestConnectionClicked);

	auto *testNote = new QLabel(
		"Checks that the ingest server accepts a network connection -- it can't verify the stream key "
		"itself without actually publishing, so a pass here doesn't guarantee Go Live will work, but a "
		"failure means something's wrong before you find out live.",
		this);
	testNote->setWordWrap(true);
	testNote->setStyleSheet("color: #9aa0a6; font-size: 11px;");
	layout->addWidget(testNote);

	auto *btnRow = new QHBoxLayout();
	auto *cancelBtn = new QPushButton("Cancel", this);
	auto *okBtn = new QPushButton("OK", this);
	okBtn->setDefault(true);
	btnRow->addStretch();
	btnRow->addWidget(cancelBtn);
	btnRow->addWidget(okBtn);
	layout->addLayout(btnRow);

	connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
	connect(okBtn, &QPushButton::clicked, this, &EncoderSettingsDialog::OnAccept);

	resize(420, sizeHint().height());
}

void EncoderSettingsDialog::OnVideoEncoderChanged()
{
	QString id = videoEncoderCombo_->currentData().toString();
	bool isX264 = id == "obs_x264";
	if (isX264 && presetEdit_->text().isEmpty())
		presetEdit_->setText("veryfast");
}

void EncoderSettingsDialog::OnTestConnectionClicked()
{
	if (config_.server.empty()) {
		testConnectionResultLabel_->setText("No URL set for this target yet.");
		testConnectionResultLabel_->setStyleSheet("color: #e74c3c; font-size: 11px;");
		return;
	}

	QUrl url(QString::fromStdString(config_.server));
	QString host = url.host();
	int port = url.port(url.scheme() == "rtmps" ? 443 : 1935);
	if (host.isEmpty()) {
		testConnectionResultLabel_->setText("Couldn't parse a host from that URL.");
		testConnectionResultLabel_->setStyleSheet("color: #e74c3c; font-size: 11px;");
		return;
	}

	testConnectionResultLabel_->setText(QString("Connecting to %1:%2…").arg(host).arg(port));
	testConnectionResultLabel_->setStyleSheet("color: #f1c40f; font-size: 11px;");
	testConnectionBtn_->setEnabled(false);

	if (testSocket_) {
		testSocket_->deleteLater();
		testSocket_ = nullptr;
	}
	testSocket_ = new QTcpSocket(this);

	connect(testSocket_, &QTcpSocket::connected, this, [this]() {
		testConnectionResultLabel_->setText("✓ Server reachable");
		testConnectionResultLabel_->setStyleSheet("color: #2ecc71; font-size: 11px;");
		testConnectionBtn_->setEnabled(true);
		testSocket_->disconnectFromHost();
	});
	connect(testSocket_, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
		testConnectionResultLabel_->setText(QString("✗ %1").arg(testSocket_->errorString()));
		testConnectionResultLabel_->setStyleSheet("color: #e74c3c; font-size: 11px;");
		testConnectionBtn_->setEnabled(true);
	});

	testSocket_->connectToHost(host, (quint16)port);
}

void EncoderSettingsDialog::OnAccept()
{
	config_.orientation = (CanvasOrientation)orientationCombo_->currentData().toInt();
	config_.videoEncoderId = videoEncoderCombo_->currentData().toString().toStdString();
	config_.rateControl = rateControlCombo_->currentText().toStdString();
	config_.preset = presetEdit_->text().toStdString();
	config_.keyframeIntervalSec = keyframeIntervalSpin_->value();
	config_.audioBitrateKbps = audioBitrateSpin_->value();

	config_.autoReconnect = autoReconnectCheck_->isChecked();
	config_.reconnectMaxRetries = maxRetriesSpin_->value();
	config_.reconnectBaseDelayMs = baseDelaySpin_->value() * 1000;
	config_.reconnectMaxDelayMs = maxDelaySpin_->value() * 1000;

	accept();
}
