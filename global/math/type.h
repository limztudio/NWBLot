// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../type.h"
#include "macro.h"


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
    constexpr Float4(const f32 _x, const f32 _y, const f32 _z)noexcept
        : x(_x), y(_y), z(_z), w(0.0f)
    {
    }
    constexpr Float4(const f32 _x, const f32 _y, const f32 _z, const f32 _w)noexcept
        : x(_x), y(_y), z(_z), w(_w)
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
    constexpr Float2U(const f32 _x, const f32 _y)noexcept
        : x(_x), y(_y)
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
    constexpr Float3U(const f32 _x, const f32 _y, const f32 _z)noexcept
        : x(_x), y(_y), z(_z)
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
    constexpr Float4U(const f32 _x, const f32 _y, const f32 _z, const f32 _w)noexcept
        : x(_x), y(_y), z(_z), w(_w)
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

