#######################################################################################################################
### Copyright (c) 2018-2026 Advanced Micro Devices, Inc. All rights reserved.
### @author AMD Developer Tools Team
#######################################################################################################################

cmake_minimum_required (VERSION 3.23)
project (ComgrUtils)
include("${CMAKE_CURRENT_LIST_DIR}/../../configuration/UnityBuild.cmake")

set (COMGR_UTILS_VERSION_MAJOR 1)
set (COMGR_UTILS_VERSION_MINOR 0)

# COMGR_DYNAMIC_LINKING loads the separately deployed amd_comgr runtime.
add_library(${PROJECT_NAME} STATIC "source/comgr_utils.cpp")
add_library(AMD::ComgrUtils ALIAS ${PROJECT_NAME})

target_include_directories(${PROJECT_NAME} PUBLIC
    "${PROJECT_SOURCE_DIR}/source")

target_compile_definitions(${PROJECT_NAME} PUBLIC COMGR_DYNAMIC_LINKING)
target_link_libraries(${PROJECT_NAME} PUBLIC nwb::comgr_headers)
if (NOT WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ${CMAKE_DL_LIBS})
endif()

set_property(TARGET ${PROJECT_NAME} PROPERTY POSITION_INDEPENDENT_CODE ON)
nwb_disable_vendor_warnings(${PROJECT_NAME})
