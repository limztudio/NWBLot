add_test(
    NAME nwb_fbx_to_nwb_regenerate_normals
    COMMAND ${CMAKE_COMMAND}
        -DFBX_TO_NWB_EXE=$<TARGET_FILE:nwb_fbx_to_nwb>
        -DINPUT_FBX=${CMAKE_CURRENT_LIST_DIR}/assets/wedge_hard_edge.fbx
        -DOUTPUT_DIR=${PROJECT_BINARY_DIR}/tests/fbx_to_nwb/regenerate_normals/$<CONFIG>
        -P ${CMAKE_CURRENT_LIST_DIR}/verify_regenerate_normals.cmake
)
