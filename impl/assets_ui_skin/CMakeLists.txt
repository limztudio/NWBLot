nwb_declare_static_library(nwb_assets_ui_skin)
target_sources(nwb_assets_ui_skin PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/region_index.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/toolkit_contract.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/asset.h"
    "${CMAKE_CURRENT_LIST_DIR}/binary_payload.h"
    "${CMAKE_CURRENT_LIST_DIR}/toolkit_contract.h"
)
target_link_libraries(nwb_assets_ui_skin PUBLIC nwb_assets)

if(NWB_BUILD_PIPELINE)
    nwb_declare_static_library(nwb_assets_ui_skin_cook)
    target_sources(nwb_assets_ui_skin_cook PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/runtime.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/region_index.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/toolkit_contract.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/cook.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/cook_metadata.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/dependencies.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/dependencies.h"
        "${CMAKE_CURRENT_LIST_DIR}/asset.h"
        "${CMAKE_CURRENT_LIST_DIR}/binary_payload.h"
        "${CMAKE_CURRENT_LIST_DIR}/cook.h"
        "${CMAKE_CURRENT_LIST_DIR}/toolkit_contract.h"
    )
    target_link_libraries(nwb_assets_ui_skin_cook PUBLIC nwb_assets nwb_metascript)
    target_compile_definitions(nwb_assets_ui_skin_cook PRIVATE NWB_COOK=1)

    nwb_declare_static_library(nwb_assets_ui_skin_volume_entry)
    target_sources(nwb_assets_ui_skin_volume_entry PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/volume_entry.cpp"
    )
    target_link_libraries(nwb_assets_ui_skin_volume_entry PRIVATE nwb_assets_ui_skin_cook nwb_assets_cook)
    target_compile_definitions(nwb_assets_ui_skin_volume_entry PRIVATE NWB_COOK=1)
endif()

