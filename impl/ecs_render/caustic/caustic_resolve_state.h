// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/assets/graphics/caustic/resolve_binding_slots.h>
#include <impl/ecs_render/caustic/settings.adhd.h>
#include <impl/ecs_render/deferred/deferred_frame_targets.h>

#include <core/graphics/rhi/device.h>
#include <core/graphics/rhi/gpu_descriptor_heap.h>
#include <core/graphics/rhi/pipeline.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_CausticResolveActivityBufferCount = 2u;
inline constexpr usize s_CausticResolveWaveletPassCount = NWB_CAUSTIC_RESOLVE_PASS_COUNT;

inline constexpr Array<Name, s_CausticResolveActivityBufferCount> s_CausticResolveActivityBufferNames = {
    Name("caustic_resolve_activity_a"),
    Name("caustic_resolve_activity_b"),
};

static_assert(NWB_CAUSTIC_RESOLVE_GROUP_SIZE * NWB_CAUSTIC_RESOLVE_GROUP_SIZE == 64u);

struct CausticResolveActivitySnapshot{
    Array<Core::BufferHandle, s_CausticResolveActivityBufferCount> buffers{};
    Array<Core::GpuDescriptorHandle, s_CausticResolveActivityBufferCount> descriptors = {
        Core::GpuDescriptorHandle::Invalid(),
        Core::GpuDescriptorHandle::Invalid(),
    };
    u32 halfWidth = 0u;
    u32 halfHeight = 0u;


public:
    [[nodiscard]] b8 valid()const noexcept;
    [[nodiscard]] b8 matches(const CausticResolveActivitySnapshot& other)const noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CausticResolveStageState{
    Core::ShaderHandle m_shader;
    Core::ComputePipelineHandle m_pipeline;
};

struct CausticResolveState{
    Core::BindingLayoutHandle m_bindingLayout;
    CausticResolveActivitySnapshot m_activity;
    CausticResolveStageState m_prepare;
    CausticResolveStageState m_wavelet;
    CausticResolveStageState m_waveletStepOne;
    CausticResolveStageState m_waveletStepTwo;
    CausticResolveStageState m_waveletDirect;
    CausticResolveStageState m_upsample;
    b8 m_failed = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] constexpr f32 EffectiveCausticIntensity(const f32 decay)noexcept{
    return decay > 0.0f ? s_CausticIntensity * (1.0f - decay) : s_CausticIntensity;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RayTracingCausticsTaskDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// A ping-pong resolve target with sampled and storage slots.
struct CausticResolvePassResources{
    Core::Texture* texture = nullptr;
    u32 sampledSlot = 0u;
    u32 storageSlot = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void EnableCausticResolveBarriers(Core::CommandList& commandList, DeferredFrameTargets& targets);

[[nodiscard]] inline CausticResolvePassResources MakeCausticHistoryResolvePassResources(DeferredFrameTargets& targets)noexcept{
    return { targets.causticHistory.get(), targets.bindless.causticHistory.slot(), targets.bindless.causticHistoryStorage.slot() };
}

[[nodiscard]] inline CausticResolvePassResources MakeCausticHalfResolvePassResources(DeferredFrameTargets& targets)noexcept{
    return { targets.causticResolveHalf.get(), targets.bindless.causticResolveHalf.slot(), targets.bindless.causticResolveHalfStorage.slot() };
}

[[nodiscard]] inline CausticResolvePassResources MakeCausticIrradianceResolvePassResources(DeferredFrameTargets& targets)noexcept{
    return { targets.causticIrradiance.get(), targets.bindless.causticIrradiance.slot(), targets.bindless.causticIrradianceStorage.slot() };
}

void DispatchCausticResolvePass(
    Core::CommandList& commandList,
    Core::GpuDescriptorHeap& heap,
    Core::ComputePipeline& pipeline,
    DeferredFrameTargets& targets,
    const CausticResolvePassResources& input,
    const CausticResolvePassResources& output,
    f32 effectiveIntensity,
    u32 stepWidth,
    CausticResolveStage::Enum stage,
    u32 groupsX,
    u32 groupsY,
    const CausticResolveActivitySnapshot& activity = {}
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

