// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "raytracing_system.h"

#include "renderer_private.h"
#include "arena_names.h"

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
    u32 pad0 = 0u;
    u32 pad1 = 0u;
    u32 pad2 = 0u;
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

// CPU mirrors of the software shadow traversal push constants. The old single multiplyMode struct is retired: each
// decomposed pass kernel declares its OWN push struct (its minimal field subset, NO mode selector) and the renderer
// mirrors each here. Every mirror matches its kernel's [[vk::push_constant]] layout exactly (asserted). The shared
// binding LAYOUT sizes its push range to the LARGEST of these (SwShadowMaxPushConstants below) so every per-pass
// pipeline is layout-compatible while each dispatch sets only its own struct's bytes.

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

// Soft COLORED TRANSPARENT half-res jittered trace (sw_shadow_transparent_soft_cs, all light types; Stage 5): identical
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

// CPU mirror of shadow_resolve_cs.slang's NwbShadowResolvePushConstants: the full/half dims, the a-trous dilation +
// stage selector, the active shadow-slot range the resolve loops, the temporal-moments-valid flag, and the upsample
// fold mode (Stage 5). 10 x 4 = 40 bytes.
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
    u32 upsampleFold = 0u;   // UPSAMPLE fold mode (Stage 5): 0 = OVERWRITE the full-res visibility (soft OPAQUE, as always);
                             // 1 = MULTIPLY the denoised colored transmittance onto it (soft TRANSPARENT fold, RMW). Ignored
                             // by PREPARE/WAVELET; see SoftShadowUpsampleFold + the UPSAMPLE stage in shadow_resolve_cs.
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

// UPSAMPLE fold mode: the RendererRayTracingSystem::SoftShadowUpsampleFold enum (declared in the header so the dispatch
// struct can carry it) is the single source of truth, kept in lockstep with shadow_resolve_cs.slang's upsampleFold branch:
//  - Overwrite: the soft OPAQUE resolve writes its denoised grayscale visibility into the full-res visibility (as always).
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

// CPU mirror of shadow_reproject_merge_cs.slang's NwbShadowReprojectMergePushConstants (Stage 3 temporal accumulation):
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


