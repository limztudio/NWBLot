// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "sw_binding_slots.h"

#define NWB_SCENE_SHADING_SET NWB_GI_SW_SET
#define NWB_SCENE_SHADING_BINDING NWB_GI_SW_BINDING_SCENE_SHADING
#define NWB_SCENE_LIGHT_LIST_SET NWB_GI_SW_SET
#define NWB_SCENE_LIGHT_LIST_BINDING NWB_GI_SW_BINDING_LIGHT_LIST

#define NWB_MESH_SET NWB_GI_SW_SET
#define NWB_MESH_BINDING_MATERIAL_TYPED NWB_GI_SW_BINDING_MATERIAL_TYPED
#define NWB_MESH_BINDING_INSTANCE NWB_GI_SW_BINDING_MESH_INSTANCES

#include "../scene/buffer.slangi"
#include "../scene/lighting.slangi"
#include "../bvh/bvh_common.slangi"
#include "../common/math.slangi"
#include "../shadow/instance_material.slangi"
#include "binding_slots.h"
#include "probe_grid.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::binding(NWB_GI_SW_BINDING_SCENE_NODES, NWB_GI_SW_SET)]]
StructuredBuffer<NwbBvhNode, Std430DataLayout> g_NwbGiSwSceneNodes;

struct NwbGiSwInstance{
    float4 worldToObjectRow0;
    float4 worldToObjectRow1;
    float4 worldToObjectRow2;
    uint meshIndex;
    uint primitiveCount;
    uint pad0;
    uint pad1;
};

[[vk::binding(NWB_GI_SW_BINDING_SCENE_INSTANCES, NWB_GI_SW_SET)]]
StructuredBuffer<NwbGiSwInstance, Std430DataLayout> g_NwbGiSwInstances;

[[vk::binding(NWB_GI_SW_BINDING_INSTANCE_MATERIAL, NWB_GI_SW_SET)]]
StructuredBuffer<NwbRtInstanceMaterial, Std430DataLayout> g_NwbGiSwInstanceMaterial;

[[vk::binding(NWB_GI_SW_BINDING_MESH_NODES, NWB_GI_SW_SET)]]
StructuredBuffer<NwbBvhNode, Std430DataLayout> g_NwbGiSwMeshNodes[NWB_GI_SW_MAX_MESHES];

[[vk::binding(NWB_GI_SW_BINDING_MESH_POSITIONS, NWB_GI_SW_SET)]]
ByteAddressBuffer g_NwbGiSwMeshPositions[NWB_GI_SW_MAX_MESHES];

[[vk::binding(NWB_GI_SW_BINDING_MESH_INDICES, NWB_GI_SW_SET)]]
ByteAddressBuffer g_NwbGiSwMeshIndices[NWB_GI_SW_MAX_MESHES];

[[vk::binding(NWB_GI_SW_BINDING_MESH_ATTRIBUTES, NWB_GI_SW_SET)]]
ByteAddressBuffer g_NwbGiSwMeshAttributes[NWB_GI_SW_MAX_MESHES];

[[vk::binding(NWB_GI_SW_BINDING_GRID_CONSTANTS, NWB_GI_SW_SET)]]
ConstantBuffer<NwbGiGridConstants> g_NwbGiGrid;

[[vk::binding(NWB_GI_SW_BINDING_RAY_DATA, NWB_GI_SW_SET)]]
RWTexture2D<float4> g_NwbGiRayData;

[[vk::binding(NWB_GI_SW_BINDING_PREV_IRRADIANCE, NWB_GI_SW_SET)]]
Texture2D<float4> g_NwbGiPrevIrradiance;

[[vk::binding(NWB_GI_SW_BINDING_PREV_DISTANCE, NWB_GI_SW_SET)]]
Texture2D<float2> g_NwbGiPrevDistance;

[[vk::binding(NWB_GI_SW_BINDING_HIT_ALBEDO, NWB_GI_SW_SET)]]
StructuredBuffer<float3> g_NwbGiHitAlbedo;

struct NwbGiTracePushConstants{
    uint activeProbeCount;
    uint updateCursor;   // first active probe in the round-robin
    uint frameIndex;     // seeds the per-frame R2 rotation
    uint raysPerProbe;   // compile-time from the default; pushed for flexibility
};

