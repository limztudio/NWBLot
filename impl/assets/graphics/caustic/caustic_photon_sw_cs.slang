// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "sw_binding_slots.h"

#define NWB_SCENE_SHADING_SET NWB_CAUSTIC_SW_SET
#define NWB_SCENE_SHADING_BINDING NWB_CAUSTIC_SW_BINDING_SCENE_SHADING
#define NWB_SCENE_LIGHT_LIST_SET NWB_CAUSTIC_SW_SET
#define NWB_SCENE_LIGHT_LIST_BINDING NWB_CAUSTIC_SW_BINDING_LIGHT_LIST

// Point the surface hook's material-constants context (g_NwbMaterialTypedWords + g_NwbMeshInstances, declared in
// mesh/material_typed_bindings.slangi at NWB_MESH_SET / NWB_MESH_BINDING_*) at this pass's binding set so the
// per-hit surface dispatch (which supplies a refractive occluder's ior / transmission) reads them here -- exactly
// as the SW shadow traversal redirects them onto its own set.
#define NWB_MESH_SET NWB_CAUSTIC_SW_SET
#define NWB_MESH_BINDING_MATERIAL_TYPED NWB_CAUSTIC_SW_BINDING_MATERIAL_TYPED
#define NWB_MESH_BINDING_INSTANCE NWB_CAUSTIC_SW_BINDING_MESH_INSTANCES

#include "../scene/buffer.slangi"
#include "../bvh/bvh_common.slangi"
#include "../common/math.slangi"
#include "../shadow/instance_material.slangi"

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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::binding(NWB_CAUSTIC_SW_BINDING_SCENE_NODES, NWB_CAUSTIC_SW_SET)]]
StructuredBuffer<NwbBvhNode, Std430DataLayout> g_NwbCausticSwSceneNodes;

// Per-instance record: the affine world->object transform (3 rows) + the slot into the parallel per-mesh
// descriptor arrays. Mirrors the C++ SceneSwBvhInstanceGpu (64 bytes) / the SW shadow NwbSwShadowInstance.
struct NwbCausticSwInstance{
    float4 worldToObjectRow0;
    float4 worldToObjectRow1;
    float4 worldToObjectRow2;
    uint meshIndex;
    uint primitiveCount;
    uint pad0;
    uint pad1;
};

[[vk::binding(NWB_CAUSTIC_SW_BINDING_SCENE_INSTANCES, NWB_CAUSTIC_SW_SET)]]
StructuredBuffer<NwbCausticSwInstance, Std430DataLayout> g_NwbCausticSwInstances;

[[vk::binding(NWB_CAUSTIC_SW_BINDING_INSTANCE_MATERIAL, NWB_CAUSTIC_SW_SET)]]
StructuredBuffer<NwbRtInstanceMaterial, Std430DataLayout> g_NwbCausticSwInstanceMaterial;

[[vk::binding(NWB_CAUSTIC_SW_BINDING_MESH_NODES, NWB_CAUSTIC_SW_SET)]]
StructuredBuffer<NwbBvhNode, Std430DataLayout> g_NwbCausticSwMeshNodes[NWB_CAUSTIC_SW_MAX_MESHES];

[[vk::binding(NWB_CAUSTIC_SW_BINDING_MESH_POSITIONS, NWB_CAUSTIC_SW_SET)]]
ByteAddressBuffer g_NwbCausticSwMeshPositions[NWB_CAUSTIC_SW_MAX_MESHES];

[[vk::binding(NWB_CAUSTIC_SW_BINDING_MESH_INDICES, NWB_CAUSTIC_SW_SET)]]
ByteAddressBuffer g_NwbCausticSwMeshIndices[NWB_CAUSTIC_SW_MAX_MESHES];

[[vk::binding(NWB_CAUSTIC_SW_BINDING_MESH_ATTRIBUTES, NWB_CAUSTIC_SW_SET)]]
ByteAddressBuffer g_NwbCausticSwMeshAttributes[NWB_CAUSTIC_SW_MAX_MESHES];

// One caustic emission target (P1): the world-space AABB of a refractive instance. Mirrors the C++
// NwbCausticEmissionTargetGpu (32 bytes; the w lanes are unused padding).
struct NwbCausticEmissionTarget{
    float4 aabbMin;
    float4 aabbMax;
};

[[vk::binding(NWB_CAUSTIC_SW_BINDING_EMISSION_TARGETS, NWB_CAUSTIC_SW_SET)]]
StructuredBuffer<NwbCausticEmissionTarget, Std430DataLayout> g_NwbCausticSwEmissionTargets;

