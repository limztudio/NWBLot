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
// 7: free. Formerly the per-instance occluder material table (NwbRtInstanceMaterial). The hardware opaque fast
// path commits the first FORCE_OPAQUE hit via ACCEPT_FIRST_HIT_AND_END_SEARCH and loads no per-instance material,
// so this binding was removed. The slot is left as a gap (this binding layout matches by explicit slot number, not
// position, so bindings need not be contiguous) to keep the mesh-array slots below at their existing numbers. The
// shared material buffer (m_shadowInstanceMaterialBuffer) still exists -- the software shadow / GI / caustics paths
// read it through their own binding sets.
// Slots 8-9 (the former parallel per-mesh index + per-triangle-corner attribute descriptor arrays) were removed in
// the step 4c bounded-path teardown: the hardware opaque shadow trace commits the first FORCE_OPAQUE hit and reads
// no geometry, so they were declared-but-never-fetched dead bindings (M3 already dropped them from occlusion.slangi /
// shadow_rayquery.slangi). The numbers are left as a gap (not renumbered) so the surrounding bindings keep their
// values. The transparent-occluder geometry the colored software shadow needs is fetched from the global descriptor
// heap by the SW path, not from this trace's slot map.
// The shared material-constants context the per-hit transmittance dispatch reads (same buffers the rasterizer
// binds at NWB_MESH_BINDING_MATERIAL_TYPED / NWB_MESH_BINDING_INSTANCE, pointed here for this pass): the typed
// material-constant words + the per-instance mutable-storage records.
#define NWB_SHADOW_RT_BINDING_MATERIAL_TYPED 10
#define NWB_SHADOW_RT_BINDING_MESH_INSTANCES 11
// Slot 12 (the former per-mesh raw object-space position descriptor array, used to derive the geometric face normal)
// was removed in the same step 4c teardown for the same reason -- the opaque trace reads no geometry. Left as a gap,
// not renumbered.

// Maximum distinct meshes the per-mesh descriptor arrays (index + attribute + position) can address in one frame;
// meshes beyond it cast a colorless (opaque) shadow that frame (logged once). The hardware shadow trace is an
// inline-RayQuery COMPUTE pass, whose single-stage descriptor budget limits these arrays to 32 (3*32=96) descriptors.
// Kept at 32 for headroom; raising it needs the compute descriptor-set budget widened (a backend change).
#define NWB_SHADOW_RT_MAX_MESHES 32

// Workgroup size of the hardware RayQuery opaque shadow trace (shadow_rayquery_cs): one thread per FULL-res pixel.
#define NWB_SHADOW_RT_GROUP_SIZE 8


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

