add_test(NAME nwb_fbx_to_nwb_regenerate_normals COMMAND ${CMAKE_COMMAND} -DFBX_TO_NWB_EXE=$<TARGET_FILE:nwb_fbx_to_nwb> -DINPUT_FBX=${CMAKE_CURRENT_LIST_DIR}/assets/wedge_hard_edge.fbx
        -DOUTPUT_DIR=${PROJECT_BINARY_DIR}/tests/integration/fbx_to_nwb/regenerate_normals/$<CONFIG> -P ${CMAKE_CURRENT_LIST_DIR}/verify_regenerate_normals.cmake)

add_test(NAME nwb_fbx_to_nwb_tangent_generation COMMAND ${CMAKE_COMMAND} -DFBX_TO_NWB_EXE=$<TARGET_FILE:nwb_fbx_to_nwb> -DINPUT_FBX=${CMAKE_CURRENT_LIST_DIR}/assets/wedge_hard_edge.fbx
        -DOUTPUT_DIR=${PROJECT_BINARY_DIR}/tests/integration/fbx_to_nwb/tangent_generation/$<CONFIG> -P ${CMAKE_CURRENT_LIST_DIR}/verify_tangent_generation.cmake)

add_test(NAME nwb_fbx_to_nwb_model_package COMMAND ${CMAKE_COMMAND} -DFBX_TO_NWB_EXE=$<TARGET_FILE:nwb_fbx_to_nwb> -DINPUT_FBX=${CMAKE_CURRENT_LIST_DIR}/assets/wedge_hard_edge.fbx
        -DOUTPUT_DIR=${PROJECT_BINARY_DIR}/tests/integration/fbx_to_nwb/model_package/$<CONFIG> -P ${CMAKE_CURRENT_LIST_DIR}/verify_model_package.cmake)

set(_nwb_fbx_to_nwb_integration_timeout_seconds 120)

add_test(NAME nwb_fbx_to_nwb_refresh_trivia COMMAND ${CMAKE_COMMAND} -DFBX_TO_NWB_EXE=$<TARGET_FILE:nwb_fbx_to_nwb>
        -DOUTPUT_DIR=${PROJECT_BINARY_DIR}/tests/integration/fbx_to_nwb/refresh_trivia/$<CONFIG> -P ${CMAKE_CURRENT_LIST_DIR}/verify_refresh_trivia.cmake)
set_tests_properties(nwb_fbx_to_nwb_refresh_trivia PROPERTIES TIMEOUT ${_nwb_fbx_to_nwb_integration_timeout_seconds})

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME nwb_fbx_to_nwb_delayed_write_failure COMMAND ${CMAKE_COMMAND} -DFBX_TO_NWB_EXE=$<TARGET_FILE:nwb_fbx_to_nwb> -DINPUT_FBX=${CMAKE_CURRENT_LIST_DIR}/assets/wedge_hard_edge.fbx
            -DOUTPUT_DIR=${PROJECT_BINARY_DIR}/tests/integration/fbx_to_nwb/delayed_write_failure/$<CONFIG> -P ${CMAKE_CURRENT_LIST_DIR}/verify_delayed_write_failure.cmake)
    set_tests_properties(nwb_fbx_to_nwb_delayed_write_failure PROPERTIES TIMEOUT ${_nwb_fbx_to_nwb_integration_timeout_seconds})
endif()
unset(_nwb_fbx_to_nwb_integration_timeout_seconds)

nwb_declare_gtest_executable(nwb_fbx_skin_tests)
target_sources(nwb_fbx_skin_tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/skin_tests.cpp" "${PROJECT_SOURCE_DIR}/utilities/fbx_to_nwb/skin.cpp" "${PROJECT_SOURCE_DIR}/utilities/fbx_to_nwb/asset_writer_skeleton.cpp" "${PROJECT_SOURCE_DIR}/utilities/fbx_to_nwb/source_mesh_streams.cpp")
nwb_target_adhd_sources(nwb_fbx_skin_tests NAMESPACE "NWB::FbxToNwb" SOURCES "${PROJECT_SOURCE_DIR}/utilities/fbx_to_nwb/types.adhd")
target_link_libraries(nwb_fbx_skin_tests PRIVATE nwb_assets_mesh nwb_mesh nwb_common nwb_alloc nwb_cpu_task nwb::ufbx)

