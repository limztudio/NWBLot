include("${CMAKE_CURRENT_LIST_DIR}/fbx_to_nwb_test_helpers.cmake")

file(MAKE_DIRECTORY "${OUTPUT_DIR}")
set(smooth_output "${OUTPUT_DIR}/wedge_smooth.nwb")
set(regenerate_output "${OUTPUT_DIR}/wedge_regenerate.nwb")
set(fallback_output "${OUTPUT_DIR}/wedge_regenerate_no_uv.nwb")
set(input_no_uv_fbx "${OUTPUT_DIR}/wedge_hard_edge_no_uv.fbx")
nwb_write_no_uv_fbx("${INPUT_FBX}" "${input_no_uv_fbx}")

nwb_run_converter("${INPUT_FBX}" smooth "${smooth_output}")
nwb_run_converter("${INPUT_FBX}" regenerate "${regenerate_output}")
nwb_run_converter("${input_no_uv_fbx}" regenerate "${fallback_output}")
set(fallback_stdout "${run_converter_stdout}")

file(READ "${smooth_output}" smooth_text)
file(READ "${regenerate_output}" regenerate_text)

nwb_extract_asset_block("${smooth_text}" "asset.normals = [" smooth_normals)
nwb_extract_asset_block("${regenerate_text}" "asset.normals = [" regenerate_normals)

nwb_require_import_output("${smooth_text}" "smooth")
nwb_require_import_output("${regenerate_text}" "regenerate")
nwb_require_match("${smooth_normals}" "\\[0, 0\\.707106" "smooth normals did not include the shared-position blended hard-edge normal:\n${smooth_normals}")
nwb_require_no_match("${regenerate_normals}" "0\\.707106" "regenerate normals unexpectedly matched smooth blended normals:\n${regenerate_normals}")
nwb_require_match("${regenerate_normals}" "\\[0, 0, 1\\]" "regenerate normals did not include the first face normal:\n${regenerate_normals}")
nwb_require_match("${regenerate_normals}" "\\[0, 1, 0\\]" "regenerate normals did not include the second face normal:\n${regenerate_normals}")
nwb_require_match("${fallback_stdout}" "tangents: generated_fallback" "missing UV fallback did not report generated_fallback:\n${fallback_stdout}")
nwb_require_match("${fallback_stdout}" "uv0: default" "missing UV fallback did not report default UVs:\n${fallback_stdout}")
