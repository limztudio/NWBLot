// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


_Use_decl_annotations_
inline Float2* MathCallConv Vector2TransformNormalStreamNeon
(
    Float2* pOutputStream,
    size_t          OutputStride,
    const Float2* pInputStream,
    size_t          InputStride,
    size_t          VectorCount,
    FXMMATRIX       M
)noexcept{
    const uint8_t* pInputVector = reinterpret_cast<const uint8_t*>(pInputStream);
    uint8_t* pOutputVector = reinterpret_cast<uint8_t*>(pOutputStream);

    const Vector column0 = M.r[0];
    const Vector column1 = M.r[1];

    size_t i = 0;
    const size_t four = VectorCount >> 2;
    if(four > 0){
        if((InputStride == sizeof(Float2)) && (OutputStride == sizeof(Float2))){
            for(size_t j = 0; j < four; ++j){
                float32x4x2_t V = vld2q_f32(reinterpret_cast<const float*>(pInputVector));
                pInputVector += sizeof(Float2) * 4;

                float32x2_t r = vget_low_f32(column0);
                Vector vResult0 = vmulq_lane_f32(V.val[0], r, 0); // Ax
                Vector vResult1 = vmulq_lane_f32(V.val[0], r, 1); // Bx

                MATH_PREFETCH(pInputVector);
                MATH_PREFETCH(pInputVector + MATH_CACHE_LINE_SIZE);

                r = vget_low_f32(column1);
                vResult0 = vmlaq_lane_f32(vResult0, V.val[1], r, 0); // Ax+Ey
                vResult1 = vmlaq_lane_f32(vResult1, V.val[1], r, 1); // Bx+Fy

                MATH_PREFETCH(pInputVector + (MATH_CACHE_LINE_SIZE * 2));
                MATH_PREFETCH(pInputVector + (MATH_CACHE_LINE_SIZE * 3));

                V.val[0] = vResult0;
                V.val[1] = vResult1;

                vst2q_f32(reinterpret_cast<float*>(pOutputVector), V);
                pOutputVector += sizeof(Float2) * 4;

                i += 4;
            }
        }
    }

    for(; i < VectorCount; ++i){
        float32x2_t V = vld1_f32(reinterpret_cast<const float*>(pInputVector));
        pInputVector += InputStride;

        Vector vResult = vmulq_lane_f32(column0, V, 0); // X
        vResult = vmlaq_lane_f32(vResult, column1, V, 1); // Y

        V = vget_low_f32(vResult);
        vst1_f32(reinterpret_cast<float*>(pOutputVector), V);
        pOutputVector += OutputStride;
    }

    return pOutputStream;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

