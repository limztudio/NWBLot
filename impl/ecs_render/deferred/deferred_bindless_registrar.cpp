// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deferred_bindless_registrar.h"

#include <impl/ecs_render/deferred/deferred_descriptor_register.h>
#include <impl/ecs_render/caustic/rt_caustic_state.h>
#include <impl/ecs_render/kernel/renderer_constants_private.h>
#include <impl/ecs_render/shadow/rt_shadow_state.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DeferredDescriptorRegisterDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::GpuDescriptorHandle> RegisterDescriptor(Core::GpuDescriptorHeap& heap, const Core::GpuDescriptorClass::Enum descriptorClass, const Core::DescriptorWriteItem& item){
    const Expected<Core::GpuDescriptorHandle> allocated = heap.allocate(descriptorClass);
    if(!allocated)
        return MakeUnexpected(allocated.error());
    const Core::GpuDescriptorHandle handle = *allocated;
    Core::GpuDescriptorRetirementGuard retireUnpublished(heap, handle);

    if(heap.write(handle, item)){
        retireUnpublished.release();
        return handle;
    }
    return MakeUnexpected(Failure{});
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
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
        m_heap,
        Core::GpuDescriptorClass::SampledImage,
        m_targets.albedo,
        m_targets.albedoFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ); descriptor)
        m_bindless.gbufferBaseColor = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
        m_heap,
        Core::GpuDescriptorClass::SampledImage,
        m_targets.normal,
        m_targets.normalFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ); descriptor)
        m_bindless.gbufferNormal = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
        m_heap,
        Core::GpuDescriptorClass::SampledImage,
        m_targets.worldPosition,
        m_targets.worldPositionFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ); descriptor)
        m_bindless.gbufferWorldPosition = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
        m_heap,
        Core::GpuDescriptorClass::SampledImage,
        m_targets.specularRoughness,
        m_targets.specularRoughnessFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ); descriptor)
        m_bindless.gbufferSpecularRoughness = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
        m_heap,
        Core::GpuDescriptorClass::SampledImage,
        m_targets.depth,
        m_targets.depthFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ); descriptor)
        m_bindless.gbufferDepth = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
        m_heap,
        Core::GpuDescriptorClass::SampledImage2DArray,
        m_targets.shadowVisibility,
        m_targets.shadowVisibilityFormat,
        ECSRenderDetail::s_ShadowVisibilitySubresources,
        Core::TextureDimension::Texture2DArray
    ); descriptor)
        m_bindless.shadowVisibility = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(m_heap, m_targets.shadowVisibility, m_targets.shadowVisibilityFormat, Core::TextureDimension::Texture2DArray); descriptor)
        m_bindless.shadowVisibilityStorage = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
        m_heap,
        Core::GpuDescriptorClass::SampledImage,
        m_targets.causticIrradiance,
        m_targets.causticIrradianceFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ); descriptor)
        m_bindless.causticIrradiance = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(m_heap, m_targets.causticIrradiance, m_targets.causticIrradianceFormat, Core::TextureDimension::Texture2D); descriptor)
        m_bindless.causticIrradianceStorage = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
        m_heap,
        Core::GpuDescriptorClass::SampledImage,
        m_targets.surfelIrradiance,
        m_targets.surfelIrradianceFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ); descriptor)
        m_bindless.surfelIrradiance = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(m_heap, m_targets.surfelIrradiance, m_targets.surfelIrradianceFormat, Core::TextureDimension::Texture2D); descriptor)
        m_bindless.surfelIrradianceStorage = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
        m_heap,
        Core::GpuDescriptorClass::SampledImage,
        m_targets.surfelIrradianceHalf,
        m_targets.surfelIrradianceFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ); descriptor)
        m_bindless.surfelIrradianceHalf = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(m_heap, m_targets.surfelIrradianceHalf, m_targets.surfelIrradianceFormat, Core::TextureDimension::Texture2D); descriptor)
        m_bindless.surfelIrradianceHalfStorage = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampler(m_heap, m_sources.sampler); descriptor)
        m_bindless.sampler = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
        m_heap,
        Core::GpuDescriptorClass::SampledImage,
        m_targets.opaqueColor,
        m_targets.opaqueColorFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ); descriptor)
        m_bindless.opaqueColor = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(m_heap, m_targets.opaqueColor, m_targets.opaqueColorFormat, Core::TextureDimension::Texture2D); descriptor)
        m_bindless.opaqueColorStorage = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
        m_heap,
        Core::GpuDescriptorClass::SampledImage,
        m_targets.compositeColor,
        m_targets.compositeColorFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ); descriptor)
        m_bindless.compositeColor = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(m_heap, m_targets.compositeColor, m_targets.compositeColorFormat, Core::TextureDimension::Texture2D); descriptor)
        m_bindless.compositeColorStorage = *descriptor;
    else
        return MakeUnexpected(Failure{});
    return {};
}

