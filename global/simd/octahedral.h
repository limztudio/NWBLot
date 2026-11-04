// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "vector.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] NWB_INLINE SIMDVectorSingle FoldOctahedralAxis(const SIMDVectorSingle axis)noexcept{
    const SIMDVectorSingle absXY = VectorAndInt(VectorAbs(axis), s_SIMDMaskXY);
    const SIMDVectorSingle foldedMagnitude = VectorSubtract(s_SIMDOne, VectorSwizzle<1, 0, 2, 3>(absXY));
    const SIMDVectorSingle foldedSign = VectorSelect(s_SIMDNegativeOne, s_SIMDOne, VectorGreaterOrEqual(axis, VectorZero()));
    const SIMDVectorSingle foldedXY = VectorMultiply(foldedMagnitude, foldedSign);
    const SIMDVectorSingle folded = VectorSelect(axis, foldedXY, s_SIMDMaskXY);
    return VectorSelect(axis, folded, VectorLess(VectorSplatZ(axis), VectorZero()));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