[[vk::push_constant]]
ConstantBuffer<NwbGiTracePushConstants> g_NwbGiPushConstants;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Ray/AABB slab overlap + entry-T (front-to-back ordered descent -- cloned from caustic, the proven pattern).
float3 nwbGiSwSafeInvDir(const float3 direction){
    const float epsilon = 1e-8;
    const float3 safe = float3(
        abs(direction.x) > epsilon ? direction.x : (direction.x < 0.0 ? -epsilon : epsilon),
        abs(direction.y) > epsilon ? direction.y : (direction.y < 0.0 ? -epsilon : epsilon),
        abs(direction.z) > epsilon ? direction.z : (direction.z < 0.0 ? -epsilon : epsilon)
    );
    return 1.0 / safe;
}

bool nwbGiSwRayAabb(const float3 origin, const float3 invDir, const float tMin, const float tMax, const float3 boxMin, const float3 boxMax){
    const float3 t0 = (boxMin - origin) * invDir;
    const float3 t1 = (boxMax - origin) * invDir;
    const float3 tSmall = min(t0, t1);
    const float3 tLarge = max(t0, t1);
    const float tNear = max(max(tSmall.x, tSmall.y), max(tSmall.z, tMin));
    const float tFar = min(min(tLarge.x, tLarge.y), min(tLarge.z, tMax));
    return tNear <= tFar;
}

bool nwbGiSwRayAabbT(const float3 origin, const float3 invDir, const float tMin, const float tMax, const float3 boxMin, const float3 boxMax, out float tEntry){
    const float3 t0 = (boxMin - origin) * invDir;
    const float3 t1 = (boxMax - origin) * invDir;
    const float3 tSmall = min(t0, t1);
    const float3 tLarge = max(t0, t1);
    const float tNear = max(max(tSmall.x, tSmall.y), max(tSmall.z, tMin));
    const float tFar = min(min(tLarge.x, tLarge.y), min(tLarge.z, tMax));
    tEntry = tNear;
    return tNear <= tFar;
}

float3 nwbGiSwTransformPoint(const NwbGiSwInstance instance, const float3 point){
    return float3(
        dot(instance.worldToObjectRow0.xyz, point) + instance.worldToObjectRow0.w,
        dot(instance.worldToObjectRow1.xyz, point) + instance.worldToObjectRow1.w,
        dot(instance.worldToObjectRow2.xyz, point) + instance.worldToObjectRow2.w
    );
}

float3 nwbGiSwTransformVector(const NwbGiSwInstance instance, const float3 vector){
    return float3(
        dot(instance.worldToObjectRow0.xyz, vector),
        dot(instance.worldToObjectRow1.xyz, vector),
        dot(instance.worldToObjectRow2.xyz, vector)
    );
}

