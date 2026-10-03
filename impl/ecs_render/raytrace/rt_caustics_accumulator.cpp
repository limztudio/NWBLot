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


void RendererRayTracingSystem::confirmCausticAccumulatorNonTemporalClear(){
    m_rayTracingState.m_causticAccumulatorInitialized = false;
    m_rayTracingState.m_causticTemporalReuseFrameCount = 0u;
}

void RendererRayTracingSystem::confirmCausticAccumulatorBootstrapClear(){
    m_rayTracingState.m_causticAccumulatorInitialized = true;
}

void RendererRayTracingSystem::dispatchCausticGeometryDownsample(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets){
    NWB_ASSERT(targets.bindless.valid());
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    NWB_ASSERT(heap.isInitialized());

    // The graph declares the writable geometry cache and its later sampled handoff.
    commandList.setEnableUavBarriersForTexture(targets.causticResolveGeometry.get(), true);

    const u32 halfWidth = (targets.width + 1u) / 2u;
    const u32 halfHeight = (targets.height + 1u) / 2u;
    const u32 halfGroupsX = DivideUp(halfWidth, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const u32 halfGroupsY = DivideUp(halfHeight, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));

    CausticGeometryDownsamplePushConstants geometryPush;
    geometryPush.width = targets.width;
    geometryPush.height = targets.height;
    geometryPush.halfWidth = halfWidth;
    geometryPush.halfHeight = halfHeight;
    geometryPush.worldPositionSlot = targets.bindless.gbufferWorldPosition.slot();
    geometryPush.depthSlot = targets.bindless.gbufferDepth.slot();
    geometryPush.outputStorageSlot = targets.bindless.causticResolveGeometryStorage.slot();

    Core::ComputeState geometryState;
    geometryState.setPipeline(m_rayTracingState.m_causticGeometryDownsamplePipeline.get());
    commandList.setComputeState(geometryState);
    heap.bindCompute(commandList, *m_rayTracingState.m_causticGeometryDownsamplePipeline.get());
    commandList.setPushConstants(&geometryPush, sizeof(geometryPush));
    commandList.dispatch(halfGroupsX, halfGroupsY, 1u);
}


void RendererRayTracingSystem::dispatchCausticResolvePrepare(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets){
    NWB_ASSERT(targets.bindless.valid());
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    NWB_ASSERT(heap.isInitialized());

    // Prepare reads accumulated photons and resolve geometry, then writes the parity-selected half-resolution target.
    commandList.setEnableUavBarriersForTexture(targets.causticAccumulator.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticHistory.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticResolveHalf.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticResolveGeometry.get(), true);

    const u32 halfWidth = (targets.width + 1u) / 2u;
    const u32 halfHeight = (targets.height + 1u) / 2u;
    const u32 halfGroupsX = DivideUp(halfWidth, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const u32 halfGroupsY = DivideUp(halfHeight, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const f32 temporalDecay = causticTemporalDecay();
    const f32 effectiveIntensity = EffectiveCausticIntensity(temporalDecay);
    const bool prepareToHalfB = (static_cast<u32>(NWB_CAUSTIC_RESOLVE_PASS_COUNT) % 2u) == 0u;
    const __hidden_caustics::CausticResolvePassResources halfA{
        targets.causticHistory.get(),
        targets.bindless.causticHistory.slot(),
        targets.bindless.causticHistoryStorage.slot()
    };
    const __hidden_caustics::CausticResolvePassResources halfB{
        targets.causticResolveHalf.get(),
        targets.bindless.causticResolveHalf.slot(),
        targets.bindless.causticResolveHalfStorage.slot()
    };
    __hidden_caustics::DispatchCausticResolvePass(
        commandList,
        heap,
        *m_rayTracingState.m_causticResolve.m_prepare.m_pipeline.get(),
        targets,
        prepareToHalfB ? halfA : halfB,
        prepareToHalfB ? halfB : halfA,
        effectiveIntensity,
        1u,
        CausticResolveStage::PrepareDownsample,
        halfGroupsX,
        halfGroupsY,
        causticResolveActivitySnapshot(targets)
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

