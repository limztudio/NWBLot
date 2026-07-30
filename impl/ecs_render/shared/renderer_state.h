// limztudio@gmail.com


#pragma once


#include <impl/ecs_render/kernel/renderer_types.h>

#include <core/ecs/entity_id.h>
#include <core/graphics/rhi/gpu_descriptor_heap.h>

#include <impl/assets/graphics/mesh/runtime_constants.h>
#include <impl/assets/graphics/scene/binding_slots.h>
#include <impl/assets/graphics/shadow/binding_slots.h>
#include <impl/assets/graphics/shadow/sw_binding_slots.h>
#include <impl/assets/graphics/caustic/sw_binding_slots.h>
#include <impl/assets/graphics/caustic/resolve_binding_slots.h>
#include <impl/assets/graphics/gi/surfel/surfel_binding_slots.h>

#include <global/generic.h>
#include <global/containers.h>   // dynamic Vector storage for the per-frame SW distinct-mesh table


NWB_IMPL_BEGIN


class RendererSystem;
class RendererShaderSystem;
class RendererMeshSystem;
class RendererMaterialSystem;
class RendererCsgSystem;
class RendererDeferredSystem;
class RendererAvboitSystem;
class RendererRayTracingSystem;


// Device-lifetime backing resources for the first material-authored-resource slice. The fixture payloads are shared
// by all materials, while each MaterialSurfaceInfo receives the matching global-heap slot word in its typed constants.
struct RendererMaterialResourceFixtureState{
    Core::TextureHandle checkerRgba8Texture;
    Core::SamplerHandle linearClampSampler;
    Core::GpuDescriptorHandle checkerRgba8HeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle linearClampHeapHandle = Core::GpuDescriptorHandle::invalid();
};


// Cross-frame Buffer*-keyed descriptor-heap handle cache. Reappearing buffers reuse their handles; unseen buffers are
// freed and evicted at the end of the gather.
//
// A refcounted BufferHandle prevents the raw pointer key from being recycled. Entries are added only after allocation
// and descriptor writes succeed; seenThisFrame drives the end-of-gather sweep.
struct RtMeshHeapHandleCacheEntry{
    Core::BufferHandle keepAlive;                       // refcount pin so the Buffer* key cannot be recycled under us
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


struct CsgFrameStateCacheSignature{
    u64 contentHash = 0u;
    u64 shapeRegistryRevision = 0u;

