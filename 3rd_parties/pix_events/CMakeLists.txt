# RGD's parser requires the Windows-only PIX event decoder.
cmake_minimum_required(VERSION 3.10)
project(pix_events_decoder)
include("${CMAKE_CURRENT_LIST_DIR}/../../configuration/UnityBuild.cmake")

if(WIN32)
    add_library(pix_events_decoder STATIC
        "decoder/lib/BlockParser.cpp"
        "decoder/lib/EventReading.cpp"
        "decoder/lib/PixEventDecoder.cpp"
        "decoder/lib/pch.cpp")

    target_include_directories(pix_events_decoder PUBLIC
        "${PROJECT_SOURCE_DIR}/decoder/include"
        "${PROJECT_SOURCE_DIR}/include")
    target_include_directories(pix_events_decoder PRIVATE
        "${PROJECT_SOURCE_DIR}"
        "${PROJECT_SOURCE_DIR}/decoder/lib")

    # Clang needs standard headers that the decoder gets transitively with MSVC.
    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        target_compile_options(pix_events_decoder PRIVATE "/FI${PROJECT_SOURCE_DIR}/nwb_force_std_includes.h")
    else()
        target_compile_options(pix_events_decoder PRIVATE "SHELL:-include ${PROJECT_SOURCE_DIR}/nwb_force_std_includes.h")
    endif()
    # windows.h defines min/max macros that collide with std::min/std::max in the decoder.
    target_compile_definitions(pix_events_decoder PRIVATE NOMINMAX)

    set_target_properties(pix_events_decoder PROPERTIES
        CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
        POSITION_INDEPENDENT_CODE ON)
    nwb_disable_vendor_warnings(pix_events_decoder)
endif()
