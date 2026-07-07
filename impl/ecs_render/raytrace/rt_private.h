// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/raytrace/raytracing_system.h>
#include <impl/ecs_render/kernel/renderer_private.h>
#include <impl/ecs_render/material/material_instance.h>  // GetMaterialMutableHalf4 (per-instance GI/caustic hit colour)
#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/assets/graphics/shadow/binding_slots.h>
#include <impl/assets/graphics/shadow/sw_binding_slots.h>
#include <impl/assets/graphics/shadow/shadow_resolve_binding_slots.h>
#include <impl/assets/graphics/shadow/shadow_reproject_merge_binding_slots.h>
#include <impl/assets/graphics/shadow/names.h>
#include <impl/assets/graphics/caustic/sw_binding_slots.h>
#include <impl/assets/graphics/caustic/hw_binding_slots.h>
#include <impl/assets/graphics/caustic/resolve_binding_slots.h>
#include <impl/assets/graphics/caustic/names.h>
#include <impl/assets/graphics/bvh/binding_slots.h>
#include <impl/assets/graphics/bvh/names.h>
#include <impl/assets/graphics/gi/names.h>
#include <impl/assets/graphics/gi/sw_binding_slots.h>
#include <impl/assets/graphics/gi/surfel/surfel_binding_slots.h>
#include <global/environment.h>
#include <global/text_utils.h>


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

// Stage-2 adaptive shadow edge-fraction instrumentation cadence, in transparent-shadow dispatches: snapshot the GPU
// counter into the CPU-readable buffer every s_SwShadowEdgeStatsPeriod ticks, then log it s_SwShadowEdgeStatsLogDelay
// ticks later -- by which point the copy is GPU-complete, so the map needs no stall (the delay is comfortably deeper than
// any frames-in-flight count).
inline constexpr u32 s_SwShadowEdgeStatsPeriod = 120u;
inline constexpr u32 s_SwShadowEdgeStatsLogDelay = 8u;

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
    Float4 aabbMin;
    Float4 aabbMax;
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

// Binned-SAH scene-BVH build tuning. BuildSceneBvhNode evaluates a fixed-grid SAH over all three centroid axes at
// every split and takes the lowest-cost bin boundary, replacing the old largest-axis spatial-median split. Each
// instance carries a leaf cost (its primitive count in production) so a large instance biases the tree like a large
// primitive would; when none is supplied every instance counts uniformly (the self-test path). Leaves remain
// single-instance (the NwbBvhNode leaf ABI encodes one instance), so SAH here only chooses split axis + position;
// s_SceneBvhSahBinCount is the per-axis bin grid size, s_SceneBvhSahTraversalCost the classic ct of the
// cost = ct + (SA_L*cost_L + SA_R*cost_R)/SA_parent model (the per-instance intersection cost ci is folded into the
// leaf-cost weight itself, so a uniform-weight build is implicitly ci = 1).
inline constexpr u32 s_SceneBvhSahBinCount = 12u;
inline constexpr f32 s_SceneBvhSahTraversalCost = 1.0f;

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

// CPU/GPU record of one caustic emission target (P1): the world-space AABB of a refractive instance. The caustic
// photon producer aims its light-side emission domain at these boxes; all caustic lights share one global target list.
// A tight 32-byte std430-friendly record ({ float4 aabbMin; float4 aabbMax; }); the w lanes are unused padding (kept
// for SIMD-friendly 16-byte lanes / std430 float4 alignment).
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
    // Per-instance base colour (asuint of the linear RGB). The software probe/photon producers
    // (GI, caustics) read this to shade a bounce with the surface's authored colour instead of a flat default; the
    // shadow trace ignores it. Defaults to the neutral GI albedo so an unresolved material still bounces mid-grey.
    u32 baseColorR = 0u;
    u32 baseColorG = 0u;
    u32 baseColorB = 0u;
};
static_assert(sizeof(NwbRtInstanceMaterialGpu) == 32u, "NwbRtInstanceMaterialGpu must match the shader NwbRtInstanceMaterial std430 layout (8 x uint)");

