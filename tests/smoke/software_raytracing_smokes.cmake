# Exercise the actual no-RT logical-device contract even on a RayQuery-capable adapter.
# The ordinary capability-gated smokes remain useful on naturally unsupported adapters.
function(nwb_add_software_raytracing_smoke TEST_NAME TARGET_NAME RUNTIME_DIRECTORY WINDOW_TITLE)
    nwb_add_window_capture_smoke(
        "${TEST_NAME}"
        "${TEST_NAME}.bmp"
        "$<TARGET_FILE:${TARGET_NAME}>"
        "${RUNTIME_DIRECTORY}"
        "${WINDOW_TITLE}"
        "--application-arg=--disable-hardware-ray-tracing"
        "--application-arg=--gpudbg"
        "--timeout" "150"
        "--render-ready-timeout" "90"
        "--settle-seconds" "8"
        "--expect-log-message" "Loader: hardware ray tracing disabled before device creation"
        "--expect-log-message" "Vulkan: hardware ray tracing policy=disabled"
        "--expect-log-message" "RayQuery=0 RayTracingPipeline=0 RayTracingAccelStruct=0 AccelStructDescriptors=0 AccelStructLayout=0"
        "--expect-log-message" "RendererSystem: dispatched software shadow traversal"
        "--reject-log-message" "RendererSystem: dispatched hardware transparent shadow traversal"
        "--reject-log-message" "RendererSystem: dispatched hardware caustic producer"
        "--reject-log-message" "RendererSystem: created surfel HW trace compute pipeline"
        "--reject-log-message" "RendererSystem: created refraction resolve pipeline (hardware ray query)"
        ${ARGN}
    )
    set_tests_properties("${TEST_NAME}" PROPERTIES
        LABELS "software_raytracing"
        TIMEOUT 180
        ENVIRONMENT "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS=0.016666667;NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME=0"
    )
endfunction()

set(_nwb_sw_smoke_runtime "${CMAKE_BINARY_DIR}/Testing/smoke_runtime/$<CONFIG>")
set(_nwb_sw_skinned_runtime "${CMAKE_BINARY_DIR}/Testing/skinning_culling_benchmark_runtime/$<CONFIG>")

if(TARGET nwb_transparent_multi_smoke)
    nwb_add_software_raytracing_smoke(
        nwb_software_raytracing_overlap_smoke nwb_transparent_multi_smoke
        "${_nwb_sw_smoke_runtime}" "NWB Transparent Multi Smoke"
        "--expect-transparent-multi"
        "--expect-log-message" "TransparentMultiSmokeProject: shutdown"
    )
    set_property(TEST nwb_software_raytracing_overlap_smoke APPEND PROPERTY
        ENVIRONMENT "NWB_TRANSPARENT_MULTI_SPIN_ANGLE=0.6"
    )
endif()

if(TARGET nwb_transparent_csg_smoke)
    foreach(_nwb_sw_csg_pose IN ITEMS early late)
        nwb_add_software_raytracing_smoke(
            "nwb_software_raytracing_csg_${_nwb_sw_csg_pose}_smoke" nwb_transparent_csg_smoke
            "${_nwb_sw_smoke_runtime}" "NWB Transparent CSG Smoke"
            "--expect-transparent-multi" "--expect-transparent-csg"
            "--transparent-csg-pose" "${_nwb_sw_csg_pose}"
            "--expect-log-message" "TransparentMultiSmokeProject: shutdown"
        )
        if(_nwb_sw_csg_pose STREQUAL "early")
            set(_nwb_sw_csg_angle "0.0")
        else()
            set(_nwb_sw_csg_angle "2.0")
        endif()
        set_property(TEST "nwb_software_raytracing_csg_${_nwb_sw_csg_pose}_smoke" APPEND PROPERTY
            ENVIRONMENT "NWB_TRANSPARENT_MULTI_SPIN_ANGLE=${_nwb_sw_csg_angle}"
        )
    endforeach()
    unset(_nwb_sw_csg_angle)
    unset(_nwb_sw_csg_pose)
