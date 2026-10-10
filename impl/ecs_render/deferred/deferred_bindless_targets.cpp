// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deferred_system.h"

#include <impl/ecs_render/deferred/deferred_descriptor_register.h>
#include <impl/ecs_render/deferred/renderer_deferred_state.h>
#include <impl/ecs_render/kernel/renderer_constants_private.h>

#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>

#include <impl/assets/graphics/mesh/runtime_constants.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererDeferredSystem::createDeferredBindlessFrameResources(
    DeferredFrameTargets& targets,
    Core::Sampler& avboitLinearSampler
){
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
    const auto registered = [&]() -> Expected<void>{
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage,
            targets.albedo.get(),
            targets.albedoFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ); descriptor)
            bindless.gbufferBaseColor = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage,
            targets.normal.get(),
            targets.normalFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ); descriptor)
            bindless.gbufferNormal = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage,
            targets.worldPosition.get(),
            targets.worldPositionFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ); descriptor)
            bindless.gbufferWorldPosition = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage,
            targets.specularRoughness.get(),
            targets.specularRoughnessFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ); descriptor)
            bindless.gbufferSpecularRoughness = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage,
            targets.depth.get(),
            targets.depthFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ); descriptor)
            bindless.gbufferDepth = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage2DArray,
            targets.shadowVisibility.get(),
            targets.shadowVisibilityFormat,
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::TextureDimension::Texture2DArray
        ); descriptor)
            bindless.shadowVisibility = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(
            heap,
            targets.shadowVisibility.get(),
            targets.shadowVisibilityFormat,
            Core::TextureDimension::Texture2DArray
        ); descriptor)
            bindless.shadowVisibilityStorage = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage,
            targets.causticIrradiance.get(),
            targets.causticIrradianceFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ); descriptor)
            bindless.causticIrradiance = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(
            heap,
            targets.causticIrradiance.get(),
            targets.causticIrradianceFormat,
            Core::TextureDimension::Texture2D
        ); descriptor)
            bindless.causticIrradianceStorage = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage,
            targets.surfelIrradiance.get(),
            targets.surfelIrradianceFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ); descriptor)
            bindless.surfelIrradiance = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(
            heap,
            targets.surfelIrradiance.get(),
            targets.surfelIrradianceFormat,
            Core::TextureDimension::Texture2D
        ); descriptor)
            bindless.surfelIrradianceStorage = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage,
            targets.surfelIrradianceHalf.get(),
            targets.surfelIrradianceFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ); descriptor)
            bindless.surfelIrradianceHalf = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(
            heap,
            targets.surfelIrradianceHalf.get(),
            targets.surfelIrradianceFormat,
            Core::TextureDimension::Texture2D
        ); descriptor)
            bindless.surfelIrradianceHalfStorage = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampler(
            heap,
            m_deferredState.m_sampler.get()
        ); descriptor)
            bindless.sampler = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage,
            targets.opaqueColor.get(),
            targets.opaqueColorFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ); descriptor)
            bindless.opaqueColor = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(
            heap,
            targets.opaqueColor.get(),
            targets.opaqueColorFormat,
            Core::TextureDimension::Texture2D
        ); descriptor)
            bindless.opaqueColorStorage = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage,
            targets.compositeColor.get(),
            targets.compositeColorFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ); descriptor)
            bindless.compositeColor = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(
            heap,
            targets.compositeColor.get(),
            targets.compositeColorFormat,
            Core::TextureDimension::Texture2D
        ); descriptor)
            bindless.compositeColorStorage = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage,
            targets.avboit.accumColor.get(),
            targets.avboit.accumColorFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ); descriptor)
            bindless.avboitAccumColor = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage,
            targets.avboit.accumExtinction.get(),
            targets.avboit.accumExtinctionFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ); descriptor)
            bindless.avboitAccumExtinction = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage,
            targets.avboit.refractionDepth.get(),
            targets.depthFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ); descriptor)
            bindless.refractionDepth = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage,
            targets.avboit.refractionNormalIor.get(),
            Core::Format::RGBA16_FLOAT,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ); descriptor)
            bindless.refractionNormalIor = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage,
            targets.avboit.refractionTintCoverage.get(),
            Core::Format::RGBA16_FLOAT,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ); descriptor)
            bindless.refractionTintCoverage = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage,
            targets.avboit.refractionInstance.get(),
            Core::Format::RG32_FLOAT,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ); descriptor)
            bindless.refractionInstance = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage,
            targets.avboit.refractionSpecularRoughness.get(),
            Core::Format::RGBA16_FLOAT,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ); descriptor)
            bindless.refractionSpecularRoughness = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage,
            targets.avboit.refractionResolve.get(),
            Core::Format::RGBA16_FLOAT,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ); descriptor)
            bindless.refractionResolve = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage,
            targets.avboit.foregroundAccumColor.get(),
            targets.avboit.accumColorFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ); descriptor)
            bindless.avboitForegroundColor = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage,
            targets.avboit.foregroundAccumExtinction.get(),
            targets.avboit.accumExtinctionFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ); descriptor)
            bindless.avboitForegroundExtinction = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(
            heap,
            targets.avboit.refractionResolve.get(),
            Core::Format::RGBA16_FLOAT,
            Core::TextureDimension::Texture2D
        ); descriptor)
            bindless.refractionResolveStorage = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage3D,
            targets.avboit.transmittanceTexture.get(),
            targets.avboit.transmittanceFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture3D
        ); descriptor)
            bindless.avboitTransmittance = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampler(heap, &avboitLinearSampler); descriptor)
            bindless.avboitLinearSampler = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterConstantBuffer(
            heap,
            m_deferredState.m_sceneShadingBuffer.get()
        ); descriptor)
            bindless.sceneShading = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStructuredBuffer(
            heap,
            m_deferredState.m_lightBuffer.get()
        ); descriptor)
            bindless.lightList = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(
            heap,
            targets.csgCapBackNormal.get(),
            targets.csgCapNormalFormat,
            Core::TextureDimension::Texture2DArray
        ); descriptor)
            bindless.csgCapBackNormal = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(
            heap,
            targets.csgIntervalDepth.get(),
            targets.csgIntervalDepthFormat,
            Core::TextureDimension::Texture2DArray
        ); descriptor)
            bindless.csgIntervalDepth = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(
            heap,
            targets.csgIntervalId.get(),
            targets.csgIntervalIdFormat,
            Core::TextureDimension::Texture2DArray
        ); descriptor)
            bindless.csgIntervalId = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(
            heap,
            targets.csgReceiverEventData.get(),
            targets.csgReceiverEventDataFormat,
            Core::TextureDimension::Texture2DArray
        ); descriptor)
            bindless.csgReceiverEventData = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(
            heap,
            targets.csgReceiverEventCount.get(),
            targets.csgReceiverEventCountFormat,
            Core::TextureDimension::Texture2DArray
        ); descriptor)
            bindless.csgReceiverEventCount = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(
            heap,
            targets.csgReceiverSpanData.get(),
            targets.csgReceiverSpanDataFormat,
            Core::TextureDimension::Texture2DArray
        ); descriptor)
            bindless.csgReceiverSpanData = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(
            heap,
            targets.csgReceiverSpanCount.get(),
            targets.csgReceiverSpanCountFormat,
            Core::TextureDimension::Texture2DArray
        ); descriptor)
            bindless.csgReceiverSpanCount = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(
            heap,
            targets.csgRemovedIntervalDepth.get(),
            targets.csgRemovedIntervalDepthFormat,
            Core::TextureDimension::Texture2DArray
        ); descriptor)
            bindless.csgRemovedIntervalDepth = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(
            heap,
            targets.csgRemovedIntervalCapNormal.get(),
            targets.csgRemovedIntervalCapNormalFormat,
            Core::TextureDimension::Texture2DArray
        ); descriptor)
            bindless.csgRemovedIntervalCapNormal = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(
            heap,
            targets.csgRemovedIntervalData.get(),
            targets.csgRemovedIntervalDataFormat,
            Core::TextureDimension::Texture2DArray
        ); descriptor)
            bindless.csgRemovedIntervalData = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(
            heap,
            targets.csgRemovedIntervalCount.get(),
            targets.csgRemovedIntervalCountFormat,
            Core::TextureDimension::Texture2DArray
        ); descriptor)
            bindless.csgRemovedIntervalCount = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage2DArrayUint,
            targets.causticAccumulator.get(),
            targets.causticAccumulatorFormat,
            ECSRenderDetail::s_CausticAccumulatorSubresources,
            Core::TextureDimension::Texture2DArray
        ); descriptor)
            bindless.causticAccumulator = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(
            heap,
            targets.causticAccumulator.get(),
            targets.causticAccumulatorFormat,
            Core::TextureDimension::Texture2DArray
        ); descriptor)
            bindless.causticAccumulatorStorage = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage,
            targets.causticHistory.get(),
            targets.causticHistoryFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ); descriptor)
            bindless.causticHistory = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(
            heap,
            targets.causticHistory.get(),
            targets.causticHistoryFormat,
            Core::TextureDimension::Texture2D
        ); descriptor)
            bindless.causticHistoryStorage = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage,
            targets.causticResolveHalf.get(),
            targets.causticHistoryFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ); descriptor)
            bindless.causticResolveHalf = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(
            heap,
            targets.causticResolveHalf.get(),
            targets.causticHistoryFormat,
            Core::TextureDimension::Texture2D
        ); descriptor)
            bindless.causticResolveHalfStorage = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage,
            targets.causticResolveGeometry.get(),
            targets.causticHistoryFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ); descriptor)
            bindless.causticResolveGeometry = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(
            heap,
            targets.causticResolveGeometry.get(),
            targets.causticHistoryFormat,
            Core::TextureDimension::Texture2D
        ); descriptor)
            bindless.causticResolveGeometryStorage = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(
            heap,
            targets.shadowCoarseTransmittance.get(),
            targets.shadowCoarseTransmittanceFormat,
            Core::TextureDimension::Texture2DArray
        ); descriptor)
            bindless.shadowCoarseTransmittanceStorage = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage,
            targets.shadowSoftGeometry.get(),
            targets.shadowSoftGeometryFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ); descriptor)
            bindless.shadowSoftGeometry = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(
            heap,
            targets.shadowSoftGeometry.get(),
            targets.shadowSoftGeometryFormat,
            Core::TextureDimension::Texture2D
        ); descriptor)
            bindless.shadowSoftGeometryStorage = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage,
            targets.shadowSoftGeometryPrev.get(),
            targets.shadowSoftGeometryFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ); descriptor)
            bindless.shadowSoftGeometryPrev = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(
            heap,
            targets.shadowSoftGeometryPrev.get(),
            targets.shadowSoftGeometryFormat,
            Core::TextureDimension::Texture2D
        ); descriptor)
            bindless.shadowSoftGeometryPrevStorage = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage2DArray,
            targets.shadowSoftHalfA.get(),
            targets.shadowSoftFormat,
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::TextureDimension::Texture2DArray
        ); descriptor)
            bindless.shadowSoftHalfA = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(
            heap,
            targets.shadowSoftHalfA.get(),
            targets.shadowSoftFormat,
            Core::TextureDimension::Texture2DArray
        ); descriptor)
            bindless.shadowSoftHalfAStorage = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage2DArray,
            targets.shadowSoftHalfB.get(),
            targets.shadowSoftFormat,
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::TextureDimension::Texture2DArray
        ); descriptor)
            bindless.shadowSoftHalfB = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(
            heap,
            targets.shadowSoftHalfB.get(),
            targets.shadowSoftFormat,
            Core::TextureDimension::Texture2DArray
        ); descriptor)
            bindless.shadowSoftHalfBStorage = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage2DArray,
            targets.shadowHistA.get(),
            targets.shadowSoftFormat,
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::TextureDimension::Texture2DArray
        ); descriptor)
            bindless.shadowHistA = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(
            heap,
            targets.shadowHistA.get(),
            targets.shadowSoftFormat,
            Core::TextureDimension::Texture2DArray
        ); descriptor)
            bindless.shadowHistAStorage = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage2DArray,
            targets.shadowHistB.get(),
            targets.shadowSoftFormat,
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::TextureDimension::Texture2DArray
        ); descriptor)
            bindless.shadowHistB = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(
            heap,
            targets.shadowHistB.get(),
            targets.shadowSoftFormat,
            Core::TextureDimension::Texture2DArray
        ); descriptor)
            bindless.shadowHistBStorage = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage2DArray,
            targets.shadowMomentsA.get(),
            targets.shadowSoftFormat,
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::TextureDimension::Texture2DArray
        ); descriptor)
            bindless.shadowMomentsA = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(
            heap,
            targets.shadowMomentsA.get(),
            targets.shadowSoftFormat,
            Core::TextureDimension::Texture2DArray
        ); descriptor)
            bindless.shadowMomentsAStorage = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage2DArray,
            targets.shadowMomentsB.get(),
            targets.shadowSoftFormat,
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::TextureDimension::Texture2DArray
        ); descriptor)
            bindless.shadowMomentsB = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(
            heap,
            targets.shadowMomentsB.get(),
            targets.shadowSoftFormat,
            Core::TextureDimension::Texture2DArray
        ); descriptor)
            bindless.shadowMomentsBStorage = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage2DArray,
            targets.transparentSoftHalf.get(),
            targets.shadowSoftFormat,
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::TextureDimension::Texture2DArray
        ); descriptor)
            bindless.transparentSoftHalf = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(
            heap,
            targets.transparentSoftHalf.get(),
            targets.shadowSoftFormat,
            Core::TextureDimension::Texture2DArray
        ); descriptor)
            bindless.transparentSoftHalfStorage = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage2DArray,
            targets.transparentHistA.get(),
            targets.shadowSoftFormat,
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::TextureDimension::Texture2DArray
        ); descriptor)
            bindless.transparentHistA = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(
            heap,
            targets.transparentHistA.get(),
            targets.shadowSoftFormat,
            Core::TextureDimension::Texture2DArray
        ); descriptor)
            bindless.transparentHistAStorage = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage2DArray,
            targets.transparentHistB.get(),
            targets.shadowSoftFormat,
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::TextureDimension::Texture2DArray
        ); descriptor)
            bindless.transparentHistB = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(
            heap,
            targets.transparentHistB.get(),
            targets.shadowSoftFormat,
            Core::TextureDimension::Texture2DArray
        ); descriptor)
            bindless.transparentHistBStorage = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage2DArray,
            targets.transparentMomentsA.get(),
            targets.shadowSoftFormat,
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::TextureDimension::Texture2DArray
        ); descriptor)
            bindless.transparentMomentsA = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(
            heap,
            targets.transparentMomentsA.get(),
            targets.shadowSoftFormat,
            Core::TextureDimension::Texture2DArray
        ); descriptor)
            bindless.transparentMomentsAStorage = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage2DArray,
            targets.transparentMomentsB.get(),
            targets.shadowSoftFormat,
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::TextureDimension::Texture2DArray
        ); descriptor)
            bindless.transparentMomentsB = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterStorageTexture(
            heap,
            targets.transparentMomentsB.get(),
            targets.shadowSoftFormat,
            Core::TextureDimension::Texture2DArray
        ); descriptor)
            bindless.transparentMomentsBStorage = *descriptor;
        else
            return MakeUnexpected(Failure{});
        return {};
    };
    if(!registered()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register deferred frame resources in the descriptor heap"));
        resetDeferredBindlessFrameResources(targets);
        return false;
    }

    bindless.slots.gbufferBaseColor = bindless.gbufferBaseColor.slot();
    bindless.slots.gbufferNormal = bindless.gbufferNormal.slot();
    bindless.slots.gbufferWorldPosition = bindless.gbufferWorldPosition.slot();
    bindless.slots.gbufferDepth = bindless.gbufferDepth.slot();
    bindless.slots.shadowVisibility = bindless.shadowVisibility.slot();
    bindless.slots.causticIrradiance = bindless.causticIrradiance.slot();
    bindless.slots.surfelIrradiance = bindless.surfelIrradiance.slot();
    bindless.slots.sampler = bindless.sampler.slot();
    bindless.slots.opaqueColor = bindless.opaqueColor.slot();
    bindless.slots.opaqueColorStorage = bindless.opaqueColorStorage.slot();
    bindless.slots.compositeColor = bindless.compositeColor.slot();
    bindless.slots.compositeColorStorage = bindless.compositeColorStorage.slot();
    bindless.slots.avboitAccumColor = bindless.avboitAccumColor.slot();
    bindless.slots.avboitAccumExtinction = bindless.avboitAccumExtinction.slot();
    bindless.slots.refractionDepth = bindless.refractionDepth.slot();
    bindless.slots.refractionNormalIor = bindless.refractionNormalIor.slot();
    bindless.slots.refractionTintCoverage = bindless.refractionTintCoverage.slot();
    bindless.slots.refractionInstance = bindless.refractionInstance.slot();
    bindless.slots.refractionResolve = bindless.refractionResolve.slot();
    bindless.slots.avboitForegroundColor = bindless.avboitForegroundColor.slot();
    bindless.slots.avboitForegroundExtinction = bindless.avboitForegroundExtinction.slot();
    bindless.slots.refractionResolveStorage = bindless.refractionResolveStorage.slot();
    bindless.slots.avboitTransmittance = bindless.avboitTransmittance.slot();
    bindless.slots.avboitLinearSampler = bindless.avboitLinearSampler.slot();
    bindless.slots.sceneShading = bindless.sceneShading.slot();
    bindless.slots.lightList = bindless.lightList.slot();
    bindless.slots.csgCapBackNormal = bindless.csgCapBackNormal.slot();
    bindless.slots.csgIntervalDepth = bindless.csgIntervalDepth.slot();
    bindless.slots.csgIntervalId = bindless.csgIntervalId.slot();
    bindless.slots.csgReceiverEventData = bindless.csgReceiverEventData.slot();
    bindless.slots.csgReceiverEventCount = bindless.csgReceiverEventCount.slot();
    bindless.slots.csgReceiverSpanData = bindless.csgReceiverSpanData.slot();
    bindless.slots.csgReceiverSpanCount = bindless.csgReceiverSpanCount.slot();
    bindless.slots.csgRemovedIntervalDepth = bindless.csgRemovedIntervalDepth.slot();
    bindless.slots.csgRemovedIntervalCapNormal = bindless.csgRemovedIntervalCapNormal.slot();
    bindless.slots.csgRemovedIntervalData = bindless.csgRemovedIntervalData.slot();
    bindless.slots.csgRemovedIntervalCount = bindless.csgRemovedIntervalCount.slot();

    Core::BufferDesc slotsBufferDesc;
    slotsBufferDesc
        .setByteSize(sizeof(DeferredBindlessResourceSlots))
        .setIsConstantBuffer(true)
        .setDebugName("ECSRender_DeferredBindlessResourceSlots")
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        // Selector spans packets; retain descriptor-visible state at every native close.
        .enableAutomaticStateTracking(Core::ResourceStates::ConstantBuffer)
    ;
    bindless.slotsBuffer = m_graphics.createBuffer(slotsBufferDesc);
    if(!bindless.slotsBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred bindless slot buffer"));
        resetDeferredBindlessFrameResources(targets);
        return false;
    }

    // The indirection payload is itself heap-addressable.  Every consumer receives this one UniformBuffer slot in
    // push constants instead of binding a local selector CBV, keeping the descriptor heap as the only resource
    // binding surface for ordinary renderer passes.
    bindless.slotsBufferDescriptor = heap.allocate(Core::GpuDescriptorClass::UniformBuffer);
    if(
        !bindless.slotsBufferDescriptor.valid()
        || !heap.write(
            bindless.slotsBufferDescriptor,
            Core::DescriptorWriteItem::ConstantBuffer(0u, bindless.slotsBuffer.get())
        )
    ){
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


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

