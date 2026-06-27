// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "raytracing_system.h"

#include "renderer_private.h"
#include "arena_names.h"

#include <impl/assets/graphics/shadow/binding_slots.h>
#include <impl/assets/graphics/shadow/sw_binding_slots.h>
#include <impl/assets/graphics/shadow/names.h>
#include <impl/assets/graphics/caustic/sw_binding_slots.h>
#include <impl/assets/graphics/caustic/hw_binding_slots.h>
#include <impl/assets/graphics/caustic/resolve_binding_slots.h>
#include <impl/assets/graphics/caustic/names.h>
#include <impl/assets/graphics/bvh/binding_slots.h>
#include <impl/assets/graphics/bvh/names.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Number of in-place refits performed on a skinned BLAS before forcing a full rebuild. A refit
// (in-place UPDATE) keeps the build-pose tree topology and only resizes the bounding boxes, so
// traversal quality drifts as the pose moves away from the last full build; the periodic rebuild
// restores it. This is the quality/cost knob for skinned ray-traced geometry.
inline constexpr u32 s_BlasMaxRefitsBeforeRebuild = 8u;

// Initial scene-TLAS instance capacity; grows by doubling when the live instance count exceeds it.
inline constexpr usize s_TlasInitialInstanceCapacity = 128u;

// Initial element capacity of the BVH Morton-sort scratch buffers (keys + payload); grows by doubling.
inline constexpr usize s_BvhSortInitialCapacity = 1024u;

// CPU mirror of the shader NwbBvhBitonicSortPushConstants block (one bitonic compare-exchange step).
struct BvhSortPushConstants{
    u32 elementCount = 0u;
    u32 compareDistance = 0u;
    u32 sequenceSize = 0u;
    u32 pad0 = 0u;
};
static_assert(sizeof(BvhSortPushConstants) == sizeof(u32) * 4u, "BvhSortPushConstants must match the shader NwbBvhBitonicSortPushConstants layout");

// Initial per-mesh triangle capacity for the shared LBVH visit-counter scratch; grows by doubling.
inline constexpr usize s_BvhBuildInitialCapacity = 1024u;

// Largest mesh (in triangles) the software BVH supports. The shared sort/counter scratch is sized once to
// this cap so that per-mesh build binding sets — which reference those shared buffers — never reference a
// reallocated buffer when meshes of different sizes are built within a frame. 256K triangles is far above
// any realistic shadow caster; oversized meshes are skipped (their shadows fall back to "all lit").
inline constexpr u32 s_BvhMaxPrimitivesPerMesh = 262144u;

// CPU mirror of the shader NwbBvhNode (std430, 32 bytes): AABB + child node indices, or a leaf-flagged
// primitive index in leftChild with rightChild = primitive count (see bvh_common.slangi).
struct NwbBvhNodeGpu{
    Float3UInt aabbMinLeftChild;
    Float3UInt aabbMaxRightChild;
};
static_assert(sizeof(NwbBvhNodeGpu) == 32u, "NwbBvhNodeGpu must match the shader NwbBvhNode std430 layout");

struct SceneBvhNodeBuildData{
    SIMDVector aabbMin;
    SIMDVector aabbMax;
    u32 leftChild = 0u;
    u32 rightChild = 0u;
};

// CPU mirror of the shader NwbBvhBuildPushConstants (shared by the morton / topology / fit kernels).
struct BvhBuildPushConstants{
    u32 primitiveCount = 0u;
    u32 internalCount = 0u;
    u32 refitMode = 0u;
    u32 pad0 = 0u;
    Float4 aabbMin = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    Float4 aabbMax = Float4(0.0f, 0.0f, 0.0f, 0.0f);
};
static_assert(sizeof(BvhBuildPushConstants) == sizeof(u32) * 4u + sizeof(Float4) * 2u, "BvhBuildPushConstants must match the shader NwbBvhBuildPushConstants layout");

// BVH node child-link encoding, mirroring the bvh_common.slangi shader constants (NWB_BVH_LEAF_FLAG /
// NWB_BVH_INVALID) for CPU-side BVH build + validation + clears: a leaf sets `LeafFlag` on its leftChild and packs
// the primitive index in the low bits; `Invalid` is the empty parent/child sentinel. (`Invalid` is the all-bits
// sentinel rather than a maskable bit, grouped here as part of the one node-index encoding.)
namespace BvhNodeIndex{
    enum Mask : u32{
        LeafFlag = 0x80000000u,
        Invalid = 0xFFFFFFFFu,
    };
}

// Initial scene/instance BVH instance capacity; grows by doubling like the hardware TLAS.
inline constexpr usize s_SceneBvhInitialInstanceCapacity = 64u;

// CPU mirror of the per-instance record the software shadow traversal (U5) consumes. Holds the affine
// world->object transform (so a world-space ray can be pushed into each instance's object space) plus the
// per-mesh BVH / geometry references resolved when traversal is wired. 64 bytes / std430-friendly.
struct SceneSwBvhInstanceGpu{
    Float34 worldToObject{};   // affine world->object (row-major 3x4); pushes a world ray into object space
    u32 meshIndex = 0u;        // slot into the parallel per-mesh node/position/index descriptor arrays
    u32 primitiveCount = 0u;   // triangle count of the referenced mesh (sanity bound)
    u32 pad0 = 0u;
    u32 pad1 = 0u;
};
static_assert(sizeof(SceneSwBvhInstanceGpu) == sizeof(Float34) + sizeof(u32) * 4u, "SceneSwBvhInstanceGpu must stay a tight 64-byte record");

// Initial capacity (in records) of the per-frame caustic emission-target buffer; grows by doubling.
inline constexpr usize s_CausticEmissionTargetInitialCapacity = 32u;

// CPU/GPU record of one caustic emission target (P1): the world-space AABB of a refractive instance. The future
// caustic photon producer aims its light-side emission domain at these boxes; for v1 a single global list (all
// caustic lights share it). A tight 32-byte std430-friendly record ({ float4 aabbMin; float4 aabbMax; }); the w
// lanes are unused padding (kept for SIMD-friendly 16-byte lanes / std430 float4 alignment).
struct NwbCausticEmissionTargetGpu{
    Float4 aabbMin = Float4(0.f, 0.f, 0.f, 0.f);
    Float4 aabbMax = Float4(0.f, 0.f, 0.f, 0.f);
};
static_assert(sizeof(NwbCausticEmissionTargetGpu) == sizeof(Float4) * 2u, "NwbCausticEmissionTargetGpu must stay a tight 32-byte std430 record");

// CPU mirror of the shader NwbRtInstanceMaterial (shadow/instance_material.slangi, 32 bytes / two float4 lanes,
// std430): the per-instance shadow-occluder transmittance-model id + flags + per-mesh attribute slot + the
// material-constants context the surface hook needs (the constant block byte offset into g_NwbMaterialTypedWords
// and the g_NwbMeshInstances index that resolves the mutable storage offset). Built per frame into one structured
// buffer indexed by the shadow instance id (hardware InstanceID() == software scene-BVH leaf index), so the
// hardware any-hit and the software traversal read the same record for the same entity. The model id dispatches
// the per-hit transmittance hook; meshSlot indexes the per-mesh attribute buffers the shadow trace interpolates.
struct NwbRtInstanceMaterialGpu{
    u32 shadowTransmittanceModelId = 0u;
    u32 flags = 0u;
    u32 meshSlot = 0u;
    u32 materialConstantByteOffset = 0u;
    u32 meshInstanceIndex = 0u;
    u32 pad0 = 0u;
    u32 pad1 = 0u;
    u32 pad2 = 0u;
};
static_assert(sizeof(NwbRtInstanceMaterialGpu) == 32u, "NwbRtInstanceMaterialGpu must match the shader NwbRtInstanceMaterial std430 layout (8 x uint)");

// Per-instance shadow-occluder flags (NwbRtInstanceMaterialGpu.flags), mirroring the shader-side
// NWB_RT_INSTANCE_MATERIAL_FLAG_* defines: `Transparent` = evaluate the per-hit transmittance hook; `Refractive` =
// the material asset's `refractive` classification, plumbed to the GPU instance record as groundwork for the future
// caustic producer. The shadow trace does NOT gate on `Refractive` -- transmittance is unified (each material's
// surface hook owns the final value, a refractive hook computing it via the engine helper). This is the producer's
// classification.
namespace RtInstanceMaterialFlag{
    enum Mask : u32{
        None = 0u,
        Transparent = 1u << 0u,
        Refractive = 1u << 1u,
    };
}

// Initial element capacity of the per-frame instance-material table; grows by doubling like the TLAS/scene BVH.
inline constexpr usize s_ShadowInstanceMaterialInitialCapacity = 128u;

// CPU mirror of the software shadow traversal push constants (see shadow_sw_traversal_cs.slang).
struct SwShadowPushConstants{
    u32 width = 0u;
    u32 height = 0u;
    u32 instanceCount = 0u;
    u32 pad0 = 0u;
};
static_assert(sizeof(SwShadowPushConstants) == sizeof(u32) * 4u, "SwShadowPushConstants must match the shader push-constant layout");

// CPU mirror of the caustic photon producer push constants, shared by BOTH the software compute producer
// (caustic/caustic_photon_sw_cs.slang) and the hardware ray-traced producer (caustic/caustic_photon_hw_raygen.slang).
// The byte-identical layout across the two backends is a parity invariant (same photon grid / flux).
struct CausticPhotonPushConstants{
    u32 width = 0u;
    u32 height = 0u;
    u32 instanceCount = 0u;
    u32 photonCount = 0u;
    u32 emissionTargetCount = 0u;
    u32 gridSide = 0u; // sqrt(photonCount): runtime so the photon density scales per build config (dbg vs opt/fin)
};
static_assert(sizeof(CausticPhotonPushConstants) == sizeof(u32) * 6u, "CausticPhotonPushConstants must match the shader push-constant layout");

// CPU mirror of the caustic resolve push constants (caustic/caustic_resolve_cs.slang). The resolve is an N-pass
// edge-avoiding a-trous wavelet denoise: per pass it carries the wavelet dilation (stepWidth) and whether this is the
// first pass (read+normalize the accumulator vs read the previous pass's color). All-scalar -> 32 bytes (no padding
// mismatch); the trailing pads keep it a clean 16B multiple matching the shader struct.
struct CausticResolvePushConstants{
    u32 width = 0u;             // FULL-res width (G-buffer dim; UPSAMPLE dispatch/output dim)
    u32 height = 0u;            // FULL-res height
    u32 halfWidth = 0u;         // HALF-res width (the resolve buffers; PREPARE/WAVELET dispatch dim)
    u32 halfHeight = 0u;        // HALF-res height
    f32 causticIntensity = 0.f; // exposure, applied once during the PREPARE-pass area-normalize
    u32 stepWidth = 1u;         // a-trous dilation for this wavelet pass (1,2,4), in HALF-res texels
    u32 stage = 0u;             // 0 = PREPARE+DOWNSAMPLE, 1 = WAVELET (half-res), 2 = UPSAMPLE (-> full-res irradiance)
    u32 pad = 0u;
};
static_assert(sizeof(CausticResolvePushConstants) == sizeof(u32) * 7u + sizeof(f32), "CausticResolvePushConstants must match the shader push-constant layout");

// Caustic resolve stages, kept in lockstep with caustic_resolve_cs.slang's pushConstants.stage switch.
namespace CausticResolveStage{
    enum Enum : u32{
        PrepareDownsample = 0u,
        Wavelet = 1u,
        Upsample = 2u,
    };
};

// Per-frame photon budget. Energy-conserving (per-photon flux = lightColor*emissionDomainArea*targetCount/photonCount,
// so it scales 1/photonCount), so a higher count only DENSIFIES the splat without changing its per-frame brightness.
// Density matters because the resolve is spatial-only (a-trous, no temporal accumulation): each frame must be dense
// enough on its own. The HARDWARE ray-traced producer handles the full density even in an unoptimized dbg build
// (hardware RT is fast), so it ALWAYS runs the full count -> a dense, coherent caustic in every build. The SOFTWARE-
// compute producer does heavy per-photon work (per-mesh BVH descent + Moeller-Trumbore + the per-hit surface dispatch,
// per bounce) that overruns the GPU watchdog (TDR) at the full count in dbg, so it is config-scaled: dbg-safe in dbg,
// full density in opt/fin. The two counts are independent because the math is per-photon identical and energy-
// conserving -> both produce the SAME image (the HW/SW parity A/B holds). The grid side rides the push constant
// (gridSide), so the shaders stay config-agnostic. PHOTON_COUNT must stay == GRID_SIDE^2.
inline constexpr u32 s_CausticHwPhotonGridSide = 512u;
inline constexpr u32 s_CausticHwPhotonCount = s_CausticHwPhotonGridSide * s_CausticHwPhotonGridSide;
#if defined(NDEBUG)
inline constexpr u32 s_CausticSwPhotonGridSide = 512u;                      // opt/fin: the full density (262144 photons)
#else
inline constexpr u32 s_CausticSwPhotonGridSide = NWB_CAUSTIC_SW_GRID_SIDE;  // dbg-safe (128 -> 16384; the SW path TDRs above this unoptimized)
#endif
inline constexpr u32 s_CausticSwPhotonCount = s_CausticSwPhotonGridSide * s_CausticSwPhotonGridSide;

// Single exposure knob (the only caustic brightness gain), applied in the resolve after the PHYSICAL flux->irradiance
// conversion (per-photon flux = lightColor * emissionDomainArea * targetCount / photonCount in the producer, divided
// by the receiver area-per-pixel in the resolve). Because the producer flux is now physical + energy-conserving (no
// fudge constant -- the old s_CausticTotalEmittedPower=20 / s_CausticIntensity=10 pair is gone), the caustic has a
// MEANINGFUL dynamic range: the DENSE focused caustic stays bright at a low exposure while the SPARSE far prism-
// scatter falls below the visible floor. ~2 is a unit-ish exposure for the demo lighting (light intensity 2.0);
// doubling the photon count leaves the per-frame image brightness unchanged (the energy-conservation invariant holds).
inline constexpr f32 s_CausticIntensity = 2.0f;

// The caustic resolve denoise is an N-pass edge-avoiding a-trous wavelet (purely spatial, NO temporal accumulation ->
// ghost-free for a moving caustic). The pass count + wavelet kernel live in the shader (NWB_CAUSTIC_RESOLVE_PASS_COUNT,
// resolve_binding_slots.h); there is no longer a single-pass blur radius or a temporal-EMA blend weight.

// Photon-aim slack for DEFORMING (runtime/skinned) refractors. Their emission AABB is derived from the bind-pose
// (rest) bounds -- there is no per-frame CPU deformed bound (the per-frame bounds live only in the GPU meshlet-bounds
// buffer). A skinned pose can push the surface past the rest extent, so the rest-bounds world AABB is inflated about
// its center by this factor for runtime instances, keeping the photon emission domain over the deformed surface.
// Matches the engine's skinned meshlet-bounds radius inflation precedent (NWB_SKINNED_MESH_BOUNDS_RADIUS_INFLATION).
inline constexpr f32 s_CausticRuntimeBoundsInflation = 1.25f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_raytracing_system{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Extracts the axis-th component (0=x, 1=y, 2=z) of a SIMD vector.
[[nodiscard]] f32 SceneBvhAxisComponent(const SIMDVector value, const u32 axis)noexcept{
    return axis == 0u ? VectorGetX(value) : (axis == 1u ? VectorGetY(value) : VectorGetZ(value));
}

// Recursively builds a binary BVH over the [lo, hi) slice of the instance-index permutation, appending nodes
// to `nodes` (the first node appended for the whole range is the root, index 0). It splits at the spatial
// median of the largest centroid-extent axis, falling back to the count median when a spatial split puts
// every instance on one side. Leaves store NWB_BVH_LEAF_FLAG | instanceIndex + the instance world AABB;
// internal nodes store child node indices + the unioned box — the exact NwbBvhNode layout the per-mesh build
// produces, so the GPU traversal is uniform across the scene BVH and every per-mesh BVH.
u32 BuildSceneBvhNode(
    u32* indices,
    const u32 lo,
    const u32 hi,
    const SIMDVector* instanceAabbMin,
    const SIMDVector* instanceAabbMax,
    const SIMDVector* instanceCentroid,
    Vector<SceneBvhNodeBuildData, Core::Alloc::ScratchArena>& nodes
){
    const u32 nodeIndex = static_cast<u32>(nodes.size());
    nodes.push_back(SceneBvhNodeBuildData{ VectorZero(), VectorZero(), 0u, 0u });

    if((hi - lo) == 1u){
        const u32 instance = indices[lo];
        SceneBvhNodeBuildData& node = nodes[nodeIndex];
        node.aabbMin = instanceAabbMin[instance];
        node.aabbMax = instanceAabbMax[instance];
        node.leftChild = BvhNodeIndex::LeafFlag | instance;
        node.rightChild = 1u;
        return nodeIndex;
    }

    SIMDVector centroidMin = VectorReplicate(1e30f);
    SIMDVector centroidMax = VectorReplicate(-1e30f);
    for(u32 i = lo; i < hi; ++i){
        const SIMDVector centroid = instanceCentroid[indices[i]];
        centroidMin = VectorMin(centroidMin, centroid);
        centroidMax = VectorMax(centroidMax, centroid);
    }

    const SIMDVector centroidExtent = VectorSubtract(centroidMax, centroidMin);
    u32 axis = 0u;
    f32 axisExtent = VectorGetX(centroidExtent);
    if(VectorGetY(centroidExtent) > axisExtent){ axis = 1u; axisExtent = VectorGetY(centroidExtent); }
    if(VectorGetZ(centroidExtent) > axisExtent){ axis = 2u; }

    const f32 splitValue = 0.5f * (SceneBvhAxisComponent(centroidMin, axis) + SceneBvhAxisComponent(centroidMax, axis));

    u32 mid = lo;
    for(u32 i = lo; i < hi; ++i){
        if(SceneBvhAxisComponent(instanceCentroid[indices[i]], axis) < splitValue){
            const u32 swap = indices[i];
            indices[i] = indices[mid];
            indices[mid] = swap;
            ++mid;
        }
    }
    if(mid == lo || mid == hi)
        mid = lo + (hi - lo) / 2u; // degenerate spatial split (coincident centroids) -> count median; still correct

    const u32 leftChild = BuildSceneBvhNode(indices, lo, mid, instanceAabbMin, instanceAabbMax, instanceCentroid, nodes);
    const u32 rightChild = BuildSceneBvhNode(indices, mid, hi, instanceAabbMin, instanceAabbMax, instanceCentroid, nodes);

    SceneBvhNodeBuildData& node = nodes[nodeIndex];
    node.aabbMin = VectorMin(nodes[leftChild].aabbMin, nodes[rightChild].aabbMin);
    node.aabbMax = VectorMax(nodes[leftChild].aabbMax, nodes[rightChild].aabbMax);
    node.leftChild = leftChild;
    node.rightChild = rightChild;
    return nodeIndex;
}

// Builds the per-instance shadow-occluder material record from the cooked material surface info: the
// transmittance-model id (dispatches the per-hit transmittance hook) + the transparent flag. Both shadow builders
// append exactly one of these per instance, in instance push order, so the table indexes by shadow instance id.
// The per-mesh attribute slot + the material-constants context (constant byte offset + g_NwbMeshInstances index)
// are supplied by the caller, which resolves them alongside the occluder's mesh + per-entity instance mapping.
[[nodiscard]] NwbRtInstanceMaterialGpu ResolveInstanceShadowMaterial(
    const MaterialSurfaceInfo& materialInfo,
    const u32 meshSlot,
    const u32 materialConstantByteOffset,
    const u32 meshInstanceIndex
){
    NwbRtInstanceMaterialGpu material;
    material.shadowTransmittanceModelId = materialInfo.shadowTransmittanceModelId;
    material.flags =
        (materialInfo.transparent ? RtInstanceMaterialFlag::Transparent : RtInstanceMaterialFlag::None)
        | (materialInfo.refractive ? RtInstanceMaterialFlag::Refractive : RtInstanceMaterialFlag::None);
    material.meshSlot = meshSlot;
    material.materialConstantByteOffset = materialConstantByteOffset;
    material.meshInstanceIndex = meshInstanceIndex;
    return material;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RendererRayTracingSystem::RendererRayTracingSystem(RendererSystem& renderer)
    : RendererSystemSubsystemBase<RendererSystem>(renderer)
{}


void RendererRayTracingSystem::logCapabilityOnce(){
    if(rayTracingState().m_capabilityLogged)
        return;

    rayTracingState().m_capabilityLogged = true;
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: ray tracing capability - accel struct {}, pipeline {}, ray query {}")
        , graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct)
        , graphics().queryFeatureSupport(Core::Feature::RayTracingPipeline)
        , graphics().queryFeatureSupport(Core::Feature::RayQuery)
    );

#if defined(NWB_DEBUG)
    runBvhSortSelfTest();
    runBvhBuildSelfTest();
    runSceneBvhSelfTest();
#endif
}

bool RendererRayTracingSystem::buildPendingMeshBlas(Core::CommandList& commandList){
    if(!graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct))
        return false;

    auto& meshes = meshState().m_meshes;
    for(auto it = meshes.begin(); it != meshes.end(); ++it){
        MeshResources& meshResources = it.value();

        // Runtime (skinned/deforming) meshes change their vertex positions every
        // frame, so their BLAS is rebuilt from the freshly skinned positions each
        // frame. Static meshes build a BLAS once and clear the pending flag.
        if(meshResources.runtimeMesh){
            if(!buildMeshBlas(commandList, meshResources))
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: runtime mesh BLAS build failed"));
            continue;
        }

        if(!meshResources.blasBuildPending)
            continue;
        if(buildMeshBlas(commandList, meshResources))
            meshResources.blasBuildPending = false;
    }
    return true;
}

bool RendererRayTracingSystem::buildPendingMeshSwBvh(Core::CommandList& commandList){
    // The software BVH is the shadow fallback for devices without hardware ray tracing. When accel structs
    // are available buildPendingMeshBlas handles shadows and this path stays idle.
    if(graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct))
        return false;

    bool allBuildsReady = true;
    auto& meshes = meshState().m_meshes;
    for(auto it = meshes.begin(); it != meshes.end(); ++it){
        MeshResources& meshResources = it.value();

        // Runtime (skinned/deforming) meshes update their software BVH every frame from the freshly skinned
        // positions; static meshes build once and clear the pending flag.
        if(meshResources.runtimeMesh){
            if(!updateMeshSwBvh(commandList, meshResources)){
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: runtime mesh '{}' software BVH update failed")
                    , StringConvert(meshResources.meshName.c_str())
                );
                allBuildsReady = false;
            }
            continue;
        }

        if(!meshResources.swBvhBuildPending)
            continue;
        if(updateMeshSwBvh(commandList, meshResources))
            meshResources.swBvhBuildPending = false;
        else{
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: static mesh '{}' software BVH build failed")
                , StringConvert(meshResources.meshName.c_str())
            );
            allBuildsReady = false;
        }
    }
    return allBuildsReady;
}

