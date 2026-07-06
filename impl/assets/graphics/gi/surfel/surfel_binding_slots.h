// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_GI_SURFEL_BINDING_SLOTS_H
#define NWB_GRAPHICS_GI_SURFEL_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Surfel GI bindings. Three compute passes + the deferred-lighting gather share the surfel pool / hash / counter /
// constants; the TRACE pass additionally reuses the SW-BVH bindings (scene/instance/per-mesh, slots 0-10) the SW
// shadow/caustic path already builds, exactly like the retired probe trace did -- so the trace body
// (gi_sw_trace.slangi: nwbGiSwTraceClosest + nwbGiSwShadeHit + per-instance baseColor tint) is reused verbatim.
//
//   HASH_BUILD: constants + pool(UAV) + cellHead(UAV)                                  [runs BEFORE spawn]
//   SPAWN     : constants + pool(UAV) + cellHead(UAV, this frame) + counter(UAV) + G-buffer worldPos/normal(SRV)
//   TRACE     : SW-BVH slots 0-10 + constants + pool(UAV)
//   GATHER    : (in the resolve set) constants + pool(SRV) + cellHead(SRV)
//
// ONE SURFEL PER HASH BUCKET. The hash is (re)built from the live pool FIRST; the spawn then only fills buckets whose
// cell-head is still empty, claiming each with an atomic CompareExchange(INVALID -> PENDING) so exactly one screen tile
// wins a given empty bucket per frame (bootstrap packs many tiles into one near-camera cell). This keeps every cell
// list length 1, so the gather's fixed-order walk is deterministic -- the flicker cause was overstuffed cells (hundreds
// of tile-surfels per cell) walked in the hash-build's non-deterministic InterlockedExchange order and truncated at
// NWB_SURFEL_MAX_WALK, so the walked subset's surface mix churned frame to frame. See .helper/surfel_gi_plan.md.

#define NWB_SURFEL_SET 0

// SW-BVH slots 0-10 the TRACE reuses -- provided by the shared trace include (gi_sw_trace.slangi -> sw_binding_slots.h).
// The surfel-specific bindings live at the tail so they never collide with the BVH slots the trace body declares.
#define NWB_SURFEL_BINDING_CONSTANTS 11   // ConstantBuffer<NwbSurfelConstants>
#define NWB_SURFEL_BINDING_POOL 12        // RWStructuredBuffer<NwbSurfel> (UAV) / StructuredBuffer (SRV at the gather)
#define NWB_SURFEL_BINDING_CELL_HEAD 13   // RWStructuredBuffer<uint> (UAV build) / StructuredBuffer (SRV spawn+gather)
#define NWB_SURFEL_BINDING_COUNTER 14     // RWStructuredBuffer<uint> (bump top + free top)

// Spawn-only G-buffer inputs (the same deferred targets the lighting pass reads: world position + normal + depth).
#define NWB_SURFEL_BINDING_GBUFFER_WORLD_POSITION 15
#define NWB_SURFEL_BINDING_GBUFFER_NORMAL 16
#define NWB_SURFEL_BINDING_GBUFFER_DEPTH 17

// Free-list (U1 recycling): a persistent LIFO stack of recycled surfel ids. The age-free pass PUSHES an id (dead surfel)
// via InterlockedAdd on counter[FREE_TOP]; the spawn POPS one (before bump-allocating) via a CAS loop. Bound UAV in the
// age-free + spawn sets. See .helper/surfel_gi_plan.md (U1).
#define NWB_SURFEL_BINDING_FREE_LIST 18   // RWStructuredBuffer<uint> (UAV) -- capacity poolCapacity, depth = counter[FREE_TOP]

// Snapshot of the PREVIOUS frame's converged field (U4 infinite bounce), bound as SRVs in the TRACE set: the trace's
// per-ray bounce gather reads these (never the live pool being written this frame), so surfel->surfel feedback reads a
// stable prev-frame field. Copied at the TOP of renderSurfelGi (pool + cell-head, so the walk is mutually consistent).
#define NWB_SURFEL_BINDING_SNAPSHOT_POOL 19       // StructuredBuffer<NwbSurfel> (SRV) -- prev-frame pool copy
#define NWB_SURFEL_BINDING_SNAPSHOT_CELL_HEAD 20  // StructuredBuffer<uint> (SRV) -- prev-frame cell-head copy

// RESOLVE pass: a COMPUTE pass that gathers the surfel irradiance ONCE PER PIXEL into a screen-space texture, which
// the deferred-lighting PIXEL shader then samples. This decouples the pixel consumer from the read-write surfel pool:
// the pool is touched only by compute (like the caustic accumulator), avoiding frames-in-flight pixel-read-vs-next-frame
// compute-write races. Output alpha = 1 where a surfel covered the pixel (rgb = gathered irradiance), 0 otherwise (the
// lighting falls back to hemiAmbient). Its own binding set.
#define NWB_SURFEL_RESOLVE_SET 0
#define NWB_SURFEL_RESOLVE_BINDING_CONSTANTS 0              // ConstantBuffer<NwbSurfelConstants>
#define NWB_SURFEL_RESOLVE_BINDING_POOL 1                  // StructuredBuffer<NwbSurfel> (SRV)
#define NWB_SURFEL_RESOLVE_BINDING_CELL_HEAD 2             // StructuredBuffer<uint> (SRV)
#define NWB_SURFEL_RESOLVE_BINDING_GBUFFER_WORLD_POSITION 3
#define NWB_SURFEL_RESOLVE_BINDING_GBUFFER_NORMAL 4
#define NWB_SURFEL_RESOLVE_BINDING_OUTPUT 5                // RWTexture2D<float4> (screen-space irradiance)
#define NWB_SURFEL_RESOLVE_GROUP_SIZE 8

