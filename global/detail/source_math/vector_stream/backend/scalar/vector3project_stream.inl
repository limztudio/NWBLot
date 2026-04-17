// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scalar_transform_common.inl"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


_Use_decl_annotations_
inline Float3* MathCallConv Vector3ProjectStreamScalar
(
    Float3*  pOutputStream,
    size_t   OutputStride,
    const Float3* pInputStream,
    size_t   InputStride,
    size_t   VectorCount,
    float    ViewportX,
    float    ViewportY,
    float    ViewportWidth,
    float    ViewportHeight,
    float    ViewportMinZ,
    float    ViewportMaxZ,
    FXMMATRIX Projection,
    CXMMATRIX View,
    CXMMATRIX World
)noexcept{
    if(VectorCount == 0){
        return pOutputStream;
    }

    Matrix Transform = BuildProjectTransformColumns(Projection, View, World);

    const uint8_t* pInputVector = reinterpret_cast<const uint8_t*>(pInputStream);
    uint8_t* pOutputVector = reinterpret_cast<uint8_t*>(pOutputStream);

#if defined(_MATH_NO_INTRINSICS_)
    const ScalarVectorStreamDetail::MatrixColumns transform(Transform);
    const ScalarVectorStreamDetail::ProjectViewportTransform viewport(
        ViewportX,
        ViewportY,
        ViewportWidth,
        ViewportHeight,
        ViewportMinZ,
        ViewportMaxZ
    );

    if(ScalarVectorStreamDetail::HasTightStride<Float3, Float3>(InputStride, OutputStride)){
        const Float3* input = pInputStream;
        Float3* output = pOutputStream;

        for(size_t i = 0; i < VectorCount; ++i){
            const float x = input->x;
            const float y = input->y;
            const float z = input->z;
            float reciprocalW;
            transform.Transform3(x, y, z, output->x, output->y, output->z, reciprocalW);
            reciprocalW = 1.0f / reciprocalW;
            viewport.Apply(output->x, output->y, output->z, reciprocalW, output->x, output->y, output->z);

            ++input;
            ++output;
        }

        return pOutputStream;
    }

    for(size_t i = 0; i < VectorCount; ++i){
        const auto* input = reinterpret_cast<const Float3*>(pInputVector);
        auto* output = reinterpret_cast<Float3*>(pOutputVector);
        const float x = input->x;
        const float y = input->y;
        const float z = input->z;
        float reciprocalW;
        transform.Transform3(x, y, z, output->x, output->y, output->z, reciprocalW);
        reciprocalW = 1.0f / reciprocalW;
        viewport.Apply(output->x, output->y, output->z, reciprocalW, output->x, output->y, output->z);

        pInputVector += InputStride;
        pOutputVector += OutputStride;
    }
#else
    const float halfViewportWidth = ViewportWidth * 0.5f;
    const float halfViewportHeight = ViewportHeight * 0.5f;
    Vector Scale = VectorSet(halfViewportWidth, -halfViewportHeight, ViewportMaxZ - ViewportMinZ, 1.0f);
    Vector Offset = VectorSet(ViewportX + halfViewportWidth, ViewportY + halfViewportHeight, ViewportMinZ, 0.0f);

    for(size_t i = 0; i < VectorCount; ++i){
        Vector V = LoadFloat3(reinterpret_cast<const Float3*>(pInputVector));

        Vector Result = Vector3TransformCoord(V, Transform);
        Result = VectorMultiplyAdd(Result, Scale, Offset);

        StoreFloat3(reinterpret_cast<Float3*>(pOutputVector), Result);

        pInputVector += InputStride;
        pOutputVector += OutputStride;
    }
#endif

    return pOutputStream;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

