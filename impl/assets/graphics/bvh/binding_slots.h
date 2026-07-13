// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_BVH_BINDING_SLOTS_H
#define NWB_GRAPHICS_BVH_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Bitonic sort over (Morton key, primitive-index payload) pairs — orders the primitives of one mesh before
// the Karras LBVH topology build. The host issues one dispatch per (sequenceSize, compareDistance) step; the
// kernel covers the power-of-two-padded element count, and padding slots hold sentinel keys that sort to the
// end. Keys and payload are parallel RWStructuredBuffer<uint> arrays.
#define NWB_BVH_SORT_GROUP_SIZE 256

#define NWB_BVH_SORT_SET 0

#define NWB_BVH_SORT_BINDING_KEYS 0
#define NWB_BVH_SORT_BINDING_PAYLOAD 1


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// LBVH build passes (Morton -> sort -> Karras topology -> bottom-up fit). One shared binding layout/set;
// each kernel binds the whole set and references only the subset it uses. Positions and triangle indices
// are raw ByteAddressBuffers; keys / payload / nodes / parent / visit-counter are RWStructuredBuffers.
#define NWB_BVH_BUILD_GROUP_SIZE 256

#define NWB_BVH_BUILD_SET 0

#define NWB_BVH_BUILD_BINDING_POSITIONS 0
#define NWB_BVH_BUILD_BINDING_TRIANGLE_INDICES 1
#define NWB_BVH_BUILD_BINDING_KEYS 2
#define NWB_BVH_BUILD_BINDING_PAYLOAD 3
#define NWB_BVH_BUILD_BINDING_NODES 4
#define NWB_BVH_BUILD_BINDING_PARENT 5
#define NWB_BVH_BUILD_BINDING_VISIT_COUNTER 6


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

