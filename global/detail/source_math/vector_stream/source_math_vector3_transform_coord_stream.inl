// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vector3transform_coord_stream{
#include "backend/scalar/vector3transform_coord_stream.inl"

#if defined(_MATH_ARM_NEON_INTRINSICS_)
#include "backend/neon/vector3transform_coord_stream.inl"
#endif

#if defined(_MATH_AVX2_INTRINSICS_)
#include "backend/avx2/vector3transform_coord_stream.inl"
#endif

#if defined(_MATH_SSE_INTRINSICS_)
#include "backend/sse/vector3transform_coord_stream.inl"
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef _PREFAST_
#pragma prefast(push)
#pragma prefast(disable : 26015 26019, "PREfast noise: Esp:1307" )
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


_Use_decl_annotations_
inline Float3* MathCallConv Vector3TransformCoordStream
(
    Float3* pOutputStream,
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

    assert(OutputStride >= sizeof(Float3));
    _Analysis_assume_(OutputStride >= sizeof(Float3));

#if defined(_MATH_NO_INTRINSICS_)
    return __hidden_vector3transform_coord_stream::Vector3TransformCoordStreamScalar(pOutputStream, OutputStride, pInputStream, InputStride, VectorCount, M);
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return __hidden_vector3transform_coord_stream::Vector3TransformCoordStreamNeon(pOutputStream, OutputStride, pInputStream, InputStride, VectorCount, M);
#elif defined(_MATH_AVX2_INTRINSICS_)
    if(VectorCount < 4)
        return __hidden_vector3transform_coord_stream::Vector3TransformCoordStreamSse(pOutputStream, OutputStride, pInputStream, InputStride, VectorCount, M);

    return __hidden_vector3transform_coord_stream::Vector3TransformCoordStreamAvx2(pOutputStream, OutputStride, pInputStream, InputStride, VectorCount, M);
#elif defined(_MATH_SSE_INTRINSICS_)
    return __hidden_vector3transform_coord_stream::Vector3TransformCoordStreamSse(pOutputStream, OutputStride, pInputStream, InputStride, VectorCount, M);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef _PREFAST_
#pragma prefast(pop)
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

