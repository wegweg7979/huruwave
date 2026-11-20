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
Version:        0.1.0
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

# Runtime, not linked. mpv is the audio engine over its JSON IPC socket, and it
# resolves YouTube URLs through yt-dlp itself, so both are hard requirements: the
# app is useless without them and neither is a build dependency. The Wayland
# platform plugin is not optional on a Hyprland-only image either.
Requires:       mpv
Requires:       yt-dlp
Requires:       qt6-qtbase
Requires:       qt6-qtdeclarative
Requires:       qt6-qtwayland
# cava is the analyser behind the spectrum bars. Without it the panel falls back
# to the synthetic animation rather than going blank, so it is a Recommends and
# not a Requires (the hurunagus images install it themselves).
Recommends:     cava

%description
huruwave plays music from three places in one window that follows the desktop
theme: YouTube Music (search, your own playlists, queue, scrub, shuffle, repeat),
the Apple Music catalog (search and Apple's 30-second previews) and local files
dropped on the window.

The audio engine is mpv over its JSON IPC socket and the spectrum bars are drawn
from cava, so the player carries no DSP of its own. Everything it knows about its
two catalogs — including which cookie source the session came from — is readable
from a shell as well as from the window (`huruwave --search ... --no-window`,
`huruwave --playlist LM --no-window`, `huruwave --cookies-status`).

%prep
%autosetup -n %{name}-%{version}

%build
%cmake -DCMAKE_BUILD_TYPE=Release
%cmake_build

%install
install -D -m 0755 %{_vpath_builddir}/huruwave %{buildroot}%{_bindir}/huruwave
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
%{_datadir}/icons/hicolor/scalable/apps/huruwave.svg
%{_datadir}/applications/huruwave.desktop

%changelog
* Fri Nov 20 2026 wegweg7979 <diode-crumble-0h@icloud.com> - 0.1.0-1
- First release: YouTube Music search and playlists, the Apple Music catalog with
  its 30-second previews, the local library, cava-backed spectrum bars, and the
  headless commands (--search / --playlist / --cookies-status).
