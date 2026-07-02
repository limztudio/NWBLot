// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_SHADOW_SW_BINDING_SLOTS_H
#define NWB_GRAPHICS_SHADOW_SW_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Software (compute) shadow traversal pass — the no-hardware-ray-tracing fallback. One thread per pixel
// reads the G-buffer, casts one occlusion ray per light through the software scene/instance BVH (and, in
// the per-mesh stage, each instance's triangle BVH), and writes per-light colored transmittance into the
// shadow-visibility Texture2DArray the deferred lighting pass samples. Slots 0-6 are the scene-level pass;
// slots 8-11 add the per-mesh triangle traversal (parallel per-mesh node/position/index/attribute arrays);
// slots 13-14 bring in the material-constants context (typed words + mesh instances) the per-hit
// transmittance dispatch reads.
#define NWB_SW_SHADOW_SET 0

#define NWB_SW_SHADOW_BINDING_GBUFFER_WORLD_POSITION 0
#define NWB_SW_SHADOW_BINDING_GBUFFER_NORMAL 1
#define NWB_SW_SHADOW_BINDING_GBUFFER_DEPTH 2
#define NWB_SW_SHADOW_BINDING_SCENE_SHADING 3
#define NWB_SW_SHADOW_BINDING_LIGHT_LIST 4
#define NWB_SW_SHADOW_BINDING_SCENE_NODES 5
#define NWB_SW_SHADOW_BINDING_VISIBILITY_OUTPUT 6
#define NWB_SW_SHADOW_BINDING_SCENE_INSTANCES 7
// Parallel per-mesh descriptor arrays (slot k = mesh k), kept contiguous: triangle BVH nodes + raw position /
// index byte buffers + the per-triangle-corner shadow-trace attribute buffer (normal/uv0 the dispatch interpolates).
#define NWB_SW_SHADOW_BINDING_MESH_NODES 8
#define NWB_SW_SHADOW_BINDING_MESH_POSITIONS 9
#define NWB_SW_SHADOW_BINDING_MESH_INDICES 10
#define NWB_SW_SHADOW_BINDING_MESH_ATTRIBUTES 11
// Per-instance occluder material table (NwbRtInstanceMaterial), indexed by the scene-BVH leaf instance index;
// built lockstep with the scene-instance buffer so the array slot matches the traversal's instanceIndex.
#define NWB_SW_SHADOW_BINDING_INSTANCE_MATERIAL 12
// The shared material-constants context the per-hit transmittance dispatch reads (same buffers the rasterizer
// binds at NWB_MESH_BINDING_MATERIAL_TYPED / NWB_MESH_BINDING_INSTANCE, pointed here for this pass): the typed
// material-constant words + the per-instance mutable-storage records.
#define NWB_SW_SHADOW_BINDING_MATERIAL_TYPED 13
#define NWB_SW_SHADOW_BINDING_MESH_INSTANCES 14
// Stage-2 adaptive transparent shadow (coarse-trace + edge-refine):
//  - COARSE: a HALF-res RGBA16F Texture2DArray (one layer per shadow slot) the coarse transparent trace (mode 4)
//    writes one transmittance per 2x2 block into and the adaptive resolve (mode 5) reads back to interpolate the
//    flat interior / detect edges. UAV (written by mode 4, UAV-loaded by mode 5).
//  - EDGE_STATS: a 2-uint UAV counter ([0] = full-res traced rays, [1] = total candidate rays) the resolve
//    atomically tallies when stats collection is on, read back to the log as the edge fraction that decides
//    whether the Stage-3 compaction/indirect path is worth building.
#define NWB_SW_SHADOW_BINDING_COARSE 15
#define NWB_SW_SHADOW_BINDING_EDGE_STATS 16
// Stage-3 compacted-indirect adaptive transparent shadow (the soft-shadow analog of an irradiance cache, but the
// re-traced edge pixels are STREAM-COMPACTED into a list and dispatched indirectly so only edge rays launch, as
// coherent waves with no in-wave divergence). Three UAVs:
//  - EDGE_COUNTER: a 2-uint counter ([0] = atomic append count, [1] = clamped trace count written by the build-args pass).
//  - EDGE_LIST: the compacted edge-pixel list, 2 u32 per record (word0 = (x<<16)|y, word1 = light-loop index).
//  - INDIRECT_ARGS: a 3-uint DispatchIndirectArguments{groupsX,1,1} buffer (created UAV + isDrawIndirectArgs) the
//    build-args pass writes and the indirect trace pass consumes via ComputeState::indirectParams.
#define NWB_SW_SHADOW_BINDING_EDGE_COUNTER 17
#define NWB_SW_SHADOW_BINDING_EDGE_LIST 18
#define NWB_SW_SHADOW_BINDING_INDIRECT_ARGS 19
// Soft directional shadow (Stage 1 of the soft-ray-traced-shadow feature): a HALF-res RGBA16F Texture2DArray (one
// layer per shadow slot) the jittered opaque directional trace (mode 11) writes ONE cone-jittered visibility sample
// per half-res pixel into. It is then denoised by the SEPARATE shadow_resolve pipeline (its own binding set) --
// geometry downsample + a-trous wavelet + bilateral upsample into the full-res visibility the lighting samples. UAV
// (written by mode 11, read by the resolve). Only the SW traversal declares/writes it; the resolve binds its own copy.
#define NWB_SW_SHADOW_BINDING_SOFT_HALF 20

