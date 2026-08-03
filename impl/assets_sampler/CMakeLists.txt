nwb_declare_static_library(nwb_assets_sampler)
target_sources(nwb_assets_sampler PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/asset.h"
    "${CMAKE_CURRENT_LIST_DIR}/binary_payload.h"
)
target_link_libraries(nwb_assets_sampler PUBLIC nwb_assets nwb_graphics)

nwb_declare_static_library(nwb_assets_sampler_loader)
target_sources(nwb_assets_sampler_loader PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/loader.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/loader.h"
)
target_link_libraries(nwb_assets_sampler_loader PUBLIC
    nwb_assets_sampler
    nwb_graphics
)

if(NWB_BUILD_RESOURCE_COOKER)
    nwb_declare_static_library(nwb_assets_sampler_cook)
    target_sources(nwb_assets_sampler_cook PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/runtime.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/cook.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/asset.h"
        "${CMAKE_CURRENT_LIST_DIR}/binary_payload.h"
        "${CMAKE_CURRENT_LIST_DIR}/cook.h"
    )
    target_link_libraries(nwb_assets_sampler_cook PUBLIC
        nwb_assets
        nwb_graphics
        nwb_metascript
    )
    target_compile_definitions(nwb_assets_sampler_cook PRIVATE NWB_COOK=1)

    nwb_declare_static_library(nwb_assets_sampler_volume_entry)
    target_sources(nwb_assets_sampler_volume_entry PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/volume_entry.cpp"
    )
    target_link_libraries(nwb_assets_sampler_volume_entry PRIVATE nwb_assets_sampler_cook nwb_assets_cook)
    target_compile_definitions(nwb_assets_sampler_volume_entry PRIVATE NWB_COOK=1)
endif()
