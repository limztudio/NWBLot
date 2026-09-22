// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector_inverse_trig.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVector SIMDCALL VectorLerp(SIMDVector v0, SIMDVector v1, f32 t)noexcept{
    return VectorMultiplyAdd(VectorSubtract(v1, v0), VectorReplicate(t), v0);
}

NWB_INLINE SIMDVector SIMDCALL VectorLerpV(SIMDVector v0, SIMDVector v1, SIMDVector t)noexcept{
    return VectorMultiplyAdd(VectorSubtract(v1, v0), t, v0);
}

NWB_INLINE SIMDVector SIMDCALL VectorHermiteV(SIMDVector position0, SIMDVector tangent0, SIMDVector position1, SIMDVector tangent1, SIMDVector t)noexcept;

NWB_INLINE SIMDVector SIMDCALL VectorHermite(SIMDVector position0, SIMDVector tangent0, SIMDVector position1, SIMDVector tangent1, f32 t)noexcept{
    return VectorHermiteV(position0, tangent0, position1, tangent1, VectorReplicate(t));
}

NWB_INLINE SIMDVector SIMDCALL VectorHermiteV(SIMDVector position0, SIMDVector tangent0, SIMDVector position1, SIMDVector tangent1, SIMDVector t)noexcept{
    const SIMDVector catMulT2 = VectorSet(-3.0f, -2.0f, 3.0f, -1.0f);
    const SIMDVector catMulT3 = VectorSet(2.0f, 1.0f, -2.0f, 1.0f);
    SIMDVector t2 = VectorMultiply(t, t);
    SIMDVector t3 = VectorMultiply(t, t2);
    t2 = VectorMultiply(t2, catMulT2);
    t3 = VectorMultiplyAdd(t3, catMulT3, t2);
    t3 = VectorAdd(t3, VectorAndInt(t, s_SIMDMaskY));
    t3 = VectorAdd(t3, s_SIMDIdentityR0);

    SIMDVector result = VectorMultiply(VectorSplatX(t3), position0);
    result = VectorMultiplyAdd(VectorSplatY(t3), tangent0, result);
    result = VectorMultiplyAdd(VectorSplatZ(t3), position1, result);
    return VectorMultiplyAdd(VectorSplatW(t3), tangent1, result);
}

NWB_INLINE SIMDVector SIMDCALL VectorCatmullRomV(SIMDVector p0, SIMDVector p1, SIMDVector p2, SIMDVector p3, SIMDVector t)noexcept;

NWB_INLINE SIMDVector SIMDCALL VectorCatmullRom(SIMDVector p0, SIMDVector p1, SIMDVector p2, SIMDVector p3, f32 t)noexcept{ return VectorCatmullRomV(p0, p1, p2, p3, VectorReplicate(t)); }

NWB_INLINE SIMDVector SIMDCALL VectorCatmullRomV(SIMDVector p0, SIMDVector p1, SIMDVector p2, SIMDVector p3, SIMDVector t)noexcept{
    const SIMDVector three = VectorReplicate(3.0f);
    const SIMDVector five = VectorReplicate(5.0f);
    SIMDVector t2 = VectorMultiply(t, t);
    SIMDVector t3 = VectorMultiply(t, t2);
    SIMDVector result = VectorSubtract(VectorAdd(t2, t2), t);
    result = VectorMultiply(VectorSubtract(result, t3), p0);
    SIMDVector temp = VectorNegativeMultiplySubtract(t2, five, VectorMultiply(t3, three));
    temp = VectorAdd(temp, s_SIMDTwo);
    result = VectorMultiplyAdd(temp, p1, result);
    temp = VectorNegativeMultiplySubtract(t3, three, VectorMultiply(t2, s_SIMDFour));
    temp = VectorAdd(temp, t);
    result = VectorMultiplyAdd(temp, p2, result);
    t3 = VectorSubtract(t3, t2);
    result = VectorMultiplyAdd(t3, p3, result);
    return VectorMultiply(result, s_SIMDOneHalf);
}

NWB_INLINE SIMDVector SIMDCALL VectorBaryCentric(SIMDVector p0, SIMDVector p1, SIMDVector p2, f32 f, f32 g)noexcept{
    const SIMDVector p10 = VectorSubtract(p1, p0);
    const SIMDVector p20 = VectorSubtract(p2, p0);
    SIMDVector result = VectorMultiplyAdd(p10, VectorReplicate(f), p0);
    return VectorMultiplyAdd(p20, VectorReplicate(g), result);
}

NWB_INLINE SIMDVector SIMDCALL VectorBaryCentricV(SIMDVector p0, SIMDVector p1, SIMDVector p2, SIMDVector f, SIMDVector g)noexcept{
    const SIMDVector p10 = VectorSubtract(p1, p0);
    const SIMDVector p20 = VectorSubtract(p2, p0);
    SIMDVector result = VectorMultiplyAdd(p10, f, p0);
    return VectorMultiplyAdd(p20, g, result);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

