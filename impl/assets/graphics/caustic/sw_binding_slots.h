// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_CAUSTIC_SW_BINDING_SLOTS_H
#define NWB_GRAPHICS_CAUSTIC_SW_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Software (compute) caustic photon producer -- the no-hardware-ray-tracing fallback (P3). A 1D dispatch over
// photonCount: each thread picks a caustic light + an emission point on the refractive-instance emission domain,
// traces the photon through the same software scene/instance + per-mesh BVHs the SW shadow trace uses (adapted to
// CLOSEST-hit), and splats the surviving flux at the opaque-receiver hit into the R32_UINT accumulators via
// InterlockedAdd. The set uses the same heap-selected SW scene/material context as shadow (so the closest-hit hook
// reads identical buffers + the per-hit surface dispatch resolves ior/transmission the SAME way) and adds caustic-specific
// inputs/outputs: heap-selected emission-target AABBs, camera view, and G-buffer depth/world-position inputs (the
// receiver identity reject), plus the local accumulator UAV.
#define NWB_CAUSTIC_SW_SET 0

// Keep the historical scene/light binding numbers stable as logical ABI positions. Binding 0 now carries the
// target-generation resource-slot cbuffer instead of the scene-shading CB; binding 1 remains an intentional gap
// because the light list is fetched from the global descriptor heap too.
#define NWB_CAUSTIC_SW_BINDING_SCENE_SHADING 0
#define NWB_CAUSTIC_SW_BINDING_LIGHT_LIST 1
#define NWB_CAUSTIC_SW_BINDING_BINDLESS_RESOURCES NWB_CAUSTIC_SW_BINDING_SCENE_SHADING
// Slots 2-4 are intentional ABI gaps: the caustic trace selects their former scene/material context through the b5
// global StorageBuffer heap indirection below. Do not repurpose or renumber these holes.
#define NWB_CAUSTIC_SW_BINDING_MATERIAL_CONTEXT_SLOTS 5 // ConstantBuffer<NwbRayTraceMaterialContextSlots>
// Slots 6-8 are intentionally unused. SW caustics reads per-mesh nodes, positions, indices, and attributes from the
// global descriptor heap through slots carried by the material record; b5 selects the scene/material context buffers.
// Slots 9-10 are further intentional ABI gaps. Do not repurpose or renumber them.
// Caustic-specific inputs/output:
//  - EMISSION_TARGETS: historical local-layout position; now a logical StorageBuffer heap slot selecting the
//    per-frame refractive-instance world AABBs (P1) the photons aim at.
//  - VIEW: historical local-layout position; now a logical UniformBuffer heap slot selecting the camera view buffer
//    (worldToClip) the splat projects the receiver hit through.
//  - GBUFFER_DEPTH: historical local-layout position; now a logical heap-SRV slot selecting the G-buffer depth the
//    splat uses to reject sky/background.
//  - ACCUMULATOR: the R32_UINT fixed-point splat target (Texture2DArray, one layer per RGB channel).
//  - GBUFFER_WORLD_POSITION: historical local-layout position; now a logical heap-SRV slot selecting the G-buffer
//    world position the splat compares the photon's receiver hit against (the screen-space-leak reject -- a WORLD-
//    distance test, robust at grazing angles where the device-depth gradient across one pixel can exceed any fixed
//    device-depth tolerance and reject every valid splat).
#define NWB_CAUSTIC_SW_BINDING_EMISSION_TARGETS 11 // logical heap-StorageBuffer position; selected through push constants
#define NWB_CAUSTIC_SW_BINDING_VIEW 12 // logical heap-UniformBuffer position; selected through push constants
#define NWB_CAUSTIC_SW_BINDING_GBUFFER_DEPTH 13 // logical heap-SRV position; selected through push constants
#define NWB_CAUSTIC_SW_BINDING_ACCUMULATOR 14
#define NWB_CAUSTIC_SW_BINDING_GBUFFER_WORLD_POSITION 15 // logical heap-SRV position; selected through push constants

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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

