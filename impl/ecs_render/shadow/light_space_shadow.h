// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "light_space_plan.h"

#include <core/alloc/global.h>
#include <core/graphics/rhi/pipeline.h>
#include <core/graphics/rhi/gpu_descriptor_heap.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuTimingRecorder;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct LightSpaceShadowPush{
    u32 viewSlot = 0u;
    u32 viewIndex = 0u;
    u32 materialContextSlot = 0u;
    u32 countsSlot = 0u;
    u32 eventsSlot = 0u;
    u32 depthSlot = 0u;
    u32 instanceIndex = 0u;
    u32 instanceCount = 0u;
    u32 width = 0u;
    u32 height = 0u;
    u32 frameIndex = 0u;
    u32 sampleCount = 0u;
    u32 deferredResourcesSlot = 0u;
    u32 outputSlot = 0u;
    u32 sceneRootSlot = 0u;
    u32 viewCount = 0u;
};
static_assert(sizeof(LightSpaceShadowPush) == NWB_LIGHT_SPACE_PUSH_BYTES);

struct LightSpaceShadowStorageCapacity{
    u64 countBytes = 0u;
    u64 eventBytes = 0u;
    u64 viewBytes = 0u;
    u64 drawArgumentBytes = 0u;
    u32 textureResolution = 0u;
    u32 arrayLayers = 0u;
};

[[nodiscard]] bool LightSpaceShadowStorageFits(const LightSpacePlan& plan, const LightSpaceShadowStorageCapacity& capacity)noexcept;

struct LightSpaceShadowCaster{
    u32 instanceIndex = 0u;
    u32 vertexCount = 0u;
    bool transparent = false;
};

// Resource handles freeze one allocation generation. Graph tasks must copy the caster span into their arena.
struct LightSpaceShadowSnapshot{
    LightSpacePlan plan;
    Core::BufferHandle counts;
    Core::BufferHandle events;
    Core::BufferHandle views;
    Core::BufferHandle drawArguments;
    Core::TextureHandle depth;
    Core::GpuDescriptorHandle countsDescriptor;
    Core::GpuDescriptorHandle eventsDescriptor;
    Core::GpuDescriptorHandle viewsDescriptor;
    Core::GpuDescriptorHandle drawArgumentsDescriptor;
    Core::GpuDescriptorHandle depthDescriptor;
    Array<Core::FramebufferHandle, NWB_SCENE_SHADOW_SLOT_COUNT * 6u> opaqueFramebuffers;
    Array<Core::FramebufferHandle, NWB_SCENE_SHADOW_SLOT_COUNT * 6u> transparentFramebuffers;
    Core::BindingLayoutHandle layout;
    Core::ComputePipelineHandle viewPipeline;
    Core::ComputePipelineHandle opaqueResolve;
    Core::ComputePipelineHandle transparentResolve;
    Core::ComputePipelineHandle opaqueFallback;
    Core::ComputePipelineHandle transparentFallback;
    Core::ComputePipelineHandle shadePipeline;
    Core::GraphicsPipelineHandle opaqueCapture;
    Core::GraphicsPipelineHandle transparentCapture;
    LightSpaceShadowPush push;
    const LightSpaceShadowCaster* casters = nullptr;
    usize casterCount = 0u;
    bool ready = false;
};

struct LightSpaceShadowState{
    SoftwareShadowSettings m_settings;
    LightSpaceShadowSnapshot m_snapshot;
    Vector<LightSpaceShadowCaster, Core::Alloc::GlobalArena> m_casters;
    static constexpr usize s_LightSpaceShaderCount = 8u;
    Core::ShaderHandle m_shaders[s_LightSpaceShaderCount];
    bool m_sceneEligible = false;
    bool m_resourcesPrepared = false;
    bool m_pipelineFailed = false;
    bool m_dispatchLogged = false;

    explicit LightSpaceShadowState(Core::Alloc::GlobalArena& arena) : m_casters(arena){}
};

// Graph tasks own uploads, clears, and entry/exit states. Resolve owns its internal map-to-fallback UAV dependency.
[[nodiscard]] bool RecordLightSpaceViews(Core::CommandList& commandList, Core::GpuDescriptorHeap& heap, const LightSpaceShadowSnapshot& snapshot);
[[nodiscard]] bool RecordLightSpaceCapture(
    Core::CommandList& commandList, Core::GpuDescriptorHeap& heap, const LightSpaceShadowSnapshot& snapshot, u32 viewIndex, bool transparent
);
[[nodiscard]] bool RecordLightSpaceShade(Core::CommandList& commandList, Core::GpuDescriptorHeap& heap, const LightSpaceShadowSnapshot& snapshot);
[[nodiscard]] bool RecordLightSpaceResolve(
    Core::CommandList& commandList, Core::GpuDescriptorHeap& heap, Core::GpuTimingRecorder& timing,
    const LightSpaceShadowSnapshot& snapshot,
    Core::Texture& outputTexture, u32 frameIndex, u32 sampleCount, u32 outputSlot, bool transparent
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

