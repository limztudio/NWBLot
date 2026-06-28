// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Hardware RayQuery (inline ray tracing) shadow trace. Replaces the raygen/any-hit/miss RT pipeline: a compute thread
// per HALF-res shadow pixel inline-traces each light's occlusion ray against the TLAS, gathering EVERY transparent
// crossing (Proceed() without committing) into a LOCAL per-occluder accumulator (NwbShadowVisibilityAccumulator) and
// stopping at an opaque hit. Because the accumulator is a compute local (not a hardware ray payload), its per-occluder
// bin array indexes safely -- which is exactly what the any-hit payload could not do. The shared
// nwbShadowIntegrateCrossing / nwbShadowFinalizeVisibility (shadow_integrate.slangi) own the physics, byte-identical
// to the software traversal. Output is the half-res visibility Texture2DArray that shadow_upsample_cs reconstructs.


#include "binding_slots.h"

// Point the surface hook's material-constants context + the scene shading / light list at this pass's binding set
// (same redirects the raygen used), so occlusion.slangi's includes resolve their bindings here.
#define NWB_SCENE_SHADING_SET NWB_SHADOW_RT_SET
#define NWB_SCENE_SHADING_BINDING NWB_SHADOW_RT_BINDING_SCENE_SHADING
#define NWB_SCENE_LIGHT_LIST_SET NWB_SHADOW_RT_SET
#define NWB_SCENE_LIGHT_LIST_BINDING NWB_SHADOW_RT_BINDING_LIGHT_LIST

#include "occlusion.slangi"
#include "../scene/buffer.slangi"
#include "../common/math.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Unpacks a per-vertex shadow-trace attribute (AttribGpu) from a per-mesh attribute byte buffer: Half4U normal
// (two packed-half uints) at +0, Float2U uv0 (two floats) at +8. Mirrors the software traversal's unpack so the
// RayQuery trace and the compute fallback interpolate identical inputs.
struct NwbShadowRqVertexAttrib{
    float3 normal;
    float2 uv0;
};

NwbShadowRqVertexAttrib nwbShadowRqLoadVertexAttrib(const uint meshIndex, const uint vertexIndex){
    const uint base = vertexIndex * 16u;
    const uint2 packedNormal = g_NwbShadowMeshAttributes[meshIndex].Load2(base);
    NwbShadowRqVertexAttrib attrib;
    attrib.normal = float3(
        nwbUnpackHalf2x16(packedNormal.x),
        nwbUnpackHalf2x16(packedNormal.y).x
    );
    attrib.uv0 = asfloat(g_NwbShadowMeshAttributes[meshIndex].Load2(base + 8u));
    return attrib;
}


