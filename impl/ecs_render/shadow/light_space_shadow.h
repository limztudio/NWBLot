// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "light_space_plan.h"
#include "light_space_csg.h"
#include <impl/assets/graphics/scene/binding_slots.h>

#include <core/alloc/global.h>
#include <core/graphics/rhi/pipeline.h>
#include <core/graphics/rhi/command.h>
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


static_assert(sizeof(LightSpaceShadowPush) == NWB_LIGHT_SPACE_PUSH_BYTES);
static_assert(offsetof(LightSpaceShadowPush, receiverFactor) == 76u);
static_assert(offsetof(LightSpaceShadowPush, captureDrawCount) == 80u);
static_assert(offsetof(LightSpaceShadowPush, casterDrawOffset) == 84u);
static_assert(offsetof(LightSpaceShadowPush, meshletDescSlot) == 88u);
static_assert(offsetof(LightSpaceShadowPush, meshletBoundsSlot) == 92u);
static_assert(offsetof(LightSpaceShadowPush, meshletCount) == 96u);
static_assert(offsetof(LightSpaceShadowPush, receiverViewSlot) == 100u);
static_assert(offsetof(LightSpaceShadowPush, receiverCsgSampleStateSlot) == 104u);

struct LightSpaceShadowCaster{
    Core::BufferHandle triangleIndexBuffer;
    Core::BufferHandle meshletDescBuffer;
    Core::BufferHandle meshletBoundsBuffer;
    u32 instanceIndex = 0u;
    u32 indexCount = 0u;
    u32 meshletCount = 0u;
    u32 meshletDescSlot = 0u;
    u32 meshletBoundsSlot = 0u;
    b8 transparent = false;
    b8 csg = false;
};

[[nodiscard]] Expected<u32> LightSpaceShadowDrawCount(Span<const LightSpaceShadowCaster> casters)noexcept;

struct LightSpaceShadowDiagnostics{
    Core::BufferHandle readback;
    Core::QueueSubmissionToken submission;
    Core::QueueSubmissionToken contextInitialized;
    u32 frameIndex = 0u;
    u32 acceptedFrame = 0u;
    u32 reportedFailures = 0u;
};

// Resource handles freeze one allocation generation. Graph tasks must copy the caster span into their arena.
struct LightSpaceShadowSnapshot{
    LightSpacePlan plan;
    Core::BufferHandle counts;
    Core::BufferHandle events;
    Core::BufferHandle views;
    Core::BufferHandle drawArguments;
    Core::BufferHandle csgContext;
    Core::BufferHandle csgOpaqueDepth;
    Core::BufferHandle receiverView;
    Core::BufferHandle receiverCsgSampleState;
    Core::TextureHandle depth;
    Core::GpuDescriptorHandle countsDescriptor;
    Core::GpuDescriptorHandle eventsDescriptor;
    Core::GpuDescriptorHandle viewsDescriptor;
    Core::GpuDescriptorHandle drawArgumentsDescriptor;
    Core::GpuDescriptorHandle csgContextDescriptor;
    Core::GpuDescriptorHandle csgOpaqueDepthDescriptor;
    Core::GpuDescriptorHandle depthDescriptor;
    Core::GpuDescriptorHandle csgTlasDescriptor;
    Array<Core::FramebufferHandle, NWB_SCENE_SHADOW_SLOT_COUNT * s_PointLightViewCount> opaqueFramebuffers;
    Array<Core::FramebufferHandle, NWB_SCENE_SHADOW_SLOT_COUNT * s_PointLightViewCount> transparentFramebuffers;
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
    Span<const LightSpaceShadowCaster> casters{};
    LightSpaceCaptureTicket captureTicket;
    LightSpaceCaptureHistory* captureHistory = nullptr;
    LightSpaceShadowDiagnostics* diagnostics = nullptr;
    Core::BufferHandle diagnosticReadback;
    u32 diagnosticFrame = 0u;
    Span<const u8> csgContextBytes{};
    Span<const Core::BufferHandle> csgDynamicBounds{};
    b8 ready = false;
};

struct LightSpaceShadowState{
    static constexpr usize s_LightSpaceShaderCount = 14u;


public:
    LightSpaceShadowSnapshot m_snapshot;
    SoftwareShadowSettings m_settings;
    LightSpaceCsgState m_csg;
    u64 m_captureSceneIdentity = 0u;
    LightSpaceCaptureHistory m_captureHistory;
    LightSpaceShadowDiagnostics m_diagnostics;
    Vector<LightSpaceShadowCaster, Core::Alloc::GlobalArena> m_casters;
    Vector<Core::BufferHandle, Core::Alloc::GlobalArena> m_sceneBuffers;
    Vector<Core::BufferHandle, Core::Alloc::GlobalArena> m_captureBuffers;
    Vector<Core::TextureHandle, Core::Alloc::GlobalArena> m_sceneTextures;
    Vector<Core::TextureHandle, Core::Alloc::GlobalArena> m_captureTextures;
    Core::ShaderHandle m_shaders[s_LightSpaceShaderCount];
    b8 m_captureSceneTrusted = false;
    b8 m_captureReuseLogged = false;
    b8 m_sceneEligible = false;
    b8 m_resourcesPrepared = false;
    b8 m_pipelineFailed = false;
    b8 m_dispatchLogged = false;
    b8 m_csgDispatchLogged = false;
    b8 m_csgRequired = false;


public:
    explicit LightSpaceShadowState(Core::Alloc::GlobalArena& arena)noexcept
        : m_csg(arena)
        , m_casters(arena)
        , m_sceneBuffers(arena)
        , m_captureBuffers(arena)
        , m_sceneTextures(arena)
        , m_captureTextures(arena)
    {}
};

// Graph tasks own uploads, clears, and entry/exit states. Resolve owns its internal map-to-fallback UAV dependency.
[[nodiscard]] b8 RecordLightSpaceViews(Core::CommandList& commandList, Core::GpuDescriptorHeap& heap, const LightSpaceShadowSnapshot& snapshot);
[[nodiscard]] b8 RecordLightSpaceCull(Core::CommandList& commandList, Core::GpuDescriptorHeap& heap, const LightSpaceShadowSnapshot& snapshot);
[[nodiscard]] b8 RecordLightSpaceCapture(Core::CommandList& commandList, Core::GpuDescriptorHeap& heap, const LightSpaceShadowSnapshot& snapshot, u32 viewIndex, b8 transparent);
[[nodiscard]] b8 RecordLightSpaceShade(Core::CommandList& commandList, Core::GpuDescriptorHeap& heap, const LightSpaceShadowSnapshot& snapshot);
[[nodiscard]] b8 RecordLightSpaceResolve(
    Core::CommandList& commandList, Core::GpuDescriptorHeap& heap, Core::GpuTimingRecorder& timing,
    const LightSpaceShadowSnapshot& snapshot,
    Core::Texture& outputTexture, u32 frameIndex, u32 sampleCount, u32 outputSlot, b8 transparent
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

