#pragma once

#include <vector>
#include <memory>
#include <mutex>
#include <functional>

#include "output-target.h"
#include "settings-store.h"

// Top-level coordinator: owns one OutputTarget per configured platform,
// starts/stops them together, and reflects their combined state so the UI
// dock has one place to poll. Intentionally decoupled from OBS's built-in
// "Start Streaming" output -- this plugin adds *extra* destinations
// alongside (or instead of) it, controlled from its own dock.
class MultistreamManager {
public:
	static MultistreamManager &Get();

	void LoadFromStore();
	void SaveToStore();

	std::vector<OutputConfig> GetConfigs() const;
	void SetConfigs(std::vector<OutputConfig> configs);

	void StartAll();
	void StopAll(bool immediate = false);
	bool IsAnyActive() const;

	using TargetsChangedCallback = std::function<void()>;
	void SetTargetsChangedCallback(TargetsChangedCallback cb) { onChanged_ = std::move(cb); }

	// Snapshot of per-target status for the UI (thread-safe copy, no live refs).
	struct TargetStatus {
		std::string name;
		OutputState state;
		std::string message;
		uint64_t bytesSent;
		double droppedPercent;
		int bitrateKbps;
		int64_t liveSeconds;
	};
	std::vector<TargetStatus> GetStatusSnapshot() const;

private:
	MultistreamManager() = default;

	mutable std::mutex mutex_;
	std::vector<std::unique_ptr<OutputTarget>> targets_;
	TargetsChangedCallback onChanged_;
};
