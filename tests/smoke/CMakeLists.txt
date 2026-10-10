find_package(Python3 COMPONENTS Interpreter)

if(TARGET nwb_logserver AND Python3_Interpreter_FOUND)
    add_test(
        NAME nwb_logger_busy_spin_smoke
        COMMAND
            "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_LIST_DIR}/logger_busy_spin_smoke.py"
            "--executable" "$<TARGET_FILE:nwb_logserver>"
            "--working-directory" "$<TARGET_FILE_DIR:nwb_logserver>"
            "--output-directory" "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/logger_busy_spin"
    )
    set_tests_properties(nwb_logger_busy_spin_smoke PROPERTIES
        RESOURCE_LOCK nwb_display
        SKIP_RETURN_CODE 77
        TIMEOUT 60
    )
endif()

nwb_declare_gtest_executable(nwb_presentation_fps_probe_tests)
target_sources(nwb_presentation_fps_probe_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/presentation_fps_probe.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/presentation_fps_probe.h"
    "${CMAKE_CURRENT_LIST_DIR}/presentation_fps_probe_tests.cpp"
)
target_link_libraries(nwb_presentation_fps_probe_tests PRIVATE nwb_common)

if(TARGET rgd_backend)
    add_subdirectory(rgd_decode)
endif()

if((TARGET testbed OR (TARGET nwb_loader AND TARGET nwb_asset_builder)) AND Python3_Interpreter_FOUND)
    # Keep cache roots short enough for Windows MAX_PATH, including content hashes.
    set(_nwb_smoke_asset_cache_directory "${PROJECT_SOURCE_DIR}/__build_obj/c/${NWB_OUTPUT_PLATFORM}/${NWB_OUTPUT_ARCH}/${NWB_OUTPUT_DOMAIN}")

    set(_nwb_smoke_capture_early_settle_seconds "0.75")
    set(_nwb_smoke_capture_mid_settle_seconds "2.0")
    set(_nwb_smoke_capture_late_settle_seconds "3.25")
    set(_nwb_skinned_caustic_capture_late_settle_seconds "4.0")
    # CTest budgets include process startup and orderly teardown around the script timeout.
    set(_nwb_window_capture_smoke_ctest_timeout_seconds 60)
    set(_nwb_csg_temporal_script_timeout_seconds 150)
    set(_nwb_csg_temporal_ctest_timeout_seconds 210)
    set(_nwb_skinning_culling_benchmark_script_timeout_seconds 240)
    set(_nwb_skinning_culling_benchmark_ctest_timeout_seconds 300)

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
        add_test(
            NAME "${TEST_NAME}"
            COMMAND
                "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_LIST_DIR}/window_capture_smoke.py"
                "--executable" "${EXECUTABLE_PATH}"
                "--working-directory" "${WORKING_DIRECTORY}"
                "--output" "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/${OUTPUT_NAME}"
                "--window-title" "${WINDOW_TITLE}"
                ${_nwb_smoke_logserver_args}
                ${_nwb_smoke_UNPARSED_ARGUMENTS}
            ${_nwb_smoke_configuration_args}
        )
        set_tests_properties("${TEST_NAME}" PROPERTIES
            RESOURCE_LOCK nwb_display
            SKIP_RETURN_CODE 77
            TIMEOUT ${_nwb_window_capture_smoke_ctest_timeout_seconds}
        )
    endfunction()

    function(nwb_configure_runtime_smoke_target TARGET_NAME RUNTIME_ROOT)
        target_sources("${TARGET_NAME}" PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/graphics_abi.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/render_quality_settings.h"
            "${CMAKE_CURRENT_LIST_DIR}/caustic_quality_settings.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/caustic_quality_settings.h"
            "${CMAKE_CURRENT_LIST_DIR}/surfel_gi_quality_settings.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/surfel_gi_quality_settings.h"
            "${CMAKE_CURRENT_LIST_DIR}/shadow_quality_settings.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/shadow_quality_settings.h"
            "${CMAKE_CURRENT_LIST_DIR}/reflection_quality_settings.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/reflection_quality_settings.h"
        )
        set_target_properties("${TARGET_NAME}" PROPERTIES
            DEBUGGER_WORKING_DIRECTORY "${RUNTIME_ROOT}"
            WIN32_EXECUTABLE ON
            VS_DEBUGGER_WORKING_DIRECTORY "${RUNTIME_ROOT}"
        )
        nwb_target_link_libraries_whole_archive("${TARGET_NAME}"
            nwb_assets_mesh
            nwb_assets_skeleton
            nwb_assets_model
            nwb_assets_material
            nwb_assets_shader
            nwb_assets_texture
            nwb_assets_sampler
        )
        target_link_libraries("${TARGET_NAME}" PRIVATE
            nwb_common
            nwb_alloc
            nwb_loader
            nwb_ecs_scene
            nwb_ecs_mesh
            nwb_ecs_render
            nwb_ecs_model_renderer
            nwb_assets_graphics
            ${ARGN}
        )
        if(TARGET nwb_logserver)
            add_dependencies("${TARGET_NAME}" nwb_logserver)
        endif()
    endfunction()

    if(TARGET testbed)
        nwb_add_window_capture_smoke(
            nwb_testbed_window_capture_smoke
            testbed_window_capture.bmp
            "$<TARGET_FILE:testbed>"
            "${NWB_OUTPUT_ROOT}"
            "NWB Testbed"
            "--expect-log-message" "ProjectTestbed: startup scene created"
            "--expect-log-message" "Loader: project startup complete"
            "--expect-log-message" "ProjectTestbed: shutdown"
            "--reject-log-message" "software BVH build failed"
            "--reject-log-message" "software BVH update failed"
            "--reject-log-message" "Gpu task graph: rejecting task 'Shadow Preparation'"
        )
        nwb_add_window_capture_smoke(
            nwb_testbed_gpudbg_window_capture_smoke
            testbed_gpudbg_window_capture.bmp
            "$<TARGET_FILE:testbed>"
            "${NWB_OUTPUT_ROOT}"
            "NWB Testbed"
            "--application-arg=--gpudbg"
            "--expect-log-message" "ProjectTestbed: startup scene created"
            "--expect-log-message" "Loader: project startup complete"
            "--expect-log-message" "Loader: GPU debug validation enabled"
            "--expect-log-message" "ProjectTestbed: shutdown"
            "--reject-log-message" "software BVH build failed"
            "--reject-log-message" "software BVH update failed"
            "--reject-log-message" "Gpu task graph: rejecting task 'Shadow Preparation'"
            CONFIGURATIONS dbg
        )
    endif()

    if(TARGET nwb_loader AND TARGET nwb_asset_builder)
        set(_nwb_transparent_multi_runtime_root "${CMAKE_BINARY_DIR}/Testing/smoke_runtime/$<CONFIG>")

        nwb_declare_executable(nwb_transparent_multi_smoke)
        target_sources(nwb_transparent_multi_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/transparent_multi_project.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/avboit_timing_render_pass.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/avboit_timing_render_pass.h"
        )
        nwb_configure_runtime_smoke_target(nwb_transparent_multi_smoke "${_nwb_transparent_multi_runtime_root}")

        # F1 exercises frame-lagged lighting teardown and re-bootstrap on the shared scene.
        nwb_declare_executable(nwb_frame_lagged_async_lighting_smoke)
        target_sources(nwb_frame_lagged_async_lighting_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/transparent_multi_project.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/avboit_timing_render_pass.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/avboit_timing_render_pass.h"
        )
        target_compile_definitions(nwb_frame_lagged_async_lighting_smoke PRIVATE
            NWB_TRANSPARENT_MULTI_FRAME_LAGGED_ASYNC_LIGHTING_SMOKE=1
        )
        nwb_configure_runtime_smoke_target(
            nwb_frame_lagged_async_lighting_smoke
            "${_nwb_transparent_multi_runtime_root}"
        )

        file(GLOB_RECURSE _nwb_transparent_multi_asset_inputs
            CONFIGURE_DEPENDS
            LIST_DIRECTORIES false
            "${PROJECT_SOURCE_DIR}/impl/assets/*"
            "${CMAKE_CURRENT_LIST_DIR}/assets/*"
        )

        set(_nwb_transparent_multi_asset_stamp "${_nwb_transparent_multi_runtime_root}/res/.nwb_transparent_multi_assets_$<CONFIG>.stamp")
        add_custom_command(
            OUTPUT "${_nwb_transparent_multi_asset_stamp}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${_nwb_transparent_multi_runtime_root}/res"
            COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/pipeline/launch.py" --skip-build
                --tool-directory "$<TARGET_FILE_DIR:nwb_asset_builder>"
                --repo-root "${PROJECT_SOURCE_DIR}"
                --asset-root "impl/assets"
                --asset-root "tests/smoke/assets"
                --output-directory "${_nwb_transparent_multi_runtime_root}/res"
                --cache-directory "${_nwb_smoke_asset_cache_directory}"
                --configuration "$<CONFIG>"
            COMMAND "${CMAKE_COMMAND}" -E touch "${_nwb_transparent_multi_asset_stamp}"
            DEPENDS
                nwb_dependency_computer nwb_asset_builder nwb_asset_gatherer
                "${PROJECT_SOURCE_DIR}/pipeline/launch.py"
                ${_nwb_transparent_multi_asset_inputs}
            WORKING_DIRECTORY "${NWB_OUTPUT_ROOT}"
            COMMENT "Cooking NWB transparent smoke assets for $<CONFIG>..."
            VERBATIM
            USES_TERMINAL
        )
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
            add_test(NAME nwb_avboit_depth_warp_probe_smoke
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/avboit_depth_warp_probe_smoke.py"
                    --executable "$<TARGET_FILE:nwb_avboit_depth_warp_probe_smoke>"
                    --working-directory "${_nwb_transparent_multi_runtime_root}"
                    --logserver-executable "$<TARGET_FILE:nwb_logserver>"
                    --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/avboit_depth_warp_probe"
                CONFIGURATIONS dbg opt
            )
            set_tests_properties(nwb_avboit_depth_warp_probe_smoke PROPERTIES
                RESOURCE_LOCK nwb_display TIMEOUT 180 LABELS "avboit;gpu_validation"
            )
        endif()

        nwb_declare_executable(nwb_csg_shadow_smoke)
        target_sources(nwb_csg_shadow_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/csg_shadow_project.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/framebuffer_capture.cpp"
        )
        target_compile_definitions(nwb_csg_shadow_smoke PRIVATE NWB_SMOKE_ENABLE_SWAP_CHAIN_READBACK=1)
        nwb_configure_runtime_smoke_target(nwb_csg_shadow_smoke "${_nwb_transparent_multi_runtime_root}")
        add_dependencies(nwb_csg_shadow_smoke nwb_transparent_multi_smoke_assets)
        foreach(_nwb_csg_route IN ITEMS hardware software)
            foreach(_nwb_csg_light IN ITEMS directional point)
                set(_nwb_csg_shadow_test "nwb_csg_shadow_${_nwb_csg_route}_${_nwb_csg_light}_smoke")
                add_test(NAME "${_nwb_csg_shadow_test}"
                    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/csg_shadow_smoke.py"
                        "$<IF:$<CONFIG:fin>,--no-gpu-validation,--gpu-validation>"
                        --executable "$<TARGET_FILE:nwb_csg_shadow_smoke>"
                        --working-directory "${_nwb_transparent_multi_runtime_root}"
                        --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/csg_shadow_${_nwb_csg_route}_${_nwb_csg_light}"
                        --route "${_nwb_csg_route}" --light "${_nwb_csg_light}"
                )
                set_tests_properties("${_nwb_csg_shadow_test}" PROPERTIES
                    RESOURCE_LOCK nwb_display SKIP_RETURN_CODE 77 TIMEOUT 600 LABELS "csg_shadow;${_nwb_csg_route}_raytracing"
                )
                set(_nwb_csg_shadow_test "nwb_csg_shadow_${_nwb_csg_route}_${_nwb_csg_light}_center_blocker_smoke")
                add_test(NAME "${_nwb_csg_shadow_test}"
                    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/csg_shadow_smoke.py"
                        "$<IF:$<CONFIG:fin>,--no-gpu-validation,--gpu-validation>"
                        --executable "$<TARGET_FILE:nwb_csg_shadow_smoke>"
                        --working-directory "${_nwb_transparent_multi_runtime_root}"
                        --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/csg_shadow_${_nwb_csg_route}_${_nwb_csg_light}_center_blocker"
                        --route "${_nwb_csg_route}" --light "${_nwb_csg_light}" --light-source finite --blocker-search center1
                )
                set_tests_properties("${_nwb_csg_shadow_test}" PROPERTIES
                    RESOURCE_LOCK nwb_display SKIP_RETURN_CODE 77 TIMEOUT 600 LABELS "csg_shadow;${_nwb_csg_route}_raytracing"
                )
            endforeach()
            set(_nwb_csg_shadow_test "nwb_csg_shadow_${_nwb_csg_route}_analytic_smoke")
            add_test(NAME "${_nwb_csg_shadow_test}"
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/csg_shadow_smoke.py"
                    "$<IF:$<CONFIG:fin>,--no-gpu-validation,--gpu-validation>"
                    --executable "$<TARGET_FILE:nwb_csg_shadow_smoke>"
                    --working-directory "${_nwb_transparent_multi_runtime_root}"
                    --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/csg_shadow_${_nwb_csg_route}_analytic"
                    --route "${_nwb_csg_route}" --atlas analytic
            )
            set_tests_properties("${_nwb_csg_shadow_test}" PROPERTIES
                RESOURCE_LOCK nwb_display SKIP_RETURN_CODE 77 TIMEOUT 240 LABELS "csg_shadow;${_nwb_csg_route}_raytracing"
            )
            set(_nwb_csg_shadow_test "nwb_csg_shadow_${_nwb_csg_route}_open_smoke")
            add_test(NAME "${_nwb_csg_shadow_test}"
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/csg_shadow_open_smoke.py"
                    "$<IF:$<CONFIG:fin>,--no-gpu-validation,--gpu-validation>"
                    --executable "$<TARGET_FILE:nwb_csg_shadow_smoke>"
                    --working-directory "${_nwb_transparent_multi_runtime_root}"
                    --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/csg_shadow_${_nwb_csg_route}_open"
                    --route "${_nwb_csg_route}"
            )
            set_tests_properties("${_nwb_csg_shadow_test}" PROPERTIES
                RESOURCE_LOCK nwb_display SKIP_RETURN_CODE 77 TIMEOUT 900 LABELS "csg_shadow;${_nwb_csg_route}_raytracing"
            )
        endforeach()
        foreach(_nwb_csg_route IN ITEMS hardware software)
            set(_nwb_csg_shadow_test "nwb_csg_shadow_${_nwb_csg_route}_approximate_smoke")
            add_test(NAME "${_nwb_csg_shadow_test}"
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/csg_shadow_smoke.py"
                    "$<IF:$<CONFIG:fin>,--no-gpu-validation,--gpu-validation>"
                    --executable "$<TARGET_FILE:nwb_csg_shadow_smoke>"
                    --working-directory "${_nwb_transparent_multi_runtime_root}"
                    --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/csg_shadow_${_nwb_csg_route}_approximate"
                    --route "${_nwb_csg_route}" --light point
                    --capture-cadence reuse_one_frame --map-resolution-divisor 2
            )
            set_tests_properties("${_nwb_csg_shadow_test}" PROPERTIES
                RESOURCE_LOCK nwb_display SKIP_RETURN_CODE 77 TIMEOUT 600 LABELS "csg_shadow;${_nwb_csg_route}_raytracing"
            )
        endforeach()
        unset(_nwb_csg_shadow_test)
        unset(_nwb_csg_light)
        unset(_nwb_csg_route)

        set(_nwb_csg_caustic_runtime_root "${CMAKE_BINARY_DIR}/Testing/csg_caustic_smoke_runtime/$<CONFIG>")
        set(_nwb_csg_caustic_generated_root "${CMAKE_BINARY_DIR}/Testing/csg_caustic_generated/$<CONFIG>")
        nwb_declare_executable(nwb_csg_caustic_smoke)
        target_sources(nwb_csg_caustic_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/csg_shadow_project.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/framebuffer_capture.cpp"
        )
        target_compile_definitions(nwb_csg_caustic_smoke PRIVATE NWB_CSG_CAUSTIC_SMOKE=1 NWB_SMOKE_ENABLE_SWAP_CHAIN_READBACK=1)
        nwb_configure_runtime_smoke_target(nwb_csg_caustic_smoke "${_nwb_csg_caustic_runtime_root}" nwb_ecs_csg)
        file(GLOB_RECURSE _nwb_csg_caustic_asset_inputs
            CONFIGURE_DEPENDS LIST_DIRECTORIES false
            "${PROJECT_SOURCE_DIR}/impl/assets/*"
            "${CMAKE_CURRENT_LIST_DIR}/assets/*"
            "${CMAKE_CURRENT_LIST_DIR}/csg_caustic_assets/*"
        )
        set(_nwb_csg_caustic_asset_stamp "${_nwb_csg_caustic_runtime_root}/res/.nwb_csg_caustic_assets_$<CONFIG>.stamp")
        add_custom_command(
            OUTPUT "${_nwb_csg_caustic_asset_stamp}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${_nwb_csg_caustic_runtime_root}/res"
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/prepare_fixture_asset_root.py"
                --output-root "${_nwb_csg_caustic_generated_root}"
                --source-root "${CMAKE_CURRENT_LIST_DIR}/assets"
                --source-root "${CMAKE_CURRENT_LIST_DIR}/csg_caustic_assets"
            COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/pipeline/launch.py" --skip-build
                --tool-directory "$<TARGET_FILE_DIR:nwb_asset_builder>"
                --repo-root "${PROJECT_SOURCE_DIR}"
                --asset-root "impl/assets"
                --asset-root "${_nwb_csg_caustic_generated_root}"
                --output-directory "${_nwb_csg_caustic_runtime_root}/res"
                --cache-directory "${_nwb_smoke_asset_cache_directory}"
                --configuration "$<CONFIG>"
            COMMAND "${CMAKE_COMMAND}" -E touch "${_nwb_csg_caustic_asset_stamp}"
            DEPENDS nwb_dependency_computer nwb_asset_builder nwb_asset_gatherer
                "${PROJECT_SOURCE_DIR}/pipeline/launch.py"
                "${CMAKE_CURRENT_LIST_DIR}/prepare_fixture_asset_root.py"
                ${_nwb_csg_caustic_asset_inputs}
            WORKING_DIRECTORY "${NWB_OUTPUT_ROOT}"
            COMMENT "Cooking NWB CSG caustic smoke assets for $<CONFIG>..."
            VERBATIM USES_TERMINAL
        )
        add_custom_target(nwb_csg_caustic_smoke_assets DEPENDS "${_nwb_csg_caustic_asset_stamp}")
        add_dependencies(nwb_csg_caustic_smoke nwb_csg_caustic_smoke_assets)
        add_test(NAME nwb_csg_caustic_capture_smoke
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/csg_caustic_smoke.py"
                "$<IF:$<CONFIG:fin>,--no-gpu-validation,--gpu-validation>"
                --executable "$<TARGET_FILE:nwb_csg_caustic_smoke>"
                --working-directory "${_nwb_csg_caustic_runtime_root}"
                --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/csg_caustic"
        )
        set_tests_properties(nwb_csg_caustic_capture_smoke PROPERTIES
            RESOURCE_LOCK nwb_display SKIP_RETURN_CODE 77 TIMEOUT 1500 LABELS "csg;caustic;hardware_raytracing;software_raytracing"
        )
        unset(_nwb_csg_caustic_runtime_root)
        unset(_nwb_csg_caustic_generated_root)
        unset(_nwb_csg_caustic_asset_inputs)
        unset(_nwb_csg_caustic_asset_stamp)

        nwb_declare_executable(nwb_reflection_smoke)
        target_sources(nwb_reflection_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/reflection_project.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/reflection_cpu_timing_probe.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/reflection_cpu_timing_probe.h"
            "${CMAKE_CURRENT_LIST_DIR}/smoke_cpu_gpu_timing_probe.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/smoke_cpu_gpu_timing_probe.h"
            "${CMAKE_CURRENT_LIST_DIR}/presentation_fps_probe.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/presentation_fps_probe.h"
            "${CMAKE_CURRENT_LIST_DIR}/reflection_csg_context_probe.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/reflection_csg_context_probe.h"
            "${CMAKE_CURRENT_LIST_DIR}/reflection_spatial_owner_probe.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/reflection_spatial_owner_probe.h"
            "${CMAKE_CURRENT_LIST_DIR}/reflection_feedback_scene.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/reflection_feedback_scene.h"
            "${CMAKE_CURRENT_LIST_DIR}/reflection_roughness_scene.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/reflection_roughness_scene.h"
            "${CMAKE_CURRENT_LIST_DIR}/reflection_optical_scene.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/reflection_optical_scene.h"
            "${CMAKE_CURRENT_LIST_DIR}/framebuffer_capture.cpp"
        )
        target_compile_definitions(nwb_reflection_smoke PRIVATE NWB_SMOKE_ENABLE_SWAP_CHAIN_READBACK=1)
        nwb_configure_runtime_smoke_target(nwb_reflection_smoke "${_nwb_transparent_multi_runtime_root}" nwb_ecs_model nwb_ecs_mesh_skinning)
        add_dependencies(nwb_reflection_smoke nwb_transparent_multi_smoke_assets)

        # Opt-in profiling fixture. No performance acquisition runs as a default CTest.
        option(NWB_BUILD_RENDERER_GATHER_BENCHMARK "Build the fixed renderer CPU gathering benchmark" OFF)
        if(NWB_BUILD_RENDERER_GATHER_BENCHMARK)
            set(_nwb_gather_runtime_root "${CMAKE_BINARY_DIR}/Testing/gather_benchmark_runtime/$<CONFIG>")
            set(_nwb_gather_generated_root "${CMAKE_BINARY_DIR}/Testing/gather_benchmark_generated/$<CONFIG>")
            # Generated material dispatch modules are mutable per cook, so the optional root needs a separate cache.
            set(_nwb_gather_cache_root "${PROJECT_SOURCE_DIR}/__build_obj/g/${NWB_OUTPUT_PLATFORM}/${NWB_OUTPUT_ARCH}/${NWB_OUTPUT_DOMAIN}")
            set(_nwb_gather_asset_stamp "${_nwb_gather_runtime_root}/res/.gather_benchmark_assets.stamp")
            add_custom_command(
                OUTPUT "${_nwb_gather_asset_stamp}"
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/generate_gather_benchmark_assets.py"
                    --output-root "${_nwb_gather_generated_root}"
                    --source-root "${CMAKE_CURRENT_LIST_DIR}/assets"
                    --mesh-template "${CMAKE_CURRENT_LIST_DIR}/assets/meshes/octahedron.nwb"
                    --surface-template "${CMAKE_CURRENT_LIST_DIR}/gather_benchmark_assets/shaders/gather_benchmark_glass.surface"
                COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/pipeline/launch.py" --skip-build
                    --tool-directory "$<TARGET_FILE_DIR:nwb_asset_builder>"
                    --repo-root "${PROJECT_SOURCE_DIR}"
                    --asset-root "impl/assets"
                    --asset-root "${_nwb_gather_generated_root}"
                    --output-directory "${_nwb_gather_runtime_root}/res"
                    --cache-directory "${_nwb_gather_cache_root}"
                    --configuration "$<CONFIG>"
                COMMAND "${CMAKE_COMMAND}" -E touch "${_nwb_gather_asset_stamp}"
                DEPENDS nwb_dependency_computer nwb_asset_builder nwb_asset_gatherer
                    "${CMAKE_CURRENT_LIST_DIR}/generate_gather_benchmark_assets.py"
                    "${CMAKE_CURRENT_LIST_DIR}/gather_benchmark_assets/shaders/gather_benchmark_glass.surface"
                    "${PROJECT_SOURCE_DIR}/pipeline/launch.py" ${_nwb_transparent_multi_asset_inputs}
                WORKING_DIRECTORY "${NWB_OUTPUT_ROOT}"
                COMMENT "Generating and cooking optional renderer gathering benchmark assets..."
                VERBATIM USES_TERMINAL
            )
            add_custom_target(nwb_renderer_gather_benchmark_assets DEPENDS "${_nwb_gather_asset_stamp}")
            nwb_declare_executable(nwb_renderer_gather_benchmark)
            target_sources(nwb_renderer_gather_benchmark PRIVATE
                "${CMAKE_CURRENT_LIST_DIR}/renderer_gather_benchmark_project.cpp"
                "${CMAKE_CURRENT_LIST_DIR}/renderer_gather_benchmark_probe.h"
                "${CMAKE_CURRENT_LIST_DIR}/renderer_gather_benchmark_probe.cpp"
                "${CMAKE_CURRENT_LIST_DIR}/renderer_compiler_statistics_probe.h"
                "${CMAKE_CURRENT_LIST_DIR}/renderer_compiler_statistics_probe.cpp"
            )
            nwb_configure_runtime_smoke_target(nwb_renderer_gather_benchmark "${_nwb_gather_runtime_root}" nwb_ecs_model nwb_ecs_mesh_skinning)
            add_dependencies(nwb_renderer_gather_benchmark nwb_renderer_gather_benchmark_assets)
        endif()

        if(TARGET nwb_logserver)
            set(_nwb_reflection_logserver_args "--logserver-executable" "$<TARGET_FILE:nwb_logserver>")
        else()
            set(_nwb_reflection_logserver_args)
        endif()
        add_test(
            NAME nwb_reflection_capture_smoke
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/reflection_smoke.py"
                "--executable" "$<TARGET_FILE:nwb_reflection_smoke>"
                "--working-directory" "${_nwb_transparent_multi_runtime_root}"
                "--output-directory" "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/reflection"
                ${_nwb_reflection_logserver_args}
        )
        set_tests_properties(nwb_reflection_capture_smoke PROPERTIES
            RESOURCE_LOCK nwb_display
            SKIP_RETURN_CODE 77
            TIMEOUT 600
        )
        if(TARGET nwb_logserver)
            add_test(
                NAME nwb_reflection_temporal_omission_smoke
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/reflection_temporal_omission_smoke.py"
                    "--executable" "$<TARGET_FILE:nwb_reflection_smoke>"
                    "--working-directory" "${_nwb_transparent_multi_runtime_root}"
                    "--logserver-executable" "$<TARGET_FILE:nwb_logserver>"
                    "--output-directory" "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/reflection_temporal_omission"
            )
            set_tests_properties(nwb_reflection_temporal_omission_smoke PROPERTIES
                RESOURCE_LOCK nwb_display
                SKIP_RETURN_CODE 77
                TIMEOUT 240
            )
        endif()

        add_test(
            NAME nwb_csg_reflection_capture_smoke
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/csg_optics_smoke.py"
                "--effect" "reflection"
                "--executable" "$<TARGET_FILE:nwb_reflection_smoke>"
                "--working-directory" "${_nwb_transparent_multi_runtime_root}"
                "--output-directory" "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/csg_reflection"
                ${_nwb_reflection_logserver_args}
        )
        set_tests_properties(nwb_csg_reflection_capture_smoke PROPERTIES
            RESOURCE_LOCK nwb_display
            SKIP_RETURN_CODE 77
            TIMEOUT 600
        )

        add_test(
            NAME nwb_csg_reflection_collection_reuse_capture_smoke
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/reflection_optical_smoke.py"
                "--csg-reuse"
                "--executable" "$<TARGET_FILE:nwb_reflection_smoke>"
                "--working-directory" "${_nwb_transparent_multi_runtime_root}"
                "--output-directory" "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/csg_reflection_collection_reuse"
                "--timeout" "180"
                ${_nwb_reflection_logserver_args}
        )
        set_tests_properties(nwb_csg_reflection_collection_reuse_capture_smoke PROPERTIES
            RESOURCE_LOCK nwb_display
            SKIP_RETURN_CODE 77
            TIMEOUT 1200
        )

        add_test(
            NAME nwb_reflection_queue_boundary_capture_smoke
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/reflection_optical_smoke.py"
                "--queue-boundaries"
                "--executable" "$<TARGET_FILE:nwb_reflection_smoke>"
                "--working-directory" "${_nwb_transparent_multi_runtime_root}"
                "--output-directory" "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/reflection_queue_boundaries"
                ${_nwb_reflection_logserver_args}
        )
        set_tests_properties(nwb_reflection_queue_boundary_capture_smoke PROPERTIES
            RESOURCE_LOCK nwb_display
            SKIP_RETURN_CODE 77
            TIMEOUT 900
        )

        foreach(_nwb_reflection_suite IN ITEMS rough temporal optical feedback feedback_diagnostics_off)
            string(REPLACE "_" "-" _nwb_reflection_suite_argument "${_nwb_reflection_suite}")
            add_test(
                NAME "nwb_reflection_${_nwb_reflection_suite}_capture_smoke"
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/reflection_smoke.py"
                    "--executable" "$<TARGET_FILE:nwb_reflection_smoke>"
                    "--working-directory" "${_nwb_transparent_multi_runtime_root}"
                    "--output-directory" "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/reflection_${_nwb_reflection_suite}"
                    "--suite" "${_nwb_reflection_suite_argument}"
                    ${_nwb_reflection_logserver_args}
            )
            set_tests_properties("nwb_reflection_${_nwb_reflection_suite}_capture_smoke" PROPERTIES
                RESOURCE_LOCK nwb_display
                SKIP_RETURN_CODE 77
                TIMEOUT 600
            )
        endforeach()

        nwb_declare_executable(nwb_refraction_smoke)
        target_sources(nwb_refraction_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/refraction_project.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/refraction_gallery_scene.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/refraction_gallery_scene.h"
            "${CMAKE_CURRENT_LIST_DIR}/framebuffer_capture.cpp"
        )
        target_compile_definitions(nwb_refraction_smoke PRIVATE NWB_SMOKE_ENABLE_SWAP_CHAIN_READBACK=1)
        nwb_configure_runtime_smoke_target(nwb_refraction_smoke "${_nwb_transparent_multi_runtime_root}")
        add_dependencies(nwb_refraction_smoke nwb_transparent_multi_smoke_assets)

        if(TARGET nwb_logserver)
            set(_nwb_refraction_logserver_args "--logserver-executable" "$<TARGET_FILE:nwb_logserver>")
        else()
            set(_nwb_refraction_logserver_args)
        endif()
        add_test(
            NAME nwb_refraction_capture_smoke
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/refraction_capture_smoke.py"
                "--executable" "$<TARGET_FILE:nwb_refraction_smoke>"
                "--working-directory" "${_nwb_transparent_multi_runtime_root}"
                "--output-directory" "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/refraction"
                ${_nwb_refraction_logserver_args}
        )
        set_tests_properties(nwb_refraction_capture_smoke PROPERTIES
            RESOURCE_LOCK nwb_display
            SKIP_RETURN_CODE 77
            TIMEOUT 240
        )
        add_test(
            NAME nwb_refraction_gpudbg_capture_smoke
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/refraction_capture_smoke.py"
                "--executable" "$<TARGET_FILE:nwb_refraction_smoke>"
                "--working-directory" "${_nwb_transparent_multi_runtime_root}"
                "--output-directory" "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/refraction_gpudbg"
                "--application-arg=--gpudbg"
                ${_nwb_refraction_logserver_args}
            CONFIGURATIONS dbg
        )
        set_tests_properties(nwb_refraction_gpudbg_capture_smoke PROPERTIES
            RESOURCE_LOCK nwb_display
            SKIP_RETURN_CODE 77
            TIMEOUT 240
        )
        add_test(
            NAME nwb_refraction_gallery_capture_smoke
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/refraction_gallery_smoke.py"
                "--executable" "$<TARGET_FILE:nwb_refraction_smoke>"
                "--working-directory" "${_nwb_transparent_multi_runtime_root}"
                "--output-directory" "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/refraction_gallery"
                ${_nwb_refraction_logserver_args}
        )
        set_tests_properties(nwb_refraction_gallery_capture_smoke PROPERTIES
            RESOURCE_LOCK nwb_display
            SKIP_RETURN_CODE 77
            TIMEOUT 600
        )
        add_test(
            NAME nwb_refraction_duplicate_capture_smoke
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/refraction_duplicate_smoke.py"
                "--executable" "$<TARGET_FILE:nwb_refraction_smoke>"
                "--working-directory" "${_nwb_transparent_multi_runtime_root}"
                "--output-directory" "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/refraction_duplicates"
                ${_nwb_refraction_logserver_args}
        )
        set_tests_properties(nwb_refraction_duplicate_capture_smoke PROPERTIES
            RESOURCE_LOCK nwb_display
            SKIP_RETURN_CODE 77
            TIMEOUT 600
        )
        add_test(
            NAME nwb_csg_refraction_capture_smoke
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/csg_optics_smoke.py"
                "--effect" "refraction"
                "--executable" "$<TARGET_FILE:nwb_refraction_smoke>"
                "--working-directory" "${_nwb_transparent_multi_runtime_root}"
                "--output-directory" "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/csg_refraction"
                ${_nwb_refraction_logserver_args}
        )
        set_tests_properties(nwb_csg_refraction_capture_smoke PROPERTIES
            RESOURCE_LOCK nwb_display
            SKIP_RETURN_CODE 77
            TIMEOUT 600
        )
        add_test(
            NAME nwb_refraction_identity_capture_smoke
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/refraction_identity_smoke.py"
                "--executable" "$<TARGET_FILE:nwb_refraction_smoke>"
                "--working-directory" "${_nwb_transparent_multi_runtime_root}"
                "--output-directory" "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/refraction_identity"
                ${_nwb_refraction_logserver_args}
        )
        set_tests_properties(nwb_refraction_identity_capture_smoke PROPERTIES
            RESOURCE_LOCK nwb_display
            SKIP_RETURN_CODE 77
            TIMEOUT 1200
        )
        unset(_nwb_refraction_logserver_args)

        nwb_declare_executable(nwb_transparent_csg_smoke)
        target_sources(nwb_transparent_csg_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/transparent_multi_project.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/avboit_timing_render_pass.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/avboit_timing_render_pass.h"
        )
        target_compile_definitions(nwb_transparent_csg_smoke PRIVATE
            NWB_TRANSPARENT_MULTI_ENABLE_CSG=1
        )
        nwb_configure_runtime_smoke_target(
            nwb_transparent_csg_smoke
            "${_nwb_transparent_multi_runtime_root}"
            nwb_ecs_csg
        )
        add_dependencies(nwb_transparent_csg_smoke nwb_transparent_multi_smoke_assets)

        nwb_add_window_capture_smoke(
            nwb_transparent_multi_capture_smoke
            transparent_multi_capture.bmp
            "$<TARGET_FILE:nwb_transparent_multi_smoke>"
            "${_nwb_transparent_multi_runtime_root}"
            "NWB Transparent Multi Smoke"
            "--expect-transparent-multi"
            "--expect-log-message" "TransparentMultiSmokeProject: natural hardware shadow route selected on RayQuery-capable hardware"
            "--expect-log-message" "TransparentMultiSmokeProject: renderer baseline capture ready after 6 rendered frames"
            "--expect-log-message" "RendererSystem: dispatched hardware transparent shadow traversal"
            "--skip-log-message" "TransparentMultiSmokeProject: natural software-only shadow route selected because RayQuery-capable hardware is unavailable"
            "--skip-blocking-log-message" "[ERROR]"
            "--skip-blocking-log-message" "VUID-"
            "--skip-blocking-log-message" "Validation Error"
            "--skip-blocking-log-message" "failed to resolve shader"
            "--reject-log-message" "RendererSystem: dispatched software shadow traversal"
            "--reject-log-message" "RendererSystem: hardware transparent shadow resource preparation failed\; colored shadows unavailable this frame"
            "--reject-log-message" "RendererSystem: changed HW shadow material context has no graph-owned upload batch"
            "--reject-log-message" "[ERROR]"
            "--reject-log-message" "failed to resolve shader"
        )
        set_tests_properties(nwb_transparent_multi_capture_smoke PROPERTIES
            ENVIRONMENT "NWB_TRANSPARENT_MULTI_SPIN_ANGLE=0.6;NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME=6"
        )

        nwb_add_window_capture_smoke(
            nwb_transparent_multi_sw_capture_smoke
            transparent_multi_sw_capture.bmp
            "$<TARGET_FILE:nwb_transparent_multi_smoke>"
            "${_nwb_transparent_multi_runtime_root}"
            "NWB Transparent Multi Smoke"
            "--expect-transparent-multi"
            "--expect-log-message" "TransparentMultiSmokeProject: natural software-only shadow route selected because RayQuery-capable hardware is unavailable"
            "--expect-log-message" "TransparentMultiSmokeProject: renderer baseline capture ready after 6 rendered frames"
            "--expect-log-message" "RendererSystem: dispatched software shadow traversal"
            "--skip-log-message" "TransparentMultiSmokeProject: natural hardware shadow route selected on RayQuery-capable hardware"
            "--skip-blocking-log-message" "[ERROR]"
            "--skip-blocking-log-message" "VUID-"
            "--skip-blocking-log-message" "Validation Error"
            "--skip-blocking-log-message" "failed to resolve shader"
            "--reject-log-message" "RendererSystem: frozen software material-context cache identity is no longer accepted"
            "--reject-log-message" "RendererSystem: frozen software scene-BVH cache identity is no longer accepted"
            "--reject-log-message" "RendererSystem: frozen software scene traversal no longer matches preflight storage"
            "--reject-log-message" "RendererSystem: software shadow scene has no frozen preflight traversal"
            "--reject-log-message" "[ERROR]"
            "--reject-log-message" "failed to resolve shader"
        )
        set_tests_properties(nwb_transparent_multi_sw_capture_smoke PROPERTIES
            ENVIRONMENT "NWB_TRANSPARENT_MULTI_SPIN_ANGLE=0.6;NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME=6"
        )

        nwb_add_window_capture_smoke(
            nwb_transparent_csg_capture_early_smoke
            transparent_csg_capture_early.bmp
            "$<TARGET_FILE:nwb_transparent_csg_smoke>"
            "${_nwb_transparent_multi_runtime_root}"
            "NWB Transparent CSG Smoke"
            "--expect-transparent-multi"
            "--expect-transparent-csg"
            "--transparent-csg-pose" "early"
            "--settle-seconds" "${_nwb_smoke_capture_early_settle_seconds}"
            "--reject-log-message" "[ERROR]"
            "--reject-log-message" "failed to resolve shader"
        )
        set_tests_properties(nwb_transparent_csg_capture_early_smoke PROPERTIES
            ENVIRONMENT "NWB_TRANSPARENT_MULTI_SPIN_ANGLE=0.0"
        )

        nwb_add_window_capture_smoke(
            nwb_transparent_csg_capture_mid_smoke
            transparent_csg_capture_mid.bmp
            "$<TARGET_FILE:nwb_transparent_csg_smoke>"
            "${_nwb_transparent_multi_runtime_root}"
            "NWB Transparent CSG Smoke"
            "--expect-transparent-multi"
            "--expect-transparent-csg"
            "--transparent-csg-pose" "mid"
            "--settle-seconds" "${_nwb_smoke_capture_mid_settle_seconds}"
            "--reject-log-message" "[ERROR]"
            "--reject-log-message" "failed to resolve shader"
        )
        set_tests_properties(nwb_transparent_csg_capture_mid_smoke PROPERTIES
            ENVIRONMENT "NWB_TRANSPARENT_MULTI_SPIN_ANGLE=1.0"
        )

        nwb_add_window_capture_smoke(
            nwb_transparent_csg_capture_late_smoke
            transparent_csg_capture_late.bmp
            "$<TARGET_FILE:nwb_transparent_csg_smoke>"
            "${_nwb_transparent_multi_runtime_root}"
            "NWB Transparent CSG Smoke"
            "--expect-transparent-multi"
            "--expect-transparent-csg"
            "--transparent-csg-pose" "late"
            "--settle-seconds" "${_nwb_smoke_capture_late_settle_seconds}"
            "--reject-log-message" "[ERROR]"
            "--reject-log-message" "failed to resolve shader"
        )
        set_tests_properties(nwb_transparent_csg_capture_late_smoke PROPERTIES
            ENVIRONMENT "NWB_TRANSPARENT_MULTI_SPIN_ANGLE=2.0"
        )

        nwb_declare_executable(nwb_caustic_sphere_smoke)
        target_sources(nwb_caustic_sphere_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/transparent_multi_project.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/avboit_timing_render_pass.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/avboit_timing_render_pass.h"
            "${CMAKE_CURRENT_LIST_DIR}/framebuffer_capture.cpp"
        )
        target_compile_definitions(nwb_caustic_sphere_smoke PRIVATE
            NWB_TRANSPARENT_MULTI_CAUSTIC_SPHERE=1
            NWB_SMOKE_ENABLE_SWAP_CHAIN_READBACK=1
        )
        nwb_configure_runtime_smoke_target(nwb_caustic_sphere_smoke "${_nwb_transparent_multi_runtime_root}")
        add_dependencies(nwb_caustic_sphere_smoke nwb_transparent_multi_smoke_assets)

        nwb_add_window_capture_smoke(
            nwb_caustic_sphere_capture_smoke
            caustic_sphere_capture.bmp
            "$<TARGET_FILE:nwb_caustic_sphere_smoke>"
            "${_nwb_transparent_multi_runtime_root}"
            "NWB Caustic Sphere Smoke"
            "--application-capture"
            "--application-capture-frame-count" "360"
            "--expect-log-message" "AVBOIT refraction resolve:"
            "--expect-log-message" "caustic producer ("
            "--expect-log-message" "TransparentMultiSmokeProject: shutdown"
            "--reject-log-message" "[ERROR]"
            "--reject-log-message" "failed to resolve shader"
        )
        set_tests_properties(nwb_caustic_sphere_capture_smoke PROPERTIES
            ENVIRONMENT "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS=0.016666667;NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME=0;NWB_REFRACTION_SMOKE_ENABLED=1;NWB_REFRACTION_SMOKE_HARDWARE=1"
        )
        add_test(
            NAME nwb_caustic_optical_capture_smoke
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/caustic_optical_smoke.py"
                "$<IF:$<CONFIG:fin>,--no-gpu-validation,--gpu-validation>"
                "--executable" "$<TARGET_FILE:nwb_caustic_sphere_smoke>"
                "--working-directory" "${_nwb_transparent_multi_runtime_root}"
                "--output-directory" "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/caustic_optical"
                ${_nwb_reflection_logserver_args}
        )
        set_tests_properties(nwb_caustic_optical_capture_smoke PROPERTIES
            RESOURCE_LOCK nwb_display
            SKIP_RETURN_CODE 77
            TIMEOUT 600
        )

        unset(_nwb_transparent_multi_asset_stamp)
        unset(_nwb_transparent_multi_runtime_root)
        unset(_nwb_transparent_multi_asset_inputs)

        if(TARGET nwb_tex_conv)
            # Generate deterministic PNG/.tex fixtures per configuration to exercise tex_conv during cooking.
            set(_nwb_texture_smoke_runtime_root "${CMAKE_BINARY_DIR}/Testing/texture_smoke_runtime/$<CONFIG>")
            set(_nwb_texture_smoke_generated_asset_root "${_nwb_texture_smoke_runtime_root}/generated_project/assets")
            set(_nwb_texture_smoke_generated_texture_directory "${_nwb_texture_smoke_generated_asset_root}/textures")
            set(_nwb_texture_smoke_source_png "${_nwb_texture_smoke_generated_texture_directory}/smoke_pattern_source.png")
            set(_nwb_texture_smoke_source_nwb "${_nwb_texture_smoke_generated_texture_directory}/smoke_pattern.nwb")
            set(_nwb_texture_smoke_source_tex "${_nwb_texture_smoke_generated_texture_directory}/smoke_pattern.tex")
            set(_nwb_texture_smoke_generator "${CMAKE_CURRENT_LIST_DIR}/generate_texture_smoke_input.py")

            nwb_declare_executable(nwb_texture_smoke)
            target_sources(nwb_texture_smoke PRIVATE
                "${CMAKE_CURRENT_LIST_DIR}/framebuffer_capture.cpp"
                "${CMAKE_CURRENT_LIST_DIR}/texture_smoke_project.cpp"
            )
            target_compile_definitions(nwb_texture_smoke PRIVATE
                NWB_SMOKE_ENABLE_SWAP_CHAIN_READBACK=1
            )
            nwb_configure_runtime_smoke_target(nwb_texture_smoke "${_nwb_texture_smoke_runtime_root}")

            file(GLOB_RECURSE _nwb_texture_smoke_asset_inputs
                CONFIGURE_DEPENDS
                LIST_DIRECTORIES false
                "${PROJECT_SOURCE_DIR}/impl/assets/*"
                "${CMAKE_CURRENT_LIST_DIR}/assets/*"
            )

            add_custom_command(
                OUTPUT
                    "${_nwb_texture_smoke_source_nwb}"
                    "${_nwb_texture_smoke_source_tex}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${_nwb_texture_smoke_generated_texture_directory}"
                COMMAND "${Python3_EXECUTABLE}" "${_nwb_texture_smoke_generator}" --output "${_nwb_texture_smoke_source_png}"
                COMMAND "$<TARGET_FILE:nwb_tex_conv>"
                    "${_nwb_texture_smoke_source_png}"
                    --output "${_nwb_texture_smoke_source_nwb}"
                    --force
                DEPENDS
                    nwb_tex_conv
                    "${_nwb_texture_smoke_generator}"
                    "${PROJECT_SOURCE_DIR}/tests/common/png_fixture.py"
                COMMENT "Generating NWB texture smoke source asset for $<CONFIG>..."
                VERBATIM
                USES_TERMINAL
            )

            set(_nwb_texture_smoke_asset_stamp "${_nwb_texture_smoke_runtime_root}/res/.nwb_texture_smoke_assets_$<CONFIG>.stamp")
            add_custom_command(
                OUTPUT "${_nwb_texture_smoke_asset_stamp}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${_nwb_texture_smoke_runtime_root}/res"
                COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/pipeline/launch.py" --skip-build
                    --tool-directory "$<TARGET_FILE_DIR:nwb_asset_builder>"
                    --repo-root "${PROJECT_SOURCE_DIR}"
                    --asset-root "impl/assets"
                    --asset-root "tests/smoke/assets"
                    --asset-root "${_nwb_texture_smoke_generated_asset_root}"
                    --output-directory "${_nwb_texture_smoke_runtime_root}/res"
                    --cache-directory "${_nwb_smoke_asset_cache_directory}"
                    --configuration "$<CONFIG>"
                COMMAND "${CMAKE_COMMAND}" -E touch "${_nwb_texture_smoke_asset_stamp}"
                DEPENDS
                    nwb_dependency_computer nwb_asset_builder nwb_asset_gatherer
                    "${PROJECT_SOURCE_DIR}/pipeline/launch.py"
                    "${_nwb_texture_smoke_source_nwb}"
                    "${_nwb_texture_smoke_source_tex}"
                    ${_nwb_texture_smoke_asset_inputs}
                WORKING_DIRECTORY "${NWB_OUTPUT_ROOT}"
                COMMENT "Cooking NWB texture smoke assets for $<CONFIG>..."
                VERBATIM
                USES_TERMINAL
            )
            add_custom_target(nwb_texture_smoke_assets DEPENDS "${_nwb_texture_smoke_asset_stamp}")
            add_dependencies(nwb_texture_smoke nwb_texture_smoke_assets)

            nwb_add_window_capture_smoke(
                nwb_texture_capture_smoke
                texture_smoke_capture.bmp
                "$<TARGET_FILE:nwb_texture_smoke>"
                "${_nwb_texture_smoke_runtime_root}"
                "NWB Texture Smoke"
                "--application-capture"
                "--application-capture-frame-count" "360"
                "--expect-texture-smoke"
                "--reject-log-message" "[ERROR]"
                "--reject-log-message" "failed to resolve shader"
            )

            unset(_nwb_texture_smoke_asset_stamp)
            unset(_nwb_texture_smoke_runtime_root)
            unset(_nwb_texture_smoke_generated_asset_root)
            unset(_nwb_texture_smoke_generated_texture_directory)
            unset(_nwb_texture_smoke_source_png)
            unset(_nwb_texture_smoke_source_nwb)
            unset(_nwb_texture_smoke_source_tex)
            unset(_nwb_texture_smoke_generator)
            unset(_nwb_texture_smoke_asset_inputs)
        else()
            message(STATUS "Skipping texture smoke; nwb_tex_conv is unavailable.")
        endif()

        set(_nwb_csg_visible_runtime_root "${CMAKE_BINARY_DIR}/Testing/csg_visible_smoke_runtime/$<CONFIG>")
        set(_nwb_csg_visible_generated_root "${CMAKE_BINARY_DIR}/Testing/csg_visible_generated/$<CONFIG>")

        nwb_declare_executable(nwb_csg_visible_smoke)
        target_sources(nwb_csg_visible_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/csg_visible_project.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/framebuffer_capture.cpp"
        )
        target_compile_definitions(nwb_csg_visible_smoke PRIVATE NWB_SMOKE_ENABLE_SWAP_CHAIN_READBACK=1)
        nwb_configure_runtime_smoke_target(
            nwb_csg_visible_smoke
            "${_nwb_csg_visible_runtime_root}"
            nwb_ecs_csg
        )

        file(GLOB_RECURSE _nwb_csg_visible_asset_inputs
            CONFIGURE_DEPENDS
            LIST_DIRECTORIES false
            "${PROJECT_SOURCE_DIR}/impl/assets/*"
            "${CMAKE_CURRENT_LIST_DIR}/assets/*"
            "${CMAKE_CURRENT_LIST_DIR}/csg_visible_assets/*"
        )

        set(_nwb_csg_visible_asset_stamp "${_nwb_csg_visible_runtime_root}/res/.nwb_csg_visible_assets_$<CONFIG>.stamp")
        add_custom_command(
            OUTPUT "${_nwb_csg_visible_asset_stamp}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${_nwb_csg_visible_runtime_root}/res"
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/prepare_fixture_asset_root.py"
                --output-root "${_nwb_csg_visible_generated_root}"
                --source-root "${CMAKE_CURRENT_LIST_DIR}/assets"
                --source-root "${CMAKE_CURRENT_LIST_DIR}/csg_visible_assets"
            COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/pipeline/launch.py" --skip-build
                --tool-directory "$<TARGET_FILE_DIR:nwb_asset_builder>"
                --repo-root "${PROJECT_SOURCE_DIR}"
                --asset-root "impl/assets"
                --asset-root "${_nwb_csg_visible_generated_root}"
                --output-directory "${_nwb_csg_visible_runtime_root}/res"
                --cache-directory "${_nwb_smoke_asset_cache_directory}"
                --configuration "$<CONFIG>"
            COMMAND "${CMAKE_COMMAND}" -E touch "${_nwb_csg_visible_asset_stamp}"
            DEPENDS
                nwb_dependency_computer nwb_asset_builder nwb_asset_gatherer
                "${PROJECT_SOURCE_DIR}/pipeline/launch.py"
                "${CMAKE_CURRENT_LIST_DIR}/prepare_fixture_asset_root.py"
                ${_nwb_csg_visible_asset_inputs}
            WORKING_DIRECTORY "${NWB_OUTPUT_ROOT}"
            COMMENT "Cooking NWB CSG visible smoke assets for $<CONFIG>..."
            VERBATIM
            USES_TERMINAL
        )
        add_custom_target(nwb_csg_visible_smoke_assets DEPENDS "${_nwb_csg_visible_asset_stamp}")
        add_dependencies(nwb_csg_visible_smoke nwb_csg_visible_smoke_assets)

        nwb_add_window_capture_smoke(
            nwb_csg_visible_capture_smoke
            csg_visible_capture.bmp
            "$<TARGET_FILE:nwb_csg_visible_smoke>"
            "${_nwb_csg_visible_runtime_root}"
            "NWB CSG Visible Smoke"
            "--expect-log-message" "CsgVisibleSmokeProject: natural native mesh-shader route selected"
            "--expect-log-message" "selected MeshShader + PS on this device"
            "--skip-log-message" "CsgVisibleSmokeProject: natural indexed route selected because Meshlets are unavailable"
            "--skip-blocking-log-message" "[ERROR]"
            "--skip-blocking-log-message" "VUID-"
            "--skip-blocking-log-message" "Validation Error"
            "--skip-blocking-log-message" "failed to resolve shader"
            "--reject-log-message" "[ERROR]"
            "--reject-log-message" "failed to resolve shader"
        )

        nwb_add_window_capture_smoke(
            nwb_csg_visible_indexed_capture_smoke
            csg_visible_indexed_capture.bmp
            "$<TARGET_FILE:nwb_csg_visible_smoke>"
            "${_nwb_csg_visible_runtime_root}"
            "NWB CSG Visible Smoke"
            "--expect-log-message" "CsgVisibleSmokeProject: natural indexed route selected because Meshlets are unavailable"
            "--expect-log-message" "selected VertexIndexed + PS from persistent object-space geometry"
            "--skip-log-message" "CsgVisibleSmokeProject: natural native mesh-shader route selected"
            "--skip-blocking-log-message" "[ERROR]"
            "--skip-blocking-log-message" "VUID-"
            "--skip-blocking-log-message" "Validation Error"
            "--skip-blocking-log-message" "failed to resolve shader"
            "--reject-log-message" "[ERROR]"
            "--reject-log-message" "failed to resolve shader"
        )

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
            add_test(NAME "${_nwb_csg_temporal_test}"
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/csg_gi_temporal_smoke.py"
                    "--executable" "$<TARGET_FILE:nwb_csg_visible_smoke>"
                    "--working-directory" "${_nwb_csg_visible_runtime_root}"
                    "--output-directory" "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/csg_gi_${TEST_SUFFIX}"
                    "--view" "${VIEW}" "--motion" "${MOTION}" "--route" "${ROUTE}"
                    "--frame-interval" "${FRAME_INTERVAL}" "${_nwb_csg_temporal_check}" "--overwrite" "--gpu-validation"
                    "--timeout" "${_nwb_csg_temporal_script_timeout_seconds}"
                    ${_nwb_csg_temporal_logserver_args} ${ARGN}
            )
            set_tests_properties("${_nwb_csg_temporal_test}" PROPERTIES
                RESOURCE_LOCK nwb_display
                SKIP_RETURN_CODE 77
                TIMEOUT ${_nwb_csg_temporal_ctest_timeout_seconds}
            )
        endfunction()

        nwb_add_csg_temporal_smoke(temporal_full full static natural 8)
        nwb_add_csg_temporal_smoke(consecutive_full full static natural 1)
        nwb_add_csg_temporal_smoke(consecutive_direct direct static natural 1)
        nwb_add_csg_temporal_smoke(consecutive_indirect indirect static natural 1)
        nwb_add_csg_temporal_smoke(consecutive_software full static software 1)
        nwb_add_csg_temporal_smoke(animated full animated natural 8)
        nwb_add_csg_temporal_smoke(animated_software full animated software 8)
        nwb_add_csg_temporal_smoke(temporal_lagged full static natural 8 "--lagged-lighting")
        nwb_add_csg_temporal_smoke(geometry_normal normal static natural 8)
        nwb_add_csg_temporal_smoke(geometry_position position static natural 8)
        nwb_add_csg_temporal_smoke(cap_state cap_state static natural 8)
        nwb_add_csg_temporal_smoke(cap_state_software cap_state static software 8)
        nwb_add_csg_temporal_smoke(overlapping_spans span_state static natural 8 "--overlapping")
        nwb_add_csg_temporal_smoke(overlapping_spans_software span_state static software 8 "--overlapping")

        unset(_nwb_csg_visible_asset_stamp)
        unset(_nwb_csg_visible_runtime_root)
        unset(_nwb_csg_visible_generated_root)
        unset(_nwb_csg_visible_asset_inputs)

        set(_nwb_csg_skinned_visible_runtime_root "${CMAKE_BINARY_DIR}/Testing/csg_skinned_visible_smoke_runtime/$<CONFIG>")

        nwb_declare_executable(nwb_csg_skinned_visible_smoke)
        target_sources(nwb_csg_skinned_visible_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/csg_skinned_visible_project.cpp"
        )
        nwb_configure_runtime_smoke_target(
            nwb_csg_skinned_visible_smoke
            "${_nwb_csg_skinned_visible_runtime_root}"
            nwb_ecs_csg
            nwb_ecs_model
            nwb_ecs_mesh_skinning
        )

        file(GLOB_RECURSE _nwb_csg_skinned_visible_asset_inputs
            CONFIGURE_DEPENDS
            LIST_DIRECTORIES false
            "${PROJECT_SOURCE_DIR}/impl/assets/*"
            "${CMAKE_CURRENT_LIST_DIR}/assets/*"
        )

        set(_nwb_csg_skinned_visible_asset_stamp "${_nwb_csg_skinned_visible_runtime_root}/res/.nwb_csg_skinned_visible_assets_$<CONFIG>.stamp")
        add_custom_command(
            OUTPUT "${_nwb_csg_skinned_visible_asset_stamp}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${_nwb_csg_skinned_visible_runtime_root}/res"
            COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/pipeline/launch.py" --skip-build
                --tool-directory "$<TARGET_FILE_DIR:nwb_asset_builder>"
                --repo-root "${PROJECT_SOURCE_DIR}"
                --asset-root "impl/assets"
                --asset-root "tests/smoke/assets"
                --output-directory "${_nwb_csg_skinned_visible_runtime_root}/res"
                --cache-directory "${_nwb_smoke_asset_cache_directory}"
                --configuration "$<CONFIG>"
            COMMAND "${CMAKE_COMMAND}" -E touch "${_nwb_csg_skinned_visible_asset_stamp}"
            DEPENDS
                nwb_dependency_computer nwb_asset_builder nwb_asset_gatherer
                "${PROJECT_SOURCE_DIR}/pipeline/launch.py"
                ${_nwb_csg_skinned_visible_asset_inputs}
            WORKING_DIRECTORY "${NWB_OUTPUT_ROOT}"
            COMMENT "Cooking NWB skinned CSG visible smoke assets for $<CONFIG>..."
            VERBATIM
            USES_TERMINAL
        )
        add_custom_target(nwb_csg_skinned_visible_smoke_assets DEPENDS "${_nwb_csg_skinned_visible_asset_stamp}")
        add_dependencies(nwb_csg_skinned_visible_smoke nwb_csg_skinned_visible_smoke_assets)

        nwb_add_window_capture_smoke(
            nwb_csg_skinned_visible_capture_early_smoke
            csg_skinned_visible_capture_early.bmp
            "$<TARGET_FILE:nwb_csg_skinned_visible_smoke>"
            "${_nwb_csg_skinned_visible_runtime_root}"
            "NWB Skinned CSG Smoke"
            "--settle-seconds" "${_nwb_smoke_capture_early_settle_seconds}"
            "--reject-log-message" "[ERROR]"
            "--reject-log-message" "failed to resolve shader"
        )

        nwb_add_window_capture_smoke(
            nwb_csg_skinned_visible_capture_smoke
            csg_skinned_visible_capture.bmp
            "$<TARGET_FILE:nwb_csg_skinned_visible_smoke>"
            "${_nwb_csg_skinned_visible_runtime_root}"
            "NWB Skinned CSG Smoke"
            "--settle-seconds" "${_nwb_smoke_capture_mid_settle_seconds}"
            "--reject-log-message" "[ERROR]"
            "--reject-log-message" "failed to resolve shader"
        )

        nwb_add_window_capture_smoke(
            nwb_csg_skinned_visible_capture_late_smoke
            csg_skinned_visible_capture_late.bmp
            "$<TARGET_FILE:nwb_csg_skinned_visible_smoke>"
            "${_nwb_csg_skinned_visible_runtime_root}"
            "NWB Skinned CSG Smoke"
            "--settle-seconds" "${_nwb_smoke_capture_late_settle_seconds}"
            "--reject-log-message" "[ERROR]"
            "--reject-log-message" "failed to resolve shader"
        )

        nwb_declare_executable(nwb_csg_skinned_sphere_visible_smoke)
        target_sources(nwb_csg_skinned_sphere_visible_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/csg_skinned_visible_project.cpp"
        )
        target_compile_definitions(nwb_csg_skinned_sphere_visible_smoke PRIVATE
            NWB_CSG_SKINNED_VISIBLE_SPHERE_CUTTER=1
        )
        nwb_configure_runtime_smoke_target(
            nwb_csg_skinned_sphere_visible_smoke
            "${_nwb_csg_skinned_visible_runtime_root}"
            nwb_ecs_csg
            nwb_ecs_model
            nwb_ecs_mesh_skinning
        )
        add_dependencies(nwb_csg_skinned_sphere_visible_smoke nwb_csg_skinned_visible_smoke_assets)

        nwb_add_window_capture_smoke(
            nwb_csg_skinned_sphere_visible_capture_early_smoke
            csg_skinned_sphere_visible_capture_early.bmp
            "$<TARGET_FILE:nwb_csg_skinned_sphere_visible_smoke>"
            "${_nwb_csg_skinned_visible_runtime_root}"
            "NWB Skinned Sphere CSG Smoke"
            "--settle-seconds" "${_nwb_smoke_capture_early_settle_seconds}"
            "--reject-log-message" "[ERROR]"
            "--reject-log-message" "failed to resolve shader"
        )

        nwb_add_window_capture_smoke(
            nwb_csg_skinned_sphere_visible_capture_mid_smoke
            csg_skinned_sphere_visible_capture_mid.bmp
            "$<TARGET_FILE:nwb_csg_skinned_sphere_visible_smoke>"
            "${_nwb_csg_skinned_visible_runtime_root}"
            "NWB Skinned Sphere CSG Smoke"
            "--settle-seconds" "${_nwb_smoke_capture_mid_settle_seconds}"
            "--reject-log-message" "[ERROR]"
            "--reject-log-message" "failed to resolve shader"
        )

        nwb_add_window_capture_smoke(
            nwb_csg_skinned_sphere_visible_capture_late_smoke
            csg_skinned_sphere_visible_capture_late.bmp
            "$<TARGET_FILE:nwb_csg_skinned_sphere_visible_smoke>"
            "${_nwb_csg_skinned_visible_runtime_root}"
            "NWB Skinned Sphere CSG Smoke"
            "--settle-seconds" "${_nwb_smoke_capture_late_settle_seconds}"
            "--reject-log-message" "[ERROR]"
            "--reject-log-message" "failed to resolve shader"
        )

        nwb_declare_executable(nwb_csg_skinned_transparent_sphere_visible_smoke)
        target_sources(nwb_csg_skinned_transparent_sphere_visible_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/csg_skinned_visible_project.cpp"
        )
        target_compile_definitions(nwb_csg_skinned_transparent_sphere_visible_smoke PRIVATE
            NWB_CSG_SKINNED_VISIBLE_SPHERE_CUTTER=1
            NWB_CSG_SKINNED_VISIBLE_TRANSPARENT_RECEIVER=1
        )
        nwb_configure_runtime_smoke_target(
            nwb_csg_skinned_transparent_sphere_visible_smoke
            "${_nwb_csg_skinned_visible_runtime_root}"
            nwb_ecs_csg
            nwb_ecs_model
            nwb_ecs_mesh_skinning
        )
        add_dependencies(nwb_csg_skinned_transparent_sphere_visible_smoke nwb_csg_skinned_visible_smoke_assets)

        nwb_add_window_capture_smoke(
            nwb_csg_skinned_transparent_sphere_visible_capture_early_smoke
            csg_skinned_transparent_sphere_visible_capture_early.bmp
            "$<TARGET_FILE:nwb_csg_skinned_transparent_sphere_visible_smoke>"
            "${_nwb_csg_skinned_visible_runtime_root}"
            "NWB Transparent Skinned Sphere CSG Smoke"
            "--settle-seconds" "${_nwb_smoke_capture_early_settle_seconds}"
            "--reject-log-message" "[ERROR]"
            "--reject-log-message" "failed to resolve shader"
        )

        nwb_add_window_capture_smoke(
            nwb_csg_skinned_transparent_sphere_visible_capture_mid_smoke
            csg_skinned_transparent_sphere_visible_capture_mid.bmp
            "$<TARGET_FILE:nwb_csg_skinned_transparent_sphere_visible_smoke>"
            "${_nwb_csg_skinned_visible_runtime_root}"
            "NWB Transparent Skinned Sphere CSG Smoke"
            "--settle-seconds" "${_nwb_smoke_capture_mid_settle_seconds}"
            "--reject-log-message" "[ERROR]"
            "--reject-log-message" "failed to resolve shader"
        )

        nwb_add_window_capture_smoke(
            nwb_csg_skinned_transparent_sphere_visible_capture_late_smoke
            csg_skinned_transparent_sphere_visible_capture_late.bmp
            "$<TARGET_FILE:nwb_csg_skinned_transparent_sphere_visible_smoke>"
            "${_nwb_csg_skinned_visible_runtime_root}"
            "NWB Transparent Skinned Sphere CSG Smoke"
            "--settle-seconds" "${_nwb_smoke_capture_late_settle_seconds}"
            "--reject-log-message" "[ERROR]"
            "--reject-log-message" "failed to resolve shader"
        )

        unset(_nwb_csg_skinned_visible_asset_stamp)
        unset(_nwb_csg_skinned_visible_runtime_root)
        unset(_nwb_csg_skinned_visible_asset_inputs)
    endif()

    set(_nwb_skinning_culling_model_nwb "${PROJECT_SOURCE_DIR}/tests/smoke/assets/characters/body.nwb")

    if(TARGET nwb_loader AND TARGET nwb_asset_builder AND EXISTS "${_nwb_skinning_culling_model_nwb}")
        set(_nwb_skinning_culling_runtime_root "${CMAKE_BINARY_DIR}/Testing/skinning_culling_benchmark_runtime/$<CONFIG>")
        if(TARGET nwb_logserver)
            set(_nwb_skinning_culling_logserver_args "--logserver-executable" "$<TARGET_FILE:nwb_logserver>")
        else()
            set(_nwb_skinning_culling_logserver_args "--no-logserver")
        endif()

        nwb_declare_executable(nwb_skinning_culling_benchmark)
        target_sources(nwb_skinning_culling_benchmark PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/skinning_culling_benchmark_project.cpp"
        )
        nwb_configure_runtime_smoke_target(
            nwb_skinning_culling_benchmark
            "${_nwb_skinning_culling_runtime_root}"
            nwb_ecs_model
            nwb_ecs_mesh_skinning
        )

        file(GLOB_RECURSE _nwb_skinning_culling_asset_inputs
            CONFIGURE_DEPENDS
            LIST_DIRECTORIES false
            "${PROJECT_SOURCE_DIR}/impl/assets/*"
            "${CMAKE_CURRENT_LIST_DIR}/assets/*"
        )

        set(_nwb_skinning_culling_asset_stamp "${_nwb_skinning_culling_runtime_root}/res/.nwb_skinning_culling_benchmark_assets_$<CONFIG>.stamp")
        add_custom_command(
            OUTPUT "${_nwb_skinning_culling_asset_stamp}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${_nwb_skinning_culling_runtime_root}/res"
            COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/pipeline/launch.py" --skip-build
                --tool-directory "$<TARGET_FILE_DIR:nwb_asset_builder>"
                --repo-root "${PROJECT_SOURCE_DIR}"
                --asset-root "impl/assets"
                --asset-root "tests/smoke/assets"
                --output-directory "${_nwb_skinning_culling_runtime_root}/res"
                --cache-directory "${_nwb_smoke_asset_cache_directory}"
                --configuration "$<CONFIG>"
            COMMAND "${CMAKE_COMMAND}" -E touch "${_nwb_skinning_culling_asset_stamp}"
            DEPENDS
                nwb_dependency_computer nwb_asset_builder nwb_asset_gatherer
                "${PROJECT_SOURCE_DIR}/pipeline/launch.py"
                "${_nwb_skinning_culling_model_nwb}"
                ${_nwb_skinning_culling_asset_inputs}
            WORKING_DIRECTORY "${NWB_OUTPUT_ROOT}"
            COMMENT "Cooking NWB Skinning Culling Benchmark assets for $<CONFIG>..."
            VERBATIM
            USES_TERMINAL
        )
        add_custom_target(nwb_skinning_culling_benchmark_assets DEPENDS "${_nwb_skinning_culling_asset_stamp}")
        add_dependencies(nwb_skinning_culling_benchmark nwb_skinning_culling_benchmark_assets)

        add_test(
            NAME nwb_skinning_culling_benchmark
            CONFIGURATIONS fin
            COMMAND
                "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_LIST_DIR}/runtime_log_smoke.py"
                "--executable" "$<TARGET_FILE:nwb_skinning_culling_benchmark>"
                "--working-directory" "${_nwb_skinning_culling_runtime_root}"
                "--timeout" "${_nwb_skinning_culling_benchmark_script_timeout_seconds}"
                ${_nwb_skinning_culling_logserver_args}
                "--expect-log-message" "SkinningCullingBenchmark: completed"
                "--expect-log-message" "Telemetry upload processed:"
        )
        set_tests_properties(nwb_skinning_culling_benchmark PROPERTIES
            ENVIRONMENT "NWB_SKINNING_CULLING_FAST_SMOKE=1"
            RESOURCE_LOCK nwb_display
            SKIP_RETURN_CODE 77
            TIMEOUT ${_nwb_skinning_culling_benchmark_ctest_timeout_seconds}
        )

        # Shadows and caustics must follow live deformed normals rather than bind pose.
        nwb_declare_executable(nwb_skinned_caustic_smoke)
        target_sources(nwb_skinned_caustic_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/skinned_caustic_project.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/framebuffer_capture.cpp"
        )
        target_compile_definitions(nwb_skinned_caustic_smoke PRIVATE NWB_SMOKE_ENABLE_SWAP_CHAIN_READBACK=1)
        nwb_configure_runtime_smoke_target(
            nwb_skinned_caustic_smoke
            "${_nwb_skinning_culling_runtime_root}"
            nwb_ecs_csg
            nwb_ecs_model
            nwb_ecs_mesh_skinning
        )
        add_dependencies(nwb_skinned_caustic_smoke nwb_skinning_culling_benchmark_assets)

        add_test(NAME nwb_skinned_caustic_csg_transition_smoke
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/skinned_caustic_csg_transition_smoke.py"
                --executable "$<TARGET_FILE:nwb_skinned_caustic_smoke>"
                --working-directory "${_nwb_skinning_culling_runtime_root}"
                --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/skinned_caustic_csg_transition"
                --timeout 180
                ${_nwb_skinning_culling_logserver_args}
            CONFIGURATIONS dbg opt
        )
        set_tests_properties(nwb_skinned_caustic_csg_transition_smoke PROPERTIES
            RESOURCE_LOCK nwb_display SKIP_RETURN_CODE 77 TIMEOUT 690
        )

        nwb_add_window_capture_smoke(
            nwb_skinned_caustic_capture_early_smoke
            skinned_caustic_capture_early.bmp
            "$<TARGET_FILE:nwb_skinned_caustic_smoke>"
            "${_nwb_skinning_culling_runtime_root}"
            "NWB Skinned Caustic Smoke"
            "--settle-seconds" "${_nwb_smoke_capture_early_settle_seconds}"
            "--reject-log-message" "[ERROR]"
            "--reject-log-message" "failed to resolve shader"
        )
        nwb_add_window_capture_smoke(
            nwb_skinned_caustic_capture_mid_smoke
            skinned_caustic_capture_mid.bmp
            "$<TARGET_FILE:nwb_skinned_caustic_smoke>"
            "${_nwb_skinning_culling_runtime_root}"
            "NWB Skinned Caustic Smoke"
            "--settle-seconds" "${_nwb_smoke_capture_mid_settle_seconds}"
            "--reject-log-message" "[ERROR]"
            "--reject-log-message" "failed to resolve shader"
        )
        nwb_add_window_capture_smoke(
            nwb_skinned_caustic_capture_late_smoke
            skinned_caustic_capture_late.bmp
            "$<TARGET_FILE:nwb_skinned_caustic_smoke>"
            "${_nwb_skinning_culling_runtime_root}"
            "NWB Skinned Caustic Smoke"
            "--settle-seconds" "${_nwb_skinned_caustic_capture_late_settle_seconds}"
            "--reject-log-message" "[ERROR]"
            "--reject-log-message" "failed to resolve shader"
        )

        # The crowd stresses scene updates and opaque/transparent shadows across two lights.
        nwb_declare_executable(nwb_stress_test_smoke)
        target_sources(nwb_stress_test_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/stress_test_project.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/stress_csg_scene.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/stress_csg_scene.h"
            "${CMAKE_CURRENT_LIST_DIR}/smoke_cpu_gpu_timing_probe.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/smoke_cpu_gpu_timing_probe.h"
            "${CMAKE_CURRENT_LIST_DIR}/avboit_timing_render_pass.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/presentation_fps_probe.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/presentation_fps_probe.h"
            "${CMAKE_CURRENT_LIST_DIR}/presentation_pacing_ring.h"
        )
        nwb_configure_runtime_smoke_target(
            nwb_stress_test_smoke
            "${_nwb_skinning_culling_runtime_root}"
            nwb_ecs_model
            nwb_ecs_mesh_skinning
        )
        add_dependencies(nwb_stress_test_smoke nwb_skinning_culling_benchmark_assets)

        if(TARGET nwb_logserver)
            add_test(NAME nwb_stress_presentation_timing_smoke
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/stress_timing_smoke.py"
                    --executable "$<TARGET_FILE:nwb_stress_test_smoke>"
                    --working-directory "${_nwb_skinning_culling_runtime_root}"
                    --logserver-executable "$<TARGET_FILE:nwb_logserver>"
                CONFIGURATIONS dbg opt
            )
            set_tests_properties(nwb_stress_presentation_timing_smoke PROPERTIES
                RESOURCE_LOCK nwb_display TIMEOUT 120 SKIP_RETURN_CODE 77
            )
        endif()

        nwb_add_window_capture_smoke(
            nwb_stress_test_capture_smoke
            stress_test_capture.bmp
            "$<TARGET_FILE:nwb_stress_test_smoke>"
            "${_nwb_skinning_culling_runtime_root}"
            "NWB Stress Test Smoke"
            "--expect-log-message" "StressTestSmokeProject: spawned 20 spinning characters (10 transparent + 10 opaque) over ground, directional + point light"
            "--expect-log-message" "StressTestSmokeProject: shutdown"
        )
        set_tests_properties(nwb_stress_test_capture_smoke PROPERTIES
            ENVIRONMENT "NWB_STRESS_TEST_SPIN_ANGLE=0.6"
        )

        # Hardware transparent shadows are compared against opaque-only hardware shadows. The test-owned A/B
        # harness launches this same binary twice, selecting an opaque-only scene for its hardware baseline arm.
        nwb_declare_executable(nwb_hardware_shadow_boundary_healthy_benchmark)
        target_sources(nwb_hardware_shadow_boundary_healthy_benchmark PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/stress_test_project.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/stress_csg_scene.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/stress_csg_scene.h"
            "${CMAKE_CURRENT_LIST_DIR}/smoke_cpu_gpu_timing_probe.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/smoke_cpu_gpu_timing_probe.h"
            "${CMAKE_CURRENT_LIST_DIR}/avboit_timing_render_pass.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/presentation_fps_probe.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/presentation_fps_probe.h"
            "${CMAKE_CURRENT_LIST_DIR}/presentation_pacing_ring.h"
        )
        target_compile_definitions(nwb_hardware_shadow_boundary_healthy_benchmark PRIVATE
            NWB_HARDWARE_SHADOW_BOUNDARY_BENCHMARK=1
        )
        nwb_configure_runtime_smoke_target(
            nwb_hardware_shadow_boundary_healthy_benchmark
            "${_nwb_skinning_culling_runtime_root}"
            nwb_ecs_model
            nwb_ecs_mesh_skinning
        )
        add_dependencies(nwb_hardware_shadow_boundary_healthy_benchmark nwb_skinning_culling_benchmark_assets)

        nwb_add_window_capture_smoke(
            nwb_hardware_shadow_boundary_healthy_capture_smoke
            hardware_shadow_boundary_healthy_capture.bmp
            "$<TARGET_FILE:nwb_hardware_shadow_boundary_healthy_benchmark>"
            "${_nwb_skinning_culling_runtime_root}"
            "NWB Hardware Shadow Boundary Benchmark"
            "--settle-seconds" "${_nwb_smoke_capture_late_settle_seconds}"
            "--expect-log-message" "StressTestSmokeProject: enabled healthy hardware transparent-shadow benchmark"
            "--expect-log-message" "StressTestSmokeProject: RayQuery-capable hardware shadow route available"
            "--expect-log-message" "RendererSystem: dispatched hardware transparent shadow traversal"
            "--skip-log-message" "StressTestSmokeProject: hardware shadow boundary skipped because RayQuery-capable hardware is unavailable"
            "--skip-blocking-log-message" "[ERROR]"
            "--skip-blocking-log-message" "VUID-"
            "--skip-blocking-log-message" "Validation Error"
            "--skip-blocking-log-message" "failed to resolve shader"
            "--reject-log-message" "RendererSystem: split opaque soft-shadow producer failed\; retaining all-lit visibility"
            "--reject-log-message" "RendererSystem: split opaque soft-shadow first wavelet failed\; retaining all-lit visibility"
            "--reject-log-message" "RendererSystem: split opaque soft-shadow resolve tail failed\; retaining all-lit visibility"
            "--reject-log-message" "RendererSystem: split transparent soft-shadow trace could not record\; preserving opaque visibility"
            "--reject-log-message" "RendererSystem: split transparent soft-shadow temporal merge failed\; preserving opaque visibility"
            "--reject-log-message" "RendererSystem: split transparent soft-shadow wavelet lost its temporal timing envelope\; preserving opaque visibility"
            "--reject-log-message" "RendererSystem: split transparent soft-shadow first wavelet failed\; preserving opaque visibility"
            "--reject-log-message" "RendererSystem: split transparent soft-shadow resolve failed\; preserving opaque visibility"
            "--reject-log-message" "RendererSystem: split transparent soft-shadow trace or first wavelet failed\; preserving opaque visibility"
            "--reject-log-message" "RendererSystem: ray-traced shadow visibility pass failed"
            "--reject-log-message" "RendererSystem: dispatched software shadow traversal"
            "--reject-log-message" "RendererSystem: hardware transparent shadow resource preparation failed\; colored shadows unavailable this frame"
            "--reject-log-message" "RendererSystem: changed HW shadow material context has no graph-owned upload batch"
            "--reject-log-message" "[ERROR]"
            "--reject-log-message" "VUID-"
            "--reject-log-message" "Validation Error"
            "--reject-log-message" "failed to resolve shader"
            CONFIGURATIONS dbg opt
        )
        set_tests_properties(nwb_hardware_shadow_boundary_healthy_capture_smoke PROPERTIES
            ENVIRONMENT "NWB_HARDWARE_SHADOW_BOUNDARY_OPAQUE_BASELINE=0;NWB_STRESS_TEST_SPIN_ANGLE=0.6"
        )

        nwb_add_window_capture_smoke(
            nwb_hardware_shadow_boundary_opaque_capture_smoke
            hardware_shadow_boundary_opaque_capture.bmp
            "$<TARGET_FILE:nwb_hardware_shadow_boundary_healthy_benchmark>"
            "${_nwb_skinning_culling_runtime_root}"
            "NWB Hardware Shadow Boundary Benchmark"
            "--settle-seconds" "${_nwb_smoke_capture_late_settle_seconds}"
            "--expect-log-message" "StressTestSmokeProject: enabled natural opaque hardware-shadow baseline"
            "--expect-log-message" "StressTestSmokeProject: RayQuery-capable hardware shadow route available"
            "--skip-log-message" "StressTestSmokeProject: hardware shadow boundary skipped because RayQuery-capable hardware is unavailable"
            "--skip-blocking-log-message" "[ERROR]"
            "--skip-blocking-log-message" "VUID-"
            "--skip-blocking-log-message" "Validation Error"
            "--skip-blocking-log-message" "failed to resolve shader"
            "--reject-log-message" "RendererSystem: dispatched software shadow traversal"
            "--reject-log-message" "RendererSystem: dispatched hardware transparent shadow traversal"
            "--reject-log-message" "RendererSystem: split opaque soft-shadow producer failed\; retaining all-lit visibility"
            "--reject-log-message" "RendererSystem: split opaque soft-shadow first wavelet failed\; retaining all-lit visibility"
            "--reject-log-message" "RendererSystem: split opaque soft-shadow resolve tail failed\; retaining all-lit visibility"
            "--reject-log-message" "RendererSystem: split transparent soft-shadow trace could not record\; preserving opaque visibility"
            "--reject-log-message" "RendererSystem: split transparent soft-shadow temporal merge failed\; preserving opaque visibility"
            "--reject-log-message" "RendererSystem: split transparent soft-shadow wavelet lost its temporal timing envelope\; preserving opaque visibility"
            "--reject-log-message" "RendererSystem: split transparent soft-shadow first wavelet failed\; preserving opaque visibility"
            "--reject-log-message" "RendererSystem: split transparent soft-shadow resolve failed\; preserving opaque visibility"
            "--reject-log-message" "RendererSystem: split transparent soft-shadow trace or first wavelet failed\; preserving opaque visibility"
            "--reject-log-message" "RendererSystem: ray-traced shadow visibility pass failed"
            "--reject-log-message" "RendererSystem: changed HW shadow material context has no graph-owned upload batch"
            "--reject-log-message" "[ERROR]"
            "--reject-log-message" "VUID-"
            "--reject-log-message" "Validation Error"
            "--reject-log-message" "failed to resolve shader"
            CONFIGURATIONS dbg opt
        )
        set_tests_properties(nwb_hardware_shadow_boundary_opaque_capture_smoke PROPERTIES
            ENVIRONMENT "NWB_HARDWARE_SHADOW_BOUNDARY_OPAQUE_BASELINE=1;NWB_STRESS_TEST_SPIN_ANGLE=0.6"
        )

        # Both M4 arms share assets; the runner requires a distinct Compute queue for the async arm.
        nwb_declare_executable(nwb_async_shadow_m4_sync_benchmark)
        target_sources(nwb_async_shadow_m4_sync_benchmark PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/stress_frame_graph_snapshot.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/stress_frame_graph_snapshot.h"
            "${CMAKE_CURRENT_LIST_DIR}/stress_test_project.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/stress_csg_scene.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/stress_csg_scene.h"
            "${CMAKE_CURRENT_LIST_DIR}/smoke_cpu_gpu_timing_probe.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/smoke_cpu_gpu_timing_probe.h"
            "${CMAKE_CURRENT_LIST_DIR}/avboit_timing_render_pass.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/presentation_fps_probe.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/presentation_fps_probe.h"
            "${CMAKE_CURRENT_LIST_DIR}/presentation_pacing_ring.h"
        )
        target_compile_definitions(nwb_async_shadow_m4_sync_benchmark PRIVATE
            NWB_ASYNC_SHADOW_M4_BENCHMARK=1
            NWB_SMOKE_DISABLE_ASYNC_COMPUTE=1
        )
        nwb_configure_runtime_smoke_target(
            nwb_async_shadow_m4_sync_benchmark
            "${_nwb_skinning_culling_runtime_root}"
            nwb_ecs_model
            nwb_ecs_mesh_skinning
        )
        target_link_libraries(nwb_async_shadow_m4_sync_benchmark PRIVATE nwb_telemetry)
        add_dependencies(nwb_async_shadow_m4_sync_benchmark nwb_skinning_culling_benchmark_assets)

        nwb_declare_executable(nwb_async_shadow_m4_async_benchmark)
        target_sources(nwb_async_shadow_m4_async_benchmark PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/stress_frame_graph_snapshot.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/stress_frame_graph_snapshot.h"
            "${CMAKE_CURRENT_LIST_DIR}/stress_test_project.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/stress_csg_scene.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/stress_csg_scene.h"
            "${CMAKE_CURRENT_LIST_DIR}/smoke_cpu_gpu_timing_probe.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/smoke_cpu_gpu_timing_probe.h"
            "${CMAKE_CURRENT_LIST_DIR}/avboit_timing_render_pass.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/presentation_fps_probe.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/presentation_fps_probe.h"
            "${CMAKE_CURRENT_LIST_DIR}/presentation_pacing_ring.h"
        )
        target_compile_definitions(nwb_async_shadow_m4_async_benchmark PRIVATE
            NWB_ASYNC_SHADOW_M4_BENCHMARK=1
        )
        nwb_configure_runtime_smoke_target(
            nwb_async_shadow_m4_async_benchmark
            "${_nwb_skinning_culling_runtime_root}"
            nwb_ecs_model
            nwb_ecs_mesh_skinning
        )
        target_link_libraries(nwb_async_shadow_m4_async_benchmark PRIVATE nwb_telemetry)
        add_dependencies(nwb_async_shadow_m4_async_benchmark nwb_skinning_culling_benchmark_assets)

        nwb_declare_executable(nwb_flicker_test_smoke)
        target_sources(nwb_flicker_test_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/flicker_test_project.cpp"
        )
        nwb_configure_runtime_smoke_target(
            nwb_flicker_test_smoke
            "${_nwb_skinning_culling_runtime_root}"
            nwb_ecs_model
            nwb_ecs_mesh_skinning
        )
        add_dependencies(nwb_flicker_test_smoke nwb_skinning_culling_benchmark_assets)

        nwb_add_window_capture_smoke(
            nwb_flicker_test_capture_smoke
            flicker_test_capture.bmp
            "$<TARGET_FILE:nwb_flicker_test_smoke>"
            "${_nwb_skinning_culling_runtime_root}"
            "NWB Flicker Test"
            "--expect-log-message" "FlickerTestSmokeProject: spawned one opaque + one transparent character over an opaque ground plane, directional + point light"
            "--expect-log-message" "FlickerTestSmokeProject: shutdown"
        )
        set_tests_properties(nwb_flicker_test_capture_smoke PROPERTIES
            ENVIRONMENT "NWB_FLICKER_TEST_SPIN_ANGLE=0.6"
        )

        nwb_declare_executable(nwb_soft_shadow_test_smoke)
        target_sources(nwb_soft_shadow_test_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/soft_shadow_test_project.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/shadow_timing_render_pass.h"
            "${CMAKE_CURRENT_LIST_DIR}/shadow_timing_render_pass.cpp"
        )
        nwb_configure_runtime_smoke_target(
            nwb_soft_shadow_test_smoke
            "${_nwb_skinning_culling_runtime_root}"
            nwb_ecs_model
            nwb_ecs_mesh_skinning
        )
        add_dependencies(nwb_soft_shadow_test_smoke nwb_skinning_culling_benchmark_assets)

        nwb_add_window_capture_smoke(
            nwb_soft_shadow_test_capture_smoke
            soft_shadow_test_capture.bmp
            "$<TARGET_FILE:nwb_soft_shadow_test_smoke>"
            "${_nwb_skinning_culling_runtime_root}"
            "NWB Soft Shadow Test"
            "--expect-log-message" "SoftShadowTestSmokeProject: opaque + glass characters on a ground plane, 3 coloured lights"
            "--expect-log-message" "SoftShadowTestSmokeProject: shutdown"
        )
        set_tests_properties(nwb_soft_shadow_test_capture_smoke PROPERTIES
            ENVIRONMENT "NWB_SOFT_SHADOW_TEST_SPIN_ANGLE=0.6"
        )

        # The default surfel-GI smoke preserves the open-top box used by the pinned renderer baseline. A closed,
        # 20-body skylight scene is available to opt-in A/B captures through NWB_GI_SMOKE_COMPLEX_SCENE.
        nwb_declare_executable(nwb_gi_test_smoke)
        target_compile_definitions(nwb_gi_test_smoke PRIVATE NWB_SMOKE_ENABLE_SWAP_CHAIN_READBACK=1)
        target_sources(nwb_gi_test_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/gi_test_project.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/csg_gi_scene.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/csg_gi_scene.h"
            "${CMAKE_CURRENT_LIST_DIR}/framebuffer_capture.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/framebuffer_capture.h"
            "${CMAKE_CURRENT_LIST_DIR}/presentation_fps_probe.cpp"
            "${CMAKE_CURRENT_LIST_DIR}/presentation_fps_probe.h"
        )
        nwb_configure_runtime_smoke_target(
            nwb_gi_test_smoke
            "${_nwb_skinning_culling_runtime_root}"
            nwb_ecs_csg
        )
        add_dependencies(nwb_gi_test_smoke nwb_skinning_culling_benchmark_assets)

        nwb_add_window_capture_smoke(
            nwb_gi_test_capture_smoke
            gi_test_capture.bmp
            "$<TARGET_FILE:nwb_gi_test_smoke>"
            "${_nwb_skinning_culling_runtime_root}"
            "NWB GI Test"
            "--expect-log-message" "GiTestSmokeProject: open-top box + red/blue opposite walls, directional light -> indirect red+blue bleed on shadowed floor"
            "--expect-log-message" "GiTestSmokeProject: shutdown"
        )

        # Diagnostic material reads the resolved GI alpha on two perpendicular faces in the same spatial cell.
        # Keep this opt-in fixture separate from the unchanged formal surfel-GI baseline scene.
        nwb_add_window_capture_smoke(
            nwb_gi_coverage_probe_capture_smoke
            gi_coverage_probe_capture.bmp
            "$<TARGET_FILE:nwb_gi_test_smoke>"
            "${_nwb_skinning_culling_runtime_root}"
            "NWB GI Test"
            "--settle-seconds" "12"
            "$<IF:$<CONFIG:fin>,--no-gpu-validation,--gpu-validation>"
            "--timeout" "75"
            "--expect-log-message" "GiTestSmokeProject: coverage probe with perpendicular same-cell faces created"
            "--expect-log-message" "GiTestSmokeProject: shutdown"
            "--expect-surfel-coverage-probe"
        )
        set_tests_properties(nwb_gi_coverage_probe_capture_smoke PROPERTIES
            ENVIRONMENT "NWB_GI_SMOKE_COVERAGE_VIEW=1"
            TIMEOUT 90
        )

        foreach(_nwb_csg_gi_route IN ITEMS natural software)
            foreach(_nwb_csg_gi_lighting IN ITEMS current lagged)
                set(_nwb_csg_gi_lighting_suffix "")
                set(_nwb_csg_gi_lighting_args --no-lagged-lighting)
                if(_nwb_csg_gi_lighting STREQUAL "lagged")
                    set(_nwb_csg_gi_lighting_suffix "_lagged")
                    set(_nwb_csg_gi_lighting_args --lagged-lighting)
                endif()
                add_test(
                    NAME "nwb_csg_gi_${_nwb_csg_gi_route}${_nwb_csg_gi_lighting_suffix}_smoke"
                    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/csg_gi_smoke.py"
                        --executable "$<TARGET_FILE:nwb_gi_test_smoke>"
                        --working-directory "${_nwb_skinning_culling_runtime_root}"
                        --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/csg_gi_${_nwb_csg_gi_route}${_nwb_csg_gi_lighting_suffix}"
                        --route "${_nwb_csg_gi_route}"
                        ${_nwb_csg_gi_lighting_args}
                        ${_nwb_skinning_culling_logserver_args}
                        "$<IF:$<CONFIG:fin>,--no-gpu-validation,--gpu-validation>"
                )
                set_tests_properties("nwb_csg_gi_${_nwb_csg_gi_route}${_nwb_csg_gi_lighting_suffix}_smoke" PROPERTIES
                    RESOURCE_LOCK nwb_display SKIP_RETURN_CODE 77 TIMEOUT 900 LABELS "csg_gi"
                )
            endforeach()
        endforeach()
        unset(_nwb_csg_gi_route)
        unset(_nwb_csg_gi_lighting)
        unset(_nwb_csg_gi_lighting_suffix)
        unset(_nwb_csg_gi_lighting_args)

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