endif()

if(TARGET nwb_caustic_sphere_smoke)
    nwb_add_software_raytracing_smoke(
        nwb_software_raytracing_caustic_smoke nwb_caustic_sphere_smoke
        "${_nwb_sw_smoke_runtime}" "NWB Caustic Sphere Smoke"
        "--application-capture" "--application-capture-frame-count" "360"
        "--expect-log-message" "RendererSystem: dispatched software caustic producer"
        "--expect-log-message" "RendererSystem: created refraction resolve pipeline (screen space)"
        "--expect-log-message" "TransparentMultiSmokeProject: shutdown"
    )
    set_property(TEST nwb_software_raytracing_caustic_smoke APPEND PROPERTY
        ENVIRONMENT "NWB_REFRACTION_SMOKE_ENABLED=1;NWB_REFRACTION_SMOKE_HARDWARE=1;NWB_REFLECTION_SMOKE_MODE=hybrid;NWB_CAUSTIC_SMOKE_ENABLED=1"
    )
    add_test(
        NAME nwb_software_raytracing_optical_smoke
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/caustic_optical_smoke.py"
            --software-ray-tracing
            --executable "$<TARGET_FILE:nwb_caustic_sphere_smoke>"
            --working-directory "${_nwb_sw_smoke_runtime}"
            --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/software_optical"
            --timeout 150
    )
    set_tests_properties(nwb_software_raytracing_optical_smoke PROPERTIES
        LABELS "software_raytracing"
        RESOURCE_LOCK nwb_display
        SKIP_RETURN_CODE 77
        TIMEOUT 1020
    )
endif()

if(TARGET nwb_skinned_caustic_smoke)
    nwb_add_software_raytracing_smoke(
        nwb_software_raytracing_skinned_smoke nwb_skinned_caustic_smoke
        "${_nwb_sw_skinned_runtime}" "NWB Skinned Caustic Smoke"
        "--expect-log-message" "RendererSystem: dispatched software caustic producer"
        "--expect-log-message" "SkinnedCausticSmokeProject: skinned glass refractor over ground created"
        "--expect-log-message" "SkinnedCausticSmokeProject: shutdown"
    )
endif()

if(TARGET nwb_stress_test_smoke)
    nwb_add_software_raytracing_smoke(
        nwb_software_raytracing_stress_resize_smoke nwb_stress_test_smoke
        "${_nwb_sw_skinned_runtime}" "NWB Stress Test Smoke"
        "--resize-client" "1001" "701" "--resize-settle-seconds" "8"
        "--expect-log-message" "StressTestSmokeProject: spawned 20 spinning characters (10 transparent + 10 opaque)"
        "--expect-log-message" "RendererSystem: dispatched software caustic producer"
        "--expect-log-message" "RendererSystem: created surfel trace compute pipeline"
        "--expect-log-message" "RendererSystem: created refraction resolve pipeline (screen space)"
        "--expect-log-message" "StressTestSmokeProject: shutdown"
    )
    set_property(TEST nwb_software_raytracing_stress_resize_smoke APPEND PROPERTY
        ENVIRONMENT "NWB_STRESS_CHARACTERS_PER_CLASS=10"
    )
endif()

if(TARGET nwb_gi_test_smoke)
    nwb_add_software_raytracing_smoke(
        nwb_software_raytracing_gi_smoke nwb_gi_test_smoke
        "${_nwb_sw_skinned_runtime}" "NWB GI Test"
        "--expect-log-message" "RendererSystem: created surfel trace compute pipeline"
        "--expect-log-message" "GiTestSmokeProject: shutdown"
    )
endif()

unset(_nwb_sw_smoke_runtime)
unset(_nwb_sw_skinned_runtime)
