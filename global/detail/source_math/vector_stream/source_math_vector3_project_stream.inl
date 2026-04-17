// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_vector3project_stream{
#include "backend/scalar/vector3project_stream.inl"

#if defined(_MATH_ARM_NEON_INTRINSICS_)
#include "backend/neon/vector3project_stream.inl"
#endif

#if defined(_MATH_SSE_INTRINSICS_)
#include "backend/sse/vector3project_stream.inl"
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef _PREFAST_
#pragma prefast(push)
#pragma prefast(disable : 26015 26019, "PREfast noise: Esp:1307" )
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


_Use_decl_annotations_
inline Float3* MathCallConv Vector3ProjectStream
(
    Float3* pOutputStream,
    size_t          OutputStride,
    const Float3* pInputStream,
    size_t          InputStride,
    size_t          VectorCount,
    float           ViewportX,
    float           ViewportY,
    float           ViewportWidth,
    float           ViewportHeight,
    float           ViewportMinZ,
    float           ViewportMaxZ,
    FXMMATRIX     Projection,
    CXMMATRIX     View,
    CXMMATRIX     World
)noexcept{
    assert(pOutputStream != nullptr);
    assert(pInputStream != nullptr);

    assert(InputStride >= sizeof(Float3));
    _Analysis_assume_(InputStride >= sizeof(Float3));

    assert(OutputStride >= sizeof(Float3));
    _Analysis_assume_(OutputStride >= sizeof(Float3));

#if defined(_MATH_NO_INTRINSICS_)
    return __hidden_vector3project_stream::Vector3ProjectStreamScalar(pOutputStream, OutputStride, pInputStream, InputStride, VectorCount, ViewportX, ViewportY, ViewportWidth, ViewportHeight, ViewportMinZ, ViewportMaxZ, Projection, View, World);
#elif defined(_MATH_ARM_NEON_INTRINSICS_)
    return __hidden_vector3project_stream::Vector3ProjectStreamNeon(pOutputStream, OutputStride, pInputStream, InputStride, VectorCount, ViewportX, ViewportY, ViewportWidth, ViewportHeight, ViewportMinZ, ViewportMaxZ, Projection, View, World);
#elif defined(_MATH_SSE_INTRINSICS_)
    return __hidden_vector3project_stream::Vector3ProjectStreamSse(pOutputStream, OutputStride, pInputStream, InputStride, VectorCount, ViewportX, ViewportY, ViewportWidth, ViewportHeight, ViewportMinZ, ViewportMaxZ, Projection, View, World);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef _PREFAST_
#pragma prefast(pop)
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

