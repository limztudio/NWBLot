// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_source_math_vector_stream3{
#include "backend/scalar/vector3transform_stream.inl"

#if defined(_MATH_ARM_NEON_INTRINSICS_)
#include "backend/neon/vector3transform_stream.inl"
#endif

#if defined(_MATH_AVX2_INTRINSICS_)
#include "backend/avx2/vector3transform_stream.inl"
#endif

#if defined(_MATH_SSE_INTRINSICS_)
#include "backend/sse/vector3transform_stream.inl"
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


_Use_decl_annotations_
inline Float4* MathCallConv Vector3TransformStream
(
    Float4* pOutputStream,
    size_t          OutputStride,
    const Float3* pInputStream,
    size_t          InputStride,
    size_t          VectorCount,
    FXMMATRIX       M
)noexcept{
    assert(pOutputStream != nullptr);
    assert(pInputStream != nullptr);

    assert(InputStride >= sizeof(Float3));
    _Analysis_assume_(InputStride >= sizeof(Float3));

    assert(OutputStride >= sizeof(Float4));
    _Analysis_assume_(OutputStride >= sizeof(Float4));

#if defined(_MATH_NO_INTRINSICS_)
    return __hidden_source_math_vector_stream3::Vector3TransformStreamScalar(pOutputStream, OutputStride, pInputStream, InputStride, VectorCount, M);
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return __hidden_source_math_vector_stream3::Vector3TransformStreamNeon(pOutputStream, OutputStride, pInputStream, InputStride, VectorCount, M);
#elif defined(_MATH_AVX2_INTRINSICS_)
    if(VectorCount < 4)
        return __hidden_source_math_vector_stream3::Vector3TransformStreamSse(pOutputStream, OutputStride, pInputStream, InputStride, VectorCount, M);

    return __hidden_source_math_vector_stream3::Vector3TransformStreamAvx2(pOutputStream, OutputStride, pInputStream, InputStride, VectorCount, M);
#elif defined(_MATH_SSE_INTRINSICS_)
    return __hidden_source_math_vector_stream3::Vector3TransformStreamSse(pOutputStream, OutputStride, pInputStream, InputStride, VectorCount, M);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

