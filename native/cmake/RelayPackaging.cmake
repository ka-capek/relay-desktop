include_guard(GLOBAL)

include(CMakeParseArguments)

option(RELAY_ENABLE_PACKAGING "Configure Relay's native release packages" OFF)
option(RELAY_BUNDLE_RUNTIMES "Bundle the ignored Git and GitHub CLI runtime payloads" ON)
option(RELAY_PACKAGE_STRICT "Fail packaging when provenance, payload, or verification is incomplete" ON)
option(RELAY_PACKAGE_QT_TRANSLATIONS "Deploy Qt translations in addition to Relay's en-US UI" OFF)
option(RELAY_PACKAGE_DEPLOY_QT "Dynamically deploy Qt into the staged release" ON)

set(RELAY_RUNTIME_ROOT "${PROJECT_SOURCE_DIR}/runtime" CACHE PATH
    "Ignored runtime payload root containing git/<platform> and gh/<platform>")
set(RELAY_QT_LICENSE_FILE "" CACHE FILEPATH "Path to the LGPL-3.0 license shipped with the selected Qt build")
set(RELAY_GIT_LICENSE_FILE "" CACHE FILEPATH "Path to Git's GPL-2.0-only license text")
set(RELAY_GH_LICENSE_FILE "" CACHE FILEPATH "Path to GitHub CLI's MIT license text")
set(RELAY_GIT_SOURCE_URI "" CACHE STRING
    "Release-stable URI for the exact corresponding source of the bundled Git payload")
set(RELAY_QT_SOURCE_URI "" CACHE STRING
    "Release-stable URI for the exact corresponding source of the bundled Qt build")
set(RELAY_GH_SOURCE_URI "" CACHE STRING
    "Release-stable URI for the exact source of the bundled GitHub CLI")
set(RELAY_BUNDLED_GIT_VERSION "" CACHE STRING "Expected bundled Git version")
set(RELAY_BUNDLED_GH_VERSION "" CACHE STRING "Expected bundled GitHub CLI version")
set(RELAY_MACOS_SIGNING_IDENTITY "" CACHE STRING
    "Developer ID identity for macdeployqt; empty keeps the current ad-hoc signature")

function(_relay_first_existing output)
  foreach(candidate IN LISTS ARGN)
    if(NOT candidate STREQUAL "" AND EXISTS "${candidate}")
      set(${output} "${candidate}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  set(${output} "" PARENT_SCOPE)
endfunction()

function(_relay_require_file description path)
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "Native packaging requires ${description}: ${path}")
  endif()
endfunction()

function(_relay_install_trimmed_git source destination)
  install(DIRECTORY "${source}/" DESTINATION "${destination}" USE_SOURCE_PERMISSIONS
    PATTERN ".DS_Store" EXCLUDE
    PATTERN "*.pdb" EXCLUDE
    PATTERN "git-lfs" EXCLUDE
    PATTERN "git-lfs.exe" EXCLUDE
    PATTERN "git-credential-manager*" EXCLUDE
    PATTERN "libexec/git-core/*.dll" EXCLUDE
    PATTERN "libexec/git-core/*.deps.json" EXCLUDE
    PATTERN "libexec/git-core/*.runtimeconfig.json" EXCLUDE
    PATTERN "libexec/git-core/libcoreclr*" EXCLUDE
    PATTERN "libexec/git-core/libhostfxr*" EXCLUDE
    PATTERN "libexec/git-core/libhostpolicy*" EXCLUDE
    PATTERN "libexec/git-core/libclrjit*" EXCLUDE
    PATTERN "libexec/git-core/libSkiaSharp*" EXCLUDE
    PATTERN "libexec/git-core/libHarfBuzzSharp*" EXCLUDE
    PATTERN "libexec/git-core/createdump" EXCLUDE
    PATTERN "mingw64/libexec/git-core/*.dll" EXCLUDE
    PATTERN "mingw64/libexec/git-core/*.deps.json" EXCLUDE
    PATTERN "mingw64/libexec/git-core/*.runtimeconfig.json" EXCLUDE
    PATTERN "mingw64/libexec/git-core/git-credential-manager*" EXCLUDE
    PATTERN "mingw64/bin/Avalonia*.dll" EXCLUDE
    PATTERN "mingw64/bin/Microsoft.*.dll" EXCLUDE
    PATTERN "mingw64/bin/System.*.dll" EXCLUDE
    PATTERN "mingw64/bin/SkiaSharp.dll" EXCLUDE
    PATTERN "mingw64/bin/libSkiaSharp.dll" EXCLUDE
    PATTERN "mingw64/bin/HarfBuzzSharp.dll" EXCLUDE
    PATTERN "mingw64/bin/libHarfBuzzSharp.dll" EXCLUDE
    PATTERN "mingw64/bin/*.deps.json" EXCLUDE
    PATTERN "mingw64/bin/*.runtimeconfig.json" EXCLUDE)
