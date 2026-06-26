// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_CAUSTIC_SW_BINDING_SLOTS_H
#define NWB_GRAPHICS_CAUSTIC_SW_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Software (compute) caustic photon producer -- the no-hardware-ray-tracing fallback (P3). A 1D dispatch over
// photonCount: each thread picks a caustic light + an emission point on the refractive-instance emission domain,
// traces the photon through the same software scene/instance + per-mesh BVHs the SW shadow trace uses (adapted to
// CLOSEST-hit), and splats the surviving flux at the opaque-receiver hit into the R32_UINT accumulators via
// InterlockedAdd. The set mirrors the SW shadow set's geometry slots (so the closest-hit hook reads identical
// buffers + the per-hit surface dispatch resolves ior/transmission the SAME way) and adds the caustic-specific
// inputs/outputs: the emission-target AABB buffer, the camera view buffer (worldToClip for the splat projection),
// the G-buffer depth SRV (the depth-reject that kills screen-space leak), and the accumulator UAV.
#define NWB_CAUSTIC_SW_SET 0

#define NWB_CAUSTIC_SW_BINDING_SCENE_SHADING 0
#define NWB_CAUSTIC_SW_BINDING_LIGHT_LIST 1
#define NWB_CAUSTIC_SW_BINDING_SCENE_NODES 2
#define NWB_CAUSTIC_SW_BINDING_SCENE_INSTANCES 3
#define NWB_CAUSTIC_SW_BINDING_INSTANCE_MATERIAL 4
// Parallel per-mesh descriptor arrays (slot k = mesh k), kept contiguous: triangle BVH nodes + raw position /
// index byte buffers + the per-vertex attribute buffer (normal/uv0 the surface dispatch interpolates).
#define NWB_CAUSTIC_SW_BINDING_MESH_NODES 5
#define NWB_CAUSTIC_SW_BINDING_MESH_POSITIONS 6
#define NWB_CAUSTIC_SW_BINDING_MESH_INDICES 7
#define NWB_CAUSTIC_SW_BINDING_MESH_ATTRIBUTES 8
// The shared material-constants context the per-hit surface dispatch reads (same buffers the SW shadow trace
// binds at NWB_MESH_BINDING_MATERIAL_TYPED / NWB_MESH_BINDING_INSTANCE, pointed here for this pass).
#define NWB_CAUSTIC_SW_BINDING_MATERIAL_TYPED 9
#define NWB_CAUSTIC_SW_BINDING_MESH_INSTANCES 10
// Caustic-specific inputs/output:
//  - EMISSION_TARGETS: the per-frame refractive-instance world AABBs (P1) the photons aim at.
//  - VIEW: the camera view buffer (worldToClip) the splat projects the receiver hit through.
//  - GBUFFER_DEPTH: the G-buffer depth the splat uses to reject splats onto sky (background skip).
//  - ACCUMULATOR: the R32_UINT fixed-point splat target (Texture2DArray, one layer per RGB channel).
//  - GBUFFER_WORLD_POSITION: the G-buffer world position the splat compares the photon's receiver hit against
//    (the screen-space-leak reject -- a WORLD-distance test, robust at grazing angles where the device-depth
//    gradient across one pixel can exceed any fixed device-depth tolerance and reject every valid splat).
#define NWB_CAUSTIC_SW_BINDING_EMISSION_TARGETS 11
#define NWB_CAUSTIC_SW_BINDING_VIEW 12
#define NWB_CAUSTIC_SW_BINDING_GBUFFER_DEPTH 13
#define NWB_CAUSTIC_SW_BINDING_ACCUMULATOR 14
#define NWB_CAUSTIC_SW_BINDING_GBUFFER_WORLD_POSITION 15

// One thread per photon in a 1D dispatch; 64 photons per group.
#define NWB_CAUSTIC_SW_GROUP_SIZE 64

// Must equal the shadow SW cap (the caustic kernel reuses the SAME per-mesh buffers + the SAME meshSlot indices the
// SW shadow scene BVH produced) so the descriptor-array sizes and the C++ buffer arrays stay one definition. Alias it
// directly to the shadow cap so the equality is enforced by the compiler, not just a comment.
#include "../shadow/sw_binding_slots.h"
#define NWB_CAUSTIC_SW_MAX_MESHES NWB_SW_SHADOW_MAX_MESHES

// Per-thread traversal stack depths. With the FRONT-TO-BACK ordered descent (caustic_photon_sw_cs.slang) only the
// deferred FAR child is pushed per internal node, so the live stack depth is <= the BVH tree depth. The scene/instance
// BVH is shallow -- depth ~log2(instanceCount), so 16 covers ~65k instances with headroom (down from 32 -> 64 fewer
// bytes/thread). The per-mesh triangle BVH stays 64: it must cover the deepest mesh (e.g. the 77706-tri skinned char,
// + LBVH imbalance headroom up to the 256K-tri cap); if a subtree ever over-runs, the far child is dropped (a missed
// hit, never a corruption).
#define NWB_CAUSTIC_SW_SCENE_STACK_SIZE 16
#define NWB_CAUSTIC_SW_MESH_STACK_SIZE 64

// DBG-SAFE REFERENCE photon budget. The ACTUAL per-frame count is config-scaled in C++ (s_CausticPhotonGridSide,
// raytracing_system.cpp) and rides the gridSide push constant -- the shaders read the grid side at runtime, so they
// are config-agnostic. Energy-conserving (flux = domainArea*targetCount/photonCount), so a higher count only DENSIFIES the
// splat (the fix for a sparse moving caustic). dbg stays here: each photon does heavy work (per-mesh BVH descent +
// Moeller-Trumbore + the per-hit surface dispatch, per bounce), so an UNOPTIMIZED debug build at the full 262144
// (512^2) overran the GPU watchdog (TDR); opt/fin run the full density. The C++ dbg branch references this value.
// Keep PHOTON_COUNT == GRID_SIDE^2.
#define NWB_CAUSTIC_SW_PHOTON_COUNT 16384u

// Photons are laid out on a square grid over the emission domain; the per-photon cell index decomposes into a 2D
// (column, row) coordinate via this side length. PHOTON_COUNT must be GRID_SIDE^2 (128^2 = 16384). This is the
// dbg-safe reference; the runtime grid side is the gridSide push constant (config-scaled in C++).
#define NWB_CAUSTIC_SW_GRID_SIDE 128u


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