namespace __hidden_raytracing_system{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Extracts the axis-th component (0=x, 1=y, 2=z) of a SIMD vector.
[[nodiscard]] f32 SceneBvhAxisComponent(const SIMDVector value, const u32 axis)noexcept{
    return axis == 0u ? VectorGetX(value) : (axis == 1u ? VectorGetY(value) : VectorGetZ(value));
}

void InflateSwShadowSceneBounds(SIMDVector& boundsMin, SIMDVector& boundsMax)noexcept{
    const SIMDVector extent = VectorSubtract(boundsMax, boundsMin);
    const SIMDVector paddingVector = VectorSetW(
        VectorMax(
            VectorReplicate(s_SwShadowSceneBoundsMinPadding),
            VectorScale(Vector3MaxComponent(extent), s_SwShadowSceneBoundsRelativePadding)
        ),
        0.0f
    );
    boundsMin = VectorSubtract(boundsMin, paddingVector);
    boundsMax = VectorAdd(boundsMax, paddingVector);
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
    const Float4* instanceAabbMin,
    const Float4* instanceAabbMax,
    const Float4* instanceCentroid,
    Vector<SceneBvhNodeBuildData, Core::Alloc::ScratchArena>& nodes
){
    const u32 nodeIndex = static_cast<u32>(nodes.size());
    nodes.push_back(SceneBvhNodeBuildData{});

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
        const SIMDVector centroid = LoadFloat(instanceCentroid[indices[i]]);
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
        if(SceneBvhAxisComponent(LoadFloat(instanceCentroid[indices[i]]), axis) < splitValue){
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
    StoreFloat(VectorMin(LoadFloat(nodes[leftChild].aabbMin), LoadFloat(nodes[rightChild].aabbMin)), &node.aabbMin);
    StoreFloat(VectorMax(LoadFloat(nodes[leftChild].aabbMax), LoadFloat(nodes[rightChild].aabbMax)), &node.aabbMax);
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
    // Builds/refits per-mesh software BVHs. Two callers gate WHEN it runs (it no longer self-gates on RT support):
    //  - The no-RayQuery fallback prepare (the only shadow backend) -- builds every mesh.
    //  - The hybrid prepare on RT hardware -- only when the scene has a TRANSPARENT occluder (whose colored shadow the
    //    software pass must trace); opaque-only / no-transparent scenes never call this, so they pay no software cost.
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
    // Whether any gathered occluder is transparent. On RT hardware this gates the hybrid software-transparent-shadow
    // prepare: the HW pass casts only the opaque (binary) shadow, so the SW colored pass is needed only when a
    // transparent occluder exists; opaque-only scenes skip the software BVH build entirely.
    rayTracingState().m_sceneHasTransparentOccluder = false;

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
        // The BLAS owns the positions it traces, but the any-hit still needs the index buffer (3 vertex indices by
        // PrimitiveIndex) + the U2 triangle-corner attribute buffer (normal/uv0 for the per-hit dispatch) + the raw
        // position buffer (the geometric face normal for crossing pairing), so require all three.
        if(!meshReady || !mesh || !mesh->blas || !mesh->triangleIndexBuffer || !mesh->attributeBuffer || !mesh->positionBuffer)
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
            rayTracingState().m_shadowMeshPositionBuffers[meshSlot] = mesh->positionBuffer.get();
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
        if(m_renderer.materialSystem().findMaterialSurfaceInfo(renderer.material, materialInfo)){
            if(materialInfo->transparent)
                rayTracingState().m_sceneHasTransparentOccluder = true;
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
    // Software scene/instance BVH (TLAS-analog) over ALL gathered occluders, plus the shadow-owned material context.
    // Built by the no-RayQuery fallback prepare (the only shadow backend) AND, on RT hardware, by the hybrid prepare
    // when the scene has a transparent occluder. The gather order matches buildSceneTlas's (same RendererComponent
    // view, aligned conditions), so the scene-BVH leaf index equals the hardware InstanceID -- and the material context
    // it rebuilds is byte-identical to buildSceneTlas's, so the HW caustic (which reads that context by InstanceID) is
    // unaffected even though both write it. The caller gates WHEN this runs (it no longer self-gates on RT support).
    auto* meshSystem = world().getSystem<NWB::Impl::MeshSystem>();
    if(!meshSystem)
        return false;

    auto rendererView = world().view<RendererComponent>();
    const usize candidateCount = rendererView.candidateCount();

    // Per-instance GPU records + the world-space AABBs / centroids the CPU build consumes (kept parallel so
    // the BVH leaf payload indexes straight into the uploaded instance buffer).
    Vector<SceneSwBvhInstanceGpu, Core::Alloc::ScratchArena> instances{ scratchArena };
    Vector<Float4, Core::Alloc::ScratchArena> instanceAabbMin{ scratchArena };
    Vector<Float4, Core::Alloc::ScratchArena> instanceAabbMax{ scratchArena };
    Vector<Float4, Core::Alloc::ScratchArena> instanceCentroid{ scratchArena };
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
            // The U2 per-triangle-corner shadow-trace attribute buffer (normal/uv0), parallel to the triangle index
            // buffer so the trace interpolates the exact raster corner attributes.
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
        __hidden_raytracing_system::InflateSwShadowSceneBounds(worldMin, worldMax);

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
        if(m_renderer.materialSystem().findMaterialSurfaceInfo(renderer.material, materialInfo)){
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

        Float4 storedWorldMin;
        Float4 storedWorldMax;
        Float4 storedCentroid;
        StoreFloat(worldMin, &storedWorldMin);
        StoreFloat(worldMax, &storedWorldMax);
        StoreFloat(VectorScale(VectorAdd(worldMin, worldMax), 0.5f), &storedCentroid);

        instances.push_back(instance);
        instanceAabbMin.push_back(storedWorldMin);
        instanceAabbMax.push_back(storedWorldMax);
        instanceCentroid.push_back(storedCentroid);
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
        StoreFloatInt(LoadFloat(buildNode.aabbMin), buildNode.leftChild, &node.aabbMinLeftChild);
        StoreFloatInt(LoadFloat(buildNode.aabbMax), buildNode.rightChild, &node.aabbMaxRightChild);
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
    // the scene -- the domain the caustic photon producer aims at. Runs once per frame regardless of the shadow
    // backend (HW TLAS or SW BVH); mirrors buildSceneSwBvh's mesh/transform resolve + 8-corner world AABB but does
    // NOT require a software BVH and keeps ONLY instances whose material is refractive. The count gates caustic-light
    // assignment (see ResolveCausticLights): zero refractive instances -> zero caustic lights.
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
        if(!m_renderer.materialSystem().findMaterialSurfaceInfo(renderer.material, materialInfo))
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

    const bool hardwareShadowSupported =
        graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct)
        && graphics().queryFeatureSupport(Core::Feature::RayQuery)
    ;
    if(hardwareShadowSupported){
        if(!buildPendingMeshBlas(commandList))
            return true;

        const bool sceneReady = buildSceneTlas(commandList, scratchArena);
        if(!sceneReady)
            return true;

        // Hardware path casts the OPAQUE (binary) shadow via inline RayQuery.
        outBackendReady =
            ensureShadowPipeline()
            && ensureShadowBindingSet(targets)
        ;

        // Hybrid TRANSPARENT shadow: when the scene holds a transparent occluder, also build the software scene/mesh
        // BVH and traversal pipeline. Its gather matches buildSceneTlas's (same RendererComponent view, aligned
        // conditions), so the scene-BVH leaf index equals the hardware InstanceID and the material context it rebuilds
        // is byte-identical -- leaving the HW caustic (which reads that context by InstanceID) untouched. The render
        // runs the SW traversal as a second pass that MULTIPLIES its colored transparent transmittance onto the opaque
        // mask. Built BEFORE prepareHwCausticResources so the (idempotently) rebuilt context buffers are final before
        // the caustic binding set captures them. Opaque-only scenes skip all of this and pay no software cost.
        rayTracingState().m_hybridTransparentShadowReady = false;
        if(outBackendReady && rayTracingState().m_sceneHasTransparentOccluder){
            if(!buildPendingMeshSwBvh(commandList))
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: hybrid transparent shadow per-mesh software BVH build failed"));
            // Guard m_swShadowMeshCount > 0 before ensureSwShadowBindingSet (which asserts it): if no per-mesh software
            // BVH was available this frame the software pass simply does not run (HW opaque-only), rather than aborting.
            const bool swReady =
                buildSceneSwBvh(commandList, scratchArena)
                && rayTracingState().m_swShadowMeshCount > 0u
                && rayTracingState().m_sceneBvhInstanceCount > 0u
                && ensureSwShadowPipeline()
                && ensureSwShadowBindingSet(targets)
            ;
            if(swReady)
                rayTracingState().m_hybridTransparentShadowReady = true;
            else
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: hybrid transparent software shadow preparation failed; transparent shadows absent this frame"));
        }

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

    // Soft opaque shadow (all light types): build the geometry-downsample + a-trous resolve pipelines/binding sets so
    // the render can denoise the half-res jittered opaque trace into the full-res visibility. Non-fatal to shadows -- a
    // failure leaves m_softShadowReady false and the slot lights keep their hard opaque mask.
    rayTracingState().m_softShadowReady =
        outBackendReady
        && ensureShadowGeometryDownsamplePipeline()
        && ensureShadowGeometryDownsampleBindingSet(targets)
        && ensureSoftShadowResolvePipeline()
        && ensureSoftShadowResolveBindingSet(targets)
    ;
    if(outBackendReady && !rayTracingState().m_softShadowReady)
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: soft opaque shadow resource preparation failed; shadows hard this frame"));

    // Soft opaque shadow TEMPORAL accumulation (Stage 3): build the reproject-merge pipeline + its two front/back binding
    // sets AND the two temporal SOFT_HALF resolve variants (the a-trous then reads the accumulated history instead of the
    // raw trace). Always on when the resources build; non-fatal: a failure leaves m_softShadowTemporalReady false and the
    // soft path feeds the raw trace straight into the a-trous (the Stage-1/2 spatial-only fallback).
    rayTracingState().m_softShadowTemporalReady =
        rayTracingState().m_softShadowReady
        && ensureShadowReprojectMergePipeline()
        && ensureShadowReprojectMergeBindingSet(targets)
    ;
    if(rayTracingState().m_softShadowReady && !rayTracingState().m_softShadowTemporalReady)
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: soft opaque shadow temporal resource preparation failed; no temporal accumulation this frame"));

    // Stage 5 soft COLORED TRANSPARENT shadow: build the RGB a-trous resolve pipeline + the parallel transparent resolve
    // binding sets (over the transparent half-res buffers), gated on the opaque soft path being ready (it shares the
    // geometry cache + the resolve binding layout + the ping-pong scratch). Non-fatal: a failure leaves m_softTransparentReady
    // false so the soft transparent fold is skipped and the OLD transparent coarse/adaptive multiply runs as before (no
    // double-fold -- they are exclusive). The transparent TEMPORAL path additionally needs the (shared) merge pipeline + the
    // parallel transparent merge binding sets; a failure there leaves m_softTransparentTemporalReady false and the transparent
    // resolve reads the raw colored trace straight into the RGB a-trous (the spatial fallback).
    rayTracingState().m_softTransparentReady =
        rayTracingState().m_softShadowReady
        && ensureSoftTransparentResolvePipeline()
        && ensureSoftTransparentResolveBindingSet(targets)
    ;
    if(rayTracingState().m_softShadowReady && !rayTracingState().m_softTransparentReady)
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: soft transparent shadow resource preparation failed; colored shadows fall back to the hard-ish path this frame"));

    rayTracingState().m_softTransparentTemporalReady =
        rayTracingState().m_softTransparentReady
        && rayTracingState().m_softShadowTemporalReady
        && ensureShadowTransparentReprojectMergeBindingSet(targets)
    ;
    if(rayTracingState().m_softTransparentReady && rayTracingState().m_softShadowTemporalReady && !rayTracingState().m_softTransparentTemporalReady)
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: soft transparent shadow temporal resource preparation failed; no colored temporal accumulation this frame"));

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

    // Stage-2 adaptive transparent shadow scratch: a HALF-res sibling of the visibility target (same slot layers, RGBA16F)
    // the coarse software trace writes one transmittance per 2x2 block into and the adaptive resolve interpolates/refines.
    // Allocated alongside the visibility target so it shares the resize lifecycle (resetDeferredFrameTargets rebuilds the
    // SW shadow binding set, which re-binds whichever coarse handle is current). Round UP so a coarse texel covers its 2x2
    // block even for odd extents -- matching the caustic half-res buffers.
    targets.shadowCoarseTransmittanceFormat = Core::Format::RGBA16_FLOAT;
    Core::TextureDesc coarseDesc;
    coarseDesc
        .setWidth((targets.width + NWB_SW_SHADOW_COARSE_FACTOR - 1u) / NWB_SW_SHADOW_COARSE_FACTOR)
        .setHeight((targets.height + NWB_SW_SHADOW_COARSE_FACTOR - 1u) / NWB_SW_SHADOW_COARSE_FACTOR)
        .setArraySize(NWB_SCENE_SHADOW_SLOT_COUNT)
        .setDimension(Core::TextureDimension::Texture2DArray)
        .setFormat(targets.shadowCoarseTransmittanceFormat)
        .setInUAV(true)
        .setName("engine/shadow/coarse_transmittance")
    ;
    targets.shadowCoarseTransmittance = graphics().createTexture(coarseDesc);
    if(!targets.shadowCoarseTransmittance){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow coarse transmittance target"));
        return false;
    }

    // Soft directional shadow (Stage 1) HALF-res targets: the two ping-pong soft buffers (RGBA16F Texture2DArrays, one
    // layer per shadow slot -- the mode-11 jittered trace writes A, the a-trous resolve alternates A<->B, the upsample
    // reads B into the full-res visibility) + the single-layer packed geometry cache (octahedral normal + camera
    // distance + validity) the geometry downsample fills for the edge-stop. Half the render extent (rounded up so a
    // half texel covers its SOFT_FACTOR block for odd extents), matching the caustic half-res buffers. Allocated with
    // the visibility target so they share the resize lifecycle (resetDeferredFrameTargets rebuilds the resolve set).
    targets.shadowSoftFormat = Core::Format::RGBA16_FLOAT;
    targets.shadowSoftGeometryFormat = Core::Format::RGBA16_FLOAT;
    const u32 softHalfWidth = (targets.width + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
    const u32 softHalfHeight = (targets.height + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;

    Core::TextureDesc softHalfADesc;
    softHalfADesc
        .setWidth(softHalfWidth)
        .setHeight(softHalfHeight)
        .setArraySize(NWB_SCENE_SHADOW_SLOT_COUNT)
        .setDimension(Core::TextureDimension::Texture2DArray)
        .setFormat(targets.shadowSoftFormat)
        .setInUAV(true)
        .setName("engine/shadow/soft_half_a")
    ;
    targets.shadowSoftHalfA = graphics().createTexture(softHalfADesc);
    if(!targets.shadowSoftHalfA){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow half-A target"));
        return false;
    }

    Core::TextureDesc softHalfBDesc = softHalfADesc;
    softHalfBDesc.setName("engine/shadow/soft_half_b");
    targets.shadowSoftHalfB = graphics().createTexture(softHalfBDesc);
    if(!targets.shadowSoftHalfB){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow half-B target"));
        return false;
    }

    Core::TextureDesc softGeometryDesc;
    softGeometryDesc
        .setWidth(softHalfWidth)
        .setHeight(softHalfHeight)
        .setFormat(targets.shadowSoftGeometryFormat)
        .setInUAV(true)
        .setName("engine/shadow/soft_geometry")
    ;
    targets.shadowSoftGeometry = graphics().createTexture(softGeometryDesc);
    if(!targets.shadowSoftGeometry){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow geometry cache target"));
        return false;
    }

    // Soft opaque shadow TEMPORAL accumulation (Stage 3) HALF-res targets: the accumulated-visibility + moments ping-pong
    // Texture2DArrays (mirror softHalfADesc: NWB_SCENE_SHADOW_SLOT_COUNT layers, UAV) + the previous-frame single-layer
    // geometry cache (mirror softGeometryDesc). Allocated here so they share the resize lifecycle; a freshly (re)created
    // history holds no valid samples, so re-seed the temporal state (the next merge treats every pixel as n=0 = pure
    // current) and invalidate the stashed prev-frame worldToClip so a resize can't reproject through a stale matrix into
    // freshly-allocated garbage history.
    Core::TextureDesc shadowHistADesc = softHalfADesc;
    shadowHistADesc.setName("engine/shadow/hist_a");
    targets.shadowHistA = graphics().createTexture(shadowHistADesc);
    if(!targets.shadowHistA){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow temporal history-A target"));
        return false;
    }
    Core::TextureDesc shadowHistBDesc = softHalfADesc;
    shadowHistBDesc.setName("engine/shadow/hist_b");
    targets.shadowHistB = graphics().createTexture(shadowHistBDesc);
    if(!targets.shadowHistB){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow temporal history-B target"));
        return false;
    }
    Core::TextureDesc shadowMomentsADesc = softHalfADesc;
    shadowMomentsADesc.setName("engine/shadow/moments_a");
    targets.shadowMomentsA = graphics().createTexture(shadowMomentsADesc);
    if(!targets.shadowMomentsA){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow temporal moments-A target"));
        return false;
    }
    Core::TextureDesc shadowMomentsBDesc = softHalfADesc;
    shadowMomentsBDesc.setName("engine/shadow/moments_b");
    targets.shadowMomentsB = graphics().createTexture(shadowMomentsBDesc);
    if(!targets.shadowMomentsB){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow temporal moments-B target"));
        return false;
    }
    Core::TextureDesc shadowSoftGeometryPrevDesc = softGeometryDesc;
    shadowSoftGeometryPrevDesc.setName("engine/shadow/soft_geometry_prev");
    targets.shadowSoftGeometryPrev = graphics().createTexture(shadowSoftGeometryPrevDesc);
    if(!targets.shadowSoftGeometryPrev){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow previous-frame geometry cache target"));
        return false;
    }
    rayTracingState().m_softShadowTemporalSeeded = false;
    rayTracingState().m_prevWorldToClipValid = false;
    rayTracingState().m_softShadowHistoryFrontIsA = 1u;

    // Stage 5 soft COLORED TRANSPARENT shadow HALF-res targets: the PARALLEL colored pipeline's buffers -- the raw colored
    // soft trace output + its accumulated-visibility & moments ping-pong (mirroring the opaque set exactly: same softHalfADesc
    // format/extent/layers/UAV). The geometry cache + prevWorldToClip are SHARED (not duplicated). Allocated here so they
    // share the resize lifecycle; the transparent history uses the SAME m_softShadowHistoryFrontIsA selector as the opaque
    // history (one frame-end flip covers both), so a freshly (re)created transparent history is covered by the temporal
    // re-seed above (the first merge treats every pixel as n=0 = pure current).
    Core::TextureDesc transparentSoftHalfDesc = softHalfADesc;
    transparentSoftHalfDesc.setName("engine/shadow/transparent_soft_half");
    targets.transparentSoftHalf = graphics().createTexture(transparentSoftHalfDesc);
    if(!targets.transparentSoftHalf){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft transparent shadow half target"));
        return false;
    }
    Core::TextureDesc transparentHistADesc = softHalfADesc;
    transparentHistADesc.setName("engine/shadow/transparent_hist_a");
    targets.transparentHistA = graphics().createTexture(transparentHistADesc);
    if(!targets.transparentHistA){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft transparent shadow history-A target"));
        return false;
    }
    Core::TextureDesc transparentHistBDesc = softHalfADesc;
    transparentHistBDesc.setName("engine/shadow/transparent_hist_b");
    targets.transparentHistB = graphics().createTexture(transparentHistBDesc);
    if(!targets.transparentHistB){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft transparent shadow history-B target"));
        return false;
    }
    Core::TextureDesc transparentMomentsADesc = softHalfADesc;
    transparentMomentsADesc.setName("engine/shadow/transparent_moments_a");
    targets.transparentMomentsA = graphics().createTexture(transparentMomentsADesc);
    if(!targets.transparentMomentsA){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft transparent shadow moments-A target"));
        return false;
    }
    Core::TextureDesc transparentMomentsBDesc = softHalfADesc;
    transparentMomentsBDesc.setName("engine/shadow/transparent_moments_b");
    targets.transparentMomentsB = graphics().createTexture(transparentMomentsBDesc);
    if(!targets.transparentMomentsB){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft transparent shadow moments-B target"));
        return false;
    }

    // Stage-3 compacted edge list (recreated on resize alongside the visibility/coarse targets, so the SW shadow binding-set
    // rebuild that already triggers on the visibility-pointer change re-binds it). Each record is NWB_SW_SHADOW_EDGE_RECORD_WORDS
    // u32. Lives on rayTracingState (a buffer, not a frame target) since it is shadow-subsystem scratch the lighting never samples.
    // SIZING: capacity = one record per full-res PIXEL, but mode 6 appends one record per (pixel, active shadow slot) edge, so the
    // TIGHT worst-case demand is width*height*activeShadowSlots (slots capped at NWB_SCENE_SHADOW_SLOT_COUNT=8). One-per-pixel is
    // deliberately NOT that bound: at the measured ~3% edge fraction the demand is ~0.03*slots per pixel, so width*height is 4-16x
    // the realistic worst case even with many lights -- and provisioning the 8x tight bound would burn ~73MB of 96%-empty scratch.
    // Overflow (a pathological all-edge multi-slot frame) is SAFE, not corrupt: the append still increments the counter but the
    // indexed list write is guarded by edgeCapacity, mode 7 clamps the trace count to it, and mode 8's tail guard reads only
    // in-range records -- so overflowed edges simply take mode 6's bilinear-interpolated fallback (Stage-1 quality), no OOB/UB.
    const u32 edgeListCapacityRecords = targets.width * targets.height;
    Core::BufferDesc edgeListDesc;
    edgeListDesc
        .setByteSize(static_cast<u64>(sizeof(u32)) * static_cast<u64>(NWB_SW_SHADOW_EDGE_RECORD_WORDS) * static_cast<u64>(edgeListCapacityRecords))
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        .setDebugName(Name("sw_shadow_edge_list"))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    rayTracingState().m_swShadowEdgeListBuffer = graphics().createBuffer(edgeListDesc);
    if(!rayTracingState().m_swShadowEdgeListBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create SW shadow edge-list buffer"));
        rayTracingState().m_swShadowEdgeListCapacity = 0u;
        return false;
    }
    rayTracingState().m_swShadowEdgeListCapacity = edgeListCapacityRecords;
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

    // A (re)created accumulator holds no valid splat history, so re-seed the splat-space temporal EMA: the next
    // temporal-enabled frame clears the accumulator instead of decaying it. (This runs on the initial create AND on a
    // resize, both of which allocate a fresh accumulator texture.)
    rayTracingState().m_causticAccumulatorInitialized = false;

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

    // Half-res geometry cache (xyz = world, w = receiver validity) for the resolve's per-tap edge-stop geometry.
    Core::TextureDesc geometryDesc;
    geometryDesc
        .setWidth(halfWidth)
        .setHeight(halfHeight)
        .setFormat(targets.causticHistoryFormat)
        .setInUAV(true)
        .setName("engine/caustic/resolve_geometry")
    ;
    targets.causticResolveGeometry = graphics().createTexture(geometryDesc);
    if(!targets.causticResolveGeometry){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic resolve geometry cache target"));
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::renderShadowVisibility(Core::CommandList& commandList, DeferredFrameTargets& targets){
    if(!targets.shadowVisibility)
        return false;
    if(!rayTracingState().m_tlas || !rayTracingState().m_shadowPipeline || !rayTracingState().m_shadowBindingSet)
        return false;

    Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_ShadowVisibility, graphics().getDevice(), commandList);

    // The per-mesh index/attribute/position byte buffers the RayQuery trace reads were last touched as accel-struct
    // build inputs (positions/indices) or are otherwise resident; move each distinct mesh's buffers to ShaderResource
    // for the per-hit transmittance dispatch, alongside the shadow-owned material-context buffers (built + uploaded by
    // buildSceneTlas on the shadow-prepare command list). setResourceStatesForBindingSet then derives the rest (TLAS
    // read, G-buffer SRVs, scene/light buffers, the visibility UAV).
    for(u32 slot = 0u; slot < rayTracingState().m_shadowMeshCount; ++slot){
        commandList.setBufferState(rayTracingState().m_shadowMeshIndexBuffers[slot], Core::ResourceStates::ShaderResource);
        commandList.setBufferState(rayTracingState().m_shadowMeshAttributeBuffers[slot], Core::ResourceStates::ShaderResource);
        commandList.setBufferState(rayTracingState().m_shadowMeshPositionBuffers[slot], Core::ResourceStates::ShaderResource);
    }
    commandList.setBufferState(rayTracingState().m_shadowMaterialTypedBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(rayTracingState().m_shadowInstanceBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setResourceStatesForBindingSet(rayTracingState().m_shadowBindingSet.get());
    commandList.commitBarriers();

    // FULL-resolution inline-RayQuery compute trace: one occlusion ray per output pixel, written straight into the
    // full-res visibility array the deferred lighting samples. This mirrors the software traversal (shadow_sw_traversal_cs)
    // one-ray-per-pixel structure exactly, so the hardware and software shadow backends produce the same result -- no
    // half-res trace + edge-adaptive resolve in between (that reconstruction surfaced per-ray differences as artifacts at
    // the colored-overlap silhouettes). The shader reads its dispatch bounds from the output's own dimensions.
    Core::ComputeState shadowState;
    shadowState.setPipeline(rayTracingState().m_shadowPipeline.get());
    shadowState.addBindingSet(rayTracingState().m_shadowBindingSet.get());
    commandList.setComputeState(shadowState);
    // Soft directional cone-jitter: advance the per-frame seed once here (the HW opaque trace is the primary shadow
    // producer on the HW path, mutually exclusive with the no-RT software traversal per frame). A soft directional
    // (angular-radius > 0) light softens this full-res HW opaque trace directly; a zero-radius directional / point /
    // spot light cone-jitters to the axis exactly -> the unchanged hard shadow.
    ShadowRqPushConstants shadowPush;
    shadowPush.frameIndex = rayTracingState().m_softShadowFrameIndex++;
    commandList.setPushConstants(&shadowPush, sizeof(shadowPush));
    commandList.dispatch(
        DivideUp(targets.width, static_cast<u32>(NWB_SHADOW_RT_GROUP_SIZE)),
        DivideUp(targets.height, static_cast<u32>(NWB_SHADOW_RT_GROUP_SIZE)),
        1u
    );
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
    // frame, so the additive term is a pixel-identical no-op. Always cleared.
    commandList.setTextureState(targets.causticIrradiance.get(), ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.clearTextureFloat(targets.causticIrradiance.get(), ECSRenderDetail::s_FramebufferSubresources, Core::Color(0.f, 0.f, 0.f, 0.f));

    // The accumulator is the R32_UINT fixed-point splat target (one Texture2DArray layer per RGB channel). When the
    // splat-space temporal EMA is ENABLED (NWB_CAUSTIC_TEMPORAL_DECAY > 0) it must PERSIST across frames -- the producer
    // decays it in place instead of clearing (prepareCausticAccumulatorForSplat) -- so it is NOT cleared here. When
    // temporal is DISABLED it is a per-frame target, cleared to 0 here exactly as before. (The a-trous scratch
    // causticHistory needs no clear either way -- every wavelet pass fully overwrites it.)
    if(causticTemporalDecay() <= 0.f){
        commandList.setTextureState(targets.causticAccumulator.get(), ECSRenderDetail::s_CausticAccumulatorSubresources, Core::ResourceStates::CopyDest);
        commandList.commitBarriers();
        commandList.clearTextureUInt(targets.causticAccumulator.get(), ECSRenderDetail::s_CausticAccumulatorSubresources, 0u);
    }
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

    // Geometry downsample pre-pass: fill the half-res geometry cache (world position + receiver validity) ONCE, so the
    // PREPARE + WAVELET passes tap one half-res texel for the edge-stop geometry instead of re-reading the full-res
    // world-position + depth G-buffer at the half pixel's 2x location every a-trous tap. The framework transitions the
    // cache UAV(write here) -> SRV(read in the resolve passes).
    {
        commandList.setResourceStatesForBindingSet(rayTracingState().m_causticGeometryDownsampleBindingSet.get());
        commandList.commitBarriers();

        CausticGeometryDownsamplePushConstants geometryPush;
        geometryPush.width = targets.width;
        geometryPush.height = targets.height;
        geometryPush.halfWidth = halfWidth;
        geometryPush.halfHeight = halfHeight;

        Core::ComputeState geometryState;
        geometryState.setPipeline(rayTracingState().m_causticGeometryDownsamplePipeline.get());
        geometryState.addBindingSet(rayTracingState().m_causticGeometryDownsampleBindingSet.get());
        commandList.setComputeState(geometryState);
        commandList.setPushConstants(&geometryPush, sizeof(geometryPush));
        commandList.dispatch(halfGroupsX, halfGroupsY, 1u);
    }

    // Splat-space temporal EMA normalization: with decay enabled the accumulator holds the EMA accum = photons/(1-decay)
    // at the static steady state, so pre-multiply the exposure by (1-decay) to keep the STATIC caustic brightness
    // byte-unchanged vs the non-temporal path. Disabled (decay <= 0) -> factor 1, the exposure is exactly s_CausticIntensity.
    const f32 temporalDecay = causticTemporalDecay();
    const f32 effectiveIntensity = (temporalDecay > 0.f) ? (s_CausticIntensity * (1.f - temporalDecay)) : s_CausticIntensity;

    const auto runPass = [&](Core::BindingSet* const bindingSet, const u32 stepWidth, const CausticResolveStage::Enum stage, const u32 groupsX, const u32 groupsY){
        commandList.setResourceStatesForBindingSet(bindingSet);
        commandList.commitBarriers();

        CausticResolvePushConstants resolvePush;
        resolvePush.width = targets.width;
        resolvePush.height = targets.height;
        resolvePush.halfWidth = halfWidth;
        resolvePush.halfHeight = halfHeight;
        resolvePush.causticIntensity = effectiveIntensity;
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

void RendererRayTracingSystem::prepareCausticAccumulatorForSplat(Core::CommandList& commandList, DeferredFrameTargets& targets, f32 decayFactor){
    // Splat-space temporal EMA step, run at the top of the SW/HW producer when temporal is enabled (decayFactor > 0).
    // clearCausticTargets left the accumulator untouched (the clear is deferred to here), so exactly ONE of two paths
    // runs per frame:
    //  - First enabled frame (or the frame after a resize/invalidation): the accumulator holds no valid history, so clear
    //    it to 0. The producer then splats a normal single-frame caustic on top -- identical to the pre-temporal frame 0.
    //  - Every later frame: dispatch the decay pass (accum_N = decayFactor*accum_{N-1}) in place; the producer atomic-adds
    //    this frame's photons on top, so the accumulator holds the EMA. A UAV barrier between the decay dispatch and the
    //    producer's atomic-adds (setEnableUavBarriersForTexture + commitBarriers) syncs the read-after-write on the image.
    // Both paths leave the accumulator in UnorderedAccess for the producer's atomic-adds.
    if(!rayTracingState().m_causticAccumulatorInitialized){
        rayTracingState().m_causticAccumulatorInitialized = true;
        commandList.setTextureState(targets.causticAccumulator.get(), ECSRenderDetail::s_CausticAccumulatorSubresources, Core::ResourceStates::CopyDest);
        commandList.commitBarriers();
        commandList.clearTextureUInt(targets.causticAccumulator.get(), ECSRenderDetail::s_CausticAccumulatorSubresources, 0u);
        return;
    }

    commandList.setEnableUavBarriersForTexture(targets.causticAccumulator.get(), true);
    commandList.setResourceStatesForBindingSet(rayTracingState().m_causticAccumulatorDecayBindingSet.get());
    commandList.commitBarriers();

    CausticAccumulatorDecayPushConstants decayPush;
    decayPush.width = targets.width;
    decayPush.height = targets.height;
    decayPush.decayFactor = decayFactor;

    Core::ComputeState decayState;
    decayState.setPipeline(rayTracingState().m_causticAccumulatorDecayPipeline.get());
    decayState.addBindingSet(rayTracingState().m_causticAccumulatorDecayBindingSet.get());
    commandList.setComputeState(decayState);
    commandList.setPushConstants(&decayPush, sizeof(decayPush));
    commandList.dispatch(
        DivideUp(targets.width, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE)),
        DivideUp(targets.height, static_cast<u32>(NWB_CAUSTIC_RESOLVE_GROUP_SIZE)),
        1u
    );

    // Sync the decay's UAV writes before the producer's atomic-adds hit the same image.
    commandList.commitBarriers();
}

bool RendererRayTracingSystem::renderGpuBvhShadowVisibility(Core::CommandList& commandList, DeferredFrameTargets& targets, bool multiplyOntoOpaque){
    // Software shadow traversal. Two callers:
    //  - No-RayQuery fallback (multiplyOntoOpaque=false): the only shadow backend; traces ALL occluders and OVERWRITES
    //    the visibility (opaque blocks + transparent tints).
    //  - Hybrid on RT hardware (multiplyOntoOpaque=true): the HW RayQuery pass (renderShadowVisibility) already wrote
    //    the opaque binary mask; this traces the TRANSPARENT-ONLY scene BVH and MULTIPLIES its colored transmittance
    //    onto that mask. Whether the SW scene BVH holds all occluders or only the transparent ones is decided by
    //    buildSceneSwBvh; this pass only needs to know to multiply rather than overwrite.
    if(!targets.shadowVisibility)
        return false;
    // No software scene BVH this frame (no traceable instances) -> the caller clears the mask to all-lit.
    if(!rayTracingState().m_sceneBvhNodeBuffer || rayTracingState().m_sceneBvhInstanceCount == 0u)
        return false;
    // Every decomposed pass pipeline must be resident (ensureSwShadowPipeline creates them all-or-nothing) alongside the
    // shared binding set + the per-mesh geometry; the opaque prepass is a representative liveness check for the set.
    if(!rayTracingState().m_swShadowOpaquePrepassPipeline || !rayTracingState().m_swShadowBindingSet || rayTracingState().m_swShadowMeshCount == 0u)
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
    // The two software sub-passes that write the visibility UAV (the opaque pre-pass + the transparent resolve/multiply)
    // need a UAV barrier between them, and the Stage-2 coarse->resolve handoff needs one on the coarse texture. Enable
    // UAV barriers on both so each commitBarriers between dispatches syncs the read-after-write hazard on the same image.
    commandList.setEnableUavBarriersForTexture(targets.shadowVisibility.get(), true);
    commandList.setEnableUavBarriersForTexture(targets.shadowCoarseTransmittance.get(), true);
    commandList.setResourceStatesForBindingSet(rayTracingState().m_swShadowBindingSet.get());
    commandList.commitBarriers();

    // Each pass now has its OWN pipeline (the multiplyMode monolith is retired) but shares the one binding set. This
    // builds the per-pass ComputeState right before its dispatch -- the only mechanical change from the old single
    // setComputeState(computeState); the barrier sequence, grids, and push values are unchanged.
    const auto passState = [&](const Core::ComputePipelineHandle& pipeline){
        Core::ComputeState state;
        state.setPipeline(pipeline.get());
        state.addBindingSet(rayTracingState().m_swShadowBindingSet.get());
        return state;
    };

    const u32 groupSize = static_cast<u32>(NWB_SW_SHADOW_GROUP_SIZE);
    const u32 fullGroupsX = DivideUp(targets.width, groupSize);
    const u32 fullGroupsY = DivideUp(targets.height, groupSize);
    const u32 coarseWidth = (targets.width + NWB_SW_SHADOW_COARSE_FACTOR - 1u) / NWB_SW_SHADOW_COARSE_FACTOR;
    const u32 coarseHeight = (targets.height + NWB_SW_SHADOW_COARSE_FACTOR - 1u) / NWB_SW_SHADOW_COARSE_FACTOR;
    const u32 coarseGroupsX = DivideUp(coarseWidth, groupSize);
    const u32 coarseGroupsY = DivideUp(coarseHeight, groupSize);

    // Stage 5: set true once the soft COLORED TRANSPARENT fold has folded its denoised colored transmittance onto the soft-
    // opaque visibility for the soft slots (the no-multiply/software-primary path only). When true, the OLD transparent
    // coarse/adaptive/uniform multiply below is SKIPPED for those slots so the colored shadow is never double-folded. Since
    // every shadow slot is soft, this is all-or-nothing per frame; a non-soft frame leaves it false and the old path runs.
    bool softTransparentRan = false;

    // No-RayQuery software path: there is no HW opaque mask, so first write the full-res OPAQUE binary mask ourselves
    // (mode 3, opaque occluders only), then the transparent pass folds its colored shadow onto it -- exactly mirroring
    // the hybrid path (HW opaque mask + transparent), so the software fallback gets the same hard-opaque/soft-transparent
    // split while its hard opaque shadows stay full-res sharp.
    if(!multiplyOntoOpaque){
        // Full-res OPAQUE binary blocker (opaque prepass) into EVERY slot -- the SW-path baseline mask. When the soft
        // pipeline is ready (the shipping case) the soft opaque pass below OVERWRITES every soft slot, so this is the
        // fallback that keeps the SW path producing a valid opaque shadow if the soft resources failed to build. (Stage 6
        // retired the adaptive-opaque coarse/edge-refine economizer: soft opaque overwrote its output, so it was pure dead
        // work here; the full-res prepass is the simpler, exact baseline.)
        SwShadowOpaquePrepassPushConstants opaquePush;
        opaquePush.width = targets.width;
        opaquePush.height = targets.height;
        opaquePush.instanceCount = rayTracingState().m_sceneBvhInstanceCount;
        commandList.setComputeState(passState(rayTracingState().m_swShadowOpaquePrepassPipeline));
        commandList.setPushConstants(&opaquePush, sizeof(opaquePush));
        commandList.dispatch(fullGroupsX, fullGroupsY, 1u);

        // Sync before the transparent pass. The opaque mask (the prepass wrote shadowVisibility) -> the transparent pass
        // reads+multiplies it: a write->read/write hazard the visibility UAV barrier covers, so it stays UnorderedAccess.
        // The mode-4 transparent coarse WRITES the coarse buffer next, so stage it ShaderResource here -> mode 4's
        // setComputeState emits the ShaderResource->UnorderedAccess barrier ordering it. (The prepass never touched it.)
        commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.setTextureState(targets.shadowCoarseTransmittance.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::ShaderResource);
        commandList.commitBarriers();

        // Soft opaque shadow (all light types). The opaque pre-pass above wrote a HARD binary mask into EVERY slot; now
        // OVERWRITE every slot light with the soft penumbra: half-res jittered opaque trace (directional softens by its
        // constant angular radius, point/spot by the distance-dependent cone their source sphere subtends -- the jitter
        // is type-aware inside the trace) -> geometry downsample -> a-trous denoise -> bilateral upsample into the full-
        // res visibility. Runs only when the resolve resources are ready this frame AND at least one light holds a shadow
        // slot; else the slot lights keep the hard mask (a clean fallback). The transparent pass below still multiplies
        // its colored shadow onto the (now soft) opaque mask -- so transparent colored shadow keeps working, exactly as
        // this feature requires.
        if(rayTracingState().m_softShadowReady && rayTracingState().m_softShadowSlotMask != 0u){
            const u32 softHalfWidth = (targets.width + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
            const u32 softHalfHeight = (targets.height + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
            const u32 softGroupsX = DivideUp(softHalfWidth, groupSize);
            const u32 softGroupsY = DivideUp(softHalfHeight, groupSize);

            // Advance the per-frame cone-jitter seed once (the no-RT software traversal is the primary shadow producer
            // this frame, mutually exclusive with the HW RayQuery path).
            const u32 frameIndex = rayTracingState().m_softShadowFrameIndex++;

            // Soft opaque trace: one cone-jittered opaque visibility sample per HALF-res pixel into soft-A (all slot
            // lights at once). Enable UAV barriers on the soft buffers + geometry cache for the resolve.
            commandList.setEnableUavBarriersForTexture(targets.shadowSoftHalfA.get(), true);
            commandList.setEnableUavBarriersForTexture(targets.shadowSoftHalfB.get(), true);
            commandList.setEnableUavBarriersForTexture(targets.shadowSoftGeometry.get(), true);
            // Stage-3 temporal accumulator buffers: enable UAV barriers so the merge's history/moments writes are ordered
            // before the a-trous PREPARE reads the accumulated history as an SRV (setResourceStatesForBindingSet handles the
            // UAV->SRV transition). No-op when temporal is off (the merge never dispatches).
            if(rayTracingState().m_softShadowTemporalReady){
                commandList.setEnableUavBarriersForTexture(targets.shadowHistA.get(), true);
                commandList.setEnableUavBarriersForTexture(targets.shadowHistB.get(), true);
                commandList.setEnableUavBarriersForTexture(targets.shadowMomentsA.get(), true);
                commandList.setEnableUavBarriersForTexture(targets.shadowMomentsB.get(), true);
            }

            SwShadowSoftOpaquePushConstants softTracePush;
            softTracePush.width = targets.width;
            softTracePush.height = targets.height;
            softTracePush.instanceCount = rayTracingState().m_sceneBvhInstanceCount;
            softTracePush.frameIndex = frameIndex;
            commandList.setComputeState(passState(rayTracingState().m_swShadowSoftOpaquePipeline));
            commandList.setPushConstants(&softTracePush, sizeof(softTracePush));
            commandList.dispatch(softGroupsX, softGroupsY, 1u);

            // Sync soft-A (soft trace write -> the resolve PREPARE reads it as an SRV). The resolve's
            // setResourceStatesForBindingSet transitions soft-A UnorderedAccess -> ShaderResource here.
            commandList.setTextureState(targets.shadowSoftHalfA.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
            commandList.commitBarriers();

            // Geometry downsample: fill the half-res packed geometry cache ONCE (slot-independent) before the per-slot
            // resolve loop taps it. Writes the cache UAV; each slot's resolve then reads it as an SRV (the cache is not
            // rewritten, so the UAV->SRV transition happens once and every directional slot shares it).
            {
                commandList.setResourceStatesForBindingSet(rayTracingState().m_shadowGeometryDownsampleBindingSet.get());
                commandList.commitBarriers();

                ShadowGeometryDownsamplePushConstants geometryPush;
                geometryPush.width = targets.width;
                geometryPush.height = targets.height;
                geometryPush.halfWidth = softHalfWidth;
                geometryPush.halfHeight = softHalfHeight;

                Core::ComputeState geometryState;
                geometryState.setPipeline(rayTracingState().m_shadowGeometryDownsamplePipeline.get());
                geometryState.addBindingSet(rayTracingState().m_shadowGeometryDownsampleBindingSet.get());
                commandList.setComputeState(geometryState);
                commandList.setPushConstants(&geometryPush, sizeof(geometryPush));
                commandList.dispatch(softGroupsX, softGroupsY, 1u);
            }

            // Stage-3 temporal insertion: when the merge is ready, select the merge binding set + the resolve's temporal
            // PREPARE override by the ping-pong front state (frontIsA==1 -> AtoB writes hist-B, resolve reads hist-B;
            // frontIsA==0 -> BtoA writes hist-A, resolve reads hist-A). historyValid gates the very first frame / a post-
            // resize frame to pure-current (n=0) so the merge never reprojects through a stale matrix into fresh garbage.
            const bool temporalActive = rayTracingState().m_softShadowTemporalReady;
            const bool frontIsA = rayTracingState().m_softShadowHistoryFrontIsA != 0u;
            Core::BindingSet* const mergeSet = temporalActive
                ? (frontIsA
                    ? rayTracingState().m_shadowReprojectMergeBindingSetAtoB.get()
                    : rayTracingState().m_shadowReprojectMergeBindingSetBtoA.get())
                : nullptr
            ;
            Core::BindingSet* const temporalPrepareSet = temporalActive
                ? (frontIsA
                    ? rayTracingState().m_shadowResolveBindingSetTemporalHistB.get()
                    : rayTracingState().m_shadowResolveBindingSetTemporalHistA.get())
                : nullptr
            ;
            const u32 historyValid = (temporalActive
                && rayTracingState().m_prevWorldToClipValid
                && rayTracingState().m_softShadowTemporalSeeded) ? 1u : 0u;

            // Denoise + upsample EACH slot light (scattered set, so one dispatch chain per set bit). With temporal on, a
            // per-slot reproject-merge runs FIRST (accumulating this frame's trace into the history), then the a-trous
            // resolve reads the accumulated history via temporalPrepareSet; else the resolve reads the raw trace directly.
            for(u32 slot = 0u; slot < NWB_SCENE_SHADOW_SLOT_COUNT; ++slot){
                if((rayTracingState().m_softShadowSlotMask & (1u << slot)) == 0u)
                    continue;

                if(temporalActive){
                    // Reproject-merge (half-res, one slot): reads the raw trace (soft-A) + prev history/moments + curr/prev
                    // geometry + the full-res world-position G-buffer; writes the accumulated visibility (history-out) +
                    // moments. setResourceStatesForBindingSet transitions the raw trace + geometry + world-pos to SRV and
                    // the history/moments out to UAV; a UAV barrier (enabled above) then orders the write before the
                    // resolve PREPARE reads history-out as an SRV.
                    commandList.setResourceStatesForBindingSet(mergeSet);
                    commandList.commitBarriers();

                    ShadowReprojectMergePushConstants mergePush;
                    mergePush.prevWorldToClip = rayTracingState().m_prevWorldToClip;
                    mergePush.width = targets.width;
                    mergePush.height = targets.height;
                    mergePush.halfWidth = softHalfWidth;
                    mergePush.halfHeight = softHalfHeight;
                    mergePush.lightSlotStart = slot;
                    mergePush.lightSlotCount = 1u;
                    mergePush.historyValid = historyValid;

                    Core::ComputeState mergeState;
                    mergeState.setPipeline(rayTracingState().m_shadowReprojectMergePipeline.get());
                    mergeState.addBindingSet(mergeSet);
                    commandList.setComputeState(mergeState);
                    commandList.setPushConstants(&mergePush, sizeof(mergePush));
                    commandList.dispatch(softGroupsX, softGroupsY, 1u);
                }

                // Opaque soft resolve: scalar pipeline, its own base sets, OVERWRITE the visibility. The dispatch struct lets
                // ONE routine serve both the opaque (here) and the transparent (below) resolve.
                SoftShadowResolveDispatch opaqueDispatch;
                opaqueDispatch.pipeline = rayTracingState().m_shadowResolvePipeline.get();
                opaqueDispatch.outputHalfA = rayTracingState().m_shadowResolveBindingSetOutputHalfA.get();
                opaqueDispatch.outputHalfB = rayTracingState().m_shadowResolveBindingSetOutputHalfB.get();
                opaqueDispatch.upsample = rayTracingState().m_shadowResolveBindingSetUpsample.get();
                opaqueDispatch.prepareOverride = temporalPrepareSet;
                opaqueDispatch.fold = SoftShadowUpsampleFold::Overwrite;
                dispatchSoftShadowResolve(commandList, targets, slot, opaqueDispatch);
            }

            // The opaque resolve left the visibility in UnorderedAccess (its final UPSAMPLE UAV write) + soft scratch in
            // whatever the last pass set.

            // ---- Stage 5: soft COLORED TRANSPARENT shadow, FOLD-MULTIPLIED onto the (now soft-opaque) visibility ----
            // A PARALLEL colored pipeline, separately traced + temporally denoised + RGB a-trous'd, folded (multiplied) onto
            // the opaque visibility ONLY here at the final upsample -- so visibility = opaqueSoftUpsampled * transparentSoft
            // Upsampled, both independently denoised (opaque binary Bernoulli vs colored chord-variance RGB have different
            // noise stats). When it runs it REPLACES the old transparent coarse/adaptive multiply for these soft slots
            // (softTransparentRan gates the old path off below), so the colored shadow is NEVER double-folded.
            if(rayTracingState().m_softTransparentReady){
                // (a) UAV barrier: the opaque UPSAMPLE's visibility WRITES must be ordered before the transparent UPSAMPLE's
                // read-modify-write READS the same image (a WAW/RAW hazard on shadowVisibility). The visibility UAV barrier
                // was enabled earlier for the opaque sub-passes; a same-state transition here emits the ordering barrier.
                commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
                commandList.commitBarriers();

                // (b) Soft transparent trace: one cone-jittered COLORED transmittance sample per HALF-res pixel into
                // transparentSoftHalf (all slot lights at once), TRANSPARENT occluder class. Reuses the SAME frameIndex as the
                // opaque trace -- the shader's compile-time salt decorrelates its low-discrepancy stream. Enable the UAV
                // barrier on transparentSoftHalf so the trace write is ordered before the merge / resolve PREPARE reads it.
                commandList.setEnableUavBarriersForTexture(targets.transparentSoftHalf.get(), true);
                if(rayTracingState().m_softTransparentTemporalReady){
                    commandList.setEnableUavBarriersForTexture(targets.transparentHistA.get(), true);
                    commandList.setEnableUavBarriersForTexture(targets.transparentHistB.get(), true);
                    commandList.setEnableUavBarriersForTexture(targets.transparentMomentsA.get(), true);
                    commandList.setEnableUavBarriersForTexture(targets.transparentMomentsB.get(), true);
                }

                SwShadowTransparentSoftPushConstants transparentTracePush;
                transparentTracePush.width = targets.width;
                transparentTracePush.height = targets.height;
                transparentTracePush.instanceCount = rayTracingState().m_sceneBvhInstanceCount;
                transparentTracePush.frameIndex = frameIndex;
                commandList.setComputeState(passState(rayTracingState().m_swShadowTransparentSoftPipeline));
                commandList.setPushConstants(&transparentTracePush, sizeof(transparentTracePush));
                commandList.dispatch(softGroupsX, softGroupsY, 1u);

                // Sync transparentSoftHalf (trace write -> the merge / resolve PREPARE reads it as an SRV).
                commandList.setTextureState(targets.transparentSoftHalf.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
                commandList.commitBarriers();

                // (c) The geometry cache is ALREADY filled (shared with the opaque path, filled once above) -- do NOT re-run
                // the geometry downsample. Reuse the SAME temporal front-state gate values (frontIsA / historyValid) as the
                // opaque path so both histories stay in lockstep under the single frame-end selector flip.
                const bool transparentTemporalActive = rayTracingState().m_softTransparentTemporalReady;
                Core::BindingSet* const transparentMergeSet = transparentTemporalActive
                    ? (frontIsA
                        ? rayTracingState().m_transparentReprojectMergeBindingSetAtoB.get()
                        : rayTracingState().m_transparentReprojectMergeBindingSetBtoA.get())
                    : nullptr
                ;
                Core::BindingSet* const transparentPrepareSet = transparentTemporalActive
                    ? (frontIsA
                        ? rayTracingState().m_transparentResolveBindingSetTemporalHistB.get()
                        : rayTracingState().m_transparentResolveBindingSetTemporalHistA.get())
                    : nullptr
                ;

                for(u32 slot = 0u; slot < NWB_SCENE_SHADOW_SLOT_COUNT; ++slot){
                    if((rayTracingState().m_softShadowSlotMask & (1u << slot)) == 0u)
                        continue;

                    if(transparentTemporalActive){
                        // Transparent reproject-merge (same RGB-safe merge pipeline; its own front/back set over the
                        // transparent hist/moments + transparentSoftHalf + the SHARED geometry caches + world-position).
                        commandList.setResourceStatesForBindingSet(transparentMergeSet);
                        commandList.commitBarriers();

                        ShadowReprojectMergePushConstants transparentMergePush;
                        transparentMergePush.prevWorldToClip = rayTracingState().m_prevWorldToClip;
                        transparentMergePush.width = targets.width;
                        transparentMergePush.height = targets.height;
                        transparentMergePush.halfWidth = softHalfWidth;
                        transparentMergePush.halfHeight = softHalfHeight;
                        transparentMergePush.lightSlotStart = slot;
                        transparentMergePush.lightSlotCount = 1u;
                        transparentMergePush.historyValid = historyValid;

                        Core::ComputeState transparentMergeState;
                        transparentMergeState.setPipeline(rayTracingState().m_shadowReprojectMergePipeline.get());
                        transparentMergeState.addBindingSet(transparentMergeSet);
                        commandList.setComputeState(transparentMergeState);
                        commandList.setPushConstants(&transparentMergePush, sizeof(transparentMergePush));
                        commandList.dispatch(softGroupsX, softGroupsY, 1u);
                    }

                    // Transparent RGB resolve: RGB pipeline, its OWN base sets (over transparentSoftHalf + the shared soft-A/B
                    // scratch + the SAME shadowVisibility as the fold target), MULTIPLY fold. The prepareOverride (temporal)
                    // swaps PREPARE to the accumulated transparent history + drives momentsValid.
                    SoftShadowResolveDispatch transparentDispatch;
                    transparentDispatch.pipeline = rayTracingState().m_shadowResolveRgbPipeline.get();
                    transparentDispatch.outputHalfA = rayTracingState().m_transparentResolveBindingSetOutputHalfA.get();
                    transparentDispatch.outputHalfB = rayTracingState().m_transparentResolveBindingSetOutputHalfB.get();
                    transparentDispatch.upsample = rayTracingState().m_transparentResolveBindingSetUpsample.get();
                    transparentDispatch.prepareOverride = transparentPrepareSet;
                    transparentDispatch.fold = SoftShadowUpsampleFold::Multiply;
                    dispatchSoftShadowResolve(commandList, targets, slot, transparentDispatch);
                }

                // The soft transparent fold ran for all soft slots this frame -> the OLD transparent coarse/adaptive/uniform
                // multiply is skipped below (anti-double-fold). Since every shadow slot is soft, this is all-or-nothing.
                softTransparentRan = true;
            }

            // Frame-end stash + ping-pong for the temporal accumulator (a no-op when temporal is off): stash this frame's
            // worldToClip for next-frame reprojection and swap the history / moments / geometry ping-pong so this frame's
            // accumulated output + geometry become next frame's history-in + prev-geometry. Covers BOTH the opaque and the
            // transparent histories (both keyed off the single m_softShadowHistoryFrontIsA selector, flipped once here).
            swapSoftShadowTemporalHistory(targets);
        }
    }

    // OLD transparent colored-shadow path (the HARD-ish coarse/adaptive/uniform multiply). SKIPPED for soft slots when the
    // Stage-5 soft transparent fold ran (softTransparentRan) -- the two are EXCLUSIVE per slot so the colored shadow is
    // never double-folded. Since every shadow slot is soft (Stage 2+), softTransparentRan is all-or-nothing; a non-soft
    // frame (or the hybrid HW path, where soft opaque never runs) leaves it false and this path runs exactly as before.
    if(!softTransparentRan && rayTracingState().m_swShadowAdaptiveEnabled){
        // Stage-2/3 ADAPTIVE transparent shadow. Shared base: the mode-4 coarse (half-res) trace. The resolve is then
        // either Stage-2's in-place conditional re-trace (mode 5) or, when NWB_SW_SHADOW_COMPACT is set, Stage-3's
        // compacted-indirect path (mode 6 classify+append -> mode 7 build-args -> mode 8 DispatchIndirect trace), which
        // launches ONLY the edge rays as coherent waves instead of a full-res grid that diverges on the ~3% edge lanes.
        // Edge-fraction instrumentation rides a slow cadence: snapshot the GPU counter every s_SwShadowEdgeStatsPeriod
        // ticks and read it back s_SwShadowEdgeStatsLogDelay ticks later (by then GPU-complete, so the map never stalls).
        const bool compact = rayTracingState().m_swShadowCompactEnabled;
        const u32 tick = rayTracingState().m_swShadowEdgeStatsTick++;
        const bool snapshot =
            rayTracingState().m_swShadowEdgeStatsEnabled
            && !rayTracingState().m_swShadowEdgeStatsPending
            && (tick % s_SwShadowEdgeStatsPeriod == 0u)
        ;

        if(snapshot){
            commandList.clearBufferUInt(rayTracingState().m_swShadowEdgeStatsBuffer.get(), 0u);
            commandList.setBufferState(rayTracingState().m_swShadowEdgeStatsBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.commitBarriers();
        }

        // Transparent coarse: one transparent trace per coarse block written into the coarse buffer (colored
        // transmittance only). Shared base for both the compacted and the Stage-2 adaptive resolve.
        SwShadowTransparentCoarsePushConstants coarsePush;
        coarsePush.width = targets.width;
        coarsePush.height = targets.height;
        coarsePush.instanceCount = rayTracingState().m_sceneBvhInstanceCount;
        coarsePush.coarseWidth = coarseWidth;
        coarsePush.coarseHeight = coarseHeight;
        commandList.setComputeState(passState(rayTracingState().m_swShadowTransparentCoarsePipeline));
        commandList.setPushConstants(&coarsePush, sizeof(coarsePush));
        commandList.dispatch(coarseGroupsX, coarseGroupsY, 1u);

        // Sync the coarse buffer before the resolve UAV-reads it (UAV write -> UAV read on the same image).
        commandList.setTextureState(targets.shadowCoarseTransmittance.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
        commandList.commitBarriers();

        if(compact){
            // Stage-3 COMPACTED resolve. Reset the per-frame append counter (the list needs no clear -- mode 8 reads only
            // indices < the clamped count, all written this frame) and stage the compaction buffers writable.
            commandList.setEnableUavBarriersForBuffer(rayTracingState().m_swShadowEdgeCounterBuffer.get(), true);
            commandList.setEnableUavBarriersForBuffer(rayTracingState().m_swShadowEdgeListBuffer.get(), true);
            commandList.clearBufferUInt(rayTracingState().m_swShadowEdgeCounterBuffer.get(), 0u);
            commandList.setBufferState(rayTracingState().m_swShadowEdgeCounterBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(rayTracingState().m_swShadowEdgeListBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(rayTracingState().m_swShadowIndirectArgsBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.commitBarriers();

            // Classify (Stage-3): classify each pixel/light; interior -> interpolate + fold in place; edge -> append to
            // the list and leave the PRISTINE opaque mask for the indirect trace's single overwrite. collectStats tallies
            // the fraction on snapshots.
            SwShadowTransparentClassifyPushConstants classifyPush;
            classifyPush.width = targets.width;
            classifyPush.height = targets.height;
            classifyPush.coarseWidth = coarseWidth;
            classifyPush.coarseHeight = coarseHeight;
            classifyPush.edgeThreshold = rayTracingState().m_swShadowEdgeThreshold;
            classifyPush.collectStats = snapshot ? 1u : 0u;
            classifyPush.edgeCapacity = rayTracingState().m_swShadowEdgeListCapacity;
            commandList.setComputeState(passState(rayTracingState().m_swShadowTransparentClassifyPipeline));
            commandList.setPushConstants(&classifyPush, sizeof(classifyPush));
            commandList.dispatch(fullGroupsX, fullGroupsY, 1u);

            // Sync the append counter + edge list (producer mode 6 -> consumers mode 7/8) and the visibility WAW (mode 6's
            // interior/overflow writes -> mode 8's edge overwrites). UAV barriers are enabled on all three.
            commandList.setBufferState(rayTracingState().m_swShadowEdgeCounterBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(rayTracingState().m_swShadowEdgeListBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setTextureState(targets.shadowVisibility.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess);
            commandList.commitBarriers();

            // Build args (Stage-3): 1 thread builds DispatchIndirectArguments{ceil(count/64),1,1} from the clamped append count.
            SwShadowTransparentBuildArgsPushConstants argsPush;
            argsPush.traceGroupSize = static_cast<u32>(NWB_SW_SHADOW_TRACE_GROUP);
            argsPush.edgeCapacity = rayTracingState().m_swShadowEdgeListCapacity;
            commandList.setComputeState(passState(rayTracingState().m_swShadowTransparentBuildArgsPipeline));
            commandList.setPushConstants(&argsPush, sizeof(argsPush));
            commandList.dispatch(1u, 1u, 1u);

            // Sync mode-7's args write before the indirect consume, and keep the list/counter readable by mode 8.
            commandList.setBufferState(rayTracingState().m_swShadowEdgeCounterBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.setBufferState(rayTracingState().m_swShadowEdgeListBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList.commitBarriers();

            // Indirect trace (Stage-3): DispatchIndirect over the compacted edge records -- one ray per thread, all real
            // edge rays. Its ComputeState carries the indirect-args buffer; setComputeState auto-transitions it
            // UnorderedAccess->IndirectArgument.
            SwShadowTransparentIndirectPushConstants tracePush;
            tracePush.width = targets.width;
            tracePush.height = targets.height;
            tracePush.instanceCount = rayTracingState().m_sceneBvhInstanceCount;
            tracePush.traceGroupSize = static_cast<u32>(NWB_SW_SHADOW_TRACE_GROUP);
            Core::ComputeState computeStateIndirect = passState(rayTracingState().m_swShadowTransparentIndirectPipeline);
            computeStateIndirect.setIndirectParams(rayTracingState().m_swShadowIndirectArgsBuffer.get());
            commandList.setComputeState(computeStateIndirect);
            commandList.setPushConstants(&tracePush, sizeof(tracePush));
            commandList.dispatchIndirect(0u);
        }
        else{
            // Stage-2 resolve: full-res adaptive (interpolate interior / re-trace edges in place, fold onto the opaque mask).
            SwShadowTransparentResolvePushConstants resolvePush;
            resolvePush.width = targets.width;
            resolvePush.height = targets.height;
            resolvePush.instanceCount = rayTracingState().m_sceneBvhInstanceCount;
            resolvePush.coarseWidth = coarseWidth;
            resolvePush.coarseHeight = coarseHeight;
            resolvePush.edgeThreshold = rayTracingState().m_swShadowEdgeThreshold;
            resolvePush.collectStats = snapshot ? 1u : 0u;
            commandList.setComputeState(passState(rayTracingState().m_swShadowTransparentResolvePipeline));
            commandList.setPushConstants(&resolvePush, sizeof(resolvePush));
            commandList.dispatch(fullGroupsX, fullGroupsY, 1u);
        }

        if(snapshot){
            // Snapshot the counter into the CPU-readable buffer; the map happens s_SwShadowEdgeStatsLogDelay ticks later.
            commandList.setBufferState(rayTracingState().m_swShadowEdgeStatsBuffer.get(), Core::ResourceStates::CopySource);
            commandList.commitBarriers();
            commandList.copyBuffer(
                rayTracingState().m_swShadowEdgeStatsReadback.get(), 0u,
                rayTracingState().m_swShadowEdgeStatsBuffer.get(), 0u,
                static_cast<u64>(sizeof(u32) * NWB_SW_SHADOW_EDGE_STATS_COUNT)
            );
            rayTracingState().m_swShadowEdgeStatsPending = true;
            rayTracingState().m_swShadowEdgeStatsPendingTick = tick;
        }
        else if(
            rayTracingState().m_swShadowEdgeStatsPending
            && (tick - rayTracingState().m_swShadowEdgeStatsPendingTick) >= s_SwShadowEdgeStatsLogDelay
        ){
            const u32* stats = static_cast<const u32*>(graphics().getDevice()->mapBuffer(rayTracingState().m_swShadowEdgeStatsReadback.get(), Core::CpuAccessMode::Read));
            if(stats){
                const u32 traced = stats[NWB_SW_SHADOW_EDGE_STATS_TRACED];
                const u32 total = stats[NWB_SW_SHADOW_EDGE_STATS_TOTAL];
                graphics().getDevice()->unmapBuffer(rayTracingState().m_swShadowEdgeStatsReadback.get());
                const f64 fraction = (total > 0u) ? (100.0 * static_cast<f64>(traced) / static_cast<f64>(total)) : 0.0;
                NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: SW shadow adaptive edge fraction = {}% ({} traced / {} total rays, threshold {})")
                    , fraction
                    , static_cast<u64>(traced)
                    , static_cast<u64>(total)
                    , static_cast<f64>(rayTracingState().m_swShadowEdgeThreshold)
                );
            }
            rayTracingState().m_swShadowEdgeStatsPending = false;
        }
    }
    else if(!softTransparentRan){
        // Non-adaptive baseline: uniform transparent multiply at HALF resolution -- one trace per 2x2 block folded onto
        // each full-res pixel's own opaque mask. Kept for A/B comparison against the adaptive path. Dispatched at the
        // COARSE grid, exactly as before (the kernel's LITERAL *2u half-res fold is preserved -- see the shader).
        SwShadowTransparentUniformPushConstants pushConstants;
        pushConstants.width = targets.width;
        pushConstants.height = targets.height;
        pushConstants.instanceCount = rayTracingState().m_sceneBvhInstanceCount;
        commandList.setComputeState(passState(rayTracingState().m_swShadowTransparentUniformPipeline));
        commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
        commandList.dispatch(coarseGroupsX, coarseGroupsY, 1u);
    }

    if(!rayTracingState().m_swShadowDispatchLogged){
        rayTracingState().m_swShadowDispatchLogged = true;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: dispatched software shadow traversal ({}x{}, {} instances)")
            , static_cast<u64>(targets.width)
            , static_cast<u64>(targets.height)
            , static_cast<u64>(rayTracingState().m_sceneBvhInstanceCount)
        );
    }
    return true;
}

bool RendererRayTracingSystem::hybridTransparentShadowReady()const noexcept{
    return rayTracingState().m_hybridTransparentShadowReady;
}

bool RendererRayTracingSystem::hasCausticWork()const noexcept{
    // The software caustic producer runs only on the no-hardware-ray-tracing path, and only when the scene holds at
    // least one caustic light AND at least one refractive instance (else the black-cleared irradiance buffer is the
    // additive no-op). The SW scene BVH must also have been built (it is the same geometry the photons trace).
    const bool hardwareShadowSupported =
        graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct)
        && graphics().queryFeatureSupport(Core::Feature::RayQuery)
    ;
    return
        !hardwareShadowSupported
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
    if(graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct) && graphics().queryFeatureSupport(Core::Feature::RayQuery))
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
    const bool resolveReady =
        ensureCausticGeometryDownsamplePipeline()
        && ensureCausticGeometryDownsampleBindingSet(targets)
        && ensureCausticResolvePipeline()
        && ensureCausticResolveBindingSet(targets)
    ;
    // The splat-space temporal EMA decay pre-pass is only dispatched when temporal is enabled (decay > 0); gate its
    // pipeline + binding set on that so the disabled path never builds them.
    const bool temporalReady =
        causticTemporalDecay() <= 0.f
        || (ensureCausticAccumulatorDecayPipeline() && ensureCausticAccumulatorDecayBindingSet(targets))
    ;
    return producerReady && resolveReady && temporalReady;
}

bool RendererRayTracingSystem::renderGpuBvhCaustics(Core::CommandList& commandList, DeferredFrameTargets& targets){
    // Software caustic photon producer + resolve — the no-hardware-ray-tracing fallback (P3). Dispatched in the
    // SW-fallback branch right after the SW shadow pass and BEFORE deferred lighting (which reads the resolved
    // irradiance). The accumulators were already cleared to black by clearCausticTargets this frame. Runs only when
    // hasCausticWork() holds (>=1 caustic light AND >=1 refractive instance), so an empty buffer = additive no-op.

    if(!hasCausticWork())
        return false;
    const f32 temporalDecay = causticTemporalDecay();
    if(
        !rayTracingState().m_swCausticPipeline
        || !rayTracingState().m_swCausticBindingSet
        || !rayTracingState().m_causticResolvePipeline
        || !rayTracingState().m_causticResolveBindingSetOutputHalfA
        || !rayTracingState().m_causticResolveBindingSetOutputHalfB
        || !rayTracingState().m_causticResolveBindingSetUpsample
        || !rayTracingState().m_causticGeometryDownsamplePipeline
        || !rayTracingState().m_causticGeometryDownsampleBindingSet
        || (temporalDecay > 0.f && (!rayTracingState().m_causticAccumulatorDecayPipeline || !rayTracingState().m_causticAccumulatorDecayBindingSet))
    )
        return false;
    if(!targets.causticAccumulator || !targets.causticIrradiance || !targets.causticHistory || !targets.causticResolveHalf || !targets.causticResolveGeometry)
        return false;

    {
        Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_CausticPhotons, graphics().getDevice(), commandList);

        // Splat-space temporal EMA step (enabled paths only): decay the resident accumulator (or clear it on the first
        // frame / after a resize) before this frame's splat. clearCausticTargets skipped the accumulator clear when
        // temporal is on, so this owns the accumulator's per-frame reset. No-op when temporal is disabled (the
        // accumulator was already cleared to 0 by clearCausticTargets).
        if(temporalDecay > 0.f)
            prepareCausticAccumulatorForSplat(commandList, targets, temporalDecay);

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

void RendererRayTracingSystem::appendShadowTraceBindingLayout(Core::BindingLayoutDesc& layoutDesc)const{
    // The hardware inline-RayQuery occlusion trace's bindings, in NWB_SHADOW_RT_* slot order (occlusion.slangi /
    // shadow_rayquery.slangi declare them). Shared by the half-res trace pipeline AND the full-res resolve pipeline
    // (which re-traces silhouette pixels), so both integrate against the identical geometry/material context.
    layoutDesc.addItem(Core::BindingLayoutItem::RayTracingAccelStruct(NWB_SHADOW_RT_BINDING_TLAS, 1)); // inline RayQuery reads the TLAS from compute
    layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_RT_BINDING_GBUFFER_WORLD_POSITION, 1));
    layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_RT_BINDING_GBUFFER_NORMAL, 1));
    layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_RT_BINDING_GBUFFER_DEPTH, 1));
    layoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_SHADOW_RT_BINDING_SCENE_SHADING, 1));
    layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SHADOW_RT_BINDING_LIGHT_LIST, 1));
    layoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_SHADOW_RT_BINDING_VISIBILITY_OUTPUT, 1)); // trace: half-res; resolve: full-res
    layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SHADOW_RT_BINDING_INSTANCE_MATERIAL, 1));
    // Per-mesh descriptor arrays (index + attribute + position byte buffers) the RayQuery trace indexes by
    // material.meshSlot, plus the shared material-constants context buffers the per-hit transmittance dispatch
    // reads. The bounded SRV arrays mirror the software traversal's compute path.
    layoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_SRV(NWB_SHADOW_RT_BINDING_MESH_INDICES, NWB_SHADOW_RT_MAX_MESHES));
    layoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_SRV(NWB_SHADOW_RT_BINDING_MESH_ATTRIBUTES, NWB_SHADOW_RT_MAX_MESHES));
    layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SHADOW_RT_BINDING_MATERIAL_TYPED, 1));
    layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(NWB_SHADOW_RT_BINDING_MESH_INSTANCES, 1));
    layoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_SRV(NWB_SHADOW_RT_BINDING_MESH_POSITIONS, NWB_SHADOW_RT_MAX_MESHES));
    // Soft directional cone-jitter: the frame counter (NwbShadowRqPushConstants) seeding the per-pixel low-discrepancy
    // jitter sample so a soft directional (angular-radius > 0) light softens this HW opaque trace.
    layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(ShadowRqPushConstants)));
}

