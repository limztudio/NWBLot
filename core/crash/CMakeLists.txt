nwb_declare_static_library(nwb_crash)
set(NWB_CRASH_HANDLER_OUTPUT_NAME "crash_handler")
target_compile_definitions(nwb_crash PRIVATE NWB_CRASH_HANDLER_EXECUTABLE_NAME="${NWB_CRASH_HANDLER_OUTPUT_NAME}")
target_sources(nwb_crash PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/module.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/request.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/spool.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/arena_names.h"
    "${CMAKE_CURRENT_LIST_DIR}/global.h"
    "${CMAKE_CURRENT_LIST_DIR}/internal.h"
    "${CMAKE_CURRENT_LIST_DIR}/io_posix.h"
    "${CMAKE_CURRENT_LIST_DIR}/io_win32.h"
    "${CMAKE_CURRENT_LIST_DIR}/module.h"
    "${CMAKE_CURRENT_LIST_DIR}/package_names.h"
    "${CMAKE_CURRENT_LIST_DIR}/reason_names.h"
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

nwb_declare_static_library(nwb_crash_package)
target_sources(nwb_crash_package PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/handler.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/package.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/package_upload.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/handler.h"
    "${CMAKE_CURRENT_LIST_DIR}/internal.h"
    "${CMAKE_CURRENT_LIST_DIR}/io_posix.h"
    "${CMAKE_CURRENT_LIST_DIR}/io_win32.h"
    "${CMAKE_CURRENT_LIST_DIR}/package_internal.h"
    "${CMAKE_CURRENT_LIST_DIR}/package_names.h"
)
target_link_libraries(nwb_crash_package PRIVATE nwb_crash)
target_link_libraries(nwb_crash_package PRIVATE nwb::curl)
if(WIN32)
    target_link_libraries(nwb_crash_package PRIVATE dbghelp)
endif()

if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    nwb_declare_executable(nwb_crash_handler)
    target_sources(nwb_crash_handler PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/main.cpp"
    )
    nwb_target_sources_standalone_runtime(nwb_crash_handler)
    target_link_libraries(nwb_crash_handler PRIVATE nwb_crash_package nwb_common)
    set_target_properties(nwb_crash_handler PROPERTIES OUTPUT_NAME "${NWB_CRASH_HANDLER_OUTPUT_NAME}")
    if(WIN32)
        set_target_properties(nwb_crash_handler PROPERTIES WIN32_EXECUTABLE ON)
    endif()
endif()
