add_test(NAME nwb_fbx_to_nwb_regenerate_normals COMMAND ${CMAKE_COMMAND} -DFBX_TO_NWB_EXE=$<TARGET_FILE:nwb_fbx_to_nwb> -DINPUT_FBX=${CMAKE_CURRENT_LIST_DIR}/assets/wedge_hard_edge.fbx
        -DOUTPUT_DIR=${PROJECT_BINARY_DIR}/tests/integration/fbx_to_nwb/regenerate_normals/$<CONFIG> -P ${CMAKE_CURRENT_LIST_DIR}/verify_regenerate_normals.cmake)

add_test(NAME nwb_fbx_to_nwb_tangent_generation COMMAND ${CMAKE_COMMAND} -DFBX_TO_NWB_EXE=$<TARGET_FILE:nwb_fbx_to_nwb> -DINPUT_FBX=${CMAKE_CURRENT_LIST_DIR}/assets/wedge_hard_edge.fbx
        -DOUTPUT_DIR=${PROJECT_BINARY_DIR}/tests/integration/fbx_to_nwb/tangent_generation/$<CONFIG> -P ${CMAKE_CURRENT_LIST_DIR}/verify_tangent_generation.cmake)

add_test(NAME nwb_fbx_to_nwb_model_package COMMAND ${CMAKE_COMMAND} -DFBX_TO_NWB_EXE=$<TARGET_FILE:nwb_fbx_to_nwb> -DINPUT_FBX=${CMAKE_CURRENT_LIST_DIR}/assets/wedge_hard_edge.fbx
        -DOUTPUT_DIR=${PROJECT_BINARY_DIR}/tests/integration/fbx_to_nwb/model_package/$<CONFIG> -P ${CMAKE_CURRENT_LIST_DIR}/verify_model_package.cmake)

add_test(NAME nwb_fbx_to_nwb_refresh_trivia COMMAND ${CMAKE_COMMAND} -DFBX_TO_NWB_EXE=$<TARGET_FILE:nwb_fbx_to_nwb>
        -DOUTPUT_DIR=${PROJECT_BINARY_DIR}/tests/integration/fbx_to_nwb/refresh_trivia/$<CONFIG> -P ${CMAKE_CURRENT_LIST_DIR}/verify_refresh_trivia.cmake)
set_tests_properties(nwb_fbx_to_nwb_refresh_trivia PROPERTIES TIMEOUT 120)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME nwb_fbx_to_nwb_delayed_write_failure COMMAND ${CMAKE_COMMAND} -DFBX_TO_NWB_EXE=$<TARGET_FILE:nwb_fbx_to_nwb> -DINPUT_FBX=${CMAKE_CURRENT_LIST_DIR}/assets/wedge_hard_edge.fbx
            -DOUTPUT_DIR=${PROJECT_BINARY_DIR}/tests/integration/fbx_to_nwb/delayed_write_failure/$<CONFIG> -P ${CMAKE_CURRENT_LIST_DIR}/verify_delayed_write_failure.cmake)
    set_tests_properties(nwb_fbx_to_nwb_delayed_write_failure PROPERTIES TIMEOUT 120)
endif()
