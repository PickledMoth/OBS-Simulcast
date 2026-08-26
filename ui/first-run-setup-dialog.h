#pragma once

#include <QDialog>
#include <vector>

#include "output-target.h"

class QLineEdit;
class QCheckBox;

// Shown once, the very first time the plugin runs against a profile (see
// SettingsStore::FirstRunSetupCompleted) -- lets the user paste their Twitch and/or
// YouTube stream keys up front instead of discovering the per-row 🔑 buttons
// on their own, and immediately saves a "Default" preset from whatever they
// enter so OnApplyPresetClicked has something real to offer right away.
class FirstRunSetupDialog : public QDialog {
	Q_OBJECT

public:
	explicit FirstRunSetupDialog(QWidget *parent = nullptr);

	// Twitch first, YouTube second -- only entries the user actually filled
	// in (enabled + non-empty key); empty if they skipped everything.
	const std::vector<OutputConfig> &Result() const { return result_; }

private slots:
	void OnAccept();

private:
	QCheckBox *twitchEnable_ = nullptr;
	QLineEdit *twitchChannel_ = nullptr;
	QLineEdit *twitchKey_ = nullptr;

	QCheckBox *youtubeEnable_ = nullptr;
	QLineEdit *youtubeKey_ = nullptr;
	QLineEdit *youtubeChannel_ = nullptr;

	std::vector<OutputConfig> result_;
};
