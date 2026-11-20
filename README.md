# huruwave

An Omarchy-native music player: YouTube Music, the Apple Music catalog, and local
files in one window that follows the desktop theme.

![huruwave playing a track](docs/screenshot.png)

It does what `cliamp` does, as a GUI: search YouTube Music, browse your own
playlists, queue tracks, scrub, shuffle, repeat, and play local files dropped on
the window. It also searches the **Apple Music catalog** and plays Apple's
30-second previews in-place. The look is Omarchy's — the live theme's colours,
JetBrainsMono Nerd Font, sharp corners (or the radius Hyprland actually uses), a
2px accent bar to mark the current row, and no Material chrome.

## What it is built on

- **C++ and Qt Quick**, one binary, the way Omarchy's own apps are built.
- **mpv** over its JSON IPC socket is the audio engine. mpv resolves YouTube
  URLs through `yt-dlp` itself, plays every codec a local library can hold, and
  plays gaplessly — none of which Qt Multimedia does without a lot of glue.
- **cava** for the spectrum bars. It is a ~90 KB console visualiser that reads the
  output device's monitor source and prints bar heights, so the player carries no
  DSP of its own. On Hurunagus it ships in the image; anywhere else, install it
  (`dnf install cava`). Without it the panel falls back to the synthetic
  animation rather than going blank.
- **YouTube Music's own inner API** for search (the `WEB_REMIX` client, the same
  one `ytmusicapi` and `cliamp` use). Measured alternatives: `yt-dlp` with
  `--flat-playlist` on a music search returns album browse-ids with `NA` titles,
  and without it resolves every result — 1m46s for six entries. One POST to the
  inner API returns real songs in about a second.
- **yt-dlp** for playlists and arbitrary URLs, where pagination and cookies matter.
- **Apple's public catalog API** (the iTunes Search API) for Apple Music search,
  artwork and previews. No authentication, no developer token: `artworkUrl100` is
  rewritten to `600x600bb` for full-size art, and `previewUrl` is a real
  30-second AAC file that mpv plays directly.

## Running it

```bash
bin/build        # cmake + make into build/huruwave
bin/test         # the Qt Test binary, offscreen
./build/huruwave # the player

./build/huruwave --search "boards of canada"                        # open with results
./build/huruwave --search "aphex twin" --provider apple             # search Apple Music
./build/huruwave --search "aphex twin" --no-window --json           # headless, for scripts
./build/huruwave --search "aphex twin" --provider apple --no-window # headless, Apple
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

Resolution order: cookies file → this app's config → **Firefox, if a profile
exists** → cliamp's setting. Firefox is in that chain ahead of the inherited
setting deliberately: it reads its cookies straight from a plain sqlite file, so
it needs no Secret Service and cannot hit the keyring failures described below —
signing into `music.youtube.com` once in Firefox is enough, with no configuration
here at all.

`bin/import-cookies` is self-verifying on purpose: it tries every candidate
keyring secret and keeps the one that actually decrypts the profile's cookies,
because stale duplicates of the "Safe Storage" item are common and a wrong key
produces garbage rather than an error. It exists because
`yt-dlp --cookies-from-browser` fails on machines where one Secret Service
collection cannot be enumerated — `yt-dlp` walks every collection to find its key,
and a single stale collection aborts the whole lookup.

## Not in yet

- Apple Music *streaming* — see below for what is possible and why.
- A file picker: drag-and-drop and a path argument work today; the portal picker
  (`org.freedesktop.portal.FileChooser`) is the next step.
- A local-library browser: `Library`/`Album`/`Artist` views over `~/Music`.

## Apple Music: what works, what cannot

Two words in the header switch the search box between **YT MUSIC** and **APPLE
MUSIC**. Apple search needs no sign-in at all — it is Apple's public catalog API.

- **Search, artwork, album and duration**: yes, no auth.
- **Playback**: 30-second previews play in-app, because Apple publishes a preview
  file per track and mpv decodes it (verified: 44100 Hz stereo, a 30.00s running
  time). A row whose preview is missing is not offered as playable — it opens its
  `music.apple.com` page instead, and the panel says "preview · the full track
  opens in your browser" while a preview plays, so a 0:30 timer never looks like a
  bug.
- **Full-track streaming**: not buildable here. It needs FairPlay/Widevine and a
  licensed MusicKit developer token; there is no CDM on the host (the only one on
  a typical box lives inside the Brave flatpak) and a fork will not ship Apple's
  token. Streaming stays in the browser.
- **Your own non-DRM Apple files** (purchased M4A/ALAC) play like any local file.

Album art shown in the screenshot comes from YouTube's public thumbnail URLs.

## License

MIT.
