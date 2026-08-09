#include "chat-url.h"

namespace ChatUrl {

std::string ExtractYouTubeVideoId(const std::string &input)
{
	auto afterMarker = [&](const std::string &marker) -> std::string {
		size_t pos = input.find(marker);
		if (pos == std::string::npos)
			return {};
		std::string rest = input.substr(pos + marker.size());
		size_t end = rest.find_first_of("&?#");
		return end == std::string::npos ? rest : rest.substr(0, end);
	};

	if (std::string id = afterMarker("youtu.be/"); !id.empty())
		return id;
	if (std::string id = afterMarker("/live/"); !id.empty())
		return id;
	// Generic "v=" query param -- covers watch?v=ID, and also URL shapes
	// like live_chat?is_popout=1&v=ID (the actual popout chat link people
	// tend to paste here, same as they would for Twitch's popout URL) that
	// don't literally contain "watch?v=".
	if (std::string id = afterMarker("?v="); !id.empty())
		return id;
	if (std::string id = afterMarker("&v="); !id.empty())
		return id;
	return input;
}

std::string Build(PlatformId platform, const std::string &channelOrVideo)
{
	std::string v = channelOrVideo;
	// trim whitespace
	size_t start = v.find_first_not_of(" \t\r\n");
	size_t end = v.find_last_not_of(" \t\r\n");
	v = (start == std::string::npos) ? "" : v.substr(start, end - start + 1);
	if (v.empty())
		return {};

	switch (platform) {
	case PlatformId::Twitch:
		// "parent" is required by Twitch's embed and is supposed to match
		// the embedding page's actual domain; OBS's browser dock isn't a
		// real web page with a domain, so this uses "obs" -- the value the
		// OBS community has found Twitch accepts in this context. If it
		// stops working, that's Twitch's embed validation changing, not
		// something fixable on our end without a real hosted page.
		return "https://www.twitch.tv/embed/" + v + "/chat?parent=obs&darkpopout";

	case PlatformId::YouTube: {
		// A bare channel handle/URL/ID (e.g. "@yourname") is valid input
		// for YouTubeChatClient's own auto-discovery, but there's no such
		// thing as a "current live video" embed endpoint -- OBS's Custom
		// Browser Dock needs one specific video's chat, so a handle alone
		// can't produce a working link here. Rather than emit a URL with
		// "@yourname" silently baked in as if it were a video ID, return
		// empty so the dialog shows nothing instead of something broken.
		if (!v.empty() && v[0] == '@')
			return {};
		return "https://www.youtube.com/live_chat?v=" + ExtractYouTubeVideoId(v) + "&embed_domain=obs";
	}

	case PlatformId::Kick:
		// Best-effort guess, not verified: Kick's embeddable-chat endpoint
		// isn't documented as stably as Twitch's/YouTube's (same caveat as
		// its default RTMP URL in platform-presets.cpp). If this doesn't
		// render a chat iframe, Kick has likely changed or never supported
		// this path -- check their current site for an embed option.
		return "https://kick.com/" + v + "/chatroom";

	case PlatformId::Custom:
	default:
		return {};
	}
}

} // namespace ChatUrl
