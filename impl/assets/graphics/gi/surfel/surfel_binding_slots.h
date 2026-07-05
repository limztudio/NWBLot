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
//   SPAWN     : constants + pool(UAV) + cellHead(SRV, prev frame) + counter(UAV) + G-buffer worldPos/normal/depth(SRV)
//   HASH_BUILD: constants + pool(UAV) + cellHead(UAV) + counter(UAV)
//   TRACE     : SW-BVH slots 0-10 + constants + pool(UAV)
//   GATHER    : (in the deferred-lighting set) constants + pool(SRV) + cellHead(SRV)

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

// g_SurfelCounter layout: index 0 = bump-allocation top (next fresh slot), index 1 = free-list top (recycled slots,
// U1). Kept as named indices so the spawn / hash-build passes agree. NWB_SURFEL_COUNTER_SIZE is the buffer length.
#define NWB_SURFEL_COUNTER_BUMP_TOP 0u
#define NWB_SURFEL_COUNTER_FREE_TOP 1u
#define NWB_SURFEL_COUNTER_SIZE 2u

// Byte size of one NwbSurfel record (surfel_record.slangi: 4 x float4 = 64B). The C++ pool buffer sizes its stride off
// this so the RWStructuredBuffer<NwbSurfel> stride matches the shader's std430 record.
#define NWB_SURFEL_RECORD_SIZE 64u

// Empty-list / end-of-list sentinel for the spatial-hash cell heads + the per-surfel nextInCell link (the C++ side
// clears the cell-head buffer to this; the shaders compare against it). Shared here so both agree on the value.
#define NWB_SURFEL_CELL_INVALID 0xFFFFFFFFu

// Per-cell linked-list walk cap: a hard bound on the surfels examined from one bucket so a hash collision or a dense
// cell can never turn the traversal into a GPU hang. Generous for the < 16k-surfel scale.
#define NWB_SURFEL_MAX_WALK 64u

// Defaults (Default tier). Pool + hash are power-of-two so the hash mask works; the spawn tile bounds spawns/frame.
#define NWB_SURFEL_POOL_CAPACITY 16384u        // 16384 * 64B = 1 MB pool
#define NWB_SURFEL_HASH_CELL_COUNT 262144u     // 2^18 * 4B = 1 MB cell-head table
#define NWB_SURFEL_DEFAULT_RADIUS 0.35f        // world units (depth-scaled in U6)
#define NWB_SURFEL_SPAWN_TILE 16u              // one spawn candidate per 16x16 screen tile
#define NWB_SURFEL_RAYS_PER_SURFEL 64u         // one workgroup (64 threads) per surfel
#define NWB_SURFEL_UPDATE_DIVISOR 4u           // steady-state: trace 1/Nth of surfels per frame (all on the bootstrap frame)
#define NWB_SURFEL_MAX_AGE 60u                 // recycle a surfel unseen for this many frames (U1)
#define NWB_SURFEL_COVERAGE_THRESHOLD 0.75f    // spawn when summed neighbour coverage < this
#define NWB_SURFEL_GROUP_SIZE 8                 // spawn/hash-build tile threads (8x8 = 64)

// Default per-instance base colour the CPU writes into the shadow instance-material record (AssignInstanceBaseColor)
// when an entity has no authored tint, so a bounce is never black. Mirrors the shader's NWB_GI_SW_TRACE_DEFAULT_ALBEDO.
#define NWB_SURFEL_DEFAULT_ALBEDO_FLOAT3 0.5f, 0.5f, 0.5f


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
