// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_SHADOW_BINDING_SLOTS_H
#define NWB_GRAPHICS_SHADOW_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_SHADOW_RT_SET 0

#define NWB_SHADOW_RT_BINDING_TLAS 0
#define NWB_SHADOW_RT_BINDING_GBUFFER_WORLD_POSITION 1
#define NWB_SHADOW_RT_BINDING_GBUFFER_NORMAL 2
#define NWB_SHADOW_RT_BINDING_GBUFFER_DEPTH 3
#define NWB_SHADOW_RT_BINDING_SCENE_SHADING 4
#define NWB_SHADOW_RT_BINDING_LIGHT_LIST 5
#define NWB_SHADOW_RT_BINDING_VISIBILITY_OUTPUT 6
// Per-instance occluder material table (NwbRtInstanceMaterial), indexed by InstanceID(); built lockstep with
// the TLAS instances so the array slot matches the hardware instance id.
#define NWB_SHADOW_RT_BINDING_INSTANCE_MATERIAL 7
// Parallel per-mesh descriptor arrays (slot k = mesh k), built lockstep with the TLAS so material.meshSlot
// indexes them: the raw triangle index buffer (the any-hit fetches the 3 vertex indices by PrimitiveIndex) +
// the U2 per-vertex shadow-trace attribute buffer (normal/uv0 the per-hit dispatch interpolates) + the raw
// object-space position buffer. The HW BLAS owns the positions it traces, but the any-hit ALSO needs them to
// derive the GEOMETRIC face normal (cross of two edges) for the per-crossing faceSign/cosI -- the shading
// normal flips over a wide band at a smooth-mesh silhouette and corrupts the signed Beer-Lambert telescoping,
// while the per-triangle geometric normal is robust (mirrors the software traversal, which reads positions too).
#define NWB_SHADOW_RT_BINDING_MESH_INDICES 8
#define NWB_SHADOW_RT_BINDING_MESH_ATTRIBUTES 9
// The shared material-constants context the per-hit transmittance dispatch reads (same buffers the rasterizer
// binds at NWB_MESH_BINDING_MATERIAL_TYPED / NWB_MESH_BINDING_INSTANCE, pointed here for this pass): the typed
// material-constant words + the per-instance mutable-storage records.
#define NWB_SHADOW_RT_BINDING_MATERIAL_TYPED 10
#define NWB_SHADOW_RT_BINDING_MESH_INSTANCES 11
// Per-mesh raw object-space position byte buffer (slot k = mesh k, float3 at vertexIndex * 12), for the any-hit's
// geometric face-normal derivation; indexed by the same i0/i1/i2 the index buffer yields.
#define NWB_SHADOW_RT_BINDING_MESH_POSITIONS 12

// Maximum distinct meshes the per-mesh descriptor arrays (index + attribute + position) can address in one frame;
// meshes beyond it cast a colorless (opaque) shadow that frame (logged once). The hardware shadow trace is now an
// inline-RayQuery COMPUTE pass, and the compute pipeline's single-stage descriptor budget is tighter than the RT
// pipeline's (which spread these three count-N SRV arrays across its raygen/miss/any-hit stages): 64 (3*64=192
// array descriptors) corrupted the compute binding set + broke the following deferred pass, while 32 (3*32=96) is
// stable. Kept at 32 for headroom; raising it needs the compute descriptor-set budget widened (a backend change).
#define NWB_SHADOW_RT_MAX_MESHES 32

// Workgroup size of the hardware RayQuery shadow trace (shadow_rayquery_cs): one thread per HALF-res shadow pixel.
#define NWB_SHADOW_RT_GROUP_SIZE 8


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Shadow UPSAMPLE pass (its own compute pipeline): the ray-traced shadow visibility is computed at HALF resolution
// (1/4 the rays), then this pass edge-aware (world-distance bilateral) upsamples it into the full-res shadow-visibility
// Texture2DArray the deferred lighting samples. Weighting each half-res tap by similarity to the full-res receiver +
// dropping background taps keeps the shadow confined to its surface (no bleed across silhouettes).
#define NWB_SHADOW_UPSAMPLE_SET 0
#define NWB_SHADOW_UPSAMPLE_BINDING_GBUFFER_WORLD_POSITION 0
#define NWB_SHADOW_UPSAMPLE_BINDING_GBUFFER_NORMAL 1        // G-buffer normal: the plane/normal edge-stop (no blur across silhouettes)
#define NWB_SHADOW_UPSAMPLE_BINDING_GBUFFER_DEPTH 2
#define NWB_SHADOW_UPSAMPLE_BINDING_HALF_VISIBILITY 3   // half-res Texture2DArray SRV (input)
#define NWB_SHADOW_UPSAMPLE_BINDING_FULL_VISIBILITY 4   // full-res Texture2DArray UAV (output, lighting reads this)
#define NWB_SHADOW_UPSAMPLE_GROUP_SIZE 8


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

