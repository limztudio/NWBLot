include("${CMAKE_CURRENT_LIST_DIR}/fbx_to_nwb_test_helpers.cmake")

file(MAKE_DIRECTORY "${OUTPUT_DIR}")
set(bunch_output "${OUTPUT_DIR}/wedge_bunch.nwb")
set(model_output "${OUTPUT_DIR}/wedge.nwb")
set(mesh_output "${OUTPUT_DIR}/wedge/mesh.nwb")

execute_process(
    COMMAND
        "${FBX_TO_NWB_EXE}"
        "${INPUT_FBX}"
        --output "${bunch_output}"
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
    message(FATAL_ERROR "fbx_to_nwb default asset bunch export failed:\n${stdout}\n${stderr}")
endif()

if(NOT EXISTS "${bunch_output}")
    message(FATAL_ERROR "Asset bunch export did not write ${bunch_output}")
endif()

file(READ "${bunch_output}" bunch_text)

require_match("${stdout}" "asset_type: bunch" "default export did not report bunch asset type:\n${stdout}")
require_match("${bunch_text}" "model model;" "asset bunch did not declare a model item:\n${bunch_text}")
require_match("${bunch_text}" "mesh mesh;" "asset bunch did not declare a mesh item:\n${bunch_text}")
require_match("${bunch_text}" "asset_bunch bunch = \\[" "default export did not emit asset bunch metadata:\n${bunch_text}")
require_match("${bunch_text}" "    model," "asset bunch did not include the model variable:\n${bunch_text}")
require_match("${bunch_text}" "    mesh," "asset bunch did not include the mesh variable:\n${bunch_text}")
require_match("${bunch_text}" "model\\.static_meshes = \\{" "asset bunch model did not emit a static mesh map:\n${bunch_text}")
require_match("${bunch_text}" "\"base\": mesh" "asset bunch model did not reference its child mesh variable:\n${bunch_text}")
require_match("${bunch_text}" "mesh\\.tangents = \\[" "asset bunch mesh did not emit mandatory tangents")
require_match("${bunch_text}" "mesh\\.vertex_refs = \\[" "asset bunch mesh did not emit vertex refs")
require_no_match("${bunch_text}" "4294967295" "asset bunch mesh emitted a missing stream index")

execute_process(
    COMMAND
        "${FBX_TO_NWB_EXE}"
        "${INPUT_FBX}"
        --output "${model_output}"
        --asset-type bunch
        --separate-assets
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
    message(FATAL_ERROR "fbx_to_nwb separate model export failed:\n${stdout}\n${stderr}")
endif()

if(NOT EXISTS "${model_output}")
    message(FATAL_ERROR "Model export did not write ${model_output}")
endif()
if(NOT EXISTS "${mesh_output}")
    message(FATAL_ERROR "Model export did not write child mesh ${mesh_output}")
endif()

file(READ "${model_output}" model_text)
file(READ "${mesh_output}" mesh_text)

require_match("${stdout}" "asset_type: bunch" "separate export did not report bunch asset type:\n${stdout}")
require_match("${stdout}" "asset_layout: separate" "separate export did not report separate layout:\n${stdout}")
require_match("${model_text}" "model asset;" "model package did not emit model metadata:\n${model_text}")
require_match("${model_text}" "asset\\.static_meshes = \\{" "model package did not emit a static mesh map:\n${model_text}")
require_match("${model_text}" "\"base\": \"project/wedge/mesh\"" "model package did not reference the child mesh virtual path:\n${model_text}")
require_no_match("${model_text}" "asset\\.skinned_meshes" "static model package unexpectedly emitted skinned mesh objects:\n${model_text}")
require_import_output("${mesh_text}" "model child mesh")
