nwb_declare_static_library(nwb_assets_subsystem_types)
nwb_target_adhd_sources(nwb_assets_subsystem_types
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/*.adhd"
)
target_link_libraries(nwb_assets_subsystem_types PUBLIC nwb_assets)

nwb_declare_static_library(nwb_assets_subsystem)
target_sources(nwb_assets_subsystem PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/*.hh"
    "${CMAKE_CURRENT_LIST_DIR}/*.hxx"
    "${CMAKE_CURRENT_LIST_DIR}/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.inl"
    "${CMAKE_CURRENT_LIST_DIR}/*.ipp"
)
target_link_libraries(nwb_assets_subsystem PUBLIC nwb_assets nwb_assets_subsystem_types)

if(NWB_BUILD_PIPELINE)
    nwb_declare_static_library(nwb_assets_subsystem_cook)
    target_sources(nwb_assets_subsystem_cook PRIVATE
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
    target_link_libraries(nwb_assets_subsystem_cook PUBLIC nwb_assets nwb_assets_cook nwb_assets_subsystem_types nwb_metascript)
    target_compile_definitions(nwb_assets_subsystem_cook PRIVATE NWB_COOK=1)
endif()
