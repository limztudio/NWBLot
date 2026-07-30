// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_CAUSTIC_SW_BINDING_SLOTS_H
#define NWB_GRAPHICS_CAUSTIC_SW_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Software (compute) caustic photon producer -- the no-hardware-ray-tracing fallback. A 1D dispatch over
// photonCount: each thread picks a caustic light + an emission point on the refractive-instance emission domain,
// traces the photon through the same software scene/instance + per-mesh BVHs the SW shadow trace uses (adapted to
// CLOSEST-hit), and splats the surviving flux at the opaque-receiver hit into the R32_UINT accumulators via
// InterlockedAdd. The push-only set uses the same heap-selected SW scene/material context as shadow (so the closest-hit
// hook reads identical buffers + the per-hit surface dispatch resolves ior/transmission the SAME way), plus heap-selected
// emission-target AABBs, camera view, G-buffer depth/world-position inputs, and accumulator storage.
// The pass-local interface contains only the shared photon push block. Its UniformBuffer selector slots identify the
// frame-resource and material-context payloads; the remaining push fields identify typed image/buffer heap entries.

// One thread per photon in a 1D dispatch; 64 photons per group.
#define NWB_CAUSTIC_SW_GROUP_SIZE 64

// The caustic kernel reuses the SW shadow scene's heap-backed per-mesh geometry. The material record selects each
// mesh's buffers through descriptor-heap slots.
#include "../shadow/sw_binding_slots.h"

// Overflow drops the far child rather than corrupting traversal state.
#define NWB_CAUSTIC_SW_SCENE_STACK_SIZE 16
#define NWB_CAUSTIC_SW_MESH_STACK_SIZE 64

// Debug reference budget; runtime gridSide selects the actual count.
// PHOTON_COUNT must equal GRID_SIDE^2.
#define NWB_CAUSTIC_SW_PHOTON_COUNT 16384u

#define NWB_CAUSTIC_SW_GRID_SIDE 128u


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

