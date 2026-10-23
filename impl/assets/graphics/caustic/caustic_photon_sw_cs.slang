// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef NWB_CAUSTIC_PHOTON_TRANSPORT
#error "NWB_CAUSTIC_PHOTON_TRANSPORT is fixed by this photon shader source"
#endif
#define NWB_CAUSTIC_PHOTON_TRANSPORT 1

#include "sw_binding_slots.h"
#include "photon_push_constants.slangi"
#include "photon_types.slangi"
#include "../bvh/constants.h"
#include "../mesh/runtime_constants.h"
#include "../deferred/bindless_resources.slangi"
#include "../raytrace/material_context_slots.slangi"
#include "../bvh/bindless_heap_rt.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::push_constant]] ConstantBuffer<NwbCausticPhotonPushConstants> g_NwbCausticSwPushConstants;

#pragma warning(push)
#pragma warning(disable: 39001) // Deliberate typed alias of the global UniformBuffer descriptor table.
[[vk::binding(NWB_BINDLESS_HEAP_BINDING_UNIFORM_BUFFER, NWB_BINDLESS_HEAP_RESOURCE_SET)]] ConstantBuffer<NwbRayTraceMaterialContextSlots, Std140DataLayout> g_NwbHeapCausticSwMaterialContextSlots[];
#pragma warning(pop)
#define g_NwbCausticSwBindlessResources g_NwbHeapDeferredBindlessResources[g_NwbCausticSwPushConstants.deferredResourcesHeapSlot]
#define g_NwbCausticSwMaterialContextSlots g_NwbHeapCausticSwMaterialContextSlots[g_NwbCausticSwPushConstants.materialContextSlotsHeapSlot]
#define NWB_SCENE_SHADING_HEAP_SLOT g_NwbCausticSwBindlessResources.avboitSlots.z
#define NWB_SCENE_LIGHT_LIST_HEAP_SLOT g_NwbCausticSwBindlessResources.avboitSlots.w


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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


#include "../mesh/bindless_heap_mesh.slangi"

#include "../scene/buffer.slangi"
#include "../bvh/bvh_common.slangi"
#include "../bvh/ray_intersect.slangi"
#include "../bvh/sw_instance.slangi"
#include "../common/math.slangi"

#include "../bvh/bindless_heap_bvh.slangi"

#include "caustic_trace.slangi"
#include "light_pick.slangi"
#if NWB_RT_CSG_ENABLED
#define NWB_CSG_RAY_HARDWARE 0
#define NWB_CSG_RAY_SW_STACK_SIZE NWB_CAUSTIC_SW_MESH_STACK_SIZE
#include "csg_hit.slangi"
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma warning(push)
#pragma warning(disable: 39001) // Deliberate typed view of the StorageBuffer descriptor table.
[[vk::binding(NWB_BINDLESS_HEAP_BINDING_STORAGE_BUFFER, NWB_BINDLESS_HEAP_RESOURCE_SET)]] StructuredBuffer<NwbSwBvhInstance, Std430DataLayout> g_NwbHeapCausticSwInstanceBuffers[];
#pragma warning(pop)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


StructuredBuffer<NwbSwBvhInstance, Std430DataLayout> nwbHeapCausticSwInstances(const uint slot){
    return g_NwbHeapCausticSwInstanceBuffers[slot];
}


#pragma warning(push)
#pragma warning(disable: 39001) // Deliberate typed aliases of the global heap descriptor tables.
[[vk::binding(NWB_BINDLESS_HEAP_BINDING_STORAGE_BUFFER, NWB_BINDLESS_HEAP_RESOURCE_SET)]] StructuredBuffer<NwbCausticEmissionTarget, Std430DataLayout> g_NwbHeapCausticSwEmissionTargets[];
[[vk::binding(NWB_BINDLESS_HEAP_BINDING_UNIFORM_BUFFER, NWB_BINDLESS_HEAP_RESOURCE_SET)]] ConstantBuffer<NwbCausticPhotonView, Std140DataLayout> g_NwbHeapCausticSwViews[];
#pragma warning(pop)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Texture2D<float4> nwbCausticSwGbufferDepth(){
    return nwbHeapSampledImage2D(g_NwbCausticSwPushConstants.depthSlot);
}

