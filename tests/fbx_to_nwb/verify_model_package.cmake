include("${CMAKE_CURRENT_LIST_DIR}/fbx_to_nwb_test_helpers.cmake")

file(MAKE_DIRECTORY "${OUTPUT_DIR}")
set(model_output "${OUTPUT_DIR}/wedge.nwb")
set(mesh_output "${OUTPUT_DIR}/wedge/mesh.nwb")

execute_process(
    COMMAND
        "${FBX_TO_NWB_EXE}"
        "${INPUT_FBX}"
        --output "${model_output}"
        --asset-type model
        --mesh first
        --normal-mode smooth
        --preserve-space
        --yes
        --force
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "fbx_to_nwb model export failed:\n${stdout}\n${stderr}")
endif()

if(NOT EXISTS "${model_output}")
    message(FATAL_ERROR "Model export did not write ${model_output}")
endif()
if(NOT EXISTS "${mesh_output}")
    message(FATAL_ERROR "Model export did not write child mesh ${mesh_output}")
endif()

file(READ "${model_output}" model_text)
file(READ "${mesh_output}" mesh_text)

require_match("${stdout}" "asset_type: model" "model export did not report model asset type:\n${stdout}")
require_match("${model_text}" "model asset;" "model package did not emit model metadata:\n${model_text}")
require_match("${model_text}" "asset\\.static_meshes = \\[" "model package did not emit a static mesh object:\n${model_text}")
require_match("${model_text}" "\"mesh\": \"project/wedge/mesh\"" "model package did not reference the child mesh virtual path:\n${model_text}")
require_no_match("${model_text}" "asset\\.skinned_meshes" "static model package unexpectedly emitted skinned mesh objects:\n${model_text}")
require_import_output("${mesh_text}" "model child mesh")
