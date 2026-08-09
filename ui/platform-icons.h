#pragma once

#include <QIcon>
#include <QColor>
#include <QString>

// Small vector-drawn platform glyphs (Twitch's chat-bubble mark, YouTube's
// play button) rather than spelling the platform name out in text -- used
// both as QIcon (status labels) and as inline base64 <img> HTML (chat
// message tags in a QTextBrowser, which can't host a QIcon directly).
QIcon MakeTwitchIcon(int size = 14);
QIcon MakeYouTubeIcon(int size = 14);

// Renders the icon to PNG and base64-encodes it into a self-contained
// <img> tag, sized for inline use next to text in rich-text HTML.
QString IconToImgTag(const QIcon &icon, int size = 14);