Texture2D<float4> nwbCausticSwGbufferWorldPosition(){
    return nwbHeapSampledImage2D(g_NwbCausticSwPushConstants.worldPositionSlot);
}

StructuredBuffer<NwbCausticEmissionTarget, Std430DataLayout> nwbCausticSwEmissionTargets(){
    return g_NwbHeapCausticSwEmissionTargets[g_NwbCausticSwPushConstants.emissionTargetSlot];
}

ConstantBuffer<NwbCausticPhotonView, Std140DataLayout> nwbCausticSwView(){
    return g_NwbHeapCausticSwViews[g_NwbCausticSwPushConstants.viewSlot];
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbOpticalTraceConfig nwbCausticOpticalConfig(){
    NwbOpticalTraceConfig config;
    config.instanceMaterialHeapSlot = g_NwbCausticSwMaterialContextSlots.sceneSlots.z;
    config.opticalSceneHeapSlot = g_NwbCausticSwMaterialContextSlots.materialSlots.y;
    config.opticalInstanceCount = g_NwbCausticSwMaterialContextSlots.materialSlots.z;
    config.maxQueries = NWB_OPTICAL_MAX_QUERIES;
    config.minDistance = 0.0;
    config.maxDistance = NWB_FLOAT_MAX;
    return config;
}

bool nwbCausticSwInstanceEvents(
    const NwbOpticalTraceConfig config, const uint instanceIndex, const float3 worldOrigin, const float3 worldDirection, const float tMin, const float maximum, const bool reverse,
    __ref NwbOpticalBootstrapEvent events[NWB_OPTICAL_MAX_CANDIDATE_EVENTS], inout uint eventCount, inout NwbCausticBoundary boundary, inout float tolerance, inout NwbOpticalTraceResult result
){
    const NwbSwBvhInstance instance = nwbHeapCausticSwInstances(g_NwbCausticSwMaterialContextSlots.sceneSlots.y)[instanceIndex];
    const NwbRtInstanceMaterial material = nwbHeapRtInstanceMaterials(g_NwbCausticSwMaterialContextSlots.sceneSlots.z)[instanceIndex];
#if NWB_RT_CSG_ENABLED
    if((material.flags & NWB_RT_INSTANCE_MATERIAL_FLAG_CSG_SHADOW) != 0u)
        return true;
#endif
    const bool refractive = (material.flags & NWB_RT_INSTANCE_MATERIAL_FLAG_REFRACTIVE) != 0u;
    if(!refractive)
        return true;
    // Per-mesh geometry slots must remain non-uniform at every access.
    const float3 origin = nwbSwBvhTransformPoint(instance, worldOrigin);
    const float3 direction = nwbSwBvhTransformVector(instance, worldDirection);
    const float3 invDir = nwbBvhSafeInvDir(direction);

    // Near-first descent tightens maximum before far-subtree tests.
    uint stack[NWB_CAUSTIC_SW_MESH_STACK_SIZE];
    stack[0u] = 0u;
    uint stackPointer = 1u;

    [loop] while(stackPointer > 0u){
        --stackPointer;
        NwbBvhNode node = nwbHeapBvhNodes(material.nodeSlot)[stack[stackPointer]];
        if(!nwbBvhRayAabb(origin, invDir, tMin, maximum, node.aabbMin, node.aabbMax, 0.0))
            continue;

        [loop] while(true){
            if(nwbBvhNodeIsLeaf(node)){
                const uint triangle = nwbBvhLeafPrimitive(node);
                const uint indexBase = triangle * NWB_RAYTRACE_TRIANGLE_CORNER_COUNT;
                const uint3 triIndices = nwbHeapRawBuffer(material.indexSlot).Load3(indexBase * NWB_RAYTRACE_INDEX_STRIDE_BYTES);
                const uint i0 = triIndices.x;
                const uint i1 = triIndices.y;
                const uint i2 = triIndices.z;
                const float3 v0 = asfloat(nwbHeapRawBuffer(material.positionSlot).Load3(i0 * NWB_RAYTRACE_POSITION_STRIDE_BYTES));
                const float3 v1 = asfloat(nwbHeapRawBuffer(material.positionSlot).Load3(i1 * NWB_RAYTRACE_POSITION_STRIDE_BYTES));
                const float3 v2 = asfloat(nwbHeapRawBuffer(material.positionSlot).Load3(i2 * NWB_RAYTRACE_POSITION_STRIDE_BYTES));
                const NwbRayTriangleHit triangleHit = nwbRayTriangleMollerTrumbore(origin, direction, tMin, maximum, v0, v1, v2);
                if(triangleHit.hit){
                    const float3 objectNormal = nwbRayTraceNormalizeGeometryVector(cross(nwbRayTraceScaleGeometryVector(v1 - v0), nwbRayTraceScaleGeometryVector(v2 - v0)));
                    const float3 normal = nwbRayTraceNormalizeGeometryVector(nwbSwBvhTransformNormal(instance, objectNormal));
                    if(!nwbCausticAppendBoundary(config, worldOrigin, worldDirection, triangleHit.t, instanceIndex, triangle, normal, reverse, events, eventCount, boundary, tolerance, result))
                        return false;
                }
                break;
            }
            else{
                const NwbBvhNode leftNode = nwbHeapBvhNodes(material.nodeSlot)[node.leftChild];
                const uint right = nwbBvhNodeRightChildIndex(node);
                const NwbBvhNode rightNode = nwbHeapBvhNodes(material.nodeSlot)[right];
                NwbBvhNode descendNode;
                uint farChild;
                bool bothHit;
                if(nwbBvhPickNearChild(origin, invDir, tMin, maximum, node, leftNode, right, rightNode, descendNode, farChild, bothHit)){
                    if(bothHit){
                        if(stackPointer == NWB_CAUSTIC_SW_MESH_STACK_SIZE){
                            result.termination = NWB_OPTICAL_TERMINATION_EVENT_LIMIT;
                            return false;
                        }
                        stack[stackPointer] = farChild;
                        ++stackPointer;
                    }
                    node = descendNode;
                    continue;
                }
                break;
            }
        }
    }
    return true;
}

bool nwbCausticCollectRefractiveEvents(
    const NwbOpticalTraceConfig config, const float3 origin, const float3 direction, const float minimum, const float maximum, const bool reverse,
    __ref NwbOpticalBootstrapEvent events[NWB_OPTICAL_MAX_CANDIDATE_EVENTS], out uint eventCount, out NwbCausticBoundary boundary, out float tolerance, inout NwbOpticalTraceResult result
){
    eventCount = 0u;
    boundary = (NwbCausticBoundary)0;
    tolerance = NWB_OPTICS_POSITION_TOLERANCE_SCALE * nwbOpticsPositionUncertainty(origin);
    if(!nwbOpticalConsumeQuery(config, result))
        return false;
    if(g_NwbCausticSwPushConstants.instanceCount == 0u)
        return true;
    const float3 invDir = nwbBvhSafeInvDir(direction);

    uint stack[NWB_CAUSTIC_SW_SCENE_STACK_SIZE];
    stack[0u] = 0u;
    uint stackPointer = 1u;

    bool sceneOverflow = false;
    [loop] while(stackPointer > 0u && !sceneOverflow){
        --stackPointer;
        NwbBvhNode node = nwbHeapBvhNodes(g_NwbCausticSwMaterialContextSlots.sceneSlots.x)[stack[stackPointer]];
        if(!nwbBvhRayAabb(origin, invDir, minimum, maximum, node.aabbMin, node.aabbMax, 0.0))
            continue;

        [loop] while(true){
            if(nwbBvhNodeIsLeaf(node)){
                if(!nwbCausticSwInstanceEvents(config, nwbBvhLeafPrimitive(node), origin, direction, minimum, maximum, reverse, events, eventCount, boundary, tolerance, result))
                    return false;
                break;
            }
            else{
                const NwbBvhNode leftNode = nwbHeapBvhNodes(g_NwbCausticSwMaterialContextSlots.sceneSlots.x)[node.leftChild];
                const uint right = nwbBvhNodeRightChildIndex(node);
                const NwbBvhNode rightNode = nwbHeapBvhNodes(g_NwbCausticSwMaterialContextSlots.sceneSlots.x)[right];
                NwbBvhNode descendNode;
                uint farChild;
                bool bothHit;
                if(nwbBvhPickNearChild(origin, invDir, minimum, maximum, node, leftNode, right, rightNode, descendNode, farChild, bothHit)){
                    if(bothHit){
                        if(stackPointer == NWB_CAUSTIC_SW_SCENE_STACK_SIZE){
                            sceneOverflow = true;
                            break;
                        }
                        stack[stackPointer] = farChild;
                        ++stackPointer;
                    }
                    node = descendNode;
                    continue;
                }
                break;
            }
        }
    }

    if(sceneOverflow){
        // Instance order is independent of SAH depth; keep every candidate when the fast traversal exceeds its local stack.
        eventCount = 0u;
        boundary = (NwbCausticBoundary)0;
        tolerance = NWB_OPTICS_POSITION_TOLERANCE_SCALE * nwbOpticsPositionUncertainty(origin);
        [loop] for(uint instance = 0u; instance < g_NwbCausticSwPushConstants.instanceCount; ++instance){
            if(!nwbCausticSwInstanceEvents(config, instance, origin, direction, minimum, maximum, reverse, events, eventCount, boundary, tolerance, result))
                return false;
        }
    }

#if NWB_RT_CSG_ENABLED
    return nwbCausticCollectCsgEvents(config, g_NwbCausticSwMaterialContextSlots.materialSlots.w, origin, direction, minimum, maximum, reverse, events, eventCount, boundary, tolerance, result);
#else
    return true;
#endif
}


// Per-instance traversal refines the nearest triangle hit.
void nwbCausticSwInstanceClosest(const uint instanceIndex, const float3 worldOrigin, const float3 worldDirection, const float tMin, inout NwbCausticHit closest){
    const NwbSwBvhInstance instance = nwbHeapCausticSwInstances(g_NwbCausticSwMaterialContextSlots.sceneSlots.y)[instanceIndex];
    const NwbRtInstanceMaterial material = nwbHeapRtInstanceMaterials(g_NwbCausticSwMaterialContextSlots.sceneSlots.z)[instanceIndex];
#if NWB_RT_CSG_ENABLED
    if((material.flags & NWB_RT_INSTANCE_MATERIAL_FLAG_CSG_SHADOW) != 0u)
        return;
#endif
    const bool refractive = (material.flags & NWB_RT_INSTANCE_MATERIAL_FLAG_REFRACTIVE) != 0u;
    const float3 origin = nwbSwBvhTransformPoint(instance, worldOrigin);
    const float3 direction = nwbSwBvhTransformVector(instance, worldDirection);
    const float3 invDir = nwbBvhSafeInvDir(direction);

    // Near-first descent tightens closest.t before far-subtree tests.
    uint stack[NWB_CAUSTIC_SW_MESH_STACK_SIZE];
    stack[0u] = 0u;
    uint stackPointer = 1u;

    [loop] while(stackPointer > 0u){
        --stackPointer;
        NwbBvhNode node = nwbHeapBvhNodes(material.nodeSlot)[stack[stackPointer]];
        if(!nwbBvhRayAabb(origin, invDir, tMin, closest.t, node.aabbMin, node.aabbMax, 0.0))
            continue;

        [loop] while(true){
            if(nwbBvhNodeIsLeaf(node)){
                const uint triangle = nwbBvhLeafPrimitive(node);
                const uint indexBase = triangle * NWB_RAYTRACE_TRIANGLE_CORNER_COUNT;
                const uint3 triIndices = nwbHeapRawBuffer(material.indexSlot).Load3(indexBase * NWB_RAYTRACE_INDEX_STRIDE_BYTES);
                const uint i0 = triIndices.x;
                const uint i1 = triIndices.y;
                const uint i2 = triIndices.z;
                const float3 v0 = asfloat(nwbHeapRawBuffer(material.positionSlot).Load3(i0 * NWB_RAYTRACE_POSITION_STRIDE_BYTES));
                const float3 v1 = asfloat(nwbHeapRawBuffer(material.positionSlot).Load3(i1 * NWB_RAYTRACE_POSITION_STRIDE_BYTES));
                const float3 v2 = asfloat(nwbHeapRawBuffer(material.positionSlot).Load3(i2 * NWB_RAYTRACE_POSITION_STRIDE_BYTES));
                const NwbRayTriangleHit triangleHit = nwbRayTriangleMollerTrumbore(origin, direction, tMin, closest.t, v0, v1, v2);
                if(triangleHit.hit){
                    closest.hit = true;
                    closest.t = triangleHit.t;
                    closest.isRefractive = refractive;
                    closest.instance = instanceIndex;
                    closest.worldPos = worldOrigin + worldDirection * triangleHit.t;
                }
                break;
            }
            else{
                const NwbBvhNode leftNode = nwbHeapBvhNodes(material.nodeSlot)[node.leftChild];
                const uint right = nwbBvhNodeRightChildIndex(node);
                const NwbBvhNode rightNode = nwbHeapBvhNodes(material.nodeSlot)[right];
                NwbBvhNode descendNode;
                uint farChild;
                bool bothHit;
                if(nwbBvhPickNearChild(origin, invDir, tMin, closest.t, node, leftNode, right, rightNode, descendNode, farChild, bothHit)){
                    if(bothHit && stackPointer < NWB_CAUSTIC_SW_MESH_STACK_SIZE){
                        stack[stackPointer] = farChild;
                        ++stackPointer;
                    }
                    node = descendNode;
                    continue;
                }
                break;
            }
        }
    }
}

// Software closest-hit refines scene-BVH candidates through mesh BVHs.
NwbCausticHit nwbCausticTraceClosest(float3 origin, float3 direction, float tMin, float tMax){
    NwbCausticHit closest = (NwbCausticHit)0;
    closest.hit = false;
    closest.isRefractive = false;
    closest.t = tMax;
    closest.worldPos = float3(0.0, 0.0, 0.0);

    if(g_NwbCausticSwPushConstants.instanceCount == 0u)
        return closest;

    const float3 invDir = nwbBvhSafeInvDir(direction);

    uint stack[NWB_CAUSTIC_SW_SCENE_STACK_SIZE];
    stack[0u] = 0u;
    uint stackPointer = 1u;

    bool sceneOverflow = false;
    [loop] while(stackPointer > 0u && !sceneOverflow){
        --stackPointer;
        NwbBvhNode node = nwbHeapBvhNodes(g_NwbCausticSwMaterialContextSlots.sceneSlots.x)[stack[stackPointer]];
        if(!nwbBvhRayAabb(origin, invDir, tMin, closest.t, node.aabbMin, node.aabbMax, 0.0))
            continue;

        [loop] while(true){
            if(nwbBvhNodeIsLeaf(node)){
                nwbCausticSwInstanceClosest(nwbBvhLeafPrimitive(node), origin, direction, tMin, closest);
                break;
            }
            else{
                const NwbBvhNode leftNode = nwbHeapBvhNodes(g_NwbCausticSwMaterialContextSlots.sceneSlots.x)[node.leftChild];
                const uint right = nwbBvhNodeRightChildIndex(node);
                const NwbBvhNode rightNode = nwbHeapBvhNodes(g_NwbCausticSwMaterialContextSlots.sceneSlots.x)[right];
                NwbBvhNode descendNode;
                uint farChild;
                bool bothHit;
                if(nwbBvhPickNearChild(origin, invDir, tMin, closest.t, node, leftNode, right, rightNode, descendNode, farChild, bothHit)){
                    if(bothHit){
                        if(stackPointer == NWB_CAUSTIC_SW_SCENE_STACK_SIZE){
                            sceneOverflow = true;
                            break;
                        }
                        stack[stackPointer] = farChild;
                        ++stackPointer;
                    }
                    node = descendNode;
                    continue;
                }
                break;
            }
        }
    }

    if(sceneOverflow){
        [loop] for(uint instance = 0u; instance < g_NwbCausticSwPushConstants.instanceCount; ++instance)
            nwbCausticSwInstanceClosest(instance, origin, direction, tMin, closest);
    }

#if NWB_RT_CSG_ENABLED
    // Match hardware closest-hit ordering: the current CSG receivers refine the nearest ordinary surface.
    const uint contextSlot = g_NwbCausticSwMaterialContextSlots.materialSlots.w;
    const uint materialSlot = g_NwbCausticSwMaterialContextSlots.sceneSlots.z;
    const uint activeOffset = nwbHeapRawBuffer(contextSlot).Load(NWB_CSG_RAY_CONTEXT_ACTIVE_BASE_OFFSET);
    const uint activeCount = nwbHeapRawBuffer(contextSlot).Load(NWB_CSG_RAY_CONTEXT_ACTIVE_COUNT_OFFSET);
    for(uint index = 0u; index < activeCount; ++index){
        const uint instance = nwbHeapRawBuffer(contextSlot).Load(activeOffset + index * NWB_CSG_RAY_ACTIVE_INDEX_BYTES);
        const NwbCausticHit candidate = nwbCausticCsgInstanceClosest(contextSlot, materialSlot, instance, origin, direction, tMin, closest.t);
        if(candidate.failure != 0u)
            return candidate;
        if(candidate.hit)
            closest = candidate;
    }
#endif
    return closest;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_CAUSTIC_SPLAT_VIEW nwbCausticSwView()
#define NWB_CAUSTIC_SPLAT_PUSH g_NwbCausticSwPushConstants
#define NWB_CAUSTIC_SPLAT_GBUFFER_DEPTH nwbCausticSwGbufferDepth()
#define NWB_CAUSTIC_SPLAT_GBUFFER_WORLD_POSITION nwbCausticSwGbufferWorldPosition()
#define NWB_CAUSTIC_SPLAT_ACCUMULATOR nwbHeapStorageImage2DArrayUint(g_NwbCausticSwPushConstants.accumulatorStorageSlot)
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

    const float2 cell = nwbCausticJitteredTemporalCell(photonIndex, g_NwbCausticSwPushConstants.gridSide, g_NwbCausticSwPushConstants.frameIndex, g_NwbCausticSwPushConstants.temporalPhaseCount);

    const NwbCausticEmission emission = nwbCausticEmitPhotonAtTarget(lightDir, targetAabbMin, targetAabbMax, cell);
    if(!emission.valid)
        return;

    const NwbCausticPhoton photon = nwbCausticCreatePhoton(
        emission, nwbCausticLightColor(light.colorIntensity), g_NwbCausticSwPushConstants.emissionTargetCount,
        g_NwbCausticSwPushConstants.photonCount, light.params.y >= NWB_SCENE_LIGHT_TYPE_DIRECTIONAL_MAX, light.position.xyz
    );

    const NwbCausticSplat splat = nwbCausticTracePhoton(photon);
    if(!splat.valid)
        return;

    nwbCausticSplat(splat.worldPos, splat.flux);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

