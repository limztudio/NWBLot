// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


_Use_decl_annotations_
inline Float3* MathCallConv Vector3TransformCoordStreamScalar
(
    Float3* pOutputStream,
    size_t          OutputStride,
    const Float3* pInputStream,
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
    const float m02 = M.r[2].vector4_f32[0];
    const float m12 = M.r[2].vector4_f32[1];
    const float m22 = M.r[2].vector4_f32[2];
    const float m32 = M.r[2].vector4_f32[3];
    const float m03 = M.r[3].vector4_f32[0];
    const float m13 = M.r[3].vector4_f32[1];
    const float m23 = M.r[3].vector4_f32[2];
    const float m33 = M.r[3].vector4_f32[3];

    if((InputStride == sizeof(Float3)) && (OutputStride == sizeof(Float3))){
        const Float3* input = pInputStream;
        Float3* output = pOutputStream;

        for(size_t i = 0; i < VectorCount; ++i){
            const float x = input->x;
            const float y = input->y;
            const float z = input->z;
            const float transformedX = (x * m00) + (y * m01) + (z * m02) + m03;
            const float transformedY = (x * m10) + (y * m11) + (z * m12) + m13;
            const float transformedZ = (x * m20) + (y * m21) + (z * m22) + m23;
            const float w = (x * m30) + (y * m31) + (z * m32) + m33;
            const float reciprocalW = 1.0f / w;

            output->x = transformedX * reciprocalW;
            output->y = transformedY * reciprocalW;
            output->z = transformedZ * reciprocalW;

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
        const float transformedX = (x * m00) + (y * m01) + (z * m02) + m03;
        const float transformedY = (x * m10) + (y * m11) + (z * m12) + m13;
        const float transformedZ = (x * m20) + (y * m21) + (z * m22) + m23;
        const float w = (x * m30) + (y * m31) + (z * m32) + m33;
        const float reciprocalW = 1.0f / w;

        output->x = transformedX * reciprocalW;
        output->y = transformedY * reciprocalW;
        output->z = transformedZ * reciprocalW;

        pInputVector += InputStride;
        pOutputVector += OutputStride;
    }
#else
    const Vector column0 = M.r[0];
    const Vector column1 = M.r[1];
    const Vector column2 = M.r[2];
    const Vector column3 = M.r[3];

    for(size_t i = 0; i < VectorCount; ++i){
        Vector V = LoadFloat3(reinterpret_cast<const Float3*>(pInputVector));
        Vector Z = VectorSplatZ(V);
        Vector Y = VectorSplatY(V);
        Vector X = VectorSplatX(V);

        Vector Result = VectorMultiplyAdd(Z, column2, column3);
        Result = VectorMultiplyAdd(Y, column1, Result);
        Result = VectorMultiplyAdd(X, column0, Result);

        Vector W = VectorSplatW(Result);
        Result = VectorMultiply(Result, TransformCoordRefinedReciprocal(W));

        StoreFloat3(reinterpret_cast<Float3*>(pOutputVector), Result);

        pInputVector += InputStride;
        pOutputVector += OutputStride;
    }
#endif

    return pOutputStream;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