// Camera view buffer: only worldToClip is read (to project the receiver hit to a screen pixel for the splat).
// Std140 layout matching the C++ MeshViewGpuData / the mesh view_buffer.slangi struct.
struct NwbCausticSwView{
    row_major float4x4 worldToClip;
    row_major float4x4 clipToWorld;
    float4 cameraPosition;
    float4 frustumPlanes[6];
};

[[vk::binding(NWB_CAUSTIC_SW_BINDING_VIEW, NWB_CAUSTIC_SW_SET)]]
ConstantBuffer<NwbCausticSwView, Std140DataLayout> g_NwbCausticSwView;

[[vk::binding(NWB_CAUSTIC_SW_BINDING_GBUFFER_DEPTH, NWB_CAUSTIC_SW_SET)]]
Texture2D<float4> g_NwbCausticSwGBufferDepth;

[[vk::binding(NWB_CAUSTIC_SW_BINDING_GBUFFER_WORLD_POSITION, NWB_CAUSTIC_SW_SET)]]
Texture2D<float4> g_NwbCausticSwGBufferWorldPosition;

[[vk::binding(NWB_CAUSTIC_SW_BINDING_ACCUMULATOR, NWB_CAUSTIC_SW_SET)]]
RWTexture2DArray<uint> g_NwbCausticSwAccumulator;

struct NwbCausticSwPushConstants{
    uint width;
    uint height;
    uint instanceCount;
    uint photonCount;
    uint emissionTargetCount;
    uint gridSide; // sqrt(photonCount): the emission grid side, runtime so C++ can scale the photon density per build config
};

[[vk::push_constant]]
ConstantBuffer<NwbCausticSwPushConstants> g_NwbCausticSwPushConstants;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Slab ray/AABB overlap over [tMin, tMax]; identical to the SW shadow trace's test (axis-aligned safe via the
// finite invDir below).
bool nwbCausticSwRayAabb(const float3 origin, const float3 invDir, const float tMin, const float tMax, const float3 boxMin, const float3 boxMax){
    const float3 t0 = (boxMin - origin) * invDir;
    const float3 t1 = (boxMax - origin) * invDir;
    const float3 tSmall = min(t0, t1);
    const float3 tLarge = max(t0, t1);
    const float tNear = max(max(tSmall.x, tSmall.y), max(tSmall.z, tMin));
    const float tFar = min(min(tLarge.x, tLarge.y), min(tLarge.z, tMax));
    return tNear <= tFar;
}

float3 nwbCausticSwSafeInvDir(const float3 direction){
    const float epsilon = 1e-8;
    const float3 safe = float3(
        abs(direction.x) > epsilon ? direction.x : (direction.x < 0.0 ? -epsilon : epsilon),
        abs(direction.y) > epsilon ? direction.y : (direction.y < 0.0 ? -epsilon : epsilon),
        abs(direction.z) > epsilon ? direction.z : (direction.z < 0.0 ? -epsilon : epsilon)
    );
    return 1.0 / safe;
}

float3 nwbCausticSwTransformPoint(const NwbCausticSwInstance instance, const float3 point){
    return float3(
        dot(instance.worldToObjectRow0.xyz, point) + instance.worldToObjectRow0.w,
        dot(instance.worldToObjectRow1.xyz, point) + instance.worldToObjectRow1.w,
        dot(instance.worldToObjectRow2.xyz, point) + instance.worldToObjectRow2.w
    );
}

// Direction transform (no translation), left UN-normalized so the ray parameter t is preserved between world and
// object space (object hit at t == world hit at t).
float3 nwbCausticSwTransformVector(const NwbCausticSwInstance instance, const float3 vector){
    return float3(
        dot(instance.worldToObjectRow0.xyz, vector),
        dot(instance.worldToObjectRow1.xyz, vector),
        dot(instance.worldToObjectRow2.xyz, vector)
    );
}

// Transforms an object-space normal/vector to world space with the inverse-transpose of object->world (the
// transpose of the world->object 3x3 carried in the instance rows), so a non-identity instance rotation/scale
// matches the rasterizer's world-space normal. Same transform the SW shadow trace's nwbSwShadowTransformNormal
// applies. Left UN-normalized (the shared loop orients + the caller normalizes for the refraction math).
float3 nwbCausticSwTransformNormal(const NwbCausticSwInstance instance, const float3 objectNormal){
    return float3(
        dot(float3(instance.worldToObjectRow0.x, instance.worldToObjectRow1.x, instance.worldToObjectRow2.x), objectNormal),
        dot(float3(instance.worldToObjectRow0.y, instance.worldToObjectRow1.y, instance.worldToObjectRow2.y), objectNormal),
        dot(float3(instance.worldToObjectRow0.z, instance.worldToObjectRow1.z, instance.worldToObjectRow2.z), objectNormal)
    );
}

