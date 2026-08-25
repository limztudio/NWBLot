// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_SHADOW_SW_BINDING_SLOTS_H
#define NWB_GRAPHICS_SHADOW_SW_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "constants.h"
#include "../scene/binding_slots.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Software shadows use push-selected global heap resources; local SRV slots are ABI gaps.
#define NWB_SW_SHADOW_SET 0

#define NWB_SW_SHADOW_BINDING_GBUFFER_WORLD_POSITION 0
#define NWB_SW_SHADOW_BINDING_GBUFFER_NORMAL 1
#define NWB_SW_SHADOW_BINDING_GBUFFER_DEPTH 2
#define NWB_SW_SHADOW_BINDING_SCENE_SHADING 3
#define NWB_SW_SHADOW_BINDING_LIGHT_LIST 4
#define NWB_SW_SHADOW_BINDING_VISIBILITY_OUTPUT 6
// Shared material context selects scene and mesh records from the heap.
#define NWB_SW_SHADOW_BINDING_MATERIAL_CONTEXT_SLOTS 8
// Slots 5, 7, and 9-14 are intentional ABI gaps; do not renumber.
#define NWB_SW_SHADOW_BINDING_COARSE 15
#define NWB_SW_SHADOW_BINDING_EDGE_STATS 16
// Compacted edge records feed the indirect transparent retrace.
#define NWB_SW_SHADOW_BINDING_EDGE_COUNTER 17
#define NWB_SW_SHADOW_BINDING_EDGE_LIST 18
#define NWB_SW_SHADOW_BINDING_INDIRECT_ARGS 19
#define NWB_SW_SHADOW_INDIRECT_ARGS_GROUP_COUNT_X 0u
#define NWB_SW_SHADOW_INDIRECT_ARGS_GROUP_COUNT_Y 1u
#define NWB_SW_SHADOW_INDIRECT_ARGS_GROUP_COUNT_Z 2u
#define NWB_SW_SHADOW_INDIRECT_ARGS_WORD_COUNT 3u
// Half-resolution opaque trace input for the soft resolve.
#define NWB_SW_SHADOW_BINDING_SOFT_HALF 20
// Parallel half-resolution transparent trace input for RGB resolve/fold.
#define NWB_SW_SHADOW_BINDING_TRANSPARENT_SOFT_HALF 21
// Target-generation frame selector cbuffer.
#define NWB_SW_SHADOW_BINDING_BINDLESS_RESOURCES 22

#define NWB_SW_SHADOW_GROUP_SIZE 8

// Shared C++/shader coarse transparent-trace scale.
#define NWB_SW_SHADOW_COARSE_SHIFT 2u
#define NWB_SW_SHADOW_COARSE_FACTOR (1u << NWB_SW_SHADOW_COARSE_SHIFT)

// Separate shared C++/shader soft-opaque scale.
#define NWB_SW_SHADOW_SOFT_SHIFT 1u
#define NWB_SW_SHADOW_SOFT_FACTOR (1u << NWB_SW_SHADOW_SOFT_SHIFT)

#define NWB_SW_SHADOW_TRACE_GROUP 64

#define NWB_SW_SHADOW_EDGE_STATS_TRACED 0
#define NWB_SW_SHADOW_EDGE_STATS_TOTAL 1
#define NWB_SW_SHADOW_EDGE_STATS_COUNT 2

#define NWB_SW_SHADOW_EDGE_COUNTER_APPEND 0
#define NWB_SW_SHADOW_EDGE_COUNTER_TRACE 1
#define NWB_SW_SHADOW_EDGE_COUNTER_SIZE 2

// Compacted edge record: packed pixel and light-loop index.
#define NWB_SW_SHADOW_EDGE_RECORD_WORDS 2

// Compile-time occluder class for traversal passes.
#define NWB_SW_SHADOW_OCCLUDER_OPAQUE 0
#define NWB_SW_SHADOW_OCCLUDER_TRANSPARENT 1
#define NWB_SW_SHADOW_OCCLUDER_ALL 2

#define NWB_SW_SHADOW_BACKGROUND_DEPTH NWB_SCENE_BACKGROUND_DEPTH

#define NWB_SW_SHADOW_SOFT_SPP 3u
#define NWB_SW_SHADOW_SOFT_TEMPORAL_SPP 3u

#define NWB_SW_SHADOW_TRANSPARENT_SPP 3u

#define NWB_SW_SHADOW_TRANSPARENT_JITTER_SALT 2654435761u

// Over-deep traversal conservatively blocks light.
#define NWB_SW_SHADOW_SCENE_STACK_SIZE 32
#define NWB_SW_SHADOW_MESH_STACK_SIZE 64


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

