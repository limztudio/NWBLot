// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector_arithmetic_double.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleExp2(SIMDVectorDouble value)noexcept{
    return VectorDoubleSet(Exp2(VectorDoubleGetX(value)), Exp2(VectorDoubleGetY(value)), Exp2(VectorDoubleGetZ(value)), Exp2(VectorDoubleGetW(value)));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleExp10(SIMDVectorDouble value)noexcept{
    return VectorDoubleSet(Pow(10.0, value[0]), Pow(10.0, value[1]), Pow(10.0, value[2]), Pow(10.0, value[3]));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleExpE(SIMDVectorDouble value)noexcept{
    return VectorDoubleSet(Exp(value[0]), Exp(value[1]), Exp(value[2]), Exp(value[3]));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleExp(SIMDVectorDouble value)noexcept{ return VectorDoubleExpE(value); }

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleLog2(SIMDVectorDouble value)noexcept{
    return VectorDoubleSet(Log2(VectorDoubleGetX(value)), Log2(VectorDoubleGetY(value)), Log2(VectorDoubleGetZ(value)), Log2(VectorDoubleGetW(value)));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleLog10(SIMDVectorDouble value)noexcept{
    return VectorDoubleSet(Log10(VectorDoubleGetX(value)), Log10(VectorDoubleGetY(value)), Log10(VectorDoubleGetZ(value)), Log10(VectorDoubleGetW(value)));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleLogE(SIMDVectorDouble value)noexcept{
    return VectorDoubleSet(Log(VectorDoubleGetX(value)), Log(VectorDoubleGetY(value)), Log(VectorDoubleGetZ(value)), Log(VectorDoubleGetW(value)));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleLog(SIMDVectorDouble value)noexcept{ return VectorDoubleLogE(value); }

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoublePow(SIMDVectorDouble v0, SIMDVectorDouble v1)noexcept{
    return VectorDoubleSet(Pow(VectorDoubleGetX(v0), VectorDoubleGetX(v1)), Pow(VectorDoubleGetY(v0), VectorDoubleGetY(v1)), Pow(VectorDoubleGetZ(v0), VectorDoubleGetZ(v1)), Pow(VectorDoubleGetW(v0), VectorDoubleGetW(v1)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

