# huruwave.spec — an Omarchy-native music player: YouTube Music, the Apple Music
# catalog and local files in one window that follows the desktop theme.
#
# huruwave (https://github.com/wegweg7979/huruwave, MIT) is a plain C++17
# CMake/Qt6 app: `bin/build` is the build, `pkgbuild/` the launcher entry and
# icon — the same shape as airgraph.spec in its own COPR.
#
# COPR-safe: every dependency is a BuildRequires and the build needs no network.
# Playback resolves YouTube URLs through yt-dlp (and mpv) at *runtime*, which is
# why those are Requires and not BuildRequires.

# The release build produces no debug info, so an empty debuginfo subpackage
# would fail the build.
%global debug_package %{nil}

Name:           huruwave
Version:        0.2.1
Release:        1%{?dist}
Summary:        An Omarchy-native music player: YouTube Music, Apple Music search and local files

License:        MIT
URL:            https://github.com/wegweg7979/huruwave

# GitHub's tag archive unpacks to huruwave-<version>/. bin/spec-sources fetches
# it and rewrites huruwave.spec.sources with its sha256 — after the tag exists,
# which is why the hash is a step of its own and not part of the version bump.
Source0:        %{url}/archive/refs/tags/v%{version}/%{name}-%{version}.tar.gz

# Compiled for x86_64 (the only arch the hurunagus images target right now).
ExclusiveArch:  x86_64

BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  make
BuildRequires:  desktop-file-utils
# CMakeLists asks for Qt6 Core/Gui/Qml/Quick/QuickControls2/Network/Test. Qt Quick
# Controls — including the Material style the window sets with QQuickStyle — ships
# inside qt6-qtdeclarative on Fedora 44 and 45; there is no qt6-qtquickcontrols2
# package any more. QtTest comes with qt6-qtbase-devel.
BuildRequires:  qt6-qtbase-devel
BuildRequires:  qt6-qtdeclarative-devel
# The in-app Apple Music DRM surface (HURUWAVE_APPLE_WEB=ON, below) is QtWebEngine:
# an embedded Chromium, because Apple's full tracks are DRM-protected and mpv cannot
# use a CDM at all. The CDM itself is NOT a dependency of anything here — it is
# proprietary and licensed per person, so the app only looks for the copy the user's
# own browser already downloaded (see src/widevine.cpp), and says so in
# `huruwave --widevine-status` when there is none.
BuildRequires:  qt6-qtwebengine-devel

# Runtime, not linked. mpv is the audio engine over its JSON IPC socket, and it
# resolves YouTube URLs through yt-dlp itself, so both are hard requirements: the
# app is useless without them and neither is a build dependency. The Wayland
# platform plugin is not optional on a Hyprland-only image either.
Requires:       mpv
Requires:       yt-dlp
Requires:       qt6-qtbase
Requires:       qt6-qtdeclarative
Requires:       qt6-qtwayland
# Linked, not optional: a build with the DRM surface needs the web engine at run
# time, so on a machine without this package the app would not start at all. It is
# what plays Apple's full tracks in-window (the Widevine CDM it also needs is the
# user's own, and is looked for rather than depended on).
Requires:       qt6-qtwebengine
# cava is the analyser behind the spectrum bars. Without it the panel falls back
# to the synthetic animation rather than going blank, so it is a Recommends and
# not a Requires (the hurunagus images install it themselves).
Recommends:     cava

