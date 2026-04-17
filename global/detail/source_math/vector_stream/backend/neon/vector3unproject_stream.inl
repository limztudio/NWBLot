// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


_Use_decl_annotations_
inline Float3* MathCallConv Vector3UnprojectStreamNeon
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
    FXMMATRIX       Projection,
    CXMMATRIX       View,
    CXMMATRIX       World
)noexcept{
    Matrix Transform = BuildProjectTransformColumns(Projection, View, World);
    Transform = MatrixInverse(nullptr, Transform);

    const uint8_t* pInputVector = reinterpret_cast<const uint8_t*>(pInputStream);
    uint8_t* pOutputVector = reinterpret_cast<uint8_t*>(pOutputStream);

    float sx = 1.f / (ViewportWidth * 0.5f);
    float sy = 1.f / (-ViewportHeight * 0.5f);
    float sz = 1.f / (ViewportMaxZ - ViewportMinZ);

    float ox = (-ViewportX * sx) - 1.f;
    float oy = (-ViewportY * sy) + 1.f;
    float oz = (-ViewportMinZ * sz);

    size_t i = 0;
    const size_t four = VectorCount >> 2;
    if(four > 0){
        if((InputStride == sizeof(Float3)) && (OutputStride == sizeof(Float3))){
            for(size_t j = 0; j < four; ++j){
                float32x4x3_t V = vld3q_f32(reinterpret_cast<const float*>(pInputVector));
                pInputVector += sizeof(Float3) * 4;

                Vector ScaleX = vdupq_n_f32(sx);
                Vector OffsetX = vdupq_n_f32(ox);
                Vector VX = vmlaq_f32(OffsetX, ScaleX, V.val[0]);

                float32x2_t r3 = vget_low_f32(Transform.r[3]);
                float32x2_t r = vget_low_f32(Transform.r[0]);
                Vector vResult0 = vmlaq_lane_f32(vdupq_lane_f32(r3, 0), VX, r, 0); // Ax+M
                Vector vResult1 = vmlaq_lane_f32(vdupq_lane_f32(r3, 1), VX, r, 1); // Bx+N

                MATH_PREFETCH(pInputVector);

                r3 = vget_high_f32(Transform.r[3]);
                r = vget_high_f32(Transform.r[0]);
                Vector vResult2 = vmlaq_lane_f32(vdupq_lane_f32(r3, 0), VX, r, 0); // Cx+O
                Vector W = vmlaq_lane_f32(vdupq_lane_f32(r3, 1), VX, r, 1); // Dx+P

                MATH_PREFETCH(pInputVector + MATH_CACHE_LINE_SIZE);

                Vector ScaleY = vdupq_n_f32(sy);
                Vector OffsetY = vdupq_n_f32(oy);
                Vector VY = vmlaq_f32(OffsetY, ScaleY, V.val[1]);

                r = vget_low_f32(Transform.r[1]);
                vResult0 = vmlaq_lane_f32(vResult0, VY, r, 0); // Ax+Ey+M
                vResult1 = vmlaq_lane_f32(vResult1, VY, r, 1); // Bx+Fy+N

                MATH_PREFETCH(pInputVector + (MATH_CACHE_LINE_SIZE * 2));

                r = vget_high_f32(Transform.r[1]);
                vResult2 = vmlaq_lane_f32(vResult2, VY, r, 0); // Cx+Gy+O
                W = vmlaq_lane_f32(W, VY, r, 1); // Dx+Hy+P

                MATH_PREFETCH(pInputVector + (MATH_CACHE_LINE_SIZE * 3));

                Vector ScaleZ = vdupq_n_f32(sz);
                Vector OffsetZ = vdupq_n_f32(oz);
                Vector VZ = vmlaq_f32(OffsetZ, ScaleZ, V.val[2]);

                r = vget_low_f32(Transform.r[2]);
                vResult0 = vmlaq_lane_f32(vResult0, VZ, r, 0); // Ax+Ey+Iz+M
                vResult1 = vmlaq_lane_f32(vResult1, VZ, r, 1); // Bx+Fy+Jz+N

                MATH_PREFETCH(pInputVector + (MATH_CACHE_LINE_SIZE * 4));

                r = vget_high_f32(Transform.r[2]);
                vResult2 = vmlaq_lane_f32(vResult2, VZ, r, 0); // Cx+Gy+Kz+O
                W = vmlaq_lane_f32(W, VZ, r, 1); // Dx+Hy+Lz+P

                MATH_PREFETCH(pInputVector + (MATH_CACHE_LINE_SIZE * 5));

            #if defined(_M_ARM64) || defined(_M_HYBRID_X86_ARM64) || defined(_M_ARM64EC) || __aarch64__
                V.val[0] = vdivq_f32(vResult0, W);
                V.val[1] = vdivq_f32(vResult1, W);
                V.val[2] = vdivq_f32(vResult2, W);
            #else
                            // 2 iterations of Newton-Raphson refinement of reciprocal
                float32x4_t Reciprocal = vrecpeq_f32(W);
                float32x4_t S = vrecpsq_f32(Reciprocal, W);
                Reciprocal = vmulq_f32(S, Reciprocal);
                S = vrecpsq_f32(Reciprocal, W);
                Reciprocal = vmulq_f32(S, Reciprocal);

                V.val[0] = vmulq_f32(vResult0, Reciprocal);
                V.val[1] = vmulq_f32(vResult1, Reciprocal);
                V.val[2] = vmulq_f32(vResult2, Reciprocal);
            #endif

                vst3q_f32(reinterpret_cast<float*>(pOutputVector), V);
                pOutputVector += sizeof(Float3) * 4;

                i += 4;
            }
        }
    }

    if(i < VectorCount){
        float32x2_t ScaleL = vcreate_f32(
            static_cast<uint64_t>(*reinterpret_cast<const uint32_t*>(&sx))
            | (static_cast<uint64_t>(*reinterpret_cast<const uint32_t*>(&sy)) << 32));
        float32x2_t ScaleH = vcreate_f32(static_cast<uint64_t>(*reinterpret_cast<const uint32_t*>(&sz)));

        float32x2_t OffsetL = vcreate_f32(
            static_cast<uint64_t>(*reinterpret_cast<const uint32_t*>(&ox))
            | (static_cast<uint64_t>(*reinterpret_cast<const uint32_t*>(&oy)) << 32));
        float32x2_t OffsetH = vcreate_f32(static_cast<uint64_t>(*reinterpret_cast<const uint32_t*>(&oz)));

        for(; i < VectorCount; ++i){
            float32x2_t VL = vld1_f32(reinterpret_cast<const float*>(pInputVector));
            float32x2_t zero = vdup_n_f32(0);
            float32x2_t VH = vld1_lane_f32(reinterpret_cast<const float*>(pInputVector) + 2, zero, 0);
            pInputVector += InputStride;

            VL = vmla_f32(OffsetL, VL, ScaleL);
            VH = vmla_f32(OffsetH, VH, ScaleH);

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

            VL = vget_low_f32(vResult);
            vst1_f32(reinterpret_cast<float*>(pOutputVector), VL);
            vst1q_lane_f32(reinterpret_cast<float*>(pOutputVector) + 2, vResult, 2);
            pOutputVector += OutputStride;
        }
    }

    return pOutputStream;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

