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

add_executable(huruwave src/main.cpp src/resources.qrc)
target_link_libraries(huruwave PRIVATE huruwave_core)

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
