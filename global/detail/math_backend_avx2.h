// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#ifndef NWB_MATH_FRAGMENT_INCLUDE
#error Include global/math.h instead of including math_backend_avx2.h directly.
#endif


#if defined(NWB_MATH_BACKEND_AVX2)
namespace Avx2{


using Float4Reg = __m128;
using Double4Reg = __m256d;


[[nodiscard]] NWB_INLINE Float4Reg SetZeroF32()noexcept{ return _mm_setzero_ps(); }
[[nodiscard]] NWB_INLINE Float4Reg SetF32(const f32 x, const f32 y, const f32 z, const f32 w)noexcept{ return _mm_set_ps(w, z, y, x); }
[[nodiscard]] NWB_INLINE Float4Reg LoadF32(NotNull<const f32*> values)noexcept{ return _mm_loadu_ps(values.get()); }
NWB_INLINE void StoreF32(NotNull<f32*> outValues, const Float4Reg value)noexcept{ _mm_storeu_ps(outValues.get(), value); }
[[nodiscard]] NWB_INLINE Float4Reg AddF32(const Float4Reg lhs, const Float4Reg rhs)noexcept{ return _mm_add_ps(lhs, rhs); }
[[nodiscard]] NWB_INLINE Float4Reg SubF32(const Float4Reg lhs, const Float4Reg rhs)noexcept{ return _mm_sub_ps(lhs, rhs); }
[[nodiscard]] NWB_INLINE Float4Reg MulF32(const Float4Reg lhs, const Float4Reg rhs)noexcept{ return _mm_mul_ps(lhs, rhs); }
[[nodiscard]] NWB_INLINE Float4Reg DivF32(const Float4Reg lhs, const Float4Reg rhs)noexcept{ return _mm_div_ps(lhs, rhs); }
[[nodiscard]] NWB_INLINE Float4Reg MulAddF32(const Float4Reg lhs, const Float4Reg rhs, const Float4Reg addend)noexcept{
#if defined(__FMA__)
    return _mm_fmadd_ps(lhs, rhs, addend);
#else
    return _mm_add_ps(_mm_mul_ps(lhs, rhs), addend);
#endif
}
[[nodiscard]] NWB_INLINE Float4Reg SplatXF32(const Float4Reg value)noexcept{ return _mm_shuffle_ps(value, value, _MM_SHUFFLE(0, 0, 0, 0)); }
[[nodiscard]] NWB_INLINE Float4Reg SplatYF32(const Float4Reg value)noexcept{ return _mm_shuffle_ps(value, value, _MM_SHUFFLE(1, 1, 1, 1)); }
[[nodiscard]] NWB_INLINE Float4Reg SplatZF32(const Float4Reg value)noexcept{ return _mm_shuffle_ps(value, value, _MM_SHUFFLE(2, 2, 2, 2)); }
[[nodiscard]] NWB_INLINE Float4Reg SplatWF32(const Float4Reg value)noexcept{ return _mm_shuffle_ps(value, value, _MM_SHUFFLE(3, 3, 3, 3)); }
[[nodiscard]] NWB_INLINE f32 ExtractXF32(const Float4Reg value)noexcept{ return _mm_cvtss_f32(value); }
[[nodiscard]] NWB_INLINE f32 ExtractYF32(const Float4Reg value)noexcept{ return _mm_cvtss_f32(_mm_shuffle_ps(value, value, _MM_SHUFFLE(1, 1, 1, 1))); }
[[nodiscard]] NWB_INLINE f32 ExtractZF32(const Float4Reg value)noexcept{ return _mm_cvtss_f32(_mm_shuffle_ps(value, value, _MM_SHUFFLE(2, 2, 2, 2))); }
[[nodiscard]] NWB_INLINE f32 ExtractWF32(const Float4Reg value)noexcept{ return _mm_cvtss_f32(_mm_shuffle_ps(value, value, _MM_SHUFFLE(3, 3, 3, 3))); }
[[nodiscard]] NWB_INLINE f32 Dot3F32(const Float4Reg lhs, const Float4Reg rhs)noexcept{ return _mm_cvtss_f32(_mm_dp_ps(lhs, rhs, 0x71)); }
[[nodiscard]] NWB_INLINE f32 Dot4F32(const Float4Reg lhs, const Float4Reg rhs)noexcept{ return _mm_cvtss_f32(_mm_dp_ps(lhs, rhs, 0xF1)); }
NWB_INLINE void Transpose4x4F32(Float4Reg& c0, Float4Reg& c1, Float4Reg& c2, Float4Reg& c3)noexcept{ _MM_TRANSPOSE4_PS(c0, c1, c2, c3); }

[[nodiscard]] NWB_INLINE Double4Reg SetZeroF64()noexcept{ return _mm256_setzero_pd(); }
[[nodiscard]] NWB_INLINE Double4Reg SetF64(const f64 x, const f64 y, const f64 z, const f64 w)noexcept{ return _mm256_set_pd(w, z, y, x); }
[[nodiscard]] NWB_INLINE Double4Reg LoadF64(NotNull<const f64*> values)noexcept{ return _mm256_loadu_pd(values.get()); }
NWB_INLINE void StoreF64(NotNull<f64*> outValues, const Double4Reg value)noexcept{ _mm256_storeu_pd(outValues.get(), value); }
[[nodiscard]] NWB_INLINE Double4Reg AddF64(const Double4Reg lhs, const Double4Reg rhs)noexcept{ return _mm256_add_pd(lhs, rhs); }
[[nodiscard]] NWB_INLINE Double4Reg SubF64(const Double4Reg lhs, const Double4Reg rhs)noexcept{ return _mm256_sub_pd(lhs, rhs); }
[[nodiscard]] NWB_INLINE Double4Reg MulF64(const Double4Reg lhs, const Double4Reg rhs)noexcept{ return _mm256_mul_pd(lhs, rhs); }
[[nodiscard]] NWB_INLINE Double4Reg DivF64(const Double4Reg lhs, const Double4Reg rhs)noexcept{ return _mm256_div_pd(lhs, rhs); }
[[nodiscard]] NWB_INLINE Double4Reg MulAddF64(const Double4Reg lhs, const Double4Reg rhs, const Double4Reg addend)noexcept{
#if defined(__FMA__)
    return _mm256_fmadd_pd(lhs, rhs, addend);
#else
    return _mm256_add_pd(_mm256_mul_pd(lhs, rhs), addend);
#endif
}
[[nodiscard]] NWB_INLINE Double4Reg SplatXF64(const Double4Reg value)noexcept{ return _mm256_permute4x64_pd(value, _MM_SHUFFLE(0, 0, 0, 0)); }
[[nodiscard]] NWB_INLINE Double4Reg SplatYF64(const Double4Reg value)noexcept{ return _mm256_permute4x64_pd(value, _MM_SHUFFLE(1, 1, 1, 1)); }
[[nodiscard]] NWB_INLINE Double4Reg SplatZF64(const Double4Reg value)noexcept{ return _mm256_permute4x64_pd(value, _MM_SHUFFLE(2, 2, 2, 2)); }
[[nodiscard]] NWB_INLINE Double4Reg SplatWF64(const Double4Reg value)noexcept{ return _mm256_permute4x64_pd(value, _MM_SHUFFLE(3, 3, 3, 3)); }
[[nodiscard]] NWB_INLINE f64 ExtractXF64(const Double4Reg value)noexcept{ return _mm_cvtsd_f64(_mm256_castpd256_pd128(value)); }
[[nodiscard]] NWB_INLINE f64 ExtractYF64(const Double4Reg value)noexcept{
    const __m128d low = _mm256_castpd256_pd128(value);
    return _mm_cvtsd_f64(_mm_unpackhi_pd(low, low));
}
[[nodiscard]] NWB_INLINE f64 ExtractZF64(const Double4Reg value)noexcept{ return _mm_cvtsd_f64(_mm256_extractf128_pd(value, 1)); }
[[nodiscard]] NWB_INLINE f64 ExtractWF64(const Double4Reg value)noexcept{
    const __m128d high = _mm256_extractf128_pd(value, 1);
    return _mm_cvtsd_f64(_mm_unpackhi_pd(high, high));
}
[[nodiscard]] NWB_INLINE f64 Dot3F64(const Double4Reg lhs, const Double4Reg rhs)noexcept{
    const Double4Reg maskedProduct = _mm256_blend_pd(MulF64(lhs, rhs), _mm256_setzero_pd(), 0x8);
    const __m128d sum = _mm_add_pd(_mm256_castpd256_pd128(maskedProduct), _mm256_extractf128_pd(maskedProduct, 1));
    return _mm_cvtsd_f64(_mm_hadd_pd(sum, sum));
}
[[nodiscard]] NWB_INLINE f64 Dot4F64(const Double4Reg lhs, const Double4Reg rhs)noexcept{
    const Double4Reg product = MulF64(lhs, rhs);
    const __m128d sum = _mm_add_pd(_mm256_castpd256_pd128(product), _mm256_extractf128_pd(product, 1));
    return _mm_cvtsd_f64(_mm_hadd_pd(sum, sum));
}
NWB_INLINE void Transpose4x4F64(Double4Reg& c0, Double4Reg& c1, Double4Reg& c2, Double4Reg& c3)noexcept{
    const Double4Reg t0 = _mm256_unpacklo_pd(c0, c1);
    const Double4Reg t1 = _mm256_unpackhi_pd(c0, c1);
    const Double4Reg t2 = _mm256_unpacklo_pd(c2, c3);
    const Double4Reg t3 = _mm256_unpackhi_pd(c2, c3);
    c0 = _mm256_permute2f128_pd(t0, t2, 0x20);
    c1 = _mm256_permute2f128_pd(t1, t3, 0x20);
    c2 = _mm256_permute2f128_pd(t0, t2, 0x31);
    c3 = _mm256_permute2f128_pd(t1, t3, 0x31);
}


}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

