// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_CAUSTIC_HW_BINDING_SLOTS_H
#define NWB_GRAPHICS_CAUSTIC_HW_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Hardware ray-traced caustic photon producer (P4) -- the byte-parallel sibling of the software compute producer
// (caustic/caustic_photon_sw_cs.slang). A raygen dispatch of GRID_SIDE x GRID_SIDE threads (one per photon, the
// SAME 128x128 = 16384 grid the SW kernel sweeps) emits each photon in light space at the refractive-instance
// emission AABBs, runs the SHARED iterative bounce loop (caustic_trace.slangi nwbCausticTracePhoton -- recursion
// stays 1, a fresh TraceRay per segment via the backend hook), and splats the surviving flux at the opaque receiver
// into the R32_UINT accumulators the resolve consumes. Its heap-selected material context uses the same per-hit surface
// dispatch as shadow (so ior/transmission resolves identically). The refraction bends on the interpolated
// SHADING normal (from the per-vertex normals in the attribute buffer), so -- unlike a geometric-normal bend -- it
// needs NO object-space position array; the world hit point comes from WorldRayOrigin()+RayTCurrent()*direction.
#define NWB_CAUSTIC_RT_SET 0

// Historical local scene/light positions: binding 1 now carries the target-generation resource-slot cbuffer, while
// binding 2 remains intentionally unbound because the light list is fetched from the global descriptor heap. Do not
// renumber the subsequent ABI slots.
#define NWB_CAUSTIC_RT_BINDING_TLAS 0
#define NWB_CAUSTIC_RT_BINDING_SCENE_SHADING 1
#define NWB_CAUSTIC_RT_BINDING_LIGHT_LIST 2
#define NWB_CAUSTIC_RT_BINDING_BINDLESS_RESOURCES NWB_CAUSTIC_RT_BINDING_SCENE_SHADING
// Slots 3-5 are intentional ABI gaps: the caustic trace selects their former material context through the b11 global
// StorageBuffer heap indirection below. Do not repurpose or renumber these holes.
// Caustic-specific I/O. The two G-buffer positions remain reserved for ABI compatibility, but are now logical heap-
// SRV selections carried in the shared photon-producer push constants.
#define NWB_CAUSTIC_RT_BINDING_EMISSION_TARGETS 6
#define NWB_CAUSTIC_RT_BINDING_VIEW 7
#define NWB_CAUSTIC_RT_BINDING_GBUFFER_DEPTH 8 // logical heap-SRV position; selected through push constants
#define NWB_CAUSTIC_RT_BINDING_GBUFFER_WORLD_POSITION 9 // logical heap-SRV position; selected through push constants
#define NWB_CAUSTIC_RT_BINDING_ACCUMULATOR 10

// b11 was intentionally unused and now carries the shared heap-slot indirection. b12 remains an intentional gap.
#define NWB_CAUSTIC_RT_BINDING_MATERIAL_CONTEXT_SLOTS 11 // ConstantBuffer<NwbRayTraceMaterialContextSlots>
// The HW closest-hit gets the triangle from the fixed-function intersector and reads corner attributes from the global
// descriptor heap through the material record's attributeSlot.


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The HW raygen reads the photon grid side from the same push-constant layout as the SW producer.
#include "sw_binding_slots.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