void RendererRayTracingSystem::appendShadowTraceBindingSet(Core::BindingSetDesc& desc, DeferredFrameTargets& targets, Core::Texture* visibilityTarget)const{
    // The visibility UAV target differs per pass (half-res for the trace, full-res for the resolve); everything else is
    // the identical trace context. The shadow-OWNED material-context buffers (built by buildSceneTlas over ALL gathered
    // occluders) feed the per-hit transmittance dispatch -- NOT the draw pass's buffers (those hold only the opaque set
    // at trace time, so the transparent occluders' tint/constants would be absent).
    const u32 meshCount = rayTracingState().m_shadowMeshCount;
    desc.addItem(Core::BindingSetItem::RayTracingAccelStruct(NWB_SHADOW_RT_BINDING_TLAS, rayTracingState().m_tlas.get()));
    desc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SHADOW_RT_BINDING_GBUFFER_WORLD_POSITION,
        targets.worldPosition.get(),
        targets.worldPositionFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    desc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SHADOW_RT_BINDING_GBUFFER_NORMAL,
        targets.normal.get(),
        targets.normalFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    desc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SHADOW_RT_BINDING_GBUFFER_DEPTH,
        targets.depth.get(),
        targets.depthFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    desc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_SHADOW_RT_BINDING_SCENE_SHADING, deferredState().m_sceneShadingBuffer.get()));
    desc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SHADOW_RT_BINDING_LIGHT_LIST, deferredState().m_lightBuffer.get()));
    desc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_SHADOW_RT_BINDING_VISIBILITY_OUTPUT,
        visibilityTarget,
        targets.shadowVisibilityFormat,
        ECSRenderDetail::s_ShadowVisibilitySubresources,
        Core::TextureDimension::Texture2DArray
    ));
    desc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SHADOW_RT_BINDING_INSTANCE_MATERIAL, rayTracingState().m_shadowInstanceMaterialBuffer.get()));
    desc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SHADOW_RT_BINDING_MATERIAL_TYPED, rayTracingState().m_shadowMaterialTypedBuffer.get()));
    desc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_SHADOW_RT_BINDING_MESH_INSTANCES, rayTracingState().m_shadowInstanceBuffer.get()));

    // Per-mesh descriptor arrays: bind every slot (the trace only indexes meshSlot < meshCount). Unused tail slots
    // are padded with the last real mesh so a non-bindless array has no unbound descriptors.
    for(u32 slot = 0u; slot < NWB_SHADOW_RT_MAX_MESHES; ++slot){
        const u32 source = (slot < meshCount) ? slot : (meshCount - 1u);
        desc.addItem(Core::BindingSetItem::RawBuffer_SRV(NWB_SHADOW_RT_BINDING_MESH_INDICES, rayTracingState().m_shadowMeshIndexBuffers[source]).setArrayElement(slot));
        desc.addItem(Core::BindingSetItem::RawBuffer_SRV(NWB_SHADOW_RT_BINDING_MESH_ATTRIBUTES, rayTracingState().m_shadowMeshAttributeBuffers[source]).setArrayElement(slot));
        desc.addItem(Core::BindingSetItem::RawBuffer_SRV(NWB_SHADOW_RT_BINDING_MESH_POSITIONS, rayTracingState().m_shadowMeshPositionBuffers[source]).setArrayElement(slot));
    }
}

