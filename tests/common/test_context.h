// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/alloc/core.h>
#include <core/alloc/general.h>

#include <tests/common/floating_point_environment_guard.h>

#include <global/global.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_TestArena("tests/common/test_arena");
inline constexpr Name s_DefaultTestArena("tests/common/default_test_arena");
inline constexpr f32 s_DefaultNearlyEqualEpsilon = 0.00001f;
inline constexpr usize s_TestPropertyNumberBufferBytes = 32u;
inline constexpr Array<u32, 3> s_TriangleIndices = { 0u, 1u, 2u };
inline constexpr Array<u32, 6> s_QuadTriangleIndices = { 0u, 1u, 2u, 0u, 2u, 3u };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Tag = void>
struct TestArena{
    Core::Alloc::GlobalArena arena;


public:
    TestArena()
        : arena(s_TestArena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TestDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Core::Alloc::GlobalArena& Arena();


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
using TestVector = DefaultVector<T, Core::Alloc::GlobalArena, TestDetail::Arena>;
using TestAString = DefaultAString<Core::Alloc::GlobalArena, TestDetail::Arena>;

[[nodiscard]] inline b8 NearlyEqual(const f32 lhs, const f32 rhs, const f32 epsilon = s_DefaultNearlyEqualEpsilon)noexcept{
    return Abs(lhs - rhs) <= epsilon;
}

[[nodiscard]] usize CountText(AStringView text, AStringView expected)noexcept;

[[nodiscard]] inline b8 NearlyEqual3(const SIMDVectorSingle value, const f32 x, const f32 y, const f32 z, const f32 epsilon = s_DefaultNearlyEqualEpsilon)noexcept{
    return Vector3LessOrEqual(VectorAbs(VectorSubtract(value, VectorSet(x, y, z, 0.0f))), VectorReplicate(epsilon));
}

[[nodiscard]] inline b8 NearlyEqual3(const Float4& value, const f32 x, const f32 y, const f32 z, const f32 epsilon = s_DefaultNearlyEqualEpsilon)noexcept{
    return NearlyEqual(value.x, x, epsilon) && NearlyEqual(value.y, y, epsilon) && NearlyEqual(value.z, z, epsilon);
}

[[nodiscard]] inline b8 NearlyEqual4(const SIMDVectorSingle value, const f32 x, const f32 y, const f32 z, const f32 w, const f32 epsilon = s_DefaultNearlyEqualEpsilon)noexcept{
    return Vector4LessOrEqual(VectorAbs(VectorSubtract(value, VectorSet(x, y, z, w))), VectorReplicate(epsilon));
}

[[nodiscard]] inline b8 NearlyEqual4(const Float4U& value, const f32 x, const f32 y, const f32 z, const f32 w, const f32 epsilon = s_DefaultNearlyEqualEpsilon)noexcept{
    return NearlyEqual(value.x, x, epsilon) && NearlyEqual(value.y, y, epsilon) && NearlyEqual(value.z, z, epsilon) && NearlyEqual(value.w, w, epsilon);
}

void RecordUnsignedTestProperty(AStringView key, u64 value);

template<typename ArenaT>
[[nodiscard]] inline ::Path<ArenaT> RepoRootOf(ArenaT& arena, const AStringView sourceFile){
    return ::Path<ArenaT>(arena, sourceFile).parentPath().parentPath().parentPath().parentPath().lexicallyNormal();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

