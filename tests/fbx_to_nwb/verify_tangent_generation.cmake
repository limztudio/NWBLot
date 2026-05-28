include("${CMAKE_CURRENT_LIST_DIR}/fbx_to_nwb_test_helpers.cmake")

function(write_import_tangent_fbx input_path output_path)
    read_normalized_fbx("${input_path}" source_fbx)

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

function(write_partial_tangent_fbx input_path output_path)
    write_import_tangent_fbx("${input_path}" "${output_path}")
    read_normalized_fbx("${output_path}" partial_fbx)
    string(REPLACE
        "a: 1,0,0, 1,0,0, 1,0,0, 0,0,1, 0,0,1, 0,0,1"
        "a: 0,0,0, 1,0,0, 1,0,0, 0,0,1, 0,0,1, 0,0,1"
        partial_fbx
        "${partial_fbx}"
    )
    file(WRITE "${output_path}" "${partial_fbx}")
endfunction()

function(write_degenerate_uv_fbx input_path output_path)
    read_normalized_fbx("${input_path}" source_fbx)
    string(REPLACE
        "a: 0,0, 1,0, 0,1, 0,0, 1,0, 0,1"
        "a: 0,0, 0,0, 0,0, 0,0, 0,0, 0,0"
        degenerate_uv_fbx
        "${source_fbx}"
    )
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

write_import_tangent_fbx("${INPUT_FBX}" "${import_tangent_input}")
write_partial_tangent_fbx("${INPUT_FBX}" "${partial_tangent_input}")
write_degenerate_uv_fbx("${INPUT_FBX}" "${degenerate_uv_input}")

run_converter("${import_tangent_input}" imported "${import_tangent_output}")
set(import_tangent_stdout "${run_converter_stdout}")
run_converter("${INPUT_FBX}" imported "${generated_uv_output}")
set(generated_uv_stdout "${run_converter_stdout}")
run_converter("${partial_tangent_input}" imported "${partial_tangent_output}")
set(partial_tangent_stdout "${run_converter_stdout}")
run_converter("${degenerate_uv_input}" imported "${fallback_output}")
set(fallback_stdout "${run_converter_stdout}")

file(READ "${import_tangent_output}" import_tangent_text)
file(READ "${generated_uv_output}" generated_uv_text)
file(READ "${partial_tangent_output}" partial_tangent_text)
file(READ "${fallback_output}" fallback_text)

require_match("${import_tangent_stdout}" "tangents: imported" "valid imported tangent input did not report imported tangents:\n${import_tangent_stdout}")
require_match("${generated_uv_stdout}" "tangents: generated_uv" "missing tangent input with UVs did not report generated_uv:\n${generated_uv_stdout}")
require_match("${partial_tangent_stdout}" "tangents: generated_uv" "partial tangent input did not regenerate UV tangents:\n${partial_tangent_stdout}")
require_match("${fallback_stdout}" "tangents: generated_fallback" "degenerate UV input did not report generated_fallback:\n${fallback_stdout}")
require_match("${fallback_stdout}" "tangent_fallback_vertices: [1-9][0-9]*" "degenerate UV input did not report fallback tangent vertices:\n${fallback_stdout}")
require_match("${fallback_stdout}" "tangent_degenerate_uv_triangles: [1-9][0-9]*" "degenerate UV input did not report degenerate UV triangles:\n${fallback_stdout}")

require_import_output("${import_tangent_text}" "imported tangent")
require_import_output("${generated_uv_text}" "generated UV tangent")
require_import_output("${partial_tangent_text}" "partial tangent")
require_import_output("${fallback_text}" "fallback tangent")
