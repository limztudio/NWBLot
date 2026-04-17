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

using Float2Data = SourceMath::Float2;
using Float3Data = SourceMath::Float3;
using Float4Data = SourceMath::Float4;
using AlignedFloat2Data = SourceMath::AlignedFloat2;
using AlignedFloat3Data = SourceMath::AlignedFloat3;
using AlignedFloat4Data = SourceMath::AlignedFloat4;

using Float3x4Data = SourceMath::Float3x4;
using AlignedFloat3x4Data = SourceMath::AlignedFloat3x4;


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
    [[nodiscard]] SimdVector& operator[](const usize index)noexcept{ return m_value.r[index]; }
    [[nodiscard]] const SimdVector& operator[](const usize index)const noexcept{ return m_value.r[index]; }
    [[nodiscard]] const SourceMath::Matrix& nativeMatrix()const noexcept{ return m_value; }
};

using MatrixArg = const SimdMatrix&;

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

