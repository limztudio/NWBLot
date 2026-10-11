// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector_exp_log_double.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSin(SIMDVectorDouble value)noexcept{
    return VectorDoubleSet(Sin(value[0]), Sin(value[1]), Sin(value[2]), Sin(value[3]));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleCos(SIMDVectorDouble value)noexcept{
    return VectorDoubleSet(Cos(value[0]), Cos(value[1]), Cos(value[2]), Cos(value[3]));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleTan(SIMDVectorDouble value)noexcept{
    return VectorDoubleSet(Tan(value[0]), Tan(value[1]), Tan(value[2]), Tan(value[3]));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSinH(SIMDVectorDouble value)noexcept{
    return VectorDoubleSet(SinH(value[0]), SinH(value[1]), SinH(value[2]), SinH(value[3]));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleCosH(SIMDVectorDouble value)noexcept{
    return VectorDoubleSet(CosH(value[0]), CosH(value[1]), CosH(value[2]), CosH(value[3]));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleTanH(SIMDVectorDouble value)noexcept{
    return VectorDoubleSet(TanH(value[0]), TanH(value[1]), TanH(value[2]), TanH(value[3]));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleSinEst(SIMDVectorDouble value)noexcept{
    return VectorDoubleSin(value);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleCosEst(SIMDVectorDouble value)noexcept{
    return VectorDoubleCos(value);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleTanEst(SIMDVectorDouble value)noexcept{
    return VectorDoubleTan(value);
}

NWB_INLINE void NWB_SIMD_CALL VectorDoubleSinCos(SIMDVectorDouble& outSin, SIMDVectorDouble& outCos, SIMDVectorDouble value)noexcept{
    outSin = VectorDoubleSin(value);
    outCos = VectorDoubleCos(value);
}

NWB_INLINE void NWB_SIMD_CALL VectorDoubleSinCosEst(SIMDVectorDouble& outSin, SIMDVectorDouble& outCos, SIMDVectorDouble value)noexcept{
    outSin = VectorDoubleSin(value);
    outCos = VectorDoubleCos(value);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

