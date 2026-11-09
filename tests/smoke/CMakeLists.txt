find_package(Python3 COMPONENTS Interpreter)

if(TARGET nwb_logserver AND Python3_Interpreter_FOUND)
    set(_nwb_logger_busy_spin_smoke_timeout_seconds 60)
    add_test(NAME nwb_logger_busy_spin_smoke COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/logger_busy_spin_smoke.py" "--executable" "$<TARGET_FILE:nwb_logserver>" "--working-directory" "$<TARGET_FILE_DIR:nwb_logserver>"
            "--output-directory" "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/logger_busy_spin")
    set_tests_properties(nwb_logger_busy_spin_smoke PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT ${_nwb_logger_busy_spin_smoke_timeout_seconds})
    unset(_nwb_logger_busy_spin_smoke_timeout_seconds)
endif()

nwb_declare_static_library(nwb_presentation_smoke_types)
nwb_target_adhd_sources(nwb_presentation_smoke_types NAMESPACE "NWB::Tests::Smoke" SOURCES "${CMAKE_CURRENT_LIST_DIR}/presentation_types.adhd" "${CMAKE_CURRENT_LIST_DIR}/scene_types.adhd")
target_link_libraries(nwb_presentation_smoke_types PUBLIC nwb_common nwb_test_common_names nwb_loader_runtime_constants)

nwb_declare_gtest_executable(nwb_presentation_fps_probe_tests)
nwb_target_adhd_sources(nwb_presentation_fps_probe_tests NAMESPACE "NWB::Tests::Smoke" SOURCES "${CMAKE_CURRENT_LIST_DIR}/smoke_test_cases.adhd")
target_sources(nwb_presentation_fps_probe_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/presentation_fps_probe_tests.cpp"
)
target_link_libraries(nwb_presentation_fps_probe_tests PRIVATE nwb_common nwb_presentation_smoke_types)


nwb_declare_gtest_executable(nwb_skinning_benchmark_pose_tests)
nwb_target_adhd_sources(nwb_skinning_benchmark_pose_tests NAMESPACE "NWB::Tests::Smoke" SOURCES "${CMAKE_CURRENT_LIST_DIR}/smoke_test_cases.adhd")
target_sources(nwb_skinning_benchmark_pose_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/skinning_benchmark_pose_tests.cpp"
)
target_link_libraries(nwb_skinning_benchmark_pose_tests PRIVATE nwb_ecs_skeleton nwb_ecs nwb_common nwb_alloc nwb_presentation_smoke_types)

nwb_declare_gtest_executable(nwb_csg_shadow_mode_tests)
nwb_target_adhd_sources(nwb_csg_shadow_mode_tests NAMESPACE "NWB::Tests::Smoke" SOURCES "${CMAKE_CURRENT_LIST_DIR}/smoke_test_cases.adhd")
target_sources(nwb_csg_shadow_mode_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/csg_shadow_mode_tests.cpp"
)
target_link_libraries(nwb_csg_shadow_mode_tests PRIVATE nwb_common nwb_alloc nwb_presentation_smoke_types)

if(TARGET rgd_backend)
    add_subdirectory(rgd_decode)
endif()

