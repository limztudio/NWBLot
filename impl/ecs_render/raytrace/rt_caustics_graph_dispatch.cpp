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


void RendererRayTracingSystem::dispatchGraphCausticGeometryDownsample(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned
){
    dispatchCausticGeometryDownsample(commandList, targets, graphEntryStatesOwned);
}

void RendererRayTracingSystem::dispatchGraphCausticResolveUpsample(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned
){
    dispatchCausticWaveletResolve(commandList, targets, graphEntryStatesOwned, true);
}

void RendererRayTracingSystem::dispatchGraphCausticResolvePrepare(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned
){
    dispatchCausticResolvePrepare(commandList, targets, graphEntryStatesOwned, true);
}

void RendererRayTracingSystem::dispatchGraphCausticResolveWavelet(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned
){
    dispatchCausticResolveWaveletPass(commandList, targets, 0u, graphEntryStatesOwned, true);
}

void RendererRayTracingSystem::dispatchGraphCausticResolveSecondWavelet(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned
){
    dispatchCausticResolveWaveletPass(commandList, targets, 1u, graphEntryStatesOwned, true);
}

void RendererRayTracingSystem::dispatchGraphCausticResolveThirdWavelet(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned
){
    dispatchCausticResolveWaveletPass(commandList, targets, 2u, graphEntryStatesOwned, true);
}

void RendererRayTracingSystem::dispatchGraphCausticResolveFourthWavelet(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned
){
    dispatchCausticResolveWaveletPass(commandList, targets, 3u, graphEntryStatesOwned, true);
}

void RendererRayTracingSystem::dispatchGraphCausticResolveFifthWavelet(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned
){
    dispatchCausticResolveWaveletPass(commandList, targets, 4u, graphEntryStatesOwned, true);
}

bool RendererRayTracingSystem::causticResolveResourcesReady(const DeferredFrameTargets& targets, const f32 temporalDecay)const{
    return
        m_rayTracingState.m_causticResolve.m_prepare.m_pipeline
        && m_rayTracingState.m_causticResolve.m_wavelet.m_pipeline
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

