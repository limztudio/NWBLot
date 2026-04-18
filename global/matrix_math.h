// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "compile.h"
#include "not_null.h"
#include "type.h"
#include "detail/math_source.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SourceMath = ::SourceMathInternal;


using SimdVector = SourceMath::Vector;
using VectorArg = SourceMath::VectorArg;
using VectorArg2 = SourceMath::VectorArg2;
using VectorArg3 = SourceMath::VectorArg3;
using VectorConstArg = SourceMath::VectorConstArg;
using DoubleSimdVector = SourceMath::DoubleVector;
using DoubleVectorArg = SourceMath::DoubleVectorArg;

using Float2Data = SourceMath::Float2;
using Float3Data = SourceMath::Float3;
using Float4Data = SourceMath::Float4;
using AlignedFloat2Data = SourceMath::AlignedFloat2;
using AlignedFloat3Data = SourceMath::AlignedFloat3;
using AlignedFloat4Data = SourceMath::AlignedFloat4;
using Double2Data = SourceMath::Double2;
using Double3Data = SourceMath::Double3;
using Double4Data = SourceMath::Double4;
using AlignedDouble2Data = SourceMath::AlignedDouble2;
using AlignedDouble3Data = SourceMath::AlignedDouble3;
using AlignedDouble4Data = SourceMath::AlignedDouble4;

using Float3x4Data = SourceMath::Float3x4;
using AlignedFloat3x4Data = SourceMath::AlignedFloat3x4;
using Double3x4Data = SourceMath::Double3x4;
using AlignedDouble3x4Data = SourceMath::AlignedDouble3x4;


struct SimdMatrix{
    SourceMath::Matrix m_value;

    SimdMatrix()noexcept = default;
    explicit SimdMatrix(const SourceMath::Matrix& value)noexcept
        : m_value(value)
    {
    }
    SimdMatrix(VectorArg column0, VectorArg column1, VectorArg column2, VectorConstArg column3)noexcept{
        m_value.r[0] = column0;
        m_value.r[1] = column1;
        m_value.r[2] = column2;
        m_value.r[3] = column3;
    }

    // Columns under NWB's column-vector convention. `matrix[3]` is translation.
    [[nodiscard]] SimdVector& operator[](const usize index)noexcept{
        return m_value.r[index];
    }

    [[nodiscard]] const SimdVector& operator[](const usize index)const noexcept{
        return m_value.r[index];
    }

    [[nodiscard]] const SourceMath::Matrix& nativeMatrix()const noexcept{
        return m_value;
    }
};

using MatrixArg = const SimdMatrix&;


struct DoubleSimdMatrix{
    SourceMath::DoubleMatrix m_value;

    DoubleSimdMatrix()noexcept = default;
    explicit DoubleSimdMatrix(const SourceMath::DoubleMatrix& value)noexcept
        : m_value(value)
    {
    }
    DoubleSimdMatrix(
        DoubleVectorArg column0,
        DoubleVectorArg column1,
        DoubleVectorArg column2,
        DoubleVectorArg column3
    )noexcept{
        m_value.r[0] = column0;
        m_value.r[1] = column1;
        m_value.r[2] = column2;
        m_value.r[3] = column3;
    }

    // Columns under NWB's column-vector convention. `matrix[3]` is translation.
    [[nodiscard]] DoubleSimdVector& operator[](const usize index)noexcept{
        return m_value.r[index];
    }

    [[nodiscard]] const DoubleSimdVector& operator[](const usize index)const noexcept{
        return m_value.r[index];
    }

    [[nodiscard]] const SourceMath::DoubleMatrix& nativeMatrix()const noexcept{
        return m_value;
    }
};

using DoubleMatrixArg = const DoubleSimdMatrix&;


