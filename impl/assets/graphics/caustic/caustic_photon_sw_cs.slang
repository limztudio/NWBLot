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

#define g_NwbCausticSwBindlessResources g_NwbHeapDeferredBindlessResources[g_NwbCausticSwPushConstants.deferredResourcesHeapSlot]
#define g_NwbCausticSwMaterialContextSlots nwbHeapMaterialContextSlots(g_NwbCausticSwPushConstants.materialContextSlotsHeapSlot)
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
    return 0u;
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
#pragma warning(disable: 39001)
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

bool nwbCausticSceneBounds(out float3 boundsMin, out float3 boundsMax, out uint flags){
    boundsMin = float3(g_NwbCausticSwPushConstants.sceneBoundsMinX, g_NwbCausticSwPushConstants.sceneBoundsMinY, g_NwbCausticSwPushConstants.sceneBoundsMinZ);
    boundsMax = float3(g_NwbCausticSwPushConstants.sceneBoundsMaxX, g_NwbCausticSwPushConstants.sceneBoundsMaxY, g_NwbCausticSwPushConstants.sceneBoundsMaxZ);
    flags = g_NwbCausticSwPushConstants.sceneBoundsFlags;
    return flags != NWB_CAUSTIC_SCENE_BOUNDS_INVALID;
}

interface INwbCausticSwBoundaryVisitor{
    [mutating] bool visit(const uint instance, const uint primitive, const float distance, const float3 normal, inout NwbOpticalTraceResult result);
};

struct NwbCausticSwCollectVisitor : INwbCausticSwBoundaryVisitor{
    NwbOpticalTraceConfig config;
    float3 origin;
    float3 direction;
    bool reverse;
    NwbOpticalBootstrapEvent events[NWB_OPTICAL_MAX_CANDIDATE_EVENTS];
    uint eventCount;
    NwbCausticBoundary boundary;
    float tolerance;

    [mutating] bool visit(const uint instance, const uint primitive, const float distance, const float3 normal, inout NwbOpticalTraceResult result){
        return nwbCausticAppendBoundary(config, origin, direction, distance, instance, primitive, normal, reverse, events, eventCount, boundary, tolerance, result);
    }
};

struct NwbCausticSwBoundaryVisitor : INwbCausticSwBoundaryVisitor{
    NwbOpticalTraceConfig config;
    float3 origin;
    float3 direction;
    uint expectedInstance;
    bool hasFirst;
    NwbOpticalBootstrapEvent firstEvent;
    uint candidateCount;
    NwbCausticBoundary boundary;
    NwbOpticalInstance optical;

    [mutating] bool visit(const uint instance, const uint primitive, const float distance, const float3 normal, inout NwbOpticalTraceResult result){
        return nwbCausticVisitBoundaryCandidate(config, origin, direction, distance, instance, primitive, normal, expectedInstance, hasFirst, firstEvent, candidateCount, boundary, optical, result);
    }
};

