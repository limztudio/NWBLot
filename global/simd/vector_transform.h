// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector4.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FOUNDATION_INLINE SIMDVectorSingle FOUNDATION_SIMD_CALL Vector4Transform(SIMDVectorSingle value, const SIMDMatrix& matrix)noexcept{
#if defined(FOUNDATION_HAS_SCALAR)
    return VectorSet(VectorGetX(Vector4Dot(matrix.v[0], value)), VectorGetX(Vector4Dot(matrix.v[1], value)), VectorGetX(Vector4Dot(matrix.v[2], value)), VectorGetX(Vector4Dot(matrix.v[3], value)));
#elif defined(FOUNDATION_HAS_NEON)
    const float32x4x2_t p0 = vzipq_f32(matrix.v[0], matrix.v[2]);
    const float32x4x2_t p1 = vzipq_f32(matrix.v[1], matrix.v[3]);
    const float32x4x2_t t0 = vzipq_f32(p0.val[0], p1.val[0]);
    const float32x4x2_t t1 = vzipq_f32(p0.val[1], p1.val[1]);
    const float32x2_t low = vget_low_f32(value);
    const float32x2_t high = vget_high_f32(value);
    SIMDVectorSingle result = vmulq_lane_f32(t0.val[0], low, 0);
    result = vmlaq_lane_f32(result, t0.val[1], low, 1);
    result = vmlaq_lane_f32(result, t1.val[0], high, 0);
    return vmlaq_lane_f32(result, t1.val[1], high, 1);
#else
    const SIMDVectorSingle x = Vector4Dot(matrix.v[0], value);
    const SIMDVectorSingle y = Vector4Dot(matrix.v[1], value);
    const SIMDVectorSingle z = Vector4Dot(matrix.v[2], value);
    const SIMDVectorSingle w = Vector4Dot(matrix.v[3], value);
    const SIMDVectorSingle xy = _mm_unpacklo_ps(x, y);
    const SIMDVectorSingle zw = _mm_unpacklo_ps(z, w);
    return _mm_movelh_ps(xy, zw);
