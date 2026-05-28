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

function(extract_asset_list_body text marker out_body)
    string(FIND "${text}" "${marker}" begin)
    if(begin EQUAL -1)
        message(FATAL_ERROR "Missing ${marker}")
    endif()

    string(LENGTH "${marker}" marker_length)
    math(EXPR body_begin "${begin} + ${marker_length}")
    string(SUBSTRING "${text}" ${body_begin} -1 tail)
    string(FIND "${tail}" "];" end)
    if(end EQUAL -1)
        message(FATAL_ERROR "Unterminated ${marker}")
    endif()

    string(SUBSTRING "${tail}" 0 ${end} body)
    set(${out_body} "${body}" PARENT_SCOPE)
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

function(require_nwb_vertex_ref_tangents_in_range text label)
    extract_asset_list_body("${text}" "asset.tangents = [" tangent_body)
    extract_asset_list_body("${text}" "asset.vertex_refs = [" vertex_ref_body)

    string(REGEX MATCHALL "\\[[^]\r\n]+\\]" tangent_entries "${tangent_body}")
    list(LENGTH tangent_entries tangent_count)
    if(tangent_count EQUAL 0)
        message(FATAL_ERROR "${label} output emitted an empty tangent stream")
    endif()

    string(REGEX MATCHALL "\\[[^]\r\n]+\\]" vertex_ref_entries "${vertex_ref_body}")
    list(LENGTH vertex_ref_entries vertex_ref_count)
    if(vertex_ref_count EQUAL 0)
        message(FATAL_ERROR "${label} output emitted no vertex refs")
    endif()

    foreach(vertex_ref_entry IN LISTS vertex_ref_entries)
        if(NOT "${vertex_ref_entry}" MATCHES "^\\[ *[0-9]+ *, *[0-9]+ *, *[0-9]+ *,")
            message(FATAL_ERROR "${label} output emitted malformed vertex_ref: ${vertex_ref_entry}")
        endif()

        string(REGEX REPLACE "^\\[ *[0-9]+ *, *[0-9]+ *, *([0-9]+) *,.*$" "\\1" tangent_index "${vertex_ref_entry}")
        if(tangent_index EQUAL 4294967295 OR tangent_index GREATER_EQUAL tangent_count)
            message(FATAL_ERROR "${label} output emitted out-of-range tangent ref ${tangent_index}: ${vertex_ref_entry}")
        endif()
    endforeach()
endfunction()

function(require_import_output text label)
    require_match("${text}" "asset\\.tangents = \\[" "${label} import did not emit mandatory tangents")
    require_no_match("${text}" "4294967295" "${label} import emitted a missing stream index")
    require_nwb_vertex_ref_tangents_in_range("${text}" "${label}")
endfunction()

function(read_normalized_fbx input_path out_text)
    file(READ "${input_path}" source_fbx)
    string(REPLACE "\r\n" "\n" source_fbx "${source_fbx}")
    string(REPLACE "\r" "\n" source_fbx "${source_fbx}")
    set(${out_text} "${source_fbx}" PARENT_SCOPE)
endfunction()

function(write_no_uv_fbx input_path output_path)
    read_normalized_fbx("${input_path}" source_fbx)

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
