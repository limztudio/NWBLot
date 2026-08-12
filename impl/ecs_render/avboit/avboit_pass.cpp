// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/avboit/avboit_private.h>

#include <impl/ecs_render/kernel/arena_names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_avboit{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Core::BlendState::RenderTarget BuildAdditiveBlendTarget(const Core::ColorMask::Mask colorWriteMask = Core::ColorMask::All){
    Core::BlendState::RenderTarget target;
    target
        .enableBlend()
        .setSrcBlend(Core::BlendFactor::One)
        .setDestBlend(Core::BlendFactor::One)
        .setBlendOp(Core::BlendOp::Add)
        .setSrcBlendAlpha(Core::BlendFactor::One)
        .setDestBlendAlpha(Core::BlendFactor::One)
        .setBlendOpAlpha(Core::BlendOp::Add)
        .setColorWriteMask(colorWriteMask)
    ;
    return target;
}

static void DispatchAvboitCompute(
    Core::CommandList& commandList,
    Core::ComputePipeline* pipeline,
    Core::GpuDescriptorHeap& heap,
    const AvboitFrameTargets& targets,
    const u32 groupCountX,
    const bool hdr10OutputActive
){
    NWB_ASSERT(pipeline);
    NWB_ASSERT(heap.isInitialized());

    Core::ComputeState computeState;
    computeState.setPipeline(pipeline);
    // The pipeline's low set is push-only; its work resources are selected by heap slots.
    commandList.setComputeState(computeState);
    heap.bindCompute(commandList, *pipeline);

    const RendererAvboitPushConstants pushConstants = BuildRendererAvboitPushConstants(targets, hdr10OutputActive);
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
    commandList.dispatch(groupCountX, 1, 1);
}

