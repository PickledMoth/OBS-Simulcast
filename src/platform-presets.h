#pragma once

#include <string>
#include <vector>

// Known destinations. "Custom" allows an arbitrary RTMP(S) URL for anything
// else (Facebook, a private relay, etc.) without code changes.
enum class PlatformId { YouTube, Twitch, Kick, Custom };

struct PlatformPreset {
	PlatformId id;
	const char *name;
	const char *defaultServer; // RTMP(S) ingest URL
	int defaultVideoBitrateKbps;
	int defaultAudioBitrateKbps;
	int maxRecommendedBitrateKbps; // platform ingest ceiling, used for UI warnings
};

const std::vector<PlatformPreset> &GetPlatformPresets();
const PlatformPreset *FindPreset(PlatformId id);

// 1080x1920 (9:16) is the de facto standard vertical resolution across
// TikTok, YouTube Shorts, Instagram Reels, and Twitch/Kick's vertical
// surfaces -- unlike horizontal ingest, there's no meaningful per-platform
// variance worth a separate default for, so all vertical targets share this.
constexpr int kVerticalCanvasWidth = 1080;
constexpr int kVerticalCanvasHeight = 1920;