bool RendererRayTracingSystem::ensureShadowPipeline(){
    if(rayTracingState().m_shadowPipeline)
        return true;
    if(rayTracingState().m_shadowPipelineFailed)
        return false;
    // Hardware shadow trace is inline RayQuery in a COMPUTE shader (not the RT pipeline), so it needs RayQuery +
    // the acceleration structure feature (the TLAS it queries).
    if(!graphics().queryFeatureSupport(Core::Feature::RayQuery) || !graphics().queryFeatureSupport(Core::Feature::RayTracingAccelStruct)){
        rayTracingState().m_shadowPipelineFailed = true;
        return false;
    }

    auto* device = graphics().getDevice();

    if(!rayTracingState().m_shadowBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        appendShadowTraceBindingLayout(layoutDesc);

        rayTracingState().m_shadowBindingLayout = device->createBindingLayout(layoutDesc);
        if(!rayTracingState().m_shadowBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow binding layout"));
            rayTracingState().m_shadowPipelineFailed = true;
            return false;
        }
    }

    if(!m_renderer.shaderSystem().loadShader(
        rayTracingState().m_shadowShader,
        AssetsGraphicsShadow::s_RayQueryShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_ShadowRayQuery"
    )){
        rayTracingState().m_shadowPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(rayTracingState().m_shadowShader)
        .addBindingLayout(rayTracingState().m_shadowBindingLayout)
    ;
    rayTracingState().m_shadowPipeline = device->createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_shadowPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create RayQuery shadow compute pipeline"));
        rayTracingState().m_shadowPipelineFailed = true;
        return false;
    }

    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created RayQuery shadow compute pipeline"));
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
    // The full-res trace writes the FULL-res visibility (one ray per output pixel) at NWB_SHADOW_RT_BINDING_VISIBILITY_OUTPUT
    // -- the same buffer the deferred lighting samples, mirroring the software traversal (no resolve pass in between).
    appendShadowTraceBindingSet(bindingSetDesc, targets, targets.shadowVisibility.get());

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
    // Idempotent: the shared layout + persistent Stage-2/3 buffers are created once (guarded by m_swShadowBindingLayout),
    // and each per-pass pipeline creation below is itself idempotent (guarded by its own handle). A prior hard failure is
    // sticky.
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
        // Stage-2 adaptive transparent shadow: the half-res coarse transmittance scratch (written by the coarse trace,
        // UAV-read by the resolve) + the edge-fraction stats counter. Always present in the layout (the shader always
        // declares them); the env config only chooses which mode is dispatched and whether the counter is tallied.
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_SW_SHADOW_BINDING_COARSE, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_SW_SHADOW_BINDING_EDGE_STATS, 1));
        // Stage-3 compaction UAVs: the edge append counter, the compacted edge-record list, and the indirect dispatch-args
        // buffer. Always present in the layout (the shader always declares them); the COMPACT env only selects whether the
        // mode-6/7/8 compacted path runs or the mode-5 adaptive path does.
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_SW_SHADOW_BINDING_EDGE_COUNTER, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_SW_SHADOW_BINDING_EDGE_LIST, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(NWB_SW_SHADOW_BINDING_INDIRECT_ARGS, 1));
        // Soft directional shadow (Stage 1): the half-res soft directional visibility UAV the mode-11 jittered trace
        // writes (read by the separate shadow_resolve pipeline). Always in the layout (the shader always declares it).
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_SW_SHADOW_BINDING_SOFT_HALF, 1));
        // Soft COLORED TRANSPARENT shadow (Stage 5): the half-res colored soft transmittance UAV the soft transparent trace
        // writes (read by the SEPARATE RGB shadow_resolve pipeline). Always in the layout -- only the soft transparent trace
        // kernel declares/writes it; the other passes leave it inert. Recreated with the visibility target on resize.
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_SW_SHADOW_BINDING_TRANSPARENT_SOFT_HALF, 1));
        // Push-constant range sized to the LARGEST pass struct: every per-pass pipeline shares this layout and each
        // dispatch sets only its own (smaller) struct's bytes -- see SwShadowMaxPushConstants.
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SwShadowMaxPushConstants)));

        rayTracingState().m_swShadowBindingLayout = device->createBindingLayout(layoutDesc);
        if(!rayTracingState().m_swShadowBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create software shadow binding layout"));
            rayTracingState().m_swShadowPipelineFailed = true;
            return false;
        }

        // The Stage-2 adaptive config is fixed at its shipping defaults (adaptive ON, compact ON, adaptiveOpaque ON, edge
        // threshold 0.1, stats OFF -- see renderer_state.h). Create the persistent edge-fraction counter + its CPU-readable
        // snapshot: both are tiny and always bound (the shader always declares slots 15/16), so they exist alongside the
        // layout regardless of the config -- the config only selects the dispatched mode + whether stats are tallied.

        Core::BufferDesc edgeStatsDesc;
        edgeStatsDesc
            .setByteSize(static_cast<u64>(sizeof(u32) * NWB_SW_SHADOW_EDGE_STATS_COUNT))
            .setStructStride(sizeof(u32))
            .setCanHaveUAVs(true)
            .setDebugName(Name("sw_shadow_edge_stats"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        rayTracingState().m_swShadowEdgeStatsBuffer = graphics().createBuffer(edgeStatsDesc);
        if(!rayTracingState().m_swShadowEdgeStatsBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create SW shadow edge-stats buffer"));
            rayTracingState().m_swShadowPipelineFailed = true;
            return false;
        }

        Core::BufferDesc edgeStatsReadbackDesc;
        edgeStatsReadbackDesc
            .setByteSize(static_cast<u64>(sizeof(u32) * NWB_SW_SHADOW_EDGE_STATS_COUNT))
            .setCpuAccess(Core::CpuAccessMode::Read)
            .setDebugName(Name("sw_shadow_edge_stats_readback"))
            .enableAutomaticStateTracking(Core::ResourceStates::CopyDest)
        ;
        rayTracingState().m_swShadowEdgeStatsReadback = graphics().createBuffer(edgeStatsReadbackDesc);
        if(!rayTracingState().m_swShadowEdgeStatsReadback){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create SW shadow edge-stats readback buffer"));
            rayTracingState().m_swShadowPipelineFailed = true;
            return false;
        }

        // Stage-3 compaction: the persistent per-frame append counter (2 u32) + the indirect dispatch-args buffer (3 u32,
        // created BOTH UAV-writable -- mode 7 writes it -- AND isDrawIndirectArgs so dispatchIndirect's validateIndirectBuffer
        // accepts it). The variable-size edge list is allocated per-resolution in createShadowVisibilityTarget.
        Core::BufferDesc edgeCounterDesc;
        edgeCounterDesc
            .setByteSize(static_cast<u64>(sizeof(u32) * NWB_SW_SHADOW_EDGE_COUNTER_SIZE))
            .setStructStride(sizeof(u32))
            .setCanHaveUAVs(true)
            .setDebugName(Name("sw_shadow_edge_counter"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        rayTracingState().m_swShadowEdgeCounterBuffer = graphics().createBuffer(edgeCounterDesc);
        if(!rayTracingState().m_swShadowEdgeCounterBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create SW shadow edge-counter buffer"));
            rayTracingState().m_swShadowPipelineFailed = true;
            return false;
        }

        Core::BufferDesc indirectArgsDesc;
        indirectArgsDesc
            .setByteSize(static_cast<u64>(sizeof(u32) * 3u))
            .setStructStride(sizeof(u32))
            .setCanHaveUAVs(true)
            .setIsDrawIndirectArgs(true)
            .setDebugName(Name("sw_shadow_indirect_args"))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        rayTracingState().m_swShadowIndirectArgsBuffer = graphics().createBuffer(indirectArgsDesc);
        if(!rayTracingState().m_swShadowIndirectArgsBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create SW shadow indirect-args buffer"));
            rayTracingState().m_swShadowPipelineFailed = true;
            return false;
        }
    }

    // One NAMED pipeline per pass, all against the shared layout. Each pass's kernel references only its own subset of
    // the slot map, so the shared binding set drives them identically; the dispatch selects the pass pipeline the same
    // env-gated way the monolith selected multiplyMode. Any single failure fails the whole ensure (the frame's SW shadow
    // backend is not ready), matching the old single-pipeline behavior.
    const bool passesReady =
        ensureSwShadowPassPipeline(rayTracingState().m_swShadowOpaquePrepassShader, rayTracingState().m_swShadowOpaquePrepassPipeline, AssetsGraphicsShadow::s_SwOpaquePrepassShaderName, "ECSRender_SwShadowOpaquePrepass")
        && ensureSwShadowPassPipeline(rayTracingState().m_swShadowSoftOpaqueShader, rayTracingState().m_swShadowSoftOpaquePipeline, AssetsGraphicsShadow::s_SwSoftOpaqueShaderName, "ECSRender_SwShadowSoftOpaque")
        && ensureSwShadowPassPipeline(rayTracingState().m_swShadowTransparentCoarseShader, rayTracingState().m_swShadowTransparentCoarsePipeline, AssetsGraphicsShadow::s_SwTransparentCoarseShaderName, "ECSRender_SwShadowTransparentCoarse")
        && ensureSwShadowPassPipeline(rayTracingState().m_swShadowTransparentResolveShader, rayTracingState().m_swShadowTransparentResolvePipeline, AssetsGraphicsShadow::s_SwTransparentResolveShaderName, "ECSRender_SwShadowTransparentResolve")
        && ensureSwShadowPassPipeline(rayTracingState().m_swShadowTransparentClassifyShader, rayTracingState().m_swShadowTransparentClassifyPipeline, AssetsGraphicsShadow::s_SwTransparentClassifyShaderName, "ECSRender_SwShadowTransparentClassify")
        && ensureSwShadowPassPipeline(rayTracingState().m_swShadowTransparentBuildArgsShader, rayTracingState().m_swShadowTransparentBuildArgsPipeline, AssetsGraphicsShadow::s_SwTransparentBuildArgsShaderName, "ECSRender_SwShadowTransparentBuildArgs")
        && ensureSwShadowPassPipeline(rayTracingState().m_swShadowTransparentIndirectShader, rayTracingState().m_swShadowTransparentIndirectPipeline, AssetsGraphicsShadow::s_SwTransparentIndirectShaderName, "ECSRender_SwShadowTransparentIndirect")
        && ensureSwShadowPassPipeline(rayTracingState().m_swShadowTransparentUniformShader, rayTracingState().m_swShadowTransparentUniformPipeline, AssetsGraphicsShadow::s_SwTransparentUniformShaderName, "ECSRender_SwShadowTransparentUniform")
        && ensureSwShadowPassPipeline(rayTracingState().m_swShadowTransparentSoftShader, rayTracingState().m_swShadowTransparentSoftPipeline, AssetsGraphicsShadow::s_SwTransparentSoftShaderName, "ECSRender_SwShadowTransparentSoft")
    ;
    if(!passesReady){
        rayTracingState().m_swShadowPipelineFailed = true;
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureSwShadowPassPipeline(Core::ShaderHandle& shader, Core::ComputePipelineHandle& pipeline, const Name& shaderName, const char* debugLabel){
    // Idempotent per-pass loader + compute-pipeline creator against the SHARED software-shadow binding layout (created by
    // ensureSwShadowPipeline before any pass is built). Returns true if the pipeline is already/newly resident; a failure
    // here bubbles up to fail the whole SW shadow ensure for the frame.
    if(pipeline)
        return true;

    if(!m_renderer.shaderSystem().loadShader(
        shader,
        shaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        debugLabel
    ))
        return false;

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(shader)
        .addBindingLayout(rayTracingState().m_swShadowBindingLayout)
    ;
    pipeline = graphics().getDevice()->createComputePipeline(pipelineDesc);
    if(!pipeline){
        // debugLabel identifies the failing pass in the shader-load path already; keep the message argument-free (the
        // NWB_TEXT log string is wide, and debugLabel is a narrow const char* the wide formatter cannot consume).
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create software shadow compute pipeline"));
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
    NWB_ASSERT(targets.shadowCoarseTransmittance);
    NWB_ASSERT(targets.shadowSoftHalfA);
    NWB_ASSERT(targets.transparentSoftHalf);
    NWB_ASSERT(rayTracingState().m_swShadowEdgeStatsBuffer);
    NWB_ASSERT(rayTracingState().m_swShadowEdgeCounterBuffer);
    NWB_ASSERT(rayTracingState().m_swShadowEdgeListBuffer);
    NWB_ASSERT(rayTracingState().m_swShadowIndirectArgsBuffer);
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
    // Stage-2 adaptive transparent shadow: the half-res coarse transmittance scratch (UAV) + the edge-fraction counter
    // (UAV). The coarse texture is recreated with the visibility target on resize, so tracking the visibility pointer in
    // the rebuild guard also covers it; the stats buffer is persistent.
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_SW_SHADOW_BINDING_COARSE,
        targets.shadowCoarseTransmittance.get(),
        targets.shadowCoarseTransmittanceFormat,
        ECSRenderDetail::s_ShadowVisibilitySubresources,
        Core::TextureDimension::Texture2DArray
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_SW_SHADOW_BINDING_EDGE_STATS, rayTracingState().m_swShadowEdgeStatsBuffer.get()));
    // Stage-3 compaction UAVs: append counter, compacted edge list, indirect dispatch-args.
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_SW_SHADOW_BINDING_EDGE_COUNTER, rayTracingState().m_swShadowEdgeCounterBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_SW_SHADOW_BINDING_EDGE_LIST, rayTracingState().m_swShadowEdgeListBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_SW_SHADOW_BINDING_INDIRECT_ARGS, rayTracingState().m_swShadowIndirectArgsBuffer.get()));
    // Soft directional shadow (Stage 1): the half-res soft directional visibility UAV (recreated with the visibility
    // target on resize, so tracking the visibility pointer in the rebuild guard also covers it).
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_SW_SHADOW_BINDING_SOFT_HALF,
        targets.shadowSoftHalfA.get(),
        targets.shadowSoftFormat,
        ECSRenderDetail::s_ShadowVisibilitySubresources,
        Core::TextureDimension::Texture2DArray
    ));
    // Soft COLORED TRANSPARENT shadow (Stage 5): the half-res colored soft transmittance UAV (recreated with the visibility
    // target on resize, so the tracked visibility-pointer rebuild guard also covers it).
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_SW_SHADOW_BINDING_TRANSPARENT_SOFT_HALF,
        targets.transparentSoftHalf.get(),
        targets.shadowSoftFormat,
        ECSRenderDetail::s_ShadowVisibilitySubresources,
        Core::TextureDimension::Texture2DArray
    ));

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
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_CAUSTIC_RESOLVE_BINDING_GEOMETRY, 1));
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
    NWB_ASSERT(targets.causticResolveGeometry);

    // Non-const (BindingSetItem needs a mutable Texture*); the const cache fields still compare/assign fine.
    Core::Texture* accumulatorTarget = targets.causticAccumulator.get();
    Core::Texture* worldPositionTarget = targets.worldPosition.get();
    Core::Texture* depthTarget = targets.depth.get();
    Core::Texture* irradianceTarget = targets.causticIrradiance.get();
    Core::Texture* halfATarget = targets.causticHistory.get();
    Core::Texture* halfBTarget = targets.causticResolveHalf.get();
    Core::Texture* geometryTarget = targets.causticResolveGeometry.get();
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
        && rayTracingState().m_causticResolveBindingSetGeometry == geometryTarget
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
        desc.addItem(Core::BindingSetItem::Texture_SRV(
            NWB_CAUSTIC_RESOLVE_BINDING_GEOMETRY,
            geometryTarget,
            targets.causticHistoryFormat,
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
        rayTracingState().m_causticResolveBindingSetGeometry = nullptr;
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
    rayTracingState().m_causticResolveBindingSetGeometry = geometryTarget;
    return true;
}

bool RendererRayTracingSystem::ensureCausticGeometryDownsamplePipeline(){
    if(rayTracingState().m_causticGeometryDownsamplePipeline)
        return true;
    if(rayTracingState().m_causticGeometryDownsamplePipelineFailed)
        return false;

    auto* device = graphics().getDevice();

    if(!rayTracingState().m_causticGeometryDownsampleBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_CAUSTIC_GEOMETRY_DOWNSAMPLE_BINDING_GBUFFER_WORLD_POSITION, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_CAUSTIC_GEOMETRY_DOWNSAMPLE_BINDING_GBUFFER_DEPTH, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_CAUSTIC_GEOMETRY_DOWNSAMPLE_BINDING_GEOMETRY_OUTPUT, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(CausticGeometryDownsamplePushConstants)));

        rayTracingState().m_causticGeometryDownsampleBindingLayout = device->createBindingLayout(layoutDesc);
        if(!rayTracingState().m_causticGeometryDownsampleBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic geometry downsample binding layout"));
            rayTracingState().m_causticGeometryDownsamplePipelineFailed = true;
            return false;
        }
    }

    if(!m_renderer.shaderSystem().loadShader(
        rayTracingState().m_causticGeometryDownsampleShader,
        AssetsGraphicsCaustic::s_GeometryDownsampleShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_CausticGeometryDownsample"
    )){
        rayTracingState().m_causticGeometryDownsamplePipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(rayTracingState().m_causticGeometryDownsampleShader)
        .addBindingLayout(rayTracingState().m_causticGeometryDownsampleBindingLayout)
    ;
    rayTracingState().m_causticGeometryDownsamplePipeline = device->createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_causticGeometryDownsamplePipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic geometry downsample compute pipeline"));
        rayTracingState().m_causticGeometryDownsamplePipelineFailed = true;
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureCausticGeometryDownsampleBindingSet(DeferredFrameTargets& targets){
    NWB_ASSERT(rayTracingState().m_causticGeometryDownsampleBindingLayout);
    NWB_ASSERT(targets.worldPosition);
    NWB_ASSERT(targets.depth);
    NWB_ASSERT(targets.causticResolveGeometry);

    Core::Texture* worldPositionTarget = targets.worldPosition.get();
    Core::Texture* depthTarget = targets.depth.get();
    Core::Texture* geometryTarget = targets.causticResolveGeometry.get();
    if(
        rayTracingState().m_causticGeometryDownsampleBindingSet
        && rayTracingState().m_causticGeometryDownsampleWorldPosition == worldPositionTarget
        && rayTracingState().m_causticGeometryDownsampleDepth == depthTarget
        && rayTracingState().m_causticGeometryDownsampleGeometry == geometryTarget
    )
        return true;

    auto* device = graphics().getDevice();

    Core::BindingSetDesc desc(arena());
    desc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_CAUSTIC_GEOMETRY_DOWNSAMPLE_BINDING_GBUFFER_WORLD_POSITION,
        worldPositionTarget,
        targets.worldPositionFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    desc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_CAUSTIC_GEOMETRY_DOWNSAMPLE_BINDING_GBUFFER_DEPTH,
        depthTarget,
        targets.depthFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    desc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_CAUSTIC_GEOMETRY_DOWNSAMPLE_BINDING_GEOMETRY_OUTPUT,
        geometryTarget,
        targets.causticHistoryFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));

    Core::BindingSetHandle bindingSet = device->createBindingSet(desc, rayTracingState().m_causticGeometryDownsampleBindingLayout);
    if(!bindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic geometry downsample binding set"));
        rayTracingState().m_causticGeometryDownsampleBindingSet = nullptr;
        rayTracingState().m_causticGeometryDownsampleWorldPosition = nullptr;
        rayTracingState().m_causticGeometryDownsampleDepth = nullptr;
        rayTracingState().m_causticGeometryDownsampleGeometry = nullptr;
        return false;
    }
    rayTracingState().m_causticGeometryDownsampleBindingSet = Move(bindingSet);
    rayTracingState().m_causticGeometryDownsampleWorldPosition = worldPositionTarget;
    rayTracingState().m_causticGeometryDownsampleDepth = depthTarget;
    rayTracingState().m_causticGeometryDownsampleGeometry = geometryTarget;
    return true;
}

