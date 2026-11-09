nwb_declare_executable(nwb_reflection_smoke)
nwb_target_adhd_sources(nwb_reflection_smoke NAMESPACE "NWB::Tests::Smoke" SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/reflection_project.adhd"
        )
target_compile_definitions(nwb_reflection_smoke PRIVATE NWB_SMOKE_ENABLE_SWAP_CHAIN_READBACK=1)
nwb_configure_runtime_smoke_target(nwb_reflection_smoke "${_nwb_transparent_multi_runtime_root}" nwb_ecs_model nwb_ecs_mesh_skinning)
# Only the temporal suite's light mutation needs its lit BXDF material, so other reflection runs keep the shared catalog.
set(_nwb_reflection_temporal_runtime_root "${CMAKE_BINARY_DIR}/Testing/reflection_temporal_smoke_runtime/$<CONFIG>")
nwb_add_fixture_smoke_assets(nwb_reflection_temporal_smoke_assets reflection_temporal "${_nwb_reflection_temporal_runtime_root}" "reflection temporal" reflection_temporal_assets)
add_dependencies(nwb_reflection_smoke nwb_transparent_multi_smoke_assets nwb_reflection_temporal_smoke_assets)

include("${CMAKE_CURRENT_LIST_DIR}/renderer_gather_benchmark.cmake")

if(TARGET nwb_logserver)
    set(_nwb_reflection_logserver_args "--logserver-executable" "$<TARGET_FILE:nwb_logserver>")
else()
    set(_nwb_reflection_logserver_args)
endif()
add_test(NAME nwb_reflection_capture_smoke COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/reflection_smoke.py" "--executable" "$<TARGET_FILE:nwb_reflection_smoke>" "--working-directory" "${_nwb_transparent_multi_runtime_root}"
        "--output-directory" "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/reflection" ${_nwb_reflection_logserver_args})
set_tests_properties(nwb_reflection_capture_smoke PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT ${_nwb_reflection_capture_smoke_timeout_seconds})
if(TARGET nwb_logserver)
    add_test(NAME nwb_reflection_temporal_omission_smoke COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/reflection_temporal_omission_smoke.py" "--executable" "$<TARGET_FILE:nwb_reflection_smoke>"
            "--working-directory" "${_nwb_transparent_multi_runtime_root}" "--logserver-executable" "$<TARGET_FILE:nwb_logserver>" "--output-directory" "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/reflection_temporal_omission")
    set_tests_properties(nwb_reflection_temporal_omission_smoke PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT ${_nwb_reflection_temporal_omission_smoke_timeout_seconds})
endif()

add_test(NAME nwb_csg_reflection_capture_smoke COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/csg_optics_smoke.py" "--effect" "reflection" "--executable" "$<TARGET_FILE:nwb_reflection_smoke>"
        "--working-directory" "${_nwb_transparent_multi_runtime_root}" "--output-directory" "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/csg_reflection" ${_nwb_reflection_logserver_args})
set_tests_properties(nwb_csg_reflection_capture_smoke PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT ${_nwb_csg_reflection_capture_smoke_timeout_seconds})

add_test(NAME nwb_csg_reflection_collection_reuse_capture_smoke COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/reflection_optical_smoke.py" "--csg-reuse" "--executable" "$<TARGET_FILE:nwb_reflection_smoke>"
        "--working-directory" "${_nwb_transparent_multi_runtime_root}" "--output-root" "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/csg_reflection_collection_reuse" "--timeout" "${_nwb_csg_reflection_reuse_script_timeout_seconds}"
            ${_nwb_reflection_logserver_args})
set_tests_properties(nwb_csg_reflection_collection_reuse_capture_smoke PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT ${_nwb_csg_reflection_capture_smoke_timeout_seconds})

add_test(NAME nwb_reflection_queue_boundary_capture_smoke COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/reflection_optical_smoke.py" "--queue-boundaries" "--executable" "$<TARGET_FILE:nwb_reflection_smoke>"
        "--working-directory" "${_nwb_transparent_multi_runtime_root}" "--output-root" "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/reflection_queue_boundaries" ${_nwb_reflection_logserver_args})
set_tests_properties(nwb_reflection_queue_boundary_capture_smoke PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT ${_nwb_reflection_queue_boundary_smoke_timeout_seconds})

foreach(_nwb_reflection_suite IN ITEMS rough temporal optical feedback feedback_diagnostics_off)
    string(REPLACE "_" "-" _nwb_reflection_suite_argument "${_nwb_reflection_suite}")
    set(_nwb_reflection_suite_runtime_root "${_nwb_transparent_multi_runtime_root}")
    if(_nwb_reflection_suite STREQUAL "temporal")
        set(_nwb_reflection_suite_runtime_root "${_nwb_reflection_temporal_runtime_root}")
    endif()
    add_test(NAME "nwb_reflection_${_nwb_reflection_suite}_capture_smoke" COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/reflection_smoke.py" "--executable" "$<TARGET_FILE:nwb_reflection_smoke>"
            "--working-directory" "${_nwb_reflection_suite_runtime_root}" "--output-directory" "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/reflection_${_nwb_reflection_suite}" "--suite" "${_nwb_reflection_suite_argument}"
            ${_nwb_reflection_logserver_args})
    set_tests_properties("nwb_reflection_${_nwb_reflection_suite}_capture_smoke" PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT ${_nwb_reflection_suite_smoke_timeout_seconds})
endforeach()
unset(_nwb_reflection_suite_runtime_root)
unset(_nwb_reflection_temporal_runtime_root)