    friend bool operator==(const CsgFrameStateCacheSignature& lhs, const CsgFrameStateCacheSignature& rhs){
        return lhs.contentHash == rhs.contentHash
            && lhs.shapeRegistryRevision == rhs.shapeRegistryRevision;
    }
};


class RendererMeshState final : NoCopy{
    friend class RendererSystem;
    friend class RendererShaderSystem;
    friend class RendererMeshSystem;
    friend class RendererMaterialSystem;
    friend class RendererCsgSystem;
    friend class RendererDeferredSystem;
    friend class RendererAvboitSystem;
    friend class RendererRayTracingSystem;

public:
    explicit RendererMeshState(Core::Alloc::GlobalArena& arena);


public:
    void invalidateResources();


private:
    HashMap<Name, MeshResources, Hasher<Name>, EqualTo<Name>, Core::Alloc::GlobalArena> m_meshes;
};

class RendererMaterialState final : NoCopy{
    friend class RendererSystem;
    friend class RendererShaderSystem;
    friend class RendererMeshSystem;
    friend class RendererMaterialSystem;
    friend class RendererCsgSystem;
    friend class RendererDeferredSystem;
    friend class RendererAvboitSystem;

public:
    explicit RendererMaterialState(Core::Alloc::GlobalArena& arena);


public:
    void invalidateResources();


private:
    HashMap<Name, MaterialSurfaceInfo, Hasher<Name>, EqualTo<Name>, Core::Alloc::GlobalArena> m_surfaceInfos;
    RendererMaterialResourceFixtureState m_resourceFixtures;
    HashMap<MaterialPipelineKey, MaterialPipelineResources, MaterialPipelineKeyHasher, MaterialPipelineKeyEqualTo, Core::Alloc::GlobalArena> m_pipelines;
    HashMap<Core::ECS::EntityID, MaterialInstanceMutableCacheEntry, Hasher<Core::ECS::EntityID>, EqualTo<Core::ECS::EntityID>, Core::Alloc::GlobalArena> m_instanceMutableCache;
    HashMap<Name, RenderPath::Enum, Hasher<Name>, EqualTo<Name>, Core::Alloc::GlobalArena> m_loggedMaterialPaths;
    u64 m_instanceMutableCacheComponentMutationVersion = 0u;
};

class RendererDrawState final : NoCopy{
    friend class RendererSystem;
    friend class RendererShaderSystem;
    friend class RendererMeshSystem;
    friend class RendererMaterialSystem;
    friend class RendererCsgSystem;
    friend class RendererDeferredSystem;
    friend class RendererAvboitSystem;
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
    // Raster material stages reach these through the global heap. The handles retain the backing buffers until
    // free()'s in-flight quarantine matures, so a capacity-growth replacement always gets fresh slots.
    Core::GpuDescriptorHandle m_instanceBufferHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle m_materialTypedBufferHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle m_meshViewBufferHeapHandle = Core::GpuDescriptorHandle::invalid();
    u8 m_meshViewGpuData[sizeof(f32) * NWB_MESH_VIEW_FLOAT_COUNT] = {};
    bool m_meshViewGpuDataValid = false;
};
static_assert(sizeof(RendererDrawState) == 368u, "RendererDrawState should keep its compact CPU-only layout");

class RendererCsgState final : NoCopy{
    friend class RendererSystem;
    friend class RendererShaderSystem;
    friend class RendererMeshSystem;
    friend class RendererMaterialSystem;
    friend class RendererCsgSystem;
    friend class RendererDeferredSystem;
    friend class RendererAvboitSystem;

public:
    RendererCsgState() = default;


public:
    void invalidateResources();


private:
    // CSG layouts carry only their push ranges.  Every resource declaration is a global descriptor-heap view.
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
    // CSG clip/cap-fill persistent inputs and its target-generation selectors live in UniformBuffer heap payloads.
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
    friend class RendererSystem;
    friend class RendererShaderSystem;
    friend class RendererMeshSystem;
    friend class RendererMaterialSystem;
    friend class RendererCsgSystem;
    friend class RendererDeferredSystem;
    friend class RendererAvboitSystem;
    friend class RendererRayTracingSystem;

public:
    RendererDeferredState() = default;


public:
    void invalidateResources();


private:
    Core::BindingLayoutHandle m_lightingBindingLayout;
    Core::BufferHandle m_sceneShadingBuffer;
    Core::BufferHandle m_lightBuffer;
    Core::ShaderHandle m_compositeVertexShader;
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
    DeferredFrameTargets m_targets;
};

class RendererAvboitState final : NoCopy{
    friend class RendererSystem;
    friend class RendererShaderSystem;
    friend class RendererMeshSystem;
    friend class RendererMaterialSystem;
    friend class RendererCsgSystem;
    friend class RendererDeferredSystem;
    friend class RendererAvboitSystem;

public:
    RendererAvboitState() = default;


public:
    void invalidateResources();


private:
    // The sole AVBOIT pipeline-local layout carries the common 128-byte draw push-constant range. Every pass
    // resource, including AVBOIT work buffers, is selected from the global descriptor heap.
    Core::BindingLayoutHandle m_emptyBindingLayout;
    Core::SamplerHandle m_linearSampler;
    Core::ShaderHandle m_depthWarpComputeShader;
    Core::ShaderHandle m_integrateComputeShader;
    Core::ComputePipelineHandle m_depthWarpPipeline;
    Core::ComputePipelineHandle m_integratePipeline;
    bool m_targetsNeedClear = true;
};

// RendererRayTracingState is decomposed into per-concern PUBLIC-membered state structs (below) that the class
// inherits, so a feature's ray-tracing state lives in its own struct while every rayTracingState().m_* access
// site keeps working unchanged. Grouping is by concern; the shared SW-BVH substrate + generic flags sit in
// RtSceneBvhState.

struct RtSceneBvhState{
    Core::RayTracingAccelStructHandle m_tlas;
    // Immutable descriptor-buffer generation for m_tlas. A replacement TLAS receives a fresh heap handle/block so
    // recorded command buffers continue to see the old AS until deferred free retires it.
    Core::GpuDescriptorHandle m_tlasHeapHandle = Core::GpuDescriptorHandle::invalid();
    usize m_tlasMaxInstances = 0u;
    u64 m_tlasDeviceAddress = 0u;
    u32 m_tlasInstanceCount = 0u; // live TLAS instance count (set by buildSceneTlas); the HW caustic raygen's non-zero guard
    u32 m_sceneBvhInstanceCount = 0u;
    // HYBRID shadow split (RT hardware): the HW RayQuery pass casts the binary OPAQUE shadow; when the scene holds a
    // transparent occluder, the software traversal additionally casts the colored TRANSPARENT shadow and multiplies it
    // onto the opaque mask. m_sceneHasTransparentOccluder (set by buildSceneTlas) gates building the software BVH on RT
    // hardware; m_hybridTransparentShadowReady (set by the prepare) tells the render to run the SW multiply pass.
    bool m_sceneHasTransparentOccluder = false;
    bool m_hybridTransparentShadowReady = false;
    bool m_prevWorldToClipValid = false;
    bool m_swCausticDispatchLogged = false;
    Core::BindingLayoutHandle m_bvhSortBindingLayout;
    Core::ShaderHandle m_bvhSortShader;
    Core::ComputePipelineHandle m_bvhSortPipeline;
    Core::BufferHandle m_bvhSortKeysBuffer;
    Core::BufferHandle m_bvhSortPayloadBuffer;
    // The sort scratch is selected by push-constant heap slots. Keep the handles beside the buffers so a capacity
    // replacement can retire the old descriptor generation after in-flight work drains.
    Core::GpuDescriptorHandle m_bvhSortKeysHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle m_bvhSortPayloadHeapHandle = Core::GpuDescriptorHandle::invalid();
    usize m_bvhSortCapacity = 0u;
    Core::BindingLayoutHandle m_bvhBuildBindingLayout;
    Core::ShaderHandle m_bvhMortonShader;
    Core::ComputePipelineHandle m_bvhMortonPipeline;
    Core::ShaderHandle m_bvhTopologyShader;
    Core::ComputePipelineHandle m_bvhTopologyPipeline;
    Core::ShaderHandle m_bvhFitShader;
    Core::ComputePipelineHandle m_bvhFitPipeline;
    Core::BufferHandle m_bvhVisitCounterBuffer;
    Core::GpuDescriptorHandle m_bvhVisitCounterHeapHandle = Core::GpuDescriptorHandle::invalid();
    usize m_bvhBuildCapacity = 0u;
    Core::BufferHandle m_sceneBvhNodeBuffer;  // CPU-built scene/instance LBVH (TLAS-analog), uploaded each frame
    Core::BufferHandle m_sceneInstanceBuffer; // per-instance world->object transform + reserved ABI word + BVH leaf cost
    // The software scene BVH's two persistent context buffers are global StorageBuffer heap entries. The common
    // trace-context slot cbuffer selects these current generations for SW shadow, GI, and caustics.
    Core::GpuDescriptorHandle m_sceneBvhNodeHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle m_sceneInstanceHeapHandle = Core::GpuDescriptorHandle::invalid();
    // Tiny renderer-owned metadata cbuffer carrying the five global heap slots for the scene/material context. It is
    // updated after each scene gather; each trace pass selects this slot-indirection payload through its own contract.
    Core::BufferHandle m_rayTraceMaterialContextSlotsBuffer;
    // Every trace family selects the shared trace-context payload through a persistent UniformBuffer heap descriptor.
    // Each owner retains its own generation so teardown can retire descriptors without coupling feature lifetimes.
    Core::GpuDescriptorHandle m_causticMaterialContextSlotsHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle m_shadowMaterialContextSlotsHeapHandle = Core::GpuDescriptorHandle::invalid();
    usize m_sceneBvhNodeCapacity = 0u;
    usize m_sceneInstanceCapacity = 0u;
    // Soft opaque shadow TEMPORAL accumulation: the reproject-merge pass
    // inserted per slot between the soft trace and the a-trous resolve, plus the CPU-side state it needs. There are NO
    // motion vectors / prev-G-buffer in this engine (the view is rebuilt fresh each frame), so the merge reprojects the
    // current world position through a STASHED previous-frame worldToClip -- exact for a static receiver regardless of how
    // the occluder moved, collapsing the moving-occluder anti-ghost to a value-agreement test. See shadow_reproject_merge_cs.
    //  - m_prevWorldToClip: last frame's worldToClip (raw 16-float row-major dump of drawState().m_meshViewGpuData's
    //    worldToClip), stashed at the end of renderGpuBvhShadowVisibility for NEXT frame's reprojection push constant.
    //    m_prevWorldToClipValid is false on frame 0 / after a resize (invalidated in createShadowVisibilityTarget) -> the
    //    merge's historyValid gate forces pure-current so it can't reproject through a stale matrix into fresh garbage.
    Float44U m_prevWorldToClip = {};
    // Software caustic photon producer — the no-hardware-ray-tracing fallback. It reuses the same software
    // scene/instance + per-mesh BVH buffers the SW shadow trace builds (the shared m_swShadowMesh* table serves
    // shadow, caustic, and GI alike). The emission targets, camera view, and G-buffer depth/world position are global-
    // heap reads selected through the shared photon push constants. The two selector payloads and R32_UINT accumulator
    // are global heap entries too, so the local layout carries only the push-constant range.
    Core::BindingLayoutHandle m_swCausticBindingLayout;
    Core::ShaderHandle m_swCausticShader;
    Core::ComputePipelineHandle m_swCausticPipeline;
    bool m_swCausticPipelineFailed = false;
    bool m_hwCausticPipelineFailed = false;
    bool m_hwCausticDispatchLogged = false;
    bool m_capabilityLogged = false;
    // Hardware ray-traced caustic photon producer -- the byte-parallel sibling of the SW producer. It uses the
    // TLAS plus heap-selected instance-material, index, position, and attribute buffers; the refraction bends on the
    // interpolated shading normal from the attributes. Feeds the SAME R32_UINT accumulator + the SAME resolve the SW
    // path uses. Its emission/view/G-buffer reads, selector payloads, and accumulator use the global heap; the set-10
    // heap block supplies the TLAS, leaving the local layout push-only.
    Core::BindingLayoutHandle m_hwCausticBindingLayout;
    Core::RayTracingPipelineHandle m_hwCausticPipeline;
    Core::RayTracingShaderTableHandle m_hwCausticShaderTable;
    bool m_bvhSortPipelineFailed = false;
    bool m_bvhBuildPipelineFailed = false;
};


struct RtShadowState{
    // The per-frame distinct-mesh table Vectors (the HW m_shadowMesh* table and the SW m_swShadowMesh*
    // table, both below) plus the stable Buffer*-keyed handle cache allocate from the renderer's global arena,
    // bound once here at construction. The arena allocator cannot be rebound afterward (its copy-assign is a
    // deliberate no-op), so RendererRayTracingState threads the arena in. Every other member keeps its default
    // initializer -- listing only the arena-bound containers, in declaration order (the HW table first, then the SW
    // table, then the handle cache).
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

