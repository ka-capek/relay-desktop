cmake_minimum_required(VERSION 3.30)

project(RelayNative
  VERSION 0.6.0
  DESCRIPTION "A native multi-account Git desktop client"
  HOMEPAGE_URL "https://github.com/ka-capek/relay-desktop"
  LANGUAGES CXX
)

option(RELAY_BUILD_TESTS "Build Relay Native tests" ON)
option(RELAY_WARNINGS_AS_ERRORS "Treat Relay Native warnings as errors" OFF)
set(RELAY_QT_MIN_VERSION "6.11.1" CACHE STRING "Minimum supported Qt 6 version")

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 ${RELAY_QT_MIN_VERSION} EXACT REQUIRED COMPONENTS
  Concurrent
  Core
  Gui
  Network
  Svg
  Widgets
)

qt_standard_project_setup(REQUIRES ${RELAY_QT_MIN_VERSION})

add_subdirectory(native)

if(RELAY_BUILD_TESTS)
  enable_testing()
  add_subdirectory(native/tests)
endif()
