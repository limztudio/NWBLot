nwb_declare_static_library(nwb_logserver_crash_test_names)
nwb_target_adhd_sources(nwb_logserver_crash_test_names NAMESPACE "NWB::Tests::LoggerCrashCases" SOURCES "${CMAKE_CURRENT_LIST_DIR}/name_cases.adhd")

nwb_declare_gtest_executable(nwb_logserver_crash_tests)
nwb_target_adhd_sources(nwb_logserver_crash_tests NAMESPACE "NWB::Log" SOURCES "${PROJECT_SOURCE_DIR}/logger/server/crash_auth.adhd" "${PROJECT_SOURCE_DIR}/logger/server/crash_retention.adhd" "${PROJECT_SOURCE_DIR}/logger/server/arena_names.adhd")
nwb_target_adhd_sources(nwb_logserver_crash_tests NAMESPACE "NWB::Tests::LoggerCrashCases" SOURCES "${CMAKE_CURRENT_LIST_DIR}/suite.adhd")
target_sources(nwb_logserver_crash_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/logger/server/crash_ingest.cpp"
    "${PROJECT_SOURCE_DIR}/logger/server/crash_archive_extractor.cpp"
    "${PROJECT_SOURCE_DIR}/logger/server/crash_manifest_validator.cpp"
    "${PROJECT_SOURCE_DIR}/logger/server/crash_symbolicate.cpp"
    "${PROJECT_SOURCE_DIR}/logger/server/crash_report_composer.cpp"
    "${PROJECT_SOURCE_DIR}/logger/server/crash_symbolicate_android.cpp"
    "${PROJECT_SOURCE_DIR}/logger/server/crash_symbolicate_linux.cpp"
    "${PROJECT_SOURCE_DIR}/logger/server/linux_frame_symbol_resolver.cpp"
    "${PROJECT_SOURCE_DIR}/logger/server/crash_symbolicate_rgd.cpp"
    "${PROJECT_SOURCE_DIR}/logger/server/crash_symbolicate_aftermath.cpp"
)
target_link_libraries(nwb_logserver_crash_tests PRIVATE nwb_crash_test_paths nwb_logserver_crash_test_names nwb_common nwb_crash_package nwb_alloc nwb_logclient nwb::aftermath_headers nwb::microhttpd nwb_logrgd)
target_link_libraries(nwb_logserver_crash_tests PRIVATE nwb_logserver_crash_path_names nwb_logserver_crash_values)
nwb_copy_aftermath_runtime(nwb_logserver_crash_tests)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND TARGET nwb_logserver AND Python3_Interpreter_FOUND)
    add_test(NAME nwb_logserver_aborted_upload_integration COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/aborted_upload_cli_tests.py" --executable "$<TARGET_FILE:nwb_logserver>"
            --output-directory "${CMAKE_BINARY_DIR}/Testing/logserver_aborted_upload/$<CONFIG>")
    set(_nwb_logserver_aborted_upload_timeout_seconds 60)
    set_tests_properties(nwb_logserver_aborted_upload_integration PROPERTIES TIMEOUT ${_nwb_logserver_aborted_upload_timeout_seconds})
    unset(_nwb_logserver_aborted_upload_timeout_seconds)
endif()
if((WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux") AND TARGET nwb_logserver AND Python3_Interpreter_FOUND)
    add_test(NAME nwb_logserver_http_status_integration COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/http_status_cli_tests.py" --executable "$<TARGET_FILE:nwb_logserver>"
            --output-directory "${CMAKE_BINARY_DIR}/Testing/logserver_http_status/$<CONFIG>")
    set_tests_properties(nwb_logserver_http_status_integration PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT 120)
endif()
if(WIN32)
    target_sources(nwb_logserver_crash_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/logger/server/clipboard_text.cpp"
        "${PROJECT_SOURCE_DIR}/logger/server/log_row_painter.cpp"
        "${PROJECT_SOURCE_DIR}/logger/server/crash_symbolicate_win32.cpp"
        "${PROJECT_SOURCE_DIR}/logger/server/minidump_image.cpp"
    )
    target_link_libraries(nwb_logserver_crash_tests PRIVATE dbghelp)
endif()
if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_dependencies(nwb_logserver_crash_tests nwb_crash_handler)
endif()