// Per light: inline-trace one occlusion ray and integrate the colored visibility over the volume path. Mirrors the
// raygen's nwbShadowLightVisible ray setup exactly; the RayQuery loop replaces the raygen+any-hit (gather all
// transparent crossings, stop at an opaque one).
float3 nwbShadowRqLightVisible(const float3 worldPosition, const float3 normal, const NwbSceneLight light){
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
        return float3(1.0, 1.0, 1.0);

    RayDesc ray;
    ray.Origin = worldPosition + normal * tMin;
    ray.Direction = toLight;
    ray.TMin = tMin;
    ray.TMax = tEnd;

    NwbShadowVisibilityAccumulator acc = nwbShadowVisibilityBegin();

    RayQuery<RAY_FLAG_NONE> query;
    query.TraceRayInline(g_NwbShadowTlas, RAY_FLAG_NONE, 0xFFu, ray);
    while(query.Proceed()){
        if(query.CandidateType() != CANDIDATE_NON_OPAQUE_TRIANGLE)
            continue;

        const uint instanceId = query.CandidateInstanceID();
        const NwbRtInstanceMaterial material = g_NwbShadowInstanceMaterial[instanceId];
        if((material.flags & NWB_RT_INSTANCE_MATERIAL_FLAG_TRANSPARENT) == 0u){
            // Opaque occluder: fully blocked, end the search.
            acc.transmittance = float3(0.0, 0.0, 0.0);
            query.Abort();
            break;
        }

        // Transparent crossing: build the hit + integrate it, then keep gathering (never commit).
        const float2 bary = query.CandidateTriangleBarycentrics();
        const float3 weights = float3(1.0 - bary.x - bary.y, bary.x, bary.y);

        const uint prim = query.CandidatePrimitiveIndex();
        const uint indexBase = prim * 3u;
        const uint i0 = g_NwbShadowMeshIndices[material.meshSlot].Load(indexBase * 4u);
        const uint i1 = g_NwbShadowMeshIndices[material.meshSlot].Load((indexBase + 1u) * 4u);
        const uint i2 = g_NwbShadowMeshIndices[material.meshSlot].Load((indexBase + 2u) * 4u);
        const NwbShadowRqVertexAttrib a0 = nwbShadowRqLoadVertexAttrib(material.meshSlot, i0);
        const NwbShadowRqVertexAttrib a1 = nwbShadowRqLoadVertexAttrib(material.meshSlot, i1);
        const NwbShadowRqVertexAttrib a2 = nwbShadowRqLoadVertexAttrib(material.meshSlot, i2);

        // Interpolated SHADING normal -> world space (the surface contract the hook reads), via the candidate's
        // world->object 3x3 inverse-transpose, exactly as the any-hit did.
        const float3 objectNormal = a0.normal * weights.x + a1.normal * weights.y + a2.normal * weights.z;
        const float3 worldNormal = nwbSafeNormalize(mul(objectNormal, (float3x3)query.CandidateWorldToObject3x4()), float3(0.0, 0.0, 1.0));

        NwbShadowHit hit;
        hit.meshInstanceIndex = material.meshInstanceIndex;
        hit.shadowTransmittanceModelId = material.shadowTransmittanceModelId;
        hit.materialConstantByteOffset = material.materialConstantByteOffset;
        hit.uv0 = a0.uv0 * weights.x + a1.uv0 * weights.y + a2.uv0 * weights.z;
        hit.worldNormal = worldNormal;
        hit.worldPosition = ray.Origin + query.CandidateTriangleRayT() * ray.Direction;
        hit.rayDirection = ray.Direction;

        const NwbMeshSurface surface = nwbShadowDispatchSurface(material.shadowTransmittanceModelId, hit);

        // Object-space GEOMETRIC face normal from the hit triangle's positions + the candidate's object-space ray dir;
        // the shared integrator owns the per-occluder, balance-checked optical-depth physics (see shadow_integrate.slangi).
        const float3 p0 = asfloat(g_NwbShadowMeshPositions[material.meshSlot].Load3(i0 * 12u));
        const float3 p1 = asfloat(g_NwbShadowMeshPositions[material.meshSlot].Load3(i1 * 12u));
        const float3 p2 = asfloat(g_NwbShadowMeshPositions[material.meshSlot].Load3(i2 * 12u));
        const float3 geometricNormal = cross(p1 - p0, p2 - p0);
        nwbShadowIntegrateCrossing(acc, instanceId, surface.ior, surface.transmission, geometricNormal, query.CandidateObjectRayDirection(), query.CandidateTriangleRayT());
    }

    return nwbShadowFinalizeVisibility(acc);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SHADOW_RT_GROUP_SIZE, NWB_SHADOW_RT_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    // HALF-RESOLUTION: one thread per half-res pixel; the output visibility array IS the half-res buffer, so its
    // dimensions bound the dispatch. Each half-res pixel reads its representative full-res G-buffer surface at the
    // top-left of its 2x2 block (pixel * 2). shadow_upsample_cs then edge-aware upsamples to full-res.
    uint halfWidth;
    uint halfHeight;
    uint slotElems;
    g_NwbShadowVisibility.GetDimensions(halfWidth, halfHeight, slotElems);

    const uint2 pixel = dispatchThreadID.xy;
    if(pixel.x >= halfWidth || pixel.y >= halfHeight)
        return;

    const int3 texel = int3(int2(pixel) * 2, 0);

    // Background (no geometry) is fully lit; the lighting pass ignores it anyway.
    if(g_NwbShadowGBufferDepth.Load(texel).r >= 0.999999){
        for(uint slot = 0u; slot < NWB_SCENE_SHADOW_SLOT_COUNT; ++slot)
            g_NwbShadowVisibility[uint3(pixel, slot)] = float4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    const float3 worldPosition = g_NwbShadowGBufferWorldPosition.Load(texel).xyz;
    const float3 normal = normalize(g_NwbShadowGBufferNormal.Load(texel).xyz * 2.0 - 1.0);

    // Trace only the lights that hold a shadow slot (params.z >= 0); write each light's visibility into its own slot
    // layer. Lights without a slot are left at the cleared "all lit" white default.
    const uint lightCount = min(nwbSceneLightCount(), NWB_SCENE_MAX_LIGHTS);
    for(uint i = 0u; i < lightCount; ++i){
        const float slot = g_NwbSceneLights[i].params.z;
        if(slot < 0.0)
            continue;

        const float3 transmittance = nwbShadowRqLightVisible(worldPosition, normal, g_NwbSceneLights[i]);
        g_NwbShadowVisibility[uint3(pixel, uint(slot))] = float4(transmittance, 1.0);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
