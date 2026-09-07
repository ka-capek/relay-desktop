cmake_minimum_required(VERSION 3.30)

# Exercise the real staged-release verifier with synthetic files and tools.
# These fixtures test validation logic, not Mach-O loading or code signing.
if(NOT UNIX OR NOT DEFINED TEST_BINARY_DIR)
  message(FATAL_ERROR "Packaging regression requires UNIX and TEST_BINARY_DIR")
endif()
set(root "${TEST_BINARY_DIR}/packaging-fixture")
file(REMOVE_RECURSE "${root}")
set(stage "${root}/source/build/stage")
set(app "${stage}/Relay.app")
set(resources "${app}/Contents/Resources")
set(bin "${root}/mock-tools")
file(MAKE_DIRECTORY "${bin}")

foreach(relative IN ITEMS
    Contents/Resources/licenses/Relay-MIT.txt
    Contents/Resources/licenses/Qt-LGPL-3.0.txt
    Contents/Resources/licenses/THIRD_PARTY_NOTICES.md
    Contents/MacOS/Relay
    Contents/Frameworks/QtCore.framework/Versions/A/QtCore
    Contents/Frameworks/QtWidgets.framework/Versions/A/QtWidgets
    Contents/Frameworks/QtSvg.framework/Versions/A/QtSvg
    Contents/PlugIns/platforms/libqcocoa.dylib
    Contents/PlugIns/imageformats/libqsvg.dylib
    Contents/PlugIns/tls/libqsecuretransportbackend.dylib)
  get_filename_component(directory "${app}/${relative}" DIRECTORY)
  file(MAKE_DIRECTORY "${directory}")
  file(WRITE "${app}/${relative}" "fixture\n")
endforeach()

file(WRITE "${bin}/file" "#!/bin/sh\nprintf 'Mach-O\\n'\n")
file(WRITE "${bin}/lipo" "#!/bin/sh\nprintf 'arm64\\n'\n")
file(WRITE "${bin}/codesign" "#!/bin/sh\nexit 0\n")
set(clean_otool [=[#!/bin/sh
printf '%s (architecture arm64):\n\t/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit (compatibility version 45.0.0, current version 2566.0.0)\n' "$2"
printf '%s (architecture x86_64):\n\t@rpath/QtCore.framework/Versions/A/QtCore (compatibility version 6.0.0, current version 6.11.1)\n' "$2"
]=])
file(WRITE "${bin}/otool" "${clean_otool}")
foreach(tool IN ITEMS file lipo codesign otool)
  file(CHMOD "${bin}/${tool}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
endforeach()

function(check_stage name expected_error)
  execute_process(
    COMMAND "${CMAKE_COMMAND}"
      "-DRELAY_STAGE_ROOT=${stage}"
      -DRELAY_STAGE_APP_RELATIVE=Relay.app
      -DRELAY_STAGE_RESOURCES_RELATIVE=Relay.app/Contents/Resources
      -DRELAY_VERIFY_EXECUTABLE_NAME=Relay
      -DRELAY_VERIFY_PLATFORM=mac-arm64
      -DRELAY_VERIFY_BUNDLED_RUNTIMES=OFF
      "-DRELAY_VERIFY_SOURCE_DIR=${root}/source"
      "-DRELAY_VERIFY_BINARY_DIR=${root}/source/build"
      "-D_file_tool=${bin}/file"
      "-D_otool=${bin}/otool"
      "-D_lipo=${bin}/lipo"
      "-D_codesign=${bin}/codesign"
      -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../tools/verify_staged_release.cmake"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(expected_error STREQUAL "")
    if(NOT result EQUAL 0)
      message(FATAL_ERROR "${name}: expected success, got ${result}:\n${output}${error}")
    endif()
  elseif(result EQUAL 0 OR NOT "${error}" MATCHES "${expected_error}")
    message(FATAL_ERROR "${name}: expected '${expected_error}', got ${result}:\n${output}${error}")
  endif()
  message(STATUS "${name}: passed")
endfunction()

check_stage("Clean stage nested within source/build paths" "")
foreach(prefix IN ITEMS /opt/homebrew /usr/local "${root}/source" "${root}/source/build")
  string(REPLACE "/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit"
    "${prefix}/libbad.dylib" bad_otool "${clean_otool}")
  file(WRITE "${bin}/otool" "${bad_otool}")
  check_stage("Reject dependency from ${prefix}" "non-relocatable local path")
endforeach()
file(WRITE "${bin}/otool" "${clean_otool}")

foreach(name IN ITEMS debug.pdb git-lfs git-credential-manager)
  file(WRITE "${resources}/${name}" "forbidden\n")
  check_stage("Reject ${name}" "Excluded debug/GCM/LFS payload")
  file(REMOVE "${resources}/${name}")
endforeach()
file(MAKE_DIRECTORY "${resources}/symbols.dSYM")
check_stage("Reject empty dSYM directory" "Excluded debug/GCM/LFS payload")
file(REMOVE_RECURSE "${resources}/symbols.dSYM")
check_stage("Clean stage after removing forbidden payload" "")
file(REMOVE_RECURSE "${root}")
