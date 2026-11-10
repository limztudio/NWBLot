set(_nwb_assets_shader_type_adhd_sources
    "${CMAKE_CURRENT_LIST_DIR}/cook_constants.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/cook_types.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/shader_source_fingerprint.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/slang_compiler_request_validator.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/types.adhd"
)

nwb_declare_static_library(nwb_assets_shader_types)
nwb_target_adhd_sources(nwb_assets_shader_types
    NAMESPACE "NWB::Impl::ShaderBinaryPayload"
    SOURCES
        ${_nwb_assets_shader_type_adhd_sources}
)
target_link_libraries(nwb_assets_shader_types PUBLIC nwb_assets)

set(_nwb_assets_shader_runtime_only_adhd_sources
    "${CMAKE_CURRENT_LIST_DIR}/asset.adhd"
    "${CMAKE_CURRENT_LIST_DIR}/shader_types.adhd"
)

nwb_declare_static_library(nwb_assets_shader)
nwb_target_adhd_sources(nwb_assets_shader
    NAMESPACE "NWB::Impl"
    SOURCES
        ${_nwb_assets_shader_runtime_only_adhd_sources}
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
    file(GLOB _nwb_assets_shader_cook_adhd_sources CONFIGURE_DEPENDS LIST_DIRECTORIES false
        "${CMAKE_CURRENT_LIST_DIR}/*.adhd"
    )
    list(REMOVE_ITEM _nwb_assets_shader_cook_adhd_sources
        ${_nwb_assets_shader_type_adhd_sources}
        ${_nwb_assets_shader_runtime_only_adhd_sources}
        "${CMAKE_CURRENT_LIST_DIR}/loader.adhd"
    )

    nwb_declare_static_library(nwb_assets_shader_cook)
    nwb_target_adhd_sources(nwb_assets_shader_cook
        NAMESPACE "NWB::Impl"
        SOURCES
            ${_nwb_assets_shader_cook_adhd_sources}
    )
    unset(_nwb_assets_shader_cook_adhd_sources)
    target_link_libraries(nwb_assets_shader_cook PUBLIC nwb_assets nwb_assets_cook nwb_assets_shader_types nwb_metascript PRIVATE nwb_shader_bytecode)
    target_compile_definitions(nwb_assets_shader_cook PRIVATE NWB_COOK=1 NWB_SLANGC_EXECUTABLE="${NWB_SLANGC_EXECUTABLE}")
endif()
unset(_nwb_assets_shader_type_adhd_sources)
unset(_nwb_assets_shader_runtime_only_adhd_sources)
