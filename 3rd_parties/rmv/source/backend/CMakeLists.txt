# NWB-rewritten RmvBackend CMakeLists for the offline/vendored RGD build. The RMT backend (data set, resource
# history, memory-event history) backs RGD's page-fault resource-history section. Differences from upstream:
# built as a plain static lib without devtools_target_options (its -Werror would turn vendored warnings into
# errors under clang), include paths target the flattened 3rd_parties layout, system_info supplies the system
# info + driver-overrides readers, and the CRT / iterator-debug level is matched to the engine by the parent
# via nwb_match_engine_runtime.
cmake_minimum_required(VERSION 3.10)
project(RmvBackend)

set(_3P "${CMAKE_SOURCE_DIR}/3rd_parties")

file(GLOB RMV_BACKEND_SRC "${PROJECT_SOURCE_DIR}/*.cpp")
# rmt_include_pevents.cpp is the Linux-only pevents shim (Windows uses native synchronization primitives).
if(WIN32)
    list(FILTER RMV_BACKEND_SRC EXCLUDE REGEX "rmt_include_pevents")
endif()

add_library(RmvBackend STATIC ${RMV_BACKEND_SRC})

target_include_directories(RmvBackend
    PUBLIC "${PROJECT_SOURCE_DIR}"
    PRIVATE
        "${_3P}"                              # rdf/inc/amdrdf.h, system_info_utils/source/*.h, pevents/src/* (flattened)
        "${PROJECT_SOURCE_DIR}/../parser")    # sibling RMT parser headers

# The RMT backend's RDF code paths and the system-info / driver-overrides readers it parses are gated on these.
target_compile_definitions(RmvBackend PRIVATE SYSTEM_INFO_ENABLE_RDF DRIVER_OVERRIDES_ENABLE_RDF NOMINMAX)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(RmvBackend PRIVATE _LINUX)
endif()

target_link_libraries(RmvBackend PUBLIC RmvParser system_info amdrdf)
target_link_libraries(RmvBackend PRIVATE nwb::pevents_headers)

set_target_properties(RmvBackend PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)
nwb_disable_vendor_warnings(RmvBackend)
