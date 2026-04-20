// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../type.h"
#include "macro.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


union alignas(16) Float3{
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

union alignas(16) Float4{
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

union alignas(16) Float34{
    struct{
        f32 _11, _12, _13, _14;
        f32 _21, _22, _23, _24;
        f32 _31, _32, _33, _34;
    };
    Float4 rows[3];
    f32 m[3][4];
    f32 raw[12];
};

union alignas(16) Float44{
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// unaligned


union Float2U{
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

union Float3U{
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

union Float4U{
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

union Float34U{
    struct{
        f32 _11, _12, _13, _14;
        f32 _21, _22, _23, _24;
        f32 _31, _32, _33, _34;
    };
    Float4U rows[3];
    f32 m[3][4];
    f32 raw[12];
};

union Float44U{
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_HAS_SCALAR)
union FPUVector4{
    f32 f[4];
    u32 u[4];
};
#endif

#if defined(NWB_HAS_NEON)
using SIMDVector = float32x4_t;
#elif defined(NWB_HAS_SCALAR)
using SIMDVector = FPUVector4;
#else
using SIMDVector = __m128;
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

