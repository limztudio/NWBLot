        set(_nwb_csg_shadow_runtime_root "${CMAKE_BINARY_DIR}/Testing/csg_shadow_smoke_runtime/$<CONFIG>")
        nwb_declare_executable(nwb_csg_shadow_smoke)
        nwb_target_adhd_sources(nwb_csg_shadow_smoke NAMESPACE "NWB::Tests::Smoke" SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/csg_shadow_project.adhd"
        )
        target_compile_definitions(nwb_csg_shadow_smoke PRIVATE NWB_SMOKE_ENABLE_SWAP_CHAIN_READBACK=1)
        nwb_configure_runtime_smoke_target(nwb_csg_shadow_smoke "${_nwb_csg_shadow_runtime_root}")
        nwb_add_fixture_smoke_assets(nwb_csg_shadow_smoke_assets csg_shadow "${_nwb_csg_shadow_runtime_root}" "CSG shadow" csg_shadow_assets)
        add_dependencies(nwb_csg_shadow_smoke nwb_csg_shadow_smoke_assets)
        foreach(_nwb_csg_route IN ITEMS hardware software)
            foreach(_nwb_csg_light IN ITEMS directional point)
                set(_nwb_csg_shadow_test "nwb_csg_shadow_${_nwb_csg_route}_${_nwb_csg_light}_smoke")
                add_test(NAME "${_nwb_csg_shadow_test}" COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/csg_shadow_smoke.py" "$<IF:$<CONFIG:fin>,--no-gpu-validation,--gpu-validation>" --executable "$<TARGET_FILE:nwb_csg_shadow_smoke>"
                        --working-directory "${_nwb_csg_shadow_runtime_root}" --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/csg_shadow_${_nwb_csg_route}_${_nwb_csg_light}" --route "${_nwb_csg_route}" --light "${_nwb_csg_light}")
                set_tests_properties("${_nwb_csg_shadow_test}" PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT ${_nwb_csg_shadow_smoke_timeout_seconds} LABELS
                    "csg_shadow;${_nwb_csg_route}_raytracing")
                set(_nwb_csg_shadow_test "nwb_csg_shadow_${_nwb_csg_route}_${_nwb_csg_light}_center_blocker_smoke")
                add_test(NAME "${_nwb_csg_shadow_test}" COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/csg_shadow_smoke.py" "$<IF:$<CONFIG:fin>,--no-gpu-validation,--gpu-validation>" --executable "$<TARGET_FILE:nwb_csg_shadow_smoke>"
                        --working-directory "${_nwb_csg_shadow_runtime_root}" --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/csg_shadow_${_nwb_csg_route}_${_nwb_csg_light}_center_blocker"
                        --route "${_nwb_csg_route}" --light "${_nwb_csg_light}" --light-source finite --blocker-search center1)
                set_tests_properties("${_nwb_csg_shadow_test}" PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT ${_nwb_csg_shadow_smoke_timeout_seconds} LABELS
                    "csg_shadow;${_nwb_csg_route}_raytracing")
            endforeach()
            set(_nwb_csg_shadow_test "nwb_csg_shadow_${_nwb_csg_route}_analytic_smoke")
            add_test(NAME "${_nwb_csg_shadow_test}" COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/csg_shadow_smoke.py" "$<IF:$<CONFIG:fin>,--no-gpu-validation,--gpu-validation>" --executable "$<TARGET_FILE:nwb_csg_shadow_smoke>"
                    --working-directory "${_nwb_csg_shadow_runtime_root}" --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/csg_shadow_${_nwb_csg_route}_analytic" --route "${_nwb_csg_route}" --atlas analytic)
            set_tests_properties("${_nwb_csg_shadow_test}" PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT ${_nwb_csg_shadow_analytic_smoke_timeout_seconds} LABELS
                "csg_shadow;${_nwb_csg_route}_raytracing")
            set(_nwb_csg_shadow_test "nwb_csg_shadow_${_nwb_csg_route}_open_smoke")
            add_test(NAME "${_nwb_csg_shadow_test}" COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/csg_shadow_open_smoke.py" --warning-policy "$<IF:$<CONFIG:fin>,disabled,enabled>"
                "$<IF:$<CONFIG:fin>,--no-gpu-validation,--gpu-validation>" --executable "$<TARGET_FILE:nwb_csg_shadow_smoke>"
                    --working-directory "${_nwb_csg_shadow_runtime_root}" --output-root "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/csg_shadow_${_nwb_csg_route}_open" --route "${_nwb_csg_route}")
            set_tests_properties("${_nwb_csg_shadow_test}" PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT ${_nwb_csg_shadow_open_smoke_timeout_seconds} LABELS
                "csg_shadow;${_nwb_csg_route}_raytracing")
        endforeach()
        foreach(_nwb_csg_route IN ITEMS hardware software)
            set(_nwb_csg_shadow_test "nwb_csg_shadow_${_nwb_csg_route}_approximate_smoke")
            add_test(NAME "${_nwb_csg_shadow_test}" COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/csg_shadow_smoke.py" "$<IF:$<CONFIG:fin>,--no-gpu-validation,--gpu-validation>" --executable "$<TARGET_FILE:nwb_csg_shadow_smoke>"
                    --working-directory "${_nwb_csg_shadow_runtime_root}" --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/csg_shadow_${_nwb_csg_route}_approximate" --route "${_nwb_csg_route}" --light point
                    --capture-cadence reuse_one_frame --map-resolution-divisor ${_nwb_csg_shadow_approximate_map_divisor})
            set_tests_properties("${_nwb_csg_shadow_test}" PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT ${_nwb_csg_shadow_smoke_timeout_seconds} LABELS
                "csg_shadow;${_nwb_csg_route}_raytracing")
        endforeach()
        unset(_nwb_csg_shadow_test)
        unset(_nwb_csg_light)
        unset(_nwb_csg_route)
        unset(_nwb_csg_shadow_runtime_root)

        set(_nwb_csg_caustic_runtime_root "${CMAKE_BINARY_DIR}/Testing/csg_caustic_smoke_runtime/$<CONFIG>")
        nwb_declare_executable(nwb_csg_caustic_smoke)
        nwb_target_adhd_sources(nwb_csg_caustic_smoke NAMESPACE "NWB::Tests::Smoke" SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/csg_shadow_project.adhd"
        )
        target_compile_definitions(nwb_csg_caustic_smoke PRIVATE NWB_CSG_CAUSTIC_SMOKE=1 NWB_SMOKE_ENABLE_SWAP_CHAIN_READBACK=1)
        nwb_configure_runtime_smoke_target(nwb_csg_caustic_smoke "${_nwb_csg_caustic_runtime_root}" nwb_ecs_csg)
        nwb_add_fixture_smoke_assets(nwb_csg_caustic_smoke_assets csg_caustic "${_nwb_csg_caustic_runtime_root}" "CSG caustic" csg_caustic_assets)
        add_dependencies(nwb_csg_caustic_smoke nwb_csg_caustic_smoke_assets)
        add_test(NAME nwb_csg_caustic_capture_smoke COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/csg_caustic_smoke.py" "$<IF:$<CONFIG:fin>,--no-gpu-validation,--gpu-validation>" --executable "$<TARGET_FILE:nwb_csg_caustic_smoke>"
                --working-directory "${_nwb_csg_caustic_runtime_root}" --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/csg_caustic")
        set_tests_properties(nwb_csg_caustic_capture_smoke PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT ${_nwb_csg_caustic_capture_smoke_timeout_seconds} LABELS
            "csg;caustic;hardware_raytracing;software_raytracing")
        unset(_nwb_csg_caustic_runtime_root)

