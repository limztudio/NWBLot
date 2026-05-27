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

    if(TARGET testbed)
        nwb_add_window_capture_smoke(
            nwb_testbed_window_capture_smoke
            testbed_window_capture.bmp
            "$<TARGET_FILE:testbed>"
            "${NWB_OUTPUT_ROOT}"
        )
    endif()

    if(TARGET nwb_loader AND TARGET nwb_resource_cooker)
        nwb_declare_executable(nwb_transparent_multi_smoke)
        target_sources(nwb_transparent_multi_smoke PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/transparent_multi_project.cpp"
        )
        set_target_properties(nwb_transparent_multi_smoke PROPERTIES
            DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Testing/smoke_runtime/$<CONFIG>"
            WIN32_EXECUTABLE ON
            VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/Testing/smoke_runtime/$<CONFIG>"
        )
        target_link_libraries(nwb_transparent_multi_smoke PRIVATE
            nwb_common
            nwb_alloc
            nwb_loader
            nwb_ecs_camera
            nwb_ecs_mesh
            nwb_ecs_lighting
            nwb_ecs_scene
            nwb_ecs_render
            "$<LINK_LIBRARY:WHOLE_ARCHIVE,nwb_assets_mesh>"
            "$<LINK_LIBRARY:WHOLE_ARCHIVE,nwb_assets_material>"
            "$<LINK_LIBRARY:WHOLE_ARCHIVE,nwb_assets_shader>"
        )
        if(TARGET nwb_logserver)
            add_dependencies(nwb_transparent_multi_smoke nwb_logserver)
        endif()

        file(GLOB_RECURSE _nwb_transparent_multi_asset_inputs
            CONFIGURE_DEPENDS
            LIST_DIRECTORIES false
            "${PROJECT_SOURCE_DIR}/impl/assets/*"
            "${CMAKE_CURRENT_LIST_DIR}/assets/*"
        )

        set(_nwb_transparent_multi_runtime_root "${CMAKE_BINARY_DIR}/Testing/smoke_runtime/$<CONFIG>")
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
endif()
