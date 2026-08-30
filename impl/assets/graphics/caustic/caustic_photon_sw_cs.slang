// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "sw_binding_slots.h"
#include "photon_push_constants.slangi"
#include "photon_types.slangi"
#include "../bvh/constants.h"
#include "../mesh/runtime_constants.h"
#include "../deferred/bindless_resources.slangi"
#include "../raytrace/material_context_slots.slangi"
#include "../bindless/bindless_heap_rt.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::push_constant]]
ConstantBuffer<NwbCausticPhotonPushConstants> g_NwbCausticSwPushConstants;

#pragma warning(push)
#pragma warning(disable: 39001) // Deliberate typed alias of the global UniformBuffer descriptor table.
[[vk::binding(NWB_BINDLESS_HEAP_BINDING_UNIFORM_BUFFER, NWB_BINDLESS_HEAP_RESOURCE_SET)]]
ConstantBuffer<NwbRayTraceMaterialContextSlots, Std140DataLayout> g_NwbHeapCausticSwMaterialContextSlots[];
#pragma warning(pop)
#define g_NwbCausticSwBindlessResources g_NwbHeapDeferredBindlessResources[g_NwbCausticSwPushConstants.deferredResourcesHeapSlot]
#define g_NwbCausticSwMaterialContextSlots g_NwbHeapCausticSwMaterialContextSlots[g_NwbCausticSwPushConstants.materialContextSlotsHeapSlot]
#define NWB_SCENE_SHADING_HEAP_SLOT g_NwbCausticSwBindlessResources.avboitSlots.z
#define NWB_SCENE_LIGHT_LIST_HEAP_SLOT g_NwbCausticSwBindlessResources.avboitSlots.w

