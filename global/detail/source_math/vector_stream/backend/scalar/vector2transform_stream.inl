// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


_Use_decl_annotations_
inline Float4* MathCallConv Vector2TransformStreamScalar
(
    Float4* pOutputStream,
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
    const float m00 = M.r[0].vector4_f32[0];
    const float m10 = M.r[0].vector4_f32[1];
    const float m20 = M.r[0].vector4_f32[2];
    const float m30 = M.r[0].vector4_f32[3];
    const float m01 = M.r[1].vector4_f32[0];
    const float m11 = M.r[1].vector4_f32[1];
    const float m21 = M.r[1].vector4_f32[2];
    const float m31 = M.r[1].vector4_f32[3];
    const float m03 = M.r[3].vector4_f32[0];
    const float m13 = M.r[3].vector4_f32[1];
    const float m23 = M.r[3].vector4_f32[2];
    const float m33 = M.r[3].vector4_f32[3];

    if((InputStride == sizeof(Float2)) && (OutputStride == sizeof(Float4))){
        const Float2* input = pInputStream;
        Float4* output = pOutputStream;

        for(size_t i = 0; i < VectorCount; ++i){
            const float x = input->x;
            const float y = input->y;

            output->x = (x * m00) + (y * m01) + m03;
            output->y = (x * m10) + (y * m11) + m13;
            output->z = (x * m20) + (y * m21) + m23;
            output->w = (x * m30) + (y * m31) + m33;

            ++input;
            ++output;
        }

        return pOutputStream;
    }

    for(size_t i = 0; i < VectorCount; ++i){
        const auto* input = reinterpret_cast<const Float2*>(pInputVector);
        auto* output = reinterpret_cast<Float4*>(pOutputVector);
        const float x = input->x;
        const float y = input->y;

        output->x = (x * m00) + (y * m01) + m03;
        output->y = (x * m10) + (y * m11) + m13;
        output->z = (x * m20) + (y * m21) + m23;
        output->w = (x * m30) + (y * m31) + m33;

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
