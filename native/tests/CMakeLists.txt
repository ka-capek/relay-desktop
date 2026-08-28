find_package(Qt6 ${RELAY_QT_MIN_VERSION} EXACT REQUIRED COMPONENTS Test)

foreach(test_name IN ITEMS app_paths avatar_cache dialogs diff_model domain git_service github_api github_auth list_models main_window process_runner relay_controller relay_store repository_discovery repository_order ssh_service theme)
  qt_add_executable(tst_${test_name} tst_${test_name}.cpp)
  target_link_libraries(tst_${test_name} PRIVATE relay_native_core Qt6::Test)
  target_compile_definitions(tst_${test_name} PRIVATE QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)
  add_test(NAME ${test_name} COMMAND tst_${test_name})
endforeach()

add_test(NAME app_smoke COMMAND $<TARGET_FILE:relay_native> --smoke-test)
set_tests_properties(app_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 10)
