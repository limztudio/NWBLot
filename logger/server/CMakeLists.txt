nwb_declare_static_library(nwb_logserver_core)
nwb_target_adhd_sources(nwb_logserver_core NAMESPACE "NWB::Log" SOURCES "${CMAKE_CURRENT_LIST_DIR}/*.adhd")
target_sources(nwb_logserver_core PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/clipboard_text.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/crash_ingest.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/crash_symbolicate.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/log_row_painter.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/module.cpp"
)
if(WIN32)
    target_link_libraries(nwb_logserver_core PRIVATE dbghelp)
endif()
target_link_libraries(nwb_logserver_core
    PUBLIC
        nwb::microhttpd
        nwb_common
        nwb_alloc
        nwb_crash
        nwb_logtelemetry
        nwb_logrgd
    PRIVATE
        nwb::aftermath_headers
)

nwb_declare_executable(nwb_logserver)
target_sources(nwb_logserver PRIVATE "${CMAKE_CURRENT_LIST_DIR}/main.cpp")
nwb_target_sources_standalone_runtime(nwb_logserver)
if(WIN32)
    set_target_properties(nwb_logserver PROPERTIES WIN32_EXECUTABLE ON)
endif()
target_link_libraries(nwb_logserver PRIVATE nwb_logserver_core nwb::cli11_headers)
nwb_copy_aftermath_runtime(nwb_logserver)
