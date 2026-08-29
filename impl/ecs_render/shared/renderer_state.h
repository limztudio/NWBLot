// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/kernel/renderer_types.h>
#include <impl/ecs_render/kernel/renderer_constants_private.h>

#include <core/ecs/entity_id.h>
#include <core/graphics/rhi/gpu_descriptor_heap.h>

#include <impl/assets/graphics/mesh/runtime_constants.h>
#include <impl/assets/graphics/scene/binding_slots.h>
#include <impl/assets/graphics/shadow/binding_slots.h>
#include <impl/assets/graphics/shadow/sw_binding_slots.h>
#include <impl/assets/graphics/caustic/sw_binding_slots.h>
#include <impl/assets/graphics/caustic/resolve_binding_slots.h>
#include <impl/assets/graphics/gi/surfel/surfel_binding_slots.h>
#include <impl/assets_texture/loader.h>
#include <impl/assets_sampler/loader.h>

#include <global/generic.h>
#include <global/containers.h>   // dynamic Vector storage for the per-frame SW distinct-mesh table


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererMeshSystem;
class RendererMaterialSystem;
class RendererCsgSystem;
class RendererDeferredSystem;
class RendererAvboitSystem;
class RendererRayTracingSystem;


// Device-lifetime material asset caches retain the descriptor owner for every patched global heap slot.
struct RendererMaterialResourceState{
    HashMap<Name, UniquePtr<TextureGpuResource>, Hasher<Name>, EqualTo<Name>, Core::Alloc::GlobalArena> textureAssetCache;
    HashMap<Name, UniquePtr<SamplerGpuResource>, Hasher<Name>, EqualTo<Name>, Core::Alloc::GlobalArena> samplerAssetCache;

