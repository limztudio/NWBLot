// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/optics/coincident_volumes.h>
#include <core/task/gpu/compiled_graph.h>
#include <global/algorithm.h>
#include <impl/ecs_render/raytrace/rt_caustics_tasks.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::causticResolveResourcesReady(const DeferredFrameTargets& targets, const f32 temporalDecay)const{
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