bool RendererRayTracingSystem::buildSceneTlas(Core::CommandList& commandList, Core::Alloc::ScratchArena& scratchArena){
    if(!graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct))
        return false;

    auto* meshSystem = world().getSystem<NWB::Impl::MeshSystem>();
    if(!meshSystem)
        return false;

    auto rendererView = world().view<RendererComponent>();
    Vector<Core::RayTracingInstanceDesc, Core::Alloc::ScratchArena> instances{ scratchArena };
    // Per-instance occluder material, built lockstep with `instances` (one record per push, same order) so the
    // uploaded table indexes by the hardware InstanceID() the shadow any-hit reads.
    Vector<NwbRtInstanceMaterialGpu, Core::Alloc::ScratchArena> instanceMaterials{ scratchArena };
    // Shadow-OWNED combined material-constants context, built lockstep with `instances`: the per-occluder
    // InstanceGpuData (g_NwbMeshInstances for the trace; index == InstanceID()) + the combined typed bytes
    // (g_NwbMaterialTypedWords for the trace; each occluder's constant + mutable block). The draw pass's buffers
    // hold only the opaque set at trace time, so the trace cannot use them -- see ensureShadowBindingSet.
    InstanceGpuDataVector shadowInstanceData{ scratchArena };
    MaterialTypedByteDataVector shadowMaterialTypedBytes{ scratchArena };
    ECSRenderDetail::MaterialTypedByteContentRangeMap shadowMutableTypedRanges(
        0,
        ECSRenderDetail::MaterialTypedByteContentKeyHasher(),
        EqualTo<ECSRenderDetail::MaterialTypedByteContentKey>(),
        scratchArena
    );
    instances.reserve(rendererView.candidateCount());
    instanceMaterials.reserve(rendererView.candidateCount());
    shadowInstanceData.reserve(rendererView.candidateCount());
    shadowMutableTypedRanges.reserve(rendererView.candidateCount());

    // Reset the per-frame distinct-mesh table; the gather repopulates it (slot k -> mesh k's index/attribute
    // buffers) for the per-mesh descriptor arrays the any-hit indexes by material.meshSlot.
    rayTracingState().m_shadowMeshCount = 0u;

    for(auto&& [entity, renderer] : rendererView){
        if(!renderer.visible)
            continue;

        RenderableMeshDesc resolvedMesh;
        if(!meshSystem->resolveRenderableMesh(entity, resolvedMesh))
            continue;

        MeshResources* mesh = nullptr;
        const bool meshReady = resolvedMesh.runtime
            ? m_renderer.meshSystem().findRuntimeMeshResources(resolvedMesh.runtimeMesh, mesh)
            : m_renderer.meshSystem().findMeshResources(resolvedMesh.mesh, mesh)
        ;
        // The BLAS owns the positions; the any-hit still needs the index buffer (3 vertex indices by
        // PrimitiveIndex) + the U2 attribute buffer (normal/uv0 for the per-hit dispatch), so require both.
        if(!meshReady || !mesh || !mesh->blas || !mesh->triangleIndexBuffer || !mesh->attributeBuffer)
            continue;

        // Dedupe to a per-mesh descriptor-array slot: instances sharing a mesh share its index/attribute
        // buffers. Once the per-frame mesh cap is reached, the instance casts a colorless (opaque) shadow.
        Core::Buffer* meshIndexBuffer = mesh->triangleIndexBuffer.get();
        u32 meshSlot = NWB_SHADOW_RT_MAX_MESHES;
        for(u32 slot = 0u; slot < rayTracingState().m_shadowMeshCount; ++slot){
            if(rayTracingState().m_shadowMeshIndexBuffers[slot] == meshIndexBuffer){
                meshSlot = slot;
                break;
            }
        }
        if(meshSlot == NWB_SHADOW_RT_MAX_MESHES){
            if(rayTracingState().m_shadowMeshCount >= NWB_SHADOW_RT_MAX_MESHES){
                if(!rayTracingState().m_shadowMeshCapReported){
                    rayTracingState().m_shadowMeshCapReported = true;
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: hardware shadow distinct-mesh cap ({}) reached; further meshes cast colorless shadows this scene")
                        , static_cast<u64>(NWB_SHADOW_RT_MAX_MESHES)
                    );
                }
                continue;
            }
            meshSlot = rayTracingState().m_shadowMeshCount++;
            rayTracingState().m_shadowMeshIndexBuffers[meshSlot] = meshIndexBuffer;
            rayTracingState().m_shadowMeshAttributeBuffers[meshSlot] = mesh->attributeBuffer.get();
        }

        Core::RayTracingInstanceDesc instanceDesc;
        instanceDesc.setBLAS(mesh->blas.get());
        instanceDesc.setInstanceID(static_cast<u32>(instances.size()));
        instanceDesc.setInstanceMask(0xFFu);

        const NWB::Impl::Scene::TransformComponent* transform = world().tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity);
        if(transform){
            // Compose object->world (T * R(quat) * S) and store it as the instance's row-major 3x4 transform;
            // the engine's column-vector SIMDMatrix rows map directly onto AffineTransform (= Float34).
            const SIMDMatrix instanceWorld = MatrixAffineTransformation(
                LoadFloat(transform->scale),
                VectorZero(),
                LoadFloat(transform->rotation),
                LoadFloat(transform->position)
            );
            StoreFloat(instanceWorld, &instanceDesc.transform);
        }

        // Resolve the occluder material (transmittance-model id + transparent flag) + the per-mesh attribute slot
        // + the material-constants context the any-hit's per-hit dispatch reads. That context is packed into the
        // SHADOW-OWNED combined buffers (NOT the draw pass's, which hold only one transparency class at trace
        // time): appendShadowOccluderMaterialContext appends this occluder's constant block (its real byte offset
        // -> materialConstantByteOffset) + its per-instance mutable block (offset packed into the InstanceGpuData
        // pushed lockstep below). meshInstanceIndex == the combined-instance push index == InstanceID(). An
        // unresolved material falls back to a fully-opaque record (still a colorless shadow) + a default instance.
        const u32 meshInstanceIndex = static_cast<u32>(instances.size());
        NwbRtInstanceMaterialGpu instanceMaterial;
        InstanceGpuData shadowInstance;
        MaterialSurfaceInfo* materialInfo = nullptr;
        if(m_renderer.materialSystem().createMaterialSurfaceInfo(renderer.material, materialInfo)){
            u32 materialConstantByteOffset = 0u;
            if(!m_renderer.materialSystem().appendShadowOccluderMaterialContext(
                entity,
                *materialInfo,
                transform,
                shadowMaterialTypedBytes,
                shadowMutableTypedRanges,
                shadowInstance,
                materialConstantByteOffset
            ))
                return false;
            instanceMaterial = __hidden_raytracing_system::ResolveInstanceShadowMaterial(*materialInfo, meshSlot, materialConstantByteOffset, meshInstanceIndex);
        }

        instances.push_back(instanceDesc);
        instanceMaterials.push_back(instanceMaterial);
        shadowInstanceData.push_back(shadowInstance);
    }

    rayTracingState().m_tlasInstanceCount = static_cast<u32>(instances.size());
    if(instances.empty()){
        rayTracingState().m_tlasDeviceAddress = 0u;
        return false;
    }

    if(!rayTracingState().m_tlas || rayTracingState().m_tlasMaxInstances < instances.size()){
        usize capacity = rayTracingState().m_tlasMaxInstances > 0u ? rayTracingState().m_tlasMaxInstances : s_TlasInitialInstanceCapacity;
        while(capacity < instances.size())
            capacity *= 2u;

        Core::RayTracingAccelStructDesc accelStructDesc(arena());
        accelStructDesc.setTopLevelMaxInstances(capacity);
        accelStructDesc.setBuildFlags(Core::RayTracingAccelStructBuildFlags::PreferFastTrace);
        accelStructDesc.setDebugName(Name("scene_tlas"));

        auto* device = graphics().getDevice();
        Core::RayTracingAccelStructHandle tlas = device->createAccelStruct(accelStructDesc);
        if(!tlas){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create scene TLAS (capacity {})")
                , static_cast<u64>(capacity)
            );
            return false;
        }
        rayTracingState().m_tlas = Move(tlas);
        rayTracingState().m_tlasMaxInstances = capacity;
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created scene TLAS (capacity {} instances)")
            , static_cast<u64>(capacity)
        );
    }

    commandList.buildTopLevelAccelStruct(
        rayTracingState().m_tlas.get(),
        instances.data(),
        instances.size(),
        Core::RayTracingAccelStructBuildFlags::PreferFastTrace
    );
    rayTracingState().m_tlasDeviceAddress = rayTracingState().m_tlas->getDeviceAddress();

    // Upload the per-instance occluder material table (indexed by InstanceID()) for the hardware any-hit.
    if(!ensureShadowInstanceMaterialBuffer(instances.size()))
        return false;
    Core::Buffer* materialBuffer = rayTracingState().m_shadowInstanceMaterialBuffer.get();
    commandList.setBufferState(materialBuffer, Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(materialBuffer, instanceMaterials.data(), instanceMaterials.size() * sizeof(NwbRtInstanceMaterialGpu));
    commandList.setBufferState(materialBuffer, Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();

    // Upload the shadow-owned combined material-constants context the any-hit's per-hit transmittance dispatch
    // reads. A word of padding keeps the typed buffer valid when no occluder contributed any typed bytes.
    if(shadowMaterialTypedBytes.empty())
        shadowMaterialTypedBytes.resize(sizeof(u32), 0u);
    if(!uploadShadowMaterialContextBuffers(commandList, shadowInstanceData, shadowMaterialTypedBytes))
        return false;
    return true;
}

bool RendererRayTracingSystem::buildSceneSwBvh(Core::CommandList& commandList, Core::Alloc::ScratchArena& scratchArena){
    // Software scene/instance BVH (TLAS-analog) — only the no-hardware-ray-tracing fallback builds it; the
    // hardware path uses buildSceneTlas instead.
    if(graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct))
        return false;

    auto* meshSystem = world().getSystem<NWB::Impl::MeshSystem>();
    if(!meshSystem)
        return false;

    auto rendererView = world().view<RendererComponent>();
    const usize candidateCount = rendererView.candidateCount();

    // Per-instance GPU records + the world-space AABBs / centroids the CPU build consumes (kept parallel so
    // the BVH leaf payload indexes straight into the uploaded instance buffer).
    Vector<SceneSwBvhInstanceGpu, Core::Alloc::ScratchArena> instances{ scratchArena };
    Vector<SIMDVector, Core::Alloc::ScratchArena> instanceAabbMin{ scratchArena };
    Vector<SIMDVector, Core::Alloc::ScratchArena> instanceAabbMax{ scratchArena };
    Vector<SIMDVector, Core::Alloc::ScratchArena> instanceCentroid{ scratchArena };
    // Per-instance occluder material, built lockstep with `instances` (same push order) so the uploaded table
    // indexes by the scene-BVH leaf instance index the software traversal reads.
    Vector<NwbRtInstanceMaterialGpu, Core::Alloc::ScratchArena> instanceMaterials{ scratchArena };
    // Shadow-OWNED combined material-constants context, built lockstep with `instances` (see buildSceneTlas): the
    // per-occluder InstanceGpuData (g_NwbMeshInstances for the trace) + the combined typed bytes
    // (g_NwbMaterialTypedWords for the trace). The draw pass's buffers hold only one transparency class at trace
    // time, so the software traversal binds these instead -- see ensureSwShadowBindingSet.
    InstanceGpuDataVector shadowInstanceData{ scratchArena };
    MaterialTypedByteDataVector shadowMaterialTypedBytes{ scratchArena };
    ECSRenderDetail::MaterialTypedByteContentRangeMap shadowMutableTypedRanges(
        0,
        ECSRenderDetail::MaterialTypedByteContentKeyHasher(),
        EqualTo<ECSRenderDetail::MaterialTypedByteContentKey>(),
        scratchArena
    );
    instances.reserve(candidateCount);
    instanceAabbMin.reserve(candidateCount);
    instanceAabbMax.reserve(candidateCount);
    instanceCentroid.reserve(candidateCount);
    instanceMaterials.reserve(candidateCount);
    shadowInstanceData.reserve(candidateCount);
    shadowMutableTypedRanges.reserve(candidateCount);

    // Reset the per-frame distinct-mesh table; the gather repopulates it (slot k -> mesh k's buffers) for the
    // per-mesh descriptor arrays the traversal binds.
    rayTracingState().m_swShadowMeshCount = 0u;

    for(auto&& [entity, renderer] : rendererView){
        if(!renderer.visible)
            continue;

        RenderableMeshDesc resolvedMesh;
        if(!meshSystem->resolveRenderableMesh(entity, resolvedMesh))
            continue;

        MeshResources* mesh = nullptr;
        const bool meshReady = resolvedMesh.runtime
            ? m_renderer.meshSystem().findRuntimeMeshResources(resolvedMesh.runtimeMesh, mesh)
            : m_renderer.meshSystem().findMeshResources(resolvedMesh.mesh, mesh)
        ;
        // Only instances whose per-mesh software BVH + geometry are built (and that have valid object-space
        // bounds) can be traced.
        if(!meshReady || !mesh || !mesh->swBvhNodeBuffer || !mesh->positionBuffer || !mesh->triangleIndexBuffer || !mesh->csgLocalBounds.valid())
            continue;

        // Dedupe to a per-mesh descriptor-array slot: instances sharing a mesh share its node/position/index
        // buffers. Once the per-frame mesh cap is reached, the instance casts no software shadow this frame.
        Core::Buffer* meshNodeBuffer = mesh->swBvhNodeBuffer.get();
        u32 meshSlot = NWB_SW_SHADOW_MAX_MESHES;
        for(u32 slot = 0u; slot < rayTracingState().m_swShadowMeshCount; ++slot){
            if(rayTracingState().m_swShadowMeshNodeBuffers[slot] == meshNodeBuffer){
                meshSlot = slot;
                break;
            }
        }
        if(meshSlot == NWB_SW_SHADOW_MAX_MESHES){
            if(rayTracingState().m_swShadowMeshCount >= NWB_SW_SHADOW_MAX_MESHES){
                if(!rayTracingState().m_swShadowMeshCapReported){
                    rayTracingState().m_swShadowMeshCapReported = true;
                    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: software shadow distinct-mesh cap ({}) reached; further meshes cast no software shadow this scene")
                        , static_cast<u64>(NWB_SW_SHADOW_MAX_MESHES)
                    );
                }
                continue;
            }
            meshSlot = rayTracingState().m_swShadowMeshCount++;
            rayTracingState().m_swShadowMeshNodeBuffers[meshSlot] = meshNodeBuffer;
            rayTracingState().m_swShadowMeshPositionBuffers[meshSlot] = mesh->positionBuffer.get();
            rayTracingState().m_swShadowMeshIndexBuffers[meshSlot] = mesh->triangleIndexBuffer.get();
            // The U2 per-vertex shadow-trace attribute buffer (positionStream-indexed normal/uv0), parallel to
            // the position/index buffers above so the trace interpolates with the SAME i0/i1/i2.
            rayTracingState().m_swShadowMeshAttributeBuffers[meshSlot] = mesh->attributeBuffer.get();
        }

        // object->world from the decomposed transform (identity when absent), matching buildSceneTlas.
        const NWB::Impl::Scene::TransformComponent* transform = world().tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity);
        SIMDMatrix objectToWorld = MatrixIdentity();
        if(transform){
            objectToWorld = MatrixAffineTransformation(
                LoadFloat(transform->scale),
                VectorZero(),
                LoadFloat(transform->rotation),
                LoadFloat(transform->position)
            );
        }
        SIMDVector determinant;
        const SIMDMatrix worldToObject = MatrixInverse(&determinant, objectToWorld);

        // World AABB = bounds of the 8 object-space corners transformed to world space (exact under rotation).
        const SIMDVector localMin = LoadFloatInt(mesh->csgLocalBounds.minBounds);
        const SIMDVector localMax = LoadFloatInt(mesh->csgLocalBounds.maxBounds);
        const f32 minX = VectorGetX(localMin), minY = VectorGetY(localMin), minZ = VectorGetZ(localMin);
        const f32 maxX = VectorGetX(localMax), maxY = VectorGetY(localMax), maxZ = VectorGetZ(localMax);
        SIMDVector worldMin = VectorReplicate(1e30f);
        SIMDVector worldMax = VectorReplicate(-1e30f);
        for(u32 corner = 0u; corner < 8u; ++corner){
            const SIMDVector localCorner = VectorSet(
                (corner & 1u) != 0u ? maxX : minX,
                (corner & 2u) != 0u ? maxY : minY,
                (corner & 4u) != 0u ? maxZ : minZ,
                1.0f
            );
            const SIMDVector worldCorner = Vector3TransformCoord(localCorner, objectToWorld);
            worldMin = VectorMin(worldMin, worldCorner);
            worldMax = VectorMax(worldMax, worldCorner);
        }

        SceneSwBvhInstanceGpu instance;
        StoreFloat(worldToObject, &instance.worldToObject);
        instance.meshIndex = meshSlot;
        instance.primitiveCount = mesh->meshletPrimitiveIndexCount / 3u;

        // Resolve the occluder material (transmittance-model id + transparent flag) + the material-constants
        // context the surface hook reads in the trace. That context is packed into the SHADOW-OWNED combined
        // buffers (NOT the draw pass's, which hold only one transparency class at trace time):
        // appendShadowOccluderMaterialContext appends this occluder's constant block (real byte offset ->
        // materialConstantByteOffset) + its per-instance mutable block (offset packed into the InstanceGpuData
        // pushed lockstep below). meshInstanceIndex == the combined-instance push index == the scene-BVH leaf
        // index the traversal reads. An unresolved material falls back to a fully-opaque record (still a colorless
        // shadow) + a default instance.
        const u32 meshInstanceIndex = static_cast<u32>(instances.size());
        NwbRtInstanceMaterialGpu instanceMaterial;
        InstanceGpuData shadowInstance;
        MaterialSurfaceInfo* materialInfo = nullptr;
        if(m_renderer.materialSystem().createMaterialSurfaceInfo(renderer.material, materialInfo)){
            u32 materialConstantByteOffset = 0u;
            if(!m_renderer.materialSystem().appendShadowOccluderMaterialContext(
                entity,
                *materialInfo,
                transform,
                shadowMaterialTypedBytes,
                shadowMutableTypedRanges,
                shadowInstance,
                materialConstantByteOffset
            ))
                return false;
            instanceMaterial = __hidden_raytracing_system::ResolveInstanceShadowMaterial(*materialInfo, meshSlot, materialConstantByteOffset, meshInstanceIndex);
        }

        instances.push_back(instance);
        instanceAabbMin.push_back(worldMin);
        instanceAabbMax.push_back(worldMax);
        instanceCentroid.push_back(VectorScale(VectorAdd(worldMin, worldMax), 0.5f));
        instanceMaterials.push_back(instanceMaterial);
        shadowInstanceData.push_back(shadowInstance);
    }

    const u32 instanceCount = static_cast<u32>(instances.size());
    if(instanceCount == 0u){
        // No traceable instances is not a failure: the consumer treats a zero instance count as
        // "nothing occludes" (fully lit), so report success and leave the resident buffers untouched.
        rayTracingState().m_sceneBvhInstanceCount = 0u;
        return true;
    }

    // CPU-build the scene BVH over the gathered instance world AABBs. At TLAS scale (a handful to a few
    // hundred instances) a CPU build + upload is far cheaper than a GPU LBVH dispatch, and the node layout
    // matches the per-mesh BVH so the traversal pass reads scene and mesh BVHs the same way.
    Vector<u32, Core::Alloc::ScratchArena> indices{ scratchArena };
    indices.reserve(instanceCount);
    for(u32 i = 0u; i < instanceCount; ++i)
        indices.push_back(i);

    const usize nodeCount = static_cast<usize>(instanceCount) * 2u - 1u;
    Vector<SceneBvhNodeBuildData, Core::Alloc::ScratchArena> buildNodes{ scratchArena };
    buildNodes.reserve(nodeCount);
    __hidden_raytracing_system::BuildSceneBvhNode(indices.data(), 0u, instanceCount, instanceAabbMin.data(), instanceAabbMax.data(), instanceCentroid.data(), buildNodes);
    NWB_ASSERT(buildNodes.size() == nodeCount);

    Vector<NwbBvhNodeGpu, Core::Alloc::ScratchArena> nodes{ scratchArena };
    nodes.reserve(buildNodes.size());
    for(const SceneBvhNodeBuildData& buildNode : buildNodes){
        NwbBvhNodeGpu node;
        StoreFloatInt(buildNode.aabbMin, buildNode.leftChild, &node.aabbMinLeftChild);
        StoreFloatInt(buildNode.aabbMax, buildNode.rightChild, &node.aabbMaxRightChild);
        nodes.push_back(node);
    }

    if(!ensureSceneBvhBuffers(instanceCount))
        return false;
    if(!ensureShadowInstanceMaterialBuffer(instances.size()))
        return false;

    Core::Buffer* nodeBuffer = rayTracingState().m_sceneBvhNodeBuffer.get();
    Core::Buffer* instanceBuffer = rayTracingState().m_sceneInstanceBuffer.get();
    Core::Buffer* materialBuffer = rayTracingState().m_shadowInstanceMaterialBuffer.get();

    commandList.setBufferState(nodeBuffer, Core::ResourceStates::CopyDest);
    commandList.setBufferState(instanceBuffer, Core::ResourceStates::CopyDest);
    commandList.setBufferState(materialBuffer, Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(nodeBuffer, nodes.data(), nodes.size() * sizeof(NwbBvhNodeGpu));
    commandList.writeBuffer(instanceBuffer, instances.data(), instances.size() * sizeof(SceneSwBvhInstanceGpu));
    commandList.writeBuffer(materialBuffer, instanceMaterials.data(), instanceMaterials.size() * sizeof(NwbRtInstanceMaterialGpu));
    commandList.setBufferState(nodeBuffer, Core::ResourceStates::ShaderResource);
    commandList.setBufferState(instanceBuffer, Core::ResourceStates::ShaderResource);
    commandList.setBufferState(materialBuffer, Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();

    // Upload the shadow-owned combined material-constants context the software traversal's per-hit transmittance
    // dispatch reads. A word of padding keeps the typed buffer valid when no occluder contributed any typed bytes.
    if(shadowMaterialTypedBytes.empty())
        shadowMaterialTypedBytes.resize(sizeof(u32), 0u);
    if(!uploadShadowMaterialContextBuffers(commandList, shadowInstanceData, shadowMaterialTypedBytes))
        return false;

    rayTracingState().m_sceneBvhInstanceCount = instanceCount;
    return true;
}

bool RendererRayTracingSystem::prepareCausticEmissionTargets(Core::CommandList& commandList, Core::Alloc::ScratchArena& scratchArena){
    // Caustic emission-target gather (P1, CPU only): collect the world-space AABB of every refractive instance in
    // the scene -- the domain the future caustic photon producer aims at. Runs once per frame regardless of the
    // shadow backend (HW TLAS or SW BVH); mirrors buildSceneSwBvh's mesh/transform resolve + 8-corner world AABB
    // but does NOT require a software BVH and keeps ONLY instances whose material is refractive. The count gates
    // caustic-light assignment (see ResolveCausticLights): zero refractive instances -> zero caustic lights. No
    // consumer reads the uploaded buffer yet.
    rayTracingState().m_causticRefractiveInstanceCount = 0u;

    auto* meshSystem = world().getSystem<NWB::Impl::MeshSystem>();
    if(!meshSystem)
        return true;

    auto rendererView = world().view<RendererComponent>();
    const usize candidateCount = rendererView.candidateCount();

    Vector<NwbCausticEmissionTargetGpu, Core::Alloc::ScratchArena> targets{ scratchArena };
    targets.reserve(candidateCount);

    SIMDVector combinedMin = VectorReplicate(1e30f);
    SIMDVector combinedMax = VectorReplicate(-1e30f);

    for(auto&& [entity, renderer] : rendererView){
        if(!renderer.visible)
            continue;

        RenderableMeshDesc resolvedMesh;
        if(!meshSystem->resolveRenderableMesh(entity, resolvedMesh))
            continue;

        MeshResources* mesh = nullptr;
        const bool meshReady = resolvedMesh.runtime
            ? m_renderer.meshSystem().findRuntimeMeshResources(resolvedMesh.runtimeMesh, mesh)
            : m_renderer.meshSystem().findMeshResources(resolvedMesh.mesh, mesh)
        ;
        // Need valid object-space bounds to build a world AABB; the per-mesh BVH / position buffers (required by
        // the SW shadow trace) are NOT required here -- the emission target is geometry-agnostic.
        if(!meshReady || !mesh || !mesh->csgLocalBounds.valid())
            continue;

        // Only refractive instances are emission targets (the producer's classification, mirroring buildSceneTlas
        // / buildSceneSwBvh's MaterialSurfaceInfo.refractive read).
        MaterialSurfaceInfo* materialInfo = nullptr;
        if(!m_renderer.materialSystem().createMaterialSurfaceInfo(renderer.material, materialInfo))
            continue;
        if(!materialInfo || !materialInfo->refractive)
            continue;

        // object->world from the decomposed transform (identity when absent), matching buildSceneSwBvh.
        const NWB::Impl::Scene::TransformComponent* transform = world().tryGetComponent<NWB::Impl::Scene::TransformComponent>(entity);
        SIMDMatrix objectToWorld = MatrixIdentity();
        if(transform){
            objectToWorld = MatrixAffineTransformation(
                LoadFloat(transform->scale),
                VectorZero(),
                LoadFloat(transform->rotation),
                LoadFloat(transform->position)
            );
        }

        // World AABB = bounds of the 8 object-space corners transformed to world space (exact under rotation).
        SIMDVector localMin = LoadFloatInt(mesh->csgLocalBounds.minBounds);
        SIMDVector localMax = LoadFloatInt(mesh->csgLocalBounds.maxBounds);
        if(resolvedMesh.runtime){
            // Inflate the bind-pose extent about its center (component-wise, in SIMD) for a deforming refractor (no
            // per-frame CPU bound exists); keeps the photon emission domain over a skinned pose that reaches past the
            // rest bounds. center = (min+max)/2; half = (max-min)/2 * inflation; [min,max] = center -+ half.
            const SIMDVector center = VectorMultiply(VectorAdd(localMin, localMax), VectorReplicate(0.5f));
            const SIMDVector half = VectorMultiply(VectorSubtract(localMax, localMin), VectorReplicate(0.5f * s_CausticRuntimeBoundsInflation));
            localMin = VectorSubtract(center, half);
            localMax = VectorAdd(center, half);
        }
        const f32 minX = VectorGetX(localMin), minY = VectorGetY(localMin), minZ = VectorGetZ(localMin);
        const f32 maxX = VectorGetX(localMax), maxY = VectorGetY(localMax), maxZ = VectorGetZ(localMax);
        SIMDVector worldMin = VectorReplicate(1e30f);
        SIMDVector worldMax = VectorReplicate(-1e30f);
        for(u32 corner = 0u; corner < 8u; ++corner){
            const SIMDVector localCorner = VectorSet(
                (corner & 1u) != 0u ? maxX : minX,
                (corner & 2u) != 0u ? maxY : minY,
                (corner & 4u) != 0u ? maxZ : minZ,
                1.0f
            );
            const SIMDVector worldCorner = Vector3TransformCoord(localCorner, objectToWorld);
            worldMin = VectorMin(worldMin, worldCorner);
            worldMax = VectorMax(worldMax, worldCorner);
        }

        combinedMin = VectorMin(combinedMin, worldMin);
        combinedMax = VectorMax(combinedMax, worldMax);

        NwbCausticEmissionTargetGpu target;
        StoreFloat(worldMin, &target.aabbMin);
        StoreFloat(worldMax, &target.aabbMax);
        target.aabbMin.w = 0.f;
        target.aabbMax.w = 0.f;
        targets.push_back(target);
    }

    const u32 targetCount = static_cast<u32>(targets.size());
    if(targetCount == 0u){
        // No refractive instances: leave the resident buffer untouched, keep the count zero (every light's
        // caustic slot stays -1 downstream), and reset the combined extent.
        rayTracingState().m_causticTargetBoundsMin = Float4(0.f, 0.f, 0.f, 0.f);
        rayTracingState().m_causticTargetBoundsMax = Float4(0.f, 0.f, 0.f, 0.f);
        rayTracingState().m_causticRefractiveInstanceCount = 0u;
        return true;
    }

    if(!ensureCausticEmissionTargetBuffer(targetCount))
        return false;

    Core::Buffer* targetBuffer = rayTracingState().m_causticEmissionTargetBuffer.get();
    commandList.setBufferState(targetBuffer, Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(targetBuffer, targets.data(), targets.size() * sizeof(NwbCausticEmissionTargetGpu));
    commandList.setBufferState(targetBuffer, Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();

    StoreFloat(combinedMin, &rayTracingState().m_causticTargetBoundsMin);
    StoreFloat(combinedMax, &rayTracingState().m_causticTargetBoundsMax);
    rayTracingState().m_causticRefractiveInstanceCount = targetCount;

    // No temporal motion-reject / reprojection: the resolve is a purely SPATIAL a-trous wavelet denoise (no history),
    // so a moving (even non-rigidly morphing) caustic is ghost-free by construction -- nothing here needs to track or
    // reseed on refractor motion.
    return true;
}

bool RendererRayTracingSystem::prepareShadowVisibilityResources(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    Core::Alloc::ScratchArena& scratchArena,
    bool& outBackendReady
){
    outBackendReady = false;
    if(!targets.shadowVisibility)
        return false;

    // Caustic emission-target gather (P1) runs once per frame regardless of the shadow backend; it populates the
    // refractive-instance world AABBs the caustic producer will aim at and the count that gates caustic-light
    // assignment in updateSceneShadingBuffer. A failure here is non-fatal to shadows (no consumer reads it yet).
    if(!prepareCausticEmissionTargets(commandList, scratchArena))
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: caustic emission-target gather failed"));

    if(graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct)){
        if(!buildPendingMeshBlas(commandList))
            return true;

        const bool sceneReady = buildSceneTlas(commandList, scratchArena);
        if(!sceneReady)
            return true;

        outBackendReady = ensureShadowPipeline() && ensureShadowBindingSet(targets);

        // Build the hardware caustic producer resources alongside the shadow ones (same TLAS + per-mesh geometry +
        // material context). Non-fatal to shadows: a failure leaves the caustic buffer black (the additive no-op),
        // mirroring the SW-branch prepareGpuBvhCausticResources call below.
        if(!prepareHwCausticResources(targets))
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: hardware caustic producer resource preparation failed"));

        return true;
    }

    // No hardware ray tracing: build/refit the per-mesh software BVHs from the already skinned geometry, then
    // build the per-frame software scene/instance BVH over them before the render pass consumes it.
    if(!buildPendingMeshSwBvh(commandList))
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: software shadow BVH update failed"));
    if(!buildSceneSwBvh(commandList, scratchArena)){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: software shadow scene BVH build failed"));
        return true;
    }

    if(rayTracingState().m_sceneBvhInstanceCount == 0u)
        return true;

    outBackendReady = ensureSwShadowPipeline() && ensureSwShadowBindingSet(targets);

    // Build the software caustic producer + resolve resources alongside the SW shadow resources (same SW scene BVH +
    // per-mesh geometry). Non-fatal to shadows: a failure leaves the caustic buffer black (the additive no-op).
    if(!prepareGpuBvhCausticResources(targets))
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: software caustic producer resource preparation failed"));

    return true;
}

bool RendererRayTracingSystem::createShadowVisibilityTarget(DeferredFrameTargets& targets){
    // The shadow-visibility image is the shared output of the shadow subsystem: both the hardware ray-traced
    // and the software-BVH backends write per-light colored transmittance into it, one Texture2DArray layer
    // per shadow slot (NWB_SCENE_SHADOW_SLOT_COUNT). The deferred lighting pass always samples it, so it is
    // allocated unconditionally and cleared to "all lit" (white) each frame (then overwritten by whichever
    // backend runs) to keep a single binding/shader path regardless of ray-tracing support.
    targets.shadowVisibilityFormat = Core::Format::RGBA16_FLOAT;

    Core::TextureDesc visibilityDesc;
    visibilityDesc
        .setWidth(targets.width)
        .setHeight(targets.height)
        .setArraySize(NWB_SCENE_SHADOW_SLOT_COUNT)
        .setDimension(Core::TextureDimension::Texture2DArray)
        .setFormat(targets.shadowVisibilityFormat)
        .setInUAV(true)
        .setName("engine/shadow/visibility")
    ;
    targets.shadowVisibility = graphics().createTexture(visibilityDesc);
    if(!targets.shadowVisibility){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow visibility target"));
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::createCausticTargets(DeferredFrameTargets& targets){
    // Additive caustic producer targets, the inverted-lifecycle sibling of the shadow visibility target. The
    // deferred lighting pass always samples the resolved irradiance (nwbBxdfCausticIrradiance), so it is allocated
    // unconditionally and cleared to BLACK each frame (the additive identity, vs the shadow buffer's white) to keep
    // a single binding/shader path regardless of whether a caustic producer ran:
    //  - causticIrradiance:  RGBA16F FULL-res, the resolve's UPSAMPLE output the lighting adds pre-tonemap.
    //  - causticAccumulator: R32_UINT FULL-res, one Texture2DArray layer per RGB channel, the fixed-point splat target the
    //                        producer InterlockedAdds into (no float image atomics exist on the backend).
    //  - causticHistory / causticResolveHalf: the two HALF-res RGBA16F ping-pong buffers for the half-res a-trous wavelet
    //                        (the prepare pass writes causticHistory, the wavelet alternates the two, the final lands in
    //                        whichever the upsample reads back to the full-res irradiance). Half-res = 1/4 the pixels.
    targets.causticIrradianceFormat = Core::Format::RGBA16_FLOAT;
    targets.causticAccumulatorFormat = Core::Format::R32_UINT;
    targets.causticHistoryFormat = Core::Format::RGBA16_FLOAT;

    // Round UP so a half-res pixel always covers its 2x2 full-res block even for odd extents.
    const u32 halfWidth = (targets.width + 1u) / 2u;
    const u32 halfHeight = (targets.height + 1u) / 2u;

    Core::TextureDesc irradianceDesc;
    irradianceDesc
        .setWidth(targets.width)
        .setHeight(targets.height)
        .setFormat(targets.causticIrradianceFormat)
        .setInUAV(true)
        .setName("engine/caustic/irradiance")
    ;
    targets.causticIrradiance = graphics().createTexture(irradianceDesc);
    if(!targets.causticIrradiance){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic irradiance target"));
        return false;
    }

    Core::TextureDesc accumulatorDesc;
    accumulatorDesc
        .setWidth(targets.width)
        .setHeight(targets.height)
        .setArraySize(ECSRenderDetail::s_CausticAccumulatorChannelCount)
        .setDimension(Core::TextureDimension::Texture2DArray)
        .setFormat(targets.causticAccumulatorFormat)
        .setInUAV(true)
        .setName("engine/caustic/accumulator")
    ;
    targets.causticAccumulator = graphics().createTexture(accumulatorDesc);
    if(!targets.causticAccumulator){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic accumulator target"));
        return false;
    }

    Core::TextureDesc historyDesc;
    historyDesc
        .setWidth(halfWidth)
        .setHeight(halfHeight)
        .setFormat(targets.causticHistoryFormat)
        .setInUAV(true)
        .setName("engine/caustic/atrous_half_a")
    ;
    targets.causticHistory = graphics().createTexture(historyDesc);
    if(!targets.causticHistory){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic a-trous half-A target"));
        return false;
    }

    Core::TextureDesc halfBDesc;
    halfBDesc
        .setWidth(halfWidth)
        .setHeight(halfHeight)
        .setFormat(targets.causticHistoryFormat)
        .setInUAV(true)
        .setName("engine/caustic/atrous_half_b")
    ;
    targets.causticResolveHalf = graphics().createTexture(halfBDesc);
    if(!targets.causticResolveHalf){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic a-trous half-B target"));
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::renderShadowVisibility(Core::CommandList& commandList, DeferredFrameTargets& targets){
    if(!targets.shadowVisibility)
        return false;
    if(!rayTracingState().m_tlas || !rayTracingState().m_shadowPipeline || !rayTracingState().m_shadowShaderTable || !rayTracingState().m_shadowBindingSet)
        return false;

    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_ShadowVisibility, graphics().getDevice(), commandList);

    // The per-mesh index/attribute byte buffers the any-hit reads were last touched as accel-struct build inputs
    // (positions/indices) or are otherwise resident; move each distinct mesh's index + attribute buffers to
    // ShaderResource for the per-hit transmittance dispatch, alongside the shadow-owned material-context buffers
    // (built + uploaded by buildSceneTlas on the shadow-prepare command list). setResourceStatesForBindingSet
    // then derives the rest (TLAS read, G-buffer SRVs, scene/light buffers, the visibility UAV).
    for(u32 slot = 0u; slot < rayTracingState().m_shadowMeshCount; ++slot){
        commandList.setBufferState(rayTracingState().m_shadowMeshIndexBuffers[slot], Core::ResourceStates::ShaderResource);
        commandList.setBufferState(rayTracingState().m_shadowMeshAttributeBuffers[slot], Core::ResourceStates::ShaderResource);
    }
    commandList.setBufferState(rayTracingState().m_shadowMaterialTypedBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(rayTracingState().m_shadowInstanceBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setResourceStatesForBindingSet(rayTracingState().m_shadowBindingSet.get());
    commandList.commitBarriers();

    Core::RayTracingState rayTracingPassState;
    rayTracingPassState.setShaderTable(rayTracingState().m_shadowShaderTable.get());
    rayTracingPassState.addBindingSet(rayTracingState().m_shadowBindingSet.get());
    commandList.setRayTracingState(rayTracingPassState);

    Core::RayTracingDispatchRaysArguments dispatchArgs;
    dispatchArgs.setDimensions(targets.width, targets.height, 1u);
    commandList.dispatchRays(dispatchArgs);
    return true;
}

void RendererRayTracingSystem::clearShadowVisibility(Core::CommandList& commandList, DeferredFrameTargets& targets){
    if(!targets.shadowVisibility)
        return;

    // White (full transmittance) across every slot layer = fully lit. This is the default the deferred
    // lighting pass samples whenever no shadow backend wrote the image this frame (ray tracing unavailable,
    // no trace-able geometry, or a trace that could not be dispatched), and the value every light without a
    // shadow slot keeps.
    commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.clearTextureFloat(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::Color(1.f, 1.f, 1.f, 1.f));
}

void RendererRayTracingSystem::clearCausticTargets(Core::CommandList& commandList, DeferredFrameTargets& targets){
    if(!targets.causticIrradiance || !targets.causticAccumulator)
        return;

    // Per-frame reset of the additive caustic targets. Black irradiance = the additive identity (the inverse of the
    // shadow buffer's white default): the value the deferred lighting samples whenever no caustic producer ran this
    // frame, so the additive term is a pixel-identical no-op. The accumulators are the per-frame splat target (integer
    // R32_UINT, one layer per RGB channel, cleared to 0). The a-trous scratch (causticHistory) needs NO clear -- every
    // wavelet pass fully overwrites it; the resolve is purely spatial (no persistent state across frames).
    commandList.setTextureState(targets.causticIrradiance.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    commandList.setTextureState(targets.causticAccumulator.get(), ECSRenderDetail::s_CausticAccumulatorSubresources, Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.clearTextureFloat(targets.causticIrradiance.get(), ECSRenderDetail::s_FramebufferSubresources, Core::Color(0.f, 0.f, 0.f, 0.f));
    commandList.clearTextureUInt(targets.causticAccumulator.get(), ECSRenderDetail::s_CausticAccumulatorSubresources, 0u);
}

void RendererRayTracingSystem::dispatchCausticResolve(Core::CommandList& commandList, DeferredFrameTargets& targets){
    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_CausticResolve, graphics().getDevice(), commandList);

    // The producer wrote the accumulator as a UAV; move it to ShaderResource for the PREPARE pass to read (un-scale +
    // area-normalize). setResourceStatesForBindingSet derives the rest each pass (G-buffer SRVs + the ping-pong UAV/SRV
    // roles of the irradiance + scratch buffers as they swap).
    commandList.setTextureState(targets.causticAccumulator.get(), ECSRenderDetail::s_CausticAccumulatorSubresources, Core::ResourceStates::ShaderResource);

    // The resolve runs at HALF resolution (1/4 the pixels); only the final UPSAMPLE dispatches at full res.
    const u32 halfWidth = (targets.width + 1u) / 2u;
    const u32 halfHeight = (targets.height + 1u) / 2u;
    const u32 halfGroupsX = DivideUp(halfWidth, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const u32 halfGroupsY = DivideUp(halfHeight, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const u32 fullGroupsX = DivideUp(targets.width, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));
    const u32 fullGroupsY = DivideUp(targets.height, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE));

    const auto runPass = [&](Core::BindingSet* const bindingSet, const u32 stepWidth, const CausticResolveStage::Enum stage, const u32 groupsX, const u32 groupsY){
        commandList.setResourceStatesForBindingSet(bindingSet);
        commandList.commitBarriers();

        CausticResolvePushConstants resolvePush;
        resolvePush.width = targets.width;
        resolvePush.height = targets.height;
        resolvePush.halfWidth = halfWidth;
        resolvePush.halfHeight = halfHeight;
        resolvePush.causticIntensity = s_CausticIntensity;
        resolvePush.stepWidth = stepWidth;
        resolvePush.stage = static_cast<u32>(stage);

        Core::ComputeState computeState;
        computeState.setPipeline(rayTracingState().m_causticResolvePipeline.get());
        computeState.addBindingSet(bindingSet);
        commandList.setComputeState(computeState);
        commandList.setPushConstants(&resolvePush, sizeof(resolvePush));
        commandList.dispatch(groupsX, groupsY, 1u);
    };

    // PREPARE+DOWNSAMPLE (half-res): sum each half-res pixel's 2x2 accumulator block, un-scale + area-normalize ONCE into
    // the prepared buffer the wavelet reads. The target is seeded by parity so the ping-pong always ENDS in half-B (the
    // upsample input) regardless of PASS_COUNT: an even count starts in half-B, an odd count in half-A.
    const bool prepareToHalfB = (static_cast<u32>(NWB_CAUSTIC_RESOLVE_PASS_COUNT) % 2u) == 0u;
    runPass(
        prepareToHalfB ? rayTracingState().m_causticResolveBindingSetOutputHalfB.get() : rayTracingState().m_causticResolveBindingSetOutputHalfA.get(),
        1u, CausticResolveStage::PrepareDownsample, halfGroupsX, halfGroupsY
    );

    // Half-res edge-avoiding a-trous wavelet passes at a doubling dilation. Each pass writes the buffer NOT holding its
    // input (OutputHalfA reads half-B + writes half-A; OutputHalfB reads half-A + writes half-B). srcIsHalfB tracks where
    // the latest result lives, seeded from the prepare target so after PASS_COUNT passes the final result is in half-B.
    bool srcIsHalfB = prepareToHalfB;
    for(u32 pass = 0u; pass < static_cast<u32>(NWB_CAUSTIC_RESOLVE_PASS_COUNT); ++pass){
        Core::BindingSet* const bindingSet = srcIsHalfB
            ? rayTracingState().m_causticResolveBindingSetOutputHalfA.get()
            : rayTracingState().m_causticResolveBindingSetOutputHalfB.get()
        ;
        runPass(bindingSet, 1u << pass, CausticResolveStage::Wavelet, halfGroupsX, halfGroupsY);
        srcIsHalfB = !srcIsHalfB;
    }

    // UPSAMPLE (full-res): edge-aware bilateral resample of the final half-res caustic (half-B) into the full-res
    // irradiance buffer the deferred lighting adds.
    runPass(rayTracingState().m_causticResolveBindingSetUpsample.get(), 1u, CausticResolveStage::Upsample, fullGroupsX, fullGroupsY);
}

bool RendererRayTracingSystem::renderGpuBvhShadowVisibility(Core::CommandList& commandList, DeferredFrameTargets& targets){
    // Software shadow traversal — the no-hardware-ray-tracing fallback that consumes the per-frame software
    // scene/instance BVH (buildSceneSwBvh) + the per-mesh BVHs. When hardware accel structs are available
    // renderShadowVisibility handles shadows and this path stays idle.
    if(graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct))
        return false;
    if(!targets.shadowVisibility)
        return false;
    // No software scene BVH this frame (no traceable instances) -> the caller clears the mask to all-lit.
    if(!rayTracingState().m_sceneBvhNodeBuffer || rayTracingState().m_sceneBvhInstanceCount == 0u)
        return false;
    if(!rayTracingState().m_swShadowPipeline || !rayTracingState().m_swShadowBindingSet || rayTracingState().m_swShadowMeshCount == 0u)
        return false;

    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_ShadowVisibility, graphics().getDevice(), commandList);

    // The per-mesh BVH node buffers were left in UnorderedAccess by the build pass; move each distinct mesh's
    // node + geometry buffers to ShaderResource for the traversal reads. The scene node buffer was already
    // uploaded as a shader resource by buildSceneSwBvh; setResourceStatesForBindingSet derives the rest
    // (G-buffer SRVs, visibility UAV).
    for(u32 slot = 0u; slot < rayTracingState().m_swShadowMeshCount; ++slot){
        commandList.setBufferState(rayTracingState().m_swShadowMeshNodeBuffers[slot], Core::ResourceStates::ShaderResource);
        commandList.setBufferState(rayTracingState().m_swShadowMeshPositionBuffers[slot], Core::ResourceStates::ShaderResource);
        commandList.setBufferState(rayTracingState().m_swShadowMeshIndexBuffers[slot], Core::ResourceStates::ShaderResource);
        commandList.setBufferState(rayTracingState().m_swShadowMeshAttributeBuffers[slot], Core::ResourceStates::ShaderResource);
    }
    // The per-hit transmittance dispatch reads the shadow-owned material-context buffers (built + uploaded by
    // buildSceneSwBvh on the shadow-prepare command list); move them explicitly alongside the per-mesh geometry
    // before traversal.
    commandList.setBufferState(rayTracingState().m_shadowMaterialTypedBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(rayTracingState().m_shadowInstanceBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setResourceStatesForBindingSet(rayTracingState().m_swShadowBindingSet.get());
    commandList.commitBarriers();

    SwShadowPushConstants pushConstants;
    pushConstants.width = targets.width;
    pushConstants.height = targets.height;
    pushConstants.instanceCount = rayTracingState().m_sceneBvhInstanceCount;

    Core::ComputeState computeState;
    computeState.setPipeline(rayTracingState().m_swShadowPipeline.get());
    computeState.addBindingSet(rayTracingState().m_swShadowBindingSet.get());
    commandList.setComputeState(computeState);
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
    commandList.dispatch(
        DivideUp(targets.width, static_cast<u32>(NWB_SW_SHADOW_GROUP_SIZE)),
        DivideUp(targets.height, static_cast<u32>(NWB_SW_SHADOW_GROUP_SIZE)),
        1u
    );

    if(!rayTracingState().m_swShadowDispatchLogged){
        rayTracingState().m_swShadowDispatchLogged = true;
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: dispatched software shadow traversal ({}x{}, {} instances)")
            , static_cast<u64>(targets.width)
            , static_cast<u64>(targets.height)
            , static_cast<u64>(rayTracingState().m_sceneBvhInstanceCount)
        );
    }
    return true;
}

bool RendererRayTracingSystem::hasCausticWork()const noexcept{
    // The software caustic producer runs only on the no-hardware-ray-tracing path, and only when the scene holds at
    // least one caustic light AND at least one refractive instance (else the black-cleared irradiance buffer is the
    // additive no-op). The SW scene BVH must also have been built (it is the same geometry the photons trace).
    return
        !graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct)
        && rayTracingState().m_causticLightCount > 0u
        && rayTracingState().m_causticRefractiveInstanceCount > 0u
        && rayTracingState().m_sceneBvhInstanceCount > 0u
        && rayTracingState().m_swShadowMeshCount > 0u
        && rayTracingState().m_causticEmissionTargetBuffer
    ;
}

bool RendererRayTracingSystem::prepareGpuBvhCausticResources(DeferredFrameTargets& targets){
    // Build the producer + resolve pipelines and their binding sets, mirroring the SW shadow prepare. Called from
    // the render-prepare path after the SW scene BVH + caustic emission targets are ready. Gated on the prepare-time
    // facts (refractive instances gathered + the SW scene BVH built + the emission-target buffer resident); the
    // caustic-LIGHT gate lives in renderGpuBvhCaustics (the light count is resolved later, in the render pass). This
    // keeps the binding sets ready the same frame the gate first opens. A failure leaves the producer idle (the
    // black-cleared caustic buffer remains the additive no-op).
    if(graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct))
        return true;
    if(
        rayTracingState().m_causticRefractiveInstanceCount == 0u
        || rayTracingState().m_sceneBvhInstanceCount == 0u
        || rayTracingState().m_swShadowMeshCount == 0u
        || !rayTracingState().m_causticEmissionTargetBuffer
    )
        return true;
    if(!targets.causticAccumulator || !targets.causticIrradiance || !drawState().m_meshViewBuffer)
        return true;

    const bool producerReady = ensureSwCausticPipeline() && ensureSwCausticBindingSet(targets);
    const bool resolveReady = ensureCausticResolvePipeline() && ensureCausticResolveBindingSet(targets);
    return producerReady && resolveReady;
}

bool RendererRayTracingSystem::renderGpuBvhCaustics(Core::CommandList& commandList, DeferredFrameTargets& targets){
    // Software caustic photon producer + resolve — the no-hardware-ray-tracing fallback (P3). Dispatched in the
    // SW-fallback branch right after the SW shadow pass and BEFORE deferred lighting (which reads the resolved
    // irradiance). The accumulators were already cleared to black by clearCausticTargets this frame. Runs only when
    // hasCausticWork() holds (>=1 caustic light AND >=1 refractive instance), so an empty buffer = additive no-op.

    if(!hasCausticWork())
        return false;
    if(
        !rayTracingState().m_swCausticPipeline
        || !rayTracingState().m_swCausticBindingSet
        || !rayTracingState().m_causticResolvePipeline
        || !rayTracingState().m_causticResolveBindingSetOutputHalfA
        || !rayTracingState().m_causticResolveBindingSetOutputHalfB
        || !rayTracingState().m_causticResolveBindingSetUpsample
    )
        return false;
    if(!targets.causticAccumulator || !targets.causticIrradiance || !targets.causticHistory || !targets.causticResolveHalf)
        return false;

    {
        Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_CausticPhotons, graphics().getDevice(), commandList);

        // The per-mesh BVH node + geometry buffers were left in ShaderResource by the SW shadow pass; re-assert the
        // state for every traced mesh, plus the shadow-owned material-context buffers (for the per-hit surface
        // dispatch) and the caustic emission-target buffer. setResourceStatesForBindingSet derives the rest (the
        // scene buffers, the view buffer, the G-buffer depth SRV, the accumulator UAV).
        for(u32 slot = 0u; slot < rayTracingState().m_swShadowMeshCount; ++slot){
            commandList.setBufferState(rayTracingState().m_swShadowMeshNodeBuffers[slot], Core::ResourceStates::ShaderResource);
            commandList.setBufferState(rayTracingState().m_swShadowMeshPositionBuffers[slot], Core::ResourceStates::ShaderResource);
            commandList.setBufferState(rayTracingState().m_swShadowMeshIndexBuffers[slot], Core::ResourceStates::ShaderResource);
            commandList.setBufferState(rayTracingState().m_swShadowMeshAttributeBuffers[slot], Core::ResourceStates::ShaderResource);
        }
        commandList.setBufferState(rayTracingState().m_shadowMaterialTypedBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setBufferState(rayTracingState().m_shadowInstanceBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setBufferState(rayTracingState().m_causticEmissionTargetBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setResourceStatesForBindingSet(rayTracingState().m_swCausticBindingSet.get());
        commandList.commitBarriers();

        CausticPhotonPushConstants pushConstants;
        pushConstants.width = targets.width;
        pushConstants.height = targets.height;
        pushConstants.instanceCount = rayTracingState().m_sceneBvhInstanceCount;
        pushConstants.photonCount = s_CausticSwPhotonCount;
        pushConstants.emissionTargetCount = rayTracingState().m_causticRefractiveInstanceCount;
        pushConstants.gridSide = s_CausticSwPhotonGridSide;

        Core::ComputeState computeState;
        computeState.setPipeline(rayTracingState().m_swCausticPipeline.get());
        computeState.addBindingSet(rayTracingState().m_swCausticBindingSet.get());
        commandList.setComputeState(computeState);
        commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
        commandList.dispatch(DivideUp(s_CausticSwPhotonCount, static_cast<u32>(NWB_CAUSTIC_SW_GROUP_SIZE)), 1u, 1u);
    }

    dispatchCausticResolve(commandList, targets);

    if(!rayTracingState().m_swCausticDispatchLogged){
        rayTracingState().m_swCausticDispatchLogged = true;
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: dispatched software caustic producer ({} photons, {} caustic lights, {} refractive instances)")
            , static_cast<u64>(s_CausticSwPhotonCount)
            , static_cast<u64>(rayTracingState().m_causticLightCount)
            , static_cast<u64>(rayTracingState().m_causticRefractiveInstanceCount)
        );
    }
    return true;
}

bool RendererRayTracingSystem::buildMeshBlas(Core::CommandList& commandList, MeshResources& meshResources){
    if(!meshResources.positionBuffer || !meshResources.triangleIndexBuffer)
        return false;

    const Core::BufferDesc& positionDesc = meshResources.positionBuffer->getDescription();
    if(positionDesc.structStride == 0u || meshResources.meshletPrimitiveIndexCount == 0u)
        return false;

    const u32 vertexStride = static_cast<u32>(positionDesc.structStride);
    const u32 vertexCount = static_cast<u32>(positionDesc.byteSize / positionDesc.structStride);

    Core::RayTracingGeometryTriangles triangles;
    triangles
        .setVertexBuffer(meshResources.positionBuffer.get())
        .setVertexFormat(Core::Format::RGB32_FLOAT)
        .setVertexStride(vertexStride)
        .setVertexCount(vertexCount)
        .setIndexBuffer(meshResources.triangleIndexBuffer.get())
        .setIndexFormat(Core::Format::R32_UINT)
        .setIndexCount(meshResources.meshletPrimitiveIndexCount)
    ;

    // Colored shadows need the any-hit to fire on every occluder, so the shadow geometry is NON-opaque (an
    // Opaque flag would skip the any-hit). NoDuplicateAnyHitInvocation keeps it firing exactly once per
    // primitive so the transmittance product is not double-applied.
    Core::RayTracingGeometryDesc geometry;
    geometry
        .setTriangles(triangles)
        .setFlags(Core::RayTracingGeometryFlags::NoDuplicateAnyHitInvocation)
    ;

    // Runtime (skinned) meshes keep a single resident BLAS built with AllowUpdate and refit it in
    // place from the freshly skinned positions each frame, forcing a full rebuild every
    // s_BlasMaxRefitsBeforeRebuild frames to restore BVH quality. Static meshes build once.
    Core::RayTracingAccelStructBuildFlags::Mask buildFlags = Core::RayTracingAccelStructBuildFlags::PreferFastTrace;
    if(meshResources.runtimeMesh)
        buildFlags |= Core::RayTracingAccelStructBuildFlags::AllowUpdate;

    const bool firstBuild = !meshResources.blas;
    if(firstBuild){
        Core::RayTracingAccelStructDesc accelStructDesc(arena());
        accelStructDesc.addBottomLevelGeometry(geometry);
        accelStructDesc.setBuildFlags(buildFlags);
        accelStructDesc.setDebugName(DeriveName(meshResources.meshName, AStringView(":blas")));

        auto* device = graphics().getDevice();
        Core::RayTracingAccelStructHandle blas = device->createAccelStruct(accelStructDesc);
        if(!blas){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BLAS for mesh '{}'")
                , StringConvert(meshResources.meshName.c_str())
            );
            return false;
        }
        meshResources.blas = Move(blas);
        meshResources.blasRefitsSinceRebuild = 0u;
    }

    const bool performRefit =
        meshResources.runtimeMesh
        && !firstBuild
        && meshResources.blasRefitsSinceRebuild < s_BlasMaxRefitsBeforeRebuild
    ;
    if(performRefit)
        buildFlags |= Core::RayTracingAccelStructBuildFlags::PerformUpdate;

    commandList.setBufferState(meshResources.positionBuffer.get(), Core::ResourceStates::AccelStructBuildInput);
    commandList.setBufferState(meshResources.triangleIndexBuffer.get(), Core::ResourceStates::AccelStructBuildInput);
    commandList.commitBarriers();

    commandList.buildBottomLevelAccelStruct(meshResources.blas.get(), &geometry, 1u, buildFlags);

    meshResources.blasRefitsSinceRebuild = performRefit ? (meshResources.blasRefitsSinceRebuild + 1u) : 0u;

    if(firstBuild){
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: built BLAS for mesh '{}' (runtime {}, {} vertices, {} indices)")
            , StringConvert(meshResources.meshName.c_str())
            , meshResources.runtimeMesh
            , static_cast<u64>(vertexCount)
            , static_cast<u64>(meshResources.meshletPrimitiveIndexCount)
        );
    }
    return true;
}

bool RendererRayTracingSystem::ensureShadowPipeline(){
    if(rayTracingState().m_shadowPipeline)
        return true;
    if(rayTracingState().m_shadowPipelineFailed)
        return false;
    if(!graphics().queryFeatureSupport(Core::Feature::RayTracingPipeline)){
        rayTracingState().m_shadowPipelineFailed = true;
        return false;
    }

    auto* device = graphics().getDevice();

    if(!rayTracingState().m_shadowBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::AllRayTracing);
        layoutDesc.addItem(Core::BindingLayoutItem::RayTracingAccelStruct(NWB_SHADOW_RT_BINDING_TLAS, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_RT_BINDING_GBUFFER_WORLD_POSITION, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_RT_BINDING_GBUFFER_NORMAL, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_RT_BINDING_GBUFFER_DEPTH, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_SHADOW_RT_BINDING_SCENE_SHADING, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SHADOW_RT_BINDING_LIGHT_LIST, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_SHADOW_RT_BINDING_VISIBILITY_OUTPUT, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SHADOW_RT_BINDING_INSTANCE_MATERIAL, 1));
        // Per-mesh descriptor arrays (index + attribute byte buffers) the any-hit indexes by material.meshSlot,
        // plus the shared material-constants context buffers the per-hit transmittance dispatch reads. The
        // bounded SRV arrays mirror the software traversal's compute path; this is the HW-pipeline feasibility
        // probe -- createBindingLayout / createRayTracingPipeline must accept count>1 SRV arrays here.
        layoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_SRV(NWB_SHADOW_RT_BINDING_MESH_INDICES, NWB_SHADOW_RT_MAX_MESHES));
        layoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_SRV(NWB_SHADOW_RT_BINDING_MESH_ATTRIBUTES, NWB_SHADOW_RT_MAX_MESHES));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SHADOW_RT_BINDING_MATERIAL_TYPED, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SHADOW_RT_BINDING_MESH_INSTANCES, 1));

        rayTracingState().m_shadowBindingLayout = device->createBindingLayout(layoutDesc);
        if(!rayTracingState().m_shadowBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow binding layout"));
            rayTracingState().m_shadowPipelineFailed = true;
            return false;
        }
    }

    Core::ShaderHandle raygenShader;
    Core::ShaderHandle missShader;
    Core::ShaderHandle anyHitShader;
    if(
        !m_renderer.shaderSystem().loadShader(raygenShader, AssetsGraphicsShadow::s_RaygenShaderName, Core::ShaderArchive::s_DefaultVariant, Core::ShaderType::RayGeneration, "ECSRender_ShadowRaygen")
        || !m_renderer.shaderSystem().loadShader(missShader, AssetsGraphicsShadow::s_MissShaderName, Core::ShaderArchive::s_DefaultVariant, Core::ShaderType::Miss, "ECSRender_ShadowMiss")
        || !m_renderer.shaderSystem().loadShader(anyHitShader, AssetsGraphicsShadow::s_AnyHitShaderName, Core::ShaderArchive::s_DefaultVariant, Core::ShaderType::AnyHit, "ECSRender_ShadowAnyHit")
    ){
        rayTracingState().m_shadowPipelineFailed = true;
        return false;
    }

    Core::RayTracingPipelineDesc pipelineDesc(arena());
    // Payload = float3 transmittance (12 bytes, rounded to 16). The any-hit accumulates colored transmittance.
    pipelineDesc.setMaxPayloadSize(static_cast<u32>(sizeof(f32) * 4u));
    pipelineDesc.setMaxRecursionDepth(1u);
    pipelineDesc.addBindingLayout(rayTracingState().m_shadowBindingLayout);

    Core::RayTracingPipelineShaderDesc raygenDesc(arena());
    raygenDesc.setShader(raygenShader).setExportName("ShadowRayGen");
    pipelineDesc.addShader(raygenDesc);

    Core::RayTracingPipelineShaderDesc missDesc(arena());
    missDesc.setShader(missShader).setExportName("ShadowMiss");
    pipelineDesc.addShader(missDesc);

    Core::RayTracingPipelineHitGroupDesc hitGroupDesc(arena());
    hitGroupDesc.setAnyHitShader(anyHitShader).setExportName("ShadowHitGroup");
    pipelineDesc.addHitGroup(hitGroupDesc);

    rayTracingState().m_shadowPipeline = device->createRayTracingPipeline(pipelineDesc);
    if(!rayTracingState().m_shadowPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create RT shadow pipeline"));
        rayTracingState().m_shadowPipelineFailed = true;
        return false;
    }

    Core::RayTracingShaderTableHandle shaderTable = rayTracingState().m_shadowPipeline->createShaderTable();
    if(!shaderTable){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create RT shadow shader table"));
        rayTracingState().m_shadowPipelineFailed = true;
        return false;
    }
    shaderTable->setRayGenerationShader("ShadowRayGen");
    shaderTable->addMissShader("ShadowMiss");
    shaderTable->addHitGroup("ShadowHitGroup");
    rayTracingState().m_shadowShaderTable = Move(shaderTable);

    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created RT shadow pipeline + shader table"));
    return true;
}

