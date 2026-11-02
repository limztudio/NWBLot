// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deferred_bindless_registrar.h"

#include <impl/ecs_render/caustic/rt_caustic_state.h>
#include <impl/ecs_render/kernel/descriptor_register_utils.h>
#include <impl/ecs_render/kernel/renderer_constants_private.h>
#include <impl/ecs_render/shadow/rt_shadow_state.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


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


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

