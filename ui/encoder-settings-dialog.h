#pragma once

#include <QDialog>

#include "output-target.h"

class QComboBox;
class QSpinBox;
class QLineEdit;
class QCheckBox;
class QPushButton;
class QLabel;
class QTcpSocket;

// Per-target encoder + reconnect settings: video encoder (x264/NVENC/QSV/AMF),
// rate control, preset, keyframe interval, audio encoder/bitrate, and
// reconnect behavior (auto-reconnect toggle, max retries, backoff delays).
// Pulled out of the main dock table into its own dialog rather than more
// columns -- these are set-once-per-platform values, not something worth
// permanent screen space next to the on/off toggle and live status.
class EncoderSettingsDialog : public QDialog {
	Q_OBJECT

public:
	explicit EncoderSettingsDialog(OutputConfig config, QWidget *parent = nullptr);

	const OutputConfig &Result() const { return config_; }

private slots:
	void OnAccept();
	void OnVideoEncoderChanged();
	void OnTestConnectionClicked();

private:
	void BuildUi();
	void PopulateVideoEncoders();

	OutputConfig config_;

	QComboBox *orientationCombo_ = nullptr;
	QComboBox *videoEncoderCombo_ = nullptr;
	QComboBox *rateControlCombo_ = nullptr;
	QLineEdit *presetEdit_ = nullptr;
	QSpinBox *keyframeIntervalSpin_ = nullptr;
	QSpinBox *audioBitrateSpin_ = nullptr;

	QCheckBox *autoReconnectCheck_ = nullptr;
	QSpinBox *maxRetriesSpin_ = nullptr;
	QSpinBox *baseDelaySpin_ = nullptr;
	QSpinBox *maxDelaySpin_ = nullptr;

	// Item 3: reachability dry-run. This only proves the ingest host:port
	// accepts a TCP/TLS connection -- it can't validate the stream key
	// itself without actually publishing (briefly going live), so it's
	// explicitly a network-reachability check, not a full "will this key
	// work" test.
	QPushButton *testConnectionBtn_ = nullptr;
	QLabel *testConnectionResultLabel_ = nullptr;
	QTcpSocket *testSocket_ = nullptr;
};
