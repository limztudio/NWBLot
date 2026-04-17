// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


_Use_decl_annotations_
inline Float3* MathCallConv Vector3TransformNormalStreamAvx2
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
    bool usedStreamingStores = false;

    size_t i = 0;
    const size_t four = VectorCount >> 2;
    if(four > 0){
        const __m256 column0 = _mm256_broadcast_ps(&M.r[0]);
        const __m256 column1 = _mm256_broadcast_ps(&M.r[1]);
        const __m256 column2 = _mm256_broadcast_ps(&M.r[2]);

        if(InputStride == sizeof(Float3)){
            const bool inputAligned = !(reinterpret_cast<uintptr_t>(pInputVector) & 0xF);
            if(OutputStride == sizeof(Float3)){
                if(!(reinterpret_cast<uintptr_t>(pOutputStream) & 0xF) &&
                    ((VectorCount * sizeof(Float3)) >= SourceMathInternal::MATH_CACHE_LINE_SIZE)){
                    usedStreamingStores = true;
                    if(inputAligned){
                        for(size_t j = 0; j < four; ++j){
                            __m128 V1 = _mm_load_ps(reinterpret_cast<const float*>(pInputVector));
                            __m128 L2 = _mm_load_ps(reinterpret_cast<const float*>(pInputVector + 16));
                            __m128 L3 = _mm_load_ps(reinterpret_cast<const float*>(pInputVector + 32));
                            pInputVector += sizeof(Float3) * 4;

                            MATH3UNPACK3INTO4(V1, L2, L3);

                            const __m256 packed12 = _mm256_insertf128_ps(_mm256_castps128_ps256(V1), V2, 1);
                            const __m256 packed34 = _mm256_insertf128_ps(_mm256_castps128_ps256(V3), V4, 1);

                            __m256 x12 = _mm256_shuffle_ps(packed12, packed12, _MM_SHUFFLE(0, 0, 0, 0));
                            __m256 y12 = _mm256_shuffle_ps(packed12, packed12, _MM_SHUFFLE(1, 1, 1, 1));
                            __m256 z12 = _mm256_shuffle_ps(packed12, packed12, _MM_SHUFFLE(2, 2, 2, 2));
                            __m256 result12 = _mm256_mul_ps(x12, column0);
                            result12 = _mm256_fmadd_ps(y12, column1, result12);
                            result12 = _mm256_fmadd_ps(z12, column2, result12);

                            __m256 x34 = _mm256_shuffle_ps(packed34, packed34, _MM_SHUFFLE(0, 0, 0, 0));
                            __m256 y34 = _mm256_shuffle_ps(packed34, packed34, _MM_SHUFFLE(1, 1, 1, 1));
                            __m256 z34 = _mm256_shuffle_ps(packed34, packed34, _MM_SHUFFLE(2, 2, 2, 2));
                            __m256 result34 = _mm256_mul_ps(x34, column0);
                            result34 = _mm256_fmadd_ps(y34, column1, result34);
                            result34 = _mm256_fmadd_ps(z34, column2, result34);

                            V1 = _mm256_castps256_ps128(result12);
                            V2 = _mm256_extractf128_ps(result12, 1);
                            V3 = _mm256_castps256_ps128(result34);
                            V4 = _mm256_extractf128_ps(result34, 1);

                            Vector vTemp;
                            MATH3PACK4INTO3(vTemp);
                            MATH_STREAM_PS(reinterpret_cast<float*>(pOutputVector), V1);
                            MATH_STREAM_PS(reinterpret_cast<float*>(pOutputVector + 16), vTemp);
                            MATH_STREAM_PS(reinterpret_cast<float*>(pOutputVector + 32), V3);
                            pOutputVector += sizeof(Float3) * 4;

                            i += 4;
                        }
                    }else{
                        for(size_t j = 0; j < four; ++j){
                            __m128 V1 = _mm_loadu_ps(reinterpret_cast<const float*>(pInputVector));
                            __m128 L2 = _mm_loadu_ps(reinterpret_cast<const float*>(pInputVector + 16));
                            __m128 L3 = _mm_loadu_ps(reinterpret_cast<const float*>(pInputVector + 32));
                            pInputVector += sizeof(Float3) * 4;

                            MATH3UNPACK3INTO4(V1, L2, L3);

                            const __m256 packed12 = _mm256_insertf128_ps(_mm256_castps128_ps256(V1), V2, 1);
                            const __m256 packed34 = _mm256_insertf128_ps(_mm256_castps128_ps256(V3), V4, 1);

                            __m256 x12 = _mm256_shuffle_ps(packed12, packed12, _MM_SHUFFLE(0, 0, 0, 0));
                            __m256 y12 = _mm256_shuffle_ps(packed12, packed12, _MM_SHUFFLE(1, 1, 1, 1));
                            __m256 z12 = _mm256_shuffle_ps(packed12, packed12, _MM_SHUFFLE(2, 2, 2, 2));
                            __m256 result12 = _mm256_mul_ps(x12, column0);
                            result12 = _mm256_fmadd_ps(y12, column1, result12);
                            result12 = _mm256_fmadd_ps(z12, column2, result12);

                            __m256 x34 = _mm256_shuffle_ps(packed34, packed34, _MM_SHUFFLE(0, 0, 0, 0));
                            __m256 y34 = _mm256_shuffle_ps(packed34, packed34, _MM_SHUFFLE(1, 1, 1, 1));
                            __m256 z34 = _mm256_shuffle_ps(packed34, packed34, _MM_SHUFFLE(2, 2, 2, 2));
                            __m256 result34 = _mm256_mul_ps(x34, column0);
                            result34 = _mm256_fmadd_ps(y34, column1, result34);
                            result34 = _mm256_fmadd_ps(z34, column2, result34);

                            V1 = _mm256_castps256_ps128(result12);
                            V2 = _mm256_extractf128_ps(result12, 1);
                            V3 = _mm256_castps256_ps128(result34);
                            V4 = _mm256_extractf128_ps(result34, 1);

                            Vector vTemp;
                            MATH3PACK4INTO3(vTemp);
                            MATH_STREAM_PS(reinterpret_cast<float*>(pOutputVector), V1);
                            MATH_STREAM_PS(reinterpret_cast<float*>(pOutputVector + 16), vTemp);
                            MATH_STREAM_PS(reinterpret_cast<float*>(pOutputVector + 32), V3);
                            pOutputVector += sizeof(Float3) * 4;

                            i += 4;
                        }
                    }
                }else{
                    if(inputAligned){
                        for(size_t j = 0; j < four; ++j){
                            __m128 V1 = _mm_load_ps(reinterpret_cast<const float*>(pInputVector));
                            __m128 L2 = _mm_load_ps(reinterpret_cast<const float*>(pInputVector + 16));
                            __m128 L3 = _mm_load_ps(reinterpret_cast<const float*>(pInputVector + 32));
                            pInputVector += sizeof(Float3) * 4;

                            MATH3UNPACK3INTO4(V1, L2, L3);

                            const __m256 packed12 = _mm256_insertf128_ps(_mm256_castps128_ps256(V1), V2, 1);
                            const __m256 packed34 = _mm256_insertf128_ps(_mm256_castps128_ps256(V3), V4, 1);

                            __m256 x12 = _mm256_shuffle_ps(packed12, packed12, _MM_SHUFFLE(0, 0, 0, 0));
                            __m256 y12 = _mm256_shuffle_ps(packed12, packed12, _MM_SHUFFLE(1, 1, 1, 1));
                            __m256 z12 = _mm256_shuffle_ps(packed12, packed12, _MM_SHUFFLE(2, 2, 2, 2));
                            __m256 result12 = _mm256_mul_ps(x12, column0);
                            result12 = _mm256_fmadd_ps(y12, column1, result12);
                            result12 = _mm256_fmadd_ps(z12, column2, result12);

                            __m256 x34 = _mm256_shuffle_ps(packed34, packed34, _MM_SHUFFLE(0, 0, 0, 0));
                            __m256 y34 = _mm256_shuffle_ps(packed34, packed34, _MM_SHUFFLE(1, 1, 1, 1));
                            __m256 z34 = _mm256_shuffle_ps(packed34, packed34, _MM_SHUFFLE(2, 2, 2, 2));
                            __m256 result34 = _mm256_mul_ps(x34, column0);
                            result34 = _mm256_fmadd_ps(y34, column1, result34);
                            result34 = _mm256_fmadd_ps(z34, column2, result34);

                            V1 = _mm256_castps256_ps128(result12);
                            V2 = _mm256_extractf128_ps(result12, 1);
                            V3 = _mm256_castps256_ps128(result34);
                            V4 = _mm256_extractf128_ps(result34, 1);

                            Vector vTemp;
                            MATH3PACK4INTO3(vTemp);
                            const __m256 packedOutput = _mm256_insertf128_ps(_mm256_castps128_ps256(V1), vTemp, 1);
                            _mm256_storeu_ps(reinterpret_cast<float*>(pOutputVector), packedOutput);
                            _mm_storeu_ps(reinterpret_cast<float*>(pOutputVector + 32), V3);
                            pOutputVector += sizeof(Float3) * 4;

                            i += 4;
                        }
                    }else{
                        for(size_t j = 0; j < four; ++j){
                            __m128 V1 = _mm_loadu_ps(reinterpret_cast<const float*>(pInputVector));
                            __m128 L2 = _mm_loadu_ps(reinterpret_cast<const float*>(pInputVector + 16));
                            __m128 L3 = _mm_loadu_ps(reinterpret_cast<const float*>(pInputVector + 32));
                            pInputVector += sizeof(Float3) * 4;

                            MATH3UNPACK3INTO4(V1, L2, L3);

                            const __m256 packed12 = _mm256_insertf128_ps(_mm256_castps128_ps256(V1), V2, 1);
                            const __m256 packed34 = _mm256_insertf128_ps(_mm256_castps128_ps256(V3), V4, 1);

                            __m256 x12 = _mm256_shuffle_ps(packed12, packed12, _MM_SHUFFLE(0, 0, 0, 0));
                            __m256 y12 = _mm256_shuffle_ps(packed12, packed12, _MM_SHUFFLE(1, 1, 1, 1));
                            __m256 z12 = _mm256_shuffle_ps(packed12, packed12, _MM_SHUFFLE(2, 2, 2, 2));
                            __m256 result12 = _mm256_mul_ps(x12, column0);
                            result12 = _mm256_fmadd_ps(y12, column1, result12);
                            result12 = _mm256_fmadd_ps(z12, column2, result12);

                            __m256 x34 = _mm256_shuffle_ps(packed34, packed34, _MM_SHUFFLE(0, 0, 0, 0));
                            __m256 y34 = _mm256_shuffle_ps(packed34, packed34, _MM_SHUFFLE(1, 1, 1, 1));
                            __m256 z34 = _mm256_shuffle_ps(packed34, packed34, _MM_SHUFFLE(2, 2, 2, 2));
                            __m256 result34 = _mm256_mul_ps(x34, column0);
                            result34 = _mm256_fmadd_ps(y34, column1, result34);
                            result34 = _mm256_fmadd_ps(z34, column2, result34);

                            V1 = _mm256_castps256_ps128(result12);
                            V2 = _mm256_extractf128_ps(result12, 1);
                            V3 = _mm256_castps256_ps128(result34);
                            V4 = _mm256_extractf128_ps(result34, 1);

                            Vector vTemp;
                            MATH3PACK4INTO3(vTemp);
                            const __m256 packedOutput = _mm256_insertf128_ps(_mm256_castps128_ps256(V1), vTemp, 1);
                            _mm256_storeu_ps(reinterpret_cast<float*>(pOutputVector), packedOutput);
                            _mm_storeu_ps(reinterpret_cast<float*>(pOutputVector + 32), V3);
                            pOutputVector += sizeof(Float3) * 4;

                            i += 4;
                        }
                    }
                }
            }else{
                if(inputAligned){
                    for(size_t j = 0; j < four; ++j){
                        __m128 V1 = _mm_load_ps(reinterpret_cast<const float*>(pInputVector));
                        __m128 L2 = _mm_load_ps(reinterpret_cast<const float*>(pInputVector + 16));
                        __m128 L3 = _mm_load_ps(reinterpret_cast<const float*>(pInputVector + 32));
                        pInputVector += sizeof(Float3) * 4;

                        MATH3UNPACK3INTO4(V1, L2, L3);

                        const __m256 packed12 = _mm256_insertf128_ps(_mm256_castps128_ps256(V1), V2, 1);
                        const __m256 packed34 = _mm256_insertf128_ps(_mm256_castps128_ps256(V3), V4, 1);

                        __m256 x12 = _mm256_shuffle_ps(packed12, packed12, _MM_SHUFFLE(0, 0, 0, 0));
                        __m256 y12 = _mm256_shuffle_ps(packed12, packed12, _MM_SHUFFLE(1, 1, 1, 1));
                        __m256 z12 = _mm256_shuffle_ps(packed12, packed12, _MM_SHUFFLE(2, 2, 2, 2));
                        __m256 result12 = _mm256_mul_ps(x12, column0);
                        result12 = _mm256_fmadd_ps(y12, column1, result12);
                        result12 = _mm256_fmadd_ps(z12, column2, result12);

                        __m256 x34 = _mm256_shuffle_ps(packed34, packed34, _MM_SHUFFLE(0, 0, 0, 0));
                        __m256 y34 = _mm256_shuffle_ps(packed34, packed34, _MM_SHUFFLE(1, 1, 1, 1));
                        __m256 z34 = _mm256_shuffle_ps(packed34, packed34, _MM_SHUFFLE(2, 2, 2, 2));
                        __m256 result34 = _mm256_mul_ps(x34, column0);
                        result34 = _mm256_fmadd_ps(y34, column1, result34);
                        result34 = _mm256_fmadd_ps(z34, column2, result34);

                        StoreFloat3(reinterpret_cast<Float3*>(pOutputVector), _mm256_castps256_ps128(result12));
                        pOutputVector += OutputStride;

                        StoreFloat3(reinterpret_cast<Float3*>(pOutputVector), _mm256_extractf128_ps(result12, 1));
                        pOutputVector += OutputStride;

                        StoreFloat3(reinterpret_cast<Float3*>(pOutputVector), _mm256_castps256_ps128(result34));
                        pOutputVector += OutputStride;

                        StoreFloat3(reinterpret_cast<Float3*>(pOutputVector), _mm256_extractf128_ps(result34, 1));
                        pOutputVector += OutputStride;

                        i += 4;
                    }
                }else{
                    for(size_t j = 0; j < four; ++j){
                        __m128 V1 = _mm_loadu_ps(reinterpret_cast<const float*>(pInputVector));
                        __m128 L2 = _mm_loadu_ps(reinterpret_cast<const float*>(pInputVector + 16));
                        __m128 L3 = _mm_loadu_ps(reinterpret_cast<const float*>(pInputVector + 32));
                        pInputVector += sizeof(Float3) * 4;

                        MATH3UNPACK3INTO4(V1, L2, L3);

                        const __m256 packed12 = _mm256_insertf128_ps(_mm256_castps128_ps256(V1), V2, 1);
                        const __m256 packed34 = _mm256_insertf128_ps(_mm256_castps128_ps256(V3), V4, 1);

                        __m256 x12 = _mm256_shuffle_ps(packed12, packed12, _MM_SHUFFLE(0, 0, 0, 0));
                        __m256 y12 = _mm256_shuffle_ps(packed12, packed12, _MM_SHUFFLE(1, 1, 1, 1));
                        __m256 z12 = _mm256_shuffle_ps(packed12, packed12, _MM_SHUFFLE(2, 2, 2, 2));
                        __m256 result12 = _mm256_mul_ps(x12, column0);
                        result12 = _mm256_fmadd_ps(y12, column1, result12);
                        result12 = _mm256_fmadd_ps(z12, column2, result12);

                        __m256 x34 = _mm256_shuffle_ps(packed34, packed34, _MM_SHUFFLE(0, 0, 0, 0));
                        __m256 y34 = _mm256_shuffle_ps(packed34, packed34, _MM_SHUFFLE(1, 1, 1, 1));
                        __m256 z34 = _mm256_shuffle_ps(packed34, packed34, _MM_SHUFFLE(2, 2, 2, 2));
                        __m256 result34 = _mm256_mul_ps(x34, column0);
                        result34 = _mm256_fmadd_ps(y34, column1, result34);
                        result34 = _mm256_fmadd_ps(z34, column2, result34);

                        StoreFloat3(reinterpret_cast<Float3*>(pOutputVector), _mm256_castps256_ps128(result12));
                        pOutputVector += OutputStride;

                        StoreFloat3(reinterpret_cast<Float3*>(pOutputVector), _mm256_extractf128_ps(result12, 1));
                        pOutputVector += OutputStride;

                        StoreFloat3(reinterpret_cast<Float3*>(pOutputVector), _mm256_castps256_ps128(result34));
                        pOutputVector += OutputStride;

                        StoreFloat3(reinterpret_cast<Float3*>(pOutputVector), _mm256_extractf128_ps(result34, 1));
                        pOutputVector += OutputStride;

                        i += 4;
                    }
                }
            }
        }
    }

    if(i < VectorCount){
        const Vector column0 = M.r[0];
        const Vector column1 = M.r[1];
        const Vector column2 = M.r[2];

        for(; i < VectorCount; ++i){
            Vector V = LoadFloat3(reinterpret_cast<const Float3*>(pInputVector));
            pInputVector += InputStride;

            Vector Z = MATH_PERMUTE_PS(V, _MM_SHUFFLE(2, 2, 2, 2));
            Vector Y = MATH_PERMUTE_PS(V, _MM_SHUFFLE(1, 1, 1, 1));
            Vector X = MATH_PERMUTE_PS(V, _MM_SHUFFLE(0, 0, 0, 0));

            Vector result = _mm_mul_ps(X, column0);
            result = MATH_FMADD_PS(Y, column1, result);
            result = MATH_FMADD_PS(Z, column2, result);

            StoreFloat3(reinterpret_cast<Float3*>(pOutputVector), result);
            pOutputVector += OutputStride;
        }
    }

    if(usedStreamingStores)
        MATH_SFENCE();

    return pOutputStream;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
