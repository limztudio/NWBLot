// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/assets/graphics/shadow/binding_slots.h>
#include <impl/assets/graphics/shadow/shadow_reproject_merge_binding_slots.h>
#include <impl/assets/graphics/shadow/shadow_resolve_binding_slots.h>
#include <impl/assets/graphics/shadow/sw_binding_slots.h>
#include <impl/ecs_render/shadow/soft_shadow_resolve_state.h>
#include <impl/ecs_render/shadow/types.adhd.h>

#include <core/graphics/api.h>
#include <core/graphics/rhi/device.h>

#include <global/containers.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Core::Color s_ShadowVisibilityAllLitClearColor = Core::Color(1.f, 1.f, 1.f, 1.f);
inline constexpr Core::TextureSubresourceSet s_ShadowVisibilitySubresources = Core::TextureSubresourceSet(0, 1, 0, NWB_SCENE_SHADOW_SLOT_COUNT);
using Impl::s_ShadowRqSoftReceiverFactorOffset;
using Impl::s_ShadowGeometryDownsampleReceiverFactorOffset;
using Impl::s_ShadowCombinedTemporalReceiverFactorOffset;
using Impl::s_SoftwareTransparentSamplingReceiverFactorOffset;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(NWB_SW_SHADOW_SOFT_TEMPORAL_SPP >= 1u && NWB_SW_SHADOW_SOFT_TEMPORAL_SPP <= NWB_SW_SHADOW_SOFT_SPP, "temporal soft-shadow sample budget must be within the bootstrap budget");
static_assert(sizeof(SwShadowHeapPushConstants) == sizeof(u32) * 20u, "SwShadowHeapPushConstants must match the shader push-constant layout");
static_assert(offsetof(SwShadowHeapPushConstants, receiverFactor) == 76u);

static_assert(sizeof(ShadowRqPushConstants) == sizeof(u32) * 6u, "ShadowRqPushConstants must match the shader push-constant layout");

static_assert(sizeof(ShadowRqSoftPushConstants) == sizeof(u32) * 10u, "ShadowRqSoftPushConstants must match the shader push-constant layout");
static_assert(offsetof(ShadowRqSoftPushConstants, receiverFactor) == ECSRenderDetail::s_ShadowRqSoftReceiverFactorOffset);

static_assert(sizeof(ShadowResolvePushConstants) == sizeof(u32) * 21u, "ShadowResolvePushConstants must match the shader push-constant layout");
static_assert(offsetof(ShadowResolvePushConstants, receiverFactor) == 80u);
static_assert(offsetof(ShadowResolvePushConstants, opaqueInputColorSlot) == sizeof(u32) * 19u);

static_assert(sizeof(ShadowCombinedWaveletPushConstants) == 96u);
static_assert(offsetof(ShadowCombinedWaveletPushConstants, opaqueMomentsSlot) == s_ShadowCombinedWaveletPushConstantsOpaqueMomentsSlotOffset);
static_assert(offsetof(ShadowCombinedWaveletPushConstants, opaqueMomentsValid) == s_ShadowCombinedWaveletPushConstantsOpaqueMomentsValidOffset);
static_assert(offsetof(ShadowCombinedWaveletPushConstants, opaqueOutputStorageSlot) == s_ShadowCombinedWaveletPushConstantsOpaqueOutputStorageSlotOffset);

static_assert(sizeof(ShadowGeometryDownsamplePushConstants) == sizeof(u32) * 10u, "ShadowGeometryDownsamplePushConstants must match the shader push-constant layout");
static_assert(offsetof(ShadowGeometryDownsamplePushConstants, receiverFactor) == ECSRenderDetail::s_ShadowGeometryDownsampleReceiverFactorOffset);

static_assert(sizeof(ShadowReprojectMergePushConstants) == sizeof(f32) * 16u + sizeof(u32) * 16u, "ShadowReprojectMergePushConstants must match the shader push-constant layout");
static_assert(offsetof(ShadowReprojectMergePushConstants, receiverFactor) == 124u);