// g_SurfelCounter layout: index 0 = bump-allocation top (fresh slots ever handed out; CAS-capped at poolCapacity so
// it is a true high-water mark, never overshoots), index 1 = free-list top (live LIFO depth of recycled ids, U1).
// Invariant: alive_count + FREE_TOP == BUMP_TOP <= poolCapacity, so live = BUMP_TOP - FREE_TOP is exact. Kept as named
// indices so the age-free / spawn passes agree. NWB_SURFEL_COUNTER_SIZE is the buffer length.
#define NWB_SURFEL_COUNTER_BUMP_TOP 0u
#define NWB_SURFEL_COUNTER_FREE_TOP 1u
#define NWB_SURFEL_COUNTER_SIZE 2u

// Byte size of one NwbSurfel record (surfel_record.slangi: 6 x float4 = 96B -- U3 grew it with the 3 SH colour lanes).
// The C++ pool buffer sizes its stride off this so the RWStructuredBuffer<NwbSurfel> stride matches the std430 record.
#define NWB_SURFEL_RECORD_SIZE 96u

// Empty-list / end-of-list sentinel for the spatial-hash cell heads + the per-surfel nextInCell link (the C++ side
// clears the cell-head buffer to this; the shaders compare against it). Shared here so both agree on the value.
#define NWB_SURFEL_CELL_INVALID 0xFFFFFFFFu

// Transient claim sentinel the spawn writes into a cell head via CompareExchange(INVALID -> PENDING) to reserve an
// empty bucket before it bump-allocates the surfel slot, so only one tile per empty bucket allocates (no wasted slots).
// The winner overwrites it with the real slot (or restores INVALID on pool exhaustion), so it never survives the pass.
#define NWB_SURFEL_CELL_PENDING 0xFFFFFFFEu

// Per-cell linked-list walk cap: a hard bound on the surfels examined from one bucket so a hash collision can never turn
// the traversal into a GPU hang. With one surfel per bucket the lists are length 1; this is pure collision headroom.
#define NWB_SURFEL_MAX_WALK 16u

// Defaults (Default tier). Pool + hash are power-of-two so the hash mask works; the spawn tile bounds spawns/frame.
#define NWB_SURFEL_POOL_CAPACITY 16384u        // 16384 * 96B = 1.5 MB pool (U3 record; the U4 snapshot mirrors it)
#define NWB_SURFEL_HASH_CELL_COUNT 262144u     // 2^18 * 4B = 1 MB cell-head table
// One surfel per hash cell: the CELL_SIZE sets the surfel spacing (GI resolution); the RADIUS is a bit larger so the
// gather's 3x3x3 distance-weighted blend of neighbouring cells' surfels overlaps smoothly without coverage gaps.
#define NWB_SURFEL_CELL_SIZE 0.6f              // world units -- hash cell edge = surfel spacing (depth-scaled in U6)
#define NWB_SURFEL_DEFAULT_RADIUS 0.9f         // world units -- gather falloff radius (~1.5x cell for neighbour overlap)
#define NWB_SURFEL_SPAWN_TILE 16u              // one spawn candidate per 16x16 screen tile
#define NWB_SURFEL_RAYS_PER_SURFEL 64u         // one workgroup (64 threads) per surfel
#define NWB_SURFEL_UPDATE_DIVISOR 4u           // steady-state: trace 1/Nth of surfels per frame (all on the bootstrap frame)
// Bounded running-mean window for the trace's temporal accumulation. The trace blends each new 64-ray estimate as a
// true incremental average (weight 1/(n+1)) until n reaches this cap, then holds a bounded EMA (weight 1/(cap+1)). A
// TRUE average is required (not a fixed-alpha EMA): each frame rotates the Fibonacci ray set, so successive estimates
// are decorrelated Monte-Carlo samples -- a fixed-alpha EMA over them never converges (holds a permanent ~sqrt(alpha)
// noise residual, visible as flicker on the brightest bleed), whereas the running mean drives variance -> 0. The cap
// keeps a bounded memory so a future dynamic-lighting unit still propagates (lower it for faster response).
#define NWB_SURFEL_MAX_ACCUM 64u
#define NWB_SURFEL_MAX_AGE 60u                 // recycle a surfel unseen for this many frames (U1)
#define NWB_SURFEL_GROUP_SIZE 8                 // spawn/hash-build tile threads (8x8 = 64)
// Per-channel ceiling on the U4 surfel->surfel bounce (mean-radiance scale). MANDATORY, not just a safety net: the SH
// reconstruction clamps each channel >= 0, which rectifies the negative lobes of the 2-band directional field and injects
// a little energy every bounce -- in a fully-enclosed high-albedo corner that can push the effective per-bounce gain
// toward 1.0 and creep the equilibrium bright. Capping the gathered bounce bounds that injection. ~2x the scene's
// brightest analytic light (gi_test directional intensity 2.0) -- generous enough for the real multi-bounce (which
// converges to <= ~5x the first bounce at albedo 0.80), tight enough to stop unbounded creep. Validated by the
// energy-stability test (the plateau must not creep toward the clamp).
#define NWB_SURFEL_BOUNCE_CLAMP 4.0f

// Default per-instance base colour the CPU writes into the shadow instance-material record (AssignInstanceBaseColor)
// when an entity has no authored tint, so a bounce is never black. Mirrors the shader's NWB_GI_SW_TRACE_DEFAULT_ALBEDO.
#define NWB_SURFEL_DEFAULT_ALBEDO_FLOAT3 0.5f, 0.5f, 0.5f


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

