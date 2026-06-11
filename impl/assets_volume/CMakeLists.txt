if(NWB_BUILD_RESOURCE_COOKER)
    nwb_declare_static_library(nwb_assets_volume_cook)
    target_sources(nwb_assets_volume_cook PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/asset_volume_writer.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/asset_volume_writer.h"
        "${CMAKE_CURRENT_LIST_DIR}/cook_extension.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/cook_extension.h"
        "${CMAKE_CURRENT_LIST_DIR}/cook_paths.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/cook_paths.h"
        "${CMAKE_CURRENT_LIST_DIR}/cook_types.h"
        "${CMAKE_CURRENT_LIST_DIR}/cooker.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/cooker.h"
    )
    target_link_libraries(nwb_assets_volume_cook PUBLIC nwb_assets nwb_assets_cook)
    target_link_libraries(nwb_assets_volume_cook PRIVATE
        nwb_metascript
        nwb_filesystem
    )
    target_compile_definitions(nwb_assets_volume_cook PRIVATE NWB_COOK=1)
endif()
