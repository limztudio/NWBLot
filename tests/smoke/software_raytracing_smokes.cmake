set(_nwb_sw_smoke_script_timeout_seconds 150)
set(_nwb_sw_smoke_render_ready_timeout_seconds 90)
set(_nwb_sw_smoke_settle_seconds 8)
set(_nwb_sw_smoke_ctest_timeout_seconds 180)
set(_nwb_sw_optical_ctest_timeout_seconds 1020)
set(_nwb_sw_stress_resize_width 1001)
set(_nwb_sw_stress_resize_height 701)

# Exercise the selected disabled hardware ray tracing policy even on a capable adapter.
function(nwb_add_software_raytracing_smoke TEST_NAME TARGET_NAME RUNTIME_DIRECTORY WINDOW_TITLE)
    nwb_add_window_capture_smoke("${TEST_NAME}" "${TEST_NAME}.bmp" "$<TARGET_FILE:${TARGET_NAME}>" "${RUNTIME_DIRECTORY}" "${WINDOW_TITLE}" "--application-arg=--disable-hardware-ray-tracing" "$<IF:$<CONFIG:fin>,--no-gpu-validation,--gpu-validation>"
        "--timeout" "${_nwb_sw_smoke_script_timeout_seconds}" "--render-ready-timeout" "${_nwb_sw_smoke_render_ready_timeout_seconds}" "--settle-seconds" "${_nwb_sw_smoke_settle_seconds}" "--expect-log-message" "Loader: hardware ray tracing disabled before device creation" "--expect-log-message" "RendererSystem: dispatched software shadow traversal"
        "--reject-log-message" "RendererSystem: dispatched hardware transparent shadow traversal" "--reject-log-message" "RendererSystem: dispatched hardware caustic producer" "--reject-log-message" "RendererSystem: created surfel HW trace compute pipeline"
        "--reject-log-message" "RendererSystem: created refraction resolve pipeline (hardware ray query, sparse queue)"
        "--reject-log-message" "RendererSystem: created refraction resolve pipeline (hardware ray query, dense grid)" "--reject-log-message" "AVBOIT refraction resolve: hardware" ${ARGN})
    set_tests_properties("${TEST_NAME}" PROPERTIES LABELS "software_raytracing" TIMEOUT ${_nwb_sw_smoke_ctest_timeout_seconds} ENVIRONMENT "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS=0.016666667;NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME=0")
endfunction()

set(_nwb_sw_smoke_runtime "${CMAKE_BINARY_DIR}/Testing/smoke_runtime/$<CONFIG>")
set(_nwb_sw_skinned_runtime "${CMAKE_BINARY_DIR}/Testing/skinning_culling_benchmark_runtime/$<CONFIG>")

if(TARGET nwb_transparent_multi_smoke)
    nwb_add_software_raytracing_smoke(nwb_software_raytracing_overlap_smoke nwb_transparent_multi_smoke "${_nwb_sw_smoke_runtime}" "NWB Transparent Multi Smoke" "--expect-transparent-multi" "--expect-log-message" "TransparentMultiSmokeProject: shutdown")
    set_property(TEST nwb_software_raytracing_overlap_smoke APPEND PROPERTY ENVIRONMENT "NWB_TRANSPARENT_MULTI_SPIN_ANGLE=0.6")
endif()

if(TARGET nwb_transparent_csg_smoke)
    foreach(_nwb_sw_csg_pose IN ITEMS early late)
        nwb_add_software_raytracing_smoke("nwb_software_raytracing_csg_${_nwb_sw_csg_pose}_smoke" nwb_transparent_csg_smoke "${_nwb_sw_smoke_runtime}" "NWB Transparent CSG Smoke" "--expect-transparent-multi" "--expect-transparent-csg"
            "--transparent-csg-pose" "${_nwb_sw_csg_pose}" "--expect-log-message" "TransparentMultiSmokeProject: shutdown")
        if(_nwb_sw_csg_pose STREQUAL "early")
            set(_nwb_sw_csg_angle "0.0")
        else()
            set(_nwb_sw_csg_angle "2.0")
        endif()
        set_property(TEST "nwb_software_raytracing_csg_${_nwb_sw_csg_pose}_smoke" APPEND PROPERTY ENVIRONMENT "NWB_TRANSPARENT_MULTI_SPIN_ANGLE=${_nwb_sw_csg_angle}")
    endforeach()
    unset(_nwb_sw_csg_angle)
    unset(_nwb_sw_csg_pose)
endif()

if(TARGET nwb_caustic_sphere_smoke)
    add_test(NAME nwb_software_raytracing_optical_smoke COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/caustic_optical_smoke.py" --software-ray-tracing "$<IF:$<CONFIG:fin>,--no-gpu-validation,--gpu-validation>"
            --executable "$<TARGET_FILE:nwb_caustic_sphere_smoke>" --working-directory "${_nwb_sw_smoke_runtime}" --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/software_optical" --timeout ${_nwb_sw_smoke_script_timeout_seconds})
    set_tests_properties(nwb_software_raytracing_optical_smoke PROPERTIES LABELS "software_raytracing" RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT ${_nwb_sw_optical_ctest_timeout_seconds})
endif()

if(TARGET nwb_stress_test_smoke)
    nwb_add_software_raytracing_smoke(nwb_software_raytracing_stress_resize_smoke nwb_stress_test_smoke "${_nwb_sw_skinned_runtime}" "NWB Stress Test Smoke" "--resize-client" "${_nwb_sw_stress_resize_width}" "${_nwb_sw_stress_resize_height}" "--resize-settle-seconds" "${_nwb_sw_smoke_settle_seconds}"
        "--expect-log-message" "StressTestSmokeProject: spawned 20 spinning characters (10 transparent + 10 opaque)" "--expect-log-message" "RendererSystem: dispatched software caustic producer"
        "--expect-log-message" "RendererSystem: created surfel trace compute pipeline" "--expect-log-message" "RendererSystem: created refraction resolve pipeline (screen space, sparse queue)"
        "--expect-log-message" "RendererSystem: created refraction resolve pipeline (screen space, dense grid)" "--expect-log-message" "AVBOIT refraction resolve: screen-space" "--expect-log-message" "StressTestSmokeProject: shutdown")
    set_property(TEST nwb_software_raytracing_stress_resize_smoke APPEND PROPERTY ENVIRONMENT "NWB_STRESS_CHARACTERS_PER_CLASS=10")
endif()

unset(_nwb_sw_smoke_script_timeout_seconds)
unset(_nwb_sw_smoke_render_ready_timeout_seconds)
unset(_nwb_sw_smoke_settle_seconds)
unset(_nwb_sw_smoke_ctest_timeout_seconds)
unset(_nwb_sw_optical_ctest_timeout_seconds)
unset(_nwb_sw_stress_resize_width)
unset(_nwb_sw_stress_resize_height)
unset(_nwb_sw_smoke_runtime)
unset(_nwb_sw_skinned_runtime)
