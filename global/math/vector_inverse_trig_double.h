// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector_trig_double.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleASin(SIMDVectorDouble value)noexcept{
    return VectorDoubleSet(ASin(value[0]), ASin(value[1]), ASin(value[2]), ASin(value[3]));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleASinEst(SIMDVectorDouble value)noexcept{
    return VectorDoubleASin(value);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleACos(SIMDVectorDouble value)noexcept{
    return VectorDoubleSet(ACos(value[0]), ACos(value[1]), ACos(value[2]), ACos(value[3]));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleACosEst(SIMDVectorDouble value)noexcept{
    return VectorDoubleACos(value);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleATan(SIMDVectorDouble value)noexcept{
    return VectorDoubleSet(ATan(value[0]), ATan(value[1]), ATan(value[2]), ATan(value[3]));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleATanEst(SIMDVectorDouble value)noexcept{
    return VectorDoubleATan(value);
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleATan2(SIMDVectorDouble y, SIMDVectorDouble x)noexcept{
    return VectorDoubleSet(ATan2(y[0], x[0]), ATan2(y[1], x[1]), ATan2(y[2], x[2]), ATan2(y[3], x[3]));
}

NWB_INLINE SIMDVectorDouble NWB_SIMD_CALL VectorDoubleATan2Est(SIMDVectorDouble y, SIMDVectorDouble x)noexcept{
    return VectorDoubleSet(ATan2(y[0], x[0]), ATan2(y[1], x[1]), ATan2(y[2], x[2]), ATan2(y[3], x[3]));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