bool nwbCausticSwInstanceWalk<T : INwbCausticSwBoundaryVisitor>(
    const uint instanceIndex, const float3 worldOrigin, const float3 worldDirection, const float tMin, const float maximum,
    __ref T visitor, inout NwbOpticalTraceResult result
){
    const NwbSwBvhInstance instance = nwbHeapSwBvhInstances(g_NwbCausticSwMaterialContextSlots.sceneSlots.y)[instanceIndex];
    const NwbRtInstanceMaterial material = nwbHeapRtInstanceMaterials(g_NwbCausticSwMaterialContextSlots.sceneSlots.z)[instanceIndex];
#if NWB_RT_CSG_ENABLED
    if((material.flags & NWB_RT_INSTANCE_MATERIAL_FLAG_CSG_SHADOW) != 0u)
        return true;
#endif
    const bool refractive = (material.flags & NWB_RT_INSTANCE_MATERIAL_FLAG_REFRACTIVE) != 0u;
    if(!refractive)
        return true;
    const float3 origin = nwbSwBvhTransformPoint(instance, worldOrigin);
    const float3 direction = nwbSwBvhTransformVector(instance, worldDirection);
    const float3 invDir = nwbBvhSafeInvDir(direction);
    const float3 negOriginInvDir = -origin * invDir;

    NwbBvhNode rootNode = nwbHeapBvhNodes(material.nodeSlot)[0u];
    float unusedRootEntry;
    if(!nwbBvhRayAabbIntervalFma(invDir, negOriginInvDir, tMin, maximum, rootNode.aabbMin, rootNode.aabbMax, unusedRootEntry))
        return true;

    uint stack[NWB_CAUSTIC_SW_MESH_STACK_SIZE];
    stack[0u] = 0u;
    NwbBvhNode node = rootNode;
    uint stackPointer = 0u;

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
                if(!visitor.visit(instanceIndex, triangle, triangleHit.t, normal, result))
                    return false;
            }
            if(stackPointer == 0u)
                break;
            --stackPointer;
            node = nwbHeapBvhNodes(material.nodeSlot)[stack[stackPointer]];
        }
        else{
            const NwbBvhNode leftNode = nwbHeapBvhNodes(material.nodeSlot)[node.leftChild];
            const uint right = nwbBvhNodeRightChildIndex(node);
            const NwbBvhNode rightNode = nwbHeapBvhNodes(material.nodeSlot)[right];
            NwbBvhNode descendNode;
            uint farChild;
            float unusedFarEntryT;
            bool bothHit;
            if(nwbBvhPickNearChildFma(invDir, negOriginInvDir, tMin, maximum, node, leftNode, right, rightNode, descendNode, farChild, unusedFarEntryT, bothHit)){
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
            if(stackPointer == 0u)
                break;
            --stackPointer;
            node = nwbHeapBvhNodes(material.nodeSlot)[stack[stackPointer]];
        }
    }
    return true;
}

