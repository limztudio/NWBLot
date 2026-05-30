// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../type.h"
#include "../hash_utils.h"
#include "macro.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MathTypeDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Value, usize Count>
[[nodiscard]] inline bool EqualArray(const Value& lhs, const Value& rhs)noexcept{
    for(usize i = 0u; i < Count; ++i){
        if(lhs.raw[i] != rhs.raw[i])
            return false;
    }
    return true;
}

template<typename Value, usize Count>
[[nodiscard]] inline usize HashArray(const Value& value)noexcept{
    usize seed = 0u;
    for(usize i = 0u; i < Count; ++i)
        HashCombine(seed, value.raw[i]);
    return seed;
}

template<typename Value, usize Count>
[[nodiscard]] inline usize HashFloatArray(const Value& value)noexcept{
    usize seed = 0u;
    for(usize i = 0u; i < Count; ++i)
        ::HashCombineFloat(seed, value.raw[i]);
    return seed;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using Half = u16;
using Float16 = Half;
static_assert(sizeof(Half) == 2u, "Half must stay a 16-bit IEEE 754 binary16 storage type");
static_assert(alignof(Half) == alignof(u16), "Half storage alignment must match u16");

struct Half2U{
    union{
        struct{
            Half x;
            Half y;
        };
        Half raw[2];
        u32 packed;
    };

    constexpr Half2U()noexcept
        : packed(0u)
    {
    }
    constexpr Half2U(const Half xValue, const Half yValue)noexcept
        : x(xValue), y(yValue)
    {
    }
    explicit constexpr Half2U(const u32 packedValue)noexcept
        : packed(packedValue)
    {
    }
};
static_assert(sizeof(Half2U) == sizeof(u32), "Half2U must stay a packed 32-bit payload");
static_assert(alignof(Half2U) == alignof(u32), "Half2U must stay word-aligned");
static_assert(IsStandardLayout_V<Half2U>, "Half2U must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<Half2U>, "Half2U must stay binary-serializable");

struct Half4U{
    union{
        struct{
            Half x;
            Half y;
            Half z;
            Half w;
        };
        Half raw[4];
        Half2U pair[2];
        u32 packed[2];
    };

    constexpr Half4U()noexcept
        : packed{0u, 0u}
    {
    }
    constexpr Half4U(const Half xValue, const Half yValue, const Half zValue, const Half wValue)noexcept
        : x(xValue), y(yValue), z(zValue), w(wValue)
    {
    }
    constexpr Half4U(const Half2U xyValue, const Half2U zwValue)noexcept
        : pair{xyValue, zwValue}
    {
    }
};
static_assert(sizeof(Half4U) == sizeof(u32) * 2u, "Half4U must stay a packed 64-bit payload");
static_assert(alignof(Half4U) == alignof(u32), "Half4U must stay word-aligned");
static_assert(IsStandardLayout_V<Half4U>, "Half4U must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<Half4U>, "Half4U must stay binary-serializable");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct alignas(16) Float4{
    union{
        struct{
            f32 x;
            f32 y;
            f32 z;
            f32 w;
    };
        struct{
            f32 r;
            f32 g;
            f32 b;
            f32 a;
        };
        f32 raw[4];
    };

    constexpr Float4()noexcept
        : x(0.0f), y(0.0f), z(0.0f), w(0.0f)
    {
    }
    constexpr Float4(const f32 xValue, const f32 yValue, const f32 zValue)noexcept
        : x(xValue), y(yValue), z(zValue), w(0.0f)
    {
    }
    constexpr Float4(const f32 xValue, const f32 yValue, const f32 zValue, const f32 wValue)noexcept
        : x(xValue), y(yValue), z(zValue), w(wValue)
    {
    }
    explicit constexpr Float4(const f32* pArray)noexcept
        : x(pArray[0]), y(pArray[1]), z(pArray[2]), w(pArray[3])
    {
    }
};

struct alignas(16) Float34{
    union{
        struct{
            f32 _11, _12, _13, _14;
            f32 _21, _22, _23, _24;
            f32 _31, _32, _33, _34;
        };
        Float4 rows[3];
        f32 m[3][4];
        f32 raw[12];
    };
};

struct alignas(16) Float44{
    union{
        struct{
            f32 _11, _12, _13, _14;
            f32 _21, _22, _23, _24;
            f32 _31, _32, _33, _34;
            f32 _41, _42, _43, _44;
        };
        Float4 rows[4];
        f32 m[4][4];
        f32 raw[16];
    };
};

struct alignas(16) Int4{
    union{
        struct{
            i32 x;
            i32 y;
            i32 z;
            i32 w;
        };
        struct{
            i32 r;
            i32 g;
            i32 b;
            i32 a;
        };
        i32 raw[4];
    };
};

struct alignas(16) UInt4{
    union{
        struct{
            u32 x;
            u32 y;
            u32 z;
            u32 w;
        };
        struct{
            u32 r;
            u32 g;
            u32 b;
            u32 a;
        };
        u32 raw[4];
    };
};

struct alignas(16) Float3Int{
    union{
        struct{
            f32 x;
            f32 y;
            f32 z;
            i32 w;
        };
        struct{
            f32 r;
            f32 g;
            f32 b;
            i32 a;
        };
    };

    constexpr Float3Int()noexcept
        : x(0.0f), y(0.0f), z(0.0f), w(0)
    {
    }
    constexpr Float3Int(const f32 xValue, const f32 yValue, const f32 zValue, const i32 wValue)noexcept
        : x(xValue), y(yValue), z(zValue), w(wValue)
    {
    }
};
static_assert(sizeof(Float3Int) == sizeof(f32) * 4u, "Float3Int must stay a packed 16-byte payload");
static_assert(alignof(Float3Int) == alignof(Float4), "Float3Int must stay SIMD-aligned");
static_assert(IsStandardLayout_V<Float3Int>, "Float3Int must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<Float3Int>, "Float3Int must stay binary-serializable");

struct alignas(16) Float3UInt{
    union{
        struct{
            f32 x;
            f32 y;
            f32 z;
            u32 w;
        };
        struct{
            f32 r;
            f32 g;
            f32 b;
            u32 a;
        };
    };

    constexpr Float3UInt()noexcept
        : x(0.0f), y(0.0f), z(0.0f), w(0u)
    {
    }
    constexpr Float3UInt(const f32 xValue, const f32 yValue, const f32 zValue, const u32 wValue)noexcept
        : x(xValue), y(yValue), z(zValue), w(wValue)
    {
    }
};
static_assert(sizeof(Float3UInt) == sizeof(f32) * 4u, "Float3UInt must stay a packed 16-byte payload");
static_assert(alignof(Float3UInt) == alignof(Float4), "Float3UInt must stay SIMD-aligned");
static_assert(IsStandardLayout_V<Float3UInt>, "Float3UInt must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<Float3UInt>, "Float3UInt must stay binary-serializable");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// unaligned


struct Float2U{
    union{
        struct{
            f32 x;
            f32 y;
        };
        struct{
            f32 r;
            f32 g;
        };
        f32 raw[2];
    };

    constexpr Float2U()noexcept
        : x(0.0f), y(0.0f)
    {
    }
    constexpr Float2U(const f32 xValue, const f32 yValue)noexcept
        : x(xValue), y(yValue)
    {
    }
    explicit constexpr Float2U(const f32* pArray)noexcept
        : x(pArray[0]), y(pArray[1])
    {
    }
};

struct Float3U{
    union{
        struct{
            f32 x;
            f32 y;
            f32 z;
        };
        struct{
            f32 r;
            f32 g;
            f32 b;
        };
        f32 raw[3];
    };

    constexpr Float3U()noexcept
        : x(0.0f), y(0.0f), z(0.0f)
    {
    }
    constexpr Float3U(const f32 xValue, const f32 yValue, const f32 zValue)noexcept
        : x(xValue), y(yValue), z(zValue)
    {
    }
    explicit constexpr Float3U(const f32* pArray)noexcept
        : x(pArray[0]), y(pArray[1]), z(pArray[2])
    {
    }
};

struct Float4U{
    union{
        struct{
            f32 x;
            f32 y;
            f32 z;
            f32 w;
        };
        struct{
            f32 r;
            f32 g;
            f32 b;
            f32 a;
        };
        f32 raw[4];
    };

    constexpr Float4U()noexcept
        : x(0.0f), y(0.0f), z(0.0f), w(0.0f)
    {
    }
    constexpr Float4U(const f32 xValue, const f32 yValue, const f32 zValue, const f32 wValue)noexcept
        : x(xValue), y(yValue), z(zValue), w(wValue)
    {
    }
    explicit constexpr Float4U(const f32* pArray)noexcept
        : x(pArray[0]), y(pArray[1]), z(pArray[2]), w(pArray[3])
    {
    }
};

struct Float33U{
    union{
        struct{
            f32 _11, _12, _13;
            f32 _21, _22, _23;
            f32 _31, _32, _33;
        };
        Float3U rows[3];
        f32 m[3][3];
        f32 raw[9];
    };
};

struct Float34U{
    union{
        struct{
            f32 _11, _12, _13, _14;
            f32 _21, _22, _23, _24;
            f32 _31, _32, _33, _34;
        };
        Float4U rows[3];
        f32 m[3][4];
        f32 raw[12];
    };
};

struct Float44U{
    union{
        struct{
            f32 _11, _12, _13, _14;
            f32 _21, _22, _23, _24;
            f32 _31, _32, _33, _34;
            f32 _41, _42, _43, _44;
        };
        Float4U rows[4];
        f32 m[4][4];
        f32 raw[16];
    };
};

struct Int2U{
    union{
        struct{
            i32 x;
            i32 y;
        };
        struct{
            i32 r;
            i32 g;
        };
        i32 raw[2];
    };
};
struct Int3U{
    union{
        struct{
            i32 x;
            i32 y;
            i32 z;
        };
        struct{
            i32 r;
            i32 g;
            i32 b;
        };
        i32 raw[3];
    };
};
struct Int4U{
    union{
        struct{
            i32 x;
            i32 y;
            i32 z;
            i32 w;
        };
        struct{
            i32 r;
            i32 g;
            i32 b;
            i32 a;
        };
        i32 raw[4];
    };
};

struct UInt2U{
    union{
        struct{
            u32 x;
            u32 y;
        };
        struct{
            u32 r;
            u32 g;
        };
        u32 raw[2];
    };
};
struct UInt3U{
    union{
        struct{
            u32 x;
            u32 y;
            u32 z;
        };
        struct{
            u32 r;
            u32 g;
            u32 b;
        };
        u32 raw[3];
    };
};
struct UInt4U{
    union{
        struct{
            u32 x;
            u32 y;
            u32 z;
            u32 w;
        };
        struct{
            u32 r;
            u32 g;
            u32 b;
            u32 a;
        };
        u32 raw[4];
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool operator==(const Half2U& lhs, const Half2U& rhs)noexcept{ return MathTypeDetail::EqualArray<Half2U, 2u>(lhs, rhs); }
[[nodiscard]] inline bool operator!=(const Half2U& lhs, const Half2U& rhs)noexcept{ return !(lhs == rhs); }

[[nodiscard]] inline bool operator==(const Half4U& lhs, const Half4U& rhs)noexcept{ return MathTypeDetail::EqualArray<Half4U, 4u>(lhs, rhs); }
[[nodiscard]] inline bool operator!=(const Half4U& lhs, const Half4U& rhs)noexcept{ return !(lhs == rhs); }

[[nodiscard]] inline bool operator==(const Float4& lhs, const Float4& rhs)noexcept{ return MathTypeDetail::EqualArray<Float4, 4u>(lhs, rhs); }
[[nodiscard]] inline bool operator!=(const Float4& lhs, const Float4& rhs)noexcept{ return !(lhs == rhs); }

[[nodiscard]] inline bool operator==(const Float34& lhs, const Float34& rhs)noexcept{ return MathTypeDetail::EqualArray<Float34, 12u>(lhs, rhs); }
[[nodiscard]] inline bool operator!=(const Float34& lhs, const Float34& rhs)noexcept{ return !(lhs == rhs); }

[[nodiscard]] inline bool operator==(const Float44& lhs, const Float44& rhs)noexcept{ return MathTypeDetail::EqualArray<Float44, 16u>(lhs, rhs); }
[[nodiscard]] inline bool operator!=(const Float44& lhs, const Float44& rhs)noexcept{ return !(lhs == rhs); }

[[nodiscard]] inline bool operator==(const Int4& lhs, const Int4& rhs)noexcept{ return MathTypeDetail::EqualArray<Int4, 4u>(lhs, rhs); }
[[nodiscard]] inline bool operator!=(const Int4& lhs, const Int4& rhs)noexcept{ return !(lhs == rhs); }

[[nodiscard]] inline bool operator==(const UInt4& lhs, const UInt4& rhs)noexcept{ return MathTypeDetail::EqualArray<UInt4, 4u>(lhs, rhs); }
[[nodiscard]] inline bool operator!=(const UInt4& lhs, const UInt4& rhs)noexcept{ return !(lhs == rhs); }

[[nodiscard]] inline bool operator==(const Float3Int& lhs, const Float3Int& rhs)noexcept{
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.w == rhs.w;
}
[[nodiscard]] inline bool operator!=(const Float3Int& lhs, const Float3Int& rhs)noexcept{ return !(lhs == rhs); }

[[nodiscard]] inline bool operator==(const Float3UInt& lhs, const Float3UInt& rhs)noexcept{
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.w == rhs.w;
}
[[nodiscard]] inline bool operator!=(const Float3UInt& lhs, const Float3UInt& rhs)noexcept{ return !(lhs == rhs); }

[[nodiscard]] inline bool operator==(const Float2U& lhs, const Float2U& rhs)noexcept{ return MathTypeDetail::EqualArray<Float2U, 2u>(lhs, rhs); }
[[nodiscard]] inline bool operator!=(const Float2U& lhs, const Float2U& rhs)noexcept{ return !(lhs == rhs); }

[[nodiscard]] inline bool operator==(const Float3U& lhs, const Float3U& rhs)noexcept{ return MathTypeDetail::EqualArray<Float3U, 3u>(lhs, rhs); }
[[nodiscard]] inline bool operator!=(const Float3U& lhs, const Float3U& rhs)noexcept{ return !(lhs == rhs); }

[[nodiscard]] inline bool operator==(const Float4U& lhs, const Float4U& rhs)noexcept{ return MathTypeDetail::EqualArray<Float4U, 4u>(lhs, rhs); }
[[nodiscard]] inline bool operator!=(const Float4U& lhs, const Float4U& rhs)noexcept{ return !(lhs == rhs); }

[[nodiscard]] inline bool operator==(const Float33U& lhs, const Float33U& rhs)noexcept{ return MathTypeDetail::EqualArray<Float33U, 9u>(lhs, rhs); }
[[nodiscard]] inline bool operator!=(const Float33U& lhs, const Float33U& rhs)noexcept{ return !(lhs == rhs); }

[[nodiscard]] inline bool operator==(const Float34U& lhs, const Float34U& rhs)noexcept{ return MathTypeDetail::EqualArray<Float34U, 12u>(lhs, rhs); }
[[nodiscard]] inline bool operator!=(const Float34U& lhs, const Float34U& rhs)noexcept{ return !(lhs == rhs); }

[[nodiscard]] inline bool operator==(const Float44U& lhs, const Float44U& rhs)noexcept{ return MathTypeDetail::EqualArray<Float44U, 16u>(lhs, rhs); }
[[nodiscard]] inline bool operator!=(const Float44U& lhs, const Float44U& rhs)noexcept{ return !(lhs == rhs); }

[[nodiscard]] inline bool operator==(const Int2U& lhs, const Int2U& rhs)noexcept{ return MathTypeDetail::EqualArray<Int2U, 2u>(lhs, rhs); }
[[nodiscard]] inline bool operator!=(const Int2U& lhs, const Int2U& rhs)noexcept{ return !(lhs == rhs); }

[[nodiscard]] inline bool operator==(const Int3U& lhs, const Int3U& rhs)noexcept{ return MathTypeDetail::EqualArray<Int3U, 3u>(lhs, rhs); }
[[nodiscard]] inline bool operator!=(const Int3U& lhs, const Int3U& rhs)noexcept{ return !(lhs == rhs); }

[[nodiscard]] inline bool operator==(const Int4U& lhs, const Int4U& rhs)noexcept{ return MathTypeDetail::EqualArray<Int4U, 4u>(lhs, rhs); }
[[nodiscard]] inline bool operator!=(const Int4U& lhs, const Int4U& rhs)noexcept{ return !(lhs == rhs); }

[[nodiscard]] inline bool operator==(const UInt2U& lhs, const UInt2U& rhs)noexcept{ return MathTypeDetail::EqualArray<UInt2U, 2u>(lhs, rhs); }
[[nodiscard]] inline bool operator!=(const UInt2U& lhs, const UInt2U& rhs)noexcept{ return !(lhs == rhs); }

[[nodiscard]] inline bool operator==(const UInt3U& lhs, const UInt3U& rhs)noexcept{ return MathTypeDetail::EqualArray<UInt3U, 3u>(lhs, rhs); }
[[nodiscard]] inline bool operator!=(const UInt3U& lhs, const UInt3U& rhs)noexcept{ return !(lhs == rhs); }

[[nodiscard]] inline bool operator==(const UInt4U& lhs, const UInt4U& rhs)noexcept{ return MathTypeDetail::EqualArray<UInt4U, 4u>(lhs, rhs); }
[[nodiscard]] inline bool operator!=(const UInt4U& lhs, const UInt4U& rhs)noexcept{ return !(lhs == rhs); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace std{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<>
struct hash<Half2U>{
    usize operator()(const Half2U& value)const noexcept{ return MathTypeDetail::HashArray<Half2U, 2u>(value); }
};

template<>
struct hash<Half4U>{
    usize operator()(const Half4U& value)const noexcept{ return MathTypeDetail::HashArray<Half4U, 4u>(value); }
};

template<>
struct hash<Float4>{
    usize operator()(const Float4& value)const noexcept{ return MathTypeDetail::HashFloatArray<Float4, 4u>(value); }
};

template<>
struct hash<Float34>{
    usize operator()(const Float34& value)const noexcept{ return MathTypeDetail::HashFloatArray<Float34, 12u>(value); }
};

template<>
struct hash<Float44>{
    usize operator()(const Float44& value)const noexcept{ return MathTypeDetail::HashFloatArray<Float44, 16u>(value); }
};

template<>
struct hash<Int4>{
    usize operator()(const Int4& value)const noexcept{ return MathTypeDetail::HashArray<Int4, 4u>(value); }
};

template<>
struct hash<UInt4>{
    usize operator()(const UInt4& value)const noexcept{ return MathTypeDetail::HashArray<UInt4, 4u>(value); }
};

template<>
struct hash<Float3Int>{
    usize operator()(const Float3Int& value)const noexcept{
        usize seed = 0u;
        ::HashCombineFloat(seed, value.x);
        ::HashCombineFloat(seed, value.y);
        ::HashCombineFloat(seed, value.z);
        ::HashCombine(seed, value.w);
        return seed;
    }
};

template<>
struct hash<Float3UInt>{
    usize operator()(const Float3UInt& value)const noexcept{
        usize seed = 0u;
        ::HashCombineFloat(seed, value.x);
        ::HashCombineFloat(seed, value.y);
        ::HashCombineFloat(seed, value.z);
        ::HashCombine(seed, value.w);
        return seed;
    }
};

template<>
struct hash<Float2U>{
    usize operator()(const Float2U& value)const noexcept{ return MathTypeDetail::HashFloatArray<Float2U, 2u>(value); }
};

template<>
struct hash<Float3U>{
    usize operator()(const Float3U& value)const noexcept{ return MathTypeDetail::HashFloatArray<Float3U, 3u>(value); }
};

template<>
struct hash<Float4U>{
    usize operator()(const Float4U& value)const noexcept{ return MathTypeDetail::HashFloatArray<Float4U, 4u>(value); }
};

template<>
struct hash<Float33U>{
    usize operator()(const Float33U& value)const noexcept{ return MathTypeDetail::HashFloatArray<Float33U, 9u>(value); }
};

template<>
struct hash<Float34U>{
    usize operator()(const Float34U& value)const noexcept{ return MathTypeDetail::HashFloatArray<Float34U, 12u>(value); }
};

template<>
struct hash<Float44U>{
    usize operator()(const Float44U& value)const noexcept{ return MathTypeDetail::HashFloatArray<Float44U, 16u>(value); }
};

template<>
struct hash<Int2U>{
    usize operator()(const Int2U& value)const noexcept{ return MathTypeDetail::HashArray<Int2U, 2u>(value); }
};

template<>
struct hash<Int3U>{
    usize operator()(const Int3U& value)const noexcept{ return MathTypeDetail::HashArray<Int3U, 3u>(value); }
};

template<>
struct hash<Int4U>{
    usize operator()(const Int4U& value)const noexcept{ return MathTypeDetail::HashArray<Int4U, 4u>(value); }
};

template<>
struct hash<UInt2U>{
    usize operator()(const UInt2U& value)const noexcept{ return MathTypeDetail::HashArray<UInt2U, 2u>(value); }
};

template<>
struct hash<UInt3U>{
    usize operator()(const UInt3U& value)const noexcept{ return MathTypeDetail::HashArray<UInt3U, 3u>(value); }
};

template<>
struct hash<UInt4U>{
    usize operator()(const UInt4U& value)const noexcept{ return MathTypeDetail::HashArray<UInt4U, 4u>(value); }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_HAS_SCALAR)
struct FPUVector4{
    union{
        f32 f[4];
        u32 u[4];
    };
};
#endif

#if defined(NWB_HAS_NEON)
using SIMDVector = float32x4_t;
#elif defined(NWB_HAS_SCALAR)
using SIMDVector = FPUVector4;
#else
using SIMDVector = __m128;
#endif

#if defined(NWB_HAS_SCALAR)
struct SIMDMatrix
#else
struct alignas(16) SIMDMatrix
#endif
{
#if defined(NWB_HAS_SCALAR)
    union{
        struct{
            f32 _11, _12, _13, _14;
            f32 _21, _22, _23, _24;
            f32 _31, _32, _33, _34;
            f32 _41, _42, _43, _44;
        };
        SIMDVector v[4];
        f32 m[4][4];
    };
#else
    SIMDVector v[4];
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

