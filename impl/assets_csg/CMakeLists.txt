nwb_declare_interface_library(nwb_assets_csg)

if(NWB_BUILD_RESOURCE_COOKER)
    nwb_declare_static_library(nwb_assets_csg_cook)
    target_sources(nwb_assets_csg_cook PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/cook.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/cook.h"
    )
    target_link_libraries(nwb_assets_csg_cook PUBLIC
        nwb_assets
        nwb_assets_shader_cook
        nwb_metascript
        nwb_filesystem
    )
    target_compile_definitions(nwb_assets_csg_cook PRIVATE NWB_COOK=1)
endif()
