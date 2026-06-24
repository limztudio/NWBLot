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
// the U2 per-vertex shadow-trace attribute buffer (normal/uv0 the per-hit dispatch interpolates). The HW BLAS
// owns the positions, so only indices + attributes are bound here.
#define NWB_SHADOW_RT_BINDING_MESH_INDICES 8
#define NWB_SHADOW_RT_BINDING_MESH_ATTRIBUTES 9
// The shared material-constants context the per-hit transmittance dispatch reads (same buffers the rasterizer
// binds at NWB_MESH_BINDING_MATERIAL_TYPED / NWB_MESH_BINDING_INSTANCE, pointed here for this pass): the typed
// material-constant words + the per-instance mutable-storage records.
#define NWB_SHADOW_RT_BINDING_MATERIAL_TYPED 10
#define NWB_SHADOW_RT_BINDING_MESH_INSTANCES 11

// Maximum distinct meshes the per-mesh descriptor arrays can address in one frame (mirrors the software path's
// NWB_SW_SHADOW_MAX_MESHES so the C++ slot arrays and the shader's `[NWB_SHADOW_RT_MAX_MESHES]` stay one cap).
#define NWB_SHADOW_RT_MAX_MESHES 64


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