if(TARGET nwb_loader AND TARGET nwb_asset_builder AND Python3_Interpreter_FOUND)
    set(_nwb_smoke_capture_early_settle_seconds "0.75")
    set(_nwb_smoke_capture_mid_settle_seconds "2.0")
    set(_nwb_smoke_capture_late_settle_seconds "3.25")
    # CTest budgets include process startup and orderly teardown around the script timeout.
    set(_nwb_window_capture_smoke_ctest_timeout_seconds 60)
    set(_nwb_csg_temporal_script_timeout_seconds 150)
    set(_nwb_csg_temporal_ctest_timeout_seconds 210)
    set(_nwb_avboit_depth_warp_probe_timeout_seconds 180)
    set(_nwb_csg_shadow_smoke_timeout_seconds 600)
    set(_nwb_csg_shadow_analytic_smoke_timeout_seconds 240)
    set(_nwb_csg_shadow_open_smoke_timeout_seconds 900)
    set(_nwb_csg_shadow_approximate_map_divisor 2)
    set(_nwb_csg_caustic_capture_smoke_timeout_seconds 1500)
    set(_nwb_reflection_capture_smoke_timeout_seconds 600)
    set(_nwb_reflection_temporal_omission_smoke_timeout_seconds 240)
    set(_nwb_csg_reflection_capture_smoke_timeout_seconds 1200)
    set(_nwb_csg_reflection_reuse_script_timeout_seconds 180)
    set(_nwb_reflection_queue_boundary_smoke_timeout_seconds 900)
    set(_nwb_reflection_suite_smoke_timeout_seconds 1800)
    set(_nwb_refraction_capture_smoke_timeout_seconds 240)
    set(_nwb_refraction_lifecycle_smoke_timeout_seconds 360)
    set(_nwb_refraction_medium_smoke_timeout_seconds 600)
    set(_nwb_refraction_gallery_smoke_timeout_seconds 2400)
    set(_nwb_refraction_identity_smoke_timeout_seconds 1200)
    set(_nwb_caustic_optical_smoke_timeout_seconds 600)
    set(_nwb_texture_smoke_capture_frame_count 360)
    set(_nwb_csg_temporal_default_frame_interval 8)
    set(_nwb_csg_temporal_consecutive_frame_interval 1)
    set(_nwb_skinned_caustic_transition_script_timeout_seconds 240)
    set(_nwb_skinned_caustic_transition_ctest_timeout_seconds 870)
    set(_nwb_stress_presentation_timing_timeout_seconds 120)
    set(_nwb_gi_coverage_probe_script_timeout_seconds 240)
    set(_nwb_gi_coverage_probe_ctest_timeout_seconds 270)
    set(_nwb_csg_gi_smoke_timeout_seconds 900)

    function(nwb_add_window_capture_smoke TEST_NAME OUTPUT_NAME EXECUTABLE_PATH WORKING_DIRECTORY WINDOW_TITLE)
        cmake_parse_arguments(PARSE_ARGV 5 _nwb_smoke "" "" "CONFIGURATIONS")

        if(TARGET nwb_logserver)
            set(_nwb_smoke_logserver_args "--logserver-executable" "$<TARGET_FILE:nwb_logserver>")
        else()
            set(_nwb_smoke_logserver_args "--no-logserver")
        endif()
        if(_nwb_smoke_CONFIGURATIONS)
            set(_nwb_smoke_configuration_args CONFIGURATIONS ${_nwb_smoke_CONFIGURATIONS})
        else()
            set(_nwb_smoke_configuration_args)
        endif()
        add_test(NAME "${TEST_NAME}" COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/window_capture_smoke.py" "--executable" "${EXECUTABLE_PATH}" "--working-directory" "${WORKING_DIRECTORY}"
                "--output" "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/${OUTPUT_NAME}" "--window-title" "${WINDOW_TITLE}" ${_nwb_smoke_logserver_args} ${_nwb_smoke_UNPARSED_ARGUMENTS} ${_nwb_smoke_configuration_args})
        set_tests_properties("${TEST_NAME}" PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT ${_nwb_window_capture_smoke_ctest_timeout_seconds})
    endfunction()

    if(TARGET testbed)
        set(_nwb_testbed_smoke_runtime_root "${CMAKE_BINARY_DIR}/Testing/testbed_runtime/$<CONFIG>")
        file(GLOB_RECURSE _nwb_testbed_smoke_asset_inputs CONFIGURE_DEPENDS LIST_DIRECTORIES false "${PROJECT_SOURCE_DIR}/impl/assets/*" "${PROJECT_SOURCE_DIR}/global/slang/*" "${PROJECT_SOURCE_DIR}/CoolStuff/Testbed/assets/*")
        set(_nwb_testbed_smoke_asset_stamp "${_nwb_testbed_smoke_runtime_root}/res/.nwb_testbed_smoke_assets_$<CONFIG>.stamp")
        add_custom_command(OUTPUT "${_nwb_testbed_smoke_asset_stamp}" COMMAND "${CMAKE_COMMAND}" -E make_directory "${_nwb_testbed_smoke_runtime_root}/res" COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/pipeline/launch.py" --skip-build
                --tool-directory "$<TARGET_FILE_DIR:nwb_asset_builder>" --repo-root "${PROJECT_SOURCE_DIR}" --asset-root "impl/assets" --asset-root "CoolStuff/Testbed/assets" --output-directory "${_nwb_testbed_smoke_runtime_root}/res"
                --configuration "$<CONFIG>" COMMAND "${CMAKE_COMMAND}" -E touch "${_nwb_testbed_smoke_asset_stamp}" DEPENDS nwb_dependency_computer nwb_asset_builder nwb_asset_gatherer
                "${PROJECT_SOURCE_DIR}/pipeline/launch.py" ${_nwb_testbed_smoke_asset_inputs} WORKING_DIRECTORY "${NWB_OUTPUT_ROOT}" COMMENT "Cooking NWB Testbed smoke assets for $<CONFIG>..." VERBATIM USES_TERMINAL)
        add_custom_target(nwb_testbed_smoke_assets DEPENDS "${_nwb_testbed_smoke_asset_stamp}")
        add_dependencies(testbed nwb_testbed_smoke_assets)
        nwb_add_window_capture_smoke(nwb_testbed_window_capture_smoke testbed_window_capture.bmp "$<TARGET_FILE:testbed>" "${_nwb_testbed_smoke_runtime_root}" "NWB Testbed"
            "$<IF:$<CONFIG:fin>,--no-gpu-validation,--gpu-validation>" "--expect-log-message" "ProjectTestbed: startup scene created" "--expect-log-message" "ProjectTestbed: shutdown")
        unset(_nwb_testbed_smoke_asset_stamp)
        unset(_nwb_testbed_smoke_asset_inputs)
        unset(_nwb_testbed_smoke_runtime_root)
    endif()

    function(nwb_configure_runtime_smoke_target TARGET_NAME RUNTIME_ROOT)
        target_sources("${TARGET_NAME}" PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/graphics_abi.cpp"
        )
        set_target_properties("${TARGET_NAME}" PROPERTIES DEBUGGER_WORKING_DIRECTORY "${RUNTIME_ROOT}" WIN32_EXECUTABLE ON VS_DEBUGGER_WORKING_DIRECTORY "${RUNTIME_ROOT}")
        nwb_target_link_libraries_whole_archive("${TARGET_NAME}" nwb_assets_mesh nwb_assets_skeleton nwb_assets_model nwb_assets_material nwb_assets_shader nwb_assets_texture nwb_assets_sampler nwb_assets_subsystem nwb_assets_scene nwb_assets_graphics)
        target_link_libraries("${TARGET_NAME}" PRIVATE nwb_common nwb_alloc nwb_input nwb_loader nwb_ecs_scene nwb_ecs_mesh nwb_ecs_render nwb_ecs_model_renderer nwb_assets_graphics nwb_presentation_smoke_types ${ARGN})
        if(TARGET nwb_logserver)
            add_dependencies("${TARGET_NAME}" nwb_logserver)
        endif()
    endfunction()

    # Fixture-only material families stay out of the shared smoke catalog.
    # Each fixture root supplies its own project.nwb and scenes/main.nwb alongside the shared smoke asset library.
    function(nwb_add_fixture_smoke_assets TARGET_NAME FIXTURE_NAME RUNTIME_ROOT DESCRIPTION)
        set(_nwb_fixture_asset_root_args --asset-root "impl/assets" --asset-root "tests/smoke/assets")
        set(_nwb_fixture_input_patterns "${PROJECT_SOURCE_DIR}/impl/assets/*" "${PROJECT_SOURCE_DIR}/global/slang/*" "${CMAKE_CURRENT_LIST_DIR}/assets/*")
        foreach(_nwb_fixture_root IN LISTS ARGN)
            list(APPEND _nwb_fixture_asset_root_args --asset-root "tests/smoke/${_nwb_fixture_root}")
            list(APPEND _nwb_fixture_input_patterns "${CMAKE_CURRENT_LIST_DIR}/${_nwb_fixture_root}/*")
        endforeach()
        file(GLOB_RECURSE _nwb_fixture_asset_inputs CONFIGURE_DEPENDS LIST_DIRECTORIES false ${_nwb_fixture_input_patterns})
        set(_nwb_fixture_asset_stamp "${RUNTIME_ROOT}/res/.nwb_${FIXTURE_NAME}_assets_$<CONFIG>.stamp")
        add_custom_command(OUTPUT "${_nwb_fixture_asset_stamp}" COMMAND "${CMAKE_COMMAND}" -E make_directory "${RUNTIME_ROOT}/res"
            COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/pipeline/launch.py" --skip-build --tool-directory "$<TARGET_FILE_DIR:nwb_asset_builder>" --repo-root "${PROJECT_SOURCE_DIR}" ${_nwb_fixture_asset_root_args}
                --output-directory "${RUNTIME_ROOT}/res" --configuration "$<CONFIG>"
            COMMAND "${CMAKE_COMMAND}" -E touch "${_nwb_fixture_asset_stamp}" DEPENDS nwb_dependency_computer nwb_asset_builder nwb_asset_gatherer "${PROJECT_SOURCE_DIR}/pipeline/launch.py"
                ${_nwb_fixture_asset_inputs} WORKING_DIRECTORY "${NWB_OUTPUT_ROOT}" COMMENT "Cooking NWB ${DESCRIPTION} smoke assets for $<CONFIG>..." VERBATIM USES_TERMINAL)
        add_custom_target("${TARGET_NAME}" DEPENDS "${_nwb_fixture_asset_stamp}")
    endfunction()

    if(TARGET nwb_loader AND TARGET nwb_asset_builder)
        set(_nwb_transparent_multi_runtime_root "${CMAKE_BINARY_DIR}/Testing/smoke_runtime/$<CONFIG>")

        nwb_declare_executable(nwb_transparent_multi_smoke)
        target_sources(nwb_transparent_multi_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/transparent_multi_project.cpp"
        )
        nwb_configure_runtime_smoke_target(nwb_transparent_multi_smoke "${_nwb_transparent_multi_runtime_root}")

        # F1 exercises frame-lagged lighting teardown and re-bootstrap on the shared scene.
        nwb_declare_executable(nwb_frame_lagged_async_lighting_smoke)
        target_sources(nwb_frame_lagged_async_lighting_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/transparent_multi_project.cpp"
        )
        target_compile_definitions(nwb_frame_lagged_async_lighting_smoke PRIVATE NWB_TRANSPARENT_MULTI_FRAME_LAGGED_ASYNC_LIGHTING_SMOKE=1)
        nwb_configure_runtime_smoke_target(nwb_frame_lagged_async_lighting_smoke "${_nwb_transparent_multi_runtime_root}")

        file(GLOB_RECURSE _nwb_transparent_multi_asset_inputs CONFIGURE_DEPENDS LIST_DIRECTORIES false "${PROJECT_SOURCE_DIR}/impl/assets/*" "${PROJECT_SOURCE_DIR}/global/slang/*" "${CMAKE_CURRENT_LIST_DIR}/assets/*" "${CMAKE_CURRENT_LIST_DIR}/shared_assets/*")

        set(_nwb_transparent_multi_asset_stamp "${_nwb_transparent_multi_runtime_root}/res/.nwb_transparent_multi_assets_$<CONFIG>.stamp")
        add_custom_command(OUTPUT "${_nwb_transparent_multi_asset_stamp}" COMMAND "${CMAKE_COMMAND}" -E make_directory "${_nwb_transparent_multi_runtime_root}/res" COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/pipeline/launch.py" --skip-build
                --tool-directory "$<TARGET_FILE_DIR:nwb_asset_builder>" --repo-root "${PROJECT_SOURCE_DIR}" --asset-root "impl/assets" --asset-root "tests/smoke/assets" --asset-root "tests/smoke/shared_assets"
                --output-directory "${_nwb_transparent_multi_runtime_root}/res" --configuration "$<CONFIG>"
                COMMAND "${CMAKE_COMMAND}" -E touch "${_nwb_transparent_multi_asset_stamp}" DEPENDS nwb_dependency_computer nwb_asset_builder nwb_asset_gatherer
                "${PROJECT_SOURCE_DIR}/pipeline/launch.py" ${_nwb_transparent_multi_asset_inputs} WORKING_DIRECTORY "${NWB_OUTPUT_ROOT}" COMMENT "Cooking NWB transparent smoke assets for $<CONFIG>..." VERBATIM USES_TERMINAL)
        add_custom_target(nwb_transparent_multi_smoke_assets DEPENDS "${_nwb_transparent_multi_asset_stamp}")
        add_dependencies(nwb_transparent_multi_smoke nwb_transparent_multi_smoke_assets)
        add_dependencies(nwb_frame_lagged_async_lighting_smoke nwb_transparent_multi_smoke_assets)

        nwb_declare_executable(nwb_avboit_depth_warp_probe_smoke)
        target_sources(nwb_avboit_depth_warp_probe_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/avboit_depth_warp_probe.cpp"
        )
        nwb_configure_runtime_smoke_target(nwb_avboit_depth_warp_probe_smoke "${_nwb_transparent_multi_runtime_root}")
        add_dependencies(nwb_avboit_depth_warp_probe_smoke nwb_transparent_multi_smoke_assets)
        if(TARGET nwb_logserver)
            add_test(NAME nwb_avboit_depth_warp_probe_smoke COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/avboit_depth_warp_probe_smoke.py" --executable "$<TARGET_FILE:nwb_avboit_depth_warp_probe_smoke>"
                    --working-directory "${_nwb_transparent_multi_runtime_root}" --logserver-executable "$<TARGET_FILE:nwb_logserver>" --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/avboit_depth_warp_probe" CONFIGURATIONS dbg opt)
            set_tests_properties(nwb_avboit_depth_warp_probe_smoke PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT ${_nwb_avboit_depth_warp_probe_timeout_seconds} LABELS "avboit;gpu_validation")
        endif()

        include("${CMAKE_CURRENT_LIST_DIR}/csg_shadow_capture.cmake")

        include("${CMAKE_CURRENT_LIST_DIR}/reflection_smoke.cmake")

        include("${CMAKE_CURRENT_LIST_DIR}/refraction_smoke.cmake")

        nwb_declare_executable(nwb_transparent_csg_smoke)
        target_sources(nwb_transparent_csg_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/transparent_multi_project.cpp"
        )
        target_compile_definitions(nwb_transparent_csg_smoke PRIVATE NWB_TRANSPARENT_MULTI_ENABLE_CSG=1)
        nwb_configure_runtime_smoke_target(nwb_transparent_csg_smoke "${_nwb_transparent_multi_runtime_root}" nwb_ecs_csg)
        add_dependencies(nwb_transparent_csg_smoke nwb_transparent_multi_smoke_assets)

        nwb_add_window_capture_smoke(nwb_transparent_multi_capture_smoke transparent_multi_capture.bmp "$<TARGET_FILE:nwb_transparent_multi_smoke>" "${_nwb_transparent_multi_runtime_root}" "NWB Transparent Multi Smoke" "--expect-transparent-multi"
            "--expect-log-message" "TransparentMultiSmokeProject: natural hardware shadow route selected on RayQuery-capable hardware" "--expect-log-message" "TransparentMultiSmokeProject: renderer baseline capture ready after 6 rendered frames"
            "--expect-log-message" "RendererSystem: dispatched hardware transparent shadow traversal" "--skip-log-message" "TransparentMultiSmokeProject: natural software-only shadow route selected because RayQuery-capable hardware is unavailable"
            "--skip-blocking-log-message" "[ERROR]" "--skip-blocking-log-message" "VUID-" "--skip-blocking-log-message" "Validation Error" "--skip-blocking-log-message" "failed to resolve shader"
            "--reject-log-message" "RendererSystem: dispatched software shadow traversal" "--reject-log-message" "RendererSystem: hardware transparent shadow resource preparation failed\; colored shadows unavailable this frame"
            "--reject-log-message" "RendererSystem: changed HW shadow material context has no graph-owned upload batch" "--reject-log-message" "[ERROR]" "--reject-log-message" "failed to resolve shader")
        set_tests_properties(nwb_transparent_multi_capture_smoke PROPERTIES ENVIRONMENT "NWB_TRANSPARENT_MULTI_SPIN_ANGLE=0.6;NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME=6")

        nwb_add_window_capture_smoke(nwb_transparent_multi_sw_capture_smoke transparent_multi_sw_capture.bmp "$<TARGET_FILE:nwb_transparent_multi_smoke>" "${_nwb_transparent_multi_runtime_root}" "NWB Transparent Multi Smoke" "--expect-transparent-multi"
            "--expect-log-message" "TransparentMultiSmokeProject: natural software-only shadow route selected because RayQuery-capable hardware is unavailable"
            "--expect-log-message" "TransparentMultiSmokeProject: renderer baseline capture ready after 6 rendered frames" "--expect-log-message" "RendererSystem: dispatched software shadow traversal"
            "--skip-log-message" "TransparentMultiSmokeProject: natural hardware shadow route selected on RayQuery-capable hardware" "--skip-blocking-log-message" "[ERROR]" "--skip-blocking-log-message" "VUID-" "--skip-blocking-log-message" "Validation Error"
            "--skip-blocking-log-message" "failed to resolve shader" "--reject-log-message" "RendererSystem: frozen software material-context cache identity is no longer accepted"
            "--reject-log-message" "RendererSystem: frozen software scene-BVH cache identity is no longer accepted" "--reject-log-message" "RendererSystem: frozen software scene traversal no longer matches preflight storage"
            "--reject-log-message" "RendererSystem: software shadow scene has no frozen preflight traversal" "--reject-log-message" "[ERROR]" "--reject-log-message" "failed to resolve shader")
        set_tests_properties(nwb_transparent_multi_sw_capture_smoke PROPERTIES ENVIRONMENT "NWB_TRANSPARENT_MULTI_SPIN_ANGLE=0.6;NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME=6")

        nwb_add_window_capture_smoke(nwb_transparent_csg_capture_early_smoke transparent_csg_capture_early.bmp "$<TARGET_FILE:nwb_transparent_csg_smoke>" "${_nwb_transparent_multi_runtime_root}" "NWB Transparent CSG Smoke" "--expect-transparent-multi"
            "--expect-transparent-csg" "--transparent-csg-pose" "early" "--settle-seconds" "${_nwb_smoke_capture_early_settle_seconds}" "--reject-log-message" "[ERROR]" "--reject-log-message" "failed to resolve shader")
        set_tests_properties(nwb_transparent_csg_capture_early_smoke PROPERTIES ENVIRONMENT "NWB_TRANSPARENT_MULTI_SPIN_ANGLE=0.0")

        nwb_add_window_capture_smoke(nwb_transparent_csg_capture_mid_smoke transparent_csg_capture_mid.bmp "$<TARGET_FILE:nwb_transparent_csg_smoke>" "${_nwb_transparent_multi_runtime_root}" "NWB Transparent CSG Smoke" "--expect-transparent-multi"
            "--expect-transparent-csg" "--transparent-csg-pose" "mid" "--settle-seconds" "${_nwb_smoke_capture_mid_settle_seconds}" "--reject-log-message" "[ERROR]" "--reject-log-message" "failed to resolve shader")
        set_tests_properties(nwb_transparent_csg_capture_mid_smoke PROPERTIES ENVIRONMENT "NWB_TRANSPARENT_MULTI_SPIN_ANGLE=1.0")

        nwb_add_window_capture_smoke(nwb_transparent_csg_capture_late_smoke transparent_csg_capture_late.bmp "$<TARGET_FILE:nwb_transparent_csg_smoke>" "${_nwb_transparent_multi_runtime_root}" "NWB Transparent CSG Smoke" "--expect-transparent-multi"
            "--expect-transparent-csg" "--transparent-csg-pose" "late" "--settle-seconds" "${_nwb_smoke_capture_late_settle_seconds}" "--reject-log-message" "[ERROR]" "--reject-log-message" "failed to resolve shader")
        set_tests_properties(nwb_transparent_csg_capture_late_smoke PROPERTIES ENVIRONMENT "NWB_TRANSPARENT_MULTI_SPIN_ANGLE=2.0")

        nwb_declare_executable(nwb_caustic_sphere_smoke)
        target_sources(nwb_caustic_sphere_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/transparent_multi_project.cpp"
        )
        target_compile_definitions(nwb_caustic_sphere_smoke PRIVATE NWB_TRANSPARENT_MULTI_CAUSTIC_SPHERE=1 NWB_SMOKE_ENABLE_SWAP_CHAIN_READBACK=1)
        nwb_configure_runtime_smoke_target(nwb_caustic_sphere_smoke "${_nwb_transparent_multi_runtime_root}")
        add_dependencies(nwb_caustic_sphere_smoke nwb_transparent_multi_smoke_assets)

        add_test(NAME nwb_caustic_optical_capture_smoke COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/caustic_optical_smoke.py" "$<IF:$<CONFIG:fin>,--no-gpu-validation,--gpu-validation>" "--executable" "$<TARGET_FILE:nwb_caustic_sphere_smoke>"
                "--working-directory" "${_nwb_transparent_multi_runtime_root}" "--output-directory" "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/caustic_optical" ${_nwb_reflection_logserver_args})
        set_tests_properties(nwb_caustic_optical_capture_smoke PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT ${_nwb_caustic_optical_smoke_timeout_seconds})

        unset(_nwb_transparent_multi_asset_stamp)
        unset(_nwb_transparent_multi_runtime_root)
        unset(_nwb_transparent_multi_asset_inputs)

        include("${CMAKE_CURRENT_LIST_DIR}/texture_smoke.cmake")

        set(_nwb_csg_visible_runtime_root "${CMAKE_BINARY_DIR}/Testing/csg_visible_smoke_runtime/$<CONFIG>")

        nwb_declare_executable(nwb_csg_visible_smoke)
        target_sources(nwb_csg_visible_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/csg_visible_project.cpp"
        )
        target_compile_definitions(nwb_csg_visible_smoke PRIVATE NWB_SMOKE_ENABLE_SWAP_CHAIN_READBACK=1)
        nwb_configure_runtime_smoke_target(nwb_csg_visible_smoke "${_nwb_csg_visible_runtime_root}" nwb_ecs_csg)

        nwb_declare_gtest_executable(nwb_csg_visible_scene_tests)
        target_sources(nwb_csg_visible_scene_tests PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/csg_visible_scene_tests.cpp"
        )
        target_link_libraries(nwb_csg_visible_scene_tests PRIVATE nwb_ecs_csg nwb_ecs_scene nwb_ecs_mesh nwb_ecs_render nwb_input nwb_common nwb_alloc nwb_presentation_smoke_types)
        nwb_add_fixture_smoke_assets(nwb_csg_visible_smoke_assets csg_visible "${_nwb_csg_visible_runtime_root}" "CSG visible" csg_visible_assets)
        add_dependencies(nwb_csg_visible_smoke nwb_csg_visible_smoke_assets)

        function(nwb_add_csg_temporal_smoke TEST_SUFFIX VIEW MOTION ROUTE FRAME_INTERVAL)
            set(_nwb_csg_temporal_check "--check-quality")
            if(VIEW MATCHES "^(normal|position)$")
                set(_nwb_csg_temporal_check "--check-geometry")
            elseif(VIEW STREQUAL "cap_state")
                set(_nwb_csg_temporal_check "--check-cap-state")
            elseif(VIEW STREQUAL "span_state")
                set(_nwb_csg_temporal_check "--check-spans")
            endif()
            set(_nwb_csg_temporal_logserver_args)
            if(TARGET nwb_logserver)
                set(_nwb_csg_temporal_logserver_args "--logserver-executable" "$<TARGET_FILE:nwb_logserver>")
            endif()
            set(_nwb_csg_temporal_test "nwb_csg_gi_${TEST_SUFFIX}_smoke")
            add_test(NAME "${_nwb_csg_temporal_test}" COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/csg_gi_temporal_smoke.py" "--executable" "$<TARGET_FILE:nwb_csg_visible_smoke>" "--working-directory" "${_nwb_csg_visible_runtime_root}"
                    "--output-directory" "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/csg_gi_${TEST_SUFFIX}" "--view" "${VIEW}" "--motion" "${MOTION}" "--route" "${ROUTE}"
                    "--frame-interval" "${FRAME_INTERVAL}" "${_nwb_csg_temporal_check}" "--overwrite" "$<IF:$<CONFIG:fin>,--no-gpu-validation,--gpu-validation>" "--timeout" "${_nwb_csg_temporal_script_timeout_seconds}"
                        ${_nwb_csg_temporal_logserver_args} ${ARGN})
            set_tests_properties("${_nwb_csg_temporal_test}" PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT ${_nwb_csg_temporal_ctest_timeout_seconds})
        endfunction()

        nwb_add_csg_temporal_smoke(temporal_full full static natural ${_nwb_csg_temporal_default_frame_interval})
        nwb_add_csg_temporal_smoke(consecutive_full full static natural ${_nwb_csg_temporal_consecutive_frame_interval})
        nwb_add_csg_temporal_smoke(consecutive_direct direct static natural ${_nwb_csg_temporal_consecutive_frame_interval})
        nwb_add_csg_temporal_smoke(consecutive_indirect indirect static natural ${_nwb_csg_temporal_consecutive_frame_interval})
        nwb_add_csg_temporal_smoke(consecutive_software full static software ${_nwb_csg_temporal_consecutive_frame_interval})
        nwb_add_csg_temporal_smoke(animated full animated natural ${_nwb_csg_temporal_default_frame_interval})
        nwb_add_csg_temporal_smoke(animated_software full animated software ${_nwb_csg_temporal_default_frame_interval})
        nwb_add_csg_temporal_smoke(temporal_lagged full static natural ${_nwb_csg_temporal_default_frame_interval} "--lagged-lighting")
        nwb_add_csg_temporal_smoke(geometry_normal normal static natural ${_nwb_csg_temporal_default_frame_interval})
        nwb_add_csg_temporal_smoke(geometry_position position static natural ${_nwb_csg_temporal_default_frame_interval})
        nwb_add_csg_temporal_smoke(cap_state cap_state static natural ${_nwb_csg_temporal_default_frame_interval})
        nwb_add_csg_temporal_smoke(cap_state_software cap_state static software ${_nwb_csg_temporal_default_frame_interval})
        nwb_add_csg_temporal_smoke(overlapping_spans span_state static natural ${_nwb_csg_temporal_default_frame_interval} "--overlapping")
        nwb_add_csg_temporal_smoke(overlapping_spans_software span_state static software ${_nwb_csg_temporal_default_frame_interval} "--overlapping")

        unset(_nwb_csg_visible_runtime_root)

        set(_nwb_csg_skinned_visible_runtime_root "${CMAKE_BINARY_DIR}/Testing/csg_skinned_visible_smoke_runtime/$<CONFIG>")

        nwb_declare_executable(nwb_csg_skinned_visible_smoke)
        target_sources(nwb_csg_skinned_visible_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/csg_skinned_visible_project.cpp"
        )
        nwb_configure_runtime_smoke_target(nwb_csg_skinned_visible_smoke "${_nwb_csg_skinned_visible_runtime_root}" nwb_ecs_csg nwb_ecs_model nwb_ecs_mesh_skinning)

        file(GLOB_RECURSE _nwb_csg_skinned_visible_asset_inputs CONFIGURE_DEPENDS LIST_DIRECTORIES false "${PROJECT_SOURCE_DIR}/impl/assets/*" "${PROJECT_SOURCE_DIR}/global/slang/*" "${CMAKE_CURRENT_LIST_DIR}/assets/*" "${CMAKE_CURRENT_LIST_DIR}/shared_assets/*")

        set(_nwb_csg_skinned_visible_asset_stamp "${_nwb_csg_skinned_visible_runtime_root}/res/.nwb_csg_skinned_visible_assets_$<CONFIG>.stamp")
        add_custom_command(OUTPUT "${_nwb_csg_skinned_visible_asset_stamp}" COMMAND "${CMAKE_COMMAND}" -E make_directory "${_nwb_csg_skinned_visible_runtime_root}/res" COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/pipeline/launch.py" --skip-build
                --tool-directory "$<TARGET_FILE_DIR:nwb_asset_builder>" --repo-root "${PROJECT_SOURCE_DIR}" --asset-root "impl/assets" --asset-root "tests/smoke/assets" --asset-root "tests/smoke/shared_assets"
                --output-directory "${_nwb_csg_skinned_visible_runtime_root}/res" --configuration "$<CONFIG>"
                COMMAND "${CMAKE_COMMAND}" -E touch "${_nwb_csg_skinned_visible_asset_stamp}" DEPENDS nwb_dependency_computer nwb_asset_builder nwb_asset_gatherer
                "${PROJECT_SOURCE_DIR}/pipeline/launch.py" ${_nwb_csg_skinned_visible_asset_inputs} WORKING_DIRECTORY "${NWB_OUTPUT_ROOT}" COMMENT "Cooking NWB skinned CSG visible smoke assets for $<CONFIG>..." VERBATIM USES_TERMINAL)
        add_custom_target(nwb_csg_skinned_visible_smoke_assets DEPENDS "${_nwb_csg_skinned_visible_asset_stamp}")
        add_dependencies(nwb_csg_skinned_visible_smoke nwb_csg_skinned_visible_smoke_assets)

        nwb_declare_executable(nwb_csg_skinned_sphere_visible_smoke)
        target_sources(nwb_csg_skinned_sphere_visible_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/csg_skinned_visible_project.cpp"
        )
        target_compile_definitions(nwb_csg_skinned_sphere_visible_smoke PRIVATE NWB_CSG_SKINNED_VISIBLE_SPHERE_CUTTER=1)
        nwb_configure_runtime_smoke_target(nwb_csg_skinned_sphere_visible_smoke "${_nwb_csg_skinned_visible_runtime_root}" nwb_ecs_csg nwb_ecs_model nwb_ecs_mesh_skinning)
        add_dependencies(nwb_csg_skinned_sphere_visible_smoke nwb_csg_skinned_visible_smoke_assets)

        nwb_declare_executable(nwb_csg_skinned_transparent_sphere_visible_smoke)
        target_sources(nwb_csg_skinned_transparent_sphere_visible_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/csg_skinned_visible_project.cpp"
        )
        target_compile_definitions(nwb_csg_skinned_transparent_sphere_visible_smoke PRIVATE NWB_CSG_SKINNED_VISIBLE_SPHERE_CUTTER=1 NWB_CSG_SKINNED_VISIBLE_TRANSPARENT_RECEIVER=1)
        nwb_configure_runtime_smoke_target(nwb_csg_skinned_transparent_sphere_visible_smoke "${_nwb_csg_skinned_visible_runtime_root}" nwb_ecs_csg nwb_ecs_model nwb_ecs_mesh_skinning)
        add_dependencies(nwb_csg_skinned_transparent_sphere_visible_smoke nwb_csg_skinned_visible_smoke_assets)

        unset(_nwb_csg_skinned_visible_asset_stamp)
        unset(_nwb_csg_skinned_visible_runtime_root)
        unset(_nwb_csg_skinned_visible_asset_inputs)
    endif()

    set(_nwb_skinning_culling_model_nwb "${PROJECT_SOURCE_DIR}/tests/smoke/assets/characters/body.nwb")

    if(TARGET nwb_loader AND TARGET nwb_asset_builder AND EXISTS "${_nwb_skinning_culling_model_nwb}")
        set(_nwb_skinning_culling_runtime_root "${CMAKE_BINARY_DIR}/Testing/skinning_culling_benchmark_runtime/$<CONFIG>")
        set(_nwb_skinning_culling_asset_root "${CMAKE_BINARY_DIR}/Testing/skinning_culling_generated/$<CONFIG>")
        if(TARGET nwb_logserver)
            set(_nwb_skinning_culling_logserver_args "--logserver-executable" "$<TARGET_FILE:nwb_logserver>")
        else()
            set(_nwb_skinning_culling_logserver_args "--no-logserver")
        endif()

        nwb_declare_executable(nwb_skinning_culling_benchmark)
        target_sources(nwb_skinning_culling_benchmark PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/skinning_culling_benchmark_project.cpp"
        )
        nwb_configure_runtime_smoke_target(nwb_skinning_culling_benchmark "${_nwb_skinning_culling_runtime_root}" nwb_ecs_model nwb_ecs_mesh_skinning)

        file(GLOB_RECURSE _nwb_skinning_culling_asset_inputs CONFIGURE_DEPENDS LIST_DIRECTORIES false "${PROJECT_SOURCE_DIR}/impl/assets/*" "${PROJECT_SOURCE_DIR}/global/slang/*" "${CMAKE_CURRENT_LIST_DIR}/assets/*")

        set(_nwb_skinning_culling_asset_stamp "${_nwb_skinning_culling_runtime_root}/res/.nwb_skinning_culling_benchmark_assets_$<CONFIG>.stamp")
        add_custom_command(OUTPUT "${_nwb_skinning_culling_asset_stamp}" COMMAND "${CMAKE_COMMAND}" -E make_directory "${_nwb_skinning_culling_runtime_root}/res" COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/prepare_caustic_fixture_assets.py"
                --source-root "${CMAKE_CURRENT_LIST_DIR}/assets" --output-root "${_nwb_skinning_culling_asset_root}" COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/pipeline/launch.py" --skip-build
                --tool-directory "$<TARGET_FILE_DIR:nwb_asset_builder>" --repo-root "${PROJECT_SOURCE_DIR}" --asset-root "impl/assets" --asset-root "tests/smoke/assets" --asset-root "${_nwb_skinning_culling_asset_root}"
                --output-directory "${_nwb_skinning_culling_runtime_root}/res" --configuration "$<CONFIG>"
                COMMAND "${CMAKE_COMMAND}" -E touch "${_nwb_skinning_culling_asset_stamp}" DEPENDS nwb_dependency_computer nwb_asset_builder nwb_asset_gatherer
                "${PROJECT_SOURCE_DIR}/pipeline/launch.py" "${CMAKE_CURRENT_LIST_DIR}/prepare_caustic_fixture_assets.py"
                "${_nwb_skinning_culling_model_nwb}" ${_nwb_skinning_culling_asset_inputs} WORKING_DIRECTORY "${NWB_OUTPUT_ROOT}" COMMENT "Cooking NWB Skinning Culling Benchmark assets for $<CONFIG>..." VERBATIM
            USES_TERMINAL)
        add_custom_target(nwb_skinning_culling_benchmark_assets DEPENDS "${_nwb_skinning_culling_asset_stamp}")
        add_dependencies(nwb_skinning_culling_benchmark nwb_skinning_culling_benchmark_assets)

        # Shadows and caustics must follow live deformed geometry rather than bind pose.
        nwb_declare_executable(nwb_skinned_caustic_smoke)
        target_sources(nwb_skinned_caustic_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/skinned_caustic_project.cpp"
        )
        target_compile_definitions(nwb_skinned_caustic_smoke PRIVATE NWB_SMOKE_ENABLE_SWAP_CHAIN_READBACK=1)
        nwb_configure_runtime_smoke_target(nwb_skinned_caustic_smoke "${_nwb_skinning_culling_runtime_root}" nwb_ecs_csg nwb_ecs_model nwb_ecs_mesh_skinning)
        add_dependencies(nwb_skinned_caustic_smoke nwb_skinning_culling_benchmark_assets)

        nwb_add_window_capture_smoke(nwb_skinned_caustic_capture_late_smoke skinned_caustic_capture_late.bmp "$<TARGET_FILE:nwb_skinned_caustic_smoke>" "${_nwb_skinning_culling_runtime_root}" "NWB Skinned Caustic Smoke"
            "--settle-seconds" "${_nwb_smoke_capture_late_settle_seconds}" "$<IF:$<CONFIG:fin>,--no-gpu-validation,--gpu-validation>"
            "--expect-log-message" "SkinnedCausticSmokeProject: skinned glass refractor over ground created" "--expect-log-message" "SkinnedCausticSmokeProject: shutdown")

        add_test(NAME nwb_skinned_caustic_csg_transition_smoke COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/skinned_caustic_csg_transition_smoke.py" --executable "$<TARGET_FILE:nwb_skinned_caustic_smoke>"
                --working-directory "${_nwb_skinning_culling_runtime_root}" --output-root "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/skinned_caustic_csg_transition" --timeout ${_nwb_skinned_caustic_transition_script_timeout_seconds}
                    ${_nwb_skinning_culling_logserver_args} CONFIGURATIONS dbg opt)
        set_tests_properties(nwb_skinned_caustic_csg_transition_smoke PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT ${_nwb_skinned_caustic_transition_ctest_timeout_seconds})

        # The crowd stresses scene updates and opaque/transparent shadows across two lights.
        nwb_declare_executable(nwb_stress_test_smoke)
        target_sources(nwb_stress_test_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/stress_test_project.cpp"
        )
        nwb_configure_runtime_smoke_target(nwb_stress_test_smoke "${_nwb_skinning_culling_runtime_root}" nwb_ecs_model nwb_ecs_mesh_skinning)
        add_dependencies(nwb_stress_test_smoke nwb_skinning_culling_benchmark_assets)

        if(TARGET nwb_logserver)
            add_test(NAME nwb_stress_presentation_timing_smoke COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/stress_timing_smoke.py" --executable "$<TARGET_FILE:nwb_stress_test_smoke>" --working-directory "${_nwb_skinning_culling_runtime_root}"
                    --logserver-executable "$<TARGET_FILE:nwb_logserver>" CONFIGURATIONS dbg opt)
            set_tests_properties(nwb_stress_presentation_timing_smoke PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} TIMEOUT ${_nwb_stress_presentation_timing_timeout_seconds} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE})
        endif()

        # The A/B harness runs this binary for both transparent and opaque-only shadow arms.
        nwb_declare_executable(nwb_hardware_shadow_boundary_healthy_benchmark)
        target_sources(nwb_hardware_shadow_boundary_healthy_benchmark PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/stress_test_project.cpp"
        )
        target_compile_definitions(nwb_hardware_shadow_boundary_healthy_benchmark PRIVATE NWB_HARDWARE_SHADOW_BOUNDARY_BENCHMARK=1)
        nwb_configure_runtime_smoke_target(nwb_hardware_shadow_boundary_healthy_benchmark "${_nwb_skinning_culling_runtime_root}" nwb_ecs_model nwb_ecs_mesh_skinning)
        add_dependencies(nwb_hardware_shadow_boundary_healthy_benchmark nwb_skinning_culling_benchmark_assets)

        # Both M4 arms share assets while the async arm requires a dedicated Compute queue.
        nwb_declare_executable(nwb_async_shadow_m4_sync_benchmark)
        target_sources(nwb_async_shadow_m4_sync_benchmark PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/stress_test_project.cpp"
        )
        target_compile_definitions(nwb_async_shadow_m4_sync_benchmark PRIVATE NWB_ASYNC_SHADOW_M4_BENCHMARK=1 NWB_SMOKE_DISABLE_ASYNC_COMPUTE=1)
        nwb_configure_runtime_smoke_target(nwb_async_shadow_m4_sync_benchmark "${_nwb_skinning_culling_runtime_root}" nwb_ecs_model nwb_ecs_mesh_skinning)
        target_link_libraries(nwb_async_shadow_m4_sync_benchmark PRIVATE nwb_telemetry)
        add_dependencies(nwb_async_shadow_m4_sync_benchmark nwb_skinning_culling_benchmark_assets)

        nwb_declare_executable(nwb_async_shadow_m4_async_benchmark)
        target_sources(nwb_async_shadow_m4_async_benchmark PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/stress_test_project.cpp"
        )
        target_compile_definitions(nwb_async_shadow_m4_async_benchmark PRIVATE NWB_ASYNC_SHADOW_M4_BENCHMARK=1)
        nwb_configure_runtime_smoke_target(nwb_async_shadow_m4_async_benchmark "${_nwb_skinning_culling_runtime_root}" nwb_ecs_model nwb_ecs_mesh_skinning)
        target_link_libraries(nwb_async_shadow_m4_async_benchmark PRIVATE nwb_telemetry)
        add_dependencies(nwb_async_shadow_m4_async_benchmark nwb_skinning_culling_benchmark_assets)

        nwb_declare_executable(nwb_flicker_test_smoke)
        target_sources(nwb_flicker_test_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/flicker_test_project.cpp"
        )
        nwb_configure_runtime_smoke_target(nwb_flicker_test_smoke "${_nwb_skinning_culling_runtime_root}" nwb_ecs_model nwb_ecs_mesh_skinning)
        add_dependencies(nwb_flicker_test_smoke nwb_skinning_culling_benchmark_assets)

        nwb_declare_executable(nwb_soft_shadow_test_smoke)
        target_sources(nwb_soft_shadow_test_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/soft_shadow_test_project.cpp"
        )
        nwb_configure_runtime_smoke_target(nwb_soft_shadow_test_smoke "${_nwb_skinning_culling_runtime_root}" nwb_ecs_model nwb_ecs_mesh_skinning)
        # Cook private timing materials for NWB_SOFT_SHADOW_TEST_TIMING=1 A/B runs.
        nwb_add_fixture_smoke_assets(nwb_soft_shadow_timing_smoke_assets soft_shadow_timing "${CMAKE_BINARY_DIR}/Testing/soft_shadow_timing_smoke_runtime/$<CONFIG>" "soft shadow timing" soft_shadow_timing_assets)
        add_dependencies(nwb_soft_shadow_test_smoke nwb_skinning_culling_benchmark_assets nwb_soft_shadow_timing_smoke_assets)

        # Default GI smoke uses the open-top box while NWB_GI_SMOKE_COMPLEX_SCENE selects the skylight maze.
        nwb_declare_executable(nwb_gi_test_smoke)
        target_compile_definitions(nwb_gi_test_smoke PRIVATE NWB_SMOKE_ENABLE_SWAP_CHAIN_READBACK=1)
        target_sources(nwb_gi_test_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/gi_test_project.cpp"
        )
        nwb_configure_runtime_smoke_target(nwb_gi_test_smoke "${_nwb_skinning_culling_runtime_root}" nwb_ecs_csg)
        # Coverage, complex, and CSG GI scenes cook private GI material families.
        set(_nwb_gi_runtime_root "${CMAKE_BINARY_DIR}/Testing/gi_smoke_runtime/$<CONFIG>")
        nwb_add_fixture_smoke_assets(nwb_gi_smoke_assets gi "${_nwb_gi_runtime_root}" "GI" gi_assets)
        add_dependencies(nwb_gi_test_smoke nwb_skinning_culling_benchmark_assets nwb_gi_smoke_assets)

        # Separate opt-in fixture for same-cell perpendicular face GI coverage.
        nwb_add_window_capture_smoke(nwb_gi_coverage_probe_capture_smoke gi_coverage_probe_capture.bmp "$<TARGET_FILE:nwb_gi_test_smoke>" "${_nwb_gi_runtime_root}" "NWB GI Test" "--application-capture" "--application-capture-frame-count" "1"
            "$<IF:$<CONFIG:fin>,--no-gpu-validation,--gpu-validation>" "--timeout" "${_nwb_gi_coverage_probe_script_timeout_seconds}" "--expect-log-message" "GiTestSmokeProject: coverage probe with perpendicular same-cell faces created"
                "--expect-log-message" "GiTestSmokeProject: complex GI live warmup start"
                "--expect-log-message" "GiTestSmokeProject: complex GI live warmup end"
                "--expect-log-message" "GiTestSmokeProject: complex GI capture source_frame="
                "--expect-log-message" "GiTestSmokeProject: shutdown"
            "--expect-surfel-coverage-probe")
        set_tests_properties(nwb_gi_coverage_probe_capture_smoke PROPERTIES ENVIRONMENT "NWB_GI_SMOKE_COVERAGE_VIEW=1;NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME=1" TIMEOUT ${_nwb_gi_coverage_probe_ctest_timeout_seconds})

        foreach(_nwb_csg_gi_route IN ITEMS natural software)
            foreach(_nwb_csg_gi_lighting IN ITEMS current lagged)
                set(_nwb_csg_gi_lighting_suffix "")
                set(_nwb_csg_gi_lighting_args --no-lagged-lighting)
                if(_nwb_csg_gi_lighting STREQUAL "lagged")
                    set(_nwb_csg_gi_lighting_suffix "_lagged")
                    set(_nwb_csg_gi_lighting_args --lagged-lighting)
                endif()
                add_test(NAME "nwb_csg_gi_${_nwb_csg_gi_route}${_nwb_csg_gi_lighting_suffix}_smoke" COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/csg_gi_smoke.py" --executable "$<TARGET_FILE:nwb_gi_test_smoke>"
                        --working-directory "${_nwb_gi_runtime_root}" --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/csg_gi_${_nwb_csg_gi_route}${_nwb_csg_gi_lighting_suffix}" --route "${_nwb_csg_gi_route}"
                        ${_nwb_csg_gi_lighting_args} ${_nwb_skinning_culling_logserver_args} "$<IF:$<CONFIG:fin>,--no-gpu-validation,--gpu-validation>")
                set_tests_properties("nwb_csg_gi_${_nwb_csg_gi_route}${_nwb_csg_gi_lighting_suffix}_smoke" PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT
                    ${_nwb_csg_gi_smoke_timeout_seconds} LABELS "csg_gi")
            endforeach()
        endforeach()
        unset(_nwb_csg_gi_route)
        unset(_nwb_csg_gi_lighting)
        unset(_nwb_csg_gi_lighting_suffix)
        unset(_nwb_csg_gi_lighting_args)
        unset(_nwb_gi_runtime_root)

        unset(_nwb_skinning_culling_asset_root)
        unset(_nwb_skinning_culling_asset_stamp)
        unset(_nwb_skinning_culling_model_nwb)
        unset(_nwb_skinning_culling_runtime_root)
        unset(_nwb_skinning_culling_asset_inputs)
        unset(_nwb_skinning_culling_logserver_args)
    else()
        message(STATUS "Skipping skinning culling benchmark; missing tests/smoke/assets/characters/body.nwb.")
    endif()
    unset(_nwb_skinning_culling_model_nwb)
    include("${CMAKE_CURRENT_LIST_DIR}/software_raytracing_smokes.cmake")
endif()

if(TARGET nwb_loader AND TARGET nwb_ecs_ui AND TARGET nwb_asset_builder AND Python3_Interpreter_FOUND)
    add_subdirectory(ui_layer)
endif()

