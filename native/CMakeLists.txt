set(RELAY_NATIVE_SOURCES
  include/relay/domain.hpp
  include/relay/avatar_cache.hpp
  include/relay/app_paths.hpp
  include/relay/diff_model.hpp
  include/relay/diff_view.hpp
  include/relay/dialogs.hpp
  include/relay/process_runner.hpp
  include/relay/relay_controller.hpp
  include/relay/relay_store.hpp
  include/relay/repository_discovery.hpp
  include/relay/repository_order.hpp
  include/relay/github_api.hpp
  include/relay/github_auth.hpp
  include/relay/git_service.hpp
  include/relay/item_delegates.hpp
  include/relay/list_models.hpp
  include/relay/relay_application.hpp
  include/relay/main_window.hpp
  include/relay/ssh_service.hpp
  include/relay/theme.hpp
  src/domain.cpp
  src/avatar_cache.cpp
  src/app_paths.cpp
  src/diff_model.cpp
  src/diff_view.cpp
  src/dialogs.cpp
  src/github_api.cpp
  src/github_auth.cpp
  src/git_workflows.cpp
  src/git_service.cpp
  src/item_delegates.cpp
  src/list_models.cpp
  src/process_runner.cpp
  src/relay_controller.cpp
  src/relay_store.cpp
  src/repository_discovery.cpp
  src/repository_order.cpp
  src/relay_application.cpp
  src/main_window.cpp
  src/workflow_ui.cpp
  src/ssh_service.cpp
  src/theme.cpp
)

qt_add_executable(relay_native
  MACOSX_BUNDLE
  WIN32
  src/main.cpp
  resources/resources.qrc
)

if(APPLE)
  set(RELAY_MACOS_ICON "${PROJECT_SOURCE_DIR}/build/icon.icns")
  set_source_files_properties("${RELAY_MACOS_ICON}" PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources"
  )
  target_sources(relay_native PRIVATE "${RELAY_MACOS_ICON}")
elseif(WIN32)
  target_sources(relay_native PRIVATE resources/relay.rc)
endif()

add_library(relay_native_core STATIC ${RELAY_NATIVE_SOURCES})
target_link_libraries(relay_native PRIVATE relay_native_core)

foreach(target IN ITEMS relay_native relay_native_core)
  target_include_directories(${target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_compile_definitions(${target} PRIVATE
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
    RELAY_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    RELAY_VERSION="${PROJECT_VERSION}"
  )
  target_link_libraries(${target} PUBLIC
    Qt6::Concurrent
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Svg
    Qt6::Widgets
  )
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /Zc:__cplusplus)
    if(RELAY_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
      -Wshadow -Wnon-virtual-dtor
    )
    if(RELAY_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endforeach()

set_target_properties(relay_native PROPERTIES
  OUTPUT_NAME "Relay"
  MACOSX_BUNDLE_BUNDLE_NAME "Relay"
  MACOSX_BUNDLE_GUI_IDENTIFIER "dev.relay.gitclient"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  MACOSX_BUNDLE_ICON_FILE "icon.icns"
  WIN32_EXECUTABLE TRUE
)

install(TARGETS relay_native
  BUNDLE DESTINATION .
  RUNTIME DESTINATION .
)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/RelayPackaging.cmake")
relay_configure_packaging(TARGET relay_native)
