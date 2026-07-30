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

// LBVH build passes (Morton -> sort -> Karras topology -> bottom-up fit). All scratch/work buffers use global
// StorageBuffer-heap entries selected by NwbBvhBuildPushConstants; the only pipeline-local binding is the push
// constant range. Positions and triangle indices use their raw global-heap entries through the same block.
#define NWB_BVH_BUILD_GROUP_SIZE 256


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