uint nwbMeshFrameMaterialTypedHeapSlot(){
    return g_NwbCausticSwMaterialContextSlots.sceneSlots.w;
}
uint nwbMeshFrameInstanceHeapSlot(){
    return g_NwbCausticSwMaterialContextSlots.materialSlots.x;
}
uint nwbMeshFrameViewHeapSlot(){
    return 0u; // The caustic surface path does not consume the mesh-view alias.
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../bindless/bindless_heap_mesh.slangi"

#include "../scene/buffer.slangi"
#include "../bvh/bvh_common.slangi"
#include "../bvh/ray_intersect.slangi"
#include "../bvh/sw_instance.slangi"
#include "../bvh/ray_vertex_attrib.slangi"
#include "../common/math.slangi"

#include "../bindless/bindless_heap_bvh.slangi"

// Caustics and shadows share per-hit surface dispatch.
#include "../shadow/shadow_surface.slangi"
#include "shadow/generated/transmittance_dispatch.slangi"

#include "caustic_trace.slangi"
#include "light_pick.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma warning(push)
#pragma warning(disable: 39001) // Deliberate typed view of the StorageBuffer descriptor table.
[[vk::binding(NWB_BINDLESS_HEAP_BINDING_STORAGE_BUFFER, NWB_BINDLESS_HEAP_RESOURCE_SET)]]
StructuredBuffer<NwbSwBvhInstance, Std430DataLayout> g_NwbHeapCausticSwInstanceBuffers[];
#pragma warning(pop)

StructuredBuffer<NwbSwBvhInstance, Std430DataLayout> NwbHeapCausticSwInstances(const uint slot){
    return g_NwbHeapCausticSwInstanceBuffers[slot];
}


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


NwbRayTraceVertexAttrib nwbCausticSwLoadVertexAttrib(const uint attributeSlot, const uint attributeIndex){
    // Per-mesh attribute slots are non-uniform heap indices.
    return nwbRayTraceDecodeVertexAttrib(NwbHeapRawBuffer(attributeSlot).Load4(attributeIndex * NWB_RAYTRACE_VERTEX_ATTRIBUTE_STRIDE_BYTES));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Per-instance traversal refines the nearest triangle hit.
void nwbCausticSwInstanceClosest(const uint instanceIndex, const float3 worldOrigin, const float3 worldDirection, const float tMin, inout NwbCausticHit closest){
    const NwbSwBvhInstance instance = NwbHeapCausticSwInstances(g_NwbCausticSwMaterialContextSlots.sceneSlots.y)[instanceIndex];
    const NwbRtInstanceMaterial material = NwbHeapRtInstanceMaterials(g_NwbCausticSwMaterialContextSlots.sceneSlots.z)[instanceIndex];
    const bool refractive = (material.flags & NWB_RT_INSTANCE_MATERIAL_FLAG_REFRACTIVE) != 0u;
    // Per-mesh geometry slots must remain non-uniform at every access.
    const float3 origin = nwbSwBvhTransformPoint(instance, worldOrigin);
    const float3 direction = nwbSwBvhTransformVector(instance, worldDirection);
    const float3 invDir = nwbBvhSafeInvDir(direction);

    // Near-first descent tightens closest.t before far-subtree tests.
    uint stack[NWB_CAUSTIC_SW_MESH_STACK_SIZE];
    stack[0u] = 0u;
    uint stackPointer = 1u;

    [loop] while(stackPointer > 0u){
        --stackPointer;
        NwbBvhNode node = NwbHeapBvhNodes(material.nodeSlot)[stack[stackPointer]];
        if(!nwbBvhRayAabb(origin, invDir, tMin, closest.t, node.aabbMin, node.aabbMax, 0.0))
            continue;

        [loop] while(true){
            if(nwbBvhNodeIsLeaf(node)){
                const uint triangle = nwbBvhLeafPrimitive(node);
                const uint indexBase = triangle * NWB_RAYTRACE_TRIANGLE_CORNER_COUNT;
                const uint3 triIndices = NwbHeapRawBuffer(material.indexSlot).Load3(
                    indexBase * NWB_RAYTRACE_INDEX_STRIDE_BYTES
                );
                const uint i0 = triIndices.x;
                const uint i1 = triIndices.y;
                const uint i2 = triIndices.z;
                const float3 v0 = asfloat(
                    NwbHeapRawBuffer(material.positionSlot).Load3(i0 * NWB_RAYTRACE_POSITION_STRIDE_BYTES)
                );
                const float3 v1 = asfloat(
                    NwbHeapRawBuffer(material.positionSlot).Load3(i1 * NWB_RAYTRACE_POSITION_STRIDE_BYTES)
                );
                const float3 v2 = asfloat(
                    NwbHeapRawBuffer(material.positionSlot).Load3(i2 * NWB_RAYTRACE_POSITION_STRIDE_BYTES)
                );
                const NwbRayTriangleHit triangleHit = nwbRayTriangleMollerTrumbore(
                    origin,
                    direction,
                    tMin,
                    closest.t,
                    v0,
                    v1,
                    v2
                );
                if(triangleHit.hit){
                    closest.hit = true;
                    closest.t = triangleHit.t;
                    closest.isRefractive = refractive;
                    closest.worldPos = worldOrigin + worldDirection * triangleHit.t;
                    closest.shadingNormal = float3(0.0, 0.0, 1.0); // replaced by the interpolated normal for refractive hits
                    closest.ior = 1.0;
                    closest.transmission = half3(1.0h, 1.0h, 1.0h);

                    if(refractive){
                        const uint attributeBase = triangle * NWB_RAYTRACE_TRIANGLE_CORNER_COUNT;
                        const NwbRayTraceVertexAttrib a0 = nwbCausticSwLoadVertexAttrib(
                            material.attributeSlot,
                            attributeBase
                        );
                        const NwbRayTraceVertexAttrib a1 = nwbCausticSwLoadVertexAttrib(
                            material.attributeSlot,
                            attributeBase + 1u
                        );
                        const NwbRayTraceVertexAttrib a2 = nwbCausticSwLoadVertexAttrib(
                            material.attributeSlot,
                            attributeBase + 2u
                        );
                        const float3 objectShadingNormal =
                            float3(a0.normal) * triangleHit.w
                            + float3(a1.normal) * triangleHit.u
                            + float3(a2.normal) * triangleHit.v
                        ;

                        const float3 worldShadingNormal = nwbSafeNormalize(
                            nwbSwBvhTransformNormal(instance, objectShadingNormal),
                            float3(0.0, 0.0, 1.0)
                        );
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
                break;
            }
            else{
                const NwbBvhNode leftNode = NwbHeapBvhNodes(material.nodeSlot)[node.leftChild];
                const uint right = nwbBvhNodeRightChildIndex(node);
                const NwbBvhNode rightNode = NwbHeapBvhNodes(material.nodeSlot)[right];
                float tLeft;
                const bool hitLeft = nwbBvhRayAabbT(
                    origin,
                    invDir,
                    tMin,
                    closest.t,
                    leftNode.aabbMin,
                    leftNode.aabbMax,
                    0.0,
                    tLeft
                );
                float tRight;
                const bool hitRight = nwbBvhRayAabbT(
                    origin,
                    invDir,
                    tMin,
                    closest.t,
                    rightNode.aabbMin,
                    rightNode.aabbMax,
                    0.0,
                    tRight
                );
                if(hitLeft && hitRight){
                    if(tLeft <= tRight){
                        if(stackPointer < NWB_CAUSTIC_SW_MESH_STACK_SIZE){
                            stack[stackPointer] = right;
                            ++stackPointer;
                        }
                        node = leftNode;
                    }
                    else{
                        if(stackPointer < NWB_CAUSTIC_SW_MESH_STACK_SIZE){
                            stack[stackPointer] = node.leftChild;
                            ++stackPointer;
                        }
                        node = rightNode;
                    }
                    continue;
                }
                if(hitLeft){ node = leftNode; continue; }
                if(hitRight){ node = rightNode; continue; }
                break;
            }
        }
    }
}

// Software closest-hit refines scene-BVH candidates through mesh BVHs.
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

    uint stack[NWB_CAUSTIC_SW_SCENE_STACK_SIZE];
    stack[0u] = 0u;
    uint stackPointer = 1u;

    [loop] while(stackPointer > 0u){
        --stackPointer;
        NwbBvhNode node = NwbHeapBvhNodes(g_NwbCausticSwMaterialContextSlots.sceneSlots.x)[stack[stackPointer]];
        if(!nwbBvhRayAabb(origin, invDir, tMin, closest.t, node.aabbMin, node.aabbMax, 0.0))
            continue;

        [loop] while(true){
            if(nwbBvhNodeIsLeaf(node)){
                nwbCausticSwInstanceClosest(nwbBvhLeafPrimitive(node), origin, direction, tMin, closest);
                break;
            }
            else{
                const NwbBvhNode leftNode = NwbHeapBvhNodes(g_NwbCausticSwMaterialContextSlots.sceneSlots.x)[node.leftChild];
                const uint right = nwbBvhNodeRightChildIndex(node);
                const NwbBvhNode rightNode = NwbHeapBvhNodes(g_NwbCausticSwMaterialContextSlots.sceneSlots.x)[right];
                float tLeft;
                const bool hitLeft = nwbBvhRayAabbT(
                    origin,
                    invDir,
                    tMin,
                    closest.t,
                    leftNode.aabbMin,
                    leftNode.aabbMax,
                    0.0,
                    tLeft
                );
                float tRight;
                const bool hitRight = nwbBvhRayAabbT(
                    origin,
                    invDir,
                    tMin,
                    closest.t,
                    rightNode.aabbMin,
                    rightNode.aabbMax,
                    0.0,
                    tRight
                );
                if(hitLeft && hitRight){
                    if(tLeft <= tRight){
                        if(stackPointer < NWB_CAUSTIC_SW_SCENE_STACK_SIZE){
                            stack[stackPointer] = right;
                            ++stackPointer;
                        }
                        node = leftNode;
                    }
                    else{
                        if(stackPointer < NWB_CAUSTIC_SW_SCENE_STACK_SIZE){
                            stack[stackPointer] = node.leftChild;
                            ++stackPointer;
                        }
                        node = rightNode;
                    }
                    continue;
                }
                if(hitLeft){ node = leftNode; continue; }
                if(hitRight){ node = rightNode; continue; }
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

    const float3 lightDir = nwbSafeNormalize(-light.direction.xyz, float3(0.0, 0.0, -1.0));

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

