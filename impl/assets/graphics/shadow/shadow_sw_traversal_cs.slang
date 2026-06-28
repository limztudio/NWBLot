// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "sw_binding_slots.h"

#define NWB_SCENE_SHADING_SET NWB_SW_SHADOW_SET
#define NWB_SCENE_SHADING_BINDING NWB_SW_SHADOW_BINDING_SCENE_SHADING
#define NWB_SCENE_LIGHT_LIST_SET NWB_SW_SHADOW_SET
#define NWB_SCENE_LIGHT_LIST_BINDING NWB_SW_SHADOW_BINDING_LIGHT_LIST

// Point the surface hook's material-constants context (g_NwbMaterialTypedWords + g_NwbMeshInstances, declared in
// mesh/material_typed_bindings.slangi at NWB_MESH_SET / NWB_MESH_BINDING_*) at this pass's binding set so the
// per-hit transmittance dispatch reads them here -- mirroring the NWB_SCENE_*_SET/BINDING redirects above. The
// mesh-set defaults (slots 6/12) collide with this set's visibility + instance-material slots, so they are moved
// onto the SW shadow slot map.
#define NWB_MESH_SET NWB_SW_SHADOW_SET
#define NWB_MESH_BINDING_MATERIAL_TYPED NWB_SW_SHADOW_BINDING_MATERIAL_TYPED
#define NWB_MESH_BINDING_INSTANCE NWB_SW_SHADOW_BINDING_MESH_INSTANCES

#include "../scene/buffer.slangi"
#include "../bvh/bvh_common.slangi"
#include "../common/math.slangi"
#include "instance_material.slangi"

// The shadow trace's material-constants context (NwbShadowHit + the per-invocation accessors + the surface
// contract + the engine-side shadow physics helpers nwbShadowFresnelTransmittedFraction /
// nwbShadowAbsorptionCoefficient the trace calls) + the cook-generated per-hit surface dispatch
// (nwbShadowDispatchSurface), which #includes each transparent material's .bind + .surface and routes by
// shadowTransmittanceModelId to return that occluder's optical params (ior / transmission).
#include "shadow_surface.slangi"
#include "shadow/generated/transmittance_dispatch.slangi"

// SHARED per-crossing integration + finalize (NwbShadowVisibilityAccumulator / nwbShadowIntegrateCrossing /
// nwbShadowFinalizeVisibility), identical to the hardware any-hit -- pulled in after the surface helpers it depends
// on. The traversal threads one accumulator by reference and the per-light entry finalizes it.
#include "shadow_integrate.slangi"


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
RWTexture2DArray<float4> g_NwbSwShadowVisibility;

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

[[vk::binding(NWB_SW_SHADOW_BINDING_INSTANCE_MATERIAL, NWB_SW_SHADOW_SET)]]
StructuredBuffer<NwbRtInstanceMaterial, Std430DataLayout> g_NwbSwShadowInstanceMaterial;

// Parallel per-mesh arrays (slot k = mesh k): triangle BVH nodes + raw position / index byte buffers.
[[vk::binding(NWB_SW_SHADOW_BINDING_MESH_NODES, NWB_SW_SHADOW_SET)]]
StructuredBuffer<NwbBvhNode, Std430DataLayout> g_NwbSwShadowMeshNodes[NWB_SW_SHADOW_MAX_MESHES];

[[vk::binding(NWB_SW_SHADOW_BINDING_MESH_POSITIONS, NWB_SW_SHADOW_SET)]]
ByteAddressBuffer g_NwbSwShadowMeshPositions[NWB_SW_SHADOW_MAX_MESHES];

[[vk::binding(NWB_SW_SHADOW_BINDING_MESH_INDICES, NWB_SW_SHADOW_SET)]]
ByteAddressBuffer g_NwbSwShadowMeshIndices[NWB_SW_SHADOW_MAX_MESHES];

