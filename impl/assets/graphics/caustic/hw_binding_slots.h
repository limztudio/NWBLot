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
// into the R32_UINT accumulators the resolve consumes. The set mirrors the shadow-RT geometry/material plumbing
// (so the per-hit surface dispatch resolves ior/transmission IDENTICALLY). The refraction bends on the interpolated
// SHADING normal (from the per-vertex normals in the attribute buffer), so -- unlike a geometric-normal bend -- it
// needs NO object-space position array; the world hit point comes from WorldRayOrigin()+RayTCurrent()*direction.
#define NWB_CAUSTIC_RT_SET 0

// Singletons.
#define NWB_CAUSTIC_RT_BINDING_TLAS 0
#define NWB_CAUSTIC_RT_BINDING_SCENE_SHADING 1
#define NWB_CAUSTIC_RT_BINDING_LIGHT_LIST 2
// Per-instance occluder material table (NwbRtInstanceMaterial, indexed by InstanceID(); built lockstep with the
// TLAS instances by buildSceneTlas) -- the REFRACTIVE flag gates the physics, the rest feeds the surface dispatch.
#define NWB_CAUSTIC_RT_BINDING_INSTANCE_MATERIAL 3
// The shared material-constants context the per-hit surface dispatch reads (same buffers the rasterizer/shadow set
// bind at NWB_MESH_BINDING_MATERIAL_TYPED / NWB_MESH_BINDING_INSTANCE, pointed here for this pass).
#define NWB_CAUSTIC_RT_BINDING_MATERIAL_TYPED 4
#define NWB_CAUSTIC_RT_BINDING_MESH_INSTANCES 5
// Caustic-specific I/O.
#define NWB_CAUSTIC_RT_BINDING_EMISSION_TARGETS 6
#define NWB_CAUSTIC_RT_BINDING_VIEW 7
#define NWB_CAUSTIC_RT_BINDING_GBUFFER_DEPTH 8
#define NWB_CAUSTIC_RT_BINDING_GBUFFER_WORLD_POSITION 9
#define NWB_CAUSTIC_RT_BINDING_ACCUMULATOR 10

// Parallel per-mesh descriptor arrays (slot k of the array = mesh k = material.meshSlot): the raw triangle index
// byte buffer + the per-triangle-corner attribute byte buffer (normal/uv0 the closest-hit interpolates into the
// SHADING normal the refraction bends on + the per-hit surface dispatch). Each is ONE binding holding
// NWB_CAUSTIC_RT_MAX_MESHES array elements (NOT consecutive slots).
#define NWB_CAUSTIC_RT_BINDING_MESH_INDICES 11
#define NWB_CAUSTIC_RT_BINDING_MESH_ATTRIBUTES 12


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Reuse the shadow per-mesh cap so the C++ slot arrays + the shader's [N] descriptor arrays stay one definition. The
// photon grid side is no longer a compile-time constant here -- the HW raygen reads it from the gridSide push constant
// (config-scaled in C++, byte-identical to the SW producer), so the grid decomposes the photon index the same way.
#include "../shadow/binding_slots.h"
#include "sw_binding_slots.h"

#define NWB_CAUSTIC_RT_MAX_MESHES NWB_SHADOW_RT_MAX_MESHES


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

