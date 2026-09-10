// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_BVH_CONSTANTS_H
#define NWB_GRAPHICS_BVH_CONSTANTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Binary BVH child-link encoding shared by CPU and shader paths. Decode right-child
// index via bvh_common.slangi (high bit is the transparent-subtree tag).
#define NWB_BVH_LEAF_FLAG 0x80000000u
#define NWB_BVH_TRANSPARENT_SUBTREE_FLAG 0x80000000u
#define NWB_BVH_CHILD_INDEX_MASK 0x7fffffffu
#define NWB_BVH_INVALID 0xffffffffu

// Raw triangle byte-address layout shared by CPU upload and ray-trace consumers.
#define NWB_RAYTRACE_TRIANGLE_CORNER_COUNT 3u
#define NWB_RAYTRACE_INDEX_STRIDE_BYTES 4u
#define NWB_RAYTRACE_POSITION_STRIDE_BYTES 12u
#define NWB_RAYTRACE_VERTEX_ATTRIBUTE_STRIDE_BYTES 16u
#define NWB_RAYTRACE_VERTEX_ATTRIBUTE_NORMAL_BYTE_OFFSET 0u
#define NWB_RAYTRACE_VERTEX_ATTRIBUTE_UV0_BYTE_OFFSET 8u

// Prevent degenerate mesh bounds from producing an undefined Morton normalization divide.
#define NWB_BVH_MORTON_BOUNDS_EXTENT_MIN 1e-8


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

