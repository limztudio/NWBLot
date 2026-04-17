// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


_Use_decl_annotations_
inline Float3* MathCallConv Vector3TransformNormalStreamNeon
(
    Float3* pOutputStream,
    size_t          OutputStride,
    const Float3* pInputStream,
    size_t          InputStride,
    size_t          VectorCount,
    FXMMATRIX       M
)noexcept{
    const uint8_t* pInputVector = reinterpret_cast<const uint8_t*>(pInputStream);
    uint8_t* pOutputVector = reinterpret_cast<uint8_t*>(pOutputStream);

    const Vector column0 = M.r[0];
    const Vector column1 = M.r[1];
    const Vector column2 = M.r[2];

    size_t i = 0;
    const size_t four = VectorCount >> 2;
    if(four > 0){
        if((InputStride == sizeof(Float3)) && (OutputStride == sizeof(Float3))){
            for(size_t j = 0; j < four; ++j){
                float32x4x3_t V = vld3q_f32(reinterpret_cast<const float*>(pInputVector));
                pInputVector += sizeof(Float3) * 4;

                float32x2_t r = vget_low_f32(column0);
                Vector vResult0 = vmulq_lane_f32(V.val[0], r, 0); // Ax
                Vector vResult1 = vmulq_lane_f32(V.val[0], r, 1); // Bx

                MATH_PREFETCH(pInputVector);

                r = vget_high_f32(column0);
                Vector vResult2 = vmulq_lane_f32(V.val[0], r, 0); // Cx

                MATH_PREFETCH(pInputVector + MATH_CACHE_LINE_SIZE);

                r = vget_low_f32(column1);
                vResult0 = vmlaq_lane_f32(vResult0, V.val[1], r, 0); // Ax+Ey
                vResult1 = vmlaq_lane_f32(vResult1, V.val[1], r, 1); // Bx+Fy

                MATH_PREFETCH(pInputVector + (MATH_CACHE_LINE_SIZE * 2));

                r = vget_high_f32(column1);
                vResult2 = vmlaq_lane_f32(vResult2, V.val[1], r, 0); // Cx+Gy

                MATH_PREFETCH(pInputVector + (MATH_CACHE_LINE_SIZE * 3));

                r = vget_low_f32(column2);
                vResult0 = vmlaq_lane_f32(vResult0, V.val[2], r, 0); // Ax+Ey+Iz
                vResult1 = vmlaq_lane_f32(vResult1, V.val[2], r, 1); // Bx+Fy+Jz

                MATH_PREFETCH(pInputVector + (MATH_CACHE_LINE_SIZE * 4));

                r = vget_high_f32(column2);
                vResult2 = vmlaq_lane_f32(vResult2, V.val[2], r, 0); // Cx+Gy+Kz

                MATH_PREFETCH(pInputVector + (MATH_CACHE_LINE_SIZE * 5));

                V.val[0] = vResult0;
                V.val[1] = vResult1;
                V.val[2] = vResult2;

                vst3q_f32(reinterpret_cast<float*>(pOutputVector), V);
                pOutputVector += sizeof(Float3) * 4;

                i += 4;
            }
        }
    }

    for(; i < VectorCount; ++i){
        float32x2_t VL = vld1_f32(reinterpret_cast<const float*>(pInputVector));
        float32x2_t zero = vdup_n_f32(0);
        float32x2_t VH = vld1_lane_f32(reinterpret_cast<const float*>(pInputVector) + 2, zero, 0);
        pInputVector += InputStride;

        Vector vResult = vmulq_lane_f32(column0, VL, 0); // X
        vResult = vmlaq_lane_f32(vResult, column1, VL, 1); // Y
        vResult = vmlaq_lane_f32(vResult, column2, VH, 0); // Z

        VL = vget_low_f32(vResult);
        vst1_f32(reinterpret_cast<float*>(pOutputVector), VL);
        vst1q_lane_f32(reinterpret_cast<float*>(pOutputVector) + 2, vResult, 2);
        pOutputVector += OutputStride;
    }

    return pOutputStream;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

