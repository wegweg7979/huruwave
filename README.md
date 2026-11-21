# huruwave

An Omarchy-native music player: YouTube Music, the Apple Music catalog, and local
files in one window that follows the desktop theme.

![huruwave playing a track](docs/screenshot.png)

It does what `cliamp` does, as a GUI: search YouTube Music, browse your own
playlists, queue tracks, scrub, shuffle, repeat, and play local files dropped on
the window. It also searches the **Apple Music catalog** and plays Apple's
30-second previews in-place — or the **full track, in-window**, when the optional
DRM surface is built and the machine has a Widevine CDM and a browser session to
borrow (see below). The look is Omarchy's — the live theme's colours,
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
- **An embedded Chromium (QtWebEngine) for exactly one job**: Apple Music's full,
  DRM-protected tracks, which mpv cannot play because it cannot load a Widevine CDM
  at all. It is the only surface in the app that is not mpv, it is opt-in
  (`-DHURUWAVE_APPLE_WEB=ON`; the package turns it on), and it carries no CDM and no
  credentials: it points the web engine at the CDM a browser on the machine already
  downloaded, and opens Apple's own player with a session borrowed from that browser.

## Running it

```bash
bin/build        # cmake + make into build/huruwave
bin/test         # the Qt Test binary, offscreen
./build/huruwave # the player

./build/huruwave --search "boards of canada"                        # open with results
./build/huruwave --search "aphex twin" --provider apple             # search Apple Music
./build/huruwave --search "aphex twin" --no-window --json           # headless, for scripts
./build/huruwave --search "aphex twin" --provider apple --no-window # headless, Apple
./build/huruwave --playlist LM                                      # your Liked Music
./build/huruwave --cookies-status                                   # is the session real?
./build/huruwave --widevine-status                                  # can it play full Apple tracks?
./build/huruwave --apple-web 'https://music.apple.com/gb/album/...?i=...'  # full track, in-window
./build/huruwave ~/Music/track.flac                                 # play a local file
./build/huruwave https://music.youtube.com/watch?v=4CE29HLQSnk      # play a URL
```

`bin/build` writes into `build/`. On Hurunagus it is already installed from its own
COPR (`wegweg7979/huruwave`) and cava ships in the image, so there is nothing to
build there — just run `huruwave`.

## Shortcuts

| Key | Action |
|---|---|
| `Space` | play / pause |
| `←` `→` | seek 5 seconds |
| `Ctrl` + `←` `→` | previous / next track |
| `Ctrl` + `L` | jump to the search box |
| Click a row | play it, and load that list as the queue |
| `F` | the full track of an Apple preview, in-window (needs the DRM surface) |
| Drop files | queue local files |
| `?` | the shortcut list |
| `Ctrl` + `Q` | quit |

## Account and cookies

Search works anonymously. Your own playlists ("Your Likes", "Watch Later") and
personalised results need your YouTube Music session, which is read from cookies.
Three ways to give it one, and it picks the first that works:

1. **Sign into `music.youtube.com` in Firefox once.** Nothing to configure —
   Firefox cookies live in a plain sqlite file, so no keyring is involved and
   none of the failure modes below can apply. Flatpak Firefox counts too
   (`~/.var/app/org.mozilla.firefox/config/mozilla/firefox/…`), which is where
   this machine's profile lives.
2. `~/.config/huruwave/config.toml` can name a browser explicitly, which is the
   way to choose Brave, Chromium or a specific profile:
   ```toml
   cookies_from = "brave+gnomekeyring:/home/you/.config/BraveSoftware/Brave-Origin/Default"
   ```
3. `bin/import-cookies` exports them to `~/.cache/huruwave/cookies.txt` (mode
   0600), which suits a headed browser with no keyring access.

Resolution order, and the reasoning: this app's config (an explicit choice) → an
imported cookies file **only if it actually holds a session** → Firefox → the
session-less cookies file, if nothing better exists → cliamp's own setting.

The session check in step two is the point: a cookies file that merely *exists*
is not a signed-in session. On this machine the export held 11 visitor cookies
and every `SID`/`__Secure-1PSID` had been dropped, so account views failed while
the app cheerfully said "signed in · cookie file". A file of visitor cookies now
never shadows a working browser, and the sidebar reports what the source can
really do instead of what it is called.

### Checking what it found

