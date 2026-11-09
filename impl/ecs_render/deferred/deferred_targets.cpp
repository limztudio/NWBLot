// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deferred_system.h"
#include <impl/ecs_render/csg/format_private.h>
#include <impl/ecs_render/csg/work_region.adhd.h>
#include <impl/ecs_render/deferred/format_private.h>
#include <impl/ecs_render/deferred/renderer_deferred_state.h>
#include <impl/ecs_render/kernel/renderer_constants_private.h>
#include <impl/assets/graphics/mesh/runtime_constants.h>
#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>
#include "deferred_bindless_registrar.h"
#include <impl/ecs_render/caustic/rt_caustic_state.h>
#include <impl/ecs_render/kernel/descriptor_register_utils.h>
#include <impl/ecs_render/shadow/rt_shadow_state.h>
#include <impl/ecs_render/deferred/deferred_bindless_slots_builder.adhd.h>
#include "deferred_frame_target_builder.h"
#include <impl/ecs_render/avboit/avboit_system.h>
#include <impl/ecs_render/csg/csg_system.h>
#include <impl/ecs_render/raytrace/raytracing_system.h>
#include <impl/ecs_render/shadow/shadow_visibility_target_builder.h>
#include <global/atomic_identity.h>
#include <impl/assets_graphics/deferred_subsystem.adhd.h>
#include <impl/ecs_render/deferred/scene_shading_upload_tracker.adhd.h>
#include <impl/ecs_render/deferred/deferred_graph_private.h>
#include <impl/ecs_render/kernel/compute_pipeline_utils.h>
#include <impl/ecs_render/shadow/types.adhd.h>
#include <impl/ecs_render/kernel/renderer_format_private.h>
#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/ecs_render/shader/shader_system.h>
#include <impl/ecs_render/shared/renderer_scene_private.h>
#include <impl/assets/graphics/deferred/binding_slots.h>
#include <impl/assets_graphics/components.adhd.h>
#include <core/graphics/shader_archive.h>
#include <global/algorithm.h>
#include <impl/ecs_render/deferred/presentation_settings.adhd.h>
#include <impl/ecs_render/material/renderer_render_state_private.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::Format::Enum SelectGBufferAlbedoFormat(Core::Device& device)noexcept{
    constexpr Array<Core::Format::Enum, 1> s_Candidates = { Core::Format::RGBA16_FLOAT };
    constexpr Core::FormatSupport::Mask s_RequiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::RenderTarget;

    return SelectSupportedFormat(device, s_Candidates, s_RequiredSupport);
}

Core::Format::Enum SelectDeferredOpaqueColorFormat(Core::Device& device)noexcept{
    constexpr Array<Core::Format::Enum, 1> s_Candidates = { Core::Format::RGBA16_FLOAT };
    constexpr Core::FormatSupport::Mask s_RequiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::ShaderUavStore;

    return SelectSupportedFormat(device, s_Candidates, s_RequiredSupport);
}

