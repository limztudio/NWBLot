// limztudio@gmail.com


#ifndef NWB_GRAPHICS_CAUSTIC_SW_BINDING_SLOTS_H
#define NWB_GRAPHICS_CAUSTIC_SW_BINDING_SLOTS_H


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


#endif


