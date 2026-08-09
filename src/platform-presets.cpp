#include "platform-presets.h"

// Default ingest URLs are provided as a convenience starting point only.
// Platforms change/rotate these periodically -- the UI always lets the user
// override with whatever server + stream key their dashboard currently shows.
//
// YouTube's and Twitch's RTMP ingest hostnames are long-standing, widely
// documented constants and safe to default. Kick's ingest infrastructure is
// newer and far less consistently documented publicly, so rather than ship
// a guessed hostname that might silently fail, its default server is left
// blank -- the dock UI flags it red/unconfigured until the user pastes the
// current one from their Kick dashboard (Creator Dashboard > Stream Settings).
static const std::vector<PlatformPreset> kPresets = {
	{PlatformId::YouTube, "YouTube", "rtmp://a.rtmp.youtube.com/live2", 6000, 160, 51000},
	{PlatformId::Twitch, "Twitch", "rtmp://live.twitch.tv/app", 6000, 160, 8500},
	{PlatformId::Kick, "Kick", "", 6000, 160, 8000},
	{PlatformId::Custom, "Custom RTMP", "", 4500, 160, 0},
};

const std::vector<PlatformPreset> &GetPlatformPresets()
{
	return kPresets;
}

const PlatformPreset *FindPreset(PlatformId id)
{
	for (auto &p : kPresets) {
		if (p.id == id)
			return &p;
	}
	return nullptr;
}