bool RendererRayTracingSystem::ensureShadowBindingSet(DeferredFrameTargets& targets){
    NWB_ASSERT(rayTracingState().m_shadowBindingLayout);
    NWB_ASSERT(rayTracingState().m_tlas);
    NWB_ASSERT(rayTracingState().m_shadowInstanceMaterialBuffer);
    NWB_ASSERT(rayTracingState().m_shadowMeshCount > 0u);
    NWB_ASSERT(targets.shadowVisibility);
    NWB_ASSERT(deferredState().m_sceneShadingBuffer);
    NWB_ASSERT(deferredState().m_lightBuffer);
    // The material-constants context the per-hit transmittance dispatch reads (g_NwbMaterialTypedWords +
    // g_NwbMeshInstances) is the SHADOW-OWNED combined pair built by buildSceneTlas over ALL gathered occluders,
    // NOT the draw pass's buffers (those hold only the opaque set at trace time, so the transparent occluders'
    // tint/constants would be absent). buildSceneTlas uploads both before this binding set is created.
    NWB_ASSERT(rayTracingState().m_shadowMaterialTypedBuffer);
    NWB_ASSERT(rayTracingState().m_shadowInstanceBuffer);

    // Rebuild when any binding input that can change without a full invalidate changes: the TLAS (recreated when
    // the live instance count outgrows its capacity), the instance-material table, the distinct-mesh count (the
    // per-mesh descriptor arrays repopulate when the set of traced meshes changes), and the shadow-owned
    // material-context buffers (recreated when they outgrow their capacity). A resize resets the set via
    // resetDeferredFrameTargets, so the target inputs need no separate key.
    const Core::RayTracingAccelStruct* tlas = rayTracingState().m_tlas.get();
    const Core::Buffer* instanceMaterialBuffer = rayTracingState().m_shadowInstanceMaterialBuffer.get();
    Core::Buffer* materialTypedBuffer = rayTracingState().m_shadowMaterialTypedBuffer.get();
    Core::Buffer* meshInstanceBuffer = rayTracingState().m_shadowInstanceBuffer.get();
    const u32 meshCount = rayTracingState().m_shadowMeshCount;
    if(
        rayTracingState().m_shadowBindingSet
        && rayTracingState().m_shadowBindingSetTlas == tlas
        && rayTracingState().m_shadowBindingSetInstanceMaterial == instanceMaterialBuffer
        && rayTracingState().m_shadowBindingSetMaterialTyped == materialTypedBuffer
        && rayTracingState().m_shadowBindingSetMeshInstances == meshInstanceBuffer
        && rayTracingState().m_shadowBindingSetMeshCount == meshCount
    )
        return true;

    Core::BindingSetDesc bindingSetDesc(arena());
    bindingSetDesc.addItem(Core::BindingSetItem::RayTracingAccelStruct(NWB_SHADOW_RT_BINDING_TLAS, rayTracingState().m_tlas.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SHADOW_RT_BINDING_GBUFFER_WORLD_POSITION,
        targets.worldPosition.get(),
        targets.worldPositionFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SHADOW_RT_BINDING_GBUFFER_NORMAL,
        targets.normal.get(),
        targets.normalFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SHADOW_RT_BINDING_GBUFFER_DEPTH,
        targets.depth.get(),
        targets.depthFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_SHADOW_RT_BINDING_SCENE_SHADING, deferredState().m_sceneShadingBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SHADOW_RT_BINDING_LIGHT_LIST, deferredState().m_lightBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_SHADOW_RT_BINDING_VISIBILITY_OUTPUT,
        targets.shadowVisibility.get(),
        targets.shadowVisibilityFormat,
        ECSRenderDetail::s_ShadowVisibilitySubresources,
        Core::TextureDimension::Texture2DArray
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SHADOW_RT_BINDING_INSTANCE_MATERIAL, rayTracingState().m_shadowInstanceMaterialBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SHADOW_RT_BINDING_MATERIAL_TYPED, materialTypedBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SHADOW_RT_BINDING_MESH_INSTANCES, meshInstanceBuffer));

    // Per-mesh descriptor arrays: bind every slot (the any-hit only indexes meshSlot < meshCount). Unused tail
    // slots are padded with the last real mesh so a non-bindless array has no unbound descriptors.
    for(u32 slot = 0u; slot < NWB_SHADOW_RT_MAX_MESHES; ++slot){
        const u32 source = (slot < meshCount) ? slot : (meshCount - 1u);
        bindingSetDesc.addItem(Core::BindingSetItem::RawBuffer_SRV(NWB_SHADOW_RT_BINDING_MESH_INDICES, rayTracingState().m_shadowMeshIndexBuffers[source]).setArrayElement(slot));
        bindingSetDesc.addItem(Core::BindingSetItem::RawBuffer_SRV(NWB_SHADOW_RT_BINDING_MESH_ATTRIBUTES, rayTracingState().m_shadowMeshAttributeBuffers[source]).setArrayElement(slot));
    }

    auto* device = graphics().getDevice();
    rayTracingState().m_shadowBindingSet = device->createBindingSet(bindingSetDesc, rayTracingState().m_shadowBindingLayout);
    if(!rayTracingState().m_shadowBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow binding set"));
        rayTracingState().m_shadowBindingSetTlas = nullptr;
        rayTracingState().m_shadowBindingSetInstanceMaterial = nullptr;
        rayTracingState().m_shadowBindingSetMaterialTyped = nullptr;
        rayTracingState().m_shadowBindingSetMeshInstances = nullptr;
        rayTracingState().m_shadowBindingSetMeshCount = 0u;
        return false;
    }
    rayTracingState().m_shadowBindingSetTlas = tlas;
    rayTracingState().m_shadowBindingSetInstanceMaterial = instanceMaterialBuffer;
    rayTracingState().m_shadowBindingSetMaterialTyped = materialTypedBuffer;
    rayTracingState().m_shadowBindingSetMeshInstances = meshInstanceBuffer;
    rayTracingState().m_shadowBindingSetMeshCount = meshCount;
    return true;
}

