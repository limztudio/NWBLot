// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vector2transform_normal_stream{
#include "backend/scalar/vector2transform_normal_stream.inl"

#if defined(_MATH_ARM_NEON_INTRINSICS_)
#include "backend/neon/vector2transform_normal_stream.inl"
#endif

#if defined(_MATH_AVX2_INTRINSICS_)
#include "backend/avx2/vector2transform_normal_stream.inl"
#endif

#if defined(_MATH_SSE_INTRINSICS_)
#include "backend/sse/vector2transform_normal_stream.inl"
#endif
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


_Use_decl_annotations_
inline Float2* MathCallConv Vector2TransformNormalStream
(
    Float2* pOutputStream,
    size_t          OutputStride,
    const Float2* pInputStream,
    size_t          InputStride,
    size_t          VectorCount,
    FXMMATRIX       M
)noexcept{
    assert(pOutputStream != nullptr);
    assert(pInputStream != nullptr);

    assert(InputStride >= sizeof(Float2));
    _Analysis_assume_(InputStride >= sizeof(Float2));

    assert(OutputStride >= sizeof(Float2));
    _Analysis_assume_(OutputStride >= sizeof(Float2));

#if defined(_MATH_NO_INTRINSICS_)
    return __hidden_vector2transform_normal_stream::Vector2TransformNormalStreamScalar(pOutputStream, OutputStride, pInputStream, InputStride, VectorCount, M);
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return __hidden_vector2transform_normal_stream::Vector2TransformNormalStreamNeon(pOutputStream, OutputStride, pInputStream, InputStride, VectorCount, M);
#elif defined(_MATH_AVX2_INTRINSICS_)
    if(VectorCount < 4)
        return __hidden_vector2transform_normal_stream::Vector2TransformNormalStreamSse(pOutputStream, OutputStride, pInputStream, InputStride, VectorCount, M);

    return __hidden_vector2transform_normal_stream::Vector2TransformNormalStreamAvx2(pOutputStream, OutputStride, pInputStream, InputStride, VectorCount, M);
#elif defined(_MATH_SSE_INTRINSICS_)
    return __hidden_vector2transform_normal_stream::Vector2TransformNormalStreamSse(pOutputStream, OutputStride, pInputStream, InputStride, VectorCount, M);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

