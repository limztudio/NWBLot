
#pragma once


#include <core/alloc/core.h>
#include <core/alloc/general.h>

#include <global/global.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_TestArena("tests/test_arena");
inline constexpr Name s_DefaultTestArena("tests/default_test_arena");
inline constexpr f32 s_DefaultNearlyEqualEpsilon = 0.00001f;

template<typename Tag = void>
struct TestArena{
    Core::Alloc::GlobalArena arena;

    TestArena()
        : arena(s_TestArena)
    {}
};

namespace TestDetail{

inline Core::Alloc::GlobalArena& Arena(){
    static Core::Alloc::GlobalArena s_Arena(s_DefaultTestArena);
    return s_Arena;
}

template<typename T>
using Allocator = ContainerDetail::DefaultArenaAllocatorFor_T<T, Core::Alloc::GlobalArena, Arena>;

};

template<typename T>
using TestVector = std::vector<T, TestDetail::Allocator<T>>;
using TestAString = std::basic_string<char, std::char_traits<char>, TestDetail::Allocator<char>>;

[[nodiscard]] inline bool NearlyEqual(const f32 lhs, const f32 rhs, const f32 epsilon = s_DefaultNearlyEqualEpsilon){
    return Abs(lhs - rhs) <= epsilon;
}

[[nodiscard]] inline bool NearlyEqual3(const SIMDVector value, const f32 x, const f32 y, const f32 z, const f32 epsilon = s_DefaultNearlyEqualEpsilon){
    return NearlyEqual(VectorGetX(value), x, epsilon) && NearlyEqual(VectorGetY(value), y, epsilon) && NearlyEqual(VectorGetZ(value), z, epsilon);
}

[[nodiscard]] inline bool NearlyEqual4(const SIMDVector value, const f32 x, const f32 y, const f32 z, const f32 w, const f32 epsilon = s_DefaultNearlyEqualEpsilon){
    return
        NearlyEqual(VectorGetX(value), x, epsilon)
        && NearlyEqual(VectorGetY(value), y, epsilon)
        && NearlyEqual(VectorGetZ(value), z, epsilon)
        && NearlyEqual(VectorGetW(value), w, epsilon)
    ;
}

[[nodiscard]] inline bool NearlyEqual3(const Float4& value, const f32 x, const f32 y, const f32 z, const f32 epsilon = s_DefaultNearlyEqualEpsilon){
    return NearlyEqual(value.x, x, epsilon) && NearlyEqual(value.y, y, epsilon) && NearlyEqual(value.z, z, epsilon);
}

[[nodiscard]] inline bool NearlyEqual4(const Float4U& value, const f32 x, const f32 y, const f32 z, const f32 w, const f32 epsilon = s_DefaultNearlyEqualEpsilon){
    return NearlyEqual(value.x, x, epsilon) && NearlyEqual(value.y, y, epsilon) && NearlyEqual(value.z, z, epsilon) && NearlyEqual(value.w, w, epsilon);
}

inline TestVector<u32> MakeTriangleIndices(){
    TestVector<u32> indices;
    indices.push_back(0u);
    indices.push_back(1u);
    indices.push_back(2u);
    return indices;
}

inline TestVector<u32> MakeQuadTriangleIndices(){
    TestVector<u32> indices = MakeTriangleIndices();
    indices.push_back(0u);
    indices.push_back(2u);
    indices.push_back(3u);
    return indices;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

