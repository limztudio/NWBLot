// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "settings.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ValidateReflectionSettings(const ReflectionSettings& settings){
    constexpr u32 s_MaxTemporalSamples = 256u;
    constexpr u32 s_MaxSpatialRadius = 3u;
    if(settings.temporalMaxSamples == 0u || settings.temporalMaxSamples > s_MaxTemporalSamples || settings.spatialRadius > s_MaxSpatialRadius)
        return false;
    if(settings.maxOpticalQueries == 0u || settings.maxOpticalQueries > NWB_OPTICAL_MAX_QUERIES)
        return false;
    if(settings.traceMode > ReflectionTraceMode::Hybrid || settings.debugView > ReflectionDebugView::Confidence)
        return false;
    if(
        !IsFinite(settings.maxRayDistance) || settings.maxRayDistance <= 0.f
        || !IsFinite(settings.distanceFadeStart) || settings.distanceFadeStart < 0.f
        || settings.distanceFadeStart >= settings.maxRayDistance
        || !IsFinite(settings.roughnessCutoff) || settings.roughnessCutoff < 0.f || settings.roughnessCutoff > 1.f
    )
        return false;
    constexpr u32 s_MinScreenSteps = 8u;
    constexpr u32 s_MaxScreenSteps = 256u;
    constexpr f32 s_MaxScreenEdgeFade = 0.25f;
    if(
        settings.screenMaxSteps < s_MinScreenSteps || settings.screenMaxSteps > s_MaxScreenSteps
        || !IsFinite(settings.screenThickness) || settings.screenThickness <= 0.f
        || !IsFinite(settings.screenConfidenceThreshold)
        || settings.screenConfidenceThreshold <= 0.f || settings.screenConfidenceThreshold > 1.f
        || !IsFinite(settings.screenEdgeFade) || settings.screenEdgeFade < 0.f || settings.screenEdgeFade > s_MaxScreenEdgeFade
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

