// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef _PREFAST_
#pragma prefast(push)
#pragma prefast(disable : 26015 26019, "PREfast noise: Esp:1307" )
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


_Use_decl_annotations_
inline Float4* MathCallConv Vector3TransformStreamNeon
(
    Float4* pOutputStream,
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
    const Vector column3 = M.r[3];

    size_t i = 0;
    const size_t four = VectorCount >> 2;
    if(four > 0){
        if((InputStride == sizeof(Float3)) && (OutputStride == sizeof(Float4))){
            for(size_t j = 0; j < four; ++j){
                float32x4x3_t V = vld3q_f32(reinterpret_cast<const float*>(pInputVector));
                pInputVector += sizeof(Float3) * 4;

                float32x2_t r3 = vget_low_f32(column3);
                float32x2_t r = vget_low_f32(column0);
                Vector vResult0 = vmlaq_lane_f32(vdupq_lane_f32(r3, 0), V.val[0], r, 0); // Ax+M
                Vector vResult1 = vmlaq_lane_f32(vdupq_lane_f32(r3, 1), V.val[0], r, 1); // Bx+N

                MATH_PREFETCH(pInputVector);

                r3 = vget_high_f32(column3);
                r = vget_high_f32(column0);
                Vector vResult2 = vmlaq_lane_f32(vdupq_lane_f32(r3, 0), V.val[0], r, 0); // Cx+O
                Vector vResult3 = vmlaq_lane_f32(vdupq_lane_f32(r3, 1), V.val[0], r, 1); // Dx+P

                MATH_PREFETCH(pInputVector + MATH_CACHE_LINE_SIZE);

                r = vget_low_f32(column1);
                vResult0 = vmlaq_lane_f32(vResult0, V.val[1], r, 0); // Ax+Ey+M
                vResult1 = vmlaq_lane_f32(vResult1, V.val[1], r, 1); // Bx+Fy+N

                MATH_PREFETCH(pInputVector + (MATH_CACHE_LINE_SIZE * 2));

                r = vget_high_f32(column1);
                vResult2 = vmlaq_lane_f32(vResult2, V.val[1], r, 0); // Cx+Gy+O
                vResult3 = vmlaq_lane_f32(vResult3, V.val[1], r, 1); // Dx+Hy+P

                MATH_PREFETCH(pInputVector + (MATH_CACHE_LINE_SIZE * 3));

                r = vget_low_f32(column2);
                vResult0 = vmlaq_lane_f32(vResult0, V.val[2], r, 0); // Ax+Ey+Iz+M
                vResult1 = vmlaq_lane_f32(vResult1, V.val[2], r, 1); // Bx+Fy+Jz+N

                MATH_PREFETCH(pInputVector + (MATH_CACHE_LINE_SIZE * 4));

                r = vget_high_f32(column2);
                vResult2 = vmlaq_lane_f32(vResult2, V.val[2], r, 0); // Cx+Gy+Kz+O
                vResult3 = vmlaq_lane_f32(vResult3, V.val[2], r, 1); // Dx+Hy+Lz+P

                MATH_PREFETCH(pInputVector + (MATH_CACHE_LINE_SIZE * 5));

                float32x4x4_t R;
                R.val[0] = vResult0;
                R.val[1] = vResult1;
                R.val[2] = vResult2;
                R.val[3] = vResult3;

                vst4q_f32(reinterpret_cast<float*>(pOutputVector), R);
                pOutputVector += sizeof(Float4) * 4;

                i += 4;
            }
        }
    }

    for(; i < VectorCount; ++i){
        float32x2_t VL = vld1_f32(reinterpret_cast<const float*>(pInputVector));
        float32x2_t zero = vdup_n_f32(0);
        float32x2_t VH = vld1_lane_f32(reinterpret_cast<const float*>(pInputVector) + 2, zero, 0);
        pInputVector += InputStride;

        Vector vResult = vmlaq_lane_f32(column3, column0, VL, 0); // X
        vResult = vmlaq_lane_f32(vResult, column1, VL, 1); // Y
        vResult = vmlaq_lane_f32(vResult, column2, VH, 0); // Z

        vst1q_f32(reinterpret_cast<float*>(pOutputVector), vResult);
        pOutputVector += OutputStride;
    }

    return pOutputStream;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef _PREFAST_
#pragma prefast(pop)
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

