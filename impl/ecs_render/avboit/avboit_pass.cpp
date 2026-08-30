// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/avboit/avboit_private.h>

#include <impl/ecs_render/deferred/csg_interval_target_clear.h>

#include <impl/ecs_render/shared/renderer_frame_bindings.h>

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
        && !m_materialSystem.prepareMaterialPassResources(
            targets.framebuffer.get(),
            MaterialPipelinePass::CsgReceiverSurface,
            true,
            csgFrameState,
            nullptr
        )
    )
        return false;

    return
        m_materialSystem.prepareMaterialPassResources(
            avboitTargets.lowFramebuffer.get(),
            MaterialPipelinePass::AvboitOccupancy,
            true,
            csgFrameState,
            &avboitTargets
        )
        && m_materialSystem.prepareMaterialPassResources(
            avboitTargets.lowFramebuffer.get(),
            MaterialPipelinePass::AvboitExtinction,
            true,
            csgFrameState,
            &avboitTargets
        )
        && m_materialSystem.prepareMaterialPassResources(
            avboitTargets.accumulationFramebuffer.get(),
            MaterialPipelinePass::AvboitAccumulate,
            true,
            csgFrameState,
            &avboitTargets
        )
    ;
}

void RendererAvboitSystem::renderPreparedTransparentCsgIntervals(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const MaterialPassDrawItems& receiverSurfaceDrawItems,
    const CsgFrameGpuData& csgFrameData,
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
    const usize instanceCount,
    const usize materialTypedByteCount,
    const bool intervalTargetsGraphOwned,
    const bool receiverSurfaceImageStatesGraphOwned,
    const bool intervalPeelTargetStatesGraphOwned,
    const bool receiverSpanOutputImageStatesGraphOwned,
    const bool removedIntervalOutputImageStatesGraphOwned,
    const bool csgClipBufferStatesGraphOwned,
    const bool materialFrameStatesGraphOwned,
    const bool materialGeometryStatesGraphOwned,
    const bool deferIntervalCombine,
    Optional<Core::GpuTimingMeasure>* const deferredIntervalTiming
){
    if(
        !targets.framebuffer
        || receiverSurfaceDrawItems.empty()
        || !csgFrameData.hasWork()
        || !csgResources.frameReady(csgFrameData)
        || !frameBindings.bindingValid()
    )
        return;

    // Graph-owned peel states are valid only when the paired graph-owned clear omitted the legacy broad CopyDest
    // setup. Keep an externally mismatched compatibility call on the safe native bridge instead of claiming that
    // a ClearDestination peel image is already a StorageImage.
    NWB_ASSERT(!intervalPeelTargetStatesGraphOwned || intervalTargetsGraphOwned);
    NWB_ASSERT(!receiverSpanOutputImageStatesGraphOwned || intervalTargetsGraphOwned);
    NWB_ASSERT(!removedIntervalOutputImageStatesGraphOwned || intervalTargetsGraphOwned);
    NWB_ASSERT(!deferIntervalCombine || (intervalTargetsGraphOwned && deferredIntervalTiming));

    // The prepared graph's Span/Combine callbacks complete this interval in the same ordered Graphics packet.
    // Direct callers retain the local RAII scope, and malformed split requests safely fall back to the aggregate
    // route.
    const bool splitIntervalCombine = deferIntervalCombine && deferredIntervalTiming;
    const bool splitReceiverSpanBuild = splitIntervalCombine;
    Optional<Core::GpuTimingMeasure> localIntervalTiming;
    Optional<Core::GpuTimingMeasure>* const intervalTiming = splitIntervalCombine
        ? deferredIntervalTiming
        : &localIntervalTiming
    ;
    if(splitIntervalCombine && intervalTiming->has_value()){
        intervalTiming->value().discardTiming();
        intervalTiming->reset();
    }
    intervalTiming->emplace(
        m_graphics.gpuTiming(),
        RendererGpuTimingScope::s_TransparentCsgIntervals,
        m_graphics.getDevice(),
        commandList
    );
    // The normal prepared path records the two exact rect clears as preceding graph primitives. Retain the direct helper
    // for compatibility callers, including its historical all-target state preparation before readiness checks.
    if(!intervalTargetsGraphOwned){
        ClearDeferredCsgIntervalTargets(
            m_graphics,
            commandList,
            targets,
            csgFrameData.workRegion.resolveRect(targets.width, targets.height)
        );
    }

    // The graph copied the material and CSG bytes after preflight froze every selected handle.  Do not rebuild or
    // rewrite them here: a rejected packet will re-declare the retained blobs, while this native step only consumes
    // the graph-owned data.
    const bool drawBuffersReady = frameBindings.frameReady(instanceCount, materialTypedByteCount);
    const bool csgResourcesReady = csgResources.frameReady(csgFrameData);
    const bool receiverSurfaceDrawResourcesReady =
        m_materialSystem.materialPassDrawResourcesReady(receiverSurfaceDrawItems, frameBindings)
    ;
    if(!drawBuffersReady || !csgResourcesReady || !receiverSurfaceDrawResourcesReady){
        if(splitIntervalCombine){
            // No Combine callback will emit the endpoint when its producer skipped. Preserve the legacy short
            // interval instead of retaining an unfinished timestamp reservation across the packet.
            intervalTiming->value().finishMarker();
            intervalTiming->value().finishTiming(commandList);
            intervalTiming->reset();
        }
        return;
    }

    Core::ViewportState viewportState;
    viewportState
        .addViewport(targets.framebuffer->getFramebufferInfo().getViewport())
        .addScissorRect(csgFrameData.workRegion.resolveRect(targets.width, targets.height))
    ;

    m_csgSystem.dispatchCsgIntervalPeels(
        commandList,
        targets,
        csgFrameData,
        csgResources,
        frameBindings,
        intervalTargetsGraphOwned && intervalPeelTargetStatesGraphOwned,
        csgClipBufferStatesGraphOwned,
        materialFrameStatesGraphOwned
    );

    const MaterialPassDrawContext csgReceiverSurfaceDrawContext{
        commandList,
        targets,
        targets.framebuffer.get(),
        MaterialPipelinePass::CsgReceiverSurface,
        nullptr,
        viewportState,
        receiverSurfaceImageStatesGraphOwned,
        false,
        csgClipBufferStatesGraphOwned,
        materialFrameStatesGraphOwned,
        materialGeometryStatesGraphOwned,
        false,
        &csgResources,
        frameBindings
    };
    m_materialSystem.renderMaterialPassDrawItems(
        csgReceiverSurfaceDrawContext,
        receiverSurfaceDrawItems
    );

    if(!splitReceiverSpanBuild){
        m_csgSystem.dispatchCsgReceiverSpanBuild(
            commandList,
            targets,
            csgFrameData,
            csgResources,
            intervalTargetsGraphOwned && receiverSpanOutputImageStatesGraphOwned
        );
    }
    if(!splitIntervalCombine){
        m_csgSystem.dispatchCsgIntervalCombine(
            commandList,
            targets,
            csgFrameData,
            csgResources,
            intervalTargetsGraphOwned && removedIntervalOutputImageStatesGraphOwned
        );
    }
    else{
        // The task marker surrounding this callback must close before the following Combine graph task begins its
        // own marker. The timestamp endpoint remains open until that callback records.
        intervalTiming->value().finishMarker();
    }
    // A deferred span callback receives graph-lowered prologue barriers before it records. The receiver-surface
    // raster pass must therefore be closed even when the native span dispatch moved out of this callback.
    commandList.endRenderPass();
}