%description
huruwave plays music from three places in one window that follows the desktop
theme: YouTube Music (search, your own playlists, queue, scrub, shuffle, repeat),
the Apple Music catalog (search, Apple's 30-second previews, and — with the DRM
surface built in — the full tracks in-window through Apple's own web player) and
local files
dropped on the window.

The audio engine is mpv over its JSON IPC socket and the spectrum bars are drawn
from cava, so the player carries no DSP of its own. Full Apple Music tracks play
in-window through an embedded Chromium (QtWebEngine): they are DRM-protected, so
that panel borrows a Widevine CDM and an Apple session from the user's own browser
(`huruwave-apple-session` writes the session; neither is ever shipped with this
package). Everything it knows about its
two catalogs — including which cookie source the session came from — is readable
from a shell as well as from the window (`huruwave --search ... --no-window`,
`huruwave --playlist LM --no-window`, `huruwave --cookies-status`,
`huruwave --widevine-status`).

%prep
%autosetup -n %{name}-%{version}

%build
# HURUWAVE_APPLE_WEB=ON is the packaged default: the DRM surface is what the release
# is for, and the BuildRequires above guarantee it can be built. A plain
# `bin/build` leaves it OFF, so a machine without qt6-qtwebengine-devel still gets
# a working player (one that hands full Apple tracks to the browser).
%cmake -DCMAKE_BUILD_TYPE=Release -DHURUWAVE_APPLE_WEB=ON
%cmake_build

%install
install -D -m 0755 %{_vpath_builddir}/huruwave %{buildroot}%{_bindir}/huruwave
# The borrowed-session helper. It is part of the product, not a dev script: without
# it a packaged user cannot get an Apple session into the DRM panel (Apple's own
# sign-in does not work in an embedded browser on this Qt).
install -D -m 0755 bin/apple-session %{buildroot}%{_bindir}/huruwave-apple-session
install -D -m 0644 pkgbuild/huruwave.svg \
  %{buildroot}%{_datadir}/icons/hicolor/scalable/apps/huruwave.svg
install -D -m 0644 pkgbuild/huruwave.desktop \
  %{buildroot}%{_datadir}/applications/huruwave.desktop

%check
desktop-file-validate %{buildroot}%{_datadir}/applications/huruwave.desktop
# Offscreen, no display, no audio device and no network — CMakeLists already pins
# QT_QPA_PLATFORM=offscreen on the test — so a release that fails its own tests
# does not publish.
%ctest

%files
%license LICENSE
%doc README.md
%{_bindir}/huruwave
%{_bindir}/huruwave-apple-session
%{_datadir}/icons/hicolor/scalable/apps/huruwave.svg
%{_datadir}/applications/huruwave.desktop

%changelog
* Sat Nov 21 2026 wegweg7979 <diode-crumble-0h@icloud.com> - 0.2.1-1
- The QML is compiled into the binary at build time (qt_add_qml_module /
  qmlcachegen) instead of being compiled at the first run. Qt caches
  runtime-compiled QML in ~/.cache/<org>/huruwave/qmlcache, keyed by the document
  URL, and it reused a unit an EARLIER build of the app had written: after
  updating to 0.2.0 the Now-Playing panel still drew the pre-0.2.0 line ("the full
  track opens in your browser" instead of the F line) and the Apple DRM surface
  looked absent, while the same binary reported it could play full tracks
  in-window. A unit shipped inside the binary is never resolved through that
  cache.
- src/resources.qrc is gone: the QML module owns the file list.
* Fri Nov 20 2026 wegweg7979 <diode-crumble-0h@icloud.com> - 0.2.0-1
- Full Apple Music tracks now play in-window: a QtWebEngine surface
  (HURUWAVE_APPLE_WEB, on in the package) shows Apple's own web player, using a
  Widevine CDM and an Apple session borrowed per-user from the browser that
  already has them. Neither is shipped. Apple's own sign-in does not work in an
  embedded browser on this Qt, so the surface never tries it.
- New: huruwave-apple-session (borrow the session), --widevine-status (what this
  build and machine have) and --apple-web (open a track in the surface); F opens
  the full track of a preview; the spectrum analyser follows the web surface too.
- Requires qt6-qtwebengine, which is what makes the surface possible.
* Fri Nov 20 2026 wegweg7979 <diode-crumble-0h@icloud.com> - 0.1.0-1
- First release: YouTube Music search and playlists, the Apple Music catalog with
  its 30-second previews, the local library, cava-backed spectrum bars, and the
  headless commands (--search / --playlist / --cookies-status).
