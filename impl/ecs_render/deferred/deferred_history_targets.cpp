// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deferred_system.h"

#include <impl/ecs_render/deferred/deferred_descriptor_register.h>
#include <impl/ecs_render/kernel/renderer_format_private.h>
#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/ecs_render/deferred/renderer_deferred_state.h>
#include <impl/ecs_render/kernel/renderer_constants_private.h>
#include <impl/assets/graphics/mesh/runtime_constants.h>

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

bool RendererDeferredSystem::createLaggedLightingHistoryResources(DeferredFrameTargets& targets){
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


    const auto descriptorsRegistered = [&]() -> Expected<void>{
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage2DArray,
            history.shadowVisibility.get(),
            targets.shadowVisibilityFormat,
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::TextureDimension::Texture2DArray
        ); descriptor)
            history.shadowVisibilityDescriptor = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage,
            history.causticIrradiance.get(),
            targets.causticIrradianceFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ); descriptor)
            history.causticIrradianceDescriptor = *descriptor;
        else
            return MakeUnexpected(Failure{});
        if(const auto descriptor = DeferredDescriptorRegisterDetail::RegisterSampledTexture(
            heap,
            Core::GpuDescriptorClass::SampledImage,
            history.surfelIrradiance.get(),
            targets.surfelIrradianceFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ); descriptor)
            history.surfelIrradianceDescriptor = *descriptor;
        else
            return MakeUnexpected(Failure{});
        return {};
    };
    if(!descriptorsRegistered()){
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

    const Expected<Core::GpuDescriptorHandle> slotsBufferDescriptor = heap.allocate(Core::GpuDescriptorClass::UniformBuffer);
    history.slotsBufferDescriptor = slotsBufferDescriptor ? *slotsBufferDescriptor : Core::GpuDescriptorHandle::Invalid();
    if(!slotsBufferDescriptor || !heap.write(history.slotsBufferDescriptor, Core::DescriptorWriteItem::ConstantBuffer(0u, history.slotsBuffer.get()))){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register lagged lighting-history slot buffer in the descriptor heap"));
        resetLaggedLightingHistoryResources(targets);
        return false;
    }

    static u64 s_nextLaggedLightingHistoryGeneration = 0u;
    ++s_nextLaggedLightingHistoryGeneration;
    if(s_nextLaggedLightingHistoryGeneration == 0u)
        ++s_nextLaggedLightingHistoryGeneration;
    history.generation = s_nextLaggedLightingHistoryGeneration;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