struct NwbCausticSwTriangleHit{
    bool hit;
    float t;
    float u;
    float v;
    float w;
};

// Moeller-Trumbore ray/triangle test (two-sided), reporting a hit strictly inside (tMin, tMax) with barycentrics.
// Identical math to the SW shadow trace's nwbSwShadowRayTriangle.
NwbCausticSwTriangleHit nwbCausticSwRayTriangle(const float3 origin, const float3 direction, const float tMin, const float tMax, const float3 v0, const float3 v1, const float3 v2){
    NwbCausticSwTriangleHit result;
    result.hit = false;
    result.t = 0.0;
    result.u = 0.0;
    result.v = 0.0;
    result.w = 0.0;

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
    result.u = u;
    result.v = v;
    result.w = 1.0 - u - v;
    return result;
}

struct NwbCausticSwVertexAttrib{
    float3 normal;
    float2 uv0;
};

NwbCausticSwVertexAttrib nwbCausticSwLoadVertexAttrib(const uint meshIndex, const uint vertexIndex){
    const uint base = vertexIndex * 16u;
    const uint2 packedNormal = g_NwbCausticSwMeshAttributes[meshIndex].Load2(base);
    NwbCausticSwVertexAttrib attrib;
    attrib.normal = float3(
        nwbUnpackHalf2x16(packedNormal.x),
        nwbUnpackHalf2x16(packedNormal.y).x
    );
    attrib.uv0 = asfloat(g_NwbCausticSwMeshAttributes[meshIndex].Load2(base + 8u));
    return attrib;
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
    const NwbCausticSwInstance instance = g_NwbCausticSwInstances[instanceIndex];
    const NwbRtInstanceMaterial material = g_NwbCausticSwInstanceMaterial[instanceIndex];
    const bool refractive = (material.flags & NWB_RT_INSTANCE_MATERIAL_FLAG_REFRACTIVE) != 0u;
    const uint meshIndex = instance.meshIndex;
    const float3 origin = nwbCausticSwTransformPoint(instance, worldOrigin);
    const float3 direction = nwbCausticSwTransformVector(instance, worldDirection);
    const float3 invDir = nwbCausticSwSafeInvDir(direction);

    uint stack[NWB_CAUSTIC_SW_MESH_STACK_SIZE];
    uint stackPointer = 0u;
    stack[stackPointer++] = 0u; // per-mesh root

    while(stackPointer > 0u){
        const uint nodeIndex = stack[--stackPointer];
        const NwbBvhNode node = g_NwbCausticSwMeshNodes[meshIndex][nodeIndex];
        // Cull subtrees beyond the current closest distance (closest.t shrinks as nearer hits are found).
        if(!nwbCausticSwRayAabb(origin, invDir, tMin, closest.t, node.aabbMin, node.aabbMax))
            continue;

        if(nwbBvhNodeIsLeaf(node)){
            const uint triangle = nwbBvhLeafPrimitive(node);
            const uint indexBase = triangle * 3u;
            const uint i0 = g_NwbCausticSwMeshIndices[meshIndex].Load(indexBase * 4u);
            const uint i1 = g_NwbCausticSwMeshIndices[meshIndex].Load((indexBase + 1u) * 4u);
            const uint i2 = g_NwbCausticSwMeshIndices[meshIndex].Load((indexBase + 2u) * 4u);
            const float3 v0 = asfloat(g_NwbCausticSwMeshPositions[meshIndex].Load3(i0 * 12u));
            const float3 v1 = asfloat(g_NwbCausticSwMeshPositions[meshIndex].Load3(i1 * 12u));
            const float3 v2 = asfloat(g_NwbCausticSwMeshPositions[meshIndex].Load3(i2 * 12u));
            const NwbCausticSwTriangleHit triangleHit = nwbCausticSwRayTriangle(origin, direction, tMin, closest.t, v0, v1, v2);
            if(triangleHit.hit){
                // Closer than the running min -> adopt it.
                closest.hit = true;
                closest.t = triangleHit.t;
                closest.isRefractive = refractive;
                closest.worldPos = worldOrigin + worldDirection * triangleHit.t;
                closest.shadingNormal = float3(0.0, 0.0, 1.0); // filled with the real interpolated normal for a refractive hit below
                closest.ior = 1.0;
                closest.transmission = float3(1.0, 1.0, 1.0);

                if(refractive){
                    // Evaluate this material's surface hook for its optical params, the SAME way the shadow trace
                    // does (interpolated normal/uv0 + the material-constants context the instance record carries).
                    const NwbCausticSwVertexAttrib a0 = nwbCausticSwLoadVertexAttrib(meshIndex, i0);
                    const NwbCausticSwVertexAttrib a1 = nwbCausticSwLoadVertexAttrib(meshIndex, i1);
                    const NwbCausticSwVertexAttrib a2 = nwbCausticSwLoadVertexAttrib(meshIndex, i2);
                    const float3 objectShadingNormal = a0.normal * triangleHit.w + a1.normal * triangleHit.u + a2.normal * triangleHit.v;

                    // The interpolated SHADING normal in world space (inverse-transpose of object->world). This is the
                    // bend normal (smooth across the mesh -> smooth caustic) AND the surface-dispatch normal.
                    const float3 worldShadingNormal = nwbSafeNormalize(nwbCausticSwTransformNormal(instance, objectShadingNormal), float3(0.0, 0.0, 1.0));
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
                    closest.ior = surface.ior;
                    closest.transmission = surface.transmission;
                }
            }
        }
        else{
            if((stackPointer + 2u) > NWB_CAUSTIC_SW_MESH_STACK_SIZE)
                continue; // over-deep subtree: skip rather than corrupt the stack (rare; a missed hit, not a crash)
            stack[stackPointer++] = node.leftChild;
            stack[stackPointer++] = node.rightChild;
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
    closest.transmission = float3(1.0, 1.0, 1.0);

    if(g_NwbCausticSwPushConstants.instanceCount == 0u)
        return closest;

    const float3 invDir = nwbCausticSwSafeInvDir(direction);

    uint stack[NWB_CAUSTIC_SW_SCENE_STACK_SIZE];
    uint stackPointer = 0u;
    stack[stackPointer++] = 0u; // root

    while(stackPointer > 0u){
        const uint nodeIndex = stack[--stackPointer];
        const NwbBvhNode node = g_NwbCausticSwSceneNodes[nodeIndex];
        if(!nwbCausticSwRayAabb(origin, invDir, tMin, closest.t, node.aabbMin, node.aabbMax))
            continue;

        if(nwbBvhNodeIsLeaf(node)){
            nwbCausticSwInstanceClosest(nwbBvhLeafPrimitive(node), origin, direction, tMin, closest);
        }
        else{
            if((stackPointer + 2u) > NWB_CAUSTIC_SW_SCENE_STACK_SIZE)
                continue;
            stack[stackPointer++] = node.leftChild;
            stack[stackPointer++] = node.rightChild;
        }
    }

    return closest;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Deposits one fixed-point value into the accumulator pixel + channel with the given tent weight (already folded
// into `value`). Guarded so out-of-bounds pixels are no-ops.
void nwbCausticSwAccumulate(const int2 pixel, const uint channel, const uint value){
    if(value == 0u)
        return;
    if(pixel.x < 0 || pixel.y < 0 || pixel.x >= (int)g_NwbCausticSwPushConstants.width || pixel.y >= (int)g_NwbCausticSwPushConstants.height)
        return;
    InterlockedAdd(g_NwbCausticSwAccumulator[uint3(uint2(pixel), channel)], value);
}

// Splats the surviving flux at the receiver world hit: project through the camera worldToClip to a pixel, reject if
// the projected depth is beyond the G-buffer depth there (kills screen-space leak onto surfaces the eye can't see
// at that pixel), then deposit the per-channel fixed-point flux into a 2x2 tent footprint (the splat-side AA).
void nwbCausticSwSplat(const float3 worldPos, const float3 flux){
    const float4 clip = mul(g_NwbCausticSwView.worldToClip, float4(worldPos, 1.0));
    if(clip.w <= 1e-6)
        return;

    const float3 ndc = clip.xyz / clip.w;
    if(ndc.x < -1.0 || ndc.x > 1.0 || ndc.y < -1.0 || ndc.y > 1.0)
        return;
    if(ndc.z < 0.0 || ndc.z > 1.0)
        return;

    // NDC -> screen UV (y flipped) -> pixel-center coordinate.
    const float2 uv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    const float2 screen = uv * float2((float)g_NwbCausticSwPushConstants.width, (float)g_NwbCausticSwPushConstants.height) - 0.5;

    const int2 baseTexel = int2(floor(screen));
    if(baseTexel.x < 0 || baseTexel.y < 0 || baseTexel.x >= (int)g_NwbCausticSwPushConstants.width || baseTexel.y >= (int)g_NwbCausticSwPushConstants.height)
        return;

    // Screen-space-leak reject: the photon's receiver hit must be the SAME world surface the camera sees at this
    // pixel, else the caustic would leak onto geometry occluded from the eye here. Compare WORLD POSITIONS, not
    // device depth: at the grazing angles a near-edge-on ground plane is viewed at, the device-depth gradient
    // across a single pixel can exceed any fixed device-depth tolerance, so a device-depth match test rejects every
    // valid splat (the zero-deposit bug). A world-distance test is space-consistent and gradient-robust -- the
    // tolerance scales with the receiver's world spacing per pixel. Background (sky) still skips via the depth read.
    const float sceneDepth = g_NwbCausticSwGBufferDepth.Load(int3(baseTexel, 0)).r;
    if(sceneDepth >= 0.999999)
        return; // background pixel (no receiver)

    const float3 sceneWorldPos = g_NwbCausticSwGBufferWorldPosition.Load(int3(baseTexel, 0)).xyz;
    // Per-pixel world spacing along x (a robust, scene-scale-relative tolerance) -- one texel to the right.
    const int2 rightTexel = int2(min(baseTexel.x + 1, (int)g_NwbCausticSwPushConstants.width - 1), baseTexel.y);
    const float3 sceneWorldPosRight = g_NwbCausticSwGBufferWorldPosition.Load(int3(rightTexel, 0)).xyz;
    const float pixelWorldSpacing = length(sceneWorldPosRight - sceneWorldPos);
    const float rejectTolerance = max(pixelWorldSpacing * 4.0, 0.05);
    if(length(worldPos - sceneWorldPos) > rejectTolerance)
        return;

    // 2x2 bilinear tent weights from the sub-pixel fraction.
    const float2 frac = screen - float2(baseTexel);
    const float w00 = (1.0 - frac.x) * (1.0 - frac.y);
    const float w10 = frac.x * (1.0 - frac.y);
    const float w01 = (1.0 - frac.x) * frac.y;
    const float w11 = frac.x * frac.y;

    const int2 t00 = baseTexel;
    const int2 t10 = baseTexel + int2(1, 0);
    const int2 t01 = baseTexel + int2(0, 1);
    const int2 t11 = baseTexel + int2(1, 1);

    for(uint channel = 0u; channel < 3u; ++channel){
        const float fluxChannel = flux[channel];
        nwbCausticSwAccumulate(t00, channel, nwbCausticFluxToFixedPoint(fluxChannel * w00));
        nwbCausticSwAccumulate(t10, channel, nwbCausticFluxToFixedPoint(fluxChannel * w10));
        nwbCausticSwAccumulate(t01, channel, nwbCausticFluxToFixedPoint(fluxChannel * w01));
        nwbCausticSwAccumulate(t11, channel, nwbCausticFluxToFixedPoint(fluxChannel * w11));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Picks the caustic light for this photon. v1 round-robins photons across the lights that hold a caustic slot
// (params.w >= 0), so each caustic light gets a fair share of the budget. Returns false when no caustic light
// exists (the dispatch should not run in that case, but guard anyway).
bool nwbCausticSwPickLight(const uint photonIndex, out NwbSceneLight outLight){
    const uint lightCount = min(nwbSceneLightCount(), NWB_SCENE_MAX_LIGHTS);
    uint causticLightCount = 0u;
    // First pass: count caustic lights.
    for(uint i = 0u; i < lightCount; ++i){
        if(g_NwbSceneLights[i].params.w >= 0.0)
            ++causticLightCount;
    }
    if(causticLightCount == 0u){
        outLight = g_NwbSceneLights[0];
        return false;
    }

    const uint pick = photonIndex % causticLightCount;
    uint seen = 0u;
    for(uint i = 0u; i < lightCount; ++i){
        if(g_NwbSceneLights[i].params.w < 0.0)
            continue;
        if(seen == pick){
            outLight = g_NwbSceneLights[i];
            return true;
        }
        ++seen;
    }
    outLight = g_NwbSceneLights[0];
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_CAUSTIC_SW_GROUP_SIZE, 1, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint photonIndex = dispatchThreadID.x;
    if(photonIndex >= g_NwbCausticSwPushConstants.photonCount)
        return;
    if(g_NwbCausticSwPushConstants.emissionTargetCount == 0u || g_NwbCausticSwPushConstants.instanceCount == 0u)
        return;

    NwbSceneLight light;
    if(!nwbCausticSwPickLight(photonIndex, light))
        return;

    // The light's principal travel direction (FROM the light). NwbSceneLight.direction is the toward-light /
    // emission axis; the photon travels along the emission axis. For a directional/spot caustic light params.y is
    // 0 (directional) or 2 (spot); point lights are excluded upstream (no caustic slot assigned).
    const float3 lightDir = nwbSafeNormalize(-light.direction.xyz, float3(0.0, 0.0, -1.0));

    // Per-target emission (the make-or-break aim): assign each photon to ONE refractive target and aim it at that
    // target's world AABB. Aiming at the COMBINED box over the spread tetrahedra centres the domain in the GAP between
    // them and wastes most photons on empty space; per-target keeps the domain tight on actual glass so a photon is
    // guaranteed to pass through (or graze) its target. The target is picked by a HASH of photonIndex, NOT
    // photonIndex % count: the cell (column,row) below is also photonIndex-derived, so a plain modulo correlates the
    // target with the grid -- e.g. with 3 targets on a 512-wide grid (512 == 2 mod 3) each target gets the cells where
    // col == row + k (mod 3), a DIAGONAL-STRIPED 1/3 subset that the refractor images into diagonal caustic streaks
    // shooting far across the receiver (invisible when sparse, glaring once dense). The hash decorrelates target from
    // cell, so each target gets a uniformly-scattered subset of the stratified grid -> no stripes, caustic stays put.
    const uint targetIndex = nwbCausticHashU32(photonIndex) % g_NwbCausticSwPushConstants.emissionTargetCount;
    const float3 targetAabbMin = g_NwbCausticSwEmissionTargets[targetIndex].aabbMin.xyz;
    const float3 targetAabbMax = g_NwbCausticSwEmissionTargets[targetIndex].aabbMax.xyz;

    // Per-photon grid cell in [0,1)^2 with a PER-PHOTON deterministic stratified jitter: the photon index maps to a
    // (column, row) on a square grid, and each cell is sampled at its OWN fixed sub-point. Per-photon keeps a single
    // frame's splat from being a rigid lattice -- a flat refractor maps a rigid lattice to visible stripes, whereas
    // stratified noise reads as honest grain the spatial a-trous resolve smooths. Frame-INDEPENDENT (no per-frame
    // variation), so a static scene is flicker-free without temporal averaging. Shared with the HW producer (parity).
    const uint gridSide = max(g_NwbCausticSwPushConstants.gridSide, 1u);
    const uint column = photonIndex % gridSide;
    const uint row = (photonIndex / gridSide) % gridSide;
    const float2 jitter = nwbCausticEmissionJitter(photonIndex);
    const float2 cell = (float2(column, row) + jitter) / (float)gridSide;

    const NwbCausticEmission emission = nwbCausticEmitPhotonAtTarget(lightDir, targetAabbMin, targetAabbMax, cell);
    if(!emission.valid)
        return;

    // PHYSICAL per-photon flux (energy conservation): the power through this target's emission domain is
    // lightIrradiance * domainArea, carried by the ~photonCount/emissionTargetCount photons aimed at it, so
    // perPhoton = lightColor * domainArea * emissionTargetCount / photonCount. Doubling photonCount halves per-photon
    // flux (per-frame image brightness unchanged); the loose domain cancels (dropped photons carry their share). No
    // fudge constant -- the focused caustic is physically bright while sparse far prism-scatter falls below the exposure
    // floor. Byte-identical to the HW raygen (shared emission.domainArea + the same push fields).
    const float3 lightColor = clamp(light.colorIntensity.xyz, float3(0.0), float3(1.5)) * min(max(light.colorIntensity.w, 0.0), 2.0);
    const float perPhoton = emission.domainArea * (float)g_NwbCausticSwPushConstants.emissionTargetCount / (float)max(g_NwbCausticSwPushConstants.photonCount, 1u);

    NwbCausticPhoton photon;
    photon.origin = emission.origin;
    photon.direction = emission.direction;
    photon.flux = lightColor * perPhoton;
    photon.inside = false;

    const NwbCausticSplat splat = nwbCausticTracePhoton(photon);
    if(!splat.valid)
        return;

    nwbCausticSwSplat(splat.worldPos, splat.flux);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

