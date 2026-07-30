// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "sw_binding_slots.h"
#include "photon_push_constants.slangi"
#include "photon_types.slangi"
#include "../mesh/runtime_constants.h"
#include "../deferred/bindless_resources.slangi"
#include "../raytrace/material_context_slots.slangi"
#include "../bindless/bindless_heap_rt.slangi"

[[vk::push_constant]]
ConstantBuffer<NwbCausticPhotonPushConstants> g_NwbCausticSwPushConstants;

// The shared scene-shading CB + light list are selected by the frame selector payload in the UniformBuffer heap.
#pragma warning(push)
#pragma warning(disable: 39001) // Deliberate typed alias of the global UniformBuffer descriptor table.
[[vk::binding(NWB_BINDLESS_HEAP_BINDING_UNIFORM_BUFFER, NWB_BINDLESS_HEAP_RESOURCE_SET)]]
ConstantBuffer<NwbRayTraceMaterialContextSlots, Std140DataLayout> g_NwbHeapCausticSwMaterialContextSlots[];
#pragma warning(pop)
#define g_NwbCausticSwBindlessResources g_NwbHeapDeferredBindlessResources[g_NwbCausticSwPushConstants.deferredResourcesHeapSlot]
#define g_NwbCausticSwMaterialContextSlots g_NwbHeapCausticSwMaterialContextSlots[g_NwbCausticSwPushConstants.materialContextSlotsHeapSlot]
#define NWB_SCENE_SHADING_HEAP_SLOT g_NwbCausticSwBindlessResources.avboitSlots.z
#define NWB_SCENE_LIGHT_LIST_HEAP_SLOT g_NwbCausticSwBindlessResources.avboitSlots.w

// The per-hit surface dispatch reads its typed words and mutable mesh-instance records through the heap-selected
// material context above.
uint nwbMeshFrameMaterialTypedHeapSlot(){
    return g_NwbCausticSwMaterialContextSlots.sceneSlots.w;
}
uint nwbMeshFrameInstanceHeapSlot(){
    return g_NwbCausticSwMaterialContextSlots.materialSlots.x;
}
uint nwbMeshFrameViewHeapSlot(){
    return 0u; // The caustic surface path does not consume the mesh-view alias.
}
#include "../bindless/bindless_heap_mesh.slangi"

#include "../scene/buffer.slangi"
#include "../bvh/bvh_common.slangi"
#include "../bvh/ray_intersect.slangi"
#include "../bvh/sw_instance.slangi"
#include "../bvh/ray_vertex_attrib.slangi"
#include "../common/math.slangi"

// Global descriptor-heap accessors for per-mesh geometry. The material record carries the slots; this include provides
// NwbHeapBvhNodes() for nodes and NwbHeapRawBuffer() for positions, indices, and attributes.
#include "../bindless/bindless_heap_bvh.slangi"

// The shadow trace's material-constants context (NwbShadowHit + the per-invocation accessors + the surface contract)
// + the cook-generated per-hit surface dispatch (nwbShadowDispatchSurface), which #includes each transparent
// material's .bind + .surface and routes by shadowTransmittanceModelId to return that occluder's optical params
// (ior / transmission). The caustic closest-hit hook evaluates a refractive surface through the IDENTICAL dispatch
// the shadow trace uses, so the two passes agree on a glass's optical params.
#include "../shadow/shadow_surface.slangi"
#include "shadow/generated/transmittance_dispatch.slangi"

// The shared caustic transport layer: the photon struct, the emission aim, the iterative bounce loop, the splat
// math. It declares the backend closest-hit hook nwbCausticTraceClosest, defined below over this pass's BVH fetch.
#include "caustic_trace.slangi"
#include "light_pick.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The software scene-instance table has its own structured record stride, but shares the global StorageBuffer
// descriptor class with all other trace-context buffers. The b5 cbuffer supplies this table's uniform heap slot.
#pragma warning(push)
#pragma warning(disable: 39001) // Deliberate typed view of the StorageBuffer descriptor table.
[[vk::binding(NWB_BINDLESS_HEAP_BINDING_STORAGE_BUFFER, NWB_BINDLESS_HEAP_RESOURCE_SET)]]
StructuredBuffer<NwbSwBvhInstance, Std430DataLayout> g_NwbHeapCausticSwInstanceBuffers[];
#pragma warning(pop)

