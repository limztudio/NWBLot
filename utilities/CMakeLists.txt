nwb_declare_static_library(nwb_utilities_global)
nwb_target_adhd_sources(nwb_utilities_global NAMESPACE "NWB::Utilities" SOURCES "${CMAKE_CURRENT_LIST_DIR}/*.adhd")

add_subdirectory(fbx_to_nwb)
add_subdirectory(tex_conv)
add_subdirectory(font_builder)
