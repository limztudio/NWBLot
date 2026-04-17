// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


_Use_decl_annotations_
inline Float4* MathCallConv Vector4TransformStreamAvx2
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
    bool usedStreamingStores = false;

    size_t i = 0;
    const size_t two = VectorCount >> 1;
    if(two > 0){
        const __m256 column0 = _mm256_broadcast_ps(&M.r[0]);
        const __m256 column1 = _mm256_broadcast_ps(&M.r[1]);
        const __m256 column2 = _mm256_broadcast_ps(&M.r[2]);
        const __m256 column3 = _mm256_broadcast_ps(&M.r[3]);

        if(InputStride == sizeof(Float4)){
            const bool inputAligned = !(reinterpret_cast<uintptr_t>(pInputVector) & 0x1F);
            if(OutputStride == sizeof(Float4)){
                if(!(reinterpret_cast<uintptr_t>(pOutputStream) & 0x1F) &&
                    ((VectorCount * sizeof(Float4)) >= SourceMathInternal::MATH_CACHE_LINE_SIZE)){
                    // Packed input, aligned & packed output
                    usedStreamingStores = true;
                    if(inputAligned){
                        for(size_t j = 0; j < two; ++j){
                            __m256 VV = _mm256_load_ps(reinterpret_cast<const float*>(pInputVector));
                            pInputVector += sizeof(Float4) * 2;

                            __m256 vTempX = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(0, 0, 0, 0));
                            __m256 vTempY = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(1, 1, 1, 1));
                            __m256 vTempZ = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(2, 2, 2, 2));
                            __m256 vTempW = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(3, 3, 3, 3));

                            vTempX = _mm256_mul_ps(vTempX, column0);
                            vTempX = _mm256_fmadd_ps(vTempY, column1, vTempX);
                            vTempX = _mm256_fmadd_ps(vTempZ, column2, vTempX);
                            vTempX = _mm256_fmadd_ps(vTempW, column3, vTempX);

                            MATH256_STREAM_PS(reinterpret_cast<float*>(pOutputVector), vTempX);
                            pOutputVector += sizeof(Float4) * 2;

                            i += 2;
                        }
                    }else{
                        for(size_t j = 0; j < two; ++j){
                            __m256 VV = _mm256_loadu_ps(reinterpret_cast<const float*>(pInputVector));
                            pInputVector += sizeof(Float4) * 2;

                            __m256 vTempX = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(0, 0, 0, 0));
                            __m256 vTempY = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(1, 1, 1, 1));
                            __m256 vTempZ = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(2, 2, 2, 2));
                            __m256 vTempW = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(3, 3, 3, 3));

                            vTempX = _mm256_mul_ps(vTempX, column0);
                            vTempX = _mm256_fmadd_ps(vTempY, column1, vTempX);
                            vTempX = _mm256_fmadd_ps(vTempZ, column2, vTempX);
                            vTempX = _mm256_fmadd_ps(vTempW, column3, vTempX);

                            MATH256_STREAM_PS(reinterpret_cast<float*>(pOutputVector), vTempX);
                            pOutputVector += sizeof(Float4) * 2;

                            i += 2;
                        }
                    }
                }else{
                    // Packed input, packed output
                    if(inputAligned){
                        for(size_t j = 0; j < two; ++j){
                            __m256 VV = _mm256_load_ps(reinterpret_cast<const float*>(pInputVector));
                            pInputVector += sizeof(Float4) * 2;

                            __m256 vTempX = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(0, 0, 0, 0));
                            __m256 vTempY = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(1, 1, 1, 1));
                            __m256 vTempZ = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(2, 2, 2, 2));
                            __m256 vTempW = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(3, 3, 3, 3));

                            vTempX = _mm256_mul_ps(vTempX, column0);
                            vTempX = _mm256_fmadd_ps(vTempY, column1, vTempX);
                            vTempX = _mm256_fmadd_ps(vTempZ, column2, vTempX);
                            vTempX = _mm256_fmadd_ps(vTempW, column3, vTempX);

                            _mm256_storeu_ps(reinterpret_cast<float*>(pOutputVector), vTempX);
                            pOutputVector += sizeof(Float4) * 2;

                            i += 2;
                        }
                    }else{
                        for(size_t j = 0; j < two; ++j){
                            __m256 VV = _mm256_loadu_ps(reinterpret_cast<const float*>(pInputVector));
                            pInputVector += sizeof(Float4) * 2;

                            __m256 vTempX = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(0, 0, 0, 0));
                            __m256 vTempY = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(1, 1, 1, 1));
                            __m256 vTempZ = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(2, 2, 2, 2));
                            __m256 vTempW = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(3, 3, 3, 3));

                            vTempX = _mm256_mul_ps(vTempX, column0);
                            vTempX = _mm256_fmadd_ps(vTempY, column1, vTempX);
                            vTempX = _mm256_fmadd_ps(vTempZ, column2, vTempX);
                            vTempX = _mm256_fmadd_ps(vTempW, column3, vTempX);

                            _mm256_storeu_ps(reinterpret_cast<float*>(pOutputVector), vTempX);
                            pOutputVector += sizeof(Float4) * 2;

                            i += 2;
                        }
                    }
                }
            }else{
                // Packed input, unpacked output
                if(inputAligned){
                    for(size_t j = 0; j < two; ++j){
                        __m256 VV = _mm256_load_ps(reinterpret_cast<const float*>(pInputVector));
                        pInputVector += sizeof(Float4) * 2;

                        __m256 vTempX = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(0, 0, 0, 0));
                        __m256 vTempY = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(1, 1, 1, 1));
                        __m256 vTempZ = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(2, 2, 2, 2));
                        __m256 vTempW = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(3, 3, 3, 3));

                        vTempX = _mm256_mul_ps(vTempX, column0);
                        vTempX = _mm256_fmadd_ps(vTempY, column1, vTempX);
                        vTempX = _mm256_fmadd_ps(vTempZ, column2, vTempX);
                        vTempX = _mm256_fmadd_ps(vTempW, column3, vTempX);

                        _mm_storeu_ps(reinterpret_cast<float*>(pOutputVector), _mm256_castps256_ps128(vTempX));
                        pOutputVector += OutputStride;

                        _mm_storeu_ps(reinterpret_cast<float*>(pOutputVector), _mm256_extractf128_ps(vTempX, 1));
                        pOutputVector += OutputStride;
                        i += 2;
                    }
                }else{
                    for(size_t j = 0; j < two; ++j){
                        __m256 VV = _mm256_loadu_ps(reinterpret_cast<const float*>(pInputVector));
                        pInputVector += sizeof(Float4) * 2;

                        __m256 vTempX = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(0, 0, 0, 0));
                        __m256 vTempY = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(1, 1, 1, 1));
                        __m256 vTempZ = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(2, 2, 2, 2));
                        __m256 vTempW = _mm256_shuffle_ps(VV, VV, _MM_SHUFFLE(3, 3, 3, 3));

                        vTempX = _mm256_mul_ps(vTempX, column0);
                        vTempX = _mm256_fmadd_ps(vTempY, column1, vTempX);
                        vTempX = _mm256_fmadd_ps(vTempZ, column2, vTempX);
                        vTempX = _mm256_fmadd_ps(vTempW, column3, vTempX);

                        _mm_storeu_ps(reinterpret_cast<float*>(pOutputVector), _mm256_castps256_ps128(vTempX));
                        pOutputVector += OutputStride;

                        _mm_storeu_ps(reinterpret_cast<float*>(pOutputVector), _mm256_extractf128_ps(vTempX, 1));
                        pOutputVector += OutputStride;
                        i += 2;
                    }
                }
            }
        }
    }

    if(i < VectorCount){
        const Vector column0 = M.r[0];
        const Vector column1 = M.r[1];
        const Vector column2 = M.r[2];
        const Vector column3 = M.r[3];
        const bool inputAligned = (InputStride == sizeof(Float4)) &&
            !(reinterpret_cast<uintptr_t>(pInputVector) & 0xF);

        if(inputAligned){
            for(; i < VectorCount; ++i){
                __m128 V = _mm_load_ps(reinterpret_cast<const float*>(pInputVector));
                pInputVector += InputStride;

                Vector vTempX = MATH_PERMUTE_PS(V, _MM_SHUFFLE(0, 0, 0, 0));
                Vector vTempY = MATH_PERMUTE_PS(V, _MM_SHUFFLE(1, 1, 1, 1));
                Vector vTempZ = MATH_PERMUTE_PS(V, _MM_SHUFFLE(2, 2, 2, 2));
                Vector vTempW = MATH_PERMUTE_PS(V, _MM_SHUFFLE(3, 3, 3, 3));

                vTempX = _mm_mul_ps(vTempX, column0);
                vTempX = MATH_FMADD_PS(vTempY, column1, vTempX);
                vTempX = MATH_FMADD_PS(vTempZ, column2, vTempX);
                vTempX = MATH_FMADD_PS(vTempW, column3, vTempX);

                _mm_storeu_ps(reinterpret_cast<float*>(pOutputVector), vTempX);
                pOutputVector += OutputStride;
            }
        }else{
            for(; i < VectorCount; ++i){
                __m128 V = _mm_loadu_ps(reinterpret_cast<const float*>(pInputVector));
                pInputVector += InputStride;

                Vector vTempX = MATH_PERMUTE_PS(V, _MM_SHUFFLE(0, 0, 0, 0));
                Vector vTempY = MATH_PERMUTE_PS(V, _MM_SHUFFLE(1, 1, 1, 1));
                Vector vTempZ = MATH_PERMUTE_PS(V, _MM_SHUFFLE(2, 2, 2, 2));
                Vector vTempW = MATH_PERMUTE_PS(V, _MM_SHUFFLE(3, 3, 3, 3));

                vTempX = _mm_mul_ps(vTempX, column0);
                vTempX = MATH_FMADD_PS(vTempY, column1, vTempX);
                vTempX = MATH_FMADD_PS(vTempZ, column2, vTempX);
                vTempX = MATH_FMADD_PS(vTempW, column3, vTempX);

                _mm_storeu_ps(reinterpret_cast<float*>(pOutputVector), vTempX);
                pOutputVector += OutputStride;
            }
        }
    }

    if(usedStreamingStores)
        MATH_SFENCE();

    return pOutputStream;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

