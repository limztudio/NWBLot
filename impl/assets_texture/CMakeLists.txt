nwb_declare_static_library(nwb_assets_texture)
target_sources(nwb_assets_texture PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/asset.h"
    "${CMAKE_CURRENT_LIST_DIR}/binary_payload.h"
    "${CMAKE_CURRENT_LIST_DIR}/format.h"
)
target_link_libraries(nwb_assets_texture PUBLIC nwb_assets)

nwb_declare_static_library(nwb_assets_texture_loader)
target_sources(nwb_assets_texture_loader PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/loader.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/loader.h"
    "${CMAKE_CURRENT_LIST_DIR}/arena_names.h"
)
target_link_libraries(nwb_assets_texture_loader PUBLIC
    nwb_assets_texture
    nwb_graphics
    nwb::basis_universal
)

if(NWB_BUILD_RESOURCE_COOKER)
    nwb_declare_static_library(nwb_assets_texture_cook)
    target_sources(nwb_assets_texture_cook PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/runtime.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/cook.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/asset.h"
        "${CMAKE_CURRENT_LIST_DIR}/binary_payload.h"
        "${CMAKE_CURRENT_LIST_DIR}/cook.h"
        "${CMAKE_CURRENT_LIST_DIR}/format.h"
    )
    target_link_libraries(nwb_assets_texture_cook PUBLIC
        nwb_assets
        nwb_metascript
        nwb_filesystem
    )
    target_compile_definitions(nwb_assets_texture_cook PRIVATE NWB_COOK=1)

    nwb_declare_static_library(nwb_assets_texture_volume_entry)
    target_sources(nwb_assets_texture_volume_entry PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/volume_entry.cpp"
    )
    target_link_libraries(nwb_assets_texture_volume_entry PRIVATE nwb_assets_texture_cook nwb_assets_cook)
    target_compile_definitions(nwb_assets_texture_volume_entry PRIVATE NWB_COOK=1)
endif()