```bash
./build/huruwave --cookies-status          # which source, and what it can do (exit 0 if usable)
./build/huruwave --widevine-status         # can this build+machine play full Apple tracks?
./bin/apple-session                        # borrow your browser's Apple session for them
                                           # (installed as huruwave-apple-session)
./build/huruwave --playlist LM --no-window # the live test: Liked Music is account-only
```

`--playlist LM` (or `WL`, or a playlist id) also works in the window, and it is
the honest check: anonymous playlists will load without cookies, but Liked Music
only lists tracks when the session is real.

`bin/import-cookies` is self-verifying on purpose: it tries every candidate
keyring secret and keeps the one that actually decrypts the profile's cookies,
because stale duplicates of the "Safe Storage" item are common and a wrong key
produces garbage rather than an error. It exists because
`yt-dlp --cookies-from-browser` fails on machines where one Secret Service
collection cannot be enumerated — `yt-dlp` walks every collection to find its key,
and a single stale collection aborts the whole lookup. Note what that means in
practice: if the exporter cannot decrypt the *account* cookies, it still writes
out the plain ones and looks successful, which is exactly how a broken export
passes for a working one.

## Window opacity

Opacity is the **compositor's** job on this desktop, not the app's, so huruwave is
opted in from the Hyprland config — one line in `~/.config/hypr/looknfeel.lua`:

```lua
o.transparent_window("huruwave", "0.80 0.76")   -- active / inactive
```

Then reload (`hyprctl reload config-only`) and restart the app, because the rule
applies as the window maps. Commenting the line out takes it back to opaque. This
also means **Appearance → Windows → Window Opacity** governs huruwave like any
other Omarchy app, instead of one app having its own private idea of transparency.

Three things were tried and rejected before settling here, all measured on this
machine:

- **Qt can't do it.** `QWindow::setOpacity` is a no-op on Wayland: the protocol
  has no per-surface alpha.
- **`hyprctl setprop` / `getprop` are stubbed** in this fork — they answer
  "unknown request".
- **`hl.dsp.window.set_prop` cannot be trusted.** It accepts a call for a property
  that cannot exist (`prop = "not_a_prop"`) and for a field that cannot exist
  (`wibble = 1`), and answers `ok` to all of them; screenshots of the window at
  `value = 0.55` were pixel-identical. A call that always says "ok" is not
  evidence, and an app relying on it would be lying to itself.

## Not in yet

- A file picker: drag-and-drop and a path argument work today; the portal picker
  (`org.freedesktop.portal.FileChooser`) is the next step.
- A local-library browser: `Library`/`Album`/`Artist` views over `~/Music`.

## Apple Music: previews, and full tracks

Two words in the header switch the search box between **YT MUSIC** and **APPLE
MUSIC**. Apple search needs no sign-in at all — it is Apple's public catalog API.

- **Search, artwork, album and duration**: yes, no auth.
- **Previews**: 30-second previews play in-app, because Apple publishes a preview
  file per track and mpv decodes it (verified: 44100 Hz stereo, a 30.00s running
  time). A row whose preview is missing is not offered as playable — it opens its
  `music.apple.com` page instead. While a preview plays, the panel says so and says
  which of the two things `F` will do — "F plays the full track here" with the DRM
  surface, "the full track opens in your browser" without it — so a 0:30 running
  time never looks like a bug.
- **Full-track streaming**: plays in-window. Two halves have to exist, and neither
  ships: a **Widevine CDM** (Chromium is the only engine here that can use one — mpv
  cannot load a CDM at all) and an **Apple session**. The CDM is the per-user copy a
  browser on the machine already downloaded, which huruwave finds and points the web
  engine at; the session is borrowed from your browser with `bin/apple-session`
  (yt-dlp does the keyring decryption), the same way the YouTube Music side borrows
  your cookies. Apple's own sign-in inside the panel does **not** work on this Qt —
  its JS dies against Apple's anti-abuse endpoints and the modal spins forever — so
  the surface does not try: it opens already signed in. Press `F` on an Apple row (or
  click the "preview" line), or start it directly:
  `huruwave --apple-web 'https://music.apple.com/gb/album/…?i=…'`. `huruwave
  --widevine-status` says what this build and machine have. With no CDM or no
  session, full tracks open in your browser exactly as before.
- **Your own non-DRM Apple files** (purchased M4A/ALAC) play like any local file.

Album art shown in the screenshot comes from Apple's catalog artwork
(`is1-ssl.mzstatic.com`), the same URLs the search results use.

## License

MIT.
