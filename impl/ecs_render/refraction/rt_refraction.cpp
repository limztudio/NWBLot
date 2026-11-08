// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/refraction/refraction_renderer.h>
#include <impl/ecs_render/raytrace/ray_trace_material_resources.h>
#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>

#include <impl/assets/graphics/refraction/push_constants.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(sizeof(RefractionPushConstants) == s_RefractionPushConstantsByteSize);
static_assert(NWB_REFRACTION_DENSE_ARGUMENT_OFFSET == s_RefractionDenseArgumentOffset);
static_assert(NWB_REFRACTION_TILE_ARGUMENT_BYTES == s_RefractionTileArgumentBytes);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RefractionRenderer::prepareRefractionResources(){
    auto& device = m_system.m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    m_system.m_rayTracingState.m_refractionUseHardwareTrace = false;
    if(!heap.isInitialized())
        return false;

    if(
        !RendererComputePipelineDetail::EnsurePushConstantComputePipeline(
            m_system.m_arena,
            device,
            m_system.m_shaderSystem,
            &RefractiveSubsystem::refractionClassifyTilesCs,
            Core::ShaderArchive::s_DefaultVariant,
            s_RefractionClassifyTilesDebugName,
            sizeof(RefractionPushConstants),
            false,
            m_system.m_rayTracingState.m_refractionBindingLayout,
            m_system.m_rayTracingState.m_refractionClassifyTilesShader,
            m_system.m_rayTracingState.m_refractionClassifyTilesPipeline
        )
        || !RendererComputePipelineDetail::EnsurePushConstantComputePipeline(
            m_system.m_arena,
            device,
            m_system.m_shaderSystem,
            &RefractiveSubsystem::refractionFinalizeTilesCs,
            Core::ShaderArchive::s_DefaultVariant,
            s_RefractionFinalizeTilesDebugName,
            sizeof(RefractionPushConstants),
            false,
            m_system.m_rayTracingState.m_refractionBindingLayout,
            m_system.m_rayTracingState.m_refractionFinalizeTilesShader,
            m_system.m_rayTracingState.m_refractionFinalizeTilesPipeline
        )
    )
        return false;

    if(m_diagnosticsEnabled && !m_statistics.prepareResources())
        return false;
    const b8 csg = m_system.m_lightSpaceShadow.m_csg.snapshot.hasCsg;
    const auto ensurePipeline = [&](const b8 hardware, const b8 dense) -> b8{
        const b8 diagnostics = m_diagnosticsEnabled;
        Core::ComputePipelineHandle& hardwarePipeline = diagnostics ? m_system.m_rayTracingState.m_refractionDiagnosticHwPipelines[csg ? 1u : 0u][dense ? 1u : 0u]
            : (csg ? (dense ? m_system.m_rayTracingState.m_refractionDenseCsgHwPipeline : m_system.m_rayTracingState.m_refractionCsgHwPipeline) : (dense ? m_system.m_rayTracingState.m_refractionDenseHwPipeline : m_system.m_rayTracingState.m_refractionHwPipeline));
        Core::ShaderHandle& hardwareShader = diagnostics ? m_system.m_rayTracingState.m_refractionDiagnosticHwShaders[csg ? 1u : 0u][dense ? 1u : 0u]
            : (csg ? (dense ? m_system.m_rayTracingState.m_refractionDenseCsgHwShader : m_system.m_rayTracingState.m_refractionCsgHwShader) : (dense ? m_system.m_rayTracingState.m_refractionDenseHwShader : m_system.m_rayTracingState.m_refractionHwShader));
        b8& hardwareFailed = diagnostics ? m_system.m_rayTracingState.m_refractionDiagnosticHwPipelineFailed[csg ? 1u : 0u][dense ? 1u : 0u]
            : (csg ? (dense ? m_system.m_rayTracingState.m_refractionDenseCsgHwPipelineFailed : m_system.m_rayTracingState.m_refractionCsgHwPipelineFailed) : (dense ? m_system.m_rayTracingState.m_refractionDenseHwPipelineFailed : m_system.m_rayTracingState.m_refractionHwPipelineFailed));
        Core::ComputePipelineHandle& pipeline = hardware ? hardwarePipeline : (dense ? m_system.m_rayTracingState.m_refractionDenseScreenPipeline : m_system.m_rayTracingState.m_refractionScreenPipeline);
        Core::ShaderHandle& shader = hardware ? hardwareShader : (dense ? m_system.m_rayTracingState.m_refractionDenseScreenShader : m_system.m_rayTracingState.m_refractionScreenShader);
        b8& failed = hardware ? hardwareFailed : (dense ? m_system.m_rayTracingState.m_refractionDenseScreenPipelineFailed : m_system.m_rayTracingState.m_refractionScreenPipelineFailed);
        if(pipeline)
            return true;
        if(failed)
            return false;
        const AStringView variant = ResolveRefractionVariant(hardware, diagnostics, csg, dense);
        if(
            !m_system.m_shaderSystem.loadShader<ComputeShader>(hardware ? &RefractiveSubsystem::refractionResolveHwCs : &RefractiveSubsystem::refractionResolveScreenCs, variant, ResolveRefractionPipelineDebugName(hardware, dense), shader)
            || !RendererComputePipelineDetail::CreateHeapComputePipeline(device, shader, m_system.m_rayTracingState.m_refractionBindingLayout, hardware, pipeline)
        ){
            failed = true;
            return false;
        }
        LogCreatedRefractionResolvePipeline(hardware, dense);
        return true;
    };

    // A separate shader keeps ray-tracing capabilities off unsupported devices.
    if(!ensurePipeline(false, false) || !ensurePipeline(false, true))
        return false;
    const b8 hardwareReady = m_system.m_rayTracingState.m_refractionHardwareTracingEnabled && m_system.shadowVisibilityHardwareSupported()
        && m_system.m_shadowVisibilityTraceResourcesPreflighted && m_system.m_shadowVisibilityBackendPipelinePreflighted
        && m_system.m_graphics.queryFeatureSupport(Core::Feature::RayQuery)
        && heap.hasAccelStructLayout()
        && m_system.m_rayTracingState.m_tlas && m_system.m_rayTracingState.m_tlasHeapHandle.valid()
        && m_system.m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer;
    if(hardwareReady && RayTraceMaterialResources(m_system.m_graphics, m_system.m_rayTracingState, m_system.m_rayTracingState).ensureRayTraceMaterialContextSlotsHeapHandle() && ensurePipeline(true, false) && ensurePipeline(true, true))
        m_system.m_rayTracingState.m_refractionUseHardwareTrace = true;
    return true;
}