endfunction()

function(relay_configure_packaging)
  cmake_parse_arguments(PARSE_ARGV 0 argument "" "TARGET" "")
  if(NOT RELAY_ENABLE_PACKAGING)
    return()
  endif()
  if(argument_UNPARSED_ARGUMENTS OR argument_KEYWORDS_MISSING_VALUES OR NOT argument_TARGET)
    message(FATAL_ERROR "relay_configure_packaging requires exactly TARGET <application-target>")
  endif()
  if(NOT TARGET "${argument_TARGET}")
    message(FATAL_ERROR "Relay packaging target does not exist: ${argument_TARGET}")
  endif()
  if(NOT APPLE AND NOT WIN32)
    message(FATAL_ERROR "Relay packages are supported only on native macOS and Windows hosts")
  endif()

  get_target_property(_qt_core_type Qt6::Core TYPE)
  if(_qt_core_type STREQUAL "STATIC_LIBRARY")
    message(FATAL_ERROR "Relay is MIT and must package dynamically linked LGPL Qt, not static Qt")
  endif()

  set(_packaging_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/..")
  set(_tools_root "${_packaging_root}/tools")
  set(_notices_template "${_packaging_root}/packaging/THIRD_PARTY_NOTICES.md.in")
  set(_relay_license "${PROJECT_SOURCE_DIR}/LICENSE")
  _relay_require_file("Relay's MIT license" "${_relay_license}")
  _relay_require_file("the third-party notice template" "${_notices_template}")

  if(APPLE)
    set(_platform "mac-arm64")
    set(_architecture "arm64")
    set(_git_entry "bin/git")
    set(_git_https "libexec/git-core/git-remote-https")
    set(_git_templates "share/git-core/templates")
    set(_gh_entry "gh")
    set(_app_relative "Relay.app")
    set(_resources_relative "${_app_relative}/Contents/Resources")
    if(RELAY_BUNDLED_GIT_VERSION STREQUAL "")
      set(RELAY_BUNDLED_GIT_VERSION "2.53.0")
    endif()
    if(RELAY_BUNDLED_GH_VERSION STREQUAL "")
      set(RELAY_BUNDLED_GH_VERSION "2.98.0")
    endif()
  else()
    set(_platform "win-x64")
    set(_architecture "x64")
    set(_git_entry "cmd/git.exe")
    set(_git_https "mingw64/bin/git-remote-https.exe")
    set(_git_templates "mingw64/share/git-core/templates")
    set(_gh_entry "gh.exe")
    set(_app_relative "Relay.exe")
    set(_resources_relative "resources")
    if(RELAY_BUNDLED_GIT_VERSION STREQUAL "")
      set(RELAY_BUNDLED_GIT_VERSION "2.52.0.windows.1")
    endif()
    if(RELAY_BUNDLED_GH_VERSION STREQUAL "")
      set(RELAY_BUNDLED_GH_VERSION "2.98.0")
    endif()
  endif()

  if(RELAY_QT_SOURCE_URI STREQUAL "" AND DEFINED Qt6_VERSION)
    string(REGEX MATCH "^[0-9]+\\.[0-9]+" _qt_series "${Qt6_VERSION}")
    set(RELAY_QT_SOURCE_URI
        "https://download.qt.io/official_releases/qt/${_qt_series}/${Qt6_VERSION}/single/qt-everywhere-src-${Qt6_VERSION}.tar.xz")
  endif()
  if(RELAY_GH_SOURCE_URI STREQUAL "")
    set(RELAY_GH_SOURCE_URI
        "https://github.com/cli/cli/archive/refs/tags/v${RELAY_BUNDLED_GH_VERSION}.tar.gz")
  endif()

  _relay_first_existing(_qt_license
    "${RELAY_QT_LICENSE_FILE}"
    "${QT6_INSTALL_PREFIX}/LICENSES/LGPL-3.0-only.txt"
    "${Qt6_DIR}/../../../LICENSES/LGPL-3.0-only.txt")
  _relay_first_existing(_git_license
    "${RELAY_GIT_LICENSE_FILE}"
    "${RELAY_RUNTIME_ROOT}/git/${_platform}/LICENSE.txt"
    "${RELAY_RUNTIME_ROOT}/git/win-x64/LICENSE.txt")
  _relay_first_existing(_gh_license
    "${RELAY_GH_LICENSE_FILE}"
    "${RELAY_RUNTIME_ROOT}/gh/${_platform}/LICENSE.txt"
    "${RELAY_RUNTIME_ROOT}/gh/win-x64/LICENSE.txt")

  if(RELAY_PACKAGE_STRICT)
    if(NOT RELAY_PACKAGE_DEPLOY_QT)
      message(FATAL_ERROR "Strict Relay packages must dynamically deploy Qt")
    endif()
    if(_qt_license STREQUAL "")
      message(FATAL_ERROR
        "No LGPL-3.0 license text was found. Set RELAY_QT_LICENSE_FILE to the license from the exact Qt build.")
    endif()
    if(RELAY_BUNDLE_RUNTIMES AND _git_license STREQUAL "")
      message(FATAL_ERROR "No Git GPL-2.0-only license text was found. Set RELAY_GIT_LICENSE_FILE.")
    endif()
    if(RELAY_BUNDLE_RUNTIMES AND _gh_license STREQUAL "")
      message(FATAL_ERROR "No GitHub CLI MIT license text was found. Set RELAY_GH_LICENSE_FILE.")
    endif()
    if(RELAY_BUNDLE_RUNTIMES AND RELAY_GIT_SOURCE_URI STREQUAL "")
      message(FATAL_ERROR
        "Set RELAY_GIT_SOURCE_URI to a release-stable corresponding-source bundle for this exact Git payload. "
        "For Git for Windows this must cover the redistributed MSYS2 component inventory, not only git.git.")
    endif()
    if(RELAY_QT_SOURCE_URI STREQUAL "")
      message(FATAL_ERROR "Set RELAY_QT_SOURCE_URI to corresponding source for the exact Qt build.")
    endif()
  endif()

  if(RELAY_BUNDLE_RUNTIMES)
    set(_git_root "${RELAY_RUNTIME_ROOT}/git/${_platform}")
    set(_gh_root "${RELAY_RUNTIME_ROOT}/gh/${_platform}")
    _relay_require_file("bundled Git" "${_git_root}/${_git_entry}")
    _relay_require_file("Git's HTTPS transport helper" "${_git_root}/${_git_https}")
    if(NOT IS_DIRECTORY "${_git_root}/${_git_templates}")
      message(FATAL_ERROR "Native packaging requires Git templates: ${_git_root}/${_git_templates}")
    endif()
    _relay_require_file("bundled GitHub CLI" "${_gh_root}/${_gh_entry}")
    if(WIN32)
      _relay_require_file("Git for Windows' CA bundle"
        "${_git_root}/mingw64/etc/ssl/certs/ca-bundle.crt")
    endif()

    execute_process(
      COMMAND "${_git_root}/${_git_entry}" --version
      RESULT_VARIABLE _git_version_result OUTPUT_VARIABLE _git_version_output ERROR_VARIABLE _git_version_error)
    execute_process(
      COMMAND "${_gh_root}/${_gh_entry}" --version
      RESULT_VARIABLE _gh_version_result OUTPUT_VARIABLE _gh_version_output ERROR_VARIABLE _gh_version_error)
    if(RELAY_PACKAGE_STRICT AND
       (NOT _git_version_result EQUAL 0 OR NOT _git_version_output MATCHES "${RELAY_BUNDLED_GIT_VERSION}"))
      message(FATAL_ERROR
        "Bundled Git does not match RELAY_BUNDLED_GIT_VERSION=${RELAY_BUNDLED_GIT_VERSION}: "
        "${_git_version_output}${_git_version_error}")
    endif()
    if(RELAY_PACKAGE_STRICT AND
       (NOT _gh_version_result EQUAL 0 OR NOT _gh_version_output MATCHES "${RELAY_BUNDLED_GH_VERSION}"))
      message(FATAL_ERROR
        "Bundled gh does not match RELAY_BUNDLED_GH_VERSION=${RELAY_BUNDLED_GH_VERSION}: "
        "${_gh_version_output}${_gh_version_error}")
    endif()

    _relay_install_trimmed_git("${_git_root}" "${_resources_relative}/git")
    install(DIRECTORY "${_gh_root}/" DESTINATION "${_resources_relative}/gh"
      USE_SOURCE_PERMISSIONS PATTERN ".DS_Store" EXCLUDE)
  endif()

  set(RELAY_NOTICE_PLATFORM "${_platform}")
  set(RELAY_NOTICE_QT_VERSION "${Qt6_VERSION}")
  set(RELAY_NOTICE_GIT_VERSION "${RELAY_BUNDLED_GIT_VERSION}")
  set(RELAY_NOTICE_GH_VERSION "${RELAY_BUNDLED_GH_VERSION}")
  set(RELAY_NOTICE_GIT_BUNDLED "${RELAY_BUNDLE_RUNTIMES}")
  configure_file("${_notices_template}"
    "${CMAKE_CURRENT_BINARY_DIR}/Relay-THIRD_PARTY_NOTICES.md" @ONLY)

  set(_licenses_destination "${_resources_relative}/licenses")
  install(FILES "${_relay_license}" DESTINATION "${_licenses_destination}" RENAME "Relay-MIT.txt")
  if(NOT _qt_license STREQUAL "")
    install(FILES "${_qt_license}" DESTINATION "${_licenses_destination}" RENAME "Qt-LGPL-3.0.txt")
  endif()
  if(RELAY_BUNDLE_RUNTIMES AND NOT _git_license STREQUAL "")
    install(FILES "${_git_license}" DESTINATION "${_licenses_destination}" RENAME "Git-GPL-2.0.txt")
  endif()
  if(RELAY_BUNDLE_RUNTIMES AND NOT _gh_license STREQUAL "")
    install(FILES "${_gh_license}" DESTINATION "${_licenses_destination}" RENAME "GitHub-CLI-MIT.txt")
  endif()
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/Relay-THIRD_PARTY_NOTICES.md"
    DESTINATION "${_licenses_destination}" RENAME "THIRD_PARTY_NOTICES.md")

  if(RELAY_PACKAGE_DEPLOY_QT)
    set(_deploy_arguments TARGET "${argument_TARGET}" OUTPUT_SCRIPT _relay_deploy_script)
    if(NOT RELAY_PACKAGE_QT_TRANSLATIONS)
      list(APPEND _deploy_arguments NO_TRANSLATIONS)
    endif()
    if(WIN32)
      list(APPEND _deploy_arguments EXCLUDE_PLUGIN_TYPES qmltooling sqldrivers)
    elseif(NOT RELAY_MACOS_SIGNING_IDENTITY STREQUAL "")
      list(APPEND _deploy_arguments DEPLOY_TOOL_OPTIONS
        -hardened-runtime "-codesign=${RELAY_MACOS_SIGNING_IDENTITY}")
    endif()
    qt_generate_deploy_app_script(${_deploy_arguments})
    install(SCRIPT "${_relay_deploy_script}")
  endif()

  set(_verify_code "
set(RELAY_STAGE_ROOT \"\${CMAKE_INSTALL_PREFIX}\")
set(RELAY_STAGE_APP_RELATIVE \"${_app_relative}\")
set(RELAY_STAGE_RESOURCES_RELATIVE \"${_resources_relative}\")
set(RELAY_VERIFY_EXECUTABLE_NAME \"Relay\")
set(RELAY_VERIFY_PLATFORM \"${_platform}\")
set(RELAY_VERIFY_BUNDLED_RUNTIMES \"${RELAY_BUNDLE_RUNTIMES}\")
set(RELAY_VERIFY_SOURCE_DIR \"${PROJECT_SOURCE_DIR}\")
set(RELAY_VERIFY_BINARY_DIR \"${CMAKE_BINARY_DIR}\")
set(RELAY_VERIFY_STRICT \"${RELAY_PACKAGE_STRICT}\")
include(\"${_tools_root}/verify_staged_release.cmake\")")
  install(CODE "${_verify_code}")

  set(CPACK_PACKAGE_NAME "Relay")
  set(CPACK_PACKAGE_VENDOR "Relay")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A native multi-account Git desktop client")
  set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
  set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
  set(CPACK_PACKAGE_DIRECTORY "${PROJECT_SOURCE_DIR}/outputs/native-installers")
  set(CPACK_PACKAGE_CHECKSUM "SHA256")
  set(CPACK_MONOLITHIC_INSTALL ON)
  set(CPACK_RESOURCE_FILE_LICENSE "${_relay_license}")
  if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "Relay ${PROJECT_VERSION}")
    set(CPACK_DMG_FORMAT "UDZO")
    set(CPACK_PACKAGE_FILE_NAME "Relay-${PROJECT_VERSION}-${_architecture}")
  else()
    set(CPACK_GENERATOR "NSIS")
    set(CPACK_PACKAGE_FILE_NAME "Relay-Setup-${PROJECT_VERSION}-${_architecture}")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "Relay")
    set(CPACK_PACKAGE_EXECUTABLES "Relay;Relay")
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
    set(CPACK_CREATE_DESKTOP_LINKS "Relay")
    set(CPACK_NSIS_DISPLAY_NAME "Relay")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH OFF)
    set(CPACK_NSIS_MUI_ICON "${PROJECT_SOURCE_DIR}/build/icon.ico")
    set(CPACK_NSIS_MUI_UNIICON "${PROJECT_SOURCE_DIR}/build/icon.ico")
  endif()
  include(CPack)
endfunction()
