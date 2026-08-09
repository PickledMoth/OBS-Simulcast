#pragma once

#include <obs.h>
#include <obs.hpp>
#include <string>
#include <functional>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>

#include "platform-presets.h"

enum class OutputState { Idle, Connecting, Live, Reconnecting, Error, Stopped };

// Which OBS canvas a target encodes from. Vertical targets pull from a
// second, independently-composed obs_canvas (its own scene collection) built
// for 9:16 platforms like TikTok/Shorts/Reels, rather than a crop/pad of the
// main horizontal canvas -- letterboxing or auto-cropping the main scene
// tends to cut off cams/UI or look wrong, so vertical output is a deliberate
// separate composition the user builds themselves, matching how OBS's own
// multi-canvas support works.
enum class CanvasOrientation { Horizontal, Vertical };

struct OutputConfig {
	PlatformId platform = PlatformId::Custom;
	std::string name; // display name, e.g. "YouTube"
	bool enabled = false;
	std::string server;
	std::string streamKey;

	CanvasOrientation orientation = CanvasOrientation::Horizontal;

	// Public channel identifier, used only to build a chat-embed URL (see
	// ChatUrl.h) -- unlike server/streamKey this is not secret and isn't
	// encrypted at rest. For Twitch/Kick this is the channel login name;
	// for YouTube, chat is tied to a specific live broadcast rather than
	// the channel, so this holds a video ID or a pasted watch/live URL
	// that changes each stream.
	std::string channelName;

	// Independent encoder settings per target so quality/bitrate can be
	// tuned per platform (e.g. Twitch's ~8.5 Mbps ceiling vs YouTube's much
	// higher headroom) without touching the main OBS output.
	std::string videoEncoderId = "obs_x264"; // or "jim_nvenc", "h264_texture_amf", etc.
	int videoBitrateKbps = 6000;
	int keyframeIntervalSec = 2;
	std::string rateControl = "CBR";
	std::string preset = "veryfast";

	std::string audioEncoderId = "ffmpeg_aac";
	int audioBitrateKbps = 160;

	// Reconnect behavior
	bool autoReconnect = true;
	int reconnectMaxRetries = 20;
	int reconnectBaseDelayMs = 1000;
	int reconnectMaxDelayMs = 30000;
};

// Wraps one independent OBS output (its own video+audio encoders, service,
// and rtmp output) so it can encode/send to one platform without affecting
// the others. Multiple OutputTargets run concurrently off the same OBS
// video/audio pipeline (each has its own encoder instance encoding frames
// independently -- this is the "local re-encode/duplicate" approach).
class OutputTarget {
public:
	using StateCallback = std::function<void(const OutputTarget &, OutputState, const std::string &message)>;

	explicit OutputTarget(OutputConfig config);
	~OutputTarget();

	OutputTarget(const OutputTarget &) = delete;
	OutputTarget &operator=(const OutputTarget &) = delete;

	bool Start();
	void Stop(bool immediate = false);

	OutputState State() const { return state_; }
	const OutputConfig &Config() const { return config_; }
	void SetConfig(OutputConfig config);

	void SetStateCallback(StateCallback cb) { stateCallback_ = std::move(cb); }

	// Live stats, polled by the UI for a per-platform status row.
	uint64_t TotalBytesSent() const;
	double DroppedFramePercent() const;
	int CurrentBitrateKbps() const;
	// Seconds since this target last reached Live; 0 if not currently live.
	int64_t LiveSeconds() const;

private:
	bool CreateEncoders();
	bool CreateServiceAndOutput();
	void ReleaseAll();
	void SetState(OutputState s, const std::string &msg = "");
	void ScheduleReconnect();
	void StopReconnectThread();

	static void OnOutputStop(void *data, calldata_t *cd);
	static void OnOutputStart(void *data, calldata_t *cd);

	OutputConfig config_;
	std::atomic<OutputState> state_{OutputState::Idle};
	StateCallback stateCallback_;

	OBSOutputAutoRelease output_;
	OBSServiceAutoRelease service_;
	OBSEncoderAutoRelease videoEncoder_;
	OBSEncoderAutoRelease audioEncoder_;
	bool holdsVerticalCanvasRef_ = false;

	std::thread reconnectThread_;
	std::atomic<bool> stopping_{false};
	std::atomic<int> reconnectAttempt_{0};
	std::atomic<int64_t> liveSinceEpochSec_{0}; // 0 means "not live"
	std::mutex mutex_;
};
