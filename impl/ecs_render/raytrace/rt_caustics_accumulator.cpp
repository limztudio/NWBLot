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


void RendererRayTracingSystem::clearNonTemporalCausticAccumulator(Core::CommandList& commandList, DeferredFrameTargets& targets){
    if(!targets.causticAccumulator)
        return;

    // Temporal splat accumulation persists; non-temporal accumulation is cleared per frame.
    if(causticTemporalDecay() <= 0.f){
        m_rayTracingState.m_causticAccumulatorInitialized = false;
        m_rayTracingState.m_causticTemporalReuseFrameCount = 0u;
        commandList.setTextureState(targets.causticAccumulator.get(), ECSRenderDetail::s_CausticAccumulatorSubresources, Core::ResourceStates::CopyDest);
        commandList.commitBarriers();
        commandList.clearTextureUInt(*targets.causticAccumulator, ECSRenderDetail::s_CausticAccumulatorSubresources, 0u);
    }
}

void RendererRayTracingSystem::confirmCausticAccumulatorNonTemporalClear(){
    m_rayTracingState.m_causticAccumulatorInitialized = false;
    m_rayTracingState.m_causticTemporalReuseFrameCount = 0u;
}

void RendererRayTracingSystem::confirmCausticAccumulatorBootstrapClear(){
    m_rayTracingState.m_causticAccumulatorInitialized = true;
}

void RendererRayTracingSystem::dispatchCausticResolve(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned
){
    Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_CausticResolve, m_graphics.getDevice(), commandList);
    dispatchCausticGeometryDownsample(commandList, targets, graphEntryStatesOwned);
    dispatchCausticResolvePrepare(commandList, targets, graphEntryStatesOwned);
    for(u32 passIndex = 0u; passIndex < static_cast<u32>(NWB_CAUSTIC_RESOLVE_PASS_COUNT); ++passIndex)
        dispatchCausticResolveWaveletPass(commandList, targets, passIndex, graphEntryStatesOwned);
    dispatchCausticWaveletResolve(commandList, targets, graphEntryStatesOwned);
}


void RendererRayTracingSystem::dispatchCausticGeometryDownsample(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned
){
    NWB_ASSERT(targets.bindless.valid());
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    NWB_ASSERT(heap.isInitialized());

    // The normal graph declares this writable cache separately from wavelet resolve, so the latter receives a compiler-lowered UAV-to-SRV handoff. Direct callers retain the original native entry setup.
    commandList.setEnableUavBarriersForTexture(targets.causticResolveGeometry.get(), true);

    const u32 halfWidth = (targets.width + 1u) / 2u;
    const u32 halfHeight = (targets.height + 1u) / 2u;
    const u32 halfGroupsX = DivideUp(halfWidth, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const u32 halfGroupsY = DivideUp(halfHeight, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));

    if(!graphEntryStatesOwned){
        commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.depth.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        commandList.setTextureState(targets.causticResolveGeometry.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
    }

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
    DeferredFrameTargets& targets,
    const bool graphEntryStatesOwned,
    const bool graphOwnsPassEntryStates
){
    NWB_ASSERT(targets.bindless.valid());
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    NWB_ASSERT(heap.isInitialized());

    // Prepare reads accumulated photons and resolve geometry, then writes the parity-selected half-resolution target. The normal graph supplies those exact entry states; compatibility callers retain the original native sequence.
    commandList.setEnableUavBarriersForTexture(targets.causticAccumulator.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticHistory.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticResolveHalf.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.causticResolveGeometry.get(), true);
    if(!graphEntryStatesOwned){
        commandList.setTextureState(
            targets.causticAccumulator.get(),
            ECSRenderDetail::s_CausticAccumulatorSubresources,
            Core::ResourceStates::ShaderResource
        );
    }

    const u32 halfWidth = (targets.width + 1u) / 2u;
    const u32 halfHeight = (targets.height + 1u) / 2u;
    const u32 halfGroupsX = DivideUp(halfWidth, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const u32 halfGroupsY = DivideUp(halfHeight, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const f32 temporalDecay = causticTemporalDecay();
    const f32 effectiveIntensity = (temporalDecay > 0.f) ? (s_CausticIntensity * (1.f - temporalDecay)) : s_CausticIntensity;
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
        graphEntryStatesOwned,
        graphOwnsPassEntryStates,
        prepareToHalfB ? halfA : halfB,
        prepareToHalfB ? halfB : halfA,
        effectiveIntensity,
        1u,
        CausticResolveStage::PrepareDownsample,
        halfGroupsX,
        halfGroupsY
    );
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

