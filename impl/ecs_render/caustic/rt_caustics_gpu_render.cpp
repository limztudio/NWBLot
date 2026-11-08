// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_rt_caustics_gpu_render{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static CausticPhotonPushConstants MakeCausticPhotonPushConstants(
    const RendererRayTracingState& state,
    const DeferredFrameTargets& targets,
    const ECSRenderDetail::MeshViewBufferSnapshot& meshView,
    const CausticPhotonBudget& photonBudget,
    const u32 instanceCount,
    const u32 frameIndex,
    const u32 temporalPhaseCount
)noexcept{
    return ResolveCausticPhotonPushConstants(
        targets.width,
        targets.height,
        instanceCount,
        photonBudget,
        state.m_causticRefractiveInstanceCount,
        frameIndex,
        targets.bindless.gbufferDepth.slot(),
        targets.bindless.gbufferWorldPosition.slot(),
        state.m_causticEmissionTargets.heapHandle.slot(),
        meshView.heapHandle.slot(),
        targets.bindless.slotsBufferDescriptor.slot(),
        state.m_rayTraceMaterialContextSlotsHeapHandle.slot(),
        targets.bindless.causticAccumulatorStorage.slot(),
        temporalPhaseCount
    );
}

template<typename RecordFn>
static void RecordCausticPhotonPass(
    Core::GraphicsRuntime& graphics,
    const RendererRayTracingState& state,
    const CausticPhotonBudget& photonBudget,
    const u32 temporalPhaseCount,
    const AStringView routeLabel,
    Core::CommandList& commandList,
    Optional<Core::GpuTimingMeasure>* const causticPhotonTiming,
    b8& dispatchLogged,
    RecordFn&& recordPhotons
){
    if(causticPhotonTiming && causticPhotonTiming->has_value()){
        recordPhotons();
        causticPhotonTiming->value().finishTiming(commandList);
        causticPhotonTiming->reset();
    }
    else{
        Core::GpuTimingMeasure timing(graphics.gpuTiming(), RendererGpuTimingScope::s_CausticPhotons, graphics.getDevice(), commandList);

        recordPhotons();
    }

    LogCausticProducerDispatchIfNeeded(
        routeLabel,
        photonBudget,
        temporalPhaseCount,
        state.m_causticLightCount,
        state.m_causticRefractiveInstanceCount,
        dispatchLogged
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererRayTracingSystem::renderGpuBvhCaustics(Core::CommandList& commandList, const ECSRenderDetail::MeshViewBufferSnapshot& meshView, DeferredFrameTargets& targets, Optional<Core::GpuTimingMeasure>* const causticPhotonTiming){
    // Software photon producer runs before deferred lighting.
    if(!hasCausticWork(meshView))
        return false;
    NWB_ASSERT(meshView.bindingValid());
    NWB_ASSERT(targets.bindless.valid());
    const auto& pipeline = m_lightSpaceShadow.m_csg.snapshot.hasCsg ? m_rayTracingState.m_swCausticCsgPipeline : m_rayTracingState.m_swCausticPipeline;
    if(!pipeline || !m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle.valid() || !causticResolveResourcesReady(targets))
        return false;
    const u32 temporalPhaseCount = s_CausticTemporalPhaseCount;
    const CausticPhotonBudget photonBudget = MakeCausticPhotonBudget(m_causticQualitySettings, NWB_CAUSTIC_SW_GRID_SIDE, temporalPhaseCount);

    __hidden_rt_caustics_gpu_render::RecordCausticPhotonPass(
        m_graphics,
        m_rayTracingState,
        photonBudget,
        temporalPhaseCount,
        "software",
        commandList,
        causticPhotonTiming,
        m_rayTracingState.m_swCausticDispatchLogged,
        [&](){
            const CausticPhotonPushConstants pushConstants = __hidden_rt_caustics_gpu_render::MakeCausticPhotonPushConstants(
                m_rayTracingState,
                targets,
                meshView,
                photonBudget,
                m_rayTracingState.m_sceneBvhInstanceCount,
                static_cast<u32>(m_graphics.getFrameIndex()),
                temporalPhaseCount
            );
            Core::ComputeState computeState;
            computeState.setPipeline(pipeline.get());
            commandList.setComputeState(computeState);
            Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
            NWB_ASSERT(heap.isInitialized());
            heap.bindCompute(commandList, *pipeline.get());
            commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
            commandList.dispatch(DivideUp(photonBudget.photonsPerFrame, static_cast<u32>(NWB_CAUSTIC_SW_GROUP_SIZE)), 1u, 1u);
        });
    return true;
}

b8 RendererRayTracingSystem::renderHwCaustics(Core::CommandList& commandList, const ECSRenderDetail::MeshViewBufferSnapshot& meshView, DeferredFrameTargets& targets, Optional<Core::GpuTimingMeasure>* const causticPhotonTiming){
    // Hardware photons share the accumulator and resolve with the software reference.
    if(!hasHwCausticWork(meshView))
        return false;
    NWB_ASSERT(meshView.bindingValid());
    NWB_ASSERT(targets.bindless.valid());
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(!heap.isInitialized() || !m_rayTracingState.m_tlasHeapHandle.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: cannot dispatch caustics without the descriptor-buffer TLAS heap handle"));
        return false;
    }
    const b8 csg = m_lightSpaceShadow.m_csg.snapshot.hasCsg;
    const auto& pipeline = csg ? m_rayTracingState.m_hwCausticCsgPipeline : m_rayTracingState.m_hwCausticPipeline;
    const auto& shaderTable = csg ? m_rayTracingState.m_hwCausticCsgShaderTable : m_rayTracingState.m_hwCausticShaderTable;
    if(!pipeline || !shaderTable || !m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle.valid() || !causticResolveResourcesReady(targets))
        return false;
    const u32 temporalPhaseCount = s_CausticTemporalPhaseCount;
    const CausticPhotonBudget photonBudget = MakeCausticPhotonBudget(m_causticQualitySettings, s_CausticHwPhotonGridSide, temporalPhaseCount);

    __hidden_rt_caustics_gpu_render::RecordCausticPhotonPass(
        m_graphics,
        m_rayTracingState,
        photonBudget,
        temporalPhaseCount,
        "hardware",
        commandList,
        causticPhotonTiming,
        m_rayTracingState.m_hwCausticDispatchLogged,
        [&](){
            const CausticPhotonPushConstants pushConstants = __hidden_rt_caustics_gpu_render::MakeCausticPhotonPushConstants(
                m_rayTracingState,
                targets,
                meshView,
                photonBudget,
                m_rayTracingState.m_tlasInstanceCount,
                static_cast<u32>(m_graphics.getFrameIndex()),
                temporalPhaseCount
            );
            Core::RayTracingState rayTracingPassState;
            rayTracingPassState.setShaderTable(shaderTable.get());
            commandList.setRayTracingState(rayTracingPassState);
            // Bind heap blocks after RayTracingState; set 2 selects the TLAS generation.
            heap.bindRayTracing(commandList, *pipeline.get(), m_rayTracingState.m_tlasHeapHandle);
            commandList.setPushConstants(&pushConstants, sizeof(pushConstants));

            Core::RayTracingDispatchRaysArguments dispatchArgs;
            dispatchArgs.setDimensions(photonBudget.gridSide, photonBudget.gridSide / temporalPhaseCount, 1u);
            commandList.dispatchRays(dispatchArgs);
        });
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

