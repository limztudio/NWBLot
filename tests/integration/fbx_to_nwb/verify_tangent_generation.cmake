include("${CMAKE_CURRENT_LIST_DIR}/fbx_to_nwb_test_helpers.cmake")

function(nwb_write_import_tangent_fbx input_path output_path)
    nwb_read_normalized_fbx("${input_path}" source_fbx)

    set(tangent_layers [=[        LayerElementTangent: 0 {
            Version: 101
            Name: ""
            MappingInformationType: "ByPolygonVertex"
            ReferenceInformationType: "Direct"
            Tangents: *18 {
                a: 1,0,0, 1,0,0, 1,0,0, 0,0,1, 0,0,1, 0,0,1
            }
        }
        LayerElementBinormal: 0 {
            Version: 101
            Name: ""
            MappingInformationType: "ByPolygonVertex"
            ReferenceInformationType: "Direct"
            Binormals: *18 {
                a: 0,1,0, 0,1,0, 0,1,0, 1,0,0, 1,0,0, 1,0,0
            }
        }
]=])
    set(tangent_layer_refs [=[            LayerElement:  {
                Type: "LayerElementTangent"
                TypedIndex: 0
            }
            LayerElement:  {
                Type: "LayerElementBinormal"
                TypedIndex: 0
            }
]=])

    string(REPLACE "        LayerElementUV: 0 {" "${tangent_layers}        LayerElementUV: 0 {" tangent_fbx "${source_fbx}")
    string(REPLACE "            LayerElement:  {\n                Type: \"LayerElementUV\"" "${tangent_layer_refs}            LayerElement:  {\n                Type: \"LayerElementUV\"" tangent_fbx "${tangent_fbx}")
    file(WRITE "${output_path}" "${tangent_fbx}")
endfunction()

function(nwb_write_partial_tangent_fbx input_path output_path)
    nwb_write_import_tangent_fbx("${input_path}" "${output_path}")
    nwb_read_normalized_fbx("${output_path}" partial_fbx)
    string(REPLACE "a: 1,0,0, 1,0,0, 1,0,0, 0,0,1, 0,0,1, 0,0,1" "a: 0,0,0, 1,0,0, 1,0,0, 0,0,1, 0,0,1, 0,0,1" partial_fbx "${partial_fbx}")
    file(WRITE "${output_path}" "${partial_fbx}")
endfunction()

function(nwb_write_scaled_tangent_fbx input_path output_path)
    nwb_write_import_tangent_fbx("${input_path}" "${output_path}")
    nwb_read_normalized_fbx("${output_path}" source_fbx)
    string(REPLACE "a: 1,0,0, 1,0,0, 1,0,0, 0,0,1, 0,0,1, 0,0,1" "a: 1,1,0, 1,1,0, 1,1,0, 0,0,1, 0,0,1, 0,0,1" source_fbx "${source_fbx}")
    string(REPLACE "a: 0,1,0, 0,1,0, 0,1,0, 1,0,0, 1,0,0, 1,0,0" "a: -1,1,0, -1,1,0, -1,1,0, 1,0,0, 1,0,0, 1,0,0" source_fbx "${source_fbx}")
    string(REPLACE "P: \"Lcl Translation\", \"Lcl Translation\", \"\", \"A\",0,0,0" "P: \"Lcl Translation\", \"Lcl Translation\", \"\", \"A\",0,0,0\n            P: \"Lcl Scaling\", \"Lcl Scaling\", \"\", \"A\",2,1,1" source_fbx "${source_fbx}")
    file(WRITE "${output_path}" "${source_fbx}")
endfunction()

function(nwb_write_degenerate_uv_fbx input_path output_path)
    nwb_read_normalized_fbx("${input_path}" source_fbx)
    string(REPLACE "a: 0,0, 1,0, 0,1, 0,0, 1,0, 0,1" "a: 0,0, 0,0, 0,0, 0,0, 0,0, 0,0" degenerate_uv_fbx "${source_fbx}")
    file(WRITE "${output_path}" "${degenerate_uv_fbx}")
endfunction()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")
set(import_tangent_input "${OUTPUT_DIR}/wedge_import_tangent.fbx")
set(partial_tangent_input "${OUTPUT_DIR}/wedge_partial_tangent.fbx")
set(degenerate_uv_input "${OUTPUT_DIR}/wedge_degenerate_uv.fbx")
set(import_tangent_output "${OUTPUT_DIR}/wedge_import_tangent.nwb")
set(generated_uv_output "${OUTPUT_DIR}/wedge_generated_uv.nwb")
set(partial_tangent_output "${OUTPUT_DIR}/wedge_partial_tangent.nwb")
set(fallback_output "${OUTPUT_DIR}/wedge_degenerate_uv.nwb")
set(scaled_tangent_input "${OUTPUT_DIR}/wedge_scaled_tangent.fbx")
set(scaled_tangent_output "${OUTPUT_DIR}/wedge_scaled_tangent.nwb")

