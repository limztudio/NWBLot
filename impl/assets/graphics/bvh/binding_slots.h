// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_BVH_BINDING_SLOTS_H
#define NWB_GRAPHICS_BVH_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Bitonic sort over (Morton key, primitive-index payload) pairs — orders the primitives of one mesh before
// the Karras LBVH topology build. The host issues one dispatch per (sequenceSize, compareDistance) step; the
// kernel covers the power-of-two-padded element count, and padding slots hold sentinel keys that sort to the
// end. Keys and payload are global StorageBuffer-heap entries selected by push constants.
#define NWB_BVH_SORT_GROUP_SIZE 256

// NwbBvhBitonicSortPushConstants.mode values. These are a CPU/shader ABI: host dispatch scheduling and the three
// inline shader bodies must agree exactly.
#define NWB_BVH_SORT_MODE_LOCAL_TILE 0u
#define NWB_BVH_SORT_MODE_GLOBAL 1u
#define NWB_BVH_SORT_MODE_GLOBAL_TAIL 2u
#define NWB_BVH_SORT_PUSH_CONSTANT_WORD_COUNT 8u

// LBVH build passes (Morton -> sort -> Karras topology -> bottom-up fit). All scratch/work buffers use global
// StorageBuffer-heap entries selected by NwbBvhBuildPushConstants; the only pipeline-local binding is the push
// constant range. Positions and triangle indices use their raw global-heap entries through the same block.
#define NWB_BVH_BUILD_GROUP_SIZE 256
#define NWB_BVH_BUILD_MODE_FULL 0u
#define NWB_BVH_BUILD_MODE_REFIT 1u


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