RayTracingRefractionGraphResources RefractionRenderer::snapshotRefractionGraphResources(const u32 width, const u32 height, const u32 frameIndex)const noexcept{
    RayTracingRefractionGraphResources resources;
    const ECSRenderDetail::MeshViewBufferSnapshot view = m_system.m_meshSystem.meshViewBufferSnapshot();
    if(!view.bindingValid())
        return resources;
    resources.viewBuffer = view.buffer;
    resources.screenFallbackPipeline = m_system.m_rayTracingState.m_refractionScreenPipeline;
    resources.denseScreenFallbackPipeline = m_system.m_rayTracingState.m_refractionDenseScreenPipeline;
    resources.classifyTilesPipeline = m_system.m_rayTracingState.m_refractionClassifyTilesPipeline;
    resources.finalizeTilesPipeline = m_system.m_rayTracingState.m_refractionFinalizeTilesPipeline;
    resources.viewHeapSlot = view.heapHandle.slot();
    resources.usesHardwareTrace = m_system.m_rayTracingState.m_refractionUseHardwareTrace;
    const b8 csg = m_system.m_lightSpaceShadow.m_csg.snapshot.hasCsg;
    resources.pipeline = resources.usesHardwareTrace
        ? (m_diagnosticsEnabled ? m_system.m_rayTracingState.m_refractionDiagnosticHwPipelines[csg ? 1u : 0u][0]
            : (csg ? m_system.m_rayTracingState.m_refractionCsgHwPipeline : m_system.m_rayTracingState.m_refractionHwPipeline))
        : m_system.m_rayTracingState.m_refractionScreenPipeline;
    resources.densePipeline = resources.usesHardwareTrace
        ? (m_diagnosticsEnabled ? m_system.m_rayTracingState.m_refractionDiagnosticHwPipelines[csg ? 1u : 0u][1]
            : (csg ? m_system.m_rayTracingState.m_refractionDenseCsgHwPipeline : m_system.m_rayTracingState.m_refractionDenseHwPipeline))
        : m_system.m_rayTracingState.m_refractionDenseScreenPipeline;
    if(m_diagnosticsEnabled){
        const RefractionStatistics metadata = MakeRefractionSnapshotMetadata(
            frameIndex,
            m_system.m_graphics.getFrameIndex(),
            width,
            height,
            m_system.m_rayTracingState.m_refractionHardwareTracingEnabled,
            resources.usesHardwareTrace,
            csg
        );
        resources.statistics = m_statistics.snapshot(metadata);
    }
    if(resources.usesHardwareTrace){
        resources.sceneTlas = m_system.m_rayTracingState.m_tlas;
        resources.tlasHeapHandle = m_system.m_rayTracingState.m_tlasHeapHandle;
        resources.materialContextSlotsBuffer = m_system.m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer;
        resources.materialContextSlotsHeapSlot = m_system.m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle.slot();
        resources.certifiedInstanceIndex = csg ? Limit<u32>::s_Max : m_system.m_rayTracingState.m_refractionCertifiedInstanceIndex;
    }
    return resources;
}

