nwb_declare_static_library(nwb_assets_material)
target_sources(nwb_assets_material PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/asset.h"
    "${CMAKE_CURRENT_LIST_DIR}/binary_payload.h"
    "${CMAKE_CURRENT_LIST_DIR}/shader_stage_names.h"
)
target_link_libraries(nwb_assets_material PUBLIC nwb_assets nwb_graphics)

if(NWB_BUILD_RESOURCE_COOKER)
    nwb_declare_static_library(nwb_assets_material_cook)
    target_sources(nwb_assets_material_cook PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/runtime.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/cook.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/material_parse.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/material_bind_codegen.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/material_dispatch_codegen.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/material_generated_shaders.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/bind.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/bind_parse.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/bind_typed_layout.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/asset.h"
        "${CMAKE_CURRENT_LIST_DIR}/cook.h"
        "${CMAKE_CURRENT_LIST_DIR}/cook_private.h"
        "${CMAKE_CURRENT_LIST_DIR}/metadata.h"
        "${CMAKE_CURRENT_LIST_DIR}/bind.h"
        "${CMAKE_CURRENT_LIST_DIR}/bind_private.h"
        "${CMAKE_CURRENT_LIST_DIR}/binary_payload.h"
        "${CMAKE_CURRENT_LIST_DIR}/cook_types.h"
        "${CMAKE_CURRENT_LIST_DIR}/shader_stage_names.h"
    )
    target_link_libraries(nwb_assets_material_cook PUBLIC nwb_assets nwb_metascript nwb_graphics)
    target_compile_definitions(nwb_assets_material_cook PRIVATE NWB_COOK=1)

    nwb_declare_static_library(nwb_assets_material_volume_entry)
    target_sources(nwb_assets_material_volume_entry PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/volume_entry.cpp"
    )
    target_link_libraries(nwb_assets_material_volume_entry PRIVATE nwb_assets_material_cook nwb_assets_cook)
    target_compile_definitions(nwb_assets_material_volume_entry PRIVATE NWB_COOK=1)
endif()
