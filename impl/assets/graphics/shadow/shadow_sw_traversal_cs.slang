// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "sw_binding_slots.h"

#define NWB_SCENE_SHADING_SET NWB_SW_SHADOW_SET
#define NWB_SCENE_SHADING_BINDING NWB_SW_SHADOW_BINDING_SCENE_SHADING
#define NWB_SCENE_LIGHT_LIST_SET NWB_SW_SHADOW_SET
#define NWB_SCENE_LIGHT_LIST_BINDING NWB_SW_SHADOW_BINDING_LIGHT_LIST

#include "../scene/buffer.slangi"
#include "../bvh/bvh_common.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::binding(NWB_SW_SHADOW_BINDING_GBUFFER_WORLD_POSITION, NWB_SW_SHADOW_SET)]]
Texture2D<float4> g_NwbSwShadowGBufferWorldPosition;

[[vk::binding(NWB_SW_SHADOW_BINDING_GBUFFER_NORMAL, NWB_SW_SHADOW_SET)]]
Texture2D<float4> g_NwbSwShadowGBufferNormal;

[[vk::binding(NWB_SW_SHADOW_BINDING_GBUFFER_DEPTH, NWB_SW_SHADOW_SET)]]
Texture2D<float4> g_NwbSwShadowGBufferDepth;

[[vk::binding(NWB_SW_SHADOW_BINDING_SCENE_NODES, NWB_SW_SHADOW_SET)]]
StructuredBuffer<NwbBvhNode, Std430DataLayout> g_NwbSwShadowSceneNodes;

[[vk::binding(NWB_SW_SHADOW_BINDING_VISIBILITY_OUTPUT, NWB_SW_SHADOW_SET)]]
RWTexture2D<uint> g_NwbSwShadowVisibility;

// Per-instance record: the affine world->object transform (3 rows) + the slot into the parallel per-mesh
// descriptor arrays. Mirrors the C++ SceneSwBvhInstanceGpu (64 bytes).
struct NwbSwShadowInstance{
    float4 worldToObjectRow0;
    float4 worldToObjectRow1;
    float4 worldToObjectRow2;
    uint meshIndex;
    uint primitiveCount;
    uint pad0;
    uint pad1;
};

[[vk::binding(NWB_SW_SHADOW_BINDING_SCENE_INSTANCES, NWB_SW_SHADOW_SET)]]
StructuredBuffer<NwbSwShadowInstance, Std430DataLayout> g_NwbSwShadowInstances;

// Parallel per-mesh arrays (slot k = mesh k): triangle BVH nodes + raw position / index byte buffers.
[[vk::binding(NWB_SW_SHADOW_BINDING_MESH_NODES, NWB_SW_SHADOW_SET)]]
StructuredBuffer<NwbBvhNode, Std430DataLayout> g_NwbSwShadowMeshNodes[NWB_SW_SHADOW_MAX_MESHES];

[[vk::binding(NWB_SW_SHADOW_BINDING_MESH_POSITIONS, NWB_SW_SHADOW_SET)]]
ByteAddressBuffer g_NwbSwShadowMeshPositions[NWB_SW_SHADOW_MAX_MESHES];

[[vk::binding(NWB_SW_SHADOW_BINDING_MESH_INDICES, NWB_SW_SHADOW_SET)]]
ByteAddressBuffer g_NwbSwShadowMeshIndices[NWB_SW_SHADOW_MAX_MESHES];

struct NwbSwShadowPushConstants{
    uint width;
    uint height;
    uint instanceCount;
    uint pad0;
};

[[vk::push_constant]]
ConstantBuffer<NwbSwShadowPushConstants> g_NwbSwShadowPushConstants;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Slab ray/AABB overlap test over the [tMin, tMax] interval. invDir may contain +/-inf for axis-aligned
// directions; the min/max ordering keeps that well-defined for all but the measure-zero on-slab case.
bool nwbSwShadowRayAabb(const float3 origin, const float3 invDir, const float tMin, const float tMax, const float3 boxMin, const float3 boxMax){
    const float3 t0 = (boxMin - origin) * invDir;
    const float3 t1 = (boxMax - origin) * invDir;
    const float3 tSmall = min(t0, t1);
    const float3 tLarge = max(t0, t1);
    const float tNear = max(max(tSmall.x, tSmall.y), max(tSmall.z, tMin));
    const float tFar = min(min(tLarge.x, tLarge.y), min(tLarge.z, tMax));
    return tNear <= tFar;
}

