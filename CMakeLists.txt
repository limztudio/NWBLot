cmake_minimum_required(VERSION 3.25)

project(NWBLot LANGUAGES C CXX)

include("${PROJECT_SOURCE_DIR}/configuration/Project.cmake")
include("${PROJECT_SOURCE_DIR}/configuration/Dependencies.cmake")
include("${PROJECT_SOURCE_DIR}/configuration/InternalHeaders.cmake")
include("${PROJECT_SOURCE_DIR}/configuration/Targets.cmake")

nwb_configure_project()

set(NWB_BUILD_LOGSERVER_DEFAULT OFF)
if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(NWB_BUILD_LOGSERVER_DEFAULT ON)
endif()
set(NWB_BUILD_LOADER_DEFAULT OFF)
if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(NWB_BUILD_LOADER_DEFAULT ON)
endif()
set(NWB_ENABLE_WAYLAND_DEFAULT OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(NWB_ENABLE_WAYLAND_DEFAULT ON)
endif()

option(NWB_BUILD_RESOURCE_COOKER "Build the resource cooker tool." ON)
option(NWB_BUILD_LOGSERVER "Build the log server app." ${NWB_BUILD_LOGSERVER_DEFAULT})
option(NWB_BUILD_LOADER "Build the loader static library." ${NWB_BUILD_LOADER_DEFAULT})
option(NWB_BUILD_TESTBED "Build the testbed app." ${WIN32})
option(NWB_ENABLE_WAYLAND "Build the native Wayland backend on Linux when dependencies are available." ${NWB_ENABLE_WAYLAND_DEFAULT})

add_subdirectory(3rd_parties)
nwb_configure_external_dependencies()
nwb_configure_internal_headers()

add_subdirectory(logger/client)
add_subdirectory(core)
add_subdirectory(impl)

if(WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_subdirectory(core/frame)
endif()

if(NWB_BUILD_LOGSERVER AND (WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux"))
    add_subdirectory(logger/server)
endif()

if((WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux") AND NWB_BUILD_LOADER)
    add_subdirectory(loader)
endif()

if(NWB_BUILD_RESOURCE_COOKER)
    add_subdirectory(resource_cooker)
endif()

if(WIN32 AND NWB_BUILD_TESTBED)
    add_subdirectory(Testbed)
endif()

message(STATUS "Configured NWBLot for ${CMAKE_SYSTEM_NAME} (${NWB_OUTPUT_ARCH}) with ${NWB_COMPILER_DESCRIPTION}")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(STATUS "Linux targets enabled: frame, loader, logserver. Testbed remains Windows-only.")
elseif(NOT WIN32)
    message(STATUS "Windows-only targets are disabled on non-Windows platforms: testbed")
endif()