StructuredBuffer<NwbSwBvhInstance, Std430DataLayout> NwbHeapCausticSwInstances(const uint slot){
    return g_NwbHeapCausticSwInstanceBuffers[slot];
}

// Per-mesh nodes, positions, indices, and attributes are read through descriptor-heap slots carried by the material
// record. The shared backing buffers remain transitioned for those heap reads.

// The caustic-specific producer inputs share the global heap's StorageBuffer / UniformBuffer tables with the trace
// material context and mesh-frame view. Their slots are uniform for one dispatch and ride the photon push block, so
// no NonUniformResourceIndex decoration is needed here.
#pragma warning(push)
#pragma warning(disable: 39001) // Deliberate typed aliases of the global heap descriptor tables.
[[vk::binding(NWB_BINDLESS_HEAP_BINDING_STORAGE_BUFFER, NWB_BINDLESS_HEAP_RESOURCE_SET)]]
StructuredBuffer<NwbCausticEmissionTarget, Std430DataLayout> g_NwbHeapCausticSwEmissionTargets[];
[[vk::binding(NWB_BINDLESS_HEAP_BINDING_UNIFORM_BUFFER, NWB_BINDLESS_HEAP_RESOURCE_SET)]]
ConstantBuffer<NwbCausticPhotonView, Std140DataLayout> g_NwbHeapCausticSwViews[];
#pragma warning(pop)

Texture2D<float4> nwbCausticSwGbufferDepth(){
    return NwbHeapSampledImage2D(g_NwbCausticSwPushConstants.depthSlot);
}

Texture2D<float4> nwbCausticSwGbufferWorldPosition(){
    return NwbHeapSampledImage2D(g_NwbCausticSwPushConstants.worldPositionSlot);
}

StructuredBuffer<NwbCausticEmissionTarget, Std430DataLayout> nwbCausticSwEmissionTargets(){
    return g_NwbHeapCausticSwEmissionTargets[g_NwbCausticSwPushConstants.emissionTargetSlot];
}

