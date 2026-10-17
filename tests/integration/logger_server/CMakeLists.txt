nwb_declare_gtest_executable(nwb_logserver_crash_tests)
target_sources(nwb_logserver_crash_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/crash_test_helpers.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/crash_test_helpers.h"
    "${CMAKE_CURRENT_LIST_DIR}/logclient_shutdown_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/logserver_crash_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/logger_producer_contract_tests.cpp"
    "${PROJECT_SOURCE_DIR}/logger/server/crash_auth.cpp"
    "${PROJECT_SOURCE_DIR}/logger/server/crash_ingest.cpp"
    "${PROJECT_SOURCE_DIR}/logger/server/crash_paths.cpp"
    "${PROJECT_SOURCE_DIR}/logger/server/crash_symbolicate.cpp"
    "${PROJECT_SOURCE_DIR}/logger/server/crash_symbolicate_android.cpp"
    "${PROJECT_SOURCE_DIR}/logger/server/crash_symbolicate_linux.cpp"
    "${PROJECT_SOURCE_DIR}/logger/server/crash_symbolicate_rgd.cpp"
    "${PROJECT_SOURCE_DIR}/logger/server/crash_symbolicate_aftermath.cpp"
)
target_link_libraries(nwb_logserver_crash_tests PRIVATE nwb_common nwb_crash_package nwb_alloc nwb_logclient nwb::aftermath_headers nwb::microhttpd nwb_logrgd)
nwb_copy_aftermath_runtime(nwb_logserver_crash_tests)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND TARGET nwb_logserver AND Python3_Interpreter_FOUND)
    add_test(NAME nwb_logserver_aborted_upload_integration COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/aborted_upload_cli_tests.py" --executable "$<TARGET_FILE:nwb_logserver>"
            --output-directory "${CMAKE_BINARY_DIR}/Testing/logserver_aborted_upload/$<CONFIG>")
    set_tests_properties(nwb_logserver_aborted_upload_integration PROPERTIES TIMEOUT 60)
endif()
if(WIN32)
    target_sources(nwb_logserver_crash_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/logger/server/crash_symbolicate_win32.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/minidump_boundary_tests.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/clipboard_text_tests.cpp"
    )
    target_link_libraries(nwb_logserver_crash_tests PRIVATE dbghelp)
endif()
if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_dependencies(nwb_logserver_crash_tests nwb_crash_handler)
endif()
