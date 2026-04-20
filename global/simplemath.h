// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "compile.h"
#include "type.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
[[nodiscard]] constexpr const T& Min(const T& a, const T& b){ return (a < b) ? a : b; }
template<typename T, typename Compare>
[[nodiscard]] constexpr const T& Min(const T& a, const T& b, Compare comp){ return comp(a, b) ? a : b; }
template<typename T>
[[nodiscard]] constexpr T Min(std::initializer_list<T> list){ return *std::min_element(list.begin(), list.end()); }
template<typename T, typename Compare>
[[nodiscard]] constexpr T Min(std::initializer_list<T> list, Compare comp){
    return *std::min_element(list.begin(), list.end(), comp);
}

template<typename T>
[[nodiscard]] constexpr const T& Max(const T& a, const T& b){ return (a > b) ? a : b; }
template<typename T, typename Compare>
[[nodiscard]] constexpr const T& Max(const T& a, const T& b, Compare comp){ return comp(a, b) ? b : a; }
template<typename T>
[[nodiscard]] constexpr T Max(std::initializer_list<T> list){ return *std::max_element(list.begin(), list.end()); }
template<typename T, typename Compare>
[[nodiscard]] constexpr T Max(std::initializer_list<T> list, Compare comp){
    return *std::max_element(list.begin(), list.end(), comp);
}

template<typename T>
[[nodiscard]] constexpr NWB_INLINE T FloorLog2(const T x){ return (x == 1) ? 0 : (1 + FloorLog2<T>(x >> 1)); }
template<typename T>
[[nodiscard]] constexpr NWB_INLINE T CeilLog2(const T x){ return (x == 1) ? 0 : (FloorLog2<T>(x - 1) + 1); }

template<typename T>
[[nodiscard]] NWB_INLINE T Floor(const T value){
    using std::floor;
    return static_cast<T>(floor(value));
}

template<typename T>
[[nodiscard]] NWB_INLINE T Ceil(const T value){
    using std::ceil;
    return static_cast<T>(ceil(value));
}

template<typename T>
[[nodiscard]] NWB_INLINE T Sqrt(const T value){
    using std::sqrt;
    return static_cast<T>(sqrt(value));
}

template<typename T>
[[nodiscard]] NWB_INLINE T Sin(const T value){
    using std::sin;
    return static_cast<T>(sin(value));
}

template<typename T>
[[nodiscard]] NWB_INLINE T Cos(const T value){
    using std::cos;
    return static_cast<T>(cos(value));
}

template<typename T>
[[nodiscard]] NWB_INLINE bool IsFinite(const T value){
    using std::isfinite;
    return isfinite(value);
}

