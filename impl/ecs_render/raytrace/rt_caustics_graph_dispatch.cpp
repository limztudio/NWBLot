// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_caustics_tasks.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererRayTracingSystem::causticResolveResourcesReady(const DeferredFrameTargets& targets, const f32 temporalDecay)const noexcept{
    return
        m_rayTracingState.m_causticResolve.m_prepare.m_pipeline
        && m_rayTracingState.m_causticResolve.m_wavelet.m_pipeline
        && m_rayTracingState.m_causticResolve.m_waveletStepOne.m_pipeline
        && m_rayTracingState.m_causticResolve.m_waveletStepTwo.m_pipeline
        && m_rayTracingState.m_causticResolve.m_waveletDirect.m_pipeline
        && m_rayTracingState.m_causticResolve.m_upsample.m_pipeline
        && m_rayTracingState.m_causticGeometryDownsamplePipeline
        && (temporalDecay <= 0.f || m_rayTracingState.m_causticAccumulatorDecayPipeline)
        && targets.causticAccumulator
        && targets.causticIrradiance
        && targets.causticHistory
        && targets.causticResolveHalf
        && targets.causticResolveGeometry
        && targets.bindless.valid()
        && targets.bindless.causticIrradianceStorage.valid()
        && targets.bindless.causticAccumulatorStorage.valid()
        && targets.bindless.causticHistoryStorage.valid()
        && targets.bindless.causticResolveHalfStorage.valid()
        && targets.bindless.causticResolveGeometryStorage.valid()
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

