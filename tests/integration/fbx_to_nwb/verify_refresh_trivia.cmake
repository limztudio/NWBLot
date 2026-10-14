cmake_minimum_required(VERSION 3.25)

include("${CMAKE_CURRENT_LIST_DIR}/fbx_to_nwb_test_helpers.cmake")

file(MAKE_DIRECTORY "${OUTPUT_DIR}")
set(mesh_source [=[mesh asset;

@positions_assignment@ [
    [0, 0, 0],
    [1, 0, 0],
    [0, 1, 0],
    // Unmatched ] and " in comments do not terminate the list.
    [0, 0, 0], /* Comment delimiters [ ] ] are not list tokens. */
]@position_expression@;
asset.normals = [[0, 0, 1], [0, 0, 1]];
asset.tangents = [[1, 0, 0, 1]];
asset.uv0 = [[0, 0], [1, 0], [0, 1]];
asset.colors = [[1, 1, 1, 1]];
asset.vertex_refs = [[3, 0, 0, 0, 0], [1, 1, 0, 1, 0], [2, 0, 0, 2, 0]];
asset.indices = [[0, 1, 2]];
@extra_assignment@
]=])
set(decoys [=[// asset.positions = [[99, 99, 99]];
/* asset.normals = [[88, 88, 88]]; */
string note = "asset.positions = [[77, 77, 77]];";
]=])
set(expected_refs [=[asset.vertex_refs = [
    [0, 0, 0, 0, 0],
    [2, 0, 0, 1, 0],
    [1, 0, 0, 2, 0],
];]=])

# Fixture text must be independent of checkout line endings before CRLF is added.
foreach(fixture IN ITEMS mesh_source decoys expected_refs)
    string(REPLACE "\r\n" "\n" ${fixture} "${${fixture}}")
endforeach()

# Comment/string decoys and trivia around punctuation must preserve unrelated bytes and actual geometry.
foreach(case_name IN ITEMS comments trivia)
    set(positions_assignment "asset.positions =")
    if(case_name STREQUAL "trivia")
        set(positions_assignment "asset /* [ ] */ . /* field trivia */ positions /* equals trivia */ = /* list trivia */")
    endif()
    string(REPLACE "@positions_assignment@" "${positions_assignment}" source "${mesh_source}")
    string(REPLACE "@position_expression@" "" source "${source}")
    string(REPLACE "@extra_assignment@" "" source "${source}")
    set(source "${decoys}${source}")
    if(NOT CMAKE_HOST_WIN32)
        string(REPLACE "\n" "\r\n" source "${source}")
    endif()
    set(input_path "${OUTPUT_DIR}/${case_name}.nwb")
    set(output_path "${OUTPUT_DIR}/${case_name}_refreshed.nwb")
    file(WRITE "${input_path}" "${source}")
    execute_process(COMMAND "${FBX_TO_NWB_EXE}" "${input_path}" --output "${output_path}" --refresh-nwb --yes --force RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "fbx_to_nwb refresh rejected valid ${case_name}:\n${stdout}\n${stderr}")
    endif()
    require_crlf_text_file("${output_path}" "${case_name} refresh")
    file(READ "${output_path}" refreshed)
    string(REPLACE "\r\n" "\n" refreshed "${refreshed}")
    string(FIND "${refreshed}" "${decoys}" decoy_offset)
    if(NOT decoy_offset EQUAL 0)
        message(FATAL_ERROR "${case_name} refresh changed a comment or string decoy")
    endif()
    foreach(pattern IN ITEMS "\\[0, 0, 0\\]" "\\[1, 0, 0\\]" "\\[0, 1, 0\\]" "\\[0, 0, 1\\]")
        string(REGEX MATCHALL "${pattern}" entries "${refreshed}")
        list(LENGTH entries count)
        if(NOT count EQUAL 1)
            message(FATAL_ERROR "${case_name} refresh did not canonicalize actual geometry for ${pattern}: ${count}")
        endif()
    endforeach()
    string(FIND "${refreshed}" "${expected_refs}" refs_offset)
    if(refs_offset LESS 0)
        message(FATAL_ERROR "${case_name} refresh did not remap the canonicalized position/normal indices")
    endif()
    set(repeat_path "${OUTPUT_DIR}/${case_name}_repeat.nwb")
    execute_process(COMMAND "${FBX_TO_NWB_EXE}" "${output_path}" --output "${repeat_path}" --refresh-nwb --yes --force RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${case_name} refreshed output was not readable: ${stdout}\n${stderr}")
    endif()
    file(READ "${output_path}" first_bytes HEX)
    file(READ "${repeat_path}" repeated_bytes HEX)
    if(NOT first_bytes STREQUAL repeated_bytes)
        message(FATAL_ERROR "${case_name} refresh was not stable after canonicalization")
    endif()
endforeach()

# Repeated/compound/expression assignments cannot safely map a parsed value back to one literal range.
foreach(case_name IN ITEMS repeated compound expression)
    string(REPLACE "@positions_assignment@" "asset.positions =" source "${mesh_source}")
    set(extra_assignment "")
    set(position_expression "")
    if(case_name STREQUAL "repeated")
        set(extra_assignment "asset.positions = [[0, 0, 0], [1, 0, 0], [0, 1, 0], [0, 0, 0]];")
    elseif(case_name STREQUAL "compound")
        set(extra_assignment "asset.positions += [[0, 0, 0]];")
    else()
        set(position_expression " + [[0, 0, 0]]")
    endif()
    string(REPLACE "@position_expression@" "${position_expression}" source "${source}")
    string(REPLACE "@extra_assignment@" "${extra_assignment}" source "${source}")
    if(NOT CMAKE_HOST_WIN32)
        string(REPLACE "\n" "\r\n" source "${source}")
    endif()
    set(input_path "${OUTPUT_DIR}/${case_name}.nwb")
    set(output_path "${OUTPUT_DIR}/${case_name}_preserved.nwb")
    set(destination_before "Ambiguous refresh must preserve the destination")
    file(WRITE "${input_path}" "${source}")
    file(WRITE "${output_path}" "${destination_before}")
    execute_process(COMMAND "${FBX_TO_NWB_EXE}" "${input_path}" --output "${output_path}" --refresh-nwb --yes --force RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 30)
    if(NOT result EQUAL 1)
        message(FATAL_ERROR "Ambiguous ${case_name} refresh must return exit1, got ${result}:\n${stdout}\n${stderr}")
    endif()
    file(READ "${output_path}" destination_after)
    if(NOT destination_after STREQUAL destination_before)
        message(FATAL_ERROR "Ambiguous ${case_name} refresh replaced the destination")
    endif()
endforeach()
