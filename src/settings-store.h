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

	// Named "which platforms are enabled" groups (e.g. "Twitch only",
	// "Everywhere") for the dock's quick-toggle preset combo. Key is the
	// preset name; value is the set of "platformId:name" identifiers
	// (matching how MultistreamManager::SetConfigs matches existing
	// targets) that should be enabled when that preset is applied.
	std::vector<std::pair<std::string, std::vector<std::string>>> LoadPresets();
	void SavePresets(const std::vector<std::pair<std::string, std::vector<std::string>>> &presets);

	// Returns the built-in defaults (YouTube/Twitch/Kick, all disabled,
	// preset servers, empty keys) used the first time the plugin runs.
	static std::vector<OutputConfig> DefaultConfigs();

private:
	SettingsStore() = default;
	std::string ConfigFilePath() const;
};
