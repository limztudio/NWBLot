// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


_Use_decl_annotations_
inline Float4* MathCallConv Vector2TransformStreamSse
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

    const Vector column0 = M.r[0];
    const Vector column1 = M.r[1];
    const Vector column3 = M.r[3];

    size_t i = 0;
    const size_t two = VectorCount >> 1;
    if(two > 0){
        if(InputStride == sizeof(Float2)){
            const bool inputAligned = !(reinterpret_cast<uintptr_t>(pInputVector) & 0xF);
            if(!(reinterpret_cast<uintptr_t>(pOutputStream) & 0xF) &&
                !(OutputStride & 0xF) &&
                ((VectorCount * sizeof(Float4)) >= SourceMathInternal::MATH_CACHE_LINE_SIZE)){
                // Packed input, aligned output
                usedStreamingStores = true;
                if(inputAligned){
                    for(size_t j = 0; j < two; ++j){
                        Vector V = _mm_load_ps(reinterpret_cast<const float*>(pInputVector));
                        pInputVector += sizeof(Float2) * 2;

                        Vector Y = MATH_PERMUTE_PS(V, _MM_SHUFFLE(1, 1, 1, 1));
                        Vector X = MATH_PERMUTE_PS(V, _MM_SHUFFLE(0, 0, 0, 0));

                        Vector vTemp = MATH_FMADD_PS(Y, column1, column3);
                        vTemp = MATH_FMADD_PS(X, column0, vTemp);

                        MATH_STREAM_PS(reinterpret_cast<float*>(pOutputVector), vTemp);
                        pOutputVector += OutputStride;

                        Y = MATH_PERMUTE_PS(V, _MM_SHUFFLE(3, 3, 3, 3));
                        X = MATH_PERMUTE_PS(V, _MM_SHUFFLE(2, 2, 2, 2));

                        vTemp = MATH_FMADD_PS(Y, column1, column3);
                        vTemp = MATH_FMADD_PS(X, column0, vTemp);

                        MATH_STREAM_PS(reinterpret_cast<float*>(pOutputVector), vTemp);
                        pOutputVector += OutputStride;

                        i += 2;
                    }
                }else{
                    for(size_t j = 0; j < two; ++j){
                        Vector V = _mm_loadu_ps(reinterpret_cast<const float*>(pInputVector));
                        pInputVector += sizeof(Float2) * 2;

                        Vector Y = MATH_PERMUTE_PS(V, _MM_SHUFFLE(1, 1, 1, 1));
                        Vector X = MATH_PERMUTE_PS(V, _MM_SHUFFLE(0, 0, 0, 0));

                        Vector vTemp = MATH_FMADD_PS(Y, column1, column3);
                        vTemp = MATH_FMADD_PS(X, column0, vTemp);

                        MATH_STREAM_PS(reinterpret_cast<float*>(pOutputVector), vTemp);
                        pOutputVector += OutputStride;

                        Y = MATH_PERMUTE_PS(V, _MM_SHUFFLE(3, 3, 3, 3));
                        X = MATH_PERMUTE_PS(V, _MM_SHUFFLE(2, 2, 2, 2));

                        vTemp = MATH_FMADD_PS(Y, column1, column3);
                        vTemp = MATH_FMADD_PS(X, column0, vTemp);

                        MATH_STREAM_PS(reinterpret_cast<float*>(pOutputVector), vTemp);
                        pOutputVector += OutputStride;

                        i += 2;
                    }
                }
            }else{
                // Packed input, unaligned output
                if(inputAligned){
                    for(size_t j = 0; j < two; ++j){
                        Vector V = _mm_load_ps(reinterpret_cast<const float*>(pInputVector));
                        pInputVector += sizeof(Float2) * 2;

                        Vector Y = MATH_PERMUTE_PS(V, _MM_SHUFFLE(1, 1, 1, 1));
                        Vector X = MATH_PERMUTE_PS(V, _MM_SHUFFLE(0, 0, 0, 0));

                        Vector vTemp = MATH_FMADD_PS(Y, column1, column3);
                        vTemp = MATH_FMADD_PS(X, column0, vTemp);

                        _mm_storeu_ps(reinterpret_cast<float*>(pOutputVector), vTemp);
                        pOutputVector += OutputStride;

                        Y = MATH_PERMUTE_PS(V, _MM_SHUFFLE(3, 3, 3, 3));
                        X = MATH_PERMUTE_PS(V, _MM_SHUFFLE(2, 2, 2, 2));

                        vTemp = MATH_FMADD_PS(Y, column1, column3);
                        vTemp = MATH_FMADD_PS(X, column0, vTemp);

                        _mm_storeu_ps(reinterpret_cast<float*>(pOutputVector), vTemp);
                        pOutputVector += OutputStride;

                        i += 2;
                    }
                }else{
                    for(size_t j = 0; j < two; ++j){
                        Vector V = _mm_loadu_ps(reinterpret_cast<const float*>(pInputVector));
                        pInputVector += sizeof(Float2) * 2;

                        Vector Y = MATH_PERMUTE_PS(V, _MM_SHUFFLE(1, 1, 1, 1));
                        Vector X = MATH_PERMUTE_PS(V, _MM_SHUFFLE(0, 0, 0, 0));

                        Vector vTemp = MATH_FMADD_PS(Y, column1, column3);
                        vTemp = MATH_FMADD_PS(X, column0, vTemp);

                        _mm_storeu_ps(reinterpret_cast<float*>(pOutputVector), vTemp);
                        pOutputVector += OutputStride;

                        Y = MATH_PERMUTE_PS(V, _MM_SHUFFLE(3, 3, 3, 3));
                        X = MATH_PERMUTE_PS(V, _MM_SHUFFLE(2, 2, 2, 2));

                        vTemp = MATH_FMADD_PS(Y, column1, column3);
                        vTemp = MATH_FMADD_PS(X, column0, vTemp);

                        _mm_storeu_ps(reinterpret_cast<float*>(pOutputVector), vTemp);
                        pOutputVector += OutputStride;

                        i += 2;
                    }
                }
            }
        }
    }

    if(!(reinterpret_cast<uintptr_t>(pOutputStream) & 0xF) &&
        !(OutputStride & 0xF) &&
        ((VectorCount * sizeof(Float4)) >= SourceMathInternal::MATH_CACHE_LINE_SIZE)){
        usedStreamingStores = true;
        for(; i < VectorCount; ++i){
            Vector V = _mm_castsi128_ps(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(pInputVector)));
            pInputVector += InputStride;

            Vector Y = MATH_PERMUTE_PS(V, _MM_SHUFFLE(1, 1, 1, 1));
            Vector X = MATH_PERMUTE_PS(V, _MM_SHUFFLE(0, 0, 0, 0));

            Vector vTemp = MATH_FMADD_PS(Y, column1, column3);
            vTemp = MATH_FMADD_PS(X, column0, vTemp);

            MATH_STREAM_PS(reinterpret_cast<float*>(pOutputVector), vTemp);
            pOutputVector += OutputStride;
        }
    }else{
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