void RendererAvboitSystem::renderAvboitTransparentCsgIntervals(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const MaterialPassDrawItems* const preparedTransparentCsgReceiverSurfaceDrawItems,
    const CsgFrameGpuData* const preparedTransparentCsgFrameData,
    const ECSRenderDetail::CsgGraphResourceSnapshot* const preparedTransparentCsgResources,
    const ECSRenderDetail::MeshFrameBindingSnapshot* const preparedFrameBindings,
    const usize preparedTransparentCsgInstanceCount,
    const usize preparedTransparentCsgMaterialTypedByteCount,
    const bool preparedTransparentCsgIntervalTargetsGraphOwned,
    const bool preparedTransparentCsgReceiverSurfaceImageStatesGraphOwned,
    const bool preparedTransparentCsgIntervalPeelTargetStatesGraphOwned,
    const bool preparedTransparentCsgReceiverSpanOutputImageStatesGraphOwned,
    const bool preparedTransparentCsgRemovedIntervalOutputImageStatesGraphOwned,
    const bool preparedTransparentCsgClipBufferStatesGraphOwned,
    const bool preparedTransparentCsgMaterialFrameStatesGraphOwned,
    const bool preparedTransparentCsgMaterialGeometryStatesGraphOwned,
    const bool deferPreparedTransparentCsgIntervalCombine,
    Optional<Core::GpuTimingMeasure>* const deferredPreparedTransparentCsgIntervalTiming
){
    if(
        preparedTransparentCsgReceiverSurfaceDrawItems
        || preparedTransparentCsgFrameData
        || preparedTransparentCsgResources
        || preparedFrameBindings
    ){
        NWB_ASSERT(preparedTransparentCsgReceiverSurfaceDrawItems);
        NWB_ASSERT(preparedTransparentCsgFrameData);
        NWB_ASSERT(preparedTransparentCsgResources);
        NWB_ASSERT(preparedFrameBindings);
        if(
            preparedTransparentCsgReceiverSurfaceDrawItems
            && preparedTransparentCsgFrameData
            && preparedTransparentCsgResources
            && preparedFrameBindings
        ){
            renderPreparedTransparentCsgIntervals(
                commandList,
                targets,
                *preparedTransparentCsgReceiverSurfaceDrawItems,
                *preparedTransparentCsgFrameData,
                *preparedTransparentCsgResources,
                *preparedFrameBindings,
                preparedTransparentCsgInstanceCount,
                preparedTransparentCsgMaterialTypedByteCount,
                preparedTransparentCsgIntervalTargetsGraphOwned,
                preparedTransparentCsgReceiverSurfaceImageStatesGraphOwned,
                preparedTransparentCsgIntervalPeelTargetStatesGraphOwned,
                preparedTransparentCsgReceiverSpanOutputImageStatesGraphOwned,
                preparedTransparentCsgRemovedIntervalOutputImageStatesGraphOwned,
                preparedTransparentCsgClipBufferStatesGraphOwned,
                preparedTransparentCsgMaterialFrameStatesGraphOwned,
                preparedTransparentCsgMaterialGeometryStatesGraphOwned,
                deferPreparedTransparentCsgIntervalCombine,
                deferredPreparedTransparentCsgIntervalTiming
            );
        }
    }
}

