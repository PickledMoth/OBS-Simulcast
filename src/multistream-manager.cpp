#include "multistream-manager.h"

#include <algorithm>

MultistreamManager &MultistreamManager::Get()
{
	static MultistreamManager instance;
	return instance;
}

void MultistreamManager::LoadFromStore()
{
	auto configs = SettingsStore::Get().Load();
	SetConfigs(std::move(configs));
}

void MultistreamManager::SaveToStore()
{
	SettingsStore::Get().Save(GetConfigs());
}

std::vector<OutputConfig> MultistreamManager::GetConfigs() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	std::vector<OutputConfig> out;
	out.reserve(targets_.size());
	for (auto &t : targets_)
		out.push_back(t->Config());
	return out;
}

void MultistreamManager::SetConfigs(std::vector<OutputConfig> configs)
{
	std::lock_guard<std::mutex> lock(mutex_);

	// Reuse existing OutputTarget objects (matched by platform + name)
	// instead of destroying and recreating everything on every edit. Two
	// reasons this matters, not just one:
	//
	// 1. The old wholesale-replace approach stopped and restarted *every*
	//    target whenever *any* field on *any* row changed -- editing Kick's
	//    bitrate would visibly interrupt an already-live YouTube stream.
	// 2. That approach also called StartAll()/StopAll() (which each take
	//    this same mutex_) from inside this function while already holding
	//    it, via a wasActive-restart path -- a guaranteed deadlock on this
	//    non-recursive mutex the moment any target was live when an edit
	//    came in, freezing the whole OBS UI thread.
	//
	// In-place updates sidestep both: unrelated live targets are left
	// running untouched, and nothing here re-enters the lock.
	std::vector<std::unique_ptr<OutputTarget>> newTargets;
	newTargets.reserve(configs.size());

	for (auto &c : configs) {
		auto it = std::find_if(targets_.begin(), targets_.end(), [&](const std::unique_ptr<OutputTarget> &t) {
			return t && t->Config().platform == c.platform && t->Config().name == c.name;
		});

		if (it != targets_.end() && *it) {
			OutputTarget *t = it->get();
			t->SetConfig(c);
			// Config changes to a live target only take effect on its next
			// (re)start -- except "disabled", which should stop it right
			// away rather than leave a stream running that the UI now
			// shows as off.
			if (!c.enabled) {
				OutputState s = t->State();
				if (s == OutputState::Live || s == OutputState::Connecting || s == OutputState::Reconnecting)
					t->Stop();
			}
			newTargets.push_back(std::move(*it));
		} else {
			newTargets.push_back(std::make_unique<OutputTarget>(c));
		}
	}

	targets_ = std::move(newTargets);

	if (onChanged_)
		onChanged_();
}

void MultistreamManager::StartAll()
{
	std::lock_guard<std::mutex> lock(mutex_);
	for (auto &t : targets_) {
		if (t->Config().enabled)
			t->Start();
	}
}

void MultistreamManager::StopAll(bool immediate)
{
	std::lock_guard<std::mutex> lock(mutex_);
	for (auto &t : targets_)
		t->Stop(immediate);
}

bool MultistreamManager::IsAnyActive() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	for (auto &t : targets_) {
		auto s = t->State();
		if (s == OutputState::Live || s == OutputState::Connecting || s == OutputState::Reconnecting)
			return true;
	}
	return false;
}

std::vector<MultistreamManager::TargetStatus> MultistreamManager::GetStatusSnapshot() const
{
	std::lock_guard<std::mutex> lock(mutex_);
	std::vector<TargetStatus> out;
	out.reserve(targets_.size());
	for (auto &t : targets_) {
		out.push_back({t->Config().name, t->State(), "", t->TotalBytesSent(), t->DroppedFramePercent(),
			       t->CurrentBitrateKbps(), t->LiveSeconds()});
	}
	return out;
}
