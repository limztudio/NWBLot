include("${CMAKE_CURRENT_LIST_DIR}/fbx_to_nwb_test_helpers.cmake")

file(MAKE_DIRECTORY "${OUTPUT_DIR}")
# Semantic argument failures remain actionable on stderr when ordinary logger diagnostics are compiled out.
function(nwb_require_cli_rejection case_name diagnostic)
    set(rejected_output "${OUTPUT_DIR}/rejected_${case_name}.nwb")
    set(destination_before "CLI validation must preserve this destination")
    file(WRITE "${rejected_output}" "${destination_before}")
    execute_process(
        COMMAND "${FBX_TO_NWB_EXE}" "${INPUT_FBX}"
            --output "${rejected_output}" --preserve-space --yes --force ${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(result EQUAL 0)
        message(FATAL_ERROR "fbx_to_nwb accepted invalid CLI case ${case_name}")
    endif()
    require_match("${stderr}" "${diagnostic}" "Invalid CLI case ${case_name} omitted its stderr diagnostic:\n${stdout}\n${stderr}")
    file(READ "${rejected_output}" destination_after)
    if(NOT destination_after STREQUAL destination_before)
        message(FATAL_ERROR "Invalid CLI case ${case_name} replaced the destination")
    endif()
endfunction()

nwb_require_cli_rejection(normal_mode "normal mode must be imported, smooth, or regenerate" --normal-mode unsupported)
nwb_require_cli_rejection(scale "positive finite number" --scale -1)
nwb_require_cli_rejection(triangle_area "finite non-negative number" --triangle-area-length-squared-epsilon -1)
nwb_require_cli_rejection(default_color "four finite numbers" --default-color 1,1,1)
nwb_require_cli_rejection(mesh_index "available zero-based index" --mesh 999999)
nwb_require_cli_rejection(separate_assets "Only valid with asset type 'bunch'" --asset-type mesh --separate-assets)

# Removed CLI aliases fail without replacing the destination, and the canonical spelling recovers.
foreach(asset_type_alias IN ITEMS asset_bunch asset-bunch)
    set(alias_output "${OUTPUT_DIR}/rejected_${asset_type_alias}.nwb")
    set(destination_before "asset type rejection must preserve this destination")
    file(WRITE "${alias_output}" "${destination_before}")
    execute_process(
        COMMAND "${FBX_TO_NWB_EXE}" "${INPUT_FBX}"
            --output "${alias_output}" --asset-type "${asset_type_alias}"
            --mesh first --normal-mode smooth --preserve-space --yes --force
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(result EQUAL 0)
        message(FATAL_ERROR "fbx_to_nwb accepted removed asset type ${asset_type_alias}")
    endif()
    require_match("${stderr}" "Asset type must be bunch, mesh, model, skeleton, or skin"
        "Removed asset type ${asset_type_alias} did not report its canonical alternatives:\n${stdout}\n${stderr}")
    file(READ "${alias_output}" destination_after)
    if(NOT destination_after STREQUAL destination_before)
        message(FATAL_ERROR "Rejected asset type ${asset_type_alias} replaced the destination")
    endif()
    execute_process(
        COMMAND "${FBX_TO_NWB_EXE}" "${INPUT_FBX}"
            --output "${alias_output}" --asset-type bunch
            --mesh first --normal-mode smooth --preserve-space --yes --force
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "fbx_to_nwb canonical bunch recovery failed:\n${stdout}\n${stderr}")
    endif()
    require_crlf_text_file("${alias_output}" "canonical bunch recovery")
    file(READ "${alias_output}" recovered_text)
    require_match("${recovered_text}" "asset_bunch bunch = \\[" "Canonical bunch recovery omitted asset bunch metadata")
endforeach()