// Reciprocal of the ray direction with each component nudged off exactly zero, so an axis-aligned ray gets
// a large-but-finite slope instead of +/-inf. That keeps the slab test from forming inf*0 = NaN when the
// ray origin lies exactly on a box face (a NaN there compares false and would silently drop the shadow).
float3 nwbSwShadowSafeInvDir(const float3 direction){
    const float epsilon = 1e-8;
    const float3 safe = float3(
        abs(direction.x) > epsilon ? direction.x : (direction.x < 0.0 ? -epsilon : epsilon),
        abs(direction.y) > epsilon ? direction.y : (direction.y < 0.0 ? -epsilon : epsilon),
        abs(direction.z) > epsilon ? direction.z : (direction.z < 0.0 ? -epsilon : epsilon)
    );
    return 1.0 / safe;
}

// Affine world->object transform of a point (row-major 3x4; the engine SIMDMatrix rows map onto the rows).
float3 nwbSwShadowTransformPoint(const NwbSwShadowInstance instance, const float3 point){
    return float3(
        dot(instance.worldToObjectRow0.xyz, point) + instance.worldToObjectRow0.w,
        dot(instance.worldToObjectRow1.xyz, point) + instance.worldToObjectRow1.w,
        dot(instance.worldToObjectRow2.xyz, point) + instance.worldToObjectRow2.w
    );
}

// Same transform applied as a direction (no translation). Left UN-normalized on purpose so the ray
// parameter t is preserved between world and object space (object hit at t == world hit at t).
float3 nwbSwShadowTransformVector(const NwbSwShadowInstance instance, const float3 vector){
    return float3(
        dot(instance.worldToObjectRow0.xyz, vector),
        dot(instance.worldToObjectRow1.xyz, vector),
        dot(instance.worldToObjectRow2.xyz, vector)
    );
}

// Moeller-Trumbore ray/triangle test (two-sided: an occluder shadows regardless of facing). Reports a hit
// strictly inside the (tMin, tMax) interval.
bool nwbSwShadowRayTriangle(const float3 origin, const float3 direction, const float tMin, const float tMax, const float3 v0, const float3 v1, const float3 v2){
    const float3 edge1 = v1 - v0;
    const float3 edge2 = v2 - v0;
    const float3 pVec = cross(direction, edge2);
    const float det = dot(edge1, pVec);
    if(abs(det) < 1e-12)
        return false;

    const float invDet = 1.0 / det;
    const float3 tVec = origin - v0;
    const float u = dot(tVec, pVec) * invDet;
    if(u < 0.0 || u > 1.0)
        return false;

    const float3 qVec = cross(tVec, edge1);
    const float v = dot(direction, qVec) * invDet;
    if(v < 0.0 || (u + v) > 1.0)
        return false;

    const float t = dot(edge2, qVec) * invDet;
    return (t > tMin && t < tMax);
}

// Per-instance occlusion: push the world ray into the instance's object space, then walk that mesh's
// triangle BVH and report the first triangle the ray crosses within (tMin, tMax).
bool nwbSwShadowInstanceOccluded(const uint instanceIndex, const float3 worldOrigin, const float3 worldDirection, const float tMin, const float tMax){
    const NwbSwShadowInstance instance = g_NwbSwShadowInstances[instanceIndex];
    const uint meshIndex = instance.meshIndex;
    const float3 origin = nwbSwShadowTransformPoint(instance, worldOrigin);
    const float3 direction = nwbSwShadowTransformVector(instance, worldDirection);
    const float3 invDir = nwbSwShadowSafeInvDir(direction);

    uint stack[NWB_SW_SHADOW_MESH_STACK_SIZE];
    uint stackPointer = 0u;
    stack[stackPointer++] = 0u; // per-mesh root

    while(stackPointer > 0u){
        const uint nodeIndex = stack[--stackPointer];
        const NwbBvhNode node = g_NwbSwShadowMeshNodes[meshIndex][nodeIndex];
        if(!nwbSwShadowRayAabb(origin, invDir, tMin, tMax, node.aabbMin, node.aabbMax))
            continue;

        if(nwbBvhNodeIsLeaf(node)){
            const uint triangle = nwbBvhLeafPrimitive(node);
            const uint indexBase = triangle * 3u;
            const uint i0 = g_NwbSwShadowMeshIndices[meshIndex].Load(indexBase * 4u);
            const uint i1 = g_NwbSwShadowMeshIndices[meshIndex].Load((indexBase + 1u) * 4u);
            const uint i2 = g_NwbSwShadowMeshIndices[meshIndex].Load((indexBase + 2u) * 4u);
            const float3 v0 = asfloat(g_NwbSwShadowMeshPositions[meshIndex].Load3(i0 * 12u));
            const float3 v1 = asfloat(g_NwbSwShadowMeshPositions[meshIndex].Load3(i1 * 12u));
            const float3 v2 = asfloat(g_NwbSwShadowMeshPositions[meshIndex].Load3(i2 * 12u));
            if(nwbSwShadowRayTriangle(origin, direction, tMin, tMax, v0, v1, v2))
                return true;
        }
        else{
            // Conservative on stack exhaustion: a subtree deeper than the traversal stack reports occluded
            // rather than being skipped, so a pathologically unbalanced BVH over-darkens at worst instead of
            // leaking light through unvisited geometry.
            if((stackPointer + 2u) > NWB_SW_SHADOW_MESH_STACK_SIZE)
                return true;
            stack[stackPointer++] = node.leftChild;
            stack[stackPointer++] = node.rightChild;
        }
    }
    return false;
}

