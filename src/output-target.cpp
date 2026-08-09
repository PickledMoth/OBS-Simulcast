#include "output-target.h"
#include "vertical-canvas.h"

#include <obs-module.h>
#include <util/platform.h>
#include <algorithm>
#include <cmath>
#include <ctime>

OutputTarget::OutputTarget(OutputConfig config) : config_(std::move(config)) {}

OutputTarget::~OutputTarget()
{
	Stop(true);
}

void OutputTarget::SetConfig(OutputConfig config)
{
	std::lock_guard<std::mutex> lock(mutex_);
	config_ = std::move(config);
}

static const char *StateName(OutputState s)
{
	switch (s) {
	case OutputState::Idle:
		return "idle";
	case OutputState::Connecting:
		return "connecting";
	case OutputState::Live:
		return "live";
	case OutputState::Reconnecting:
		return "reconnecting";
	case OutputState::Error:
		return "error";
	case OutputState::Stopped:
		return "stopped";
	default:
		return "unknown";
	}
}

// RTMP embeds the stream key as the tail of the connection URL
// (rtmp://server/app/KEY), so librtmp/FFmpeg error strings -- what
// obs_output_get_last_error() returns, which SetState() logs verbatim --
// can contain it in the clear. Scrub it before it ever reaches blog(),
// since OBS log files get pasted into support requests and GitHub issues
// routinely; leaking a key that way is a realistic, not theoretical, risk.
static std::string RedactKey(const std::string &msg, const std::string &key)
{
	if (key.empty() || msg.empty())
		return msg;
	std::string result = msg;
	size_t pos = 0;
	while ((pos = result.find(key, pos)) != std::string::npos) {
		result.replace(pos, key.size(), "[redacted]");
		pos += 10; // length of "[redacted]", skip past the replacement
	}
	return result;
}

void OutputTarget::SetState(OutputState s, const std::string &msg)
{
	state_ = s;
	liveSinceEpochSec_ = (s == OutputState::Live) ? (int64_t)time(nullptr) : 0;

	// One log line per state transition, tagged by target name -- this is
	// the main thing a streamer (or whoever they ask for help) has to go on
	// after the fact when a platform silently drops mid-stream, since the
	// dock only shows current state, not history.
	int level = s == OutputState::Error ? LOG_WARNING : LOG_INFO;
	std::string safeMsg = RedactKey(msg, config_.streamKey);
	blog(level, "[OBS-Simulcast] %s: %s%s%s", config_.name.c_str(), StateName(s), safeMsg.empty() ? "" : " - ",
	     safeMsg.c_str());

	if (stateCallback_)
		stateCallback_(*this, s, msg);
}

bool OutputTarget::CreateEncoders()
{
	obs_data_t *vSettings = obs_data_create();
	obs_data_set_int(vSettings, "bitrate", config_.videoBitrateKbps);
	obs_data_set_string(vSettings, "rate_control", config_.rateControl.c_str());
	obs_data_set_int(vSettings, "keyint_sec", config_.keyframeIntervalSec);
	if (config_.videoEncoderId == "obs_x264")
		obs_data_set_string(vSettings, "preset", config_.preset.c_str());

	std::string vName = "ms-video-" + config_.name;
	videoEncoder_ = obs_video_encoder_create(config_.videoEncoderId.c_str(), vName.c_str(), vSettings, nullptr);
	obs_data_release(vSettings);
	if (!videoEncoder_) {
		SetState(OutputState::Error, "Failed to create video encoder " + config_.videoEncoderId);
		return false;
	}

	if (config_.orientation == CanvasOrientation::Vertical) {
		video_t *verticalVideo = VerticalCanvas::Get().Acquire();
		holdsVerticalCanvasRef_ = true;
		if (!verticalVideo) {
			SetState(OutputState::Error, "Failed to create vertical canvas");
			return false;
		}
		obs_encoder_set_video(videoEncoder_, verticalVideo);
	} else {
		obs_encoder_set_video(videoEncoder_, obs_get_video());
	}

	obs_data_t *aSettings = obs_data_create();
	obs_data_set_int(aSettings, "bitrate", config_.audioBitrateKbps);
	std::string aName = "ms-audio-" + config_.name;
	audioEncoder_ = obs_audio_encoder_create(config_.audioEncoderId.c_str(), aName.c_str(), aSettings, 0, nullptr);
	obs_data_release(aSettings);
	if (!audioEncoder_) {
		SetState(OutputState::Error, "Failed to create audio encoder " + config_.audioEncoderId);
		return false;
	}
	obs_encoder_set_audio(audioEncoder_, obs_get_audio());

	return true;
}

bool OutputTarget::CreateServiceAndOutput()
{
	obs_data_t *svcSettings = obs_data_create();
	obs_data_set_string(svcSettings, "server", config_.server.c_str());
	obs_data_set_string(svcSettings, "key", config_.streamKey.c_str());

	std::string svcName = "ms-service-" + config_.name;
	service_ = obs_service_create("rtmp_custom", svcName.c_str(), svcSettings, nullptr);
	obs_data_release(svcSettings);
	if (!service_) {
		SetState(OutputState::Error, "Failed to create RTMP service");
		return false;
	}

	obs_data_t *outSettings = obs_data_create();
	std::string outName = "ms-output-" + config_.name;
	output_ = obs_output_create("rtmp_output", outName.c_str(), outSettings, nullptr);
	obs_data_release(outSettings);
	if (!output_) {
		SetState(OutputState::Error, "Failed to create RTMP output");
		return false;
	}

	obs_output_set_video_encoder(output_, videoEncoder_);
	obs_output_set_audio_encoder(output_, audioEncoder_, 0);
	obs_output_set_service(output_, service_);

	signal_handler_t *sh = obs_output_get_signal_handler(output_);
	signal_handler_connect(sh, "stop", OnOutputStop, this);
	signal_handler_connect(sh, "start", OnOutputStart, this);

	return true;
}

