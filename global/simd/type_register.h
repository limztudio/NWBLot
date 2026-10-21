// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../type.h"
#include "../bit.h"
#include "macro.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_SIMDVectorBytes = 16u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using SIMDVectorDouble = f64 __attribute__((ext_vector_type(4)));
static_assert(sizeof(SIMDVectorDouble) == sizeof(f64) * 4u);
static_assert(alignof(SIMDVectorDouble) >= alignof(f64));


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDVectorDoubleDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_HAS_NEON64)
using NativeDoublePair = float64x2_t;
using NativeUIntPair = uint64x2_t;
#elif defined(NWB_HAS_SSE2)
using NativeDoublePair = __m128d;
using NativeUIntPair = __m128i;
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_HAS_NEON64) || defined(NWB_HAS_SSE2)
NWB_INLINE NativeDoublePair NWB_SIMD_CALL GetLow(SIMDVectorDouble value)noexcept{
    return BitCast<NativeDoublePair>(__builtin_shufflevector(value, value, 0, 1));
}

NWB_INLINE NativeDoublePair NWB_SIMD_CALL GetHigh(SIMDVectorDouble value)noexcept{
    return BitCast<NativeDoublePair>(__builtin_shufflevector(value, value, 2, 3));
}

NWB_INLINE NativeUIntPair NWB_SIMD_CALL GetLowBits(SIMDVectorDouble value)noexcept{
    return BitCast<NativeUIntPair>(GetLow(value));
}

NWB_INLINE NativeUIntPair NWB_SIMD_CALL GetHighBits(SIMDVectorDouble value)noexcept{
    return BitCast<NativeUIntPair>(GetHigh(value));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL FromPairs(NativeDoublePair low, NativeDoublePair high)noexcept{
    using DoublePair = f64 __attribute__((ext_vector_type(2)));
    return __builtin_shufflevector(BitCast<DoublePair>(low), BitCast<DoublePair>(high), 0, 1, 2, 3);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL FromUIntPairs(NativeUIntPair low, NativeUIntPair high)noexcept{
    return FromPairs(BitCast<NativeDoublePair>(low), BitCast<NativeDoublePair>(high));
}
#endif


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
using SIMDVectorSingle = float32x4_t;
#elif defined(NWB_HAS_SCALAR)
using SIMDVectorSingle = FPUVector4;
#else
using SIMDVectorSingle = __m128;
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
        SIMDVectorSingle v[4];
        f32 m[4][4];
    };
#else
    SIMDVectorSingle v[4];
#endif
};

struct alignas(SIMDVectorDouble) SIMDMatrixDouble{
    SIMDVectorDouble v[4];
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

