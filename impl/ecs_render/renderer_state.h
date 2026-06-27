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

class RendererRayTracingState final : NoCopy{
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


private:
    Core::RayTracingAccelStructHandle m_tlas;
    usize m_tlasMaxInstances = 0u;
    u64 m_tlasDeviceAddress = 0u;
    u32 m_tlasInstanceCount = 0u; // live TLAS instance count (set by buildSceneTlas); the HW caustic raygen's non-zero guard
    Core::BindingLayoutHandle m_shadowBindingLayout;
    Core::RayTracingPipelineHandle m_shadowPipeline;
    Core::RayTracingShaderTableHandle m_shadowShaderTable;
    Core::BindingSetHandle m_shadowBindingSet;
    const Core::RayTracingAccelStruct* m_shadowBindingSetTlas = nullptr;
    const Core::Buffer* m_shadowBindingSetInstanceMaterial = nullptr;
    const Core::Buffer* m_shadowBindingSetMaterialTyped = nullptr;
    const Core::Buffer* m_shadowBindingSetMeshInstances = nullptr;
    u32 m_shadowBindingSetMeshCount = 0u;
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
    // bind these (parallel: slot k = mesh k's index/attribute buffers, indexed by material.meshSlot). The HW BLAS
    // owns the positions, so only the index + attribute buffers are tracked here. Sized by the shared shader cap
    // so the C++ arrays and the shader's `[NWB_SHADOW_RT_MAX_MESHES]` stay one definition.
    Core::Buffer* m_shadowMeshIndexBuffers[NWB_SHADOW_RT_MAX_MESHES] = {};
    Core::Buffer* m_shadowMeshAttributeBuffers[NWB_SHADOW_RT_MAX_MESHES] = {};
    u32 m_shadowMeshCount = 0u;
    bool m_shadowMeshCapReported = false;
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
    // Caustic emission targets (P1): per-frame world-space AABBs of every refractive instance, the domain the
    // future caustic photon producer aims at. A single global list (all caustic lights share it). Resident
    // structured SRV ({ float4 aabbMin; float4 aabbMax; }), CPU-written each frame, grows by doubling, never
    // shrinks; the count gates caustic-light assignment (zero refractive instances -> zero caustic lights). No
    // consumer reads this yet -- a later unit's producer will. m_causticTargetBoundsMin/Max hold the combined
    // extent over all targets (for the P1 gate log); m_causticEmissionGateLogged rate-limits that log.
    Core::BufferHandle m_causticEmissionTargetBuffer;
    usize m_causticEmissionTargetCapacity = 0u;
    u32 m_causticRefractiveInstanceCount = 0u;
    // Caustic-light count assigned this frame by ResolveCausticLights (cached in updateSceneShadingBuffer). Gates
    // the software caustic producer dispatch: the producer runs only when this AND m_causticRefractiveInstanceCount
    // are both > 0 (else the black-cleared irradiance buffer is the additive no-op).
    u32 m_causticLightCount = 0u;
    Float4 m_causticTargetBoundsMin = Float4(0.f, 0.f, 0.f, 0.f);
    Float4 m_causticTargetBoundsMax = Float4(0.f, 0.f, 0.f, 0.f);
    // The caustic resolve is a purely SPATIAL a-trous wavelet denoise -- no temporal history, no motion vectors, no
    // motion-reject reseed (a moving caustic is ghost-free by construction) and the producer emission is deterministic +
    // frame-independent, so NO per-frame caustic state is needed.
    bool m_causticEmissionGateLogged = false;
    Core::BindingLayoutHandle m_swShadowBindingLayout; // software (compute) shadow traversal pass
    Core::ShaderHandle m_swShadowShader;
    Core::ComputePipelineHandle m_swShadowPipeline;
    Core::BindingSetHandle m_swShadowBindingSet;
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
    bool m_capabilityLogged = false;
    bool m_shadowPipelineFailed = false;
    bool m_bvhSortPipelineFailed = false;
    bool m_bvhSortSelfTestDone = false;
    bool m_bvhBuildPipelineFailed = false;
    bool m_bvhBuildSelfTestDone = false;
    bool m_sceneBvhSelfTestDone = false;
    bool m_swShadowPipelineFailed = false;
    bool m_swShadowDispatchLogged = false;
    bool m_swShadowMeshCapReported = false;
    bool m_swCausticPipelineFailed = false;
    bool m_causticResolvePipelineFailed = false;
    bool m_swCausticDispatchLogged = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

