function(run_converter input_path normal_mode output_path)
    execute_process(
        COMMAND
            "${FBX_TO_NWB_EXE}"
            "${input_path}"
            --output "${output_path}"
            --asset-type mesh
            --normal-mode "${normal_mode}"
            --preserve-space
            --yes
            --force
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "fbx_to_nwb ${normal_mode} failed:\n${stdout}\n${stderr}")
    endif()
    set(run_converter_stdout "${stdout}" PARENT_SCOPE)
endfunction()

function(extract_asset_block text marker out_block)
    string(FIND "${text}" "${marker}" begin)
    if(begin EQUAL -1)
        message(FATAL_ERROR "Missing ${marker}")
    endif()

    string(SUBSTRING "${text}" ${begin} -1 tail)
    string(FIND "${tail}" "];" end)
    if(end EQUAL -1)
        message(FATAL_ERROR "Unterminated ${marker}")
    endif()

    math(EXPR block_length "${end} + 2")
    string(SUBSTRING "${tail}" 0 ${block_length} block)
    set(${out_block} "${block}" PARENT_SCOPE)
endfunction()

function(require_match text pattern failure_message)
    if(NOT "${text}" MATCHES "${pattern}")
        message(FATAL_ERROR "${failure_message}")
    endif()
endfunction()

function(require_no_match text pattern failure_message)
    if("${text}" MATCHES "${pattern}")
        message(FATAL_ERROR "${failure_message}")
    endif()
endfunction()

function(require_import_output text label)
    require_match("${text}" "asset\\.tangents = \\[" "${label} import did not emit mandatory tangents")
    require_no_match("${text}" "4294967295" "${label} import emitted a missing stream index")
endfunction()

function(write_no_uv_fbx input_path output_path)
    file(READ "${input_path}" source_fbx)
    string(REPLACE "\r\n" "\n" source_fbx "${source_fbx}")
    string(REPLACE "\r" "\n" source_fbx "${source_fbx}")

    set(uv_layer [=[        LayerElementUV: 0 {
            Version: 101
            Name: "UVChannel_1"
            MappingInformationType: "ByPolygonVertex"
            ReferenceInformationType: "Direct"
            UV: *12 {
                a: 0,0, 1,0, 0,1, 0,0, 1,0, 0,1
            }
        }
]=])
    set(uv_layer_ref [=[            LayerElement:  {
                Type: "LayerElementUV"
                TypedIndex: 0
            }
]=])
    string(REPLACE "${uv_layer}" "" no_uv_fbx "${source_fbx}")
    string(REPLACE "${uv_layer_ref}" "" no_uv_fbx "${no_uv_fbx}")
    file(WRITE "${output_path}" "${no_uv_fbx}")
endfunction()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")
set(smooth_output "${OUTPUT_DIR}/wedge_smooth.nwb")
set(regenerate_output "${OUTPUT_DIR}/wedge_regenerate.nwb")
set(fallback_output "${OUTPUT_DIR}/wedge_regenerate_no_uv.nwb")
set(input_no_uv_fbx "${OUTPUT_DIR}/wedge_hard_edge_no_uv.fbx")
write_no_uv_fbx("${INPUT_FBX}" "${input_no_uv_fbx}")

run_converter("${INPUT_FBX}" smooth "${smooth_output}")
run_converter("${INPUT_FBX}" regenerate "${regenerate_output}")
run_converter("${input_no_uv_fbx}" regenerate "${fallback_output}")
set(fallback_stdout "${run_converter_stdout}")

file(READ "${smooth_output}" smooth_text)
file(READ "${regenerate_output}" regenerate_text)

extract_asset_block("${smooth_text}" "asset.normals = [" smooth_normals)
extract_asset_block("${regenerate_text}" "asset.normals = [" regenerate_normals)

require_import_output("${smooth_text}" "smooth")
require_import_output("${regenerate_text}" "regenerate")
require_match(
    "${smooth_normals}"
    "\\[0, 0\\.707106"
    "smooth normals did not include the shared-position blended hard-edge normal:\n${smooth_normals}"
)
require_no_match(
    "${regenerate_normals}"
    "0\\.707106"
    "regenerate normals unexpectedly matched smooth blended normals:\n${regenerate_normals}"
)
require_match("${regenerate_normals}" "\\[0, 0, 1\\]" "regenerate normals did not include the first face normal:\n${regenerate_normals}")
require_match("${regenerate_normals}" "\\[0, 1, 0\\]" "regenerate normals did not include the second face normal:\n${regenerate_normals}")
require_match("${fallback_stdout}" "tangents: generated_fallback" "missing UV fallback did not report generated_fallback:\n${fallback_stdout}")
require_match("${fallback_stdout}" "uv0: default" "missing UV fallback did not report default UVs:\n${fallback_stdout}")
