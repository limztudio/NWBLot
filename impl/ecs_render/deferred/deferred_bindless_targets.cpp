// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deferred_system.h"

#include <impl/ecs_render/deferred/deferred_bindless_registrar.h>
#include <impl/ecs_render/kernel/descriptor_register_utils.h>

#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


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

    bindless.slots = BuildDeferredBindlessResourceSlots(
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


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

