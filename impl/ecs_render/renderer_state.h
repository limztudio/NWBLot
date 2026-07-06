// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "renderer_types.h"

#include <core/ecs/entity_id.h>

#include <impl/assets/graphics/mesh/runtime_constants.h>
#include <impl/assets/graphics/scene/binding_slots.h>
#include <impl/assets/graphics/shadow/binding_slots.h>
#include <impl/assets/graphics/shadow/sw_binding_slots.h>
#include <impl/assets/graphics/caustic/sw_binding_slots.h>
#include <impl/assets/graphics/caustic/resolve_binding_slots.h>
#include <impl/assets/graphics/gi/surfel/surfel_binding_slots.h>

#include <global/generic.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererSystem;
class RendererShaderSystem;
class RendererMeshSystem;
class RendererMaterialSystem;
class RendererCsgSystem;
class RendererDeferredSystem;
class RendererAvboitSystem;
class RendererRayTracingSystem;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CsgFrameStateCacheSignature{
    u64 contentHash = 0u;

    friend bool operator==(const CsgFrameStateCacheSignature& lhs, const CsgFrameStateCacheSignature& rhs){
        return lhs.contentHash == rhs.contentHash;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    Core::BindingLayoutHandle m_meshBindingLayout;
    Core::BindingLayoutHandle m_computeBindingLayout;
    Core::BindingLayoutHandle m_emulationViewBindingLayout;
    Core::BufferHandle m_instanceBuffer;
    Core::BufferHandle m_materialTypedBuffer;
    Core::BufferHandle m_meshViewBuffer;
    Core::BindingSetHandle m_emulationViewBindingSet;
    Core::ShaderHandle m_emulationVertexShader;
    Core::InputLayoutHandle m_emulationInputLayout;
    u8 m_meshViewGpuData[sizeof(f32) * NWB_MESH_VIEW_FLOAT_COUNT] = {};
    usize m_instanceBufferCapacity = 0;
    usize m_materialTypedBufferCapacity = 0;
    bool m_meshViewGpuDataValid = false;
};

class RendererCsgState final : NoCopy{
    friend class RendererSystem;
    friend class RendererShaderSystem;
    friend class RendererMeshSystem;
    friend class RendererMaterialSystem;
    friend class RendererCsgSystem;
    friend class RendererDeferredSystem;
    friend class RendererAvboitSystem;

public:
    explicit RendererCsgState(Core::Alloc::GlobalArena& arena);


public:
    void invalidateResources();


private:
    Core::BindingLayoutHandle m_clipBindingLayout;
    Core::BindingSetHandle m_clipBindingSet;
    Core::BindingLayoutHandle m_intervalPeelBindingLayout;
    Core::BindingSetHandle m_intervalPeelBindingSet;
    Core::BindingLayoutHandle m_receiverSpanBuildBindingLayout;
    Core::BindingSetHandle m_receiverSpanBuildBindingSet;
    Core::BindingLayoutHandle m_intervalCombineBindingLayout;
    Core::BindingSetHandle m_intervalCombineBindingSet;
    Core::BindingLayoutHandle m_receiverSurfaceBindingLayout;
    Core::BindingSetHandle m_receiverSurfaceBindingSet;
    Core::BindingLayoutHandle m_intervalSampleBindingLayout;
    Core::BindingSetHandle m_intervalSampleBindingSet;
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
    Core::BufferHandle m_intervalSampleStateBuffer;
    CsgFrameStateCacheSignature m_frameStateCacheSignature;
    CsgFrameState m_frameStateCache;
    usize m_receiverRangeBufferCapacity = 0u;
    usize m_cutterBufferCapacity = 0u;
    bool m_frameStateCacheValid = false;
};

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
    Core::ShaderHandle m_lightingPixelShader;
    Core::GraphicsPipelineHandle m_lightingPipeline;
    Core::BindingLayoutHandle m_compositeBindingLayout;
    Core::SamplerHandle m_sampler;
    Core::ShaderHandle m_compositePixelShader;
    Core::GraphicsPipelineHandle m_compositePipeline;
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
    Core::BindingLayoutHandle m_emptyBindingLayout;
    Core::BindingLayoutHandle m_occupancyBindingLayout;
    Core::BindingLayoutHandle m_depthWarpBindingLayout;
    Core::BindingLayoutHandle m_extinctionBindingLayout;
    Core::BindingLayoutHandle m_integrateBindingLayout;
    Core::BindingLayoutHandle m_accumulateBindingLayout;
    Core::SamplerHandle m_linearSampler;
    Core::ShaderHandle m_occupancyPixelShader;
    Core::ShaderHandle m_depthWarpComputeShader;
    Core::ShaderHandle m_extinctionPixelShader;
    Core::ShaderHandle m_integrateComputeShader;
    Core::ShaderHandle m_accumulatePixelShader;
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
    usize m_tlasMaxInstances = 0u;
    u64 m_tlasDeviceAddress = 0u;
    u32 m_tlasInstanceCount = 0u; // live TLAS instance count (set by buildSceneTlas); the HW caustic raygen's non-zero guard
    Core::BindingLayoutHandle m_bvhSortBindingLayout;
    Core::ShaderHandle m_bvhSortShader;
    Core::ComputePipelineHandle m_bvhSortPipeline;
    Core::BufferHandle m_bvhSortKeysBuffer;
    Core::BufferHandle m_bvhSortPayloadBuffer;
    Core::BindingSetHandle m_bvhSortBindingSet;
    const Core::Buffer* m_bvhSortBindingSetKeys = nullptr;
    usize m_bvhSortCapacity = 0u;
    Core::BindingLayoutHandle m_bvhBuildBindingLayout;
    Core::ShaderHandle m_bvhMortonShader;
    Core::ComputePipelineHandle m_bvhMortonPipeline;
    Core::ShaderHandle m_bvhTopologyShader;
    Core::ComputePipelineHandle m_bvhTopologyPipeline;
    Core::ShaderHandle m_bvhFitShader;
    Core::ComputePipelineHandle m_bvhFitPipeline;
    Core::BufferHandle m_bvhVisitCounterBuffer;
    usize m_bvhBuildCapacity = 0u;
    Core::BufferHandle m_sceneBvhNodeBuffer;  // CPU-built scene/instance LBVH (TLAS-analog), uploaded each frame
    Core::BufferHandle m_sceneInstanceBuffer; // per-instance world->object transform + per-mesh refs
    usize m_sceneBvhNodeCapacity = 0u;
    usize m_sceneInstanceCapacity = 0u;
    u32 m_sceneBvhInstanceCount = 0u;
    // HYBRID shadow split (RT hardware): the HW RayQuery pass casts the binary OPAQUE shadow; when the scene holds a
    // transparent occluder, the software traversal additionally casts the colored TRANSPARENT shadow and multiplies it
    // onto the opaque mask. m_sceneHasTransparentOccluder (set by buildSceneTlas) gates building the software BVH on RT
    // hardware; m_hybridTransparentShadowReady (set by the prepare) tells the render to run the SW multiply pass.
    bool m_sceneHasTransparentOccluder = false;
    bool m_hybridTransparentShadowReady = false;
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
    bool m_prevWorldToClipValid = false;
    // The five transparent resolve binding-set variants, built over transparentSoftHalf / transparentHistA/B / transparent
    // MomentsA/B as SOFT_HALF/INPUT + the SAME half-res soft-A/soft-B as the ping-pong OUTPUT scratch (the a-trous ping-pong
    // is signal-agnostic -- it just needs two half-res arrays) + the SAME full-res shadowVisibility as VISIBILITY (the fold
    // target). Selected exactly like the opaque set (output-A/output-B/upsample + two temporal-hist variants).
    Core::BindingSetHandle m_transparentResolveBindingSetOutputHalfA;
    Core::BindingSetHandle m_transparentResolveBindingSetOutputHalfB;
    Core::BindingSetHandle m_transparentResolveBindingSetUpsample;
    Core::BindingSetHandle m_transparentResolveBindingSetTemporalHistA;
    Core::BindingSetHandle m_transparentResolveBindingSetTemporalHistB;
    const Core::Texture* m_transparentResolveBindingSetSoftHalf = nullptr;   // transparentSoftHalf (the raw colored trace)
    const Core::Texture* m_transparentResolveBindingSetScratchA = nullptr;   // soft-A ping-pong scratch (shared with opaque)
    const Core::Texture* m_transparentResolveBindingSetScratchB = nullptr;   // soft-B ping-pong scratch (shared with opaque)
    const Core::Texture* m_transparentResolveBindingSetGeometry = nullptr;
    const Core::Texture* m_transparentResolveBindingSetDepth = nullptr;
    const Core::Texture* m_transparentResolveBindingSetVisibility = nullptr;
    const Core::Texture* m_transparentResolveBindingSetHistA = nullptr;
    const Core::Texture* m_transparentResolveBindingSetHistB = nullptr;
    const Core::Texture* m_transparentResolveBindingSetMomentsA = nullptr;
    const Core::Texture* m_transparentResolveBindingSetMomentsB = nullptr;
    const Core::Texture* m_transparentResolveBindingSetWorldPos = nullptr;
    const Core::Texture* m_transparentResolveBindingSetNormal = nullptr;
    // The two front/back TRANSPARENT reproject-merge binding sets (SAME m_shadowReprojectMergePipeline/Layout drive both;
    // the merge shader is fully RGB-safe and reused verbatim). Built over transparentSoftHalf (SOFT_TRACE) + transparentHist
    // A/B (history) + transparentMomentsA/B (moments) + the SHARED shadowSoftGeometry/Prev + full-res world-position.
    Core::BindingSetHandle m_transparentReprojectMergeBindingSetAtoB; // histIn/momIn = A -> histOut/momOut = B
    Core::BindingSetHandle m_transparentReprojectMergeBindingSetBtoA; // histIn/momIn = B -> histOut/momOut = A
    const Core::Texture* m_transparentReprojectMergeSoftTrace = nullptr;
    const Core::Texture* m_transparentReprojectMergeHistA = nullptr;
    const Core::Texture* m_transparentReprojectMergeHistB = nullptr;
    const Core::Texture* m_transparentReprojectMergeMomentsA = nullptr;
    const Core::Texture* m_transparentReprojectMergeMomentsB = nullptr;
    const Core::Texture* m_transparentReprojectMergeGeometryCurr = nullptr;
    const Core::Texture* m_transparentReprojectMergeGeometryPrev = nullptr;
    const Core::Texture* m_transparentReprojectMergeWorldPosition = nullptr;
    // Software caustic photon producer (P3) — the no-hardware-ray-tracing fallback. It reuses the same software
    // scene/instance + per-mesh BVH buffers the SW shadow trace builds (NWB_CAUSTIC_SW_MAX_MESHES ==
    // NWB_SW_SHADOW_MAX_MESHES, so the m_swShadowMesh* arrays serve both), and adds the caustic-specific inputs
    // (the P1 emission-target buffer, the camera view buffer, the G-buffer depth) + output (the R32_UINT
    // accumulators). The binding set is rebuilt when any cached input changes, mirroring the SW shadow set.
    Core::BindingLayoutHandle m_swCausticBindingLayout;
    Core::ShaderHandle m_swCausticShader;
    Core::ComputePipelineHandle m_swCausticPipeline;
    Core::BindingSetHandle m_swCausticBindingSet;
    const Core::Buffer* m_swCausticBindingSetSceneNodes = nullptr;
    const Core::Buffer* m_swCausticBindingSetInstances = nullptr;
    const Core::Buffer* m_swCausticBindingSetInstanceMaterial = nullptr;
    const Core::Buffer* m_swCausticBindingSetMaterialTyped = nullptr;
    const Core::Buffer* m_swCausticBindingSetMeshInstances = nullptr;
    const Core::Buffer* m_swCausticBindingSetEmissionTargets = nullptr;
    const Core::Buffer* m_swCausticBindingSetView = nullptr;
    const Core::Texture* m_swCausticBindingSetDepth = nullptr;
    const Core::Texture* m_swCausticBindingSetWorldPosition = nullptr;
    const Core::Texture* m_swCausticBindingSetAccumulator = nullptr;
    u32 m_swCausticBindingSetMeshCount = 0u;
    // Hardware ray-traced caustic photon producer (P4) -- the byte-parallel sibling of the SW producer. Mirrors the
    // shadow RT pipeline and REUSES m_tlas + the shadow instance-material / material-context / per-mesh
    // index+attribute buffers verbatim (the refraction bends on the interpolated SHADING normal from the attribute
    // buffer, so no per-mesh position array is needed). Feeds the SAME R32_UINT accumulator + the SAME resolve the SW
    // path uses. The binding set is rebuilt when any cached input changes, mirroring the shadow set.
    Core::BindingLayoutHandle m_hwCausticBindingLayout;
    Core::RayTracingPipelineHandle m_hwCausticPipeline;
    Core::RayTracingShaderTableHandle m_hwCausticShaderTable;
    Core::BindingSetHandle m_hwCausticBindingSet;
    const Core::RayTracingAccelStruct* m_hwCausticBindingSetTlas = nullptr;
    const Core::Buffer* m_hwCausticBindingSetInstanceMaterial = nullptr;
    const Core::Buffer* m_hwCausticBindingSetMaterialTyped = nullptr;
    const Core::Buffer* m_hwCausticBindingSetMeshInstances = nullptr;
    const Core::Buffer* m_hwCausticBindingSetEmissionTargets = nullptr;
    const Core::Buffer* m_hwCausticBindingSetView = nullptr;
    const Core::Texture* m_hwCausticBindingSetDepth = nullptr;
    const Core::Texture* m_hwCausticBindingSetWorldPosition = nullptr;
    const Core::Texture* m_hwCausticBindingSetAccumulator = nullptr;
    u32 m_hwCausticBindingSetMeshCount = 0u;
    bool m_hwCausticPipelineFailed = false;
    bool m_hwCausticDispatchLogged = false;
    bool m_capabilityLogged = false;
    bool m_bvhSortPipelineFailed = false;
    bool m_bvhSortSelfTestDone = false;
    bool m_bvhBuildPipelineFailed = false;
    bool m_bvhBuildSelfTestDone = false;
    bool m_sceneBvhSelfTestDone = false;
    bool m_swCausticPipelineFailed = false;
    bool m_swCausticDispatchLogged = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RtShadowState{
    Core::BindingLayoutHandle m_shadowBindingLayout;
    // Hardware shadow trace is an inline-RayQuery COMPUTE pass (shadow_rayquery_cs); the per-occluder optical-depth
    // accumulator lives in a compute local, which a hardware ray payload could not index safely.
    Core::ShaderHandle m_shadowShader;
    Core::ComputePipelineHandle m_shadowPipeline;
    Core::BindingSetHandle m_shadowBindingSet;
    const Core::RayTracingAccelStruct* m_shadowBindingSetTlas = nullptr;
    const Core::Buffer* m_shadowBindingSetInstanceMaterial = nullptr;
    const Core::Buffer* m_shadowBindingSetMaterialTyped = nullptr;
    const Core::Buffer* m_shadowBindingSetMeshInstances = nullptr;
    u32 m_shadowBindingSetMeshCount = 0u;
    // Active shadow slots this frame (= min(lightCount, NWB_SCENE_SHADOW_SLOT_COUNT)), set during the light upload and
    // read by the edge-adaptive shadow resolve so it only processes the slots that hold a light.
    u32 m_shadowSlotCount = 0u;
    // Per-frame instance-material table (NwbRtInstanceMaterialGpu), shared by the hardware any-hit and the
    // software traversal; built lockstep with the TLAS / scene-instance buffer so the array index matches the
    // shadow instance id. Grows by doubling, never shrinks.
    Core::BufferHandle m_shadowInstanceMaterialBuffer;
    usize m_shadowInstanceMaterialCapacity = 0u;
    // Shadow-OWNED combined material-constants context the per-hit transmittance dispatch reads (g_NwbMeshInstances
    // + g_NwbMaterialTypedWords). The draw passes' equivalents hold only ONE transparency class at trace time (the
    // opaque set is resident; the transparent occluders' blocks are uploaded after the trace), so the trace builds
    // its own combined buffers over ALL gathered occluders (both transparency classes) lockstep with the shadow
    // instances. m_shadowInstanceBuffer = InstanceGpuData per occluder (mutable byte offset in translation.w);
    // m_shadowMaterialTypedBuffer = each occluder's constant + mutable typed blocks (constant offset stored in the
    // instance-material record). Both grow by doubling, never shrink, and only the shadow trace binds them.
    Core::BufferHandle m_shadowInstanceBuffer;
    Core::BufferHandle m_shadowMaterialTypedBuffer;
    usize m_shadowInstanceCapacity = 0u;
    usize m_shadowMaterialTypedCapacity = 0u;
    // Per-frame distinct meshes referenced by the TLAS (filled by buildSceneTlas); the per-mesh descriptor arrays
    // bind these (parallel: slot k = mesh k's index/attribute/position buffers, indexed by material.meshSlot). The
    // HW BLAS owns the positions it traces, but the any-hit ALSO needs the raw positions to derive the geometric
    // face normal for the per-crossing faceSign/cosI, so the position buffer is tracked here too. Sized by the
    // shared shader cap so the C++ arrays and the shader's `[NWB_SHADOW_RT_MAX_MESHES]` stay one definition.
    Core::Buffer* m_shadowMeshIndexBuffers[NWB_SHADOW_RT_MAX_MESHES] = {};
    Core::Buffer* m_shadowMeshAttributeBuffers[NWB_SHADOW_RT_MAX_MESHES] = {};
    Core::Buffer* m_shadowMeshPositionBuffers[NWB_SHADOW_RT_MAX_MESHES] = {};
    u32 m_shadowMeshCount = 0u;
    bool m_shadowMeshCapReported = false;
    // Software (compute) shadow traversal, decomposed into one named pipeline per pass. All passes share one binding
    // layout + one binding set: each pass's kernel references only its own subset of the slot map, and the bound resources
    // at every slot are identical regardless of pass. The shared layout's push-constant range is sized to the largest pass
    // struct (SwShadowMaxPushConstants); each pass sets only its own bytes.
    Core::BindingLayoutHandle m_swShadowBindingLayout;
    Core::BindingSetHandle m_swShadowBindingSet;
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
    // Soft COLORED TRANSPARENT trace: the colored (Beer-Lambert/Fresnel) analog of the soft opaque trace, against
    // the SAME shared SW-shadow binding layout/set (it only adds the NWB_SW_SHADOW_BINDING_TRANSPARENT_SOFT_HALF UAV slot).
    Core::ShaderHandle m_swShadowTransparentSoftShader;
    Core::ComputePipelineHandle m_swShadowTransparentSoftPipeline;
    const Core::Buffer* m_swShadowBindingSetSceneNodes = nullptr;
    const Core::Buffer* m_swShadowBindingSetInstances = nullptr;
    const Core::Buffer* m_swShadowBindingSetInstanceMaterial = nullptr;
    const Core::Buffer* m_swShadowBindingSetMaterialTyped = nullptr;
    const Core::Buffer* m_swShadowBindingSetMeshInstances = nullptr;
    const Core::Texture* m_swShadowBindingSetVisibility = nullptr;
    u32 m_swShadowBindingSetMeshCount = 0u;
    // Per-frame distinct meshes referenced by the software scene BVH (filled by buildSceneSwBvh); the per-mesh
    // descriptor arrays bind these (parallel: slot k = mesh k's node/position/index buffers). Sized by the
    // shared shader cap so the C++ arrays and the shader's `[NWB_SW_SHADOW_MAX_MESHES]` stay one definition.
    Core::Buffer* m_swShadowMeshNodeBuffers[NWB_SW_SHADOW_MAX_MESHES] = {};
    Core::Buffer* m_swShadowMeshPositionBuffers[NWB_SW_SHADOW_MAX_MESHES] = {};
    Core::Buffer* m_swShadowMeshIndexBuffers[NWB_SW_SHADOW_MAX_MESHES] = {};
    Core::Buffer* m_swShadowMeshAttributeBuffers[NWB_SW_SHADOW_MAX_MESHES] = {}; // U2 per-vertex normal/uv0 for the per-hit transmittance dispatch
    u32 m_swShadowMeshCount = 0u;
    // Adaptive transparent shadow (coarse-trace + edge-refine) config, fixed at shipping defaults (adaptive ON,
    // edge threshold 0.1, stats OFF). These flags drive the transparent economizer path used by the HW-hybrid backend
    // (renderGpuBvhShadowVisibility multiplyOntoOpaque=true). The full-res opaque prepass remains the SW-path
    // baseline/fallback.
    bool m_swShadowAdaptiveEnabled = true;
    bool m_swShadowEdgeStatsEnabled = false;
    // Compacted-indirect resolve (default ON): when set and adaptive is on, classify+append -> build-args ->
    // DispatchIndirect trace launches only edge rays as coherent waves. Disabled falls back to the coarse/adaptive path.
    bool m_swShadowCompactEnabled = true;
    f32 m_swShadowEdgeThreshold = 0.1f;
    // Edge-fraction instrumentation: a 2-uint UAV counter the resolve tallies into ([0] traced rays, [1] total rays),
    // snapshotted into a CPU-readable buffer on a slow cadence and logged a safe number of frames later (so the copy is
    // GPU-complete without a stall). The tick counts transparent-shadow dispatches; pending guards the in-flight copy.
    Core::BufferHandle m_swShadowEdgeStatsBuffer;
    Core::BufferHandle m_swShadowEdgeStatsReadback;
    u32 m_swShadowEdgeStatsTick = 0u;
    bool m_swShadowEdgeStatsPending = false;
    // Soft opaque shadow RESOLVE: the a-trous wavelet denoise pipeline (cloned from the caustic resolve)
    // dispatched per pass with two ping-pong binding sets that swap the (output UAV, input-color SRV) pair between the
    // half-res soft-A / soft-B buffers; the upsample set writes the full-res visibility. All sets share the geometry
    // cache + depth SRVs; rebuilt when any tracked target changes (on resize). Mirrors the caustic resolve state.
    Core::BindingLayoutHandle m_shadowResolveBindingLayout;
    Core::ShaderHandle m_shadowResolveShader;
    Core::ComputePipelineHandle m_shadowResolvePipeline;
    bool m_shadowResolvePipelineFailed = false;
    Core::BindingSetHandle m_shadowResolveBindingSetOutputHalfA; // output=soft-A, input=soft-B (prepare + odd wavelet passes)
    Core::BindingSetHandle m_shadowResolveBindingSetOutputHalfB; // output=soft-B, input=soft-A (even wavelet passes)
    Core::BindingSetHandle m_shadowResolveBindingSetUpsample;    // output=full-res visibility, input=soft-B (final upsample)
    const Core::Texture* m_shadowResolveBindingSetSoftHalfA = nullptr;
    const Core::Texture* m_shadowResolveBindingSetSoftHalfB = nullptr;
    const Core::Texture* m_shadowResolveBindingSetGeometry = nullptr;
    const Core::Texture* m_shadowResolveBindingSetDepth = nullptr;
    const Core::Texture* m_shadowResolveBindingSetVisibility = nullptr;
    // Tracked resolve targets: the SVGF moments buffers (bound as the MOMENTS SRV -- paired per temporal set, dummy on the
    // rest) + the full-res world-position/normal G-buffers the guided upsample reads. Any change rebuilds all five sets.
    const Core::Texture* m_shadowResolveBindingSetMomentsA = nullptr;
    const Core::Texture* m_shadowResolveBindingSetMomentsB = nullptr;
    const Core::Texture* m_shadowResolveBindingSetWorldPos = nullptr;
    const Core::Texture* m_shadowResolveBindingSetNormal = nullptr;
    // Two EXTRA soft-shadow resolve binding-set variants whose SOFT_HALF (the PREPARE stage's input) is a temporal history
    // buffer (shadowHistA / shadowHistB) instead of the raw shadowSoftHalfA. The dispatch selects the one matching the merge's
    // history-out buffer this frame (B when frontIsA, A otherwise) so the a-trous denoises the ACCUMULATED visibility. Neither
    // binds its SOFT_HALF as the wavelet OUTPUT (the ping-pong output is soft-A/soft-B, distinct from the hist buffers), so no
    // SRV+UAV alias -- the same constraint the base three resolve sets document. Built alongside them in ensureSoftShadowResolveBindingSet.
    Core::BindingSetHandle m_shadowResolveBindingSetTemporalHistA; // PREPARE reads shadowHistA as SOFT_HALF
    Core::BindingSetHandle m_shadowResolveBindingSetTemporalHistB; // PREPARE reads shadowHistB as SOFT_HALF
    const Core::Texture* m_shadowResolveBindingSetTemporalHistATex = nullptr;
    const Core::Texture* m_shadowResolveBindingSetTemporalHistBTex = nullptr;
    // ---- Parallel soft COLORED TRANSPARENT denoise state (mirrors the opaque m_shadowResolve* / merge blocks) ----
    // The RGB a-trous resolve pipeline: the SAME shadow_resolve source cooked with NWB_SHADOW_RESOLVE_CHANNELS=3 (via the
    // shadow_resolve_rgb_cs wrapper). It shares the resolve BINDING LAYOUT (identical bindings; only the wavelet channel
    // count + a runtime fold flag differ), so only a distinct shader + pipeline handle are needed, not a new layout.
    Core::ShaderHandle m_shadowResolveRgbShader;
    Core::ComputePipelineHandle m_shadowResolveRgbPipeline;
    bool m_shadowResolveRgbPipelineFailed = false;
    u32 m_swShadowEdgeStatsPendingTick = 0u;
    // Stage-3 compaction resources: the per-frame append counter (2 u32: [0] append count, [1] clamped trace count), the
    // compacted edge-record list (recreated on resize, sized in records), and the persistent indirect dispatch-args
    // buffer (created UAV + isDrawIndirectArgs). The edge list is recreated alongside the visibility target, so the
    // binding-set rebuild already triggers on the tracked visibility pointer.
    Core::BufferHandle m_swShadowEdgeCounterBuffer;
    Core::BufferHandle m_swShadowEdgeListBuffer;
    u32 m_swShadowEdgeListCapacity = 0u; // capacity in RECORDS
    Core::BufferHandle m_swShadowIndirectArgsBuffer;
    bool m_shadowPipelineFailed = false;
    bool m_swShadowPipelineFailed = false;
    bool m_swShadowDispatchLogged = false;
    bool m_swShadowMeshCapReported = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RtSoftShadowState{
    // Hardware (RayQuery) SOFT OPAQUE half-res trace pipeline + its binding set (shadow_rayquery_soft_cs). It REUSES the
    // shared m_shadowBindingLayout (identical trace context; only the bound visibility-output texture differs -- the soft
    // set binds shadowSoftHalfA as the UAV output instead of the full-res shadowVisibility), so the HW opaque shadow can
    // run the SAME half-res -> temporal -> a-trous -> upsample soft-shadow denoise chain as the SW path. The binding set
    // rebuilds on the SAME tracked-pointer keys as m_shadowBindingSet (TLAS / instance-material / material-context /
    // mesh-count), so a TLAS/G-buffer/target change rebuilds it.
    Core::ShaderHandle m_shadowSoftShader;
    Core::ComputePipelineHandle m_shadowSoftPipeline;
    bool m_shadowSoftPipelineFailed = false;
    Core::BindingSetHandle m_shadowSoftBindingSet;
    const Core::RayTracingAccelStruct* m_shadowSoftBindingSetTlas = nullptr;
    const Core::Buffer* m_shadowSoftBindingSetInstanceMaterial = nullptr;
    const Core::Buffer* m_shadowSoftBindingSetMaterialTyped = nullptr;
    const Core::Buffer* m_shadowSoftBindingSetMeshInstances = nullptr;
    u32 m_shadowSoftBindingSetMeshCount = 0u;
    // Soft opaque shadow (soft-ray-traced-shadow feature): a per-frame counter seeding the per-pixel low-discrepancy
    // cone-jitter sample. Incremented once per frame by whichever primary shadow producer runs (the HW RayQuery opaque
    // trace on the HW path, the no-RT software traversal otherwise -- mutually exclusive per frame), so each pixel's
    // single jittered ray walks the source across frames. No temporal reuse this stage, so this only decorrelates the
    // per-frame sample (a later stage feeds it into a temporal accumulator).
    u32 m_softShadowFrameIndex = 0u;
    // The set of shadow slots this frame that hold a shadow slot (params.z >= 0), regardless of light type -- the slots
    // the soft opaque path traces + denoises + upsamples. A directional light softens by its constant angular radius
    // (params2.x), a point/spot light by the distance-dependent cone its source sphere (params2.y) subtends; both are
    // handled inside the trace, so every slot light is soft. A bitmask (slot k -> bit k) over the
    // NWB_SCENE_SHADOW_SLOT_COUNT pool, filled by updateSceneShadingBuffer from the resolved light data. The resolve
    // dispatches once per set bit (its lightSlotStart/lightSlotCount address a single slot), so the scattered slot
    // assignment (a light can land on any slot index) is handled without a contiguous range assumption.
    u32 m_softShadowSlotMask = 0u;
    // Shadow geometry downsample pre-pass (its own pipeline): fills the half-res packed geometry cache (octahedral
    // normal + camera distance + validity) the resolve passes read for the edge-stop, so they tap one half-res texel.
    Core::BindingLayoutHandle m_shadowGeometryDownsampleBindingLayout;
    Core::ShaderHandle m_shadowGeometryDownsampleShader;
    Core::ComputePipelineHandle m_shadowGeometryDownsamplePipeline;
    bool m_shadowGeometryDownsamplePipelineFailed = false;
    Core::BindingSetHandle m_shadowGeometryDownsampleBindingSet;
    const Core::Texture* m_shadowGeometryDownsampleWorldPosition = nullptr;
    const Core::Texture* m_shadowGeometryDownsampleNormal = nullptr;
    const Core::Texture* m_shadowGeometryDownsampleDepth = nullptr;
    const Core::Texture* m_shadowGeometryDownsampleGeometry = nullptr;
    // Set by prepareShadowVisibilityResources (no-RT path) when the soft opaque geometry-downsample + resolve
    // pipelines AND binding sets are all ready this frame; gates the render's soft opaque trace + resolve dispatch.
    // A failure here is non-fatal to shadows -- the slot lights simply keep their hard opaque mask this frame.
    bool m_softShadowReady = false;
    // Ping-pong selector: 1 = shadowHistA/MomentsA hold the INCOMING history this frame (merge writes B), 0 = the reverse.
    // Flipped by the frame-end swap. Also selects which merge binding set + which temporal resolve SOFT_HALF source is used.
    u32 m_softShadowHistoryFrontIsA = 1u;
    // Cleared whenever the temporal targets are (re)created (createShadowVisibilityTarget); the FIRST merge dispatch sets it
    // true. Until then the merge treats every pixel as n=0 (pure current sample) -- the clean first-frame / post-resize path.
    bool m_softShadowTemporalSeeded = false;
    // Set by prepareShadowVisibilityResources when m_softShadowReady AND the merge pipeline + both
    // merge binding sets + both temporal resolve SOFT_HALF variants built this frame; gates the per-slot merge insertion +
    // the frame-end stash/swap. Non-fatal: a failure leaves it false and the soft path runs its non-temporal fallback.
    bool m_softShadowTemporalReady = false;
    // The reproject-merge pipeline (mirrors the m_shadowResolve* block). Two binding sets, front/back, so the history-in
    // SRV and history-out UAV never alias the same texture: AtoB (histIn=A,momIn=A -> histOut=B,momOut=B) and BtoA (mirror).
    // Both share SOFT_TRACE=shadowSoftHalfA, GEOMETRY_CURR=shadowSoftGeometry, GEOMETRY_PREV=shadowSoftGeometryPrev,
    // WORLDPOS=targets.worldPosition. Rebuilt (tracked-pointer compare) when any bound target changes (resize / frame-end swap).
    Core::BindingLayoutHandle m_shadowReprojectMergeBindingLayout;
    Core::ShaderHandle m_shadowReprojectMergeShader;
    Core::ComputePipelineHandle m_shadowReprojectMergePipeline;
    bool m_shadowReprojectMergePipelineFailed = false;
    Core::BindingSetHandle m_shadowReprojectMergeBindingSetAtoB; // histIn/momIn = A -> histOut/momOut = B
    Core::BindingSetHandle m_shadowReprojectMergeBindingSetBtoA; // histIn/momIn = B -> histOut/momOut = A
    const Core::Texture* m_shadowReprojectMergeHistA = nullptr;
    const Core::Texture* m_shadowReprojectMergeHistB = nullptr;
    const Core::Texture* m_shadowReprojectMergeMomentsA = nullptr;
    const Core::Texture* m_shadowReprojectMergeMomentsB = nullptr;
    const Core::Texture* m_shadowReprojectMergeSoftTrace = nullptr;
    const Core::Texture* m_shadowReprojectMergeGeometryCurr = nullptr;
    const Core::Texture* m_shadowReprojectMergeGeometryPrev = nullptr;
    const Core::Texture* m_shadowReprojectMergeWorldPosition = nullptr;
    // Gates (mirror m_softShadowReady / m_softShadowTemporalReady): set by prepareShadowVisibilityResources when the RGB
    // resolve pipeline + the transparent resolve binding sets (and, for temporal, the transparent merge pipeline is shared +
    // its two binding sets) are all ready this frame. Non-fatal: a failure leaves the soft transparent path off; the
    // transparent coarse/adaptive path then runs its colored multiply, with no double-fold because the paths are exclusive.
    bool m_softTransparentReady = false;
    bool m_softTransparentTemporalReady = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RtCausticState{
    // Caustic emission targets (P1): per-frame world-space AABBs of every refractive instance, the domain the caustic
    // photon producer aims at. A single global list is shared by all caustic lights. Resident structured SRV
    // ({ float4 aabbMin; float4 aabbMax; }), CPU-written each frame, grows by doubling, never shrinks; the count gates
    // caustic-light assignment together with per-light opt-in (zero refractive instances or zero opted-in lights ->
    // zero caustic lights). m_causticTargetBoundsMin/Max hold the combined extent over all targets (for the P1 gate
    // log); m_causticEmissionGateLogged rate-limits that log.
    Core::BufferHandle m_causticEmissionTargetBuffer;
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
    bool m_causticEmissionGateLogged = false;
    // Caustic resolve pass (P3): an N-pass edge-avoiding a-trous wavelet denoise. The single compute pipeline is
    // dispatched per pass with two ping-pong binding sets that swap the (output UAV, input-color SRV) pair: one outputs
    // the irradiance buffer, the other the scratch buffer; the loop alternates so the final pass writes irradiance.
    // Both sets share the accumulator + G-buffer SRVs; they are rebuilt when any of those targets change (on resize).
    Core::BindingLayoutHandle m_causticResolveBindingLayout;
    Core::ShaderHandle m_causticResolveShader;
    Core::ComputePipelineHandle m_causticResolvePipeline;
    Core::BindingSetHandle m_causticResolveBindingSetOutputHalfA; // output=half-A, input=half-B (prepare + odd wavelet passes)
    Core::BindingSetHandle m_causticResolveBindingSetOutputHalfB; // output=half-B, input=half-A (even wavelet passes)
    Core::BindingSetHandle m_causticResolveBindingSetUpsample;    // output=full-res irradiance, input=half-B (final upsample)
    const Core::Texture* m_causticResolveBindingSetAccumulator = nullptr;
    const Core::Texture* m_causticResolveBindingSetWorldPosition = nullptr;
    const Core::Texture* m_causticResolveBindingSetDepth = nullptr;
    const Core::Texture* m_causticResolveBindingSetIrradiance = nullptr;
    const Core::Texture* m_causticResolveBindingSetHalfA = nullptr;
    const Core::Texture* m_causticResolveBindingSetHalfB = nullptr;
    const Core::Texture* m_causticResolveBindingSetGeometry = nullptr;
    // Geometry downsample pre-pass (its own pipeline): fills the half-res geometry cache (world + receiver validity) the
    // resolve passes read, so they tap one half-res texel instead of re-reading the full-res world/depth G-buffer per tap.
    Core::BindingLayoutHandle m_causticGeometryDownsampleBindingLayout;
    Core::ShaderHandle m_causticGeometryDownsampleShader;
    Core::ComputePipelineHandle m_causticGeometryDownsamplePipeline;
    Core::BindingSetHandle m_causticGeometryDownsampleBindingSet;
    const Core::Texture* m_causticGeometryDownsampleWorldPosition = nullptr;
    const Core::Texture* m_causticGeometryDownsampleDepth = nullptr;
    const Core::Texture* m_causticGeometryDownsampleGeometry = nullptr;
    bool m_causticGeometryDownsamplePipelineFailed = false;
    // Caustic accumulator decay pre-pass (splat-space temporal EMA): a single-resource compute pass that multiplies the
    // resident R32_UINT accumulator by m_causticTemporalDecay before the producer splats this frame's photons.
    // m_causticAccumulatorInitialized gates the first-frame (and post-resize) clear-vs-decay: the accumulator holds no
    // valid history until the producer has splatted once, so the first enabled frame clears and every later
    // frame decays. Reset to false wherever the deferred targets are (re)created so a resize re-seeds cleanly.
    Core::BindingLayoutHandle m_causticAccumulatorDecayBindingLayout;
    Core::ShaderHandle m_causticAccumulatorDecayShader;
    Core::ComputePipelineHandle m_causticAccumulatorDecayPipeline;
    Core::BindingSetHandle m_causticAccumulatorDecayBindingSet;
    const Core::Texture* m_causticAccumulatorDecayAccumulator = nullptr;
    bool m_causticAccumulatorDecayPipelineFailed = false;
    bool m_causticAccumulatorInitialized = false;
    bool m_causticResolvePipelineFailed = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RtSurfelGiState{
    // ---- Surfel GI state ----
    // Screen-spawned, world-hashed surfels integrate one-bounce diffuse GI. The persistent buffers (pool / cell-head /
    // counter / params CB) live HERE (beside the caustic block), NOT on DeferredFrameTargets, which is torn down on
    // every window resize and would silently reset surfel convergence. Lifetime = device reset only. Three compute
    // passes per frame: spawn (screen tiles -> bump-allocate a surfel where existing coverage is low) -> hash-build
    // (link live surfels into the spatial-hash cell lists) -> trace (one workgroup per surfel, 64 SW rays -> EMA
    // irradiance). The deferred-lighting gather walks the hash at each shaded point. See .helper/surfel_gi_plan.md.
    // The three passes have DISTINCT binding layouts: the cell-head is an SRV at spawn (reads the previous frame) but a
    // UAV at hash-build (writes the links), so they cannot share one layout the way the SW-shadow passes do.
    Core::BindingLayoutHandle m_surfelSpawnBindingLayout;
    Core::BindingLayoutHandle m_surfelAgeFreeBindingLayout;
    Core::BindingLayoutHandle m_surfelHashBuildBindingLayout;
    Core::BindingLayoutHandle m_surfelTraceBindingLayout;
    Core::ShaderHandle m_surfelSpawnShader;
    Core::ComputePipelineHandle m_surfelSpawnPipeline;
    // Age-free (U1 recycling): one thread per pool slot; frees surfels unseen for maxAge frames + pushes their ids onto
    // the free-list. Depends only on the persistent buffers, so it is built once (like hash-build).
    Core::ShaderHandle m_surfelAgeFreeShader;
    Core::ComputePipelineHandle m_surfelAgeFreePipeline;
    Core::ShaderHandle m_surfelHashBuildShader;
    Core::ComputePipelineHandle m_surfelHashBuildPipeline;
    Core::ShaderHandle m_surfelTraceShader;
    Core::ComputePipelineHandle m_surfelTracePipeline;
    Core::BindingSetHandle m_surfelSpawnBindingSet;
    Core::BindingSetHandle m_surfelAgeFreeBindingSet;
    Core::BindingSetHandle m_surfelHashBuildBindingSet;
    Core::BindingSetHandle m_surfelTraceBindingSet;
    // Resolve pass: a COMPUTE pass that gathers the surfel field once per pixel into the screen-space surfelIrradiance
    // texture the deferred lighting samples. Keeping the gather in compute (not the pixel shader) keeps the RW pool off
    // the pixel stage, eliminating the frames-in-flight pool race. Binds surfel constants/pool(SRV)/cell-head(SRV) + the
    // G-buffer world-position/normal + the surfelIrradiance UAV.
    Core::BindingLayoutHandle m_surfelResolveBindingLayout;
    Core::ShaderHandle m_surfelResolveShader;
    Core::ComputePipelineHandle m_surfelResolvePipeline;
    Core::BindingSetHandle m_surfelResolveBindingSet;
    // Tracked pointers for the resolve set rebuild (G-buffer world-position/normal + the surfelIrradiance output, all
    // recreated on resize).
    const Core::Texture* m_surfelResolveBindingSetWorldPosition = nullptr;
    const Core::Texture* m_surfelResolveBindingSetNormal = nullptr;
    const Core::Texture* m_surfelResolveBindingSetOutput = nullptr;
    // Tracked pointers for the spawn set rebuild (the G-buffer world-position + normal are recreated on resize).
    const Core::Texture* m_surfelSpawnBindingSetWorldPosition = nullptr;
    const Core::Texture* m_surfelSpawnBindingSetNormal = nullptr;
    // Tracked pointers for the trace set rebuild (the SW scene BVH + per-mesh arrays; mirrors the SW shadow set guard).
    const Core::Buffer* m_surfelTraceBindingSetSceneNodes = nullptr;
    const Core::Buffer* m_surfelTraceBindingSetInstances = nullptr;
    const Core::Buffer* m_surfelTraceBindingSetMaterialTyped = nullptr;
    const Core::Buffer* m_surfelTraceBindingSetMeshInstances = nullptr;
    u32 m_surfelTraceBindingSetMeshCount = 0u;
    // Persistent surfel buffers (created once, never resized with the window). The pool holds the NwbSurfel records; the
    // cell-head buffer is the spatial-hash linked-list heads (one uint per cell); the counter is 2 u32 (bump top, free
    // top). All UAV-writable; the gather binds the pool + cell-head as SRVs. One-shot init on creation: pool zeroed,
    // cell-head = 0xFFFFFFFF, counter = 0.
    Core::BufferHandle m_surfelPoolBuffer;
    Core::BufferHandle m_surfelCellHeadBuffer;
    Core::BufferHandle m_surfelCounterBuffer;
    // Free-list (U1): persistent LIFO stack of recycled surfel ids (poolCapacity uints). Pushed by age-free, popped by
    // spawn; the stack depth lives in counter[FREE_TOP]. Same barrier/state-tracking as the pool.
    Core::BufferHandle m_surfelFreeListBuffer;
    // Snapshot of the previous frame's converged field (U4 infinite bounce): the trace's bounce gather reads these (SRV)
    // instead of the live pool it is writing, so surfel->surfel feedback reads a stable frame-start field. Both pool +
    // cell-head are snapshotted (mutually consistent prev-frame walk); overwritten by copyBuffer at the top of each frame.
    Core::BufferHandle m_surfelPoolSnapshotBuffer;
    Core::BufferHandle m_surfelCellHeadSnapshotBuffer;
    // CPU-readable copy of the counter (BUMP_TOP, FREE_TOP) for the periodic live-count diagnostic (U1). The copy is
    // snapshotted on a log-interval frame and mapped a few frames later (async), mirroring the SW-shadow edge-stats path.
    Core::BufferHandle m_surfelCounterReadback;
    bool m_surfelCountReadbackPending = false;
    u32 m_surfelCountReadbackFrame = 0u;
    // Params CB (NwbSurfelConstants: 5 x Float4). Uploaded each rendered frame.
    Core::BufferHandle m_surfelConstants;
    u32 m_surfelPoolCapacity = NWB_SURFEL_POOL_CAPACITY;
    u32 m_surfelHashCellCount = NWB_SURFEL_HASH_CELL_COUNT;
    // Per-frame counter seeding the ray rotation + age comparisons.
    u32 m_surfelFrameIndex = 0u;
    // m_surfelSeeded: false on the first enabled frame -> the trace's update divisor is 1 (ALL surfels traced to
    // bootstrap in one frame), set true after the first trace. Mirrors the caustic/GI seed precedent.
    bool m_surfelSeeded = false;
    // m_surfelResourcesNeedClear: set when the buffers are (re)created in ensureSurfelResources (no command list) and
    // consumed once in prepareSurfelResources (which has one) to clear the pool/cell-head/counter before the first pass.
    bool m_surfelResourcesNeedClear = false;
    // Feature gate + per-pipeline-failed flags (mirrors the caustic / shadow precedent).
    bool m_surfelEnabled = false;
    bool m_surfelSpawnPipelineFailed = false;
    bool m_surfelAgeFreePipelineFailed = false;
    bool m_surfelHashBuildPipelineFailed = false;
    bool m_surfelTracePipelineFailed = false;
    bool m_surfelResolvePipelineFailed = false;
    bool m_surfelDispatchLogged = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    RendererRayTracingState() = default;


public:
    void invalidateResources();


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

