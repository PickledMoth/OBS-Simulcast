#pragma once

#include <obs.h>
#include <obs.hpp>
#include <mutex>

// Owns the single private obs_canvas_t that all vertical-orientation
// OutputTargets share. Ref-counted rather than created/destroyed per target:
// vertical targets are meant to encode the *same* user-composed vertical
// scene (see CanvasOrientation in output-target.h), not one independent
// canvas each, and creating/tearing down a canvas is too heavy to do per
// reconnect attempt.
class VerticalCanvas {
public:
	static VerticalCanvas &Get();

	// Returns the canvas's video_t, creating the canvas on first call.
	// Must be paired with a Release() when the caller no longer needs it
	// (i.e. when its OutputTarget is destroyed/released).
	video_t *Acquire();
	void Release();

private:
	VerticalCanvas() = default;

	std::mutex mutex_;
	OBSCanvasAutoRelease canvas_;
	int refCount_ = 0;
};