bool RendererRayTracingSystem::ensureSwShadowPipeline(){
    if(rayTracingState().m_swShadowPipeline)
        return true;
    if(rayTracingState().m_swShadowPipelineFailed)
        return false;

    auto* device = graphics().getDevice();

    if(!rayTracingState().m_swShadowBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SW_SHADOW_BINDING_GBUFFER_WORLD_POSITION, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SW_SHADOW_BINDING_GBUFFER_NORMAL, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SW_SHADOW_BINDING_GBUFFER_DEPTH, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_SW_SHADOW_BINDING_SCENE_SHADING, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SW_SHADOW_BINDING_LIGHT_LIST, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SW_SHADOW_BINDING_SCENE_NODES, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_SW_SHADOW_BINDING_VISIBILITY_OUTPUT, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SW_SHADOW_BINDING_SCENE_INSTANCES, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SW_SHADOW_BINDING_MESH_NODES, NWB_SW_SHADOW_MAX_MESHES));
        layoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_SRV(NWB_SW_SHADOW_BINDING_MESH_POSITIONS, NWB_SW_SHADOW_MAX_MESHES));
        layoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_SRV(NWB_SW_SHADOW_BINDING_MESH_INDICES, NWB_SW_SHADOW_MAX_MESHES));
        layoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_SRV(NWB_SW_SHADOW_BINDING_MESH_ATTRIBUTES, NWB_SW_SHADOW_MAX_MESHES));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SW_SHADOW_BINDING_INSTANCE_MATERIAL, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SW_SHADOW_BINDING_MATERIAL_TYPED, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SW_SHADOW_BINDING_MESH_INSTANCES, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SwShadowPushConstants)));

        rayTracingState().m_swShadowBindingLayout = device->createBindingLayout(layoutDesc);
        if(!rayTracingState().m_swShadowBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create software shadow binding layout"));
            rayTracingState().m_swShadowPipelineFailed = true;
            return false;
        }
    }

    if(!m_renderer.shaderSystem().loadShader(
        rayTracingState().m_swShadowShader,
        AssetsGraphicsShadow::s_SwTraversalShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_SwShadowTraversal"
    )){
        rayTracingState().m_swShadowPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(rayTracingState().m_swShadowShader)
        .addBindingLayout(rayTracingState().m_swShadowBindingLayout)
    ;
    rayTracingState().m_swShadowPipeline = device->createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_swShadowPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create software shadow compute pipeline"));
        rayTracingState().m_swShadowPipelineFailed = true;
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureSwShadowBindingSet(DeferredFrameTargets& targets){
    NWB_ASSERT(rayTracingState().m_swShadowBindingLayout);
    NWB_ASSERT(rayTracingState().m_sceneBvhNodeBuffer);
    NWB_ASSERT(rayTracingState().m_sceneInstanceBuffer);
    NWB_ASSERT(rayTracingState().m_shadowInstanceMaterialBuffer);
    NWB_ASSERT(rayTracingState().m_swShadowMeshCount > 0u);
    NWB_ASSERT(targets.shadowVisibility);
    NWB_ASSERT(deferredState().m_sceneShadingBuffer);
    NWB_ASSERT(deferredState().m_lightBuffer);
    // The material-constants context the per-hit transmittance dispatch reads (g_NwbMaterialTypedWords +
    // g_NwbMeshInstances) is the SHADOW-OWNED combined pair built by buildSceneSwBvh over ALL gathered occluders,
    // NOT the draw pass's buffers (those hold only one transparency class at trace time). buildSceneSwBvh uploads
    // both before this binding set is created.
    NWB_ASSERT(rayTracingState().m_shadowMaterialTypedBuffer);
    NWB_ASSERT(rayTracingState().m_shadowInstanceBuffer);

    // Rebuild when any binding input that can change without a full invalidate changes: the scene node /
    // instance buffers (recreated when they outgrow their capacity), the visibility target (recreated on
    // resize, which also resets this set via resetDeferredFrameTargets), the distinct-mesh count (the per-mesh
    // descriptor arrays repopulate when the set of traced meshes changes), and the shadow-owned material-context
    // buffers (recreated when they outgrow their capacity).
    Core::Buffer* sceneNodeBuffer = rayTracingState().m_sceneBvhNodeBuffer.get();
    Core::Buffer* instanceBuffer = rayTracingState().m_sceneInstanceBuffer.get();
    Core::Buffer* instanceMaterialBuffer = rayTracingState().m_shadowInstanceMaterialBuffer.get();
    Core::Buffer* materialTypedBuffer = rayTracingState().m_shadowMaterialTypedBuffer.get();
    Core::Buffer* meshInstanceBuffer = rayTracingState().m_shadowInstanceBuffer.get();
    const Core::Texture* visibilityTarget = targets.shadowVisibility.get();
    const u32 meshCount = rayTracingState().m_swShadowMeshCount;
    if(
        rayTracingState().m_swShadowBindingSet
        && rayTracingState().m_swShadowBindingSetSceneNodes == sceneNodeBuffer
        && rayTracingState().m_swShadowBindingSetInstances == instanceBuffer
        && rayTracingState().m_swShadowBindingSetInstanceMaterial == instanceMaterialBuffer
        && rayTracingState().m_swShadowBindingSetMaterialTyped == materialTypedBuffer
        && rayTracingState().m_swShadowBindingSetMeshInstances == meshInstanceBuffer
        && rayTracingState().m_swShadowBindingSetVisibility == visibilityTarget
        && rayTracingState().m_swShadowBindingSetMeshCount == meshCount
    )
        return true;

    Core::BindingSetDesc bindingSetDesc(arena());
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SW_SHADOW_BINDING_GBUFFER_WORLD_POSITION,
        targets.worldPosition.get(),
        targets.worldPositionFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SW_SHADOW_BINDING_GBUFFER_NORMAL,
        targets.normal.get(),
        targets.normalFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SW_SHADOW_BINDING_GBUFFER_DEPTH,
        targets.depth.get(),
        targets.depthFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_SW_SHADOW_BINDING_SCENE_SHADING, deferredState().m_sceneShadingBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SW_SHADOW_BINDING_LIGHT_LIST, deferredState().m_lightBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SW_SHADOW_BINDING_SCENE_NODES, sceneNodeBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_SW_SHADOW_BINDING_VISIBILITY_OUTPUT,
        targets.shadowVisibility.get(),
        targets.shadowVisibilityFormat,
        ECSRenderDetail::s_ShadowVisibilitySubresources,
        Core::TextureDimension::Texture2DArray
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SW_SHADOW_BINDING_SCENE_INSTANCES, instanceBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SW_SHADOW_BINDING_INSTANCE_MATERIAL, instanceMaterialBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SW_SHADOW_BINDING_MATERIAL_TYPED, materialTypedBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SW_SHADOW_BINDING_MESH_INSTANCES, meshInstanceBuffer));

    // Per-mesh descriptor arrays: bind every slot (the shader only indexes meshIndex < meshCount). Unused
    // tail slots are padded with the last real mesh so a non-bindless array has no unbound descriptors.
    for(u32 slot = 0u; slot < NWB_SW_SHADOW_MAX_MESHES; ++slot){
        const u32 source = (slot < meshCount) ? slot : (meshCount - 1u);
        bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SW_SHADOW_BINDING_MESH_NODES, rayTracingState().m_swShadowMeshNodeBuffers[source]).setArrayElement(slot));
        bindingSetDesc.addItem(Core::BindingSetItem::RawBuffer_SRV(NWB_SW_SHADOW_BINDING_MESH_POSITIONS, rayTracingState().m_swShadowMeshPositionBuffers[source]).setArrayElement(slot));
        bindingSetDesc.addItem(Core::BindingSetItem::RawBuffer_SRV(NWB_SW_SHADOW_BINDING_MESH_INDICES, rayTracingState().m_swShadowMeshIndexBuffers[source]).setArrayElement(slot));
        bindingSetDesc.addItem(Core::BindingSetItem::RawBuffer_SRV(NWB_SW_SHADOW_BINDING_MESH_ATTRIBUTES, rayTracingState().m_swShadowMeshAttributeBuffers[source]).setArrayElement(slot));
    }

    auto* device = graphics().getDevice();
    rayTracingState().m_swShadowBindingSet = device->createBindingSet(bindingSetDesc, rayTracingState().m_swShadowBindingLayout);
    if(!rayTracingState().m_swShadowBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create software shadow binding set"));
        rayTracingState().m_swShadowBindingSetSceneNodes = nullptr;
        rayTracingState().m_swShadowBindingSetInstances = nullptr;
        rayTracingState().m_swShadowBindingSetInstanceMaterial = nullptr;
        rayTracingState().m_swShadowBindingSetMaterialTyped = nullptr;
        rayTracingState().m_swShadowBindingSetMeshInstances = nullptr;
        rayTracingState().m_swShadowBindingSetVisibility = nullptr;
        rayTracingState().m_swShadowBindingSetMeshCount = 0u;
        return false;
    }
    rayTracingState().m_swShadowBindingSetSceneNodes = sceneNodeBuffer;
    rayTracingState().m_swShadowBindingSetInstances = instanceBuffer;
    rayTracingState().m_swShadowBindingSetInstanceMaterial = instanceMaterialBuffer;
    rayTracingState().m_swShadowBindingSetMaterialTyped = materialTypedBuffer;
    rayTracingState().m_swShadowBindingSetMeshInstances = meshInstanceBuffer;
    rayTracingState().m_swShadowBindingSetVisibility = visibilityTarget;
    rayTracingState().m_swShadowBindingSetMeshCount = meshCount;
    return true;
}

