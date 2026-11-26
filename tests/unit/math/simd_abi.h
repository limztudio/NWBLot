// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>


#include <global/simdmath.h>
#include <tests/common/gtest_sdk.h>

#if defined(FOUNDATION_PLATFORM_LINUX) && defined(FOUNDATION_HAS_AVX2)
#include <csignal>
#include <fenv.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::MathCases{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<auto Function, typename... Args>
FOUNDATION_NOINLINE decltype(auto) InvokeOutOfLine(const Args&... args)noexcept(noexcept(Function(args...))){
#pragma STDC FENV_ACCESS ON
    return Function(args...);
}

template<auto Compare, typename Vector>
FOUNDATION_NOINLINE b8 CompareRegisters(const Vector left, const Vector right)noexcept{
#pragma STDC FENV_ACCESS ON
    return Compare(left, right);
}

template<typename Compare>
FOUNDATION_NOINLINE b8 RunDoubleComparison(Compare compare, const SIMDVectorDouble left, const SIMDVectorDouble right)noexcept{
#pragma STDC FENV_ACCESS ON
    return compare(left, right);
}

template<typename Frustum, typename Point, usize Count>
void ReadFrustumCorners(const Frustum& frustum, Array<Point, Count>& output)noexcept{
    Point corners[Count];
    frustum.getCorners(corners);
    for(usize i = 0u; i < Count; ++i)
        output[i] = corners[i];
}

inline void ReadFrustumPlanes(const BoundingFrustum& frustum, Array<Float4, CollisionDetail::s_FrustumPlaneCount>& output)noexcept{
    SIMDVectorSingle planeNear{};
    SIMDVectorSingle planeFar{};
    SIMDVectorSingle planeRight{};
    SIMDVectorSingle planeLeft{};
    SIMDVectorSingle planeTop{};
    SIMDVectorSingle planeBottom{};
    frustum.getPlanes(&planeNear, &planeFar, &planeRight, &planeLeft, &planeTop, &planeBottom);
    StoreFloat(planeNear, output[CollisionDetail::FrustumPlaneIndex::Near]);
    StoreFloat(planeFar, output[CollisionDetail::FrustumPlaneIndex::Far]);
    StoreFloat(planeRight, output[CollisionDetail::FrustumPlaneIndex::Right]);
    StoreFloat(planeLeft, output[CollisionDetail::FrustumPlaneIndex::Left]);
    StoreFloat(planeTop, output[CollisionDetail::FrustumPlaneIndex::Top]);
    StoreFloat(planeBottom, output[CollisionDetail::FrustumPlaneIndex::Bottom]);
}

inline SIMDVectorDoubleConstF MakeDoubleConstant(const f64 x, const f64 y, const f64 z, const f64 w)noexcept{
    return { { { x, y, z, w } } };
}

inline Double4 DoubleConstantComponents(const SIMDVectorDoubleConstF& constant)noexcept{ return Double4(constant.f); }

inline SIMDVectorDouble DoubleConstantVector(const SIMDVectorDoubleConstF& constant)noexcept{ return constant; }

inline SIMDMatrixDouble InvertWithDeterminant(SIMDVectorDouble& determinant, const SIMDMatrixDouble& matrix)noexcept{
    return MatrixDoubleInverse(&determinant, matrix);
}

inline SIMDVectorDouble& DoubleMatrixRow(SIMDMatrixDouble& matrix, const usize row)noexcept{ return matrix.v[row]; }

template<usize Count>
usize ConvertFloatsToHalves(Array<u16, Count>& packed, const Array<f32, Count>& source)noexcept{
    return reinterpret_cast<usize>(ConvertFloatBufferToHalf(MakeNotNull(packed.data()), MakeNotNull(source.data()), Count).get());
}

template<usize Count>
usize ConvertHalvesToFloats(Array<f32, Count>& values, const Array<u16, Count>& packed)noexcept{
    return reinterpret_cast<usize>(ConvertHalfBufferToFloat(MakeNotNull(values.data()), MakeNotNull(packed.data()), Count).get());
}

template<typename Handler>
[[nodiscard]] b8 InstallFloatingTrap([[maybe_unused]] Handler handler)noexcept{
#if defined(FOUNDATION_PLATFORM_LINUX) && defined(FOUNDATION_HAS_AVX2)
    struct sigaction action{};
    action.sa_sigaction = [](int, siginfo_t* info, void*)noexcept{ Handler{}(info && info->si_code == FPE_FLTINV); };
    action.sa_flags = SA_SIGINFO;
    return sigemptyset(&action.sa_mask) == 0 && sigaction(SIGFPE, &action, nullptr) == 0;
#else
    return false;
#endif
}

[[nodiscard]] inline b8 EnableFloatingTraps()noexcept{
#if defined(FOUNDATION_PLATFORM_LINUX) && defined(FOUNDATION_HAS_AVX2)
    return feenableexcept(FE_INVALID | FE_OVERFLOW) != -1;
#else
    return false;
#endif
}

[[nodiscard]] FOUNDATION_INLINE u32 BaselineLaneMask(const SIMDVectorDouble comparison)noexcept{
#if defined(FOUNDATION_HAS_NEON64)
    const uint64x2_t low = vshrq_n_u64(SIMDVectorDoubleDetail::GetLowBits(comparison), 63u);
    const uint64x2_t high = vshrq_n_u64(SIMDVectorDoubleDetail::GetHighBits(comparison), 63u);
    return static_cast<u32>(vgetq_lane_u64(low, 0) | (vgetq_lane_u64(low, 1) << 1u)
        | (vgetq_lane_u64(high, 0) << 2u) | (vgetq_lane_u64(high, 1) << 3u));
#else
    return VectorDoubleMoveMask(comparison);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
NWB_END


#if defined(FOUNDATION_PLATFORM_LINUX) && defined(FOUNDATION_HAS_AVX2)
#define NWB_GTEST_LINUX_AVX2_CASE(Suite, Case, Fn) NWB_GTEST_CASE(Suite, Case, Fn)
#else
#define NWB_GTEST_LINUX_AVX2_CASE(Suite, Case, Fn)
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

