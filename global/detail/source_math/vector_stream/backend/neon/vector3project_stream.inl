// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


_Use_decl_annotations_
inline Float3* MathCallConv Vector3ProjectStreamNeon
(
    Float3* pOutputStream,
    size_t          OutputStride,
    const Float3* pInputStream,
    size_t          InputStride,
    size_t          VectorCount,
    float           ViewportX,
    float           ViewportY,
    float           ViewportWidth,
    float           ViewportHeight,
    float           ViewportMinZ,
    float           ViewportMaxZ,
    FXMMATRIX     Projection,
    CXMMATRIX     View,
    CXMMATRIX     World
)noexcept{
    const float HalfViewportWidth = ViewportWidth * 0.5f;
    const float HalfViewportHeight = ViewportHeight * 0.5f;

    Matrix Transform = BuildProjectTransformColumns(Projection, View, World);

    const uint8_t* pInputVector = reinterpret_cast<const uint8_t*>(pInputStream);
    uint8_t* pOutputVector = reinterpret_cast<uint8_t*>(pOutputStream);

    size_t i = 0;
    const size_t four = VectorCount >> 2;
    if(four > 0){
        if((InputStride == sizeof(Float3)) && (OutputStride == sizeof(Float3))){
            Vector ScaleX = vdupq_n_f32(HalfViewportWidth);
            Vector ScaleY = vdupq_n_f32(-HalfViewportHeight);
            Vector ScaleZ = vdupq_n_f32(ViewportMaxZ - ViewportMinZ);

            Vector OffsetX = vdupq_n_f32(ViewportX + HalfViewportWidth);
            Vector OffsetY = vdupq_n_f32(ViewportY + HalfViewportHeight);
            Vector OffsetZ = vdupq_n_f32(ViewportMinZ);

            for(size_t j = 0; j < four; ++j){
                float32x4x3_t V = vld3q_f32(reinterpret_cast<const float*>(pInputVector));
                pInputVector += sizeof(Float3) * 4;

                float32x2_t r3 = vget_low_f32(Transform.r[3]);
                float32x2_t r = vget_low_f32(Transform.r[0]);
                Vector vResult0 = vmlaq_lane_f32(vdupq_lane_f32(r3, 0), V.val[0], r, 0); // Ax+M
                Vector vResult1 = vmlaq_lane_f32(vdupq_lane_f32(r3, 1), V.val[0], r, 1); // Bx+N

                MATH_PREFETCH(pInputVector);

                r3 = vget_high_f32(Transform.r[3]);
                r = vget_high_f32(Transform.r[0]);
                Vector vResult2 = vmlaq_lane_f32(vdupq_lane_f32(r3, 0), V.val[0], r, 0); // Cx+O
                Vector W = vmlaq_lane_f32(vdupq_lane_f32(r3, 1), V.val[0], r, 1); // Dx+P

                MATH_PREFETCH(pInputVector + MATH_CACHE_LINE_SIZE);

                r = vget_low_f32(Transform.r[1]);
                vResult0 = vmlaq_lane_f32(vResult0, V.val[1], r, 0); // Ax+Ey+M
                vResult1 = vmlaq_lane_f32(vResult1, V.val[1], r, 1); // Bx+Fy+N

                MATH_PREFETCH(pInputVector + (MATH_CACHE_LINE_SIZE * 2));

                r = vget_high_f32(Transform.r[1]);
                vResult2 = vmlaq_lane_f32(vResult2, V.val[1], r, 0); // Cx+Gy+O
                W = vmlaq_lane_f32(W, V.val[1], r, 1); // Dx+Hy+P

                MATH_PREFETCH(pInputVector + (MATH_CACHE_LINE_SIZE * 3));

                r = vget_low_f32(Transform.r[2]);
                vResult0 = vmlaq_lane_f32(vResult0, V.val[2], r, 0); // Ax+Ey+Iz+M
                vResult1 = vmlaq_lane_f32(vResult1, V.val[2], r, 1); // Bx+Fy+Jz+N

                MATH_PREFETCH(pInputVector + (MATH_CACHE_LINE_SIZE * 4));

                r = vget_high_f32(Transform.r[2]);
                vResult2 = vmlaq_lane_f32(vResult2, V.val[2], r, 0); // Cx+Gy+Kz+O
                W = vmlaq_lane_f32(W, V.val[2], r, 1); // Dx+Hy+Lz+P

                MATH_PREFETCH(pInputVector + (MATH_CACHE_LINE_SIZE * 5));

            #if defined(_M_ARM64) || defined(_M_HYBRID_X86_ARM64) || defined(_M_ARM64EC) || __aarch64__
                vResult0 = vdivq_f32(vResult0, W);
                vResult1 = vdivq_f32(vResult1, W);
                vResult2 = vdivq_f32(vResult2, W);
            #else
                            // 2 iterations of Newton-Raphson refinement of reciprocal
                float32x4_t Reciprocal = vrecpeq_f32(W);
                float32x4_t S = vrecpsq_f32(Reciprocal, W);
                Reciprocal = vmulq_f32(S, Reciprocal);
                S = vrecpsq_f32(Reciprocal, W);
                Reciprocal = vmulq_f32(S, Reciprocal);

                vResult0 = vmulq_f32(vResult0, Reciprocal);
                vResult1 = vmulq_f32(vResult1, Reciprocal);
                vResult2 = vmulq_f32(vResult2, Reciprocal);
            #endif

                V.val[0] = vmlaq_f32(OffsetX, vResult0, ScaleX);
                V.val[1] = vmlaq_f32(OffsetY, vResult1, ScaleY);
                V.val[2] = vmlaq_f32(OffsetZ, vResult2, ScaleZ);

                vst3q_f32(reinterpret_cast<float*>(pOutputVector), V);
                pOutputVector += sizeof(Float3) * 4;

                i += 4;
            }
        }
    }

    if(i < VectorCount){
        Vector Scale = VectorSet(HalfViewportWidth, -HalfViewportHeight, ViewportMaxZ - ViewportMinZ, 1.0f);
        Vector Offset = VectorSet(ViewportX + HalfViewportWidth, ViewportY + HalfViewportHeight, ViewportMinZ, 0.0f);

        for(; i < VectorCount; ++i){
            float32x2_t VL = vld1_f32(reinterpret_cast<const float*>(pInputVector));
            float32x2_t zero = vdup_n_f32(0);
            float32x2_t VH = vld1_lane_f32(reinterpret_cast<const float*>(pInputVector) + 2, zero, 0);
            pInputVector += InputStride;

            Vector vResult = vmlaq_lane_f32(Transform.r[3], Transform.r[0], VL, 0); // X
            vResult = vmlaq_lane_f32(vResult, Transform.r[1], VL, 1); // Y
            vResult = vmlaq_lane_f32(vResult, Transform.r[2], VH, 0); // Z

            VH = vget_high_f32(vResult);
            Vector W = vdupq_lane_f32(VH, 1);

        #if defined(_M_ARM64) || defined(_M_HYBRID_X86_ARM64) || defined(_M_ARM64EC) || __aarch64__
            vResult = vdivq_f32(vResult, W);
        #else
                    // 2 iterations of Newton-Raphson refinement of reciprocal for W
            float32x4_t Reciprocal = vrecpeq_f32(W);
            float32x4_t S = vrecpsq_f32(Reciprocal, W);
            Reciprocal = vmulq_f32(S, Reciprocal);
            S = vrecpsq_f32(Reciprocal, W);
            Reciprocal = vmulq_f32(S, Reciprocal);

            vResult = vmulq_f32(vResult, Reciprocal);
        #endif

            vResult = vmlaq_f32(Offset, vResult, Scale);

            VL = vget_low_f32(vResult);
            vst1_f32(reinterpret_cast<float*>(pOutputVector), VL);
            vst1q_lane_f32(reinterpret_cast<float*>(pOutputVector) + 2, vResult, 2);
            pOutputVector += OutputStride;
        }
    }

    return pOutputStream;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

