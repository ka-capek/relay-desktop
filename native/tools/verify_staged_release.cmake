cmake_minimum_required(VERSION 3.30)

foreach(required_variable IN ITEMS
    RELAY_STAGE_ROOT RELAY_STAGE_APP_RELATIVE RELAY_STAGE_RESOURCES_RELATIVE
    RELAY_VERIFY_EXECUTABLE_NAME RELAY_VERIFY_PLATFORM)
  if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
    message(FATAL_ERROR "Staged release verification is missing ${required_variable}")
  endif()
endforeach()

function(_relay_assert_file description path)
  if(NOT EXISTS "${path}" OR IS_DIRECTORY "${path}")
    message(FATAL_ERROR "Staged release is missing ${description}: ${path}")
  endif()
  file(SIZE "${path}" size)
  if(size EQUAL 0)
    message(FATAL_ERROR "Staged ${description} is empty: ${path}")
  endif()
endfunction()

function(_relay_assert_directory description path)
  if(NOT IS_DIRECTORY "${path}")
    message(FATAL_ERROR "Staged release is missing ${description}: ${path}")
  endif()
endfunction()

set(_app "${RELAY_STAGE_ROOT}/${RELAY_STAGE_APP_RELATIVE}")
set(_resources "${RELAY_STAGE_ROOT}/${RELAY_STAGE_RESOURCES_RELATIVE}")
_relay_assert_file("Relay's MIT license" "${_resources}/licenses/Relay-MIT.txt")
_relay_assert_file("Qt's LGPL license" "${_resources}/licenses/Qt-LGPL-3.0.txt")
_relay_assert_file("third-party notices" "${_resources}/licenses/THIRD_PARTY_NOTICES.md")

if(RELAY_VERIFY_BUNDLED_RUNTIMES)
  _relay_assert_file("Git's GPL license" "${_resources}/licenses/Git-GPL-2.0.txt")
  _relay_assert_file("GitHub CLI's MIT license" "${_resources}/licenses/GitHub-CLI-MIT.txt")
endif()

file(GLOB_RECURSE _resource_entries LIST_DIRECTORIES TRUE "${_resources}/*")
set(_excluded_payload)
foreach(entry IN LISTS _resource_entries)
  get_filename_component(_entry_name "${entry}" NAME)
  if(_entry_name MATCHES "git-lfs|git-credential-manager|\\.pdb$|\\.dSYM$")
    list(APPEND _excluded_payload "${entry}")
  endif()
endforeach()
if(_excluded_payload)
  list(JOIN _excluded_payload "\n  " _excluded_list)
  message(FATAL_ERROR "Excluded debug/GCM/LFS payload leaked into the staged release:\n  ${_excluded_list}")
endif()

