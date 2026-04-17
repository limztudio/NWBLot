// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scalar_transform_common.inl"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


_Use_decl_annotations_
inline Float2* MathCallConv Vector2TransformCoordStreamScalar
(
    Float2* pOutputStream,
    size_t          OutputStride,
    const Float2* pInputStream,
    size_t          InputStride,
    size_t          VectorCount,
    FXMMATRIX       M
)noexcept{
    if(VectorCount == 0){
        return pOutputStream;
    }

    const uint8_t* pInputVector = reinterpret_cast<const uint8_t*>(pInputStream);
    uint8_t* pOutputVector = reinterpret_cast<uint8_t*>(pOutputStream);

#if defined(_MATH_NO_INTRINSICS_)
    const ScalarVectorStreamDetail::MatrixColumns transform(M);

    if(ScalarVectorStreamDetail::HasTightStride<Float2, Float2>(InputStride, OutputStride)){
        const Float2* input = pInputStream;
        Float2* output = pOutputStream;

        for(size_t i = 0; i < VectorCount; ++i){
            const float x = input->x;
            const float y = input->y;
            float transformedZ;
            float reciprocalW;
            transform.Transform2(x, y, output->x, output->y, transformedZ, reciprocalW);
            reciprocalW = 1.0f / reciprocalW;
            output->x *= reciprocalW;
            output->y *= reciprocalW;

            ++input;
            ++output;
        }

        return pOutputStream;
    }

    for(size_t i = 0; i < VectorCount; ++i){
        const auto* input = reinterpret_cast<const Float2*>(pInputVector);
        auto* output = reinterpret_cast<Float2*>(pOutputVector);
        const float x = input->x;
        const float y = input->y;
        float transformedZ;
        float reciprocalW;
        transform.Transform2(x, y, output->x, output->y, transformedZ, reciprocalW);
        reciprocalW = 1.0f / reciprocalW;
        output->x *= reciprocalW;
        output->y *= reciprocalW;

        pInputVector += InputStride;
        pOutputVector += OutputStride;
    }
#else
    const Vector column0 = M.r[0];
    const Vector column1 = M.r[1];
    const Vector column3 = M.r[3];

    for(size_t i = 0; i < VectorCount; ++i){
        Vector V = LoadFloat2(reinterpret_cast<const Float2*>(pInputVector));
        Vector Y = VectorSplatY(V);
        Vector X = VectorSplatX(V);

        Vector Result = VectorMultiplyAdd(Y, column1, column3);
        Result = VectorMultiplyAdd(X, column0, Result);

        Vector W = VectorSplatW(Result);
        Result = VectorMultiply(Result, TransformCoordRefinedReciprocal(W));

    #ifdef _PREFAST_
    #pragma prefast(push)
    #pragma prefast(disable : 26015, "PREfast noise: Esp:1307" )
    #endif

        StoreFloat2(reinterpret_cast<Float2*>(pOutputVector), Result);

    #ifdef _PREFAST_
    #pragma prefast(pop)
    #endif

        pInputVector += InputStride;
        pOutputVector += OutputStride;
    }
#endif

    return pOutputStream;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

