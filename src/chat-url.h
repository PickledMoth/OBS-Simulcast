#pragma once

#include <string>

#include "platform-presets.h"

// Builds the chat-embed URL for a platform's Custom Browser Dock (see
// SETUP.md). Nothing here is guaranteed stable -- these are third-party
// embed endpoints/params, not something this plugin controls -- so treat
// the output as a starting point the user may need to adjust, not a
// guaranteed-correct link.
namespace ChatUrl {

// channelOrVideo: Twitch/Kick channel login name, or for YouTube a video ID
// (or a full watch/live URL, from which the ID is extracted).
// Returns empty if the platform has no generic chat concept (Custom) or the
// input is empty/unparseable.
std::string Build(PlatformId platform, const std::string &channelOrVideo);

// Pulls a YouTube video ID out of common URL shapes (watch?v=, youtu.be/,
// /live/), or returns the input unchanged if it doesn't match any of them
// -- covers "I just pasted the raw ID" too. Shared by Build() above and
// YouTubeChatClient, which both need to resolve whatever the user typed
// into the same actual video ID.
std::string ExtractYouTubeVideoId(const std::string &input);

} // namespace ChatUrl
