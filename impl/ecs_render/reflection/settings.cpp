// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "settings.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ValidateReflectionSettings(const ReflectionSettings& settings){
    if(settings.traceMode > ReflectionTraceMode::Hybrid || settings.debugView > ReflectionDebugView::Confidence)
        return false;
    if(
        !IsFinite(settings.maxRayDistance) || settings.maxRayDistance <= 0.f
        || !IsFinite(settings.distanceFadeStart) || settings.distanceFadeStart < 0.f
        || settings.distanceFadeStart >= settings.maxRayDistance
        || !IsFinite(settings.roughnessCutoff) || settings.roughnessCutoff < 0.f || settings.roughnessCutoff > 1.f
    )
        return false;
    const SIMDVector top = LoadFloat(settings.environmentTop);
    const SIMDVector bottom = LoadFloat(settings.environmentBottom);
    return VectorIsFinite(top, VectorComponentMask::s_XYZ) && VectorIsFinite(bottom, VectorComponentMask::s_XYZ)
        && Vector3GreaterOrEqual(top, VectorZero()) && Vector3GreaterOrEqual(bottom, VectorZero());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

