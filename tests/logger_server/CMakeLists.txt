nwb_declare_executable(nwb_logserver_crash_tests)
target_sources(nwb_logserver_crash_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/tests.cpp"
    "${PROJECT_SOURCE_DIR}/logger/server/crash_auth.cpp"
    "${PROJECT_SOURCE_DIR}/logger/server/crash_ingest.cpp"
    "${PROJECT_SOURCE_DIR}/logger/server/crash_paths.cpp"
    "${PROJECT_SOURCE_DIR}/logger/server/crash_symbolicate.cpp"
)
target_link_libraries(nwb_logserver_crash_tests PRIVATE
    nwb_common
    nwb_alloc
)
if(WIN32)
    target_link_libraries(nwb_logserver_crash_tests PRIVATE dbghelp)
endif()
add_test(NAME nwb_logserver_crash_tests COMMAND nwb_logserver_crash_tests)
