nwb_declare_interface_library(nwb_assets_graphics)
target_link_libraries(nwb_assets_graphics INTERFACE
    nwb_assets_mesh
    nwb_assets_skeleton
    nwb_assets_model
    nwb_assets_material
    nwb_assets_shader
)

if(NWB_BUILD_RESOURCE_COOKER)
    nwb_declare_static_library(nwb_assets_graphics_cook)
    target_sources(nwb_assets_graphics_cook PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/csg_shader_variants.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/csg_shader_variants.h"
        "${CMAKE_CURRENT_LIST_DIR}/material_validation.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/material_validation.h"
        "${CMAKE_CURRENT_LIST_DIR}/shader_cook_plan.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/shader_cook_plan.h"
        "${CMAKE_CURRENT_LIST_DIR}/shader_stage_key.h"
        "${CMAKE_CURRENT_LIST_DIR}/shader_volume_writer.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/shader_volume_writer.h"
        "${CMAKE_CURRENT_LIST_DIR}/volume_prepare.cpp"
    )
    target_link_libraries(nwb_assets_graphics_cook PUBLIC nwb_assets)
    target_link_libraries(nwb_assets_graphics_cook PRIVATE
        nwb_assets_volume_cook
        nwb_assets_csg_cook
        nwb_assets_material_cook
        nwb_assets_shader_cook
        nwb_graphics
        nwb_filesystem
    )
    target_compile_definitions(nwb_assets_graphics_cook PRIVATE NWB_COOK=1)
endif()