// Per-instance shadow-occluder flags (NwbRtInstanceMaterialGpu.flags), mirroring the shader-side
// NWB_RT_INSTANCE_MATERIAL_FLAG_* defines: `Transparent` = evaluate the per-hit transmittance hook; `Refractive` =
// the material asset's `refractive` classification for the caustic producer. The shadow trace does NOT gate on
// `Refractive` -- transmittance is unified (each material's surface hook owns the final value, a refractive hook
// computing it via the engine helper). This is the producer's classification.
namespace RtInstanceMaterialFlag{
    enum Mask : u32{
        None = 0u,
        Transparent = 1u << 0u,
        Refractive = 1u << 1u,
    };
}

// Initial element capacity of the per-frame instance-material table; grows by doubling like the TLAS/scene BVH.
inline constexpr usize s_ShadowInstanceMaterialInitialCapacity = 128u;

// CPU mirrors of the software shadow traversal push constants. Each pass kernel declares its own minimal push struct,
// and every mirror matches its kernel's [[vk::push_constant]] layout exactly (asserted). The shared binding layout sizes
// its push range to the largest mirror below so every per-pass pipeline is layout-compatible.

// Opaque pre-pass (full-res OPAQUE binary blocker; sw_shadow_opaque_prepass_cs). The SW-path baseline opaque mask -- soft
// opaque overwrites it per soft slot when ready, else it IS the shadow (the fallback).
struct SwShadowOpaquePrepassPushConstants{
    u32 width = 0u;
    u32 height = 0u;
    u32 instanceCount = 0u;
};
static_assert(sizeof(SwShadowOpaquePrepassPushConstants) == sizeof(u32) * 3u, "SwShadowOpaquePrepassPushConstants must match the kernel push-constant layout");

// Soft opaque half-res jittered trace (sw_shadow_soft_opaque_cs, all light types): frameIndex seeds the per-pixel cone jitter.
struct SwShadowSoftOpaquePushConstants{
    u32 width = 0u;
    u32 height = 0u;
    u32 instanceCount = 0u;
    u32 frameIndex = 0u;
};
static_assert(sizeof(SwShadowSoftOpaquePushConstants) == sizeof(u32) * 4u, "SwShadowSoftOpaquePushConstants must match the kernel push-constant layout");

// Soft COLORED TRANSPARENT half-res jittered trace (sw_shadow_transparent_soft_cs, all light types): identical
// layout to the soft opaque trace (frameIndex seeds the per-pixel cone jitter -- the shader adds a compile-time salt to
// decorrelate its low-discrepancy stream from the opaque trace's). A distinct type so the two dispatches cannot swap args.
struct SwShadowTransparentSoftPushConstants{
    u32 width = 0u;
    u32 height = 0u;
    u32 instanceCount = 0u;
    u32 frameIndex = 0u;
};
static_assert(sizeof(SwShadowTransparentSoftPushConstants) == sizeof(u32) * 4u, "SwShadowTransparentSoftPushConstants must match the kernel push-constant layout");

// Stage-2 COARSE transparent trace (sw_shadow_transparent_coarse_cs): one TRANSPARENT trace per coarse block, colored.
struct SwShadowTransparentCoarsePushConstants{
    u32 width = 0u;
    u32 height = 0u;
    u32 instanceCount = 0u;
    u32 coarseWidth = 0u;
    u32 coarseHeight = 0u;
};
static_assert(sizeof(SwShadowTransparentCoarsePushConstants) == sizeof(u32) * 5u, "SwShadowTransparentCoarsePushConstants must match the kernel push-constant layout");

