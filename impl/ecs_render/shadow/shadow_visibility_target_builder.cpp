// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shadow_visibility_target_builder.h"

#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/shadow/light_space_shadow_diagnostics.adhd.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


b8 ShadowVisibilityTargetBuilder::build(DeferredFrameTargets& targets){
    if(targets.shadowReceiverFactor != s_ShadowReceiverFactorHalf && targets.shadowReceiverFactor != s_ShadowReceiverFactorQuarter)
        return false;
    // Deferred lighting always samples this per-slot transmittance target.
    targets.shadowVisibilityFormat = Core::Format::RGBA16_FLOAT;

    const auto createShadowTarget = [&](Core::TextureDesc desc, const Name& name, const tchar* const errorMessage, Core::TextureHandle& outTexture){
        desc.setName(name);
        auto result = m_graphics.createTexture(desc);
        if(!result){
            outTexture.reset();
            NWB_LOGGER_ERROR(errorMessage);
            return false;
        }
        outTexture = Move(*result);
        return true;
    };

    Core::TextureDesc visibilityDesc;
    visibilityDesc
        .setWidth(targets.width)
        .setHeight(targets.height)
        .setArraySize(NWB_SCENE_SHADOW_SLOT_COUNT)
        .setDimension(Core::TextureDimension::Texture2DArray)
        .setFormat(targets.shadowVisibilityFormat)
        .setIsUAV(true)
        // The lagged-history copy may run on Transfer after producers finish. Keep all three transports shareable.
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
    ;
    if(!createShadowTarget(visibilityDesc, "engine/shadow/visibility", NWB_TEXT("RendererSystem: failed to create shadow visibility target"), targets.shadowVisibility))
        return false;

    // Round up half-resolution scratch so odd extents retain coverage.
    targets.shadowCoarseTransmittanceFormat = Core::Format::RGBA16_FLOAT;
    Core::TextureDesc coarseDesc;
    coarseDesc
        .setWidth(DivideUpWrapping(targets.width, NWB_SW_SHADOW_COARSE_FACTOR))
        .setHeight(DivideUpWrapping(targets.height, NWB_SW_SHADOW_COARSE_FACTOR))
        .setArraySize(NWB_SCENE_SHADOW_SLOT_COUNT)
        .setDimension(Core::TextureDimension::Texture2DArray)
        .setFormat(targets.shadowCoarseTransmittanceFormat)
        .setIsUAV(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
    ;
    if(!createShadowTarget(coarseDesc, "engine/shadow/coarse_transmittance", NWB_TEXT("RendererSystem: failed to create shadow coarse transmittance target"), targets.shadowCoarseTransmittance))
        return false;

    // Ping-pong half-resolution soft visibility and geometry cache.
    targets.shadowSoftFormat = Core::Format::RGBA16_FLOAT;
    targets.shadowSoftGeometryFormat = Core::Format::RGBA16_FLOAT;
    const u32 softHalfWidth = DivideUpWrapping(targets.width, targets.shadowReceiverFactor);
    const u32 softHalfHeight = DivideUpWrapping(targets.height, targets.shadowReceiverFactor);

    Core::TextureDesc softHalfDesc;
    softHalfDesc
        .setWidth(softHalfWidth)
        .setHeight(softHalfHeight)
        .setArraySize(NWB_SCENE_SHADOW_SLOT_COUNT)
        .setDimension(Core::TextureDimension::Texture2DArray)
        .setFormat(targets.shadowSoftFormat)
        .setIsUAV(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
    ;
    Core::TextureDesc softGeometryDesc;
    softGeometryDesc
        .setWidth(softHalfWidth)
        .setHeight(softHalfHeight)
        .setFormat(targets.shadowSoftGeometryFormat)
        .setIsUAV(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
    ;

    // Recreated history must not reproject through the previous target's matrix.
    if(
        !createShadowTarget(softHalfDesc, "engine/shadow/soft_half_a", NWB_TEXT("RendererSystem: failed to create soft shadow half-A target"), targets.shadowSoftHalfA)
        || !createShadowTarget(softHalfDesc, "engine/shadow/soft_half_b", NWB_TEXT("RendererSystem: failed to create soft shadow half-B target"), targets.shadowSoftHalfB)
        || !createShadowTarget(softGeometryDesc, "engine/shadow/soft_geometry", NWB_TEXT("RendererSystem: failed to create soft shadow geometry cache target"), targets.shadowSoftGeometry)
        || !createShadowTarget(softHalfDesc, "engine/shadow/hist_a", NWB_TEXT("RendererSystem: failed to create soft shadow temporal history-A target"), targets.shadowHistA)
        || !createShadowTarget(softHalfDesc, "engine/shadow/hist_b", NWB_TEXT("RendererSystem: failed to create soft shadow temporal history-B target"), targets.shadowHistB)
        || !createShadowTarget(softHalfDesc, "engine/shadow/moments_a", NWB_TEXT("RendererSystem: failed to create soft shadow temporal moments-A target"), targets.shadowMomentsA)
        || !createShadowTarget(softHalfDesc, "engine/shadow/moments_b", NWB_TEXT("RendererSystem: failed to create soft shadow temporal moments-B target"), targets.shadowMomentsB)
        || !createShadowTarget(softGeometryDesc, "engine/shadow/soft_geometry_prev", NWB_TEXT("RendererSystem: failed to create soft shadow previous-frame geometry cache target"), targets.shadowSoftGeometryPrev)
    )
        return false;

    m_rayTracingState.m_softwareTransparentSampling.m_history.discard();
    m_rayTracingState.m_transparentShadowSamplingHistory.reset();
    ResetSoftShadowTemporalHistoryState(
        m_rayTracingState.m_softShadowTemporalSeeded,
        m_rayTracingState.m_softShadowTemporalHistoryAdvancePending,
        m_rayTracingState.m_prevWorldToClipValid,
        m_rayTracingState.m_softShadowHistoryFrontIsA
    );

    // Transparent temporal history shares geometry and the opaque history selector.
    if(
        !createShadowTarget(softHalfDesc, "engine/shadow/transparent_soft_half", NWB_TEXT("RendererSystem: failed to create soft transparent shadow half target"), targets.transparentSoftHalf)
        || !createShadowTarget(softHalfDesc, "engine/shadow/transparent_hist_a", NWB_TEXT("RendererSystem: failed to create soft transparent shadow history-A target"), targets.transparentHistA)
        || !createShadowTarget(softHalfDesc, "engine/shadow/transparent_hist_b", NWB_TEXT("RendererSystem: failed to create soft transparent shadow history-B target"), targets.transparentHistB)
        || !createShadowTarget(softHalfDesc, "engine/shadow/transparent_moments_a", NWB_TEXT("RendererSystem: failed to create soft transparent shadow moments-A target"), targets.transparentMomentsA)
        || !createShadowTarget(softHalfDesc, "engine/shadow/transparent_moments_b", NWB_TEXT("RendererSystem: failed to create soft transparent shadow moments-B target"), targets.transparentMomentsB)
    )
        return false;

    // All resolve allocations belong to this target generation. Verify both history selectors once, before publication.
    for(u32 selector = 0u; selector < s_ShadowHistorySelectorCount; ++selector){
        const b8 frontIsA = selector != 0u;
        const SoftShadowCombinedWaveletInputs waveletInputs{
            .opaqueHistory = frontIsA ? targets.shadowHistB.get() : targets.shadowHistA.get(),
            .opaqueMoments = frontIsA ? targets.shadowMomentsB.get() : targets.shadowMomentsA.get(),
            .transparentHistory = frontIsA ? targets.transparentHistB.get() : targets.transparentHistA.get(),
            .transparentMoments = frontIsA ? targets.transparentMomentsB.get() : targets.transparentMomentsA.get(),
            .geometry = targets.shadowSoftGeometry.get(),
            .opaqueOutput = targets.shadowSoftHalfB.get(),
            .transparentOutput = targets.shadowSoftHalfA.get(),
        };
        if(!waveletInputs.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: soft-shadow target allocation produced aliased resolve resources"));
            return false;
        }
    }

    // Edge records are capped at one per pixel; overflow falls back to interpolation.
    const Expected<u32> edgeListCapacityResult = TryResolveShadowEdgeListCapacityRecords(targets.width, targets.height);
    if(!edgeListCapacityResult)
        return false;
    const u32 edgeListCapacityRecords = *edgeListCapacityResult;
    Core::BufferDesc edgeListDesc;
    edgeListDesc
        .setByteSize(static_cast<u64>(sizeof(u32)) * static_cast<u64>(NWB_SW_SHADOW_EDGE_RECORD_WORDS) * static_cast<u64>(edgeListCapacityRecords))
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(Name("sw_shadow_edge_list"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    auto edgeListBufferResult = m_graphics.createBuffer(edgeListDesc);
    if(!edgeListBufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create SW shadow edge-list buffer"));
        m_rayTracingState.m_swShadowEdgeListCapacity = 0u;
        return false;
    }
    Core::BufferHandle edgeListBuffer = Move(*edgeListBufferResult);
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized() || !RayTracingDetail::ReplaceHeapBuffer(Core::GpuDescriptorClass::StorageBuffer, true, heap, *edgeListBuffer.get(), m_rayTracingState.m_swShadowEdgeListHeapHandle)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register SW shadow edge-list buffer in the descriptor heap"));
        m_rayTracingState.m_swShadowEdgeListCapacity = 0u;
        return false;
    }
    m_rayTracingState.m_swShadowEdgeListBuffer = Move(edgeListBuffer);
    m_rayTracingState.m_swShadowEdgeListCapacity = edgeListCapacityRecords;

    LogCreatedShadowReceiverGrid(targets.shadowReceiverFactor, targets.width, targets.height, softHalfWidth, softHalfHeight);

    return true;
}

NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
