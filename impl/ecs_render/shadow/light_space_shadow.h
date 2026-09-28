// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "light_space_plan.h"
#include "light_space_capture_history.h"
#include "light_space_csg.h"

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
    u32 csgContextSlot = 0u;
    u32 csgOpaqueDepthSlot = 0u;
    u32 csgFlags = 0u;
    u32 receiverFactor = 2u;
    u32 captureDrawCount = 0u;
    u32 casterDrawOffset = 0u;
    u32 meshletDescSlot = 0u;
    u32 meshletBoundsSlot = 0u;
    u32 meshletCount = 0u;
};
static_assert(sizeof(LightSpaceShadowPush) == NWB_LIGHT_SPACE_PUSH_BYTES);
static_assert(offsetof(LightSpaceShadowPush, receiverFactor) == 76u);
static_assert(offsetof(LightSpaceShadowPush, captureDrawCount) == 80u);
static_assert(offsetof(LightSpaceShadowPush, casterDrawOffset) == 84u);
static_assert(offsetof(LightSpaceShadowPush, meshletDescSlot) == 88u);
static_assert(offsetof(LightSpaceShadowPush, meshletBoundsSlot) == 92u);
static_assert(offsetof(LightSpaceShadowPush, meshletCount) == 96u);

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
    u32 indexCount = 0u;
    bool transparent = false;
    bool csg = false;
    Core::BufferHandle triangleIndexBuffer;
    u32 meshletCount = 0u;
    u32 meshletDescSlot = 0u;
    u32 meshletBoundsSlot = 0u;
    Core::BufferHandle meshletDescBuffer;
    Core::BufferHandle meshletBoundsBuffer;
};

[[nodiscard]] u32 LightSpaceShadowDrawCount(const LightSpaceShadowCaster* casters, usize casterCount)noexcept;

// Resource handles freeze one allocation generation. Graph tasks must copy the caster span into their arena.
struct LightSpaceShadowSnapshot{
    LightSpacePlan plan;
    Core::BufferHandle counts;
    Core::BufferHandle events;
    Core::BufferHandle views;
    Core::BufferHandle drawArguments;
    Core::BufferHandle csgContext;
    Core::BufferHandle csgOpaqueDepth;
    Core::TextureHandle depth;
    Core::GpuDescriptorHandle countsDescriptor;
    Core::GpuDescriptorHandle eventsDescriptor;
    Core::GpuDescriptorHandle viewsDescriptor;
    Core::GpuDescriptorHandle drawArgumentsDescriptor;
    Core::GpuDescriptorHandle csgContextDescriptor;
    Core::GpuDescriptorHandle csgOpaqueDepthDescriptor;
    Core::GpuDescriptorHandle depthDescriptor;
    Array<Core::FramebufferHandle, NWB_SCENE_SHADOW_SLOT_COUNT * 6u> opaqueFramebuffers;
    Array<Core::FramebufferHandle, NWB_SCENE_SHADOW_SLOT_COUNT * 6u> transparentFramebuffers;
    Core::BindingLayoutHandle layout;
    Core::ComputePipelineHandle viewPipeline;
    Core::ComputePipelineHandle cullPipeline;
    Core::ComputePipelineHandle opaqueResolve;
    Core::ComputePipelineHandle transparentResolve;
    Core::ComputePipelineHandle opaqueFallback;
    Core::ComputePipelineHandle transparentFallback;
    Core::ComputePipelineHandle shadePipeline;
    Core::ComputePipelineHandle csgShadePipeline;
    Core::ComputePipelineHandle csgOpaqueResolve;
    Core::ComputePipelineHandle csgTransparentResolve;
    Core::ComputePipelineHandle csgOpaqueFallback;
    Core::ComputePipelineHandle csgTransparentFallback;
    Core::GraphicsPipelineHandle opaqueCapture;
    Core::GraphicsPipelineHandle transparentCapture;
    LightSpaceShadowPush push;
    const LightSpaceShadowCaster* casters = nullptr;
    usize casterCount = 0u;
    LightSpaceCaptureTicket captureTicket;
    LightSpaceCaptureHistory* captureHistory = nullptr;
    const u8* csgContextBytes = nullptr;
    usize csgContextByteCount = 0u;
    const Core::BufferHandle* csgDynamicBounds = nullptr;
    usize csgDynamicBoundsCount = 0u;
    bool ready = false;
};

struct LightSpaceShadowState{
    static constexpr usize s_LightSpaceShaderCount = 14u;


public:
    SoftwareShadowSettings m_settings;
    LightSpaceCsgState m_csg;
    LightSpaceShadowSnapshot m_snapshot;
    LightSpaceCaptureHistory m_captureHistory;
    u64 m_captureSceneIdentity = 0u;
    bool m_captureSceneTrusted = false;
    bool m_captureReuseLogged = false;
    Vector<LightSpaceShadowCaster, Core::Alloc::GlobalArena> m_casters;
    Vector<Core::BufferHandle, Core::Alloc::GlobalArena> m_sceneBuffers;
    Vector<Core::BufferHandle, Core::Alloc::GlobalArena> m_captureBuffers;
    Vector<Core::TextureHandle, Core::Alloc::GlobalArena> m_sceneTextures;
    Vector<Core::TextureHandle, Core::Alloc::GlobalArena> m_captureTextures;
    Core::ShaderHandle m_shaders[s_LightSpaceShaderCount];
    bool m_sceneEligible = false;
    bool m_resourcesPrepared = false;
    bool m_pipelineFailed = false;
    bool m_dispatchLogged = false;
    bool m_csgDispatchLogged = false;
    bool m_csgRequired = false;


public:
    explicit LightSpaceShadowState(Core::Alloc::GlobalArena& arena)
        : m_csg(arena)
        , m_casters(arena)
        , m_sceneBuffers(arena)
        , m_captureBuffers(arena)
        , m_sceneTextures(arena)
        , m_captureTextures(arena)
    {}
};

// Graph tasks own uploads, clears, and entry/exit states. Resolve owns its internal map-to-fallback UAV dependency.
[[nodiscard]] bool RecordLightSpaceViews(Core::CommandList& commandList, Core::GpuDescriptorHeap& heap, const LightSpaceShadowSnapshot& snapshot);
[[nodiscard]] bool RecordLightSpaceCull(Core::CommandList& commandList, Core::GpuDescriptorHeap& heap, const LightSpaceShadowSnapshot& snapshot);
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

