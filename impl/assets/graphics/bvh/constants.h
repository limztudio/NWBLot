// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_BVH_CONSTANTS_H
#define NWB_GRAPHICS_BVH_CONSTANTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Binary BVH child-link encoding shared by the CPU build/clear path and the shader traversal/build kernels.
// The CPU-built scene BVH additionally uses the otherwise-unreachable high bit of `rightChild` as a transparent-
// subtree tag. Per-mesh GPU-built BVHs leave it clear, so consumers must decode a right-child index through the
// helper in bvh_common.slangi before dereferencing it.
#define NWB_BVH_LEAF_FLAG 0x80000000u
#define NWB_BVH_TRANSPARENT_SUBTREE_FLAG 0x80000000u
#define NWB_BVH_CHILD_INDEX_MASK 0x7fffffffu
#define NWB_BVH_INVALID 0xffffffffu

// Raw triangle geometry is shared by the CPU mesh upload and every hardware/software ray-trace consumer. Keep the
// byte-address layout here so BVH build, shadow, GI, and caustic traces cannot silently decode different records.
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

