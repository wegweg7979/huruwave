cmake_minimum_required(VERSION 3.21)
project(huruwave LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Network is for artwork/catalog HTTP calls. Deliberately NOT Qt Multimedia:
# playback goes through mpv, so linking it would add a build dependency (and on
# Fedora a -devel package) for nothing.
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Network Test)

# Everything but main.cpp is compiled into a static library so the tests can
# link the same code the app runs.
file(GLOB huruwave_sources CONFIGURE_DEPENDS src/*.cpp src/*.h)
list(REMOVE_ITEM huruwave_sources ${CMAKE_SOURCE_DIR}/src/main.cpp)
add_library(huruwave_core STATIC ${huruwave_sources})
target_include_directories(huruwave_core PUBLIC src)
target_link_libraries(huruwave_core PUBLIC
  Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Network)
target_compile_options(huruwave_core PUBLIC -Wall -Wextra)

# The QML is compiled at BUILD time (qt_add_qml_module → qmlcachegen) instead of at
# the first run. Speed is the smaller half of why: Qt keeps QML compiled at runtime
# in a per-user disk cache keyed by the document's URL, and it reuses a unit that an
# EARLIER build of the app wrote — measured: once the packaged 0.2.0 was on the
# machine, the Now-Playing panel still drew a pre-0.2.0 string
# ("the full track opens in your browser" instead of the F line) from a cache file
# written before that QML was edited, so the app presented as the old version until
# ~/.cache/<org>/huruwave/qmlcache was cleared. A unit that ships inside the binary
# is never resolved through that cache.
qt_add_executable(huruwave src/main.cpp)
target_link_libraries(huruwave PRIVATE huruwave_core)

set(huruwave_qml_files
  Main.qml
  Sidebar.qml
  TrackDelegate.qml
  NowPlaying.qml
  PulseWave.qml
  TransportBar.qml
  HelpOverlay.qml
  AppleWeb.qml)
set(huruwave_qml_sources "")
foreach(qml IN LISTS huruwave_qml_files)
  # Flat aliases: the files sit at the module root, so Main.qml's relative
  # `Loader { source: "AppleWeb.qml" }` keeps resolving.
  set_source_files_properties(src/${qml} PROPERTIES QT_RESOURCE_ALIAS ${qml})
  list(APPEND huruwave_qml_sources src/${qml})
endforeach()
qt_add_qml_module(huruwave
  URI huruwave
  VERSION 1.0
  # Stated rather than defaulted: the engine looks this module up as
  # qrc:/qt/qml/huruwave/qmldir, and the generated qmldir's `prefer` line showed the
  # resources landing at :/huruwave/ without it — which fails loadFromModule with no
  # message (main returns 1 on an empty root object).
  RESOURCE_PREFIX /qt/qml
  # NOT the build root: the module's generated qmldir/qml sources default to
  # <build>/huruwave, which is also where the executable must land (rpm/huruwave.spec
  # installs %{_vpath_builddir}/huruwave) — the collision fails the link with
  # "cannot open output file huruwave: Is a directory".
  OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/qml/huruwave
  QML_FILES ${huruwave_qml_sources})

# Full-track Apple Music. Apple's catalog API serves a 30-second preview and the
# full track is DRM-protected, which needs a Widevine CDM — and mpv, the engine
# everything else plays through, cannot load one. QtWebEngine (an embedded
# Chromium) can, so the DRM surface is built from it.
#
# OFF by default so a plain `bin/build` on a machine without the web engine still
# builds (the feature is then absent and the panel keeps pointing at the browser).
# The package turns it ON, and the spec carries the matching BuildRequires, so a
# green COPR build cannot ship a huruwave without it.
#
# The CDM itself is never built in, linked or installed: the app only looks for the
# copy the user's own browser downloaded (see src/widevine.cpp).
option(HURUWAVE_APPLE_WEB
       "Build the in-app Apple Music DRM surface (needs qt6-qtwebengine-devel)" OFF)
if(HURUWAVE_APPLE_WEB)
  find_package(Qt6 REQUIRED COMPONENTS WebEngineQuick)
  target_link_libraries(huruwave PRIVATE Qt6::WebEngineQuick)
  target_compile_definitions(huruwave PRIVATE HURUWAVE_APPLE_WEB=1)
endif()

enable_testing()
add_executable(huruwave_tests tests/huruwave_tests.cpp)
target_link_libraries(huruwave_tests PRIVATE huruwave_core Qt6::Test)
target_compile_options(huruwave_tests PRIVATE -Wno-psabi)
add_test(NAME huruwave_tests COMMAND huruwave_tests)
set_tests_properties(huruwave_tests PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
