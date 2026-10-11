// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector_lane_double.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleTranspose4(SIMDVectorDouble r0, SIMDVectorDouble r1, SIMDVectorDouble r2, SIMDVectorDouble r3)noexcept{
    const SIMDVectorDouble xy01 = __builtin_shufflevector(r0, r1, 0, 1, 4, 5);
    const SIMDVectorDouble zw01 = __builtin_shufflevector(r0, r1, 2, 3, 6, 7);
    const SIMDVectorDouble xy23 = __builtin_shufflevector(r2, r3, 0, 1, 4, 5);
    const SIMDVectorDouble zw23 = __builtin_shufflevector(r2, r3, 2, 3, 6, 7);

    SIMDMatrixDouble result{};
    result.v[0] = __builtin_shufflevector(xy01, xy23, 0, 2, 4, 6);
    result.v[1] = __builtin_shufflevector(xy01, xy23, 1, 3, 5, 7);
    result.v[2] = __builtin_shufflevector(zw01, zw23, 0, 2, 4, 6);
    result.v[3] = __builtin_shufflevector(zw01, zw23, 1, 3, 5, 7);
    return result;
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL MatrixDoubleTransposeForTransform(const SIMDMatrixDouble& matrix)noexcept{
    return MatrixDoubleTranspose4(matrix.v[0], matrix.v[1], matrix.v[2], matrix.v[3]);
}


NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4TransformTransposed(SIMDVectorDouble value, const SIMDMatrixDouble& transposedMatrix)noexcept{
    const SIMDVectorDouble x = __builtin_shufflevector(value, value, 0, 0, 0, 0);
    const SIMDVectorDouble y = __builtin_shufflevector(value, value, 1, 1, 1, 1);
    const SIMDVectorDouble z = __builtin_shufflevector(value, value, 2, 2, 2, 2);
    const SIMDVectorDouble w = __builtin_shufflevector(value, value, 3, 3, 3, 3);
    return ((transposedMatrix.v[0] * x + transposedMatrix.v[1] * y) + transposedMatrix.v[2] * z) + transposedMatrix.v[3] * w;
}

template<typename OutputT, typename InputT, typename TransformT>
NWB_INLINE OutputT* NWB_SIMD_CALL VectorDoubleTransformStreamImpl(
    OutputT* outputStream,
    usize outputStride,
    const InputT* inputStream,
    usize inputStride,
    usize vectorCount,
    const SIMDMatrixDouble& matrix,
    TransformT transform
)noexcept{
    NWB_ASSERT(outputStream != nullptr);
    NWB_ASSERT(inputStream != nullptr);
    NWB_ASSERT(inputStride >= sizeof(InputT));
    NWB_ASSERT(outputStride >= sizeof(OutputT));
    NWB_ASSERT(inputStride % alignof(InputT) == 0u);
    NWB_ASSERT(outputStride % alignof(OutputT) == 0u);

    const SIMDMatrixDouble transposedMatrix = MatrixDoubleTransposeForTransform(matrix);
    for(usize i = 0u; i < vectorCount; ++i){
        const SIMDVectorDouble value = LoadDouble(*StridePointer(inputStream, inputStride, i));
        StoreDouble(transform(value, transposedMatrix), *StridePointer(outputStream, outputStride, i));
    }

    return outputStream;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

