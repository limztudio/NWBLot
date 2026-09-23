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


bool RendererRayTracingSystem::renderGpuBvhCaustics(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const bool graphEntryStatesOwned,
    const bool graphOwnsAccumulatorBootstrapClear,
    const bool graphOwnsAccumulatorDecay,
    const bool graphOwnsResolve,
    Optional<Core::GpuTimingMeasure>* const causticPhotonTiming
){
    const ECSRenderDetail::MeshViewBufferSnapshot meshView = m_meshSystem.meshViewBufferSnapshot();
    return renderGpuBvhCaustics(
        commandList,
        meshView,
        targets,
        deferredLightingResources,
        graphEntryStatesOwned,
        graphOwnsAccumulatorBootstrapClear,
        graphOwnsAccumulatorDecay,
        graphOwnsResolve,
        causticPhotonTiming
    );
}

bool RendererRayTracingSystem::renderGpuBvhCaustics(
    Core::CommandList& commandList,
    const ECSRenderDetail::MeshViewBufferSnapshot& meshView,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const bool graphEntryStatesOwned,
    const bool graphOwnsAccumulatorBootstrapClear,
    const bool graphOwnsAccumulatorDecay,
    const bool graphOwnsResolve,
    Optional<Core::GpuTimingMeasure>* const causticPhotonTiming
){
    // Software photon producer runs before deferred lighting.

    if(!hasCausticWork(meshView))
        return false;
    NWB_ASSERT(meshView.bindingValid());
    NWB_ASSERT(targets.bindless.valid());
    NWB_ASSERT(deferredLightingResources.valid());
    const f32 temporalDecay = causticTemporalDecay();
    if(
        !m_rayTracingState.m_swCausticPipeline
        || !m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle.valid()
        || !causticResolveResourcesReady(targets, temporalDecay)
    )
        return false;
    const u32 temporalPhaseCount = causticTemporalPhaseCount();
    const u32 photonCount = s_CausticSwPhotonCount / temporalPhaseCount;

    const auto recordPhotons = [&](){
        if(temporalDecay > 0.f && !graphOwnsAccumulatorBootstrapClear && !graphOwnsAccumulatorDecay)
            prepareCausticAccumulatorForSplat(commandList, targets, temporalDecay);

        if(!graphEntryStatesOwned){
            // Direct compatibility callers restore heap-selected traversal inputs locally. The normal deferred graph declares and commits these descriptor-visible states before this callback begins.
            transitionSwShadowTraversalResources(commandList);
            commandList.setBufferState(m_rayTracingState.m_shadowInstanceBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(m_rayTracingState.m_causticEmissionTargetBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(meshView.buffer.get(), Core::ResourceStates::ConstantBuffer);
            commandList.setBufferState(targets.bindless.slotsBuffer.get(), Core::ResourceStates::ConstantBuffer);
            commandList.setTextureState(targets.depth.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
            commandList.setTextureState(targets.worldPosition.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::ShaderResource);
        }
        if(!graphOwnsAccumulatorDecay){
            commandList.setTextureState(targets.causticAccumulator.get(), ECSRenderDetail::s_CausticAccumulatorSubresources, Core::ResourceStates::UnorderedAccess);
            commandList.setEnableUavBarriersForTexture(targets.causticAccumulator.get(), true);
        }
        if(!graphEntryStatesOwned){
            commandList.setBufferState(deferredLightingResources.sceneShadingBuffer.get(), Core::ResourceStates::ConstantBuffer);
            commandList.setBufferState(deferredLightingResources.lightBuffer.get(), Core::ResourceStates::ShaderResource);
        }
        if(!graphEntryStatesOwned || !graphOwnsAccumulatorDecay)
            commandList.commitBarriers();

        CausticPhotonPushConstants pushConstants;
        pushConstants.width = targets.width;
        pushConstants.height = targets.height;
        pushConstants.instanceCount = m_rayTracingState.m_sceneBvhInstanceCount;
        // Temporal sampling phases retain the full-domain flux.
        pushConstants.photonCount = photonCount;
        pushConstants.emissionTargetCount = m_rayTracingState.m_causticRefractiveInstanceCount;
        pushConstants.gridSide = s_CausticSwPhotonGridSide;
        // The photon temporal phase rides the graphics frame index, not the dispatch count: early-frame
        // producer skips (pipeline warmup) would otherwise shift every later phase and make captures
        // run-varying. The graphics frame is capture-anchored, so identical frames emit identical photons.
        pushConstants.frameIndex = static_cast<u32>(m_graphics.getFrameIndex());
        pushConstants.depthSlot = targets.bindless.gbufferDepth.slot();
        pushConstants.worldPositionSlot = targets.bindless.gbufferWorldPosition.slot();
        pushConstants.emissionTargetSlot = m_rayTracingState.m_causticEmissionTargetHeapHandle.slot();
        pushConstants.viewSlot = meshView.heapHandle.slot();
        pushConstants.deferredResourcesHeapSlot = targets.bindless.slotsBufferDescriptor.slot();
        pushConstants.materialContextSlotsHeapSlot = m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle.slot();
        pushConstants.accumulatorStorageSlot = targets.bindless.causticAccumulatorStorage.slot();
        pushConstants.temporalPhaseCount = temporalPhaseCount;

        Core::ComputeState computeState;
        computeState.setPipeline(m_rayTracingState.m_swCausticPipeline.get());
        commandList.setComputeState(computeState);
        Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
        NWB_ASSERT(heap.isInitialized());
        heap.bindCompute(commandList, *m_rayTracingState.m_swCausticPipeline.get());
        commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
        commandList.dispatch(DivideUp(photonCount, static_cast<u32>(NWB_CAUSTIC_SW_GROUP_SIZE)), 1u, 1u);
        // Advance temporal phase only after recording a producer dispatch.
        m_rayTracingState.m_swCausticFrameIndex = m_rayTracingState.m_swCausticFrameIndex + 1u;
        advanceCausticTemporalReuse();
    };
    if(causticPhotonTiming && causticPhotonTiming->has_value()){
        recordPhotons();
        causticPhotonTiming->value().finishTiming(commandList);
        causticPhotonTiming->reset();
    }
    else{
        Core::GpuTimingMeasure timing(
            m_graphics.gpuTiming(),
            RendererGpuTimingScope::s_CausticPhotons,
            m_graphics.getDevice(),
            commandList
        );
        recordPhotons();
    }

    if(!graphOwnsResolve)
        dispatchCausticResolve(commandList, targets, graphEntryStatesOwned);

    if(!m_rayTracingState.m_swCausticDispatchLogged){
        m_rayTracingState.m_swCausticDispatchLogged = true;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: dispatched software caustic producer ({} photons/frame, {} temporal phases, {} full-grid budget, {} caustic lights, {} refractive instances)")
            , static_cast<u64>(photonCount)
            , static_cast<u64>(temporalPhaseCount)
            , static_cast<u64>(s_CausticSwPhotonCount)
            , static_cast<u64>(m_rayTracingState.m_causticLightCount)
            , static_cast<u64>(m_rayTracingState.m_causticRefractiveInstanceCount)
        );
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

