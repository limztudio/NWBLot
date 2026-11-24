include("${PROJECT_SOURCE_DIR}/configuration/UnityBuild.cmake")

add_library(nwb_vendor_blake3 STATIC
    src/blake3.c
    src/blake3_dispatch.c
    src/blake3_portable.c
)
add_library(nwb::blake3 ALIAS nwb_vendor_blake3)

if(NWB_OUTPUT_ARCH STREQUAL "arm64")
    target_sources(nwb_vendor_blake3 PRIVATE src/blake3_neon.c)
endif()

set_target_properties(nwb_vendor_blake3 PROPERTIES
    C_STANDARD 17
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO
)
target_include_directories(nwb_vendor_blake3 SYSTEM PUBLIC "${CMAKE_CURRENT_LIST_DIR}/include")
target_compile_definitions(nwb_vendor_blake3 PRIVATE
    BLAKE3_NO_AVX512
    BLAKE3_NO_AVX2
    BLAKE3_NO_SSE41
    BLAKE3_NO_SSE2
)
nwb_disable_vendor_warnings(nwb_vendor_blake3)
nwb_apply_debug_symbols(nwb_vendor_blake3)
