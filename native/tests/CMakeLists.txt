find_package(Qt6 ${RELAY_QT_MIN_VERSION} EXACT REQUIRED COMPONENTS Test)

foreach(test_name IN ITEMS app_paths avatar_cache dialogs diff_model domain git_service git_workflows github_api github_auth list_models main_window process_runner relay_controller relay_store repository_discovery repository_order ssh_service theme)
  qt_add_executable(tst_${test_name} tst_${test_name}.cpp)
  # Console entry points keep QtTest diagnostics visible to CTest on Windows.
  set_target_properties(tst_${test_name} PROPERTIES WIN32_EXECUTABLE FALSE)
  target_link_libraries(tst_${test_name} PRIVATE relay_native_core Qt6::Test)
  target_compile_definitions(tst_${test_name} PRIVATE QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)
  add_test(NAME ${test_name} COMMAND tst_${test_name}
    -o -,txt -o "${CMAKE_CURRENT_BINARY_DIR}/${test_name}.txt,txt")
endforeach()

# Git for Windows process startup makes this multi-repository integration suite
# substantially slower than the individual unit/UI suites.
set_tests_properties(git_workflows PROPERTIES TIMEOUT 180)

add_test(NAME app_smoke COMMAND $<TARGET_FILE:relay_native> --smoke-test)
set_tests_properties(app_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 10)

# Mock macOS inspection tools use POSIX shell scripts. Linux and macOS run
# this verifier regression; Windows still requires native release validation.
if(UNIX)
  add_test(NAME packaging_verifier COMMAND "${CMAKE_COMMAND}"
    "-DTEST_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tst_packaging.cmake")
  set_tests_properties(packaging_verifier PROPERTIES TIMEOUT 30)
endif()