bool RendererRayTracingSystem::ensureSwCausticPipeline(){
    if(rayTracingState().m_swCausticPipeline)
        return true;
    if(rayTracingState().m_swCausticPipelineFailed)
        return false;

    auto* device = graphics().getDevice();

    if(!rayTracingState().m_swCausticBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_CAUSTIC_SW_BINDING_SCENE_SHADING, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_CAUSTIC_SW_BINDING_LIGHT_LIST, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_CAUSTIC_SW_BINDING_SCENE_NODES, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_CAUSTIC_SW_BINDING_SCENE_INSTANCES, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_CAUSTIC_SW_BINDING_INSTANCE_MATERIAL, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_CAUSTIC_SW_BINDING_MESH_NODES, NWB_CAUSTIC_SW_MAX_MESHES));
        layoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_SRV(NWB_CAUSTIC_SW_BINDING_MESH_POSITIONS, NWB_CAUSTIC_SW_MAX_MESHES));
        layoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_SRV(NWB_CAUSTIC_SW_BINDING_MESH_INDICES, NWB_CAUSTIC_SW_MAX_MESHES));
        layoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_SRV(NWB_CAUSTIC_SW_BINDING_MESH_ATTRIBUTES, NWB_CAUSTIC_SW_MAX_MESHES));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_CAUSTIC_SW_BINDING_MATERIAL_TYPED, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_CAUSTIC_SW_BINDING_MESH_INSTANCES, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_CAUSTIC_SW_BINDING_EMISSION_TARGETS, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_CAUSTIC_SW_BINDING_VIEW, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_CAUSTIC_SW_BINDING_GBUFFER_DEPTH, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_CAUSTIC_SW_BINDING_GBUFFER_WORLD_POSITION, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_CAUSTIC_SW_BINDING_ACCUMULATOR, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(CausticPhotonPushConstants)));

        rayTracingState().m_swCausticBindingLayout = device->createBindingLayout(layoutDesc);
        if(!rayTracingState().m_swCausticBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create software caustic binding layout"));
            rayTracingState().m_swCausticPipelineFailed = true;
            return false;
        }
    }

    if(!m_renderer.shaderSystem().loadShader(
        rayTracingState().m_swCausticShader,
        AssetsGraphicsCaustic::s_SwPhotonShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_SwCausticPhotons"
    )){
        rayTracingState().m_swCausticPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(rayTracingState().m_swCausticShader)
        .addBindingLayout(rayTracingState().m_swCausticBindingLayout)
    ;
    rayTracingState().m_swCausticPipeline = device->createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_swCausticPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create software caustic compute pipeline"));
        rayTracingState().m_swCausticPipelineFailed = true;
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureSwCausticBindingSet(DeferredFrameTargets& targets){
    NWB_ASSERT(rayTracingState().m_swCausticBindingLayout);
    NWB_ASSERT(rayTracingState().m_sceneBvhNodeBuffer);
    NWB_ASSERT(rayTracingState().m_sceneInstanceBuffer);
    NWB_ASSERT(rayTracingState().m_shadowInstanceMaterialBuffer);
    NWB_ASSERT(rayTracingState().m_swShadowMeshCount > 0u);
    NWB_ASSERT(rayTracingState().m_causticEmissionTargetBuffer);
    NWB_ASSERT(targets.causticAccumulator);
    NWB_ASSERT(targets.depth);
    NWB_ASSERT(targets.worldPosition);
    NWB_ASSERT(deferredState().m_sceneShadingBuffer);
    NWB_ASSERT(deferredState().m_lightBuffer);
    NWB_ASSERT(drawState().m_meshViewBuffer);
    NWB_ASSERT(rayTracingState().m_shadowMaterialTypedBuffer);
    NWB_ASSERT(rayTracingState().m_shadowInstanceBuffer);

    Core::Buffer* sceneNodeBuffer = rayTracingState().m_sceneBvhNodeBuffer.get();
    Core::Buffer* instanceBuffer = rayTracingState().m_sceneInstanceBuffer.get();
    Core::Buffer* instanceMaterialBuffer = rayTracingState().m_shadowInstanceMaterialBuffer.get();
    Core::Buffer* materialTypedBuffer = rayTracingState().m_shadowMaterialTypedBuffer.get();
    Core::Buffer* meshInstanceBuffer = rayTracingState().m_shadowInstanceBuffer.get();
    Core::Buffer* emissionTargetBuffer = rayTracingState().m_causticEmissionTargetBuffer.get();
    Core::Buffer* viewBuffer = drawState().m_meshViewBuffer.get();
    const Core::Texture* depthTarget = targets.depth.get();
    const Core::Texture* worldPositionTarget = targets.worldPosition.get();
    const Core::Texture* accumulatorTarget = targets.causticAccumulator.get();
    const u32 meshCount = rayTracingState().m_swShadowMeshCount;
    if(
        rayTracingState().m_swCausticBindingSet
        && rayTracingState().m_swCausticBindingSetSceneNodes == sceneNodeBuffer
        && rayTracingState().m_swCausticBindingSetInstances == instanceBuffer
        && rayTracingState().m_swCausticBindingSetInstanceMaterial == instanceMaterialBuffer
        && rayTracingState().m_swCausticBindingSetMaterialTyped == materialTypedBuffer
        && rayTracingState().m_swCausticBindingSetMeshInstances == meshInstanceBuffer
        && rayTracingState().m_swCausticBindingSetEmissionTargets == emissionTargetBuffer
        && rayTracingState().m_swCausticBindingSetView == viewBuffer
        && rayTracingState().m_swCausticBindingSetDepth == depthTarget
        && rayTracingState().m_swCausticBindingSetWorldPosition == worldPositionTarget
        && rayTracingState().m_swCausticBindingSetAccumulator == accumulatorTarget
        && rayTracingState().m_swCausticBindingSetMeshCount == meshCount
    )
        return true;

    Core::BindingSetDesc bindingSetDesc(arena());
    bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_CAUSTIC_SW_BINDING_SCENE_SHADING, deferredState().m_sceneShadingBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_CAUSTIC_SW_BINDING_LIGHT_LIST, deferredState().m_lightBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_CAUSTIC_SW_BINDING_SCENE_NODES, sceneNodeBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_CAUSTIC_SW_BINDING_SCENE_INSTANCES, instanceBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_CAUSTIC_SW_BINDING_INSTANCE_MATERIAL, instanceMaterialBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_CAUSTIC_SW_BINDING_MATERIAL_TYPED, materialTypedBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_CAUSTIC_SW_BINDING_MESH_INSTANCES, meshInstanceBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_CAUSTIC_SW_BINDING_EMISSION_TARGETS, emissionTargetBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_CAUSTIC_SW_BINDING_VIEW, viewBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_CAUSTIC_SW_BINDING_GBUFFER_DEPTH,
        targets.depth.get(),
        targets.depthFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_CAUSTIC_SW_BINDING_GBUFFER_WORLD_POSITION,
        targets.worldPosition.get(),
        targets.worldPositionFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_CAUSTIC_SW_BINDING_ACCUMULATOR,
        targets.causticAccumulator.get(),
        targets.causticAccumulatorFormat,
        ECSRenderDetail::s_CausticAccumulatorSubresources,
        Core::TextureDimension::Texture2DArray
    ));

    // Per-mesh descriptor arrays: bind every slot (the shader only indexes meshIndex < meshCount). Unused tail
    // slots are padded with the last real mesh, exactly as the SW shadow set does. The caustic producer reuses the
    // SAME per-mesh buffers the SW shadow scene BVH populated (meshSlot indices match), so no separate gather.
    for(u32 slot = 0u; slot < NWB_CAUSTIC_SW_MAX_MESHES; ++slot){
        const u32 source = (slot < meshCount) ? slot : (meshCount - 1u);
        bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_CAUSTIC_SW_BINDING_MESH_NODES, rayTracingState().m_swShadowMeshNodeBuffers[source]).setArrayElement(slot));
        bindingSetDesc.addItem(Core::BindingSetItem::RawBuffer_SRV(NWB_CAUSTIC_SW_BINDING_MESH_POSITIONS, rayTracingState().m_swShadowMeshPositionBuffers[source]).setArrayElement(slot));
        bindingSetDesc.addItem(Core::BindingSetItem::RawBuffer_SRV(NWB_CAUSTIC_SW_BINDING_MESH_INDICES, rayTracingState().m_swShadowMeshIndexBuffers[source]).setArrayElement(slot));
        bindingSetDesc.addItem(Core::BindingSetItem::RawBuffer_SRV(NWB_CAUSTIC_SW_BINDING_MESH_ATTRIBUTES, rayTracingState().m_swShadowMeshAttributeBuffers[source]).setArrayElement(slot));
    }

    auto* device = graphics().getDevice();
    rayTracingState().m_swCausticBindingSet = device->createBindingSet(bindingSetDesc, rayTracingState().m_swCausticBindingLayout);
    if(!rayTracingState().m_swCausticBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create software caustic binding set"));
        rayTracingState().m_swCausticBindingSetSceneNodes = nullptr;
        rayTracingState().m_swCausticBindingSetInstances = nullptr;
        rayTracingState().m_swCausticBindingSetInstanceMaterial = nullptr;
        rayTracingState().m_swCausticBindingSetMaterialTyped = nullptr;
        rayTracingState().m_swCausticBindingSetMeshInstances = nullptr;
        rayTracingState().m_swCausticBindingSetEmissionTargets = nullptr;
        rayTracingState().m_swCausticBindingSetView = nullptr;
        rayTracingState().m_swCausticBindingSetDepth = nullptr;
        rayTracingState().m_swCausticBindingSetWorldPosition = nullptr;
        rayTracingState().m_swCausticBindingSetAccumulator = nullptr;
        rayTracingState().m_swCausticBindingSetMeshCount = 0u;
        return false;
    }
    rayTracingState().m_swCausticBindingSetSceneNodes = sceneNodeBuffer;
    rayTracingState().m_swCausticBindingSetInstances = instanceBuffer;
    rayTracingState().m_swCausticBindingSetInstanceMaterial = instanceMaterialBuffer;
    rayTracingState().m_swCausticBindingSetMaterialTyped = materialTypedBuffer;
    rayTracingState().m_swCausticBindingSetMeshInstances = meshInstanceBuffer;
    rayTracingState().m_swCausticBindingSetEmissionTargets = emissionTargetBuffer;
    rayTracingState().m_swCausticBindingSetView = viewBuffer;
    rayTracingState().m_swCausticBindingSetDepth = depthTarget;
    rayTracingState().m_swCausticBindingSetWorldPosition = worldPositionTarget;
    rayTracingState().m_swCausticBindingSetAccumulator = accumulatorTarget;
    rayTracingState().m_swCausticBindingSetMeshCount = meshCount;
    return true;
}

bool RendererRayTracingSystem::ensureCausticResolvePipeline(){
    if(rayTracingState().m_causticResolvePipeline)
        return true;
    if(rayTracingState().m_causticResolvePipelineFailed)
        return false;

    auto* device = graphics().getDevice();

    if(!rayTracingState().m_causticResolveBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_CAUSTIC_RESOLVE_BINDING_ACCUMULATOR, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_CAUSTIC_RESOLVE_BINDING_GBUFFER_WORLD_POSITION, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_CAUSTIC_RESOLVE_BINDING_GBUFFER_DEPTH, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_CAUSTIC_RESOLVE_BINDING_OUTPUT, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_CAUSTIC_RESOLVE_BINDING_INPUT_COLOR, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(CausticResolvePushConstants)));

        rayTracingState().m_causticResolveBindingLayout = device->createBindingLayout(layoutDesc);
        if(!rayTracingState().m_causticResolveBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic resolve binding layout"));
            rayTracingState().m_causticResolvePipelineFailed = true;
            return false;
        }
    }

    if(!m_renderer.shaderSystem().loadShader(
        rayTracingState().m_causticResolveShader,
        AssetsGraphicsCaustic::s_ResolveShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_CausticResolve"
    )){
        rayTracingState().m_causticResolvePipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(rayTracingState().m_causticResolveShader)
        .addBindingLayout(rayTracingState().m_causticResolveBindingLayout)
    ;
    rayTracingState().m_causticResolvePipeline = device->createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_causticResolvePipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic resolve compute pipeline"));
        rayTracingState().m_causticResolvePipelineFailed = true;
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureCausticResolveBindingSet(DeferredFrameTargets& targets){
    NWB_ASSERT(rayTracingState().m_causticResolveBindingLayout);
    NWB_ASSERT(targets.causticAccumulator);
    NWB_ASSERT(targets.worldPosition);
    NWB_ASSERT(targets.depth);
    NWB_ASSERT(targets.causticIrradiance);
    NWB_ASSERT(targets.causticHistory);
    NWB_ASSERT(targets.causticResolveHalf);

    // Non-const (BindingSetItem needs a mutable Texture*); the const cache fields still compare/assign fine.
    Core::Texture* accumulatorTarget = targets.causticAccumulator.get();
    Core::Texture* worldPositionTarget = targets.worldPosition.get();
    Core::Texture* depthTarget = targets.depth.get();
    Core::Texture* irradianceTarget = targets.causticIrradiance.get();
    Core::Texture* halfATarget = targets.causticHistory.get();
    Core::Texture* halfBTarget = targets.causticResolveHalf.get();
    if(
        rayTracingState().m_causticResolveBindingSetOutputHalfA
        && rayTracingState().m_causticResolveBindingSetOutputHalfB
        && rayTracingState().m_causticResolveBindingSetUpsample
        && rayTracingState().m_causticResolveBindingSetAccumulator == accumulatorTarget
        && rayTracingState().m_causticResolveBindingSetWorldPosition == worldPositionTarget
        && rayTracingState().m_causticResolveBindingSetDepth == depthTarget
        && rayTracingState().m_causticResolveBindingSetIrradiance == irradianceTarget
        && rayTracingState().m_causticResolveBindingSetHalfA == halfATarget
        && rayTracingState().m_causticResolveBindingSetHalfB == halfBTarget
    )
        return true;

    auto* device = graphics().getDevice();

    // Three binding sets. All share the accumulator + G-buffer SRVs and only swap the (output UAV, input-color SRV) pair:
    //  - OutputHalfA: writes half-A reading half-B (the PREPARE pass + the odd wavelet passes),
    //  - OutputHalfB: writes half-B reading half-A (the even wavelet passes),
    //  - Upsample:    writes the FULL-res irradiance reading the final half-res caustic (half-B).
    // The half buffers are half-res and the irradiance is full-res, but the sets are dimensionless (the bound textures
    // carry the extent), so the same layout serves all three.
    const auto buildSet = [&](Core::Texture* outputTex, Core::Format::Enum outputFormat, Core::Texture* inputTex, Core::Format::Enum inputFormat) -> Core::BindingSetHandle {
        Core::BindingSetDesc desc(arena());
        desc.addItem(Core::BindingSetItem::Texture_SRV(
            NWB_CAUSTIC_RESOLVE_BINDING_ACCUMULATOR,
            accumulatorTarget,
            targets.causticAccumulatorFormat,
            ECSRenderDetail::s_CausticAccumulatorSubresources,
            Core::TextureDimension::Texture2DArray
        ));
        desc.addItem(Core::BindingSetItem::Texture_SRV(
            NWB_CAUSTIC_RESOLVE_BINDING_GBUFFER_WORLD_POSITION,
            worldPositionTarget,
            targets.worldPositionFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ));
        desc.addItem(Core::BindingSetItem::Texture_SRV(
            NWB_CAUSTIC_RESOLVE_BINDING_GBUFFER_DEPTH,
            depthTarget,
            targets.depthFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ));
        desc.addItem(Core::BindingSetItem::Texture_UAV(
            NWB_CAUSTIC_RESOLVE_BINDING_OUTPUT,
            outputTex,
            outputFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ));
        desc.addItem(Core::BindingSetItem::Texture_SRV(
            NWB_CAUSTIC_RESOLVE_BINDING_INPUT_COLOR,
            inputTex,
            inputFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ));
        return device->createBindingSet(desc, rayTracingState().m_causticResolveBindingLayout);
    };

    Core::BindingSetHandle outputHalfA = buildSet(halfATarget, targets.causticHistoryFormat, halfBTarget, targets.causticHistoryFormat);
    Core::BindingSetHandle outputHalfB = buildSet(halfBTarget, targets.causticHistoryFormat, halfATarget, targets.causticHistoryFormat);
    Core::BindingSetHandle upsample    = buildSet(irradianceTarget, targets.causticIrradianceFormat, halfBTarget, targets.causticHistoryFormat);
    if(!outputHalfA || !outputHalfB || !upsample){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic resolve a-trous binding sets"));
        rayTracingState().m_causticResolveBindingSetOutputHalfA = nullptr;
        rayTracingState().m_causticResolveBindingSetOutputHalfB = nullptr;
        rayTracingState().m_causticResolveBindingSetUpsample = nullptr;
        rayTracingState().m_causticResolveBindingSetAccumulator = nullptr;
        rayTracingState().m_causticResolveBindingSetWorldPosition = nullptr;
        rayTracingState().m_causticResolveBindingSetDepth = nullptr;
        rayTracingState().m_causticResolveBindingSetIrradiance = nullptr;
        rayTracingState().m_causticResolveBindingSetHalfA = nullptr;
        rayTracingState().m_causticResolveBindingSetHalfB = nullptr;
        return false;
    }
    rayTracingState().m_causticResolveBindingSetOutputHalfA = Move(outputHalfA);
    rayTracingState().m_causticResolveBindingSetOutputHalfB = Move(outputHalfB);
    rayTracingState().m_causticResolveBindingSetUpsample = Move(upsample);
    rayTracingState().m_causticResolveBindingSetAccumulator = accumulatorTarget;
    rayTracingState().m_causticResolveBindingSetWorldPosition = worldPositionTarget;
    rayTracingState().m_causticResolveBindingSetDepth = depthTarget;
    rayTracingState().m_causticResolveBindingSetIrradiance = irradianceTarget;
    rayTracingState().m_causticResolveBindingSetHalfA = halfATarget;
    rayTracingState().m_causticResolveBindingSetHalfB = halfBTarget;
    return true;
}

bool RendererRayTracingSystem::ensureCausticRtPipeline(){
    if(rayTracingState().m_hwCausticPipeline)
        return true;
    if(rayTracingState().m_hwCausticPipelineFailed)
        return false;
    if(!graphics().queryFeatureSupport(Core::Feature::RayTracingPipeline)){
        rayTracingState().m_hwCausticPipelineFailed = true;
        return false;
    }

    auto* device = graphics().getDevice();

    if(!rayTracingState().m_hwCausticBindingLayout){
        // Mirrors the shadow RT layout (TLAS + scene/light + instance-material + material-context + per-mesh
        // index/attribute arrays) and adds the caustic I/O (emission targets, view, G-buffer depth + world position
        // for the splat, the R32_UINT accumulator UAV) + the push constants (byte-identical to the SW producer's).
        // No per-mesh position array: the refraction bends on the interpolated shading normal (from the attributes).
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::AllRayTracing);
        layoutDesc.addItem(Core::BindingLayoutItem::RayTracingAccelStruct(NWB_CAUSTIC_RT_BINDING_TLAS, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_CAUSTIC_RT_BINDING_SCENE_SHADING, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_CAUSTIC_RT_BINDING_LIGHT_LIST, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_CAUSTIC_RT_BINDING_INSTANCE_MATERIAL, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_CAUSTIC_RT_BINDING_MATERIAL_TYPED, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_CAUSTIC_RT_BINDING_MESH_INSTANCES, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_CAUSTIC_RT_BINDING_EMISSION_TARGETS, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_CAUSTIC_RT_BINDING_VIEW, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_CAUSTIC_RT_BINDING_GBUFFER_DEPTH, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_CAUSTIC_RT_BINDING_GBUFFER_WORLD_POSITION, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_CAUSTIC_RT_BINDING_ACCUMULATOR, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_SRV(NWB_CAUSTIC_RT_BINDING_MESH_INDICES, NWB_CAUSTIC_RT_MAX_MESHES));
        layoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_SRV(NWB_CAUSTIC_RT_BINDING_MESH_ATTRIBUTES, NWB_CAUSTIC_RT_MAX_MESHES));
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(CausticPhotonPushConstants)));

        rayTracingState().m_hwCausticBindingLayout = device->createBindingLayout(layoutDesc);
        if(!rayTracingState().m_hwCausticBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create hardware caustic binding layout"));
            rayTracingState().m_hwCausticPipelineFailed = true;
            return false;
        }
    }

    Core::ShaderHandle raygenShader;
    Core::ShaderHandle missShader;
    Core::ShaderHandle closestHitShader;
    if(
        !m_renderer.shaderSystem().loadShader(raygenShader, AssetsGraphicsCaustic::s_HwRaygenShaderName, Core::ShaderArchive::s_DefaultVariant, Core::ShaderType::RayGeneration, "ECSRender_CausticHwRaygen")
        || !m_renderer.shaderSystem().loadShader(missShader, AssetsGraphicsCaustic::s_HwMissShaderName, Core::ShaderArchive::s_DefaultVariant, Core::ShaderType::Miss, "ECSRender_CausticHwMiss")
        || !m_renderer.shaderSystem().loadShader(closestHitShader, AssetsGraphicsCaustic::s_HwClosestHitShaderName, Core::ShaderArchive::s_DefaultVariant, Core::ShaderType::ClosestHit, "ECSRender_CausticHwClosestHit")
    ){
        rayTracingState().m_hwCausticPipelineFailed = true;
        return false;
    }

    Core::RayTracingPipelineDesc pipelineDesc(arena());
    // Payload = NwbCausticHwPayload (3*float3 + 2*float + 2*uint = 52 bytes); round up to 64. Recursion stays 1 (the
    // shared bounce loop drives the bounces via a fresh TraceRay per segment, not shader recursion).
    pipelineDesc.setMaxPayloadSize(static_cast<u32>(sizeof(f32) * 16u));
    pipelineDesc.setMaxRecursionDepth(1u);
    pipelineDesc.addBindingLayout(rayTracingState().m_hwCausticBindingLayout);

    Core::RayTracingPipelineShaderDesc raygenDesc(arena());
    raygenDesc.setShader(raygenShader).setExportName("CausticHwRayGen");
    pipelineDesc.addShader(raygenDesc);

    Core::RayTracingPipelineShaderDesc missDesc(arena());
    missDesc.setShader(missShader).setExportName("CausticHwMiss");
    pipelineDesc.addShader(missDesc);

    Core::RayTracingPipelineHitGroupDesc hitGroupDesc(arena());
    hitGroupDesc.setClosestHitShader(closestHitShader).setExportName("CausticHwHitGroup");
    pipelineDesc.addHitGroup(hitGroupDesc);

    rayTracingState().m_hwCausticPipeline = device->createRayTracingPipeline(pipelineDesc);
    if(!rayTracingState().m_hwCausticPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create RT caustic pipeline"));
        rayTracingState().m_hwCausticPipelineFailed = true;
        return false;
    }

    Core::RayTracingShaderTableHandle shaderTable = rayTracingState().m_hwCausticPipeline->createShaderTable();
    if(!shaderTable){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create RT caustic shader table"));
        rayTracingState().m_hwCausticPipelineFailed = true;
        return false;
    }
    shaderTable->setRayGenerationShader("CausticHwRayGen");
    shaderTable->addMissShader("CausticHwMiss");
    shaderTable->addHitGroup("CausticHwHitGroup");
    rayTracingState().m_hwCausticShaderTable = Move(shaderTable);

    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created RT caustic pipeline + shader table"));
    return true;
}

