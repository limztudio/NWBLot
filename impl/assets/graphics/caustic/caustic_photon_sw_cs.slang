// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#ifdef NWB_CAUSTIC_PHOTON_TRANSPORT
#error "NWB_CAUSTIC_PHOTON_TRANSPORT is fixed by this photon shader source"
#endif
#define NWB_CAUSTIC_PHOTON_TRANSPORT 1

#include "sw_binding_slots.h"
#include "caustic_photon_common.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "../bvh/bvh_common.slangi"
#include "../bvh/ray_intersect.slangi"
#include "../bvh/sw_instance.slangi"
#include "../bvh/bindless_heap_bvh.slangi"

#include "caustic_trace.slangi"
#if NWB_RT_CSG_ENABLED
#define NWB_CSG_RAY_HARDWARE 0
#define NWB_CSG_RAY_SW_STACK_SIZE NWB_CAUSTIC_SW_MESH_STACK_SIZE
#include "csg_hit.slangi"
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool nwbCausticSwInstanceWalk<T : INwbCausticBoundaryVisitor>(
    const uint instanceIndex, const float3 worldOrigin, const float3 worldDirection, const float tMin, const float maximum,
    __ref T visitor, inout NwbOpticalTraceResult result
){
    const NwbSwBvhInstance instance = nwbHeapSwBvhInstances(g_NwbCausticMaterialContextSlots.sceneSlots.y)[instanceIndex];
    const NwbRtInstanceMaterial material = nwbHeapRtInstanceMaterials(g_NwbCausticMaterialContextSlots.sceneSlots.z)[instanceIndex];
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

bool nwbCausticSwWalkRefractiveInstances<T : INwbCausticBoundaryVisitor>(
    const float3 origin, const float3 direction, const float minimum, const float maximum,
    __ref T visitor, inout bool sceneOverflow, inout NwbOpticalTraceResult result
){
    sceneOverflow = false;
    if(g_NwbCausticPushConstants.instanceCount == 0u)
        return true;
    const float3 invDir = nwbBvhSafeInvDir(direction);
    const float3 negOriginInvDir = -origin * invDir;

    NwbBvhNode node = nwbHeapBvhNodesUniform(g_NwbCausticMaterialContextSlots.sceneSlots.x)[0u];
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
            node = nwbHeapBvhNodesUniform(g_NwbCausticMaterialContextSlots.sceneSlots.x)[stack[stackPointer]];
        }
        else{
            const NwbBvhNode leftNode = nwbHeapBvhNodesUniform(g_NwbCausticMaterialContextSlots.sceneSlots.x)[node.leftChild];
            const uint right = nwbBvhNodeRightChildIndex(node);
            const NwbBvhNode rightNode = nwbHeapBvhNodesUniform(g_NwbCausticMaterialContextSlots.sceneSlots.x)[right];
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
            node = nwbHeapBvhNodesUniform(g_NwbCausticMaterialContextSlots.sceneSlots.x)[stack[stackPointer]];
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

    NwbCausticCollectVisitor visitor = (NwbCausticCollectVisitor)0;
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
        [loop] for(uint instance = 0u; instance < g_NwbCausticPushConstants.instanceCount; ++instance){
            if(!nwbCausticSwInstanceWalk(instance, origin, direction, minimum, maximum, visitor, result))
                return false;
        }
    }
#if NWB_RT_CSG_ENABLED
    if(!nwbCausticWalkCsgRefractiveCrossings(config, g_NwbCausticMaterialContextSlots.materialSlots.w, origin, direction, minimum, maximum, visitor, result))
        return false;
#endif
    events = visitor.events;
    eventCount = visitor.eventCount;
    boundary = visitor.boundary;
    tolerance = visitor.tolerance;
    return true;
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

    NwbCausticCheckBoundaryVisitor visitor = (NwbCausticCheckBoundaryVisitor)0;
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
        [loop] for(uint instance = 0u; instance < g_NwbCausticPushConstants.instanceCount; ++instance){
            if(!nwbCausticSwInstanceWalk(instance, origin, direction, minimum, maximum, visitor, result))
                return false;
        }
    }
