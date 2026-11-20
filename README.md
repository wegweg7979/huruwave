# huruwave

An Omarchy-native music player: YouTube Music, artwork, and local files in one
window that follows the desktop theme.

![huruwave playing a track](docs/screenshot.png)

It does what `cliamp` does, as a GUI: search YouTube Music, browse your own
playlists, queue tracks, scrub, shuffle, repeat, and play local files dropped on
the window. The look is Omarchy's — the live theme's colours, JetBrainsMono Nerd
Font, sharp corners (or the radius Hyprland actually uses), a 2px accent bar to
mark the current row, and no Material chrome.

## What it is built on

- **C++ and Qt Quick**, one binary, the way Omarchy's own apps are built.
- **mpv** over its JSON IPC socket is the audio engine. mpv resolves YouTube
  URLs through `yt-dlp` itself, plays every codec a local library can hold, and
  plays gaplessly — none of which Qt Multimedia does without a lot of glue.
- **YouTube Music's own inner API** for search (the `WEB_REMIX` client, the same
  one `ytmusicapi` and `cliamp` use). Measured alternatives: `yt-dlp` with
  `--flat-playlist` on a music search returns album browse-ids with `NA` titles,
  and without it resolves every result — 1m46s for six entries. One POST to the
  inner API returns real songs in about a second.
- **yt-dlp** for playlists and arbitrary URLs, where pagination and cookies matter.

## Running it

```bash
bin/build        # cmake + make into build/huruwave
bin/test         # the Qt Test binary, offscreen
./build/huruwave # the player

./build/huruwave --search "boards of canada"                        # open with results
./build/huruwave --search "aphex twin" --no-window --json           # headless, for scripts
./build/huruwave ~/Music/track.flac                                 # play a local file
./build/huruwave https://music.youtube.com/watch?v=4CE29HLQSnk      # play a URL
```

## Shortcuts

| Key | Action |
|---|---|
| `Space` | play / pause |
| `←` `→` | seek 5 seconds |
| `Ctrl` + `←` `→` | previous / next track |
| `Ctrl` + `L` | jump to the search box |
| Click a row | play it, and load that list as the queue |
| Drop files | queue local files |
| `?` | the shortcut list |
| `Ctrl` + `Q` | quit |

## Account and cookies

Search works anonymously. Your own playlists ("Your Likes", "Watch Later") and
personalised results need your YouTube Music session, which is read from cookies:

1. `bin/import-cookies` exports them from a Chromium-family browser to
   `~/.cache/huruwave/cookies.txt` (mode 0600), or
2. `~/.config/huruwave/config.toml` can name a browser instead:
   ```toml
   cookies_from = "brave+gnomekeyring:/home/you/.config/BraveSoftware/Brave-Origin/Default"
   ```
   and cliamp's own `cookies_from` is inherited if this app has no config yet.

`bin/import-cookies` is self-verifying on purpose: it tries every candidate
keyring secret and keeps the one that actually decrypts the profile's cookies,
because stale duplicates of the "Safe Storage" item are common and a wrong key
produces garbage rather than an error. It exists because
`yt-dlp --cookies-from-browser` fails on machines where one Secret Service
collection cannot be enumerated — `yt-dlp` walks every collection to find its key,
and a single stale collection aborts the whole lookup.

## Not in yet

- Apple Music *streaming* (see below) — catalog search and artwork are planned.
- A file picker: drag-and-drop and a path argument work today; the portal picker
  (`org.freedesktop.portal.FileChooser`) is the next step.
- The visualiser is an activity indicator, **not** a spectrum analyser: the bars
  are driven by playback state, not by the audio signal. Tapping mpv's output for
  real bands is a separate piece of work.
- A local-library browser: `Library`/`Album`/`Artist` views over `~/Music`.

## Why Apple Music is a separate problem

Apple Music streaming cannot be built into a Qt app: there is no Widevine CDM on
the host (the only one on a typical box lives inside the Brave flatpak),
`qtwebengine` would still need that CDM, and Apple's MusicKit requires a licensed
developer token — which will not be baked into a fork. What *is* buildable, and
planned: Apple Music **catalog search and artwork** through Apple's public API
(no auth), and playback of your own non-DRM Apple files (purchased M4A/ALAC).
Streaming stays in the browser.

Album art shown in the screenshot comes from YouTube's public thumbnail URLs.

## License

MIT.
