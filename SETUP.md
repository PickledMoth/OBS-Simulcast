# Setup Guide

This covers getting your stream URL + key from each platform and entering
them into the Multistream dock. (No screenshots here — write access to
each platform's current dashboard layout isn't something that can be
verified from this environment, and dashboards change often enough that a
stale screenshot would be worse than none. The menu paths below are
current as of writing; if a platform has moved something, their own "Go
Live" / stream setup help page is the source of truth.)

## Twitch

1. Go to the [Twitch Creator Dashboard](https://dashboard.twitch.tv/) →
   **Settings → Stream**.
2. Under "Primary Stream key", click **Copy**. Treat this like a password —
   anyone with it can stream to your channel.
3. In the Multistream dock, click the 🔑 button on the Twitch row and paste
   it in.
4. The Twitch row's 🔗 (URL) is pre-filled with `rtmp://live.twitch.tv/app`,
   which is Twitch's standard ingest endpoint — you shouldn't need to
   change it unless Twitch tells you to use a regional ingest server for
   lower latency (their dashboard lists alternatives under "Server").

## YouTube

1. Go to [YouTube Studio](https://studio.youtube.com/) → **Create → Go
   Live** (or **Content → Live** if you've streamed before).
2. Under **Stream settings → Stream key**, click **Copy**.
3. Paste it into the 🔑 button on the YouTube row in the dock.
4. The 🔗 (URL) defaults to `rtmp://a.rtmp.youtube.com/live2`, YouTube's
   standard endpoint. YouTube also shows a "Backup ingestion URL" on that
   same page — only use it if the primary one is having issues.
5. YouTube requires you to have an active "stream" configured in Studio
   (even a basic scheduled one) before it will accept an incoming RTMP
   connection — if OBS shows "Connecting" indefinitely, check Studio to
   make sure a live session is actually set up there first.

## Kick

1. Go to the Kick Creator Dashboard → **Stream Settings** (Kick's exact
   menu wording may vary — look for "Stream key" or "RTMP settings").
2. Copy both the **server URL** and **stream key** shown there. Kick's
   ingest infrastructure is newer than Twitch/YouTube's and its endpoint
   isn't a stable, long-documented constant the way theirs are — the
   Multistream dock intentionally ships with the Kick URL field blank
   rather than guessing, so you must paste in whatever URL Kick's
   dashboard currently shows.
3. Enter both into the Kick row's 🔗 and 🔑 buttons.

## After entering keys

- Check the **On** checkbox for each platform you want to stream to.
- Everything saves automatically — no Save button, no extra step.
- Click **Go Live (all enabled)** in the dock, or just hit OBS's normal
  **Start Streaming** button (they're linked: starting/stopping one starts
  or stops the other).
- Watch the **Status** column: `Connecting…` → `Live · N kb/s` means it
  worked. If a platform sits on `Error`, check Help → Log Files → View
  Current Log for a line like `[OBS-Simulcast] <platform>: error - ...`
  with the specific failure reason.

## Streaming to more than the built-in three

Click **+ Add Target** in the top row to add another RTMP(S) destination
(Facebook, a private relay, a second Twitch/YouTube account, etc.).
Double-click its name to rename it, then fill in URL/key the same way as
the built-in platforms.

## Chat dock

The **Multistream Chat** dock (View → Docks → Multistream Chat) merges
Twitch and YouTube chat into one feed, tagged by platform.

- **Twitch**: click the chat dock's Twitch button and enter your channel
  name (or paste a twitch.tv/popout/embed chat URL — it's parsed either
  way). No login needed, it connects anonymously and read-only.
- **YouTube**: click the YouTube button and enter your channel handle
  (`@yourname`), channel URL, or raw channel ID — no API key or Google
  account setup needed. It auto-detects your current live broadcast once
  you start streaming (checking only while you're actually live), no need
  to re-enter a video URL each stream.
- Use the **filter** box to only show messages containing certain text, and
  the **highlight** box to have matching messages (e.g. your own username)
  stand out with a background color.
- Kick chat isn't supported — see Known limitations in the README.

## Vertical streaming (TikTok, YouTube Shorts, Instagram Reels)

By default every target encodes from your main OBS scene, the same 16:9
canvas OBS's own preview shows. To stream a portrait 9:16 feed to a
vertical-first platform instead:

1. Open ⚙ **Encoder Settings** on the target you want to make vertical, and
   set **Orientation** to "Vertical (9:16)".
2. Compose the actual vertical layout yourself in OBS's vertical canvas
   (accessible from OBS's own scene/canvas switcher once a vertical target
   is enabled) — the plugin does **not** auto-crop or letterbox your main
   scene into 9:16. Auto-cropping tends to cut off your cam or important
   game UI, and letterboxing looks unfinished, so vertical output is a
   scene you build on purpose, the same way you built your main one.
3. All vertical targets share one 1080x1920 canvas — if you enable vertical
   orientation on more than one target (e.g. TikTok and Shorts at once),
   they all stream the same vertical composition.
4. Horizontal targets are completely unaffected — you can run a normal
   16:9 stream to Twitch alongside a vertical one to TikTok in the same
   session.

## Presets

If you regularly stream to different subsets of platforms (e.g. "Twitch
only" vs. "everywhere"), enable the platforms you want, then click the
save icon next to the preset dropdown and give it a name. Pick it from the
dropdown and click the apply (▶) icon to flip enabled/disabled states to
match — it only touches the On/Off checkboxes, not your saved URLs/keys.

## Backup / restore (export / import)

Click the ↑ (export) icon to save all your targets, presets, and the
YouTube channel/API settings to a single encrypted file — you'll be asked
for a password. Losing that password means the backup is unrecoverable
(there's no reset), so store it somewhere safe. Click the ↓ (import) icon
and enter the same password to restore it, e.g. after a fresh install or
on a new machine. Stream keys inside the export are protected by the
backup's password, not by Windows DPAPI, so it's portable across machines
unlike the live config file.

## Bitrate guidance

Each platform has a different practical ceiling for what its ingest will
accept without transcoding/rejecting your stream:

| Platform | Recommended max video bitrate |
|----------|-------------------------------|
| Twitch   | ~8,500 kb/s |
| Kick     | ~8,000 kb/s (unofficial estimate — Kick doesn't publish this as clearly as Twitch/YouTube; treat as a starting point, not a hard number) |
| YouTube  | Much higher (tens of Mb/s) — rarely the limiting factor |

The dock will flag the bitrate field red if you exceed a platform's known
ceiling, but streaming to multiple platforms with *different* ceilings
means your video bitrate is really capped by whichever enabled platform is
most restrictive if you want identical quality everywhere — or set
different bitrates per platform via the ⚙ Encoder Settings on each row.