Core::Format::Enum SelectGBufferDepthFormat(Core::Device& device)noexcept{
    constexpr Array<Core::Format::Enum, 3> s_Candidates = { Core::Format::D32, Core::Format::D24S8, Core::Format::D16 };

    return SelectSupportedFormat(device, s_Candidates, Core::FormatSupport::DepthStencil);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererDeferredState::invalidateResources()noexcept{
    m_lightingBindingLayout.reset();
    m_sceneShadingBuffer.reset();
    m_lightBuffer.reset();
    m_lightingComputeShader.reset();
    m_lightingPipeline.reset();
    m_compositeComputeBindingLayout.reset();
    m_compositeComputeShader.reset();
    m_compositeComputePipeline.reset();
    m_presentBindingLayout.reset();
    m_sampler.reset();
    m_presentPixelShader.reset();
    m_presentPipeline.reset();
    m_sceneShadingGpuDataValid = false;
    m_lightGpuDataCount = 0u;
    m_lightGpuDataValid = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 DeferredBindlessFrameResources::valid()const noexcept{
    return
        slotsBuffer != nullptr
        && slotsBufferDescriptor.valid()
        && gbufferBaseColor.valid()
        && gbufferNormal.valid()
        && gbufferWorldPosition.valid()
        && gbufferSpecularRoughness.valid()
        && gbufferDepth.valid()
        && shadowVisibility.valid()
        && shadowVisibilityStorage.valid()
        && causticIrradiance.valid()
        && causticIrradianceStorage.valid()
        && surfelIrradiance.valid()
        && surfelIrradianceStorage.valid()
        && surfelIrradianceHalf.valid()
        && surfelIrradianceHalfStorage.valid()
        && sampler.valid()
        && opaqueColor.valid()
        && opaqueColorStorage.valid()
        && compositeColor.valid()
        && compositeColorStorage.valid()
        && avboitAccumColor.valid()
        && avboitAccumExtinction.valid()
        && avboitTransmittance.valid()
        && avboitLinearSampler.valid()
        && sceneShading.valid()
        && lightList.valid()
        && causticAccumulator.valid()
        && causticAccumulatorStorage.valid()
        && causticHistory.valid()
        && causticHistoryStorage.valid()
        && causticResolveHalf.valid()
        && causticResolveHalfStorage.valid()
        && causticResolveGeometry.valid()
        && causticResolveGeometryStorage.valid()
        && shadowCoarseTransmittanceStorage.valid()
        && shadowSoftGeometry.valid()
        && shadowSoftGeometryStorage.valid()
        && shadowSoftGeometryPrev.valid()
        && shadowSoftGeometryPrevStorage.valid()
        && shadowSoftHalfA.valid()
        && shadowSoftHalfAStorage.valid()
        && shadowSoftHalfB.valid()
        && shadowSoftHalfBStorage.valid()
        && shadowHistA.valid()
        && shadowHistAStorage.valid()
        && shadowHistB.valid()
        && shadowHistBStorage.valid()
        && shadowMomentsA.valid()
        && shadowMomentsAStorage.valid()
        && shadowMomentsB.valid()
        && shadowMomentsBStorage.valid()
        && transparentSoftHalf.valid()
        && transparentSoftHalfStorage.valid()
        && transparentHistA.valid()
        && transparentHistAStorage.valid()
        && transparentHistB.valid()
        && transparentHistBStorage.valid()
        && transparentMomentsA.valid()
        && transparentMomentsAStorage.valid()
        && transparentMomentsB.valid()
        && transparentMomentsBStorage.valid()
        && csgCapBackNormal.valid()
        && csgIntervalDepth.valid()
        && csgIntervalId.valid()
        && csgReceiverEventData.valid()
        && csgReceiverEventCount.valid()
        && csgReceiverSpanData.valid()
        && csgReceiverSpanCount.valid()
        && csgRemovedIntervalDepth.valid()
        && csgRemovedIntervalCapNormal.valid()
        && csgRemovedIntervalData.valid()
        && csgRemovedIntervalCount.valid()
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 DeferredLaggedLightingHistoryResources::valid()const noexcept{
    return
        shadowVisibility != nullptr
        && causticIrradiance != nullptr
        && surfelIrradiance != nullptr
        && slotsBuffer != nullptr
        && slotsBufferDescriptor.valid()
        && shadowVisibilityDescriptor.valid()
        && causticIrradianceDescriptor.valid()
        && surfelIrradianceDescriptor.valid()
        && generation != 0u
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ResetCsgIntervalTargets(DeferredFrameTargets& targets)noexcept{
    targets.csgCapBackNormal.reset();
    targets.csgIntervalDepth.reset();
    targets.csgIntervalId.reset();
    targets.csgReceiverEventData.reset();
    targets.csgReceiverEventCount.reset();
    targets.csgReceiverSpanData.reset();
    targets.csgReceiverSpanCount.reset();
    targets.csgRemovedIntervalDepth.reset();
    targets.csgRemovedIntervalCapNormal.reset();
    targets.csgRemovedIntervalData.reset();
    targets.csgRemovedIntervalCount.reset();
}

b8 DeferredFrameTargets::csgIntervalTargetsValid()const noexcept{
    return
        csgCapNormalFormat != Core::Format::UNKNOWN
        && csgIntervalDepthFormat != Core::Format::UNKNOWN
        && csgIntervalIdFormat != Core::Format::UNKNOWN
        && csgReceiverEventDataFormat != Core::Format::UNKNOWN
        && csgReceiverEventCountFormat != Core::Format::UNKNOWN
        && csgReceiverSpanDataFormat != Core::Format::UNKNOWN
        && csgReceiverSpanCountFormat != Core::Format::UNKNOWN
        && csgRemovedIntervalDepthFormat != Core::Format::UNKNOWN
        && csgRemovedIntervalCapNormalFormat != Core::Format::UNKNOWN
        && csgRemovedIntervalDataFormat != Core::Format::UNKNOWN
        && csgRemovedIntervalCountFormat != Core::Format::UNKNOWN
        && csgPeelLayerCount > 0u
        && csgReceiverEventLayerCount > 0u
        && csgReceiverSpanLayerCount > 0u
        && csgRemovedIntervalLayerCount > 0u
        && csgCapBackNormal != nullptr
        && csgIntervalDepth != nullptr
        && csgIntervalId != nullptr
        && csgReceiverEventData != nullptr
        && csgReceiverEventCount != nullptr
        && csgReceiverSpanData != nullptr
        && csgReceiverSpanCount != nullptr
        && csgRemovedIntervalDepth != nullptr
        && csgRemovedIntervalCapNormal != nullptr
        && csgRemovedIntervalData != nullptr
        && csgRemovedIntervalCount != nullptr
    ;
}

b8 DeferredFrameTargets::valid()const noexcept{
#if defined(NWB_DEBUG)
    return
        width > 0
        && height > 0
        && albedoFormat != Core::Format::UNKNOWN
        && normalFormat != Core::Format::UNKNOWN
        && worldPositionFormat != Core::Format::UNKNOWN
        && specularRoughnessFormat != Core::Format::UNKNOWN
        && opaqueColorFormat != Core::Format::UNKNOWN
        && compositeColorFormat != Core::Format::UNKNOWN
        && depthFormat != Core::Format::UNKNOWN
        && csgIntervalTargetsValid()
        && albedo != nullptr
        && normal != nullptr
        && worldPosition != nullptr
        && specularRoughness != nullptr
        && opaqueColor != nullptr
        && compositeColor != nullptr
        && depth != nullptr
        && framebuffer != nullptr
        && bindless.valid()
        && avboit.valid()
    ;
#else
    return framebuffer != nullptr;
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererDeferredSystem::invalidateSceneLightingUploadMirrors()noexcept{
    m_deferredState.m_sceneShadingGpuDataValid = false;
    m_deferredState.m_lightGpuDataValid = false;
}

b8 RendererDeferredSystem::setPresentationSettings(const PresentationSettings& settings)noexcept{
    if(!ValidatePresentationSettings(settings))
        return false;
    m_deferredState.m_presentationSettings = settings;
    return true;
}

Expected<DeferredFrameTargets> RendererDeferredSystem::createDeferredFrameTargets(const u32 width, const u32 height){
    if(width == 0 || height == 0)
        return MakeUnexpected(Failure{});

    auto& device = m_graphics.getDevice();
    const Core::Format::Enum albedoFormat = ECSRenderDetail::SelectGBufferAlbedoFormat(device);
    const Core::Format::Enum normalFormat = ECSRenderDetail::SelectGBufferAlbedoFormat(device);
    const Core::Format::Enum worldPositionFormat = ECSRenderDetail::SelectGBufferAlbedoFormat(device);
    const Core::Format::Enum opaqueColorFormat = ECSRenderDetail::SelectDeferredOpaqueColorFormat(device);
    const Core::Format::Enum depthFormat = ECSRenderDetail::SelectGBufferDepthFormat(device);
    const Core::Format::Enum csgCapNormalFormat = ECSRenderDetail::SelectCsgFormat(NWB_CSG_CAP_NORMAL_CORE_FORMAT, ECSRenderDetail::s_CsgPeelFormatSupport, device);
    const Core::Format::Enum csgIntervalDepthFormat = ECSRenderDetail::SelectCsgFormat(NWB_CSG_INTERVAL_DEPTH_CORE_FORMAT, ECSRenderDetail::s_CsgPeelFormatSupport, device);
    const Core::Format::Enum csgIntervalIdFormat = ECSRenderDetail::SelectCsgFormat(NWB_CSG_INTERVAL_ID_CORE_FORMAT, ECSRenderDetail::s_CsgPeelFormatSupport, device);
    const Core::Format::Enum csgReceiverEventDataFormat = ECSRenderDetail::SelectCsgFormat(NWB_CSG_RECEIVER_EVENT_DATA_CORE_FORMAT, ECSRenderDetail::s_CsgStorageFormatSupport, device);
    const Core::Format::Enum csgReceiverEventCountFormat = ECSRenderDetail::SelectCsgFormat(NWB_CSG_RECEIVER_EVENT_COUNT_CORE_FORMAT, ECSRenderDetail::s_CsgStorageFormatSupport, device);
    const Core::Format::Enum csgReceiverSpanDataFormat = ECSRenderDetail::SelectCsgFormat(NWB_CSG_RECEIVER_SPAN_DATA_CORE_FORMAT, ECSRenderDetail::s_CsgStorageFormatSupport, device);
    const Core::Format::Enum csgReceiverSpanCountFormat = ECSRenderDetail::SelectCsgFormat(NWB_CSG_RECEIVER_SPAN_COUNT_CORE_FORMAT, ECSRenderDetail::s_CsgStorageFormatSupport, device);
    const Core::Format::Enum csgRemovedIntervalDepthFormat = ECSRenderDetail::SelectCsgFormat(NWB_CSG_REMOVED_INTERVAL_DEPTH_CORE_FORMAT, ECSRenderDetail::s_CsgStorageFormatSupport, device);
    const Core::Format::Enum csgRemovedIntervalCapNormalFormat = ECSRenderDetail::SelectCsgFormat(NWB_CSG_REMOVED_INTERVAL_CAP_NORMAL_CORE_FORMAT, ECSRenderDetail::s_CsgStorageFormatSupport, device);
    const Core::Format::Enum csgRemovedIntervalDataFormat = ECSRenderDetail::SelectCsgFormat(NWB_CSG_REMOVED_INTERVAL_DATA_CORE_FORMAT, ECSRenderDetail::s_CsgStorageFormatSupport, device);
    const Core::Format::Enum csgRemovedIntervalCountFormat = ECSRenderDetail::SelectCsgFormat(NWB_CSG_REMOVED_INTERVAL_COUNT_CORE_FORMAT, ECSRenderDetail::s_CsgStorageFormatSupport, device);
    if(
        albedoFormat == Core::Format::UNKNOWN
        || normalFormat == Core::Format::UNKNOWN
        || worldPositionFormat == Core::Format::UNKNOWN
        || opaqueColorFormat == Core::Format::UNKNOWN
        || depthFormat == Core::Format::UNKNOWN
        || csgCapNormalFormat == Core::Format::UNKNOWN
        || csgIntervalDepthFormat == Core::Format::UNKNOWN
        || csgIntervalIdFormat == Core::Format::UNKNOWN
        || csgReceiverEventDataFormat == Core::Format::UNKNOWN
        || csgReceiverEventCountFormat == Core::Format::UNKNOWN
        || csgReceiverSpanDataFormat == Core::Format::UNKNOWN
        || csgReceiverSpanCountFormat == Core::Format::UNKNOWN
        || csgRemovedIntervalDepthFormat == Core::Format::UNKNOWN
        || csgRemovedIntervalCapNormalFormat == Core::Format::UNKNOWN
        || csgRemovedIntervalDataFormat == Core::Format::UNKNOWN
        || csgRemovedIntervalCountFormat == Core::Format::UNKNOWN
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to find supported deferred framebuffer formats"));
        return MakeUnexpected(Failure{});
    }
    if(!createDeferredLightingResources())
        return MakeUnexpected(Failure{});
    if(!createDeferredCompositeResources())
        return MakeUnexpected(Failure{});

    m_deferredState.m_lightingPipeline.reset();
    m_deferredState.m_compositeComputePipeline.reset();
    m_deferredState.m_presentPipeline.reset();

    DeferredFrameTargets createdTargets;
    createdTargets.width = width;
    createdTargets.height = height;
    createdTargets.albedoFormat = albedoFormat;
    createdTargets.normalFormat = normalFormat;
    createdTargets.worldPositionFormat = worldPositionFormat;
    createdTargets.specularRoughnessFormat = worldPositionFormat;
    createdTargets.opaqueColorFormat = opaqueColorFormat;
    createdTargets.compositeColorFormat = opaqueColorFormat;
    createdTargets.depthFormat = depthFormat;
    createdTargets.csgCapNormalFormat = csgCapNormalFormat;
    createdTargets.csgIntervalDepthFormat = csgIntervalDepthFormat;
    createdTargets.csgIntervalIdFormat = csgIntervalIdFormat;
    createdTargets.csgReceiverEventDataFormat = csgReceiverEventDataFormat;
    createdTargets.csgReceiverEventCountFormat = csgReceiverEventCountFormat;
    createdTargets.csgReceiverSpanDataFormat = csgReceiverSpanDataFormat;
    createdTargets.csgReceiverSpanCountFormat = csgReceiverSpanCountFormat;
    createdTargets.csgRemovedIntervalDepthFormat = csgRemovedIntervalDepthFormat;
    createdTargets.csgRemovedIntervalCapNormalFormat = csgRemovedIntervalCapNormalFormat;
    createdTargets.csgRemovedIntervalDataFormat = csgRemovedIntervalDataFormat;
    createdTargets.csgRemovedIntervalCountFormat = csgRemovedIntervalCountFormat;
    createdTargets.csgPeelLayerCount = s_CsgPeelLayerCount;
    createdTargets.csgReceiverEventLayerCount = s_CsgReceiverEventLayerCount;
    createdTargets.csgReceiverSpanLayerCount = s_CsgReceiverSpanLayerCount;
    createdTargets.csgRemovedIntervalLayerCount = s_CsgRemovedIntervalLayerCount;

    Core::TextureDesc albedoDesc;
    albedoDesc
        .setWidth(createdTargets.width)
        .setHeight(createdTargets.height)
        .setFormat(createdTargets.albedoFormat)
        .setIsRenderTarget(true)
        // AsyncCompute lighting samples every G-buffer attachment after Graphics AVBOIT; keep their sharing concurrent.
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).setName("engine/deferred/gbuffer_albedo").setClearValue(ECSRenderDetail::s_ClearColor)
    ;
    auto albedoResult = m_graphics.createTexture(albedoDesc);
    if(albedoResult)
        createdTargets.albedo = Move(*albedoResult);
    else
        createdTargets.albedo.reset();
    if(!albedoResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred albedo target"));
        return MakeUnexpected(Failure{});
    }

    Core::TextureDesc normalDesc;
    normalDesc
        .setWidth(createdTargets.width)
        .setHeight(createdTargets.height)
        .setFormat(createdTargets.normalFormat)
        .setIsRenderTarget(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setName("engine/deferred/gbuffer_normal")
        .setClearValue(ECSRenderDetail::s_GBufferNormalClearColor)
    ;
    auto normalResult = m_graphics.createTexture(normalDesc);
    if(normalResult)
        createdTargets.normal = Move(*normalResult);
    else
        createdTargets.normal.reset();
    if(!normalResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred normal target"));
        return MakeUnexpected(Failure{});
    }

    Core::TextureDesc worldPositionDesc;
    worldPositionDesc
        .setWidth(createdTargets.width)
        .setHeight(createdTargets.height)
        .setFormat(createdTargets.worldPositionFormat)
        .setIsRenderTarget(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setName("engine/deferred/gbuffer_world_position")
        .setClearValue(ECSRenderDetail::s_GBufferWorldPositionClearColor)
    ;
    auto worldPositionResult = m_graphics.createTexture(worldPositionDesc);
    if(worldPositionResult)
        createdTargets.worldPosition = Move(*worldPositionResult);
    else
        createdTargets.worldPosition.reset();
    if(!worldPositionResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred world-position target"));
        return MakeUnexpected(Failure{});
    }

    Core::TextureDesc specularRoughnessDesc;
    specularRoughnessDesc
        .setWidth(createdTargets.width)
        .setHeight(createdTargets.height)
        .setFormat(createdTargets.specularRoughnessFormat)
        .setIsRenderTarget(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setName("engine/deferred/gbuffer_specular_roughness")
        .setClearValue(ECSRenderDetail::s_GBufferSpecularRoughnessClearColor)
    ;
    auto specularRoughnessResult = m_graphics.createTexture(specularRoughnessDesc);
    if(specularRoughnessResult)
        createdTargets.specularRoughness = Move(*specularRoughnessResult);
    else
        createdTargets.specularRoughness.reset();
    if(!specularRoughnessResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred specular/roughness target"));
        return MakeUnexpected(Failure{});
    }

    Core::TextureDesc opaqueColorDesc;
    opaqueColorDesc
        .setWidth(createdTargets.width)
        .setHeight(createdTargets.height)
        .setFormat(createdTargets.opaqueColorFormat)
        .setIsUAV(true)
        .setInitialState(Core::ResourceStates::Common)
        .setKeepInitialState(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setName("engine/deferred/opaque_color")
        .setClearValue(ECSRenderDetail::s_ClearColor)
    ;
    auto opaqueColorResult = m_graphics.createTexture(opaqueColorDesc);
    if(opaqueColorResult)
        createdTargets.opaqueColor = Move(*opaqueColorResult);
    else
        createdTargets.opaqueColor.reset();
    if(!opaqueColorResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred opaque color target"));
        return MakeUnexpected(Failure{});
    }

    Core::TextureDesc compositeColorDesc;
    compositeColorDesc
        .setWidth(createdTargets.width)
        .setHeight(createdTargets.height)
        .setFormat(createdTargets.compositeColorFormat)
        .setIsUAV(true)
        .setInitialState(Core::ResourceStates::Common)
        .setKeepInitialState(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setName("engine/deferred/composite_color")
        .setClearValue(ECSRenderDetail::s_ClearColor)
    ;
    auto compositeColorResult = m_graphics.createTexture(compositeColorDesc);
    if(compositeColorResult)
        createdTargets.compositeColor = Move(*compositeColorResult);
    else
        createdTargets.compositeColor.reset();
    if(!compositeColorResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred composite color target"));
        return MakeUnexpected(Failure{});
    }

    Core::TextureDesc depthDesc;
    depthDesc.setWidth(createdTargets.width).setHeight(createdTargets.height).setFormat(createdTargets.depthFormat).setIsRenderTarget(true).setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).setName("engine/deferred/depth");
    auto depthResult = m_graphics.createTexture(depthDesc);
    if(depthResult)
        createdTargets.depth = Move(*depthResult);
    else
        createdTargets.depth.reset();
    if(!depthResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred depth target"));
        return MakeUnexpected(Failure{});
    }

    Array<Core::FramebufferAttachment, NWB_MESH_GBUFFER_TARGET_COUNT> gbufferAttachments = {};
    gbufferAttachments[NWB_MESH_GBUFFER_BASE_COLOR_LOCATION].setTexture(createdTargets.albedo.get()).setSubresources(ECSRenderDetail::s_FramebufferSubresources);
    gbufferAttachments[NWB_MESH_GBUFFER_NORMAL_LOCATION].setTexture(createdTargets.normal.get()).setSubresources(ECSRenderDetail::s_FramebufferSubresources);
    gbufferAttachments[NWB_MESH_GBUFFER_WORLD_POSITION_LOCATION].setTexture(createdTargets.worldPosition.get()).setSubresources(ECSRenderDetail::s_FramebufferSubresources);
    gbufferAttachments[NWB_MESH_GBUFFER_SPECULAR_ROUGHNESS_LOCATION].setTexture(createdTargets.specularRoughness.get()).setSubresources(ECSRenderDetail::s_FramebufferSubresources);
    Core::FramebufferDesc framebufferDesc;
    for(const Core::FramebufferAttachment& attachment : gbufferAttachments)
        framebufferDesc.addColorAttachment(attachment);
    framebufferDesc.setDepthAttachment(createdTargets.depth.get(), ECSRenderDetail::s_FramebufferSubresources);
    auto framebufferResult = device.createFramebuffer(framebufferDesc);
    if(framebufferResult)
        createdTargets.framebuffer = Move(*framebufferResult);
    else
        createdTargets.framebuffer.reset();
    if(!framebufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred framebuffer"));
        return MakeUnexpected(Failure{});
    }

    return createdTargets;
}

b8 RendererDeferredSystem::createDeferredFrameTargetResources(DeferredFrameTargets& targets, Core::Sampler& avboitLinearSampler){
    if(!createDeferredBindlessFrameResources(targets, avboitLinearSampler))
        return false;
    if(createLaggedLightingHistoryResources(targets))
        return true;
    resetDeferredBindlessFrameResources(targets);
    return false;
}

void RendererDeferredSystem::resetDeferredFrameTargets(DeferredFrameTargets& targets){
    resetLaggedLightingHistoryResources(targets);
    resetDeferredBindlessFrameResources(targets);
    targets.framebuffer.reset();

    targets.albedo.reset();
    targets.normal.reset();
    targets.worldPosition.reset();
    targets.specularRoughness.reset();
    ResetCsgIntervalTargets(targets);
    targets.opaqueColor.reset();
    targets.compositeColor.reset();
    targets.depth.reset();
    targets.shadowVisibility.reset();

    targets = DeferredFrameTargets{};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_deferred_bindless_registrar{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 AssignSampled2D(
    Core::GpuDescriptorHeap& heap,
    const Core::TextureHandle& texture,
    const Core::Format::Enum format,
    Core::GpuDescriptorHandle& outHandle
){
    return RendererDescriptorRegisterDetail::AssignSampledTexture(
        heap,
        Core::GpuDescriptorClass::SampledImage,
        texture,
        format,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D,
        outHandle
    );
}

[[nodiscard]] static b8 AssignStorage2D(
    Core::GpuDescriptorHeap& heap,
    const Core::TextureHandle& texture,
    const Core::Format::Enum format,
    Core::GpuDescriptorHandle& outHandle
){
    return RendererDescriptorRegisterDetail::AssignStorageTexture(heap, texture, format, Core::TextureDimension::Texture2D, outHandle);
}

[[nodiscard]] static b8 AssignSampledAndStorage2D(
    Core::GpuDescriptorHeap& heap,
    const Core::TextureHandle& texture,
    const Core::Format::Enum format,
    Core::GpuDescriptorHandle& outSampledHandle,
    Core::GpuDescriptorHandle& outStorageHandle
){
    return AssignSampled2D(heap, texture, format, outSampledHandle) && AssignStorage2D(heap, texture, format, outStorageHandle);
}

[[nodiscard]] static b8 AssignStorage2DArray(
    Core::GpuDescriptorHeap& heap,
    const Core::TextureHandle& texture,
    const Core::Format::Enum format,
    Core::GpuDescriptorHandle& outHandle
){
    return RendererDescriptorRegisterDetail::AssignStorageTexture(heap, texture, format, Core::TextureDimension::Texture2DArray, outHandle);
}

[[nodiscard]] static b8 AssignSampledAndStorageShadowArray(
    Core::GpuDescriptorHeap& heap,
    const Core::TextureHandle& texture,
    const Core::Format::Enum format,
    Core::GpuDescriptorHandle& outSampledHandle,
    Core::GpuDescriptorHandle& outStorageHandle
){
    return
        RendererDescriptorRegisterDetail::AssignSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage2DArray,
            texture,
            format,
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::TextureDimension::Texture2DArray,
            outSampledHandle
        )
        && AssignStorage2DArray(heap, texture, format, outStorageHandle)
    ;
}

[[nodiscard]] static b8 AssignSampler(Core::GpuDescriptorHeap& heap, Core::Sampler& sampler, Core::GpuDescriptorHandle& outHandle){
    const Expected<Core::GpuDescriptorHandle> descriptor = RendererDescriptorRegisterDetail::RegisterSampler(heap, sampler);
    if(!descriptor)
        return false;
    outHandle = *descriptor;
    return true;
}

[[nodiscard]] static b8 AssignConstantBuffer(Core::GpuDescriptorHeap& heap, Core::Buffer& buffer, Core::GpuDescriptorHandle& outHandle){
    const Expected<Core::GpuDescriptorHandle> descriptor = RendererDescriptorRegisterDetail::RegisterConstantBuffer(heap, buffer);
    if(!descriptor)
        return false;
    outHandle = *descriptor;
    return true;
}

[[nodiscard]] static b8 AssignStructuredBuffer(Core::GpuDescriptorHeap& heap, Core::Buffer& buffer, Core::GpuDescriptorHandle& outHandle){
    const Expected<Core::GpuDescriptorHandle> descriptor = RendererDescriptorRegisterDetail::RegisterStructuredBuffer(heap, buffer);
    if(!descriptor)
        return false;
    outHandle = *descriptor;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<void> DeferredBindlessDescriptorRegistrar::registerAll(){
    if(!registerLightingTargets() || !registerTransparencyTargets() || !registerSceneBuffers() || !registerCsgIntervalTargets() || !registerCausticResolveTargets() || !registerSoftShadowTargets())
        return MakeUnexpected(Failure{});
    return {};
}

Expected<void> DeferredBindlessDescriptorRegistrar::registerLightingTargets(){
    using namespace __hidden_deferred_bindless_registrar;
    if(
        !AssignSampled2D(m_heap, m_targets.albedo, m_targets.albedoFormat, m_bindless.gbufferBaseColor)
        || !AssignSampled2D(m_heap, m_targets.normal, m_targets.normalFormat, m_bindless.gbufferNormal)
        || !AssignSampled2D(m_heap, m_targets.worldPosition, m_targets.worldPositionFormat, m_bindless.gbufferWorldPosition)
        || !AssignSampled2D(m_heap, m_targets.specularRoughness, m_targets.specularRoughnessFormat, m_bindless.gbufferSpecularRoughness)
        || !AssignSampled2D(m_heap, m_targets.depth, m_targets.depthFormat, m_bindless.gbufferDepth)
        || !AssignSampledAndStorageShadowArray(m_heap, m_targets.shadowVisibility, m_targets.shadowVisibilityFormat, m_bindless.shadowVisibility, m_bindless.shadowVisibilityStorage)
        || !AssignSampledAndStorage2D(m_heap, m_targets.causticIrradiance, m_targets.causticIrradianceFormat, m_bindless.causticIrradiance, m_bindless.causticIrradianceStorage)
        || !AssignSampledAndStorage2D(m_heap, m_targets.surfelIrradiance, m_targets.surfelIrradianceFormat, m_bindless.surfelIrradiance, m_bindless.surfelIrradianceStorage)
        || !AssignSampledAndStorage2D(m_heap, m_targets.surfelIrradianceHalf, m_targets.surfelIrradianceFormat, m_bindless.surfelIrradianceHalf, m_bindless.surfelIrradianceHalfStorage)
        || !AssignSampler(m_heap, m_sources.sampler, m_bindless.sampler)
        || !AssignSampledAndStorage2D(m_heap, m_targets.opaqueColor, m_targets.opaqueColorFormat, m_bindless.opaqueColor, m_bindless.opaqueColorStorage)
        || !AssignSampledAndStorage2D(m_heap, m_targets.compositeColor, m_targets.compositeColorFormat, m_bindless.compositeColor, m_bindless.compositeColorStorage)
    )
        return MakeUnexpected(Failure{});
    return {};
}

Expected<void> DeferredBindlessDescriptorRegistrar::registerTransparencyTargets(){
    using namespace __hidden_deferred_bindless_registrar;
    if(
        !AssignSampled2D(m_heap, m_targets.avboit.accumColor, m_targets.avboit.accumColorFormat, m_bindless.avboitAccumColor)
        || !AssignSampled2D(m_heap, m_targets.avboit.accumExtinction, m_targets.avboit.accumExtinctionFormat, m_bindless.avboitAccumExtinction)
        || !AssignSampled2D(m_heap, m_targets.avboit.refractionDepth, m_targets.depthFormat, m_bindless.refractionDepth)
        || !AssignSampled2D(m_heap, m_targets.avboit.refractionNormalIor, Core::Format::RGBA16_FLOAT, m_bindless.refractionNormalIor)
        || !AssignSampled2D(m_heap, m_targets.avboit.refractionTintCoverage, Core::Format::RGBA16_FLOAT, m_bindless.refractionTintCoverage)
        || !AssignSampled2D(m_heap, m_targets.avboit.refractionInstance, Core::Format::RG32_FLOAT, m_bindless.refractionInstance)
        || !AssignSampled2D(m_heap, m_targets.avboit.refractionSpecularRoughness, Core::Format::RGBA16_FLOAT, m_bindless.refractionSpecularRoughness)
        || !AssignSampled2D(m_heap, m_targets.avboit.refractionResolve, Core::Format::RGBA16_FLOAT, m_bindless.refractionResolve)
        || !AssignSampled2D(m_heap, m_targets.avboit.foregroundAccumColor, m_targets.avboit.accumColorFormat, m_bindless.avboitForegroundColor)
        || !AssignSampled2D(m_heap, m_targets.avboit.foregroundAccumExtinction, m_targets.avboit.accumExtinctionFormat, m_bindless.avboitForegroundExtinction)
        || !AssignStorage2D(m_heap, m_targets.avboit.refractionResolve, Core::Format::RGBA16_FLOAT, m_bindless.refractionResolveStorage)
        || !RendererDescriptorRegisterDetail::AssignSampledTexture(
            m_heap,
            Core::GpuDescriptorClass::SampledImage3D,
            m_targets.avboit.transmittanceTexture,
            m_targets.avboit.transmittanceFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture3D,
            m_bindless.avboitTransmittance
        )
        || !AssignSampler(m_heap, m_sources.avboitLinearSampler, m_bindless.avboitLinearSampler)
    )
        return MakeUnexpected(Failure{});
    return {};
}

Expected<void> DeferredBindlessDescriptorRegistrar::registerSceneBuffers(){
    using namespace __hidden_deferred_bindless_registrar;
    if(
        !AssignConstantBuffer(m_heap, m_sources.sceneShadingBuffer, m_bindless.sceneShading)
        || !AssignStructuredBuffer(m_heap, m_sources.lightBuffer, m_bindless.lightList)
    )
        return MakeUnexpected(Failure{});
    return {};
}

Expected<void> DeferredBindlessDescriptorRegistrar::registerCsgIntervalTargets(){
    using namespace __hidden_deferred_bindless_registrar;
    if(
        !AssignStorage2DArray(m_heap, m_targets.csgCapBackNormal, m_targets.csgCapNormalFormat, m_bindless.csgCapBackNormal)
        || !AssignStorage2DArray(m_heap, m_targets.csgIntervalDepth, m_targets.csgIntervalDepthFormat, m_bindless.csgIntervalDepth)
        || !AssignStorage2DArray(m_heap, m_targets.csgIntervalId, m_targets.csgIntervalIdFormat, m_bindless.csgIntervalId)
        || !AssignStorage2DArray(m_heap, m_targets.csgReceiverEventData, m_targets.csgReceiverEventDataFormat, m_bindless.csgReceiverEventData)
        || !AssignStorage2DArray(m_heap, m_targets.csgReceiverEventCount, m_targets.csgReceiverEventCountFormat, m_bindless.csgReceiverEventCount)
        || !AssignStorage2DArray(m_heap, m_targets.csgReceiverSpanData, m_targets.csgReceiverSpanDataFormat, m_bindless.csgReceiverSpanData)
        || !AssignStorage2DArray(m_heap, m_targets.csgReceiverSpanCount, m_targets.csgReceiverSpanCountFormat, m_bindless.csgReceiverSpanCount)
        || !AssignStorage2DArray(m_heap, m_targets.csgRemovedIntervalDepth, m_targets.csgRemovedIntervalDepthFormat, m_bindless.csgRemovedIntervalDepth)
        || !AssignStorage2DArray(m_heap, m_targets.csgRemovedIntervalCapNormal, m_targets.csgRemovedIntervalCapNormalFormat, m_bindless.csgRemovedIntervalCapNormal)
        || !AssignStorage2DArray(m_heap, m_targets.csgRemovedIntervalData, m_targets.csgRemovedIntervalDataFormat, m_bindless.csgRemovedIntervalData)
        || !AssignStorage2DArray(m_heap, m_targets.csgRemovedIntervalCount, m_targets.csgRemovedIntervalCountFormat, m_bindless.csgRemovedIntervalCount)
    )
        return MakeUnexpected(Failure{});
    return {};
}

Expected<void> DeferredBindlessDescriptorRegistrar::registerCausticResolveTargets(){
    using namespace __hidden_deferred_bindless_registrar;
    if(
        !RendererDescriptorRegisterDetail::AssignSampledTexture(
            m_heap,
            Core::GpuDescriptorClass::SampledImage2DArrayUint,
            m_targets.causticAccumulator,
            m_targets.causticAccumulatorFormat,
            ECSRenderDetail::s_CausticAccumulatorSubresources,
            Core::TextureDimension::Texture2DArray,
            m_bindless.causticAccumulator
        )
        || !AssignStorage2DArray(m_heap, m_targets.causticAccumulator, m_targets.causticAccumulatorFormat, m_bindless.causticAccumulatorStorage)
        || !AssignSampledAndStorage2D(m_heap, m_targets.causticHistory, m_targets.causticHistoryFormat, m_bindless.causticHistory, m_bindless.causticHistoryStorage)
        || !AssignSampledAndStorage2D(m_heap, m_targets.causticResolveHalf, m_targets.causticHistoryFormat, m_bindless.causticResolveHalf, m_bindless.causticResolveHalfStorage)
        || !AssignSampledAndStorage2D(m_heap, m_targets.causticResolveGeometry, m_targets.causticHistoryFormat, m_bindless.causticResolveGeometry, m_bindless.causticResolveGeometryStorage)
    )
        return MakeUnexpected(Failure{});
    return {};
}

Expected<void> DeferredBindlessDescriptorRegistrar::registerSoftShadowTargets(){
    using namespace __hidden_deferred_bindless_registrar;
    if(
        !AssignStorage2DArray(m_heap, m_targets.shadowCoarseTransmittance, m_targets.shadowCoarseTransmittanceFormat, m_bindless.shadowCoarseTransmittanceStorage)
        || !AssignSampledAndStorage2D(m_heap, m_targets.shadowSoftGeometry, m_targets.shadowSoftGeometryFormat, m_bindless.shadowSoftGeometry, m_bindless.shadowSoftGeometryStorage)
        || !AssignSampledAndStorage2D(m_heap, m_targets.shadowSoftGeometryPrev, m_targets.shadowSoftGeometryFormat, m_bindless.shadowSoftGeometryPrev, m_bindless.shadowSoftGeometryPrevStorage)
        || !AssignSampledAndStorageShadowArray(m_heap, m_targets.shadowSoftHalfA, m_targets.shadowSoftFormat, m_bindless.shadowSoftHalfA, m_bindless.shadowSoftHalfAStorage)
        || !AssignSampledAndStorageShadowArray(m_heap, m_targets.shadowSoftHalfB, m_targets.shadowSoftFormat, m_bindless.shadowSoftHalfB, m_bindless.shadowSoftHalfBStorage)
        || !AssignSampledAndStorageShadowArray(m_heap, m_targets.shadowHistA, m_targets.shadowSoftFormat, m_bindless.shadowHistA, m_bindless.shadowHistAStorage)
        || !AssignSampledAndStorageShadowArray(m_heap, m_targets.shadowHistB, m_targets.shadowSoftFormat, m_bindless.shadowHistB, m_bindless.shadowHistBStorage)
        || !AssignSampledAndStorageShadowArray(m_heap, m_targets.shadowMomentsA, m_targets.shadowSoftFormat, m_bindless.shadowMomentsA, m_bindless.shadowMomentsAStorage)
        || !AssignSampledAndStorageShadowArray(m_heap, m_targets.shadowMomentsB, m_targets.shadowSoftFormat, m_bindless.shadowMomentsB, m_bindless.shadowMomentsBStorage)
        || !AssignSampledAndStorageShadowArray(m_heap, m_targets.transparentSoftHalf, m_targets.shadowSoftFormat, m_bindless.transparentSoftHalf, m_bindless.transparentSoftHalfStorage)
        || !AssignSampledAndStorageShadowArray(m_heap, m_targets.transparentHistA, m_targets.shadowSoftFormat, m_bindless.transparentHistA, m_bindless.transparentHistAStorage)
        || !AssignSampledAndStorageShadowArray(m_heap, m_targets.transparentHistB, m_targets.shadowSoftFormat, m_bindless.transparentHistB, m_bindless.transparentHistBStorage)
        || !AssignSampledAndStorageShadowArray(m_heap, m_targets.transparentMomentsA, m_targets.shadowSoftFormat, m_bindless.transparentMomentsA, m_bindless.transparentMomentsAStorage)
        || !AssignSampledAndStorageShadowArray(m_heap, m_targets.transparentMomentsB, m_targets.shadowSoftFormat, m_bindless.transparentMomentsB, m_bindless.transparentMomentsBStorage)
    )
        return MakeUnexpected(Failure{});
    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererDeferredSystem::createDeferredBindlessFrameResources(DeferredFrameTargets& targets, Core::Sampler& avboitLinearSampler){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: deferred lighting/compositor requires the global descriptor heap"));
        return false;
    }
    NWB_ASSERT(m_deferredState.m_sampler);
    NWB_ASSERT(m_deferredState.m_sceneShadingBuffer);
    NWB_ASSERT(m_deferredState.m_lightBuffer);
    NWB_ASSERT(targets.csgIntervalTargetsValid());

    DeferredBindlessFrameResources& bindless = targets.bindless;
    const DeferredBindlessDescriptorSources sources{
        .sampler = *m_deferredState.m_sampler.get(),
        .avboitLinearSampler = avboitLinearSampler,
        .sceneShadingBuffer = *m_deferredState.m_sceneShadingBuffer.get(),
        .lightBuffer = *m_deferredState.m_lightBuffer.get(),
    };
    DeferredBindlessDescriptorRegistrar registrar(heap, targets, sources);
    if(!registrar.registerAll()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register deferred frame resources in the descriptor heap"));
        resetDeferredBindlessFrameResources(targets);
        return false;
    }

    bindless.slots = DeferredBindlessSlotsBuilder{}.build(
        bindless.gbufferBaseColor.slot(),
        bindless.gbufferNormal.slot(),
        bindless.gbufferWorldPosition.slot(),
        bindless.gbufferDepth.slot(),
        bindless.shadowVisibility.slot(),
        bindless.causticIrradiance.slot(),
        bindless.surfelIrradiance.slot(),
        bindless.sampler.slot(),
        bindless.opaqueColor.slot(),
        bindless.opaqueColorStorage.slot(),
        bindless.compositeColor.slot(),
        bindless.compositeColorStorage.slot(),
        bindless.avboitAccumColor.slot(),
        bindless.avboitAccumExtinction.slot(),
        bindless.refractionDepth.slot(),
        bindless.refractionNormalIor.slot(),
        bindless.refractionTintCoverage.slot(),
        bindless.refractionInstance.slot(),
        bindless.refractionResolve.slot(),
        bindless.avboitForegroundColor.slot(),
        bindless.avboitForegroundExtinction.slot(),
        bindless.refractionResolveStorage.slot(),
        bindless.avboitTransmittance.slot(),
        bindless.avboitLinearSampler.slot(),
        bindless.sceneShading.slot(),
        bindless.lightList.slot(),
        bindless.csgCapBackNormal.slot(),
        bindless.csgIntervalDepth.slot(),
        bindless.csgIntervalId.slot(),
        bindless.csgReceiverEventData.slot(),
        bindless.csgReceiverEventCount.slot(),
        bindless.csgReceiverSpanData.slot(),
        bindless.csgReceiverSpanCount.slot(),
        bindless.csgRemovedIntervalDepth.slot(),
        bindless.csgRemovedIntervalCapNormal.slot(),
        bindless.csgRemovedIntervalData.slot(),
        bindless.csgRemovedIntervalCount.slot()
    );

    Core::BufferDesc slotsBufferDesc;
    slotsBufferDesc
        .setByteSize(sizeof(DeferredBindlessResourceSlots))
        .setIsConstantBuffer(true)
        .setDebugName("ECSRender_DeferredBindlessResourceSlots")
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        // Selector spans packets; retain descriptor-visible state at every native close.
        .enableAutomaticStateTracking(Core::ResourceStates::ConstantBuffer)
    ;
    auto slotsBufferResult = m_graphics.createBuffer(slotsBufferDesc);
    if(slotsBufferResult)
        bindless.slotsBuffer = Move(*slotsBufferResult);
    else
        bindless.slotsBuffer.reset();
    if(!slotsBufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred bindless slot buffer"));
        resetDeferredBindlessFrameResources(targets);
        return false;
    }

    // The indirection payload is itself heap-addressable.
    // Every consumer takes this one slot in push constants, not a local selector; the heap stays the binding surface.
    const Expected<Core::GpuDescriptorHandle> slotsBufferDescriptor = RendererDescriptorRegisterDetail::RegisterConstantBuffer(heap, bindless.slotsBuffer);
    bindless.slotsBufferDescriptor = slotsBufferDescriptor ? *slotsBufferDescriptor : Core::GpuDescriptorHandle::Invalid();
    if(!slotsBufferDescriptor){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register deferred bindless slot buffer in the descriptor heap"));
        resetDeferredBindlessFrameResources(targets);
        return false;
    }

    return true;
}

void RendererDeferredSystem::resetDeferredBindlessFrameResources(DeferredFrameTargets& targets){
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(heap.isInitialized()){
        heap.free(targets.bindless.slotsBufferDescriptor);
        heap.free(targets.bindless.gbufferBaseColor);
        heap.free(targets.bindless.gbufferNormal);
        heap.free(targets.bindless.gbufferWorldPosition);
        heap.free(targets.bindless.gbufferSpecularRoughness);
        heap.free(targets.bindless.gbufferDepth);
        heap.free(targets.bindless.shadowVisibility);
        heap.free(targets.bindless.shadowVisibilityStorage);
        heap.free(targets.bindless.causticIrradiance);
        heap.free(targets.bindless.causticIrradianceStorage);
        heap.free(targets.bindless.surfelIrradiance);
        heap.free(targets.bindless.surfelIrradianceStorage);
        heap.free(targets.bindless.surfelIrradianceHalf);
        heap.free(targets.bindless.surfelIrradianceHalfStorage);
        heap.free(targets.bindless.sampler);
        heap.free(targets.bindless.opaqueColor);
        heap.free(targets.bindless.opaqueColorStorage);
        heap.free(targets.bindless.compositeColor);
        heap.free(targets.bindless.compositeColorStorage);
        heap.free(targets.bindless.avboitAccumColor);
        heap.free(targets.bindless.avboitAccumExtinction);
        heap.free(targets.bindless.refractionDepth);
        heap.free(targets.bindless.refractionNormalIor);
        heap.free(targets.bindless.refractionTintCoverage);
        heap.free(targets.bindless.refractionInstance);
        heap.free(targets.bindless.refractionSpecularRoughness);
        heap.free(targets.bindless.refractionResolve);
        heap.free(targets.bindless.avboitForegroundColor);
        heap.free(targets.bindless.avboitForegroundExtinction);
        heap.free(targets.bindless.refractionResolveStorage);
        heap.free(targets.bindless.avboitTransmittance);
        heap.free(targets.bindless.avboitLinearSampler);
        heap.free(targets.bindless.sceneShading);
        heap.free(targets.bindless.lightList);
        heap.free(targets.bindless.causticAccumulator);
        heap.free(targets.bindless.causticAccumulatorStorage);
        heap.free(targets.bindless.causticHistory);
        heap.free(targets.bindless.causticHistoryStorage);
        heap.free(targets.bindless.causticResolveHalf);
        heap.free(targets.bindless.causticResolveHalfStorage);
        heap.free(targets.bindless.causticResolveGeometry);
        heap.free(targets.bindless.causticResolveGeometryStorage);
        heap.free(targets.bindless.shadowCoarseTransmittanceStorage);
        heap.free(targets.bindless.shadowSoftGeometry);
        heap.free(targets.bindless.shadowSoftGeometryStorage);
        heap.free(targets.bindless.shadowSoftGeometryPrev);
        heap.free(targets.bindless.shadowSoftGeometryPrevStorage);
        heap.free(targets.bindless.shadowSoftHalfA);
        heap.free(targets.bindless.shadowSoftHalfAStorage);
        heap.free(targets.bindless.shadowSoftHalfB);
        heap.free(targets.bindless.shadowSoftHalfBStorage);
        heap.free(targets.bindless.shadowHistA);
        heap.free(targets.bindless.shadowHistAStorage);
        heap.free(targets.bindless.shadowHistB);
        heap.free(targets.bindless.shadowHistBStorage);
        heap.free(targets.bindless.shadowMomentsA);
        heap.free(targets.bindless.shadowMomentsAStorage);
        heap.free(targets.bindless.shadowMomentsB);
        heap.free(targets.bindless.shadowMomentsBStorage);
        heap.free(targets.bindless.transparentSoftHalf);
        heap.free(targets.bindless.transparentSoftHalfStorage);
        heap.free(targets.bindless.transparentHistA);
        heap.free(targets.bindless.transparentHistAStorage);
        heap.free(targets.bindless.transparentHistB);
        heap.free(targets.bindless.transparentHistBStorage);
        heap.free(targets.bindless.transparentMomentsA);
        heap.free(targets.bindless.transparentMomentsAStorage);
        heap.free(targets.bindless.transparentMomentsB);
        heap.free(targets.bindless.transparentMomentsBStorage);
        heap.free(targets.bindless.csgCapBackNormal);
        heap.free(targets.bindless.csgIntervalDepth);
        heap.free(targets.bindless.csgIntervalId);
        heap.free(targets.bindless.csgReceiverEventData);
        heap.free(targets.bindless.csgReceiverEventCount);
        heap.free(targets.bindless.csgReceiverSpanData);
        heap.free(targets.bindless.csgReceiverSpanCount);
        heap.free(targets.bindless.csgRemovedIntervalDepth);
        heap.free(targets.bindless.csgRemovedIntervalCapNormal);
        heap.free(targets.bindless.csgRemovedIntervalData);
        heap.free(targets.bindless.csgRemovedIntervalCount);
    }
    targets.bindless = DeferredBindlessFrameResources{};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<DeferredFrameTargets> DeferredFrameTargetBuilder::build(const u32 width, const u32 height, const u32 shadowReceiverFactor, const u32 surfelResolveFactor){
    auto createdTargetsResult = m_deferred.createDeferredFrameTargets(width, height);
    if(!createdTargetsResult)
        return MakeUnexpected(Failure{});
    DeferredFrameTargets& createdTargets = *createdTargetsResult;
    const auto resetCreatedTargets = [this, &createdTargets](){
        m_avboit.resetAvboitFrameTargets(createdTargets.avboit);
        m_deferred.resetDeferredFrameTargets(createdTargets);
    };
    if(!m_avboit.createAvboitSampler()){
        resetCreatedTargets();
        return MakeUnexpected(Failure{});
    }
    if(!m_avboit.createAvboitFrameTargets(createdTargets)){
        resetCreatedTargets();
        return MakeUnexpected(Failure{});
    }
    if(!m_csg.createCsgPeelTargets(createdTargets)){
        resetCreatedTargets();
        return MakeUnexpected(Failure{});
    }
    createdTargets.shadowReceiverFactor = shadowReceiverFactor;
    if(!ShadowVisibilityTargetBuilder(m_graphics, m_rayTracingState).build(createdTargets)){
        resetCreatedTargets();
        return MakeUnexpected(Failure{});
    }
    if(!m_rayTracing.createCausticTargets(createdTargets)){
        resetCreatedTargets();
        return MakeUnexpected(Failure{});
    }
    createdTargets.surfelResolveFactor = surfelResolveFactor;
    if(!m_rayTracing.createSurfelTargets(createdTargets)){
        resetCreatedTargets();
        return MakeUnexpected(Failure{});
    }
    if(!m_deferred.createDeferredFrameTargetResources(createdTargets, m_avboit.linearSampler())){
        resetCreatedTargets();
        return MakeUnexpected(Failure{});
    }
    if(!m_avboit.registerAvboitFrameTargetDescriptors(createdTargets, createdTargets.avboit)){
        resetCreatedTargets();
        return MakeUnexpected(Failure{});
    }
    return Move(createdTargets);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererDeferredSystem::resetLaggedLightingHistoryResources(DeferredFrameTargets& targets){
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(heap.isInitialized()){
        heap.free(targets.laggedLightingHistory.slotsBufferDescriptor);
        heap.free(targets.laggedLightingHistory.shadowVisibilityDescriptor);
        heap.free(targets.laggedLightingHistory.causticIrradianceDescriptor);
        heap.free(targets.laggedLightingHistory.surfelIrradianceDescriptor);
    }

    targets.laggedLightingHistory = DeferredLaggedLightingHistoryResources{};
}

b8 RendererDeferredSystem::createLaggedLightingHistoryResources(DeferredFrameTargets& targets){
    DeferredLaggedLightingHistoryResources& history = targets.laggedLightingHistory;
    if(history.valid())
        return true;

    NWB_ASSERT(targets.bindless.valid());
    NWB_ASSERT(targets.shadowVisibility);
    NWB_ASSERT(targets.causticIrradiance);
    NWB_ASSERT(targets.surfelIrradiance);

    resetLaggedLightingHistoryResources(targets);

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: lagged lighting history requires the global descriptor heap"));
        return false;
    }

    Core::TextureDesc shadowHistoryDesc;
    shadowHistoryDesc
        .setWidth(targets.width)
        .setHeight(targets.height)
        .setArraySize(NWB_SCENE_SHADOW_SLOT_COUNT)
        .setDimension(Core::TextureDimension::Texture2DArray)
        .setFormat(targets.shadowVisibilityFormat)
        .setInitialState(Core::ResourceStates::Common)
        .setKeepInitialState(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
        .setName("engine/temporal/lagged_shadow_visibility")
    ;
    auto shadowVisibilityResult = m_graphics.createTexture(shadowHistoryDesc);
    if(shadowVisibilityResult)
        history.shadowVisibility = Move(*shadowVisibilityResult);
    else
        history.shadowVisibility.reset();
    if(!shadowVisibilityResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create lagged shadow-visibility history"));
        resetLaggedLightingHistoryResources(targets);
        return false;
    }

    Core::TextureDesc irradianceHistoryDesc;
    irradianceHistoryDesc
        .setWidth(targets.width)
        .setHeight(targets.height)
        .setFormat(targets.causticIrradianceFormat)
        .setInitialState(Core::ResourceStates::Common)
        .setKeepInitialState(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
        .setName("engine/temporal/lagged_caustic_irradiance")
    ;
    auto causticIrradianceResult = m_graphics.createTexture(irradianceHistoryDesc);
    if(causticIrradianceResult)
        history.causticIrradiance = Move(*causticIrradianceResult);
    else
        history.causticIrradiance.reset();
    if(!causticIrradianceResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create lagged caustic-irradiance history"));
        resetLaggedLightingHistoryResources(targets);
        return false;
    }

    irradianceHistoryDesc.setFormat(targets.surfelIrradianceFormat).setName("engine/temporal/lagged_surfel_irradiance");
    auto surfelIrradianceResult = m_graphics.createTexture(irradianceHistoryDesc);
    if(surfelIrradianceResult)
        history.surfelIrradiance = Move(*surfelIrradianceResult);
    else
        history.surfelIrradiance.reset();
    if(!surfelIrradianceResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create lagged surfel-irradiance history"));
        resetLaggedLightingHistoryResources(targets);
        return false;
    }

    if(
        !RendererDescriptorRegisterDetail::AssignSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage2DArray,
            history.shadowVisibility,
            targets.shadowVisibilityFormat,
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::TextureDimension::Texture2DArray,
            history.shadowVisibilityDescriptor
        )
        || !RendererDescriptorRegisterDetail::AssignSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage,
            history.causticIrradiance,
            targets.causticIrradianceFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D,
            history.causticIrradianceDescriptor
        )
        || !RendererDescriptorRegisterDetail::AssignSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage,
            history.surfelIrradiance,
            targets.surfelIrradianceFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D,
            history.surfelIrradianceDescriptor
        )
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register lagged lighting-history images in the descriptor heap"));
        resetLaggedLightingHistoryResources(targets);
        return false;
    }

    history.slots = DeferredBindlessSlotsBuilder{}.buildHistory(
        targets.bindless.slots,
        history.shadowVisibilityDescriptor.slot(),
        history.causticIrradianceDescriptor.slot(),
        history.surfelIrradianceDescriptor.slot()
    );

    Core::BufferDesc slotsBufferDesc;
    slotsBufferDesc
        .setByteSize(sizeof(DeferredBindlessResourceSlots))
        .setIsConstantBuffer(true)
        .setDebugName("ECSRender_DeferredLaggedLightingResourceSlots")
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    auto slotsBufferResult = m_graphics.createBuffer(slotsBufferDesc);
    if(slotsBufferResult)
        history.slotsBuffer = Move(*slotsBufferResult);
    else
        history.slotsBuffer.reset();
    if(!slotsBufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create lagged lighting-history slot buffer"));
        resetLaggedLightingHistoryResources(targets);
        return false;
    }

    const Expected<Core::GpuDescriptorHandle> slotsBufferDescriptor = RendererDescriptorRegisterDetail::RegisterConstantBuffer(heap, history.slotsBuffer);
    history.slotsBufferDescriptor = slotsBufferDescriptor ? *slotsBufferDescriptor : Core::GpuDescriptorHandle::Invalid();
    if(!slotsBufferDescriptor){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register lagged lighting-history slot buffer in the descriptor heap"));
        resetLaggedLightingHistoryResources(targets);
        return false;
    }

    static u64 s_NextLaggedLightingHistoryGeneration = 0u;
    AdvanceWrappingNonzero(s_NextLaggedLightingHistoryGeneration);
    history.generation = s_NextLaggedLightingHistoryGeneration;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_deferred_lighting{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(sizeof(DeferredLightingPushConstants) == sizeof(u32) * 2u);


struct DeferredLightingGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    // The deferred system, the frame targets, and the timing ticket are required: the pass always records against this frame's targets.
    struct Payload{
        RendererDeferredSystem& deferredSystem;
        DeferredFrameTargets& targets;
        Core::GpuTimingSubmissionTicket& timingTicket;
        b8 useLaggedLightingHistory = false;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){

        return ECSRenderDetail::RecordDeferredGraphTask(payload, commandList, [&](RendererDeferredSystem& deferredSystem, DeferredFrameTargets& targets, Core::CommandList& taskCommandList){
            return deferredSystem.renderDeferredLighting(taskCommandList, targets, payload.useLaggedLightingHistory);
        });
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SceneShadingBufferUploads RendererDeferredSystem::prepareSceneShadingBufferUploads(const f32 fallbackAspectRatio, const RayTracingLightingClassificationInput& rayTracingInput)noexcept{
    NWB_ASSERT(m_deferredState.m_sceneShadingBuffer);
    NWB_ASSERT(m_deferredState.m_lightBuffer);
    SceneShadingBufferUploads uploads{};

    Array<f32, NWB_SCENE_MAX_LIGHTS> causticLightImportance{};
    const u32 lightCount = ECSRenderDetail::ResolveSceneLights(m_world, uploads.lightData, causticLightImportance);

    // Rank caustic lights into params.w; needs a refractive instance.
    const u32 causticLightCount = ECSRenderDetail::ResolveCausticLights(
        Span<const f32>(causticLightImportance.data(), lightCount),
        rayTracingInput.refractiveInstanceCount,
        Span<SceneLightGpuData>(uploads.lightData.data(), lightCount)
    );
    uploads.lightingClassification.causticLightCount = causticLightCount;
    const Span<const SceneLightGpuData> activeLights(uploads.lightData.data(), lightCount);
    // Bitmask of shadow slots holding a light.
    uploads.lightingClassification.softShadowSlotMask = ResolveSoftShadowSlotMask(activeLights);

    // A zero-light scene has nothing to copy; still record the empty mirror.
    uploads.lightUploadRequired = SceneShadingUploadTracker{}.lightUploadRequired(
        m_deferredState.m_lightGpuDataValid,
        m_deferredState.m_lightGpuDataCount,
        m_deferredState.m_lightGpuData,
        activeLights
    );
    uploads.lightCount = lightCount;

    uploads.sceneShadingState = ECSRenderDetail::ResolveSceneShadingState(fallbackAspectRatio, lightCount, m_world);
    uploads.sceneShadingUploadRequired = SceneShadingUploadTracker{}.shadingUploadRequired(
        m_deferredState.m_sceneShadingGpuDataValid,
        m_deferredState.m_sceneShadingGpuData,
        uploads.sceneShadingState
    );
    return uploads;
}

void RendererDeferredSystem::confirmSceneShadingBufferUploads(
    const Span<const SceneLightGpuData> lights,
    const b8 lightUploadRequired,
    const SceneShadingGpuData& sceneShadingState,
    const b8 sceneShadingUploadRequired
)noexcept{
    SceneShadingUploadTracker{}.confirm(
        lights,
        lightUploadRequired,
        sceneShadingState,
        sceneShadingUploadRequired,
        m_deferredState.m_lightGpuData,
        m_deferredState.m_lightGpuDataCount,
        m_deferredState.m_lightGpuDataValid,
        m_deferredState.m_sceneShadingGpuData,
        m_deferredState.m_sceneShadingGpuDataValid
    );
}

b8 RendererDeferredSystem::createDeferredLightingResources(){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: deferred lighting requires the global descriptor heap"));
        return false;
    }

    if(!m_deferredState.m_sceneShadingBuffer){
        Core::BufferDesc sceneShadingBufferDesc;
        sceneShadingBufferDesc
            .setByteSize(sizeof(SceneShadingGpuData))
            .setIsConstantBuffer(true)
            .setDebugName(s_SceneShadingBufferName)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        auto sceneShadingBufferResult = m_graphics.createBuffer(sceneShadingBufferDesc);
        if(sceneShadingBufferResult)
            m_deferredState.m_sceneShadingBuffer = Move(*sceneShadingBufferResult);
        else
            m_deferredState.m_sceneShadingBuffer.reset();
        if(!sceneShadingBufferResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create scene shading buffer"));
            return false;
        }
    }

    if(!m_deferredState.m_lightBuffer){
        Core::BufferDesc lightBufferDesc;
        lightBufferDesc
            .setByteSize(static_cast<u64>(sizeof(SceneLightGpuData) * NWB_SCENE_MAX_LIGHTS))
            .setStructStride(sizeof(SceneLightGpuData))
            .setDebugName(s_SceneLightBufferName)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        auto lightBufferResult = m_graphics.createBuffer(lightBufferDesc);
        if(lightBufferResult)
            m_deferredState.m_lightBuffer = Move(*lightBufferResult);
        else
            m_deferredState.m_lightBuffer.reset();
        if(!lightBufferResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create scene light buffer"));
            return false;
        }
    }

    // Local layout carries the heap slot plus the swap-chain mode.
    if(!RendererComputePipelineDetail::EnsurePushConstantBindingLayout(m_arena, device, sizeof(DeferredLightingPushConstants), Core::ShaderType::Compute, m_deferredState.m_lightingBindingLayout)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred lighting binding layout"));
        return false;
    }

    if(!ECSRenderDetail::CreateClampSampler(device, false, m_deferredState.m_sampler)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred lighting sampler"));
        return false;
    }

    // Shading is material-driven via the generated BXDF dispatch.
    if(!m_shaderSystem.loadShader<ComputeShader>(&DeferredSubsystem::lightingCs, Core::ShaderArchive::s_DefaultVariant, "ECSRender_DeferredLightingCS", m_deferredState.m_lightingComputeShader))
        return false;

    return true;
}

b8 RendererDeferredSystem::createDeferredLightingPipeline(){
    if(!createDeferredLightingResources())
        return false;

    if(m_deferredState.m_lightingPipeline)
        return true;

    if(!RendererComputePipelineDetail::CreateHeapComputePipeline(m_graphics.getDevice(), m_deferredState.m_lightingComputeShader, m_deferredState.m_lightingBindingLayout, false, m_deferredState.m_lightingPipeline)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred lighting pipeline"));
        return false;
    }

    return true;
}

Expected<Core::GpuTaskId> RendererDeferredSystem::declareDeferredLightingTask(Core::GpuTaskGraph& graph, const Core::GpuTaskDesc& desc, const b8 useLaggedLightingHistory, DeferredFrameTargets& targets, Core::GpuTimingSubmissionTicket& timingTicket){
    return graph.addTask<__hidden_deferred_lighting::DeferredLightingGraphTask>(
        desc,
        __hidden_deferred_lighting::DeferredLightingGraphTask::Payload{ .deferredSystem = *this, .targets = targets, .timingTicket = timingTicket, .useLaggedLightingHistory = useLaggedLightingHistory, });
}

b8 RendererDeferredSystem::renderDeferredLighting(Core::CommandList& commandList, DeferredFrameTargets& targets, const b8 useLaggedLightingHistory){
    NWB_ASSERT(m_deferredState.m_lightingPipeline);

    const DeferredLaggedLightingHistoryResources* const laggedHistory = useLaggedLightingHistory ? &targets.laggedLightingHistory : nullptr;
    if(useLaggedLightingHistory && (!laggedHistory || !laggedHistory->valid()))
        return false;

    const Core::GpuDescriptorHandle resourceSlots = laggedHistory ? laggedHistory->slotsBufferDescriptor : targets.bindless.slotsBufferDescriptor;

    // Boundary transitions are graph-owned; this thunk holds upload + dispatch.

    Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_DeferredLighting, m_graphics.getDevice(), commandList);

    Core::ComputeState computeState;
    computeState.setPipeline(m_deferredState.m_lightingPipeline.get());
    commandList.setComputeState(computeState);
    m_graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *m_deferredState.m_lightingPipeline);
    const DeferredLightingPushConstants pushConstants = ResolveDeferredLightingPushConstants(resourceSlots.slot(), m_graphics.isHDR10OutputActive());
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));

    const u32 groupCountX = DivideUpWrapping(targets.width, NWB_DEFERRED_LIGHTING_GROUP_SIZE);
    const u32 groupCountY = DivideUpWrapping(targets.height, NWB_DEFERRED_LIGHTING_GROUP_SIZE);
    commandList.dispatch(groupCountX, groupCountY, 1u);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_deferred_composite{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(sizeof(DeferredCompositePushConstants) == sizeof(u32) * 5u);
static_assert(sizeof(DeferredPresentPushConstants) == sizeof(u32) * 6u);


struct DeferredCompositeGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    // The deferred system, the frame targets, and the timing ticket are required: the pass always records against this frame's targets.
    struct Payload{
        RendererDeferredSystem& deferredSystem;
        DeferredFrameTargets& targets;
        Core::GpuTimingSubmissionTicket& timingTicket;
        ReflectionCompositeInputs reflectionInputs;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, [[maybe_unused]] const Core::GpuTaskRecordContext& context){
        return ECSRenderDetail::RecordDeferredGraphTask(payload, commandList, [&](RendererDeferredSystem& deferredSystem, DeferredFrameTargets& targets, Core::CommandList& taskCommandList){
            return deferredSystem.renderDeferredComposite(taskCommandList, targets, payload.reflectionInputs);
        });
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererDeferredSystem::createDeferredCompositeResources(){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: deferred compositing requires the global descriptor heap"));
        return false;
    }

    if(!RendererComputePipelineDetail::EnsurePushConstantBindingLayout(m_arena, device, sizeof(DeferredCompositePushConstants), Core::ShaderType::Compute, m_deferredState.m_compositeComputeBindingLayout)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred composite-compute binding layout"));
        return false;
    }

    if(!RendererComputePipelineDetail::EnsurePushConstantBindingLayout(m_arena, device, sizeof(DeferredPresentPushConstants), Core::ShaderType::Pixel, m_deferredState.m_presentBindingLayout)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred presentation binding layout"));
        return false;
    }

    if(!ECSRenderDetail::CreateClampSampler(device, false, m_deferredState.m_sampler)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred composite sampler"));
        return false;
    }

    if(!m_shaderSystem.loadShader<ComputeShader>(&DeferredSubsystem::compositeCs, Core::ShaderArchive::s_DefaultVariant, "ECSRender_DeferredCompositeCS", m_deferredState.m_compositeComputeShader))
        return false;

    if(!m_shaderSystem.loadDeferredCompositeVertexShader())
        return false;

    if(!m_shaderSystem.loadShader<PixelShader>(&DeferredSubsystem::compositePs, Core::ShaderArchive::s_DefaultVariant, "ECSRender_DeferredPresentPS", m_deferredState.m_presentPixelShader))
        return false;

    return true;
}

b8 RendererDeferredSystem::createDeferredCompositePipeline(){
    if(!createDeferredCompositeResources())
        return false;

    if(m_deferredState.m_compositeComputePipeline)
        return true;

    if(!RendererComputePipelineDetail::CreateHeapComputePipeline(m_graphics.getDevice(), m_deferredState.m_compositeComputeShader, m_deferredState.m_compositeComputeBindingLayout, false, m_deferredState.m_compositeComputePipeline)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred composite-compute pipeline"));
        return false;
    }

    return true;
}

b8 RendererDeferredSystem::createDeferredPresentPipeline(Core::Framebuffer& presentationFramebuffer){
    if(!createDeferredCompositeResources())
        return false;

    const Core::FramebufferInfo& framebufferInfo = presentationFramebuffer.getFramebufferInfo();
    if(m_deferredState.m_presentPipeline && m_deferredState.m_presentPipeline->getFramebufferInfo() == framebufferInfo)
        return true;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    Core::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc
        .setVertexShader(m_shaderSystem.deferredCompositeVertexShader())
        .setPixelShader(m_deferredState.m_presentPixelShader)
        .setRenderState(ECSRenderDetail::BuildCompositeRenderState())
        .addBindingLayout(m_deferredState.m_presentBindingLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;

    auto presentPipelineResult = device.createGraphicsPipeline(pipelineDesc, framebufferInfo);
    if(presentPipelineResult)
        m_deferredState.m_presentPipeline = Move(*presentPipelineResult);
    else
        m_deferredState.m_presentPipeline.reset();
    if(!presentPipelineResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred presentation pipeline"));
        return false;
    }

    return true;
}

Expected<Core::GpuTaskId> RendererDeferredSystem::declareDeferredCompositeTask(
    Core::GpuTaskGraph& graph,
    const Core::GpuTaskDesc& desc,
    const ReflectionCompositeInputs& reflectionInputs,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket& timingTicket
){
    return graph.addTask<__hidden_deferred_composite::DeferredCompositeGraphTask>(
        desc,
        __hidden_deferred_composite::DeferredCompositeGraphTask::Payload{ .deferredSystem = *this, .targets = targets, .timingTicket = timingTicket, .reflectionInputs = reflectionInputs, });
}


b8 RendererDeferredSystem::renderDeferredComposite(Core::CommandList& commandList, DeferredFrameTargets& targets, const ReflectionCompositeInputs& reflectionInputs){
    NWB_ASSERT(m_deferredState.m_compositeComputePipeline);

    // Boundary states are graph-owned; this thunk holds only composite commands.

    Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_DeferredComposite, m_graphics.getDevice(), commandList);

    Core::ComputeState computeState;
    computeState.setPipeline(m_deferredState.m_compositeComputePipeline.get());
    commandList.setComputeState(computeState);
    m_graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *m_deferredState.m_compositeComputePipeline);
    const DeferredCompositePushConstants pushConstants = ResolveDeferredCompositePushConstants(
        targets.bindless.slotsBufferDescriptor.slot(),
        targets.avboit.refractionFramebuffer != nullptr,
        reflectionInputs
    );
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
    const u32 groupCountX = DivideUpWrapping(targets.width, NWB_DEFERRED_COMPOSITE_GROUP_SIZE);
    const u32 groupCountY = DivideUpWrapping(targets.height, NWB_DEFERRED_COMPOSITE_GROUP_SIZE);
    commandList.dispatch(groupCountX, groupCountY, 1u);
    return true;
}

b8 RendererDeferredSystem::renderDeferredPresent(Core::CommandList& commandList, DeferredFrameTargets& targets, const Core::AcquiredPresentationFrame& presentationFrame, const Core::GpuDescriptorHandle outputLayerImage){
    NWB_ASSERT(presentationFrame.valid());
    NWB_ASSERT(m_deferredState.m_presentPipeline);
    Core::Framebuffer& presentationFramebuffer = *presentationFrame.framebuffer;
    NWB_ASSERT(
        presentationFramebuffer.getDescription().colorAttachments.size() == 1u
        && presentationFramebuffer.getDescription().colorAttachments[0].texture == presentationFrame.backBuffer.texture.get()
        && m_deferredState.m_presentPipeline->getFramebufferInfo() == presentationFramebuffer.getFramebufferInfo()
    );

    // Graph owns the composite transition and the acquired texture's render-target state.

    Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_DeferredPresent, m_graphics.getDevice(), commandList);

    // Discard avoids a tile restore; STORE keeps the image for UI and present.
    Core::RenderPassParameters renderPassParameters;
    renderPassParameters.colorAttachmentActions[0u].loadAction = Core::RenderPassLoadAction::Discard;
    renderPassParameters.colorAttachmentActions[0u].storeAction = Core::RenderPassStoreAction::Store;
    commandList.beginRenderPass(presentationFramebuffer, renderPassParameters);
    if(!commandList.isRenderPassActive())
        return false;

    Core::ViewportState viewportState;
    viewportState.addViewportAndScissorRect(presentationFramebuffer.getFramebufferInfo().getViewport());

    Core::GraphicsState graphicsState;
    graphicsState.setPipeline(m_deferredState.m_presentPipeline.get());
    graphicsState.setFramebuffer(&presentationFramebuffer);
    graphicsState.setViewport(viewportState);

    commandList.setGraphicsState(graphicsState);
    m_graphics.getDevice().getDescriptorHeap().bindGraphics(commandList, *m_deferredState.m_presentPipeline);
    const DeferredPresentPushConstants pushConstants = ResolveDeferredPresentPushConstants(
        targets.bindless.slotsBufferDescriptor.slot(),
        m_graphics.isHDR10OutputActive(),
        m_deferredState.m_presentationSettings,
        outputLayerImage.valid() ? outputLayerImage.slot() : Core::GpuDescriptorHandle::s_Invalid
    );
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));

    Core::DrawArguments drawArgs;
    drawArgs.setVertexCount(s_FullscreenTriangleVertexCount);
    commandList.draw(drawArgs);
    commandList.endRenderPass();
    return !commandList.isRenderPassActive();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