[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdVectorZero()noexcept{
    return SourceMath::DoubleVectorZero();
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdVectorSet(
    const f64 x,
    const f64 y,
    const f64 z,
    const f64 w
)noexcept{
    return SourceMath::DoubleVectorSet(x, y, z, w);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdVectorReplicate(const f64 value)noexcept{
    return SourceMath::DoubleVectorReplicate(value);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector LoadDoubleData(const f64& source)noexcept{
    return SourceMath::LoadDouble(&source);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector LoadDouble2Data(const Double2Data& source)noexcept{
    return SourceMath::LoadDouble2(&source);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector LoadDouble3Data(const Double3Data& source)noexcept{
    return SourceMath::LoadDouble3(&source);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector LoadDouble4Data(const Double4Data& source)noexcept{
    return SourceMath::LoadDouble4(&source);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector LoadDouble2AData(const AlignedDouble2Data& source)noexcept{
    return SourceMath::LoadDouble2A(&source);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector LoadDouble3AData(const AlignedDouble3Data& source)noexcept{
    return SourceMath::LoadDouble3A(&source);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector LoadDouble4AData(const AlignedDouble4Data& source)noexcept{
    return SourceMath::LoadDouble4A(&source);
}

NWB_INLINE void StoreDouble2Data(Double2Data& destination, DoubleVectorArg vector)noexcept{
    SourceMath::StoreDouble2(&destination, vector);
}

NWB_INLINE void StoreDouble3Data(Double3Data& destination, DoubleVectorArg vector)noexcept{
    SourceMath::StoreDouble3(&destination, vector);
}

NWB_INLINE void StoreDouble4Data(Double4Data& destination, DoubleVectorArg vector)noexcept{
    SourceMath::StoreDouble4(&destination, vector);
}

NWB_INLINE void StoreDoubleData(f64& destination, DoubleVectorArg vector)noexcept{
    SourceMath::StoreDouble(&destination, vector);
}

NWB_INLINE void StoreDouble2AData(AlignedDouble2Data& destination, DoubleVectorArg vector)noexcept{
    SourceMath::StoreDouble2A(&destination, vector);
}

NWB_INLINE void StoreDouble3AData(AlignedDouble3Data& destination, DoubleVectorArg vector)noexcept{
    SourceMath::StoreDouble3A(&destination, vector);
}

NWB_INLINE void StoreDouble4AData(AlignedDouble4Data& destination, DoubleVectorArg vector)noexcept{
    SourceMath::StoreDouble4A(&destination, vector);
}

// Preferred compact affine load path for NWB's column-vector convention.
[[nodiscard]] NWB_INLINE DoubleSimdMatrix LoadDouble3x4(const Double3x4Data& source)noexcept{
    return DoubleSimdMatrix(SourceMath::LoadDouble3x4(&source));
}

// Preferred compact affine load path for NWB's column-vector convention.
[[nodiscard]] NWB_INLINE DoubleSimdMatrix LoadDouble3x4A(const AlignedDouble3x4Data& source)noexcept{
    return DoubleSimdMatrix(SourceMath::LoadDouble3x4A(&source));
}

// Preferred compact affine store path for NWB's column-vector convention.
NWB_INLINE void StoreDouble3x4(Double3x4Data& destination, DoubleMatrixArg matrix)noexcept{
    SourceMath::StoreDouble3x4(&destination, matrix.m_value);
}

// Preferred compact affine store path for NWB's column-vector convention.
NWB_INLINE void StoreDouble3x4A(AlignedDouble3x4Data& destination, DoubleMatrixArg matrix)noexcept{
    SourceMath::StoreDouble3x4A(&destination, matrix.m_value);
}

[[nodiscard]] NWB_INLINE f64 DoubleSimdVectorGetX(DoubleVectorArg vector)noexcept{
    return SourceMath::DoubleVectorGetX(vector);
}

[[nodiscard]] NWB_INLINE f64 DoubleSimdVectorGetY(DoubleVectorArg vector)noexcept{
    return SourceMath::DoubleVectorGetY(vector);
}

[[nodiscard]] NWB_INLINE f64 DoubleSimdVectorGetZ(DoubleVectorArg vector)noexcept{
    return SourceMath::DoubleVectorGetZ(vector);
}

[[nodiscard]] NWB_INLINE f64 DoubleSimdVectorGetW(DoubleVectorArg vector)noexcept{
    return SourceMath::DoubleVectorGetW(vector);
}

[[nodiscard]] NWB_INLINE f64 DoubleSimdVectorGetByIndex(DoubleVectorArg vector, const usize index)noexcept{
    return SourceMath::DoubleVectorGetByIndex(vector, index);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdVectorSplatX(DoubleVectorArg vector)noexcept{
    return SourceMath::DoubleVectorSplatX(vector);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdVectorSplatY(DoubleVectorArg vector)noexcept{
    return SourceMath::DoubleVectorSplatY(vector);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdVectorSplatZ(DoubleVectorArg vector)noexcept{
    return SourceMath::DoubleVectorSplatZ(vector);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdVectorSplatW(DoubleVectorArg vector)noexcept{
    return SourceMath::DoubleVectorSplatW(vector);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdVectorAdd(DoubleVectorArg lhs, DoubleVectorArg rhs)noexcept{
    return SourceMath::DoubleVectorAdd(lhs, rhs);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdVectorSubtract(DoubleVectorArg lhs, DoubleVectorArg rhs)noexcept{
    return SourceMath::DoubleVectorSubtract(lhs, rhs);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdVectorMultiply(DoubleVectorArg lhs, DoubleVectorArg rhs)noexcept{
    return SourceMath::DoubleVectorMultiply(lhs, rhs);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdVectorDivide(DoubleVectorArg lhs, DoubleVectorArg rhs)noexcept{
    return SourceMath::DoubleVectorDivide(lhs, rhs);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdVectorMultiplyAdd(
    DoubleVectorArg lhs,
    DoubleVectorArg rhs,
    DoubleVectorArg addend
)noexcept{
    return SourceMath::DoubleVectorMultiplyAdd(lhs, rhs, addend);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdVectorNegate(DoubleVectorArg vector)noexcept{
    return SourceMath::DoubleVectorNegate(vector);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdVectorScale(DoubleVectorArg vector, const f64 scaleFactor)noexcept{
    return SourceMath::DoubleVectorScale(vector, scaleFactor);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdVectorSqrt(DoubleVectorArg vector)noexcept{
    return SourceMath::DoubleVectorSqrt(vector);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdVectorAbs(DoubleVectorArg vector)noexcept{
    return SourceMath::DoubleVectorAbs(vector);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdVector2Dot(DoubleVectorArg lhs, DoubleVectorArg rhs)noexcept{
    return SourceMath::DoubleVector2Dot(lhs, rhs);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdVector2Cross(DoubleVectorArg lhs, DoubleVectorArg rhs)noexcept{
    return SourceMath::DoubleVector2Cross(lhs, rhs);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdVector2LengthSq(DoubleVectorArg vector)noexcept{
    return SourceMath::DoubleVector2LengthSq(vector);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdVector2Length(DoubleVectorArg vector)noexcept{
    return SourceMath::DoubleVector2Length(vector);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdVector2Normalize(DoubleVectorArg vector)noexcept{
    return SourceMath::DoubleVector2Normalize(vector);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdVector3Dot(DoubleVectorArg lhs, DoubleVectorArg rhs)noexcept{
    return SourceMath::DoubleVector3Dot(lhs, rhs);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdVector4Dot(DoubleVectorArg lhs, DoubleVectorArg rhs)noexcept{
    return SourceMath::DoubleVector4Dot(lhs, rhs);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdVector3Cross(DoubleVectorArg lhs, DoubleVectorArg rhs)noexcept{
    return SourceMath::DoubleVector3Cross(lhs, rhs);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdVector3LengthSq(DoubleVectorArg vector)noexcept{
    return SourceMath::DoubleVector3LengthSq(vector);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdVector3Length(DoubleVectorArg vector)noexcept{
    return SourceMath::DoubleVector3Length(vector);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdVector3Normalize(DoubleVectorArg vector)noexcept{
    return SourceMath::DoubleVector3Normalize(vector);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdVector4LengthSq(DoubleVectorArg vector)noexcept{
    return SourceMath::DoubleVector4LengthSq(vector);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdVector4Length(DoubleVectorArg vector)noexcept{
    return SourceMath::DoubleVector4Length(vector);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdVector4Normalize(DoubleVectorArg vector)noexcept{
    return SourceMath::DoubleVector4Normalize(vector);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdVector2Transform(DoubleVectorArg vector, DoubleMatrixArg matrix)noexcept{
    return SourceMath::DoubleVector2Transform(vector, matrix.m_value);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdVector2TransformCoord(
    DoubleVectorArg vector,
    DoubleMatrixArg matrix
)noexcept{
    return SourceMath::DoubleVector2TransformCoord(vector, matrix.m_value);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdVector2TransformNormal(
    DoubleVectorArg vector,
    DoubleMatrixArg matrix
)noexcept{
    return SourceMath::DoubleVector2TransformNormal(vector, matrix.m_value);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdVector4Transform(DoubleVectorArg vector, DoubleMatrixArg matrix)noexcept{
    return SourceMath::DoubleVector4Transform(vector, matrix.m_value);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdVector3Transform(DoubleVectorArg vector, DoubleMatrixArg matrix)noexcept{
    return SourceMath::DoubleVector3Transform(vector, matrix.m_value);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdVector3TransformCoord(
    DoubleVectorArg vector,
    DoubleMatrixArg matrix
)noexcept{
    return SourceMath::DoubleVector3TransformCoord(vector, matrix.m_value);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdVector3TransformNormal(
    DoubleVectorArg vector,
    DoubleMatrixArg matrix
)noexcept{
    return SourceMath::DoubleVector3TransformNormal(vector, matrix.m_value);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdVector3Project(
    DoubleVectorArg vector,
    const f64 viewportX,
    const f64 viewportY,
    const f64 viewportWidth,
    const f64 viewportHeight,
    const f64 viewportMinZ,
    const f64 viewportMaxZ,
    DoubleMatrixArg projection,
    DoubleMatrixArg view,
    DoubleMatrixArg world
)noexcept{
    return SourceMath::DoubleVector3Project(
        vector,
        viewportX,
        viewportY,
        viewportWidth,
        viewportHeight,
        viewportMinZ,
        viewportMaxZ,
        projection.m_value,
        view.m_value,
        world.m_value
    );
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdVector3Unproject(
    DoubleVectorArg vector,
    const f64 viewportX,
    const f64 viewportY,
    const f64 viewportWidth,
    const f64 viewportHeight,
    const f64 viewportMinZ,
    const f64 viewportMaxZ,
    DoubleMatrixArg projection,
    DoubleMatrixArg view,
    DoubleMatrixArg world
)noexcept{
    return SourceMath::DoubleVector3Unproject(
        vector,
        viewportX,
        viewportY,
        viewportWidth,
        viewportHeight,
        viewportMinZ,
        viewportMaxZ,
        projection.m_value,
        view.m_value,
        world.m_value
    );
}

NWB_INLINE Double4Data* DoubleSimdVector2TransformStream(
    NotNull<Double4Data*> outputStream,
    const usize outputStride,
    NotNull<const Double2Data*> inputStream,
    const usize inputStride,
    const usize vectorCount,
    DoubleMatrixArg matrix
)noexcept{
    return SourceMath::DoubleVector2TransformStream(
        outputStream.get(),
        outputStride,
        inputStream.get(),
        inputStride,
        vectorCount,
        matrix.m_value
    );
}

NWB_INLINE Double2Data* DoubleSimdVector2TransformCoordStream(
    NotNull<Double2Data*> outputStream,
    const usize outputStride,
    NotNull<const Double2Data*> inputStream,
    const usize inputStride,
    const usize vectorCount,
    DoubleMatrixArg matrix
)noexcept{
    return SourceMath::DoubleVector2TransformCoordStream(
        outputStream.get(),
        outputStride,
        inputStream.get(),
        inputStride,
        vectorCount,
        matrix.m_value
    );
}

NWB_INLINE Double2Data* DoubleSimdVector2TransformNormalStream(
    NotNull<Double2Data*> outputStream,
    const usize outputStride,
    NotNull<const Double2Data*> inputStream,
    const usize inputStride,
    const usize vectorCount,
    DoubleMatrixArg matrix
)noexcept{
    return SourceMath::DoubleVector2TransformNormalStream(
        outputStream.get(),
        outputStride,
        inputStream.get(),
        inputStride,
        vectorCount,
        matrix.m_value
    );
}

NWB_INLINE Double4Data* DoubleSimdVector3TransformStream(
    NotNull<Double4Data*> outputStream,
    const usize outputStride,
    NotNull<const Double3Data*> inputStream,
    const usize inputStride,
    const usize vectorCount,
    DoubleMatrixArg matrix
)noexcept{
    return SourceMath::DoubleVector3TransformStream(
        outputStream.get(),
        outputStride,
        inputStream.get(),
        inputStride,
        vectorCount,
        matrix.m_value
    );
}

NWB_INLINE Double3Data* DoubleSimdVector3TransformCoordStream(
    NotNull<Double3Data*> outputStream,
    const usize outputStride,
    NotNull<const Double3Data*> inputStream,
    const usize inputStride,
    const usize vectorCount,
    DoubleMatrixArg matrix
)noexcept{
    return SourceMath::DoubleVector3TransformCoordStream(
        outputStream.get(),
        outputStride,
        inputStream.get(),
        inputStride,
        vectorCount,
        matrix.m_value
    );
}

NWB_INLINE Double3Data* DoubleSimdVector3TransformNormalStream(
    NotNull<Double3Data*> outputStream,
    const usize outputStride,
    NotNull<const Double3Data*> inputStream,
    const usize inputStride,
    const usize vectorCount,
    DoubleMatrixArg matrix
)noexcept{
    return SourceMath::DoubleVector3TransformNormalStream(
        outputStream.get(),
        outputStride,
        inputStream.get(),
        inputStride,
        vectorCount,
        matrix.m_value
    );
}

NWB_INLINE Double3Data* DoubleSimdVector3ProjectStream(
    NotNull<Double3Data*> outputStream,
    const usize outputStride,
    NotNull<const Double3Data*> inputStream,
    const usize inputStride,
    const usize vectorCount,
    const f64 viewportX,
    const f64 viewportY,
    const f64 viewportWidth,
    const f64 viewportHeight,
    const f64 viewportMinZ,
    const f64 viewportMaxZ,
    DoubleMatrixArg projection,
    DoubleMatrixArg view,
    DoubleMatrixArg world
)noexcept{
    return SourceMath::DoubleVector3ProjectStream(
        outputStream.get(),
        outputStride,
        inputStream.get(),
        inputStride,
        vectorCount,
        viewportX,
        viewportY,
        viewportWidth,
        viewportHeight,
        viewportMinZ,
        viewportMaxZ,
        projection.m_value,
        view.m_value,
        world.m_value
    );
}

NWB_INLINE Double3Data* DoubleSimdVector3UnprojectStream(
    NotNull<Double3Data*> outputStream,
    const usize outputStride,
    NotNull<const Double3Data*> inputStream,
    const usize inputStride,
    const usize vectorCount,
    const f64 viewportX,
    const f64 viewportY,
    const f64 viewportWidth,
    const f64 viewportHeight,
    const f64 viewportMinZ,
    const f64 viewportMaxZ,
    DoubleMatrixArg projection,
    DoubleMatrixArg view,
    DoubleMatrixArg world
)noexcept{
    return SourceMath::DoubleVector3UnprojectStream(
        outputStream.get(),
        outputStride,
        inputStream.get(),
        inputStride,
        vectorCount,
        viewportX,
        viewportY,
        viewportWidth,
        viewportHeight,
        viewportMinZ,
        viewportMaxZ,
        projection.m_value,
        view.m_value,
        world.m_value
    );
}

NWB_INLINE Double4Data* DoubleSimdVector4TransformStream(
    NotNull<Double4Data*> outputStream,
    const usize outputStride,
    NotNull<const Double4Data*> inputStream,
    const usize inputStride,
    const usize vectorCount,
    DoubleMatrixArg matrix
)noexcept{
    return SourceMath::DoubleVector4TransformStream(
        outputStream.get(),
        outputStride,
        inputStream.get(),
        inputStride,
        vectorCount,
        matrix.m_value
    );
}

[[nodiscard]] NWB_INLINE bool DoubleSimdMatrixIsNaN(DoubleMatrixArg matrix)noexcept{
    return SourceMath::DoubleMatrixIsNaN(matrix.m_value);
}

[[nodiscard]] NWB_INLINE bool DoubleSimdMatrixIsInfinite(DoubleMatrixArg matrix)noexcept{
    return SourceMath::DoubleMatrixIsInfinite(matrix.m_value);
}

[[nodiscard]] NWB_INLINE bool DoubleSimdMatrixIsIdentity(DoubleMatrixArg matrix)noexcept{
    return SourceMath::DoubleMatrixIsIdentity(matrix.m_value);
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixMultiply(DoubleMatrixArg lhs, DoubleMatrixArg rhs)noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixMultiply(lhs.m_value, rhs.m_value));
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixMultiplyTranspose(DoubleMatrixArg lhs, DoubleMatrixArg rhs)noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixMultiplyTranspose(lhs.m_value, rhs.m_value));
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixTranspose(DoubleMatrixArg matrix)noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixTranspose(matrix.m_value));
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixInverse(
    DoubleSimdVector* determinant,
    DoubleMatrixArg matrix
)noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixInverse(determinant, matrix.m_value));
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdMatrixDeterminant(DoubleMatrixArg matrix)noexcept{
    return SourceMath::DoubleMatrixDeterminant(matrix.m_value);
}

[[nodiscard]] NWB_INLINE bool DoubleSimdMatrixDecompose(
    DoubleSimdVector& outScale,
    DoubleSimdVector& outRotQuat,
    DoubleSimdVector& outTrans,
    DoubleMatrixArg matrix
)noexcept{
    return SourceMath::DoubleMatrixDecompose(&outScale, &outRotQuat, &outTrans, matrix.m_value);
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixVectorTensorProduct(DoubleVectorArg lhs, DoubleVectorArg rhs)noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixVectorTensorProduct(lhs, rhs));
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixZero()noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixZero());
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixIdentity()noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixIdentity());
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixSetColumns(
    DoubleVectorArg column0,
    DoubleVectorArg column1,
    DoubleVectorArg column2,
    DoubleVectorArg column3
)noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixSetColumns(column0, column1, column2, column3));
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixSet(
    const f64 m00, const f64 m01, const f64 m02, const f64 m03,
    const f64 m10, const f64 m11, const f64 m12, const f64 m13,
    const f64 m20, const f64 m21, const f64 m22, const f64 m23,
    const f64 m30, const f64 m31, const f64 m32, const f64 m33
)noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixSet(
        m00, m01, m02, m03,
        m10, m11, m12, m13,
        m20, m21, m22, m23,
        m30, m31, m32, m33
    ));
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixTranslation(
    const f64 offsetX,
    const f64 offsetY,
    const f64 offsetZ
)noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixTranslation(offsetX, offsetY, offsetZ));
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixTranslationFromVector(DoubleVectorArg offset)noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixTranslationFromVector(offset));
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixScaling(
    const f64 scaleX,
    const f64 scaleY,
    const f64 scaleZ
)noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixScaling(scaleX, scaleY, scaleZ));
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixScalingFromVector(DoubleVectorArg scale)noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixScalingFromVector(scale));
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixRotationX(const f64 angle)noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixRotationX(angle));
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixRotationY(const f64 angle)noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixRotationY(angle));
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixRotationZ(const f64 angle)noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixRotationZ(angle));
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixRotationRollPitchYaw(
    const f64 pitch,
    const f64 yaw,
    const f64 roll
)noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixRotationRollPitchYaw(pitch, yaw, roll));
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixRotationRollPitchYawFromVector(DoubleVectorArg angles)noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixRotationRollPitchYawFromVector(angles));
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixRotationNormal(DoubleVectorArg normalAxis, const f64 angle)noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixRotationNormal(normalAxis, angle));
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixRotationAxis(DoubleVectorArg axis, const f64 angle)noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixRotationAxis(axis, angle));
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixRotationQuaternion(DoubleVectorArg quaternion)noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixRotationQuaternion(quaternion));
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixTransformation2D(
    DoubleVectorArg scalingOrigin,
    const f64 scalingOrientation,
    DoubleVectorArg scaling,
    DoubleVectorArg rotationOrigin,
    const f64 rotation,
    DoubleVectorArg translation
)noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixTransformation2D(
        scalingOrigin,
        scalingOrientation,
        scaling,
        rotationOrigin,
        rotation,
        translation
    ));
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixTransformation(
    DoubleVectorArg scalingOrigin,
    DoubleVectorArg scalingOrientationQuaternion,
    DoubleVectorArg scaling,
    DoubleVectorArg rotationOrigin,
    DoubleVectorArg rotationQuaternion,
    DoubleVectorArg translation
)noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixTransformation(
        scalingOrigin,
        scalingOrientationQuaternion,
        scaling,
        rotationOrigin,
        rotationQuaternion,
        translation
    ));
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixAffineTransformation2D(
    DoubleVectorArg scaling,
    DoubleVectorArg rotationOrigin,
    const f64 rotation,
    DoubleVectorArg translation
)noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixAffineTransformation2D(scaling, rotationOrigin, rotation, translation));
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixAffineTransformation(
    DoubleVectorArg scaling,
    DoubleVectorArg rotationOrigin,
    DoubleVectorArg rotationQuaternion,
    DoubleVectorArg translation
)noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixAffineTransformation(
        scaling,
        rotationOrigin,
        rotationQuaternion,
        translation
    ));
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixReflect(DoubleVectorArg reflectionPlane)noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixReflect(reflectionPlane));
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixShadow(
    DoubleVectorArg shadowPlane,
    DoubleVectorArg lightPosition
)noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixShadow(shadowPlane, lightPosition));
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixLookAtLH(
    DoubleVectorArg eyePosition,
    DoubleVectorArg focusPosition,
    DoubleVectorArg upDirection
)noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixLookAtLH(eyePosition, focusPosition, upDirection));
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixLookAtRH(
    DoubleVectorArg eyePosition,
    DoubleVectorArg focusPosition,
    DoubleVectorArg upDirection
)noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixLookAtRH(eyePosition, focusPosition, upDirection));
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixLookToLH(
    DoubleVectorArg eyePosition,
    DoubleVectorArg eyeDirection,
    DoubleVectorArg upDirection
)noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixLookToLH(eyePosition, eyeDirection, upDirection));
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixLookToRH(
    DoubleVectorArg eyePosition,
    DoubleVectorArg eyeDirection,
    DoubleVectorArg upDirection
)noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixLookToRH(eyePosition, eyeDirection, upDirection));
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixPerspectiveLH(
    const f64 viewWidth,
    const f64 viewHeight,
    const f64 nearZ,
    const f64 farZ
)noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixPerspectiveLH(viewWidth, viewHeight, nearZ, farZ));
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixPerspectiveRH(
    const f64 viewWidth,
    const f64 viewHeight,
    const f64 nearZ,
    const f64 farZ
)noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixPerspectiveRH(viewWidth, viewHeight, nearZ, farZ));
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixPerspectiveFovLH(
    const f64 fovAngleY,
    const f64 aspectRatio,
    const f64 nearZ,
    const f64 farZ
)noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixPerspectiveFovLH(fovAngleY, aspectRatio, nearZ, farZ));
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixPerspectiveFovRH(
    const f64 fovAngleY,
    const f64 aspectRatio,
    const f64 nearZ,
    const f64 farZ
)noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixPerspectiveFovRH(fovAngleY, aspectRatio, nearZ, farZ));
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixPerspectiveOffCenterLH(
    const f64 viewLeft,
    const f64 viewRight,
    const f64 viewBottom,
    const f64 viewTop,
    const f64 nearZ,
    const f64 farZ
)noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixPerspectiveOffCenterLH(
        viewLeft,
        viewRight,
        viewBottom,
        viewTop,
        nearZ,
        farZ
    ));
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixPerspectiveOffCenterRH(
    const f64 viewLeft,
    const f64 viewRight,
    const f64 viewBottom,
    const f64 viewTop,
    const f64 nearZ,
    const f64 farZ
)noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixPerspectiveOffCenterRH(
        viewLeft,
        viewRight,
        viewBottom,
        viewTop,
        nearZ,
        farZ
    ));
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixOrthographicLH(
    const f64 viewWidth,
    const f64 viewHeight,
    const f64 nearZ,
    const f64 farZ
)noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixOrthographicLH(viewWidth, viewHeight, nearZ, farZ));
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixOrthographicRH(
    const f64 viewWidth,
    const f64 viewHeight,
    const f64 nearZ,
    const f64 farZ
)noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixOrthographicRH(viewWidth, viewHeight, nearZ, farZ));
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixOrthographicOffCenterLH(
    const f64 viewLeft,
    const f64 viewRight,
    const f64 viewBottom,
    const f64 viewTop,
    const f64 nearZ,
    const f64 farZ
)noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixOrthographicOffCenterLH(
        viewLeft,
        viewRight,
        viewBottom,
        viewTop,
        nearZ,
        farZ
    ));
}

[[nodiscard]] NWB_INLINE DoubleSimdMatrix DoubleSimdMatrixOrthographicOffCenterRH(
    const f64 viewLeft,
    const f64 viewRight,
    const f64 viewBottom,
    const f64 viewTop,
    const f64 nearZ,
    const f64 farZ
)noexcept{
    return DoubleSimdMatrix(SourceMath::DoubleMatrixOrthographicOffCenterRH(
        viewLeft,
        viewRight,
        viewBottom,
        viewTop,
        nearZ,
        farZ
    ));
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdQuaternionRotationMatrix(DoubleMatrixArg matrix)noexcept{
    return SourceMath::DoubleQuaternionRotationMatrix(matrix.m_value);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdQuaternionIdentity()noexcept{
    return SourceMath::DoubleQuaternionIdentity();
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdQuaternionConjugate(DoubleVectorArg quaternion)noexcept{
    return SourceMath::DoubleQuaternionConjugate(quaternion);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdQuaternionMultiply(
    DoubleVectorArg lhs,
    DoubleVectorArg rhs
)noexcept{
    return SourceMath::DoubleQuaternionMultiply(lhs, rhs);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdQuaternionNormalize(DoubleVectorArg quaternion)noexcept{
    return SourceMath::DoubleQuaternionNormalize(quaternion);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdQuaternionRotationNormal(
    DoubleVectorArg normalAxis,
    const f64 angle
)noexcept{
    return SourceMath::DoubleQuaternionRotationNormal(normalAxis, angle);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdQuaternionRotationAxis(
    DoubleVectorArg axis,
    const f64 angle
)noexcept{
    return SourceMath::DoubleQuaternionRotationAxis(axis, angle);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdQuaternionRotationRollPitchYaw(
    const f64 pitch,
    const f64 yaw,
    const f64 roll
)noexcept{
    return SourceMath::DoubleQuaternionRotationRollPitchYaw(pitch, yaw, roll);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdQuaternionRotationRollPitchYawFromVector(
    DoubleVectorArg angles
)noexcept{
    return SourceMath::DoubleQuaternionRotationRollPitchYawFromVector(angles);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdPlaneDot(DoubleVectorArg plane, DoubleVectorArg vector)noexcept{
    return SourceMath::DoublePlaneDot(plane, vector);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdPlaneNormalize(DoubleVectorArg plane)noexcept{
    return SourceMath::DoublePlaneNormalize(plane);
}

[[nodiscard]] NWB_INLINE DoubleSimdVector DoubleSimdPlaneTransform(
    DoubleVectorArg plane,
    DoubleMatrixArg inverseTransposeMatrix
)noexcept{
    return SourceMath::DoublePlaneTransform(plane, inverseTransposeMatrix.m_value);
}

NWB_INLINE Double4Data* DoubleSimdPlaneTransformStream(
    NotNull<Double4Data*> outputStream,
    const usize outputStride,
    NotNull<const Double4Data*> inputStream,
    const usize inputStride,
    const usize planeCount,
    DoubleMatrixArg inverseTransposeMatrix
)noexcept{
    return SourceMath::DoublePlaneTransformStream(
        outputStream.get(),
        outputStride,
        inputStream.get(),
        inputStride,
        planeCount,
        inverseTransposeMatrix.m_value
    );
}

// Preferred compact affine load path for NWB's column-vector convention.
[[nodiscard]] NWB_INLINE SimdMatrix LoadFloat3x4(const Float3x4Data& source)noexcept{
    return SimdMatrix(SourceMath::LoadFloat3x4(&source));
}

// Preferred compact affine load path for NWB's column-vector convention.
[[nodiscard]] NWB_INLINE SimdMatrix LoadFloat3x4A(const AlignedFloat3x4Data& source)noexcept{
    return SimdMatrix(SourceMath::LoadFloat3x4A(&source));
}

// Preferred compact affine store path for NWB's column-vector convention.
NWB_INLINE void StoreFloat3x4(Float3x4Data& destination, MatrixArg matrix)noexcept{
    SourceMath::StoreFloat3x4(&destination, matrix.m_value);
}

// Preferred compact affine store path for NWB's column-vector convention.
NWB_INLINE void StoreFloat3x4A(AlignedFloat3x4Data& destination, MatrixArg matrix)noexcept{
    SourceMath::StoreFloat3x4A(&destination, matrix.m_value);
}

[[nodiscard]] NWB_INLINE SimdVector Vector2Transform(VectorArg vector, MatrixArg matrix)noexcept{
    return SourceMath::Vector2Transform(vector, matrix.m_value);
}

NWB_INLINE Float4Data* Vector2TransformStream(
    NotNull<Float4Data*> outputStream,
    const usize outputStride,
    NotNull<const Float2Data*> inputStream,
    const usize inputStride,
    const usize vectorCount,
    MatrixArg matrix
)noexcept{
    return SourceMath::Vector2TransformStream(
        outputStream.get(),
        outputStride,
        inputStream.get(),
        inputStride,
        vectorCount,
        matrix.m_value
    );
}

[[nodiscard]] NWB_INLINE SimdVector Vector2TransformCoord(VectorArg vector, MatrixArg matrix)noexcept{
    return SourceMath::Vector2TransformCoord(vector, matrix.m_value);
}

NWB_INLINE Float2Data* Vector2TransformCoordStream(
    NotNull<Float2Data*> outputStream,
    const usize outputStride,
    NotNull<const Float2Data*> inputStream,
    const usize inputStride,
    const usize vectorCount,
    MatrixArg matrix
)noexcept{
    return SourceMath::Vector2TransformCoordStream(
        outputStream.get(),
        outputStride,
        inputStream.get(),
        inputStride,
        vectorCount,
        matrix.m_value
    );
}

[[nodiscard]] NWB_INLINE SimdVector Vector2TransformNormal(VectorArg vector, MatrixArg matrix)noexcept{
    return SourceMath::Vector2TransformNormal(vector, matrix.m_value);
}

NWB_INLINE Float2Data* Vector2TransformNormalStream(
    NotNull<Float2Data*> outputStream,
    const usize outputStride,
    NotNull<const Float2Data*> inputStream,
    const usize inputStride,
    const usize vectorCount,
    MatrixArg matrix
)noexcept{
    return SourceMath::Vector2TransformNormalStream(
        outputStream.get(),
        outputStride,
        inputStream.get(),
        inputStride,
        vectorCount,
        matrix.m_value
    );
}


[[nodiscard]] NWB_INLINE SimdVector Vector3Transform(VectorArg vector, MatrixArg matrix)noexcept{
    return SourceMath::Vector3Transform(vector, matrix.m_value);
}

NWB_INLINE Float4Data* Vector3TransformStream(
    NotNull<Float4Data*> outputStream,
    const usize outputStride,
    NotNull<const Float3Data*> inputStream,
    const usize inputStride,
    const usize vectorCount,
    MatrixArg matrix
)noexcept{
    return SourceMath::Vector3TransformStream(
        outputStream.get(),
        outputStride,
        inputStream.get(),
        inputStride,
        vectorCount,
        matrix.m_value
    );
}

[[nodiscard]] NWB_INLINE SimdVector Vector3TransformCoord(VectorArg vector, MatrixArg matrix)noexcept{
    return SourceMath::Vector3TransformCoord(vector, matrix.m_value);
}

NWB_INLINE Float3Data* Vector3TransformCoordStream(
    NotNull<Float3Data*> outputStream,
    const usize outputStride,
    NotNull<const Float3Data*> inputStream,
    const usize inputStride,
    const usize vectorCount,
    MatrixArg matrix
)noexcept{
    return SourceMath::Vector3TransformCoordStream(
        outputStream.get(),
        outputStride,
        inputStream.get(),
        inputStride,
        vectorCount,
        matrix.m_value
    );
}

[[nodiscard]] NWB_INLINE SimdVector Vector3TransformNormal(VectorArg vector, MatrixArg matrix)noexcept{
    return SourceMath::Vector3TransformNormal(vector, matrix.m_value);
}

NWB_INLINE Float3Data* Vector3TransformNormalStream(
    NotNull<Float3Data*> outputStream,
    const usize outputStride,
    NotNull<const Float3Data*> inputStream,
    const usize inputStride,
    const usize vectorCount,
    MatrixArg matrix
)noexcept{
    return SourceMath::Vector3TransformNormalStream(
        outputStream.get(),
        outputStride,
        inputStream.get(),
        inputStride,
        vectorCount,
        matrix.m_value
    );
}

[[nodiscard]] NWB_INLINE SimdVector Vector3Project(
    VectorArg vector,
    const f32 viewportX,
    const f32 viewportY,
    const f32 viewportWidth,
    const f32 viewportHeight,
    const f32 viewportMinZ,
    const f32 viewportMaxZ,
    MatrixArg projection,
    MatrixArg view,
    MatrixArg world
)noexcept{
    return SourceMath::Vector3Project(
        vector,
        viewportX,
        viewportY,
        viewportWidth,
        viewportHeight,
        viewportMinZ,
        viewportMaxZ,
        projection.m_value,
        view.m_value,
        world.m_value
    );
}

NWB_INLINE Float3Data* Vector3ProjectStream(
    NotNull<Float3Data*> outputStream,
    const usize outputStride,
    NotNull<const Float3Data*> inputStream,
    const usize inputStride,
    const usize vectorCount,
    const f32 viewportX,
    const f32 viewportY,
    const f32 viewportWidth,
    const f32 viewportHeight,
    const f32 viewportMinZ,
    const f32 viewportMaxZ,
    MatrixArg projection,
    MatrixArg view,
    MatrixArg world
)noexcept{
    return SourceMath::Vector3ProjectStream(
        outputStream.get(),
        outputStride,
        inputStream.get(),
        inputStride,
        vectorCount,
        viewportX,
        viewportY,
        viewportWidth,
        viewportHeight,
        viewportMinZ,
        viewportMaxZ,
        projection.m_value,
        view.m_value,
        world.m_value
    );
}

[[nodiscard]] NWB_INLINE SimdVector Vector3Unproject(
    VectorArg vector,
    const f32 viewportX,
    const f32 viewportY,
    const f32 viewportWidth,
    const f32 viewportHeight,
    const f32 viewportMinZ,
    const f32 viewportMaxZ,
    MatrixArg projection,
    MatrixArg view,
    MatrixArg world
)noexcept{
    return SourceMath::Vector3Unproject(
        vector,
        viewportX,
        viewportY,
        viewportWidth,
        viewportHeight,
        viewportMinZ,
        viewportMaxZ,
        projection.m_value,
        view.m_value,
        world.m_value
    );
}

NWB_INLINE Float3Data* Vector3UnprojectStream(
    NotNull<Float3Data*> outputStream,
    const usize outputStride,
    NotNull<const Float3Data*> inputStream,
    const usize inputStride,
    const usize vectorCount,
    const f32 viewportX,
    const f32 viewportY,
    const f32 viewportWidth,
    const f32 viewportHeight,
    const f32 viewportMinZ,
    const f32 viewportMaxZ,
    MatrixArg projection,
    MatrixArg view,
    MatrixArg world
)noexcept{
    return SourceMath::Vector3UnprojectStream(
        outputStream.get(),
        outputStride,
        inputStream.get(),
        inputStride,
        vectorCount,
        viewportX,
        viewportY,
        viewportWidth,
        viewportHeight,
        viewportMinZ,
        viewportMaxZ,
        projection.m_value,
        view.m_value,
        world.m_value
    );
}


[[nodiscard]] NWB_INLINE SimdVector Vector4Transform(VectorArg vector, MatrixArg matrix)noexcept{
    return SourceMath::Vector4Transform(vector, matrix.m_value);
}

NWB_INLINE Float4Data* Vector4TransformStream(
    NotNull<Float4Data*> outputStream,
    const usize outputStride,
    NotNull<const Float4Data*> inputStream,
    const usize inputStride,
    const usize vectorCount,
    MatrixArg matrix
)noexcept{
    return SourceMath::Vector4TransformStream(
        outputStream.get(),
        outputStride,
        inputStream.get(),
        inputStride,
        vectorCount,
        matrix.m_value
    );
}


[[nodiscard]] NWB_INLINE bool MatrixIsNaN(MatrixArg matrix)noexcept{
    return SourceMath::MatrixIsNaN(matrix.m_value);
}

[[nodiscard]] NWB_INLINE bool MatrixIsInfinite(MatrixArg matrix)noexcept{
    return SourceMath::MatrixIsInfinite(matrix.m_value);
}

[[nodiscard]] NWB_INLINE bool MatrixIsIdentity(MatrixArg matrix)noexcept{
    return SourceMath::MatrixIsIdentity(matrix.m_value);
}

[[nodiscard]] NWB_INLINE SimdMatrix MatrixMultiply(MatrixArg lhs, MatrixArg rhs)noexcept{
    // Column-vector convention: `MatrixMultiply(a, b)` returns `a * b`.
    return SimdMatrix(SourceMath::MatrixMultiply(lhs.m_value, rhs.m_value));
}

[[nodiscard]] NWB_INLINE SimdMatrix MatrixMultiplyTranspose(MatrixArg lhs, MatrixArg rhs)noexcept{
    // Column-vector convention: transpose of `lhs * rhs`.
    return SimdMatrix(SourceMath::MatrixMultiplyTranspose(lhs.m_value, rhs.m_value));
}

[[nodiscard]] NWB_INLINE SimdMatrix MatrixTranspose(MatrixArg matrix)noexcept{
    return SimdMatrix(SourceMath::MatrixTranspose(matrix.m_value));
}

[[nodiscard]] NWB_INLINE SimdMatrix MatrixInverse(SimdVector* determinant, MatrixArg matrix)noexcept{
    return SimdMatrix(SourceMath::MatrixInverse(determinant, matrix.m_value));
}

[[nodiscard]] NWB_INLINE SimdMatrix MatrixVectorTensorProduct(VectorArg lhs, VectorArg rhs)noexcept{
    return SimdMatrix(SourceMath::MatrixVectorTensorProduct(lhs, rhs));
}

[[nodiscard]] NWB_INLINE SimdVector MatrixDeterminant(MatrixArg matrix)noexcept{
    return SourceMath::MatrixDeterminant(matrix.m_value);
}

[[nodiscard]] NWB_INLINE bool MatrixDecompose(
    SimdVector& outScale,
    SimdVector& outRotQuat,
    SimdVector& outTrans,
    MatrixArg matrix
)noexcept{
    return SourceMath::MatrixDecompose(&outScale, &outRotQuat, &outTrans, matrix.m_value);
}

[[nodiscard]] NWB_INLINE SimdMatrix MatrixIdentity()noexcept{
    return SimdMatrix(SourceMath::MatrixIdentity());
}

[[nodiscard]] NWB_INLINE SimdMatrix MatrixSet(
    const f32 m00,
    const f32 m01,
    const f32 m02,
    const f32 m03,
    const f32 m10,
    const f32 m11,
    const f32 m12,
    const f32 m13,
    const f32 m20,
    const f32 m21,
    const f32 m22,
    const f32 m23,
    const f32 m30,
    const f32 m31,
    const f32 m32,
    const f32 m33
)noexcept{
    // Scalars are supplied in mathematical row/column order and converted to
    // NWB's internal column layout.
    return SimdMatrix(SourceMath::MatrixSet(
        m00,
        m01,
        m02,
        m03,
        m10,
        m11,
        m12,
        m13,
        m20,
        m21,
        m22,
        m23,
        m30,
        m31,
        m32,
        m33
    ));
}

[[nodiscard]] NWB_INLINE SimdMatrix MatrixTranslation(const f32 offsetX, const f32 offsetY, const f32 offsetZ)noexcept{
    return SimdMatrix(SourceMath::MatrixTranslation(offsetX, offsetY, offsetZ));
}

[[nodiscard]] NWB_INLINE SimdMatrix MatrixTranslationFromVector(VectorArg offset)noexcept{
    return SimdMatrix(SourceMath::MatrixTranslationFromVector(offset));
}

[[nodiscard]] NWB_INLINE SimdMatrix MatrixScaling(const f32 scaleX, const f32 scaleY, const f32 scaleZ)noexcept{
    return SimdMatrix(SourceMath::MatrixScaling(scaleX, scaleY, scaleZ));
}

[[nodiscard]] NWB_INLINE SimdMatrix MatrixScalingFromVector(VectorArg scale)noexcept{
    return SimdMatrix(SourceMath::MatrixScalingFromVector(scale));
}

[[nodiscard]] NWB_INLINE SimdMatrix MatrixRotationX(const f32 angle)noexcept{
    return SimdMatrix(SourceMath::MatrixRotationX(angle));
}

[[nodiscard]] NWB_INLINE SimdMatrix MatrixRotationY(const f32 angle)noexcept{
    return SimdMatrix(SourceMath::MatrixRotationY(angle));
}

[[nodiscard]] NWB_INLINE SimdMatrix MatrixRotationZ(const f32 angle)noexcept{
    return SimdMatrix(SourceMath::MatrixRotationZ(angle));
}

[[nodiscard]] NWB_INLINE SimdMatrix MatrixRotationRollPitchYaw(const f32 pitch, const f32 yaw, const f32 roll)noexcept{
    return SimdMatrix(SourceMath::MatrixRotationRollPitchYaw(pitch, yaw, roll));
}

[[nodiscard]] NWB_INLINE SimdMatrix MatrixRotationRollPitchYawFromVector(VectorArg angles)noexcept{
    return SimdMatrix(SourceMath::MatrixRotationRollPitchYawFromVector(angles));
}

[[nodiscard]] NWB_INLINE SimdMatrix MatrixRotationNormal(VectorArg normalAxis, const f32 angle)noexcept{
    return SimdMatrix(SourceMath::MatrixRotationNormal(normalAxis, angle));
}

[[nodiscard]] NWB_INLINE SimdMatrix MatrixRotationAxis(VectorArg axis, const f32 angle)noexcept{
    return SimdMatrix(SourceMath::MatrixRotationAxis(axis, angle));
}

[[nodiscard]] NWB_INLINE SimdMatrix MatrixRotationQuaternion(VectorArg quaternion)noexcept{
    return SimdMatrix(SourceMath::MatrixRotationQuaternion(quaternion));
}

[[nodiscard]] NWB_INLINE SimdMatrix MatrixTransformation2D(
    VectorArg scalingOrigin,
    const f32 scalingOrientation,
    VectorArg scaling,
    VectorArg rotationOrigin,
    const f32 rotation,
    VectorArg2 translation
)noexcept{
    return SimdMatrix(SourceMath::MatrixTransformation2D(
        scalingOrigin,
        scalingOrientation,
        scaling,
        rotationOrigin,
        rotation,
        translation
    ));
}

[[nodiscard]] NWB_INLINE SimdMatrix MatrixTransformation(
    VectorArg scalingOrigin,
    VectorArg scalingOrientationQuaternion,
    VectorArg scaling,
    VectorArg2 rotationOrigin,
    VectorArg3 rotationQuaternion,
    VectorArg3 translation
)noexcept{
    return SimdMatrix(SourceMath::MatrixTransformation(
        scalingOrigin,
        scalingOrientationQuaternion,
        scaling,
        rotationOrigin,
        rotationQuaternion,
        translation
    ));
}

[[nodiscard]] NWB_INLINE SimdMatrix MatrixAffineTransformation2D(
    VectorArg scaling,
    VectorArg rotationOrigin,
    const f32 rotation,
    VectorArg translation
)noexcept{
    return SimdMatrix(SourceMath::MatrixAffineTransformation2D(scaling, rotationOrigin, rotation, translation));
}

[[nodiscard]] NWB_INLINE SimdMatrix MatrixAffineTransformation(
    VectorArg scaling,
    VectorArg rotationOrigin,
    VectorArg rotationQuaternion,
    VectorArg2 translation
)noexcept{
    return SimdMatrix(SourceMath::MatrixAffineTransformation(scaling, rotationOrigin, rotationQuaternion, translation));
}

[[nodiscard]] NWB_INLINE SimdMatrix MatrixReflect(VectorArg reflectionPlane)noexcept{
    return SimdMatrix(SourceMath::MatrixReflect(reflectionPlane));
}

[[nodiscard]] NWB_INLINE SimdMatrix MatrixShadow(VectorArg shadowPlane, VectorArg lightPosition)noexcept{
    return SimdMatrix(SourceMath::MatrixShadow(shadowPlane, lightPosition));
}

[[nodiscard]] NWB_INLINE SimdMatrix MatrixLookAtLH(
    VectorArg eyePosition,
    VectorArg focusPosition,
    VectorArg upDirection
)noexcept{
    return SimdMatrix(SourceMath::MatrixLookAtLH(eyePosition, focusPosition, upDirection));
}

[[nodiscard]] NWB_INLINE SimdMatrix MatrixLookAtRH(
    VectorArg eyePosition,
    VectorArg focusPosition,
    VectorArg upDirection
)noexcept{
    return SimdMatrix(SourceMath::MatrixLookAtRH(eyePosition, focusPosition, upDirection));
}

[[nodiscard]] NWB_INLINE SimdMatrix MatrixLookToLH(
    VectorArg eyePosition,
    VectorArg eyeDirection,
    VectorArg upDirection
)noexcept{
    return SimdMatrix(SourceMath::MatrixLookToLH(eyePosition, eyeDirection, upDirection));
}

[[nodiscard]] NWB_INLINE SimdMatrix MatrixLookToRH(
    VectorArg eyePosition,
    VectorArg eyeDirection,
    VectorArg upDirection
)noexcept{
    return SimdMatrix(SourceMath::MatrixLookToRH(eyePosition, eyeDirection, upDirection));
}

[[nodiscard]] NWB_INLINE SimdMatrix MatrixPerspectiveLH(
    const f32 viewWidth,
    const f32 viewHeight,
    const f32 nearZ,
    const f32 farZ
)noexcept{
    return SimdMatrix(SourceMath::MatrixPerspectiveLH(viewWidth, viewHeight, nearZ, farZ));
}

[[nodiscard]] NWB_INLINE SimdMatrix MatrixPerspectiveRH(
    const f32 viewWidth,
    const f32 viewHeight,
    const f32 nearZ,
    const f32 farZ
)noexcept{
    return SimdMatrix(SourceMath::MatrixPerspectiveRH(viewWidth, viewHeight, nearZ, farZ));
}

[[nodiscard]] NWB_INLINE SimdMatrix MatrixPerspectiveFovLH(
    const f32 fovAngleY,
    const f32 aspectRatio,
    const f32 nearZ,
    const f32 farZ
)noexcept{
    return SimdMatrix(SourceMath::MatrixPerspectiveFovLH(fovAngleY, aspectRatio, nearZ, farZ));
}

[[nodiscard]] NWB_INLINE SimdMatrix MatrixPerspectiveFovRH(
    const f32 fovAngleY,
    const f32 aspectRatio,
    const f32 nearZ,
    const f32 farZ
)noexcept{
    return SimdMatrix(SourceMath::MatrixPerspectiveFovRH(fovAngleY, aspectRatio, nearZ, farZ));
}

[[nodiscard]] NWB_INLINE SimdMatrix MatrixPerspectiveOffCenterLH(
    const f32 viewLeft,
    const f32 viewRight,
    const f32 viewBottom,
    const f32 viewTop,
    const f32 nearZ,
    const f32 farZ
)noexcept{
    return SimdMatrix(SourceMath::MatrixPerspectiveOffCenterLH(viewLeft, viewRight, viewBottom, viewTop, nearZ, farZ));
}

[[nodiscard]] NWB_INLINE SimdMatrix MatrixPerspectiveOffCenterRH(
    const f32 viewLeft,
    const f32 viewRight,
    const f32 viewBottom,
    const f32 viewTop,
    const f32 nearZ,
    const f32 farZ
)noexcept{
    return SimdMatrix(SourceMath::MatrixPerspectiveOffCenterRH(viewLeft, viewRight, viewBottom, viewTop, nearZ, farZ));
}

[[nodiscard]] NWB_INLINE SimdMatrix MatrixOrthographicLH(
    const f32 viewWidth,
    const f32 viewHeight,
    const f32 nearZ,
    const f32 farZ
)noexcept{
    return SimdMatrix(SourceMath::MatrixOrthographicLH(viewWidth, viewHeight, nearZ, farZ));
}

[[nodiscard]] NWB_INLINE SimdMatrix MatrixOrthographicRH(
    const f32 viewWidth,
    const f32 viewHeight,
    const f32 nearZ,
    const f32 farZ
)noexcept{
    return SimdMatrix(SourceMath::MatrixOrthographicRH(viewWidth, viewHeight, nearZ, farZ));
}

[[nodiscard]] NWB_INLINE SimdMatrix MatrixOrthographicOffCenterLH(
    const f32 viewLeft,
    const f32 viewRight,
    const f32 viewBottom,
    const f32 viewTop,
    const f32 nearZ,
    const f32 farZ
)noexcept{
    return SimdMatrix(SourceMath::MatrixOrthographicOffCenterLH(viewLeft, viewRight, viewBottom, viewTop, nearZ, farZ));
}

[[nodiscard]] NWB_INLINE SimdMatrix MatrixOrthographicOffCenterRH(
    const f32 viewLeft,
    const f32 viewRight,
    const f32 viewBottom,
    const f32 viewTop,
    const f32 nearZ,
    const f32 farZ
)noexcept{
    return SimdMatrix(SourceMath::MatrixOrthographicOffCenterRH(viewLeft, viewRight, viewBottom, viewTop, nearZ, farZ));
}


[[nodiscard]] NWB_INLINE SimdVector QuaternionRotationMatrix(MatrixArg matrix)noexcept{
    return SourceMath::QuaternionRotationMatrix(matrix.m_value);
}


[[nodiscard]] NWB_INLINE SimdVector PlaneTransform(VectorArg plane, MatrixArg inverseTransposeMatrix)noexcept{
    return SourceMath::PlaneTransform(plane, inverseTransposeMatrix.m_value);
}

NWB_INLINE Float4Data* PlaneTransformStream(
    NotNull<Float4Data*> outputStream,
    const usize outputStride,
    NotNull<const Float4Data*> inputStream,
    const usize inputStride,
    const usize planeCount,
    MatrixArg inverseTransposeMatrix
)noexcept{
    return SourceMath::PlaneTransformStream(
        outputStream.get(),
        outputStride,
        inputStream.get(),
        inputStride,
        planeCount,
        inverseTransposeMatrix.m_value
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

