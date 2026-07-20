#pragma once


#include "../global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Stored vectors are affine matrix rows; the implicit fourth row is (0, 0, 0, 1).
using SkeletonJointMatrix = Float34;
inline constexpr u32 s_SkeletonJointMatrixFloatCount = 12u;
static_assert(IsStandardLayout_V<SkeletonJointMatrix>, "SkeletonJointMatrix must stay GPU-uploadable");
static_assert(IsTriviallyCopyable_V<SkeletonJointMatrix>, "SkeletonJointMatrix must stay GPU-uploadable");
static_assert(sizeof(SkeletonJointMatrix) == sizeof(f32) * s_SkeletonJointMatrixFloatCount, "SkeletonJointMatrix GPU layout drifted");
static_assert(alignof(SkeletonJointMatrix) >= alignof(Float4), "SkeletonJointMatrix must stay SIMD-aligned");

NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

