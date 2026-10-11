// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector4_double.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble4Transform(SIMDVectorDouble value, const SIMDMatrixDouble& matrix)noexcept{
    return VectorDoubleMergeX(
        VectorDouble4Dot(matrix.v[0], value),
        VectorDouble4Dot(matrix.v[1], value),
        VectorDouble4Dot(matrix.v[2], value),
        VectorDouble4Dot(matrix.v[3], value)
    );
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2Transform(SIMDVectorDouble value, const SIMDMatrixDouble& matrix)noexcept{
    return VectorDouble4Transform(VectorDoubleSetW(VectorDoubleSetZ(value, 0.0), 1.0), matrix);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2TransformCoord(SIMDVectorDouble value, const SIMDMatrixDouble& matrix)noexcept{
    SIMDVectorDouble result = VectorDouble2Transform(value, matrix);
    return VectorDoubleSetW(VectorDoubleDivide(result, VectorDoubleSplatW(result)), 1.0);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble2TransformNormal(SIMDVectorDouble value, const SIMDMatrixDouble& matrix)noexcept{
    return VectorDouble4Transform(VectorDoubleAndInt(value, s_DoubleSIMDMaskXY), matrix);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble3Transform(SIMDVectorDouble value, const SIMDMatrixDouble& matrix)noexcept{
    return VectorDouble4Transform(VectorDoubleSetW(value, 1.0), matrix);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble3TransformCoord(SIMDVectorDouble value, const SIMDMatrixDouble& matrix)noexcept{
    SIMDVectorDouble result = VectorDouble3Transform(value, matrix);
    return VectorDoubleSetW(VectorDoubleDivide(result, VectorDoubleSplatW(result)), 1.0);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDouble3TransformNormal(SIMDVectorDouble value, const SIMDMatrixDouble& matrix)noexcept{
    return VectorDouble4Transform(VectorDoubleAndInt(value, s_DoubleSIMDMask3), matrix);
}

NWB_INLINE Double4U* NWB_SIMD_CALL VectorDouble2TransformStream(Double4U* outputStream, usize outputStride, const Double2U* inputStream, usize inputStride, usize vectorCount, const SIMDMatrixDouble& matrix)noexcept{
    return SIMDVectorDoubleDetail::VectorDoubleTransformStreamImpl(outputStream, outputStride, inputStream, inputStride, vectorCount, matrix,
        [](SIMDVectorDouble value, const SIMDMatrixDouble& transposedMatrix)noexcept{
            return SIMDVectorDoubleDetail::VectorDouble4TransformTransposed(VectorDoubleSetW(VectorDoubleSetZ(value, 0.0), 1.0), transposedMatrix);
        }
    );
}

NWB_INLINE Double2U* NWB_SIMD_CALL VectorDouble2TransformCoordStream(Double2U* outputStream, usize outputStride, const Double2U* inputStream, usize inputStride, usize vectorCount, const SIMDMatrixDouble& matrix)noexcept{
    return SIMDVectorDoubleDetail::VectorDoubleTransformStreamImpl(outputStream, outputStride, inputStream, inputStride, vectorCount, matrix,
        [](SIMDVectorDouble value, const SIMDMatrixDouble& transposedMatrix)noexcept{
            SIMDVectorDouble result = SIMDVectorDoubleDetail::VectorDouble4TransformTransposed(
                VectorDoubleSetW(VectorDoubleSetZ(value, 0.0), 1.0),
                transposedMatrix
            );
            result = VectorDoubleSetW(VectorDoubleDivide(result, VectorDoubleSplatW(result)), 1.0);
            return result;
        }
    );
}

NWB_INLINE Double2U* NWB_SIMD_CALL VectorDouble2TransformNormalStream(Double2U* outputStream, usize outputStride, const Double2U* inputStream, usize inputStride, usize vectorCount, const SIMDMatrixDouble& matrix)noexcept{
    return SIMDVectorDoubleDetail::VectorDoubleTransformStreamImpl(outputStream, outputStride, inputStream, inputStride, vectorCount, matrix,
        [](SIMDVectorDouble value, const SIMDMatrixDouble& transposedMatrix)noexcept{
            return SIMDVectorDoubleDetail::VectorDouble4TransformTransposed(VectorDoubleAndInt(value, s_DoubleSIMDMaskXY), transposedMatrix);
        }
    );
}

NWB_INLINE Double4U* NWB_SIMD_CALL VectorDouble3TransformStream(Double4U* outputStream, usize outputStride, const Double3U* inputStream, usize inputStride, usize vectorCount, const SIMDMatrixDouble& matrix)noexcept{
    return SIMDVectorDoubleDetail::VectorDoubleTransformStreamImpl(outputStream, outputStride, inputStream, inputStride, vectorCount, matrix,
        [](SIMDVectorDouble value, const SIMDMatrixDouble& transposedMatrix)noexcept{
            return SIMDVectorDoubleDetail::VectorDouble4TransformTransposed(VectorDoubleSetW(value, 1.0), transposedMatrix);
        }
    );
}

NWB_INLINE Double3U* NWB_SIMD_CALL VectorDouble3TransformCoordStream(Double3U* outputStream, usize outputStride, const Double3U* inputStream, usize inputStride, usize vectorCount, const SIMDMatrixDouble& matrix)noexcept{
    return SIMDVectorDoubleDetail::VectorDoubleTransformStreamImpl(outputStream, outputStride, inputStream, inputStride, vectorCount, matrix,
        [](SIMDVectorDouble value, const SIMDMatrixDouble& transposedMatrix)noexcept{
            SIMDVectorDouble result = SIMDVectorDoubleDetail::VectorDouble4TransformTransposed(
                VectorDoubleSetW(value, 1.0),
                transposedMatrix
            );
            result = VectorDoubleSetW(VectorDoubleDivide(result, VectorDoubleSplatW(result)), 1.0);
            return result;
        }
    );
}

NWB_INLINE Double3U* NWB_SIMD_CALL VectorDouble3TransformNormalStream(Double3U* outputStream, usize outputStride, const Double3U* inputStream, usize inputStride, usize vectorCount, const SIMDMatrixDouble& matrix)noexcept{
    return SIMDVectorDoubleDetail::VectorDoubleTransformStreamImpl(outputStream, outputStride, inputStream, inputStride, vectorCount, matrix,
        [](SIMDVectorDouble value, const SIMDMatrixDouble& transposedMatrix)noexcept{
            return SIMDVectorDoubleDetail::VectorDouble4TransformTransposed(VectorDoubleAndInt(value, s_DoubleSIMDMask3), transposedMatrix);
        }
    );
}

NWB_INLINE Double4U* NWB_SIMD_CALL VectorDouble4TransformStream(Double4U* outputStream, usize outputStride, const Double4U* inputStream, usize inputStride, usize vectorCount, const SIMDMatrixDouble& matrix)noexcept{
    return SIMDVectorDoubleDetail::VectorDoubleTransformStreamImpl(outputStream, outputStride, inputStream, inputStride, vectorCount, matrix,
        [](SIMDVectorDouble value, const SIMDMatrixDouble& transposedMatrix)noexcept{
            return SIMDVectorDoubleDetail::VectorDouble4TransformTransposed(value, transposedMatrix);
        }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

