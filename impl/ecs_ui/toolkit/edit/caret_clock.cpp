// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "caret_clock.h"

#include <global/simd/vector.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


f64 AdvanceCaretPhase(const f64 phase, const f64 deltaSeconds)noexcept{
    const bool validPhase = IsFinite(phase) && phase > 0.0;
    const bool validDelta = IsFinite(deltaSeconds) && deltaSeconds > 0.0;
    const SIMDVectorDouble one = VectorDoubleSplatOne();
    // Lanes are phase and delta. Bad inputs become zero, so only finite values >= 0 reach the wrap.
    // For those, x - Truncate(x) is the exact fraction, the same as FMod(x, 1.0), and whole numbers give +0.
    const SIMDVectorDouble inputs = VectorDoubleSet(validPhase ? phase : 0.0, validDelta ? deltaSeconds : 0.0, 0.0, 0.0);
    const SIMDVectorDouble wrapped = VectorDoubleSubtract(inputs, VectorDoubleTruncate(inputs));
    const SIMDVectorDouble current = validPhase ? VectorDoubleSplatX(wrapped) : VectorDoubleZero();
    if(!validDelta)
        return VectorDoubleGetX(current);
    const SIMDVectorDouble next = VectorDoubleAdd(current, VectorDoubleSplatY(wrapped));
    return VectorDoubleGetX(VectorDoubleSelect(next, VectorDoubleSubtract(next, one), VectorDoubleGreaterOrEqual(next, one)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

