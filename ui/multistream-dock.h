#pragma once

#include <QWidget>
#include <QTimer>
#include <QTableWidget>
#include <QPushButton>
#include <QToolButton>
#include <QLabel>
#include <QComboBox>
#include <QSystemTrayIcon>
#include <vector>
#include <map>

#include "output-target.h"

class ChatViewDock;

// Main dock: one row per platform target with enable checkbox, server +
// stream key fields, per-target bitrate, and a live status column. Also
// hosts the global "Go Live" / "Stop" buttons that start/stop all enabled
// targets together. Embedded into the OBS main window via
// obs_frontend_add_dock_by_id rather than shown as a separate popup window,
// so it lives alongside the Scenes/Sources/Mixer docks. Built up
// programmatically (no .ui file) to keep the widget tree and the row <->
// OutputConfig mapping in one place.
class MultistreamDock : public QWidget {
	Q_OBJECT

public:
	explicit MultistreamDock(QWidget *parent = nullptr);
	~MultistreamDock() override;

	// Rebuilds the table from MultistreamManager's current configs.
	// Called after an external reload (e.g. OBS profile switch swapped in a
	// different profile's target file) so the dock reflects what's actually
	// loaded instead of stale rows from the previous profile.
	void RefreshFromManager();

	// Set once from plugin-main.cpp right after both docks are constructed
	// -- lets the Diagnostics button (this dock) reach the chat clients
	// that actually live inside ChatViewDock, without merging the two
	// docks or routing chat state through MultistreamManager.
	void SetChatDock(ChatViewDock *dock) { chatDock_ = dock; }

private slots:
	void OnGoLiveClicked();
	void OnStopClicked();
	void OnAddCustomTargetClicked();
	void RefreshStatus();
	void OnSavePresetClicked();
	void OnApplyPresetClicked();
	void OnExportConfigClicked();
	void OnImportConfigClicked();
	void OnDiagnosticsClicked();

private:
	void BuildUi();
	void PopulateFromManager();
	void ApplyRowsToManager();
	void AddPlatformRow(const OutputConfig &config);
	void RemoveRow(int row);
	void RefreshPresetCombo();
	void NotifyStateChange(const QString &platformName, OutputState oldState, OutputState newState);

	QTableWidget *table_ = nullptr;
	QPushButton *goLiveBtn_ = nullptr;
	QPushButton *stopBtn_ = nullptr;
	QPushButton *addTargetBtn_ = nullptr;
	QLabel *summaryLabel_ = nullptr;
	QTimer *statusTimer_ = nullptr;
	std::vector<OutputConfig> workingConfigs_;

	// Item 4: toast on disconnect/reconnect -- tracks each row's previous
	// state so RefreshStatus can detect Live -> non-Live and back
	// transitions without a separate callback wiring through OutputTarget.
	std::vector<OutputState> lastStates_;
	QSystemTrayIcon *trayIcon_ = nullptr;

	// Item 6: named groups of "which platforms are enabled", stored
	// alongside targets in the same settings file.
	QComboBox *presetCombo_ = nullptr;
	QToolButton *savePresetBtn_ = nullptr;
	QToolButton *applyPresetBtn_ = nullptr;

	// Item 5: password-encrypted export/import.
	QToolButton *exportBtn_ = nullptr;
	QToolButton *importBtn_ = nullptr;

	// Tip jar link -- opens the author's Ko-fi in the system browser.
	QToolButton *tipBtn_ = nullptr;

	QToolButton *diagBtn_ = nullptr;
	ChatViewDock *chatDock_ = nullptr;
};
