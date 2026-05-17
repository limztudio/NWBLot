nwb_declare_interface_library(nwb_skinned_geometry_types)
target_sources(nwb_skinned_geometry_types INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/skinned_geometry_joint_types.h"
)