void RendererAvboitSystem::renderAvboitOccupancyPass(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const MaterialPassDrawItemPartitions* const preparedOccupancyDrawItems,
    const CsgFrameGpuData* const preparedOccupancyCsgFrameData,
    const ECSRenderDetail::CsgGraphResourceSnapshot* const preparedOccupancyCsgResources,
    const ECSRenderDetail::MeshFrameBindingSnapshot* const preparedOccupancyFrameBindings,
    const usize preparedOccupancyInstanceCount,
    const usize preparedOccupancyMaterialTypedByteCount,
    const bool occupancyStatesGraphOwned,
    const bool occupancyCsgIntervalSampleImageStatesGraphOwned,
    const bool occupancyCsgClipBufferStatesGraphOwned,
    const bool occupancyMaterialFrameStatesGraphOwned,
    const bool occupancyMaterialGeometryStatesGraphOwned,
    const bool occupancyComputeEmulationOutputStatesGraphOwned,
    Optional<Core::GpuTimingMeasure>* const occupancyComputeEmulationTiming,
    const bool occupancyCsgComputeEmulationOutputStatesGraphOwned
){
    AvboitFrameTargets& avboitTargets = targets.avboit;
    NWB_ASSERT(avboitTargets.valid());
    NWB_ASSERT(m_avboitState.m_depthWarpPipeline);
    NWB_ASSERT(m_avboitState.m_integratePipeline);

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

    if(
        preparedOccupancyDrawItems
        || preparedOccupancyCsgFrameData
        || preparedOccupancyCsgResources
        || preparedOccupancyFrameBindings
    ){
        NWB_ASSERT(preparedOccupancyDrawItems);
        NWB_ASSERT(preparedOccupancyCsgFrameData);
        NWB_ASSERT(preparedOccupancyCsgResources);
        NWB_ASSERT(preparedOccupancyFrameBindings);
        if(
            preparedOccupancyDrawItems
            && preparedOccupancyCsgFrameData
            && preparedOccupancyCsgResources
            && preparedOccupancyFrameBindings
        ){
            m_materialSystem.renderPreparedMaterialPass(
                commandList,
                targets,
                avboitTargets.lowFramebuffer.get(),
                MaterialPipelinePass::AvboitOccupancy,
                &avboitTargets,
                *preparedOccupancyDrawItems,
                *preparedOccupancyCsgFrameData,
                *preparedOccupancyCsgResources,
                *preparedOccupancyFrameBindings,
                preparedOccupancyInstanceCount,
                preparedOccupancyMaterialTypedByteCount,
                occupancyCsgIntervalSampleImageStatesGraphOwned,
                occupancyCsgClipBufferStatesGraphOwned,
                occupancyMaterialFrameStatesGraphOwned,
                occupancyMaterialGeometryStatesGraphOwned,
                occupancyComputeEmulationOutputStatesGraphOwned,
                occupancyComputeEmulationTiming,
                occupancyCsgComputeEmulationOutputStatesGraphOwned
            );
        }
    }
    commandList.endRenderPass();
}