// Stage-2 ADAPTIVE transparent resolve (sw_shadow_transparent_resolve_cs): interp / re-trace + multiply, stats optional.
struct SwShadowTransparentResolvePushConstants{
    u32 width = 0u;
    u32 height = 0u;
    u32 instanceCount = 0u;
    u32 coarseWidth = 0u;
    u32 coarseHeight = 0u;
    f32 edgeThreshold = 0.1f;
    u32 collectStats = 0u;
};
static_assert(sizeof(SwShadowTransparentResolvePushConstants) == sizeof(u32) * 7u, "SwShadowTransparentResolvePushConstants must match the kernel push-constant layout");

// Stage-3 CLASSIFY + append (sw_shadow_transparent_classify_cs): interior fold in place / edge append; no trace.
struct SwShadowTransparentClassifyPushConstants{
    u32 width = 0u;
    u32 height = 0u;
    u32 coarseWidth = 0u;
    u32 coarseHeight = 0u;
    f32 edgeThreshold = 0.1f;
    u32 collectStats = 0u;
    u32 edgeCapacity = 0u;
};
static_assert(sizeof(SwShadowTransparentClassifyPushConstants) == sizeof(u32) * 7u, "SwShadowTransparentClassifyPushConstants must match the kernel push-constant layout");

// Stage-3 BUILD ARGS (sw_shadow_transparent_buildargs_cs): 1 thread; clamp count + write DispatchIndirectArguments.
struct SwShadowTransparentBuildArgsPushConstants{
    u32 traceGroupSize = 0u;
    u32 edgeCapacity = 0u;
};
static_assert(sizeof(SwShadowTransparentBuildArgsPushConstants) == sizeof(u32) * 2u, "SwShadowTransparentBuildArgsPushConstants must match the kernel push-constant layout");

// Stage-3 INDIRECT trace + scatter (sw_shadow_transparent_indirect_cs): one edge record per thread, single overwrite.
struct SwShadowTransparentIndirectPushConstants{
    u32 width = 0u;
    u32 height = 0u;
    u32 instanceCount = 0u;
    u32 traceGroupSize = 0u;
};
static_assert(sizeof(SwShadowTransparentIndirectPushConstants) == sizeof(u32) * 4u, "SwShadowTransparentIndirectPushConstants must match the kernel push-constant layout");

// Uniform HALF-res transparent multiply (sw_shadow_transparent_uniform_cs): the non-adaptive Stage-1 A/B baseline.
struct SwShadowTransparentUniformPushConstants{
    u32 width = 0u;
    u32 height = 0u;
    u32 instanceCount = 0u;
};
static_assert(sizeof(SwShadowTransparentUniformPushConstants) == sizeof(u32) * 3u, "SwShadowTransparentUniformPushConstants must match the kernel push-constant layout");

// The push-constant range the SHARED binding layout declares: sized to the LARGEST pass struct so every per-pass
// pipeline (each of which sets only its own smaller struct) is layout-compatible. Currently the 7-u32 resolve/classify
// structs; kept as an explicit max so adding a wider pass struct later fails the static_assert rather than silently
// under-sizing the range.
struct SwShadowMaxPushConstants{
    u32 words[7] = {};
};
static_assert(sizeof(SwShadowMaxPushConstants) >= sizeof(SwShadowOpaquePrepassPushConstants), "SwShadowMaxPushConstants must cover every SW-shadow pass push struct");
static_assert(sizeof(SwShadowMaxPushConstants) >= sizeof(SwShadowSoftOpaquePushConstants), "SwShadowMaxPushConstants must cover every SW-shadow pass push struct");
static_assert(sizeof(SwShadowMaxPushConstants) >= sizeof(SwShadowTransparentSoftPushConstants), "SwShadowMaxPushConstants must cover every SW-shadow pass push struct");
static_assert(sizeof(SwShadowMaxPushConstants) >= sizeof(SwShadowTransparentCoarsePushConstants), "SwShadowMaxPushConstants must cover every SW-shadow pass push struct");
static_assert(sizeof(SwShadowMaxPushConstants) >= sizeof(SwShadowTransparentResolvePushConstants), "SwShadowMaxPushConstants must cover every SW-shadow pass push struct");
static_assert(sizeof(SwShadowMaxPushConstants) >= sizeof(SwShadowTransparentClassifyPushConstants), "SwShadowMaxPushConstants must cover every SW-shadow pass push struct");
static_assert(sizeof(SwShadowMaxPushConstants) >= sizeof(SwShadowTransparentBuildArgsPushConstants), "SwShadowMaxPushConstants must cover every SW-shadow pass push struct");
static_assert(sizeof(SwShadowMaxPushConstants) >= sizeof(SwShadowTransparentIndirectPushConstants), "SwShadowMaxPushConstants must cover every SW-shadow pass push struct");
static_assert(sizeof(SwShadowMaxPushConstants) >= sizeof(SwShadowTransparentUniformPushConstants), "SwShadowMaxPushConstants must cover every SW-shadow pass push struct");

