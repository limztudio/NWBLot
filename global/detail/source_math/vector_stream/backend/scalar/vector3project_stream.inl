// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


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

    const float HalfViewportWidth = ViewportWidth * 0.5f;
    const float HalfViewportHeight = ViewportHeight * 0.5f;
    Matrix Transform = BuildProjectTransformColumns(Projection, View, World);

    const uint8_t* pInputVector = reinterpret_cast<const uint8_t*>(pInputStream);
    uint8_t* pOutputVector = reinterpret_cast<uint8_t*>(pOutputStream);

#if defined(_MATH_NO_INTRINSICS_)
    const float m00 = Transform.r[0].vector4_f32[0];
    const float m10 = Transform.r[0].vector4_f32[1];
    const float m20 = Transform.r[0].vector4_f32[2];
    const float m30 = Transform.r[0].vector4_f32[3];
    const float m01 = Transform.r[1].vector4_f32[0];
    const float m11 = Transform.r[1].vector4_f32[1];
    const float m21 = Transform.r[1].vector4_f32[2];
    const float m31 = Transform.r[1].vector4_f32[3];
    const float m02 = Transform.r[2].vector4_f32[0];
    const float m12 = Transform.r[2].vector4_f32[1];
    const float m22 = Transform.r[2].vector4_f32[2];
    const float m32 = Transform.r[2].vector4_f32[3];
    const float m03 = Transform.r[3].vector4_f32[0];
    const float m13 = Transform.r[3].vector4_f32[1];
    const float m23 = Transform.r[3].vector4_f32[2];
    const float m33 = Transform.r[3].vector4_f32[3];
    const float scaleX = HalfViewportWidth;
    const float scaleY = -HalfViewportHeight;
    const float scaleZ = ViewportMaxZ - ViewportMinZ;
    const float offsetX = ViewportX + HalfViewportWidth;
    const float offsetY = ViewportY + HalfViewportHeight;
    const float offsetZ = ViewportMinZ;

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

            output->x = (transformedX * reciprocalW * scaleX) + offsetX;
            output->y = (transformedY * reciprocalW * scaleY) + offsetY;
            output->z = (transformedZ * reciprocalW * scaleZ) + offsetZ;

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

        output->x = (transformedX * reciprocalW * scaleX) + offsetX;
        output->y = (transformedY * reciprocalW * scaleY) + offsetY;
        output->z = (transformedZ * reciprocalW * scaleZ) + offsetZ;

        pInputVector += InputStride;
        pOutputVector += OutputStride;
    }
#else
    Vector Scale = VectorSet(HalfViewportWidth, -HalfViewportHeight, ViewportMaxZ - ViewportMinZ, 1.0f);
    Vector Offset = VectorSet(ViewportX + HalfViewportWidth, ViewportY + HalfViewportHeight, ViewportMinZ, 0.0f);

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