static_assert(sizeof(ShadowCombinedTemporalPushConstants) == 128u);
static_assert(offsetof(ShadowCombinedTemporalPushConstants, receiverFactor) == ECSRenderDetail::s_ShadowCombinedTemporalReceiverFactorOffset);
static_assert(offsetof(ShadowCombinedTemporalPushConstants, opaqueSoftTraceSlot) == 88u);
static_assert(offsetof(ShadowCombinedTemporalPushConstants, transparentSoftTraceSlot) == 108u);
static_assert(offsetof(ShadowCombinedTemporalPushConstants, transparentMomentsOutSlot) == 124u);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Cross-frame Buffer* cache. keepAlive prevents key reuse; failed registrations stay uncached.
struct RtMeshHeapHandleCacheEntry{
    Core::BufferHandle keepAlive; // Pins the Buffer* key.
    Core::GpuDescriptorHandle handle = Core::GpuDescriptorHandle::Invalid();
    b8 seenThisFrame = false;
};

using RtMeshHeapHandleCache = HashMap<NotNull<const Core::Buffer*>, RtMeshHeapHandleCacheEntry, Core::Alloc::GlobalArena, Hasher<NotNull<const Core::Buffer*>>, EqualTo<NotNull<const Core::Buffer*>>>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SoftwareTransparentSamplingState{
    Core::BindingLayoutHandle m_layout;
    // Index zero keeps the fixed budget; index one consumes accepted sampling history.
    Array<Core::ShaderHandle, 2> m_shaders;
    Array<Core::ComputePipelineHandle, 2> m_pipelines;
    SoftwareTransparentSamplingHistory m_history;
};

