add_library(nwb_vendor_xxhash STATIC xxhash.c)
add_library(nwb::xxhash ALIAS nwb_vendor_xxhash)

set_target_properties(nwb_vendor_xxhash PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS OFF)
target_include_directories(nwb_vendor_xxhash SYSTEM PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
target_compile_definitions(nwb_vendor_xxhash PUBLIC XXH_NAMESPACE=NWB_XXHASH_)
nwb_disable_vendor_warnings(nwb_vendor_xxhash)