// Per-vertex shadow-trace attributes (AttribGpu = Half4U normal @ +0 (8B) + Float2U uv0 @ +8 (8B), 16B stride),
// positionStream-indexed in lockstep with g_NwbSwShadowMeshPositions, so a triangle hit interpolates normal/uv0
// with the SAME i0/i1/i2 it loaded for positions.
[[vk::binding(NWB_SW_SHADOW_BINDING_MESH_ATTRIBUTES, NWB_SW_SHADOW_SET)]]
ByteAddressBuffer g_NwbSwShadowMeshAttributes[NWB_SW_SHADOW_MAX_MESHES];

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

// Transforms an object-space normal to world space for the surface hook (NwbMeshSurface.normal is world space).
// The normal transform is the inverse-transpose of object->world -- the transpose of the world->object 3x3 carried
// in the instance rows; that transpose's i-th component is the dot of the object normal with the i-th COLUMN of the
// world->object 3x3 (the i-th components of the three rows). This matches the hardware any-hit and is the
// geometrically-correct transform; it equals the rasterizer's world-space normal under uniform instance scale (the
// common case) and diverges in direction only under NON-uniform scale (where the rasterizer applies the forward
// map instead) -- latent for transmittance hooks that don't read the normal. nwbSafeNormalize guards the
// degenerate (near-zero / non-finite, e.g. a singular instance transform) result, matching the engine pattern.
float3 nwbSwShadowTransformNormal(const NwbSwShadowInstance instance, const float3 objectNormal){
    const float3 worldNormal = float3(
        dot(float3(instance.worldToObjectRow0.x, instance.worldToObjectRow1.x, instance.worldToObjectRow2.x), objectNormal),
        dot(float3(instance.worldToObjectRow0.y, instance.worldToObjectRow1.y, instance.worldToObjectRow2.y), objectNormal),
        dot(float3(instance.worldToObjectRow0.z, instance.worldToObjectRow1.z, instance.worldToObjectRow2.z), objectNormal)
    );
    return nwbSafeNormalize(worldNormal, float3(0.0, 0.0, 1.0));
}

// Result of a ray/triangle test: whether the ray crossed the triangle inside (tMin, tMax) + the hit's
// barycentrics. (u, v) are the Moeller-Trumbore parameters (weights of v1, v2); w = 1 - u - v is v0's weight, so
// an attribute interpolates as a*w + b*u + c*v. t is the world/object ray parameter at the hit.
struct NwbSwShadowTriangleHit{
    bool hit;
    float t;
    float u;
    float v;
    float w;
};

