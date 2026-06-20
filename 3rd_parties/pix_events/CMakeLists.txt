# NWB-authored CMakeLists for the PIX event decoder static library (RGD decodes PIX-format execution
# markers with it). Upstream ships only a .vcxproj. The decoder uses windows.h/Strsafe.h, so it is built on
# Windows only; for a Vulkan engine its decode path is inert at runtime (markers are Vulkan debug labels),
# but RGD's core parser includes <PixEventDecoder.h> at compile time, so the lib must exist to link.
cmake_minimum_required(VERSION 3.10)
project(pix_events_decoder)

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

    # The vendored decoder relies on MSVC's implicit transitive std includes (std::wstring_view, std::optional);
    # clang needs them explicit, so force-include a small header ahead of every TU (no vendored-source edit).
    target_compile_options(pix_events_decoder PRIVATE "SHELL:-include ${PROJECT_SOURCE_DIR}/nwb_force_std_includes.h")
    # windows.h defines min/max macros that collide with std::min/std::max in the decoder.
    target_compile_definitions(pix_events_decoder PRIVATE NOMINMAX)

    set_target_properties(pix_events_decoder PROPERTIES
        CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
        POSITION_INDEPENDENT_CODE ON)
    nwb_disable_vendor_warnings(pix_events_decoder)
endif()