float3 nwbGiSwTransformNormal(const NwbGiSwInstance instance, const float3 objectNormal){
    return float3(
        dot(float3(instance.worldToObjectRow0.x, instance.worldToObjectRow1.x, instance.worldToObjectRow2.x), objectNormal),
        dot(float3(instance.worldToObjectRow0.y, instance.worldToObjectRow1.y, instance.worldToObjectRow2.y), objectNormal),
        dot(float3(instance.worldToObjectRow0.z, instance.worldToObjectRow1.z, instance.worldToObjectRow2.z), objectNormal)
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbGiSwTriangleHit{
    bool hit;
    float t;
    float3 normal;
};

NwbGiSwTriangleHit nwbGiSwRayTriangle(const float3 origin, const float3 direction, const float tMin, const float tMax, const float3 v0, const float3 v1, const float3 v2){
    NwbGiSwTriangleHit result;
    result.hit = false;
    result.t = 0.0;
    result.normal = float3(0.0, 0.0, 1.0);

    const float3 edge1 = v1 - v0;
    const float3 edge2 = v2 - v0;
    const float3 pVec = cross(direction, edge2);
    const float det = dot(edge1, pVec);
    if(abs(det) < 1e-12)
        return result;

    const float invDet = 1.0 / det;
    const float3 tVec = origin - v0;
    const float u = dot(tVec, pVec) * invDet;
    if(u < 0.0 || u > 1.0)
        return result;

    const float3 qVec = cross(tVec, edge1);
    const float v = dot(direction, qVec) * invDet;
    if(v < 0.0 || (u + v) > 1.0)
        return result;

    const float t = dot(edge2, qVec) * invDet;
    if(t <= tMin || t >= tMax)
        return result;

    result.hit = true;
    result.t = t;
    result.normal = normalize(cross(edge1, edge2));
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Probe closest-hit result: the world-space hit point, the geometric face normal (Lambert uses it), and the hit
// instance's flat albedo for the bounce.
struct NwbGiProbeHit{
    bool hit;
    float t;
    float3 worldPos;
    float3 normal;
    float3 albedo;
};

// Per-instance closest-hit refinement: push the world ray into object space, walk the mesh BVH front-to-back,
// keep the nearest triangle crossing. On hit, compute the geometric face normal (inverse-transpose) + read the
// per-instance flat albedo. Cloned from nwbCausticSwInstanceClosest but simpler (no refractive dispatch, just
// Lambert geometry).
void nwbGiSwInstanceClosest(const uint instanceIndex, const float3 worldOrigin, const float3 worldDirection, const float tMin, inout NwbGiProbeHit closest){
    const NwbGiSwInstance instance = g_NwbGiSwInstances[instanceIndex];
    const NwbRtInstanceMaterial material = g_NwbGiSwInstanceMaterial[instanceIndex];
    const uint meshIndex = instance.meshIndex;

    const float3 origin = nwbGiSwTransformPoint(instance, worldOrigin);
    const float3 direction = nwbGiSwTransformVector(instance, worldDirection);
    const float3 invDir = nwbGiSwSafeInvDir(direction);

    uint stack[NWB_GI_SW_MESH_STACK_SIZE];
    uint stackPointer = 0u;
    NwbBvhNode node = g_NwbGiSwMeshNodes[NonUniformResourceIndex(meshIndex)][0u];
    bool active = nwbGiSwRayAabb(origin, invDir, tMin, closest.t, node.aabbMin, node.aabbMax);

    [loop] while(active){
        if(nwbBvhNodeIsLeaf(node)){
            const uint triangle = nwbBvhLeafPrimitive(node);
            const uint indexBase = triangle * 3u;
            const uint3 triIndices = g_NwbGiSwMeshIndices[NonUniformResourceIndex(meshIndex)].Load3(indexBase * 4u);
            const float3 v0 = asfloat(g_NwbGiSwMeshPositions[NonUniformResourceIndex(meshIndex)].Load3(triIndices.x * 12u));
            const float3 v1 = asfloat(g_NwbGiSwMeshPositions[NonUniformResourceIndex(meshIndex)].Load3(triIndices.y * 12u));
            const float3 v2 = asfloat(g_NwbGiSwMeshPositions[NonUniformResourceIndex(meshIndex)].Load3(triIndices.z * 12u));
            const NwbGiSwTriangleHit triHit = nwbGiSwRayTriangle(origin, direction, tMin, closest.t, v0, v1, v2);
            if(triHit.hit){
                closest.hit = true;
                closest.t = triHit.t;
                closest.worldPos = worldOrigin + worldDirection * triHit.t;
                closest.normal = nwbSafeNormalize(nwbGiSwTransformNormal(instance, triHit.normal), float3(0.0, 0.0, 1.0));
                // Per-instance flat albedo from the CPU-built buffer (NWB_GI_DEFAULT_ALBEDO fallback).
                const uint albedoIndex = instanceIndex;
                const uint albedoCount = NWB_GI_SW_MAX_MESHES; // placeholder; actual count passed via grid CB in a later refinement
                closest.albedo = g_NwbGiHitAlbedo[albedoIndex];
            }
            active = false;
        }
        else{
            const NwbBvhNode leftNode = g_NwbGiSwMeshNodes[NonUniformResourceIndex(meshIndex)][node.leftChild];
            const NwbBvhNode rightNode = g_NwbGiSwMeshNodes[NonUniformResourceIndex(meshIndex)][node.rightChild];
            float tLeft;
            const bool hitLeft = nwbGiSwRayAabbT(origin, invDir, tMin, closest.t, leftNode.aabbMin, leftNode.aabbMax, tLeft);
            float tRight;
            const bool hitRight = nwbGiSwRayAabbT(origin, invDir, tMin, closest.t, rightNode.aabbMin, rightNode.aabbMax, tRight);
            if(hitLeft && hitRight){
                if(tLeft <= tRight){
                    if(stackPointer < NWB_GI_SW_MESH_STACK_SIZE) stack[stackPointer++] = node.rightChild;
                    node = leftNode;
                }
                else{
                    if(stackPointer < NWB_GI_SW_MESH_STACK_SIZE) stack[stackPointer++] = node.leftChild;
                    node = rightNode;
                }
                continue;
            }
            if(hitLeft){ node = leftNode; continue; }
            if(hitRight){ node = rightNode; continue; }
            active = false;
        }

        [loop] while(stackPointer > 0u){
            node = g_NwbGiSwMeshNodes[NonUniformResourceIndex(meshIndex)][stack[--stackPointer]];
            if(nwbGiSwRayAabb(origin, invDir, tMin, closest.t, node.aabbMin, node.aabbMax)){
                active = true;
                break;
            }
        }
    }
}

// Scene-BVH closest-hit over all instances (front-to-back ordered). Returns the nearest hit across all meshes.
NwbGiProbeHit nwbGiSwTraceClosest(float3 origin, float3 direction, float tMin, float tMax){
    NwbGiProbeHit closest;
    closest.hit = false;
    closest.t = tMax;
    closest.worldPos = float3(0.0, 0.0, 0.0);
    closest.normal = float3(0.0, 0.0, 1.0);
    closest.albedo = float3(NWB_GI_DEFAULT_ALBEDO_FLOAT3);

    const float3 invDir = nwbGiSwSafeInvDir(direction);

    uint stack[NWB_GI_SW_SCENE_STACK_SIZE];
    uint stackPointer = 0u;
    NwbBvhNode node = g_NwbGiSwSceneNodes[0u];
    bool active = nwbGiSwRayAabb(origin, invDir, tMin, closest.t, node.aabbMin, node.aabbMax);

    [loop] while(active){
        if(nwbBvhNodeIsLeaf(node)){
            nwbGiSwInstanceClosest(nwbBvhLeafPrimitive(node), origin, direction, tMin, closest);
            active = false;
        }
        else{
            const NwbBvhNode leftNode = g_NwbGiSwSceneNodes[node.leftChild];
            const NwbBvhNode rightNode = g_NwbGiSwSceneNodes[node.rightChild];
            float tLeft;
            const bool hitLeft = nwbGiSwRayAabbT(origin, invDir, tMin, closest.t, leftNode.aabbMin, leftNode.aabbMax, tLeft);
            float tRight;
            const bool hitRight = nwbGiSwRayAabbT(origin, invDir, tMin, closest.t, rightNode.aabbMin, rightNode.aabbMax, tRight);
            if(hitLeft && hitRight){
                if(tLeft <= tRight){
                    if(stackPointer < NWB_GI_SW_SCENE_STACK_SIZE) stack[stackPointer++] = node.rightChild;
                    node = leftNode;
                }
                else{
                    if(stackPointer < NWB_GI_SW_SCENE_STACK_SIZE) stack[stackPointer++] = node.leftChild;
                    node = rightNode;
                }
                continue;
            }
            if(hitLeft){ node = leftNode; continue; }
            if(hitRight){ node = rightNode; continue; }
            active = false;
        }

        [loop] while(stackPointer > 0u){
            node = g_NwbGiSwSceneNodes[stack[--stackPointer]];
            if(nwbGiSwRayAabb(origin, invDir, tMin, closest.t, node.aabbMin, node.aabbMax)){
                active = true;
                break;
            }
        }
    }

    return closest;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Lambert hit shade: sum direct light (<=8 lights, unshadowed base + D4 dominant-light shadow ray) + per-instance
// flat albedo + prev-front atlas bounce fetch. Miss -> far-distance / zero (sky).
float3 nwbGiSwShadeHit(const NwbGiProbeHit hit, const float3 rayDir){
    float3 irradiance = float3(0.0, 0.0, 0.0);

    const float3 normal = hit.normal;
    // Face-forward the normal against the ray (the probe ray could hit a back face -- use the face the ray sees).
    const float3 facingNormal = dot(normal, rayDir) < 0.0 ? normal : -normal;

    // Lambert direct light over the <=8 NwbSceneLight entries (unshadowed -- D4 shadow ray is separate).
    const uint lightCount = min(nwbSceneLightCount(), NWB_SCENE_MAX_LIGHTS);
    for(uint i = 0u; i < lightCount; ++i){
        const NwbSceneLight light = g_NwbSceneLights[i];
        float3 lightVector;
        half3 lightRadiance;
        nwbSceneResolveLight(light, hit.worldPos, lightVector, lightRadiance);
        if(all(lightRadiance <= half3(0.0)))
            continue;
        const float ndotl = max(dot(facingNormal, lightVector), 0.0);
        irradiance += float3(lightRadiance) * ndotl;
    }

#if NWB_GI_HIT_SHADOW_RAYS
    // D4: one occlusion ray toward the DOMINANT light. The dominant light is the one with the highest NdotL.
    // This is an any-hit early-out (cheaper than the primary ray): miss -> lit, hit -> shadowed.
    {
        float bestNdotl = 0.0;
        float3 dominantLightDir = float3(0.0, 1.0, 0.0);
        uint dominantLightIndex = 0xFFFFFFFFu;
        for(uint i = 0u; i < lightCount; ++i){
            const NwbSceneLight light = g_NwbSceneLights[i];
            float3 lv;
            half3 lr;
            nwbSceneResolveLight(light, hit.worldPos, lv, lr);
            if(all(lr <= half3(0.0)))
                continue;
            const float ndotl = max(dot(facingNormal, lv), 0.0);
            if(ndotl > bestNdotl){
                bestNdotl = ndotl;
                dominantLightDir = lv;
                dominantLightIndex = i;
            }
        }
        if(dominantLightIndex != 0xFFFFFFFFu && bestNdotl > 0.0){
            const float shadowTMax = 1.0e4;
            const NwbGiProbeHit shadowHit = nwbGiSwTraceClosest(hit.worldPos + facingNormal * 0.001, dominantLightDir, 0.001, shadowTMax);
            if(shadowHit.hit)
                irradiance *= 0.0; // fully shadowed by the dominant-light occlusion ray
        }
    }
#endif

    // Apply the per-instance flat albedo (the bounce's diffuse reflectance).
    irradiance *= hit.albedo;

    // Infinite bounce: add the prev-front atlas probe fetch at this hit position. The prev-front irradiance atlas
    // holds the last frame's accumulated irradiance, so adding it creates an infinite-bounce feedback from day one.
    // U2 scaffold: the atlas is black-cleared (additive identity), so this is a no-op until U4 fills it.
    // TODO(U4): octahedral fetch from g_NwbGiPrevIrradiance at the hit's facing-normal direction + the nearest probe.

    return irradiance;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Workgroup-per-probe: one workgroup per active probe, one thread per ray.
// dispatch(activeProbeCount, 1, 1) with [numthreads(64, 1, 1)].
[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint probeOffset = dispatchThreadID.x; // ray index within the probe
    const uint probeIndex = (dispatchThreadID.y + g_NwbGiPushConstants.updateCursor) % nwbGiProbeCount(g_NwbGiGrid);

    // Only active probes trace (round-robin: 1/Nth per frame).
    if(!nwbGiProbeActiveThisFrame(probeIndex, g_NwbGiGrid))
        return;

    const uint rayIndex = dispatchThreadID.y * 64u + dispatchThreadID.x;
    const uint raysPerProbe = g_NwbGiPushConstants.raysPerProbe;
    if(rayIndex >= raysPerProbe)
        return;

    // De-linearize the probe index to 3D.
    const uint3 dims = uint3(g_NwbGiGrid.gridDims.xyz);
    const uint ix = probeIndex % dims.x;
    const uint iy = (probeIndex / dims.x) % dims.y;
    const uint iz = probeIndex / (dims.x * dims.y);
    const uint3 probeCoord = uint3(ix, iy, iz);
    const float3 probePos = nwbGiProbeWorldPos(probeCoord, g_NwbGiGrid);

    // Fibonacci ray direction with per-frame R2 rotation.
    const float2 rotation = nwbGiFrameRotation(g_NwbGiPushConstants.frameIndex);
    const float3 rayDir = nwbGiFibonacciDirection(rayIndex, raysPerProbe, rotation);

    // Trace.
    const float tMax = 1.0e4; // grid diagonal * 1.5 in a later refinement
    const NwbGiProbeHit hit = nwbGiSwTraceClosest(probePos, rayDir, 0.001, tMax);

    float4 rayResult;
    if(hit.hit){
        rayResult.rgb = nwbGiSwShadeHit(hit, rayDir);
        rayResult.a = hit.t;
    }
    else{
        // Miss: far-distance / zero. The sky contributes nothing (black).
        rayResult = float4(0.0, 0.0, 0.0, tMax);
    }

    // Write to ray-data texture (x = probe linear index, y = ray index).
    g_NwbGiRayData[uint2(probeIndex, rayIndex)] = rayResult;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

