// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector4.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVector SIMDCALL Vector4Transform(SIMDVector value, const SIMDMatrix& matrix)noexcept{
#if defined(NWB_HAS_SCALAR)
    return VectorSet(
        VectorGetX(Vector4Dot(matrix.v[0], value)),
        VectorGetX(Vector4Dot(matrix.v[1], value)),
        VectorGetX(Vector4Dot(matrix.v[2], value)),
        VectorGetX(Vector4Dot(matrix.v[3], value))
    );
#elif defined(NWB_HAS_NEON)
    const float32x4x2_t p0 = vzipq_f32(matrix.v[0], matrix.v[2]);
    const float32x4x2_t p1 = vzipq_f32(matrix.v[1], matrix.v[3]);
    const float32x4x2_t t0 = vzipq_f32(p0.val[0], p1.val[0]);
    const float32x4x2_t t1 = vzipq_f32(p0.val[1], p1.val[1]);
    const float32x2_t low = vget_low_f32(value);
    const float32x2_t high = vget_high_f32(value);
    SIMDVector result = vmulq_lane_f32(t0.val[0], low, 0);
    result = vmlaq_lane_f32(result, t0.val[1], low, 1);
    result = vmlaq_lane_f32(result, t1.val[0], high, 0);
    return vmlaq_lane_f32(result, t1.val[1], high, 1);
