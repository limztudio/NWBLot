// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../bit.h"
#include "convert.h"
#include "constant_double.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SIMDConvertDoubleDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL MakeF64(f64 x, f64 y, f64 z, f64 w)noexcept{
    SIMDVectorDouble result{};
    result[0] = x;
    result[1] = y;
    result[2] = z;
    result[3] = w;
    return result;
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL MakeU64(u64 x, u64 y, u64 z, u64 w)noexcept{
    SIMDVectorDouble result{};
    result[0] = BitCast<f64>(x);
    result[1] = BitCast<f64>(y);
    result[2] = BitCast<f64>(z);
    result[3] = BitCast<f64>(w);
    return result;
}


template<typename secondRow>
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDouble3Components(const secondRow& src)noexcept{
    return MakeF64(src.x, src.y, src.z, 0.0);
}

template<typename secondRow>
NWB_INLINE void NWB_SIMD_CALL StoreDouble3Components(SIMDVectorDouble src, secondRow& dst)noexcept{
    dst.x = src[0];
    dst.y = src[1];
    dst.z = src[2];
}

template<typename secondRow>
NWB_INLINE void StoreDouble34Scalar(SIMDMatrixDouble src, secondRow& dst)noexcept{
    dst._11 = src.v[0][0];
    dst._12 = src.v[0][1];
    dst._13 = src.v[0][2];
    dst._14 = src.v[0][3];

    dst._21 = src.v[1][0];
    dst._22 = src.v[1][1];
    dst._23 = src.v[1][2];
    dst._24 = src.v[1][3];

    dst._31 = src.v[2][0];
    dst._32 = src.v[2][1];
    dst._33 = src.v[2][2];
    dst._34 = src.v[2][3];
}

template<typename secondRow>
NWB_INLINE void StoreDouble44Scalar(SIMDMatrixDouble src, secondRow& dst)noexcept{
    StoreDouble34Scalar(src, dst);

    dst._41 = src.v[3][0];
    dst._42 = src.v[3][1];
    dst._43 = src.v[3][2];
    dst._44 = src.v[3][3];
}

template<typename secondRow>
NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL LoadDouble34Scalar(const secondRow& src)noexcept{
    SIMDMatrixDouble result{};
    result.v[0] = MakeF64(src._11, src._12, src._13, src._14);
    result.v[1] = MakeF64(src._21, src._22, src._23, src._24);
    result.v[2] = MakeF64(src._31, src._32, src._33, src._34);
    result.v[3] = s_DoubleSIMDIdentityR3;
    return result;
}

template<typename secondRow>
NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL LoadDouble44Scalar(const secondRow& src)noexcept{
    SIMDMatrixDouble result = LoadDouble34Scalar(src);
    result.v[3] = MakeF64(src._41, src._42, src._43, src._44);
    return result;
}


