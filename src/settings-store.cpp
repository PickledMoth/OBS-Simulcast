#include "settings-store.h"
#include "secure-string.h"

#include <obs-module.h>
#include <obs.h>
#include <obs-frontend-api.h>
#include <util/platform.h>
#include <util/config-file.h>
#include <cstring>

SettingsStore &SettingsStore::Get()
{
	static SettingsStore instance;
	return instance;
}

std::string SettingsStore::ConfigFilePath() const
{
	// Stored inside the current OBS *profile* directory (same place OBS
	// keeps its own stream service settings) rather than the plugin's
	// global module-config dir, so each profile can point at a different
	// channel/account -- switching profiles switches targets, matching how
	// OBS's own "Stream" settings already behave per-profile.
	char *profilePath = obs_frontend_get_current_profile_path();
	if (profilePath && *profilePath) {
		std::string result = std::string(profilePath) + "/OBS-Simulcast-targets.json";
		bfree(profilePath);
		return result;
	}
	bfree(profilePath);

	// Fallback for the (practically unreachable outside tests) case where
	// no profile is active yet.
	char *path = obs_module_config_path("targets.json");
	std::string result = path ? path : "";
	bfree(path);
	return result;
}

std::vector<OutputConfig> SettingsStore::DefaultConfigs()
{
	std::vector<OutputConfig> defaults;
	for (auto &preset : GetPlatformPresets()) {
		if (preset.id == PlatformId::Custom)
			continue;
		OutputConfig c;
		c.platform = preset.id;
		c.name = preset.name;
		c.enabled = false;
		c.server = preset.defaultServer;
		c.videoBitrateKbps = preset.defaultVideoBitrateKbps;
		c.audioBitrateKbps = preset.defaultAudioBitrateKbps;
		defaults.push_back(std::move(c));
	}
	return defaults;
}

static const char *PlatformIdToStr(PlatformId id)
{
	switch (id) {
	case PlatformId::YouTube:
		return "youtube";
	case PlatformId::Twitch:
		return "twitch";
	case PlatformId::Kick:
		return "kick";
	default:
		return "custom";
	}
}

static PlatformId PlatformIdFromStr(const char *s)
{
	if (!s)
		return PlatformId::Custom;
	if (strcmp(s, "youtube") == 0)
		return PlatformId::YouTube;
	if (strcmp(s, "twitch") == 0)
		return PlatformId::Twitch;
	if (strcmp(s, "kick") == 0)
		return PlatformId::Kick;
	return PlatformId::Custom;
}

static const char *OrientationToStr(CanvasOrientation o)
{
	return o == CanvasOrientation::Vertical ? "vertical" : "horizontal";
}

static CanvasOrientation OrientationFromStr(const char *s)
{
	return (s && strcmp(s, "vertical") == 0) ? CanvasOrientation::Vertical : CanvasOrientation::Horizontal;
}

std::vector<OutputConfig> SettingsStore::Load()
{
	std::string path = ConfigFilePath();
	if (path.empty() || !os_file_exists(path.c_str()))
		return DefaultConfigs();

	obs_data_t *root = obs_data_create_from_json_file(path.c_str());
	if (!root)
		return DefaultConfigs();

	obs_data_array_t *arr = obs_data_get_array(root, "targets");
	std::vector<OutputConfig> configs;

	if (arr) {
		size_t count = obs_data_array_count(arr);
		for (size_t i = 0; i < count; i++) {
			obs_data_t *item = obs_data_array_item(arr, i);
			OutputConfig c;
			c.platform = PlatformIdFromStr(obs_data_get_string(item, "platform"));
			c.name = obs_data_get_string(item, "name");
			c.enabled = obs_data_get_bool(item, "enabled");
			c.server = obs_data_get_string(item, "server");
			c.channelName = obs_data_get_string(item, "channel_name");
			c.orientation = OrientationFromStr(obs_data_get_string(item, "orientation"));

			// stream_key_enc is DPAPI-encrypted (current format); stream_key
			// is the old plaintext field, read as a one-time migration for
			// configs saved before encryption was added. If DPAPI decryption
			// fails (e.g. config copied to another machine/user), the key
			// comes back empty and the dock flags that target as unconfigured
			// rather than streaming with a garbled key.
			std::string encKey = obs_data_get_string(item, "stream_key_enc");
			if (!encKey.empty()) {
				c.streamKey = SecureString::Decrypt(encKey);
			} else {
				c.streamKey = obs_data_get_string(item, "stream_key");
			}
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
			configs.push_back(std::move(c));
			obs_data_release(item);
		}
		obs_data_array_release(arr);
	}

	obs_data_release(root);

	if (configs.empty())
		return DefaultConfigs();
	return configs;
}

