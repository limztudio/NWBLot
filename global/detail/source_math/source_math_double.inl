// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline DoubleMatrix MathCallConv DoubleMatrixSet(
    double m00, double m01, double m02, double m03,
    double m10, double m11, double m12, double m13,
    double m20, double m21, double m22, double m23,
    double m30, double m31, double m32, double m33
)noexcept;
inline DoubleMatrix MathCallConv DoubleMatrixTranspose(const DoubleMatrix& M)noexcept;
inline double MathCallConv DoubleVectorGetX(DoubleVectorArg V)noexcept;
inline double MathCallConv DoubleVectorGetY(DoubleVectorArg V)noexcept;
inline double MathCallConv DoubleVectorGetZ(DoubleVectorArg V)noexcept;
inline double MathCallConv DoubleVectorGetW(DoubleVectorArg V)noexcept;
inline void MathCallConv StoreDouble4(Double4* pDestination, DoubleVectorArg V)noexcept;
inline DoubleVector MathCallConv DoubleVectorSubtract(DoubleVectorArg V1, DoubleVectorArg V2)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline DoubleVector MathCallConv DoubleVectorZero()noexcept{
#if defined(_MATH_AVX_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    return _mm256_setzero_pd();
#elif defined(_MATH_SSE_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    DoubleVector result{};
    result.lo = _mm_setzero_pd();
    result.hi = _mm_setzero_pd();
    return result;
#elif defined(_MATH_ARM_NEON_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_) && (defined(__aarch64__) || defined(_M_ARM64))
    DoubleVector result{};
    result.lo = vdupq_n_f64(0.0);
    result.hi = vdupq_n_f64(0.0);
    return result;
#else
    DoubleVector result{};
    result.vector4_f64[0] = 0.0;
    result.vector4_f64[1] = 0.0;
    result.vector4_f64[2] = 0.0;
    result.vector4_f64[3] = 0.0;
    return result;
#endif
}

inline DoubleVector MathCallConv DoubleVectorSet(
    double x,
    double y,
    double z,
    double w
)noexcept{
#if defined(_MATH_AVX_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    return _mm256_set_pd(w, z, y, x);
#elif defined(_MATH_SSE_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    DoubleVector result{};
    result.lo = _mm_set_pd(y, x);
    result.hi = _mm_set_pd(w, z);
    return result;
#elif defined(_MATH_ARM_NEON_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_) && (defined(__aarch64__) || defined(_M_ARM64))
    DoubleVector result{};
    const double low[2] = { x, y };
    const double high[2] = { z, w };
    result.lo = vld1q_f64(low);
    result.hi = vld1q_f64(high);
    return result;
#else
    DoubleVector result{};
    result.vector4_f64[0] = x;
    result.vector4_f64[1] = y;
    result.vector4_f64[2] = z;
    result.vector4_f64[3] = w;
    return result;
#endif
}

inline DoubleVector MathCallConv DoubleVectorReplicate(double value)noexcept{
#if defined(_MATH_AVX_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    return _mm256_set1_pd(value);
#elif defined(_MATH_SSE_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    DoubleVector result{};
    result.lo = _mm_set1_pd(value);
    result.hi = _mm_set1_pd(value);
    return result;
#elif defined(_MATH_ARM_NEON_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_) && (defined(__aarch64__) || defined(_M_ARM64))
    DoubleVector result{};
    result.lo = vdupq_n_f64(value);
    result.hi = vdupq_n_f64(value);
    return result;
#else
    return DoubleVectorSet(value, value, value, value);
#endif
}

_Use_decl_annotations_
inline DoubleVector MathCallConv LoadDouble(const double* pSource)noexcept{
    return DoubleVectorSet(pSource[0], 0.0, 0.0, 0.0);
}

_Use_decl_annotations_
inline DoubleVector MathCallConv LoadDouble2(const Double2* pSource)noexcept{
    return DoubleVectorSet(pSource->x, pSource->y, 0.0, 0.0);
}

_Use_decl_annotations_
inline DoubleVector MathCallConv LoadDouble2A(const AlignedDouble2* pSource)noexcept{
    return LoadDouble2(pSource);
}

_Use_decl_annotations_
inline DoubleVector MathCallConv LoadDouble3(const Double3* pSource)noexcept{
    return DoubleVectorSet(pSource->x, pSource->y, pSource->z, 0.0);
}

_Use_decl_annotations_
inline DoubleVector MathCallConv LoadDouble3A(const AlignedDouble3* pSource)noexcept{
    return LoadDouble3(pSource);
}

_Use_decl_annotations_
inline DoubleVector MathCallConv LoadDouble4(const Double4* pSource)noexcept{
#if defined(_MATH_AVX_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    return _mm256_loadu_pd(&pSource->x);
#elif defined(_MATH_SSE_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    DoubleVector result{};
    result.lo = _mm_loadu_pd(&pSource->x);
    result.hi = _mm_loadu_pd(&pSource->z);
    return result;
#elif defined(_MATH_ARM_NEON_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_) && (defined(__aarch64__) || defined(_M_ARM64))
    DoubleVector result{};
    result.lo = vld1q_f64(&pSource->x);
    result.hi = vld1q_f64(&pSource->z);
    return result;
#else
    return DoubleVectorSet(pSource->x, pSource->y, pSource->z, pSource->w);
#endif
}

_Use_decl_annotations_
inline DoubleVector MathCallConv LoadDouble4A(const AlignedDouble4* pSource)noexcept{
#if defined(_MATH_AVX_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    return _mm256_load_pd(&pSource->x);
#elif defined(_MATH_SSE_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    DoubleVector result{};
    result.lo = _mm_load_pd(&pSource->x);
    result.hi = _mm_load_pd(&pSource->z);
    return result;
#else
    return LoadDouble4(pSource);
#endif
}

_Use_decl_annotations_
inline void MathCallConv StoreDouble(double* pDestination, DoubleVectorArg V)noexcept{
    pDestination[0] = DoubleVectorGetX(V);
}

_Use_decl_annotations_
inline void MathCallConv StoreDouble2(Double2* pDestination, DoubleVectorArg V)noexcept{
    Double4 values{};
    StoreDouble4(&values, V);
    pDestination->x = values.x;
    pDestination->y = values.y;
}

_Use_decl_annotations_
inline void MathCallConv StoreDouble2A(AlignedDouble2* pDestination, DoubleVectorArg V)noexcept{
    StoreDouble2(pDestination, V);
}

_Use_decl_annotations_
inline void MathCallConv StoreDouble3(Double3* pDestination, DoubleVectorArg V)noexcept{
    Double4 values{};
    StoreDouble4(&values, V);
    pDestination->x = values.x;
    pDestination->y = values.y;
    pDestination->z = values.z;
}

_Use_decl_annotations_
inline void MathCallConv StoreDouble3A(AlignedDouble3* pDestination, DoubleVectorArg V)noexcept{
    StoreDouble3(pDestination, V);
}

_Use_decl_annotations_
inline void MathCallConv StoreDouble4(Double4* pDestination, DoubleVectorArg V)noexcept{
#if defined(_MATH_AVX_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    _mm256_storeu_pd(&pDestination->x, V);
#elif defined(_MATH_SSE_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    _mm_storeu_pd(&pDestination->x, V.lo);
    _mm_storeu_pd(&pDestination->z, V.hi);
#elif defined(_MATH_ARM_NEON_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_) && (defined(__aarch64__) || defined(_M_ARM64))
    vst1q_f64(&pDestination->x, V.lo);
    vst1q_f64(&pDestination->z, V.hi);
#else
    pDestination->x = V.vector4_f64[0];
    pDestination->y = V.vector4_f64[1];
    pDestination->z = V.vector4_f64[2];
    pDestination->w = V.vector4_f64[3];
#endif
}

_Use_decl_annotations_
inline void MathCallConv StoreDouble4A(AlignedDouble4* pDestination, DoubleVectorArg V)noexcept{
#if defined(_MATH_AVX_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    _mm256_store_pd(&pDestination->x, V);
#elif defined(_MATH_SSE_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    _mm_store_pd(&pDestination->x, V.lo);
    _mm_store_pd(&pDestination->z, V.hi);
#else
    StoreDouble4(pDestination, V);
#endif
}

_Use_decl_annotations_
inline DoubleMatrix MathCallConv LoadDouble3x4(const Double3x4* pSource)noexcept{
    assert(pSource);
    return DoubleMatrix(
        DoubleVectorSet(pSource->m[0][0], pSource->m[1][0], pSource->m[2][0], 0.0),
        DoubleVectorSet(pSource->m[0][1], pSource->m[1][1], pSource->m[2][1], 0.0),
        DoubleVectorSet(pSource->m[0][2], pSource->m[1][2], pSource->m[2][2], 0.0),
        DoubleVectorSet(pSource->m[0][3], pSource->m[1][3], pSource->m[2][3], 1.0)
    );
}

_Use_decl_annotations_
inline DoubleMatrix MathCallConv LoadDouble3x4A(const AlignedDouble3x4* pSource)noexcept{
    assert(pSource);
#if defined(_MATH_AVX_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    assert((reinterpret_cast<uintptr_t>(pSource) & 0x1F) == 0);
#else
    assert((reinterpret_cast<uintptr_t>(pSource) & 0xF) == 0);
#endif
    return LoadDouble3x4(pSource);
}

_Use_decl_annotations_
inline void MathCallConv StoreDouble3x4(Double3x4* pDestination, const DoubleMatrix& M)noexcept{
    assert(pDestination);

    Double4 column0{};
    Double4 column1{};
    Double4 column2{};
    Double4 column3{};
    StoreDouble4(&column0, M.r[0]);
    StoreDouble4(&column1, M.r[1]);
    StoreDouble4(&column2, M.r[2]);
    StoreDouble4(&column3, M.r[3]);

    pDestination->m[0][0] = column0.x;
    pDestination->m[0][1] = column1.x;
    pDestination->m[0][2] = column2.x;
    pDestination->m[0][3] = column3.x;

    pDestination->m[1][0] = column0.y;
    pDestination->m[1][1] = column1.y;
    pDestination->m[1][2] = column2.y;
    pDestination->m[1][3] = column3.y;

    pDestination->m[2][0] = column0.z;
    pDestination->m[2][1] = column1.z;
    pDestination->m[2][2] = column2.z;
    pDestination->m[2][3] = column3.z;
}

