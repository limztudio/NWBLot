// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_GI_BINDING_SLOTS_H
#define NWB_GRAPHICS_GI_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Dynamic Diffuse GI (DDGI). One world-space probe grid; per-frame a round-robin fraction of probes trace
// NWB_GI_RAYS_PER_PROBE spherical-Fibonacci rays (one shared per-frame R2 rotation), closest-hit shade (Lambert +
// prev-atlas bounce + optional dominant-light shadow ray), and the results EMA-blend into ping-pong octahedral
// irradiance + distance atlases. The atlases live on RendererRayTracingState (NOT DeferredFrameTargets) so a window
// resize does not reset probe convergence. See .helper/ddgi_plan.md §2.
//
// Binding layout (U1 scaffold): the trace pass binds the grid CB + the TLAS / SW scene-BVH context (mirrors the
// shadow trace) + the ray-data UAV + the prev-front atlases (SRV) + the hit-albedo buffer. The blend passes bind
// the grid CB + the ray-data SRV + the front/back atlas UAVs. The border pass binds the grid CB + the front atlas
// UAV. All passes share set 0.


// Set 0 for every GI pass (matches the shadow / caustic single-set convention).
#define NWB_GI_SET 0

// The grid constant buffer (~10 Float4: grid origin/spacing/dims, rays/probe, update fraction, frame counter,
// hysteresis, history-front flag, bias params). Lives in its own CB because it fits neither the 16B scene-shading
// CB nor the 128B push ceiling.
#define NWB_GI_BINDING_GRID_CONSTANTS 0

// --- Trace pass bindings (gi_probe_trace_*_cs) ---
// The ray-data OUTPUT UAV (RGBA16F: rgb = irradiance, a = hitT; first NWB_GI_PROBE_FIXED_RAY_COUNT slots are
// deterministic and excluded from blending so v2 relocation/classification are additive kernels).
#define NWB_GI_BINDING_RAY_DATA 1
// The PREVIOUS-FRONT irradiance atlas (SRV) -- the trace fetches probe irradiance for the infinite-bounce term.
#define NWB_GI_BINDING_PREV_IRRADIANCE 2
// The PREVIOUS-FRONT distance atlas (SRV) -- the trace fetches probe mean distance for the hit-T compare / bias.
#define NWB_GI_BINDING_PREV_DISTANCE 3
// Per-instance flat albedo (CPU-built StructuredBuffer; NWB_GI_DEFAULT_ALBEDO fallback).
#define NWB_GI_BINDING_HIT_ALBEDO 4

// --- Blend pass bindings (gi_probe_blend_*_cs) ---
// The ray-data SRV (the blend reads this frame's trace results). Shares NWB_GI_BINDING_RAY_DATA's slot index; the
// pass transitions it UAV->SRV between the trace and the blend.
// The FRONT (history-in) irradiance atlas SRV. Reads the EMA history the new sample blends into.
#define NWB_GI_BINDING_FRONT_IRRADIANCE 5
// The BACK (history-out) irradiance atlas UAV. Writes the blended result; flipped to front at block end.
#define NWB_GI_BINDING_BACK_IRRADIANCE 6
// The FRONT (history-in) distance atlas SRV.
#define NWB_GI_BINDING_FRONT_DISTANCE 7
// The BACK (history-out) distance atlas UAV.
#define NWB_GI_BINDING_BACK_DISTANCE 8

// --- Border pass bindings (gi_probe_border_cs) ---
// The irradiance atlas UAV (border fill -- copies the edge texels from the interior). Shares the BACK slot when the
// border runs after the blend on the same (now-front) atlas.
// The distance atlas UAV (border fill). Shares the BACK slot likewise.


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Workgroup size: one workgroup per probe (trace), one thread per atlas texel (blend/border). 8x8 = 64 threads -- a
// single probe's NWB_GI_RAYS_PER_PROBE rays fit one warp/wavefront with headroom, and the 6x6 / 14x14 atlas interior
// + border tiles cleanly under 64 threads.
#define NWB_GI_GROUP_SIZE 8

// Fixed ray count reserved at the front of the ray-data texture so v2 relocation/classification are ADDITIVE
// kernels (never a layout migration). The trace writes into [0, NWB_GI_RAYS_PER_PROBE); v2 appends into the
// reserved tail. 32 is generous for v1 (the Default tier ships 64 rays/probe, but the reservation is layout-stable).
#define NWB_GI_PROBE_FIXED_RAY_COUNT 32

// Default albedo written into the per-instance hit-albedo buffer when a mesh has no resolved albedo (the trace's
// Lambert term uses this so a missing albedo never blackens the bounce).
#define NWB_GI_DEFAULT_ALBEDO_FLOAT3 0.5f, 0.5f, 0.5f

// Default grid parameters (Default tier per D1). Hand-placed per scene (D5); the grid-CB carries the runtime values.
#define NWB_GI_DEFAULT_GRID_ORIGIN_X -8.0f
#define NWB_GI_DEFAULT_GRID_ORIGIN_Y 0.0f
#define NWB_GI_DEFAULT_GRID_ORIGIN_Z -8.0f
#define NWB_GI_DEFAULT_GRID_SPACING 1.0f
#define NWB_GI_DEFAULT_GRID_SIZE_X 16u
#define NWB_GI_DEFAULT_GRID_SIZE_Y 8u
#define NWB_GI_DEFAULT_GRID_SIZE_Z 16u

// Default rays/probe + update fraction (Default tier per D1; the divisor is the master pressure valve).
#define NWB_GI_DEFAULT_RAYS_PER_PROBE 64u
#define NWB_GI_DEFAULT_UPDATE_DIVISOR 4u

// Hit shadow rays toward the dominant light per hit (D4 = 1; U3 measures both 1 and 0).
#define NWB_GI_HIT_SHADOW_RAYS 1


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