// 8x8 = 64 threads per group (one thread per pixel).
#define NWB_SW_SHADOW_GROUP_SIZE 8

// Coarse-trace downscale for the adaptive passes (modes 4/5/6/9/10): the coarse buffer + trace run at full-res >> SHIFT,
// i.e. one coarse trace per (FACTOR x FACTOR) full-res block. SHIFT=1 -> half-res (1 trace / 2x2 block); SHIFT=2 ->
// quarter-res (1 trace / 4x4 block, ~4x fewer coarse traces but coarser interior + more edge-refine). Shared by the
// shader (coarse-trace block stride + resolve coordinate) and C++ (coarse-buffer dims + dispatch grid) so they agree.
#define NWB_SW_SHADOW_COARSE_SHIFT 2u
#define NWB_SW_SHADOW_COARSE_FACTOR (1u << NWB_SW_SHADOW_COARSE_SHIFT)

// Soft directional shadow downscale (mode 11 + the shadow_resolve denoise): the jittered directional trace + its
// a-trous denoise run at full-res >> SOFT_SHIFT. SHIFT=1 -> HALF resolution (1 jittered trace per 2x2 block, the
// Stage-1 target). Independent of COARSE_SHIFT above (that is the adaptive TRANSPARENT trace's quarter-res); the soft
// directional trace is a separate signal with its own half-res buffers, so its factor is kept distinct and explicit.
#define NWB_SW_SHADOW_SOFT_SHIFT 1u
#define NWB_SW_SHADOW_SOFT_FACTOR (1u << NWB_SW_SHADOW_SOFT_SHIFT)

// Threads per group for the 1D indirect trace pass (mode 8). Equals GROUP_SIZE^2 so it reuses the [numthreads(8,8,1)]
// entry point: the build-args pass computes groupsX = ceil(traceCount / 64) and each thread derives its flat record
// index as groupID.x * 64 + SV_GroupIndex.
#define NWB_SW_SHADOW_TRACE_GROUP 64

// Edge-stats counter layout (NWB_SW_SHADOW_BINDING_EDGE_STATS): [0] traced rays, [1] total candidate rays.
#define NWB_SW_SHADOW_EDGE_STATS_TRACED 0
#define NWB_SW_SHADOW_EDGE_STATS_TOTAL 1
#define NWB_SW_SHADOW_EDGE_STATS_COUNT 2

// Compaction counter layout (NWB_SW_SHADOW_BINDING_EDGE_COUNTER): [0] atomic append count, [1] clamped trace count.
#define NWB_SW_SHADOW_EDGE_COUNTER_APPEND 0
#define NWB_SW_SHADOW_EDGE_COUNTER_TRACE 1
#define NWB_SW_SHADOW_EDGE_COUNTER_SIZE 2

// Compacted edge record: 2 u32 words. word0 packs the full-res pixel (x<<16)|y (16 bits each); word1 holds the
// light-LOOP index (0..NWB_SCENE_MAX_LIGHTS-1) so the trace pass does one g_NwbSceneLights load + recovers slot=params.z.
#define NWB_SW_SHADOW_EDGE_RECORD_WORDS 2