b8 RefractionRenderer::recordRefractionTileClassification(Core::CommandList& commandList, const DeferredFrameTargets& targets, const RayTracingRefractionGraphResources& resources)const{
    if(!resources.valid() || !targets.bindless.valid() || !targets.avboit.refractionTileListDescriptor.valid() || !targets.avboit.refractionTileArgumentsDescriptor.valid() || targets.width == 0u || targets.height == 0u)
        return false;
    const auto push = ResolveRefractionPushConstants(
        targets.width,
        targets.height,
        targets.bindless.slotsBufferDescriptor.slot(),
        resources.materialContextSlotsHeapSlot,
        resources.viewHeapSlot,
        resources.opaqueReflectionSlot,
        resources.refractionEnabled,
        targets.avboit.refractionTileListDescriptor.slot(),
        targets.avboit.refractionTileArgumentsDescriptor.slot(),
        resources.statistics.counterDescriptor.slot(),
        resources.certifiedInstanceIndex
    );
    Core::ComputeState state;
    state.setPipeline(resources.classifyTilesPipeline.get());
    commandList.setComputeState(state);
    m_system.m_graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *resources.classifyTilesPipeline);
    commandList.setPushConstants(&push, sizeof(push));
    Core::GpuTimingMeasure timing(m_system.m_graphics.gpuTiming(), RendererGpuTimingScope::s_RefractionClassifyTiles, m_system.m_graphics.getDevice(), commandList);

    commandList.dispatch(DivideUpWrapping(targets.width, NWB_REFRACTION_GROUP_SIZE), DivideUpWrapping(targets.height, NWB_REFRACTION_GROUP_SIZE), 1u);
    // Finalization consumes the completed counter and replaces both mutually exclusive argument records.
    commandList.setBufferState(targets.avboit.refractionTileArguments.get(), Core::ResourceStates::UnorderedAccess, true);
    commandList.commitBarriers();
    state.setPipeline(resources.finalizeTilesPipeline.get());
    commandList.setComputeState(state);
    m_system.m_graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *resources.finalizeTilesPipeline);
    commandList.setPushConstants(&push, sizeof(push));
    commandList.dispatch(1u, 1u, 1u);
    return true;
}

b8 RefractionRenderer::recordRefractionResolve(Core::CommandList& commandList, const DeferredFrameTargets& targets, const RayTracingRefractionGraphResources& resources)const{
    if(!resources.valid() || !targets.bindless.valid() || targets.width == 0u || targets.height == 0u)
        return false;

    // The graph owns transitions and frozen declarations; this records without rebuilds.
    const auto push = ResolveRefractionPushConstants(
        targets.width,
        targets.height,
        targets.bindless.slotsBufferDescriptor.slot(),
        resources.materialContextSlotsHeapSlot,
        resources.viewHeapSlot,
        resources.opaqueReflectionSlot,
        resources.refractionEnabled,
        targets.avboit.refractionTileListDescriptor.slot(),
        targets.avboit.refractionTileArgumentsDescriptor.slot(),
        resources.statistics.counterDescriptor.slot(),
        resources.certifiedInstanceIndex
    );
    Core::ComputeState state;
    state.setPipeline(resources.pipeline.get()).setIndirectParams(targets.avboit.refractionTileArguments.get());
    commandList.setComputeState(state);
    m_system.m_graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *resources.pipeline.get(), resources.tlasHeapHandle);
    commandList.setPushConstants(&push, sizeof(push));
    {
        Core::GpuTimingMeasure timing(m_system.m_graphics.gpuTiming(), RendererGpuTimingScope::s_RefractionResolve, m_system.m_graphics.getDevice(), commandList);

        commandList.dispatchIndirect(0u);
        // Both kernels declare the same output; retain its write dependency even though one argument record is empty.
        commandList.setTextureState(targets.avboit.refractionResolve.get(), Core::TextureSubresourceSet{}, Core::ResourceStates::UnorderedAccess, true);
        commandList.commitBarriers();
        state.setPipeline(resources.densePipeline.get());
        commandList.setComputeState(state);
        m_system.m_graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *resources.densePipeline, resources.tlasHeapHandle);
        commandList.setPushConstants(&push, sizeof(push));
        commandList.dispatchIndirect(NWB_REFRACTION_DENSE_ARGUMENT_OFFSET);
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