bool RendererRayTracingSystem::ensureShadowGeometryDownsamplePipeline(){
    if(rayTracingState().m_shadowGeometryDownsamplePipeline)
        return true;
    if(rayTracingState().m_shadowGeometryDownsamplePipelineFailed)
        return false;

    auto* device = graphics().getDevice();

    if(!rayTracingState().m_shadowGeometryDownsampleBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_GBUFFER_WORLD_POSITION, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_GBUFFER_NORMAL, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_GBUFFER_DEPTH, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_SCENE_SHADING, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_GEOMETRY_OUTPUT, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(ShadowGeometryDownsamplePushConstants)));

        rayTracingState().m_shadowGeometryDownsampleBindingLayout = device->createBindingLayout(layoutDesc);
        if(!rayTracingState().m_shadowGeometryDownsampleBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow geometry downsample binding layout"));
            rayTracingState().m_shadowGeometryDownsamplePipelineFailed = true;
            return false;
        }
    }

    if(!m_renderer.shaderSystem().loadShader(
        rayTracingState().m_shadowGeometryDownsampleShader,
        AssetsGraphicsShadow::s_GeometryDownsampleShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_ShadowGeometryDownsample"
    )){
        rayTracingState().m_shadowGeometryDownsamplePipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(rayTracingState().m_shadowGeometryDownsampleShader)
        .addBindingLayout(rayTracingState().m_shadowGeometryDownsampleBindingLayout)
    ;
    rayTracingState().m_shadowGeometryDownsamplePipeline = device->createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_shadowGeometryDownsamplePipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow geometry downsample compute pipeline"));
        rayTracingState().m_shadowGeometryDownsamplePipelineFailed = true;
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureShadowGeometryDownsampleBindingSet(DeferredFrameTargets& targets){
    NWB_ASSERT(rayTracingState().m_shadowGeometryDownsampleBindingLayout);
    NWB_ASSERT(targets.worldPosition);
    NWB_ASSERT(targets.normal);
    NWB_ASSERT(targets.depth);
    NWB_ASSERT(targets.shadowSoftGeometry);
    NWB_ASSERT(deferredState().m_sceneShadingBuffer);

    Core::Texture* worldPositionTarget = targets.worldPosition.get();
    Core::Texture* normalTarget = targets.normal.get();
    Core::Texture* depthTarget = targets.depth.get();
    Core::Texture* geometryTarget = targets.shadowSoftGeometry.get();
    if(
        rayTracingState().m_shadowGeometryDownsampleBindingSet
        && rayTracingState().m_shadowGeometryDownsampleWorldPosition == worldPositionTarget
        && rayTracingState().m_shadowGeometryDownsampleNormal == normalTarget
        && rayTracingState().m_shadowGeometryDownsampleDepth == depthTarget
        && rayTracingState().m_shadowGeometryDownsampleGeometry == geometryTarget
    )
        return true;

    auto* device = graphics().getDevice();

    Core::BindingSetDesc desc(arena());
    desc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_GBUFFER_WORLD_POSITION,
        worldPositionTarget,
        targets.worldPositionFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    desc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_GBUFFER_NORMAL,
        normalTarget,
        targets.normalFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    desc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_GBUFFER_DEPTH,
        depthTarget,
        targets.depthFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    desc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_SCENE_SHADING, deferredState().m_sceneShadingBuffer.get()));
    desc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_SHADOW_GEOMETRY_DOWNSAMPLE_BINDING_GEOMETRY_OUTPUT,
        geometryTarget,
        targets.shadowSoftGeometryFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));

    Core::BindingSetHandle bindingSet = device->createBindingSet(desc, rayTracingState().m_shadowGeometryDownsampleBindingLayout);
    if(!bindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow geometry downsample binding set"));
        rayTracingState().m_shadowGeometryDownsampleBindingSet = nullptr;
        rayTracingState().m_shadowGeometryDownsampleWorldPosition = nullptr;
        rayTracingState().m_shadowGeometryDownsampleNormal = nullptr;
        rayTracingState().m_shadowGeometryDownsampleDepth = nullptr;
        rayTracingState().m_shadowGeometryDownsampleGeometry = nullptr;
        return false;
    }
    rayTracingState().m_shadowGeometryDownsampleBindingSet = Move(bindingSet);
    rayTracingState().m_shadowGeometryDownsampleWorldPosition = worldPositionTarget;
    rayTracingState().m_shadowGeometryDownsampleNormal = normalTarget;
    rayTracingState().m_shadowGeometryDownsampleDepth = depthTarget;
    rayTracingState().m_shadowGeometryDownsampleGeometry = geometryTarget;
    return true;
}

