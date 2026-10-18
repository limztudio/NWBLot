// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_BVH_CONSTANTS_H
#define NWB_GRAPHICS_BVH_CONSTANTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Child-link encoding; high bit tags transparent subtrees.
#define NWB_BVH_LEAF_FLAG 0x80000000u
#define NWB_BVH_TRANSPARENT_SUBTREE_FLAG 0x80000000u
#define NWB_BVH_CHILD_INDEX_MASK 0x7fffffffu
#define NWB_BVH_INVALID 0xffffffffu

// Per-instance ray-tracing material flag bits shared by CPU and every trace backend.
#define NWB_RT_INSTANCE_MATERIAL_FLAG_TRANSPARENT 0x1u
#define NWB_RT_INSTANCE_MATERIAL_FLAG_REFRACTIVE 0x2u
#define NWB_RT_INSTANCE_MATERIAL_FLAG_CSG_SHADOW 0x4u

#define NWB_RT_SHADOW_BASE_INSTANCE_MASK 0x4u
#define NWB_RT_SHADOW_TRANSPARENT_INSTANCE_MASK 0x8u

// Shared triangle byte-address layout.
#define NWB_RAYTRACE_TRIANGLE_CORNER_COUNT 3u
#define NWB_RAYTRACE_INDEX_STRIDE_BYTES 4u
#define NWB_RAYTRACE_POSITION_STRIDE_BYTES 12u
#define NWB_RAYTRACE_VERTEX_ATTRIBUTE_STRIDE_BYTES 16u
#define NWB_RAYTRACE_VERTEX_ATTRIBUTE_UV0_BYTE_OFFSET 8u

// Guard against degenerate bounds in Morton normalization.
#define NWB_BVH_MORTON_BOUNDS_EXTENT_MIN 1e-8


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