bool nwbCausticSwWalkRefractiveInstances<T : INwbCausticSwBoundaryVisitor>(
    const float3 origin, const float3 direction, const float minimum, const float maximum,
    __ref T visitor, inout bool sceneOverflow, inout NwbOpticalTraceResult result
){
    sceneOverflow = false;
    if(g_NwbCausticSwPushConstants.instanceCount == 0u)
        return true;
    const float3 invDir = nwbBvhSafeInvDir(direction);
    const float3 negOriginInvDir = -origin * invDir;

    NwbBvhNode node = nwbHeapBvhNodesUniform(g_NwbCausticSwMaterialContextSlots.sceneSlots.x)[0u];
    float unusedRootEntry;
    if(!nwbBvhRayAabbIntervalFma(invDir, negOriginInvDir, minimum, maximum, node.aabbMin, node.aabbMax, unusedRootEntry))
        return true;

    uint stack[NWB_CAUSTIC_SW_SCENE_STACK_SIZE];
    stack[0u] = 0u;
    uint stackPointer = 0u;

    [loop] while(!sceneOverflow){
        if(nwbBvhNodeIsLeaf(node)){
            if(!nwbCausticSwInstanceWalk(nwbBvhLeafPrimitive(node), origin, direction, minimum, maximum, visitor, result))
                return false;
            if(stackPointer == 0u)
                break;
            --stackPointer;
            node = nwbHeapBvhNodesUniform(g_NwbCausticSwMaterialContextSlots.sceneSlots.x)[stack[stackPointer]];
        }
        else{
            const NwbBvhNode leftNode = nwbHeapBvhNodesUniform(g_NwbCausticSwMaterialContextSlots.sceneSlots.x)[node.leftChild];
            const uint right = nwbBvhNodeRightChildIndex(node);
            const NwbBvhNode rightNode = nwbHeapBvhNodesUniform(g_NwbCausticSwMaterialContextSlots.sceneSlots.x)[right];
            NwbBvhNode descendNode;
            uint farChild;
            float unusedFarEntryT;
            bool bothHit;
            if(nwbBvhPickNearChildFma(invDir, negOriginInvDir, minimum, maximum, node, leftNode, right, rightNode, descendNode, farChild, unusedFarEntryT, bothHit)){
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
            if(stackPointer == 0u)
                break;
            --stackPointer;
            node = nwbHeapBvhNodesUniform(g_NwbCausticSwMaterialContextSlots.sceneSlots.x)[stack[stackPointer]];
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

    NwbCausticSwCollectVisitor visitor = (NwbCausticSwCollectVisitor)0;
    visitor.config = config;
    visitor.origin = origin;
    visitor.direction = direction;
    visitor.reverse = reverse;
    visitor.tolerance = tolerance;
    bool sceneOverflow = false;
    const bool walked = nwbCausticSwWalkRefractiveInstances(origin, direction, minimum, maximum, visitor, sceneOverflow, result);
    if(!walked)
        return false;
    if(sceneOverflow){
        visitor.eventCount = 0u;
        visitor.boundary = (NwbCausticBoundary)0;
        visitor.tolerance = NWB_OPTICS_POSITION_TOLERANCE_SCALE * nwbOpticsPositionUncertainty(origin);
        [loop] for(uint instance = 0u; instance < g_NwbCausticSwPushConstants.instanceCount; ++instance){
            if(!nwbCausticSwInstanceWalk(instance, origin, direction, minimum, maximum, visitor, result))
                return false;
        }
    }
    events = visitor.events;
    eventCount = visitor.eventCount;
    boundary = visitor.boundary;
    tolerance = visitor.tolerance;

#if NWB_RT_CSG_ENABLED
    return nwbCausticCollectCsgEvents(config, g_NwbCausticSwMaterialContextSlots.materialSlots.w, origin, direction, minimum, maximum, reverse, events, eventCount, boundary, tolerance, result);
#else
    return true;
#endif
}

bool nwbCausticCheckRefractiveBoundary(
    const NwbOpticalTraceConfig config,
    const float3 origin,
    const float3 direction,
    const float minimum,
    const float maximum,
    const uint expectedInstance,
    inout bool hasFirst,
    inout NwbOpticalBootstrapEvent firstEvent,
    inout uint candidateCount,
    inout NwbCausticBoundary boundary,
    inout NwbOpticalInstance outOptical,
    inout NwbOpticalTraceResult result
){
    if(!nwbOpticalConsumeQuery(config, result))
        return false;

    NwbCausticSwBoundaryVisitor visitor = (NwbCausticSwBoundaryVisitor)0;
    visitor.config = config;
    visitor.origin = origin;
    visitor.direction = direction;
    visitor.expectedInstance = expectedInstance;
    visitor.hasFirst = hasFirst;
    visitor.firstEvent = firstEvent;
    visitor.candidateCount = candidateCount;
    visitor.boundary = boundary;
    visitor.optical = outOptical;
    bool sceneOverflow = false;
    const bool walked = nwbCausticSwWalkRefractiveInstances(origin, direction, minimum, maximum, visitor, sceneOverflow, result);
    if(!walked)
        return false;
    if(sceneOverflow){
        visitor.hasFirst = hasFirst;
        visitor.firstEvent = firstEvent;
        visitor.candidateCount = candidateCount;
        visitor.boundary = boundary;
        visitor.optical = outOptical;
        [loop] for(uint instance = 0u; instance < g_NwbCausticSwPushConstants.instanceCount; ++instance){
            if(!nwbCausticSwInstanceWalk(instance, origin, direction, minimum, maximum, visitor, result))
                return false;
        }
    }
    hasFirst = visitor.hasFirst;
    firstEvent = visitor.firstEvent;
    candidateCount = visitor.candidateCount;
    boundary = visitor.boundary;
    outOptical = visitor.optical;

#if NWB_RT_CSG_ENABLED
    return nwbCausticCheckCsgBoundary(config, g_NwbCausticSwMaterialContextSlots.materialSlots.w, origin, direction, minimum, maximum, expectedInstance, hasFirst, firstEvent, candidateCount, boundary, outOptical, result);
#else
    return true;
#endif
}

void nwbCausticSwInstanceClosest(const uint instanceIndex, const float3 worldOrigin, const float3 worldDirection, const float tMin, inout NwbCausticHit closest){
    const NwbSwBvhInstance instance = nwbHeapSwBvhInstances(g_NwbCausticSwMaterialContextSlots.sceneSlots.y)[instanceIndex];
    const NwbRtInstanceMaterial material = nwbHeapRtInstanceMaterials(g_NwbCausticSwMaterialContextSlots.sceneSlots.z)[instanceIndex];
#if NWB_RT_CSG_ENABLED
    if((material.flags & NWB_RT_INSTANCE_MATERIAL_FLAG_CSG_SHADOW) != 0u)
        return;
#endif
    const bool refractive = (material.flags & NWB_RT_INSTANCE_MATERIAL_FLAG_REFRACTIVE) != 0u;
    const float3 origin = nwbSwBvhTransformPoint(instance, worldOrigin);
    const float3 direction = nwbSwBvhTransformVector(instance, worldDirection);
    const float3 invDir = nwbBvhSafeInvDir(direction);
    const float3 negOriginInvDir = -origin * invDir;

    NwbBvhNode node = nwbHeapBvhNodes(material.nodeSlot)[0u];
    float unusedRootEntry;
    if(!nwbBvhRayAabbIntervalFma(invDir, negOriginInvDir, tMin, closest.t, node.aabbMin, node.aabbMax, unusedRootEntry))
        return;

    uint stack[NWB_CAUSTIC_SW_MESH_STACK_SIZE];
    stack[0u] = 0u;
    uint stackPointer = 0u;

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
        }
        else{
            const NwbBvhNode leftNode = nwbHeapBvhNodes(material.nodeSlot)[node.leftChild];
            const uint right = nwbBvhNodeRightChildIndex(node);
            const NwbBvhNode rightNode = nwbHeapBvhNodes(material.nodeSlot)[right];
            NwbBvhNode descendNode;
            uint farChild;
            float farEntryT;
            bool bothHit;
            if(nwbBvhPickNearChildFma(invDir, negOriginInvDir, tMin, closest.t, node, leftNode, right, rightNode, descendNode, farChild, farEntryT, bothHit)){
                if(bothHit && stackPointer < NWB_CAUSTIC_SW_MESH_STACK_SIZE){
                    stack[stackPointer] = farChild;
                    ++stackPointer;
                }
                node = descendNode;
                continue;
            }
        }
        bool popped = false;
        while(stackPointer > 0u){
            --stackPointer;
            node = nwbHeapBvhNodes(material.nodeSlot)[stack[stackPointer]];
            float popEntryT;
            if(nwbBvhRayAabbIntervalFma(invDir, negOriginInvDir, tMin, closest.t, node.aabbMin, node.aabbMax, popEntryT)){
                popped = true;
                break;
            }
        }
        if(!popped)
            break;
    }
}

NwbCausticHit nwbCausticTraceClosest(float3 origin, float3 direction, float tMin, float tMax){
    NwbCausticHit closest = (NwbCausticHit)0;
    closest.hit = false;
    closest.isRefractive = false;
    closest.t = tMax;
    closest.worldPos = float3(0.0, 0.0, 0.0);

    if(g_NwbCausticSwPushConstants.instanceCount == 0u)
        return closest;

    const float3 invDir = nwbBvhSafeInvDir(direction);
    const float3 negOriginInvDir = -origin * invDir;

    NwbBvhNode node = nwbHeapBvhNodesUniform(g_NwbCausticSwMaterialContextSlots.sceneSlots.x)[0u];
    float unusedRootEntry;
    if(nwbBvhRayAabbIntervalFma(invDir, negOriginInvDir, tMin, closest.t, node.aabbMin, node.aabbMax, unusedRootEntry)){
        uint stack[NWB_CAUSTIC_SW_SCENE_STACK_SIZE];
        stack[0u] = 0u;
        uint stackPointer = 0u;

        bool sceneOverflow = false;
        [loop] while(!sceneOverflow){
            if(nwbBvhNodeIsLeaf(node)){
                nwbCausticSwInstanceClosest(nwbBvhLeafPrimitive(node), origin, direction, tMin, closest);
            }
            else{
                const NwbBvhNode leftNode = nwbHeapBvhNodesUniform(g_NwbCausticSwMaterialContextSlots.sceneSlots.x)[node.leftChild];
                const uint right = nwbBvhNodeRightChildIndex(node);
                const NwbBvhNode rightNode = nwbHeapBvhNodesUniform(g_NwbCausticSwMaterialContextSlots.sceneSlots.x)[right];
                NwbBvhNode descendNode;
                uint farChild;
                float farEntryT;
                bool bothHit;
                if(nwbBvhPickNearChildFma(invDir, negOriginInvDir, tMin, closest.t, node, leftNode, right, rightNode, descendNode, farChild, farEntryT, bothHit)){
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
            }
            bool popped = false;
            while(stackPointer > 0u){
                --stackPointer;
                node = nwbHeapBvhNodesUniform(g_NwbCausticSwMaterialContextSlots.sceneSlots.x)[stack[stackPointer]];
                float popEntryT;
                if(nwbBvhRayAabbIntervalFma(invDir, negOriginInvDir, tMin, closest.t, node.aabbMin, node.aabbMax, popEntryT)){
                    popped = true;
                    break;
                }
            }
            if(!popped)
                break;
        }

        if(sceneOverflow){
            [loop] for(uint instance = 0u; instance < g_NwbCausticSwPushConstants.instanceCount; ++instance)
                nwbCausticSwInstanceClosest(instance, origin, direction, tMin, closest);
        }
    }

#if NWB_RT_CSG_ENABLED
    const uint contextSlot = g_NwbCausticSwMaterialContextSlots.materialSlots.w;
    const uint materialSlot = g_NwbCausticSwMaterialContextSlots.sceneSlots.z;
    const uint activeOffset = nwbHeapRawBufferUniform(contextSlot).Load(NWB_CSG_RAY_CONTEXT_ACTIVE_BASE_OFFSET);
    const uint activeCount = nwbHeapRawBufferUniform(contextSlot).Load(NWB_CSG_RAY_CONTEXT_ACTIVE_COUNT_OFFSET);
    for(uint index = 0u; index < activeCount; ++index){
        const uint instance = nwbHeapRawBufferUniform(contextSlot).Load(activeOffset + index * NWB_CSG_RAY_ACTIVE_INDEX_BYTES);
        const NwbCausticHit candidate = nwbCausticCsgInstanceClosest(contextSlot, materialSlot, instance, origin, direction, tMin, closest.t);
        if(candidate.failure != 0u)
            return candidate;
        if(candidate.hit)
            closest = candidate;
    }
#endif
    return closest;
}

#if !NWB_RT_CSG_ENABLED
NwbCausticHit nwbCausticTraceBounceZero(float3 origin, float3 direction, float tMin, float tMax){
    return nwbCausticTraceClosest(origin, direction, tMin, tMax);
}
#endif


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
    uint targetIndex;
    uint localIndex;
    uint localCount;
    uint combo;
    uint totalCombos;
    if(!nwbCausticSelectLightAndTarget(photonIndex, g_NwbCausticSwPushConstants.photonCount, g_NwbCausticSwPushConstants.emissionTargetCount, light, targetIndex, localIndex, localCount, combo, totalCombos))
        return;

    const float3 lightDir = nwbSafeNormalize(-light.direction.xyz, float3(0.0, 0.0, -1.0));

    const NwbCausticEmissionTarget target = nwbCausticSwEmissionTargets()[targetIndex];
    const float3 targetAabbMin = target.aabbMin.xyz;
    const float3 targetAabbMax = target.aabbMax.xyz;

    const float2 cell = totalCombos == 1u
        ? NwbCausticPhotonEmission.temporalCell(photonIndex, g_NwbCausticSwPushConstants.gridSide, g_NwbCausticSwPushConstants.frameIndex, g_NwbCausticSwPushConstants.temporalPhaseCount)
        : NwbCausticPhotonEmission.stratifiedBlockCell(localIndex, localCount, g_NwbCausticSwPushConstants.frameIndex, g_NwbCausticSwPushConstants.temporalPhaseCount, combo);

    const NwbCausticEmission emission = NwbCausticPhotonEmission.emitAtTarget(lightDir, targetAabbMin, targetAabbMax, cell);
    if(!emission.valid)
        return;

    const NwbCausticPhoton photon = NwbCausticPhotonEmission.createPhoton(
        emission, NwbCausticPhotonEmission.lightColor(light.colorIntensity), g_NwbCausticSwPushConstants.emissionTargetCount,
        g_NwbCausticSwPushConstants.photonCount, light.params.y >= NWB_SCENE_LIGHT_TYPE_DIRECTIONAL_MAX, light.position.xyz
    );

    const NwbCausticSplat splat = nwbCausticTracePhoton(photon);
    if(!splat.valid)
        return;

    nwbCausticSplat(splat.worldPos, splat.flux);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

