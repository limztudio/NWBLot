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
    nwb_target_adhd_sources(nwb_texture_smoke NAMESPACE "NWB::Tests::Smoke" SOURCES
            "${CMAKE_CURRENT_LIST_DIR}/texture_smoke_project.adhd"
        )
    target_compile_definitions(nwb_texture_smoke PRIVATE NWB_SMOKE_ENABLE_SWAP_CHAIN_READBACK=1)
    nwb_configure_runtime_smoke_target(nwb_texture_smoke "${_nwb_texture_smoke_runtime_root}")

    file(GLOB_RECURSE _nwb_texture_smoke_asset_inputs CONFIGURE_DEPENDS LIST_DIRECTORIES false "${PROJECT_SOURCE_DIR}/impl/assets/*" "${PROJECT_SOURCE_DIR}/global/slang/*" "${CMAKE_CURRENT_LIST_DIR}/assets/*")

    add_custom_command(OUTPUT "${_nwb_texture_smoke_source_nwb}" "${_nwb_texture_smoke_source_tex}" COMMAND "${CMAKE_COMMAND}" -E make_directory "${_nwb_texture_smoke_generated_texture_directory}"
        COMMAND "${Python3_EXECUTABLE}" "${_nwb_texture_smoke_generator}" --output "${_nwb_texture_smoke_source_png}" COMMAND "$<TARGET_FILE:nwb_tex_conv>" "${_nwb_texture_smoke_source_png}" --output "${_nwb_texture_smoke_source_nwb}" --force DEPENDS
            nwb_tex_conv "${_nwb_texture_smoke_generator}" "${PROJECT_SOURCE_DIR}/tests/common/png_fixture.py" COMMENT "Generating NWB texture smoke source asset for $<CONFIG>..." VERBATIM USES_TERMINAL)

    set(_nwb_texture_smoke_asset_stamp "${_nwb_texture_smoke_runtime_root}/res/.nwb_texture_smoke_assets_$<CONFIG>.stamp")
    add_custom_command(OUTPUT "${_nwb_texture_smoke_asset_stamp}" COMMAND "${CMAKE_COMMAND}" -E make_directory "${_nwb_texture_smoke_runtime_root}/res" COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/pipeline/launch.py" --skip-build
            --tool-directory "$<TARGET_FILE_DIR:nwb_asset_builder>" --repo-root "${PROJECT_SOURCE_DIR}" --asset-root "impl/assets" --asset-root "tests/smoke/assets" --asset-root "${_nwb_texture_smoke_generated_asset_root}"
            --output-directory "${_nwb_texture_smoke_runtime_root}/res" --configuration "$<CONFIG>" COMMAND "${CMAKE_COMMAND}" -E touch "${_nwb_texture_smoke_asset_stamp}" DEPENDS
            nwb_dependency_computer nwb_asset_builder nwb_asset_gatherer "${PROJECT_SOURCE_DIR}/pipeline/launch.py" "${_nwb_texture_smoke_source_nwb}" "${_nwb_texture_smoke_source_tex}" ${_nwb_texture_smoke_asset_inputs}
        WORKING_DIRECTORY "${NWB_OUTPUT_ROOT}" COMMENT "Cooking NWB texture smoke assets for $<CONFIG>..." VERBATIM USES_TERMINAL)
    add_custom_target(nwb_texture_smoke_assets DEPENDS "${_nwb_texture_smoke_asset_stamp}")
    add_dependencies(nwb_texture_smoke nwb_texture_smoke_assets)

    nwb_add_window_capture_smoke(nwb_texture_capture_smoke texture_smoke_capture.bmp "$<TARGET_FILE:nwb_texture_smoke>" "${_nwb_texture_smoke_runtime_root}" "NWB Texture Smoke" "--application-capture"
        "--application-capture-frame-count" "${_nwb_texture_smoke_capture_frame_count}"
        "--expect-texture-smoke" "--expect-log-message" "TextureSmokeProject: bounded sRGB clear edges preserve alpha and neighboring pixels" "--reject-log-message" "[ERROR]" "--reject-log-message" "failed to resolve shader")

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