_Use_decl_annotations_
inline void MathCallConv StoreDouble3x4A(AlignedDouble3x4* pDestination, const DoubleMatrix& M)noexcept{
    assert(pDestination);
#if defined(_MATH_AVX_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    assert((reinterpret_cast<uintptr_t>(pDestination) & 0x1F) == 0);
#else
    assert((reinterpret_cast<uintptr_t>(pDestination) & 0xF) == 0);
#endif
    StoreDouble3x4(pDestination, M);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline DoubleVector MathCallConv DoubleVectorSplatX(DoubleVectorArg V)noexcept{
#if defined(_MATH_AVX2_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    return _mm256_permute4x64_pd(V, _MM_SHUFFLE(0, 0, 0, 0));
#else
    return DoubleVectorReplicate(DoubleVectorGetX(V));
#endif
}

inline DoubleVector MathCallConv DoubleVectorSplatY(DoubleVectorArg V)noexcept{
#if defined(_MATH_AVX2_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    return _mm256_permute4x64_pd(V, _MM_SHUFFLE(1, 1, 1, 1));
#else
    return DoubleVectorReplicate(DoubleVectorGetY(V));
#endif
}

inline DoubleVector MathCallConv DoubleVectorSplatZ(DoubleVectorArg V)noexcept{
#if defined(_MATH_AVX2_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    return _mm256_permute4x64_pd(V, _MM_SHUFFLE(2, 2, 2, 2));
#else
    return DoubleVectorReplicate(DoubleVectorGetZ(V));
#endif
}

inline DoubleVector MathCallConv DoubleVectorSplatW(DoubleVectorArg V)noexcept{
#if defined(_MATH_AVX2_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    return _mm256_permute4x64_pd(V, _MM_SHUFFLE(3, 3, 3, 3));
#else
    return DoubleVectorReplicate(DoubleVectorGetW(V));
#endif
}

inline double MathCallConv DoubleVectorGetByIndex(DoubleVectorArg V, size_t i)noexcept{
    assert(i < 4);
    _Analysis_assume_(i < 4);
    switch(i){
    case 0:
        return DoubleVectorGetX(V);
    case 1:
        return DoubleVectorGetY(V);
    case 2:
        return DoubleVectorGetZ(V);
    default:
        return DoubleVectorGetW(V);
    }
}

inline double MathCallConv DoubleVectorGetX(DoubleVectorArg V)noexcept{
#if defined(_MATH_AVX_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    return _mm_cvtsd_f64(_mm256_castpd256_pd128(V));
#elif defined(_MATH_SSE_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    return _mm_cvtsd_f64(V.lo);
#elif defined(_MATH_ARM_NEON_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_) && (defined(__aarch64__) || defined(_M_ARM64))
    return vgetq_lane_f64(V.lo, 0);
#else
    return V.vector4_f64[0];
#endif
}

inline double MathCallConv DoubleVectorGetY(DoubleVectorArg V)noexcept{
#if defined(_MATH_AVX_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    const __m128d low = _mm256_castpd256_pd128(V);
    return _mm_cvtsd_f64(_mm_unpackhi_pd(low, low));
#elif defined(_MATH_SSE_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    return _mm_cvtsd_f64(_mm_unpackhi_pd(V.lo, V.lo));
#elif defined(_MATH_ARM_NEON_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_) && (defined(__aarch64__) || defined(_M_ARM64))
    return vgetq_lane_f64(V.lo, 1);
#else
    return V.vector4_f64[1];
#endif
}

inline double MathCallConv DoubleVectorGetZ(DoubleVectorArg V)noexcept{
#if defined(_MATH_AVX_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    return _mm_cvtsd_f64(_mm256_extractf128_pd(V, 1));
#elif defined(_MATH_SSE_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    return _mm_cvtsd_f64(V.hi);
#elif defined(_MATH_ARM_NEON_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_) && (defined(__aarch64__) || defined(_M_ARM64))
    return vgetq_lane_f64(V.hi, 0);
#else
    return V.vector4_f64[2];
#endif
}

inline double MathCallConv DoubleVectorGetW(DoubleVectorArg V)noexcept{
#if defined(_MATH_AVX_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    const __m128d high = _mm256_extractf128_pd(V, 1);
    return _mm_cvtsd_f64(_mm_unpackhi_pd(high, high));
#elif defined(_MATH_SSE_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    return _mm_cvtsd_f64(_mm_unpackhi_pd(V.hi, V.hi));
#elif defined(_MATH_ARM_NEON_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_) && (defined(__aarch64__) || defined(_M_ARM64))
    return vgetq_lane_f64(V.hi, 1);
#else
    return V.vector4_f64[3];
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline DoubleVector MathCallConv DoubleVectorNegate(DoubleVectorArg V)noexcept{
    return DoubleVectorSubtract(DoubleVectorZero(), V);
}

inline DoubleVector MathCallConv DoubleVectorAdd(DoubleVectorArg V1, DoubleVectorArg V2)noexcept{
#if defined(_MATH_AVX_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    return _mm256_add_pd(V1, V2);
#elif defined(_MATH_SSE_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    DoubleVector result{};
    result.lo = _mm_add_pd(V1.lo, V2.lo);
    result.hi = _mm_add_pd(V1.hi, V2.hi);
    return result;
#elif defined(_MATH_ARM_NEON_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_) && (defined(__aarch64__) || defined(_M_ARM64))
    DoubleVector result{};
    result.lo = vaddq_f64(V1.lo, V2.lo);
    result.hi = vaddq_f64(V1.hi, V2.hi);
    return result;
#else
    return DoubleVectorSet(
        V1.vector4_f64[0] + V2.vector4_f64[0],
        V1.vector4_f64[1] + V2.vector4_f64[1],
        V1.vector4_f64[2] + V2.vector4_f64[2],
        V1.vector4_f64[3] + V2.vector4_f64[3]
    );
#endif
}

inline DoubleVector MathCallConv DoubleVectorSubtract(DoubleVectorArg V1, DoubleVectorArg V2)noexcept{
#if defined(_MATH_AVX_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    return _mm256_sub_pd(V1, V2);
#elif defined(_MATH_SSE_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    DoubleVector result{};
    result.lo = _mm_sub_pd(V1.lo, V2.lo);
    result.hi = _mm_sub_pd(V1.hi, V2.hi);
    return result;
#elif defined(_MATH_ARM_NEON_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_) && (defined(__aarch64__) || defined(_M_ARM64))
    DoubleVector result{};
    result.lo = vsubq_f64(V1.lo, V2.lo);
    result.hi = vsubq_f64(V1.hi, V2.hi);
    return result;
#else
    return DoubleVectorSet(
        V1.vector4_f64[0] - V2.vector4_f64[0],
        V1.vector4_f64[1] - V2.vector4_f64[1],
        V1.vector4_f64[2] - V2.vector4_f64[2],
        V1.vector4_f64[3] - V2.vector4_f64[3]
    );
#endif
}

inline DoubleVector MathCallConv DoubleVectorMultiply(DoubleVectorArg V1, DoubleVectorArg V2)noexcept{
#if defined(_MATH_AVX_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    return _mm256_mul_pd(V1, V2);
#elif defined(_MATH_SSE_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    DoubleVector result{};
    result.lo = _mm_mul_pd(V1.lo, V2.lo);
    result.hi = _mm_mul_pd(V1.hi, V2.hi);
    return result;
#elif defined(_MATH_ARM_NEON_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_) && (defined(__aarch64__) || defined(_M_ARM64))
    DoubleVector result{};
    result.lo = vmulq_f64(V1.lo, V2.lo);
    result.hi = vmulq_f64(V1.hi, V2.hi);
    return result;
#else
    return DoubleVectorSet(
        V1.vector4_f64[0] * V2.vector4_f64[0],
        V1.vector4_f64[1] * V2.vector4_f64[1],
        V1.vector4_f64[2] * V2.vector4_f64[2],
        V1.vector4_f64[3] * V2.vector4_f64[3]
    );
#endif
}

inline DoubleVector MathCallConv DoubleVectorMultiplyAdd(DoubleVectorArg V1, DoubleVectorArg V2, DoubleVectorArg V3)noexcept{
#if defined(_MATH_AVX_INTRINSICS_) && defined(_MATH_FMA3_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    return _mm256_fmadd_pd(V1, V2, V3);
#elif defined(_MATH_ARM_NEON_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_) && (defined(__aarch64__) || defined(_M_ARM64))
    DoubleVector result{};
    result.lo = vfmaq_f64(V3.lo, V1.lo, V2.lo);
    result.hi = vfmaq_f64(V3.hi, V1.hi, V2.hi);
    return result;
#else
    return DoubleVectorAdd(DoubleVectorMultiply(V1, V2), V3);
#endif
}

inline DoubleVector MathCallConv DoubleVectorDivide(DoubleVectorArg V1, DoubleVectorArg V2)noexcept{
#if defined(_MATH_AVX_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    return _mm256_div_pd(V1, V2);
#elif defined(_MATH_SSE_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    DoubleVector result{};
    result.lo = _mm_div_pd(V1.lo, V2.lo);
    result.hi = _mm_div_pd(V1.hi, V2.hi);
    return result;
#elif defined(_MATH_ARM_NEON_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_) && (defined(__aarch64__) || defined(_M_ARM64))
    DoubleVector result{};
    result.lo = vdivq_f64(V1.lo, V2.lo);
    result.hi = vdivq_f64(V1.hi, V2.hi);
    return result;
#else
    return DoubleVectorSet(
        V1.vector4_f64[0] / V2.vector4_f64[0],
        V1.vector4_f64[1] / V2.vector4_f64[1],
        V1.vector4_f64[2] / V2.vector4_f64[2],
        V1.vector4_f64[3] / V2.vector4_f64[3]
    );
#endif
}

inline DoubleVector MathCallConv DoubleVectorScale(DoubleVectorArg V, double scaleFactor)noexcept{
    return DoubleVectorMultiply(V, DoubleVectorReplicate(scaleFactor));
}

inline DoubleVector MathCallConv DoubleVectorSqrt(DoubleVectorArg V)noexcept{
#if defined(_MATH_AVX_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    return _mm256_sqrt_pd(V);
#elif defined(_MATH_SSE_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    DoubleVector result{};
    result.lo = _mm_sqrt_pd(V.lo);
    result.hi = _mm_sqrt_pd(V.hi);
    return result;
#elif defined(_MATH_ARM_NEON_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_) && (defined(__aarch64__) || defined(_M_ARM64))
    DoubleVector result{};
    result.lo = vsqrtq_f64(V.lo);
    result.hi = vsqrtq_f64(V.hi);
    return result;
#else
    return DoubleVectorSet(
        sqrt(V.vector4_f64[0]),
        sqrt(V.vector4_f64[1]),
        sqrt(V.vector4_f64[2]),
        sqrt(V.vector4_f64[3])
    );
#endif
}

inline DoubleVector MathCallConv DoubleVectorAbs(DoubleVectorArg V)noexcept{
#if defined(_MATH_AVX_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    const __m256d mask = _mm256_castsi256_pd(_mm256_set1_epi64x(0x7fffffffffffffffLL));
    return _mm256_and_pd(V, mask);
#elif defined(_MATH_SSE_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_)
    const __m128d mask = _mm_castsi128_pd(_mm_set_epi32(0x7fffffff, -1, 0x7fffffff, -1));
    DoubleVector result{};
    result.lo = _mm_and_pd(V.lo, mask);
    result.hi = _mm_and_pd(V.hi, mask);
    return result;
#elif defined(_MATH_ARM_NEON_INTRINSICS_) && !defined(_MATH_NO_INTRINSICS_) && (defined(__aarch64__) || defined(_M_ARM64))
    DoubleVector result{};
    result.lo = vabsq_f64(V.lo);
    result.hi = vabsq_f64(V.hi);
    return result;
#else
    Double4 values{};
    StoreDouble4(&values, V);
    return DoubleVectorSet(
        fabs(values.x),
        fabs(values.y),
        fabs(values.z),
        fabs(values.w)
    );
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline DoubleVector MathCallConv DoubleVector2Dot(DoubleVectorArg V1, DoubleVectorArg V2)noexcept{
    const double value = DoubleVectorGetX(V1) * DoubleVectorGetX(V2)
        + DoubleVectorGetY(V1) * DoubleVectorGetY(V2);
    return DoubleVectorReplicate(value);
}

inline DoubleVector MathCallConv DoubleVector2LengthSq(DoubleVectorArg V)noexcept{
    return DoubleVector2Dot(V, V);
}

inline DoubleVector MathCallConv DoubleVector2Length(DoubleVectorArg V)noexcept{
    return DoubleVectorSqrt(DoubleVector2LengthSq(V));
}

inline DoubleVector MathCallConv DoubleVector2Normalize(DoubleVectorArg V)noexcept{
    const double length = DoubleVectorGetX(DoubleVector2Length(V));
    if(length <= 0.0 || isinf(length))
        return DoubleVectorZero();

    return DoubleVectorScale(V, 1.0 / length);
}

inline DoubleVector MathCallConv DoubleVector2Cross(DoubleVectorArg V1, DoubleVectorArg V2)noexcept{
    const double value = DoubleVectorGetX(V1) * DoubleVectorGetY(V2)
        - DoubleVectorGetY(V1) * DoubleVectorGetX(V2);
    return DoubleVectorSet(0.0, 0.0, value, 0.0);
}

inline DoubleVector MathCallConv DoubleVector3Dot(DoubleVectorArg V1, DoubleVectorArg V2)noexcept{
    const double value = DoubleVectorGetX(V1) * DoubleVectorGetX(V2)
        + DoubleVectorGetY(V1) * DoubleVectorGetY(V2)
        + DoubleVectorGetZ(V1) * DoubleVectorGetZ(V2);
    return DoubleVectorReplicate(value);
}

inline DoubleVector MathCallConv DoubleVector4Dot(DoubleVectorArg V1, DoubleVectorArg V2)noexcept{
    const double value = DoubleVectorGetX(V1) * DoubleVectorGetX(V2)
        + DoubleVectorGetY(V1) * DoubleVectorGetY(V2)
        + DoubleVectorGetZ(V1) * DoubleVectorGetZ(V2)
        + DoubleVectorGetW(V1) * DoubleVectorGetW(V2);
    return DoubleVectorReplicate(value);
}

inline DoubleVector MathCallConv DoubleVector3Cross(DoubleVectorArg V1, DoubleVectorArg V2)noexcept{
    const double x1 = DoubleVectorGetX(V1);
    const double y1 = DoubleVectorGetY(V1);
    const double z1 = DoubleVectorGetZ(V1);
    const double x2 = DoubleVectorGetX(V2);
    const double y2 = DoubleVectorGetY(V2);
    const double z2 = DoubleVectorGetZ(V2);
    return DoubleVectorSet(
        y1 * z2 - z1 * y2,
        z1 * x2 - x1 * z2,
        x1 * y2 - y1 * x2,
        0.0
    );
}

inline DoubleVector MathCallConv DoubleVector3LengthSq(DoubleVectorArg V)noexcept{
    return DoubleVector3Dot(V, V);
}

inline DoubleVector MathCallConv DoubleVector3Length(DoubleVectorArg V)noexcept{
    return DoubleVectorSqrt(DoubleVector3LengthSq(V));
}

inline DoubleVector MathCallConv DoubleVector3Normalize(DoubleVectorArg V)noexcept{
    const double length = DoubleVectorGetX(DoubleVector3Length(V));
    if(length <= 0.0 || isinf(length))
        return DoubleVectorZero();

    return DoubleVectorScale(V, 1.0 / length);
}

inline DoubleVector MathCallConv DoubleVector4LengthSq(DoubleVectorArg V)noexcept{
    return DoubleVector4Dot(V, V);
}

inline DoubleVector MathCallConv DoubleVector4Length(DoubleVectorArg V)noexcept{
    return DoubleVectorSqrt(DoubleVector4LengthSq(V));
}

inline DoubleVector MathCallConv DoubleVector4Normalize(DoubleVectorArg V)noexcept{
    const double length = DoubleVectorGetX(DoubleVector4Length(V));
    if(length <= 0.0 || isinf(length))
        return DoubleVectorZero();

    return DoubleVectorScale(V, 1.0 / length);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline DoubleVector MathCallConv DoubleQuaternionIdentity()noexcept{
    return DoubleVectorSet(0.0, 0.0, 0.0, 1.0);
}

inline DoubleVector MathCallConv DoubleQuaternionConjugate(DoubleVectorArg Q)noexcept{
    return DoubleVectorSet(
        -DoubleVectorGetX(Q),
        -DoubleVectorGetY(Q),
        -DoubleVectorGetZ(Q),
        DoubleVectorGetW(Q)
    );
}

inline DoubleVector MathCallConv DoubleQuaternionMultiply(DoubleVectorArg Q1, DoubleVectorArg Q2)noexcept{
    const double x1 = DoubleVectorGetX(Q1);
    const double y1 = DoubleVectorGetY(Q1);
    const double z1 = DoubleVectorGetZ(Q1);
    const double w1 = DoubleVectorGetW(Q1);
    const double x2 = DoubleVectorGetX(Q2);
    const double y2 = DoubleVectorGetY(Q2);
    const double z2 = DoubleVectorGetZ(Q2);
    const double w2 = DoubleVectorGetW(Q2);

    return DoubleVectorSet(
        w2 * x1 + x2 * w1 + y2 * z1 - z2 * y1,
        w2 * y1 - x2 * z1 + y2 * w1 + z2 * x1,
        w2 * z1 + x2 * y1 - y2 * x1 + z2 * w1,
        w2 * w1 - x2 * x1 - y2 * y1 - z2 * z1
    );
}

inline DoubleVector MathCallConv DoubleQuaternionNormalize(DoubleVectorArg Q)noexcept{
    return DoubleVector4Normalize(Q);
}

inline DoubleVector MathCallConv DoubleQuaternionRotationNormal(DoubleVectorArg normalAxis, double angle)noexcept{
    const double halfAngle = 0.5 * angle;
    const double sinAngle = sin(halfAngle);
    const double cosAngle = cos(halfAngle);
    return DoubleVectorSet(
        DoubleVectorGetX(normalAxis) * sinAngle,
        DoubleVectorGetY(normalAxis) * sinAngle,
        DoubleVectorGetZ(normalAxis) * sinAngle,
        cosAngle
    );
}

inline DoubleVector MathCallConv DoubleQuaternionRotationAxis(DoubleVectorArg axis, double angle)noexcept{
    return DoubleQuaternionRotationNormal(DoubleVector3Normalize(axis), angle);
}

inline DoubleVector MathCallConv DoubleQuaternionRotationRollPitchYaw(
    double pitch,
    double yaw,
    double roll
)noexcept{
    const DoubleVector qYaw = DoubleQuaternionRotationNormal(DoubleVectorSet(0.0, 1.0, 0.0, 0.0), yaw);
    const DoubleVector qPitch = DoubleQuaternionRotationNormal(DoubleVectorSet(1.0, 0.0, 0.0, 0.0), pitch);
    const DoubleVector qRoll = DoubleQuaternionRotationNormal(DoubleVectorSet(0.0, 0.0, 1.0, 0.0), roll);
    return DoubleQuaternionMultiply(DoubleQuaternionMultiply(qYaw, qPitch), qRoll);
}

inline DoubleVector MathCallConv DoubleQuaternionRotationRollPitchYawFromVector(DoubleVectorArg angles)noexcept{
    return DoubleQuaternionRotationRollPitchYaw(
        DoubleVectorGetX(angles),
        DoubleVectorGetY(angles),
        DoubleVectorGetZ(angles)
    );
}

inline DoubleVector MathCallConv DoublePlaneDot(DoubleVectorArg P, DoubleVectorArg V)noexcept{
    return DoubleVector4Dot(P, V);
}

inline DoubleVector MathCallConv DoublePlaneNormalize(DoubleVectorArg P)noexcept{
    const double length = DoubleVectorGetX(DoubleVector3Length(P));
    if(length <= 0.0 || isinf(length))
        return DoubleVectorZero();

    return DoubleVectorScale(P, 1.0 / length);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline DoubleVector MathCallConv DoubleVector4Transform(DoubleVectorArg V, const DoubleMatrix& M)noexcept{
    DoubleVector result = DoubleVectorMultiply(DoubleVectorSplatW(V), M.r[3]);
    result = DoubleVectorMultiplyAdd(DoubleVectorSplatZ(V), M.r[2], result);
    result = DoubleVectorMultiplyAdd(DoubleVectorSplatY(V), M.r[1], result);
    result = DoubleVectorMultiplyAdd(DoubleVectorSplatX(V), M.r[0], result);
    return result;
}

inline DoubleVector MathCallConv DoubleVector2Transform(DoubleVectorArg V, const DoubleMatrix& M)noexcept{
    DoubleVector result = DoubleVectorMultiplyAdd(DoubleVectorSplatY(V), M.r[1], M.r[3]);
    result = DoubleVectorMultiplyAdd(DoubleVectorSplatX(V), M.r[0], result);
    return result;
}

inline DoubleVector MathCallConv DoubleVector2TransformCoord(DoubleVectorArg V, const DoubleMatrix& M)noexcept{
    DoubleVector result = DoubleVector2Transform(V, M);
    return DoubleVectorDivide(result, DoubleVectorSplatW(result));
}

inline DoubleVector MathCallConv DoubleVector2TransformNormal(DoubleVectorArg V, const DoubleMatrix& M)noexcept{
    DoubleVector result = DoubleVectorMultiply(DoubleVectorSplatY(V), M.r[1]);
    result = DoubleVectorMultiplyAdd(DoubleVectorSplatX(V), M.r[0], result);
    return result;
}

inline DoubleVector MathCallConv DoubleVector3Transform(DoubleVectorArg V, const DoubleMatrix& M)noexcept{
    return DoubleVector4Transform(DoubleVectorSet(DoubleVectorGetX(V), DoubleVectorGetY(V), DoubleVectorGetZ(V), 1.0), M);
}

inline DoubleVector MathCallConv DoubleVector3TransformCoord(DoubleVectorArg V, const DoubleMatrix& M)noexcept{
    DoubleVector result = DoubleVector3Transform(V, M);
    return DoubleVectorDivide(result, DoubleVectorSplatW(result));
}

inline DoubleVector MathCallConv DoubleVector3TransformNormal(DoubleVectorArg V, const DoubleMatrix& M)noexcept{
    DoubleVector result = DoubleVectorMultiply(DoubleVectorSplatZ(V), M.r[2]);
    result = DoubleVectorMultiplyAdd(DoubleVectorSplatY(V), M.r[1], result);
    result = DoubleVectorMultiplyAdd(DoubleVectorSplatX(V), M.r[0], result);
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline DoubleMatrix MathCallConv DoubleMatrixZero()noexcept{
    const DoubleVector zero = DoubleVectorZero();
    return DoubleMatrix(zero, zero, zero, zero);
}

inline DoubleMatrix MathCallConv DoubleMatrixIdentity()noexcept{
    return DoubleMatrix(
        DoubleVectorSet(1.0, 0.0, 0.0, 0.0),
        DoubleVectorSet(0.0, 1.0, 0.0, 0.0),
        DoubleVectorSet(0.0, 0.0, 1.0, 0.0),
        DoubleVectorSet(0.0, 0.0, 0.0, 1.0)
    );
}

inline DoubleMatrix MathCallConv DoubleMatrixSetColumns(
    DoubleVectorArg column0,
    DoubleVectorArg column1,
    DoubleVectorArg column2,
    DoubleVectorArg column3
)noexcept{
    return DoubleMatrix(column0, column1, column2, column3);
}

inline DoubleMatrix MathCallConv DoubleMatrixSet(
    double m00, double m01, double m02, double m03,
    double m10, double m11, double m12, double m13,
    double m20, double m21, double m22, double m23,
    double m30, double m31, double m32, double m33
)noexcept{
    return DoubleMatrixSetColumns(
        DoubleVectorSet(m00, m10, m20, m30),
        DoubleVectorSet(m01, m11, m21, m31),
        DoubleVectorSet(m02, m12, m22, m32),
        DoubleVectorSet(m03, m13, m23, m33)
    );
}

inline DoubleMatrix MathCallConv DoubleMatrixTranslation(double offsetX, double offsetY, double offsetZ)noexcept{
    return DoubleMatrixSetColumns(
        DoubleVectorSet(1.0, 0.0, 0.0, 0.0),
        DoubleVectorSet(0.0, 1.0, 0.0, 0.0),
        DoubleVectorSet(0.0, 0.0, 1.0, 0.0),
        DoubleVectorSet(offsetX, offsetY, offsetZ, 1.0)
    );
}

inline DoubleMatrix MathCallConv DoubleMatrixTranslationFromVector(DoubleVectorArg offset)noexcept{
    return DoubleMatrixTranslation(DoubleVectorGetX(offset), DoubleVectorGetY(offset), DoubleVectorGetZ(offset));
}

inline DoubleMatrix MathCallConv DoubleMatrixScaling(double scaleX, double scaleY, double scaleZ)noexcept{
    return DoubleMatrixSetColumns(
        DoubleVectorSet(scaleX, 0.0, 0.0, 0.0),
        DoubleVectorSet(0.0, scaleY, 0.0, 0.0),
        DoubleVectorSet(0.0, 0.0, scaleZ, 0.0),
        DoubleVectorSet(0.0, 0.0, 0.0, 1.0)
    );
}

inline DoubleMatrix MathCallConv DoubleMatrixScalingFromVector(DoubleVectorArg scale)noexcept{
    return DoubleMatrixScaling(DoubleVectorGetX(scale), DoubleVectorGetY(scale), DoubleVectorGetZ(scale));
}

inline DoubleMatrix MathCallConv DoubleMatrixRotationX(double angle)noexcept{
    const double sinAngle = sin(angle);
    const double cosAngle = cos(angle);
    return DoubleMatrixSetColumns(
        DoubleVectorSet(1.0, 0.0, 0.0, 0.0),
        DoubleVectorSet(0.0, cosAngle, sinAngle, 0.0),
        DoubleVectorSet(0.0, -sinAngle, cosAngle, 0.0),
        DoubleVectorSet(0.0, 0.0, 0.0, 1.0)
    );
}

inline DoubleMatrix MathCallConv DoubleMatrixRotationY(double angle)noexcept{
    const double sinAngle = sin(angle);
    const double cosAngle = cos(angle);
    return DoubleMatrixSetColumns(
        DoubleVectorSet(cosAngle, 0.0, -sinAngle, 0.0),
        DoubleVectorSet(0.0, 1.0, 0.0, 0.0),
        DoubleVectorSet(sinAngle, 0.0, cosAngle, 0.0),
        DoubleVectorSet(0.0, 0.0, 0.0, 1.0)
    );
}

inline DoubleMatrix MathCallConv DoubleMatrixRotationZ(double angle)noexcept{
    const double sinAngle = sin(angle);
    const double cosAngle = cos(angle);
    return DoubleMatrixSetColumns(
        DoubleVectorSet(cosAngle, sinAngle, 0.0, 0.0),
        DoubleVectorSet(-sinAngle, cosAngle, 0.0, 0.0),
        DoubleVectorSet(0.0, 0.0, 1.0, 0.0),
        DoubleVectorSet(0.0, 0.0, 0.0, 1.0)
    );
}

inline DoubleMatrix MathCallConv DoubleMatrixMultiply(const DoubleMatrix& M1, const DoubleMatrix& M2)noexcept{
    return DoubleMatrixSetColumns(
        DoubleVector4Transform(M2.r[0], M1),
        DoubleVector4Transform(M2.r[1], M1),
        DoubleVector4Transform(M2.r[2], M1),
        DoubleVector4Transform(M2.r[3], M1)
    );
}

inline DoubleMatrix MathCallConv DoubleMatrixMultiplyTranspose(const DoubleMatrix& M1, const DoubleMatrix& M2)noexcept{
    return DoubleMatrixTranspose(DoubleMatrixMultiply(M1, M2));
}

inline DoubleMatrix DoubleMatrix::operator-()const noexcept{
    return DoubleMatrixSetColumns(
        DoubleVectorNegate(r[0]),
        DoubleVectorNegate(r[1]),
        DoubleVectorNegate(r[2]),
        DoubleVectorNegate(r[3])
    );
}

inline DoubleMatrix& MathCallConv DoubleMatrix::operator+=(const DoubleMatrix& matrix)noexcept{
    r[0] = DoubleVectorAdd(r[0], matrix.r[0]);
    r[1] = DoubleVectorAdd(r[1], matrix.r[1]);
    r[2] = DoubleVectorAdd(r[2], matrix.r[2]);
    r[3] = DoubleVectorAdd(r[3], matrix.r[3]);
    return *this;
}

inline DoubleMatrix& MathCallConv DoubleMatrix::operator-=(const DoubleMatrix& matrix)noexcept{
    r[0] = DoubleVectorSubtract(r[0], matrix.r[0]);
    r[1] = DoubleVectorSubtract(r[1], matrix.r[1]);
    r[2] = DoubleVectorSubtract(r[2], matrix.r[2]);
    r[3] = DoubleVectorSubtract(r[3], matrix.r[3]);
    return *this;
}

inline DoubleMatrix& MathCallConv DoubleMatrix::operator*=(const DoubleMatrix& matrix)noexcept{
    *this = DoubleMatrixMultiply(*this, matrix);
    return *this;
}

inline DoubleMatrix& DoubleMatrix::operator*=(double scalar)noexcept{
    r[0] = DoubleVectorScale(r[0], scalar);
    r[1] = DoubleVectorScale(r[1], scalar);
    r[2] = DoubleVectorScale(r[2], scalar);
    r[3] = DoubleVectorScale(r[3], scalar);
    return *this;
}

inline DoubleMatrix& DoubleMatrix::operator/=(double scalar)noexcept{
    const DoubleVector scale = DoubleVectorReplicate(scalar);
    r[0] = DoubleVectorDivide(r[0], scale);
    r[1] = DoubleVectorDivide(r[1], scale);
    r[2] = DoubleVectorDivide(r[2], scale);
    r[3] = DoubleVectorDivide(r[3], scale);
    return *this;
}

inline DoubleMatrix MathCallConv DoubleMatrix::operator+(const DoubleMatrix& matrix)const noexcept{
    return DoubleMatrixSetColumns(
        DoubleVectorAdd(r[0], matrix.r[0]),
        DoubleVectorAdd(r[1], matrix.r[1]),
        DoubleVectorAdd(r[2], matrix.r[2]),
        DoubleVectorAdd(r[3], matrix.r[3])
    );
}

inline DoubleMatrix MathCallConv DoubleMatrix::operator-(const DoubleMatrix& matrix)const noexcept{
    return DoubleMatrixSetColumns(
        DoubleVectorSubtract(r[0], matrix.r[0]),
        DoubleVectorSubtract(r[1], matrix.r[1]),
        DoubleVectorSubtract(r[2], matrix.r[2]),
        DoubleVectorSubtract(r[3], matrix.r[3])
    );
}

inline DoubleMatrix MathCallConv DoubleMatrix::operator*(const DoubleMatrix& matrix)const noexcept{
    return DoubleMatrixMultiply(*this, matrix);
}

inline DoubleMatrix DoubleMatrix::operator*(double scalar)const noexcept{
    return DoubleMatrixSetColumns(
        DoubleVectorScale(r[0], scalar),
        DoubleVectorScale(r[1], scalar),
        DoubleVectorScale(r[2], scalar),
        DoubleVectorScale(r[3], scalar)
    );
}

inline DoubleMatrix DoubleMatrix::operator/(double scalar)const noexcept{
    const DoubleVector scale = DoubleVectorReplicate(scalar);
    return DoubleMatrixSetColumns(
        DoubleVectorDivide(r[0], scale),
        DoubleVectorDivide(r[1], scale),
        DoubleVectorDivide(r[2], scale),
        DoubleVectorDivide(r[3], scale)
    );
}

inline DoubleMatrix MathCallConv operator*(double scalar, const DoubleMatrix& matrix)noexcept{
    return matrix * scalar;
}

inline DoubleMatrix MathCallConv DoubleMatrixRotationQuaternion(DoubleVectorArg quaternion)noexcept{
    const double qx = DoubleVectorGetX(quaternion);
    const double qy = DoubleVectorGetY(quaternion);
    const double qz = DoubleVectorGetZ(quaternion);
    const double qw = DoubleVectorGetW(quaternion);
    const double twoQx = qx + qx;
    const double twoQy = qy + qy;
    const double twoQz = qz + qz;

    return DoubleMatrixSetColumns(
        DoubleVectorSet(
            1.0 - qy * twoQy - qz * twoQz,
            qx * twoQy + qz * (qw + qw),
            qx * twoQz - qy * (qw + qw),
            0.0
        ),
        DoubleVectorSet(
            qx * twoQy - qz * (qw + qw),
            1.0 - qx * twoQx - qz * twoQz,
            qy * twoQz + qx * (qw + qw),
            0.0
        ),
        DoubleVectorSet(
            qx * twoQz + qy * (qw + qw),
            qy * twoQz - qx * (qw + qw),
            1.0 - qx * twoQx - qy * twoQy,
            0.0
        ),
        DoubleVectorSet(0.0, 0.0, 0.0, 1.0)
    );
}

inline DoubleMatrix MathCallConv DoubleMatrixRotationRollPitchYaw(
    double pitch,
    double yaw,
    double roll
)noexcept{
    return DoubleMatrixRotationQuaternion(DoubleQuaternionRotationRollPitchYaw(pitch, yaw, roll));
}

inline DoubleMatrix MathCallConv DoubleMatrixRotationRollPitchYawFromVector(DoubleVectorArg angles)noexcept{
    return DoubleMatrixRotationRollPitchYaw(
        DoubleVectorGetX(angles),
        DoubleVectorGetY(angles),
        DoubleVectorGetZ(angles)
    );
}

inline DoubleMatrix MathCallConv DoubleMatrixRotationNormal(DoubleVectorArg normalAxis, double angle)noexcept{
    const double x = DoubleVectorGetX(normalAxis);
    const double y = DoubleVectorGetY(normalAxis);
    const double z = DoubleVectorGetZ(normalAxis);
    const double sinAngle = sin(angle);
    const double cosAngle = cos(angle);
    const double oneMinusCos = 1.0 - cosAngle;

    return DoubleMatrixSetColumns(
        DoubleVectorSet(
            cosAngle + oneMinusCos * x * x,
            oneMinusCos * x * y + sinAngle * z,
            oneMinusCos * x * z - sinAngle * y,
            0.0
        ),
        DoubleVectorSet(
            oneMinusCos * x * y - sinAngle * z,
            cosAngle + oneMinusCos * y * y,
            oneMinusCos * y * z + sinAngle * x,
            0.0
        ),
        DoubleVectorSet(
            oneMinusCos * x * z + sinAngle * y,
            oneMinusCos * y * z - sinAngle * x,
            cosAngle + oneMinusCos * z * z,
            0.0
        ),
        DoubleVectorSet(0.0, 0.0, 0.0, 1.0)
    );
}

inline DoubleMatrix MathCallConv DoubleMatrixRotationAxis(DoubleVectorArg axis, double angle)noexcept{
    return DoubleMatrixRotationNormal(DoubleVector3Normalize(axis), angle);
}

inline DoubleMatrix MathCallConv DoubleMatrixTransformation2D(
    DoubleVectorArg scalingOrigin,
    double scalingOrientation,
    DoubleVectorArg scaling,
    DoubleVectorArg rotationOrigin,
    double rotation,
    DoubleVectorArg translation
)noexcept{
    const DoubleVector scalingOrigin2D = DoubleVectorSet(
        DoubleVectorGetX(scalingOrigin),
        DoubleVectorGetY(scalingOrigin),
        0.0,
        0.0
    );
    const DoubleVector rotationOrigin2D = DoubleVectorSet(
        DoubleVectorGetX(rotationOrigin),
        DoubleVectorGetY(rotationOrigin),
        0.0,
        0.0
    );
    const DoubleVector scaling2D = DoubleVectorSet(DoubleVectorGetX(scaling), DoubleVectorGetY(scaling), 1.0, 0.0);
    const DoubleVector translation2D = DoubleVectorSet(DoubleVectorGetX(translation), DoubleVectorGetY(translation), 0.0, 0.0);

    DoubleMatrix M = DoubleMatrixMultiply(
        DoubleMatrixRotationZ(-scalingOrientation),
        DoubleMatrixTranslationFromVector(DoubleVectorNegate(scalingOrigin2D))
    );
    M = DoubleMatrixMultiply(DoubleMatrixScalingFromVector(scaling2D), M);
    M = DoubleMatrixMultiply(DoubleMatrixRotationZ(scalingOrientation), M);
    M.r[3] = DoubleVectorAdd(M.r[3], scalingOrigin2D);
    M.r[3] = DoubleVectorSubtract(M.r[3], rotationOrigin2D);
    M = DoubleMatrixMultiply(DoubleMatrixRotationZ(rotation), M);
    M.r[3] = DoubleVectorAdd(M.r[3], rotationOrigin2D);
    M.r[3] = DoubleVectorAdd(M.r[3], translation2D);
    return M;
}

inline DoubleMatrix MathCallConv DoubleMatrixTransformation(
    DoubleVectorArg scalingOrigin,
    DoubleVectorArg scalingOrientationQuaternion,
    DoubleVectorArg scaling,
    DoubleVectorArg rotationOrigin,
    DoubleVectorArg rotationQuaternion,
    DoubleVectorArg translation
)noexcept{
    const DoubleVector scalingOrigin3D = DoubleVectorSet(
        DoubleVectorGetX(scalingOrigin),
        DoubleVectorGetY(scalingOrigin),
        DoubleVectorGetZ(scalingOrigin),
        0.0
    );
    const DoubleVector rotationOrigin3D = DoubleVectorSet(
        DoubleVectorGetX(rotationOrigin),
        DoubleVectorGetY(rotationOrigin),
        DoubleVectorGetZ(rotationOrigin),
        0.0
    );
    const DoubleVector scaling3D = DoubleVectorSet(
        DoubleVectorGetX(scaling),
        DoubleVectorGetY(scaling),
        DoubleVectorGetZ(scaling),
        0.0
    );
    const DoubleVector translation3D = DoubleVectorSet(
        DoubleVectorGetX(translation),
        DoubleVectorGetY(translation),
        DoubleVectorGetZ(translation),
        0.0
    );

    DoubleMatrix M = DoubleMatrixMultiply(
        DoubleMatrixRotationQuaternion(DoubleQuaternionConjugate(scalingOrientationQuaternion)),
        DoubleMatrixTranslationFromVector(DoubleVectorNegate(scalingOrigin3D))
    );
    M = DoubleMatrixMultiply(DoubleMatrixScalingFromVector(scaling3D), M);
    M = DoubleMatrixMultiply(DoubleMatrixRotationQuaternion(scalingOrientationQuaternion), M);
    M.r[3] = DoubleVectorAdd(M.r[3], scalingOrigin3D);
    M.r[3] = DoubleVectorSubtract(M.r[3], rotationOrigin3D);
    M = DoubleMatrixMultiply(DoubleMatrixRotationQuaternion(rotationQuaternion), M);
    M.r[3] = DoubleVectorAdd(M.r[3], rotationOrigin3D);
    M.r[3] = DoubleVectorAdd(M.r[3], translation3D);
    return M;
}

inline DoubleMatrix MathCallConv DoubleMatrixAffineTransformation2D(
    DoubleVectorArg scaling,
    DoubleVectorArg rotationOrigin,
    double rotation,
    DoubleVectorArg translation
)noexcept{
    const DoubleVector scaling2D = DoubleVectorSet(DoubleVectorGetX(scaling), DoubleVectorGetY(scaling), 1.0, 0.0);
    const DoubleVector rotationOrigin2D = DoubleVectorSet(
        DoubleVectorGetX(rotationOrigin),
        DoubleVectorGetY(rotationOrigin),
        0.0,
        0.0
    );
    const DoubleVector translation2D = DoubleVectorSet(DoubleVectorGetX(translation), DoubleVectorGetY(translation), 0.0, 0.0);

    DoubleMatrix M = DoubleMatrixScalingFromVector(scaling2D);
    M.r[3] = DoubleVectorSubtract(M.r[3], rotationOrigin2D);
    M = DoubleMatrixMultiply(DoubleMatrixRotationZ(rotation), M);
    M.r[3] = DoubleVectorAdd(M.r[3], rotationOrigin2D);
    M.r[3] = DoubleVectorAdd(M.r[3], translation2D);
    return M;
}

inline DoubleMatrix MathCallConv DoubleMatrixAffineTransformation(
    DoubleVectorArg scaling,
    DoubleVectorArg rotationOrigin,
    DoubleVectorArg rotationQuaternion,
    DoubleVectorArg translation
)noexcept{
    const DoubleVector scaling3D = DoubleVectorSet(
        DoubleVectorGetX(scaling),
        DoubleVectorGetY(scaling),
        DoubleVectorGetZ(scaling),
        0.0
    );
    const DoubleVector rotationOrigin3D = DoubleVectorSet(
        DoubleVectorGetX(rotationOrigin),
        DoubleVectorGetY(rotationOrigin),
        DoubleVectorGetZ(rotationOrigin),
        0.0
    );
    const DoubleVector translation3D = DoubleVectorSet(
        DoubleVectorGetX(translation),
        DoubleVectorGetY(translation),
        DoubleVectorGetZ(translation),
        0.0
    );

    DoubleMatrix M = DoubleMatrixScalingFromVector(scaling3D);
    M.r[3] = DoubleVectorSubtract(M.r[3], rotationOrigin3D);
    M = DoubleMatrixMultiply(DoubleMatrixRotationQuaternion(rotationQuaternion), M);
    M.r[3] = DoubleVectorAdd(M.r[3], rotationOrigin3D);
    M.r[3] = DoubleVectorAdd(M.r[3], translation3D);
    return M;
}

inline DoubleMatrix MathCallConv DoubleMatrixTranspose(const DoubleMatrix& M)noexcept{
    Double4 c0{};
    Double4 c1{};
    Double4 c2{};
    Double4 c3{};
    StoreDouble4(&c0, M.r[0]);
    StoreDouble4(&c1, M.r[1]);
    StoreDouble4(&c2, M.r[2]);
    StoreDouble4(&c3, M.r[3]);
    return DoubleMatrixSet(
        c0.x, c0.y, c0.z, c0.w,
        c1.x, c1.y, c1.z, c1.w,
        c2.x, c2.y, c2.z, c2.w,
        c3.x, c3.y, c3.z, c3.w
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void DoubleMatrixRankDecompose(size_t& a, size_t& b, size_t& c, double x, double y, double z)noexcept{
    if(x < y){
        if(y < z){
            a = 2;
            b = 1;
            c = 0;
        }
        else{
            a = 1;
            if(x < z){
                b = 2;
                c = 0;
            }
            else{
                b = 0;
                c = 2;
            }
        }
    }
    else{
        if(x < z){
            a = 2;
            b = 0;
            c = 1;
        }
        else{
            a = 0;
            if(y < z){
                b = 2;
                c = 1;
            }
            else{
                b = 1;
                c = 2;
            }
        }
    }
}

inline void DoubleMatrixStoreRows(double rows[4][4], const DoubleMatrix& matrix)noexcept{
    Double4 columns[4]{};
    StoreDouble4(&columns[0], matrix.r[0]);
    StoreDouble4(&columns[1], matrix.r[1]);
    StoreDouble4(&columns[2], matrix.r[2]);
    StoreDouble4(&columns[3], matrix.r[3]);

    for(size_t column = 0; column < 4; ++column){
        rows[0][column] = columns[column].x;
        rows[1][column] = columns[column].y;
        rows[2][column] = columns[column].z;
        rows[3][column] = columns[column].w;
    }
}

inline double DoubleMatrixDeterminantRows(const double source[4][4])noexcept{
    double work[4][4]{};
    for(size_t row = 0; row < 4; ++row){
        for(size_t column = 0; column < 4; ++column)
            work[row][column] = source[row][column];
    }

    double determinant = 1.0;
    for(size_t column = 0; column < 4; ++column){
        size_t pivotRow = column;
        double pivotAbs = fabs(work[column][column]);
        for(size_t row = column + 1; row < 4; ++row){
            const double candidateAbs = fabs(work[row][column]);
            if(candidateAbs > pivotAbs){
                pivotAbs = candidateAbs;
                pivotRow = row;
            }
        }

        if(pivotAbs <= 0.0)
            return 0.0;

        if(pivotRow != column){
            for(size_t swapColumn = column; swapColumn < 4; ++swapColumn){
                const double temp = work[column][swapColumn];
                work[column][swapColumn] = work[pivotRow][swapColumn];
                work[pivotRow][swapColumn] = temp;
            }
            determinant = -determinant;
        }

        const double pivot = work[column][column];
        determinant *= pivot;
        for(size_t row = column + 1; row < 4; ++row){
            const double factor = work[row][column] / pivot;
            for(size_t updateColumn = column + 1; updateColumn < 4; ++updateColumn)
                work[row][updateColumn] -= factor * work[column][updateColumn];
        }
    }

    return determinant;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline bool MathCallConv DoubleMatrixIsNaN(const DoubleMatrix& M)noexcept{
    double rows[4][4]{};
    DoubleMatrixStoreRows(rows, M);
    for(size_t row = 0; row < 4; ++row){
        for(size_t column = 0; column < 4; ++column){
            if(isnan(rows[row][column]))
                return true;
        }
    }

    return false;
}

inline bool MathCallConv DoubleMatrixIsInfinite(const DoubleMatrix& M)noexcept{
    double rows[4][4]{};
    DoubleMatrixStoreRows(rows, M);
    for(size_t row = 0; row < 4; ++row){
        for(size_t column = 0; column < 4; ++column){
            if(isinf(rows[row][column]))
                return true;
        }
    }

    return false;
}

inline bool MathCallConv DoubleMatrixIsIdentity(const DoubleMatrix& M)noexcept{
    double rows[4][4]{};
    DoubleMatrixStoreRows(rows, M);
    for(size_t row = 0; row < 4; ++row){
        for(size_t column = 0; column < 4; ++column){
            const double expected = (row == column) ? 1.0 : 0.0;
            if(rows[row][column] != expected)
                return false;
        }
    }

    return true;
}

inline DoubleVector MathCallConv DoubleMatrixDeterminant(const DoubleMatrix& M)noexcept{
    double rows[4][4]{};
    DoubleMatrixStoreRows(rows, M);
    return DoubleVectorReplicate(DoubleMatrixDeterminantRows(rows));
}

inline DoubleMatrix MathCallConv DoubleMatrixInverse(DoubleVector* pDeterminant, const DoubleMatrix& M)noexcept{
    double work[4][8]{};
    double rows[4][4]{};
    DoubleMatrixStoreRows(rows, M);

    for(size_t row = 0; row < 4; ++row){
        for(size_t column = 0; column < 4; ++column)
            work[row][column] = rows[row][column];

        for(size_t column = 0; column < 4; ++column)
            work[row][column + 4] = (row == column) ? 1.0 : 0.0;
    }

    double determinant = 1.0;
    for(size_t column = 0; column < 4; ++column){
        size_t pivotRow = column;
        double pivotAbs = fabs(work[column][column]);
        for(size_t row = column + 1; row < 4; ++row){
            const double candidateAbs = fabs(work[row][column]);
            if(candidateAbs > pivotAbs){
                pivotAbs = candidateAbs;
                pivotRow = row;
            }
        }

        if(pivotAbs <= 0.0){
            if(pDeterminant != nullptr)
                *pDeterminant = DoubleVectorZero();

            return DoubleMatrixZero();
        }

        if(pivotRow != column){
            for(size_t swapColumn = 0; swapColumn < 8; ++swapColumn){
                const double temp = work[column][swapColumn];
                work[column][swapColumn] = work[pivotRow][swapColumn];
                work[pivotRow][swapColumn] = temp;
            }
            determinant = -determinant;
        }

        const double pivot = work[column][column];
        determinant *= pivot;
        const double inversePivot = 1.0 / pivot;
        for(size_t updateColumn = 0; updateColumn < 8; ++updateColumn)
            work[column][updateColumn] *= inversePivot;

        for(size_t row = 0; row < 4; ++row){
            if(row == column)
                continue;

            const double factor = work[row][column];
            if(factor == 0.0)
                continue;

            for(size_t updateColumn = 0; updateColumn < 8; ++updateColumn)
                work[row][updateColumn] -= factor * work[column][updateColumn];
        }
    }

    if(pDeterminant != nullptr)
        *pDeterminant = DoubleVectorReplicate(determinant);

    return DoubleMatrixSet(
        work[0][4], work[0][5], work[0][6], work[0][7],
        work[1][4], work[1][5], work[1][6], work[1][7],
        work[2][4], work[2][5], work[2][6], work[2][7],
        work[3][4], work[3][5], work[3][6], work[3][7]
    );
}

inline DoubleMatrix MathCallConv DoubleMatrixVectorTensorProduct(DoubleVectorArg V1, DoubleVectorArg V2)noexcept{
    return DoubleMatrixSetColumns(
        DoubleVectorScale(V1, DoubleVectorGetX(V2)),
        DoubleVectorScale(V1, DoubleVectorGetY(V2)),
        DoubleVectorScale(V1, DoubleVectorGetZ(V2)),
        DoubleVectorScale(V1, DoubleVectorGetW(V2))
    );
}

inline DoubleVector MathCallConv DoubleQuaternionRotationMatrix(const DoubleMatrix& M)noexcept{
    double rows[4][4]{};
    DoubleMatrixStoreRows(rows, M);

    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double w = 1.0;
    const double r22 = rows[2][2];
    if(r22 <= 0.0){
        const double difference10 = rows[1][1] - rows[0][0];
        const double oneMinusR22 = 1.0 - r22;
        if(difference10 <= 0.0){
            const double fourXSqr = oneMinusR22 - difference10;
            const double inv4X = 0.5 / sqrt(fourXSqr);
            x = fourXSqr * inv4X;
            y = (rows[0][1] + rows[1][0]) * inv4X;
            z = (rows[0][2] + rows[2][0]) * inv4X;
            w = (rows[2][1] - rows[1][2]) * inv4X;
        }
        else{
            const double fourYSqr = oneMinusR22 + difference10;
            const double inv4Y = 0.5 / sqrt(fourYSqr);
            x = (rows[0][1] + rows[1][0]) * inv4Y;
            y = fourYSqr * inv4Y;
            z = (rows[1][2] + rows[2][1]) * inv4Y;
            w = (rows[0][2] - rows[2][0]) * inv4Y;
        }
    }
    else{
        const double sum10 = rows[1][1] + rows[0][0];
        const double onePlusR22 = 1.0 + r22;
        if(sum10 <= 0.0){
            const double fourZSqr = onePlusR22 - sum10;
            const double inv4Z = 0.5 / sqrt(fourZSqr);
            x = (rows[0][2] + rows[2][0]) * inv4Z;
            y = (rows[1][2] + rows[2][1]) * inv4Z;
            z = fourZSqr * inv4Z;
            w = (rows[1][0] - rows[0][1]) * inv4Z;
        }
        else{
            const double fourWSqr = onePlusR22 + sum10;
            const double inv4W = 0.5 / sqrt(fourWSqr);
            x = (rows[2][1] - rows[1][2]) * inv4W;
            y = (rows[0][2] - rows[2][0]) * inv4W;
            z = (rows[1][0] - rows[0][1]) * inv4W;
            w = fourWSqr * inv4W;
        }
    }

    return DoubleVectorSet(x, y, z, w);
}

_Use_decl_annotations_
inline bool MathCallConv DoubleMatrixDecompose(
    DoubleVector* outScale,
    DoubleVector* outRotQuat,
    DoubleVector* outTrans,
    const DoubleMatrix& M
)noexcept{
    static const DoubleVector canonicalBasis[3] = {
        DoubleVectorSet(1.0, 0.0, 0.0, 0.0),
        DoubleVectorSet(0.0, 1.0, 0.0, 0.0),
        DoubleVectorSet(0.0, 0.0, 1.0, 0.0)
    };
    static constexpr double s_DecomposeEpsilon = 1.0e-10;

    assert(outScale != nullptr);
    assert(outRotQuat != nullptr);
    assert(outTrans != nullptr);

    *outTrans = M.r[3];

    DoubleVector basis[3] = {
        M.r[0],
        M.r[1],
        M.r[2]
    };
    double scales[3] = {
        DoubleVectorGetX(DoubleVector3Length(basis[0])),
        DoubleVectorGetX(DoubleVector3Length(basis[1])),
        DoubleVectorGetX(DoubleVector3Length(basis[2]))
    };

    size_t a = 0;
    size_t b = 1;
    size_t c = 2;
    DoubleMatrixRankDecompose(a, b, c, scales[0], scales[1], scales[2]);

    if(scales[a] < s_DecomposeEpsilon)
        basis[a] = canonicalBasis[a];
    basis[a] = DoubleVector3Normalize(basis[a]);

    if(scales[b] < s_DecomposeEpsilon){
        size_t aa = 0;
        size_t bb = 1;
        size_t cc = 2;
        DoubleMatrixRankDecompose(
            aa,
            bb,
            cc,
            fabs(DoubleVectorGetX(basis[a])),
            fabs(DoubleVectorGetY(basis[a])),
            fabs(DoubleVectorGetZ(basis[a]))
        );
        basis[b] = DoubleVector3Cross(basis[a], canonicalBasis[cc]);
    }
    basis[b] = DoubleVector3Normalize(basis[b]);

    if(scales[c] < s_DecomposeEpsilon)
        basis[c] = DoubleVector3Cross(basis[a], basis[b]);
    basis[c] = DoubleVector3Normalize(basis[c]);

    DoubleMatrix rotation = DoubleMatrixSetColumns(
        basis[0],
        basis[1],
        basis[2],
        DoubleVectorSet(0.0, 0.0, 0.0, 1.0)
    );

    double determinant = DoubleVectorGetX(DoubleMatrixDeterminant(rotation));
    if(determinant < 0.0){
        scales[a] = -scales[a];
        basis[a] = DoubleVectorNegate(basis[a]);
        rotation.r[a] = basis[a];
        determinant = -determinant;
    }

    determinant -= 1.0;
    determinant *= determinant;
    if(s_DecomposeEpsilon < determinant)
        return false;

    *outScale = DoubleVectorSet(scales[0], scales[1], scales[2], 0.0);
    *outRotQuat = DoubleQuaternionRotationMatrix(rotation);
    return true;
}

inline DoubleMatrix MathCallConv DoubleMatrixReflect(DoubleVectorArg reflectionPlane)noexcept{
    const DoubleVector plane = DoublePlaneNormalize(reflectionPlane);
    const DoubleVector scale = DoubleVectorSet(
        -2.0 * DoubleVectorGetX(plane),
        -2.0 * DoubleVectorGetY(plane),
        -2.0 * DoubleVectorGetZ(plane),
        0.0
    );

    return DoubleMatrixSetColumns(
        DoubleVectorMultiplyAdd(DoubleVectorSplatX(plane), scale, DoubleVectorSet(1.0, 0.0, 0.0, 0.0)),
        DoubleVectorMultiplyAdd(DoubleVectorSplatY(plane), scale, DoubleVectorSet(0.0, 1.0, 0.0, 0.0)),
        DoubleVectorMultiplyAdd(DoubleVectorSplatZ(plane), scale, DoubleVectorSet(0.0, 0.0, 1.0, 0.0)),
        DoubleVectorMultiplyAdd(DoubleVectorSplatW(plane), scale, DoubleVectorSet(0.0, 0.0, 0.0, 1.0))
    );
}

inline DoubleMatrix MathCallConv DoubleMatrixShadow(DoubleVectorArg shadowPlane, DoubleVectorArg lightPosition)noexcept{
    const DoubleVector plane = DoublePlaneNormalize(shadowPlane);
    const DoubleVector dot = DoublePlaneDot(plane, lightPosition);

    return DoubleMatrixSetColumns(
        DoubleVectorSubtract(
            DoubleVectorMultiply(dot, DoubleVectorSet(1.0, 0.0, 0.0, 0.0)),
            DoubleVectorScale(lightPosition, DoubleVectorGetX(plane))
        ),
        DoubleVectorSubtract(
            DoubleVectorMultiply(dot, DoubleVectorSet(0.0, 1.0, 0.0, 0.0)),
            DoubleVectorScale(lightPosition, DoubleVectorGetY(plane))
        ),
        DoubleVectorSubtract(
            DoubleVectorMultiply(dot, DoubleVectorSet(0.0, 0.0, 1.0, 0.0)),
            DoubleVectorScale(lightPosition, DoubleVectorGetZ(plane))
        ),
        DoubleVectorSubtract(
            DoubleVectorMultiply(dot, DoubleVectorSet(0.0, 0.0, 0.0, 1.0)),
            DoubleVectorScale(lightPosition, DoubleVectorGetW(plane))
        )
    );
}

inline DoubleVector MathCallConv DoublePlaneTransform(
    DoubleVectorArg plane,
    const DoubleMatrix& inverseTransposeMatrix
)noexcept{
    return DoubleVector4Transform(plane, inverseTransposeMatrix);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline DoubleMatrix MathCallConv DoubleMatrixLookToLH(
    DoubleVectorArg eyePosition,
    DoubleVectorArg eyeDirection,
    DoubleVectorArg upDirection
)noexcept{
    const DoubleVector forward = DoubleVector3Normalize(eyeDirection);
    const DoubleVector right = DoubleVector3Normalize(DoubleVector3Cross(upDirection, forward));
    const DoubleVector up = DoubleVector3Cross(forward, right);
    const DoubleVector negEye = DoubleVectorNegate(eyePosition);

    return DoubleMatrixSetColumns(
        DoubleVectorSet(DoubleVectorGetX(right), DoubleVectorGetX(up), DoubleVectorGetX(forward), 0.0),
        DoubleVectorSet(DoubleVectorGetY(right), DoubleVectorGetY(up), DoubleVectorGetY(forward), 0.0),
        DoubleVectorSet(DoubleVectorGetZ(right), DoubleVectorGetZ(up), DoubleVectorGetZ(forward), 0.0),
        DoubleVectorSet(
            DoubleVectorGetX(DoubleVector3Dot(right, negEye)),
            DoubleVectorGetX(DoubleVector3Dot(up, negEye)),
            DoubleVectorGetX(DoubleVector3Dot(forward, negEye)),
            1.0
        )
    );
}

inline DoubleMatrix MathCallConv DoubleMatrixLookToRH(
    DoubleVectorArg eyePosition,
    DoubleVectorArg eyeDirection,
    DoubleVectorArg upDirection
)noexcept{
    return DoubleMatrixLookToLH(eyePosition, DoubleVectorNegate(eyeDirection), upDirection);
}

inline DoubleMatrix MathCallConv DoubleMatrixLookAtLH(
    DoubleVectorArg eyePosition,
    DoubleVectorArg focusPosition,
    DoubleVectorArg upDirection
)noexcept{
    return DoubleMatrixLookToLH(eyePosition, DoubleVectorSubtract(focusPosition, eyePosition), upDirection);
}

inline DoubleMatrix MathCallConv DoubleMatrixLookAtRH(
    DoubleVectorArg eyePosition,
    DoubleVectorArg focusPosition,
    DoubleVectorArg upDirection
)noexcept{
    return DoubleMatrixLookToRH(eyePosition, DoubleVectorSubtract(focusPosition, eyePosition), upDirection);
}

inline DoubleMatrix MathCallConv DoubleMatrixPerspectiveLH(
    double viewWidth,
    double viewHeight,
    double nearZ,
    double farZ
)noexcept{
    const double twoNearZ = nearZ + nearZ;
    const double range = farZ / (farZ - nearZ);
    return DoubleMatrixSetColumns(
        DoubleVectorSet(twoNearZ / viewWidth, 0.0, 0.0, 0.0),
        DoubleVectorSet(0.0, twoNearZ / viewHeight, 0.0, 0.0),
        DoubleVectorSet(0.0, 0.0, range, 1.0),
        DoubleVectorSet(0.0, 0.0, -range * nearZ, 0.0)
    );
}

inline DoubleMatrix MathCallConv DoubleMatrixPerspectiveRH(
    double viewWidth,
    double viewHeight,
    double nearZ,
    double farZ
)noexcept{
    const double twoNearZ = nearZ + nearZ;
    const double range = farZ / (nearZ - farZ);
    return DoubleMatrixSetColumns(
        DoubleVectorSet(twoNearZ / viewWidth, 0.0, 0.0, 0.0),
        DoubleVectorSet(0.0, twoNearZ / viewHeight, 0.0, 0.0),
        DoubleVectorSet(0.0, 0.0, range, -1.0),
        DoubleVectorSet(0.0, 0.0, range * nearZ, 0.0)
    );
}

inline DoubleMatrix MathCallConv DoubleMatrixPerspectiveOffCenterLH(
    double viewLeft,
    double viewRight,
    double viewBottom,
    double viewTop,
    double nearZ,
    double farZ
)noexcept{
    const double twoNearZ = nearZ + nearZ;
    const double reciprocalWidth = 1.0 / (viewRight - viewLeft);
    const double reciprocalHeight = 1.0 / (viewTop - viewBottom);
    const double range = farZ / (farZ - nearZ);
    return DoubleMatrixSetColumns(
        DoubleVectorSet(twoNearZ * reciprocalWidth, 0.0, 0.0, 0.0),
        DoubleVectorSet(0.0, twoNearZ * reciprocalHeight, 0.0, 0.0),
        DoubleVectorSet(-(viewLeft + viewRight) * reciprocalWidth, -(viewTop + viewBottom) * reciprocalHeight, range, 1.0),
        DoubleVectorSet(0.0, 0.0, -range * nearZ, 0.0)
    );
}

inline DoubleMatrix MathCallConv DoubleMatrixPerspectiveOffCenterRH(
    double viewLeft,
    double viewRight,
    double viewBottom,
    double viewTop,
    double nearZ,
    double farZ
)noexcept{
    const double twoNearZ = nearZ + nearZ;
    const double reciprocalWidth = 1.0 / (viewRight - viewLeft);
    const double reciprocalHeight = 1.0 / (viewTop - viewBottom);
    const double range = farZ / (nearZ - farZ);
    return DoubleMatrixSetColumns(
        DoubleVectorSet(twoNearZ * reciprocalWidth, 0.0, 0.0, 0.0),
        DoubleVectorSet(0.0, twoNearZ * reciprocalHeight, 0.0, 0.0),
        DoubleVectorSet((viewLeft + viewRight) * reciprocalWidth, (viewTop + viewBottom) * reciprocalHeight, range, -1.0),
        DoubleVectorSet(0.0, 0.0, range * nearZ, 0.0)
    );
}

inline DoubleMatrix MathCallConv DoubleMatrixPerspectiveFovLH(
    double fovAngleY,
    double aspectRatio,
    double nearZ,
    double farZ
)noexcept{
    const double height = cos(0.5 * fovAngleY) / sin(0.5 * fovAngleY);
    const double width = height / aspectRatio;
    const double range = farZ / (farZ - nearZ);
    return DoubleMatrixSetColumns(
        DoubleVectorSet(width, 0.0, 0.0, 0.0),
        DoubleVectorSet(0.0, height, 0.0, 0.0),
        DoubleVectorSet(0.0, 0.0, range, 1.0),
        DoubleVectorSet(0.0, 0.0, -range * nearZ, 0.0)
    );
}

inline DoubleMatrix MathCallConv DoubleMatrixPerspectiveFovRH(
    double fovAngleY,
    double aspectRatio,
    double nearZ,
    double farZ
)noexcept{
    const double height = cos(0.5 * fovAngleY) / sin(0.5 * fovAngleY);
    const double width = height / aspectRatio;
    const double range = farZ / (nearZ - farZ);
    return DoubleMatrixSetColumns(
        DoubleVectorSet(width, 0.0, 0.0, 0.0),
        DoubleVectorSet(0.0, height, 0.0, 0.0),
        DoubleVectorSet(0.0, 0.0, range, -1.0),
        DoubleVectorSet(0.0, 0.0, range * nearZ, 0.0)
    );
}

inline DoubleMatrix MathCallConv DoubleMatrixOrthographicLH(
    double viewWidth,
    double viewHeight,
    double nearZ,
    double farZ
)noexcept{
    const double range = 1.0 / (farZ - nearZ);
    return DoubleMatrixSetColumns(
        DoubleVectorSet(2.0 / viewWidth, 0.0, 0.0, 0.0),
        DoubleVectorSet(0.0, 2.0 / viewHeight, 0.0, 0.0),
        DoubleVectorSet(0.0, 0.0, range, 0.0),
        DoubleVectorSet(0.0, 0.0, -range * nearZ, 1.0)
    );
}

inline DoubleMatrix MathCallConv DoubleMatrixOrthographicRH(
    double viewWidth,
    double viewHeight,
    double nearZ,
    double farZ
)noexcept{
    const double range = 1.0 / (nearZ - farZ);
    return DoubleMatrixSetColumns(
        DoubleVectorSet(2.0 / viewWidth, 0.0, 0.0, 0.0),
        DoubleVectorSet(0.0, 2.0 / viewHeight, 0.0, 0.0),
        DoubleVectorSet(0.0, 0.0, range, 0.0),
        DoubleVectorSet(0.0, 0.0, range * nearZ, 1.0)
    );
}

inline DoubleMatrix MathCallConv DoubleMatrixOrthographicOffCenterLH(
    double viewLeft,
    double viewRight,
    double viewBottom,
    double viewTop,
    double nearZ,
    double farZ
)noexcept{
    const double reciprocalWidth = 1.0 / (viewRight - viewLeft);
    const double reciprocalHeight = 1.0 / (viewTop - viewBottom);
    const double range = 1.0 / (farZ - nearZ);
    return DoubleMatrixSetColumns(
        DoubleVectorSet(reciprocalWidth + reciprocalWidth, 0.0, 0.0, 0.0),
        DoubleVectorSet(0.0, reciprocalHeight + reciprocalHeight, 0.0, 0.0),
        DoubleVectorSet(0.0, 0.0, range, 0.0),
        DoubleVectorSet(
            -(viewLeft + viewRight) * reciprocalWidth,
            -(viewTop + viewBottom) * reciprocalHeight,
            -range * nearZ,
            1.0
        )
    );
}

inline DoubleMatrix MathCallConv DoubleMatrixOrthographicOffCenterRH(
    double viewLeft,
    double viewRight,
    double viewBottom,
    double viewTop,
    double nearZ,
    double farZ
)noexcept{
    const double reciprocalWidth = 1.0 / (viewRight - viewLeft);
    const double reciprocalHeight = 1.0 / (viewTop - viewBottom);
    const double range = 1.0 / (nearZ - farZ);
    return DoubleMatrixSetColumns(
        DoubleVectorSet(reciprocalWidth + reciprocalWidth, 0.0, 0.0, 0.0),
        DoubleVectorSet(0.0, reciprocalHeight + reciprocalHeight, 0.0, 0.0),
        DoubleVectorSet(0.0, 0.0, range, 0.0),
        DoubleVectorSet(
            -(viewLeft + viewRight) * reciprocalWidth,
            -(viewTop + viewBottom) * reciprocalHeight,
            range * nearZ,
            1.0
        )
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline DoubleMatrix MathCallConv BuildDoubleProjectTransformColumns(
    const DoubleMatrix& projection,
    const DoubleMatrix& view,
    const DoubleMatrix& world
)noexcept{
    DoubleMatrix transform = DoubleMatrixMultiply(projection, view);
    transform = DoubleMatrixMultiply(transform, world);
    return transform;
}

inline DoubleVector MathCallConv DoubleVector3Project(
    DoubleVectorArg V,
    double viewportX,
    double viewportY,
    double viewportWidth,
    double viewportHeight,
    double viewportMinZ,
    double viewportMaxZ,
    const DoubleMatrix& projection,
    const DoubleMatrix& view,
    const DoubleMatrix& world
)noexcept{
    const double halfViewportWidth = viewportWidth * 0.5;
    const double halfViewportHeight = viewportHeight * 0.5;

    const DoubleVector scale = DoubleVectorSet(halfViewportWidth, -halfViewportHeight, viewportMaxZ - viewportMinZ, 0.0);
    const DoubleVector offset = DoubleVectorSet(
        viewportX + halfViewportWidth,
        viewportY + halfViewportHeight,
        viewportMinZ,
        0.0
    );
    const DoubleMatrix transform = BuildDoubleProjectTransformColumns(projection, view, world);

    DoubleVector result = DoubleVector3TransformCoord(V, transform);
    result = DoubleVectorMultiplyAdd(result, scale, offset);
    return result;
}

inline DoubleVector MathCallConv DoubleVector3Unproject(
    DoubleVectorArg V,
    double viewportX,
    double viewportY,
    double viewportWidth,
    double viewportHeight,
    double viewportMinZ,
    double viewportMaxZ,
    const DoubleMatrix& projection,
    const DoubleMatrix& view,
    const DoubleMatrix& world
)noexcept{
    const DoubleVector one = DoubleVectorReplicate(1.0);
    const DoubleVector d = DoubleVectorSet(-1.0, 1.0, 0.0, 0.0);
    DoubleVector scale = DoubleVectorSet(viewportWidth * 0.5, -viewportHeight * 0.5, viewportMaxZ - viewportMinZ, 1.0);
    scale = DoubleVectorDivide(one, scale);

    DoubleVector offset = DoubleVectorSet(-viewportX, -viewportY, -viewportMinZ, 0.0);
    offset = DoubleVectorMultiplyAdd(scale, offset, d);

    const DoubleMatrix transform = DoubleMatrixInverse(nullptr, BuildDoubleProjectTransformColumns(projection, view, world));
    const DoubleVector result = DoubleVectorMultiplyAdd(V, scale, offset);
    return DoubleVector3TransformCoord(result, transform);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


_Use_decl_annotations_
inline Double4* MathCallConv DoubleVector2TransformStream(
    Double4* pOutputStream,
    size_t OutputStride,
    const Double2* pInputStream,
    size_t InputStride,
    size_t VectorCount,
    const DoubleMatrix& M
)noexcept{
    const uint8_t* pInputVector = reinterpret_cast<const uint8_t*>(pInputStream);
    uint8_t* pOutputVector = reinterpret_cast<uint8_t*>(pOutputStream);

    for(size_t i = 0; i < VectorCount; ++i){
        const DoubleVector value = LoadDouble2(reinterpret_cast<const Double2*>(pInputVector));
        StoreDouble4(reinterpret_cast<Double4*>(pOutputVector), DoubleVector2Transform(value, M));
        pInputVector += InputStride;
        pOutputVector += OutputStride;
    }

    return pOutputStream;
}

_Use_decl_annotations_
inline Double2* MathCallConv DoubleVector2TransformCoordStream(
    Double2* pOutputStream,
    size_t OutputStride,
    const Double2* pInputStream,
    size_t InputStride,
    size_t VectorCount,
    const DoubleMatrix& M
)noexcept{
    const uint8_t* pInputVector = reinterpret_cast<const uint8_t*>(pInputStream);
    uint8_t* pOutputVector = reinterpret_cast<uint8_t*>(pOutputStream);

    for(size_t i = 0; i < VectorCount; ++i){
        const DoubleVector value = LoadDouble2(reinterpret_cast<const Double2*>(pInputVector));
        StoreDouble2(reinterpret_cast<Double2*>(pOutputVector), DoubleVector2TransformCoord(value, M));
        pInputVector += InputStride;
        pOutputVector += OutputStride;
    }

    return pOutputStream;
}

_Use_decl_annotations_
inline Double2* MathCallConv DoubleVector2TransformNormalStream(
    Double2* pOutputStream,
    size_t OutputStride,
    const Double2* pInputStream,
    size_t InputStride,
    size_t VectorCount,
    const DoubleMatrix& M
)noexcept{
    const uint8_t* pInputVector = reinterpret_cast<const uint8_t*>(pInputStream);
    uint8_t* pOutputVector = reinterpret_cast<uint8_t*>(pOutputStream);

    for(size_t i = 0; i < VectorCount; ++i){
        const DoubleVector value = LoadDouble2(reinterpret_cast<const Double2*>(pInputVector));
        StoreDouble2(reinterpret_cast<Double2*>(pOutputVector), DoubleVector2TransformNormal(value, M));
        pInputVector += InputStride;
        pOutputVector += OutputStride;
    }

    return pOutputStream;
}

_Use_decl_annotations_
inline Double4* MathCallConv DoubleVector3TransformStream(
    Double4* pOutputStream,
    size_t OutputStride,
    const Double3* pInputStream,
    size_t InputStride,
    size_t VectorCount,
    const DoubleMatrix& M
)noexcept{
    const uint8_t* pInputVector = reinterpret_cast<const uint8_t*>(pInputStream);
    uint8_t* pOutputVector = reinterpret_cast<uint8_t*>(pOutputStream);

    for(size_t i = 0; i < VectorCount; ++i){
        const DoubleVector value = LoadDouble3(reinterpret_cast<const Double3*>(pInputVector));
        StoreDouble4(reinterpret_cast<Double4*>(pOutputVector), DoubleVector3Transform(value, M));
        pInputVector += InputStride;
        pOutputVector += OutputStride;
    }

    return pOutputStream;
}

_Use_decl_annotations_
inline Double3* MathCallConv DoubleVector3TransformCoordStream(
    Double3* pOutputStream,
    size_t OutputStride,
    const Double3* pInputStream,
    size_t InputStride,
    size_t VectorCount,
    const DoubleMatrix& M
)noexcept{
    const uint8_t* pInputVector = reinterpret_cast<const uint8_t*>(pInputStream);
    uint8_t* pOutputVector = reinterpret_cast<uint8_t*>(pOutputStream);

    for(size_t i = 0; i < VectorCount; ++i){
        const DoubleVector value = LoadDouble3(reinterpret_cast<const Double3*>(pInputVector));
        StoreDouble3(reinterpret_cast<Double3*>(pOutputVector), DoubleVector3TransformCoord(value, M));
        pInputVector += InputStride;
        pOutputVector += OutputStride;
    }

    return pOutputStream;
}

_Use_decl_annotations_
inline Double3* MathCallConv DoubleVector3TransformNormalStream(
    Double3* pOutputStream,
    size_t OutputStride,
    const Double3* pInputStream,
    size_t InputStride,
    size_t VectorCount,
    const DoubleMatrix& M
)noexcept{
    const uint8_t* pInputVector = reinterpret_cast<const uint8_t*>(pInputStream);
    uint8_t* pOutputVector = reinterpret_cast<uint8_t*>(pOutputStream);

    for(size_t i = 0; i < VectorCount; ++i){
        const DoubleVector value = LoadDouble3(reinterpret_cast<const Double3*>(pInputVector));
        StoreDouble3(reinterpret_cast<Double3*>(pOutputVector), DoubleVector3TransformNormal(value, M));
        pInputVector += InputStride;
        pOutputVector += OutputStride;
    }

    return pOutputStream;
}

_Use_decl_annotations_
inline Double3* MathCallConv DoubleVector3ProjectStream(
    Double3* pOutputStream,
    size_t OutputStride,
    const Double3* pInputStream,
    size_t InputStride,
    size_t VectorCount,
    double viewportX,
    double viewportY,
    double viewportWidth,
    double viewportHeight,
    double viewportMinZ,
    double viewportMaxZ,
    const DoubleMatrix& projection,
    const DoubleMatrix& view,
    const DoubleMatrix& world
)noexcept{
    const uint8_t* pInputVector = reinterpret_cast<const uint8_t*>(pInputStream);
    uint8_t* pOutputVector = reinterpret_cast<uint8_t*>(pOutputStream);

    for(size_t i = 0; i < VectorCount; ++i){
        const DoubleVector value = LoadDouble3(reinterpret_cast<const Double3*>(pInputVector));
        StoreDouble3(
            reinterpret_cast<Double3*>(pOutputVector),
            DoubleVector3Project(
                value,
                viewportX,
                viewportY,
                viewportWidth,
                viewportHeight,
                viewportMinZ,
                viewportMaxZ,
                projection,
                view,
                world
            )
        );
        pInputVector += InputStride;
        pOutputVector += OutputStride;
    }

    return pOutputStream;
}

_Use_decl_annotations_
inline Double3* MathCallConv DoubleVector3UnprojectStream(
    Double3* pOutputStream,
    size_t OutputStride,
    const Double3* pInputStream,
    size_t InputStride,
    size_t VectorCount,
    double viewportX,
    double viewportY,
    double viewportWidth,
    double viewportHeight,
    double viewportMinZ,
    double viewportMaxZ,
    const DoubleMatrix& projection,
    const DoubleMatrix& view,
    const DoubleMatrix& world
)noexcept{
    const uint8_t* pInputVector = reinterpret_cast<const uint8_t*>(pInputStream);
    uint8_t* pOutputVector = reinterpret_cast<uint8_t*>(pOutputStream);

    for(size_t i = 0; i < VectorCount; ++i){
        const DoubleVector value = LoadDouble3(reinterpret_cast<const Double3*>(pInputVector));
        StoreDouble3(
            reinterpret_cast<Double3*>(pOutputVector),
            DoubleVector3Unproject(
                value,
                viewportX,
                viewportY,
                viewportWidth,
                viewportHeight,
                viewportMinZ,
                viewportMaxZ,
                projection,
                view,
                world
            )
        );
        pInputVector += InputStride;
        pOutputVector += OutputStride;
    }

    return pOutputStream;
}

_Use_decl_annotations_
inline Double4* MathCallConv DoubleVector4TransformStream(
    Double4* pOutputStream,
    size_t OutputStride,
    const Double4* pInputStream,
    size_t InputStride,
    size_t VectorCount,
    const DoubleMatrix& M
)noexcept{
    const uint8_t* pInputVector = reinterpret_cast<const uint8_t*>(pInputStream);
    uint8_t* pOutputVector = reinterpret_cast<uint8_t*>(pOutputStream);

    for(size_t i = 0; i < VectorCount; ++i){
        const DoubleVector value = LoadDouble4(reinterpret_cast<const Double4*>(pInputVector));
        StoreDouble4(reinterpret_cast<Double4*>(pOutputVector), DoubleVector4Transform(value, M));
        pInputVector += InputStride;
        pOutputVector += OutputStride;
    }

    return pOutputStream;
}

_Use_decl_annotations_
inline Double4* MathCallConv DoublePlaneTransformStream(
    Double4* pOutputStream,
    size_t OutputStride,
    const Double4* pInputStream,
    size_t InputStride,
    size_t PlaneCount,
    const DoubleMatrix& inverseTransposeMatrix
)noexcept{
    return DoubleVector4TransformStream(
        pOutputStream,
        OutputStride,
        pInputStream,
        InputStride,
        PlaneCount,
        inverseTransposeMatrix
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

