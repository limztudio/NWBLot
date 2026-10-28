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
    explicit constexpr Float2U(const f32 (&values)[2])noexcept
        : x(values[0]), y(values[1])
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
    explicit constexpr Double2U(const f64 (&values)[2])noexcept
        : x(values[0]), y(values[1])
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
    explicit constexpr Float3U(const f32 (&values)[3])noexcept
        : x(values[0]), y(values[1]), z(values[2])
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
    explicit constexpr Double3U(const f64 (&values)[3])noexcept
        : x(values[0]), y(values[1]), z(values[2])
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
    explicit constexpr Float4U(const f32 (&values)[4])noexcept
        : x(values[0]), y(values[1]), z(values[2]), w(values[3])
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
    explicit constexpr Double4U(const f64 (&values)[4])noexcept
        : x(values[0]), y(values[1]), z(values[2]), w(values[3])
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

    constexpr Int2U()noexcept
        : x(0), y(0)
    {
    }
    constexpr Int2U(const i32 xValue, const i32 yValue)noexcept
        : x(xValue), y(yValue)
    {
    }
    explicit constexpr Int2U(const i32 (&values)[2])noexcept
        : x(values[0]), y(values[1])
    {
    }
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

    constexpr Int64x2U()noexcept
        : x(0), y(0)
    {
    }
    constexpr Int64x2U(const i64 xValue, const i64 yValue)noexcept
        : x(xValue), y(yValue)
    {
    }
    explicit constexpr Int64x2U(const i64 (&values)[2])noexcept
        : x(values[0]), y(values[1])
    {
    }
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

    constexpr Int3U()noexcept
        : x(0), y(0), z(0)
    {
    }
    constexpr Int3U(const i32 xValue, const i32 yValue, const i32 zValue)noexcept
        : x(xValue), y(yValue), z(zValue)
    {
    }
    explicit constexpr Int3U(const i32 (&values)[3])noexcept
        : x(values[0]), y(values[1]), z(values[2])
    {
    }
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

    constexpr Int64x3U()noexcept
        : x(0), y(0), z(0)
    {
    }
    constexpr Int64x3U(const i64 xValue, const i64 yValue, const i64 zValue)noexcept
        : x(xValue), y(yValue), z(zValue)
    {
    }
    explicit constexpr Int64x3U(const i64 (&values)[3])noexcept
        : x(values[0]), y(values[1]), z(values[2])
    {
    }
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

    constexpr Int4U()noexcept
        : x(0), y(0), z(0), w(0)
    {
    }
    constexpr Int4U(const i32 xValue, const i32 yValue, const i32 zValue, const i32 wValue)noexcept
        : x(xValue), y(yValue), z(zValue), w(wValue)
    {
    }
    explicit constexpr Int4U(const i32 (&values)[4])noexcept
        : x(values[0]), y(values[1]), z(values[2]), w(values[3])
    {
    }
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

    constexpr Int64x4U()noexcept
        : x(0), y(0), z(0), w(0)
    {
    }
    constexpr Int64x4U(const i64 xValue, const i64 yValue, const i64 zValue, const i64 wValue)noexcept
        : x(xValue), y(yValue), z(zValue), w(wValue)
    {
    }
    explicit constexpr Int64x4U(const i64 (&values)[4])noexcept
        : x(values[0]), y(values[1]), z(values[2]), w(values[3])
    {
    }
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

    constexpr UInt2U()noexcept
        : x(0u), y(0u)
    {
    }
    constexpr UInt2U(const u32 xValue, const u32 yValue)noexcept
        : x(xValue), y(yValue)
    {
    }
    explicit constexpr UInt2U(const u32 (&values)[2])noexcept
        : x(values[0]), y(values[1])
    {
    }
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

    constexpr UInt64x2U()noexcept
        : x(0u), y(0u)
    {
    }
    constexpr UInt64x2U(const u64 xValue, const u64 yValue)noexcept
        : x(xValue), y(yValue)
    {
    }
    explicit constexpr UInt64x2U(const u64 (&values)[2])noexcept
        : x(values[0]), y(values[1])
    {
    }
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

    constexpr UInt3U()noexcept
        : x(0u), y(0u), z(0u)
    {
    }
    constexpr UInt3U(const u32 xValue, const u32 yValue, const u32 zValue)noexcept
        : x(xValue), y(yValue), z(zValue)
    {
    }
    explicit constexpr UInt3U(const u32 (&values)[3])noexcept
        : x(values[0]), y(values[1]), z(values[2])
    {
    }
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

    constexpr UInt64x3U()noexcept
        : x(0u), y(0u), z(0u)
    {
    }
    constexpr UInt64x3U(const u64 xValue, const u64 yValue, const u64 zValue)noexcept
        : x(xValue), y(yValue), z(zValue)
    {
    }
    explicit constexpr UInt64x3U(const u64 (&values)[3])noexcept
        : x(values[0]), y(values[1]), z(values[2])
    {
    }
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

    constexpr UInt4U()noexcept
        : x(0u), y(0u), z(0u), w(0u)
    {
    }
    constexpr UInt4U(const u32 xValue, const u32 yValue, const u32 zValue, const u32 wValue)noexcept
        : x(xValue), y(yValue), z(zValue), w(wValue)
    {
    }
    explicit constexpr UInt4U(const u32 (&values)[4])noexcept
        : x(values[0]), y(values[1]), z(values[2]), w(values[3])
    {
    }
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

    constexpr UInt64x4U()noexcept
        : x(0u), y(0u), z(0u), w(0u)
    {
    }
    constexpr UInt64x4U(const u64 xValue, const u64 yValue, const u64 zValue, const u64 wValue)noexcept
        : x(xValue), y(yValue), z(zValue), w(wValue)
    {
    }
    explicit constexpr UInt64x4U(const u64 (&values)[4])noexcept
        : x(values[0]), y(values[1]), z(values[2]), w(values[3])
    {
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

