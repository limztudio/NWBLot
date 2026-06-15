nwb_declare_executable(nwb_logserver)
target_sources(nwb_logserver PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/crash_auth.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/crash_symbolicate_android.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/crash_ingest.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/crash_symbolicate_linux.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/crash_paths.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/crash_symbolicate.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/frame.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/main.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/module.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/crash_auth.h"
    "${CMAKE_CURRENT_LIST_DIR}/crash_ingest.h"
    "${CMAKE_CURRENT_LIST_DIR}/crash_symbolicate_internal.h"
    "${CMAKE_CURRENT_LIST_DIR}/crash_paths.h"
    "${CMAKE_CURRENT_LIST_DIR}/crash_symbolicate.h"
    "${CMAKE_CURRENT_LIST_DIR}/frame.h"
    "${CMAKE_CURRENT_LIST_DIR}/module.h"
)
nwb_target_sources_standalone_runtime(nwb_logserver)
if(WIN32)
    target_sources(nwb_logserver PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/crash_symbolicate_win32.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/frame_win.cpp"
    )
    set_target_properties(nwb_logserver PROPERTIES WIN32_EXECUTABLE ON)
    target_link_libraries(nwb_logserver PRIVATE dbghelp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(nwb_logserver PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/frame_linux.cpp"
    )
endif()
target_link_libraries(nwb_logserver PRIVATE nwb::microhttpd nwb_common nwb_alloc nwb::cli11_headers)