#else
    const SIMDVector x = Vector4Dot(matrix.v[0], value);
    const SIMDVector y = Vector4Dot(matrix.v[1], value);
    const SIMDVector z = Vector4Dot(matrix.v[2], value);
    const SIMDVector w = Vector4Dot(matrix.v[3], value);
    const SIMDVector xy = _mm_unpacklo_ps(x, y);
    const SIMDVector zw = _mm_unpacklo_ps(z, w);
    return _mm_movelh_ps(xy, zw);
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector2Transform(SIMDVector value, const SIMDMatrix& matrix)noexcept{
#if defined(NWB_HAS_SSE4)
    const SIMDVector v = _mm_or_ps(_mm_and_ps(value, s_SIMDMaskXY), s_SIMDIdentityR3);
    return SIMDVectorDetail::MatrixDotPack<0xBF>(matrix, v);
#else
    return Vector4Transform(VectorSetW(VectorSetZ(value, 0.0f), 1.0f), matrix);
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector2TransformCoord(SIMDVector value, const SIMDMatrix& matrix)noexcept{
    SIMDVector result = Vector2Transform(value, matrix);
    return VectorSetW(VectorDivide(result, VectorSplatW(result)), 1.0f);
}

NWB_INLINE SIMDVector SIMDCALL Vector2TransformNormal(SIMDVector value, const SIMDMatrix& matrix)noexcept{
#if defined(NWB_HAS_SSE4)
    const SIMDVector v = _mm_and_ps(value, s_SIMDMaskXY);
    return SIMDVectorDetail::MatrixDotPack<0x3F>(matrix, v);
#else
    return Vector4Transform(VectorAndInt(value, s_SIMDMaskXY), matrix);
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector3Transform(SIMDVector value, const SIMDMatrix& matrix)noexcept{
#if defined(NWB_HAS_SSE4)
    const SIMDVector v = _mm_or_ps(_mm_and_ps(value, s_SIMDMask3), s_SIMDIdentityR3);
    return SIMDVectorDetail::MatrixDotPack<0xFF>(matrix, v);
#else
    return Vector4Transform(VectorSetW(value, 1.0f), matrix);
#endif
}

NWB_INLINE SIMDVector SIMDCALL Vector3TransformCoord(SIMDVector value, const SIMDMatrix& matrix)noexcept{
    SIMDVector result = Vector3Transform(value, matrix);
    return VectorSetW(VectorDivide(result, VectorSplatW(result)), 1.0f);
}

NWB_INLINE SIMDVector SIMDCALL Vector3TransformNormal(SIMDVector value, const SIMDMatrix& matrix)noexcept{
#if defined(NWB_HAS_SSE4)
    const SIMDVector v = _mm_and_ps(value, s_SIMDMask3);
    return SIMDVectorDetail::MatrixDotPack<0x7F>(matrix, v);
#else
    return Vector4Transform(VectorAndInt(value, s_SIMDMask3), matrix);
#endif
}

NWB_INLINE Float4U* SIMDCALL Vector2TransformStream(Float4U* outputStream, usize outputStride, const Float2U* inputStream, usize inputStride, usize vectorCount, const SIMDMatrix& matrix)noexcept{
    return SIMDVectorDetail::VectorTransformStreamImpl(outputStream, outputStride, inputStream, inputStride, vectorCount, matrix,
        [](SIMDVector value, const SIMDMatrix& transposedMatrix)noexcept{
            return SIMDVectorDetail::Vector4TransformTransposed(VectorSetW(VectorSetZ(value, 0.0f), 1.0f), transposedMatrix);
        }
    );
}

NWB_INLINE Float2U* SIMDCALL Vector2TransformCoordStream(Float2U* outputStream, usize outputStride, const Float2U* inputStream, usize inputStride, usize vectorCount, const SIMDMatrix& matrix)noexcept{
    return SIMDVectorDetail::VectorTransformStreamImpl(outputStream, outputStride, inputStream, inputStride, vectorCount, matrix,
        [](SIMDVector value, const SIMDMatrix& transposedMatrix)noexcept{
            SIMDVector result = SIMDVectorDetail::Vector4TransformTransposed(
                VectorSetW(VectorSetZ(value, 0.0f), 1.0f),
                transposedMatrix
            );
            result = VectorSetW(VectorDivide(result, VectorSplatW(result)), 1.0f);
            return result;
        }
    );
}

NWB_INLINE Float2U* SIMDCALL Vector2TransformNormalStream(Float2U* outputStream, usize outputStride, const Float2U* inputStream, usize inputStride, usize vectorCount, const SIMDMatrix& matrix)noexcept{
    return SIMDVectorDetail::VectorTransformStreamImpl(outputStream, outputStride, inputStream, inputStride, vectorCount, matrix,
        [](SIMDVector value, const SIMDMatrix& transposedMatrix)noexcept{
            return SIMDVectorDetail::Vector4TransformTransposed(VectorAndInt(value, s_SIMDMaskXY), transposedMatrix);
        }
    );
}

NWB_INLINE Float4U* SIMDCALL Vector3TransformStream(Float4U* outputStream, usize outputStride, const Float3U* inputStream, usize inputStride, usize vectorCount, const SIMDMatrix& matrix)noexcept{
    return SIMDVectorDetail::VectorTransformStreamImpl(outputStream, outputStride, inputStream, inputStride, vectorCount, matrix,
        [](SIMDVector value, const SIMDMatrix& transposedMatrix)noexcept{
            return SIMDVectorDetail::Vector4TransformTransposed(VectorSetW(value, 1.0f), transposedMatrix);
        }
    );
}

NWB_INLINE Float3U* SIMDCALL Vector3TransformCoordStream(Float3U* outputStream, usize outputStride, const Float3U* inputStream, usize inputStride, usize vectorCount, const SIMDMatrix& matrix)noexcept{
    return SIMDVectorDetail::VectorTransformStreamImpl(outputStream, outputStride, inputStream, inputStride, vectorCount, matrix,
        [](SIMDVector value, const SIMDMatrix& transposedMatrix)noexcept{
            SIMDVector result = SIMDVectorDetail::Vector4TransformTransposed(
                VectorSetW(value, 1.0f),
                transposedMatrix
            );
            result = VectorSetW(VectorDivide(result, VectorSplatW(result)), 1.0f);
            return result;
        }
    );
}

NWB_INLINE Float3U* SIMDCALL Vector3TransformNormalStream(Float3U* outputStream, usize outputStride, const Float3U* inputStream, usize inputStride, usize vectorCount, const SIMDMatrix& matrix)noexcept{
    return SIMDVectorDetail::VectorTransformStreamImpl(outputStream, outputStride, inputStream, inputStride, vectorCount, matrix,
        [](SIMDVector value, const SIMDMatrix& transposedMatrix)noexcept{
            return SIMDVectorDetail::Vector4TransformTransposed(VectorAndInt(value, s_SIMDMask3), transposedMatrix);
        }
    );
}

NWB_INLINE Float4U* SIMDCALL Vector4TransformStream(Float4U* outputStream, usize outputStride, const Float4U* inputStream, usize inputStride, usize vectorCount, const SIMDMatrix& matrix)noexcept{
    return SIMDVectorDetail::VectorTransformStreamImpl(outputStream, outputStride, inputStream, inputStride, vectorCount, matrix,
        [](SIMDVector value, const SIMDMatrix& transposedMatrix)noexcept{
            return SIMDVectorDetail::Vector4TransformTransposed(value, transposedMatrix);
        }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