    Core::BindingLayoutHandle m_shadowBindingLayout;
    // Hardware shadow trace is an inline-RayQuery COMPUTE pass (shadow_rayquery_cs); the per-occluder optical-depth
    // accumulator lives in a compute local, which a hardware ray payload could not index safely.
    Core::ShaderHandle m_shadowShader;
    Core::ComputePipelineHandle m_shadowPipeline;
    // Active shadow slots this frame (= min(lightCount, NWB_SCENE_SHADOW_SLOT_COUNT)), set during the light upload and
    // read by the edge-adaptive shadow resolve so it only processes the slots that hold a light.
    u32 m_shadowSlotCount = 0u;
    // Per-frame instance-material table (NwbRtInstanceMaterialGpu), shared by the hardware and software trace
    // paths; built lockstep with the TLAS / scene-instance buffer so the array index matches the
    // shadow instance id. Grows by doubling, never shrinks.
    Core::BufferHandle m_shadowInstanceMaterialBuffer;
    usize m_shadowInstanceMaterialCapacity = 0u;
    // Trace-owned combined material-constants context used by per-hit surface evaluation (g_NwbMeshInstances
    // + g_NwbMaterialTypedWords). The draw passes' equivalents hold only ONE transparency class at trace time (the
    // opaque set is resident; the transparent occluders' blocks are uploaded after the trace), so the trace builds
    // its own combined buffers over ALL gathered occluders (both transparency classes) lockstep with the shadow
    // instances. m_shadowInstanceBuffer = InstanceGpuData per occluder (mutable byte offset in translation.w);
    // m_shadowMaterialTypedBuffer = each occluder's constant + mutable typed blocks (constant offset stored in the
    // instance-material record). Both grow by doubling, never shrink, and are shared by the shadow, GI, and caustic
    // trace paths.
    Core::BufferHandle m_shadowInstanceBuffer;
    Core::BufferHandle m_shadowMaterialTypedBuffer;
    // Global StorageBuffer heap entries for the three shadow-owned portions of the shared trace material context.
    // A capacity-growth replacement receives a fresh descriptor so previously recorded work retains the old resource
    // until the heap's deferred-free quarantine retires it.
    Core::GpuDescriptorHandle m_shadowInstanceMaterialHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle m_shadowInstanceHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle m_shadowMaterialTypedHeapHandle = Core::GpuDescriptorHandle::invalid();
    usize m_shadowInstanceCapacity = 0u;
    usize m_shadowMaterialTypedCapacity = 0u;
    // Per-frame distinct meshes referenced by the TLAS (filled by buildSceneTlas). The three backing buffers feed the
    // global-heap descriptors the HW caustic/GI passes read through material.{index,attribute,position}Slot; the HW GI
    // trace also needs raw positions to derive geometric face normals, so the position buffer is tracked here too. Dynamic GlobalArena
    // Vectors (bound in the RtShadowState ctor above) grown on demand, so no distinct mesh is ever dropped.
    Vector<Core::Buffer*, Core::Alloc::GlobalArena> m_shadowMeshIndexBuffers;
    Vector<Core::Buffer*, Core::Alloc::GlobalArena> m_shadowMeshAttributeBuffers;
    Vector<Core::Buffer*, Core::Alloc::GlobalArena> m_shadowMeshPositionBuffers;
    // Parallel global-heap handles for the three backing buffers above. The cache reuses them across frames; HW
    // caustic reads attributes and HW GI reads positions, indices, and attributes through NwbHeapRawBuffer().
    Vector<Core::GpuDescriptorHandle, Core::Alloc::GlobalArena> m_shadowMeshIndexHandles;
    Vector<Core::GpuDescriptorHandle, Core::Alloc::GlobalArena> m_shadowMeshAttributeHandles;
    Vector<Core::GpuDescriptorHandle, Core::Alloc::GlobalArena> m_shadowMeshPositionHandles;
    u32 m_shadowMeshCount = 0u;
    u32 m_shadowMeshHeapHighWater = 0u;
    // Adaptive transparent shadow (coarse-trace + edge-refine) config, fixed at shipping defaults (adaptive ON,
    // edge threshold 0.1, stats OFF). These flags drive the transparent economizer path used by the HW-hybrid backend
    // (renderGpuBvhShadowVisibility multiplyOntoOpaque=true). The full-res opaque prepass remains the SW-path
    // baseline/fallback.
    bool m_swShadowAdaptiveEnabled = true;
    bool m_swShadowEdgeStatsEnabled = false;
    // Compacted-indirect resolve (default ON): when set and adaptive is on, classify+append -> build-args ->
    // DispatchIndirect trace launches only edge rays as coherent waves. Disabled falls back to the coarse/adaptive path.
    bool m_swShadowCompactEnabled = true;
    // Software (compute) shadow traversal, decomposed into one named pipeline per pass. The shared layout is push-only;
    // every G-buffer, context, output, and work-buffer descriptor is selected from the global heap.
    Core::BindingLayoutHandle m_swShadowBindingLayout;
    // One compute pipeline per software-shadow pass (created lazily; each loads its own kernel). A per-pass shader handle
    // keeps each kernel resident for its pipeline.
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
    // Soft COLORED TRANSPARENT trace: the colored (Beer-Lambert/Fresnel) analog of the soft opaque trace.
    Core::ShaderHandle m_swShadowTransparentSoftShader;
    Core::ComputePipelineHandle m_swShadowTransparentSoftPipeline;
    u32 m_swShadowMeshCount = 0u;
    // Per-frame distinct meshes referenced by the software scene BVH (filled by buildSceneSwBvh). The SW shadow /
    // caustic / GI traces fetch this geometry from the global descriptor heap by the per-buffer slots carried on the
    // instance-material record. The Vectors grow on demand -- no fixed per-frame mesh cap -- and are cleared (capacity
    // retained) each rebuild; m_swShadowMeshCount mirrors their length. All eight grow in lockstep (one push per
    // distinct mesh), so slot k indexes them all.
    Vector<Core::Buffer*, Core::Alloc::GlobalArena> m_swShadowMeshNodeBuffers;
    Vector<Core::Buffer*, Core::Alloc::GlobalArena> m_swShadowMeshPositionBuffers;
    Vector<Core::Buffer*, Core::Alloc::GlobalArena> m_swShadowMeshIndexBuffers;
    Vector<Core::Buffer*, Core::Alloc::GlobalArena> m_swShadowMeshAttributeBuffers; // U2 per-vertex normal/uv0 for the per-hit transmittance dispatch
    // Parallel global-heap handles for the four backing buffers above. Nodes use StructuredBuffer<NwbBvhNode>; the
    // other buffers use raw views. The cache reuses handles across frames, and SW shadow, caustic, and GI read them.
    Vector<Core::GpuDescriptorHandle, Core::Alloc::GlobalArena> m_swShadowMeshNodeHandles;
    Vector<Core::GpuDescriptorHandle, Core::Alloc::GlobalArena> m_swShadowMeshPositionHandles;
    Vector<Core::GpuDescriptorHandle, Core::Alloc::GlobalArena> m_swShadowMeshIndexHandles;
    Vector<Core::GpuDescriptorHandle, Core::Alloc::GlobalArena> m_swShadowMeshAttributeHandles;
    u32 m_swShadowMeshHeapHighWater = 0u;
    // Stable Buffer*-keyed descriptor-heap handle caches: buildSceneTlas (HW) and buildSceneSwBvh (SW) each keep
    // their own cross-frame cache so a per-gather sweep frees only that gather's dropouts without touching the other
    // (on the hybrid backend the SW gather runs after the HW one and reuses position/index/attribute buffers). Every
    // backing buffer lands in its gather's cache once and is reused across frames; a buffer unseen this frame is freed
    // + evicted at end of gather. Arena-bound at construction (see RtShadowState ctor).
    RtMeshHeapHandleCache m_hwMeshHeapHandleCache;
    RtMeshHeapHandleCache m_swMeshHeapHandleCache;
    f32 m_swShadowEdgeThreshold = 0.1f;
    bool m_swShadowEdgeStatsPending = false;
    bool m_shadowResolvePipelineFailed = false;
    bool m_shadowResolveRgbPipelineFailed = false;
    // Edge-fraction instrumentation: a 2-uint UAV counter the resolve tallies into ([0] traced rays, [1] total rays),
    // snapshotted into a CPU-readable buffer on a slow cadence. The tick throttles attempts, while the accepted
    // shadow-submission token below proves the copy completed before mapping on a dedicated Compute queue.
    Core::BufferHandle m_swShadowEdgeStatsBuffer;
    Core::GpuDescriptorHandle m_swShadowEdgeStatsHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::BufferHandle m_swShadowEdgeStatsReadback;
    u32 m_swShadowEdgeStatsTick = 0u;
    u64 m_swShadowEdgeStatsPendingSubmissionID = 0u;
    Core::CommandQueue::Enum m_swShadowEdgeStatsPendingSubmissionQueue = Core::CommandQueue::kCount;
    bool m_swShadowEdgeStatsPendingSubmissionUnconfirmed = false;
    u32 m_swShadowEdgeListCapacity = 0u; // capacity in RECORDS
    // Soft opaque shadow RESOLVE: the a-trous wavelet denoise pipeline selects ping-pong inputs and storage outputs
    // through dispatch heap slots; its layout is push-only.
    Core::BindingLayoutHandle m_shadowResolveBindingLayout;
    Core::ShaderHandle m_shadowResolveShader;
    Core::ComputePipelineHandle m_shadowResolvePipeline;
    // ---- Parallel soft COLORED TRANSPARENT denoise state (mirrors the opaque m_shadowResolve* / merge blocks) ----
    // The RGB a-trous resolve pipeline: the SAME shadow_resolve source cooked with NWB_SHADOW_RESOLVE_CHANNELS=3 (via the
    // shadow_resolve_rgb_cs wrapper). It shares the resolve BINDING LAYOUT (identical bindings; only the wavelet channel
    // count + a runtime fold flag differ), so only a distinct shader + pipeline handle are needed, not a new layout.
    Core::ShaderHandle m_shadowResolveRgbShader;
    Core::ComputePipelineHandle m_shadowResolveRgbPipeline;
    bool m_shadowPipelineFailed = false;
    bool m_swShadowPipelineFailed = false;
    bool m_swShadowDispatchLogged = false;
    u32 m_swShadowEdgeStatsPendingTick = 0u;
    // Stage-3 compaction resources are StorageBuffer heap entries. The edge list is recreated with the visibility target;
    // a replacement descriptor generation keeps in-flight dispatches valid through heap retirement.
    Core::BufferHandle m_swShadowEdgeCounterBuffer;
    Core::GpuDescriptorHandle m_swShadowEdgeCounterHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::BufferHandle m_swShadowEdgeListBuffer;
    Core::GpuDescriptorHandle m_swShadowEdgeListHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::BufferHandle m_swShadowIndirectArgsBuffer;
    Core::GpuDescriptorHandle m_swShadowIndirectArgsHeapHandle = Core::GpuDescriptorHandle::invalid();
};


struct RtSoftShadowState{
    // Hardware (RayQuery) SOFT OPAQUE half-res trace. It reuses the shared push-only shadow layout; heap slots select
    // soft-A as its output, so HW and SW feed the same temporal + a-trous chain.
    Core::ShaderHandle m_shadowSoftShader;
    Core::ComputePipelineHandle m_shadowSoftPipeline;
    bool m_shadowSoftPipelineFailed = false;
    bool m_shadowGeometryDownsamplePipelineFailed = false;
    // Set by prepareShadowVisibilityResources when the soft opaque pipelines are ready this frame; gates the render's
    // soft opaque trace + resolve dispatch.
    // A failure here is non-fatal to shadows -- the slot lights simply keep their hard opaque mask this frame.
    bool m_softShadowReady = false;
    // Set by prepareShadowVisibilityResources when m_softShadowReady and the heap-only merge pipeline is ready; gates the per-slot merge insertion +
    // the frame-end stash/swap. Non-fatal: a failure leaves it false and the soft path runs its non-temporal fallback.
    bool m_softShadowTemporalReady = false;
    // The set of shadow slots this frame that hold a shadow slot (params.z >= 0), regardless of light type -- the slots
    // the soft opaque path traces + denoises + upsamples. A directional light softens by its constant angular radius
    // (params2.x), a point/spot light by the distance-dependent cone its source sphere (params2.y) subtends; both are
    // handled inside the trace, so every slot light is soft. A bitmask (slot k -> bit k) over the
    // NWB_SCENE_SHADOW_SLOT_COUNT pool, filled by updateSceneShadingBuffer from the resolved light data. The resolve
    // dispatches once per set bit (its lightSlotStart/lightSlotCount address a single slot), so the scattered slot
    // assignment (a light can land on any slot index) is handled without a contiguous range assumption.
    u32 m_softShadowSlotMask = 0u;
    // Soft opaque shadow (soft-ray-traced-shadow feature): a per-frame counter seeding the per-pixel low-discrepancy
    // cone-jitter sample. Incremented once per frame by whichever primary shadow producer runs (the HW RayQuery opaque
    // trace on the HW path, the no-RT software traversal otherwise -- mutually exclusive per frame), so each pixel's
    // single jittered ray walks the source across frames. No temporal reuse this stage, so this only decorrelates the
    // per-frame sample (a later stage feeds it into a temporal accumulator).
    u32 m_softShadowFrameIndex = 0u;
    // Shadow geometry downsample pre-pass (its own pipeline): fills the half-res packed geometry cache (octahedral
    // normal + camera distance + validity) the resolve passes read for the edge-stop, so they tap one half-res texel.
    Core::BindingLayoutHandle m_shadowGeometryDownsampleBindingLayout;
    Core::ShaderHandle m_shadowGeometryDownsampleShader;
    Core::ComputePipelineHandle m_shadowGeometryDownsamplePipeline;
    // Cleared whenever the temporal targets are (re)created (createShadowVisibilityTarget); the FIRST merge dispatch sets it
    // true. Until then the merge treats every pixel as n=0 (pure current sample) -- the clean first-frame / post-resize path.
    bool m_softShadowTemporalSeeded = false;
    // The soft packet records its temporal output before the independent caustics and surfel-GI packets finish
    // validating the shared deferred target bundle. Defer the CPU-side history-handle swap until the complete ordered
    // submission
    // succeeds, so worker recording never mutates that shared bundle concurrently.
    bool m_softShadowTemporalHistoryAdvancePending = false;
    bool m_shadowReprojectMergePipelineFailed = false;
    // Gates (mirror m_softShadowReady / m_softShadowTemporalReady): set by prepareShadowVisibilityResources when the RGB
    // resolve and shared transparent merge pipelines are ready. Non-fatal: a failure leaves the soft transparent path off; the
    // transparent coarse/adaptive path then runs its colored multiply, with no double-fold because the paths are exclusive.
    bool m_softTransparentTemporalReady = false;
    bool m_softTransparentReady = false;
    // Ping-pong selector: 1 = shadowHistA/MomentsA hold the INCOMING history this frame (merge writes B), 0 = the reverse.
    // Flipped by the frame-end swap. Also selects temporal input/output heap slots.
    u32 m_softShadowHistoryFrontIsA = 1u;
    // The reproject-merge pipeline selects its front/back history and moments output roles directly from heap slots.
    Core::BindingLayoutHandle m_shadowReprojectMergeBindingLayout;
    Core::ShaderHandle m_shadowReprojectMergeShader;
    Core::ComputePipelineHandle m_shadowReprojectMergePipeline;
};


struct RtCausticState{
    // Caustic emission targets: per-frame world-space AABBs of every refractive instance, the domain the caustic
    // photon producer aims at. A single global list is shared by all caustic lights. Resident structured SRV
    // ({ float4 aabbMin; float4 aabbMax; }), CPU-written each frame, grows by doubling, never shrinks; the count gates
    // caustic-light assignment together with per-light opt-in (zero refractive instances or zero opted-in lights ->
    // zero caustic lights). m_causticTargetBoundsMin/Max hold the combined extent over all targets (for the emission gate
    // log); m_causticEmissionGateLogged rate-limits that log.
    Core::BufferHandle m_causticEmissionTargetBuffer;
    // Global StorageBuffer heap descriptor for the emission-target buffer. Capacity replacement acquires a new slot
    // before retiring this one, so recorded photon dispatches retain their original generation until GPU completion.
    Core::GpuDescriptorHandle m_causticEmissionTargetHeapHandle = Core::GpuDescriptorHandle::invalid();
    usize m_causticEmissionTargetCapacity = 0u;
    u32 m_causticRefractiveInstanceCount = 0u;
    // Caustic-light count assigned this frame by ResolveCausticLights (cached in updateSceneShadingBuffer). Gates
    // the software caustic producer dispatch: the producer runs only when this AND m_causticRefractiveInstanceCount
    // are both > 0 (else the black-cleared irradiance buffer is the additive no-op).
    u32 m_causticLightCount = 0u;
    Float4 m_causticTargetBoundsMin = Float4(0.f, 0.f, 0.f, 0.f);
    Float4 m_causticTargetBoundsMax = Float4(0.f, 0.f, 0.f, 0.f);
    // The caustic resolve is a purely SPATIAL a-trous wavelet denoise. The one bit of temporal state is the SPLAT-SPACE
    // EMA in the R32_UINT accumulator (m_causticTemporalDecay > 0): the accumulator is decayed then re-splatted each
    // frame instead of cleared, so the sparkle-flicker of a moving caustic averages out WITHOUT any image-space
    // reprojection (reprojection would ghost). Fixed at 0.85 (a moderate ~6-7 frame time constant). The static steady
    // state is photons/(1-decay), so the resolve pre-multiplies causticIntensity by (1-decay) to keep the STATIC
    // brightness byte-unchanged.
    f32 m_causticTemporalDecay = 0.85f;
    // SW-only 2x temporal-reuse checkerboard phase: the software producer emits half the photon grid each frame, on a
    // frame-parity checkerboard, so the two-frame union covers the full stratified domain at half the per-frame BVH
    // cost (the splat-space EMA recombines the two halves). phase = m_swCausticFrameIndex & 1.
    u32 m_swCausticFrameIndex = 0u;
    // HW-only 2x temporal-reuse checkerboard phase: the byte-parallel hardware producer mirrors the SW scheme --
    // emits half the photon grid each frame on a frame-parity checkerboard so the two-frame union covers the full
    // stratified domain at half the per-frame TraceRay cost (the splat-space EMA recombines the two halves). phase =
    // m_hwCausticFrameIndex & 1.
    u32 m_hwCausticFrameIndex = 0u;
    bool m_causticEmissionGateLogged = false;
    bool m_causticGeometryDownsamplePipelineFailed = false;
    bool m_causticResolvePipelineFailed = false;
    bool m_causticAccumulatorInitialized = false;
    // Caustic resolve pass: an N-pass edge-avoiding a-trous wavelet denoise. The single compute pipeline is
    // dispatched per pass with all inputs and outputs selected through target-generation heap slots in the dispatch
    // push constants.
    Core::BindingLayoutHandle m_causticResolveBindingLayout;
    Core::ShaderHandle m_causticResolveShader;
    Core::ComputePipelineHandle m_causticResolvePipeline;
    // Geometry downsample pre-pass (its own pipeline): fills the half-res geometry cache (world + receiver validity) the
    // resolve passes read, so they tap one half-res texel instead of re-reading the full-res world/depth G-buffer per tap.
    Core::BindingLayoutHandle m_causticGeometryDownsampleBindingLayout;
    Core::ShaderHandle m_causticGeometryDownsampleShader;
    Core::ComputePipelineHandle m_causticGeometryDownsamplePipeline;
    // Caustic accumulator decay pre-pass (splat-space temporal EMA): a single-resource compute pass that multiplies the
    // resident R32_UINT accumulator by m_causticTemporalDecay before the producer splats this frame's photons.
    // m_causticAccumulatorInitialized gates the first-frame (and post-resize) clear-vs-decay: the accumulator holds no
    // valid history until the producer has splatted once, so the first enabled frame clears and every later
    // frame decays. Reset to false wherever the deferred targets are (re)created so a resize re-seeds cleanly.
    Core::BindingLayoutHandle m_causticAccumulatorDecayBindingLayout;
    Core::ShaderHandle m_causticAccumulatorDecayShader;
    Core::ComputePipelineHandle m_causticAccumulatorDecayPipeline;
    bool m_causticAccumulatorDecayPipelineFailed = false;
};


struct RtSurfelGiState{
    // ---- Surfel GI state ----
    // Screen-spawned, world-hashed surfels integrate one-bounce diffuse GI. The persistent buffers (pool / cell-head /
    // counter / params CB) live HERE (beside the caustic block), NOT on DeferredFrameTargets, which is torn down on
    // every window resize and would silently reset surfel convergence. Lifetime = device reset only. Each frame
    // snapshots the previous pool/hash, age-frees stale surfels, spawns and hash-links new surfels, derives indirect
    // trace arguments, traces one workgroup per surfel (64 SW rays -> EMA irradiance), then resolves and upsamples the
    // screen-space irradiance sampled by deferred lighting.
    // Every surfel pass is descriptor-heap-only. The separate layouts retain their independent pipeline lifetimes but
    // carry the same push-constant selector ABI; all CBV/SRV/UAV resources live in the global heap.
    Core::BindingLayoutHandle m_surfelSpawnBindingLayout;
    Core::BindingLayoutHandle m_surfelAgeFreeBindingLayout;
    Core::BindingLayoutHandle m_surfelHashBuildBindingLayout;
    Core::BindingLayoutHandle m_surfelTraceBindingLayout;
    Core::ShaderHandle m_surfelSpawnShader;
    Core::ComputePipelineHandle m_surfelSpawnPipeline;
    // Age-free recycling: one thread per pool slot; frees surfels unseen for maxAge frames + pushes their ids onto
    // the free-list. Depends only on the persistent buffers, so it is built once (like hash-build).
    Core::ShaderHandle m_surfelAgeFreeShader;
    Core::ComputePipelineHandle m_surfelAgeFreePipeline;
    Core::ShaderHandle m_surfelHashBuildShader;
    Core::ComputePipelineHandle m_surfelHashBuildPipeline;
    Core::ShaderHandle m_surfelTraceShader;
    Core::ComputePipelineHandle m_surfelTracePipeline;
    // Resolve pass: a COMPUTE pass that gathers the surfel field once per pixel into the screen-space surfelIrradiance
    // texture the deferred-lighting compute shader samples. Keeping the gather in compute keeps the RW pool off the
    // lighting dispatch, eliminating the frames-in-flight pool race. Its field, G-buffer, and irradiance output are all
    // selected through global heap slots.
    Core::BindingLayoutHandle m_surfelResolveBindingLayout;
    Core::ShaderHandle m_surfelResolveShader;
    Core::ComputePipelineHandle m_surfelResolvePipeline;
    // Half-res producer: the resolve writes surfelIrradianceHalf; this upsample pass reconstructs the full-res
    // surfelIrradiance with a surface-gated joint-bilinear filter (surfel_upsample_cs). Its heap slots select the
    // half-res irradiance + full-res G-buffer normal/world-position, with the full-res output heap-selected too.
    Core::BindingLayoutHandle m_surfelUpsampleBindingLayout;
    Core::ShaderHandle m_surfelUpsampleShader;
    Core::ComputePipelineHandle m_surfelUpsamplePipeline;
    // Trace dispatchIndirect: a 1-thread build-args pass (surfel_trace_buildargs_cs) reads the live high-water
    // BUMP_TOP + the update divisor (surfel CB) and writes the trace's DispatchIndirectArguments into
    // m_surfelTraceIndirectArgsBuffer, so the trace dispatches one workgroup per LIVE surfel instead of the fixed
    // ceil(poolCapacity/divisor). Both SW + HW trace consume the same heap-selected args buffer.
    Core::BindingLayoutHandle m_surfelTraceBuildArgsBindingLayout;
    Core::ShaderHandle m_surfelTraceBuildArgsShader;
    Core::ComputePipelineHandle m_surfelTraceBuildArgsPipeline;
    bool m_surfelTraceHwPipelineFailed = false;
    bool m_surfelUseHwTrace = false;
    bool m_surfelCountReadbackPending = false;
    // HW-RayQuery trace twin (surfel_trace_hw_cs / gi_hw_trace.slangi): a parallel pipeline that reads
    // the scene TLAS and reconstructs the authored surface through heap-selected material-record geometry slots instead
    // of the SW BVH. m_surfelUseHwTrace selects the
    // path in ensureSurfelResources / prepareSurfelResources / renderSurfelGi (set true by the HW-shadow branch, false
    // by the SW branch). The TLAS and trace context are heap-selected at dispatch.
    Core::BindingLayoutHandle m_surfelTraceHwBindingLayout;
    Core::ShaderHandle m_surfelTraceHwShader;
    Core::ComputePipelineHandle m_surfelTraceHwPipeline;
    bool m_surfelTraceBuildArgsPipelineFailed = false;
    bool m_surfelUpsamplePipelineFailed = false;
    bool m_surfelResolvePipelineFailed = false;
    bool m_surfelTracePipelineFailed = false;
    // Persistent surfel buffers (created once, never resized with the window). The pool holds the NwbSurfel records; the
    // cell-head buffer is the spatial-hash linked-list heads (one uint per cell); the counter is 2 u32 (bump top, free
    // top). All UAV-writable; the gather binds the pool + cell-head as SRVs. One-shot init on creation: pool zeroed,
    // cell-head = 0xFFFFFFFF, counter = 0.
    Core::BufferHandle m_surfelPoolBuffer;
    Core::BufferHandle m_surfelCellHeadBuffer;
    Core::BufferHandle m_surfelCounterBuffer;
    // Trace dispatchIndirect args (3 u32 = DispatchIndirectArguments), rewritten by the build-args pass each frame.
    Core::BufferHandle m_surfelTraceIndirectArgsBuffer;
    // Free-list: persistent LIFO stack of recycled surfel ids (poolCapacity uints). Pushed by age-free, popped by
    // spawn; the stack depth lives in counter[FREE_TOP]. Same barrier/state-tracking as the pool.
    Core::BufferHandle m_surfelFreeListBuffer;
    // Snapshot of the previous frame's converged field for the infinite bounce: the trace's bounce gather reads these (SRV)
    // instead of the live pool it is writing, so surfel->surfel feedback reads a stable frame-start field. Both pool +
    // cell-head are snapshotted (mutually consistent prev-frame walk); overwritten by copyBuffer at the top of each frame.
    Core::BufferHandle m_surfelPoolSnapshotBuffer;
    Core::BufferHandle m_surfelCellHeadSnapshotBuffer;
    // Persistent descriptor heap generations. Each is retired before this state releases its backing buffer; the shared
    // deferred target selector is consumed directly from targets.bindless.slotsBufferDescriptor.
    Core::GpuDescriptorHandle m_surfelConstantsHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle m_surfelPoolHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle m_surfelCellHeadHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle m_surfelCounterHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle m_surfelTraceIndirectArgsHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle m_surfelFreeListHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle m_surfelPoolSnapshotHeapHandle = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle m_surfelCellHeadSnapshotHeapHandle = Core::GpuDescriptorHandle::invalid();
    // The ray-trace material-context slot payload is shared with shadows/caustics, but surfel owns this UniformBuffer
    // descriptor generation so the other pass bindings remain unchanged.
    Core::GpuDescriptorHandle m_surfelMaterialContextSlotsHeapHandle = Core::GpuDescriptorHandle::invalid();
    // CPU-readable copy of the counter (BUMP_TOP, FREE_TOP) for the periodic live-count diagnostic. Its accepted
    // producer token proves completion before mapping when the surfel packet records on AsyncCompute.
    Core::BufferHandle m_surfelCounterReadback;
    // Feature gate + per-pipeline-failed flags (mirrors the caustic / shadow precedent).
    bool m_surfelHashBuildPipelineFailed = false;
    bool m_surfelAgeFreePipelineFailed = false;
    bool m_surfelSpawnPipelineFailed = false;
    bool m_surfelEnabled = false;
    u32 m_surfelCountReadbackFrame = 0u;
    u64 m_surfelCountReadbackPendingSubmissionID = 0u;
    Core::CommandQueue::Enum m_surfelCountReadbackPendingSubmissionQueue = Core::CommandQueue::kCount;
    bool m_surfelCountReadbackPendingSubmissionUnconfirmed = false;
    // Params CB (NwbSurfelConstants: 5 x Float4). Uploaded each rendered frame.
    Core::BufferHandle m_surfelConstants;
    u32 m_surfelPoolCapacity = NWB_SURFEL_POOL_CAPACITY;
    u32 m_surfelHashCellCount = NWB_SURFEL_HASH_CELL_COUNT;
    // Per-frame counter seeding the ray rotation + age comparisons.
    u32 m_surfelFrameIndex = 0u;
    // m_surfelSeeded: false on the first enabled frame -> the trace's update divisor is 1 (ALL surfels traced to
    // bootstrap in one frame), set true after the first trace.
    bool m_surfelSeeded = false;
    // m_surfelResourcesNeedClear: set when the buffers are (re)created in ensureSurfelResources (no command list) and
    // retained until the packet that records their clear succeeds. On Graphics fallback this is shadow preparation;
    // on a dedicated lane it is the AsyncCompute surfel packet. A rejected packet leaves NeedClear set for retry.
    bool m_surfelResourcesNeedClear = false;
    bool m_surfelResourcesClearPending = false;
};


class RendererRayTracingState final : NoCopy, public RtSceneBvhState, public RtShadowState, public RtSoftShadowState, public RtCausticState, public RtSurfelGiState{
    friend class RendererSystem;
    friend class RendererShaderSystem;
    friend class RendererMeshSystem;
    friend class RendererMaterialSystem;
    friend class RendererCsgSystem;
    friend class RendererDeferredSystem;
    friend class RendererAvboitSystem;
    friend class RendererRayTracingSystem;

public:
    // Forward the renderer's global arena to RtShadowState so its per-frame SW distinct-mesh table Vectors
    // bind their allocator at construction (the other state bases default-construct).
    explicit RendererRayTracingState(Core::Alloc::GlobalArena& arena)
        : RtShadowState(arena)
    {}


public:
    void invalidateResources();
};


NWB_IMPL_END