bool RendererRayTracingSystem::ensureCausticRtBindingSet(DeferredFrameTargets& targets){
    NWB_ASSERT(rayTracingState().m_hwCausticBindingLayout);
    NWB_ASSERT(rayTracingState().m_tlas);
    NWB_ASSERT(rayTracingState().m_shadowInstanceMaterialBuffer);
    NWB_ASSERT(rayTracingState().m_shadowMeshCount > 0u);
    NWB_ASSERT(rayTracingState().m_shadowMaterialTypedBuffer);
    NWB_ASSERT(rayTracingState().m_shadowInstanceBuffer);
    NWB_ASSERT(rayTracingState().m_causticEmissionTargetBuffer);
    NWB_ASSERT(deferredState().m_sceneShadingBuffer);
    NWB_ASSERT(deferredState().m_lightBuffer);
    NWB_ASSERT(drawState().m_meshViewBuffer);
    NWB_ASSERT(targets.depth);
    NWB_ASSERT(targets.worldPosition);
    NWB_ASSERT(targets.causticAccumulator);

    // Rebuild when any cached input changes (mirrors the shadow + SW caustic sets): the TLAS, the shadow-owned
    // instance-material / material-context buffers, the emission targets, the view CB, the G-buffer SRVs the splat
    // reads, the accumulator UAV, and the distinct-mesh count (the per-mesh descriptor arrays repopulate).
    const Core::RayTracingAccelStruct* tlas = rayTracingState().m_tlas.get();
    const Core::Buffer* instanceMaterialBuffer = rayTracingState().m_shadowInstanceMaterialBuffer.get();
    Core::Buffer* materialTypedBuffer = rayTracingState().m_shadowMaterialTypedBuffer.get();
    Core::Buffer* meshInstanceBuffer = rayTracingState().m_shadowInstanceBuffer.get();
    Core::Buffer* emissionTargetBuffer = rayTracingState().m_causticEmissionTargetBuffer.get();
    Core::Buffer* viewBuffer = drawState().m_meshViewBuffer.get();
    const Core::Texture* depthTarget = targets.depth.get();
    const Core::Texture* worldPositionTarget = targets.worldPosition.get();
    const Core::Texture* accumulatorTarget = targets.causticAccumulator.get();
    const u32 meshCount = rayTracingState().m_shadowMeshCount;
    if(
        rayTracingState().m_hwCausticBindingSet
        && rayTracingState().m_hwCausticBindingSetTlas == tlas
        && rayTracingState().m_hwCausticBindingSetInstanceMaterial == instanceMaterialBuffer
        && rayTracingState().m_hwCausticBindingSetMaterialTyped == materialTypedBuffer
        && rayTracingState().m_hwCausticBindingSetMeshInstances == meshInstanceBuffer
        && rayTracingState().m_hwCausticBindingSetEmissionTargets == emissionTargetBuffer
        && rayTracingState().m_hwCausticBindingSetView == viewBuffer
        && rayTracingState().m_hwCausticBindingSetDepth == depthTarget
        && rayTracingState().m_hwCausticBindingSetWorldPosition == worldPositionTarget
        && rayTracingState().m_hwCausticBindingSetAccumulator == accumulatorTarget
        && rayTracingState().m_hwCausticBindingSetMeshCount == meshCount
    )
        return true;

    Core::BindingSetDesc bindingSetDesc(arena());
    bindingSetDesc.addItem(Core::BindingSetItem::RayTracingAccelStruct(NWB_CAUSTIC_RT_BINDING_TLAS, rayTracingState().m_tlas.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_CAUSTIC_RT_BINDING_SCENE_SHADING, deferredState().m_sceneShadingBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_CAUSTIC_RT_BINDING_LIGHT_LIST, deferredState().m_lightBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_CAUSTIC_RT_BINDING_INSTANCE_MATERIAL, rayTracingState().m_shadowInstanceMaterialBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_CAUSTIC_RT_BINDING_MATERIAL_TYPED, materialTypedBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_CAUSTIC_RT_BINDING_MESH_INSTANCES, meshInstanceBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_CAUSTIC_RT_BINDING_EMISSION_TARGETS, emissionTargetBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_CAUSTIC_RT_BINDING_VIEW, viewBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_CAUSTIC_RT_BINDING_GBUFFER_DEPTH,
        targets.depth.get(),
        targets.depthFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_CAUSTIC_RT_BINDING_GBUFFER_WORLD_POSITION,
        targets.worldPosition.get(),
        targets.worldPositionFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_CAUSTIC_RT_BINDING_ACCUMULATOR,
        targets.causticAccumulator.get(),
        targets.causticAccumulatorFormat,
        ECSRenderDetail::s_CausticAccumulatorSubresources,
        Core::TextureDimension::Texture2DArray
    ));

    // Per-mesh descriptor arrays: bind every slot (the closest-hit only indexes meshSlot < meshCount). Unused tail
    // slots are padded with the last real mesh so the non-bindless arrays have no unbound descriptors. The caustic
    // reuses the shadow per-mesh index + attribute buffers verbatim (the shading-normal bend needs no positions).
    for(u32 slot = 0u; slot < NWB_CAUSTIC_RT_MAX_MESHES; ++slot){
        const u32 source = (slot < meshCount) ? slot : (meshCount - 1u);
        bindingSetDesc.addItem(Core::BindingSetItem::RawBuffer_SRV(NWB_CAUSTIC_RT_BINDING_MESH_INDICES, rayTracingState().m_shadowMeshIndexBuffers[source]).setArrayElement(slot));
        bindingSetDesc.addItem(Core::BindingSetItem::RawBuffer_SRV(NWB_CAUSTIC_RT_BINDING_MESH_ATTRIBUTES, rayTracingState().m_shadowMeshAttributeBuffers[source]).setArrayElement(slot));
    }

    auto* device = graphics().getDevice();
    rayTracingState().m_hwCausticBindingSet = device->createBindingSet(bindingSetDesc, rayTracingState().m_hwCausticBindingLayout);
    if(!rayTracingState().m_hwCausticBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create hardware caustic binding set"));
        rayTracingState().m_hwCausticBindingSetTlas = nullptr;
        rayTracingState().m_hwCausticBindingSetInstanceMaterial = nullptr;
        rayTracingState().m_hwCausticBindingSetMaterialTyped = nullptr;
        rayTracingState().m_hwCausticBindingSetMeshInstances = nullptr;
        rayTracingState().m_hwCausticBindingSetEmissionTargets = nullptr;
        rayTracingState().m_hwCausticBindingSetView = nullptr;
        rayTracingState().m_hwCausticBindingSetDepth = nullptr;
        rayTracingState().m_hwCausticBindingSetWorldPosition = nullptr;
        rayTracingState().m_hwCausticBindingSetAccumulator = nullptr;
        rayTracingState().m_hwCausticBindingSetMeshCount = 0u;
        return false;
    }
    rayTracingState().m_hwCausticBindingSetTlas = tlas;
    rayTracingState().m_hwCausticBindingSetInstanceMaterial = instanceMaterialBuffer;
    rayTracingState().m_hwCausticBindingSetMaterialTyped = materialTypedBuffer;
    rayTracingState().m_hwCausticBindingSetMeshInstances = meshInstanceBuffer;
    rayTracingState().m_hwCausticBindingSetEmissionTargets = emissionTargetBuffer;
    rayTracingState().m_hwCausticBindingSetView = viewBuffer;
    rayTracingState().m_hwCausticBindingSetDepth = depthTarget;
    rayTracingState().m_hwCausticBindingSetWorldPosition = worldPositionTarget;
    rayTracingState().m_hwCausticBindingSetAccumulator = accumulatorTarget;
    rayTracingState().m_hwCausticBindingSetMeshCount = meshCount;
    return true;
}

bool RendererRayTracingSystem::hasHwCausticWork()const noexcept{
    // The hardware caustic producer runs only on the hardware-ray-tracing path, and only when the scene holds at
    // least one caustic light AND at least one refractive instance (else the black-cleared irradiance buffer is the
    // additive no-op). The TLAS + at least one tracked mesh must exist so the photon has geometry to hit.
    return graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct)
        && rayTracingState().m_causticLightCount > 0u
        && rayTracingState().m_causticRefractiveInstanceCount > 0u
        && rayTracingState().m_tlas
        && rayTracingState().m_shadowMeshCount > 0u
        && rayTracingState().m_causticEmissionTargetBuffer
    ;
}

bool RendererRayTracingSystem::prepareHwCausticResources(DeferredFrameTargets& targets){
    // Build the hardware caustic producer + resolve resources, mirroring prepareGpuBvhCausticResources for the HW
    // path. Gated on the prepare-time invariants (refractive instances + the TLAS + tracked meshes + emission targets
    // + the caustic targets + the view buffer); the caustic-LIGHT gate lives in renderHwCaustics (the light count is
    // resolved later, in the render pass). A non-HW device early-returns true (nothing to build here).
    if(!graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct))
        return true;
    if(
        rayTracingState().m_causticRefractiveInstanceCount == 0u
        || !rayTracingState().m_tlas
        || rayTracingState().m_shadowMeshCount == 0u
        || !rayTracingState().m_causticEmissionTargetBuffer
    )
        return true;
    if(!targets.causticAccumulator || !targets.causticIrradiance || !drawState().m_meshViewBuffer)
        return true;

    const bool producerReady = ensureCausticRtPipeline() && ensureCausticRtBindingSet(targets);
    const bool resolveReady = ensureCausticResolvePipeline() && ensureCausticResolveBindingSet(targets);
    return producerReady && resolveReady;
}