    explicit RendererMaterialResourceState(Core::Alloc::GlobalArena& arena)
        : textureAssetCache(0, Hasher<Name>(), EqualTo<Name>(), arena)
        , samplerAssetCache(0, Hasher<Name>(), EqualTo<Name>(), arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Cross-frame Buffer* cache. keepAlive prevents key reuse; failed registrations stay uncached.
struct RtMeshHeapHandleCacheEntry{
    Core::BufferHandle keepAlive; // Pins the Buffer* key.
    Core::GpuDescriptorHandle handle = Core::GpuDescriptorHandle::invalid();
    bool seenThisFrame = false;
};

using RtMeshHeapHandleCache = HashMap<
    const Core::Buffer*,
    RtMeshHeapHandleCacheEntry,
    Hasher<const Core::Buffer*>,
    EqualTo<const Core::Buffer*>,
    Core::Alloc::GlobalArena
>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CsgFrameStateCacheSignature{
    u64 contentHash = 0u;
    u64 shapeRegistryRevision = 0u;

    friend bool operator==(const CsgFrameStateCacheSignature& lhs, const CsgFrameStateCacheSignature& rhs){
        return lhs.contentHash == rhs.contentHash
            && lhs.shapeRegistryRevision == rhs.shapeRegistryRevision;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererMeshState final : NoCopy{
    friend class RendererMeshSystem;
    friend class RendererRayTracingSystem;

public:
    explicit RendererMeshState(Core::Alloc::GlobalArena& arena);


private:
    void invalidateResources();


private:
    HashMap<Name, MeshResources, Hasher<Name>, EqualTo<Name>, Core::Alloc::GlobalArena> m_meshes;
};

class RendererMaterialState final : NoCopy{
    friend class RendererMaterialSystem;

public:
    explicit RendererMaterialState(Core::Alloc::GlobalArena& arena);


private:
    void invalidateResources();


private:
    Core::BindingLayoutHandle m_materialPassBindingLayout;
    HashMap<Name, MaterialSurfaceInfo, Hasher<Name>, EqualTo<Name>, Core::Alloc::GlobalArena> m_surfaceInfos;
    RendererMaterialResourceState m_resourceState;
    HashMap<MaterialPipelineKey, MaterialPipelineResources, MaterialPipelineKeyHasher, MaterialPipelineKeyEqualTo, Core::Alloc::GlobalArena> m_pipelines;
    HashMap<Core::ECS::EntityID, MaterialInstanceMutableCacheEntry, Hasher<Core::ECS::EntityID>, EqualTo<Core::ECS::EntityID>, Core::Alloc::GlobalArena> m_instanceMutableCache;
    HashMap<Name, RenderPath::Enum, Hasher<Name>, EqualTo<Name>, Core::Alloc::GlobalArena> m_loggedMaterialPaths;
    u64 m_instanceMutableCacheComponentMutationVersion = 0u;
};

class RendererDrawState final : NoCopy{
    friend class RendererMeshSystem;
    friend class RendererMaterialSystem;
    friend class RendererCsgSystem;
    friend class RendererRayTracingSystem;

public:
    RendererDrawState() = default;


public:
    void invalidateResources();


private:
    Core::BindingLayoutHandle m_computeBindingLayout;
    Core::BufferHandle m_instanceBuffer;
    Core::BufferHandle m_materialTypedBuffer;
    Core::BufferHandle m_meshViewBuffer;
    Core::ShaderHandle m_emulationVertexShader;
    Core::InputLayoutHandle m_emulationInputLayout;
    usize m_instanceBufferCapacity = 0;
    usize m_materialTypedBufferCapacity = 0;
    // Capacity changes get fresh heap slots after deferred retirement.
    Core::GpuDescriptorHandle m_instanceBufferHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle m_materialTypedBufferHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle m_meshViewBufferHeapHandle = Core::GpuDescriptorHandle::invalid();
    u8 m_meshViewGpuData[sizeof(f32) * NWB_MESH_VIEW_FLOAT_COUNT] = {};
    bool m_meshViewGpuDataValid = false;
};
static_assert(sizeof(RendererDrawState) == 368u, "RendererDrawState should keep its compact CPU-only layout");

class RendererCsgState final : NoCopy{
    friend class RendererCsgSystem;

public:
    RendererCsgState() = default;


private:
    void invalidateResources();


private:
    // CSG layouts are push-only; resources use the global heap.
    Core::BindingLayoutHandle m_clipBindingLayout;
    Core::BindingLayoutHandle m_intervalPeelBindingLayout;
    Core::BindingLayoutHandle m_receiverSpanBuildBindingLayout;
    Core::BindingLayoutHandle m_intervalCombineBindingLayout;
    Core::ShaderHandle m_intervalPeelComputeShader;
    Core::ShaderHandle m_receiverSpanBuildComputeShader;
    Core::ShaderHandle m_intervalCombineComputeShader;
    Core::ShaderHandle m_intervalCapFillPixelShader;
    Core::ComputePipelineHandle m_intervalPeelPipeline;
    Core::ComputePipelineHandle m_receiverSpanBuildPipeline;
    Core::ComputePipelineHandle m_intervalCombinePipeline;
    Core::GraphicsPipelineHandle m_intervalCapFillPipeline;
    Core::BufferHandle m_receiverRangeBuffer;
    Core::BufferHandle m_cutterBuffer;
    Core::BufferHandle m_clipContextSlotsBuffer;
    Core::BufferHandle m_intervalSampleStateBuffer;
    CsgFrameStateCacheSignature m_frameStateCacheSignature;
    CsgFrameState m_frameStateCache;
    usize m_receiverRangeBufferCapacity = 0u;
    usize m_cutterBufferCapacity = 0u;
    Core::GpuDescriptorHandle m_receiverRangeBufferHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle m_cutterBufferHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle m_clipContextSlotsHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle m_intervalSampleStateHeapHandle = Core::GpuDescriptorHandle::invalid();
    bool m_frameStateCacheValid = false;
};
static_assert(sizeof(RendererCsgState) == 328u, "RendererCsgState should keep its compact CPU-only layout");

class RendererDeferredState final : NoCopy{
    friend class RendererDeferredSystem;

public:
    RendererDeferredState() = default;


public:
    void invalidateResources();


private:
    Core::BindingLayoutHandle m_lightingBindingLayout;
    Core::BufferHandle m_sceneShadingBuffer;
    Core::BufferHandle m_lightBuffer;
    Core::ShaderHandle m_lightingComputeShader;
    Core::ComputePipelineHandle m_lightingPipeline;
    Core::BindingLayoutHandle m_compositeComputeBindingLayout;
    Core::ShaderHandle m_compositeComputeShader;
    Core::ComputePipelineHandle m_compositeComputePipeline;
    Core::BindingLayoutHandle m_presentBindingLayout;
    Core::SamplerHandle m_sampler;
    Core::ShaderHandle m_presentPixelShader;
    Core::GraphicsPipelineHandle m_presentPipeline;
    u8 m_sceneShadingGpuData[sizeof(f32) * NWB_SCENE_SHADING_BUFFER_FLOAT_COUNT] = {};
    bool m_sceneShadingGpuDataValid = false;
    // Cached bytes avoid redundant light-buffer uploads.
    u8 m_lightGpuData[sizeof(f32) * NWB_SCENE_LIGHT_RECORD_FLOAT_COUNT * NWB_SCENE_MAX_LIGHTS] = {};
    u32 m_lightGpuDataCount = 0u;
    bool m_lightGpuDataValid = false;
};

class RendererAvboitState final : NoCopy{
    friend class RendererAvboitSystem;

public:
    RendererAvboitState() = default;


public:
    void invalidateResources();


private:
    Core::SamplerHandle m_linearSampler;
    Core::ShaderHandle m_depthWarpComputeShader;
    Core::ShaderHandle m_integrateComputeShader;
    Core::ComputePipelineHandle m_depthWarpPipeline;
    Core::ComputePipelineHandle m_integratePipeline;
    bool m_targetsNeedClear = true;
};

// Feature-state bases preserve existing m_* access.

struct RtSceneBvhState{
    usize m_tlasMaxInstances = 0u;
    u64 m_tlasDeviceAddress = 0u;
    // Runtime meshes invalidate static-scene reuse because their BVHs can update in place.
    u64 m_tlasStaticSceneHash = 0u;
    u64 m_sceneSwBvhStaticSceneHash = 0u;
    // HW and SW context hashes distinguish descriptor-slot encodings of the shared buffer.
    u64 m_hwShadowMaterialContextHash = 0u;
    u64 m_swShadowMaterialContextHash = 0u;
    usize m_bvhSortCapacity = 0u;
    usize m_bvhBuildCapacity = 0u;
    usize m_sceneBvhNodeCapacity = 0u;
    usize m_sceneInstanceCapacity = 0u;

    Core::RayTracingAccelStructHandle m_tlas;
    // A new backing generation begins in Common until native direct recording or an accepted Shadow Preparation
    // handoff records its final state. This remains true across discarded frozen plans.
    bool m_tlasBackingFresh = false;
    bool m_tlasBackingStateHandoffPending = false;
    Core::BindingLayoutHandle m_bvhSortBindingLayout;
    Core::ShaderHandle m_bvhSortShader;
    Core::ComputePipelineHandle m_bvhSortPipeline;
    Core::BufferHandle m_bvhSortKeysBuffer;
    Core::BufferHandle m_bvhSortPayloadBuffer;
    Core::BindingLayoutHandle m_bvhBuildBindingLayout;
    Core::ShaderHandle m_bvhMortonShader;
    Core::ComputePipelineHandle m_bvhMortonPipeline;
    Core::ShaderHandle m_bvhTopologyShader;
    Core::ComputePipelineHandle m_bvhTopologyPipeline;
    Core::ShaderHandle m_bvhFitShader;
    Core::ComputePipelineHandle m_bvhFitPipeline;
    Core::BufferHandle m_bvhVisitCounterBuffer;
    Core::BufferHandle m_sceneBvhNodeBuffer;  // CPU-built binned-SAH scene/instance BVH (TLAS-analog), uploaded when inputs change
    Core::BufferHandle m_sceneInstanceBuffer; // per-instance world->object transform + reserved ABI word + BVH leaf cost
    Core::BufferHandle m_rayTraceMaterialContextSlotsBuffer;
    // Software caustics are push-only and reuse the SW trace geometry.
    Core::BindingLayoutHandle m_swCausticBindingLayout;
    Core::ShaderHandle m_swCausticShader;
    Core::ComputePipelineHandle m_swCausticPipeline;
    // Hardware caustics are push-only and use the TLAS plus global heap views.
    Core::BindingLayoutHandle m_hwCausticBindingLayout;
    Core::RayTracingPipelineHandle m_hwCausticPipeline;
    Core::RayTracingShaderTableHandle m_hwCausticShaderTable;

    // Replacement TLASs get fresh handles so recorded work retains the old generation.
    Core::GpuDescriptorHandle m_tlasHeapHandle = Core::GpuDescriptorHandle::invalid();
    u32 m_tlasInstanceCount = 0u;
    u32 m_sceneBvhInstanceCount = 0u;
    // Capacity replacement gives sort buffers fresh heap generations.
    Core::GpuDescriptorHandle m_bvhSortKeysHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle m_bvhSortPayloadHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle m_bvhVisitCounterHeapHandle = Core::GpuDescriptorHandle::invalid();
    // Global StorageBuffer heap views selected by the common trace-context slot cbuffer for SW shadow, GI, and caustics.
    Core::GpuDescriptorHandle m_sceneBvhNodeHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle m_sceneInstanceHeapHandle = Core::GpuDescriptorHandle::invalid();
    // Shared context-slot payload with independently owned heap views.
    Core::GpuDescriptorHandle m_causticMaterialContextSlotsHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle m_shadowMaterialContextSlotsHeapHandle = Core::GpuDescriptorHandle::invalid();
    // Previous-frame transform for soft-shadow reprojection; invalid after target recreation.
    Float44U m_prevWorldToClip = {};

    bool m_tlasStaticSceneHashValid = false;
    bool m_sceneSwBvhStaticSceneHashValid = false;
    bool m_hwShadowMaterialContextHashValid = false;
    bool m_swShadowMaterialContextHashValid = false;
    // HW traces opaque shadows; SW adds transparent transmittance when needed.
    bool m_sceneHasTransparentOccluder = false;
    bool m_hybridTransparentShadowReady = false;
    bool m_prevWorldToClipValid = false;
    bool m_swCausticDispatchLogged = false;
    bool m_swCausticPipelineFailed = false;
    bool m_hwCausticPipelineFailed = false;
    bool m_hwCausticDispatchLogged = false;
    bool m_capabilityLogged = false;
    bool m_bvhSortPipelineFailed = false;
    bool m_bvhBuildPipelineFailed = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RtShadowState{
    // Arena-backed tables and caches must be constructed with the renderer arena.
    explicit RtShadowState(Core::Alloc::GlobalArena& arena)
        : m_shadowMeshIndexBuffers(arena)
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

    usize m_shadowInstanceMaterialCapacity = 0u;
    usize m_shadowInstanceCapacity = 0u;
    usize m_shadowMaterialTypedCapacity = 0u;
    u64 m_swShadowEdgeStatsPendingSubmissionID = 0u;

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
    Core::ShaderHandle m_swShadowTransparentUniformShader;
    Core::ComputePipelineHandle m_swShadowTransparentUniformPipeline;
    Core::ShaderHandle m_swShadowTransparentSoftShader;
    Core::ComputePipelineHandle m_swShadowTransparentSoftPipeline;
    // Periodic async-safe edge-stat readback.
    Core::BufferHandle m_swShadowEdgeStatsBuffer;
    Core::BufferHandle m_swShadowEdgeStatsReadback;
    // Push-only a-trous opaque shadow resolve.
    Core::BindingLayoutHandle m_shadowResolveBindingLayout;
    Core::ShaderHandle m_shadowResolveShader;
    Core::ComputePipelineHandle m_shadowResolvePipeline;
    // RGB variant shares the opaque resolve layout.
    Core::ShaderHandle m_shadowResolveRgbShader;
    Core::ComputePipelineHandle m_shadowResolveRgbPipeline;
    // Compaction buffers get fresh heap views after target recreation.
    Core::BufferHandle m_swShadowEdgeCounterBuffer;
    Core::BufferHandle m_swShadowEdgeListBuffer;
    Core::BufferHandle m_swShadowIndirectArgsBuffer;

    // HW distinct meshes and heap views shared with caustic/GI.
    Vector<Core::Buffer*, Core::Alloc::GlobalArena> m_shadowMeshIndexBuffers;
    Vector<Core::Buffer*, Core::Alloc::GlobalArena> m_shadowMeshAttributeBuffers;
    Vector<Core::Buffer*, Core::Alloc::GlobalArena> m_shadowMeshPositionBuffers;
    Vector<Core::GpuDescriptorHandle, Core::Alloc::GlobalArena> m_shadowMeshIndexHandles;
    Vector<Core::GpuDescriptorHandle, Core::Alloc::GlobalArena> m_shadowMeshAttributeHandles;
    Vector<Core::GpuDescriptorHandle, Core::Alloc::GlobalArena> m_shadowMeshPositionHandles;
    // SW distinct-mesh tables; matching entries share an index.
    Vector<Core::Buffer*, Core::Alloc::GlobalArena> m_swShadowMeshNodeBuffers;
    Vector<Core::Buffer*, Core::Alloc::GlobalArena> m_swShadowMeshPositionBuffers;
    Vector<Core::Buffer*, Core::Alloc::GlobalArena> m_swShadowMeshIndexBuffers;
    Vector<Core::Buffer*, Core::Alloc::GlobalArena> m_swShadowMeshAttributeBuffers; // Packed normal/uv0.
    Vector<Core::GpuDescriptorHandle, Core::Alloc::GlobalArena> m_swShadowMeshNodeHandles;
    Vector<Core::GpuDescriptorHandle, Core::Alloc::GlobalArena> m_swShadowMeshPositionHandles;
    Vector<Core::GpuDescriptorHandle, Core::Alloc::GlobalArena> m_swShadowMeshIndexHandles;
    Vector<Core::GpuDescriptorHandle, Core::Alloc::GlobalArena> m_swShadowMeshAttributeHandles;
    // Separate HW/SW caches avoid cross-gather eviction.
    RtMeshHeapHandleCache m_hwMeshHeapHandleCache;
    RtMeshHeapHandleCache m_swMeshHeapHandleCache;

    // Fresh heap views preserve old buffers for in-flight work.
    Core::GpuDescriptorHandle m_shadowInstanceMaterialHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle m_shadowInstanceHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle m_shadowMaterialTypedHeapHandle = Core::GpuDescriptorHandle::invalid();
    u32 m_shadowMeshCount = 0u;
    u32 m_shadowMeshHeapHighWater = 0u;
    u32 m_swShadowMeshCount = 0u;
    u32 m_swShadowMeshHeapHighWater = 0u;
    f32 m_swShadowEdgeThreshold = ECSRenderDetail::s_DefaultSwShadowEdgeThreshold;
    Core::GpuDescriptorHandle m_swShadowEdgeStatsHeapHandle = Core::GpuDescriptorHandle::invalid();
    u32 m_swShadowEdgeStatsTick = 0u;
    u32 m_swShadowEdgeListCapacity = 0u;
    u32 m_swShadowEdgeStatsPendingTick = 0u;
    Core::GpuDescriptorHandle m_swShadowEdgeCounterHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle m_swShadowEdgeListHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle m_swShadowIndirectArgsHeapHandle = Core::GpuDescriptorHandle::invalid();

    bool m_swShadowAdaptiveEnabled = true;
    bool m_swShadowEdgeStatsEnabled = false;
    // Compaction dispatches edge rays indirectly when enabled.
    bool m_swShadowCompactEnabled = true;
    bool m_swShadowEdgeStatsPending = false;
    bool m_shadowResolvePipelineFailed = false;
    bool m_shadowResolveRgbPipelineFailed = false;
    Core::GpuPhysicalQueueId m_swShadowEdgeStatsPendingSubmissionPhysicalQueue;
    bool m_shadowPipelineFailed = false;
    bool m_swShadowPipelineFailed = false;
    bool m_swShadowDispatchLogged = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RtSoftShadowState{
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

    bool m_shadowSoftPipelineFailed = false;
    bool m_shadowGeometryDownsamplePipelineFailed = false;
    // Nonfatal soft-shadow resource gate.
    bool m_softShadowReady = false;
    // Enables temporal soft-shadow merge; falls back to non-temporal mode.
    bool m_softShadowTemporalReady = false;
    // Set after the first temporal merge; reset when targets are recreated.
    bool m_softShadowTemporalSeeded = false;
    // Defer the history swap until the ordered submission succeeds.
    bool m_softShadowTemporalHistoryAdvancePending = false;
    bool m_shadowReprojectMergePipelineFailed = false;
    // Transparent temporal gates; coarse/adaptive fallback remains available.
    bool m_softTransparentTemporalReady = false;
    bool m_softTransparentReady = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RtCausticState{
    Float4 m_causticTargetBoundsMin = Float4(0.f, 0.f, 0.f, 0.f);
    Float4 m_causticTargetBoundsMax = Float4(0.f, 0.f, 0.f, 0.f);
    usize m_causticEmissionTargetCapacity = 0u;

    // Per-frame refractive-instance AABBs shared by caustic lights.
    Core::BufferHandle m_causticEmissionTargetBuffer;
    // Push-only a-trous caustic resolve.
    Core::BindingLayoutHandle m_causticResolveBindingLayout;
    Core::ShaderHandle m_causticResolveShader;
    Core::ComputePipelineHandle m_causticResolvePipeline;
    // Half-resolution geometry cache for edge-aware caustic resolve.
    Core::BindingLayoutHandle m_causticGeometryDownsampleBindingLayout;
    Core::ShaderHandle m_causticGeometryDownsampleShader;
    Core::ComputePipelineHandle m_causticGeometryDownsamplePipeline;
    // Decay persistent accumulator after seeding; clear it after target recreation.
    Core::BindingLayoutHandle m_causticAccumulatorDecayBindingLayout;
    Core::ShaderHandle m_causticAccumulatorDecayShader;
    Core::ComputePipelineHandle m_causticAccumulatorDecayPipeline;

    // Fresh heap views preserve old buffers for recorded dispatches.
    Core::GpuDescriptorHandle m_causticEmissionTargetHeapHandle = Core::GpuDescriptorHandle::invalid();
    u32 m_causticRefractiveInstanceCount = 0u;
    // Producer runs only when refractive instances and caustic lights exist.
    u32 m_causticLightCount = 0u;
    // Splat-space EMA avoids image-space reprojection ghosts; resolve normalizes steady-state brightness.
    f32 m_causticTemporalDecay = ECSRenderDetail::s_DefaultCausticTemporalDecay;
    // Accepted updates select bootstrap then converged phases; rollback restores this counter.
    u32 m_causticTemporalReuseFrameCount = 0u;
    u32 m_swCausticFrameIndex = 0u;
    u32 m_hwCausticFrameIndex = 0u;

    bool m_causticEmissionGateLogged = false;
    bool m_causticGeometryDownsamplePipelineFailed = false;
    bool m_causticResolvePipelineFailed = false;
    bool m_causticAccumulatorInitialized = false;
    bool m_causticAccumulatorDecayPipelineFailed = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RtSurfelGiState{

    // Device-lifetime, heap-only one-bounce surfel GI stays outside resizable frame targets.
    Core::BindingLayoutHandle m_surfelSpawnBindingLayout;
    Core::BindingLayoutHandle m_surfelAgeFreeBindingLayout;
    Core::BindingLayoutHandle m_surfelHashBuildBindingLayout;
    Core::BindingLayoutHandle m_surfelTraceBindingLayout;
    Core::ShaderHandle m_surfelSpawnShader;
    Core::ComputePipelineHandle m_surfelSpawnPipeline;
    Core::ShaderHandle m_surfelAgeFreeShader;
    Core::ComputePipelineHandle m_surfelAgeFreePipeline;
    Core::ShaderHandle m_surfelHashBuildShader;
    Core::ComputePipelineHandle m_surfelHashBuildPipeline;
    Core::ShaderHandle m_surfelTraceShader;
    Core::ComputePipelineHandle m_surfelTracePipeline;
    // Compute gather avoids frames-in-flight pool races with deferred lighting.
    Core::BindingLayoutHandle m_surfelResolveBindingLayout;
    Core::ShaderHandle m_surfelResolveShader;
    Core::ComputePipelineHandle m_surfelResolvePipeline;
    // Surface-gated upsample from half-resolution irradiance.
    Core::BindingLayoutHandle m_surfelUpsampleBindingLayout;
    Core::ShaderHandle m_surfelUpsampleShader;
    Core::ComputePipelineHandle m_surfelUpsamplePipeline;
    Core::BindingLayoutHandle m_surfelTraceBuildArgsBindingLayout;
    Core::ShaderHandle m_surfelTraceBuildArgsShader;
    Core::ComputePipelineHandle m_surfelTraceBuildArgsPipeline;
    // HW trace uses the TLAS instead of the SW BVH.
    Core::BindingLayoutHandle m_surfelTraceHwBindingLayout;
    Core::ShaderHandle m_surfelTraceHwShader;
    Core::ComputePipelineHandle m_surfelTraceHwPipeline;
    // Device-lifetime pool, hash heads, and counters; initialized once.
    Core::BufferHandle m_surfelPoolBuffer;
    Core::BufferHandle m_surfelCellHeadBuffer;
    Core::BufferHandle m_surfelCounterBuffer;
    Core::BufferHandle m_surfelTraceIndirectArgsBuffer;
    Core::BufferHandle m_surfelFreeListBuffer;
    // Previous-frame field prevents read/write feedback during tracing.
    Core::BufferHandle m_surfelPoolSnapshotBuffer;
    Core::BufferHandle m_surfelCellHeadSnapshotBuffer;
    // Async-safe counter readback. The graph-owned copy task publishes this only after its packet accepts.
    Core::BufferHandle m_surfelCounterReadback;
    Core::BufferHandle m_surfelConstants;

    // Descriptor generations retire before their backing buffers.
    Core::GpuDescriptorHandle m_surfelConstantsHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle m_surfelPoolHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle m_surfelCellHeadHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle m_surfelCounterHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle m_surfelTraceIndirectArgsHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle m_surfelFreeListHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle m_surfelPoolSnapshotHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle m_surfelCellHeadSnapshotHeapHandle = Core::GpuDescriptorHandle::invalid();
    // Surfel owns its shared material-context heap view.
    Core::GpuDescriptorHandle m_surfelMaterialContextSlotsHeapHandle = Core::GpuDescriptorHandle::invalid();

    u32 m_surfelCountReadbackFrame = 0u;
    u32 m_surfelPoolCapacity = NWB_SURFEL_POOL_CAPACITY;
    u32 m_surfelHashCellCount = NWB_SURFEL_HASH_CELL_COUNT;
    // Ray-rotation and age counter.
    u32 m_surfelFrameIndex = 0u;

    bool m_surfelTraceHwPipelineFailed = false;
    bool m_surfelUseHwTrace = false;
    bool m_surfelTraceBuildArgsPipelineFailed = false;
    bool m_surfelUpsamplePipelineFailed = false;
    bool m_surfelResolvePipelineFailed = false;
    bool m_surfelTracePipelineFailed = false;
    bool m_surfelHashBuildPipelineFailed = false;
    bool m_surfelAgeFreePipelineFailed = false;
    bool m_surfelSpawnPipelineFailed = false;
    bool m_surfelEnabled = false;
    Core::QueueSubmissionToken m_surfelCountReadbackSubmissionToken;
    // First trace uses all surfels; later traces use the update divisor.
    bool m_surfelSeeded = false;
    // Retries graph-owned resource clears after a rejected packet.
    bool m_surfelResourcesNeedClear = false;
    bool m_surfelResourcesClearPending = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererRayTracingState final : NoCopy, public RtSceneBvhState, public RtShadowState, public RtSoftShadowState, public RtCausticState, public RtSurfelGiState{
    friend class RendererRayTracingSystem;

public:
    // RtShadowState needs the renderer arena for its persistent tables.
    explicit RendererRayTracingState(Core::Alloc::GlobalArena& arena)
        : RtShadowState(arena)
    {}


public:
    void invalidateResources();
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

