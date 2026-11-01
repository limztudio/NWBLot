nwb_declare_executable(nwb_fbx_to_nwb)
target_sources(nwb_fbx_to_nwb PRIVATE
    "${PROJECT_SOURCE_DIR}/impl/assets_mesh/cook_metadata.cpp"
    "${PROJECT_SOURCE_DIR}/impl/assets_mesh/cook_source_streams.cpp"
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
nwb_target_adhd_sources(nwb_fbx_to_nwb NAMESPACE "NWB::FbxToNwb" SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/types.adhd"
)
target_link_libraries(nwb_fbx_to_nwb PRIVATE nwb_assets_mesh nwb_mesh nwb_common nwb_alloc nwb_cpu_task nwb_metascript nwb_logclient nwb::ufbx nwb::cli11_headers)
target_compile_definitions(nwb_fbx_to_nwb PRIVATE __TBB_NO_IMPLICIT_LINKAGE=1 __TBBMALLOC_NO_IMPLICIT_LINKAGE=1)

