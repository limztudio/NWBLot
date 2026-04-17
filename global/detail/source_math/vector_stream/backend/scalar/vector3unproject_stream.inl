// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scalar_transform_common.inl"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


_Use_decl_annotations_
inline Float3* MathCallConv Vector3UnprojectStreamScalar
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
    Transform = MatrixInverse(nullptr, Transform);

    const uint8_t* pInputVector = reinterpret_cast<const uint8_t*>(pInputStream);
    uint8_t* pOutputVector = reinterpret_cast<uint8_t*>(pOutputStream);

#if defined(_MATH_NO_INTRINSICS_)
    const ScalarVectorStreamDetail::MatrixColumns transform(Transform);
    const ScalarVectorStreamDetail::UnprojectViewportTransform viewport(
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
            float x;
            float y;
            float z;
            viewport.Apply(input->x, input->y, input->z, x, y, z);

            float reciprocalW;
            transform.Transform3(x, y, z, output->x, output->y, output->z, reciprocalW);
            reciprocalW = 1.0f / reciprocalW;
            output->x *= reciprocalW;
            output->y *= reciprocalW;
            output->z *= reciprocalW;

            ++input;
            ++output;
        }

        return pOutputStream;
    }

    for(size_t i = 0; i < VectorCount; ++i){
        const auto* input = reinterpret_cast<const Float3*>(pInputVector);
        auto* output = reinterpret_cast<Float3*>(pOutputVector);
        float x;
        float y;
        float z;
        viewport.Apply(input->x, input->y, input->z, x, y, z);

        float reciprocalW;
        transform.Transform3(x, y, z, output->x, output->y, output->z, reciprocalW);
        reciprocalW = 1.0f / reciprocalW;
        output->x *= reciprocalW;
        output->y *= reciprocalW;
        output->z *= reciprocalW;

        pInputVector += InputStride;
        pOutputVector += OutputStride;
    }
#else
    static const VectorF32 D = { { { -1.0f, 1.0f, 0.0f, 0.0f } } };

    Vector Scale = VectorSet(ViewportWidth * 0.5f, -ViewportHeight * 0.5f, ViewportMaxZ - ViewportMinZ, 1.0f);
    Scale = VectorReciprocal(Scale);

    Vector Offset = VectorSet(-ViewportX, -ViewportY, -ViewportMinZ, 0.0f);
    Offset = VectorMultiplyAdd(Scale, Offset, D.v);

    for(size_t i = 0; i < VectorCount; ++i){
        Vector V = LoadFloat3(reinterpret_cast<const Float3*>(pInputVector));

        Vector Result = VectorMultiplyAdd(V, Scale, Offset);

        Result = Vector3TransformCoord(Result, Transform);

        StoreFloat3(reinterpret_cast<Float3*>(pOutputVector), Result);

        pInputVector += InputStride;
        pOutputVector += OutputStride;
    }
#endif

    return pOutputStream;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

