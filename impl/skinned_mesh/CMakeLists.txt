nwb_declare_interface_library(nwb_skinned_mesh_types)
target_sources(nwb_skinned_mesh_types INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/joint_types.h"
)
