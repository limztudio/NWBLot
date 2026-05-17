// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Stored vectors are skinned geometry columns; Float44 provides the aligned 4x4 payload layout.
using SkinnedGeometryJointMatrix = Float44;
static_assert(IsStandardLayout_V<SkinnedGeometryJointMatrix>, "SkinnedGeometryJointMatrix must stay GPU-uploadable");
static_assert(IsTriviallyCopyable_V<SkinnedGeometryJointMatrix>, "SkinnedGeometryJointMatrix must stay GPU-uploadable");
static_assert(sizeof(SkinnedGeometryJointMatrix) == sizeof(f32) * 16u, "SkinnedGeometryJointMatrix GPU layout drifted");
static_assert(alignof(SkinnedGeometryJointMatrix) >= alignof(Float4), "SkinnedGeometryJointMatrix must stay SIMD-aligned");

[[nodiscard]] inline SkinnedGeometryJointMatrix MakeIdentitySkinnedGeometryJointMatrix(){
    SkinnedGeometryJointMatrix matrix{};
    matrix.rows[0] = Float4(1.0f, 0.0f, 0.0f, 0.0f);
    matrix.rows[1] = Float4(0.0f, 1.0f, 0.0f, 0.0f);
    matrix.rows[2] = Float4(0.0f, 0.0f, 1.0f, 0.0f);
    matrix.rows[3] = Float4(0.0f, 0.0f, 0.0f, 1.0f);
    return matrix;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

