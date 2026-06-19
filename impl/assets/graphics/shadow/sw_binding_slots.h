// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifndef NWB_GRAPHICS_SHADOW_SW_BINDING_SLOTS_H
#define NWB_GRAPHICS_SHADOW_SW_BINDING_SLOTS_H


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Software (compute) shadow traversal pass — the no-hardware-ray-tracing fallback. One thread per pixel
// reads the G-buffer, casts one occlusion ray per light through the software scene/instance BVH (and, in
// the per-mesh stage, each instance's triangle BVH), and packs a visible-bit per light into the R32_UINT
// visibility mask the deferred lighting pass already samples. Slots 0-6 are the scene-level pass; slots
// 7-10 add the per-mesh triangle traversal (instances + parallel per-mesh node/position/index arrays).
#define NWB_SW_SHADOW_SET 0

#define NWB_SW_SHADOW_BINDING_GBUFFER_WORLD_POSITION 0
#define NWB_SW_SHADOW_BINDING_GBUFFER_NORMAL 1
#define NWB_SW_SHADOW_BINDING_GBUFFER_DEPTH 2
#define NWB_SW_SHADOW_BINDING_SCENE_SHADING 3
#define NWB_SW_SHADOW_BINDING_LIGHT_LIST 4
#define NWB_SW_SHADOW_BINDING_SCENE_NODES 5
#define NWB_SW_SHADOW_BINDING_VISIBILITY_OUTPUT 6
#define NWB_SW_SHADOW_BINDING_SCENE_INSTANCES 7
#define NWB_SW_SHADOW_BINDING_MESH_NODES 8
#define NWB_SW_SHADOW_BINDING_MESH_POSITIONS 9
#define NWB_SW_SHADOW_BINDING_MESH_INDICES 10

// 8x8 = 64 threads per group (one thread per pixel).
#define NWB_SW_SHADOW_GROUP_SIZE 8

// Maximum distinct meshes the per-mesh descriptor arrays can address in one frame.
#define NWB_SW_SHADOW_MAX_MESHES 64

// Per-thread traversal stack depths. The scene/instance BVH is shallow (a few-to-hundreds of instances);
// the per-mesh triangle BVH is deeper. Both traversals treat a deeper subtree as occluded rather than
// skipping it, so these are generous-but-not-proven bounds, not correctness-critical.
#define NWB_SW_SHADOW_SCENE_STACK_SIZE 32
#define NWB_SW_SHADOW_MESH_STACK_SIZE 64


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