nwb_write_import_tangent_fbx("${INPUT_FBX}" "${import_tangent_input}")
nwb_write_partial_tangent_fbx("${INPUT_FBX}" "${partial_tangent_input}")
nwb_write_degenerate_uv_fbx("${INPUT_FBX}" "${degenerate_uv_input}")
nwb_write_scaled_tangent_fbx("${INPUT_FBX}" "${scaled_tangent_input}")

nwb_run_converter("${import_tangent_input}" imported "${import_tangent_output}")
set(import_tangent_stdout "${run_converter_stdout}")
nwb_run_converter("${INPUT_FBX}" imported "${generated_uv_output}")
set(generated_uv_stdout "${run_converter_stdout}")
nwb_run_converter("${partial_tangent_input}" imported "${partial_tangent_output}")
set(partial_tangent_stdout "${run_converter_stdout}")
nwb_run_converter("${degenerate_uv_input}" imported "${fallback_output}")
set(fallback_stdout "${run_converter_stdout}")
nwb_run_converter("${scaled_tangent_input}" imported "${scaled_tangent_output}")
set(scaled_tangent_stdout "${run_converter_stdout}")

file(READ "${import_tangent_output}" import_tangent_text)
file(READ "${generated_uv_output}" generated_uv_text)
file(READ "${partial_tangent_output}" partial_tangent_text)
file(READ "${fallback_output}" fallback_text)
file(READ "${scaled_tangent_output}" scaled_tangent_text)

nwb_require_match("${scaled_tangent_stdout}" "tangents: imported" "scaled input did not retain imported tangents")
nwb_extract_asset_list_body("${scaled_tangent_text}" "asset.tangents = [" scaled_tangent_body)
nwb_require_match("${scaled_tangent_body}" "\\[0[.]8944[0-9]*, 0[.]4472[0-9]*, 0, 1\\]" "nonuniform scaling transformed the tangent incorrectly: ${scaled_tangent_body}")

nwb_require_match("${import_tangent_stdout}" "tangents: imported" "valid imported tangent input did not report imported tangents:\n${import_tangent_stdout}")
nwb_require_match("${generated_uv_stdout}" "tangents: generated_uv" "missing tangent input with UVs did not report generated_uv:\n${generated_uv_stdout}")
nwb_require_match("${partial_tangent_stdout}" "tangents: generated_uv" "partial tangent input did not regenerate UV tangents:\n${partial_tangent_stdout}")
nwb_require_match("${fallback_stdout}" "tangents: generated_fallback" "degenerate UV input did not report generated_fallback:\n${fallback_stdout}")
nwb_require_match("${fallback_stdout}" "tangent_fallback_vertices: [1-9][0-9]*" "degenerate UV input did not report fallback tangent vertices:\n${fallback_stdout}")
nwb_require_match("${fallback_stdout}" "tangent_degenerate_uv_triangles: [1-9][0-9]*" "degenerate UV input did not report degenerate UV triangles:\n${fallback_stdout}")

nwb_require_import_output("${import_tangent_text}" "imported tangent")
nwb_require_import_output("${generated_uv_text}" "generated UV tangent")
nwb_require_import_output("${partial_tangent_text}" "partial tangent")
nwb_require_import_output("${fallback_text}" "fallback tangent")

# Default unit conversion (centimeters to meters) shrinks 1-unit FBX triangles near 1e-8 area^2.
foreach(default_space_mode IN ITEMS imported smooth regenerate)
    set(default_space_output "${OUTPUT_DIR}/wedge_default_${default_space_mode}.nwb")
    execute_process(COMMAND "${FBX_TO_NWB_EXE}" "${INPUT_FBX}" --output "${default_space_output}" --asset-type mesh --normal-mode "${default_space_mode}" --yes --force RESULT_VARIABLE default_result OUTPUT_VARIABLE default_stdout ERROR_VARIABLE default_stderr)
    if(NOT default_result EQUAL 0)
        message(FATAL_ERROR "fbx_to_nwb default-space ${default_space_mode} failed:\n${default_stdout}\n${default_stderr}")
    endif()
    file(READ "${default_space_output}" default_space_text)
    nwb_require_import_output("${default_space_text}" "default-space ${default_space_mode}")
endforeach()

set(tiny_scale_output "${OUTPUT_DIR}/wedge_tiny_scale_zero_eps.nwb")
execute_process(COMMAND "${FBX_TO_NWB_EXE}" "${INPUT_FBX}" --output "${tiny_scale_output}" --asset-type mesh --normal-mode smooth --scale 0.001 --triangle-area-length-squared-epsilon 0 --yes --force RESULT_VARIABLE tiny_result OUTPUT_VARIABLE tiny_stdout ERROR_VARIABLE tiny_stderr)
if(NOT tiny_result EQUAL 0)
    message(FATAL_ERROR "fbx_to_nwb tiny-scale zero-epsilon failed:\n${tiny_stdout}\n${tiny_stderr}")
endif()
file(READ "${tiny_scale_output}" tiny_scale_text)
nwb_require_import_output("${tiny_scale_text}" "tiny-scale zero-epsilon")
