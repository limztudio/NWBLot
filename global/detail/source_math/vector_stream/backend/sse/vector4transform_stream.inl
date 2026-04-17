// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


_Use_decl_annotations_
inline Float4* MathCallConv Vector4TransformStreamSse
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

    const Vector column0 = M.r[0];
    const Vector column1 = M.r[1];
    const Vector column2 = M.r[2];
    const Vector column3 = M.r[3];

    if(!(reinterpret_cast<uintptr_t>(pOutputStream) & 0xF) &&
        !(OutputStride & 0xF) &&
        ((VectorCount * sizeof(Float4)) >= SourceMathInternal::MATH_CACHE_LINE_SIZE)){
        usedStreamingStores = true;
        if(!(reinterpret_cast<uintptr_t>(pInputStream) & 0xF) && !(InputStride & 0xF)){
            // Aligned input, aligned output
            for(size_t i = 0; i < VectorCount; ++i){
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

                MATH_STREAM_PS(reinterpret_cast<float*>(pOutputVector), vTempX);
                pOutputVector += OutputStride;
            }
        }else{
            // Unaligned input, aligned output
            for(size_t i = 0; i < VectorCount; ++i){
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

                MATH_STREAM_PS(reinterpret_cast<float*>(pOutputVector), vTempX);
                pOutputVector += OutputStride;
            }
        }
    }else{
        if(!(reinterpret_cast<uintptr_t>(pInputStream) & 0xF) && !(InputStride & 0xF)){
            // Aligned input, unaligned output
            for(size_t i = 0; i < VectorCount; ++i){
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
            // Unaligned input, unaligned output
            for(size_t i = 0; i < VectorCount; ++i){
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