bool RendererRayTracingSystem::ensureSoftShadowResolvePipeline(){
    if(rayTracingState().m_shadowResolvePipeline)
        return true;
    if(rayTracingState().m_shadowResolvePipelineFailed)
        return false;

    auto* device = graphics().getDevice();

    if(!rayTracingState().m_shadowResolveBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_RESOLVE_BINDING_SOFT_HALF, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_RESOLVE_BINDING_GEOMETRY, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_RESOLVE_BINDING_GBUFFER_DEPTH, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_SHADOW_RESOLVE_BINDING_OUTPUT, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_RESOLVE_BINDING_INPUT_COLOR, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_SHADOW_RESOLVE_BINDING_VISIBILITY, 1));
        // Stage 4: the SVGF temporal moments SRV (variance-coupled a-trous) + the full-res world-pos/normal SRVs and the
        // scene-shading CB (full-res-guided upsample). The moments SRV is a dummy on the non-temporal path (see dispatch).
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_RESOLVE_BINDING_MOMENTS, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_RESOLVE_BINDING_GBUFFER_WORLDPOS, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_RESOLVE_BINDING_GBUFFER_NORMAL, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(NWB_SHADOW_RESOLVE_BINDING_SCENE_SHADING, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(ShadowResolvePushConstants)));

        rayTracingState().m_shadowResolveBindingLayout = device->createBindingLayout(layoutDesc);
        if(!rayTracingState().m_shadowResolveBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow resolve binding layout"));
            rayTracingState().m_shadowResolvePipelineFailed = true;
            return false;
        }
    }

    if(!m_renderer.shaderSystem().loadShader(
        rayTracingState().m_shadowResolveShader,
        AssetsGraphicsShadow::s_SoftResolveShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_SoftShadowResolve"
    )){
        rayTracingState().m_shadowResolvePipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(rayTracingState().m_shadowResolveShader)
        .addBindingLayout(rayTracingState().m_shadowResolveBindingLayout)
    ;
    rayTracingState().m_shadowResolvePipeline = device->createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_shadowResolvePipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow resolve compute pipeline"));
        rayTracingState().m_shadowResolvePipelineFailed = true;
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureSoftTransparentResolvePipeline(){
    // Stage 5: the RGB variant of the soft-shadow a-trous resolve -- the SAME shadow_resolve source cooked with
    // NWB_SHADOW_RESOLVE_CHANNELS=3 (via the shadow_resolve_rgb_cs wrapper). It shares the resolve BINDING LAYOUT (the
    // bindings are identical; only the wavelet channel count + a runtime fold flag differ), created by
    // ensureSoftShadowResolvePipeline -- always called first (m_softTransparentReady is gated on m_softShadowReady), so the
    // layout is resident. Idempotent per handle; a prior hard failure is sticky.
    if(rayTracingState().m_shadowResolveRgbPipeline)
        return true;
    if(rayTracingState().m_shadowResolveRgbPipelineFailed)
        return false;

    NWB_ASSERT(rayTracingState().m_shadowResolveBindingLayout); // opaque resolve pipeline (built first) owns the shared layout

    auto* device = graphics().getDevice();

    if(!m_renderer.shaderSystem().loadShader(
        rayTracingState().m_shadowResolveRgbShader,
        AssetsGraphicsShadow::s_SoftResolveRgbShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_SoftShadowResolveRgb"
    )){
        rayTracingState().m_shadowResolveRgbPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(rayTracingState().m_shadowResolveRgbShader)
        .addBindingLayout(rayTracingState().m_shadowResolveBindingLayout)
    ;
    rayTracingState().m_shadowResolveRgbPipeline = device->createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_shadowResolveRgbPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft transparent shadow RGB resolve compute pipeline"));
        rayTracingState().m_shadowResolveRgbPipelineFailed = true;
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureSoftShadowResolveBindingSet(DeferredFrameTargets& targets){
    NWB_ASSERT(rayTracingState().m_shadowResolveBindingLayout);
    NWB_ASSERT(targets.shadowSoftHalfA);
    NWB_ASSERT(targets.shadowSoftHalfB);
    NWB_ASSERT(targets.shadowSoftGeometry);
    NWB_ASSERT(targets.depth);
    NWB_ASSERT(targets.shadowVisibility);
    NWB_ASSERT(targets.shadowHistA);
    NWB_ASSERT(targets.shadowHistB);
    NWB_ASSERT(targets.shadowMomentsA);
    NWB_ASSERT(targets.shadowMomentsB);
    NWB_ASSERT(targets.worldPosition);
    NWB_ASSERT(targets.normal);
    NWB_ASSERT(deferredState().m_sceneShadingBuffer);

    Core::Texture* softATarget = targets.shadowSoftHalfA.get();
    Core::Texture* softBTarget = targets.shadowSoftHalfB.get();
    Core::Texture* geometryTarget = targets.shadowSoftGeometry.get();
    Core::Texture* depthTarget = targets.depth.get();
    Core::Texture* visibilityTarget = targets.shadowVisibility.get();
    // The two TEMPORAL SOFT_HALF variants read the accumulated history (hist-A / hist-B) as the PREPARE input instead of the
    // raw soft-A trace. Track their bound handles too so a resize / frame-end swap that changes which physical texture the
    // hist role points at rebuilds the sets (mirrors the base tracked-pointer rebuild).
    Core::Texture* histATarget = targets.shadowHistA.get();
    Core::Texture* histBTarget = targets.shadowHistB.get();
    // Stage 4: the SVGF moments buffers (bound as the MOMENTS SRV on the matching temporal set; a dummy on the others),
    // plus the full-res world-position + normal G-buffers the guided upsample reads. All tracked for the resize rebuild.
    Core::Texture* momentsATarget = targets.shadowMomentsA.get();
    Core::Texture* momentsBTarget = targets.shadowMomentsB.get();
    Core::Texture* worldPositionTarget = targets.worldPosition.get();
    Core::Texture* normalTarget = targets.normal.get();
    if(
        rayTracingState().m_shadowResolveBindingSetOutputHalfA
        && rayTracingState().m_shadowResolveBindingSetOutputHalfB
        && rayTracingState().m_shadowResolveBindingSetUpsample
        && rayTracingState().m_shadowResolveBindingSetTemporalHistA
        && rayTracingState().m_shadowResolveBindingSetTemporalHistB
        && rayTracingState().m_shadowResolveBindingSetSoftHalfA == softATarget
        && rayTracingState().m_shadowResolveBindingSetSoftHalfB == softBTarget
        && rayTracingState().m_shadowResolveBindingSetGeometry == geometryTarget
        && rayTracingState().m_shadowResolveBindingSetDepth == depthTarget
        && rayTracingState().m_shadowResolveBindingSetVisibility == visibilityTarget
        && rayTracingState().m_shadowResolveBindingSetTemporalHistATex == histATarget
        && rayTracingState().m_shadowResolveBindingSetTemporalHistBTex == histBTarget
        && rayTracingState().m_shadowResolveBindingSetMomentsA == momentsATarget
        && rayTracingState().m_shadowResolveBindingSetMomentsB == momentsBTarget
        && rayTracingState().m_shadowResolveBindingSetWorldPos == worldPositionTarget
        && rayTracingState().m_shadowResolveBindingSetNormal == normalTarget
    )
        return true;

    auto* device = graphics().getDevice();

    // SOFT_HALF is the mode-11 trace target (soft-A), read ONLY by the PREPARE stage (which runs on the OutputHalfB
    // set). The three sets differ in the (SOFT_HALF, OUTPUT, INPUT_COLOR) triple, chosen so NO set ever binds the same
    // texture as both an SRV and a UAV (which the resource-state framework cannot resolve to one state):
    //  - OutputHalfB: softHalf=soft-A, out=soft-B, in=soft-A -- PREPARE (copies soft-A -> soft-B) + the even wavelets.
    //  - OutputHalfA: softHalf=soft-B, out=soft-A, in=soft-B -- the odd wavelets. SOFT_HALF is bound-but-unused here, so
    //                 pointing it at soft-B (not soft-A == OUTPUT) avoids an SRV+UAV alias of soft-A in this set.
    //  - Upsample:    softHalf=soft-A, out=full-res visibility, in=soft-A (the final wavelet lands in soft-A, odd count).
    // The half + full targets are dimensionless in the set (the bound texture carries the extent), so one layout serves.
    // momentsTex: the MOMENTS SRV source for this set. For the two temporal variants it is the merge's history-OUT moments
    // buffer (the accumulated moments this frame's a-trous should read); for the non-temporal sets it is a valid-but-unused
    // dummy (any half-res array) -- the shader guards the read behind push.momentsValid == 0, so the dummy is never sampled.
    const auto buildSet = [&](Core::Texture* softHalfTex, Core::Texture* outputTex, Core::Format::Enum outputFormat, Core::TextureDimension::Enum outputDim, Core::Texture* inputTex, Core::Texture* momentsTex) -> Core::BindingSetHandle {
        Core::BindingSetDesc desc(arena());
        desc.addItem(Core::BindingSetItem::Texture_SRV(
            NWB_SHADOW_RESOLVE_BINDING_SOFT_HALF,
            softHalfTex,
            targets.shadowSoftFormat,
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::TextureDimension::Texture2DArray
        ));
        desc.addItem(Core::BindingSetItem::Texture_SRV(
            NWB_SHADOW_RESOLVE_BINDING_GEOMETRY,
            geometryTarget,
            targets.shadowSoftGeometryFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ));
        desc.addItem(Core::BindingSetItem::Texture_SRV(
            NWB_SHADOW_RESOLVE_BINDING_GBUFFER_DEPTH,
            depthTarget,
            targets.depthFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ));
        desc.addItem(Core::BindingSetItem::Texture_UAV(
            NWB_SHADOW_RESOLVE_BINDING_OUTPUT,
            outputTex,
            outputFormat,
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            outputDim
        ));
        desc.addItem(Core::BindingSetItem::Texture_SRV(
            NWB_SHADOW_RESOLVE_BINDING_INPUT_COLOR,
            inputTex,
            targets.shadowSoftFormat,
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::TextureDimension::Texture2DArray
        ));
        desc.addItem(Core::BindingSetItem::Texture_UAV(
            NWB_SHADOW_RESOLVE_BINDING_VISIBILITY,
            visibilityTarget,
            targets.shadowVisibilityFormat,
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::TextureDimension::Texture2DArray
        ));
        // Stage 4A: the temporal moments SRV (per-set source above). Same half-res array format/subresources as the hist
        // buffers (a Texture2DArray, one layer per slot). Dummy-bound + shader-guarded on the non-temporal sets.
        desc.addItem(Core::BindingSetItem::Texture_SRV(
            NWB_SHADOW_RESOLVE_BINDING_MOMENTS,
            momentsTex,
            targets.shadowSoftFormat,
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::TextureDimension::Texture2DArray
        ));
        // Stage 4B: the full-res world-position + normal G-buffers -- the guided upsample's bilateral centre.
        desc.addItem(Core::BindingSetItem::Texture_SRV(
            NWB_SHADOW_RESOLVE_BINDING_GBUFFER_WORLDPOS,
            worldPositionTarget,
            targets.worldPositionFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ));
        desc.addItem(Core::BindingSetItem::Texture_SRV(
            NWB_SHADOW_RESOLVE_BINDING_GBUFFER_NORMAL,
            normalTarget,
            targets.normalFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ));
        // The scene-shading CB (camera world position) -- the upsample centre's camera distance (same buffer + slot pattern
        // the geometry downsample binds).
        desc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_SHADOW_RESOLVE_BINDING_SCENE_SHADING, deferredState().m_sceneShadingBuffer.get()));
        return device->createBindingSet(desc, rayTracingState().m_shadowResolveBindingLayout);
    };

    // OUTPUT is a half-res Texture2DArray for the ping-pong sets; for the upsample it is UNUSED (the upsample writes the
    // full-res VISIBILITY UAV instead) but must still be a valid binding -- point it at soft-B (a half-res array).
    // The non-temporal sets never sample the MOMENTS SRV (push.momentsValid == 0 on those dispatches), so bind moments-A as
    // an inert dummy. The two temporal variants bind the moments buffer PAIRED with the hist buffer they read as SOFT_HALF
    // (hist-A <-> moments-A, hist-B <-> moments-B), i.e. the merge's accumulated moments for this frame's a-trous.
    Core::BindingSetHandle outputHalfA = buildSet(softBTarget, softATarget, targets.shadowSoftFormat, Core::TextureDimension::Texture2DArray, softBTarget, momentsATarget);
    Core::BindingSetHandle outputHalfB = buildSet(softATarget, softBTarget, targets.shadowSoftFormat, Core::TextureDimension::Texture2DArray, softATarget, momentsATarget);
    Core::BindingSetHandle upsample    = buildSet(softATarget, softBTarget, targets.shadowSoftFormat, Core::TextureDimension::Texture2DArray, softATarget, momentsATarget);
    // Two TEMPORAL variants: PREPARE reads the accumulated history (hist-A / hist-B) as SOFT_HALF (+ INPUT_COLOR, unused by
    // PREPARE), writes soft-B (out). SOFT_HALF == hist buffer is DISTINCT from the ping-pong output soft-A/soft-B, so no
    // SRV+UAV alias. The dispatch picks the variant matching the merge's history-out buffer (B when frontIsA, else A). Each
    // binds its paired moments buffer as the MOMENTS SRV so the variance-coupled a-trous reads the accumulated moments.
    Core::BindingSetHandle temporalHistA = buildSet(histATarget, softBTarget, targets.shadowSoftFormat, Core::TextureDimension::Texture2DArray, histATarget, momentsATarget);
    Core::BindingSetHandle temporalHistB = buildSet(histBTarget, softBTarget, targets.shadowSoftFormat, Core::TextureDimension::Texture2DArray, histBTarget, momentsBTarget);
    if(!outputHalfA || !outputHalfB || !upsample || !temporalHistA || !temporalHistB){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft shadow resolve binding sets"));
        rayTracingState().m_shadowResolveBindingSetOutputHalfA = nullptr;
        rayTracingState().m_shadowResolveBindingSetOutputHalfB = nullptr;
        rayTracingState().m_shadowResolveBindingSetUpsample = nullptr;
        rayTracingState().m_shadowResolveBindingSetTemporalHistA = nullptr;
        rayTracingState().m_shadowResolveBindingSetTemporalHistB = nullptr;
        rayTracingState().m_shadowResolveBindingSetSoftHalfA = nullptr;
        rayTracingState().m_shadowResolveBindingSetSoftHalfB = nullptr;
        rayTracingState().m_shadowResolveBindingSetGeometry = nullptr;
        rayTracingState().m_shadowResolveBindingSetDepth = nullptr;
        rayTracingState().m_shadowResolveBindingSetVisibility = nullptr;
        rayTracingState().m_shadowResolveBindingSetTemporalHistATex = nullptr;
        rayTracingState().m_shadowResolveBindingSetTemporalHistBTex = nullptr;
        rayTracingState().m_shadowResolveBindingSetMomentsA = nullptr;
        rayTracingState().m_shadowResolveBindingSetMomentsB = nullptr;
        rayTracingState().m_shadowResolveBindingSetWorldPos = nullptr;
        rayTracingState().m_shadowResolveBindingSetNormal = nullptr;
        return false;
    }
    rayTracingState().m_shadowResolveBindingSetOutputHalfA = Move(outputHalfA);
    rayTracingState().m_shadowResolveBindingSetOutputHalfB = Move(outputHalfB);
    rayTracingState().m_shadowResolveBindingSetUpsample = Move(upsample);
    rayTracingState().m_shadowResolveBindingSetTemporalHistA = Move(temporalHistA);
    rayTracingState().m_shadowResolveBindingSetTemporalHistB = Move(temporalHistB);
    rayTracingState().m_shadowResolveBindingSetSoftHalfA = softATarget;
    rayTracingState().m_shadowResolveBindingSetSoftHalfB = softBTarget;
    rayTracingState().m_shadowResolveBindingSetGeometry = geometryTarget;
    rayTracingState().m_shadowResolveBindingSetDepth = depthTarget;
    rayTracingState().m_shadowResolveBindingSetVisibility = visibilityTarget;
    rayTracingState().m_shadowResolveBindingSetTemporalHistATex = histATarget;
    rayTracingState().m_shadowResolveBindingSetTemporalHistBTex = histBTarget;
    rayTracingState().m_shadowResolveBindingSetMomentsA = momentsATarget;
    rayTracingState().m_shadowResolveBindingSetMomentsB = momentsBTarget;
    rayTracingState().m_shadowResolveBindingSetWorldPos = worldPositionTarget;
    rayTracingState().m_shadowResolveBindingSetNormal = normalTarget;
    return true;
}

bool RendererRayTracingSystem::ensureSoftTransparentResolveBindingSet(DeferredFrameTargets& targets){
    // Stage 5: the PARALLEL transparent resolve binding sets (mirror of ensureSoftShadowResolveBindingSet, over the colored
    // buffers). They share the resolve BINDING LAYOUT + the SAME half-res ping-pong SCRATCH (soft-A/soft-B) + the SAME
    // full-res shadowVisibility as the opaque resolve; only the (SOFT_HALF, INPUT-history, MOMENTS) sources differ:
    //  - the RAW colored trace lives in transparentSoftHalf (NOT soft-A), so PREPARE reads it as SOFT_HALF into soft-B.
    //  - the wavelets ping-pong on soft-A/soft-B exactly as opaque (INPUT_COLOR is the ping-pong scratch, not the raw trace).
    //  - the two temporal variants read the accumulated transparent history (transparentHistA/B) as SOFT_HALF instead.
    // Runs strictly AFTER the opaque resolve each frame (sequential dispatch), so sharing the scratch is race-free.
    NWB_ASSERT(rayTracingState().m_shadowResolveBindingLayout);
    NWB_ASSERT(targets.transparentSoftHalf);
    NWB_ASSERT(targets.shadowSoftHalfA);
    NWB_ASSERT(targets.shadowSoftHalfB);
    NWB_ASSERT(targets.shadowSoftGeometry);
    NWB_ASSERT(targets.depth);
    NWB_ASSERT(targets.shadowVisibility);
    NWB_ASSERT(targets.transparentHistA);
    NWB_ASSERT(targets.transparentHistB);
    NWB_ASSERT(targets.transparentMomentsA);
    NWB_ASSERT(targets.transparentMomentsB);
    NWB_ASSERT(targets.worldPosition);
    NWB_ASSERT(targets.normal);
    NWB_ASSERT(deferredState().m_sceneShadingBuffer);

    Core::Texture* rawTraceTarget = targets.transparentSoftHalf.get();
    Core::Texture* scratchATarget = targets.shadowSoftHalfA.get();
    Core::Texture* scratchBTarget = targets.shadowSoftHalfB.get();
    Core::Texture* geometryTarget = targets.shadowSoftGeometry.get();
    Core::Texture* depthTarget = targets.depth.get();
    Core::Texture* visibilityTarget = targets.shadowVisibility.get();
    Core::Texture* histATarget = targets.transparentHistA.get();
    Core::Texture* histBTarget = targets.transparentHistB.get();
    Core::Texture* momentsATarget = targets.transparentMomentsA.get();
    Core::Texture* momentsBTarget = targets.transparentMomentsB.get();
    Core::Texture* worldPositionTarget = targets.worldPosition.get();
    Core::Texture* normalTarget = targets.normal.get();
    if(
        rayTracingState().m_transparentResolveBindingSetOutputHalfA
        && rayTracingState().m_transparentResolveBindingSetOutputHalfB
        && rayTracingState().m_transparentResolveBindingSetUpsample
        && rayTracingState().m_transparentResolveBindingSetTemporalHistA
        && rayTracingState().m_transparentResolveBindingSetTemporalHistB
        && rayTracingState().m_transparentResolveBindingSetSoftHalf == rawTraceTarget
        && rayTracingState().m_transparentResolveBindingSetScratchA == scratchATarget
        && rayTracingState().m_transparentResolveBindingSetScratchB == scratchBTarget
        && rayTracingState().m_transparentResolveBindingSetGeometry == geometryTarget
        && rayTracingState().m_transparentResolveBindingSetDepth == depthTarget
        && rayTracingState().m_transparentResolveBindingSetVisibility == visibilityTarget
        && rayTracingState().m_transparentResolveBindingSetHistA == histATarget
        && rayTracingState().m_transparentResolveBindingSetHistB == histBTarget
        && rayTracingState().m_transparentResolveBindingSetMomentsA == momentsATarget
        && rayTracingState().m_transparentResolveBindingSetMomentsB == momentsBTarget
        && rayTracingState().m_transparentResolveBindingSetWorldPos == worldPositionTarget
        && rayTracingState().m_transparentResolveBindingSetNormal == normalTarget
    )
        return true;

    auto* device = graphics().getDevice();

    // buildSet: (SOFT_HALF SRV, OUTPUT UAV, INPUT_COLOR SRV, MOMENTS SRV). GEOMETRY/DEPTH/VISIBILITY/WORLDPOS/NORMAL/CB are
    // fixed. No set binds the same texture as both SRV and UAV: the raw colored trace + the two hist buffers are only ever
    // SRVs here; the OUTPUT UAV is always the ping-pong scratch (soft-A/soft-B), distinct from all of them; the VISIBILITY
    // UAV (the fold target) is a separate resource. (The upsample's OUTPUT is bound-but-unused -> soft-B, still no alias.)
    const auto buildSet = [&](Core::Texture* softHalfTex, Core::Texture* outputTex, Core::Texture* inputTex, Core::Texture* momentsTex) -> Core::BindingSetHandle {
        Core::BindingSetDesc desc(arena());
        desc.addItem(Core::BindingSetItem::Texture_SRV(
            NWB_SHADOW_RESOLVE_BINDING_SOFT_HALF,
            softHalfTex,
            targets.shadowSoftFormat,
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::TextureDimension::Texture2DArray
        ));
        desc.addItem(Core::BindingSetItem::Texture_SRV(
            NWB_SHADOW_RESOLVE_BINDING_GEOMETRY,
            geometryTarget,
            targets.shadowSoftGeometryFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ));
        desc.addItem(Core::BindingSetItem::Texture_SRV(
            NWB_SHADOW_RESOLVE_BINDING_GBUFFER_DEPTH,
            depthTarget,
            targets.depthFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ));
        desc.addItem(Core::BindingSetItem::Texture_UAV(
            NWB_SHADOW_RESOLVE_BINDING_OUTPUT,
            outputTex,
            targets.shadowSoftFormat,
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::TextureDimension::Texture2DArray
        ));
        desc.addItem(Core::BindingSetItem::Texture_SRV(
            NWB_SHADOW_RESOLVE_BINDING_INPUT_COLOR,
            inputTex,
            targets.shadowSoftFormat,
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::TextureDimension::Texture2DArray
        ));
        desc.addItem(Core::BindingSetItem::Texture_UAV(
            NWB_SHADOW_RESOLVE_BINDING_VISIBILITY,
            visibilityTarget,
            targets.shadowVisibilityFormat,
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::TextureDimension::Texture2DArray
        ));
        desc.addItem(Core::BindingSetItem::Texture_SRV(
            NWB_SHADOW_RESOLVE_BINDING_MOMENTS,
            momentsTex,
            targets.shadowSoftFormat,
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::TextureDimension::Texture2DArray
        ));
        desc.addItem(Core::BindingSetItem::Texture_SRV(
            NWB_SHADOW_RESOLVE_BINDING_GBUFFER_WORLDPOS,
            worldPositionTarget,
            targets.worldPositionFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ));
        desc.addItem(Core::BindingSetItem::Texture_SRV(
            NWB_SHADOW_RESOLVE_BINDING_GBUFFER_NORMAL,
            normalTarget,
            targets.normalFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ));
        desc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_SHADOW_RESOLVE_BINDING_SCENE_SHADING, deferredState().m_sceneShadingBuffer.get()));
        return device->createBindingSet(desc, rayTracingState().m_shadowResolveBindingLayout);
    };

    // outputHalfB: PREPARE (SOFT_HALF = the raw colored trace -> soft-B) + even wavelets (INPUT_COLOR = soft-A -> soft-B).
    // outputHalfA: odd wavelets (SOFT_HALF bound-unused; INPUT_COLOR = soft-B -> soft-A). SOFT_HALF -> the raw trace here
    //              (an SRV distinct from the soft-A OUTPUT), NOT soft-A, so no SRV+UAV alias of the scratch.
    // upsample: reads INPUT_COLOR = soft-A (the final odd-count result), folds the VISIBILITY (OUTPUT = soft-B, unused).
    Core::BindingSetHandle outputHalfA = buildSet(rawTraceTarget, scratchATarget, scratchBTarget, momentsATarget);
    Core::BindingSetHandle outputHalfB = buildSet(rawTraceTarget, scratchBTarget, scratchATarget, momentsATarget);
    Core::BindingSetHandle upsample    = buildSet(rawTraceTarget, scratchBTarget, scratchATarget, momentsATarget);
    // Temporal variants: PREPARE reads the accumulated transparent history (hist-A / hist-B) as SOFT_HALF -> soft-B; the
    // wavelet ping-pong + INPUT_COLOR are still soft-A/soft-B. Each binds its paired transparent moments as the MOMENTS SRV.
    Core::BindingSetHandle temporalHistA = buildSet(histATarget, scratchBTarget, scratchATarget, momentsATarget);
    Core::BindingSetHandle temporalHistB = buildSet(histBTarget, scratchBTarget, scratchATarget, momentsBTarget);
    if(!outputHalfA || !outputHalfB || !upsample || !temporalHistA || !temporalHistB){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft transparent shadow resolve binding sets"));
        rayTracingState().m_transparentResolveBindingSetOutputHalfA = nullptr;
        rayTracingState().m_transparentResolveBindingSetOutputHalfB = nullptr;
        rayTracingState().m_transparentResolveBindingSetUpsample = nullptr;
        rayTracingState().m_transparentResolveBindingSetTemporalHistA = nullptr;
        rayTracingState().m_transparentResolveBindingSetTemporalHistB = nullptr;
        rayTracingState().m_transparentResolveBindingSetSoftHalf = nullptr;
        rayTracingState().m_transparentResolveBindingSetScratchA = nullptr;
        rayTracingState().m_transparentResolveBindingSetScratchB = nullptr;
        rayTracingState().m_transparentResolveBindingSetGeometry = nullptr;
        rayTracingState().m_transparentResolveBindingSetDepth = nullptr;
        rayTracingState().m_transparentResolveBindingSetVisibility = nullptr;
        rayTracingState().m_transparentResolveBindingSetHistA = nullptr;
        rayTracingState().m_transparentResolveBindingSetHistB = nullptr;
        rayTracingState().m_transparentResolveBindingSetMomentsA = nullptr;
        rayTracingState().m_transparentResolveBindingSetMomentsB = nullptr;
        rayTracingState().m_transparentResolveBindingSetWorldPos = nullptr;
        rayTracingState().m_transparentResolveBindingSetNormal = nullptr;
        return false;
    }
    rayTracingState().m_transparentResolveBindingSetOutputHalfA = Move(outputHalfA);
    rayTracingState().m_transparentResolveBindingSetOutputHalfB = Move(outputHalfB);
    rayTracingState().m_transparentResolveBindingSetUpsample = Move(upsample);
    rayTracingState().m_transparentResolveBindingSetTemporalHistA = Move(temporalHistA);
    rayTracingState().m_transparentResolveBindingSetTemporalHistB = Move(temporalHistB);
    rayTracingState().m_transparentResolveBindingSetSoftHalf = rawTraceTarget;
    rayTracingState().m_transparentResolveBindingSetScratchA = scratchATarget;
    rayTracingState().m_transparentResolveBindingSetScratchB = scratchBTarget;
    rayTracingState().m_transparentResolveBindingSetGeometry = geometryTarget;
    rayTracingState().m_transparentResolveBindingSetDepth = depthTarget;
    rayTracingState().m_transparentResolveBindingSetVisibility = visibilityTarget;
    rayTracingState().m_transparentResolveBindingSetHistA = histATarget;
    rayTracingState().m_transparentResolveBindingSetHistB = histBTarget;
    rayTracingState().m_transparentResolveBindingSetMomentsA = momentsATarget;
    rayTracingState().m_transparentResolveBindingSetMomentsB = momentsBTarget;
    rayTracingState().m_transparentResolveBindingSetWorldPos = worldPositionTarget;
    rayTracingState().m_transparentResolveBindingSetNormal = normalTarget;
    return true;
}