template<typename T>
[[nodiscard]] constexpr bool AddNoOverflow(const T lhs, const T rhs, T& outResult){
    if(lhs > ((std::numeric_limits<T>::max)() - rhs))
        return false;

    outResult = lhs + rhs;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct Float2Data{
    f32 x = 0.0f;
    f32 y = 0.0f;

    constexpr Float2Data()noexcept = default;
    constexpr Float2Data(const f32 _x, const f32 _y)noexcept
        : x(_x), y(_y)
    {
    }
    explicit Float2Data(const f32* pArray)noexcept
        : x(pArray[0]), y(pArray[1])
    {
    }
};

struct alignas(16) AlignedFloat2Data : Float2Data{
    constexpr AlignedFloat2Data()noexcept = default;
    using Float2Data::Float2Data;
};

struct Float3Data{
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;

    constexpr Float3Data()noexcept = default;
    constexpr Float3Data(const f32 _x, const f32 _y, const f32 _z)noexcept
        : x(_x), y(_y), z(_z)
    {
    }
    explicit Float3Data(const f32* pArray)noexcept
        : x(pArray[0]), y(pArray[1]), z(pArray[2])
    {
    }
};

struct alignas(16) AlignedFloat3Data : Float3Data{
    constexpr AlignedFloat3Data()noexcept = default;
    using Float3Data::Float3Data;
};

struct Float4Data{
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;
    f32 w = 0.0f;

    constexpr Float4Data()noexcept = default;
    constexpr Float4Data(const f32 _x, const f32 _y, const f32 _z, const f32 _w)noexcept
        : x(_x), y(_y), z(_z), w(_w)
    {
    }
    explicit Float4Data(const f32* pArray)noexcept
        : x(pArray[0]), y(pArray[1]), z(pArray[2]), w(pArray[3])
    {
    }
};

struct alignas(16) AlignedFloat4Data : Float4Data{
    constexpr AlignedFloat4Data()noexcept = default;
    using Float4Data::Float4Data;
};

static_assert(sizeof(Float2Data) == sizeof(f32) * 2u, "Float2Data layout drifted");
static_assert(sizeof(Float3Data) == sizeof(f32) * 3u, "Float3Data layout drifted");
static_assert(sizeof(Float4Data) == sizeof(f32) * 4u, "Float4Data layout drifted");
static_assert(sizeof(AlignedFloat2Data) == sizeof(f32) * 4u, "AlignedFloat2Data layout drifted");
static_assert(sizeof(AlignedFloat3Data) == sizeof(f32) * 4u, "AlignedFloat3Data layout drifted");
static_assert(sizeof(AlignedFloat4Data) == sizeof(f32) * 4u, "AlignedFloat4Data layout drifted");
static_assert(alignof(AlignedFloat2Data) >= 16u, "AlignedFloat2Data alignment drifted");
static_assert(alignof(AlignedFloat3Data) >= 16u, "AlignedFloat3Data alignment drifted");
static_assert(alignof(AlignedFloat4Data) >= 16u, "AlignedFloat4Data alignment drifted");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SimpleMath{


inline constexpr f32 Pi = 3.141592654f;


[[nodiscard]] constexpr f32 ConvertToRadians(const f32 degrees)noexcept{
    return degrees * (Pi / 180.0f);
}

[[nodiscard]] constexpr f32 ConvertToDegrees(const f32 radians)noexcept{
    return radians * (180.0f / Pi);
}

[[nodiscard]] constexpr AlignedFloat4Data QuaternionIdentity()noexcept{
    return AlignedFloat4Data(0.0f, 0.0f, 0.0f, 1.0f);
}

[[nodiscard]] NWB_INLINE AlignedFloat4Data QuaternionMultiply(
    const Float4Data& lhs,
    const Float4Data& rhs
)noexcept{
    return AlignedFloat4Data(
        lhs.w * rhs.x + lhs.x * rhs.w + lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.w * rhs.y - lhs.x * rhs.z + lhs.y * rhs.w + lhs.z * rhs.x,
        lhs.w * rhs.z + lhs.x * rhs.y - lhs.y * rhs.x + lhs.z * rhs.w,
        lhs.w * rhs.w - lhs.x * rhs.x - lhs.y * rhs.y - lhs.z * rhs.z
    );
}

[[nodiscard]] NWB_INLINE AlignedFloat4Data QuaternionRotationAxis(
    const f32 axisX,
    const f32 axisY,
    const f32 axisZ,
    const f32 angle
)noexcept{
    const f32 halfAngle = angle * 0.5f;
    const f32 sinHalfAngle = Sin(halfAngle);
    const f32 cosHalfAngle = Cos(halfAngle);
    return AlignedFloat4Data(axisX * sinHalfAngle, axisY * sinHalfAngle, axisZ * sinHalfAngle, cosHalfAngle);
}

[[nodiscard]] NWB_INLINE AlignedFloat4Data QuaternionRotationRollPitchYaw(
    const f32 pitch,
    const f32 yaw,
    const f32 roll
)noexcept{
    const AlignedFloat4Data qYaw = QuaternionRotationAxis(0.0f, 1.0f, 0.0f, yaw);
    const AlignedFloat4Data qPitch = QuaternionRotationAxis(1.0f, 0.0f, 0.0f, pitch);
    const AlignedFloat4Data qRoll = QuaternionRotationAxis(0.0f, 0.0f, 1.0f, roll);
    return QuaternionMultiply(QuaternionMultiply(qYaw, qPitch), qRoll);
}

[[nodiscard]] NWB_INLINE AlignedFloat3Data Vector3Rotate(
    const Float3Data& value,
    const Float4Data& rotation
)noexcept{
    const Float3Data q(rotation.x, rotation.y, rotation.z);
    const Float3Data twiceCross(
        2.0f * ((q.y * value.z) - (q.z * value.y)),
        2.0f * ((q.z * value.x) - (q.x * value.z)),
        2.0f * ((q.x * value.y) - (q.y * value.x))
    );
    return AlignedFloat3Data(
        value.x + (rotation.w * twiceCross.x) + ((q.y * twiceCross.z) - (q.z * twiceCross.y)),
        value.y + (rotation.w * twiceCross.y) + ((q.z * twiceCross.x) - (q.x * twiceCross.z)),
        value.z + (rotation.w * twiceCross.z) + ((q.x * twiceCross.y) - (q.y * twiceCross.x))
    );
}


}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

