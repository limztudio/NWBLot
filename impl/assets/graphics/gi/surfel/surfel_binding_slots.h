// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_GI_SURFEL_BINDING_SLOTS_H
#define NWB_GRAPHICS_GI_SURFEL_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// No surfel pass owns local bindings; one surfel per hash bucket.

#define NWB_SURFEL_RESOLVE_GROUP_SIZE 8
#define NWB_SURFEL_RESOLVE_HALF_FACTOR 2

#define NWB_SURFEL_UPSAMPLE_GROUP_SIZE 8
// Keeps GI out of creases.
#define NWB_SURFEL_UPSAMPLE_NORMAL_GATE 0.5f
// Shared by spawn, resolve, gather, trace, and upsample.
#define NWB_SURFEL_GBUFFER_NORMAL_LENGTH_SQUARED_MIN 0.01f
#define NWB_SURFEL_HASH_CELL_SIZE_MIN 1e-6f
#define NWB_SURFEL_RADIUS_MIN 1e-4f
#define NWB_SURFEL_GATHER_WEIGHT_EPSILON 1e-4f
#define NWB_SURFEL_GATHER_FULL_CONFIDENCE_SAMPLE_COUNT 8.0f
#define NWB_SURFEL_SEED_CELL_EXTENT 1
#define NWB_SURFEL_SEED_NEIGHBOR_MIN_SAMPLE_COUNT 2u
#define NWB_SURFEL_UPSAMPLE_WORLD_SPACING_MIN 1e-4f
#define NWB_SURFEL_UPSAMPLE_WORLD_SIGMA_SCALE 3.0f
#define NWB_SURFEL_UPSAMPLE_TAP_SIDE 2
#define NWB_SURFEL_UPSAMPLE_COVERAGE_MIN 0.5f

// Five float4 lanes.
#define NWB_SURFEL_CONSTANTS_FLOAT4_COUNT 5u

// One-thread indirect-args build ABI.
#define NWB_SURFEL_TRACE_BUILDARGS_GROUP_SIZE_X 1u
#define NWB_SURFEL_TRACE_BUILDARGS_GROUP_SIZE_Y 1u
#define NWB_SURFEL_TRACE_BUILDARGS_GROUP_SIZE_Z 1u
#define NWB_SURFEL_TRACE_BUILDARGS_DISPATCH_GROUP_COUNT_X 1u
#define NWB_SURFEL_TRACE_BUILDARGS_DISPATCH_GROUP_COUNT_Y 1u
#define NWB_SURFEL_TRACE_BUILDARGS_DISPATCH_GROUP_COUNT_Z 1u
#define NWB_SURFEL_TRACE_INDIRECT_ARGS_GROUP_COUNT_X 0u
#define NWB_SURFEL_TRACE_INDIRECT_ARGS_GROUP_COUNT_Y 1u
#define NWB_SURFEL_TRACE_INDIRECT_ARGS_GROUP_COUNT_Z 2u
#define NWB_SURFEL_TRACE_INDIRECT_ARGS_WORD_COUNT 3u

// Live count is BUMP_TOP - FREE_TOP.
#define NWB_SURFEL_COUNTER_BUMP_TOP 0u
#define NWB_SURFEL_COUNTER_FREE_TOP 1u
#define NWB_SURFEL_COUNTER_SIZE 2u

// Must match the shader ABI.
#define NWB_SURFEL_RECORD_SIZE 96u

#define NWB_SURFEL_CELL_INVALID 0xFFFFFFFFu

// Temporary spawn claim.
#define NWB_SURFEL_CELL_PENDING 0xFFFFFFFEu

// Collision walk safety bound.
#define NWB_SURFEL_MAX_WALK 16u

#define NWB_SURFEL_POOL_CAPACITY 16384u        // 16384 * 96B = 1.5 MB pool (the snapshot mirrors it)
#define NWB_SURFEL_HASH_CELL_COUNT 262144u     // 2^18 * 4B = 1 MB cell-head table
#define NWB_SURFEL_CELL_SIZE 0.6f              // world units -- hash cell edge = surfel spacing
#define NWB_SURFEL_DEFAULT_RADIUS 0.9f         // world units -- gather falloff radius (~1.5x cell for neighbour overlap)
// Avoids cell-boundary seams.
#define NWB_SURFEL_GATHER_CELL_EXTENT 2
#define NWB_SURFEL_SPAWN_TILE 16u              // one spawn candidate per 16x16 screen tile
#define NWB_SURFEL_LINEAR_GROUP_SIZE 64u
#define NWB_SURFEL_RAYS_PER_SURFEL 64u         // 64 threads per surfel.
// Converged surfels reuse history.
#define NWB_SURFEL_CONVERGED_SAMPLE_COUNT 8u
#define NWB_SURFEL_CONVERGED_RAYS_PER_SURFEL 32u
// Comparison path.
#define NWB_SURFEL_USE_WAVE_REDUCE 0u
#define NWB_SURFEL_UPDATE_DIVISOR 4u           // steady-state: trace 1/Nth per frame
// Bounded running mean.
#define NWB_SURFEL_MAX_ACCUM 64u
#define NWB_SURFEL_MAX_AGE 60u                 // recycle unseen surfels
#define NWB_SURFEL_GROUP_SIZE 8                 // spawn/hash-build tile threads (8x8 = 64)
// Per-channel bounce-energy ceiling.
#define NWB_SURFEL_BOUNCE_CLAMP 4.0f


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