// CPU mirror of the hardware RayQuery shadow push constants (shadow_rayquery_cs.slang): just the frame counter that
// seeds the soft cone-jitter (the full-res HW opaque trace softens directly -- directional by its angular radius,
// point/spot by the cone their source sphere subtends).
struct ShadowRqPushConstants{
    u32 frameIndex = 0u;
};
static_assert(sizeof(ShadowRqPushConstants) == sizeof(u32), "ShadowRqPushConstants must match the shader push-constant layout");

// CPU mirror of the hardware RayQuery SOFT OPAQUE half-res trace push constants (shadow_rayquery_soft_cs.slang): the
// explicit half-res grid dims + the frame counter seeding the per-pixel cone-jitter. Mirrors NwbShadowRqSoftPushConstants
// (and, minus instanceCount, SwShadowSoftOpaquePushConstants) so the half-res grid guard is structured identically and the
// downsample/resolve tap alignment stays byte-identical.
struct ShadowRqSoftPushConstants{
    u32 width = 0u;
    u32 height = 0u;
    u32 frameIndex = 0u;
};
static_assert(sizeof(ShadowRqSoftPushConstants) == sizeof(u32) * 3u, "ShadowRqSoftPushConstants must match the shader push-constant layout");

// CPU mirror of shadow_resolve_cs.slang's NwbShadowResolvePushConstants: the full/half dims, the a-trous dilation +
// stage selector, the active shadow-slot range the resolve loops, the temporal-moments-valid flag, and the upsample
// fold mode. 10 x 4 = 40 bytes.
struct ShadowResolvePushConstants{
    u32 width = 0u;          // FULL-res width (UPSAMPLE dispatch/output dim)
    u32 height = 0u;         // FULL-res height
    u32 halfWidth = 0u;      // HALF-res width (PREPARE/WAVELET dispatch dim)
    u32 halfHeight = 0u;     // HALF-res height
    u32 stepWidth = 1u;      // a-trous dilation for this wavelet pass (1,2,4,8,16), in HALF-res texels
    u32 stage = 0u;          // 0 = PREPARE, 1 = WAVELET (half-res), 2 = UPSAMPLE (-> full-res visibility)
    u32 lightSlotStart = 0u; // first active shadow slot to process
    u32 lightSlotCount = 0u; // number of contiguous active slots to process
    u32 momentsValid = 0u;   // 1 = the MOMENTS SRV holds this-frame integrated temporal moments (the merge ran this frame)
                             // -> the WAVELET's SVGF variance edge-stop may use the temporal variance; 0 = temporal off /
                             // first frame -> the shader never samples the (dummy) MOMENTS SRV and uses the spatial fallback.
    u32 upsampleFold = 0u;   // UPSAMPLE fold mode: 0 = OVERWRITE the full-res visibility (soft OPAQUE);
                             // 1 = MULTIPLY the denoised colored transmittance onto it (soft TRANSPARENT fold, RMW). Ignored
                             // by PREPARE/WAVELET; see SoftShadowUpsampleFold::Enum + the UPSAMPLE stage in shadow_resolve_cs.
};
static_assert(sizeof(ShadowResolvePushConstants) == sizeof(u32) * 10u, "ShadowResolvePushConstants must match the shader push-constant layout");

