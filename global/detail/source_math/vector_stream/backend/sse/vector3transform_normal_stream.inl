// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


_Use_decl_annotations_
inline Float3* MathCallConv Vector3TransformNormalStreamSse
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

    const Vector column0 = M.r[0];
    const Vector column1 = M.r[1];
    const Vector column2 = M.r[2];

    size_t i = 0;
    const size_t four = VectorCount >> 2;
    if(four > 0){
        if(InputStride == sizeof(Float3)){
            const bool inputAligned = !(reinterpret_cast<uintptr_t>(pInputVector) & 0xF);
            if(OutputStride == sizeof(Float3)){
                if(!(reinterpret_cast<uintptr_t>(pOutputStream) & 0xF) &&
                    ((VectorCount * sizeof(Float3)) >= SourceMathInternal::MATH_CACHE_LINE_SIZE)){
                    // Packed input, aligned & packed output
                    usedStreamingStores = true;
                    if(inputAligned){
                        for(size_t j = 0; j < four; ++j){
                            __m128 V1 = _mm_load_ps(reinterpret_cast<const float*>(pInputVector));
                            __m128 L2 = _mm_load_ps(reinterpret_cast<const float*>(pInputVector + 16));
                            __m128 L3 = _mm_load_ps(reinterpret_cast<const float*>(pInputVector + 32));
                            pInputVector += sizeof(Float3) * 4;

                            // Unpack the 4 vectors (.w components are junk)
                            MATH3UNPACK3INTO4(V1, L2, L3);

                            // Result 1
                            Vector Z = MATH_PERMUTE_PS(V1, _MM_SHUFFLE(2, 2, 2, 2));
                            Vector Y = MATH_PERMUTE_PS(V1, _MM_SHUFFLE(1, 1, 1, 1));
                            Vector X = MATH_PERMUTE_PS(V1, _MM_SHUFFLE(0, 0, 0, 0));

                            Vector vTemp = _mm_mul_ps(Z, column2);
                            vTemp = MATH_FMADD_PS(Y, column1, vTemp);
                            V1 = MATH_FMADD_PS(X, column0, vTemp);

                            // Result 2
                            Z = MATH_PERMUTE_PS(V2, _MM_SHUFFLE(2, 2, 2, 2));
                            Y = MATH_PERMUTE_PS(V2, _MM_SHUFFLE(1, 1, 1, 1));
                            X = MATH_PERMUTE_PS(V2, _MM_SHUFFLE(0, 0, 0, 0));

                            vTemp = _mm_mul_ps(Z, column2);
                            vTemp = MATH_FMADD_PS(Y, column1, vTemp);
                            V2 = MATH_FMADD_PS(X, column0, vTemp);

                            // Result 3
                            Z = MATH_PERMUTE_PS(V3, _MM_SHUFFLE(2, 2, 2, 2));
                            Y = MATH_PERMUTE_PS(V3, _MM_SHUFFLE(1, 1, 1, 1));
                            X = MATH_PERMUTE_PS(V3, _MM_SHUFFLE(0, 0, 0, 0));

                            vTemp = _mm_mul_ps(Z, column2);
                            vTemp = MATH_FMADD_PS(Y, column1, vTemp);
                            V3 = MATH_FMADD_PS(X, column0, vTemp);

                            // Result 4
                            Z = MATH_PERMUTE_PS(V4, _MM_SHUFFLE(2, 2, 2, 2));
                            Y = MATH_PERMUTE_PS(V4, _MM_SHUFFLE(1, 1, 1, 1));
                            X = MATH_PERMUTE_PS(V4, _MM_SHUFFLE(0, 0, 0, 0));

                            vTemp = _mm_mul_ps(Z, column2);
                            vTemp = MATH_FMADD_PS(Y, column1, vTemp);
                            V4 = MATH_FMADD_PS(X, column0, vTemp);

                            // Pack and store the vectors
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

                            // Unpack the 4 vectors (.w components are junk)
                            MATH3UNPACK3INTO4(V1, L2, L3);

                            // Result 1
                            Vector Z = MATH_PERMUTE_PS(V1, _MM_SHUFFLE(2, 2, 2, 2));
                            Vector Y = MATH_PERMUTE_PS(V1, _MM_SHUFFLE(1, 1, 1, 1));
                            Vector X = MATH_PERMUTE_PS(V1, _MM_SHUFFLE(0, 0, 0, 0));

                            Vector vTemp = _mm_mul_ps(Z, column2);
                            vTemp = MATH_FMADD_PS(Y, column1, vTemp);
                            V1 = MATH_FMADD_PS(X, column0, vTemp);

                            // Result 2
                            Z = MATH_PERMUTE_PS(V2, _MM_SHUFFLE(2, 2, 2, 2));
                            Y = MATH_PERMUTE_PS(V2, _MM_SHUFFLE(1, 1, 1, 1));
                            X = MATH_PERMUTE_PS(V2, _MM_SHUFFLE(0, 0, 0, 0));

                            vTemp = _mm_mul_ps(Z, column2);
                            vTemp = MATH_FMADD_PS(Y, column1, vTemp);
                            V2 = MATH_FMADD_PS(X, column0, vTemp);

                            // Result 3
                            Z = MATH_PERMUTE_PS(V3, _MM_SHUFFLE(2, 2, 2, 2));
                            Y = MATH_PERMUTE_PS(V3, _MM_SHUFFLE(1, 1, 1, 1));
                            X = MATH_PERMUTE_PS(V3, _MM_SHUFFLE(0, 0, 0, 0));

                            vTemp = _mm_mul_ps(Z, column2);
                            vTemp = MATH_FMADD_PS(Y, column1, vTemp);
                            V3 = MATH_FMADD_PS(X, column0, vTemp);

                            // Result 4
                            Z = MATH_PERMUTE_PS(V4, _MM_SHUFFLE(2, 2, 2, 2));
                            Y = MATH_PERMUTE_PS(V4, _MM_SHUFFLE(1, 1, 1, 1));
                            X = MATH_PERMUTE_PS(V4, _MM_SHUFFLE(0, 0, 0, 0));

                            vTemp = _mm_mul_ps(Z, column2);
                            vTemp = MATH_FMADD_PS(Y, column1, vTemp);
                            V4 = MATH_FMADD_PS(X, column0, vTemp);

                            // Pack and store the vectors
                            MATH3PACK4INTO3(vTemp);
                            MATH_STREAM_PS(reinterpret_cast<float*>(pOutputVector), V1);
                            MATH_STREAM_PS(reinterpret_cast<float*>(pOutputVector + 16), vTemp);
                            MATH_STREAM_PS(reinterpret_cast<float*>(pOutputVector + 32), V3);
                            pOutputVector += sizeof(Float3) * 4;
                            i += 4;
                        }
                    }
                }else{
                    // Packed input, unaligned & packed output
                    if(inputAligned){
                        for(size_t j = 0; j < four; ++j){
                            __m128 V1 = _mm_load_ps(reinterpret_cast<const float*>(pInputVector));
                            __m128 L2 = _mm_load_ps(reinterpret_cast<const float*>(pInputVector + 16));
                            __m128 L3 = _mm_load_ps(reinterpret_cast<const float*>(pInputVector + 32));
                            pInputVector += sizeof(Float3) * 4;

                            // Unpack the 4 vectors (.w components are junk)
                            MATH3UNPACK3INTO4(V1, L2, L3);

                            // Result 1
                            Vector Z = MATH_PERMUTE_PS(V1, _MM_SHUFFLE(2, 2, 2, 2));
                            Vector Y = MATH_PERMUTE_PS(V1, _MM_SHUFFLE(1, 1, 1, 1));
                            Vector X = MATH_PERMUTE_PS(V1, _MM_SHUFFLE(0, 0, 0, 0));

                            Vector vTemp = _mm_mul_ps(Z, column2);
                            vTemp = MATH_FMADD_PS(Y, column1, vTemp);
                            V1 = MATH_FMADD_PS(X, column0, vTemp);

                            // Result 2
                            Z = MATH_PERMUTE_PS(V2, _MM_SHUFFLE(2, 2, 2, 2));
                            Y = MATH_PERMUTE_PS(V2, _MM_SHUFFLE(1, 1, 1, 1));
                            X = MATH_PERMUTE_PS(V2, _MM_SHUFFLE(0, 0, 0, 0));

                            vTemp = _mm_mul_ps(Z, column2);
                            vTemp = MATH_FMADD_PS(Y, column1, vTemp);
                            V2 = MATH_FMADD_PS(X, column0, vTemp);

                            // Result 3
                            Z = MATH_PERMUTE_PS(V3, _MM_SHUFFLE(2, 2, 2, 2));
                            Y = MATH_PERMUTE_PS(V3, _MM_SHUFFLE(1, 1, 1, 1));
                            X = MATH_PERMUTE_PS(V3, _MM_SHUFFLE(0, 0, 0, 0));

                            vTemp = _mm_mul_ps(Z, column2);
                            vTemp = MATH_FMADD_PS(Y, column1, vTemp);
                            V3 = MATH_FMADD_PS(X, column0, vTemp);

                            // Result 4
                            Z = MATH_PERMUTE_PS(V4, _MM_SHUFFLE(2, 2, 2, 2));
                            Y = MATH_PERMUTE_PS(V4, _MM_SHUFFLE(1, 1, 1, 1));
                            X = MATH_PERMUTE_PS(V4, _MM_SHUFFLE(0, 0, 0, 0));

                            vTemp = _mm_mul_ps(Z, column2);
                            vTemp = MATH_FMADD_PS(Y, column1, vTemp);
                            V4 = MATH_FMADD_PS(X, column0, vTemp);

                            // Pack and store the vectors
                            MATH3PACK4INTO3(vTemp);
                            _mm_storeu_ps(reinterpret_cast<float*>(pOutputVector), V1);
                            _mm_storeu_ps(reinterpret_cast<float*>(pOutputVector + 16), vTemp);
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

                            // Unpack the 4 vectors (.w components are junk)
                            MATH3UNPACK3INTO4(V1, L2, L3);

                            // Result 1
                            Vector Z = MATH_PERMUTE_PS(V1, _MM_SHUFFLE(2, 2, 2, 2));
                            Vector Y = MATH_PERMUTE_PS(V1, _MM_SHUFFLE(1, 1, 1, 1));
                            Vector X = MATH_PERMUTE_PS(V1, _MM_SHUFFLE(0, 0, 0, 0));

                            Vector vTemp = _mm_mul_ps(Z, column2);
                            vTemp = MATH_FMADD_PS(Y, column1, vTemp);
                            V1 = MATH_FMADD_PS(X, column0, vTemp);

                            // Result 2
                            Z = MATH_PERMUTE_PS(V2, _MM_SHUFFLE(2, 2, 2, 2));
                            Y = MATH_PERMUTE_PS(V2, _MM_SHUFFLE(1, 1, 1, 1));
                            X = MATH_PERMUTE_PS(V2, _MM_SHUFFLE(0, 0, 0, 0));

                            vTemp = _mm_mul_ps(Z, column2);
                            vTemp = MATH_FMADD_PS(Y, column1, vTemp);
                            V2 = MATH_FMADD_PS(X, column0, vTemp);

                            // Result 3
                            Z = MATH_PERMUTE_PS(V3, _MM_SHUFFLE(2, 2, 2, 2));
                            Y = MATH_PERMUTE_PS(V3, _MM_SHUFFLE(1, 1, 1, 1));
                            X = MATH_PERMUTE_PS(V3, _MM_SHUFFLE(0, 0, 0, 0));

                            vTemp = _mm_mul_ps(Z, column2);
                            vTemp = MATH_FMADD_PS(Y, column1, vTemp);
                            V3 = MATH_FMADD_PS(X, column0, vTemp);

                            // Result 4
                            Z = MATH_PERMUTE_PS(V4, _MM_SHUFFLE(2, 2, 2, 2));
                            Y = MATH_PERMUTE_PS(V4, _MM_SHUFFLE(1, 1, 1, 1));
                            X = MATH_PERMUTE_PS(V4, _MM_SHUFFLE(0, 0, 0, 0));

                            vTemp = _mm_mul_ps(Z, column2);
                            vTemp = MATH_FMADD_PS(Y, column1, vTemp);
                            V4 = MATH_FMADD_PS(X, column0, vTemp);

                            // Pack and store the vectors
                            MATH3PACK4INTO3(vTemp);
                            _mm_storeu_ps(reinterpret_cast<float*>(pOutputVector), V1);
                            _mm_storeu_ps(reinterpret_cast<float*>(pOutputVector + 16), vTemp);
                            _mm_storeu_ps(reinterpret_cast<float*>(pOutputVector + 32), V3);
                            pOutputVector += sizeof(Float3) * 4;
                            i += 4;
                        }
                    }
                }
            }else{
                // Packed input, unpacked output
                if(inputAligned){
                    for(size_t j = 0; j < four; ++j){
                        __m128 V1 = _mm_load_ps(reinterpret_cast<const float*>(pInputVector));
                        __m128 L2 = _mm_load_ps(reinterpret_cast<const float*>(pInputVector + 16));
                        __m128 L3 = _mm_load_ps(reinterpret_cast<const float*>(pInputVector + 32));
                        pInputVector += sizeof(Float3) * 4;

                        // Unpack the 4 vectors (.w components are junk)
                        MATH3UNPACK3INTO4(V1, L2, L3);

                        // Result 1
                        Vector Z = MATH_PERMUTE_PS(V1, _MM_SHUFFLE(2, 2, 2, 2));
                        Vector Y = MATH_PERMUTE_PS(V1, _MM_SHUFFLE(1, 1, 1, 1));
                        Vector X = MATH_PERMUTE_PS(V1, _MM_SHUFFLE(0, 0, 0, 0));

                        Vector vTemp = _mm_mul_ps(Z, column2);
                        vTemp = MATH_FMADD_PS(Y, column1, vTemp);
                        vTemp = MATH_FMADD_PS(X, column0, vTemp);

                        StoreFloat3(reinterpret_cast<Float3*>(pOutputVector), vTemp);
                        pOutputVector += OutputStride;

                        // Result 2
                        Z = MATH_PERMUTE_PS(V2, _MM_SHUFFLE(2, 2, 2, 2));
                        Y = MATH_PERMUTE_PS(V2, _MM_SHUFFLE(1, 1, 1, 1));
                        X = MATH_PERMUTE_PS(V2, _MM_SHUFFLE(0, 0, 0, 0));

                        vTemp = _mm_mul_ps(Z, column2);
                        vTemp = MATH_FMADD_PS(Y, column1, vTemp);
                        vTemp = MATH_FMADD_PS(X, column0, vTemp);

                        StoreFloat3(reinterpret_cast<Float3*>(pOutputVector), vTemp);
                        pOutputVector += OutputStride;

                        // Result 3
                        Z = MATH_PERMUTE_PS(V3, _MM_SHUFFLE(2, 2, 2, 2));
                        Y = MATH_PERMUTE_PS(V3, _MM_SHUFFLE(1, 1, 1, 1));
                        X = MATH_PERMUTE_PS(V3, _MM_SHUFFLE(0, 0, 0, 0));

                        vTemp = _mm_mul_ps(Z, column2);
                        vTemp = MATH_FMADD_PS(Y, column1, vTemp);
                        vTemp = MATH_FMADD_PS(X, column0, vTemp);

                        StoreFloat3(reinterpret_cast<Float3*>(pOutputVector), vTemp);
                        pOutputVector += OutputStride;

                        // Result 4
                        Z = MATH_PERMUTE_PS(V4, _MM_SHUFFLE(2, 2, 2, 2));
                        Y = MATH_PERMUTE_PS(V4, _MM_SHUFFLE(1, 1, 1, 1));
                        X = MATH_PERMUTE_PS(V4, _MM_SHUFFLE(0, 0, 0, 0));

                        vTemp = _mm_mul_ps(Z, column2);
                        vTemp = MATH_FMADD_PS(Y, column1, vTemp);
                        vTemp = MATH_FMADD_PS(X, column0, vTemp);

                        StoreFloat3(reinterpret_cast<Float3*>(pOutputVector), vTemp);
                        pOutputVector += OutputStride;

                        i += 4;
                    }
                }else{
                    for(size_t j = 0; j < four; ++j){
                        __m128 V1 = _mm_loadu_ps(reinterpret_cast<const float*>(pInputVector));
                        __m128 L2 = _mm_loadu_ps(reinterpret_cast<const float*>(pInputVector + 16));
                        __m128 L3 = _mm_loadu_ps(reinterpret_cast<const float*>(pInputVector + 32));
                        pInputVector += sizeof(Float3) * 4;

                        // Unpack the 4 vectors (.w components are junk)
                        MATH3UNPACK3INTO4(V1, L2, L3);

                        // Result 1
                        Vector Z = MATH_PERMUTE_PS(V1, _MM_SHUFFLE(2, 2, 2, 2));
                        Vector Y = MATH_PERMUTE_PS(V1, _MM_SHUFFLE(1, 1, 1, 1));
                        Vector X = MATH_PERMUTE_PS(V1, _MM_SHUFFLE(0, 0, 0, 0));

                        Vector vTemp = _mm_mul_ps(Z, column2);
                        vTemp = MATH_FMADD_PS(Y, column1, vTemp);
                        vTemp = MATH_FMADD_PS(X, column0, vTemp);

                        StoreFloat3(reinterpret_cast<Float3*>(pOutputVector), vTemp);
                        pOutputVector += OutputStride;

                        // Result 2
                        Z = MATH_PERMUTE_PS(V2, _MM_SHUFFLE(2, 2, 2, 2));
                        Y = MATH_PERMUTE_PS(V2, _MM_SHUFFLE(1, 1, 1, 1));
                        X = MATH_PERMUTE_PS(V2, _MM_SHUFFLE(0, 0, 0, 0));

                        vTemp = _mm_mul_ps(Z, column2);
                        vTemp = MATH_FMADD_PS(Y, column1, vTemp);
                        vTemp = MATH_FMADD_PS(X, column0, vTemp);

                        StoreFloat3(reinterpret_cast<Float3*>(pOutputVector), vTemp);
                        pOutputVector += OutputStride;

                        // Result 3
                        Z = MATH_PERMUTE_PS(V3, _MM_SHUFFLE(2, 2, 2, 2));
                        Y = MATH_PERMUTE_PS(V3, _MM_SHUFFLE(1, 1, 1, 1));
                        X = MATH_PERMUTE_PS(V3, _MM_SHUFFLE(0, 0, 0, 0));

                        vTemp = _mm_mul_ps(Z, column2);
                        vTemp = MATH_FMADD_PS(Y, column1, vTemp);
                        vTemp = MATH_FMADD_PS(X, column0, vTemp);

                        StoreFloat3(reinterpret_cast<Float3*>(pOutputVector), vTemp);
                        pOutputVector += OutputStride;

                        // Result 4
                        Z = MATH_PERMUTE_PS(V4, _MM_SHUFFLE(2, 2, 2, 2));
                        Y = MATH_PERMUTE_PS(V4, _MM_SHUFFLE(1, 1, 1, 1));
                        X = MATH_PERMUTE_PS(V4, _MM_SHUFFLE(0, 0, 0, 0));

                        vTemp = _mm_mul_ps(Z, column2);
                        vTemp = MATH_FMADD_PS(Y, column1, vTemp);
                        vTemp = MATH_FMADD_PS(X, column0, vTemp);

                        StoreFloat3(reinterpret_cast<Float3*>(pOutputVector), vTemp);
                        pOutputVector += OutputStride;

                        i += 4;
                    }
                }
            }
        }
    }

    for(; i < VectorCount; ++i){
        Vector V = LoadFloat3(reinterpret_cast<const Float3*>(pInputVector));
        pInputVector += InputStride;

        Vector Z = MATH_PERMUTE_PS(V, _MM_SHUFFLE(2, 2, 2, 2));
        Vector Y = MATH_PERMUTE_PS(V, _MM_SHUFFLE(1, 1, 1, 1));
        Vector X = MATH_PERMUTE_PS(V, _MM_SHUFFLE(0, 0, 0, 0));

        Vector vTemp = _mm_mul_ps(Z, column2);
        vTemp = MATH_FMADD_PS(Y, column1, vTemp);
        vTemp = MATH_FMADD_PS(X, column0, vTemp);

        StoreFloat3(reinterpret_cast<Float3*>(pOutputVector), vTemp);
        pOutputVector += OutputStride;
    }

    if(usedStreamingStores)
        MATH_SFENCE();

    return pOutputStream;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