#if NWB_RT_CSG_ENABLED
    if(!nwbCausticWalkCsgRefractiveCrossings(config, g_NwbCausticMaterialContextSlots.materialSlots.w, origin, direction, minimum, maximum, visitor, result))
        return false;
#endif
    hasFirst = visitor.hasFirst;
    firstEvent = visitor.firstEvent;
    candidateCount = visitor.candidateCount;
    boundary = visitor.boundary;
    outOptical = visitor.optical;
    return true;
}

void nwbCausticSwInstanceClosest(const uint instanceIndex, const float3 worldOrigin, const float3 worldDirection, const float tMin, inout NwbCausticHit closest){
    const NwbSwBvhInstance instance = nwbHeapSwBvhInstances(g_NwbCausticMaterialContextSlots.sceneSlots.y)[instanceIndex];
    const NwbRtInstanceMaterial material = nwbHeapRtInstanceMaterials(g_NwbCausticMaterialContextSlots.sceneSlots.z)[instanceIndex];
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

    if(g_NwbCausticPushConstants.instanceCount == 0u)
        return closest;

    const float3 invDir = nwbBvhSafeInvDir(direction);
    const float3 negOriginInvDir = -origin * invDir;

    NwbBvhNode node = nwbHeapBvhNodesUniform(g_NwbCausticMaterialContextSlots.sceneSlots.x)[0u];
    float unusedRootEntry;
    const bool rootHit = nwbBvhRayAabbIntervalFma(invDir, negOriginInvDir, tMin, closest.t, node.aabbMin, node.aabbMax, unusedRootEntry);
    uint stack[NWB_CAUSTIC_SW_SCENE_STACK_SIZE];
    stack[0u] = 0u;
    uint stackPointer = 0u;

    bool sceneOverflow = false;
    [loop] while(rootHit && !sceneOverflow){
        if(nwbBvhNodeIsLeaf(node)){
            nwbCausticSwInstanceClosest(nwbBvhLeafPrimitive(node), origin, direction, tMin, closest);
        }
        else{
            const NwbBvhNode leftNode = nwbHeapBvhNodesUniform(g_NwbCausticMaterialContextSlots.sceneSlots.x)[node.leftChild];
            const uint right = nwbBvhNodeRightChildIndex(node);
            const NwbBvhNode rightNode = nwbHeapBvhNodesUniform(g_NwbCausticMaterialContextSlots.sceneSlots.x)[right];
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
            node = nwbHeapBvhNodesUniform(g_NwbCausticMaterialContextSlots.sceneSlots.x)[stack[stackPointer]];
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
        [loop] for(uint instance = 0u; instance < g_NwbCausticPushConstants.instanceCount; ++instance)
            nwbCausticSwInstanceClosest(instance, origin, direction, tMin, closest);
    }

#if NWB_RT_CSG_ENABLED
    closest = nwbCausticCsgClosestAll(g_NwbCausticMaterialContextSlots.materialSlots.w, g_NwbCausticMaterialContextSlots.sceneSlots.z, origin, direction, tMin, closest);
#endif
    return closest;
}

#if !NWB_RT_CSG_ENABLED
NwbCausticHit nwbCausticTraceBounceZero(float3 origin, float3 direction, float tMin, float tMax){
    return nwbCausticTraceClosest(origin, direction, tMin, tMax);
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_CAUSTIC_SW_GROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint photonIndex = dispatchThreadID.x;
    NwbCausticPhoton photon;
    if(!nwbCausticEmitPhoton(photonIndex, photon))
        return;

    const NwbCausticSplat splat = nwbCausticTracePhoton(photon);
    if(!splat.valid)
        return;

    nwbCausticSplat(splat.worldPos, splat.flux);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

