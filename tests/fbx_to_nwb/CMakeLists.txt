add_test(
    NAME nwb_fbx_to_nwb_regenerate_normals
    COMMAND ${CMAKE_COMMAND}
        -DFBX_TO_NWB_EXE=$<TARGET_FILE:nwb_fbx_to_nwb>
        -DINPUT_FBX=${CMAKE_CURRENT_LIST_DIR}/assets/wedge_hard_edge.fbx
        -DOUTPUT_DIR=${PROJECT_BINARY_DIR}/tests/fbx_to_nwb/regenerate_normals/$<CONFIG>
        -P ${CMAKE_CURRENT_LIST_DIR}/verify_regenerate_normals.cmake
)

add_test(
    NAME nwb_fbx_to_nwb_tangent_generation
    COMMAND ${CMAKE_COMMAND}
        -DFBX_TO_NWB_EXE=$<TARGET_FILE:nwb_fbx_to_nwb>
        -DINPUT_FBX=${CMAKE_CURRENT_LIST_DIR}/assets/wedge_hard_edge.fbx
        -DOUTPUT_DIR=${PROJECT_BINARY_DIR}/tests/fbx_to_nwb/tangent_generation/$<CONFIG>
        -P ${CMAKE_CURRENT_LIST_DIR}/verify_tangent_generation.cmake
)

add_test(
    NAME nwb_fbx_to_nwb_model_package
    COMMAND ${CMAKE_COMMAND}
        -DFBX_TO_NWB_EXE=$<TARGET_FILE:nwb_fbx_to_nwb>
        -DINPUT_FBX=${CMAKE_CURRENT_LIST_DIR}/assets/wedge_hard_edge.fbx
        -DOUTPUT_DIR=${PROJECT_BINARY_DIR}/tests/fbx_to_nwb/model_package/$<CONFIG>
        -P ${CMAKE_CURRENT_LIST_DIR}/verify_model_package.cmake
)
