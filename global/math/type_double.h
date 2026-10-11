// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MathTypeDoubleDetail{


template<typename Value, usize Count>
[[nodiscard]] NWB_INLINE usize HashDoubleArray(const Value& value)noexcept{
    usize seed = 0u;
    for(usize i = 0u; i < Count; ++i)
        HashCombineDouble(seed, value.raw[i]);
    return seed;
}


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct alignas(32) Double4{
    union{
        struct{
            f64 x;
            f64 y;
            f64 z;
            f64 w;
        };
        struct{
            f64 r;
            f64 g;
            f64 b;
            f64 a;
        };
        f64 raw[4];
    };

    constexpr Double4()noexcept
        : x(0.0), y(0.0), z(0.0), w(0.0)
    {
    }
    constexpr Double4(const f64 xValue, const f64 yValue, const f64 zValue)noexcept
        : x(xValue), y(yValue), z(zValue), w(0.0)
    {
    }
    constexpr Double4(const f64 xValue, const f64 yValue, const f64 zValue, const f64 wValue)noexcept
        : x(xValue), y(yValue), z(zValue), w(wValue)
    {
    }
    explicit constexpr Double4(const f64* pArray)noexcept
        : x(pArray[0]), y(pArray[1]), z(pArray[2]), w(pArray[3])
    {
    }
};

struct alignas(32) Double34{
    union{
        struct{
            f64 _11, _12, _13, _14;
            f64 _21, _22, _23, _24;
            f64 _31, _32, _33, _34;
        };
        Double4 rows[3];
        f64 m[3][4];
        f64 raw[12];
    };
};

[[nodiscard]] constexpr Double34 Double34Identity()noexcept{
    Double34 matrix = {};
    matrix.rows[0] = Double4(1.0, 0.0, 0.0, 0.0);
    matrix.rows[1] = Double4(0.0, 1.0, 0.0, 0.0);
    matrix.rows[2] = Double4(0.0, 0.0, 1.0, 0.0);
    return matrix;
}

struct alignas(32) Double44{
    union{
        struct{
            f64 _11, _12, _13, _14;
            f64 _21, _22, _23, _24;
            f64 _31, _32, _33, _34;
            f64 _41, _42, _43, _44;
        };
        Double4 rows[4];
        f64 m[4][4];
        f64 raw[16];
    };
};

struct alignas(32) Int64x4{
    union{
        struct{
            i64 x;
            i64 y;
            i64 z;
            i64 w;
        };
        struct{
            i64 r;
            i64 g;
            i64 b;
            i64 a;
        };
        i64 raw[4];
    };
};

struct alignas(32) UInt64x4{
    union{
        struct{
            u64 x;
            u64 y;
            u64 z;
            u64 w;
        };
        struct{
            u64 r;
            u64 g;
            u64 b;
            u64 a;
        };
        u64 raw[4];
    };
};

struct alignas(32) Double3Int{
    union{
        struct{
            f64 x;
            f64 y;
            f64 z;
            i64 w;
        };
        struct{
            f64 r;
            f64 g;
            f64 b;
            i64 a;
        };
    };

