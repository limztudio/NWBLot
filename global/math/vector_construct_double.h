// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector_matrix_double.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE u32 NWB_SIMD_CALL VectorDoubleMoveMask(SIMDVectorDouble value)noexcept{
    const SIMDVectorDoubleDetail::UIntVector bits = BitCast<SIMDVectorDoubleDetail::UIntVector>(value) >> 63u;
    return static_cast<u32>(bits[0] | (bits[1] << 1u) | (bits[2] << 2u) | (bits[3] << 3u));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleZero()noexcept{
    return SIMDVectorDouble{};
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSet(f64 x, f64 y, f64 z, f64 w)noexcept{
    return SIMDVectorDouble{ x, y, z, w };
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetInt(u64 x, u64 y, u64 z, u64 w)noexcept{
    return BitCast<SIMDVectorDouble>(SIMDVectorDoubleDetail::UIntVector{ x, y, z, w });
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleReplicate(f64 value)noexcept{
    return VectorDoubleSet(value, value, value, value);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleReplicatePtr(const f64 value)noexcept{
    return VectorDoubleReplicate(value);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleReplicateInt(u64 value)noexcept{
    return SIMDConvertDoubleDetail::MakeU64(value, value, value, value);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleReplicateIntPtr(const u64 value)noexcept{
    return VectorDoubleReplicateInt(value);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleTrueInt()noexcept{
    return SIMDConvertDoubleDetail::MakeU64(0xFFFFFFFFFFFFFFFFu, 0xFFFFFFFFFFFFFFFFu, 0xFFFFFFFFFFFFFFFFu, 0xFFFFFFFFFFFFFFFFu);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleFalseInt()noexcept{
    return SIMDConvertDoubleDetail::MakeU64(0u, 0u, 0u, 0u);
}

NWB_INLINE f64 NWB_SIMD_CALL VectorDoubleGetX(SIMDVectorDouble value)noexcept{ return SIMDVectorDoubleDetail::GetLane<0u>(value); }
NWB_INLINE f64 NWB_SIMD_CALL VectorDoubleGetY(SIMDVectorDouble value)noexcept{ return SIMDVectorDoubleDetail::GetLane<1u>(value); }
NWB_INLINE f64 NWB_SIMD_CALL VectorDoubleGetZ(SIMDVectorDouble value)noexcept{ return SIMDVectorDoubleDetail::GetLane<2u>(value); }
NWB_INLINE f64 NWB_SIMD_CALL VectorDoubleGetW(SIMDVectorDouble value)noexcept{ return SIMDVectorDoubleDetail::GetLane<3u>(value); }

NWB_INLINE f64 NWB_SIMD_CALL VectorDoubleGetByIndex(SIMDVectorDouble value, usize index)noexcept{
    NWB_ASSERT(index < 4);
    switch(index){
    case 0u: return VectorDoubleGetX(value);
    case 1u: return VectorDoubleGetY(value);
    case 2u: return VectorDoubleGetZ(value);
    default: return VectorDoubleGetW(value);
    }
}

NWB_INLINE void NWB_SIMD_CALL VectorDoubleGetByIndexPtr(f64& out, SIMDVectorDouble value, usize index)noexcept{
    NWB_ASSERT(index < 4);
    out = VectorDoubleGetByIndex(value, index);
}

NWB_INLINE void NWB_SIMD_CALL VectorDoubleGetXPtr(f64& out, SIMDVectorDouble value)noexcept{ out = VectorDoubleGetX(value); }
NWB_INLINE void NWB_SIMD_CALL VectorDoubleGetYPtr(f64& out, SIMDVectorDouble value)noexcept{ out = VectorDoubleGetY(value); }
NWB_INLINE void NWB_SIMD_CALL VectorDoubleGetZPtr(f64& out, SIMDVectorDouble value)noexcept{ out = VectorDoubleGetZ(value); }
NWB_INLINE void NWB_SIMD_CALL VectorDoubleGetWPtr(f64& out, SIMDVectorDouble value)noexcept{ out = VectorDoubleGetW(value); }

NWB_INLINE u64 NWB_SIMD_CALL VectorDoubleGetIntX(SIMDVectorDouble value)noexcept{ return SIMDVectorDoubleDetail::GetIntLane<0u>(value); }
NWB_INLINE u64 NWB_SIMD_CALL VectorDoubleGetIntY(SIMDVectorDouble value)noexcept{ return SIMDVectorDoubleDetail::GetIntLane<1u>(value); }
NWB_INLINE u64 NWB_SIMD_CALL VectorDoubleGetIntZ(SIMDVectorDouble value)noexcept{ return SIMDVectorDoubleDetail::GetIntLane<2u>(value); }
NWB_INLINE u64 NWB_SIMD_CALL VectorDoubleGetIntW(SIMDVectorDouble value)noexcept{ return SIMDVectorDoubleDetail::GetIntLane<3u>(value); }

NWB_INLINE u64 NWB_SIMD_CALL VectorDoubleGetIntByIndex(SIMDVectorDouble value, usize index)noexcept{
    NWB_ASSERT(index < 4);
    switch(index){
    case 0u: return VectorDoubleGetIntX(value);
    case 1u: return VectorDoubleGetIntY(value);
    case 2u: return VectorDoubleGetIntZ(value);
    default: return VectorDoubleGetIntW(value);
    }
}

NWB_INLINE void NWB_SIMD_CALL VectorDoubleGetIntByIndexPtr(u64& out, SIMDVectorDouble value, usize index)noexcept{
    NWB_ASSERT(index < 4);
    out = VectorDoubleGetIntByIndex(value, index);
}

NWB_INLINE void NWB_SIMD_CALL VectorDoubleGetIntXPtr(u64& out, SIMDVectorDouble value)noexcept{ out = VectorDoubleGetIntX(value); }
NWB_INLINE void NWB_SIMD_CALL VectorDoubleGetIntYPtr(u64& out, SIMDVectorDouble value)noexcept{ out = VectorDoubleGetIntY(value); }
NWB_INLINE void NWB_SIMD_CALL VectorDoubleGetIntZPtr(u64& out, SIMDVectorDouble value)noexcept{ out = VectorDoubleGetIntZ(value); }
NWB_INLINE void NWB_SIMD_CALL VectorDoubleGetIntWPtr(u64& out, SIMDVectorDouble value)noexcept{ out = VectorDoubleGetIntW(value); }

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetX(SIMDVectorDouble value, f64 x)noexcept{
    value[0] = x;
    return value;
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetY(SIMDVectorDouble value, f64 y)noexcept{
    value[1] = y;
    return value;
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetZ(SIMDVectorDouble value, f64 z)noexcept{
    value[2] = z;
    return value;
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetW(SIMDVectorDouble value, f64 w)noexcept{
    value[3] = w;
    return value;
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetByIndex(SIMDVectorDouble value, f64 component, usize index)noexcept{
    NWB_ASSERT(index < 4);
    switch(index){
    case 0u: return VectorDoubleSetX(value, component);
    case 1u: return VectorDoubleSetY(value, component);
    case 2u: return VectorDoubleSetZ(value, component);
    default: return VectorDoubleSetW(value, component);
    }
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetByIndexPtr(SIMDVectorDouble value, const f64 component, usize index)noexcept{
    NWB_ASSERT(index < 4);
    return VectorDoubleSetByIndex(value, component, index);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetXPtr(SIMDVectorDouble value, const f64 x)noexcept{
    return VectorDoubleSetX(value, x);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetYPtr(SIMDVectorDouble value, const f64 y)noexcept{
    return VectorDoubleSetY(value, y);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetZPtr(SIMDVectorDouble value, const f64 z)noexcept{
    return VectorDoubleSetZ(value, z);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetWPtr(SIMDVectorDouble value, const f64 w)noexcept{
    return VectorDoubleSetW(value, w);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetIntX(SIMDVectorDouble value, u64 x)noexcept{
    value[0] = BitCast<f64>(x);
    return value;
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetIntY(SIMDVectorDouble value, u64 y)noexcept{
    value[1] = BitCast<f64>(y);
    return value;
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetIntZ(SIMDVectorDouble value, u64 z)noexcept{
    value[2] = BitCast<f64>(z);
    return value;
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetIntW(SIMDVectorDouble value, u64 w)noexcept{
    value[3] = BitCast<f64>(w);
    return value;
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetIntByIndex(SIMDVectorDouble value, u64 component, usize index)noexcept{
    NWB_ASSERT(index < 4);
    switch(index){
    case 0u: return VectorDoubleSetIntX(value, component);
    case 1u: return VectorDoubleSetIntY(value, component);
    case 2u: return VectorDoubleSetIntZ(value, component);
    default: return VectorDoubleSetIntW(value, component);
    }
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetIntByIndexPtr(SIMDVectorDouble value, const u64 component, usize index)noexcept{
    NWB_ASSERT(index < 4);
    return VectorDoubleSetIntByIndex(value, component, index);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetIntXPtr(SIMDVectorDouble value, const u64 x)noexcept{
    return VectorDoubleSetIntX(value, x);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetIntYPtr(SIMDVectorDouble value, const u64 y)noexcept{
    return VectorDoubleSetIntY(value, y);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetIntZPtr(SIMDVectorDouble value, const u64 z)noexcept{
    return VectorDoubleSetIntZ(value, z);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSetIntWPtr(SIMDVectorDouble value, const u64 w)noexcept{
    return VectorDoubleSetIntW(value, w);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSplatX(SIMDVectorDouble value)noexcept{ return SIMDVectorDoubleDetail::SplatLane<0u>(value); }
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSplatY(SIMDVectorDouble value)noexcept{ return SIMDVectorDoubleDetail::SplatLane<1u>(value); }
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSplatZ(SIMDVectorDouble value)noexcept{ return SIMDVectorDoubleDetail::SplatLane<2u>(value); }
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSplatW(SIMDVectorDouble value)noexcept{ return SIMDVectorDoubleDetail::SplatLane<3u>(value); }
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSplatOne()noexcept{ return s_DoubleSIMDOne; }
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSplatInfinity()noexcept{ return s_DoubleSIMDInfinity; }
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSplatQNaN()noexcept{ return s_DoubleSIMDQNaN; }
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSplatEpsilon()noexcept{ return s_DoubleSIMDEpsilon; }
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSplatSignMask()noexcept{ return s_DoubleSIMDNegativeZero; }

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSwizzle(SIMDVectorDouble value, u32 e0, u32 e1, u32 e2, u32 e3)noexcept{
    NWB_ASSERT(e0 < 4 && e1 < 4 && e2 < 4 && e3 < 4);
    return VectorDoubleSetInt(BitCast<u64>(value[e0]), BitCast<u64>(value[e1]), BitCast<u64>(value[e2]), BitCast<u64>(value[e3]));
}

template<u32 E0, u32 E1, u32 E2, u32 E3>
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSwizzle(SIMDVectorDouble value)noexcept{
    static_assert(E0 < 4u && E1 < 4u && E2 < 4u && E3 < 4u);
    return __builtin_shufflevector(value, value, E0, E1, E2, E3);
}

template<>
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSwizzle<0, 1, 2, 3>(SIMDVectorDouble value)noexcept{ return value; }

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoublePermute(SIMDVectorDouble v0, SIMDVectorDouble v1, u32 x, u32 y, u32 z, u32 w)noexcept{
    NWB_ASSERT(x < 8 && y < 8 && z < 8 && w < 8);
    return VectorDoubleSetInt(
        x < 4u ? BitCast<u64>(v0[x]) : BitCast<u64>(v1[x - 4u]),
        y < 4u ? BitCast<u64>(v0[y]) : BitCast<u64>(v1[y - 4u]),
        z < 4u ? BitCast<u64>(v0[z]) : BitCast<u64>(v1[z - 4u]),
        w < 4u ? BitCast<u64>(v0[w]) : BitCast<u64>(v1[w - 4u])
    );
}

template<u32 X, u32 Y, u32 Z, u32 W>
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoublePermute(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    static_assert(X < 8u && Y < 8u && Z < 8u && W < 8u);
    return __builtin_shufflevector(v0, v1, X, Y, Z, W);
}

template<>
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoublePermute<0, 1, 2, 3>(SIMDVectorDouble v0, SIMDVectorDouble)noexcept{ return v0; }
template<>
NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoublePermute<4, 5, 6, 7>(SIMDVectorDouble, SIMDVectorDouble v1)noexcept{ return v1; }

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSelectControl(u32 selectX, u32 selectY, u32 selectZ, u32 selectW)noexcept{
    return VectorDoubleSetInt(selectX ? 0xFFFFFFFFFFFFFFFFu : 0u, selectY ? 0xFFFFFFFFFFFFFFFFu : 0u, selectZ ? 0xFFFFFFFFFFFFFFFFu : 0u, selectW ? 0xFFFFFFFFFFFFFFFFu : 0u);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleAndInt(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    const SIMDVectorDoubleDetail::UIntVector v0Bits = BitCast<SIMDVectorDoubleDetail::UIntVector>(v0);
    const SIMDVectorDoubleDetail::UIntVector v1Bits = BitCast<SIMDVectorDoubleDetail::UIntVector>(v1);
    return BitCast<SIMDVectorDouble>(v0Bits & v1Bits);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleOrInt(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    const SIMDVectorDoubleDetail::UIntVector v0Bits = BitCast<SIMDVectorDoubleDetail::UIntVector>(v0);
    const SIMDVectorDoubleDetail::UIntVector v1Bits = BitCast<SIMDVectorDoubleDetail::UIntVector>(v1);
    return BitCast<SIMDVectorDouble>(v0Bits | v1Bits);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleXorInt(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    const SIMDVectorDoubleDetail::UIntVector v0Bits = BitCast<SIMDVectorDoubleDetail::UIntVector>(v0);
    const SIMDVectorDoubleDetail::UIntVector v1Bits = BitCast<SIMDVectorDoubleDetail::UIntVector>(v1);
    return BitCast<SIMDVectorDouble>(v0Bits ^ v1Bits);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleAndCInt(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    const SIMDVectorDoubleDetail::UIntVector v0Bits = BitCast<SIMDVectorDoubleDetail::UIntVector>(v0);
    const SIMDVectorDoubleDetail::UIntVector v1Bits = BitCast<SIMDVectorDoubleDetail::UIntVector>(v1);
    return BitCast<SIMDVectorDouble>(v0Bits & ~v1Bits);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleNorInt(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    const SIMDVectorDoubleDetail::UIntVector v0Bits = BitCast<SIMDVectorDoubleDetail::UIntVector>(v0);
    const SIMDVectorDoubleDetail::UIntVector v1Bits = BitCast<SIMDVectorDoubleDetail::UIntVector>(v1);
    return BitCast<SIMDVectorDouble>(~(v0Bits | v1Bits));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSelect(SIMDVectorDouble v0, SIMDVectorDouble v1, SIMDVectorDouble control)noexcept{
    return VectorDoubleOrInt(VectorDoubleAndCInt(v0, control), VectorDoubleAndInt(v1, control));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleMergeXY(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    return __builtin_shufflevector(v0, v1, 0, 4, 1, 5);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleMergeZW(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    return __builtin_shufflevector(v0, v1, 2, 6, 3, 7);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleShiftLeft(SIMDVectorDouble v0, SIMDVectorDouble v1, u32 elements)noexcept{
    NWB_ASSERT(elements < 4);
    switch(elements){
    case 0: return VectorDoublePermute<0, 1, 2, 3>(v0, v1);
    case 1: return VectorDoublePermute<1, 2, 3, 4>(v0, v1);
    case 2: return VectorDoublePermute<2, 3, 4, 5>(v0, v1);
    default: return VectorDoublePermute<3, 4, 5, 6>(v0, v1);
    }
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleRotateLeft(SIMDVectorDouble value, u32 elements)noexcept{
    NWB_ASSERT(elements < 4);
    switch(elements){
    case 0: return value;
    case 1: return VectorDoubleSwizzle<1, 2, 3, 0>(value);
    case 2: return VectorDoubleSwizzle<2, 3, 0, 1>(value);
    default: return VectorDoubleSwizzle<3, 0, 1, 2>(value);
    }
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleRotateRight(SIMDVectorDouble value, u32 elements)noexcept{
    NWB_ASSERT(elements < 4);
    switch(elements){
    case 0: return value;
    case 1: return VectorDoubleSwizzle<3, 0, 1, 2>(value);
    case 2: return VectorDoubleSwizzle<2, 3, 0, 1>(value);
    default: return VectorDoubleSwizzle<1, 2, 3, 0>(value);
    }
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleInsert(SIMDVectorDouble vd, SIMDVectorDouble vs, u32 vsLeftRotateElements, u32 select0, u32 select1, u32 select2, u32 select3)noexcept{
    const SIMDVectorDouble control = VectorDoubleSelectControl(select0 & 1u, select1 & 1u, select2 & 1u, select3 & 1u);
    return VectorDoubleSelect(vd, VectorDoubleRotateLeft(vs, vsLeftRotateElements), control);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