void RendererRayTracingSystem::dispatchSoftShadowResolve(Core::CommandList& commandList, DeferredFrameTargets& targets, u32 slot, const SoftShadowResolveDispatch& dispatch){
    // The a-trous denoise + upsample of ONE slot's half-res jittered visibility into the full-res visibility. Cloned from
    // dispatchCausticResolve: PREPARE (copy) -> N wavelet ping-pong passes -> bilateral upsample. Assumes the pipeline +
    // binding sets in `dispatch` are ready, the trace already wrote its raw half-res buffer (this frame) with a UAV barrier,
    // AND the slot-independent geometry downsample already filled the geometry cache (the caller runs it ONCE per frame).
    // ONE routine serves BOTH signals: the OPAQUE resolve (scalar pipeline, its own base sets, Overwrite fold) and the
    // TRANSPARENT resolve (RGB pipeline, its own base sets over transparentSoftHalf/transparentHist, Multiply fold). The
    // ping-pong SCRATCH (the outputHalfA/B sets' OUTPUT + INPUT) is the SAME half-res soft-A/soft-B for both, dispatched
    // strictly sequentially (opaque resolve fully done, then transparent), so they never race on the scratch.
    // TEMPORAL (Stage 3): when the merge ran, dispatch.prepareOverride is the temporal SOFT_HALF variant whose PREPARE reads
    // the ACCUMULATED history instead of the raw trace; it still writes soft-B, so the wavelet + upsample chain is identical.
    // prepareOverride == nullptr (temporal off / first frame) keeps the raw-trace PREPARE AND drives momentsValid=0.
    const u32 halfWidth = (targets.width + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
    const u32 halfHeight = (targets.height + NWB_SW_SHADOW_SOFT_FACTOR - 1u) / NWB_SW_SHADOW_SOFT_FACTOR;
    const u32 halfGroupsX = DivideUp(halfWidth, static_cast<u32>(NWB_SHADOW_RESOLVE_GROUP_SIZE));
    const u32 halfGroupsY = DivideUp(halfHeight, static_cast<u32>(NWB_SHADOW_RESOLVE_GROUP_SIZE));
    const u32 fullGroupsX = DivideUp(targets.width, static_cast<u32>(NWB_SHADOW_RESOLVE_GROUP_SIZE));
    const u32 fullGroupsY = DivideUp(targets.height, static_cast<u32>(NWB_SHADOW_RESOLVE_GROUP_SIZE));

    const u32 foldValue = static_cast<u32>(dispatch.fold);
    const auto runPass = [&](Core::BindingSet* const bindingSet, const u32 stepWidth, const ShadowResolveStage::Enum stage, const u32 groupsX, const u32 groupsY){
        commandList.setResourceStatesForBindingSet(bindingSet);
        commandList.commitBarriers();

        ShadowResolvePushConstants resolvePush;
        resolvePush.width = targets.width;
        resolvePush.height = targets.height;
        resolvePush.halfWidth = halfWidth;
        resolvePush.halfHeight = halfHeight;
        resolvePush.stepWidth = stepWidth;
        resolvePush.stage = static_cast<u32>(stage);
        resolvePush.lightSlotStart = slot;
        resolvePush.lightSlotCount = 1u; // one soft slot per dispatch (scattered slots handled by the C++ loop)
        // The moments SRV holds this-frame integrated temporal moments IFF the merge ran, which is exactly when the caller
        // passes a temporal prepareOverride. So prepareOverride != nullptr is the single source of the momentsValid flag: on
        // it the WAVELET's SVGF variance stop may use the temporal variance; off it never samples the (dummy) moments SRV.
        resolvePush.momentsValid = (dispatch.prepareOverride != nullptr) ? 1u : 0u;
        // OVERWRITE (opaque) vs MULTIPLY-onto-visibility (transparent fold). Ignored by PREPARE/WAVELET (only UPSAMPLE reads it).
        resolvePush.upsampleFold = foldValue;

        Core::ComputeState computeState;
        computeState.setPipeline(dispatch.pipeline);
        computeState.addBindingSet(bindingSet);
        commandList.setComputeState(computeState);
        commandList.setPushConstants(&resolvePush, sizeof(resolvePush));
        commandList.dispatch(groupsX, groupsY, 1u);
    };

    // PREPARE: copy the half-res traced visibility (SOFT_HALF) into soft-B. The base set (SOFT_HALF == raw trace, out=soft-B,
    // in=raw) never read-write aliases the scratch; the temporal override (SOFT_HALF == the merge's accumulated history buffer,
    // still out=soft-B) reads the accumulated visibility instead. Either way the result lives in soft-B for the wavelets.
    Core::BindingSet* const prepareSet = dispatch.prepareOverride ? dispatch.prepareOverride : dispatch.outputHalfB;
    runPass(prepareSet, 1u, ShadowResolveStage::Prepare, halfGroupsX, halfGroupsY);

    // Half-res a-trous wavelet passes at a doubling dilation, starting from soft-B. Each pass writes the buffer NOT
    // holding its input (outputHalfA reads soft-B writes soft-A; outputHalfB reads soft-A writes soft-B). srcIsHalfB
    // tracks where the latest result lives; PREPARE left it in soft-B so it starts true.
    bool srcIsHalfB = true;
    for(u32 pass = 0u; pass < static_cast<u32>(NWB_SHADOW_RESOLVE_PASS_COUNT); ++pass){
        Core::BindingSet* const bindingSet = srcIsHalfB ? dispatch.outputHalfA : dispatch.outputHalfB;
        runPass(bindingSet, 1u << pass, ShadowResolveStage::Wavelet, halfGroupsX, halfGroupsY);
        srcIsHalfB = !srcIsHalfB;
    }

    // UPSAMPLE (full-res): edge-aware bilateral resample of the FINAL half-res visibility into the full-res visibility
    // slot. The final wavelet result lives in soft-A when PASS_COUNT is ODD (our config, 5) -- srcIsHalfB is now false.
    // Both upsample sets read soft-A (INPUT_COLOR) by construction; assert the parity so a PASS_COUNT change is caught.
    NWB_ASSERT(!srcIsHalfB); // PASS_COUNT must be odd for the final result to land in soft-A (the upsample's input)
    runPass(dispatch.upsample, 1u, ShadowResolveStage::Upsample, fullGroupsX, fullGroupsY);
}

bool RendererRayTracingSystem::ensureShadowReprojectMergePipeline(){
    if(rayTracingState().m_shadowReprojectMergePipeline)
        return true;
    if(rayTracingState().m_shadowReprojectMergePipelineFailed)
        return false;

    auto* device = graphics().getDevice();

    if(!rayTracingState().m_shadowReprojectMergeBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_REPROJECT_MERGE_BINDING_SOFT_TRACE, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_REPROJECT_MERGE_BINDING_HISTORY_IN, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_REPROJECT_MERGE_BINDING_MOMENTS_IN, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_REPROJECT_MERGE_BINDING_GEOMETRY_CURR, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_REPROJECT_MERGE_BINDING_GEOMETRY_PREV, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_SHADOW_REPROJECT_MERGE_BINDING_GBUFFER_WORLDPOS, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_SHADOW_REPROJECT_MERGE_BINDING_HISTORY_OUT, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_SHADOW_REPROJECT_MERGE_BINDING_MOMENTS_OUT, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(ShadowReprojectMergePushConstants)));

        rayTracingState().m_shadowReprojectMergeBindingLayout = device->createBindingLayout(layoutDesc);
        if(!rayTracingState().m_shadowReprojectMergeBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow reproject-merge binding layout"));
            rayTracingState().m_shadowReprojectMergePipelineFailed = true;
            return false;
        }
    }

    if(!m_renderer.shaderSystem().loadShader(
        rayTracingState().m_shadowReprojectMergeShader,
        AssetsGraphicsShadow::s_SoftReprojectMergeShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_SoftShadowReprojectMerge"
    )){
        rayTracingState().m_shadowReprojectMergePipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(rayTracingState().m_shadowReprojectMergeShader)
        .addBindingLayout(rayTracingState().m_shadowReprojectMergeBindingLayout)
    ;
    rayTracingState().m_shadowReprojectMergePipeline = device->createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_shadowReprojectMergePipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow reproject-merge compute pipeline"));
        rayTracingState().m_shadowReprojectMergePipelineFailed = true;
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureShadowReprojectMergeBindingSet(DeferredFrameTargets& targets){
    NWB_ASSERT(rayTracingState().m_shadowReprojectMergeBindingLayout);
    NWB_ASSERT(targets.shadowSoftHalfA);
    NWB_ASSERT(targets.shadowSoftGeometry);
    NWB_ASSERT(targets.shadowSoftGeometryPrev);
    NWB_ASSERT(targets.worldPosition);
    NWB_ASSERT(targets.shadowHistA);
    NWB_ASSERT(targets.shadowHistB);
    NWB_ASSERT(targets.shadowMomentsA);
    NWB_ASSERT(targets.shadowMomentsB);

    Core::Texture* softTraceTarget = targets.shadowSoftHalfA.get();
    Core::Texture* geometryCurrTarget = targets.shadowSoftGeometry.get();
    Core::Texture* geometryPrevTarget = targets.shadowSoftGeometryPrev.get();
    Core::Texture* worldPositionTarget = targets.worldPosition.get();
    Core::Texture* histATarget = targets.shadowHistA.get();
    Core::Texture* histBTarget = targets.shadowHistB.get();
    Core::Texture* momentsATarget = targets.shadowMomentsA.get();
    Core::Texture* momentsBTarget = targets.shadowMomentsB.get();
    if(
        rayTracingState().m_shadowReprojectMergeBindingSetAtoB
        && rayTracingState().m_shadowReprojectMergeBindingSetBtoA
        && rayTracingState().m_shadowReprojectMergeSoftTrace == softTraceTarget
        && rayTracingState().m_shadowReprojectMergeGeometryCurr == geometryCurrTarget
        && rayTracingState().m_shadowReprojectMergeGeometryPrev == geometryPrevTarget
        && rayTracingState().m_shadowReprojectMergeWorldPosition == worldPositionTarget
        && rayTracingState().m_shadowReprojectMergeHistA == histATarget
        && rayTracingState().m_shadowReprojectMergeHistB == histBTarget
        && rayTracingState().m_shadowReprojectMergeMomentsA == momentsATarget
        && rayTracingState().m_shadowReprojectMergeMomentsB == momentsBTarget
    )
        return true;

    auto* device = graphics().getDevice();

    // Two front/back sets so the accumulated-history SRV (history-in) and the accumulated-history UAV (history-out) never
    // bind the SAME texture (the resource-state framework cannot resolve one texture to both SRV and UAV in one set):
    //  - AtoB: histIn/momIn = A -> histOut/momOut = B  (used when m_softShadowHistoryFrontIsA == 1, i.e. A holds this frame's
    //          incoming history; the merge writes B, which the temporal-histB resolve variant then denoises).
    //  - BtoA: histIn/momIn = B -> histOut/momOut = A  (the mirror; A becomes the accumulated buffer the resolve reads).
    // All other bindings are shared: SOFT_TRACE=soft-A, GEOMETRY_CURR/PREV, WORLDPOS=the full-res world-position G-buffer.
    const auto buildSet = [&](Core::Texture* histInTex, Core::Texture* momInTex, Core::Texture* histOutTex, Core::Texture* momOutTex) -> Core::BindingSetHandle {
        Core::BindingSetDesc desc(arena());
        desc.addItem(Core::BindingSetItem::Texture_SRV(
            NWB_SHADOW_REPROJECT_MERGE_BINDING_SOFT_TRACE,
            softTraceTarget,
            targets.shadowSoftFormat,
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::TextureDimension::Texture2DArray
        ));
        desc.addItem(Core::BindingSetItem::Texture_SRV(
            NWB_SHADOW_REPROJECT_MERGE_BINDING_HISTORY_IN,
            histInTex,
            targets.shadowSoftFormat,
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::TextureDimension::Texture2DArray
        ));
        desc.addItem(Core::BindingSetItem::Texture_SRV(
            NWB_SHADOW_REPROJECT_MERGE_BINDING_MOMENTS_IN,
            momInTex,
            targets.shadowSoftFormat,
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::TextureDimension::Texture2DArray
        ));
        desc.addItem(Core::BindingSetItem::Texture_SRV(
            NWB_SHADOW_REPROJECT_MERGE_BINDING_GEOMETRY_CURR,
            geometryCurrTarget,
            targets.shadowSoftGeometryFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ));
        desc.addItem(Core::BindingSetItem::Texture_SRV(
            NWB_SHADOW_REPROJECT_MERGE_BINDING_GEOMETRY_PREV,
            geometryPrevTarget,
            targets.shadowSoftGeometryFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ));
        desc.addItem(Core::BindingSetItem::Texture_SRV(
            NWB_SHADOW_REPROJECT_MERGE_BINDING_GBUFFER_WORLDPOS,
            worldPositionTarget,
            targets.worldPositionFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ));
        desc.addItem(Core::BindingSetItem::Texture_UAV(
            NWB_SHADOW_REPROJECT_MERGE_BINDING_HISTORY_OUT,
            histOutTex,
            targets.shadowSoftFormat,
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::TextureDimension::Texture2DArray
        ));
        desc.addItem(Core::BindingSetItem::Texture_UAV(
            NWB_SHADOW_REPROJECT_MERGE_BINDING_MOMENTS_OUT,
            momOutTex,
            targets.shadowSoftFormat,
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::TextureDimension::Texture2DArray
        ));
        return device->createBindingSet(desc, rayTracingState().m_shadowReprojectMergeBindingLayout);
    };

    Core::BindingSetHandle setAtoB = buildSet(histATarget, momentsATarget, histBTarget, momentsBTarget);
    Core::BindingSetHandle setBtoA = buildSet(histBTarget, momentsBTarget, histATarget, momentsATarget);
    if(!setAtoB || !setBtoA){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shadow reproject-merge binding sets"));
        rayTracingState().m_shadowReprojectMergeBindingSetAtoB = nullptr;
        rayTracingState().m_shadowReprojectMergeBindingSetBtoA = nullptr;
        rayTracingState().m_shadowReprojectMergeSoftTrace = nullptr;
        rayTracingState().m_shadowReprojectMergeGeometryCurr = nullptr;
        rayTracingState().m_shadowReprojectMergeGeometryPrev = nullptr;
        rayTracingState().m_shadowReprojectMergeWorldPosition = nullptr;
        rayTracingState().m_shadowReprojectMergeHistA = nullptr;
        rayTracingState().m_shadowReprojectMergeHistB = nullptr;
        rayTracingState().m_shadowReprojectMergeMomentsA = nullptr;
        rayTracingState().m_shadowReprojectMergeMomentsB = nullptr;
        return false;
    }
    rayTracingState().m_shadowReprojectMergeBindingSetAtoB = Move(setAtoB);
    rayTracingState().m_shadowReprojectMergeBindingSetBtoA = Move(setBtoA);
    rayTracingState().m_shadowReprojectMergeSoftTrace = softTraceTarget;
    rayTracingState().m_shadowReprojectMergeGeometryCurr = geometryCurrTarget;
    rayTracingState().m_shadowReprojectMergeGeometryPrev = geometryPrevTarget;
    rayTracingState().m_shadowReprojectMergeWorldPosition = worldPositionTarget;
    rayTracingState().m_shadowReprojectMergeHistA = histATarget;
    rayTracingState().m_shadowReprojectMergeHistB = histBTarget;
    rayTracingState().m_shadowReprojectMergeMomentsA = momentsATarget;
    rayTracingState().m_shadowReprojectMergeMomentsB = momentsBTarget;
    return true;
}

bool RendererRayTracingSystem::ensureShadowTransparentReprojectMergeBindingSet(DeferredFrameTargets& targets){
    // Stage 5: the two front/back TRANSPARENT reproject-merge binding sets (mirror of ensureShadowReprojectMergeBindingSet,
    // over the colored buffers). They drive the SAME m_shadowReprojectMergePipeline (the merge shader is fully RGB-safe and
    // reused verbatim); only the SOFT_TRACE / HISTORY / MOMENTS sources are the transparent buffers. The GEOMETRY_CURR/PREV
    // caches + the full-res world-position are SHARED with the opaque merge (same receivers), so this frame's transparent
    // history reprojects through the same stashed prevWorldToClip + gates against the same geometry as the opaque history.
    NWB_ASSERT(rayTracingState().m_shadowReprojectMergeBindingLayout);
    NWB_ASSERT(targets.transparentSoftHalf);
    NWB_ASSERT(targets.shadowSoftGeometry);
    NWB_ASSERT(targets.shadowSoftGeometryPrev);
    NWB_ASSERT(targets.worldPosition);
    NWB_ASSERT(targets.transparentHistA);
    NWB_ASSERT(targets.transparentHistB);
    NWB_ASSERT(targets.transparentMomentsA);
    NWB_ASSERT(targets.transparentMomentsB);

    Core::Texture* softTraceTarget = targets.transparentSoftHalf.get();
    Core::Texture* geometryCurrTarget = targets.shadowSoftGeometry.get();
    Core::Texture* geometryPrevTarget = targets.shadowSoftGeometryPrev.get();
    Core::Texture* worldPositionTarget = targets.worldPosition.get();
    Core::Texture* histATarget = targets.transparentHistA.get();
    Core::Texture* histBTarget = targets.transparentHistB.get();
    Core::Texture* momentsATarget = targets.transparentMomentsA.get();
    Core::Texture* momentsBTarget = targets.transparentMomentsB.get();
    if(
        rayTracingState().m_transparentReprojectMergeBindingSetAtoB
        && rayTracingState().m_transparentReprojectMergeBindingSetBtoA
        && rayTracingState().m_transparentReprojectMergeSoftTrace == softTraceTarget
        && rayTracingState().m_transparentReprojectMergeGeometryCurr == geometryCurrTarget
        && rayTracingState().m_transparentReprojectMergeGeometryPrev == geometryPrevTarget
        && rayTracingState().m_transparentReprojectMergeWorldPosition == worldPositionTarget
        && rayTracingState().m_transparentReprojectMergeHistA == histATarget
        && rayTracingState().m_transparentReprojectMergeHistB == histBTarget
        && rayTracingState().m_transparentReprojectMergeMomentsA == momentsATarget
        && rayTracingState().m_transparentReprojectMergeMomentsB == momentsBTarget
    )
        return true;

    auto* device = graphics().getDevice();

    const auto buildSet = [&](Core::Texture* histInTex, Core::Texture* momInTex, Core::Texture* histOutTex, Core::Texture* momOutTex) -> Core::BindingSetHandle {
        Core::BindingSetDesc desc(arena());
        desc.addItem(Core::BindingSetItem::Texture_SRV(
            NWB_SHADOW_REPROJECT_MERGE_BINDING_SOFT_TRACE,
            softTraceTarget,
            targets.shadowSoftFormat,
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::TextureDimension::Texture2DArray
        ));
        desc.addItem(Core::BindingSetItem::Texture_SRV(
            NWB_SHADOW_REPROJECT_MERGE_BINDING_HISTORY_IN,
            histInTex,
            targets.shadowSoftFormat,
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::TextureDimension::Texture2DArray
        ));
        desc.addItem(Core::BindingSetItem::Texture_SRV(
            NWB_SHADOW_REPROJECT_MERGE_BINDING_MOMENTS_IN,
            momInTex,
            targets.shadowSoftFormat,
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::TextureDimension::Texture2DArray
        ));
        desc.addItem(Core::BindingSetItem::Texture_SRV(
            NWB_SHADOW_REPROJECT_MERGE_BINDING_GEOMETRY_CURR,
            geometryCurrTarget,
            targets.shadowSoftGeometryFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ));
        desc.addItem(Core::BindingSetItem::Texture_SRV(
            NWB_SHADOW_REPROJECT_MERGE_BINDING_GEOMETRY_PREV,
            geometryPrevTarget,
            targets.shadowSoftGeometryFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ));
        desc.addItem(Core::BindingSetItem::Texture_SRV(
            NWB_SHADOW_REPROJECT_MERGE_BINDING_GBUFFER_WORLDPOS,
            worldPositionTarget,
            targets.worldPositionFormat,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::TextureDimension::Texture2D
        ));
        desc.addItem(Core::BindingSetItem::Texture_UAV(
            NWB_SHADOW_REPROJECT_MERGE_BINDING_HISTORY_OUT,
            histOutTex,
            targets.shadowSoftFormat,
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::TextureDimension::Texture2DArray
        ));
        desc.addItem(Core::BindingSetItem::Texture_UAV(
            NWB_SHADOW_REPROJECT_MERGE_BINDING_MOMENTS_OUT,
            momOutTex,
            targets.shadowSoftFormat,
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::TextureDimension::Texture2DArray
        ));
        return device->createBindingSet(desc, rayTracingState().m_shadowReprojectMergeBindingLayout);
    };

    Core::BindingSetHandle setAtoB = buildSet(histATarget, momentsATarget, histBTarget, momentsBTarget);
    Core::BindingSetHandle setBtoA = buildSet(histBTarget, momentsBTarget, histATarget, momentsATarget);
    if(!setAtoB || !setBtoA){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create soft transparent shadow reproject-merge binding sets"));
        rayTracingState().m_transparentReprojectMergeBindingSetAtoB = nullptr;
        rayTracingState().m_transparentReprojectMergeBindingSetBtoA = nullptr;
        rayTracingState().m_transparentReprojectMergeSoftTrace = nullptr;
        rayTracingState().m_transparentReprojectMergeGeometryCurr = nullptr;
        rayTracingState().m_transparentReprojectMergeGeometryPrev = nullptr;
        rayTracingState().m_transparentReprojectMergeWorldPosition = nullptr;
        rayTracingState().m_transparentReprojectMergeHistA = nullptr;
        rayTracingState().m_transparentReprojectMergeHistB = nullptr;
        rayTracingState().m_transparentReprojectMergeMomentsA = nullptr;
        rayTracingState().m_transparentReprojectMergeMomentsB = nullptr;
        return false;
    }
    rayTracingState().m_transparentReprojectMergeBindingSetAtoB = Move(setAtoB);
    rayTracingState().m_transparentReprojectMergeBindingSetBtoA = Move(setBtoA);
    rayTracingState().m_transparentReprojectMergeSoftTrace = softTraceTarget;
    rayTracingState().m_transparentReprojectMergeGeometryCurr = geometryCurrTarget;
    rayTracingState().m_transparentReprojectMergeGeometryPrev = geometryPrevTarget;
    rayTracingState().m_transparentReprojectMergeWorldPosition = worldPositionTarget;
    rayTracingState().m_transparentReprojectMergeHistA = histATarget;
    rayTracingState().m_transparentReprojectMergeHistB = histBTarget;
    rayTracingState().m_transparentReprojectMergeMomentsA = momentsATarget;
    rayTracingState().m_transparentReprojectMergeMomentsB = momentsBTarget;
    return true;
}

void RendererRayTracingSystem::swapSoftShadowTemporalHistory(DeferredFrameTargets& targets){
    // Frame-end stash + ping-pong for the Stage-3 temporal accumulator. Runs only when the merge was live this frame
    // (m_softShadowTemporalReady), so the cadence never stalls the non-temporal / HW paths.
    //  - STASH: this frame's resolved worldToClip (cached in drawState().m_meshViewGpuData by updateMeshViewBuffer earlier
    //    this frame -- the first field of MeshViewGpuData) is copied into m_prevWorldToClip for NEXT frame's reprojection.
    //  - HISTORY / MOMENTS PING-PONG (SELECTOR FLIP, NOT a handle swap): the two merge binding sets (AtoB in=A/out=B, BtoA
    //    in=B/out=A) already encode both ping-pong directions against the FIXED physical A/B textures. Flipping the selector
    //    alone alternates which set runs: frame N (frontIsA=1) uses AtoB -> accumulates into B; frame N+1 (frontIsA=0) uses
    //    BtoA -> reads B (last frame's out) + accumulates into A; and so on. Swapping the HANDLES too would double-count and
    //    make the merge read the WRONG buffer, so the hist/moments handles are deliberately NOT swapped -- only the selector.
    //  - GEOMETRY PING-PONG (a real HANDLE SWAP): unlike history, the geometry cache has no per-set selector -- the
    //    downsample ALWAYS writes shadowSoftGeometry and the merge ALWAYS reads shadowSoftGeometryPrev, so this frame's curr
    //    must physically become next frame's prev. The handle swap changes which texture each role points at, so the
    //    geometry-downsample + merge binding sets rebuild next frame via their tracked-pointer compare (as a resize does).
    //  - SEED / VALID: the first merge has now run, so history is valid from next frame on.
    if(!rayTracingState().m_softShadowTemporalReady)
        return;

    if(drawState().m_meshViewGpuDataValid){
        // MeshViewGpuData::worldToClip is the leading 16 floats (row-major) of the cached byte blob; copy them raw into the
        // 64-byte push matrix (Float44U raw dump). reinterpret_cast is safe: the ray-tracing system is a RendererDrawState
        // friend and the byte buffer is exactly MeshViewGpuData-shaped (static_assert'd in mesh_view_private.h).
        const auto* meshView = reinterpret_cast<const ECSRenderDetail::MeshViewGpuData*>(drawState().m_meshViewGpuData);
        NWB_MEMCPY(&rayTracingState().m_prevWorldToClip, sizeof(rayTracingState().m_prevWorldToClip), &meshView->worldToClip, sizeof(rayTracingState().m_prevWorldToClip));
        rayTracingState().m_prevWorldToClipValid = true;
    }
    rayTracingState().m_softShadowTemporalSeeded = true;

    Swap(targets.shadowSoftGeometry, targets.shadowSoftGeometryPrev);
    rayTracingState().m_softShadowHistoryFrontIsA ^= 1u;
}

f32 RendererRayTracingSystem::causticTemporalDecay(){
    // Splat-space temporal EMA decay factor: 0.85 = a moderate ~6-7 frame time constant that de-sparkles a spinning
    // refractor while still following its motion (a fixed value on the renderer state, clamped to [0,1) at its default so
    // the EMA can never diverge). Was an env A/B knob; removed under the no-engine-env policy -- tuning belongs in tests.
    return rayTracingState().m_causticTemporalDecay;
}

bool RendererRayTracingSystem::ensureCausticAccumulatorDecayPipeline(){
    if(rayTracingState().m_causticAccumulatorDecayPipeline)
        return true;
    if(rayTracingState().m_causticAccumulatorDecayPipelineFailed)
        return false;

    auto* device = graphics().getDevice();

    if(!rayTracingState().m_causticAccumulatorDecayBindingLayout){
        Core::BindingLayoutDesc layoutDesc(arena());
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(NWB_CAUSTIC_ACCUMULATOR_DECAY_BINDING_ACCUMULATOR, 1));
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(CausticAccumulatorDecayPushConstants)));

        rayTracingState().m_causticAccumulatorDecayBindingLayout = device->createBindingLayout(layoutDesc);
        if(!rayTracingState().m_causticAccumulatorDecayBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic accumulator decay binding layout"));
            rayTracingState().m_causticAccumulatorDecayPipelineFailed = true;
            return false;
        }
    }

    if(!m_renderer.shaderSystem().loadShader(
        rayTracingState().m_causticAccumulatorDecayShader,
        AssetsGraphicsCaustic::s_AccumulatorDecayShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSRender_CausticAccumulatorDecay"
    )){
        rayTracingState().m_causticAccumulatorDecayPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(rayTracingState().m_causticAccumulatorDecayShader)
        .addBindingLayout(rayTracingState().m_causticAccumulatorDecayBindingLayout)
    ;
    rayTracingState().m_causticAccumulatorDecayPipeline = device->createComputePipeline(pipelineDesc);
    if(!rayTracingState().m_causticAccumulatorDecayPipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic accumulator decay compute pipeline"));
        rayTracingState().m_causticAccumulatorDecayPipelineFailed = true;
        return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureCausticAccumulatorDecayBindingSet(DeferredFrameTargets& targets){
    NWB_ASSERT(rayTracingState().m_causticAccumulatorDecayBindingLayout);
    NWB_ASSERT(targets.causticAccumulator);

    Core::Texture* accumulatorTarget = targets.causticAccumulator.get();
    if(
        rayTracingState().m_causticAccumulatorDecayBindingSet
        && rayTracingState().m_causticAccumulatorDecayAccumulator == accumulatorTarget
    )
        return true;

    auto* device = graphics().getDevice();

    Core::BindingSetDesc desc(arena());
    desc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_CAUSTIC_ACCUMULATOR_DECAY_BINDING_ACCUMULATOR,
        accumulatorTarget,
        targets.causticAccumulatorFormat,
        ECSRenderDetail::s_CausticAccumulatorSubresources,
        Core::TextureDimension::Texture2DArray
    ));

    Core::BindingSetHandle bindingSet = device->createBindingSet(desc, rayTracingState().m_causticAccumulatorDecayBindingLayout);
    if(!bindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create caustic accumulator decay binding set"));
        rayTracingState().m_causticAccumulatorDecayBindingSet = nullptr;
        rayTracingState().m_causticAccumulatorDecayAccumulator = nullptr;
        return false;
    }
    rayTracingState().m_causticAccumulatorDecayBindingSet = Move(bindingSet);
    rayTracingState().m_causticAccumulatorDecayAccumulator = accumulatorTarget;
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
    const bool resolveReady =
        ensureCausticGeometryDownsamplePipeline()
        && ensureCausticGeometryDownsampleBindingSet(targets)
        && ensureCausticResolvePipeline()
        && ensureCausticResolveBindingSet(targets)
    ;
    // The splat-space temporal EMA decay pre-pass is only dispatched when temporal is enabled (decay > 0); gate its
    // pipeline + binding set on that so the disabled path never builds them.
    const bool temporalReady =
        causticTemporalDecay() <= 0.f
        || (ensureCausticAccumulatorDecayPipeline() && ensureCausticAccumulatorDecayBindingSet(targets))
    ;
    return producerReady && resolveReady && temporalReady;
}

