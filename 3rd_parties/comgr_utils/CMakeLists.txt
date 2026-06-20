#######################################################################################################################
### Copyright (c) 2018-2026 Advanced Micro Devices, Inc. All rights reserved.
### @author AMD Developer Tools Team
#######################################################################################################################

cmake_minimum_required (VERSION 3.23)
project (ComgrUtils)

set (COMGR_UTILS_VERSION_MAJOR 1)
set (COMGR_UTILS_VERSION_MINOR 0)

# NWB: amd_comgr is loaded at runtime via dlopen/LoadLibrary (ComgrEntryPoints), so we vendor only its
# header (the flattened top-level 3rd_parties/comgr) and never link the ~135 MB LLVM-based lib. Defining
# COMGR_DYNAMIC_LINKING selects that runtime-loaded code path. (Upstream used FetchContent to download comgr;
# that is removed here so the build is fully offline/vendored. The amd_comgr runtime lib is deployed separately.)
add_library(${PROJECT_NAME} STATIC "source/comgr_utils.cpp")
add_library(AMD::ComgrUtils ALIAS ${PROJECT_NAME})

target_include_directories(${PROJECT_NAME} PUBLIC
    "${PROJECT_SOURCE_DIR}/source"
    "${CMAKE_SOURCE_DIR}/3rd_parties/comgr/include")   # amd_comgr.h, flattened out of external/comgr to top-level

target_compile_definitions(${PROJECT_NAME} PUBLIC COMGR_DYNAMIC_LINKING)
if (NOT WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${CMAKE_DL_LIBS})
endif()

set_property(TARGET ${PROJECT_NAME} PROPERTY POSITION_INDEPENDENT_CODE ON)
