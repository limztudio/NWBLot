// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector_inverse_trig.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorLerp(SIMDVectorSingle v0, SIMDVectorSingle v1, f32 t)noexcept{
    return VectorMultiplyAdd(VectorSubtract(v1, v0), VectorReplicate(t), v0);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorLerpV(SIMDVectorSingle v0, SIMDVectorSingle v1, SIMDVectorSingle t)noexcept{
    return VectorMultiplyAdd(VectorSubtract(v1, v0), t, v0);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorHermiteV(SIMDVectorSingle position0, SIMDVectorSingle tangent0, SIMDVectorSingle position1, SIMDVectorSingle tangent1, SIMDVectorSingle t)noexcept;

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorHermite(SIMDVectorSingle position0, SIMDVectorSingle tangent0, SIMDVectorSingle position1, SIMDVectorSingle tangent1, f32 t)noexcept{
    return VectorHermiteV(position0, tangent0, position1, tangent1, VectorReplicate(t));
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorHermiteV(SIMDVectorSingle position0, SIMDVectorSingle tangent0, SIMDVectorSingle position1, SIMDVectorSingle tangent1, SIMDVectorSingle t)noexcept{
    const SIMDVectorSingle catMulT2 = VectorSet(-3.0f, -2.0f, 3.0f, -1.0f);
    const SIMDVectorSingle catMulT3 = VectorSet(2.0f, 1.0f, -2.0f, 1.0f);
    SIMDVectorSingle t2 = VectorMultiply(t, t);
    SIMDVectorSingle t3 = VectorMultiply(t, t2);
    t2 = VectorMultiply(t2, catMulT2);
    t3 = VectorMultiplyAdd(t3, catMulT3, t2);
    t3 = VectorAdd(t3, VectorAndInt(t, s_SIMDMaskY));
    t3 = VectorAdd(t3, s_SIMDIdentityR0);

    SIMDVectorSingle result = VectorMultiply(VectorSplatX(t3), position0);
    result = VectorMultiplyAdd(VectorSplatY(t3), tangent0, result);
    result = VectorMultiplyAdd(VectorSplatZ(t3), position1, result);
    return VectorMultiplyAdd(VectorSplatW(t3), tangent1, result);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorCatmullRomV(SIMDVectorSingle p0, SIMDVectorSingle p1, SIMDVectorSingle p2, SIMDVectorSingle p3, SIMDVectorSingle t)noexcept;

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorCatmullRom(SIMDVectorSingle p0, SIMDVectorSingle p1, SIMDVectorSingle p2, SIMDVectorSingle p3, f32 t)noexcept{ return VectorCatmullRomV(p0, p1, p2, p3, VectorReplicate(t)); }

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorCatmullRomV(SIMDVectorSingle p0, SIMDVectorSingle p1, SIMDVectorSingle p2, SIMDVectorSingle p3, SIMDVectorSingle t)noexcept{
    const SIMDVectorSingle three = VectorReplicate(3.0f);
    const SIMDVectorSingle five = VectorReplicate(5.0f);
    SIMDVectorSingle t2 = VectorMultiply(t, t);
    SIMDVectorSingle t3 = VectorMultiply(t, t2);
    SIMDVectorSingle result = VectorSubtract(VectorAdd(t2, t2), t);
    result = VectorMultiply(VectorSubtract(result, t3), p0);
    SIMDVectorSingle temp = VectorNegativeMultiplySubtract(t2, five, VectorMultiply(t3, three));
    temp = VectorAdd(temp, s_SIMDTwo);
    result = VectorMultiplyAdd(temp, p1, result);
    temp = VectorNegativeMultiplySubtract(t3, three, VectorMultiply(t2, s_SIMDFour));
    temp = VectorAdd(temp, t);
    result = VectorMultiplyAdd(temp, p2, result);
    t3 = VectorSubtract(t3, t2);
    result = VectorMultiplyAdd(t3, p3, result);
    return VectorMultiply(result, s_SIMDOneHalf);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorBaryCentric(SIMDVectorSingle p0, SIMDVectorSingle p1, SIMDVectorSingle p2, f32 f, f32 g)noexcept{
    const SIMDVectorSingle p10 = VectorSubtract(p1, p0);
    const SIMDVectorSingle p20 = VectorSubtract(p2, p0);
    SIMDVectorSingle result = VectorMultiplyAdd(p10, VectorReplicate(f), p0);
    return VectorMultiplyAdd(p20, VectorReplicate(g), result);
}

NWB_INLINE SIMDVectorSingle NWB_SIMD_CALL VectorBaryCentricV(SIMDVectorSingle p0, SIMDVectorSingle p1, SIMDVectorSingle p2, SIMDVectorSingle f, SIMDVectorSingle g)noexcept{
    const SIMDVectorSingle p10 = VectorSubtract(p1, p0);
    const SIMDVectorSingle p20 = VectorSubtract(p2, p0);
    SIMDVectorSingle result = VectorMultiplyAdd(p10, f, p0);
    return VectorMultiplyAdd(p20, g, result);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

