#pragma once

#include <vector>
#include <string>
#include "output-target.h"

// Persists target configs as JSON under OBS's per-profile plugin config dir
// (obs_module_config_path), the same mechanism OBS core uses for its own
// stream service settings -- so keys live alongside the profile, not in a
// single shared global file, and are cleaned up if OBS's data dir is wiped.
class SettingsStore {
public:
	static SettingsStore &Get();

	std::vector<OutputConfig> Load();
	void Save(const std::vector<OutputConfig> &configs);

	// True once a targets file has actually been written for the current
	// profile. False the very first time the plugin runs against a profile
	// (Load() still returns usable data in that case -- DefaultConfigs() --
	// so this exists purely to let the UI distinguish "brand new install/
	// profile" from "user has zero targets configured".
	//
	// NOT what gates the first-run setup wizard -- see
	// FirstRunSetupCompleted below. A plain open-and-close of OBS writes
	// this file too (MultistreamDock's destructor always saves, even if
	// nothing was configured), so using file-existence to decide whether
	// to show the wizard let a single untouched session silently burn the
	// one-time prompt before the user ever saw it.
	bool ConfigFileExists() const;

	// Whether the first-run setup wizard (Twitch/YouTube key entry) has
	// actually been shown to the user for this profile -- set the moment
	// the dialog is shown, regardless of whether they filled it in or hit
	// Skip, so it's a genuine "have they seen it" flag rather than a proxy
	// for "does some file happen to exist" (see ConfigFileExists).
	bool FirstRunSetupCompleted() const;
	void MarkFirstRunSetupCompleted();

	// Named full-setup snapshots (e.g. "Solo", "Collab") for the dock's
	// quick-toggle preset combo. Key is the preset name; value is a full
	// OutputConfig snapshot per member (channel, server, stream key,
	// bitrate, encoder/reconnect settings, enabled state -- everything),
	// keyed against current targets the same way MultistreamManager::
	// SetConfigs matches them (platform + name). Applying a preset restores
	// each matching target's entire config, not just whether it's enabled
	// -- e.g. switching to a preset that streams to a different Twitch
	// channel actually swaps the channel, not just flips a checkbox.
	std::vector<std::pair<std::string, std::vector<OutputConfig>>> LoadPresets();
	void SavePresets(const std::vector<std::pair<std::string, std::vector<OutputConfig>>> &presets);

	// Returns the built-in defaults (YouTube/Twitch/Kick, all disabled,
	// preset servers, empty keys) used the first time the plugin runs.
	static std::vector<OutputConfig> DefaultConfigs();

private:
	SettingsStore() = default;
	std::string ConfigFilePath() const;
};