Expected<void> DeferredBindlessDescriptorRegistrar::registerTransparencyTargets(){
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
        m_heap,
        Core::GpuDescriptorClass::SampledImage,
        m_targets.avboit.accumColor,
        m_targets.avboit.accumColorFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ); descriptor)
        m_bindless.avboitAccumColor = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
        m_heap,
        Core::GpuDescriptorClass::SampledImage,
        m_targets.avboit.accumExtinction,
        m_targets.avboit.accumExtinctionFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ); descriptor)
        m_bindless.avboitAccumExtinction = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
        m_heap,
        Core::GpuDescriptorClass::SampledImage,
        m_targets.avboit.refractionDepth,
        m_targets.depthFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ); descriptor)
        m_bindless.refractionDepth = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
        m_heap,
        Core::GpuDescriptorClass::SampledImage,
        m_targets.avboit.refractionNormalIor,
        Core::Format::RGBA16_FLOAT,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ); descriptor)
        m_bindless.refractionNormalIor = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
        m_heap,
        Core::GpuDescriptorClass::SampledImage,
        m_targets.avboit.refractionTintCoverage,
        Core::Format::RGBA16_FLOAT,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ); descriptor)
        m_bindless.refractionTintCoverage = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
        m_heap,
        Core::GpuDescriptorClass::SampledImage,
        m_targets.avboit.refractionInstance,
        Core::Format::RG32_FLOAT,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ); descriptor)
        m_bindless.refractionInstance = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
        m_heap,
        Core::GpuDescriptorClass::SampledImage,
        m_targets.avboit.refractionSpecularRoughness,
        Core::Format::RGBA16_FLOAT,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ); descriptor)
        m_bindless.refractionSpecularRoughness = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
        m_heap,
        Core::GpuDescriptorClass::SampledImage,
        m_targets.avboit.refractionResolve,
        Core::Format::RGBA16_FLOAT,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ); descriptor)
        m_bindless.refractionResolve = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
        m_heap,
        Core::GpuDescriptorClass::SampledImage,
        m_targets.avboit.foregroundAccumColor,
        m_targets.avboit.accumColorFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ); descriptor)
        m_bindless.avboitForegroundColor = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
        m_heap,
        Core::GpuDescriptorClass::SampledImage,
        m_targets.avboit.foregroundAccumExtinction,
        m_targets.avboit.accumExtinctionFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ); descriptor)
        m_bindless.avboitForegroundExtinction = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(m_heap, m_targets.avboit.refractionResolve, Core::Format::RGBA16_FLOAT, Core::TextureDimension::Texture2D); descriptor)
        m_bindless.refractionResolveStorage = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
        m_heap,
        Core::GpuDescriptorClass::SampledImage3D,
        m_targets.avboit.transmittanceTexture,
        m_targets.avboit.transmittanceFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture3D
    ); descriptor)
        m_bindless.avboitTransmittance = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampler(m_heap, m_sources.avboitLinearSampler); descriptor)
        m_bindless.avboitLinearSampler = *descriptor;
    else
        return MakeUnexpected(Failure{});
    return {};
}

