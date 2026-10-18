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
    // Lanes are phase and delta. Bad inputs become zero so the mod never sees them.
    // Used inputs are positive, so floor mod matches FMod.
    const SIMDVectorDouble wrapped = VectorDoubleMod(VectorDoubleSet(validPhase ? phase : 0.0, validDelta ? deltaSeconds : 0.0, 0.0, 0.0), one);
    const SIMDVectorDouble current = validPhase ? VectorDoubleSplatX(wrapped) : VectorDoubleZero();
    if(!validDelta)
        return VectorDoubleGetX(current);
    const SIMDVectorDouble next = VectorDoubleAdd(current, VectorDoubleSplatY(wrapped));
    return VectorDoubleGetX(VectorDoubleSelect(next, VectorDoubleSubtract(next, one), VectorDoubleGreaterOrEqual(next, one)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

