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

template<typename OutputT, typename InputT>
    requires ((IsSame_V<OutputT, Double4> || IsSame_V<OutputT, Double4U>)
        && (IsSame_V<InputT, Double2> || IsSame_V<InputT, Double2U>))
NWB_INLINE OutputT* NWB_SIMD_CALL VectorDouble2TransformStream(
    OutputT* outputStream,
    usize outputStride,
    const InputT* inputStream,
    usize inputStride,
    usize vectorCount,
    const SIMDMatrixDouble& matrix
)noexcept{
    return SIMDVectorDoubleDetail::VectorDoubleTransformStreamImpl(outputStream, outputStride, inputStream, inputStride, vectorCount, matrix,
        [](SIMDVectorDouble value, const SIMDMatrixDouble& transposedMatrix)noexcept{
            return SIMDVectorDoubleDetail::VectorDouble4TransformTransposed(VectorDoubleSetW(VectorDoubleSetZ(value, 0.0), 1.0), transposedMatrix);
        }
    );
}

template<typename OutputT, typename InputT>
    requires ((IsSame_V<OutputT, Double2> || IsSame_V<OutputT, Double2U>)
        && (IsSame_V<InputT, Double2> || IsSame_V<InputT, Double2U>))
NWB_INLINE OutputT* NWB_SIMD_CALL VectorDouble2TransformCoordStream(
    OutputT* outputStream,
    usize outputStride,
    const InputT* inputStream,
    usize inputStride,
    usize vectorCount,
    const SIMDMatrixDouble& matrix
)noexcept{
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

template<typename OutputT, typename InputT>
    requires ((IsSame_V<OutputT, Double2> || IsSame_V<OutputT, Double2U>)
        && (IsSame_V<InputT, Double2> || IsSame_V<InputT, Double2U>))
NWB_INLINE OutputT* NWB_SIMD_CALL VectorDouble2TransformNormalStream(
    OutputT* outputStream,
    usize outputStride,
    const InputT* inputStream,
    usize inputStride,
    usize vectorCount,
    const SIMDMatrixDouble& matrix
)noexcept{
    return SIMDVectorDoubleDetail::VectorDoubleTransformStreamImpl(outputStream, outputStride, inputStream, inputStride, vectorCount, matrix,
        [](SIMDVectorDouble value, const SIMDMatrixDouble& transposedMatrix)noexcept{
            return SIMDVectorDoubleDetail::VectorDouble4TransformTransposed(VectorDoubleAndInt(value, s_DoubleSIMDMaskXY), transposedMatrix);
        }
    );
}

template<typename OutputT, typename InputT>
    requires ((IsSame_V<OutputT, Double4> || IsSame_V<OutputT, Double4U>)
        && (IsSame_V<InputT, Double3> || IsSame_V<InputT, Double3U>))
NWB_INLINE OutputT* NWB_SIMD_CALL VectorDouble3TransformStream(
    OutputT* outputStream,
    usize outputStride,
    const InputT* inputStream,
    usize inputStride,
    usize vectorCount,
    const SIMDMatrixDouble& matrix
)noexcept{
    return SIMDVectorDoubleDetail::VectorDoubleTransformStreamImpl(outputStream, outputStride, inputStream, inputStride, vectorCount, matrix,
        [](SIMDVectorDouble value, const SIMDMatrixDouble& transposedMatrix)noexcept{
            return SIMDVectorDoubleDetail::VectorDouble4TransformTransposed(VectorDoubleSetW(value, 1.0), transposedMatrix);
        }
    );
}

template<typename OutputT, typename InputT>
    requires ((IsSame_V<OutputT, Double3> || IsSame_V<OutputT, Double3U>)
        && (IsSame_V<InputT, Double3> || IsSame_V<InputT, Double3U>))
NWB_INLINE OutputT* NWB_SIMD_CALL VectorDouble3TransformCoordStream(
    OutputT* outputStream,
    usize outputStride,
    const InputT* inputStream,
    usize inputStride,
    usize vectorCount,
    const SIMDMatrixDouble& matrix
)noexcept{
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

template<typename OutputT, typename InputT>
    requires ((IsSame_V<OutputT, Double3> || IsSame_V<OutputT, Double3U>)
        && (IsSame_V<InputT, Double3> || IsSame_V<InputT, Double3U>))
NWB_INLINE OutputT* NWB_SIMD_CALL VectorDouble3TransformNormalStream(
    OutputT* outputStream,
    usize outputStride,
    const InputT* inputStream,
    usize inputStride,
    usize vectorCount,
    const SIMDMatrixDouble& matrix
)noexcept{
    return SIMDVectorDoubleDetail::VectorDoubleTransformStreamImpl(outputStream, outputStride, inputStream, inputStride, vectorCount, matrix,
        [](SIMDVectorDouble value, const SIMDMatrixDouble& transposedMatrix)noexcept{
            return SIMDVectorDoubleDetail::VectorDouble4TransformTransposed(VectorDoubleAndInt(value, s_DoubleSIMDMask3), transposedMatrix);
        }
    );
}

template<typename OutputT, typename InputT>
    requires ((IsSame_V<OutputT, Double4> || IsSame_V<OutputT, Double4U>)
        && (IsSame_V<InputT, Double4> || IsSame_V<InputT, Double4U>))
NWB_INLINE OutputT* NWB_SIMD_CALL VectorDouble4TransformStream(
    OutputT* outputStream,
    usize outputStride,
    const InputT* inputStream,
    usize inputStride,
    usize vectorCount,
    const SIMDMatrixDouble& matrix
)noexcept{
    return SIMDVectorDoubleDetail::VectorDoubleTransformStreamImpl(outputStream, outputStride, inputStream, inputStride, vectorCount, matrix,
        [](SIMDVectorDouble value, const SIMDMatrixDouble& transposedMatrix)noexcept{
            return SIMDVectorDoubleDetail::VectorDouble4TransformTransposed(value, transposedMatrix);
        }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