void OutputTarget::ReleaseAll()
{
	if (output_) {
		signal_handler_t *sh = obs_output_get_signal_handler(output_);
		signal_handler_disconnect(sh, "stop", OnOutputStop, this);
		signal_handler_disconnect(sh, "start", OnOutputStart, this);
	}
	output_ = nullptr;
	service_ = nullptr;
	videoEncoder_ = nullptr;
	audioEncoder_ = nullptr;

	if (holdsVerticalCanvasRef_) {
		VerticalCanvas::Get().Release();
		holdsVerticalCanvasRef_ = false;
	}
}

bool OutputTarget::Start()
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (!config_.enabled)
		return false;
	if (config_.server.empty() || config_.streamKey.empty()) {
		SetState(OutputState::Error, "Missing server or stream key");
		return false;
	}

	stopping_ = false;
	reconnectAttempt_ = 0;
	SetState(OutputState::Connecting);

	if (!CreateEncoders() || !CreateServiceAndOutput()) {
		ReleaseAll();
		return false;
	}

	if (!obs_output_start(output_)) {
		const char *err = obs_output_get_last_error(output_);
		SetState(OutputState::Error, err ? err : "obs_output_start failed");
		if (config_.autoReconnect)
			ScheduleReconnect();
		return false;
	}

	return true;
}

void OutputTarget::Stop(bool immediate)
{
	stopping_ = true;
	StopReconnectThread();

	std::lock_guard<std::mutex> lock(mutex_);
	if (output_) {
		if (immediate)
			obs_output_force_stop(output_);
		else
			obs_output_stop(output_);
	}
	ReleaseAll();
	SetState(OutputState::Stopped);
}

void OutputTarget::StopReconnectThread()
{
	if (reconnectThread_.joinable())
		reconnectThread_.join();
}

void OutputTarget::ScheduleReconnect()
{
	if (stopping_ || !config_.autoReconnect)
		return;
	if (reconnectAttempt_ >= config_.reconnectMaxRetries) {
		SetState(OutputState::Error, "Max reconnect attempts reached");
		return;
	}

	StopReconnectThread();

	int attempt = reconnectAttempt_++;
	int delay = std::min(config_.reconnectBaseDelayMs * (1 << std::min(attempt, 10)), config_.reconnectMaxDelayMs);

	SetState(OutputState::Reconnecting, "Retrying in " + std::to_string(delay / 1000) + "s (attempt " +
					       std::to_string(attempt + 1) + "/" + std::to_string(config_.reconnectMaxRetries) +
					       ")");

	reconnectThread_ = std::thread([this, delay]() {
		int waited = 0;
		while (waited < delay && !stopping_) {
			os_sleep_ms(100);
			waited += 100;
		}
		if (stopping_)
			return;

		std::lock_guard<std::mutex> lock(mutex_);
		if (stopping_)
			return;
		ReleaseAll();
		if (!CreateEncoders() || !CreateServiceAndOutput()) {
			ScheduleReconnect();
			return;
		}
		SetState(OutputState::Connecting);
		if (!obs_output_start(output_)) {
			const char *err = obs_output_get_last_error(output_);
			SetState(OutputState::Error, err ? err : "reconnect failed");
			ScheduleReconnect();
		}
	});
}

void OutputTarget::OnOutputStart(void *data, calldata_t *)
{
	auto *self = static_cast<OutputTarget *>(data);
	self->reconnectAttempt_ = 0;
	self->SetState(OutputState::Live);
}

void OutputTarget::OnOutputStop(void *data, calldata_t *cd)
{
	auto *self = static_cast<OutputTarget *>(data);
	int64_t code = 0;
	calldata_get_int(cd, "code", &code);

	if (self->stopping_) {
		self->SetState(OutputState::Stopped);
		return;
	}

	// Any non-explicit stop while we're supposed to be live is treated as a
	// dropped connection and retried with backoff instead of surfacing as a
	// hard failure -- streamers should not have to notice or manually
	// restart an individual platform mid-broadcast.
	std::string msg = code != OBS_OUTPUT_SUCCESS ? "Disconnected (code " + std::to_string(code) + ")" : "Disconnected";
	self->SetState(OutputState::Reconnecting, msg);
	if (self->config_.autoReconnect)
		self->ScheduleReconnect();
}

uint64_t OutputTarget::TotalBytesSent() const
{
	return output_ ? obs_output_get_total_bytes(output_) : 0;
}

double OutputTarget::DroppedFramePercent() const
{
	if (!output_)
		return 0.0;
	int total = obs_output_get_total_frames(output_);
	int dropped = obs_output_get_frames_dropped(output_);
	if (total <= 0)
		return 0.0;
	return (double)dropped / (double)total * 100.0;
}

int OutputTarget::CurrentBitrateKbps() const
{
	return config_.videoBitrateKbps + config_.audioBitrateKbps;
}

int64_t OutputTarget::LiveSeconds() const
{
	int64_t since = liveSinceEpochSec_;
	if (since <= 0)
		return 0;
	int64_t now = (int64_t)time(nullptr);
	return now > since ? now - since : 0;
}