void RendererAvboitSystem::renderAvboitExtinctionPass(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const MaterialPassDrawItemPartitions* const preparedExtinctionDrawItems,
    const CsgFrameGpuData* const preparedExtinctionCsgFrameData,
    const ECSRenderDetail::CsgGraphResourceSnapshot* const preparedExtinctionCsgResources,
    const ECSRenderDetail::MeshFrameBindingSnapshot* const preparedExtinctionFrameBindings,
    const usize preparedExtinctionInstanceCount,
    const usize preparedExtinctionMaterialTypedByteCount,
    const bool extinctionCsgIntervalSampleImageStatesGraphOwned,
    const bool extinctionCsgClipBufferStatesGraphOwned,
    const bool extinctionMaterialFrameStatesGraphOwned,
    const bool extinctionMaterialGeometryStatesGraphOwned,
    const bool extinctionComputeEmulationOutputStatesGraphOwned,
    Optional<Core::GpuTimingMeasure>* const extinctionComputeEmulationTiming,
    const bool extinctionCsgComputeEmulationOutputStatesGraphOwned
){
    AvboitFrameTargets& avboitTargets = targets.avboit;
    NWB_ASSERT(avboitTargets.valid());

    // The graph records the warp/control reads and packed-extinction writes as packet-boundary state; this thunk
    // contains only the native raster pass.

    if(
        preparedExtinctionDrawItems
        || preparedExtinctionCsgFrameData
        || preparedExtinctionCsgResources
        || preparedExtinctionFrameBindings
    ){
        NWB_ASSERT(preparedExtinctionDrawItems);
        NWB_ASSERT(preparedExtinctionCsgFrameData);
        NWB_ASSERT(preparedExtinctionCsgResources);
        NWB_ASSERT(preparedExtinctionFrameBindings);
        if(
            preparedExtinctionDrawItems
            && preparedExtinctionCsgFrameData
            && preparedExtinctionCsgResources
            && preparedExtinctionFrameBindings
        ){
            m_materialSystem.renderPreparedMaterialPass(
                commandList,
                targets,
                avboitTargets.lowFramebuffer.get(),
                MaterialPipelinePass::AvboitExtinction,
                &avboitTargets,
                *preparedExtinctionDrawItems,
                *preparedExtinctionCsgFrameData,
                *preparedExtinctionCsgResources,
                *preparedExtinctionFrameBindings,
                preparedExtinctionInstanceCount,
                preparedExtinctionMaterialTypedByteCount,
                extinctionCsgIntervalSampleImageStatesGraphOwned,
                extinctionCsgClipBufferStatesGraphOwned,
                extinctionMaterialFrameStatesGraphOwned,
                extinctionMaterialGeometryStatesGraphOwned,
                extinctionComputeEmulationOutputStatesGraphOwned,
                extinctionComputeEmulationTiming,
                extinctionCsgComputeEmulationOutputStatesGraphOwned
            );
        }
    }
    commandList.endRenderPass();
}