bool RendererRayTracingSystem::renderHwCaustics(Core::CommandList& commandList, DeferredFrameTargets& targets){
    // Hardware ray-traced caustic photon producer + resolve (P4) -- the byte-parallel sibling of renderGpuBvhCaustics,
    // dispatched in the HW branch right after the shadow pass + clearCausticTargets and BEFORE deferred lighting. The
    // raygen runs the SHARED iterative bounce loop (recursion 1) over the TLAS and splats into the SAME R32_UINT
    // accumulator the SAME resolve consumes, so the HW + SW paths converge to the same caustic (Monte-Carlo A/B).
    if(!hasHwCausticWork())
        return false;
    const f32 temporalDecay = causticTemporalDecay();
    if(
        !rayTracingState().m_hwCausticPipeline
        || !rayTracingState().m_hwCausticShaderTable
        || !rayTracingState().m_hwCausticBindingSet
        || !rayTracingState().m_causticResolvePipeline
        || !rayTracingState().m_causticResolveBindingSetOutputHalfA
        || !rayTracingState().m_causticResolveBindingSetOutputHalfB
        || !rayTracingState().m_causticResolveBindingSetUpsample
        || !rayTracingState().m_causticGeometryDownsamplePipeline
        || !rayTracingState().m_causticGeometryDownsampleBindingSet
        || (temporalDecay > 0.f && (!rayTracingState().m_causticAccumulatorDecayPipeline || !rayTracingState().m_causticAccumulatorDecayBindingSet))
    )
        return false;
    if(!targets.causticAccumulator || !targets.causticIrradiance || !targets.causticHistory || !targets.causticResolveHalf || !targets.causticResolveGeometry)
        return false;

    {
        Core::GpuTimingMeasure timing(graphics().gpuTiming(), RendererGpuTimingScope::s_CausticPhotons, graphics().getDevice(), commandList);

        // Splat-space temporal EMA step (enabled paths only): decay the resident accumulator (or clear it on the first
        // frame / after a resize) before this frame's splat. Byte-identical to the SW producer's temporal step.
        if(temporalDecay > 0.f)
            prepareCausticAccumulatorForSplat(commandList, targets, temporalDecay);

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
    // The caustic emission-target list is CPU-written each frame and read by the caustic producer, so it is a
    // structured SRV (no UAV) that grows by doubling like the scene-BVH / instance-material buffers.
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

    Vector<Float4, Core::Alloc::ScratchArena> instanceAabbMin{ scratchArena };
    Vector<Float4, Core::Alloc::ScratchArena> instanceAabbMax{ scratchArena };
    Vector<Float4, Core::Alloc::ScratchArena> instanceCentroid{ scratchArena };
    instanceAabbMin.reserve(instanceCount);
    instanceAabbMax.reserve(instanceCount);
    instanceCentroid.reserve(instanceCount);

    SIMDVector boundsMin = VectorReplicate(1e30f);
    SIMDVector boundsMax = VectorReplicate(-1e30f);
    for(u32 i = 0u; i < instanceCount; ++i){
        const f32 base = static_cast<f32>(i) * 3.0f;
        const SIMDVector boxMin = VectorSet(base, base * 0.25f, -base * 0.5f, 0.0f);
        const SIMDVector boxMax = VectorAdd(boxMin, VectorSet(1.5f, 2.0f, 1.0f, 0.0f));

        Float4 storedBoxMin;
        Float4 storedBoxMax;
        Float4 storedCentroid;
        StoreFloat(boxMin, &storedBoxMin);
        StoreFloat(boxMax, &storedBoxMax);
        StoreFloat(VectorScale(VectorAdd(boxMin, boxMax), 0.5f), &storedCentroid);

        instanceAabbMin.push_back(storedBoxMin);
        instanceAabbMax.push_back(storedBoxMax);
        instanceCentroid.push_back(storedCentroid);

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
            !Vector3LessOrEqual(VectorAbs(VectorSubtract(LoadFloat(nodes[0].aabbMin), boundsMin)), epsilonVector)
            || !Vector3LessOrEqual(VectorAbs(VectorSubtract(LoadFloat(nodes[0].aabbMax), boundsMax)), epsilonVector)
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
        const SIMDVector childMin = VectorMin(LoadFloat(nodes[leftChild].aabbMin), LoadFloat(nodes[rightChild].aabbMin));
        const SIMDVector childMax = VectorMax(LoadFloat(nodes[leftChild].aabbMax), LoadFloat(nodes[rightChild].aabbMax));
        if(
            !Vector3LessOrEqual(LoadFloat(nodes[i].aabbMin), VectorAdd(childMin, epsilonVector))
            || !Vector3GreaterOrEqual(LoadFloat(nodes[i].aabbMax), VectorSubtract(childMax, epsilonVector))
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