// Scene-level occlusion: walk the software scene/instance BVH; each leaf is an instance whose world AABB the
// ray crossed, so refine it with the exact per-mesh triangle traversal and report the first real hit.
bool nwbSwShadowOccluded(const float3 origin, const float3 direction, const float tMin, const float tMax){
    if(g_NwbSwShadowPushConstants.instanceCount == 0u)
        return false;

    const float3 invDir = nwbSwShadowSafeInvDir(direction);

    uint stack[NWB_SW_SHADOW_SCENE_STACK_SIZE];
    uint stackPointer = 0u;
    stack[stackPointer++] = 0u; // root

    while(stackPointer > 0u){
        const uint nodeIndex = stack[--stackPointer];
        const NwbBvhNode node = g_NwbSwShadowSceneNodes[nodeIndex];
        if(!nwbSwShadowRayAabb(origin, invDir, tMin, tMax, node.aabbMin, node.aabbMax))
            continue;

        if(nwbBvhNodeIsLeaf(node)){
            if(nwbSwShadowInstanceOccluded(nwbBvhLeafPrimitive(node), origin, direction, tMin, tMax))
                return true;
        }
        else{
            // Conservative on stack exhaustion (see nwbSwShadowInstanceOccluded): an over-deep scene tree
            // reports occluded rather than dropping instances. Unreachable in practice (few instances).
            if((stackPointer + 2u) > NWB_SW_SHADOW_SCENE_STACK_SIZE)
                return true;
            stack[stackPointer++] = node.leftChild;
            stack[stackPointer++] = node.rightChild;
        }
    }
    return false;
}

// Per light: cast one occlusion ray from the surface toward the light. Mirrors the hardware raygen
// (directional aims along the toward-light axis to "infinity"; point/spot aim at the light with tMax = the
// distance; geometric-normal bias + tMin avoids self-intersection; a light inside the bias shell is lit).
bool nwbSwShadowLightVisible(const float3 worldPosition, const float3 normal, const NwbSceneLight light){
    float3 toLight;
    float tMax;
    if(light.params.y < 0.5){
        toLight = normalize(light.direction.xyz);
        tMax = 1.0e7;
    }
    else{
        const float3 delta = light.position.xyz - worldPosition;
        tMax = length(delta);
        toLight = (tMax > 1.0e-5) ? (delta / tMax) : float3(0.0, 1.0, 0.0);
    }

    const float tMin = 0.001;
    const float tEnd = tMax - 0.002;
    if(tEnd <= tMin)
        return true;

    const float3 origin = worldPosition + normal * tMin;
    return !nwbSwShadowOccluded(origin, toLight, tMin, tEnd);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SW_SHADOW_GROUP_SIZE, NWB_SW_SHADOW_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint2 pixel = dispatchThreadID.xy;
    if(pixel.x >= g_NwbSwShadowPushConstants.width || pixel.y >= g_NwbSwShadowPushConstants.height)
        return;

    const int3 texel = int3(int2(pixel), 0);

    // Background (no geometry) is fully lit; the lighting pass ignores it anyway.
    if(g_NwbSwShadowGBufferDepth.Load(texel).r >= 0.999999){
        g_NwbSwShadowVisibility[pixel] = 0xFFFFFFFFu;
        return;
    }

    const float3 worldPosition = g_NwbSwShadowGBufferWorldPosition.Load(texel).xyz;
    const float3 normal = normalize(g_NwbSwShadowGBufferNormal.Load(texel).xyz * 2.0 - 1.0);

    const uint lightCount = min(nwbSceneLightCount(), NWB_SCENE_SHADOW_VISIBILITY_LIGHT_COUNT);
    uint visibilityMask = 0u;
    for(uint i = 0u; i < lightCount; ++i){
        if(nwbSwShadowLightVisible(worldPosition, normal, g_NwbSceneLights[i]))
            visibilityMask |= (1u << i);
    }

    g_NwbSwShadowVisibility[pixel] = visibilityMask;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
