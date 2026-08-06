// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#pragma once


#include "../math/vector.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TriangleAreaNormal64{
    f64 x = 0.0;
    f64 y = 0.0;
    f64 z = 0.0;
};
static_assert(IsTriviallyCopyable_V<TriangleAreaNormal64>);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TriangleAreaDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(__AVX2__) || defined(_M_AVX2)
template<typename Vector3Like>
[[nodiscard]] NWB_INLINE __m256d MakeVector3F64(const Vector3Like& value)noexcept{
    return _mm256_set_pd(
        0.0,
        static_cast<f64>(value.z),
        static_cast<f64>(value.y),
        static_cast<f64>(value.x)
    );
}

[[nodiscard]] NWB_INLINE __m256d MakeVector3F64(const SIMDVector value)noexcept{
    return _mm256_cvtps_pd(value);
}

[[nodiscard]] NWB_INLINE TriangleAreaNormal64 MakeTriangleAreaNormal64(const __m256d value)noexcept{
    const __m128d xy = _mm256_castpd256_pd128(value);
    const __m128d z0 = _mm256_extractf128_pd(value, 1);
    return TriangleAreaNormal64{
        _mm_cvtsd_f64(xy),
        _mm_cvtsd_f64(_mm_unpackhi_pd(xy, xy)),
        _mm_cvtsd_f64(z0),
    };
}

[[nodiscard]] NWB_INLINE TriangleAreaNormal64 CrossVector3F64(const __m256d ab, const __m256d ac)noexcept{
    const __m256d abYzx = _mm256_permute4x64_pd(ab, _MM_SHUFFLE(3, 0, 2, 1));
    const __m256d abZxy = _mm256_permute4x64_pd(ab, _MM_SHUFFLE(3, 1, 0, 2));
    const __m256d acYzx = _mm256_permute4x64_pd(ac, _MM_SHUFFLE(3, 0, 2, 1));
    const __m256d acZxy = _mm256_permute4x64_pd(ac, _MM_SHUFFLE(3, 1, 0, 2));
    return MakeTriangleAreaNormal64(_mm256_sub_pd(
        _mm256_mul_pd(abYzx, acZxy),
        _mm256_mul_pd(abZxy, acYzx)
    ));
}
#endif