// Shadow resolve stages, kept in lockstep with shadow_resolve_cs.slang's pushConstants.stage switch.
namespace ShadowResolveStage{
    enum Enum : u32{
        Prepare = 0u,
        Wavelet = 1u,
        Upsample = 2u,
    };
};

// UPSAMPLE fold mode: the SoftShadowUpsampleFold::Enum values are the single source of truth, kept in lockstep with
// shadow_resolve_cs.slang's upsampleFold branch:
//  - Overwrite: the soft OPAQUE resolve writes its denoised grayscale visibility into the full-res visibility.
//  - Multiply:  the soft COLORED TRANSPARENT resolve read-modify-write MULTIPLIES its denoised colored transmittance onto
//    the visibility (which already holds the opaque result), so visibility = opaqueSoftUpsampled * transparentSoftUpsampled.

// Mirror of shadow_geometry_downsample_cs.slang's NwbShadowGeometryDownsamplePushConstants: full-res G-buffer dims +
// the half-res output dims.
struct ShadowGeometryDownsamplePushConstants{
    u32 width = 0u;
    u32 height = 0u;
    u32 halfWidth = 0u;
    u32 halfHeight = 0u;
};
static_assert(sizeof(ShadowGeometryDownsamplePushConstants) == sizeof(u32) * 4u, "ShadowGeometryDownsamplePushConstants must match the shader push-constant layout");

// CPU mirror of shadow_reproject_merge_cs.slang's NwbShadowReprojectMergePushConstants (temporal accumulation):
// the STASHED previous-frame worldToClip (64-byte row-major matrix -- Float44U's raw[16] is the row-major dump of the
// mesh-view worldToClip, matching the shader's `row_major float4x4`) + the full/half dims + the active shadow-slot range +
// the history-valid flag + one pad word to a 16-byte multiple. 64 + 8*4 = 96 bytes (within the 128-byte push guarantee).
struct ShadowReprojectMergePushConstants{
    Float44U prevWorldToClip = {};
    u32 width = 0u;          // FULL-res width (to read the world-position G-buffer at the 2x texel)
    u32 height = 0u;         // FULL-res height
    u32 halfWidth = 0u;      // HALF-res width (dispatch/output dim)
    u32 halfHeight = 0u;     // HALF-res height
    u32 lightSlotStart = 0u; // first active SOFT shadow slot to process
    u32 lightSlotCount = 0u; // number of contiguous active SOFT slots (one per dispatch -> 1)
    u32 historyValid = 0u;   // 0 = no valid history this frame (first frame / after resize) -> force n=0 (pure current)
    u32 pad0 = 0u;           // pad to a 16-byte multiple (mirrors the shader's pad0)
};
static_assert(sizeof(ShadowReprojectMergePushConstants) == sizeof(f32) * 16u + sizeof(u32) * 8u, "ShadowReprojectMergePushConstants must match the shader push-constant layout");

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

// Mirror of caustic_geometry_downsample_cs.slang's NwbCausticGeometryDownsamplePushConstants (the half-res geometry
// cache pre-pass): full-res G-buffer dims + the half-res output dims.
struct CausticGeometryDownsamplePushConstants{
    u32 width = 0u;
    u32 height = 0u;
    u32 halfWidth = 0u;
    u32 halfHeight = 0u;
};
static_assert(sizeof(CausticGeometryDownsamplePushConstants) == sizeof(u32) * 4u, "CausticGeometryDownsamplePushConstants must match the shader push-constant layout");