void RendererAvboitSystem::renderAvboitAccumulatePass(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const MaterialPassDrawItemPartitions* const preparedAccumulationDrawItems,
    const CsgFrameGpuData* const preparedAccumulationCsgFrameData,
    const ECSRenderDetail::CsgGraphResourceSnapshot* const preparedAccumulationCsgResources,
    const ECSRenderDetail::MeshFrameBindingSnapshot* const preparedAccumulationFrameBindings,
    const usize preparedAccumulationInstanceCount,
    const usize preparedAccumulationMaterialTypedByteCount,
    const bool accumulationFinalStatesGraphOwned,
    const bool accumulationCsgIntervalSampleImageStatesGraphOwned,
    const bool accumulationCsgClipBufferStatesGraphOwned,
    const bool accumulationMaterialFrameStatesGraphOwned,
    const bool accumulationMaterialGeometryStatesGraphOwned,
    const bool accumulationComputeEmulationOutputStatesGraphOwned,
    Optional<Core::GpuTimingMeasure>* const accumulationComputeEmulationTiming,
    const bool accumulationCsgComputeEmulationOutputStatesGraphOwned
){
    AvboitFrameTargets& avboitTargets = targets.avboit;
    NWB_ASSERT(avboitTargets.valid());

    // The graph records the integrated volume and work-buffer reads as packet-boundary state; this thunk owns only
    // the native raster pass and its explicit final cross-graph transition below.

    if(
        preparedAccumulationDrawItems
        || preparedAccumulationCsgFrameData
        || preparedAccumulationCsgResources
        || preparedAccumulationFrameBindings
    ){
        NWB_ASSERT(preparedAccumulationDrawItems);
        NWB_ASSERT(preparedAccumulationCsgFrameData);
        NWB_ASSERT(preparedAccumulationCsgResources);
        NWB_ASSERT(preparedAccumulationFrameBindings);
        if(
            preparedAccumulationDrawItems
            && preparedAccumulationCsgFrameData
            && preparedAccumulationCsgResources
            && preparedAccumulationFrameBindings
        ){
            m_materialSystem.renderPreparedMaterialPass(
                commandList,
                targets,
                avboitTargets.accumulationFramebuffer.get(),
                MaterialPipelinePass::AvboitAccumulate,
                &avboitTargets,
                *preparedAccumulationDrawItems,
                *preparedAccumulationCsgFrameData,
                *preparedAccumulationCsgResources,
                *preparedAccumulationFrameBindings,
                preparedAccumulationInstanceCount,
                preparedAccumulationMaterialTypedByteCount,
                accumulationCsgIntervalSampleImageStatesGraphOwned,
                accumulationCsgClipBufferStatesGraphOwned,
                accumulationMaterialFrameStatesGraphOwned,
                accumulationMaterialGeometryStatesGraphOwned,
                accumulationComputeEmulationOutputStatesGraphOwned,
                accumulationComputeEmulationTiming,
                accumulationCsgComputeEmulationOutputStatesGraphOwned
            );
        }
    }
    commandList.endRenderPass();

    // Deferred composite is a Compute pass. The normal graph lowers the two accumulation attachment and read-only
    // depth transitions in its following Graphics finalizer, so later packets never name framebuffer attachment
    // source states. Direct callers retain the established bridge.
    if(!accumulationFinalStatesGraphOwned){
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
        commandList.setTextureState(
            targets.depth.get(),
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource
        );
    }
}

void RendererAvboitSystem::dispatchAvboitDepthWarp(
    Core::CommandList& commandList,
    AvboitFrameTargets& targets,
    const Core::GpuTimingSampleAttribution timingAttribution,
    bool* const timingRecorded
){
    Core::GpuTimingMeasure timing(
        m_graphics.gpuTiming(),
        RendererGpuTimingScope::s_AvboitDepthWarp,
        m_graphics.getDevice(),
        commandList,
        timingAttribution
    );
    if(timingRecorded)
        *timingRecorded = timing.valid();

    // The graph records the coverage read and warp/control writes as packet-boundary state; this thunk contains only
    // the native dispatch itself.

    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();

    __hidden_avboit::DispatchAvboitCompute(
        commandList,
        m_avboitState.m_depthWarpPipeline.get(),
        heap,
        targets,
        NWB_AVBOIT_DEPTH_WARP_DISPATCH_GROUP_COUNT_X,
        m_graphics.isHDR10OutputActive()
    );
}

void RendererAvboitSystem::dispatchAvboitIntegration(
    Core::CommandList& commandList,
    AvboitFrameTargets& targets,
    const Core::GpuTimingSampleAttribution timingAttribution,
    bool* const timingRecorded
){
    const u32 pixelCount = targets.lowWidth * targets.lowHeight;
    Core::GpuTimingMeasure timing(
        m_graphics.gpuTiming(),
        RendererGpuTimingScope::s_AvboitIntegration,
        m_graphics.getDevice(),
        commandList,
        timingAttribution
    );
    if(timingRecorded)
        *timingRecorded = timing.valid();

    // The graph records packed-extinction reads and the Texture3D UAV write as packet-boundary state; this thunk
    // contains only the native dispatch itself.

    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();

    __hidden_avboit::DispatchAvboitCompute(
        commandList,
        m_avboitState.m_integratePipeline.get(),
        heap,
        targets,
        DivideUp(pixelCount, static_cast<u32>(NWB_AVBOIT_INTEGRATE_GROUP_SIZE_X)),
        m_graphics.isHDR10OutputActive()
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