[[nodiscard]] NWB_INLINE TriangleAreaNormal64 BuildTriangleAreaNormal64FromEdges(
    const f64 abX,
    const f64 abY,
    const f64 abZ,
    const f64 acX,
    const f64 acY,
    const f64 acZ
)noexcept{
#if defined(NWB_HAS_SSE4)
    const __m128d xy = _mm_sub_pd(
        _mm_mul_pd(_mm_set_pd(abZ, abY), _mm_set_pd(acX, acZ)),
        _mm_mul_pd(_mm_set_pd(abX, abZ), _mm_set_pd(acZ, acY))
    );
    const __m128d zProducts = _mm_mul_pd(_mm_set_pd(abY, abX), _mm_set_pd(acX, acY));
    const __m128d z = _mm_sub_sd(zProducts, _mm_unpackhi_pd(zProducts, zProducts));
    return TriangleAreaNormal64{
        _mm_cvtsd_f64(xy),
        _mm_cvtsd_f64(_mm_unpackhi_pd(xy, xy)),
        _mm_cvtsd_f64(z),
    };
#else
    return TriangleAreaNormal64{
        abY * acZ - abZ * acY,
        abZ * acX - abX * acZ,
        abX * acY - abY * acX,
    };
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Vector3Like>
[[nodiscard]] NWB_INLINE TriangleAreaNormal64 BuildTriangleAreaNormal64(
    const Vector3Like& a,
    const Vector3Like& b,
    const Vector3Like& c
)noexcept{
#if defined(__AVX2__) || defined(_M_AVX2)
    const __m256d ab = _mm256_sub_pd(TriangleAreaDetail::MakeVector3F64(b), TriangleAreaDetail::MakeVector3F64(a));
    const __m256d ac = _mm256_sub_pd(TriangleAreaDetail::MakeVector3F64(c), TriangleAreaDetail::MakeVector3F64(a));
    return TriangleAreaDetail::CrossVector3F64(ab, ac);
#else
    const f64 abX = static_cast<f64>(b.x) - static_cast<f64>(a.x);
    const f64 abY = static_cast<f64>(b.y) - static_cast<f64>(a.y);
    const f64 abZ = static_cast<f64>(b.z) - static_cast<f64>(a.z);
    const f64 acX = static_cast<f64>(c.x) - static_cast<f64>(a.x);
    const f64 acY = static_cast<f64>(c.y) - static_cast<f64>(a.y);
    const f64 acZ = static_cast<f64>(c.z) - static_cast<f64>(a.z);
    return TriangleAreaDetail::BuildTriangleAreaNormal64FromEdges(abX, abY, abZ, acX, acY, acZ);
#endif
}

[[nodiscard]] NWB_INLINE TriangleAreaNormal64 BuildTriangleAreaNormal64(
    const SIMDVector a,
    const SIMDVector b,
    const SIMDVector c
)noexcept{
#if defined(__AVX2__) || defined(_M_AVX2)
    const __m256d ab = _mm256_sub_pd(TriangleAreaDetail::MakeVector3F64(b), TriangleAreaDetail::MakeVector3F64(a));
    const __m256d ac = _mm256_sub_pd(TriangleAreaDetail::MakeVector3F64(c), TriangleAreaDetail::MakeVector3F64(a));
    return TriangleAreaDetail::CrossVector3F64(ab, ac);
#else
    const f64 abX = static_cast<f64>(VectorGetX(b)) - static_cast<f64>(VectorGetX(a));
    const f64 abY = static_cast<f64>(VectorGetY(b)) - static_cast<f64>(VectorGetY(a));
    const f64 abZ = static_cast<f64>(VectorGetZ(b)) - static_cast<f64>(VectorGetZ(a));
    const f64 acX = static_cast<f64>(VectorGetX(c)) - static_cast<f64>(VectorGetX(a));
    const f64 acY = static_cast<f64>(VectorGetY(c)) - static_cast<f64>(VectorGetY(a));
    const f64 acZ = static_cast<f64>(VectorGetZ(c)) - static_cast<f64>(VectorGetZ(a));
    return TriangleAreaDetail::BuildTriangleAreaNormal64FromEdges(abX, abY, abZ, acX, acY, acZ);
#endif
}

[[nodiscard]] NWB_INLINE f64 TriangleAreaNormalLengthSquared(const TriangleAreaNormal64& areaNormal)noexcept{
#if defined(__AVX2__) || defined(_M_AVX2)
    const __m256d normal = _mm256_set_pd(0.0, areaNormal.z, areaNormal.y, areaNormal.x);
    const __m256d squared = _mm256_mul_pd(normal, normal);
    const __m128d xy = _mm256_castpd256_pd128(squared);
    const __m128d z0 = _mm256_extractf128_pd(squared, 1);
    const __m128d xzY = _mm_add_pd(xy, z0);
    const __m128d sum = _mm_add_sd(xzY, _mm_unpackhi_pd(xzY, xzY));
    return _mm_cvtsd_f64(sum);
#elif defined(NWB_HAS_SSE4)
    const __m128d xy = _mm_set_pd(areaNormal.y, areaNormal.x);
    const __m128d xySquared = _mm_mul_pd(xy, xy);
    const __m128d xySum = _mm_add_sd(xySquared, _mm_unpackhi_pd(xySquared, xySquared));
    return _mm_cvtsd_f64(xySum) + areaNormal.z * areaNormal.z;
#else
    return areaNormal.x * areaNormal.x + areaNormal.y * areaNormal.y + areaNormal.z * areaNormal.z;
#endif
}

template<typename Vector3Like>
[[nodiscard]] NWB_INLINE bool TriangleHasArea(
    const Vector3Like& a,
    const Vector3Like& b,
    const Vector3Like& c,
    const f64 triangleAreaLengthSquaredEpsilon
)noexcept{
    return TriangleAreaNormalLengthSquared(BuildTriangleAreaNormal64(a, b, c)) > triangleAreaLengthSquaredEpsilon;
}

[[nodiscard]] NWB_INLINE bool TriangleHasArea(
    const SIMDVector a,
    const SIMDVector b,
    const SIMDVector c,
    const f64 triangleAreaLengthSquaredEpsilon
)noexcept{
    return TriangleAreaNormalLengthSquared(BuildTriangleAreaNormal64(a, b, c)) > triangleAreaLengthSquaredEpsilon;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

