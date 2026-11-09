nwb_declare_executable(nwb_refraction_smoke)
nwb_target_adhd_sources(nwb_refraction_smoke NAMESPACE "NWB::Tests::Smoke" SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/refraction_project.adhd"
        )
target_compile_definitions(nwb_refraction_smoke PRIVATE NWB_SMOKE_ENABLE_SWAP_CHAIN_READBACK=1)
nwb_configure_runtime_smoke_target(nwb_refraction_smoke "${_nwb_transparent_multi_runtime_root}")
add_dependencies(nwb_refraction_smoke nwb_transparent_multi_smoke_assets)

if(TARGET nwb_logserver)
    set(_nwb_refraction_logserver_args "--logserver-executable" "$<TARGET_FILE:nwb_logserver>")
else()
    set(_nwb_refraction_logserver_args)
endif()
add_test(NAME nwb_refraction_capture_smoke COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/refraction_capture_smoke.py" "--executable" "$<TARGET_FILE:nwb_refraction_smoke>" "--working-directory" "${_nwb_transparent_multi_runtime_root}"
        "--output-directory" "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/refraction" ${_nwb_refraction_logserver_args})
set_tests_properties(nwb_refraction_capture_smoke PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT ${_nwb_refraction_capture_smoke_timeout_seconds})
add_test(NAME nwb_refraction_gpudbg_capture_smoke COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/refraction_capture_smoke.py" "--executable" "$<TARGET_FILE:nwb_refraction_smoke>"
        "--working-directory" "${_nwb_transparent_multi_runtime_root}" "--output-directory" "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/refraction_gpudbg" "--application-arg=--gpudbg" ${_nwb_refraction_logserver_args} CONFIGURATIONS dbg)
set_tests_properties(nwb_refraction_gpudbg_capture_smoke PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT ${_nwb_refraction_capture_smoke_timeout_seconds})
add_test(NAME nwb_refraction_lifecycle_capture_smoke COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/refraction_lifecycle_smoke.py" "--executable" "$<TARGET_FILE:nwb_refraction_smoke>"
        "--working-directory" "${_nwb_transparent_multi_runtime_root}" "--output-directory" "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/refraction_lifecycle" ${_nwb_refraction_logserver_args})
set_tests_properties(nwb_refraction_lifecycle_capture_smoke PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT ${_nwb_refraction_lifecycle_smoke_timeout_seconds})
add_test(NAME nwb_refraction_density_capture_smoke COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/refraction_density_capture_smoke.py" "--executable" "$<TARGET_FILE:nwb_refraction_smoke>"
        "--working-directory" "${_nwb_transparent_multi_runtime_root}" "--output-directory" "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/refraction_density" ${_nwb_refraction_logserver_args})
set_tests_properties(nwb_refraction_density_capture_smoke PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT ${_nwb_refraction_medium_smoke_timeout_seconds})
add_test(NAME nwb_refraction_query_diagnostics_smoke COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/refraction_query_diagnostics_smoke.py" "--executable" "$<TARGET_FILE:nwb_refraction_smoke>"
        "--working-directory" "${_nwb_transparent_multi_runtime_root}" "--output-directory" "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/refraction_query_diagnostics" ${_nwb_refraction_logserver_args})
set_tests_properties(nwb_refraction_query_diagnostics_smoke PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT ${_nwb_refraction_lifecycle_smoke_timeout_seconds})
add_test(NAME nwb_refraction_lifecycle_gpudbg_capture_smoke COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/refraction_lifecycle_smoke.py" "--executable" "$<TARGET_FILE:nwb_refraction_smoke>"
        "--working-directory" "${_nwb_transparent_multi_runtime_root}" "--output-directory" "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/refraction_lifecycle_gpudbg" "--application-arg=--gpudbg" ${_nwb_refraction_logserver_args} CONFIGURATIONS dbg)
set_tests_properties(nwb_refraction_lifecycle_gpudbg_capture_smoke PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT ${_nwb_refraction_lifecycle_smoke_timeout_seconds})
add_test(NAME nwb_refraction_gallery_capture_smoke COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/refraction_gallery_smoke.py" "--executable" "$<TARGET_FILE:nwb_refraction_smoke>"
        "--working-directory" "${_nwb_transparent_multi_runtime_root}" "--output-directory" "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/refraction_gallery" ${_nwb_refraction_logserver_args})
set_tests_properties(nwb_refraction_gallery_capture_smoke PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT ${_nwb_refraction_gallery_smoke_timeout_seconds})
add_test(NAME nwb_refraction_duplicate_capture_smoke COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/refraction_duplicate_smoke.py" "--executable" "$<TARGET_FILE:nwb_refraction_smoke>"
        "--working-directory" "${_nwb_transparent_multi_runtime_root}" "--output-directory" "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/refraction_duplicates" ${_nwb_refraction_logserver_args})
set_tests_properties(nwb_refraction_duplicate_capture_smoke PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT ${_nwb_refraction_medium_smoke_timeout_seconds})
add_test(NAME nwb_csg_refraction_capture_smoke COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/csg_optics_smoke.py" "--effect" "refraction" "--executable" "$<TARGET_FILE:nwb_refraction_smoke>"
        "--working-directory" "${_nwb_transparent_multi_runtime_root}" "--output-directory" "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/csg_refraction" ${_nwb_refraction_logserver_args})
set_tests_properties(nwb_csg_refraction_capture_smoke PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT ${_nwb_refraction_medium_smoke_timeout_seconds})
add_test(NAME nwb_refraction_identity_capture_smoke COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/refraction_identity_smoke.py" "--executable" "$<TARGET_FILE:nwb_refraction_smoke>"
        "--working-directory" "${_nwb_transparent_multi_runtime_root}" "--output-directory" "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/refraction_identity" ${_nwb_refraction_logserver_args})
set_tests_properties(nwb_refraction_identity_capture_smoke PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT ${_nwb_refraction_identity_smoke_timeout_seconds})
unset(_nwb_refraction_logserver_args)