Expected<void> DeferredBindlessDescriptorRegistrar::registerSceneBuffers(){
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterConstantBuffer(m_heap, m_sources.sceneShadingBuffer); descriptor)
        m_bindless.sceneShading = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStructuredBuffer(m_heap, m_sources.lightBuffer); descriptor)
        m_bindless.lightList = *descriptor;
    else
        return MakeUnexpected(Failure{});
    return {};
}

Expected<void> DeferredBindlessDescriptorRegistrar::registerCsgIntervalTargets(){
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(m_heap, m_targets.csgCapBackNormal, m_targets.csgCapNormalFormat, Core::TextureDimension::Texture2DArray); descriptor)
        m_bindless.csgCapBackNormal = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(m_heap, m_targets.csgIntervalDepth, m_targets.csgIntervalDepthFormat, Core::TextureDimension::Texture2DArray); descriptor)
        m_bindless.csgIntervalDepth = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(m_heap, m_targets.csgIntervalId, m_targets.csgIntervalIdFormat, Core::TextureDimension::Texture2DArray); descriptor)
        m_bindless.csgIntervalId = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(m_heap, m_targets.csgReceiverEventData, m_targets.csgReceiverEventDataFormat, Core::TextureDimension::Texture2DArray); descriptor)
        m_bindless.csgReceiverEventData = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(m_heap, m_targets.csgReceiverEventCount, m_targets.csgReceiverEventCountFormat, Core::TextureDimension::Texture2DArray); descriptor)
        m_bindless.csgReceiverEventCount = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(m_heap, m_targets.csgReceiverSpanData, m_targets.csgReceiverSpanDataFormat, Core::TextureDimension::Texture2DArray); descriptor)
        m_bindless.csgReceiverSpanData = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(m_heap, m_targets.csgReceiverSpanCount, m_targets.csgReceiverSpanCountFormat, Core::TextureDimension::Texture2DArray); descriptor)
        m_bindless.csgReceiverSpanCount = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(m_heap, m_targets.csgRemovedIntervalDepth, m_targets.csgRemovedIntervalDepthFormat, Core::TextureDimension::Texture2DArray); descriptor)
        m_bindless.csgRemovedIntervalDepth = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(m_heap, m_targets.csgRemovedIntervalCapNormal, m_targets.csgRemovedIntervalCapNormalFormat, Core::TextureDimension::Texture2DArray); descriptor)
        m_bindless.csgRemovedIntervalCapNormal = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(m_heap, m_targets.csgRemovedIntervalData, m_targets.csgRemovedIntervalDataFormat, Core::TextureDimension::Texture2DArray); descriptor)
        m_bindless.csgRemovedIntervalData = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(m_heap, m_targets.csgRemovedIntervalCount, m_targets.csgRemovedIntervalCountFormat, Core::TextureDimension::Texture2DArray); descriptor)
        m_bindless.csgRemovedIntervalCount = *descriptor;
    else
        return MakeUnexpected(Failure{});
    return {};
}

Expected<void> DeferredBindlessDescriptorRegistrar::registerCausticResolveTargets(){
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
        m_heap,
        Core::GpuDescriptorClass::SampledImage2DArrayUint,
        m_targets.causticAccumulator,
        m_targets.causticAccumulatorFormat,
        ECSRenderDetail::s_CausticAccumulatorSubresources,
        Core::TextureDimension::Texture2DArray
    ); descriptor)
        m_bindless.causticAccumulator = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(m_heap, m_targets.causticAccumulator, m_targets.causticAccumulatorFormat, Core::TextureDimension::Texture2DArray); descriptor)
        m_bindless.causticAccumulatorStorage = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
        m_heap,
        Core::GpuDescriptorClass::SampledImage,
        m_targets.causticHistory,
        m_targets.causticHistoryFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ); descriptor)
        m_bindless.causticHistory = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(m_heap, m_targets.causticHistory, m_targets.causticHistoryFormat, Core::TextureDimension::Texture2D); descriptor)
        m_bindless.causticHistoryStorage = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
        m_heap,
        Core::GpuDescriptorClass::SampledImage,
        m_targets.causticResolveHalf,
        m_targets.causticHistoryFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ); descriptor)
        m_bindless.causticResolveHalf = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(m_heap, m_targets.causticResolveHalf, m_targets.causticHistoryFormat, Core::TextureDimension::Texture2D); descriptor)
        m_bindless.causticResolveHalfStorage = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
        m_heap,
        Core::GpuDescriptorClass::SampledImage,
        m_targets.causticResolveGeometry,
        m_targets.causticHistoryFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ); descriptor)
        m_bindless.causticResolveGeometry = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(m_heap, m_targets.causticResolveGeometry, m_targets.causticHistoryFormat, Core::TextureDimension::Texture2D); descriptor)
        m_bindless.causticResolveGeometryStorage = *descriptor;
    else
        return MakeUnexpected(Failure{});
    return {};
}

