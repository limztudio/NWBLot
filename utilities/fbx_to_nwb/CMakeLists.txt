file(GLOB _nwb_fbx_to_nwb_lib_adhd CONFIGURE_DEPENDS LIST_DIRECTORIES false "${CMAKE_CURRENT_LIST_DIR}/*.adhd")
list(REMOVE_ITEM _nwb_fbx_to_nwb_lib_adhd "${CMAKE_CURRENT_LIST_DIR}/main.adhd")

nwb_declare_static_library(nwb_fbx_to_nwb_lib)
nwb_target_adhd_sources(nwb_fbx_to_nwb_lib NAMESPACE "NWB::FbxToNwb" SOURCES ${_nwb_fbx_to_nwb_lib_adhd})
target_link_libraries(nwb_fbx_to_nwb_lib PUBLIC nwb_utilities_global nwb_assets_mesh nwb_mesh nwb_common nwb_alloc nwb_cpu_task nwb::ufbx PRIVATE nwb_metascript nwb::cli11_headers)
target_compile_definitions(nwb_fbx_to_nwb_lib PUBLIC __TBB_NO_IMPLICIT_LINKAGE=1 __TBBMALLOC_NO_IMPLICIT_LINKAGE=1)
unset(_nwb_fbx_to_nwb_lib_adhd)

nwb_declare_executable(nwb_fbx_to_nwb)
nwb_target_adhd_sources(nwb_fbx_to_nwb NAMESPACE "NWB::FbxToNwb" SOURCES "${CMAKE_CURRENT_LIST_DIR}/main.adhd")
target_link_libraries(nwb_fbx_to_nwb PUBLIC nwb_utilities_global PRIVATE nwb_fbx_to_nwb_lib nwb_logclient)