bool RendererRayTracingSystem::renderHwCaustics(Core::CommandList& commandList, DeferredFrameTargets& targets){
    // Hardware ray-traced caustic photon producer + resolve (P4) -- the byte-parallel sibling of renderGpuBvhCaustics,
    // dispatched in the HW branch right after the shadow pass + clearCausticTargets and BEFORE deferred lighting. The
    // raygen runs the SHARED iterative bounce loop (recursion 1) over the TLAS and splats into the SAME R32_UINT
    // accumulator the SAME resolve consumes, so the HW + SW paths converge to the same caustic (Monte-Carlo A/B).
    if(!hasHwCausticWork())
        return false;
    if(
        !rayTracingState().m_hwCausticPipeline
        || !rayTracingState().m_hwCausticShaderTable
        || !rayTracingState().m_hwCausticBindingSet
        || !rayTracingState().m_causticResolvePipeline
        || !rayTracingState().m_causticResolveBindingSetOutputHalfA
        || !rayTracingState().m_causticResolveBindingSetOutputHalfB
        || !rayTracingState().m_causticResolveBindingSetUpsample
    )
        return false;
    if(!targets.causticAccumulator || !targets.causticIrradiance || !targets.causticHistory || !targets.causticResolveHalf)
        return false;

    {
        Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_CausticPhotons, graphics().getDevice(), commandList);

        // Move the per-mesh index/attribute/position byte buffers + the shadow-owned material context + the emission
        // targets to ShaderResource; setResourceStatesForBindingSet derives the TLAS, G-buffer SRVs, view CB, and the
        // accumulator UAV.
        for(u32 slot = 0u; slot < rayTracingState().m_shadowMeshCount; ++slot){
            commandList.setBufferState(rayTracingState().m_shadowMeshIndexBuffers[slot], Core::ResourceStates::ShaderResource);
            commandList.setBufferState(rayTracingState().m_shadowMeshAttributeBuffers[slot], Core::ResourceStates::ShaderResource);
        }
        commandList.setBufferState(rayTracingState().m_shadowMaterialTypedBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setBufferState(rayTracingState().m_shadowInstanceBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setBufferState(rayTracingState().m_causticEmissionTargetBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setResourceStatesForBindingSet(rayTracingState().m_hwCausticBindingSet.get());
        commandList.commitBarriers();

        // Push constants byte-identical to the SW producer (same struct + same constants) so the photon grid / flux /
        // emission / jitter match the SW reference exactly. instanceCount is the live TLAS instance count (the SW
        // scene-BVH count is zero on the HW path), used only as the raygen's non-zero geometry guard.
        CausticPhotonPushConstants pushConstants;
        pushConstants.width = targets.width;
        pushConstants.height = targets.height;
        pushConstants.instanceCount = rayTracingState().m_tlasInstanceCount;
        pushConstants.photonCount = s_CausticHwPhotonCount;
        pushConstants.emissionTargetCount = rayTracingState().m_causticRefractiveInstanceCount;
        pushConstants.gridSide = s_CausticHwPhotonGridSide;

        Core::RayTracingState rayTracingPassState;
        rayTracingPassState.setShaderTable(rayTracingState().m_hwCausticShaderTable.get());
        rayTracingPassState.addBindingSet(rayTracingState().m_hwCausticBindingSet.get());
        commandList.setRayTracingState(rayTracingPassState);
        commandList.setPushConstants(&pushConstants, sizeof(pushConstants));

        // Dispatch one ray per photon over a gridSide x gridSide grid == the SW photon grid, so the raygen's
        // photonIndex (y*W + x) equals the SW SV_DispatchThreadID index per photon. The grid side scales per build
        // config (dbg 128 / opt+fin 512) -- the producer is energy-conserving, so the higher count just densifies.
        Core::RayTracingDispatchRaysArguments dispatchArgs;
        dispatchArgs.setDimensions(s_CausticHwPhotonGridSide, s_CausticHwPhotonGridSide, 1u);
        commandList.dispatchRays(dispatchArgs);
    }

    // Identical resolve to the SW path (the SAME pipeline + ping-pong binding sets): the shared a-trous wavelet denoise.
    dispatchCausticResolve(commandList, targets);

    if(!rayTracingState().m_hwCausticDispatchLogged){
        rayTracingState().m_hwCausticDispatchLogged = true;
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: dispatched hardware caustic producer ({} photons, {} caustic lights, {} refractive instances)")
            , static_cast<u64>(s_CausticHwPhotonCount)
            , static_cast<u64>(rayTracingState().m_causticLightCount)
            , static_cast<u64>(rayTracingState().m_causticRefractiveInstanceCount)
        );
    }
    return true;
}

bool RendererRayTracingSystem::ensureBvhSortPipeline(){
    if(rayTracingState().m_bvhSortPipeline)
        return true;
    if(rayTracingState().m_bvhSortPipelineFailed)
        return false;

    auto* device = graphics().getDevice();

    if(!rayTracingState().m_bvhSortBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_BVH_SORT_BINDING_KEYS, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_BVH_SORT_BINDING_PAYLOAD, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(BvhSortPushConstants)));

        rayTracingState().m_bvhSortBindingLayout = device->createBindingLayout(layoutDesc);
        if(!rayTracingState().m_bvhSortBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH sort binding layout"));
            rayTracingState().m_bvhSortPipelineFailed = true;
            return false;
        }
    }

    if(!m_renderer.shaderSystem().loadShader(
        rayTracingState().m_bvhSortShader,
        AssetsGraphicsBvh::s_BitonicSortShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_BvhBitonicSort"
    )){
        rayTracingState().m_bvhSortPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(rayTracingState().m_bvhSortShader)
        .addBindingLayout(rayTracingState().m_bvhSortBindingLayout)
    ;
    rayTracingState().m_bvhSortPipeline = device->createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_bvhSortPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH sort compute pipeline"));
        rayTracingState().m_bvhSortPipelineFailed = true;
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureBvhSortBuffers(usize paddedCount){
    auto* device = graphics().getDevice();

    if(!rayTracingState().m_bvhSortKeysBuffer || rayTracingState().m_bvhSortCapacity < paddedCount){
        usize capacity = rayTracingState().m_bvhSortCapacity > 0u ? rayTracingState().m_bvhSortCapacity : s_BvhSortInitialCapacity;
        while(capacity < paddedCount)
            capacity *= 2u;

        Core::BufferDesc keysBufferDesc;
        keysBufferDesc
            .setByteSize(static_cast<u64>(sizeof(u32) * capacity))
            .setStructStride(sizeof(u32))
            .setCanHaveUAVs(true)
            .setDebugName(Name("bvh_sort_keys"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        rayTracingState().m_bvhSortKeysBuffer = graphics().createBuffer(keysBufferDesc);
        if(!rayTracingState().m_bvhSortKeysBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH sort keys buffer"));
            return false;
        }

        Core::BufferDesc payloadBufferDesc;
        payloadBufferDesc
            .setByteSize(static_cast<u64>(sizeof(u32) * capacity))
            .setStructStride(sizeof(u32))
            .setCanHaveUAVs(true)
            .setDebugName(Name("bvh_sort_payload"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        rayTracingState().m_bvhSortPayloadBuffer = graphics().createBuffer(payloadBufferDesc);
        if(!rayTracingState().m_bvhSortPayloadBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH sort payload buffer"));
            return false;
        }

        rayTracingState().m_bvhSortCapacity = capacity;
        rayTracingState().m_bvhSortBindingSet.reset();
    }

    const Core::Buffer* keysBuffer = rayTracingState().m_bvhSortKeysBuffer.get();
    if(
        rayTracingState().m_bvhSortBindingSet
        && rayTracingState().m_bvhSortBindingSetKeys == keysBuffer
    )
        return true;

    Core::BindingSetDesc bindingSetDesc(arena());
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_BVH_SORT_BINDING_KEYS, rayTracingState().m_bvhSortKeysBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_BVH_SORT_BINDING_PAYLOAD, rayTracingState().m_bvhSortPayloadBuffer.get()));

    rayTracingState().m_bvhSortBindingSet = device->createBindingSet(bindingSetDesc, rayTracingState().m_bvhSortBindingLayout);
    if(!rayTracingState().m_bvhSortBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH sort binding set"));
        rayTracingState().m_bvhSortBindingSetKeys = nullptr;
        return false;
    }
    rayTracingState().m_bvhSortBindingSetKeys = keysBuffer;
    return true;
}

bool RendererRayTracingSystem::bvhBitonicSort(Core::CommandList& commandList, u32 elementCount, u32 paddedCount){
    NWB_ASSERT(rayTracingState().m_bvhSortPipeline);
    NWB_ASSERT(rayTracingState().m_bvhSortBindingSet);
    NWB_ASSERT(rayTracingState().m_bvhSortKeysBuffer);
    NWB_ASSERT(rayTracingState().m_bvhSortPayloadBuffer);

    // paddedCount must be a power of two and a multiple of the dispatch group size; the caller fills the
    // sort buffers to it and pads the tail with sentinel keys that sort to the end.
    if(paddedCount < static_cast<u32>(NWB_BVH_SORT_GROUP_SIZE))
        return false;

    Core::Buffer* keysBuffer = rayTracingState().m_bvhSortKeysBuffer.get();
    Core::Buffer* payloadBuffer = rayTracingState().m_bvhSortPayloadBuffer.get();

    // Each (sequenceSize, compareDistance) step reads and writes the same buffers, so consecutive steps
    // must be serialized with UAV barriers: enable per-buffer UAV barriers, then commit one per step.
    commandList.setEnableUavBarriersForBuffer(keysBuffer, true);
    commandList.setEnableUavBarriersForBuffer(payloadBuffer, true);

    const u32 groupCount = paddedCount / static_cast<u32>(NWB_BVH_SORT_GROUP_SIZE);
    for(u32 sequenceSize = 2u; sequenceSize <= paddedCount; sequenceSize <<= 1u){
        for(u32 compareDistance = sequenceSize >> 1u; compareDistance > 0u; compareDistance >>= 1u){
            Core::ComputeState computeState;
            computeState.setPipeline(rayTracingState().m_bvhSortPipeline.get());
            computeState.addBindingSet(rayTracingState().m_bvhSortBindingSet.get());
            commandList.setComputeState(computeState);

            BvhSortPushConstants pushConstants;
            pushConstants.elementCount = elementCount;
            pushConstants.compareDistance = compareDistance;
            pushConstants.sequenceSize = sequenceSize;
            commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
            commandList.dispatch(groupCount, 1u, 1u);

            commandList.setBufferState(keysBuffer, Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(payloadBuffer, Core::ResourceStates::UnorderedAccess);
            commandList.commitBarriers();
        }
    }
    return true;
}

bool RendererRayTracingSystem::ensureBvhBuildPipeline(){
    if(
        rayTracingState().m_bvhMortonPipeline
        && rayTracingState().m_bvhTopologyPipeline
        && rayTracingState().m_bvhFitPipeline
    )
        return true;
    if(rayTracingState().m_bvhBuildPipelineFailed)
        return false;

    auto* device = graphics().getDevice();

    if(!rayTracingState().m_bvhBuildBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_SRV(NWB_BVH_BUILD_BINDING_POSITIONS, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_SRV(NWB_BVH_BUILD_BINDING_TRIANGLE_INDICES, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_BVH_BUILD_BINDING_KEYS, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_BVH_BUILD_BINDING_PAYLOAD, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_BVH_BUILD_BINDING_NODES, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_BVH_BUILD_BINDING_PARENT, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_BVH_BUILD_BINDING_VISIT_COUNTER, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(BvhBuildPushConstants)));

        rayTracingState().m_bvhBuildBindingLayout = device->createBindingLayout(layoutDesc);
        if(!rayTracingState().m_bvhBuildBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH build binding layout"));
            rayTracingState().m_bvhBuildPipelineFailed = true;
            return false;
        }
    }

    const auto createBuildPipeline = [this, device](
        Core::ShaderHandle& shader,
        Core::ComputePipelineHandle& pipeline,
        const Name& shaderName,
        const char* debugLabel
    )->bool{
        if(pipeline)
            return true;
        if(!m_renderer.shaderSystem().loadShader(shader, shaderName, Core::ShaderArchive::s_DefaultVariant, Core::ShaderType::Compute, debugLabel))
            return false;

        Core::ComputePipelineDesc pipelineDesc;
        pipelineDesc
            .setComputeShader(shader)
            .addBindingLayout(rayTracingState().m_bvhBuildBindingLayout)
        ;
        pipeline = device->createComputePipeline(pipelineDesc);
        return pipeline != nullptr;
    };

    if(
        !createBuildPipeline(rayTracingState().m_bvhMortonShader, rayTracingState().m_bvhMortonPipeline, AssetsGraphicsBvh::s_BvhMortonShaderName, "ECSRender_BvhMorton")
        || !createBuildPipeline(rayTracingState().m_bvhTopologyShader, rayTracingState().m_bvhTopologyPipeline, AssetsGraphicsBvh::s_BvhTopologyShaderName, "ECSRender_BvhTopology")
        || !createBuildPipeline(rayTracingState().m_bvhFitShader, rayTracingState().m_bvhFitPipeline, AssetsGraphicsBvh::s_BvhFitShaderName, "ECSRender_BvhFit")
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH build compute pipeline"));
        rayTracingState().m_bvhBuildPipelineFailed = true;
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureBvhVisitCounterBuffer(usize primitiveCount){
    if(rayTracingState().m_bvhVisitCounterBuffer && rayTracingState().m_bvhBuildCapacity >= primitiveCount)
        return true;

    usize capacity = rayTracingState().m_bvhBuildCapacity > 0u ? rayTracingState().m_bvhBuildCapacity : s_BvhBuildInitialCapacity;
    while(capacity < primitiveCount)
        capacity *= 2u;

    // The fit's bottom-up rendezvous counter is SHARED scratch (one u32 per internal node, < N). Meshes are
    // built sequentially within a frame and serialized by barriers, so a single shared counter suffices.
    Core::BufferDesc counterBufferDesc;
    counterBufferDesc
        .setByteSize(static_cast<u64>(sizeof(u32) * capacity))
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        .setDebugName(Name("bvh_visit_counter"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    rayTracingState().m_bvhVisitCounterBuffer = graphics().createBuffer(counterBufferDesc);
    if(!rayTracingState().m_bvhVisitCounterBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH visit counter buffer"));
        return false;
    }
    rayTracingState().m_bvhBuildCapacity = capacity;
    return true;
}

bool RendererRayTracingSystem::createMeshBvhStorage(usize primitiveCount, Core::BufferHandle& nodeBuffer, Core::BufferHandle& parentBuffer){
    if(nodeBuffer && parentBuffer)
        return true;

    // A binary LBVH over N primitives has exactly 2N-1 nodes (internal [0,N-1) + leaves [N-1,2N-1)). These
    // are PER-MESH and persist across frames (refit reuses the topology), so they are sized exactly to N.
    const usize nodeCount = primitiveCount * 2u - 1u;

    Core::BufferDesc nodeBufferDesc;
    nodeBufferDesc
        .setByteSize(static_cast<u64>(sizeof(NwbBvhNodeGpu) * nodeCount))
        .setStructStride(sizeof(NwbBvhNodeGpu))
        .setCanHaveUAVs(true)
        .setDebugName(Name("bvh_mesh_nodes"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    nodeBuffer = graphics().createBuffer(nodeBufferDesc);
    if(!nodeBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create per-mesh BVH node buffer"));
        return false;
    }

    Core::BufferDesc parentBufferDesc;
    parentBufferDesc
        .setByteSize(static_cast<u64>(sizeof(u32) * nodeCount))
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        .setDebugName(Name("bvh_mesh_parent"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    parentBuffer = graphics().createBuffer(parentBufferDesc);
    if(!parentBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create per-mesh BVH parent buffer"));
        nodeBuffer.reset();
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureMeshBvhBindingSet(
    Core::Buffer* positionBuffer,
    Core::Buffer* triangleIndexBuffer,
    Core::Buffer* nodeBuffer,
    Core::Buffer* parentBuffer,
    Core::BindingSetHandle& bindingSet
){
    if(bindingSet)
        return true;

    NWB_ASSERT(rayTracingState().m_bvhBuildBindingLayout);
    NWB_ASSERT(rayTracingState().m_bvhSortKeysBuffer);
    NWB_ASSERT(rayTracingState().m_bvhSortPayloadBuffer);
    NWB_ASSERT(rayTracingState().m_bvhVisitCounterBuffer);
    NWB_ASSERT(positionBuffer);
    NWB_ASSERT(triangleIndexBuffer);
    NWB_ASSERT(nodeBuffer);
    NWB_ASSERT(parentBuffer);

    // The set binds this mesh's per-mesh nodes/parent + the shared sort keys/payload + the shared visit
    // counter + this mesh's raw position/index buffers.
    Core::BindingSetDesc bindingSetDesc(arena());
    bindingSetDesc.addItem(Core::BindingSetItem::RawBuffer_SRV(NWB_BVH_BUILD_BINDING_POSITIONS, positionBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::RawBuffer_SRV(NWB_BVH_BUILD_BINDING_TRIANGLE_INDICES, triangleIndexBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_BVH_BUILD_BINDING_KEYS, rayTracingState().m_bvhSortKeysBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_BVH_BUILD_BINDING_PAYLOAD, rayTracingState().m_bvhSortPayloadBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_BVH_BUILD_BINDING_NODES, nodeBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_BVH_BUILD_BINDING_PARENT, parentBuffer));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_BVH_BUILD_BINDING_VISIT_COUNTER, rayTracingState().m_bvhVisitCounterBuffer.get()));

    bindingSet = graphics().getDevice()->createBindingSet(bindingSetDesc, rayTracingState().m_bvhBuildBindingLayout);
    if(!bindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create per-mesh BVH build binding set"));
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureMeshSwBvhResources(
    Core::Buffer* positionBuffer,
    Core::Buffer* triangleIndexBuffer,
    u32 primitiveCount,
    Core::BufferHandle& nodeBuffer,
    Core::BufferHandle& parentBuffer,
    Core::BindingSetHandle& bindingSet
){
    if(primitiveCount > s_BvhMaxPrimitivesPerMesh){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: mesh exceeds software BVH primitive cap ({} > {}), shadows skipped")
            , static_cast<u64>(primitiveCount)
            , static_cast<u64>(s_BvhMaxPrimitivesPerMesh)
        );
        return false;
    }

    if(!ensureBvhSortPipeline())
        return false;
    if(!ensureBvhBuildPipeline())
        return false;
    // Size the shared sort/counter scratch ONCE to the per-mesh cap (a power of two, so it is itself a valid
    // padded sort length). This keeps the shared buffers stable across builds of different-sized meshes, so
    // the per-mesh binding sets that reference them stay valid; the per-mesh node/parent are sized exactly.
    if(!ensureBvhSortBuffers(s_BvhMaxPrimitivesPerMesh))
        return false;
    if(!ensureBvhVisitCounterBuffer(s_BvhMaxPrimitivesPerMesh))
        return false;
    if(!createMeshBvhStorage(primitiveCount, nodeBuffer, parentBuffer))
        return false;
    return ensureMeshBvhBindingSet(positionBuffer, triangleIndexBuffer, nodeBuffer.get(), parentBuffer.get(), bindingSet);
}

bool RendererRayTracingSystem::buildMeshSwBvh(
    Core::CommandList& commandList,
    Core::Buffer* positionBuffer,
    Core::Buffer* triangleIndexBuffer,
    u32 primitiveCount,
    const SIMDVector aabbMin,
    const SIMDVector aabbMax,
    Core::BufferHandle& nodeBuffer,
    Core::BufferHandle& parentBuffer,
    Core::BindingSetHandle& bindingSet
){
    if(primitiveCount == 0u)
        return false;
    if(!ensureMeshSwBvhResources(positionBuffer, triangleIndexBuffer, primitiveCount, nodeBuffer, parentBuffer, bindingSet))
        return false;

    u32 paddedCount = static_cast<u32>(NWB_BVH_SORT_GROUP_SIZE);
    while(paddedCount < primitiveCount)
        paddedCount <<= 1u;

    Core::Buffer* keysBuffer = rayTracingState().m_bvhSortKeysBuffer.get();
    Core::Buffer* payloadBuffer = rayTracingState().m_bvhSortPayloadBuffer.get();
    Core::Buffer* visitCounterBuffer = rayTracingState().m_bvhVisitCounterBuffer.get();
    Core::Buffer* meshNodeBuffer = nodeBuffer.get();
    Core::Buffer* meshParentBuffer = parentBuffer.get();
    Core::BindingSet* meshBindingSet = bindingSet.get();

    BvhBuildPushConstants pushConstants;
    pushConstants.primitiveCount = primitiveCount;
    pushConstants.internalCount = primitiveCount - 1u;
    pushConstants.aabbMin = Float4(VectorGetX(aabbMin), VectorGetY(aabbMin), VectorGetZ(aabbMin), 0.0f);
    pushConstants.aabbMax = Float4(VectorGetX(aabbMax), VectorGetY(aabbMax), VectorGetZ(aabbMax), 0.0f);

    // Initialize: sort-key padding to a sentinel that sorts last, parent links to "no parent", and the
    // per-internal-node visit counters to zero (the fit's second-arrival rendezvous).
    commandList.setBufferState(keysBuffer, Core::ResourceStates::CopyDest);
    commandList.setBufferState(meshParentBuffer, Core::ResourceStates::CopyDest);
    commandList.setBufferState(visitCounterBuffer, Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.clearBufferUInt(keysBuffer, 0xFFFFFFFFu);
    commandList.clearBufferUInt(meshParentBuffer, BvhNodeIndex::Invalid);
    commandList.clearBufferUInt(visitCounterBuffer, 0u);

    commandList.setEnableUavBarriersForBuffer(keysBuffer, true);
    commandList.setEnableUavBarriersForBuffer(payloadBuffer, true);
    commandList.setEnableUavBarriersForBuffer(meshNodeBuffer, true);
    commandList.setEnableUavBarriersForBuffer(meshParentBuffer, true);
    commandList.setEnableUavBarriersForBuffer(visitCounterBuffer, true);

    const auto bvhBuildBarrier = [&commandList, keysBuffer, payloadBuffer, meshNodeBuffer, meshParentBuffer, visitCounterBuffer](){
        commandList.setBufferState(keysBuffer, Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(payloadBuffer, Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(meshNodeBuffer, Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(meshParentBuffer, Core::ResourceStates::UnorderedAccess);
        commandList.setBufferState(visitCounterBuffer, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();
    };

    const auto dispatchBuildKernel = [&commandList, &pushConstants, meshBindingSet](Core::ComputePipeline* pipeline, const u32 groupCount){
        Core::ComputeState computeState;
        computeState.setPipeline(pipeline);
        computeState.addBindingSet(meshBindingSet);
        commandList.setComputeState(computeState);
        commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
        commandList.dispatch(groupCount, 1u, 1u);
    };

    bvhBuildBarrier();

    dispatchBuildKernel(rayTracingState().m_bvhMortonPipeline.get(), DivideUp(primitiveCount, static_cast<u32>(NWB_BVH_BUILD_GROUP_SIZE)));
    bvhBuildBarrier();

    if(!bvhBitonicSort(commandList, primitiveCount, paddedCount))
        return false;
    bvhBuildBarrier();

    if(primitiveCount > 1u){
        dispatchBuildKernel(rayTracingState().m_bvhTopologyPipeline.get(), DivideUp(primitiveCount - 1u, static_cast<u32>(NWB_BVH_BUILD_GROUP_SIZE)));
        bvhBuildBarrier();
    }

    dispatchBuildKernel(rayTracingState().m_bvhFitPipeline.get(), DivideUp(primitiveCount, static_cast<u32>(NWB_BVH_BUILD_GROUP_SIZE)));
    bvhBuildBarrier();
    return true;
}

bool RendererRayTracingSystem::refitMeshSwBvh(
    Core::CommandList& commandList,
    Core::Buffer* positionBuffer,
    Core::Buffer* triangleIndexBuffer,
    u32 primitiveCount,
    Core::BufferHandle& nodeBuffer,
    Core::BufferHandle& parentBuffer,
    Core::BindingSetHandle& bindingSet
){
    if(primitiveCount == 0u)
        return false;
    if(!ensureMeshSwBvhResources(positionBuffer, triangleIndexBuffer, primitiveCount, nodeBuffer, parentBuffer, bindingSet))
        return false;

    Core::Buffer* meshNodeBuffer = nodeBuffer.get();
    Core::Buffer* meshParentBuffer = parentBuffer.get();
    Core::Buffer* visitCounterBuffer = rayTracingState().m_bvhVisitCounterBuffer.get();

    BvhBuildPushConstants pushConstants;
    pushConstants.primitiveCount = primitiveCount;
    pushConstants.internalCount = primitiveCount - 1u;
    pushConstants.refitMode = 1u;

    // A refit reuses the sorted topology, child links, and per-leaf primitive from the last full build, so
    // only the rendezvous counters reset; the fit pass recomputes every box from the current positions.
    commandList.setBufferState(visitCounterBuffer, Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.clearBufferUInt(visitCounterBuffer, 0u);

    commandList.setEnableUavBarriersForBuffer(meshNodeBuffer, true);
    commandList.setEnableUavBarriersForBuffer(meshParentBuffer, true);
    commandList.setEnableUavBarriersForBuffer(visitCounterBuffer, true);
    commandList.setBufferState(meshNodeBuffer, Core::ResourceStates::UnorderedAccess);
    commandList.setBufferState(meshParentBuffer, Core::ResourceStates::UnorderedAccess);
    commandList.setBufferState(visitCounterBuffer, Core::ResourceStates::UnorderedAccess);
    commandList.commitBarriers();

    Core::ComputeState computeState;
    computeState.setPipeline(rayTracingState().m_bvhFitPipeline.get());
    computeState.addBindingSet(bindingSet.get());
    commandList.setComputeState(computeState);
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
    commandList.dispatch(DivideUp(primitiveCount, static_cast<u32>(NWB_BVH_BUILD_GROUP_SIZE)), 1u, 1u);

    commandList.setBufferState(meshNodeBuffer, Core::ResourceStates::UnorderedAccess);
    commandList.commitBarriers();
    return true;
}

bool RendererRayTracingSystem::updateMeshSwBvh(Core::CommandList& commandList, MeshResources& meshResources){
    if(!meshResources.positionBuffer || !meshResources.triangleIndexBuffer)
        return false;
    // meshletPrimitiveIndexCount is the reconstructed triangle-index count (3 per triangle).
    if(meshResources.meshletPrimitiveIndexCount == 0u || (meshResources.meshletPrimitiveIndexCount % 3u) != 0u)
        return false;
    const u32 primitiveCount = meshResources.meshletPrimitiveIndexCount / 3u;

    // The morton / topology / fit kernels read positions and triangle indices as raw byte buffers, so move
    // both to ShaderResource before the build/refit dispatches bind them.
    commandList.setBufferState(meshResources.positionBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(meshResources.triangleIndexBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();

    // Skinned (runtime) meshes deform every frame: refit the build-pose topology in place from the freshly
    // skinned positions, forcing a full rebuild every s_BlasMaxRefitsBeforeRebuild frames to restore tree
    // quality. Static meshes build once. A mesh's first appearance is always a full build.
    const bool firstBuild = !meshResources.swBvhNodeBuffer;
    const bool performRefit =
        meshResources.runtimeMesh
        && !firstBuild
        && meshResources.swBvhRefitsSinceRebuild < s_BlasMaxRefitsBeforeRebuild
    ;

    bool built = false;
    if(performRefit){
        built = refitMeshSwBvh(
            commandList,
            meshResources.positionBuffer.get(),
            meshResources.triangleIndexBuffer.get(),
            primitiveCount,
            meshResources.swBvhNodeBuffer,
            meshResources.swBvhParentBuffer,
            meshResources.swBvhBindingSet
        );
    }
    else{
        const SIMDVector aabbMin = LoadFloatInt(meshResources.csgLocalBounds.minBounds);
        const SIMDVector aabbMax = LoadFloatInt(meshResources.csgLocalBounds.maxBounds);
        built = buildMeshSwBvh(
            commandList,
            meshResources.positionBuffer.get(),
            meshResources.triangleIndexBuffer.get(),
            primitiveCount,
            aabbMin,
            aabbMax,
            meshResources.swBvhNodeBuffer,
            meshResources.swBvhParentBuffer,
            meshResources.swBvhBindingSet
        );
    }
    if(!built)
        return false;

    if(firstBuild){
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: built software BVH for mesh '{}' (runtime {}, {} triangles)")
            , StringConvert(meshResources.meshName.c_str())
            , meshResources.runtimeMesh
            , static_cast<u64>(primitiveCount)
        );
    }

    meshResources.swBvhRefitsSinceRebuild = performRefit ? (meshResources.swBvhRefitsSinceRebuild + 1u) : 0u;
    return true;
}

bool RendererRayTracingSystem::ensureSceneBvhBuffers(u32 instanceCount){
    // A binary BVH over N instances has 2N-1 nodes. Both buffers are CPU-written each frame and read by the
    // traversal pass, so they are structured SRVs (no UAV) that grow by doubling like the hardware TLAS.
    const usize requiredNodes = static_cast<usize>(instanceCount) * 2u - 1u;
    if(!rayTracingState().m_sceneBvhNodeBuffer || rayTracingState().m_sceneBvhNodeCapacity < requiredNodes){
        usize capacity = rayTracingState().m_sceneBvhNodeCapacity > 0u
            ? rayTracingState().m_sceneBvhNodeCapacity
            : (s_SceneBvhInitialInstanceCapacity * 2u - 1u)
        ;
        while(capacity < requiredNodes)
            capacity *= 2u;

        Core::BufferDesc nodeBufferDesc;
        nodeBufferDesc
            .setByteSize(static_cast<u64>(sizeof(NwbBvhNodeGpu) * capacity))
            .setStructStride(sizeof(NwbBvhNodeGpu))
            .setDebugName(Name("scene_bvh_nodes"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        rayTracingState().m_sceneBvhNodeBuffer = graphics().createBuffer(nodeBufferDesc);
        if(!rayTracingState().m_sceneBvhNodeBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create scene BVH node buffer"));
            return false;
        }
        rayTracingState().m_sceneBvhNodeCapacity = capacity;
    }

    if(!rayTracingState().m_sceneInstanceBuffer || rayTracingState().m_sceneInstanceCapacity < instanceCount){
        usize capacity = rayTracingState().m_sceneInstanceCapacity > 0u
            ? rayTracingState().m_sceneInstanceCapacity
            : s_SceneBvhInitialInstanceCapacity
        ;
        while(capacity < instanceCount)
            capacity *= 2u;

        Core::BufferDesc instanceBufferDesc;
        instanceBufferDesc
            .setByteSize(static_cast<u64>(sizeof(SceneSwBvhInstanceGpu) * capacity))
            .setStructStride(sizeof(SceneSwBvhInstanceGpu))
            .setDebugName(Name("scene_bvh_instances"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        rayTracingState().m_sceneInstanceBuffer = graphics().createBuffer(instanceBufferDesc);
        if(!rayTracingState().m_sceneInstanceBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create scene BVH instance buffer"));
            return false;
        }
        rayTracingState().m_sceneInstanceCapacity = capacity;
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created software scene BVH buffers (capacity {} instances)")
            , static_cast<u64>(capacity)
        );
    }
    return true;
}

bool RendererRayTracingSystem::ensureCausticEmissionTargetBuffer(usize targetCount){
    // The caustic emission-target list is CPU-written each frame and (eventually) read by the caustic producer,
    // so it is a structured SRV (no UAV) that grows by doubling like the scene-BVH / instance-material buffers.
    if(rayTracingState().m_causticEmissionTargetBuffer && rayTracingState().m_causticEmissionTargetCapacity >= targetCount)
        return true;

    usize capacity = rayTracingState().m_causticEmissionTargetCapacity > 0u
        ? rayTracingState().m_causticEmissionTargetCapacity
        : s_CausticEmissionTargetInitialCapacity
    ;
    while(capacity < targetCount)
        capacity *= 2u;

    Core::BufferDesc targetBufferDesc;
    targetBufferDesc
        .setByteSize(static_cast<u64>(sizeof(NwbCausticEmissionTargetGpu) * capacity))
        .setStructStride(sizeof(NwbCausticEmissionTargetGpu))
        .setDebugName(Name("caustic_emission_targets"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    rayTracingState().m_causticEmissionTargetBuffer = graphics().createBuffer(targetBufferDesc);
    if(!rayTracingState().m_causticEmissionTargetBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic emission-target buffer"));
        return false;
    }
    rayTracingState().m_causticEmissionTargetCapacity = capacity;
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created caustic emission-target buffer (capacity {} targets)")
        , static_cast<u64>(capacity)
    );
    return true;
}

bool RendererRayTracingSystem::ensureShadowInstanceMaterialBuffer(usize instanceCount){
    // The per-instance occluder material table is CPU-written each frame and read by the shadow shaders, so it
    // is a structured SRV (no UAV) that grows by doubling like the TLAS / scene-instance buffers. Shared by the
    // hardware and software backends (only one runs per frame), built lockstep with that backend's instances.
    if(rayTracingState().m_shadowInstanceMaterialBuffer && rayTracingState().m_shadowInstanceMaterialCapacity >= instanceCount)
        return true;

    usize capacity = rayTracingState().m_shadowInstanceMaterialCapacity > 0u
        ? rayTracingState().m_shadowInstanceMaterialCapacity
        : s_ShadowInstanceMaterialInitialCapacity
    ;
    while(capacity < instanceCount)
        capacity *= 2u;

    Core::BufferDesc materialBufferDesc;
    materialBufferDesc
        .setByteSize(static_cast<u64>(sizeof(NwbRtInstanceMaterialGpu) * capacity))
        .setStructStride(sizeof(NwbRtInstanceMaterialGpu))
        .setDebugName(Name("shadow_instance_material"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    rayTracingState().m_shadowInstanceMaterialBuffer = graphics().createBuffer(materialBufferDesc);
    if(!rayTracingState().m_shadowInstanceMaterialBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow instance material buffer"));
        return false;
    }
    rayTracingState().m_shadowInstanceMaterialCapacity = capacity;
    return true;
}

bool RendererRayTracingSystem::ensureShadowInstanceContextBuffer(usize instanceCount){
    // Shadow-owned combined instance buffer (g_NwbMeshInstances for the trace): InstanceGpuData per occluder,
    // structured SRV, grows by doubling like the draw pass's instance buffer. Built each frame over ALL gathered
    // occluders so the trace's surface hook can resolve the mutable storage offset that lives in translation.w.
    if(instanceCount == 0u)
        return true;
    if(rayTracingState().m_shadowInstanceBuffer && rayTracingState().m_shadowInstanceCapacity >= instanceCount)
        return true;

    const usize capacity = ECSRenderDetail::NextGrowingCapacity(rayTracingState().m_shadowInstanceCapacity, instanceCount);
    Core::BufferDesc instanceBufferDesc;
    instanceBufferDesc
        .setByteSize(static_cast<u64>(capacity * sizeof(InstanceGpuData)))
        .setStructStride(sizeof(InstanceGpuData))
        .setDebugName(Name("shadow_instance_context"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    rayTracingState().m_shadowInstanceBuffer = graphics().createBuffer(instanceBufferDesc);
    if(!rayTracingState().m_shadowInstanceBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow instance context buffer"));
        return false;
    }
    rayTracingState().m_shadowInstanceCapacity = capacity;
    return true;
}

bool RendererRayTracingSystem::ensureShadowMaterialTypedBuffer(usize byteCount){
    // Shadow-owned combined material-typed buffer (g_NwbMaterialTypedWords for the trace): each occluder's
    // constant + mutable typed blocks, word-strided structured SRV, grows by doubling like the draw pass's typed
    // buffer. Always at least one word so the binding is valid even with no transparent occluders.
    usize requiredByteCount = Max<usize>(byteCount, sizeof(u32));
    requiredByteCount = AlignUp(requiredByteCount, sizeof(u32));
    if(rayTracingState().m_shadowMaterialTypedBuffer && rayTracingState().m_shadowMaterialTypedCapacity >= requiredByteCount)
        return true;

    const usize capacity = ECSRenderDetail::NextGrowingCapacity(rayTracingState().m_shadowMaterialTypedCapacity, requiredByteCount);
    Core::BufferDesc materialTypedBufferDesc;
    materialTypedBufferDesc
        .setByteSize(static_cast<u64>(capacity))
        .setStructStride(sizeof(u32))
        .setDebugName(Name("shadow_material_typed"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    rayTracingState().m_shadowMaterialTypedBuffer = graphics().createBuffer(materialTypedBufferDesc);
    if(!rayTracingState().m_shadowMaterialTypedBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow material typed buffer"));
        return false;
    }
    rayTracingState().m_shadowMaterialTypedCapacity = capacity;
    return true;
}

bool RendererRayTracingSystem::uploadShadowMaterialContextBuffers(
    Core::CommandList& commandList,
    const InstanceGpuDataVector& instanceData,
    const MaterialTypedByteDataVector& materialTypedBytes
){
    // The combined typed buffer always has content (at minimum the padded word reserved below) so the trace's
    // material-context binding is always valid; the instance buffer may be empty only when no occluder resolved a
    // material, in which case the trace never indexes it (no transparent hit dispatches).
    usize uploadBytes = 0u;
    if(!ECSRenderDetail::ResolveMaterialTypedUploadByteCount(materialTypedBytes, uploadBytes))
        return false;

    if(!ensureShadowInstanceContextBuffer(instanceData.size()) || !ensureShadowMaterialTypedBuffer(uploadBytes))
        return false;

    if(!instanceData.empty()){
        Core::Buffer* instanceBuffer = rayTracingState().m_shadowInstanceBuffer.get();
        commandList.setBufferState(instanceBuffer, Core::ResourceStates::CopyDest);
        commandList.commitBarriers();
        commandList.writeBuffer(instanceBuffer, instanceData.data(), instanceData.size() * sizeof(InstanceGpuData));
        commandList.setBufferState(instanceBuffer, Core::ResourceStates::ShaderResource);
        commandList.commitBarriers();
    }

    Core::Buffer* materialTypedBuffer = rayTracingState().m_shadowMaterialTypedBuffer.get();
    commandList.setBufferState(materialTypedBuffer, Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(materialTypedBuffer, materialTypedBytes.data(), uploadBytes);
    commandList.setBufferState(materialTypedBuffer, Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_DEBUG)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererRayTracingSystem::runBvhSortSelfTest(){
    if(rayTracingState().m_bvhSortSelfTestDone)
        return;
    rayTracingState().m_bvhSortSelfTestDone = true;

    // A wrong sort silently corrupts every BVH built on top of it, so verify the kernel directly in debug
    // builds: sort a reversed sequence with an identity payload and read it back. The ascending result must
    // be exactly 0..elementCount-1, with the sentinel-padded tail still non-decreasing.
    constexpr u32 elementCount = 1000u;
    constexpr u32 paddedCount = 1024u;
    static_assert(paddedCount >= static_cast<u32>(NWB_BVH_SORT_GROUP_SIZE), "self-test padded count must cover one dispatch group");

    if(!ensureBvhSortPipeline())
        return;
    if(!ensureBvhSortBuffers(paddedCount))
        return;

    auto* device = graphics().getDevice();

    u32 inputKeys[paddedCount];
    u32 inputPayload[paddedCount];
    for(u32 i = 0u; i < paddedCount; ++i){
        inputKeys[i] = i < elementCount ? (elementCount - 1u - i) : 0xFFFFFFFFu;
        inputPayload[i] = i;
    }

    Core::BufferDesc readbackBufferDesc;
    readbackBufferDesc
        .setByteSize(static_cast<u64>(sizeof(u32) * paddedCount))
        .setCpuAccess(Core::CpuAccessMode::Read)
        .setDebugName(Name("bvh_sort_selftest_readback"))
        .enableAutomaticStateTracking(Core::ResourceStates::CopyDest)
    ;
    Core::BufferHandle readbackBuffer = graphics().createBuffer(readbackBufferDesc);
    if(!readbackBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH sort self-test readback buffer"));
        return;
    }

    Core::CommandListHandle commandList = device->createCommandList();
    if(!commandList){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH sort self-test command list"));
        return;
    }

    commandList->open();
    commandList->setBufferState(rayTracingState().m_bvhSortKeysBuffer.get(), Core::ResourceStates::CopyDest);
    commandList->setBufferState(rayTracingState().m_bvhSortPayloadBuffer.get(), Core::ResourceStates::CopyDest);
    commandList->commitBarriers();
    commandList->writeBuffer(rayTracingState().m_bvhSortKeysBuffer.get(), inputKeys, sizeof(inputKeys));
    commandList->writeBuffer(rayTracingState().m_bvhSortPayloadBuffer.get(), inputPayload, sizeof(inputPayload));
    commandList->setBufferState(rayTracingState().m_bvhSortKeysBuffer.get(), Core::ResourceStates::UnorderedAccess);
    commandList->setBufferState(rayTracingState().m_bvhSortPayloadBuffer.get(), Core::ResourceStates::UnorderedAccess);
    commandList->commitBarriers();

    if(!bvhBitonicSort(*commandList, elementCount, paddedCount)){
        commandList->close();
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: BVH sort self-test dispatch setup failed"));
        return;
    }

    commandList->setBufferState(rayTracingState().m_bvhSortKeysBuffer.get(), Core::ResourceStates::CopySource);
    commandList->commitBarriers();
    commandList->copyBuffer(readbackBuffer.get(), 0u, rayTracingState().m_bvhSortKeysBuffer.get(), 0u, static_cast<u64>(sizeof(u32) * paddedCount));
    commandList->close();

    Core::CommandList* commandLists[] = { commandList.get() };
    device->executeCommandLists(commandLists, 1u);
    if(!device->waitForIdle()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: BVH sort self-test wait-for-idle failed"));
        return;
    }

    const u32* sortedKeys = static_cast<const u32*>(device->mapBuffer(readbackBuffer.get(), Core::CpuAccessMode::Read));
    if(!sortedKeys){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to map BVH sort self-test readback buffer"));
        return;
    }

    bool sorted = true;
    for(u32 i = 0u; i < elementCount; ++i){
        if(sortedKeys[i] != i){
            sorted = false;
            break;
        }
    }
    for(u32 i = 0u; sorted && (i + 1u) < paddedCount; ++i){
        if(sortedKeys[i] > sortedKeys[i + 1u]){
            sorted = false;
            break;
        }
    }
    device->unmapBuffer(readbackBuffer.get());

    if(sorted)
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: BVH bitonic sort self-test PASSED ({} elements)"), elementCount);
    else
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: BVH bitonic sort self-test FAILED"));
}

void RendererRayTracingSystem::runBvhBuildSelfTest(){
    if(rayTracingState().m_bvhBuildSelfTestDone)
        return;
    rayTracingState().m_bvhBuildSelfTestDone = true;

    // Build an LBVH for a known triangle set and validate it on the CPU. A wrong topology can still bound
    // everything correctly, so this checks leaf coverage and child-box nesting, not just the root box.
    constexpr u32 triangleCount = 16u;
    constexpr u32 vertexCount = triangleCount * 3u;
    constexpr u32 floatCount = vertexCount * 3u;
    constexpr u32 nodeCount = triangleCount * 2u - 1u;
    constexpr u32 internalCount = triangleCount - 1u;

    f32 positionData[floatCount];
    u32 indexData[vertexCount];
    for(u32 triangle = 0u; triangle < triangleCount; ++triangle){
        const f32 baseX = static_cast<f32>(triangle);
        const u32 floatBase = triangle * 9u;
        positionData[floatBase + 0u] = baseX;         positionData[floatBase + 1u] = 0.0f;  positionData[floatBase + 2u] = 0.0f;
        positionData[floatBase + 3u] = baseX + 0.6f;  positionData[floatBase + 4u] = 0.8f;  positionData[floatBase + 5u] = 0.1f;
        positionData[floatBase + 6u] = baseX + 0.2f;  positionData[floatBase + 7u] = 0.1f;  positionData[floatBase + 8u] = 0.9f;
    }
    for(u32 index = 0u; index < vertexCount; ++index)
        indexData[index] = index;

    SIMDVector boundsMinVector = VectorReplicate(1e30f);
    SIMDVector boundsMaxVector = VectorReplicate(-1e30f);
    for(u32 vertex = 0u; vertex < vertexCount; ++vertex){
        const SIMDVector position = VectorSet(positionData[vertex * 3u + 0u], positionData[vertex * 3u + 1u], positionData[vertex * 3u + 2u], 0.0f);
        boundsMinVector = VectorMin(boundsMinVector, position);
        boundsMaxVector = VectorMax(boundsMaxVector, position);
    }
    auto* device = graphics().getDevice();

    // Per-mesh BVH storage now lives with the caller (here, the self-test); the build/refit helpers create
    // and reuse these on first use, mirroring how MeshResources will own them on the real path.
    Core::BufferHandle testNodeBuffer;
    Core::BufferHandle testParentBuffer;
    Core::BindingSetHandle testBindingSet;

    Core::BufferDesc positionBufferDesc;
    positionBufferDesc
        .setByteSize(sizeof(positionData))
        .setCanHaveRawViews(true)
        .setDebugName(Name("bvh_build_selftest_positions"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle positionBuffer = graphics().createBuffer(positionBufferDesc);

    Core::BufferDesc indexBufferDesc;
    indexBufferDesc
        .setByteSize(sizeof(indexData))
        .setCanHaveRawViews(true)
        .setDebugName(Name("bvh_build_selftest_indices"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle indexBuffer = graphics().createBuffer(indexBufferDesc);
    if(!positionBuffer || !indexBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH build self-test geometry buffers"));
        return;
    }

    Core::BufferDesc readbackBufferDesc;
    readbackBufferDesc
        .setByteSize(static_cast<u64>(sizeof(NwbBvhNodeGpu) * nodeCount))
        .setCpuAccess(Core::CpuAccessMode::Read)
        .setDebugName(Name("bvh_build_selftest_readback"))
        .enableAutomaticStateTracking(Core::ResourceStates::CopyDest)
    ;
    Core::BufferHandle readbackBuffer = graphics().createBuffer(readbackBufferDesc);
    if(!readbackBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH build self-test readback buffer"));
        return;
    }

    Core::CommandListHandle commandList = device->createCommandList();
    if(!commandList){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH build self-test command list"));
        return;
    }

    commandList->open();
    commandList->setBufferState(positionBuffer.get(), Core::ResourceStates::CopyDest);
    commandList->setBufferState(indexBuffer.get(), Core::ResourceStates::CopyDest);
    commandList->commitBarriers();
    commandList->writeBuffer(positionBuffer.get(), positionData, sizeof(positionData));
    commandList->writeBuffer(indexBuffer.get(), indexData, sizeof(indexData));
    commandList->setBufferState(positionBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList->setBufferState(indexBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList->commitBarriers();

    if(!buildMeshSwBvh(*commandList, positionBuffer.get(), indexBuffer.get(), triangleCount, boundsMinVector, boundsMaxVector, testNodeBuffer, testParentBuffer, testBindingSet)){
        commandList->close();
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: BVH build self-test build failed"));
        return;
    }

    commandList->setBufferState(testNodeBuffer.get(), Core::ResourceStates::CopySource);
    commandList->commitBarriers();
    commandList->copyBuffer(readbackBuffer.get(), 0u, testNodeBuffer.get(), 0u, static_cast<u64>(sizeof(NwbBvhNodeGpu) * nodeCount));
    commandList->close();

    Core::CommandList* commandLists[] = { commandList.get() };
    device->executeCommandLists(commandLists, 1u);
    if(!device->waitForIdle()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: BVH build self-test wait-for-idle failed"));
        return;
    }

    const NwbBvhNodeGpu* nodes = static_cast<const NwbBvhNodeGpu*>(device->mapBuffer(readbackBuffer.get(), Core::CpuAccessMode::Read));
    if(!nodes){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to map BVH build self-test readback buffer"));
        return;
    }

    bool valid = true;

    // (1) Leaf coverage: every leaf node carries the flag + a unique primitive, and all primitives appear.
    bool primitiveSeen[triangleCount] = {};
    for(u32 leaf = 0u; valid && leaf < triangleCount; ++leaf){
        const NwbBvhNodeGpu& node = nodes[internalCount + leaf];
        if((node.aabbMinLeftChild.w & BvhNodeIndex::LeafFlag) == 0u){
            valid = false;
            break;
        }
        const u32 primitive = node.aabbMinLeftChild.w & ~BvhNodeIndex::LeafFlag;
        if(primitive >= triangleCount || primitiveSeen[primitive]){
            valid = false;
            break;
        }
        primitiveSeen[primitive] = true;
    }
    for(u32 primitive = 0u; valid && primitive < triangleCount; ++primitive){
        if(!primitiveSeen[primitive])
            valid = false;
    }

    // (2) Root box matches the input bounds.
    const SIMDVector epsilonVector = VectorReplicate(1e-3f);
    if(valid){
        const SIMDVector rootMin = LoadFloatInt(nodes[0].aabbMinLeftChild);
        const SIMDVector rootMax = LoadFloatInt(nodes[0].aabbMaxRightChild);
        if(
            !Vector3LessOrEqual(VectorAbs(VectorSubtract(rootMin, boundsMinVector)), epsilonVector)
            || !Vector3LessOrEqual(VectorAbs(VectorSubtract(rootMax, boundsMaxVector)), epsilonVector)
        )
            valid = false;
    }

    // (3) Every internal node references valid children and its box contains both child boxes.
    for(u32 internal = 0u; valid && internal < internalCount; ++internal){
        const NwbBvhNodeGpu& node = nodes[internal];
        if(node.aabbMinLeftChild.w >= nodeCount || node.aabbMaxRightChild.w >= nodeCount){
            valid = false;
            break;
        }
        const NwbBvhNodeGpu& left = nodes[node.aabbMinLeftChild.w];
        const NwbBvhNodeGpu& right = nodes[node.aabbMaxRightChild.w];
        const SIMDVector childMin = VectorMin(LoadFloatInt(left.aabbMinLeftChild), LoadFloatInt(right.aabbMinLeftChild));
        const SIMDVector childMax = VectorMax(LoadFloatInt(left.aabbMaxRightChild), LoadFloatInt(right.aabbMaxRightChild));
        if(
            !Vector3LessOrEqual(LoadFloatInt(node.aabbMinLeftChild), VectorAdd(childMin, epsilonVector))
            || !Vector3GreaterOrEqual(LoadFloatInt(node.aabbMaxRightChild), VectorSubtract(childMax, epsilonVector))
        )
            valid = false;
    }

    device->unmapBuffer(readbackBuffer.get());

    if(valid)
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: BVH build self-test PASSED ({} triangles, {} nodes)"), triangleCount, nodeCount);
    else
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: BVH build self-test FAILED"));

    // Refit phase: translate every vertex and refit the EXISTING topology. A refit reuses the build-pose
    // tree and only recomputes boxes from the current positions, so the root box must track the translation.
    for(u32 vertex = 0u; vertex < vertexCount; ++vertex)
        positionData[vertex * 3u + 0u] += 5.0f;
    const SIMDVector refitOffset = VectorSet(5.0f, 0.0f, 0.0f, 0.0f);
    const SIMDVector refitMin = VectorAdd(boundsMinVector, refitOffset);
    const SIMDVector refitMax = VectorAdd(boundsMaxVector, refitOffset);

    Core::BufferHandle refitReadbackBuffer = graphics().createBuffer(readbackBufferDesc);
    Core::CommandListHandle refitCommandList = device->createCommandList();
    if(!refitReadbackBuffer || !refitCommandList){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create BVH refit self-test resources"));
        return;
    }

    refitCommandList->open();
    refitCommandList->setBufferState(positionBuffer.get(), Core::ResourceStates::CopyDest);
    refitCommandList->commitBarriers();
    refitCommandList->writeBuffer(positionBuffer.get(), positionData, sizeof(positionData));
    refitCommandList->setBufferState(positionBuffer.get(), Core::ResourceStates::ShaderResource);
    refitCommandList->commitBarriers();

    if(!refitMeshSwBvh(*refitCommandList, positionBuffer.get(), indexBuffer.get(), triangleCount, testNodeBuffer, testParentBuffer, testBindingSet)){
        refitCommandList->close();
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: BVH refit self-test refit failed"));
        return;
    }

    refitCommandList->setBufferState(testNodeBuffer.get(), Core::ResourceStates::CopySource);
    refitCommandList->commitBarriers();
    refitCommandList->copyBuffer(refitReadbackBuffer.get(), 0u, testNodeBuffer.get(), 0u, static_cast<u64>(sizeof(NwbBvhNodeGpu) * nodeCount));
    refitCommandList->close();

    Core::CommandList* refitCommandLists[] = { refitCommandList.get() };
    device->executeCommandLists(refitCommandLists, 1u);
    if(!device->waitForIdle()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: BVH refit self-test wait-for-idle failed"));
        return;
    }

    const NwbBvhNodeGpu* refitNodes = static_cast<const NwbBvhNodeGpu*>(device->mapBuffer(refitReadbackBuffer.get(), Core::CpuAccessMode::Read));
    if(!refitNodes){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to map BVH refit self-test readback buffer"));
        return;
    }
    const bool refitValid =
        Vector3LessOrEqual(VectorAbs(VectorSubtract(LoadFloatInt(refitNodes[0].aabbMinLeftChild), refitMin)), epsilonVector)
        && Vector3LessOrEqual(VectorAbs(VectorSubtract(LoadFloatInt(refitNodes[0].aabbMaxRightChild), refitMax)), epsilonVector)
    ;
    device->unmapBuffer(refitReadbackBuffer.get());

    if(refitValid)
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: BVH refit self-test PASSED ({} triangles)"), triangleCount);
    else
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: BVH refit self-test FAILED"));
}

void RendererRayTracingSystem::runSceneBvhSelfTest(){
    if(rayTracingState().m_sceneBvhSelfTestDone)
        return;
    rayTracingState().m_sceneBvhSelfTestDone = true;

    // The scene/instance BVH is CPU-built, so validate the builder directly: build a tree over known instance
    // AABBs and check leaf coverage, the root box, and child-box nesting (a wrong topology can still bound
    // everything, so the structural checks matter, not just the root box).
    constexpr u32 instanceCount = 12u;
    constexpr u32 nodeCount = instanceCount * 2u - 1u;

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RayTracingBuildArena, 16u * 1024u);

    Vector<SIMDVector, Core::Alloc::ScratchArena> instanceAabbMin{ scratchArena };
    Vector<SIMDVector, Core::Alloc::ScratchArena> instanceAabbMax{ scratchArena };
    Vector<SIMDVector, Core::Alloc::ScratchArena> instanceCentroid{ scratchArena };
    instanceAabbMin.reserve(instanceCount);
    instanceAabbMax.reserve(instanceCount);
    instanceCentroid.reserve(instanceCount);

    SIMDVector boundsMin = VectorReplicate(1e30f);
    SIMDVector boundsMax = VectorReplicate(-1e30f);
    for(u32 i = 0u; i < instanceCount; ++i){
        const f32 base = static_cast<f32>(i) * 3.0f;
        const SIMDVector boxMin = VectorSet(base, base * 0.25f, -base * 0.5f, 0.0f);
        const SIMDVector boxMax = VectorAdd(boxMin, VectorSet(1.5f, 2.0f, 1.0f, 0.0f));

        instanceAabbMin.push_back(boxMin);
        instanceAabbMax.push_back(boxMax);
        instanceCentroid.push_back(VectorScale(VectorAdd(boxMin, boxMax), 0.5f));

        boundsMin = VectorMin(boundsMin, boxMin);
        boundsMax = VectorMax(boundsMax, boxMax);
    }

    Vector<u32, Core::Alloc::ScratchArena> indices{ scratchArena };
    indices.reserve(instanceCount);
    for(u32 i = 0u; i < instanceCount; ++i)
        indices.push_back(i);

    Vector<SceneBvhNodeBuildData, Core::Alloc::ScratchArena> nodes{ scratchArena };
    nodes.reserve(nodeCount);
    __hidden_raytracing_system::BuildSceneBvhNode(indices.data(), 0u, instanceCount, instanceAabbMin.data(), instanceAabbMax.data(), instanceCentroid.data(), nodes);

    bool valid = (nodes.size() == nodeCount);

    // (1) Leaf coverage: every instance appears in exactly one leaf, and every leaf is a valid instance.
    bool instanceSeen[instanceCount] = {};
    u32 leafCount = 0u;
    for(u32 i = 0u; valid && i < nodes.size(); ++i){
        const u32 leftChild = nodes[i].leftChild;
        if((leftChild & BvhNodeIndex::LeafFlag) == 0u)
            continue;
        ++leafCount;
        const u32 instance = leftChild & ~BvhNodeIndex::LeafFlag;
        if(instance >= instanceCount || instanceSeen[instance]){
            valid = false;
            break;
        }
        instanceSeen[instance] = true;
    }
    if(valid && leafCount != instanceCount)
        valid = false;
    for(u32 i = 0u; valid && i < instanceCount; ++i){
        if(!instanceSeen[i])
            valid = false;
    }

    // (2) Root box == union of all instance AABBs.
    const SIMDVector epsilonVector = VectorReplicate(1e-3f);
    if(valid){
        if(
            !Vector3LessOrEqual(VectorAbs(VectorSubtract(nodes[0].aabbMin, boundsMin)), epsilonVector)
            || !Vector3LessOrEqual(VectorAbs(VectorSubtract(nodes[0].aabbMax, boundsMax)), epsilonVector)
        )
            valid = false;
    }

    // (3) Every internal node references valid children and its box contains both child boxes.
    for(u32 i = 0u; valid && i < nodes.size(); ++i){
        const u32 leftChild = nodes[i].leftChild;
        if((leftChild & BvhNodeIndex::LeafFlag) != 0u)
            continue;
        const u32 rightChild = nodes[i].rightChild;
        if(leftChild >= nodes.size() || rightChild >= nodes.size()){
            valid = false;
            break;
        }
        const SIMDVector childMin = VectorMin(nodes[leftChild].aabbMin, nodes[rightChild].aabbMin);
        const SIMDVector childMax = VectorMax(nodes[leftChild].aabbMax, nodes[rightChild].aabbMax);
        if(
            !Vector3LessOrEqual(nodes[i].aabbMin, VectorAdd(childMin, epsilonVector))
            || !Vector3GreaterOrEqual(nodes[i].aabbMax, VectorSubtract(childMax, epsilonVector))
        )
            valid = false;
    }

    if(valid)
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: scene BVH self-test PASSED ({} instances, {} nodes)"), instanceCount, nodeCount);
    else
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: scene BVH self-test FAILED"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

