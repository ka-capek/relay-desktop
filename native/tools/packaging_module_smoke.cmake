# Configure-time smoke probe for RelayPackaging.cmake. Inject with
# -DCMAKE_PROJECT_INCLUDE=<absolute path to this file>. It deliberately skips
# ignored payloads and strict legal checks; release presets must not.

set(RELAY_ENABLE_PACKAGING ON CACHE BOOL "" FORCE)
set(RELAY_BUNDLE_RUNTIMES OFF CACHE BOOL "" FORCE)
set(RELAY_PACKAGE_STRICT OFF CACHE BOOL "" FORCE)
set(RELAY_PACKAGE_DEPLOY_QT OFF CACHE BOOL "" FORCE)
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/RelayPackaging.cmake")
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}"
  CALL relay_configure_packaging TARGET relay_native)