Expected<void> DeferredBindlessDescriptorRegistrar::registerSoftShadowTargets(){
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(m_heap, m_targets.shadowCoarseTransmittance, m_targets.shadowCoarseTransmittanceFormat, Core::TextureDimension::Texture2DArray); descriptor)
        m_bindless.shadowCoarseTransmittanceStorage = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
        m_heap,
        Core::GpuDescriptorClass::SampledImage,
        m_targets.shadowSoftGeometry,
        m_targets.shadowSoftGeometryFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ); descriptor)
        m_bindless.shadowSoftGeometry = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(m_heap, m_targets.shadowSoftGeometry, m_targets.shadowSoftGeometryFormat, Core::TextureDimension::Texture2D); descriptor)
        m_bindless.shadowSoftGeometryStorage = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
        m_heap,
        Core::GpuDescriptorClass::SampledImage,
        m_targets.shadowSoftGeometryPrev,
        m_targets.shadowSoftGeometryFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ); descriptor)
        m_bindless.shadowSoftGeometryPrev = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(m_heap, m_targets.shadowSoftGeometryPrev, m_targets.shadowSoftGeometryFormat, Core::TextureDimension::Texture2D); descriptor)
        m_bindless.shadowSoftGeometryPrevStorage = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
        m_heap,
        Core::GpuDescriptorClass::SampledImage2DArray,
        m_targets.shadowSoftHalfA,
        m_targets.shadowSoftFormat,
        ECSRenderDetail::s_ShadowVisibilitySubresources,
        Core::TextureDimension::Texture2DArray
    ); descriptor)
        m_bindless.shadowSoftHalfA = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(m_heap, m_targets.shadowSoftHalfA, m_targets.shadowSoftFormat, Core::TextureDimension::Texture2DArray); descriptor)
        m_bindless.shadowSoftHalfAStorage = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
        m_heap,
        Core::GpuDescriptorClass::SampledImage2DArray,
        m_targets.shadowSoftHalfB,
        m_targets.shadowSoftFormat,
        ECSRenderDetail::s_ShadowVisibilitySubresources,
        Core::TextureDimension::Texture2DArray
    ); descriptor)
        m_bindless.shadowSoftHalfB = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(m_heap, m_targets.shadowSoftHalfB, m_targets.shadowSoftFormat, Core::TextureDimension::Texture2DArray); descriptor)
        m_bindless.shadowSoftHalfBStorage = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
        m_heap,
        Core::GpuDescriptorClass::SampledImage2DArray,
        m_targets.shadowHistA,
        m_targets.shadowSoftFormat,
        ECSRenderDetail::s_ShadowVisibilitySubresources,
        Core::TextureDimension::Texture2DArray
    ); descriptor)
        m_bindless.shadowHistA = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(m_heap, m_targets.shadowHistA, m_targets.shadowSoftFormat, Core::TextureDimension::Texture2DArray); descriptor)
        m_bindless.shadowHistAStorage = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
        m_heap,
        Core::GpuDescriptorClass::SampledImage2DArray,
        m_targets.shadowHistB,
        m_targets.shadowSoftFormat,
        ECSRenderDetail::s_ShadowVisibilitySubresources,
        Core::TextureDimension::Texture2DArray
    ); descriptor)
        m_bindless.shadowHistB = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(m_heap, m_targets.shadowHistB, m_targets.shadowSoftFormat, Core::TextureDimension::Texture2DArray); descriptor)
        m_bindless.shadowHistBStorage = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
        m_heap,
        Core::GpuDescriptorClass::SampledImage2DArray,
        m_targets.shadowMomentsA,
        m_targets.shadowSoftFormat,
        ECSRenderDetail::s_ShadowVisibilitySubresources,
        Core::TextureDimension::Texture2DArray
    ); descriptor)
        m_bindless.shadowMomentsA = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(m_heap, m_targets.shadowMomentsA, m_targets.shadowSoftFormat, Core::TextureDimension::Texture2DArray); descriptor)
        m_bindless.shadowMomentsAStorage = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
        m_heap,
        Core::GpuDescriptorClass::SampledImage2DArray,
        m_targets.shadowMomentsB,
        m_targets.shadowSoftFormat,
        ECSRenderDetail::s_ShadowVisibilitySubresources,
        Core::TextureDimension::Texture2DArray
    ); descriptor)
        m_bindless.shadowMomentsB = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(m_heap, m_targets.shadowMomentsB, m_targets.shadowSoftFormat, Core::TextureDimension::Texture2DArray); descriptor)
        m_bindless.shadowMomentsBStorage = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
        m_heap,
        Core::GpuDescriptorClass::SampledImage2DArray,
        m_targets.transparentSoftHalf,
        m_targets.shadowSoftFormat,
        ECSRenderDetail::s_ShadowVisibilitySubresources,
        Core::TextureDimension::Texture2DArray
    ); descriptor)
        m_bindless.transparentSoftHalf = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(m_heap, m_targets.transparentSoftHalf, m_targets.shadowSoftFormat, Core::TextureDimension::Texture2DArray); descriptor)
        m_bindless.transparentSoftHalfStorage = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
        m_heap,
        Core::GpuDescriptorClass::SampledImage2DArray,
        m_targets.transparentHistA,
        m_targets.shadowSoftFormat,
        ECSRenderDetail::s_ShadowVisibilitySubresources,
        Core::TextureDimension::Texture2DArray
    ); descriptor)
        m_bindless.transparentHistA = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(m_heap, m_targets.transparentHistA, m_targets.shadowSoftFormat, Core::TextureDimension::Texture2DArray); descriptor)
        m_bindless.transparentHistAStorage = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
        m_heap,
        Core::GpuDescriptorClass::SampledImage2DArray,
        m_targets.transparentHistB,
        m_targets.shadowSoftFormat,
        ECSRenderDetail::s_ShadowVisibilitySubresources,
        Core::TextureDimension::Texture2DArray
    ); descriptor)
        m_bindless.transparentHistB = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(m_heap, m_targets.transparentHistB, m_targets.shadowSoftFormat, Core::TextureDimension::Texture2DArray); descriptor)
        m_bindless.transparentHistBStorage = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
        m_heap,
        Core::GpuDescriptorClass::SampledImage2DArray,
        m_targets.transparentMomentsA,
        m_targets.shadowSoftFormat,
        ECSRenderDetail::s_ShadowVisibilitySubresources,
        Core::TextureDimension::Texture2DArray
    ); descriptor)
        m_bindless.transparentMomentsA = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(m_heap, m_targets.transparentMomentsA, m_targets.shadowSoftFormat, Core::TextureDimension::Texture2DArray); descriptor)
        m_bindless.transparentMomentsAStorage = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
        m_heap,
        Core::GpuDescriptorClass::SampledImage2DArray,
        m_targets.transparentMomentsB,
        m_targets.shadowSoftFormat,
        ECSRenderDetail::s_ShadowVisibilitySubresources,
        Core::TextureDimension::Texture2DArray
    ); descriptor)
        m_bindless.transparentMomentsB = *descriptor;
    else
        return MakeUnexpected(Failure{});
    if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(m_heap, m_targets.transparentMomentsB, m_targets.shadowSoftFormat, Core::TextureDimension::Texture2DArray); descriptor)
        m_bindless.transparentMomentsBStorage = *descriptor;
    else
        return MakeUnexpected(Failure{});
    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

