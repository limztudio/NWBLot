include_guard(GLOBAL)

function(nwb_configure_name_symbols)
    if(NOT NWB_BUILDMODE)
        return()
    endif()

    add_compile_definitions(NWB_BUILDMODE=1 GLB_BUILD_SYMBOLS=1)
endfunction()

# Build and run an isolated NWB_BUILDMODE variant, then copy its Name sidecars into the release output.
# Run on demand because GUI capture needs a display:
#   cmake --build <release-build-dir> --config <cfg> --target nwb_namesym
function(nwb_add_name_symbol_target)
    # The capture build must not recursively create another capture target.
    if(NWB_BUILDMODE)
        return()
    endif()

    if(WIN32)
        set(_namesym_platform_prefix "windows-clang-namesym")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(_namesym_platform_prefix "linux-clang-namesym")
    else()
        message(STATUS "nwb_namesym: no build-mode preset for ${CMAKE_SYSTEM_NAME}; target not created.")
        return()
    endif()

    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(NOT Python3_Interpreter_FOUND)
        message(STATUS "nwb_namesym: Python3 interpreter not found; target not created.")
        return()
    endif()

    string(TOLOWER "${CMAKE_SYSTEM_NAME}" _namesym_output_platform)
    set(_namesym_configure_preset "${_namesym_platform_prefix}-${NWB_OUTPUT_ARCH}")
    if(NWB_OUTPUT_ARCH STREQUAL "x64")
        set(_namesym_build_preset "${_namesym_platform_prefix}-$<CONFIG>")
    else()
        set(_namesym_build_preset "${_namesym_platform_prefix}-${NWB_OUTPUT_ARCH}-$<CONFIG>")
    endif()
    set(_namesym_build_dir "${PROJECT_SOURCE_DIR}/__cmake/build/${_namesym_configure_preset}")
    set(_namesym_buildmode_bin_dir "${PROJECT_SOURCE_DIR}/__exec/${_namesym_output_platform}/${NWB_OUTPUT_ARCH}/namesym/$<CONFIG>")
    set(_namesym_release_dest "${NWB_OUTPUT_ROOT}/$<CONFIG>")

    # A headless cook captures pipeline and asset names using a separate output/cache tree.
    set(_namesym_cook_out "${_namesym_build_dir}/namesym_cook/res")
    set(_namesym_cook_cache "${PROJECT_SOURCE_DIR}/__build_obj/c/${_namesym_output_platform}/${NWB_OUTPUT_ARCH}/namesym")
    set(_namesym_cook_run "${PROJECT_SOURCE_DIR}/pipeline/launch.py|||--skip-build|||--tool-directory|||${_namesym_buildmode_bin_dir}|||--repo-root|||${PROJECT_SOURCE_DIR}|||--asset-root|||impl/assets|||--asset-root|||tests/smoke/assets|||--output-directory|||${_namesym_cook_out}|||--cache-directory|||${_namesym_cook_cache}|||--configuration|||$<CONFIG>")

    # GUI captures must exit gracefully to write sidecars; --expect-sidecar reports missing output.
    add_custom_target(nwb_namesym
        COMMAND "${CMAKE_COMMAND}" -E env "CMAKE_COMMAND=${CMAKE_COMMAND}"
            "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/launcher/generate_name_symbols.py"
            --source-dir "${PROJECT_SOURCE_DIR}"
            --configure-preset "${_namesym_configure_preset}"
            --build-preset "${_namesym_build_preset}"
            --build-dir "${_namesym_build_dir}"
            --config "$<CONFIG>"
            --buildmode-bin-dir "${_namesym_buildmode_bin_dir}"
            --dest "${_namesym_release_dest}"
            --mkdir "${_namesym_cook_out}"
            --mkdir "${_namesym_cook_cache}"
            --run "${_namesym_cook_run}"
            --ctest-regex "nwb_testbed_window_capture_smoke"
            --ctest-regex "nwb_skinned_caustic_capture_late_smoke"
            --expect-sidecar "dependency_computer.namesym"
            --expect-sidecar "asset_builder.namesym"
            --expect-sidecar "asset_gatherer.namesym"
            --expect-sidecar "testbed.namesym"
        VERBATIM
        USES_TERMINAL
        COMMENT "nwb_namesym: capturing Name symbols from a build-mode run -> ${_namesym_release_dest}"
    )
endfunction()
