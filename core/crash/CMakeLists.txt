nwb_declare_static_library(nwb_crash)
nwb_target_adhd_sources(nwb_crash NAMESPACE "NWB::Core::Crash::CrashArenaScope" SOURCES "${CMAKE_CURRENT_LIST_DIR}/arena_names.adhd")
nwb_declare_static_library(nwb_crash_package_names)
nwb_target_adhd_sources(nwb_crash_package_names NAMESPACE "NWB::Core::Crash::PackageNames" SOURCES "${CMAKE_CURRENT_LIST_DIR}/package_names.adhd")
nwb_declare_static_library(nwb_crash_reason_names)
nwb_target_adhd_sources(nwb_crash_reason_names NAMESPACE "NWB::Core::Crash::CrashReasonNames" SOURCES "${CMAKE_CURRENT_LIST_DIR}/reason_names.adhd")
nwb_declare_static_library(nwb_crash_types)
nwb_target_adhd_sources(nwb_crash_types NAMESPACE "NWB::Core::Crash::CrashTypes" SOURCES "${CMAKE_CURRENT_LIST_DIR}/crash_types.adhd" "${CMAKE_CURRENT_LIST_DIR}/internal_values.adhd")
set(NWB_CRASH_HANDLER_OUTPUT_NAME "crash_handler")
target_compile_definitions(nwb_crash PRIVATE NWB_CRASH_HANDLER_EXECUTABLE_NAME="${NWB_CRASH_HANDLER_OUTPUT_NAME}")
target_sources(nwb_crash PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/module.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/diagnostic_capture_policy.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/request.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/protocol.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/spool.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/*.hh"
    "${CMAKE_CURRENT_LIST_DIR}/*.hxx"
    "${CMAKE_CURRENT_LIST_DIR}/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.inl"
    "${CMAKE_CURRENT_LIST_DIR}/*.ipp"
)
if(WIN32)
    target_sources(nwb_crash PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/platform_win32.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/platform_win32_transport.cpp"
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Android")
    target_sources(nwb_crash PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/platform_posix.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/posix_callstack_capture.cpp"
    )
endif()
target_link_libraries(nwb_crash PUBLIC nwb_alloc)
target_link_libraries(nwb_crash PUBLIC nwb_crash_package_names nwb_crash_reason_names)
target_link_libraries(nwb_crash PUBLIC nwb_crash_types)

nwb_declare_static_library(nwb_crash_package_values)
nwb_target_adhd_sources(nwb_crash_package_values NAMESPACE "NWB::Core::Crash::PackageValues" SOURCES "${CMAKE_CURRENT_LIST_DIR}/package_values.adhd")

nwb_declare_static_library(nwb_crash_package)
target_sources(nwb_crash_package PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/handler.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/package*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/*.hh"
    "${CMAKE_CURRENT_LIST_DIR}/*.hxx"
    "${CMAKE_CURRENT_LIST_DIR}/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.inl"
    "${CMAKE_CURRENT_LIST_DIR}/*.ipp"
)
target_link_libraries(nwb_crash_package PRIVATE nwb_crash)
target_link_libraries(nwb_crash_package PUBLIC nwb_crash_package_names nwb_crash_reason_names nwb_crash_types nwb_crash_package_values)
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