static void ClearAvboitTargetValues(Core::CommandList& commandList, AvboitFrameTargets& targets){
    const Core::Color transparentBlack(0.f, 0.f, 0.f, 0.f);
    commandList.clearTextureFloat(targets.lowRasterTarget.get(), ECSRenderDetail::s_FramebufferSubresources, transparentBlack);
    commandList.clearTextureFloat(targets.accumColor.get(), ECSRenderDetail::s_FramebufferSubresources, transparentBlack);
    commandList.clearTextureFloat(targets.accumExtinction.get(), ECSRenderDetail::s_FramebufferSubresources, transparentBlack);
    commandList.clearBufferUInt(targets.coverageBuffer.get(), 0u);
    commandList.clearBufferUInt(targets.depthWarpBuffer.get(), 0u);
    commandList.clearBufferUInt(targets.controlBuffer.get(), 0u);
    commandList.clearBufferUInt(targets.extinctionBuffer.get(), 0u);
    commandList.clearBufferUInt(targets.extinctionOverflowBuffer.get(), NWB_AVBOIT_OVERFLOW_INVALID);
    commandList.clearTextureFloat(
        targets.transmittanceTexture.get(),
        ECSRenderDetail::s_FramebufferSubresources,
        Core::Color(1.f, 1.f, 1.f, 1.f)
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::Format::Enum SelectRendererAvboitAccumColorFormat(Core::Device& device){
    constexpr Core::Format::Enum candidates[] = {
        Core::Format::RGBA16_FLOAT,
        Core::Format::RGBA8_UNORM,
    };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::RenderTarget | Core::FormatSupport::Blendable;

    return ECSRenderDetail::SelectSupportedFormat(device, candidates, requiredSupport);
}

Core::Format::Enum SelectRendererAvboitAccumExtinctionFormat(Core::Device& device){
    constexpr Core::Format::Enum candidates[] = {
        Core::Format::R16_FLOAT,
        Core::Format::R32_FLOAT,
        Core::Format::RGBA16_FLOAT,
        Core::Format::R8_UNORM,
        Core::Format::RGBA8_UNORM,
    };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::RenderTarget | Core::FormatSupport::Blendable;

    return ECSRenderDetail::SelectSupportedFormat(device, candidates, requiredSupport);
}

Core::Format::Enum SelectRendererAvboitTransmittanceFormat(Core::Device& device){
    constexpr Core::Format::Enum candidates[] = {
        NWB_AVBOIT_TRANSMITTANCE_CORE_FORMAT,
    };
    constexpr Core::FormatSupport::Mask requiredSupport =
        Core::FormatSupport::Texture
        | Core::FormatSupport::ShaderSample
        | Core::FormatSupport::ShaderUavStore
    ;

    return ECSRenderDetail::SelectSupportedFormat(device, candidates, requiredSupport);
}

Core::Format::Enum SelectRendererAvboitLowRasterFormat(Core::Device& device){
    constexpr Core::Format::Enum candidates[] = {
        Core::Format::R8_UNORM,
        Core::Format::RGBA8_UNORM,
    };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::RenderTarget;

    return ECSRenderDetail::SelectSupportedFormat(device, candidates, requiredSupport);
}

Core::RenderState BuildRendererAvboitVoxelRenderState(){
    Core::RenderState renderState;
    renderState.depthStencilState.disableDepthTest().disableDepthWrite();
    renderState.rasterState.enableDepthClip().setCullBack();
    renderState.blendState.targets[0].setColorWriteMask(Core::ColorMask::None);
    return renderState;
}

Core::RenderState BuildRendererAvboitAccumulateRenderState(){
    Core::RenderState renderState;
    renderState.depthStencilState
        .enableDepthTest()
        .disableDepthWrite()
        .setDepthFunc(Core::ComparisonFunc::LessOrEqual)
    ;
    renderState.rasterState.enableDepthClip().setCullBack();
    renderState.blendState
        .setRenderTarget(NWB_AVBOIT_ACCUM_COLOR_LOCATION, __hidden_avboit::BuildAdditiveBlendTarget())
        .setRenderTarget(NWB_AVBOIT_ACCUM_EXTINCTION_LOCATION, __hidden_avboit::BuildAdditiveBlendTarget(Core::ColorMask::Red))
    ;
    return renderState;
}

RendererAvboitPushConstants BuildRendererAvboitPushConstants(const AvboitFrameTargets& targets, const bool hdr10OutputActive){
    NWB_ASSERT(targets.deferredSlotsBufferDescriptor.valid());
    NWB_ASSERT(targets.deferredSlotsBufferDescriptor.descriptorClass() == Core::GpuDescriptorClass::UniformBuffer);

    RendererAvboitPushConstants pushConstants;
    pushConstants.frame[NWB_AVBOIT_PUSH_FRAME_FULL_WIDTH] = targets.fullWidth;
    pushConstants.frame[NWB_AVBOIT_PUSH_FRAME_FULL_HEIGHT] = targets.fullHeight;
    pushConstants.frame[NWB_AVBOIT_PUSH_FRAME_LOW_WIDTH] = targets.lowWidth;
    pushConstants.frame[NWB_AVBOIT_PUSH_FRAME_LOW_HEIGHT] = targets.lowHeight;
    pushConstants.volume[NWB_AVBOIT_PUSH_VOLUME_VIRTUAL_SLICE_COUNT] = targets.virtualSliceCount;
    pushConstants.volume[NWB_AVBOIT_PUSH_VOLUME_PHYSICAL_SLICE_COUNT] = targets.physicalSliceCount;
    const u32 physicalExtinctionWordCount = DivideUp(targets.physicalSliceCount, ECSRenderAvboitDetail::s_AvboitExtinctionSlicesPerWord);
    pushConstants.volume[NWB_AVBOIT_PUSH_VOLUME_EXTINCTION_WORD_COUNT] = static_cast<u32>(
        static_cast<u64>(targets.lowWidth) * static_cast<u64>(targets.lowHeight) * static_cast<u64>(physicalExtinctionWordCount)
    );
    pushConstants.volume[NWB_AVBOIT_PUSH_VOLUME_COVERAGE_WORD_COUNT] = DivideUp(targets.virtualSliceCount, NWB_AVBOIT_COVERAGE_SLICES_PER_WORD);
    pushConstants.params.raw[NWB_AVBOIT_PUSH_PARAMS_PRESENTATION_MODE] = hdr10OutputActive
        ? NWB_AVBOIT_PRESENTATION_HDR10
        : NWB_AVBOIT_PRESENTATION_SDR
    ;
    pushConstants.params.raw[NWB_AVBOIT_PUSH_PARAMS_EXTINCTION_FIXED_SCALE] = ECSRenderAvboitDetail::s_AvboitExtinctionFixedScale;
    pushConstants.params.raw[NWB_AVBOIT_PUSH_PARAMS_SELF_OCCLUSION_SLICE_BIAS] = ECSRenderAvboitDetail::s_AvboitSelfOcclusionSliceBias;
    pushConstants.heapSlots[NWB_AVBOIT_PUSH_HEAP_SLOT_DEFERRED_BINDLESS_RESOURCES] = targets.deferredSlotsBufferDescriptor.slot();
    return pushConstants;
}

void RendererAvboitSystem::clearAvboitTargets(Core::CommandList& commandList, AvboitFrameTargets& targets){
    NWB_ASSERT(targets.valid());

    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_AvboitClear, graphics().getDevice(), commandList);

    commandList.setTextureState(targets.lowRasterTarget.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.accumColor.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.accumExtinction.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    commandList.setBufferState(targets.coverageBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.setBufferState(targets.depthWarpBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.setBufferState(targets.controlBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.setBufferState(targets.extinctionBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.setBufferState(targets.extinctionOverflowBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.transmittanceTexture.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);

    commandList.commitBarriers();

    __hidden_avboit::ClearAvboitTargetValues(commandList, targets);
}

void RendererAvboitSystem::clearGraphOwnedAvboitTargets(Core::CommandList& commandList, AvboitFrameTargets& targets){
    NWB_ASSERT(targets.valid());

    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_AvboitClear, graphics().getDevice(), commandList);
    __hidden_avboit::ClearAvboitTargetValues(commandList, targets);
}

bool RendererAvboitSystem::prepareAvboitPassResources(
    DeferredFrameTargets& targets,
    const CsgFrameState& csgFrameState
){
    AvboitFrameTargets& avboitTargets = targets.avboit;
    if(!avboitTargets.valid())
        return false;

    const bool hasTransparentCsgWork = csgFrameState.hasTransparentStaticWork || csgFrameState.hasTransparentSkinnedWork;
    if(
        hasTransparentCsgWork
        && !m_renderer.materialSystem().prepareMaterialPassResources(
            targets.framebuffer.get(),
            MaterialPipelinePass::CsgReceiverSurface,
            true,
            csgFrameState,
            nullptr
        )
    )
        return false;

    return
        m_renderer.materialSystem().prepareMaterialPassResources(
            avboitTargets.lowFramebuffer.get(),
            MaterialPipelinePass::AvboitOccupancy,
            true,
            csgFrameState,
            &avboitTargets
        )
        && m_renderer.materialSystem().prepareMaterialPassResources(
            avboitTargets.lowFramebuffer.get(),
            MaterialPipelinePass::AvboitExtinction,
            true,
            csgFrameState,
            &avboitTargets
        )
        && m_renderer.materialSystem().prepareMaterialPassResources(
            avboitTargets.accumulationFramebuffer.get(),
            MaterialPipelinePass::AvboitAccumulate,
            true,
            csgFrameState,
            &avboitTargets
        )
    ;
}

void RendererAvboitSystem::buildTransparentCsgIntervals(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const CsgFrameState& csgFrameState
){
    if(!targets.framebuffer)
        return;
    if(!csgFrameState.hasTransparentStaticWork && !csgFrameState.hasTransparentSkinnedWork)
        return;

    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_TransparentCsgIntervals, graphics().getDevice(), commandList);

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TransparentCsgIntervalArena);
    MaterialPassDrawItemPartitions drawItems{scratchArena};
    InstanceGpuDataVector instanceData{scratchArena};
    CsgFrameGpuData csgFrameData{scratchArena};
#if defined(NWB_DEBUG)
    ECSRenderDetail::MaterialTypedInstanceRangeVector materialTypedRanges{scratchArena};
#endif
    MaterialTypedByteDataVector materialTypedBytes{scratchArena};

    m_renderer.materialSystem().gatherMaterialPassDrawItems(
        targets.framebuffer.get(),
        MaterialPipelinePass::CsgReceiverSurface,
        true,
        csgFrameState,
        drawItems,
        instanceData,
        csgFrameData,
#if defined(NWB_DEBUG)
        materialTypedRanges,
#endif
        materialTypedBytes,
        RendererResourceLookupMode::PreparedOnly
    );
    if(drawItems.csgReceiverSurface.empty() || !csgFrameData.hasWork())
        return;

    m_renderer.m_deferredSystem.clearCsgIntervalTargets(
        commandList,
        targets,
        csgFrameData.workRegion.resolveRect(targets.width, targets.height)
    );

    const bool drawBuffersReady = m_renderer.materialSystem().materialPassDrawBuffersReady(instanceData, materialTypedBytes);
    const bool csgResourcesReady = m_renderer.csgSystem().csgFrameBuffersReady(csgFrameData);
    const bool receiverSurfaceDrawResourcesReady =
        m_renderer.materialSystem().materialPassDrawResourcesReady(drawItems.csgReceiverSurface)
    ;
    if(!drawBuffersReady || !csgResourcesReady || !receiverSurfaceDrawResourcesReady)
        return;

    if(!m_renderer.materialSystem().uploadMaterialPassDrawBuffers(
        commandList,
        instanceData,
#if defined(NWB_DEBUG)
        materialTypedRanges,
#endif
        materialTypedBytes
    ))
        return;
    if(!m_renderer.csgSystem().uploadCsgFrameBuffers(commandList, csgFrameData))
        return;
    if(!m_renderer.csgSystem().uploadCsgIntervalSampleState(commandList, targets, csgFrameData))
        return;

    const f32 meshViewAspectRatio = ECSRenderDetail::ResolveFramebufferAspectRatio(targets.framebuffer->getFramebufferInfo());
    if(!m_renderer.meshSystem().updateMeshViewBuffer(commandList, meshViewAspectRatio))
        return;

    Core::ViewportState viewportState;
    viewportState
        .addViewport(targets.framebuffer->getFramebufferInfo().getViewport())
        .addScissorRect(csgFrameData.workRegion.resolveRect(targets.width, targets.height))
    ;

    m_renderer.csgSystem().dispatchCsgIntervalPeels(commandList, targets, csgFrameData);

    const MaterialPassDrawContext csgReceiverSurfaceDrawContext{
        commandList,
        targets.framebuffer.get(),
        MaterialPipelinePass::CsgReceiverSurface,
        nullptr,
        viewportState
    };
    m_renderer.materialSystem().renderMaterialPassDrawItems(
        csgReceiverSurfaceDrawContext,
        drawItems.csgReceiverSurface
    );

    m_renderer.csgSystem().dispatchCsgReceiverSpanBuild(commandList, targets, csgFrameData);
    m_renderer.csgSystem().dispatchCsgIntervalCombine(commandList, targets, csgFrameData);
}

void RendererAvboitSystem::renderPreparedTransparentCsgIntervals(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const MaterialPassDrawItems& receiverSurfaceDrawItems,
    const CsgFrameGpuData& csgFrameData,
    const usize instanceCount,
    const usize materialTypedByteCount,
    const bool intervalTargetsGraphOwned
){
    if(
        !targets.framebuffer
        || receiverSurfaceDrawItems.empty()
        || !csgFrameData.hasWork()
    )
        return;

    Core::GpuTimingMeasure timing(
        graphics().gpuTiming(),
        RendererGpuTimingScope::s_TransparentCsgIntervals,
        graphics().getDevice(),
        commandList
    );
    // The normal prepared path records this exact rect clear as a preceding graph task. Retain the direct helper
    // for compatibility callers, including its historical all-target state preparation before readiness checks.
    if(!intervalTargetsGraphOwned){
        m_renderer.m_deferredSystem.clearCsgIntervalTargets(
            commandList,
            targets,
            csgFrameData.workRegion.resolveRect(targets.width, targets.height)
        );
    }

    // The graph copied the material and CSG bytes after preflight froze every selected handle.  Do not rebuild or
    // rewrite them here: a rejected packet will re-declare the retained blobs, while this native step only consumes
    // the graph-owned data.
    const bool drawBuffersReady = m_renderer.materialSystem().materialPassDrawBuffersReady(
        instanceCount,
        materialTypedByteCount
    );
    const bool csgResourcesReady = m_renderer.csgSystem().csgFrameBuffersReady(csgFrameData);
    const bool receiverSurfaceDrawResourcesReady =
        m_renderer.materialSystem().materialPassDrawResourcesReady(receiverSurfaceDrawItems)
    ;
    if(!drawBuffersReady || !csgResourcesReady || !receiverSurfaceDrawResourcesReady)
        return;

    Core::ViewportState viewportState;
    viewportState
        .addViewport(targets.framebuffer->getFramebufferInfo().getViewport())
        .addScissorRect(csgFrameData.workRegion.resolveRect(targets.width, targets.height))
    ;

    m_renderer.csgSystem().dispatchCsgIntervalPeels(commandList, targets, csgFrameData);

    const MaterialPassDrawContext csgReceiverSurfaceDrawContext{
        commandList,
        targets.framebuffer.get(),
        MaterialPipelinePass::CsgReceiverSurface,
        nullptr,
        viewportState
    };
    m_renderer.materialSystem().renderMaterialPassDrawItems(
        csgReceiverSurfaceDrawContext,
        receiverSurfaceDrawItems
    );

    m_renderer.csgSystem().dispatchCsgReceiverSpanBuild(commandList, targets, csgFrameData);
    m_renderer.csgSystem().dispatchCsgIntervalCombine(commandList, targets, csgFrameData);
}

void RendererAvboitSystem::renderAvboitTransparentCsgIntervals(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const CsgFrameState& csgFrameState,
    const MaterialPassDrawItems* const preparedTransparentCsgReceiverSurfaceDrawItems,
    const CsgFrameGpuData* const preparedTransparentCsgFrameData,
    const usize preparedTransparentCsgInstanceCount,
    const usize preparedTransparentCsgMaterialTypedByteCount,
    const bool preparedTransparentCsgIntervalTargetsGraphOwned
){
    if(preparedTransparentCsgReceiverSurfaceDrawItems || preparedTransparentCsgFrameData){
        NWB_ASSERT(preparedTransparentCsgReceiverSurfaceDrawItems);
        NWB_ASSERT(preparedTransparentCsgFrameData);
        if(preparedTransparentCsgReceiverSurfaceDrawItems && preparedTransparentCsgFrameData){
            renderPreparedTransparentCsgIntervals(
                commandList,
                targets,
                *preparedTransparentCsgReceiverSurfaceDrawItems,
                *preparedTransparentCsgFrameData,
                preparedTransparentCsgInstanceCount,
                preparedTransparentCsgMaterialTypedByteCount,
                preparedTransparentCsgIntervalTargetsGraphOwned
            );
        }
    }
    else
        buildTransparentCsgIntervals(commandList, targets, csgFrameState);
}

void RendererAvboitSystem::renderAvboitOccupancyPass(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const CsgFrameState& csgFrameState,
    const MaterialPassDrawItemPartitions* const preparedOccupancyDrawItems,
    const CsgFrameGpuData* const preparedOccupancyCsgFrameData,
    const usize preparedOccupancyInstanceCount,
    const usize preparedOccupancyMaterialTypedByteCount,
    const bool occupancyStatesGraphOwned
){
    AvboitFrameTargets& avboitTargets = targets.avboit;
    NWB_ASSERT(avboitTargets.valid());
    NWB_ASSERT(avboitState().m_depthWarpPipeline);
    NWB_ASSERT(avboitState().m_integratePipeline);

    // Occupancy discovers opaque depth and writes coverage solely through global heap descriptors. The normal graph
    // path declares those exact states at its packet boundary; direct compatibility callers retain this bridge.
    if(!occupancyStatesGraphOwned){
        commandList.setTextureState(
            targets.depth.get(),
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        );
        commandList.setBufferState(avboitTargets.coverageBuffer.get(), Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
    }

    if(preparedOccupancyDrawItems || preparedOccupancyCsgFrameData){
        NWB_ASSERT(preparedOccupancyDrawItems);
        NWB_ASSERT(preparedOccupancyCsgFrameData);
        if(preparedOccupancyDrawItems && preparedOccupancyCsgFrameData){
            m_renderer.materialSystem().renderPreparedMaterialPass(
                commandList,
                avboitTargets.lowFramebuffer.get(),
                MaterialPipelinePass::AvboitOccupancy,
                &avboitTargets,
                *preparedOccupancyDrawItems,
                *preparedOccupancyCsgFrameData,
                preparedOccupancyInstanceCount,
                preparedOccupancyMaterialTypedByteCount
            );
        }
    }
    else{
        m_renderer.materialSystem().renderMaterialPass(
            commandList,
            avboitTargets.lowFramebuffer.get(),
            MaterialPipelinePass::AvboitOccupancy,
            true,
            csgFrameState,
            &avboitTargets
        );
    }
    commandList.endRenderPass();
}

void RendererAvboitSystem::renderAvboitPreDepthWarpPasses(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const CsgFrameState& csgFrameState,
    const MaterialPassDrawItems* const preparedTransparentCsgReceiverSurfaceDrawItems,
    const CsgFrameGpuData* const preparedTransparentCsgFrameData,
    const usize preparedTransparentCsgInstanceCount,
    const usize preparedTransparentCsgMaterialTypedByteCount
){
    renderAvboitTransparentCsgIntervals(
        commandList,
        targets,
        csgFrameState,
        preparedTransparentCsgReceiverSurfaceDrawItems,
        preparedTransparentCsgFrameData,
        preparedTransparentCsgInstanceCount,
        preparedTransparentCsgMaterialTypedByteCount
    );
    renderAvboitOccupancyPass(commandList, targets, csgFrameState);
}

void RendererAvboitSystem::renderAvboitExtinctionPass(
    Core::CommandList& commandList,
    AvboitFrameTargets& avboitTargets,
    const CsgFrameState& csgFrameState,
    const MaterialPassDrawItemPartitions* const preparedExtinctionDrawItems,
    const CsgFrameGpuData* const preparedExtinctionCsgFrameData,
    const usize preparedExtinctionInstanceCount,
    const usize preparedExtinctionMaterialTypedByteCount
){
    NWB_ASSERT(avboitTargets.valid());

    // The graph records the warp/control reads and packed-extinction writes as packet-boundary state; this thunk
    // contains only the native raster pass.

    if(preparedExtinctionDrawItems || preparedExtinctionCsgFrameData){
        NWB_ASSERT(preparedExtinctionDrawItems);
        NWB_ASSERT(preparedExtinctionCsgFrameData);
        if(preparedExtinctionDrawItems && preparedExtinctionCsgFrameData){
            m_renderer.materialSystem().renderPreparedMaterialPass(
                commandList,
                avboitTargets.lowFramebuffer.get(),
                MaterialPipelinePass::AvboitExtinction,
                &avboitTargets,
                *preparedExtinctionDrawItems,
                *preparedExtinctionCsgFrameData,
                preparedExtinctionInstanceCount,
                preparedExtinctionMaterialTypedByteCount
            );
        }
    }
    else{
        m_renderer.materialSystem().renderMaterialPass(
            commandList,
            avboitTargets.lowFramebuffer.get(),
            MaterialPipelinePass::AvboitExtinction,
            true,
            csgFrameState,
            &avboitTargets
        );
    }
    commandList.endRenderPass();
}

void RendererAvboitSystem::renderAvboitAccumulatePass(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const CsgFrameState& csgFrameState,
    const MaterialPassDrawItemPartitions* const preparedAccumulationDrawItems,
    const CsgFrameGpuData* const preparedAccumulationCsgFrameData,
    const usize preparedAccumulationInstanceCount,
    const usize preparedAccumulationMaterialTypedByteCount,
    const bool accumulationAttachmentStatesGraphOwned
){
    AvboitFrameTargets& avboitTargets = targets.avboit;
    NWB_ASSERT(avboitTargets.valid());

    // The graph records the integrated volume and work-buffer reads as packet-boundary state; this thunk owns only
    // the native raster pass and its explicit final cross-graph transition below.

    if(preparedAccumulationDrawItems || preparedAccumulationCsgFrameData){
        NWB_ASSERT(preparedAccumulationDrawItems);
        NWB_ASSERT(preparedAccumulationCsgFrameData);
        if(preparedAccumulationDrawItems && preparedAccumulationCsgFrameData){
            m_renderer.materialSystem().renderPreparedMaterialPass(
                commandList,
                avboitTargets.accumulationFramebuffer.get(),
                MaterialPipelinePass::AvboitAccumulate,
                &avboitTargets,
                *preparedAccumulationDrawItems,
                *preparedAccumulationCsgFrameData,
                preparedAccumulationInstanceCount,
                preparedAccumulationMaterialTypedByteCount
            );
        }
    }
    else{
        m_renderer.materialSystem().renderMaterialPass(
            commandList,
            avboitTargets.accumulationFramebuffer.get(),
            MaterialPipelinePass::AvboitAccumulate,
            true,
            csgFrameState,
            &avboitTargets
        );
    }
    commandList.endRenderPass();

    // Deferred composite is a Compute pass. The normal graph lowers the two accumulation attachment transitions
    // in its following Graphics finalizer, so Compute never names unsupported attachment accesses. Direct callers
    // retain the established bridge. Read-only depth remains explicit because framebuffer setup changed it to
    // DepthRead and its broader G-buffer compatibility handoff is intentionally outside this slice.
    if(!accumulationAttachmentStatesGraphOwned){
        commandList.setTextureState(
            avboitTargets.accumColor.get(),
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        );
        commandList.setTextureState(
            avboitTargets.accumExtinction.get(),
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        );
    }
    commandList.setTextureState(
        targets.depth.get(),
        ECSRenderDetail::s_FramebufferSubresources,
        Core::ResourceStates::ShaderResource
    );
}

void RendererAvboitSystem::renderAvboitPostOccupancyPreAccumulationPasses(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const CsgFrameState& csgFrameState,
    const MaterialPassDrawItemPartitions* const preparedExtinctionDrawItems,
    const CsgFrameGpuData* const preparedExtinctionCsgFrameData,
    const usize preparedExtinctionInstanceCount,
    const usize preparedExtinctionMaterialTypedByteCount
){
    AvboitFrameTargets& avboitTargets = targets.avboit;
    NWB_ASSERT(avboitTargets.valid());
    NWB_ASSERT(avboitState().m_depthWarpPipeline);
    NWB_ASSERT(avboitState().m_integratePipeline);

    dispatchAvboitDepthWarp(commandList, avboitTargets);
    renderAvboitExtinctionPass(
        commandList,
        avboitTargets,
        csgFrameState,
        preparedExtinctionDrawItems,
        preparedExtinctionCsgFrameData,
        preparedExtinctionInstanceCount,
        preparedExtinctionMaterialTypedByteCount
    );
    dispatchAvboitIntegration(commandList, avboitTargets);
}

void RendererAvboitSystem::renderAvboitPostOccupancyPasses(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const CsgFrameState& csgFrameState,
    const MaterialPassDrawItemPartitions* const preparedExtinctionDrawItems,
    const CsgFrameGpuData* const preparedExtinctionCsgFrameData,
    const usize preparedExtinctionInstanceCount,
    const usize preparedExtinctionMaterialTypedByteCount
){
    renderAvboitPostOccupancyPreAccumulationPasses(
        commandList,
        targets,
        csgFrameState,
        preparedExtinctionDrawItems,
        preparedExtinctionCsgFrameData,
        preparedExtinctionInstanceCount,
        preparedExtinctionMaterialTypedByteCount
    );
    renderAvboitAccumulatePass(commandList, targets, csgFrameState);
}

void RendererAvboitSystem::renderAvboitPasses(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const CsgFrameState& csgFrameState,
    const MaterialPassDrawItems* const preparedTransparentCsgReceiverSurfaceDrawItems,
    const CsgFrameGpuData* const preparedTransparentCsgFrameData,
    const usize preparedTransparentCsgInstanceCount,
    const usize preparedTransparentCsgMaterialTypedByteCount
){
    renderAvboitPreDepthWarpPasses(
        commandList,
        targets,
        csgFrameState,
        preparedTransparentCsgReceiverSurfaceDrawItems,
        preparedTransparentCsgFrameData,
        preparedTransparentCsgInstanceCount,
        preparedTransparentCsgMaterialTypedByteCount
    );
    renderAvboitPostOccupancyPasses(commandList, targets, csgFrameState);
}

void RendererAvboitSystem::dispatchAvboitDepthWarp(Core::CommandList& commandList, AvboitFrameTargets& targets){
    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_AvboitDepthWarp, graphics().getDevice(), commandList);

    // The graph records the coverage read and warp/control writes as packet-boundary state; this thunk contains only
    // the native dispatch itself.

    Core::GpuDescriptorHeap& heap = graphics().getDevice().getDescriptorHeap();

    __hidden_avboit::DispatchAvboitCompute(
        commandList,
        avboitState().m_depthWarpPipeline.get(),
        heap,
        targets,
        NWB_AVBOIT_DEPTH_WARP_DISPATCH_GROUP_COUNT_X,
        graphics().isHDR10OutputActive()
    );
}

void RendererAvboitSystem::dispatchAvboitIntegration(Core::CommandList& commandList, AvboitFrameTargets& targets){
    const u32 pixelCount = targets.lowWidth * targets.lowHeight;
    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_AvboitIntegration, graphics().getDevice(), commandList);

    // The graph records packed-extinction reads and the Texture3D UAV write as packet-boundary state; this thunk
    // contains only the native dispatch itself.

    Core::GpuDescriptorHeap& heap = graphics().getDevice().getDescriptorHeap();

    __hidden_avboit::DispatchAvboitCompute(
        commandList,
        avboitState().m_integratePipeline.get(),
        heap,
        targets,
        DivideUp(pixelCount, static_cast<u32>(NWB_AVBOIT_INTEGRATE_GROUP_SIZE_X)),
        graphics().isHDR10OutputActive()
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