static_assert(sizeof(SoftwareTransparentSamplingPush) == 116u);
static_assert(offsetof(SoftwareTransparentSamplingPush, receiverFactor) == ECSRenderDetail::s_SoftwareTransparentSamplingReceiverFactorOffset);
static_assert(offsetof(SoftwareTransparentSamplingPush, prevWorldToClip) == s_SoftwareTransparentSamplingPushPrevWorldToClipOffset);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RtShadowState{
    // HW and SW context hashes distinguish descriptor-slot encodings of the shared buffer.
    u64 m_hwShadowMaterialContextHash = 0u;
    u64 m_swShadowMaterialContextHash = 0u;
    usize m_shadowInstanceMaterialCapacity = 0u;
    usize m_shadowInstanceCapacity = 0u;
    usize m_shadowMaterialTypedCapacity = 0u;

    Core::BindingLayoutHandle m_shadowBindingLayout;
    Core::ShaderHandle m_shadowShader;
    Core::ComputePipelineHandle m_shadowPipeline;
    // Shared HW/SW per-instance material table; indices match trace instance IDs.
    Core::BufferHandle m_shadowInstanceMaterialBuffer;
    // All-occluder trace material context; draw buffers contain one transparency class.
    Core::BufferHandle m_shadowInstanceBuffer;
    Core::BufferHandle m_shadowMaterialTypedBuffer;
    // Software shadow layout is push-only; resources use the global heap.
    Core::BindingLayoutHandle m_swShadowBindingLayout;
    Core::ShaderHandle m_swShadowOpaquePrepassShader;
    Core::ComputePipelineHandle m_swShadowOpaquePrepassPipeline;
    Core::ShaderHandle m_swShadowSoftOpaqueShader;
    Core::ComputePipelineHandle m_swShadowSoftOpaquePipeline;
    Core::ShaderHandle m_swShadowTransparentCoarseShader;
    Core::ComputePipelineHandle m_swShadowTransparentCoarsePipeline;
    Core::ShaderHandle m_swShadowTransparentResolveShader;
    Core::ComputePipelineHandle m_swShadowTransparentResolvePipeline;
    Core::ShaderHandle m_swShadowTransparentClassifyShader;
    Core::ComputePipelineHandle m_swShadowTransparentClassifyPipeline;
    Core::ShaderHandle m_swShadowTransparentBuildArgsShader;
    Core::ComputePipelineHandle m_swShadowTransparentBuildArgsPipeline;
    Core::ShaderHandle m_swShadowTransparentIndirectShader;
    Core::ComputePipelineHandle m_swShadowTransparentIndirectPipeline;
    SoftwareTransparentSamplingState m_softwareTransparentSampling;
    // Scalar and RGB wavelet/upsample pipelines share one push-constant layout.
    SoftShadowResolveState m_softShadowResolve;
    // Compaction buffers get fresh heap views after target recreation.
    Core::BufferHandle m_swShadowEdgeCounterBuffer;
    Core::BufferHandle m_swShadowEdgeListBuffer;
    Core::BufferHandle m_swShadowIndirectArgsBuffer;

    // Shared selector and optical readers may occupy several physical transports in one queue class.
    Vector<Core::QueueSubmissionToken, Core::Alloc::GlobalArena> m_rayTraceContextReadSubmissionTokens;
    // HW distinct meshes and heap views shared with caustic/GI.
    Vector<NotNull<Core::Buffer*>, Core::Alloc::GlobalArena> m_shadowMeshIndexBuffers;
    Vector<NotNull<Core::Buffer*>, Core::Alloc::GlobalArena> m_shadowMeshAttributeBuffers;
    Vector<NotNull<Core::Buffer*>, Core::Alloc::GlobalArena> m_shadowMeshPositionBuffers;
    Vector<Core::GpuDescriptorHandle, Core::Alloc::GlobalArena> m_shadowMeshIndexHandles;
    Vector<Core::GpuDescriptorHandle, Core::Alloc::GlobalArena> m_shadowMeshAttributeHandles;
    Vector<Core::GpuDescriptorHandle, Core::Alloc::GlobalArena> m_shadowMeshPositionHandles;
    // SW distinct-mesh tables; matching entries share an index.
    Vector<NotNull<Core::Buffer*>, Core::Alloc::GlobalArena> m_swShadowMeshNodeBuffers;
    Vector<NotNull<Core::Buffer*>, Core::Alloc::GlobalArena> m_swShadowMeshPositionBuffers;
    Vector<NotNull<Core::Buffer*>, Core::Alloc::GlobalArena> m_swShadowMeshIndexBuffers;
    Vector<NotNull<Core::Buffer*>, Core::Alloc::GlobalArena> m_swShadowMeshAttributeBuffers; // Packed normal/uv0.
    Vector<Core::GpuDescriptorHandle, Core::Alloc::GlobalArena> m_swShadowMeshNodeHandles;
    Vector<Core::GpuDescriptorHandle, Core::Alloc::GlobalArena> m_swShadowMeshPositionHandles;
    Vector<Core::GpuDescriptorHandle, Core::Alloc::GlobalArena> m_swShadowMeshIndexHandles;
    Vector<Core::GpuDescriptorHandle, Core::Alloc::GlobalArena> m_swShadowMeshAttributeHandles;
    // Separate HW/SW caches avoid cross-gather eviction.
    RtMeshHeapHandleCache m_hwMeshHeapHandleCache;
    RtMeshHeapHandleCache m_swMeshHeapHandleCache;

    // Fresh heap views preserve old buffers for in-flight work.
    Core::GpuDescriptorHandle m_shadowInstanceMaterialHeapHandle = Core::GpuDescriptorHandle::Invalid();
    Core::GpuDescriptorHandle m_shadowInstanceHeapHandle = Core::GpuDescriptorHandle::Invalid();
    Core::GpuDescriptorHandle m_shadowMaterialTypedHeapHandle = Core::GpuDescriptorHandle::Invalid();
    u32 m_shadowMeshCount = 0u;
    u32 m_shadowMeshHeapHighWater = 0u;
    u32 m_swShadowMeshCount = 0u;
    u32 m_swShadowMeshHeapHighWater = 0u;
    u32 m_swShadowEdgeListCapacity = 0u;
    Core::GpuDescriptorHandle m_swShadowEdgeCounterHeapHandle = Core::GpuDescriptorHandle::Invalid();
    Core::GpuDescriptorHandle m_swShadowEdgeListHeapHandle = Core::GpuDescriptorHandle::Invalid();
    Core::GpuDescriptorHandle m_swShadowIndirectArgsHeapHandle = Core::GpuDescriptorHandle::Invalid();

    b8 m_hwShadowMaterialContextHashValid = false;
    b8 m_swShadowMaterialContextHashValid = false;
    b8 m_shadowPipelineFailed = false;
    b8 m_swShadowPipelineFailed = false;
    b8 m_swShadowDispatchLogged = false;


public:
    // Arena-backed tables and caches must be constructed with the renderer arena.
    explicit RtShadowState(Core::Alloc::GlobalArena& arena)
        : m_rayTraceContextReadSubmissionTokens(arena)
        , m_shadowMeshIndexBuffers(arena)
        , m_shadowMeshAttributeBuffers(arena)
        , m_shadowMeshPositionBuffers(arena)
        , m_shadowMeshIndexHandles(arena)
        , m_shadowMeshAttributeHandles(arena)
        , m_shadowMeshPositionHandles(arena)
        , m_swShadowMeshNodeBuffers(arena)
        , m_swShadowMeshPositionBuffers(arena)
        , m_swShadowMeshIndexBuffers(arena)
        , m_swShadowMeshAttributeBuffers(arena)
        , m_swShadowMeshNodeHandles(arena)
        , m_swShadowMeshPositionHandles(arena)
        , m_swShadowMeshIndexHandles(arena)
        , m_swShadowMeshAttributeHandles(arena)
        , m_hwMeshHeapHandleCache(arena)
        , m_swMeshHeapHandleCache(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RtSoftShadowState{
    // Previous-frame transform for soft-shadow reprojection; invalid after target recreation.
    Float44 m_prevWorldToClip = {};
    // HW half-resolution soft trace shares the temporal resolve chain.
    Core::ShaderHandle m_shadowSoftShader;
    Core::ComputePipelineHandle m_shadowSoftPipeline;
    // Half-resolution geometry cache for edge-aware resolves.
    Core::BindingLayoutHandle m_shadowGeometryDownsampleBindingLayout;
    Core::ShaderHandle m_shadowGeometryDownsampleShader;
    Core::ComputePipelineHandle m_shadowGeometryDownsamplePipeline;
    Core::BindingLayoutHandle m_shadowReprojectMergeBindingLayout;
    Core::ShaderHandle m_shadowReprojectMergeShader;
    Core::ComputePipelineHandle m_shadowReprojectMergePipeline;

    // Bits identify populated shadow slots; dispatches handle sparse assignments.
    u32 m_softShadowSlotMask = 0u;
    // Primary shadow producer's temporal sample index.
    u32 m_softShadowFrameIndex = 0u;
    // 1: A is history input; 0: B is history input.
    u32 m_softShadowHistoryFrontIsA = 1u;
    // Transparent temporal gates; coarse/adaptive fallback remains available.
    TransparentShadowSamplingHistory m_transparentShadowSamplingHistory;

    b8 m_prevWorldToClipValid = false;
    b8 m_shadowSoftPipelineFailed = false;
    b8 m_shadowGeometryDownsamplePipelineFailed = false;
    // Nonfatal soft-shadow resource gate.
    b8 m_softShadowReady = false;
    // Enables temporal soft-shadow merge; falls back to non-temporal mode.
    b8 m_softShadowTemporalReady = false;
    // Set after the first temporal merge; reset when targets are recreated.
    b8 m_softShadowTemporalSeeded = false;
    // Defer the history swap until the ordered submission succeeds.
    b8 m_softShadowTemporalHistoryAdvancePending = false;
    b8 m_shadowReprojectMergePipelineFailed = false;
    b8 m_softTransparentTemporalReady = false;
    b8 m_softTransparentReady = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