// Maximum distinct meshes the per-mesh descriptor arrays can address in one frame.
#define NWB_SW_SHADOW_MAX_MESHES 64

// Occluder class the per-mesh traversal filters to. Each pass kernel that traces #defines NWB_SW_SHADOW_OCCLUDER to
// one of these BEFORE including sw_shadow_traverse.slangi; the filter in nwbSwShadowInstanceOccluded then skips the
// other class. This replaces the old numeric multiplyMode occluder switch -- the class is now a COMPILE-TIME identity
// baked into each pass, not a runtime push value:
//  - OPAQUE      -> skip TRANSPARENT occluders: a binary blocker mask. The opaque prepass / adaptive-opaque coarse +
//                   resolve / soft directional half-res trace (the SW analog of the HW RayQuery opaque mask).
//  - TRANSPARENT -> skip OPAQUE occluders: the colored Beer-Lambert tint MULTIPLIED onto an existing opaque mask
//                   (the transparent coarse / resolve / indirect re-trace / uniform half-res multiply). The opaque
//                   shadow already came from the HW mask (hybrid) or the opaque prepass (software), so tracing opaque
//                   here would only redundantly re-darken -- and skipping it avoids walking the opaque meshes' BVHs.
//  - ALL         -> trace BOTH classes (no pass uses this today; reserved so a future single-pass fallback composes).
#define NWB_SW_SHADOW_OCCLUDER_OPAQUE 0
#define NWB_SW_SHADOW_OCCLUDER_TRANSPARENT 1
#define NWB_SW_SHADOW_OCCLUDER_ALL 2

// G-buffer background/validity depth: a depth at or above this is the cleared background (no geometry) -- fully lit,
// casts no candidate ray. Centralised here (was a bare 0.999999 literal repeated across every trace/resolve pass) so
// the gbuffer concern's nwbSwShadowIsBackground and every pass share one definition.
#define NWB_SW_SHADOW_BACKGROUND_DEPTH 0.999999

// Anti-pop dilation (in COARSE cells) for the opaque adaptive resolve's edge TEST. A hard opaque shadow is NOT band-
// limited, so the tight (bracketing-2x2) edge test leaves a block in agreement until a sweeping silhouette edge crosses
// a coarse SAMPLE -- the interpolated block then lags ~1 coarse cell and snaps (the visible pop), and sub-cell slivers
// between samples are missed. Widening the edge TEST by this many coarse cells re-traces the block full-res BEFORE the
// edge arrives, so it is already exact as the edge sweeps through. 1 cell removes the dominant sweep pop at ~3x the
// (still small) edge-retrace count; the interpolation itself stays on the tight bracketing 2x2. (Contract-shared so the
// opaque-resolve pass + any C++ that references the dilation agree on one value.)
#define NWB_SW_SHADOW_OPAQUE_EDGE_DILATE 1

// Per-frame samples-per-pixel for the soft directional trace. Stage 3 adds TEMPORAL accumulation (the reproject-merge pass)
// that supplies the sun-disk samples OVER FRAMES, so this drops from Stage 1's per-frame wash-out count (8) toward 1 --
// pinned at 2 (not 1) on purpose: for a STATIC receiver the temporal history + the wide a-trous converge fine from a single
// sample, but in a freshly-disoccluded / gate-B-clamped region (a spinning occluder's leading edge) the effective history
// length collapses to ~0 and the pixel falls back to (near) pure current -- where 1 spp is a single binary ray whose
// dithered penumbra the a-trous cannot fully smooth in ONE frame, whereas 2 spp halves that moving-region noise for the
// same still-cheap half-res binary trace cost. So 2 keeps moving regions clean while temporal handles static convergence.
// (An A/B at 1 vs 2 is a one-line edit here.) Contract-shared with the soft directional pass.
#define NWB_SW_SHADOW_SOFT_SPP 2u

// Per-thread traversal stack depths. The scene/instance BVH is shallow (a few-to-hundreds of instances);
// the per-mesh triangle BVH is deeper. Both traversals treat a deeper subtree as occluded rather than
// skipping it, so these are generous-but-not-proven bounds, not correctness-critical.
#define NWB_SW_SHADOW_SCENE_STACK_SIZE 32
#define NWB_SW_SHADOW_MESH_STACK_SIZE 64


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