    constexpr Double3Int()noexcept
        : x(0.0), y(0.0), z(0.0), w(0)
    {
    }
    constexpr Double3Int(const f64 xValue, const f64 yValue, const f64 zValue, const i64 wValue)noexcept
        : x(xValue), y(yValue), z(zValue), w(wValue)
    {
    }
};
static_assert(sizeof(Double3Int) == sizeof(f64) * 4u, "Double3Int must stay a packed 32-byte payload");
static_assert(alignof(Double3Int) == alignof(Double4), "Double3Int must stay SIMD-aligned");
static_assert(IsStandardLayout_V<Double3Int>, "Double3Int must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<Double3Int>, "Double3Int must stay binary-serializable");

struct alignas(32) Double3UInt{
    union{
        struct{
            f64 x;
            f64 y;
            f64 z;
            u64 w;
        };
        struct{
            f64 r;
            f64 g;
            f64 b;
            u64 a;
        };
    };

    constexpr Double3UInt()noexcept
        : x(0.0), y(0.0), z(0.0), w(0u)
    {
    }
    constexpr Double3UInt(const f64 xValue, const f64 yValue, const f64 zValue, const u64 wValue)noexcept
        : x(xValue), y(yValue), z(zValue), w(wValue)
    {
    }
};
static_assert(sizeof(Double3UInt) == sizeof(f64) * 4u, "Double3UInt must stay a packed 32-byte payload");
static_assert(alignof(Double3UInt) == alignof(Double4), "Double3UInt must stay SIMD-aligned");
static_assert(IsStandardLayout_V<Double3UInt>, "Double3UInt must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<Double3UInt>, "Double3UInt must stay binary-serializable");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Double2U{
    union{
        struct{
            f64 x;
            f64 y;
        };
        struct{
            f64 r;
            f64 g;
        };
        f64 raw[2];
    };

    constexpr Double2U()noexcept
        : x(0.0), y(0.0)
    {
    }
    constexpr Double2U(const f64 xValue, const f64 yValue)noexcept
        : x(xValue), y(yValue)
    {
    }
    explicit constexpr Double2U(const f64* pArray)noexcept
        : x(pArray[0]), y(pArray[1])
    {
    }
};

struct Double3U{
    union{
        struct{
            f64 x;
            f64 y;
            f64 z;
        };
        struct{
            f64 r;
            f64 g;
            f64 b;
        };
        f64 raw[3];
    };

    constexpr Double3U()noexcept
        : x(0.0), y(0.0), z(0.0)
    {
    }
    constexpr Double3U(const f64 xValue, const f64 yValue, const f64 zValue)noexcept
        : x(xValue), y(yValue), z(zValue)
    {
    }
    explicit constexpr Double3U(const f64* pArray)noexcept
        : x(pArray[0]), y(pArray[1]), z(pArray[2])
    {
    }
};

struct Double4U{
    union{
        struct{
            f64 x;
            f64 y;
            f64 z;
            f64 w;
        };
        struct{
            f64 r;
            f64 g;
            f64 b;
            f64 a;
        };
        f64 raw[4];
    };

    constexpr Double4U()noexcept
        : x(0.0), y(0.0), z(0.0), w(0.0)
    {
    }
    constexpr Double4U(const f64 xValue, const f64 yValue, const f64 zValue, const f64 wValue)noexcept
        : x(xValue), y(yValue), z(zValue), w(wValue)
    {
    }
    explicit constexpr Double4U(const f64* pArray)noexcept
        : x(pArray[0]), y(pArray[1]), z(pArray[2]), w(pArray[3])
    {
    }
};

struct Double33U{
    union{
        struct{
            f64 _11, _12, _13;
            f64 _21, _22, _23;
            f64 _31, _32, _33;
        };
        Double3U rows[3];
        f64 m[3][3];
        f64 raw[9];
    };
};

struct Double34U{
    union{
        struct{
            f64 _11, _12, _13, _14;
            f64 _21, _22, _23, _24;
            f64 _31, _32, _33, _34;
        };
        Double4U rows[3];
        f64 m[3][4];
        f64 raw[12];
    };
};

struct Double44U{
    union{
        struct{
            f64 _11, _12, _13, _14;
            f64 _21, _22, _23, _24;
            f64 _31, _32, _33, _34;
            f64 _41, _42, _43, _44;
        };
        Double4U rows[4];
        f64 m[4][4];
        f64 raw[16];
    };
};

struct Int64x2U{
    union{
        struct{
            i64 x;
            i64 y;
        };
        struct{
            i64 r;
            i64 g;
        };
        i64 raw[2];
    };
};
struct Int64x3U{
    union{
        struct{
            i64 x;
            i64 y;
            i64 z;
        };
        struct{
            i64 r;
            i64 g;
            i64 b;
        };
        i64 raw[3];
    };
};
struct Int64x4U{
    union{
        struct{
            i64 x;
            i64 y;
            i64 z;
            i64 w;
        };
        struct{
            i64 r;
            i64 g;
            i64 b;
            i64 a;
        };
        i64 raw[4];
    };
};

struct UInt64x2U{
    union{
        struct{
            u64 x;
            u64 y;
        };
        struct{
            u64 r;
            u64 g;
        };
        u64 raw[2];
    };
};
struct UInt64x3U{
    union{
        struct{
            u64 x;
            u64 y;
            u64 z;
        };
        struct{
            u64 r;
            u64 g;
            u64 b;
        };
        u64 raw[3];
    };
};
struct UInt64x4U{
    union{
        struct{
            u64 x;
            u64 y;
            u64 z;
            u64 w;
        };
        struct{
            u64 r;
            u64 g;
            u64 b;
            u64 a;
        };
        u64 raw[4];
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE bool operator==(const Double4& lhs, const Double4& rhs)noexcept{ return MathTypeDetail::EqualArray<Double4, 4u>(lhs, rhs); }
[[nodiscard]] NWB_INLINE bool operator!=(const Double4& lhs, const Double4& rhs)noexcept{ return !(lhs == rhs); }

[[nodiscard]] NWB_INLINE bool operator==(const Double34& lhs, const Double34& rhs)noexcept{ return MathTypeDetail::EqualArray<Double34, 12u>(lhs, rhs); }
[[nodiscard]] NWB_INLINE bool operator!=(const Double34& lhs, const Double34& rhs)noexcept{ return !(lhs == rhs); }

[[nodiscard]] NWB_INLINE bool operator==(const Double44& lhs, const Double44& rhs)noexcept{ return MathTypeDetail::EqualArray<Double44, 16u>(lhs, rhs); }
[[nodiscard]] NWB_INLINE bool operator!=(const Double44& lhs, const Double44& rhs)noexcept{ return !(lhs == rhs); }

[[nodiscard]] NWB_INLINE bool operator==(const Int64x4& lhs, const Int64x4& rhs)noexcept{ return MathTypeDetail::EqualArray<Int64x4, 4u>(lhs, rhs); }
[[nodiscard]] NWB_INLINE bool operator!=(const Int64x4& lhs, const Int64x4& rhs)noexcept{ return !(lhs == rhs); }

[[nodiscard]] NWB_INLINE bool operator==(const UInt64x4& lhs, const UInt64x4& rhs)noexcept{ return MathTypeDetail::EqualArray<UInt64x4, 4u>(lhs, rhs); }
[[nodiscard]] NWB_INLINE bool operator!=(const UInt64x4& lhs, const UInt64x4& rhs)noexcept{ return !(lhs == rhs); }

[[nodiscard]] NWB_INLINE bool operator==(const Double3Int& lhs, const Double3Int& rhs)noexcept{
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.w == rhs.w;
}
[[nodiscard]] NWB_INLINE bool operator!=(const Double3Int& lhs, const Double3Int& rhs)noexcept{ return !(lhs == rhs); }

[[nodiscard]] NWB_INLINE bool operator==(const Double3UInt& lhs, const Double3UInt& rhs)noexcept{
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.w == rhs.w;
}
[[nodiscard]] NWB_INLINE bool operator!=(const Double3UInt& lhs, const Double3UInt& rhs)noexcept{ return !(lhs == rhs); }

[[nodiscard]] NWB_INLINE bool operator==(const Double2U& lhs, const Double2U& rhs)noexcept{ return MathTypeDetail::EqualArray<Double2U, 2u>(lhs, rhs); }
[[nodiscard]] NWB_INLINE bool operator!=(const Double2U& lhs, const Double2U& rhs)noexcept{ return !(lhs == rhs); }

[[nodiscard]] NWB_INLINE bool operator==(const Double3U& lhs, const Double3U& rhs)noexcept{ return MathTypeDetail::EqualArray<Double3U, 3u>(lhs, rhs); }
[[nodiscard]] NWB_INLINE bool operator!=(const Double3U& lhs, const Double3U& rhs)noexcept{ return !(lhs == rhs); }

[[nodiscard]] NWB_INLINE bool operator==(const Double4U& lhs, const Double4U& rhs)noexcept{ return MathTypeDetail::EqualArray<Double4U, 4u>(lhs, rhs); }
[[nodiscard]] NWB_INLINE bool operator!=(const Double4U& lhs, const Double4U& rhs)noexcept{ return !(lhs == rhs); }

[[nodiscard]] NWB_INLINE bool operator==(const Double33U& lhs, const Double33U& rhs)noexcept{ return MathTypeDetail::EqualArray<Double33U, 9u>(lhs, rhs); }
[[nodiscard]] NWB_INLINE bool operator!=(const Double33U& lhs, const Double33U& rhs)noexcept{ return !(lhs == rhs); }

[[nodiscard]] NWB_INLINE bool operator==(const Double34U& lhs, const Double34U& rhs)noexcept{ return MathTypeDetail::EqualArray<Double34U, 12u>(lhs, rhs); }
[[nodiscard]] NWB_INLINE bool operator!=(const Double34U& lhs, const Double34U& rhs)noexcept{ return !(lhs == rhs); }

[[nodiscard]] NWB_INLINE bool operator==(const Double44U& lhs, const Double44U& rhs)noexcept{ return MathTypeDetail::EqualArray<Double44U, 16u>(lhs, rhs); }
[[nodiscard]] NWB_INLINE bool operator!=(const Double44U& lhs, const Double44U& rhs)noexcept{ return !(lhs == rhs); }

[[nodiscard]] NWB_INLINE bool operator==(const Int64x2U& lhs, const Int64x2U& rhs)noexcept{ return MathTypeDetail::EqualArray<Int64x2U, 2u>(lhs, rhs); }
[[nodiscard]] NWB_INLINE bool operator!=(const Int64x2U& lhs, const Int64x2U& rhs)noexcept{ return !(lhs == rhs); }

[[nodiscard]] NWB_INLINE bool operator==(const Int64x3U& lhs, const Int64x3U& rhs)noexcept{ return MathTypeDetail::EqualArray<Int64x3U, 3u>(lhs, rhs); }
[[nodiscard]] NWB_INLINE bool operator!=(const Int64x3U& lhs, const Int64x3U& rhs)noexcept{ return !(lhs == rhs); }

[[nodiscard]] NWB_INLINE bool operator==(const Int64x4U& lhs, const Int64x4U& rhs)noexcept{ return MathTypeDetail::EqualArray<Int64x4U, 4u>(lhs, rhs); }
[[nodiscard]] NWB_INLINE bool operator!=(const Int64x4U& lhs, const Int64x4U& rhs)noexcept{ return !(lhs == rhs); }

[[nodiscard]] NWB_INLINE bool operator==(const UInt64x2U& lhs, const UInt64x2U& rhs)noexcept{ return MathTypeDetail::EqualArray<UInt64x2U, 2u>(lhs, rhs); }
[[nodiscard]] NWB_INLINE bool operator!=(const UInt64x2U& lhs, const UInt64x2U& rhs)noexcept{ return !(lhs == rhs); }

[[nodiscard]] NWB_INLINE bool operator==(const UInt64x3U& lhs, const UInt64x3U& rhs)noexcept{ return MathTypeDetail::EqualArray<UInt64x3U, 3u>(lhs, rhs); }
[[nodiscard]] NWB_INLINE bool operator!=(const UInt64x3U& lhs, const UInt64x3U& rhs)noexcept{ return !(lhs == rhs); }

[[nodiscard]] NWB_INLINE bool operator==(const UInt64x4U& lhs, const UInt64x4U& rhs)noexcept{ return MathTypeDetail::EqualArray<UInt64x4U, 4u>(lhs, rhs); }
[[nodiscard]] NWB_INLINE bool operator!=(const UInt64x4U& lhs, const UInt64x4U& rhs)noexcept{ return !(lhs == rhs); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace std{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<>
struct hash<Double4>{
    usize operator()(const Double4& value)const noexcept{ return MathTypeDoubleDetail::HashDoubleArray<Double4, 4u>(value); }
};

template<>
struct hash<Double34>{
    usize operator()(const Double34& value)const noexcept{ return MathTypeDoubleDetail::HashDoubleArray<Double34, 12u>(value); }
};

template<>
struct hash<Double44>{
    usize operator()(const Double44& value)const noexcept{ return MathTypeDoubleDetail::HashDoubleArray<Double44, 16u>(value); }
};

template<>
struct hash<Int64x4>{
    usize operator()(const Int64x4& value)const noexcept{ return MathTypeDetail::HashArray<Int64x4, 4u>(value); }
};

template<>
struct hash<UInt64x4>{
    usize operator()(const UInt64x4& value)const noexcept{ return MathTypeDetail::HashArray<UInt64x4, 4u>(value); }
};

template<>
struct hash<Double3Int>{
    usize operator()(const Double3Int& value)const noexcept{
        usize seed = 0u;
        ::HashCombineDouble(seed, value.x);
        ::HashCombineDouble(seed, value.y);
        ::HashCombineDouble(seed, value.z);
        ::HashCombine(seed, value.w);
        return seed;
    }
};

template<>
struct hash<Double3UInt>{
    usize operator()(const Double3UInt& value)const noexcept{
        usize seed = 0u;
        ::HashCombineDouble(seed, value.x);
        ::HashCombineDouble(seed, value.y);
        ::HashCombineDouble(seed, value.z);
        ::HashCombine(seed, value.w);
        return seed;
    }
};

template<>
struct hash<Double2U>{
    usize operator()(const Double2U& value)const noexcept{ return MathTypeDoubleDetail::HashDoubleArray<Double2U, 2u>(value); }
};

template<>
struct hash<Double3U>{
    usize operator()(const Double3U& value)const noexcept{ return MathTypeDoubleDetail::HashDoubleArray<Double3U, 3u>(value); }
};

template<>
struct hash<Double4U>{
    usize operator()(const Double4U& value)const noexcept{ return MathTypeDoubleDetail::HashDoubleArray<Double4U, 4u>(value); }
};

template<>
struct hash<Double33U>{
    usize operator()(const Double33U& value)const noexcept{ return MathTypeDoubleDetail::HashDoubleArray<Double33U, 9u>(value); }
};

template<>
struct hash<Double34U>{
    usize operator()(const Double34U& value)const noexcept{ return MathTypeDoubleDetail::HashDoubleArray<Double34U, 12u>(value); }
};

template<>
struct hash<Double44U>{
    usize operator()(const Double44U& value)const noexcept{ return MathTypeDoubleDetail::HashDoubleArray<Double44U, 16u>(value); }
};

template<>
struct hash<Int64x2U>{
    usize operator()(const Int64x2U& value)const noexcept{ return MathTypeDetail::HashArray<Int64x2U, 2u>(value); }
};

template<>
struct hash<Int64x3U>{
    usize operator()(const Int64x3U& value)const noexcept{ return MathTypeDetail::HashArray<Int64x3U, 3u>(value); }
};

template<>
struct hash<Int64x4U>{
    usize operator()(const Int64x4U& value)const noexcept{ return MathTypeDetail::HashArray<Int64x4U, 4u>(value); }
};

template<>
struct hash<UInt64x2U>{
    usize operator()(const UInt64x2U& value)const noexcept{ return MathTypeDetail::HashArray<UInt64x2U, 2u>(value); }
};

template<>
struct hash<UInt64x3U>{
    usize operator()(const UInt64x3U& value)const noexcept{ return MathTypeDetail::HashArray<UInt64x3U, 3u>(value); }
};

template<>
struct hash<UInt64x4U>{
    usize operator()(const UInt64x4U& value)const noexcept{ return MathTypeDetail::HashArray<UInt64x4U, 4u>(value); }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using SIMDVectorDouble = f64 __attribute__((ext_vector_type(4)));
static_assert(sizeof(SIMDVectorDouble) == sizeof(f64) * 4u);
static_assert(alignof(SIMDVectorDouble) >= alignof(f64));

struct SIMDMatrixDouble{
    SIMDVectorDouble v[4];
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

