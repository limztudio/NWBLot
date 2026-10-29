nwb_declare_static_library(nwb_ecs_mesh)
target_sources(nwb_ecs_mesh PRIVATE
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
    "${CMAKE_CURRENT_LIST_DIR}/runtime/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/runtime/*.cc"
    "${CMAKE_CURRENT_LIST_DIR}/runtime/*.cxx"
    "${CMAKE_CURRENT_LIST_DIR}/runtime/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/runtime/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/runtime/*.hh"
    "${CMAKE_CURRENT_LIST_DIR}/runtime/*.hxx"
    "${CMAKE_CURRENT_LIST_DIR}/runtime/*.hpp"
    "${CMAKE_CURRENT_LIST_DIR}/runtime/*.inl"
    "${CMAKE_CURRENT_LIST_DIR}/runtime/*.ipp"
    "${CMAKE_CURRENT_LIST_DIR}/runtime/*.ixx"
)
nwb_target_adhd_sources(nwb_ecs_mesh
    NAMESPACE "NWB::Impl"
    SOURCES
        "${CMAKE_CURRENT_LIST_DIR}/*.adhd"
        "${CMAKE_CURRENT_LIST_DIR}/runtime/*.adhd"
    DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/runtime/*.adhd"
)
target_link_libraries(nwb_ecs_mesh PUBLIC nwb_ecs nwb_assets nwb_graphics nwb_assets_skeleton_types nwb_assets_mesh_types)

add_subdirectory(skinning)
