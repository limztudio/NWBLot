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
// Its pass-local interface is only the shared photon push block. All scene, material-context, input, and accumulator
// resources are selected from the global descriptor heap by slots in that block.


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The HW raygen reads the photon grid side from the same push-constant layout as the SW producer.
#include "sw_binding_slots.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