if(RELAY_VERIFY_PLATFORM STREQUAL "mac-arm64")
  _relay_assert_directory("application bundle" "${_app}")
  _relay_assert_file("Relay executable" "${_app}/Contents/MacOS/${RELAY_VERIFY_EXECUTABLE_NAME}")
  _relay_assert_file("Qt Core framework" "${_app}/Contents/Frameworks/QtCore.framework/Versions/A/QtCore")
  _relay_assert_file("Qt Widgets framework" "${_app}/Contents/Frameworks/QtWidgets.framework/Versions/A/QtWidgets")
  _relay_assert_file("Qt SVG framework" "${_app}/Contents/Frameworks/QtSvg.framework/Versions/A/QtSvg")
  _relay_assert_file("Qt Cocoa platform plugin" "${_app}/Contents/PlugIns/platforms/libqcocoa.dylib")
  _relay_assert_file("Qt SVG image plugin" "${_app}/Contents/PlugIns/imageformats/libqsvg.dylib")
  _relay_assert_file("Qt SecureTransport TLS plugin" "${_app}/Contents/PlugIns/tls/libqsecuretransportbackend.dylib")

  if(RELAY_VERIFY_BUNDLED_RUNTIMES)
    _relay_assert_file("bundled Git" "${_resources}/git/bin/git")
    _relay_assert_file("Git HTTPS transport" "${_resources}/git/libexec/git-core/git-remote-https")
    _relay_assert_directory("Git templates" "${_resources}/git/share/git-core/templates")
    _relay_assert_file("bundled GitHub CLI" "${_resources}/gh/gh")
  endif()

  find_program(_file_tool file REQUIRED)
  find_program(_otool otool REQUIRED)
  find_program(_lipo lipo REQUIRED)
  find_program(_codesign codesign REQUIRED)
  file(GLOB_RECURSE _stage_files LIST_DIRECTORIES FALSE "${_app}/*")
  foreach(candidate IN LISTS _stage_files)
    execute_process(
      COMMAND "${_file_tool}" -b "${candidate}"
      RESULT_VARIABLE _file_result OUTPUT_VARIABLE _file_kind ERROR_QUIET
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(_file_result EQUAL 0 AND _file_kind MATCHES "Mach-O")
      execute_process(
        COMMAND "${_otool}" -L "${candidate}"
        RESULT_VARIABLE _otool_result OUTPUT_VARIABLE _dependencies ERROR_VARIABLE _otool_error)
      if(NOT _otool_result EQUAL 0)
        message(FATAL_ERROR "otool failed for ${candidate}: ${_otool_error}")
      endif()
      # otool prints the inspected file's absolute path as an unindented
      # header. Check dependency records only: the stage itself may be inside
      # the source/build tree, and universal binaries have multiple headers.
      string(REPLACE "\n" ";" _dependency_lines "${_dependencies}")
      set(_dependency_records "")
      foreach(line IN LISTS _dependency_lines)
        if(line MATCHES "^[ \t]+")
          string(APPEND _dependency_records "${line}\n")
        endif()
      endforeach()
      foreach(forbidden_prefix IN ITEMS
          "/opt/homebrew" "/usr/local" "${RELAY_VERIFY_SOURCE_DIR}" "${RELAY_VERIFY_BINARY_DIR}")
        string(FIND "${_dependency_records}" "${forbidden_prefix}" _forbidden_position)
        if(NOT forbidden_prefix STREQUAL "" AND NOT _forbidden_position EQUAL -1)
          message(FATAL_ERROR
            "Staged Mach-O depends on a non-relocatable local path '${forbidden_prefix}':\n"
            "  ${candidate}\n${_dependencies}")
        endif()
      endforeach()
    endif()
  endforeach()

  set(_architecture_files "${_app}/Contents/MacOS/${RELAY_VERIFY_EXECUTABLE_NAME}")
  if(RELAY_VERIFY_BUNDLED_RUNTIMES)
    list(APPEND _architecture_files "${_resources}/git/bin/git" "${_resources}/gh/gh")
  endif()
  foreach(candidate IN LISTS _architecture_files)
    execute_process(
      COMMAND "${_lipo}" -archs "${candidate}"
      RESULT_VARIABLE _lipo_result OUTPUT_VARIABLE _architectures ERROR_VARIABLE _lipo_error
      OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT _lipo_result EQUAL 0 OR NOT _architectures MATCHES "(^| )arm64($| )")
      message(FATAL_ERROR "Expected arm64 Mach-O payload at ${candidate}: ${_architectures}${_lipo_error}")
    endif()
  endforeach()

  execute_process(
    COMMAND "${_codesign}" --verify --deep --strict "${_app}"
    RESULT_VARIABLE _codesign_result ERROR_VARIABLE _codesign_error)
  if(NOT _codesign_result EQUAL 0)
    message(FATAL_ERROR "The final macOS bundle seal is invalid: ${_codesign_error}")
  endif()
elseif(RELAY_VERIFY_PLATFORM STREQUAL "win-x64")
  _relay_assert_file("Relay executable" "${_app}")
  _relay_assert_file("Qt Core DLL" "${RELAY_STAGE_ROOT}/Qt6Core.dll")
  _relay_assert_file("Qt Widgets DLL" "${RELAY_STAGE_ROOT}/Qt6Widgets.dll")
  _relay_assert_file("Qt Windows platform plugin" "${RELAY_STAGE_ROOT}/platforms/qwindows.dll")
  _relay_assert_file("Qt SVG image plugin" "${RELAY_STAGE_ROOT}/imageformats/qsvg.dll")
  _relay_assert_file("Qt Schannel TLS plugin" "${RELAY_STAGE_ROOT}/tls/qschannelbackend.dll")
  if(EXISTS "${RELAY_STAGE_ROOT}/Qt6Cored.dll")
    message(FATAL_ERROR "A debug Qt runtime leaked into the release stage: Qt6Cored.dll")
  endif()

  if(RELAY_VERIFY_BUNDLED_RUNTIMES)
    _relay_assert_file("bundled Git" "${_resources}/git/cmd/git.exe")
    _relay_assert_file("Git HTTPS transport" "${_resources}/git/mingw64/bin/git-remote-https.exe")
    _relay_assert_directory("Git templates" "${_resources}/git/mingw64/share/git-core/templates")
    _relay_assert_file("Git CA bundle" "${_resources}/git/mingw64/etc/ssl/certs/ca-bundle.crt")
    _relay_assert_file("bundled GitHub CLI" "${_resources}/gh/gh.exe")
  endif()

  find_program(_dumpbin dumpbin)
  if(_dumpbin)
    set(_architecture_files "${_app}")
    if(RELAY_VERIFY_BUNDLED_RUNTIMES)
      list(APPEND _architecture_files "${_resources}/git/cmd/git.exe" "${_resources}/gh/gh.exe")
    endif()
    foreach(candidate IN LISTS _architecture_files)
      execute_process(
        COMMAND "${_dumpbin}" /headers "${candidate}"
        RESULT_VARIABLE _dumpbin_result OUTPUT_VARIABLE _headers ERROR_VARIABLE _dumpbin_error)
      if(NOT _dumpbin_result EQUAL 0 OR NOT _headers MATCHES "machine \\(x64\\)")
        message(FATAL_ERROR "Expected PE x64 payload at ${candidate}: ${_dumpbin_error}")
      endif()
    endforeach()
  elseif(RELAY_VERIFY_STRICT)
    message(FATAL_ERROR "Strict Windows stage verification requires dumpbin in the MSVC environment")
  endif()
else()
  message(FATAL_ERROR "Unknown Relay package platform: ${RELAY_VERIFY_PLATFORM}")
endif()

file(SHA256 "${_resources}/licenses/THIRD_PARTY_NOTICES.md" _notice_hash)
message(STATUS "Relay staged release verified (${RELAY_VERIFY_PLATFORM}, notices SHA-256 ${_notice_hash})")
