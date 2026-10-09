// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_SHADOW_SW_BINDING_SLOTS_H
#define NWB_GRAPHICS_SHADOW_SW_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "constants.h"
#include "../scene/binding_slots.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Compacted edge records feed the indirect transparent retrace.
#define NWB_SW_SHADOW_INDIRECT_ARGS_GROUP_COUNT_X 0u
#define NWB_SW_SHADOW_INDIRECT_ARGS_GROUP_COUNT_Y 1u
#define NWB_SW_SHADOW_INDIRECT_ARGS_GROUP_COUNT_Z 2u
#define NWB_SW_SHADOW_INDIRECT_ARGS_WORD_COUNT 3u

#define NWB_SW_SHADOW_GROUP_SIZE 8

// Shared C++/shader coarse transparent-trace scale.
#define NWB_SW_SHADOW_COARSE_SHIFT 2u
#define NWB_SW_SHADOW_COARSE_FACTOR (1u << NWB_SW_SHADOW_COARSE_SHIFT)

#define NWB_SW_SHADOW_TRACE_GROUP 64

#define NWB_SW_SHADOW_EDGE_COUNTER_APPEND 0
#define NWB_SW_SHADOW_EDGE_COUNTER_TRACE 1
#define NWB_SW_SHADOW_EDGE_COUNTER_SIZE 2

// Packed pixel and light-loop index.
#define NWB_SW_SHADOW_EDGE_RECORD_WORDS 2

// Occluder class for traversal passes.
#define NWB_SW_SHADOW_OCCLUDER_OPAQUE 0
#define NWB_SW_SHADOW_OCCLUDER_TRANSPARENT 1

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

