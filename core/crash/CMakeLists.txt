nwb_declare_static_library(nwb_crash)
target_sources(nwb_crash PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/handler.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/module.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/package.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/request.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/global.h"
    "${CMAKE_CURRENT_LIST_DIR}/internal.h"
    "${CMAKE_CURRENT_LIST_DIR}/module.h"
)
if(WIN32)
    target_sources(nwb_crash PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/platform_win32.cpp"
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Android")
    target_sources(nwb_crash PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/platform_posix.cpp"
    )
endif()
target_link_libraries(nwb_crash PUBLIC nwb_alloc)
target_link_libraries(nwb_crash PRIVATE nwb::curl)
if(WIN32)
    target_link_libraries(nwb_crash PUBLIC dbghelp)
endif()

if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    nwb_declare_executable(nwb_crash_handler)
    target_sources(nwb_crash_handler PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/main.cpp"
    )
    nwb_target_sources_standalone_runtime(nwb_crash_handler)
    target_link_libraries(nwb_crash_handler PRIVATE nwb_crash nwb_common)
    if(WIN32)
        set_target_properties(nwb_crash_handler PROPERTIES WIN32_EXECUTABLE ON)
    endif()
endif()
