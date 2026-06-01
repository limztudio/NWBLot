// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"
#include <impl/assets/graphics/skinned_mesh/constants.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Stored vectors are skinned mesh columns; Float44 provides the aligned 4x4 payload layout.
using SkinnedMeshJointMatrix = Float44;
static_assert(IsStandardLayout_V<SkinnedMeshJointMatrix>, "SkinnedMeshJointMatrix must stay GPU-uploadable");
static_assert(IsTriviallyCopyable_V<SkinnedMeshJointMatrix>, "SkinnedMeshJointMatrix must stay GPU-uploadable");
static_assert(sizeof(SkinnedMeshJointMatrix) == sizeof(f32) * NWB_SKINNED_MESH_JOINT_MATRIX_FLOAT_COUNT, "SkinnedMeshJointMatrix GPU layout drifted");
static_assert(alignof(SkinnedMeshJointMatrix) >= alignof(Float4), "SkinnedMeshJointMatrix must stay SIMD-aligned");

[[nodiscard]] inline SkinnedMeshJointMatrix MakeIdentitySkinnedMeshJointMatrix(){
    SkinnedMeshJointMatrix matrix{};
    matrix.rows[0] = Float4(1.0f, 0.0f, 0.0f, 0.0f);
    matrix.rows[1] = Float4(0.0f, 1.0f, 0.0f, 0.0f);
    matrix.rows[2] = Float4(0.0f, 0.0f, 1.0f, 0.0f);
    matrix.rows[3] = Float4(0.0f, 0.0f, 0.0f, 1.0f);
    return matrix;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