// Moeller-Trumbore ray/triangle test (two-sided: an occluder shadows regardless of facing). Reports a hit
// strictly inside the (tMin, tMax) interval along with its barycentrics for per-hit attribute interpolation.
NwbSwShadowTriangleHit nwbSwShadowRayTriangle(const float3 origin, const float3 direction, const float tMin, const float tMax, const float3 v0, const float3 v1, const float3 v2){
    NwbSwShadowTriangleHit result;
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

// Unpacks a per-vertex shadow-trace attribute (AttribGpu) from a per-mesh attribute byte buffer: Half4U normal
// (two packed-half uints) at +0, Float2U uv0 (two floats) at +8. Returns object-space normal in .xyz, uv0 in .w*.
struct NwbSwShadowVertexAttrib{
    float3 normal;
    float2 uv0;
};

NwbSwShadowVertexAttrib nwbSwShadowLoadVertexAttrib(const uint meshIndex, const uint vertexIndex){
    const uint base = vertexIndex * 16u;
    const uint2 packedNormal = g_NwbSwShadowMeshAttributes[meshIndex].Load2(base);
    NwbSwShadowVertexAttrib attrib;
    attrib.normal = float3(
        nwbUnpackHalf2x16(packedNormal.x),
        nwbUnpackHalf2x16(packedNormal.y).x
    );
    attrib.uv0 = asfloat(g_NwbSwShadowMeshAttributes[meshIndex].Load2(base + 8u));
    return attrib;
}

// Below this per-channel transmittance the occluder ray is effectively fully blocked; traversal stops early.
#define NWB_SW_SHADOW_TRANSMITTANCE_EPSILON 1e-3

// Per-instance occlusion: push the world ray into the instance's object space, then walk that mesh's
// triangle BVH. Each triangle the ray crosses folds one surface crossing into the shared running accumulator (the
// same nwbShadowIntegrateCrossing the hardware any-hit calls) for a transparent occluder, or zeroes the
// transmittance and stops for an opaque occluder.
void nwbSwShadowInstanceOccluded(const uint instanceIndex, const float3 worldOrigin, const float3 worldDirection, const float tMin, const float tMax, inout NwbShadowVisibilityAccumulator acc){
    const NwbSwShadowInstance instance = g_NwbSwShadowInstances[instanceIndex];
    const NwbRtInstanceMaterial material = g_NwbSwShadowInstanceMaterial[instanceIndex];
    const bool transparent = (material.flags & NWB_RT_INSTANCE_MATERIAL_FLAG_TRANSPARENT) != 0u;
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
            const NwbSwShadowTriangleHit triangleHit = nwbSwShadowRayTriangle(origin, direction, tMin, tMax, v0, v1, v2);
            if(triangleHit.hit){
                if(!transparent){
                    // Opaque occluder: light is fully blocked, stop the whole traversal.
                    acc.transmittance = half3(0.0, 0.0, 0.0);
                    return;
                }
                // Transparent occluder: evaluate this material's surface hook for its optical params and integrate
                // this surface crossing into the running visibility, then keep tracing for further occluders. The
                // hook reads the interpolated normal/uv0 + the material-constants context carried by the instance
                // record; dispatched by shadowTransmittanceModelId.
                const NwbSwShadowVertexAttrib a0 = nwbSwShadowLoadVertexAttrib(meshIndex, i0);
                const NwbSwShadowVertexAttrib a1 = nwbSwShadowLoadVertexAttrib(meshIndex, i1);
                const NwbSwShadowVertexAttrib a2 = nwbSwShadowLoadVertexAttrib(meshIndex, i2);

                // Interpolate the object-space normal, then transform it to world space (the surface contract) with
                // the inverse-transpose of object->world carried in the instance's world->object rows, so a
                // non-identity instance rotation/scale matches the rasterizer's world-space normal.
                const float3 objectNormal = a0.normal * triangleHit.w + a1.normal * triangleHit.u + a2.normal * triangleHit.v;

                NwbShadowHit hit;
                hit.meshInstanceIndex = material.meshInstanceIndex;
                hit.shadowTransmittanceModelId = material.shadowTransmittanceModelId;
                hit.materialConstantByteOffset = material.materialConstantByteOffset;
                hit.uv0 = a0.uv0 * triangleHit.w + a1.uv0 * triangleHit.u + a2.uv0 * triangleHit.v;
                hit.worldNormal = nwbSwShadowTransformNormal(instance, objectNormal);
                hit.worldPosition = worldOrigin + worldDirection * triangleHit.t;
                hit.rayDirection = worldDirection;

                // Evaluate this material's surface hook for its optical params (ior / transmission), then fold the
                // crossing into the running accumulator via the SHARED integrator -- byte-identical to the HW any-hit.
                // The GEOMETRIC face normal cross(v1-v0,v2-v0) + the object-space ray `direction` are already in hand;
                // triangleHit.t is the world distance (the ray dir is left un-normalised so its t matches RayTCurrent()).
                // hit.worldNormal stays the SHADING normal for any hook that reads it; the physics uses geometry only.
                const NwbMeshSurface surface = nwbShadowDispatchSurface(material.shadowTransmittanceModelId, hit);
                const float3 geometricNormal = cross(v1 - v0, v2 - v0);
                nwbShadowIntegrateCrossing(acc, instanceIndex, surface.ior, surface.transmission, geometricNormal, direction, triangleHit.t);
                // Cost safeguard for deeply-stacked transparency: stop once effectively opaque, but KEEP the
                // accumulated (near-zero) value -- the finalize applies the same Beer-Lambert term. Hard-zeroing here
                // was order-dependent and could snap an overlap pixel to pure black; matches the HW soft early-out.
                if(max(max(acc.transmittance.x, acc.transmittance.y), acc.transmittance.z) < NWB_SW_SHADOW_TRANSMITTANCE_EPSILON)
                    return;
            }
        }
        else{
            // Conservative on stack exhaustion: a subtree deeper than the traversal stack is treated as a
            // fully-opaque occluder rather than being skipped, so a pathologically unbalanced BVH over-darkens
            // at worst instead of leaking light through unvisited geometry.
            if((stackPointer + 2u) > NWB_SW_SHADOW_MESH_STACK_SIZE){
                acc.transmittance = half3(0.0, 0.0, 0.0);
                return;
            }
            stack[stackPointer++] = node.leftChild;
            stack[stackPointer++] = node.rightChild;
        }
    }
}

