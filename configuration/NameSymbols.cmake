include_guard(GLOBAL)

function(nwb_configure_name_symbols)
    if(NOT NWB_BUILDMODE)
        return()
    endif()

    add_compile_definitions(NWB_BUILDMODE=1)
endfunction()

# Creates the on-demand "nwb_namesym" target for a release (non-buildmode) configuration. Building it runs the full
# capture the user asked for in one step -- "compile NWB_BUILDMODE -> create namesym -> use it from the non-buildmode
# build" -- without anyone configuring/building twice by hand:
#
#   cmake --build <release-build-dir> --config <cfg> --target nwb_namesym
#
# It drives configuration/generate_name_symbols.py, which configures + builds the buildmode variant (preset
# "<platform>-clang-namesym-x64", output domain "namesym", separate binaryDir so the release tree is untouched), runs
# its workloads, and copies the produced "<exe>.namesym" sidecars into this configuration's output root. The release
# exes there load them at startup (NameSymbols::LoadDefaultFile) so Name::c_str() resolves its own hashes -> readable
# opt/fin logs.
#
# It is an explicit on-demand target, NOT a dependency of the normal build: the GUI workloads (testbed + window-capture
# smokes) launch real windows and need a display, so wiring them into every build would be both heavy and headless-
# hostile. Run it when you want fresh sidecars.
function(nwb_add_name_symbol_target)
    # Guard against recursion: inside the buildmode variant there is nothing to generate from itself, and building this
    # target there would re-invoke the driver -> re-build the buildmode variant -> ...
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
    set(_namesym_configure_preset "${_namesym_platform_prefix}-x64")
    set(_namesym_build_dir "${PROJECT_SOURCE_DIR}/__cmake/build/${_namesym_configure_preset}")
    set(_namesym_buildmode_bin_dir "${PROJECT_SOURCE_DIR}/__exec/${_namesym_output_platform}/${NWB_OUTPUT_ARCH}/namesym/$<CONFIG>")
    set(_namesym_release_dest "${NWB_OUTPUT_ROOT}/$<CONFIG>")

    # Headless workload: a real cook (mirrors the smoke-asset cook in tests/smoke/CMakeLists.txt) so the cooker records
    # its full pipeline + asset-id literals -- not just startup symbols. Output/cache live under the build-mode tree so
    # the release asset cache is untouched. This run needs no display, so it is the graceful path on headless hosts.
    set(_namesym_cook_out "${_namesym_build_dir}/namesym_cook/res")
    set(_namesym_cook_cache "${PROJECT_SOURCE_DIR}/__build_obj/asset_cache/${_namesym_output_platform}/${NWB_OUTPUT_ARCH}/namesym")
    set(_namesym_cook_run "resource_cooker|||--repo-root|||${PROJECT_SOURCE_DIR}|||--asset-root|||impl/assets|||--asset-root|||tests/smoke/assets|||--output-directory|||${_namesym_cook_out}|||--cache-directory|||${_namesym_cook_cache}|||--configuration|||$<CONFIG>")

    # GUI workloads (need a display): the window-capture harness runs each, captures, then shuts it down GRACEFULLY
    # (posts WM_CLOSE) so the app's NWB_BUILDMODE exit writes its sidecar -- a hard kill would skip it. The testbed
    # covers the base render path; the skinned-caustic smoke adds the shadow / caustic / AVBOIT / skinning scopes the
    # testbed scene never exercises. On a headless host these ctest runs skip (warned, not fatal) and only the cook
    # sidecar is produced. --expect-sidecar surfaces a capture that silently produced nothing.
    add_custom_target(nwb_namesym
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/configuration/generate_name_symbols.py"
            --source-dir "${PROJECT_SOURCE_DIR}"
            --configure-preset "${_namesym_configure_preset}"
            --build-preset "${_namesym_platform_prefix}-$<CONFIG>"
            --build-dir "${_namesym_build_dir}"
            --config "$<CONFIG>"
            --buildmode-bin-dir "${_namesym_buildmode_bin_dir}"
            --dest "${_namesym_release_dest}"
            --mkdir "${_namesym_cook_out}"
            --mkdir "${_namesym_cook_cache}"
            --run "${_namesym_cook_run}"
            --ctest-regex "nwb_testbed_window_capture_smoke"
            --ctest-regex "nwb_skinned_caustic_capture_late_smoke"
            --expect-sidecar "resource_cooker.namesym"
            --expect-sidecar "testbed.namesym"
        VERBATIM
        USES_TERMINAL
        COMMENT "nwb_namesym: capturing Name symbols from a build-mode run -> ${_namesym_release_dest}"
    )
endfunction()
