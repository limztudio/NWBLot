// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


_Use_decl_annotations_
inline Float4* MathCallConv Vector4TransformStreamNeon
(
    Float4* pOutputStream,
    size_t          OutputStride,
    const Float4* pInputStream,
    size_t          InputStride,
    size_t          VectorCount,
    FXMMATRIX       M
)noexcept{
    const uint8_t* pInputVector = reinterpret_cast<const uint8_t*>(pInputStream);
    uint8_t* pOutputVector = reinterpret_cast<uint8_t*>(pOutputStream);

    const Vector column0 = M.r[0];
    const Vector column1 = M.r[1];
    const Vector column2 = M.r[2];
    const Vector column3 = M.r[3];

    size_t i = 0;
    const size_t four = VectorCount >> 2;
    if(four > 0){
        if((InputStride == sizeof(Float4)) && (OutputStride == sizeof(Float4))){
            for(size_t j = 0; j < four; ++j){
                float32x4x4_t V = vld4q_f32(reinterpret_cast<const float*>(pInputVector));
                pInputVector += sizeof(Float4) * 4;

                float32x2_t r = vget_low_f32(column0);
                Vector vResult0 = vmulq_lane_f32(V.val[0], r, 0); // Ax
                Vector vResult1 = vmulq_lane_f32(V.val[0], r, 1); // Bx

                MATH_PREFETCH(pInputVector);

                r = vget_high_f32(column0);
                Vector vResult2 = vmulq_lane_f32(V.val[0], r, 0); // Cx
                Vector vResult3 = vmulq_lane_f32(V.val[0], r, 1); // Dx

                MATH_PREFETCH(pInputVector + MATH_CACHE_LINE_SIZE);

                r = vget_low_f32(column1);
                vResult0 = vmlaq_lane_f32(vResult0, V.val[1], r, 0); // Ax+Ey
                vResult1 = vmlaq_lane_f32(vResult1, V.val[1], r, 1); // Bx+Fy

                MATH_PREFETCH(pInputVector + (MATH_CACHE_LINE_SIZE * 2));

                r = vget_high_f32(column1);
                vResult2 = vmlaq_lane_f32(vResult2, V.val[1], r, 0); // Cx+Gy
                vResult3 = vmlaq_lane_f32(vResult3, V.val[1], r, 1); // Dx+Hy

                MATH_PREFETCH(pInputVector + (MATH_CACHE_LINE_SIZE * 3));

                r = vget_low_f32(column2);
                vResult0 = vmlaq_lane_f32(vResult0, V.val[2], r, 0); // Ax+Ey+Iz
                vResult1 = vmlaq_lane_f32(vResult1, V.val[2], r, 1); // Bx+Fy+Jz

                MATH_PREFETCH(pInputVector + (MATH_CACHE_LINE_SIZE * 4));

                r = vget_high_f32(column2);
                vResult2 = vmlaq_lane_f32(vResult2, V.val[2], r, 0); // Cx+Gy+Kz
                vResult3 = vmlaq_lane_f32(vResult3, V.val[2], r, 1); // Dx+Hy+Lz

                MATH_PREFETCH(pInputVector + (MATH_CACHE_LINE_SIZE * 5));

                r = vget_low_f32(column3);
                vResult0 = vmlaq_lane_f32(vResult0, V.val[3], r, 0); // Ax+Ey+Iz+Mw
                vResult1 = vmlaq_lane_f32(vResult1, V.val[3], r, 1); // Bx+Fy+Jz+Nw

                MATH_PREFETCH(pInputVector + (MATH_CACHE_LINE_SIZE * 6));

                r = vget_high_f32(column3);
                vResult2 = vmlaq_lane_f32(vResult2, V.val[3], r, 0); // Cx+Gy+Kz+Ow
                vResult3 = vmlaq_lane_f32(vResult3, V.val[3], r, 1); // Dx+Hy+Lz+Pw

                MATH_PREFETCH(pInputVector + (MATH_CACHE_LINE_SIZE * 7));

                V.val[0] = vResult0;
                V.val[1] = vResult1;
                V.val[2] = vResult2;
                V.val[3] = vResult3;

                vst4q_f32(reinterpret_cast<float*>(pOutputVector), V);
                pOutputVector += sizeof(Float4) * 4;

                i += 4;
            }
        }
    }

    for(; i < VectorCount; ++i){
        Vector V = vld1q_f32(reinterpret_cast<const float*>(pInputVector));
        pInputVector += InputStride;

        float32x2_t VL = vget_low_f32(V);
        Vector vResult = vmulq_lane_f32(column0, VL, 0); // X
        vResult = vmlaq_lane_f32(vResult, column1, VL, 1); // Y
        float32x2_t VH = vget_high_f32(V);
        vResult = vmlaq_lane_f32(vResult, column2, VH, 0); // Z
        vResult = vmlaq_lane_f32(vResult, column3, VH, 1); // W

        vst1q_f32(reinterpret_cast<float*>(pOutputVector), vResult);
        pOutputVector += OutputStride;
    }

    return pOutputStream;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

