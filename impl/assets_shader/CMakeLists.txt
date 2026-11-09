nwb_declare_static_library(nwb_assets_shader_types)
nwb_target_adhd_sources(nwb_assets_shader_types
    NAMESPACE "NWB::Impl::ShaderBinaryPayload"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/cook_constants.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/cook_types.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/shader_source_fingerprint.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/slang_compiler_request_validator.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/types.adhd"
)
target_link_libraries(nwb_assets_shader_types PUBLIC nwb_assets)

nwb_declare_static_library(nwb_assets_shader)
nwb_target_adhd_sources(nwb_assets_shader
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/asset.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/shader_types.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/runtime.adhd"
)
target_link_libraries(nwb_assets_shader PUBLIC nwb_assets nwb_assets_shader_types PRIVATE nwb_shader_bytecode)

nwb_declare_static_library(nwb_assets_shader_loader)
nwb_target_adhd_sources(nwb_assets_shader_loader
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/loader.adhd"
)
target_link_libraries(nwb_assets_shader_loader PUBLIC nwb_assets_shader nwb_graphics)

if(NWB_BUILD_PIPELINE)
    nwb_declare_static_library(nwb_assets_shader_cook)
    nwb_target_adhd_sources(nwb_assets_shader_cook
        NAMESPACE "NWB::Impl"
        SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/runtime.adhd"
            "${CMAKE_CURRENT_LIST_DIR}/cook.adhd"
            "${CMAKE_CURRENT_LIST_DIR}/shader_define_planner.adhd"
            "${CMAKE_CURRENT_LIST_DIR}/slang_compiler.adhd"
            "${CMAKE_CURRENT_LIST_DIR}/source_dependencies.adhd"
    )
    target_link_libraries(nwb_assets_shader_cook PUBLIC nwb_assets nwb_assets_cook nwb_assets_shader_types nwb_metascript PRIVATE nwb_shader_bytecode)
    target_compile_definitions(nwb_assets_shader_cook PRIVATE NWB_COOK=1 NWB_SLANGC_EXECUTABLE="${NWB_SLANGC_EXECUTABLE}")
endif()