template<typename secondRow>
NWB_INLINE void NWB_SIMD_CALL StoreInt4Scalar(SIMDVectorDouble src, secondRow& dst)noexcept{
    dst.x = static_cast<i64>(BitCast<u64>(src[0]));
    dst.y = static_cast<i64>(BitCast<u64>(src[1]));
    dst.z = static_cast<i64>(BitCast<u64>(src[2]));
    dst.w = static_cast<i64>(BitCast<u64>(src[3]));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDouble(const Half2U& src)noexcept{
    return SIMDVectorDouble{ ConvertHalfToFloat(src.x), ConvertHalfToFloat(src.y), 0.0, 0.0 };
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDouble(const Half4U& src)noexcept{
    return SIMDVectorDouble{ ConvertHalfToFloat(src.x), ConvertHalfToFloat(src.y), ConvertHalfToFloat(src.z), ConvertHalfToFloat(src.w) };
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDouble(const Double4& src)noexcept{
    return SIMDConvertDoubleDetail::MakeF64(src.x, src.y, src.z, src.w);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDouble(f64 src)noexcept{
    return SIMDConvertDoubleDetail::MakeF64(src, 0.0, 0.0, 0.0);
}
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDouble(const Double2U& src)noexcept{
    return SIMDConvertDoubleDetail::MakeF64(src.x, src.y, 0.0, 0.0);
}
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDouble(const Double3U& src)noexcept{
    return SIMDConvertDoubleDetail::LoadDouble3Components(src);
}
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDoubleInt(const Double3Int& src)noexcept{
    return SIMDConvertDoubleDetail::LoadDouble3Components(src);
}
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDoubleInt(const Double3UInt& src)noexcept{
    return SIMDConvertDoubleDetail::LoadDouble3Components(src);
}
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDouble(const Double4U& src)noexcept{
    return SIMDConvertDoubleDetail::MakeF64(src.x, src.y, src.z, src.w);
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL LoadDouble(const Double34& src)noexcept{
    return SIMDConvertDoubleDetail::LoadDouble34Scalar(src);
}
NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL LoadDouble(const Double44& src)noexcept{
    return SIMDConvertDoubleDetail::LoadDouble44Scalar(src);
}

NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL LoadDouble(const Double33U& src)noexcept{
    SIMDMatrixDouble result{};
    result.v[0] = SIMDConvertDoubleDetail::MakeF64(src._11, src._12, src._13, 0.0);
    result.v[1] = SIMDConvertDoubleDetail::MakeF64(src._21, src._22, src._23, 0.0);
    result.v[2] = SIMDConvertDoubleDetail::MakeF64(src._31, src._32, src._33, 0.0);
    result.v[3] = s_DoubleSIMDIdentityR3;
    return result;
}
NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL LoadDouble(const Double34U& src)noexcept{
    return SIMDConvertDoubleDetail::LoadDouble34Scalar(src);
}
NWB_INLINE SIMDMatrixDouble NWB_SIMD_CALL LoadDouble(const Double44U& src)noexcept{
    return SIMDConvertDoubleDetail::LoadDouble44Scalar(src);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDoubleInt(const Int64x4& src)noexcept{
    return SIMDConvertDoubleDetail::MakeU64(static_cast<u64>(src.x), static_cast<u64>(src.y), static_cast<u64>(src.z), static_cast<u64>(src.w));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDoubleInt(i64 src)noexcept{
    return SIMDConvertDoubleDetail::MakeU64(static_cast<u64>(src), 0, 0, 0);
}
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDoubleInt(const Int64x2U& src)noexcept{
    return SIMDConvertDoubleDetail::MakeU64(static_cast<u64>(src.x), static_cast<u64>(src.y), 0, 0);
}
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDoubleInt(const Int64x3U& src)noexcept{
    return SIMDConvertDoubleDetail::MakeU64(static_cast<u64>(src.x), static_cast<u64>(src.y), static_cast<u64>(src.z), 0);
}
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDoubleInt(const Int64x4U& src)noexcept{
    return SIMDConvertDoubleDetail::MakeU64(static_cast<u64>(src.x), static_cast<u64>(src.y), static_cast<u64>(src.z), static_cast<u64>(src.w));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDoubleInt(const UInt64x4& src)noexcept{
    return SIMDConvertDoubleDetail::MakeU64(src.x, src.y, src.z, src.w);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDoubleInt(u64 src)noexcept{
    return SIMDConvertDoubleDetail::MakeU64(src, 0, 0, 0);
}
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDoubleInt(const UInt64x2U& src)noexcept{
    return SIMDConvertDoubleDetail::MakeU64(src.x, src.y, 0, 0);
}
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDoubleInt(const UInt64x3U& src)noexcept{
    return SIMDConvertDoubleDetail::MakeU64(src.x, src.y, src.z, 0);
}
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL LoadDoubleInt(const UInt64x4U& src)noexcept{
    return SIMDConvertDoubleDetail::MakeU64(src.x, src.y, src.z, src.w);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE void StreamDoubleFence()noexcept{
    StreamFloatFence();
}


NWB_INLINE void NWB_SIMD_CALL StoreDouble(SIMDVectorDouble src, Double4& dst)noexcept{
    dst.x = src[0];
    dst.y = src[1];
    dst.z = src[2];
    dst.w = src[3];
}

NWB_INLINE void NWB_SIMD_CALL StreamDouble(SIMDVectorDouble src, Double4& dst)noexcept{
    __builtin_nontemporal_store(src, reinterpret_cast<SIMDVectorDouble*>(&dst));
}

NWB_INLINE void NWB_SIMD_CALL StoreDouble(SIMDVectorDouble src, f64& dst)noexcept{
    dst = src[0];
}
NWB_INLINE void NWB_SIMD_CALL StoreDouble(SIMDVectorDouble src, Double2U& dst)noexcept{
    dst.x = src[0];
    dst.y = src[1];
}
NWB_INLINE void NWB_SIMD_CALL StoreDouble(SIMDVectorDouble src, Double3U& dst)noexcept{
    SIMDConvertDoubleDetail::StoreDouble3Components(src, dst);
}
NWB_INLINE void NWB_SIMD_CALL StoreDoubleInt(SIMDVectorDouble src, i64 w, Double3Int& dst)noexcept{
    SIMDConvertDoubleDetail::StoreDouble3Components(src, dst);
    dst.w = w;
}
NWB_INLINE void NWB_SIMD_CALL StoreDoubleInt(SIMDVectorDouble src, u64 w, Double3UInt& dst)noexcept{
    SIMDConvertDoubleDetail::StoreDouble3Components(src, dst);
    dst.w = w;
}
NWB_INLINE void NWB_SIMD_CALL StoreDouble(SIMDVectorDouble src, Double4U& dst)noexcept{
    dst.x = src[0];
    dst.y = src[1];
    dst.z = src[2];
    dst.w = src[3];
}

NWB_INLINE void NWB_SIMD_CALL StoreDouble(SIMDMatrixDouble src, Double34& dst)noexcept{
    SIMDConvertDoubleDetail::StoreDouble34Scalar(src, dst);
}

NWB_INLINE void NWB_SIMD_CALL StreamDouble(SIMDMatrixDouble src, Double34& dst)noexcept{
    for(usize row = 0u; row < 3u; ++row)
        __builtin_nontemporal_store(src.v[row], reinterpret_cast<SIMDVectorDouble*>(&dst.rows[row]));
}

NWB_INLINE void NWB_SIMD_CALL StoreDouble(SIMDMatrixDouble src, Double44& dst)noexcept{
    SIMDConvertDoubleDetail::StoreDouble44Scalar(src, dst);
}

NWB_INLINE void NWB_SIMD_CALL StreamDouble(SIMDMatrixDouble src, Double44& dst)noexcept{
    for(usize row = 0u; row < 4u; ++row)
        __builtin_nontemporal_store(src.v[row], reinterpret_cast<SIMDVectorDouble*>(&dst.rows[row]));
}

NWB_INLINE void NWB_SIMD_CALL StoreDouble(SIMDMatrixDouble src, Double33U& dst)noexcept{
    dst._11 = src.v[0][0];
    dst._12 = src.v[0][1];
    dst._13 = src.v[0][2];

    dst._21 = src.v[1][0];
    dst._22 = src.v[1][1];
    dst._23 = src.v[1][2];

    dst._31 = src.v[2][0];
    dst._32 = src.v[2][1];
    dst._33 = src.v[2][2];
}
NWB_INLINE void NWB_SIMD_CALL StoreDouble(SIMDMatrixDouble src, Double34U& dst)noexcept{
    SIMDConvertDoubleDetail::StoreDouble34Scalar(src, dst);
}
NWB_INLINE void NWB_SIMD_CALL StoreDouble(SIMDMatrixDouble src, Double44U& dst)noexcept{
    SIMDConvertDoubleDetail::StoreDouble44Scalar(src, dst);
}

NWB_INLINE void NWB_SIMD_CALL StoreDoubleInt(SIMDVectorDouble src, Int64x4& dst)noexcept{
    SIMDConvertDoubleDetail::StoreInt4Scalar(src, dst);
}

NWB_INLINE void NWB_SIMD_CALL StoreDoubleInt(SIMDVectorDouble src, i64& dst)noexcept{
    dst = static_cast<i64>(BitCast<u64>(src[0]));
}
NWB_INLINE void NWB_SIMD_CALL StoreDoubleInt(SIMDVectorDouble src, Int64x2U& dst)noexcept{
    dst.x = static_cast<i64>(BitCast<u64>(src[0]));
    dst.y = static_cast<i64>(BitCast<u64>(src[1]));
}
NWB_INLINE void NWB_SIMD_CALL StoreDoubleInt(SIMDVectorDouble src, Int64x3U& dst)noexcept{
    dst.x = static_cast<i64>(BitCast<u64>(src[0]));
    dst.y = static_cast<i64>(BitCast<u64>(src[1]));
    dst.z = static_cast<i64>(BitCast<u64>(src[2]));
}
NWB_INLINE void NWB_SIMD_CALL StoreDoubleInt(SIMDVectorDouble src, Int64x4U& dst)noexcept{
    SIMDConvertDoubleDetail::StoreInt4Scalar(src, dst);
}

NWB_INLINE void NWB_SIMD_CALL StoreDoubleInt(SIMDVectorDouble src, UInt64x4& dst)noexcept{
    dst.x = BitCast<u64>(src[0]);
    dst.y = BitCast<u64>(src[1]);
    dst.z = BitCast<u64>(src[2]);
    dst.w = BitCast<u64>(src[3]);
}

NWB_INLINE void NWB_SIMD_CALL StoreDoubleInt(SIMDVectorDouble src, u64& dst)noexcept{
    dst = BitCast<u64>(src[0]);
}
NWB_INLINE void NWB_SIMD_CALL StoreDoubleInt(SIMDVectorDouble src, UInt64x2U& dst)noexcept{
    dst.x = BitCast<u64>(src[0]);
    dst.y = BitCast<u64>(src[1]);
}
NWB_INLINE void NWB_SIMD_CALL StoreDoubleInt(SIMDVectorDouble src, UInt64x3U& dst)noexcept{
    dst.x = BitCast<u64>(src[0]);
    dst.y = BitCast<u64>(src[1]);
    dst.z = BitCast<u64>(src[2]);
}
NWB_INLINE void NWB_SIMD_CALL StoreDoubleInt(SIMDVectorDouble src, UInt64x4U& dst)noexcept{
    dst.x = BitCast<u64>(src[0]);
    dst.y = BitCast<u64>(src[1]);
    dst.z = BitCast<u64>(src[2]);
    dst.w = BitCast<u64>(src[3]);
}


NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL ConvertVectorSingleToDouble(SIMDVectorSingle value)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDVectorDouble{ value.f[0], value.f[1], value.f[2], value.f[3] };
#else
    return __builtin_convertvector(value, SIMDVectorDouble);
#endif
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL ConvertVectorDoubleToSingle(SIMDVectorDouble value)noexcept{
#if defined(NWB_HAS_SCALAR)
    return SIMDConvertDetail::MakeF32(static_cast<f32>(value[0]), static_cast<f32>(value[1]), static_cast<f32>(value[2]), static_cast<f32>(value[3]));
#else
    return __builtin_convertvector(value, SIMDVectorSingle);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