// Mirror of caustic_accumulator_decay_cs.slang's NwbCausticAccumulatorDecayPushConstants (the splat-space temporal EMA
// pre-pass): the accumulator dims + the per-frame decay factor (accum_N = decayFactor*accum_{N-1} before this frame's
// splat). pad keeps the block a 16-byte multiple.
struct CausticAccumulatorDecayPushConstants{
    u32 width = 0u;
    u32 height = 0u;
    f32 decayFactor = 0.f;
    u32 pad = 0u;
};
static_assert(sizeof(CausticAccumulatorDecayPushConstants) == sizeof(u32) * 4u, "CausticAccumulatorDecayPushConstants must match the shader push-constant layout");

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
// by the receiver area-per-pixel in the resolve). This keeps the caustic energy-conserving: focused caustics stay
// bright at low exposure while sparse far-prism scatter falls below the visible floor. ~2 is a unit-ish exposure for
// the demo lighting (light intensity 2.0); doubling the photon count leaves per-frame brightness unchanged.
inline constexpr f32 s_CausticIntensity = 2.0f;

// The caustic resolve denoise is an N-pass edge-avoiding a-trous wavelet (purely spatial, NO temporal accumulation ->
// ghost-free for a moving caustic). The pass count + wavelet kernel live in the shader
// (NWB_CAUSTIC_RESOLVE_PASS_COUNT, resolve_binding_slots.h).

// Photon-aim slack for DEFORMING (runtime/skinned) refractors. Their emission AABB is derived from the bind-pose
// (rest) bounds -- there is no per-frame CPU deformed bound (the per-frame bounds live only in the GPU meshlet-bounds
// buffer). A skinned pose can push the surface past the rest extent, so the rest-bounds world AABB is inflated about
// its center by this factor for runtime instances, keeping the photon emission domain over the deformed surface.
// Matches the engine's skinned meshlet-bounds radius inflation precedent (NWB_SKINNED_MESH_BOUNDS_RADIUS_INFLATION).
inline constexpr f32 s_CausticRuntimeBoundsInflation = 1.25f;

// Software transparent-shadow broad phase: the scene BVH is only an instance-level reject before the per-mesh BVH and
// exact triangle tests. Keep this box slightly conservative so grazing half-res shadow rays are never dropped by the
// top-level instance AABB when several transparent shadows overlap.
inline constexpr f32 s_SwShadowSceneBoundsMinPadding = 0.25f;
inline constexpr f32 s_SwShadowSceneBoundsRelativePadding = 0.10f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Shared helpers defined in rt_detail.cpp, used across the raytracing_* TUs (scene-BVH build + per-instance
// occluder-material resolve). Moved out of the anon TU namespace so the split TUs can all reach them.
namespace __hidden_raytracing_system{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] f32 SceneBvhAxisComponent(const SIMDVector value, const u32 axis)noexcept;
[[nodiscard]] f32 SceneBvhAabbSurfaceArea(const SIMDVector aabbMin, const SIMDVector aabbMax)noexcept;
void InflateSwShadowSceneBounds(SIMDVector& boundsMin, SIMDVector& boundsMax)noexcept;
u32 BuildSceneBvhNode(
    u32* indices,
    const u32 lo,
    const u32 hi,
    const Float4* instanceAabbMin,
    const Float4* instanceAabbMax,
    const Float4* instanceCentroid,
    Vector<SceneBvhNodeBuildData, Core::Alloc::ScratchArena>& nodes,
    const u32* instanceLeafCost = nullptr
);
[[nodiscard]] NwbRtInstanceMaterialGpu ResolveInstanceShadowMaterial(
    const MaterialSurfaceInfo& materialInfo,
    const u32 meshSlot,
    const u32 materialConstantByteOffset,
    const u32 meshInstanceIndex
);
[[nodiscard]] u32 FloatToUintBits(const f32 value);
void AssignInstanceBaseColor(NwbRtInstanceMaterialGpu& material, Core::ECS::World& world, const Core::ECS::EntityID entity);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

