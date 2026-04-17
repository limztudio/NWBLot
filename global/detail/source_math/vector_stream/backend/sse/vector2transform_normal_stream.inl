// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


_Use_decl_annotations_
inline Float2* MathCallConv Vector2TransformNormalStreamSse
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
    bool usedStreamingStores = false;

    const Vector column0 = M.r[0];
    const Vector column1 = M.r[1];

    size_t i = 0;
    const size_t two = VectorCount >> 1;
    if(two > 0){
        if(InputStride == sizeof(Float2)){
            const bool inputAligned = !(reinterpret_cast<uintptr_t>(pInputVector) & 0xF);
            if(OutputStride == sizeof(Float2)){
                if(!(reinterpret_cast<uintptr_t>(pOutputStream) & 0xF) &&
                    ((VectorCount * sizeof(Float2)) >= SourceMathInternal::MATH_CACHE_LINE_SIZE)){
                    // Packed input, aligned & packed output
                    usedStreamingStores = true;
                    if(inputAligned){
                        for(size_t j = 0; j < two; ++j){
                            Vector V = _mm_load_ps(reinterpret_cast<const float*>(pInputVector));
                            pInputVector += sizeof(Float2) * 2;

                            // Result 1
                            Vector Y = MATH_PERMUTE_PS(V, _MM_SHUFFLE(1, 1, 1, 1));
                            Vector X = MATH_PERMUTE_PS(V, _MM_SHUFFLE(0, 0, 0, 0));

                            Vector vTemp = _mm_mul_ps(Y, column1);
                            Vector V1 = MATH_FMADD_PS(X, column0, vTemp);

                            // Result 2
                            Y = MATH_PERMUTE_PS(V, _MM_SHUFFLE(3, 3, 3, 3));
                            X = MATH_PERMUTE_PS(V, _MM_SHUFFLE(2, 2, 2, 2));

                            vTemp = _mm_mul_ps(Y, column1);
                            Vector V2 = MATH_FMADD_PS(X, column0, vTemp);

                            vTemp = _mm_movelh_ps(V1, V2);

                            MATH_STREAM_PS(reinterpret_cast<float*>(pOutputVector), vTemp);
                            pOutputVector += sizeof(Float2) * 2;

                            i += 2;
                        }
                    }else{
                        for(size_t j = 0; j < two; ++j){
                            Vector V = _mm_loadu_ps(reinterpret_cast<const float*>(pInputVector));
                            pInputVector += sizeof(Float2) * 2;

                            // Result 1
                            Vector Y = MATH_PERMUTE_PS(V, _MM_SHUFFLE(1, 1, 1, 1));
                            Vector X = MATH_PERMUTE_PS(V, _MM_SHUFFLE(0, 0, 0, 0));

                            Vector vTemp = _mm_mul_ps(Y, column1);
                            Vector V1 = MATH_FMADD_PS(X, column0, vTemp);

                            // Result 2
                            Y = MATH_PERMUTE_PS(V, _MM_SHUFFLE(3, 3, 3, 3));
                            X = MATH_PERMUTE_PS(V, _MM_SHUFFLE(2, 2, 2, 2));

                            vTemp = _mm_mul_ps(Y, column1);
                            Vector V2 = MATH_FMADD_PS(X, column0, vTemp);

                            vTemp = _mm_movelh_ps(V1, V2);

                            MATH_STREAM_PS(reinterpret_cast<float*>(pOutputVector), vTemp);
                            pOutputVector += sizeof(Float2) * 2;

                            i += 2;
                        }
                    }
                }else{
                    // Packed input, unaligned & packed output
                    if(inputAligned){
                        for(size_t j = 0; j < two; ++j){
                            Vector V = _mm_load_ps(reinterpret_cast<const float*>(pInputVector));
                            pInputVector += sizeof(Float2) * 2;

                            // Result 1
                            Vector Y = MATH_PERMUTE_PS(V, _MM_SHUFFLE(1, 1, 1, 1));
                            Vector X = MATH_PERMUTE_PS(V, _MM_SHUFFLE(0, 0, 0, 0));

                            Vector vTemp = _mm_mul_ps(Y, column1);
                            Vector V1 = MATH_FMADD_PS(X, column0, vTemp);

                            // Result 2
                            Y = MATH_PERMUTE_PS(V, _MM_SHUFFLE(3, 3, 3, 3));
                            X = MATH_PERMUTE_PS(V, _MM_SHUFFLE(2, 2, 2, 2));

                            vTemp = _mm_mul_ps(Y, column1);
                            Vector V2 = MATH_FMADD_PS(X, column0, vTemp);

                            vTemp = _mm_movelh_ps(V1, V2);

                            _mm_storeu_ps(reinterpret_cast<float*>(pOutputVector), vTemp);
                            pOutputVector += sizeof(Float2) * 2;

                            i += 2;
                        }
                    }else{
                        for(size_t j = 0; j < two; ++j){
                            Vector V = _mm_loadu_ps(reinterpret_cast<const float*>(pInputVector));
                            pInputVector += sizeof(Float2) * 2;

                            // Result 1
                            Vector Y = MATH_PERMUTE_PS(V, _MM_SHUFFLE(1, 1, 1, 1));
                            Vector X = MATH_PERMUTE_PS(V, _MM_SHUFFLE(0, 0, 0, 0));

                            Vector vTemp = _mm_mul_ps(Y, column1);
                            Vector V1 = MATH_FMADD_PS(X, column0, vTemp);

                            // Result 2
                            Y = MATH_PERMUTE_PS(V, _MM_SHUFFLE(3, 3, 3, 3));
                            X = MATH_PERMUTE_PS(V, _MM_SHUFFLE(2, 2, 2, 2));

                            vTemp = _mm_mul_ps(Y, column1);
                            Vector V2 = MATH_FMADD_PS(X, column0, vTemp);

                            vTemp = _mm_movelh_ps(V1, V2);

                            _mm_storeu_ps(reinterpret_cast<float*>(pOutputVector), vTemp);
                            pOutputVector += sizeof(Float2) * 2;

                            i += 2;
                        }
                    }
                }
            }else{
                // Packed input, unpacked output
                if(inputAligned){
                    for(size_t j = 0; j < two; ++j){
                        Vector V = _mm_load_ps(reinterpret_cast<const float*>(pInputVector));
                        pInputVector += sizeof(Float2) * 2;

                        // Result 1
                        Vector Y = MATH_PERMUTE_PS(V, _MM_SHUFFLE(1, 1, 1, 1));
                        Vector X = MATH_PERMUTE_PS(V, _MM_SHUFFLE(0, 0, 0, 0));

                        Vector vTemp = _mm_mul_ps(Y, column1);
                        vTemp = MATH_FMADD_PS(X, column0, vTemp);

                        _mm_storel_epi64(reinterpret_cast<__m128i*>(pOutputVector), _mm_castps_si128(vTemp));
                        pOutputVector += OutputStride;

                        // Result 2
                        Y = MATH_PERMUTE_PS(V, _MM_SHUFFLE(3, 3, 3, 3));
                        X = MATH_PERMUTE_PS(V, _MM_SHUFFLE(2, 2, 2, 2));

                        vTemp = _mm_mul_ps(Y, column1);
                        vTemp = MATH_FMADD_PS(X, column0, vTemp);

                        _mm_storel_epi64(reinterpret_cast<__m128i*>(pOutputVector), _mm_castps_si128(vTemp));
                        pOutputVector += OutputStride;

                        i += 2;
                    }
                }else{
                    for(size_t j = 0; j < two; ++j){
                        Vector V = _mm_loadu_ps(reinterpret_cast<const float*>(pInputVector));
                        pInputVector += sizeof(Float2) * 2;

                        // Result 1
                        Vector Y = MATH_PERMUTE_PS(V, _MM_SHUFFLE(1, 1, 1, 1));
                        Vector X = MATH_PERMUTE_PS(V, _MM_SHUFFLE(0, 0, 0, 0));

                        Vector vTemp = _mm_mul_ps(Y, column1);
                        vTemp = MATH_FMADD_PS(X, column0, vTemp);

                        _mm_storel_epi64(reinterpret_cast<__m128i*>(pOutputVector), _mm_castps_si128(vTemp));
                        pOutputVector += OutputStride;

                        // Result 2
                        Y = MATH_PERMUTE_PS(V, _MM_SHUFFLE(3, 3, 3, 3));
                        X = MATH_PERMUTE_PS(V, _MM_SHUFFLE(2, 2, 2, 2));

                        vTemp = _mm_mul_ps(Y, column1);
                        vTemp = MATH_FMADD_PS(X, column0, vTemp);

                        _mm_storel_epi64(reinterpret_cast<__m128i*>(pOutputVector), _mm_castps_si128(vTemp));
                        pOutputVector += OutputStride;

                        i += 2;
                    }
                }
            }
        }
    }

    for(; i < VectorCount; ++i){
        Vector V = _mm_castsi128_ps(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(pInputVector)));
        pInputVector += InputStride;

        Vector Y = MATH_PERMUTE_PS(V, _MM_SHUFFLE(1, 1, 1, 1));
        Vector X = MATH_PERMUTE_PS(V, _MM_SHUFFLE(0, 0, 0, 0));

        Vector vTemp = _mm_mul_ps(Y, column1);
        vTemp = MATH_FMADD_PS(X, column0, vTemp);

        _mm_storel_epi64(reinterpret_cast<__m128i*>(pOutputVector), _mm_castps_si128(vTemp));
        pOutputVector += OutputStride;
    }

    if(usedStreamingStores)
        MATH_SFENCE();

    return pOutputStream;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

