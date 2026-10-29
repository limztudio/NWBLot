nwb_declare_static_library(nwb_assets_shader_types)
nwb_target_adhd_sources(nwb_assets_shader_types
    NAMESPACE NWB::Impl::ShaderBinaryPayload
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/types.adhd"
)
target_link_libraries(nwb_assets_shader_types PUBLIC nwb_assets)

nwb_declare_static_library(nwb_assets_shader)
target_sources(nwb_assets_shader PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/*.hh"
    "${CMAKE_CURRENT_LIST_DIR}/*.hxx"
    "${CMAKE_CURRENT_LIST_DIR}/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.inl"
    "${CMAKE_CURRENT_LIST_DIR}/*.ipp"
)
target_link_libraries(nwb_assets_shader PUBLIC nwb_assets nwb_assets_shader_types PRIVATE nwb_shader_bytecode)

nwb_declare_interface_library(nwb_assets_shader_loader)
target_sources(nwb_assets_shader_loader INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/loader.h"
)
target_link_libraries(nwb_assets_shader_loader INTERFACE nwb_assets_shader nwb_graphics)

if(NWB_BUILD_PIPELINE)
    nwb_declare_static_library(nwb_assets_shader_cook)
    target_sources(nwb_assets_shader_cook PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/*.c"
        "${CMAKE_CURRENT_LIST_DIR}/*.cc"
        "${CMAKE_CURRENT_LIST_DIR}/*.cxx"
        "${CMAKE_CURRENT_LIST_DIR}/*.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/*.h"
        "${CMAKE_CURRENT_LIST_DIR}/*.hh"
        "${CMAKE_CURRENT_LIST_DIR}/*.hxx"
        "${CMAKE_CURRENT_LIST_DIR}/*.hpp"
        "${CMAKE_CURRENT_LIST_DIR}/*.inl"
        "${CMAKE_CURRENT_LIST_DIR}/*.ipp"
        "${CMAKE_CURRENT_LIST_DIR}/*.ixx"
    )
    target_link_libraries(nwb_assets_shader_cook PUBLIC nwb_assets nwb_assets_shader_types nwb_metascript PRIVATE nwb_shader_bytecode)
    target_compile_definitions(nwb_assets_shader_cook PRIVATE NWB_COOK=1 NWB_SLANGC_EXECUTABLE="${NWB_SLANGC_EXECUTABLE}")
endif()
