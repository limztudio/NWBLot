// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Intentionally no `#pragma once`: this helper is included inside multiple
// backend-specific namespaces.
namespace ScalarVectorStreamDetail{

template<typename InputType, typename OutputType>
[[nodiscard]] inline bool HasTightStride(const size_t inputStride, const size_t outputStride)noexcept{
    return (inputStride == sizeof(InputType)) && (outputStride == sizeof(OutputType));
}


struct MatrixColumns{
    float m00;
    float m10;
    float m20;
    float m30;
    float m01;
    float m11;
    float m21;
    float m31;
    float m02;
    float m12;
    float m22;
    float m32;
    float m03;
    float m13;
    float m23;
    float m33;

    explicit MatrixColumns(FXMMATRIX matrix)noexcept{
        Float4 column0;
        Float4 column1;
        Float4 column2;
        Float4 column3;
        StoreFloat4(&column0, matrix.r[0]);
        StoreFloat4(&column1, matrix.r[1]);
        StoreFloat4(&column2, matrix.r[2]);
        StoreFloat4(&column3, matrix.r[3]);

        m00 = column0.x;
        m10 = column0.y;
        m20 = column0.z;
        m30 = column0.w;
        m01 = column1.x;
        m11 = column1.y;
        m21 = column1.z;
        m31 = column1.w;
        m02 = column2.x;
        m12 = column2.y;
        m22 = column2.z;
        m32 = column2.w;
        m03 = column3.x;
        m13 = column3.y;
        m23 = column3.z;
        m33 = column3.w;
    }

    inline void Transform2(
        const float x,
        const float y,
        float& outX,
        float& outY,
        float& outZ,
        float& outW
    )const noexcept{
        outX = (x * m00) + (y * m01) + m03;
        outY = (x * m10) + (y * m11) + m13;
        outZ = (x * m20) + (y * m21) + m23;
        outW = (x * m30) + (y * m31) + m33;
    }

    inline void Transform2Normal(
        const float x,
        const float y,
        float& outX,
        float& outY
    )const noexcept{
        outX = (x * m00) + (y * m01);
        outY = (x * m10) + (y * m11);
    }

    inline void Transform3(
        const float x,
        const float y,
        const float z,
        float& outX,
        float& outY,
        float& outZ,
        float& outW
    )const noexcept{
        outX = (x * m00) + (y * m01) + (z * m02) + m03;
        outY = (x * m10) + (y * m11) + (z * m12) + m13;
        outZ = (x * m20) + (y * m21) + (z * m22) + m23;
        outW = (x * m30) + (y * m31) + (z * m32) + m33;
    }

    inline void Transform4(
        const float x,
        const float y,
        const float z,
        const float w,
        float& outX,
        float& outY,
        float& outZ,
        float& outW
    )const noexcept{
        outX = (x * m00) + (y * m01) + (z * m02) + (w * m03);
        outY = (x * m10) + (y * m11) + (z * m12) + (w * m13);
        outZ = (x * m20) + (y * m21) + (z * m22) + (w * m23);
        outW = (x * m30) + (y * m31) + (z * m32) + (w * m33);
    }

    inline void Transform3Normal(
        const float x,
        const float y,
        const float z,
        float& outX,
        float& outY,
        float& outZ
    )const noexcept{
        outX = (x * m00) + (y * m01) + (z * m02);
        outY = (x * m10) + (y * m11) + (z * m12);
        outZ = (x * m20) + (y * m21) + (z * m22);
    }
};


struct ProjectViewportTransform{
    float scaleX;
    float scaleY;
    float scaleZ;
    float offsetX;
    float offsetY;
    float offsetZ;

    ProjectViewportTransform(
        const float viewportX,
        const float viewportY,
        const float viewportWidth,
        const float viewportHeight,
        const float viewportMinZ,
        const float viewportMaxZ
    )noexcept
        : scaleX(viewportWidth * 0.5f),
          scaleY(-viewportHeight * 0.5f),
          scaleZ(viewportMaxZ - viewportMinZ),
          offsetX(viewportX + (viewportWidth * 0.5f)),
          offsetY(viewportY + (viewportHeight * 0.5f)),
          offsetZ(viewportMinZ){}

    inline void Apply(
        const float transformedX,
        const float transformedY,
        const float transformedZ,
        const float reciprocalW,
        float& outX,
        float& outY,
        float& outZ
    )const noexcept{
        outX = (transformedX * reciprocalW * scaleX) + offsetX;
        outY = (transformedY * reciprocalW * scaleY) + offsetY;
        outZ = (transformedZ * reciprocalW * scaleZ) + offsetZ;
    }
};


struct UnprojectViewportTransform{
    float scaleX;
    float scaleY;
    float scaleZ;
    float offsetX;
    float offsetY;
    float offsetZ;

    UnprojectViewportTransform(
        const float viewportX,
        const float viewportY,
        const float viewportWidth,
        const float viewportHeight,
        const float viewportMinZ,
        const float viewportMaxZ
    )noexcept
        : scaleX(2.0f / viewportWidth),
          scaleY(-2.0f / viewportHeight),
          scaleZ(1.0f / (viewportMaxZ - viewportMinZ)),
          offsetX(((-2.0f / viewportWidth) * viewportX) - 1.0f),
          offsetY(((2.0f / viewportHeight) * viewportY) + 1.0f),
          offsetZ((-1.0f / (viewportMaxZ - viewportMinZ)) * viewportMinZ){}

    inline void Apply(
        const float inputX,
        const float inputY,
        const float inputZ,
        float& outX,
        float& outY,
        float& outZ
    )const noexcept{
        outX = (inputX * scaleX) + offsetX;
        outY = (inputY * scaleY) + offsetY;
        outZ = (inputZ * scaleZ) + offsetZ;
    }
};

}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
