// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../type.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using Half = u16;
using Float16 = Half;
static constexpr usize s_HalfByteSize = 2u;
static_assert(sizeof(Half) == s_HalfByteSize, "Half must stay a 16-bit IEEE 754 binary16 storage type");
static_assert(alignof(Half) == alignof(u16), "Half storage alignment must match u16");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


struct alignas(32) Double2{
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

    constexpr Double2()noexcept
        : x(0.0)
        , y(0.0)
    {
    }
    constexpr Double2(const f64 xValue, const f64 yValue)noexcept
        : x(xValue)
        , y(yValue)
    {
    }
    explicit constexpr Double2(const f64 (&pArray)[2])noexcept
        : x(pArray[0])
        , y(pArray[1])
    {
    }
};
static_assert(sizeof(Double2) == sizeof(f64) * 4u, "Double2 must stay a 32-byte aligned payload");
static_assert(alignof(Double2) == 32u, "Double2 must stay SIMD-aligned");
static_assert(IsStandardLayout_V<Double2> && IsTriviallyCopyable_V<Double2>, "Double2 must stay binary-serializable");

struct alignas(32) Double3{
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

    constexpr Double3()noexcept
        : x(0.0)
        , y(0.0)
        , z(0.0)
    {
    }
    constexpr Double3(const f64 xValue, const f64 yValue, const f64 zValue)noexcept
        : x(xValue)
        , y(yValue)
        , z(zValue)
    {
    }
    explicit constexpr Double3(const f64 (&pArray)[3])noexcept
        : x(pArray[0])
        , y(pArray[1])
        , z(pArray[2])
    {
    }
};
static_assert(sizeof(Double3) == sizeof(f64) * 4u, "Double3 must stay a 32-byte aligned payload");
static_assert(alignof(Double3) == 32u, "Double3 must stay SIMD-aligned");
static_assert(IsStandardLayout_V<Double3> && IsTriviallyCopyable_V<Double3>, "Double3 must stay binary-serializable");

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
    explicit constexpr Float4(const f32 (&pArray)[4])noexcept
        : x(pArray[0]), y(pArray[1]), z(pArray[2]), w(pArray[3])
    {
    }
};

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
    explicit constexpr Double4(const f64 (&pArray)[4])noexcept
        : x(pArray[0]), y(pArray[1]), z(pArray[2]), w(pArray[3])
    {
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

