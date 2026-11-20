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

enable_testing()
add_executable(huruwave_tests tests/huruwave_tests.cpp)
target_link_libraries(huruwave_tests PRIVATE huruwave_core Qt6::Test)
target_compile_options(huruwave_tests PRIVATE -Wno-psabi)
add_test(NAME huruwave_tests COMMAND huruwave_tests)
set_tests_properties(huruwave_tests PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
