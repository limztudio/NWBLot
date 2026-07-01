find_package(Python3 COMPONENTS Interpreter)

if(Python3_Interpreter_FOUND)
    function(nwb_add_window_capture_smoke TEST_NAME OUTPUT_NAME EXECUTABLE_PATH WORKING_DIRECTORY WINDOW_TITLE)
        if(TARGET nwb_logserver)
            set(_nwb_smoke_logserver_args "--logserver-executable" "$<TARGET_FILE:nwb_logserver>")
        else()
            set(_nwb_smoke_logserver_args "--no-logserver")
        endif()
        add_test(
            NAME "${TEST_NAME}"
            COMMAND
                "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_LIST_DIR}/testbed_window_capture_smoke.py"
                "--executable" "${EXECUTABLE_PATH}"
                "--working-directory" "${WORKING_DIRECTORY}"
                "--output" "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/${OUTPUT_NAME}"
                "--window-title" "${WINDOW_TITLE}"
                ${_nwb_smoke_logserver_args}
                ${ARGN}
        )
        set_tests_properties("${TEST_NAME}" PROPERTIES
            RESOURCE_LOCK nwb_display
            SKIP_RETURN_CODE 77
            TIMEOUT 60
        )
    endfunction()

    function(nwb_configure_runtime_smoke_target TARGET_NAME RUNTIME_ROOT)
        set_target_properties("${TARGET_NAME}" PROPERTIES
            DEBUGGER_WORKING_DIRECTORY "${RUNTIME_ROOT}"
            WIN32_EXECUTABLE ON
            VS_DEBUGGER_WORKING_DIRECTORY "${RUNTIME_ROOT}"
        )
        target_link_libraries("${TARGET_NAME}" PRIVATE
            nwb_common
            nwb_alloc
            nwb_loader
            nwb_ecs_scene
            nwb_ecs_mesh
            nwb_ecs_render
            ${ARGN}
            "$<LINK_LIBRARY:WHOLE_ARCHIVE,nwb_assets_mesh>"
            "$<LINK_LIBRARY:WHOLE_ARCHIVE,nwb_assets_skeleton>"
            "$<LINK_LIBRARY:WHOLE_ARCHIVE,nwb_assets_model>"
            "$<LINK_LIBRARY:WHOLE_ARCHIVE,nwb_assets_material>"
            "$<LINK_LIBRARY:WHOLE_ARCHIVE,nwb_assets_shader>"
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
        )
    endif()

    if(TARGET nwb_loader AND TARGET nwb_resource_cooker)
        set(_nwb_transparent_multi_runtime_root "${CMAKE_BINARY_DIR}/Testing/smoke_runtime/$<CONFIG>")

        nwb_declare_executable(nwb_transparent_multi_smoke)
        target_sources(nwb_transparent_multi_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/transparent_multi_project.cpp"
        )
        nwb_configure_runtime_smoke_target(nwb_transparent_multi_smoke "${_nwb_transparent_multi_runtime_root}")

        nwb_declare_executable(nwb_transparent_multi_sw_smoke)
        target_sources(nwb_transparent_multi_sw_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/transparent_multi_project.cpp"
        )
        target_compile_definitions(nwb_transparent_multi_sw_smoke PRIVATE
            NWB_TRANSPARENT_MULTI_FORCE_RT_EMULATION=1
        )
        nwb_configure_runtime_smoke_target(nwb_transparent_multi_sw_smoke "${_nwb_transparent_multi_runtime_root}")

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
            COMMAND "$<TARGET_FILE:nwb_resource_cooker>"
                --repo-root "${PROJECT_SOURCE_DIR}"
                --asset-root "impl/assets"
                --asset-root "tests/smoke/assets"
                --output-directory "${_nwb_transparent_multi_runtime_root}/res"
                --cache-directory "${PROJECT_SOURCE_DIR}/__build_obj/asset_cache/${NWB_OUTPUT_PLATFORM}/${NWB_OUTPUT_ARCH}/${NWB_OUTPUT_DOMAIN}"
                --configuration "$<CONFIG>"
            COMMAND "${CMAKE_COMMAND}" -E touch "${_nwb_transparent_multi_asset_stamp}"
            DEPENDS
                nwb_resource_cooker
                ${_nwb_transparent_multi_asset_inputs}
            WORKING_DIRECTORY "${NWB_OUTPUT_ROOT}"
            COMMENT "Cooking NWB transparent smoke assets for $<CONFIG>..."
            VERBATIM
            USES_TERMINAL
        )
        add_custom_target(nwb_transparent_multi_smoke_assets DEPENDS "${_nwb_transparent_multi_asset_stamp}")
        add_dependencies(nwb_transparent_multi_smoke nwb_transparent_multi_smoke_assets)
        add_dependencies(nwb_transparent_multi_sw_smoke nwb_transparent_multi_smoke_assets)

        nwb_declare_executable(nwb_transparent_csg_smoke)
        target_sources(nwb_transparent_csg_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/transparent_multi_project.cpp"
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
        )

        nwb_add_window_capture_smoke(
            nwb_transparent_multi_sw_capture_smoke
            transparent_multi_sw_capture.bmp
            "$<TARGET_FILE:nwb_transparent_multi_sw_smoke>"
            "${_nwb_transparent_multi_runtime_root}"
            "NWB Transparent Multi Smoke"
            "--expect-transparent-multi"
            "--expect-log-message" "RendererSystem: dispatched software shadow traversal"
            "--reject-log-message" "[ERROR]"
            "--reject-log-message" "failed to resolve shader"
        )

        nwb_add_window_capture_smoke(
            nwb_transparent_csg_capture_early_smoke
            transparent_csg_capture_early.bmp
            "$<TARGET_FILE:nwb_transparent_csg_smoke>"
            "${_nwb_transparent_multi_runtime_root}"
            "NWB Transparent CSG Smoke"
            "--expect-transparent-multi"
            "--expect-transparent-csg"
            "--settle-seconds" "0.75"
            "--reject-log-message" "[ERROR]"
            "--reject-log-message" "failed to resolve shader"
        )

        nwb_add_window_capture_smoke(
            nwb_transparent_csg_capture_mid_smoke
            transparent_csg_capture_mid.bmp
            "$<TARGET_FILE:nwb_transparent_csg_smoke>"
            "${_nwb_transparent_multi_runtime_root}"
            "NWB Transparent CSG Smoke"
            "--expect-transparent-multi"
            "--expect-transparent-csg"
            "--settle-seconds" "2.0"
            "--reject-log-message" "[ERROR]"
            "--reject-log-message" "failed to resolve shader"
        )

        nwb_add_window_capture_smoke(
            nwb_transparent_csg_capture_late_smoke
            transparent_csg_capture_late.bmp
            "$<TARGET_FILE:nwb_transparent_csg_smoke>"
            "${_nwb_transparent_multi_runtime_root}"
            "NWB Transparent CSG Smoke"
            "--expect-transparent-multi"
            "--expect-transparent-csg"
            "--settle-seconds" "3.25"
            "--reject-log-message" "[ERROR]"
            "--reject-log-message" "failed to resolve shader"
        )

        nwb_declare_executable(nwb_caustic_sphere_smoke)
        target_sources(nwb_caustic_sphere_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/transparent_multi_project.cpp"
        )
        target_compile_definitions(nwb_caustic_sphere_smoke PRIVATE
            NWB_TRANSPARENT_MULTI_CAUSTIC_SPHERE=1
        )
        nwb_configure_runtime_smoke_target(nwb_caustic_sphere_smoke "${_nwb_transparent_multi_runtime_root}")
        add_dependencies(nwb_caustic_sphere_smoke nwb_transparent_multi_smoke_assets)

        nwb_add_window_capture_smoke(
            nwb_caustic_sphere_capture_smoke
            caustic_sphere_capture.bmp
            "$<TARGET_FILE:nwb_caustic_sphere_smoke>"
            "${_nwb_transparent_multi_runtime_root}"
            "NWB Caustic Sphere Smoke"
            "--settle-seconds" "3.0"
            "--reject-log-message" "[ERROR]"
            "--reject-log-message" "failed to resolve shader"
        )

        # Forced-software-emulation sibling of the caustic sphere demo: identical scene, but the SOFTWARE caustic
        # producer (P3) runs even on RT hardware, so the two can be captured + diffed to verify the HW (P4) and SW
        # producers converge to the same caustic (the byte-parallel parity gate).
        nwb_declare_executable(nwb_caustic_sphere_sw_smoke)
        target_sources(nwb_caustic_sphere_sw_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/transparent_multi_project.cpp"
        )
        target_compile_definitions(nwb_caustic_sphere_sw_smoke PRIVATE
            NWB_TRANSPARENT_MULTI_CAUSTIC_SPHERE=1
            NWB_TRANSPARENT_MULTI_FORCE_RT_EMULATION=1
        )
        nwb_configure_runtime_smoke_target(nwb_caustic_sphere_sw_smoke "${_nwb_transparent_multi_runtime_root}")
        add_dependencies(nwb_caustic_sphere_sw_smoke nwb_transparent_multi_smoke_assets)

        unset(_nwb_transparent_multi_asset_stamp)
        unset(_nwb_transparent_multi_runtime_root)
        unset(_nwb_transparent_multi_asset_inputs)

        set(_nwb_csg_visible_runtime_root "${CMAKE_BINARY_DIR}/Testing/csg_visible_smoke_runtime/$<CONFIG>")

        nwb_declare_executable(nwb_csg_visible_smoke)
        target_sources(nwb_csg_visible_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/csg_visible_project.cpp"
        )
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
        )

        set(_nwb_csg_visible_asset_stamp "${_nwb_csg_visible_runtime_root}/res/.nwb_csg_visible_assets_$<CONFIG>.stamp")
        add_custom_command(
            OUTPUT "${_nwb_csg_visible_asset_stamp}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${_nwb_csg_visible_runtime_root}/res"
            COMMAND "$<TARGET_FILE:nwb_resource_cooker>"
                --repo-root "${PROJECT_SOURCE_DIR}"
                --asset-root "impl/assets"
                --asset-root "tests/smoke/assets"
                --output-directory "${_nwb_csg_visible_runtime_root}/res"
                --cache-directory "${PROJECT_SOURCE_DIR}/__build_obj/asset_cache/${NWB_OUTPUT_PLATFORM}/${NWB_OUTPUT_ARCH}/${NWB_OUTPUT_DOMAIN}"
                --configuration "$<CONFIG>"
            COMMAND "${CMAKE_COMMAND}" -E touch "${_nwb_csg_visible_asset_stamp}"
            DEPENDS
                nwb_resource_cooker
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
            "--reject-log-message" "[ERROR]"
            "--reject-log-message" "failed to resolve shader"
        )

        nwb_declare_executable(nwb_csg_visible_compute_emulation_smoke)
        target_sources(nwb_csg_visible_compute_emulation_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/csg_visible_project.cpp"
        )
        target_compile_definitions(nwb_csg_visible_compute_emulation_smoke PRIVATE
            NWB_CSG_VISIBLE_FORCE_MESHLET_EMULATION=1
        )
        nwb_configure_runtime_smoke_target(
            nwb_csg_visible_compute_emulation_smoke
            "${_nwb_csg_visible_runtime_root}"
            nwb_ecs_csg
        )
        add_dependencies(nwb_csg_visible_compute_emulation_smoke nwb_csg_visible_smoke_assets)

        nwb_add_window_capture_smoke(
            nwb_csg_visible_compute_emulation_capture_smoke
            csg_visible_compute_emulation_capture.bmp
            "$<TARGET_FILE:nwb_csg_visible_compute_emulation_smoke>"
            "${_nwb_csg_visible_runtime_root}"
            "NWB CSG Visible Compute Emulation Smoke"
            "--expect-log-message" "selected CS + PS by compiling its mesh shader for compute emulation"
            "--reject-log-message" "[ERROR]"
            "--reject-log-message" "failed to resolve shader"
        )
        set_tests_properties(nwb_csg_visible_compute_emulation_capture_smoke PROPERTIES
            CONFIGURATIONS "dbg;opt"
        )

        unset(_nwb_csg_visible_asset_stamp)
        unset(_nwb_csg_visible_runtime_root)
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
            COMMAND "$<TARGET_FILE:nwb_resource_cooker>"
                --repo-root "${PROJECT_SOURCE_DIR}"
                --asset-root "impl/assets"
                --asset-root "tests/smoke/assets"
                --output-directory "${_nwb_csg_skinned_visible_runtime_root}/res"
                --cache-directory "${PROJECT_SOURCE_DIR}/__build_obj/asset_cache/${NWB_OUTPUT_PLATFORM}/${NWB_OUTPUT_ARCH}/${NWB_OUTPUT_DOMAIN}"
                --configuration "$<CONFIG>"
            COMMAND "${CMAKE_COMMAND}" -E touch "${_nwb_csg_skinned_visible_asset_stamp}"
            DEPENDS
                nwb_resource_cooker
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
            "--settle-seconds" "0.75"
            "--reject-log-message" "[ERROR]"
            "--reject-log-message" "failed to resolve shader"
        )

        nwb_add_window_capture_smoke(
            nwb_csg_skinned_visible_capture_smoke
            csg_skinned_visible_capture.bmp
            "$<TARGET_FILE:nwb_csg_skinned_visible_smoke>"
            "${_nwb_csg_skinned_visible_runtime_root}"
            "NWB Skinned CSG Smoke"
            "--settle-seconds" "2.0"
            "--reject-log-message" "[ERROR]"
            "--reject-log-message" "failed to resolve shader"
        )

        nwb_add_window_capture_smoke(
            nwb_csg_skinned_visible_capture_late_smoke
            csg_skinned_visible_capture_late.bmp
            "$<TARGET_FILE:nwb_csg_skinned_visible_smoke>"
            "${_nwb_csg_skinned_visible_runtime_root}"
            "NWB Skinned CSG Smoke"
            "--settle-seconds" "3.25"
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
            "--settle-seconds" "0.75"
            "--reject-log-message" "[ERROR]"
            "--reject-log-message" "failed to resolve shader"
        )

        nwb_add_window_capture_smoke(
            nwb_csg_skinned_sphere_visible_capture_mid_smoke
            csg_skinned_sphere_visible_capture_mid.bmp
            "$<TARGET_FILE:nwb_csg_skinned_sphere_visible_smoke>"
            "${_nwb_csg_skinned_visible_runtime_root}"
            "NWB Skinned Sphere CSG Smoke"
            "--settle-seconds" "2.0"
            "--reject-log-message" "[ERROR]"
            "--reject-log-message" "failed to resolve shader"
        )

        nwb_add_window_capture_smoke(
            nwb_csg_skinned_sphere_visible_capture_late_smoke
            csg_skinned_sphere_visible_capture_late.bmp
            "$<TARGET_FILE:nwb_csg_skinned_sphere_visible_smoke>"
            "${_nwb_csg_skinned_visible_runtime_root}"
            "NWB Skinned Sphere CSG Smoke"
            "--settle-seconds" "3.25"
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
            "--settle-seconds" "0.75"
            "--reject-log-message" "[ERROR]"
            "--reject-log-message" "failed to resolve shader"
        )

        nwb_add_window_capture_smoke(
            nwb_csg_skinned_transparent_sphere_visible_capture_mid_smoke
            csg_skinned_transparent_sphere_visible_capture_mid.bmp
            "$<TARGET_FILE:nwb_csg_skinned_transparent_sphere_visible_smoke>"
            "${_nwb_csg_skinned_visible_runtime_root}"
            "NWB Transparent Skinned Sphere CSG Smoke"
            "--settle-seconds" "2.0"
            "--reject-log-message" "[ERROR]"
            "--reject-log-message" "failed to resolve shader"
        )

        nwb_add_window_capture_smoke(
            nwb_csg_skinned_transparent_sphere_visible_capture_late_smoke
            csg_skinned_transparent_sphere_visible_capture_late.bmp
            "$<TARGET_FILE:nwb_csg_skinned_transparent_sphere_visible_smoke>"
            "${_nwb_csg_skinned_visible_runtime_root}"
            "NWB Transparent Skinned Sphere CSG Smoke"
            "--settle-seconds" "3.25"
            "--reject-log-message" "[ERROR]"
            "--reject-log-message" "failed to resolve shader"
        )

        unset(_nwb_csg_skinned_visible_asset_stamp)
        unset(_nwb_csg_skinned_visible_runtime_root)
        unset(_nwb_csg_skinned_visible_asset_inputs)
    endif()

    set(_nwb_skinning_culling_model_nwb "${PROJECT_SOURCE_DIR}/tests/smoke/assets/characters/body.nwb")

    if(TARGET nwb_loader AND TARGET nwb_resource_cooker AND EXISTS "${_nwb_skinning_culling_model_nwb}")
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
            COMMAND "$<TARGET_FILE:nwb_resource_cooker>"
                --repo-root "${PROJECT_SOURCE_DIR}"
                --asset-root "impl/assets"
                --asset-root "tests/smoke/assets"
                --output-directory "${_nwb_skinning_culling_runtime_root}/res"
                --cache-directory "${PROJECT_SOURCE_DIR}/__build_obj/asset_cache/${NWB_OUTPUT_PLATFORM}/${NWB_OUTPUT_ARCH}/${NWB_OUTPUT_DOMAIN}"
                --configuration "$<CONFIG>"
            COMMAND "${CMAKE_COMMAND}" -E touch "${_nwb_skinning_culling_asset_stamp}"
            DEPENDS
                nwb_resource_cooker
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
                "--timeout" "240"
                ${_nwb_skinning_culling_logserver_args}
                "--expect-log-message" "SkinningCullingBenchmark: completed"
        )
        set_tests_properties(nwb_skinning_culling_benchmark PROPERTIES
            RESOURCE_LOCK nwb_display
            SKIP_RETURN_CODE 77
            TIMEOUT 300
        )

        # Skinned-glass caustic validation: a SKINNED refractor whose skeleton pose deforms every frame, over an opaque
        # ground under one light. Validates the per-frame skinned-normal repack (repack_normals_cs / dispatchRepackNormals)
        # -- the RT shadow + caustic must bend on the LIVE deformed normals. Reuses the benchmark's cooked assets (same
        # impl/assets + tests/smoke/assets cook; the body.nwb model + the new engine repack shader).
        nwb_declare_executable(nwb_skinned_caustic_smoke)
        target_sources(nwb_skinned_caustic_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/skinned_caustic_project.cpp"
        )
        nwb_configure_runtime_smoke_target(
            nwb_skinned_caustic_smoke
            "${_nwb_skinning_culling_runtime_root}"
            nwb_ecs_model
            nwb_ecs_mesh_skinning
        )
        add_dependencies(nwb_skinned_caustic_smoke nwb_skinning_culling_benchmark_assets)

        # Forced-software-emulation sibling: the SW shadow + caustic path runs even on RT hardware, for the HW-vs-SW
        # parity A/B (both paths read the same per-frame repacked attribute buffer).
        nwb_declare_executable(nwb_skinned_caustic_sw_smoke)
        target_sources(nwb_skinned_caustic_sw_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/skinned_caustic_project.cpp"
        )
        target_compile_definitions(nwb_skinned_caustic_sw_smoke PRIVATE
            NWB_SKINNED_CAUSTIC_FORCE_RT_EMULATION=1
        )
        nwb_configure_runtime_smoke_target(
            nwb_skinned_caustic_sw_smoke
            "${_nwb_skinning_culling_runtime_root}"
            nwb_ecs_model
            nwb_ecs_mesh_skinning
        )
        add_dependencies(nwb_skinned_caustic_sw_smoke nwb_skinning_culling_benchmark_assets)

        nwb_add_window_capture_smoke(
            nwb_skinned_caustic_capture_early_smoke
            skinned_caustic_capture_early.bmp
            "$<TARGET_FILE:nwb_skinned_caustic_smoke>"
            "${_nwb_skinning_culling_runtime_root}"
            "NWB Skinned Caustic Smoke"
            "--settle-seconds" "0.75"
            "--reject-log-message" "[ERROR]"
            "--reject-log-message" "failed to resolve shader"
        )
        nwb_add_window_capture_smoke(
            nwb_skinned_caustic_capture_mid_smoke
            skinned_caustic_capture_mid.bmp
            "$<TARGET_FILE:nwb_skinned_caustic_smoke>"
            "${_nwb_skinning_culling_runtime_root}"
            "NWB Skinned Caustic Smoke"
            "--settle-seconds" "2.0"
            "--reject-log-message" "[ERROR]"
            "--reject-log-message" "failed to resolve shader"
        )
        nwb_add_window_capture_smoke(
            nwb_skinned_caustic_capture_late_smoke
            skinned_caustic_capture_late.bmp
            "$<TARGET_FILE:nwb_skinned_caustic_smoke>"
            "${_nwb_skinning_culling_runtime_root}"
            "NWB Skinned Caustic Smoke"
            "--settle-seconds" "4.0"
            "--reject-log-message" "[ERROR]"
            "--reject-log-message" "failed to resolve shader"
        )

        # Load-time STRESS scene: TEN animated skinned `body` characters (5 transparent + 5 opaque) over a ground plane
        # under a directional + a point light. Reuses the benchmark's cooked assets (body model + transparent_multi
        # glass/ground materials); exercises per-frame skinning x10 + SW BVH refits + the hybrid shadow path across two
        # shadowed lights at load.
        nwb_declare_executable(nwb_stress_test_smoke)
        target_sources(nwb_stress_test_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/stress_test_project.cpp"
        )
        nwb_configure_runtime_smoke_target(
            nwb_stress_test_smoke
            "${_nwb_skinning_culling_runtime_root}"
            nwb_ecs_model
            nwb_ecs_mesh_skinning
        )
        add_dependencies(nwb_stress_test_smoke nwb_skinning_culling_benchmark_assets)

        # Forced-software-emulation sibling: the SW shadow path runs even on RT hardware, for the HW-vs-SW parity A/B.
        nwb_declare_executable(nwb_stress_test_sw_smoke)
        target_sources(nwb_stress_test_sw_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/stress_test_project.cpp"
        )
        target_compile_definitions(nwb_stress_test_sw_smoke PRIVATE
            NWB_STRESS_TEST_FORCE_RT_EMULATION=1
        )
        nwb_configure_runtime_smoke_target(
            nwb_stress_test_sw_smoke
            "${_nwb_skinning_culling_runtime_root}"
            nwb_ecs_model
            nwb_ecs_mesh_skinning
        )
        add_dependencies(nwb_stress_test_sw_smoke nwb_skinning_culling_benchmark_assets)

        # Minimal FLICKER-REPRO scene: ONE opaque `body` character on ONE opaque ground plane under a SINGLE directional
        # light, arrow-key-spun. The tightest isolation of the hard-shadow edge crawl (no transparency, no point light, no
        # crowd). Reuses the benchmark's cooked assets (body model + ground material).
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

        # Forced-software-emulation sibling: the SW shadow path runs even on RT hardware, for the HW-vs-SW flicker A/B.
        nwb_declare_executable(nwb_flicker_test_sw_smoke)
        target_sources(nwb_flicker_test_sw_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/flicker_test_project.cpp"
        )
        target_compile_definitions(nwb_flicker_test_sw_smoke PRIVATE
            NWB_FLICKER_TEST_FORCE_RT_EMULATION=1
        )
        nwb_configure_runtime_smoke_target(
            nwb_flicker_test_sw_smoke
            "${_nwb_skinning_culling_runtime_root}"
            nwb_ecs_model
            nwb_ecs_mesh_skinning
        )
        add_dependencies(nwb_flicker_test_sw_smoke nwb_skinning_culling_benchmark_assets)

        # Dedicated SOFT-SHADOW scene (Stage 1): ONE opaque `body` character on ONE opaque ground plane under a SINGLE
        # directional light with a physical angular source size (env NWB_SOFT_SHADOW_TEST_ANGLE). Isolates the soft
        # directional penumbra (contact hardening + distance-widening), arrow-key-spun for a crawl check. Reuses the
        # benchmark's cooked assets (body model + ground material).
        nwb_declare_executable(nwb_soft_shadow_test_smoke)
        target_sources(nwb_soft_shadow_test_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/soft_shadow_test_project.cpp"
        )
        nwb_configure_runtime_smoke_target(
            nwb_soft_shadow_test_smoke
            "${_nwb_skinning_culling_runtime_root}"
            nwb_ecs_model
            nwb_ecs_mesh_skinning
        )
        add_dependencies(nwb_soft_shadow_test_smoke nwb_skinning_culling_benchmark_assets)

        # Forced-software-emulation sibling: runs the FULL soft pipeline (half-res jittered trace -> a-trous denoise ->
        # bilateral upsample) even on RT hardware -- the intended way to see the denoised soft shadow.
        nwb_declare_executable(nwb_soft_shadow_test_sw_smoke)
        target_sources(nwb_soft_shadow_test_sw_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/soft_shadow_test_project.cpp"
        )
        target_compile_definitions(nwb_soft_shadow_test_sw_smoke PRIVATE
            NWB_SOFT_SHADOW_TEST_FORCE_RT_EMULATION=1
        )
        nwb_configure_runtime_smoke_target(
            nwb_soft_shadow_test_sw_smoke
            "${_nwb_skinning_culling_runtime_root}"
            nwb_ecs_model
            nwb_ecs_mesh_skinning
        )
        add_dependencies(nwb_soft_shadow_test_sw_smoke nwb_skinning_culling_benchmark_assets)

        unset(_nwb_skinning_culling_asset_stamp)
        unset(_nwb_skinning_culling_model_nwb)
        unset(_nwb_skinning_culling_runtime_root)
        unset(_nwb_skinning_culling_asset_inputs)
        unset(_nwb_skinning_culling_logserver_args)
    else()
        message(STATUS "Skipping skinning culling benchmark; missing tests/smoke/assets/characters/body.nwb.")
    endif()
    unset(_nwb_skinning_culling_model_nwb)
endif()
