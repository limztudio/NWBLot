find_package(Python3 COMPONENTS Interpreter)

if(Python3_Interpreter_FOUND)
    function(nwb_add_window_capture_smoke TEST_NAME OUTPUT_NAME EXECUTABLE_PATH WORKING_DIRECTORY)
        add_test(
            NAME "${TEST_NAME}"
            COMMAND
                "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_LIST_DIR}/testbed_window_capture_smoke.py"
                "--executable" "${EXECUTABLE_PATH}"
                "--working-directory" "${WORKING_DIRECTORY}"
                "--output" "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/${OUTPUT_NAME}"
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
            nwb_ecs_camera
            nwb_ecs_mesh
            nwb_ecs_lighting
            nwb_ecs_scene
            nwb_ecs_render
            ${ARGN}
            "$<LINK_LIBRARY:WHOLE_ARCHIVE,nwb_assets_mesh>"
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
        )
    endif()

    if(TARGET nwb_loader AND TARGET nwb_resource_cooker)
        set(_nwb_transparent_multi_runtime_root "${CMAKE_BINARY_DIR}/Testing/smoke_runtime/$<CONFIG>")

        nwb_declare_executable(nwb_transparent_multi_smoke)
        target_sources(nwb_transparent_multi_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/transparent_multi_project.cpp"
        )
        nwb_configure_runtime_smoke_target(nwb_transparent_multi_smoke "${_nwb_transparent_multi_runtime_root}")

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
                --cache-directory "${PROJECT_SOURCE_DIR}/__build_obj/shader_cache/${NWB_OUTPUT_PLATFORM}/${NWB_OUTPUT_ARCH}/${NWB_OUTPUT_DOMAIN}"
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

        nwb_add_window_capture_smoke(
            nwb_transparent_multi_capture_smoke
            transparent_multi_capture.bmp
            "$<TARGET_FILE:nwb_transparent_multi_smoke>"
            "${_nwb_transparent_multi_runtime_root}"
            "--expect-transparent-multi"
        )

        unset(_nwb_transparent_multi_asset_stamp)
        unset(_nwb_transparent_multi_runtime_root)
        unset(_nwb_transparent_multi_asset_inputs)
    endif()

    set(_nwb_default_skinned_cone_benchmark_fbx "$ENV{NWB_SKINNED_CONE_BENCHMARK_FBX}")
    if(_nwb_default_skinned_cone_benchmark_fbx STREQUAL "")
        set(_nwb_default_skinned_cone_benchmark_fbx "C:/Users/ltw94/Downloads/FBX Model/Medical worker.Fbx")
    endif()
    set(NWB_SKINNED_CONE_BENCHMARK_FBX "${_nwb_default_skinned_cone_benchmark_fbx}" CACHE FILEPATH "External FBX used by the skinned cone culling benchmark")

    if(TARGET nwb_fbx_to_nwb AND EXISTS "${NWB_SKINNED_CONE_BENCHMARK_FBX}")
        set(_nwb_skinned_cone_runtime_root "${CMAKE_BINARY_DIR}/Testing/skinned_cone_benchmark_runtime/$<CONFIG>")

        nwb_declare_executable(nwb_skinned_cone_benchmark)
        target_sources(nwb_skinned_cone_benchmark PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/skinned_cone_benchmark_project.cpp"
        )
        nwb_configure_runtime_smoke_target(
            nwb_skinned_cone_benchmark
            "${_nwb_skinned_cone_runtime_root}"
            nwb_ecs_skinned_mesh_render
        )

        file(GLOB_RECURSE _nwb_skinned_cone_asset_inputs
            CONFIGURE_DEPENDS
            LIST_DIRECTORIES false
            "${PROJECT_SOURCE_DIR}/impl/assets/*"
            "${PROJECT_SOURCE_DIR}/Testbed/assets/*"
        )

        set(_nwb_skinned_cone_generated_asset_root "${CMAKE_BINARY_DIR}/Testing/skinned_cone_benchmark_generated/$<CONFIG>/assets")
        set(_nwb_skinned_cone_medical_worker_nwb "${_nwb_skinned_cone_generated_asset_root}/characters/medical_worker.nwb")
        set(_nwb_skinned_cone_asset_stamp "${_nwb_skinned_cone_runtime_root}/res/.nwb_skinned_cone_benchmark_assets_$<CONFIG>.stamp")
        add_custom_command(
            OUTPUT "${_nwb_skinned_cone_asset_stamp}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${_nwb_skinned_cone_generated_asset_root}/characters"
            COMMAND "$<TARGET_FILE:nwb_fbx_to_nwb>"
                "${NWB_SKINNED_CONE_BENCHMARK_FBX}"
                --output "${_nwb_skinned_cone_medical_worker_nwb}"
                --asset-type skinned_mesh
                --normal-mode imported
                --yes
                --force
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${_nwb_skinned_cone_runtime_root}/res"
            COMMAND "$<TARGET_FILE:nwb_resource_cooker>"
                --repo-root "${PROJECT_SOURCE_DIR}"
                --asset-root "impl/assets"
                --asset-root "Testbed/assets"
                --asset-root "${_nwb_skinned_cone_generated_asset_root}"
                --output-directory "${_nwb_skinned_cone_runtime_root}/res"
                --cache-directory "${PROJECT_SOURCE_DIR}/__build_obj/shader_cache/${NWB_OUTPUT_PLATFORM}/${NWB_OUTPUT_ARCH}/${NWB_OUTPUT_DOMAIN}"
                --configuration "$<CONFIG>"
            COMMAND "${CMAKE_COMMAND}" -E touch "${_nwb_skinned_cone_asset_stamp}"
            DEPENDS
                nwb_fbx_to_nwb
                nwb_resource_cooker
                "${NWB_SKINNED_CONE_BENCHMARK_FBX}"
                ${_nwb_skinned_cone_asset_inputs}
            WORKING_DIRECTORY "${NWB_OUTPUT_ROOT}"
            COMMENT "Cooking NWB skinned cone benchmark assets for $<CONFIG>..."
            VERBATIM
            USES_TERMINAL
        )
        add_custom_target(nwb_skinned_cone_benchmark_assets DEPENDS "${_nwb_skinned_cone_asset_stamp}")
        add_dependencies(nwb_skinned_cone_benchmark nwb_skinned_cone_benchmark_assets)

        add_test(
            NAME nwb_skinned_cone_culling_benchmark
            COMMAND
                "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_LIST_DIR}/runtime_log_smoke.py"
                "--executable" "$<TARGET_FILE:nwb_skinned_cone_benchmark>"
                "--working-directory" "${_nwb_skinned_cone_runtime_root}"
                "--timeout" "240"
                "--expect-log-message" "SkinnedConeBenchmark: completed"
        )
        set_tests_properties(nwb_skinned_cone_culling_benchmark PROPERTIES
            RESOURCE_LOCK nwb_display
            SKIP_RETURN_CODE 77
            TIMEOUT 300
        )

        unset(_nwb_skinned_cone_asset_stamp)
        unset(_nwb_skinned_cone_medical_worker_nwb)
        unset(_nwb_skinned_cone_generated_asset_root)
        unset(_nwb_skinned_cone_runtime_root)
        unset(_nwb_skinned_cone_asset_inputs)
    else()
        message(STATUS "Skipping skinned cone culling benchmark; set NWB_SKINNED_CONE_BENCHMARK_FBX to an existing FBX file to enable it.")
    endif()
    unset(_nwb_default_skinned_cone_benchmark_fbx)
endif()