#endif
}

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL VectorDouble4Transform(SIMDVectorDouble value, const SIMDMatrixDouble& matrix)noexcept{
    return VectorDoubleMergeX(VectorDouble4Dot(matrix.v[0], value), VectorDouble4Dot(matrix.v[1], value), VectorDouble4Dot(matrix.v[2], value), VectorDouble4Dot(matrix.v[3], value));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FOUNDATION_INLINE SIMDVectorSingle FOUNDATION_SIMD_CALL Vector2Transform(SIMDVectorSingle value, const SIMDMatrix& matrix)noexcept{
#if defined(FOUNDATION_HAS_SSE4)
    const SIMDVectorSingle v = _mm_or_ps(_mm_and_ps(value, s_SIMDMaskXY), s_SIMDIdentityR3);
    return SIMDVectorDetail::MatrixDotPack<s_SIMDSse4Dot2TransformMask>(matrix, v);
#else
    return Vector4Transform(VectorSetW(VectorSetZ(value, 0.0f), 1.0f), matrix);
#endif
}

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL VectorDouble2Transform(SIMDVectorDouble value, const SIMDMatrixDouble& matrix)noexcept{
    return VectorDouble4Transform(VectorDoubleSetW(VectorDoubleSetZ(value, 0.0), 1.0), matrix);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename OutputT, typename InputT>
    requires ((IsSame_V<OutputT, Double2> || IsSame_V<OutputT, Double2U>) && (IsSame_V<InputT, Double2> || IsSame_V<InputT, Double2U>))
FOUNDATION_INLINE NotNull<OutputT*> FOUNDATION_SIMD_CALL VectorDouble2TransformCoordStream(NotNull<OutputT*> outputStream, usize outputStride, NotNull<const InputT*> inputStream, usize inputStride, usize vectorCount, const SIMDMatrixDouble& matrix)noexcept{
    return SIMDVectorDoubleDetail::VectorDoubleTransformStreamImpl(outputStream, outputStride, inputStream, inputStride, vectorCount, matrix, [](SIMDVectorDouble value, const SIMDMatrixDouble& transposedMatrix)noexcept{
        SIMDVectorDouble result = SIMDVectorDoubleDetail::VectorDouble4TransformTransposed(VectorDoubleSetW(VectorDoubleSetZ(value, 0.0), 1.0), transposedMatrix);
        result = VectorDoubleSetW(VectorDoubleDivide(result, VectorDoubleSplatW(result)), 1.0);
        return result;
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FOUNDATION_INLINE SIMDVectorSingle FOUNDATION_SIMD_CALL Vector3Transform(SIMDVectorSingle value, const SIMDMatrix& matrix)noexcept{
#if defined(FOUNDATION_HAS_SSE4)
    const SIMDVectorSingle v = _mm_or_ps(_mm_and_ps(value, s_SIMDMask3), s_SIMDIdentityR3);
    return SIMDVectorDetail::MatrixDotPack<s_SIMDSse4Dot4BroadcastMask>(matrix, v);
#else
    return Vector4Transform(VectorSetW(value, 1.0f), matrix);
#endif
}

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL VectorDouble3Transform(SIMDVectorDouble value, const SIMDMatrixDouble& matrix)noexcept{
    return VectorDouble4Transform(VectorDoubleSetW(value, 1.0), matrix);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL VectorDouble3TransformCoord(SIMDVectorDouble value, const SIMDMatrixDouble& matrix)noexcept{
    SIMDVectorDouble result = VectorDouble3Transform(value, matrix);
    return VectorDoubleSetW(VectorDoubleDivide(result, VectorDoubleSplatW(result)), 1.0);
}

template<typename OutputT, typename InputT>
    requires ((IsSame_V<OutputT, Double3> || IsSame_V<OutputT, Double3U>) && (IsSame_V<InputT, Double3> || IsSame_V<InputT, Double3U>))
FOUNDATION_INLINE NotNull<OutputT*> FOUNDATION_SIMD_CALL VectorDouble3TransformCoordStream(NotNull<OutputT*> outputStream, usize outputStride, NotNull<const InputT*> inputStream, usize inputStride, usize vectorCount, const SIMDMatrixDouble& matrix)noexcept{
    return SIMDVectorDoubleDetail::VectorDoubleTransformStreamImpl(outputStream, outputStride, inputStream, inputStride, vectorCount, matrix, [](SIMDVectorDouble value, const SIMDMatrixDouble& transposedMatrix)noexcept{
        SIMDVectorDouble result = SIMDVectorDoubleDetail::VectorDouble4TransformTransposed(VectorDoubleSetW(value, 1.0), transposedMatrix);
        result = VectorDoubleSetW(VectorDoubleDivide(result, VectorDoubleSplatW(result)), 1.0);
        return result;
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


FOUNDATION_INLINE SIMDVectorSingle FOUNDATION_SIMD_CALL Vector3TransformNormal(SIMDVectorSingle value, const SIMDMatrix& matrix)noexcept{
#if defined(FOUNDATION_HAS_SSE4)
    const SIMDVectorSingle v = _mm_and_ps(value, s_SIMDMask3);
    return SIMDVectorDetail::MatrixDotPack<s_SIMDSse4Dot3BroadcastMask>(matrix, v);
#else
    return Vector4Transform(VectorAndInt(value, s_SIMDMask3), matrix);
#endif
}

FOUNDATION_INLINE SIMDVectorDouble FOUNDATION_SIMD_CALL VectorDouble3TransformNormal(SIMDVectorDouble value, const SIMDMatrixDouble& matrix)noexcept{
    return VectorDouble4Transform(VectorDoubleAndInt(value, s_DoubleSIMDMask3), matrix);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

