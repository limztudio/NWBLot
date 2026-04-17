// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scalar_transform_common.inl"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


_Use_decl_annotations_
inline Float4* MathCallConv Vector4TransformStreamScalar
(
    Float4* pOutputStream,
    size_t  OutputStride,
    const Float4* pInputStream,
    size_t  InputStride,
    size_t  VectorCount,
    FXMMATRIX M
)noexcept{
    if(VectorCount == 0){
        return pOutputStream;
    }

    const uint8_t* pInputVector = reinterpret_cast<const uint8_t*>(pInputStream);
    uint8_t* pOutputVector = reinterpret_cast<uint8_t*>(pOutputStream);

#if defined(_MATH_NO_INTRINSICS_)
    const ScalarVectorStreamDetail::MatrixColumns transform(M);

    if(ScalarVectorStreamDetail::HasTightStride<Float4, Float4>(InputStride, OutputStride)){
        const Float4* input = pInputStream;
        Float4* output = pOutputStream;

        for(size_t i = 0; i < VectorCount; ++i){
            const float x = input->x;
            const float y = input->y;
            const float z = input->z;
            const float w = input->w;

            transform.Transform4(x, y, z, w, output->x, output->y, output->z, output->w);

            ++input;
            ++output;
        }

        return pOutputStream;
    }

    for(size_t i = 0; i < VectorCount; ++i){
        const auto* input = reinterpret_cast<const Float4*>(pInputVector);
        auto* output = reinterpret_cast<Float4*>(pOutputVector);
        const float x = input->x;
        const float y = input->y;
        const float z = input->z;
        const float w = input->w;

        transform.Transform4(x, y, z, w, output->x, output->y, output->z, output->w);

        pInputVector += InputStride;
        pOutputVector += OutputStride;
    }
#else
    const Vector column0 = M.r[0];
    const Vector column1 = M.r[1];
    const Vector column2 = M.r[2];
    const Vector column3 = M.r[3];

    for(size_t i = 0; i < VectorCount; ++i){
        Vector V = LoadFloat4(reinterpret_cast<const Float4*>(pInputVector));
        Vector W = VectorSplatW(V);
        Vector Z = VectorSplatZ(V);
        Vector Y = VectorSplatY(V);
        Vector X = VectorSplatX(V);

        Vector Result = VectorMultiply(W, column3);
        Result = VectorMultiplyAdd(Z, column2, Result);
        Result = VectorMultiplyAdd(Y, column1, Result);
        Result = VectorMultiplyAdd(X, column0, Result);

    #ifdef _PREFAST_
    #pragma prefast(push)
    #pragma prefast(disable : 26015, "PREfast noise: Esp:1307" )
    #endif

        StoreFloat4(reinterpret_cast<Float4*>(pOutputVector), Result);

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

