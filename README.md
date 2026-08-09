# OBS-Simulcast

Native OBS Studio plugin that streams simultaneously to YouTube, Twitch,
Kick, and any custom RTMP(S) target — each with its own independent encoder
so you can tune bitrate/quality per platform (e.g. keep Twitch under its
~8.5 Mbps ceiling while pushing YouTube higher).

See **[SETUP.md](SETUP.md)** for how to get stream keys from each platform
and use the dock. See **[QA-CHECKLIST.md](QA-CHECKLIST.md)** for the manual
test pass to run before a release.

## How it works

Each enabled platform gets its own `obs_output_t` ("rtmp_output") with its
own video/audio encoder instances, all encoding off the same underlying OBS
canvas/audio bus. This is a local re-encode/duplicate approach: no external
relay server or third-party service required, but CPU/GPU load and upload
bandwidth scale with the number of simultaneous targets. A hardware encoder
(NVENC/QuickSync/AMF) is strongly recommended once you go past two targets
— pick one per-target via the ⚙ button in the dock, if your GPU/drivers
support it (only encoders actually registered in your libobs show up).

Each target reconnects independently with exponential backoff if its
connection drops, so one platform having ingest trouble doesn't take the
others down.

## Features

- Simultaneous streaming to YouTube, Twitch, Kick, and any number of custom
  RTMP(S) targets, each with its own encoder/bitrate/reconnect settings.
- Merged native chat dock: anonymous Twitch IRC + YouTube live chat (via
  YouTube's own web client, no API key needed) in
  one feed, tagged by platform, with filter and mention-highlight boxes.
- Presets (save/apply named "which platforms are enabled" groups),
  password-encrypted config export/import for backups, uptime and
  dropped-frame indicators, system-tray toast notifications on
  disconnect/reconnect, and a Test Connection check on each target's ingest
  host before you go live.
- Stream keys encrypted at rest via Windows DPAPI; config stored per OBS
  profile.

## Installing

Download the latest installer from the [Releases](../../releases) page and
run it — it auto-detects your OBS install location from the registry.

## Compatibility

Built and tested against OBS Studio 32.2.0 on Windows. Plugin DLLs are
sensitive to libobs' ABI — a build made against one OBS version isn't
guaranteed to load cleanly on a meaningfully different one. If you're
sharing the installer with someone else, check their `Help → About` OBS
version first; if it's far from 32.2.0, safest bet is to rebuild against
their version rather than assume the existing DLL will load.

## Known limitations

- Windows only, currently. DPAPI-based stream key encryption and the NSIS
  installer are Windows-specific; the rest of the plugin logic is portable
  C++/Qt but hasn't been built or tested on macOS/Linux.
- No automated tests — see the note at the bottom of `QA-CHECKLIST.md` for
  why, and what's most worth carving out into real unit tests first.
- Kick's default ingest URL ships blank rather than a guessed value (see
  `SETUP.md`), since Kick's infrastructure isn't documented as stably as
  Twitch's/YouTube's.
- No Kick chat integration — Kick doesn't have a documented public anonymous
  chat protocol/API the way Twitch and YouTube do.
- Chat requires OBS's official install to be missing its Qt TLS backend
  plugin, which is why one is bundled and registered at load
  (`data/tls/qschannelbackend.dll`); if chat still fails to connect, check
  that file made it into `<OBS install>\data\obs-plugins\OBS-Simulcast\tls\`.

## License

[PolyForm Noncommercial License 1.0.0](LICENSE) — free to use, modify, and
share for any noncommercial purpose; commercial use (selling it, bundling
it into a paid product/service, etc.) isn't permitted.

## Support

If this plugin saves you the cost of a paid multistreaming service,
consider tipping: **[ko-fi.com/pickledmoth](https://ko-fi.com/pickledmoth)**
