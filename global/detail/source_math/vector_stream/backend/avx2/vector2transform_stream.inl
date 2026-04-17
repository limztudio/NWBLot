// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


_Use_decl_annotations_
inline Float4* MathCallConv Vector2TransformStreamAvx2
(
    Float4* pOutputStream,
    size_t          OutputStride,
    const Float2* pInputStream,
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
        const __m256 column3 = _mm256_broadcast_ps(&M.r[3]);

        if(InputStride == sizeof(Float2)){
            const bool inputAligned = !(reinterpret_cast<uintptr_t>(pInputVector) & 0x1F);
            if(OutputStride == sizeof(Float4)){
                if(!(reinterpret_cast<uintptr_t>(pOutputStream) & 0x1F) &&
                    ((VectorCount * sizeof(Float4)) >= SourceMathInternal::MATH_CACHE_LINE_SIZE)){
                    // Packed input, aligned & packed output
                    usedStreamingStores = true;
                    if(inputAligned){
                        for(size_t j = 0; j < four; ++j){
                            __m256 VV = _mm256_load_ps(reinterpret_cast<const float*>(pInputVector));
                            pInputVector += sizeof(Float2) * 4;

                            __m256 Y2 = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(3, 3, 3, 3));
                            __m256 X2 = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(2, 2, 2, 2));
                            __m256 Y1 = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(1, 1, 1, 1));
                            __m256 X1 = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(0, 0, 0, 0));

                            __m256 vTempB = _mm256_fmadd_ps(Y1, column1, column3);
                            __m256 vTempB2 = _mm256_fmadd_ps(Y2, column1, column3);
                            __m256 vTempA = _mm256_fmadd_ps(X1, column0, vTempB);
                            __m256 vTempA2 = _mm256_fmadd_ps(X2, column0, vTempB2);

                            X1 = _mm256_permute2f128_ps(vTempA, vTempA2, 0x20);
                            MATH256_STREAM_PS(reinterpret_cast<float*>(pOutputVector), X1);
                            pOutputVector += sizeof(Float4) * 2;

                            X2 = _mm256_permute2f128_ps(vTempA, vTempA2, 0x31);
                            MATH256_STREAM_PS(reinterpret_cast<float*>(pOutputVector), X2);
                            pOutputVector += sizeof(Float4) * 2;

                            i += 4;
                        }
                    }else{
                        for(size_t j = 0; j < four; ++j){
                            __m256 VV = _mm256_loadu_ps(reinterpret_cast<const float*>(pInputVector));
                            pInputVector += sizeof(Float2) * 4;

                            __m256 Y2 = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(3, 3, 3, 3));
                            __m256 X2 = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(2, 2, 2, 2));
                            __m256 Y1 = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(1, 1, 1, 1));
                            __m256 X1 = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(0, 0, 0, 0));

                            __m256 vTempB = _mm256_fmadd_ps(Y1, column1, column3);
                            __m256 vTempB2 = _mm256_fmadd_ps(Y2, column1, column3);
                            __m256 vTempA = _mm256_fmadd_ps(X1, column0, vTempB);
                            __m256 vTempA2 = _mm256_fmadd_ps(X2, column0, vTempB2);

                            X1 = _mm256_permute2f128_ps(vTempA, vTempA2, 0x20);
                            MATH256_STREAM_PS(reinterpret_cast<float*>(pOutputVector), X1);
                            pOutputVector += sizeof(Float4) * 2;

                            X2 = _mm256_permute2f128_ps(vTempA, vTempA2, 0x31);
                            MATH256_STREAM_PS(reinterpret_cast<float*>(pOutputVector), X2);
                            pOutputVector += sizeof(Float4) * 2;

                            i += 4;
                        }
                    }
                }else{
                    // Packed input, packed output
                    if(inputAligned){
                        for(size_t j = 0; j < four; ++j){
                            __m256 VV = _mm256_load_ps(reinterpret_cast<const float*>(pInputVector));
                            pInputVector += sizeof(Float2) * 4;

                            __m256 Y2 = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(3, 3, 3, 3));
                            __m256 X2 = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(2, 2, 2, 2));
                            __m256 Y1 = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(1, 1, 1, 1));
                            __m256 X1 = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(0, 0, 0, 0));

                            __m256 vTempB = _mm256_fmadd_ps(Y1, column1, column3);
                            __m256 vTempB2 = _mm256_fmadd_ps(Y2, column1, column3);
                            __m256 vTempA = _mm256_fmadd_ps(X1, column0, vTempB);
                            __m256 vTempA2 = _mm256_fmadd_ps(X2, column0, vTempB2);

                            X1 = _mm256_permute2f128_ps(vTempA, vTempA2, 0x20);
                            _mm256_storeu_ps(reinterpret_cast<float*>(pOutputVector), X1);
                            pOutputVector += sizeof(Float4) * 2;

                            X2 = _mm256_permute2f128_ps(vTempA, vTempA2, 0x31);
                            _mm256_storeu_ps(reinterpret_cast<float*>(pOutputVector), X2);
                            pOutputVector += sizeof(Float4) * 2;

                            i += 4;
                        }
                    }else{
                        for(size_t j = 0; j < four; ++j){
                            __m256 VV = _mm256_loadu_ps(reinterpret_cast<const float*>(pInputVector));
                            pInputVector += sizeof(Float2) * 4;

                            __m256 Y2 = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(3, 3, 3, 3));
                            __m256 X2 = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(2, 2, 2, 2));
                            __m256 Y1 = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(1, 1, 1, 1));
                            __m256 X1 = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(0, 0, 0, 0));

                            __m256 vTempB = _mm256_fmadd_ps(Y1, column1, column3);
                            __m256 vTempB2 = _mm256_fmadd_ps(Y2, column1, column3);
                            __m256 vTempA = _mm256_fmadd_ps(X1, column0, vTempB);
                            __m256 vTempA2 = _mm256_fmadd_ps(X2, column0, vTempB2);

                            X1 = _mm256_permute2f128_ps(vTempA, vTempA2, 0x20);
                            _mm256_storeu_ps(reinterpret_cast<float*>(pOutputVector), X1);
                            pOutputVector += sizeof(Float4) * 2;

                            X2 = _mm256_permute2f128_ps(vTempA, vTempA2, 0x31);
                            _mm256_storeu_ps(reinterpret_cast<float*>(pOutputVector), X2);
                            pOutputVector += sizeof(Float4) * 2;

                            i += 4;
                        }
                    }
                }
            }else{
                // Packed input, unpacked output
                if(inputAligned){
                    for(size_t j = 0; j < four; ++j){
                        __m256 VV = _mm256_load_ps(reinterpret_cast<const float*>(pInputVector));
                        pInputVector += sizeof(Float2) * 4;

                        __m256 Y2 = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(3, 3, 3, 3));
                        __m256 X2 = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(2, 2, 2, 2));
                        __m256 Y1 = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(1, 1, 1, 1));
                        __m256 X1 = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(0, 0, 0, 0));

                        __m256 vTempB = _mm256_fmadd_ps(Y1, column1, column3);
                        __m256 vTempB2 = _mm256_fmadd_ps(Y2, column1, column3);
                        __m256 vTempA = _mm256_fmadd_ps(X1, column0, vTempB);
                        __m256 vTempA2 = _mm256_fmadd_ps(X2, column0, vTempB2);

                        _mm_storeu_ps(reinterpret_cast<float*>(pOutputVector), _mm256_castps256_ps128(vTempA));
                        pOutputVector += OutputStride;

                        _mm_storeu_ps(reinterpret_cast<float*>(pOutputVector), _mm256_castps256_ps128(vTempA2));
                        pOutputVector += OutputStride;

                        _mm_storeu_ps(reinterpret_cast<float*>(pOutputVector), _mm256_extractf128_ps(vTempA, 1));
                        pOutputVector += OutputStride;

                        _mm_storeu_ps(reinterpret_cast<float*>(pOutputVector), _mm256_extractf128_ps(vTempA2, 1));
                        pOutputVector += OutputStride;

                        i += 4;
                    }
                }else{
                    for(size_t j = 0; j < four; ++j){
                        __m256 VV = _mm256_loadu_ps(reinterpret_cast<const float*>(pInputVector));
                        pInputVector += sizeof(Float2) * 4;

                        __m256 Y2 = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(3, 3, 3, 3));
                        __m256 X2 = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(2, 2, 2, 2));
                        __m256 Y1 = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(1, 1, 1, 1));
                        __m256 X1 = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(0, 0, 0, 0));

                        __m256 vTempB = _mm256_fmadd_ps(Y1, column1, column3);
                        __m256 vTempB2 = _mm256_fmadd_ps(Y2, column1, column3);
                        __m256 vTempA = _mm256_fmadd_ps(X1, column0, vTempB);
                        __m256 vTempA2 = _mm256_fmadd_ps(X2, column0, vTempB2);

                        _mm_storeu_ps(reinterpret_cast<float*>(pOutputVector), _mm256_castps256_ps128(vTempA));
                        pOutputVector += OutputStride;

                        _mm_storeu_ps(reinterpret_cast<float*>(pOutputVector), _mm256_castps256_ps128(vTempA2));
                        pOutputVector += OutputStride;

                        _mm_storeu_ps(reinterpret_cast<float*>(pOutputVector), _mm256_extractf128_ps(vTempA, 1));
                        pOutputVector += OutputStride;

                        _mm_storeu_ps(reinterpret_cast<float*>(pOutputVector), _mm256_extractf128_ps(vTempA2, 1));
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
        const Vector column3 = M.r[3];

        for(; i < VectorCount; ++i){
            Vector V = _mm_castsi128_ps(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(pInputVector)));
            pInputVector += InputStride;

            Vector Y = MATH_PERMUTE_PS(V, _MM_SHUFFLE(1, 1, 1, 1));
            Vector X = MATH_PERMUTE_PS(V, _MM_SHUFFLE(0, 0, 0, 0));

            Vector vTemp = MATH_FMADD_PS(Y, column1, column3);
            vTemp = MATH_FMADD_PS(X, column0, vTemp);

            _mm_storeu_ps(reinterpret_cast<float*>(pOutputVector), vTemp);
            pOutputVector += OutputStride;
        }
    }

    if(usedStreamingStores)
        MATH_SFENCE();

    return pOutputStream;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

