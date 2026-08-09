#include "vertical-canvas.h"
#include "platform-presets.h"

#include <obs-module.h>

VerticalCanvas &VerticalCanvas::Get()
{
	static VerticalCanvas instance;
	return instance;
}

video_t *VerticalCanvas::Acquire()
{
	std::lock_guard<std::mutex> lock(mutex_);

	if (!canvas_) {
		// Base the vertical canvas's timing/format on the main canvas
		// (fps, color format/space/range) so vertical targets encode
		// at the same frame rate as the rest of the stream -- only
		// the resolution differs.
		obs_video_info ovi{};
		obs_get_video_info(&ovi);
		ovi.base_width = kVerticalCanvasWidth;
		ovi.base_height = kVerticalCanvasHeight;
		ovi.output_width = kVerticalCanvasWidth;
		ovi.output_height = kVerticalCanvasHeight;

		// Private: not shown in OBS's canvas dropdown/UI chrome, but the
		// user still composes it like any other canvas via the scene
		// collection the dock points it at (see SETUP.md). ACTIVATE so
		// its sources render, MIX_AUDIO so its audio sources are heard,
		// SCENE_REF so its scene sources stay alive while referenced.
		canvas_ = obs_canvas_create_private("OBS-Simulcast Vertical", &ovi,
						     obs_canvas_flags::ACTIVATE | obs_canvas_flags::MIX_AUDIO |
							     obs_canvas_flags::SCENE_REF);
	}

	refCount_++;
	return canvas_ ? obs_canvas_get_video(canvas_) : nullptr;
}

void VerticalCanvas::Release()
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (refCount_ > 0)
		refCount_--;
	if (refCount_ == 0)
		canvas_ = nullptr;
}
