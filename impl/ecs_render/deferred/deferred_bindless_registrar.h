// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>

#include <core/graphics/api.h>
#include <core/graphics/rhi/gpu_descriptor_heap.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DeferredBindlessDescriptorSources{
    Core::Sampler& sampler;
    Core::Sampler& avboitLinearSampler;
    Core::Buffer& sceneShadingBuffer;
    Core::Buffer& lightBuffer;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Registers every deferred frame target in the global descriptor heap, in the fixed order the slot table expects.
class DeferredBindlessDescriptorRegistrar final : NoCopy{
public:
    DeferredBindlessDescriptorRegistrar(Core::GpuDescriptorHeap& heap, DeferredFrameTargets& targets, const DeferredBindlessDescriptorSources& sources)noexcept
        : m_heap(heap)
        , m_targets(targets)
        , m_bindless(targets.bindless)
        , m_sources(sources)
    {}


public:
    [[nodiscard]] Expected<void> registerAll();


private:
    [[nodiscard]] Expected<void> registerLightingTargets();
    [[nodiscard]] Expected<void> registerTransparencyTargets();
    [[nodiscard]] Expected<void> registerSceneBuffers();
    [[nodiscard]] Expected<void> registerCsgIntervalTargets();
    [[nodiscard]] Expected<void> registerCausticResolveTargets();
    [[nodiscard]] Expected<void> registerSoftShadowTargets();


private:
    Core::GpuDescriptorHeap& m_heap;
    DeferredFrameTargets& m_targets;
    DeferredBindlessFrameResources& m_bindless;
    const DeferredBindlessDescriptorSources& m_sources;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

