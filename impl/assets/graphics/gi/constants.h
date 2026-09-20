// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_GI_CONSTANTS_H
#define NWB_GRAPHICS_GI_CONSTANTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Hit shading keeps the same dominant-light occlusion policy on both traversal backends.
#ifndef NWB_GI_HIT_SHADOW_RAYS
#define NWB_GI_HIT_SHADOW_RAYS 1
#endif

// Software scene and mesh traversal capacities; unchanged by the heap-binding migration.
#define NWB_GI_SW_MESH_STACK_SIZE 32
#define NWB_GI_SW_SCENE_STACK_SIZE 64

// Sampling and trace-distance policy shared by the GI shader paths. Macro-only so SW/HW trace variants and the surfel producer share one sequence and ray span.
#define NWB_GI_FIBONACCI_GOLDEN_ANGLE 2.39996323f
#define NWB_GI_FRAME_ROTATION_SEQUENCE_FRACTION 0.61803398875f
#define NWB_GI_FULL_TURN_RADIANS 6.28318530718f
#define NWB_GI_TRACE_RAY_MIN_DISTANCE 0.001f
#define NWB_GI_TRACE_RAY_MAX_DISTANCE 10000.0f
#define NWB_GI_INVALID_LIGHT_INDEX 0xffffffffu


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