// Scene-level occlusion: walk the software scene/instance BVH; each leaf is an instance whose world AABB the
// ray crossed, so refine it with the exact per-mesh triangle traversal and fold its contribution into the
// shared running accumulator. Stops as soon as the transmittance reaches zero (an opaque hit).
void nwbSwShadowOccluded(const float3 origin, const float3 direction, const float tMin, const float tMax, inout NwbShadowVisibilityAccumulator acc){
    if(g_NwbSwShadowPushConstants.instanceCount == 0u)
        return;

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
            nwbSwShadowInstanceOccluded(nwbBvhLeafPrimitive(node), origin, direction, tMin, tMax, acc);
            if(max(max(acc.transmittance.x, acc.transmittance.y), acc.transmittance.z) < NWB_SW_SHADOW_TRANSMITTANCE_EPSILON)
                return;
        }
        else{
            // Conservative on stack exhaustion (see nwbSwShadowInstanceOccluded): an over-deep scene tree is
            // treated as a fully-opaque occluder rather than dropping instances. Unreachable in practice.
            if((stackPointer + 2u) > NWB_SW_SHADOW_SCENE_STACK_SIZE){
                acc.transmittance = half3(0.0, 0.0, 0.0);
                return;
            }
            stack[stackPointer++] = node.leftChild;
            stack[stackPointer++] = node.rightChild;
        }
    }
}

// Per light: cast one occlusion ray from the surface toward the light. Mirrors the hardware raygen
// (directional aims along the toward-light axis to "infinity"; point/spot aim at the light with tMax = the
// distance; geometric-normal bias + tMin avoids self-intersection; a light inside the bias shell is lit).
// Returns the per-channel transmittance: float3(1) when fully lit, tinted toward zero through occluders.
half3 nwbSwShadowLightVisible(const float3 worldPosition, const float3 normal, const NwbSceneLight light){
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
        return half3(1.0, 1.0, 1.0);

    const float3 origin = worldPosition + normal * tMin;
    NwbShadowVisibilityAccumulator acc = nwbShadowVisibilityBegin();
    nwbSwShadowOccluded(origin, toLight, tMin, tEnd, acc);
    // Combine into the final per-channel visibility (Fresnel product * Beer-Lambert, with the absolute-t bias
    // correction) -- the SAME shared finalize the HW raygen uses, so the two backends match exactly.
    return nwbShadowFinalizeVisibility(acc);
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
        for(uint slot = 0u; slot < NWB_SCENE_SHADOW_SLOT_COUNT; ++slot)
            g_NwbSwShadowVisibility[uint3(pixel, slot)] = float4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    const float3 worldPosition = g_NwbSwShadowGBufferWorldPosition.Load(texel).xyz;
    const float3 normal = normalize(g_NwbSwShadowGBufferNormal.Load(texel).xyz * 2.0 - 1.0);

    // Trace only the lights that hold a shadow slot (params.z >= 0); write each light's visibility into its
    // own slot layer. Lights without a slot are left at the cleared "all lit" white default.
    const uint lightCount = min(nwbSceneLightCount(), NWB_SCENE_MAX_LIGHTS);
    for(uint i = 0u; i < lightCount; ++i){
        const float slot = g_NwbSceneLights[i].params.z;
        if(slot < 0.0)
            continue;

        const half3 transmittance = nwbSwShadowLightVisible(worldPosition, normal, g_NwbSceneLights[i]);
        g_NwbSwShadowVisibility[uint3(pixel, uint(slot))] = float4(transmittance, 1.0);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

