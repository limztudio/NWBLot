cmake_minimum_required(VERSION 3.25)

project(NWBLot LANGUAGES C CXX)

include("${PROJECT_SOURCE_DIR}/configuration/Project.cmake")
include("${PROJECT_SOURCE_DIR}/configuration/Dependencies.cmake")
include("${PROJECT_SOURCE_DIR}/configuration/InternalHeaders.cmake")
include("${PROJECT_SOURCE_DIR}/configuration/Targets.cmake")

nwb_configure_project()

option(NWB_BUILD_RESOURCE_COOKER "Build the resource cooker tool." ON)
option(NWB_BUILD_LOGSERVER "Build the log server app." ${WIN32})
option(NWB_BUILD_LOADER "Build the loader static library." ${WIN32})
option(NWB_BUILD_TESTBED "Build the testbed app." ${WIN32})

add_subdirectory(3rd_parties)
nwb_configure_external_dependencies()
nwb_configure_internal_headers()

add_subdirectory(logger/client)
add_subdirectory(core)
add_subdirectory(impl)

if(WIN32)
    add_subdirectory(core/frame)
endif()

if(WIN32 AND NWB_BUILD_LOGSERVER)
    add_subdirectory(logger/server)
endif()

if(WIN32 AND NWB_BUILD_LOADER)
    add_subdirectory(loader)
endif()

if(NWB_BUILD_RESOURCE_COOKER)
    add_subdirectory(resource_cooker)
endif()

if(WIN32 AND NWB_BUILD_TESTBED)
    add_subdirectory(Testbed)
endif()

message(STATUS "Configured NWBLot for ${CMAKE_SYSTEM_NAME} (${NWB_OUTPUT_ARCH}) with ${NWB_COMPILER_DESCRIPTION}")
if(NOT WIN32)
    message(STATUS "Windows-only targets are disabled on non-Windows platforms: frame, loader, logserver, testbed")
endif()