ConstantBuffer<NwbCausticPhotonView, Std140DataLayout> nwbCausticSwView(){
    return g_NwbHeapCausticSwViews[g_NwbCausticSwPushConstants.viewSlot];
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbCausticSwTriangleHit{
    bool hit;
    float t;
    float u;
    float v;
    float w;
};

// Moller-Trumbore ray/triangle test (two-sided), reporting a hit strictly inside (tMin, tMax) with barycentrics.
NwbCausticSwTriangleHit nwbCausticSwRayTriangle(const float3 origin, const float3 direction, const float tMin, const float tMax, const float3 v0, const float3 v1, const float3 v2){
    NwbCausticSwTriangleHit result;
    result.hit = false;
    result.t = 0.0;
    result.u = 0.0;
    result.v = 0.0;
    result.w = 0.0;

    const NwbRayTriangleHit hit = nwbRayTriangleMollerTrumbore(origin, direction, tMin, tMax, v0, v1, v2);
    result.hit = hit.hit;
    result.t = hit.t;
    result.u = hit.u;
    result.v = hit.v;
    result.w = hit.w;
    return result;
}

NwbRayTraceVertexAttrib nwbCausticSwLoadVertexAttrib(const uint attributeSlot, const uint attributeIndex){
    // attributeSlot is this mesh's per-corner attribute buffer's GLOBAL HEAP slot (carried on the instance material
    // record, populated lockstep with the mesh in rt_swbvh.cpp). It is non-uniform across the wave -- each photon's ray
    // hits a different mesh -- so the heap subscript MUST be NonUniformResourceIndex-tagged; NwbHeapRawBuffer() applies
    // that internally and is re-invoked at each Load so the decoration is applied fresh at every descriptor access.
    // One Load4 for the whole 16-byte attribute record (packed normal in .xy, uv0 in .zw) -- a single memory request
    // instead of two dependent Load2s (bit-identical result, lower latency in the refractive-hit path).
    return nwbRayTraceDecodeVertexAttrib(NwbHeapRawBuffer(attributeSlot).Load4(attributeIndex * 16u));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Per-instance closest-hit refinement: push the world ray into the instance's object space, walk that mesh's
// triangle BVH, and keep the NEAREST triangle crossing (smallest t). Updates the running closest hit in `closest`
// (its .t is the current min). On a closer hit it records the world-space hit point, the interpolated SHADING world
// normal (a0*w + a1*u + a2*v transformed by the inverse-transpose -- smooth across the mesh -> smooth caustic; the
// HW closest-hit interpolates the SAME way for parity), the refractive flag, and -- for a refractive crossing -- the
// optical params (ior / transmission) from the occluder's surface hook, evaluated through the SAME dispatch the
// shadow trace uses.
void nwbCausticSwInstanceClosest(const uint instanceIndex, const float3 worldOrigin, const float3 worldDirection, const float tMin, inout NwbCausticHit closest){
    const NwbSwBvhInstance instance = NwbHeapCausticSwInstances(g_NwbCausticSwMaterialContextSlots.sceneSlots.y)[instanceIndex];
    const NwbRtInstanceMaterial material = NwbHeapRtInstanceMaterials(g_NwbCausticSwMaterialContextSlots.sceneSlots.z)[instanceIndex];
    const bool refractive = (material.flags & NWB_RT_INSTANCE_MATERIAL_FLAG_REFRACTIVE) != 0u;
    // This instance's per-mesh geometry (BVH nodes / indices / positions / attributes) is fetched from the global
    // descriptor heap by the four per-buffer slots the material record carries (populated lockstep with the mesh in
    // rt_swbvh.cpp). Each slot is NON-UNIFORM across the wave -- different photons' rays
    // hit different meshes -- so its heap subscript must be NonUniformResourceIndex-tagged; NwbHeapBvhNodes() /
    // NwbHeapRawBuffer() apply that internally and are re-invoked at EACH access below (never hoisted into a resource
    // local) so the decoration is applied fresh at every subscript. Omitting it is undefined behaviour: a wave
    // straddling a mesh boundary reads a DIFFERENT mesh's BVH/geometry for some lanes.
    const float3 origin = nwbSwBvhTransformPoint(instance, worldOrigin);
    const float3 direction = nwbSwBvhTransformVector(instance, worldDirection);
    const float3 invDir = nwbBvhSafeInvDir(direction);

    // FRONT-TO-BACK ordered descent. Keep the current node loaded; at an internal node descend the NEARER-entry child
    // and defer the farther onto the stack (re-tested when popped, against the by-then-smaller closest.t). Processing
    // near first shrinks closest.t sooner, so more far subtrees cull -> fewer triangle tests + refractive dispatches.
    // The stack holds only deferred FAR children (<= tree depth).
    uint stack[NWB_CAUSTIC_SW_MESH_STACK_SIZE];
    uint stackPointer = 0u;
    NwbBvhNode node = NwbHeapBvhNodes(material.nodeSlot)[0u]; // per-mesh root
    bool active = nwbBvhRayAabb(origin, invDir, tMin, closest.t, node.aabbMin, node.aabbMax, 0.0);

    [loop] while(active){
        if(nwbBvhNodeIsLeaf(node)){
            const uint triangle = nwbBvhLeafPrimitive(node);
            const uint indexBase = triangle * 3u;
            // The 3 triangle indices are contiguous -> one Load3 instead of three Loads (bit-identical, one request).
            const uint3 triIndices = NwbHeapRawBuffer(material.indexSlot).Load3(indexBase * 4u);
            const uint i0 = triIndices.x;
            const uint i1 = triIndices.y;
            const uint i2 = triIndices.z;
            const float3 v0 = asfloat(NwbHeapRawBuffer(material.positionSlot).Load3(i0 * 12u));
            const float3 v1 = asfloat(NwbHeapRawBuffer(material.positionSlot).Load3(i1 * 12u));
            const float3 v2 = asfloat(NwbHeapRawBuffer(material.positionSlot).Load3(i2 * 12u));
            const NwbCausticSwTriangleHit triangleHit = nwbCausticSwRayTriangle(origin, direction, tMin, closest.t, v0, v1, v2);
            if(triangleHit.hit){
                // Closer than the running min -> adopt it.
                closest.hit = true;
                closest.t = triangleHit.t;
                closest.isRefractive = refractive;
                closest.worldPos = worldOrigin + worldDirection * triangleHit.t;
                closest.shadingNormal = float3(0.0, 0.0, 1.0); // filled with the real interpolated normal for a refractive hit below
                closest.ior = 1.0;
                closest.transmission = half3(1.0h, 1.0h, 1.0h);

                if(refractive){
                    // Evaluate this material's surface hook for its optical params, the SAME way the shadow trace
                    // does (interpolated normal/uv0 + the material-constants context the instance record carries).
                    const uint attributeBase = triangle * 3u;
                    const NwbRayTraceVertexAttrib a0 = nwbCausticSwLoadVertexAttrib(material.attributeSlot, attributeBase);
                    const NwbRayTraceVertexAttrib a1 = nwbCausticSwLoadVertexAttrib(material.attributeSlot, attributeBase + 1u);
                    const NwbRayTraceVertexAttrib a2 = nwbCausticSwLoadVertexAttrib(material.attributeSlot, attributeBase + 2u);
                    const float3 objectShadingNormal = a0.normal * triangleHit.w + a1.normal * triangleHit.u + a2.normal * triangleHit.v;

                    // The interpolated SHADING normal in world space (inverse-transpose of object->world). This is the
                    // bend normal (smooth across the mesh -> smooth caustic) AND the surface-dispatch normal.
                    const float3 worldShadingNormal = nwbSafeNormalize(nwbSwBvhTransformNormal(instance, objectShadingNormal), float3(0.0, 0.0, 1.0));
                    closest.shadingNormal = worldShadingNormal;

                    NwbShadowHit hit;
                    hit.meshInstanceIndex = material.meshInstanceIndex;
                    hit.shadowTransmittanceModelId = material.shadowTransmittanceModelId;
                    hit.materialConstantByteOffset = material.materialConstantByteOffset;
                    hit.uv0 = a0.uv0 * triangleHit.w + a1.uv0 * triangleHit.u + a2.uv0 * triangleHit.v;
                    hit.worldNormal = worldShadingNormal;
                    hit.worldPosition = closest.worldPos;
                    hit.rayDirection = worldDirection;

                    const NwbMeshSurface surface = nwbShadowDispatchSurface(material.shadowTransmittanceModelId, hit);
                    closest.ior = surface.refractionIor;
                    closest.transmission = surface.shadowAbsorptionTint;
                }
            }
            active = false; // leaf done -> pop the next deferred node below
        }
        else{
            const NwbBvhNode leftNode = NwbHeapBvhNodes(material.nodeSlot)[node.leftChild];
            const NwbBvhNode rightNode = NwbHeapBvhNodes(material.nodeSlot)[node.rightChild];
            float tLeft;
            const bool hitLeft = nwbBvhRayAabbT(origin, invDir, tMin, closest.t, leftNode.aabbMin, leftNode.aabbMax, 0.0, tLeft);
            float tRight;
            const bool hitRight = nwbBvhRayAabbT(origin, invDir, tMin, closest.t, rightNode.aabbMin, rightNode.aabbMax, 0.0, tRight);
            if(hitLeft && hitRight){
                // Descend the nearer child; defer the farther. A full stack drops that farther subtree rather than
                // corrupting traversal state.
                if(tLeft <= tRight){
                    if(stackPointer < NWB_CAUSTIC_SW_MESH_STACK_SIZE) stack[stackPointer++] = node.rightChild;
                    node = leftNode;
                }
                else{
                    if(stackPointer < NWB_CAUSTIC_SW_MESH_STACK_SIZE) stack[stackPointer++] = node.leftChild;
                    node = rightNode;
                }
                continue;
            }
            if(hitLeft){ node = leftNode; continue; }
            if(hitRight){ node = rightNode; continue; }
            active = false; // both children culled -> pop
        }

        // Pop the next deferred node that still survives the (now-smaller) closest.t cull; drain -> done.
        [loop] while(stackPointer > 0u){
            node = NwbHeapBvhNodes(material.nodeSlot)[stack[--stackPointer]];
            if(nwbBvhRayAabb(origin, invDir, tMin, closest.t, node.aabbMin, node.aabbMax, 0.0)){
                active = true;
                break;
            }
        }
    }
}

// The SW backend's closest-hit hook: walk the software scene/instance BVH for every instance whose world AABB the
// ray crosses, refining each with the exact per-mesh triangle traversal and keeping the nearest hit across all of
// them. Mirrors nwbSwShadowOccluded's scene-BVH descent but tracks min-t instead of accumulating.
NwbCausticHit nwbCausticTraceClosest(float3 origin, float3 direction, float tMin, float tMax){
    NwbCausticHit closest;
    closest.hit = false;
    closest.isRefractive = false;
    closest.t = tMax;
    closest.ior = 1.0;
    closest.worldPos = float3(0.0, 0.0, 0.0);
    closest.shadingNormal = float3(0.0, 0.0, 1.0);
    closest.transmission = half3(1.0h, 1.0h, 1.0h);

    if(g_NwbCausticSwPushConstants.instanceCount == 0u)
        return closest;

    const float3 invDir = nwbBvhSafeInvDir(direction);

    // Near-first traversal avoids unnecessary mesh work without changing closest-hit results.
    uint stack[NWB_CAUSTIC_SW_SCENE_STACK_SIZE];
    uint stackPointer = 0u;
    NwbBvhNode node = NwbHeapBvhNodes(g_NwbCausticSwMaterialContextSlots.sceneSlots.x)[0u]; // root
    bool active = nwbBvhRayAabb(origin, invDir, tMin, closest.t, node.aabbMin, node.aabbMax, 0.0);

    [loop] while(active){
        if(nwbBvhNodeIsLeaf(node)){
            nwbCausticSwInstanceClosest(nwbBvhLeafPrimitive(node), origin, direction, tMin, closest);
            active = false; // -> pop below
        }
        else{
            const NwbBvhNode leftNode = NwbHeapBvhNodes(g_NwbCausticSwMaterialContextSlots.sceneSlots.x)[node.leftChild];
            const NwbBvhNode rightNode = NwbHeapBvhNodes(g_NwbCausticSwMaterialContextSlots.sceneSlots.x)[node.rightChild];
            float tLeft;
            const bool hitLeft = nwbBvhRayAabbT(origin, invDir, tMin, closest.t, leftNode.aabbMin, leftNode.aabbMax, 0.0, tLeft);
            float tRight;
            const bool hitRight = nwbBvhRayAabbT(origin, invDir, tMin, closest.t, rightNode.aabbMin, rightNode.aabbMax, 0.0, tRight);
            if(hitLeft && hitRight){
                if(tLeft <= tRight){
                    if(stackPointer < NWB_CAUSTIC_SW_SCENE_STACK_SIZE) stack[stackPointer++] = node.rightChild;
                    node = leftNode;
                }
                else{
                    if(stackPointer < NWB_CAUSTIC_SW_SCENE_STACK_SIZE) stack[stackPointer++] = node.leftChild;
                    node = rightNode;
                }
                continue;
            }
            if(hitLeft){ node = leftNode; continue; }
            if(hitRight){ node = rightNode; continue; }
            active = false; // both children culled -> pop
        }

        [loop] while(stackPointer > 0u){
            node = NwbHeapBvhNodes(g_NwbCausticSwMaterialContextSlots.sceneSlots.x)[stack[--stackPointer]];
            if(nwbBvhRayAabb(origin, invDir, tMin, closest.t, node.aabbMin, node.aabbMax, 0.0)){
                active = true;
                break;
            }
        }
    }

    return closest;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_CAUSTIC_SPLAT_VIEW nwbCausticSwView()
#define NWB_CAUSTIC_SPLAT_PUSH g_NwbCausticSwPushConstants
#define NWB_CAUSTIC_SPLAT_GBUFFER_DEPTH nwbCausticSwGbufferDepth()
#define NWB_CAUSTIC_SPLAT_GBUFFER_WORLD_POSITION nwbCausticSwGbufferWorldPosition()
#define NWB_CAUSTIC_SPLAT_ACCUMULATOR NwbHeapStorageImage2DArrayUint(g_NwbCausticSwPushConstants.accumulatorStorageSlot)
#include "splat.slangi"
#undef NWB_CAUSTIC_SPLAT_ACCUMULATOR
#undef NWB_CAUSTIC_SPLAT_GBUFFER_WORLD_POSITION
#undef NWB_CAUSTIC_SPLAT_GBUFFER_DEPTH
#undef NWB_CAUSTIC_SPLAT_PUSH
#undef NWB_CAUSTIC_SPLAT_VIEW


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_CAUSTIC_SW_GROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint photonIndex = dispatchThreadID.x;
    if(photonIndex >= g_NwbCausticSwPushConstants.photonCount)
        return;
    if(g_NwbCausticSwPushConstants.emissionTargetCount == 0u || g_NwbCausticSwPushConstants.instanceCount == 0u)
        return;

    NwbSceneLight light;
    if(!nwbCausticPickLight(photonIndex, light))
        return;

    // Photons travel along the light's emission axis.
    const float3 lightDir = nwbSafeNormalize(-light.direction.xyz, float3(0.0, 0.0, -1.0));

    // Hash target selection to decorrelate it from the photon grid.
    const uint targetIndex = nwbCausticHashU32(photonIndex) % g_NwbCausticSwPushConstants.emissionTargetCount;
    const NwbCausticEmissionTarget target = nwbCausticSwEmissionTargets()[targetIndex];
    const float3 targetAabbMin = target.aabbMin.xyz;
    const float3 targetAabbMax = target.aabbMax.xyz;

    const float2 cell = nwbCausticJitteredTemporalCell(
        photonIndex,
        g_NwbCausticSwPushConstants.gridSide,
        g_NwbCausticSwPushConstants.frameIndex,
        g_NwbCausticSwPushConstants.temporalPhaseCount
    );

    const NwbCausticEmission emission = nwbCausticEmitPhotonAtTarget(lightDir, targetAabbMin, targetAabbMax, cell);
    if(!emission.valid)
        return;

    const NwbCausticPhoton photon = nwbCausticCreatePhoton(
        emission,
        nwbCausticLightColor(light.colorIntensity),
        g_NwbCausticSwPushConstants.emissionTargetCount,
        g_NwbCausticSwPushConstants.photonCount
    );

    const NwbCausticSplat splat = nwbCausticTracePhoton(photon);
    if(!splat.valid)
        return;

    nwbCausticSplat(splat.worldPos, splat.flux);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


