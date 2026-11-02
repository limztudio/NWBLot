// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deferred_system.h"

#include <impl/ecs_render/kernel/descriptor_register_utils.h>
#include <impl/ecs_render/kernel/renderer_constants_private.h>
#include <impl/ecs_render/shadow/rt_shadow_state.h>

#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


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

    history.slots = targets.bindless.slots;
    history.slots.shadowVisibility = history.shadowVisibilityDescriptor.slot();
    history.slots.causticIrradiance = history.causticIrradianceDescriptor.slot();
    history.slots.surfelIrradiance = history.surfelIrradianceDescriptor.slot();

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
    ++s_NextLaggedLightingHistoryGeneration;
    if(s_NextLaggedLightingHistoryGeneration == 0u)
        ++s_NextLaggedLightingHistoryGeneration;
    history.generation = s_NextLaggedLightingHistoryGeneration;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