void SettingsStore::Save(const std::vector<OutputConfig> &configs)
{
	std::string path = ConfigFilePath();
	if (path.empty())
		return;

	// Read-modify-write rather than starting from a blank root: this file
	// also holds fields Save() doesn't know about (youtube_api_key_enc),
	// and a fresh obs_data_create() here would silently wipe those out on
	// the next target edit.
	obs_data_t *root = obs_data_create_from_json_file(path.c_str());
	if (!root)
		root = obs_data_create();
	obs_data_array_t *arr = obs_data_array_create();

	for (auto &c : configs) {
		obs_data_t *item = obs_data_create();
		obs_data_set_string(item, "platform", PlatformIdToStr(c.platform));
		obs_data_set_string(item, "name", c.name.c_str());
		obs_data_set_bool(item, "enabled", c.enabled);
		obs_data_set_string(item, "server", c.server.c_str());
		obs_data_set_string(item, "channel_name", c.channelName.c_str());
		obs_data_set_string(item, "orientation", OrientationToStr(c.orientation));
		obs_data_set_string(item, "stream_key_enc", SecureString::Encrypt(c.streamKey).c_str());
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
		obs_data_array_push_back(arr, item);
		obs_data_release(item);
	}

	obs_data_set_array(root, "targets", arr);
	obs_data_array_release(arr);

	obs_data_save_json_safe(root, path.c_str(), "tmp", "bak");
	obs_data_release(root);
}

std::vector<std::pair<std::string, std::vector<std::string>>> SettingsStore::LoadPresets()
{
	std::vector<std::pair<std::string, std::vector<std::string>>> out;

	std::string path = ConfigFilePath();
	if (path.empty() || !os_file_exists(path.c_str()))
		return out;

	obs_data_t *root = obs_data_create_from_json_file(path.c_str());
	if (!root)
		return out;

	obs_data_array_t *presets = obs_data_get_array(root, "presets");
	if (presets) {
		size_t count = obs_data_array_count(presets);
		for (size_t i = 0; i < count; i++) {
			obs_data_t *item = obs_data_array_item(presets, i);
			std::string name = obs_data_get_string(item, "name");

			std::vector<std::string> enabled;
			obs_data_array_t *ids = obs_data_get_array(item, "enabled");
			if (ids) {
				size_t idCount = obs_data_array_count(ids);
				for (size_t j = 0; j < idCount; j++) {
					obs_data_t *idItem = obs_data_array_item(ids, j);
					enabled.push_back(obs_data_get_string(idItem, "id"));
					obs_data_release(idItem);
				}
				obs_data_array_release(ids);
			}

			out.push_back({name, enabled});
			obs_data_release(item);
		}
		obs_data_array_release(presets);
	}

	obs_data_release(root);
	return out;
}

void SettingsStore::SavePresets(const std::vector<std::pair<std::string, std::vector<std::string>>> &presets)
{
	std::string path = ConfigFilePath();
	if (path.empty())
		return;

	obs_data_t *root = obs_data_create_from_json_file(path.c_str());
	if (!root)
		root = obs_data_create();

	obs_data_array_t *arr = obs_data_array_create();
	for (auto &[name, enabled] : presets) {
		obs_data_t *item = obs_data_create();
		obs_data_set_string(item, "name", name.c_str());

		obs_data_array_t *ids = obs_data_array_create();
		for (auto &id : enabled) {
			obs_data_t *idItem = obs_data_create();
			obs_data_set_string(idItem, "id", id.c_str());
			obs_data_array_push_back(ids, idItem);
			obs_data_release(idItem);
		}
		obs_data_set_array(item, "enabled", ids);
		obs_data_array_release(ids);

		obs_data_array_push_back(arr, item);
		obs_data_release(item);
	}
	obs_data_set_array(root, "presets", arr);
	obs_data_array_release(arr);

	obs_data_save_json_safe(root, path.c_str(), "tmp", "bak");
	obs_data_release(root);
}
