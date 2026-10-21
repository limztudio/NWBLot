// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../type.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    explicit constexpr Float2U(const f32 (&pArray)[2])noexcept
        : x(pArray[0]), y(pArray[1])
    {
    }
};

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
    explicit constexpr Double2U(const f64 (&pArray)[2])noexcept
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
    explicit constexpr Float3U(const f32 (&pArray)[3])noexcept
        : x(pArray[0]), y(pArray[1]), z(pArray[2])
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
    explicit constexpr Double3U(const f64 (&pArray)[3])noexcept
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
    explicit constexpr Float4U(const f32 (&pArray)[4])noexcept
        : x(pArray[0]), y(pArray[1]), z(pArray[2]), w(pArray[3])
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
    explicit constexpr Double4U(const f64 (&pArray)[4])noexcept
        : x(pArray[0]), y(pArray[1]), z(pArray[2]), w(pArray[3])
    {
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

