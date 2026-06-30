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

// 8x8 = 64 threads per group (one thread per pixel).
#define NWB_SW_SHADOW_GROUP_SIZE 8

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

// Per-thread traversal stack depths. The scene/instance BVH is shallow (a few-to-hundreds of instances);
// the per-mesh triangle BVH is deeper. Both traversals treat a deeper subtree as occluded rather than
// skipping it, so these are generous-but-not-proven bounds, not correctness-critical.
#define NWB_SW_SHADOW_SCENE_STACK_SIZE 32
#define NWB_SW_SHADOW_MESH_STACK_SIZE 64


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

