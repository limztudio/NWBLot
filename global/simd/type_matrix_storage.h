// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type_vector_aligned.h"
#include "type_vector_unaligned.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The nine values are contiguous; 24-byte rows are compact and alignment adds only tail padding.
struct alignas(32) Double33{
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
static_assert(sizeof(Double33) == sizeof(f64) * 12u, "Double33 must stay a 96-byte aligned payload");
static_assert(alignof(Double33) == 32u, "Double33 must stay SIMD-aligned");
static_assert(offsetof(Double33, _21) == sizeof(f64) * 3u && offsetof(Double33, _31) == sizeof(f64) * 6u, "Double33 rows must stay compact");
static_assert(IsStandardLayout_V<Double33> && IsTriviallyCopyable_V<Double33>, "Double33 must stay binary-serializable");

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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

