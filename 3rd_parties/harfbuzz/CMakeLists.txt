add_library(nwb_vendor_harfbuzz STATIC
    "${CMAKE_CURRENT_LIST_DIR}/src/harfbuzz.cc"
)
add_library(nwb::harfbuzz ALIAS nwb_vendor_harfbuzz)
target_include_directories(nwb_vendor_harfbuzz SYSTEM PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/src"
)
target_compile_features(nwb_vendor_harfbuzz PRIVATE cxx_std_20)
if(NWB_COMPILER_FRONTEND_MSVC)
    target_compile_options(nwb_vendor_harfbuzz PRIVATE /bigobj)
endif()
nwb_disable_vendor_warnings(nwb_vendor_harfbuzz)
nwb_apply_debug_symbols(nwb_vendor_harfbuzz)
