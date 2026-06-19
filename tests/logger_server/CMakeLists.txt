nwb_declare_executable(nwb_logserver_crash_tests)
target_sources(nwb_logserver_crash_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/crash_test_helpers.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/crash_test_helpers.h"
    "${CMAKE_CURRENT_LIST_DIR}/tests.cpp"
    "${PROJECT_SOURCE_DIR}/logger/server/crash_auth.cpp"
    "${PROJECT_SOURCE_DIR}/logger/server/crash_ingest.cpp"
    "${PROJECT_SOURCE_DIR}/logger/server/crash_paths.cpp"
    "${PROJECT_SOURCE_DIR}/logger/server/crash_symbolicate.cpp"
    "${PROJECT_SOURCE_DIR}/logger/server/crash_symbolicate_android.cpp"
    "${PROJECT_SOURCE_DIR}/logger/server/crash_symbolicate_linux.cpp"
    "${PROJECT_SOURCE_DIR}/logger/server/crash_symbolicate_rgd.cpp"
)
target_link_libraries(nwb_logserver_crash_tests PRIVATE
    nwb_common
    nwb_crash_package
    nwb_alloc
)
if(WIN32)
    target_sources(nwb_logserver_crash_tests PRIVATE
        "${PROJECT_SOURCE_DIR}/logger/server/crash_symbolicate_win32.cpp"
    )
    target_link_libraries(nwb_logserver_crash_tests PRIVATE dbghelp)
endif()
if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_dependencies(nwb_logserver_crash_tests nwb_crash_handler)
endif()
add_test(NAME nwb_logserver_crash_tests COMMAND nwb_logserver_crash_tests)
