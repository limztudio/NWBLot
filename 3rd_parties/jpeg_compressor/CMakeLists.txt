add_library(nwb_vendor_jpeg_compressor STATIC
    "${CMAKE_CURRENT_LIST_DIR}/jpgd.cpp"
)
add_library(nwb::jpeg_compressor ALIAS nwb_vendor_jpeg_compressor)
target_include_directories(nwb_vendor_jpeg_compressor SYSTEM PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}"
)
target_compile_features(nwb_vendor_jpeg_compressor PRIVATE cxx_std_20)
target_compile_definitions(nwb_vendor_jpeg_compressor PRIVATE JPGD_USE_SSE2=0)
nwb_disable_vendor_warnings(nwb_vendor_jpeg_compressor)
nwb_apply_debug_symbols(nwb_vendor_jpeg_compressor)
