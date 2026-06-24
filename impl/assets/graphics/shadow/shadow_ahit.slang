// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "occlusion.slangi"
#include "../common/math.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Below this per-channel transmittance the occluder ray is effectively fully blocked; the search ends early.
#define NWB_SHADOW_RT_TRANSMITTANCE_EPSILON 1e-3

// Unpacks a per-vertex shadow-trace attribute (AttribGpu) from a per-mesh attribute byte buffer: Half4U normal
// (two packed-half uints) at +0, Float2U uv0 (two floats) at +8. Mirrors the software traversal's unpack so the
// hardware any-hit and the compute fallback interpolate identical inputs into the per-hit dispatch.
struct NwbShadowRtVertexAttrib{
    float3 normal;
    float2 uv0;
};

NwbShadowRtVertexAttrib nwbShadowRtLoadVertexAttrib(const uint meshIndex, const uint vertexIndex){
    const uint base = vertexIndex * 16u;
    const uint2 packedNormal = g_NwbShadowMeshAttributes[meshIndex].Load2(base);
    NwbShadowRtVertexAttrib attrib;
    attrib.normal = float3(
        nwbUnpackHalf2x16(packedNormal.x),
        nwbUnpackHalf2x16(packedNormal.y).x
    );
    attrib.uv0 = asfloat(g_NwbShadowMeshAttributes[meshIndex].Load2(base + 8u));
    return attrib;
}

// Colored-shadow any-hit. Each occluder the ray crosses looks up its per-instance material (indexed by
// InstanceID(), built lockstep with the TLAS instances). A transparent occluder fetches its hit triangle's
// vertex attributes (index buffer by PrimitiveIndex + the per-mesh attribute buffer at material.meshSlot),
// interpolates uv0/normal at the hit barycentrics, evaluates the material's per-hit transmittance hook (routed
// by shadowTransmittanceModelId) and multiplies the running transmittance by the result, then ignores the hit so
// the ray keeps gathering further occluders -- ending the search early once the light is effectively blocked. An
// opaque occluder zeroes the transmittance and ends the search. Geometry is built non-opaque with
// NoDuplicateAnyHitInvocation so this fires exactly once per occluder.
[shader("anyhit")]
void main(inout NwbShadowOcclusionPayload payload, in BuiltInTriangleIntersectionAttributes attributes){
    const NwbRtInstanceMaterial material = g_NwbShadowInstanceMaterial[InstanceID()];
    if((material.flags & NWB_RT_INSTANCE_MATERIAL_FLAG_TRANSPARENT) != 0u){
        // Hit barycentrics: BuiltInTriangleIntersectionAttributes carries (u, v) = weights of v1, v2; v0's weight
        // is w = 1 - u - v, so an attribute interpolates as a0*w + a1*u + a2*v (matching the software trace).
        const float2 bary = attributes.barycentrics;
        const float3 weights = float3(1.0 - bary.x - bary.y, bary.x, bary.y);

        const uint prim = PrimitiveIndex();
        const uint indexBase = prim * 3u;
        const uint i0 = g_NwbShadowMeshIndices[material.meshSlot].Load(indexBase * 4u);
        const uint i1 = g_NwbShadowMeshIndices[material.meshSlot].Load((indexBase + 1u) * 4u);
        const uint i2 = g_NwbShadowMeshIndices[material.meshSlot].Load((indexBase + 2u) * 4u);
        const NwbShadowRtVertexAttrib a0 = nwbShadowRtLoadVertexAttrib(material.meshSlot, i0);
        const NwbShadowRtVertexAttrib a1 = nwbShadowRtLoadVertexAttrib(material.meshSlot, i1);
        const NwbShadowRtVertexAttrib a2 = nwbShadowRtLoadVertexAttrib(material.meshSlot, i2);

        // Evaluate this material's per-hit transmittance hook. The hook reads the interpolated normal/uv0 + the
        // material-constants context carried by the instance record; dispatched by shadowTransmittanceModelId.
        // The interpolated normal is object space; transform it to world space (the surface contract --
        // NwbMeshSurface.normal is world space) with the inverse-transpose of object->world, i.e. the transpose of
        // WorldToObject3x4(), expressed as the row-vector product mul(objectNormal, (float3x3)WorldToObject3x4()).
        // This is the geometrically-correct normal transform; it equals the rasterizer's world-space normal under
        // uniform instance scale (the common case) and diverges in direction only under NON-uniform scale (where
        // the rasterizer applies the forward object->world map via nwbMeshTransformDirection) -- latent for any
        // transmittance hook that does not read the normal. nwbSafeNormalize guards the degenerate (near-zero /
        // non-finite) result, matching the engine's normal handling elsewhere.
        const float3 objectNormal = a0.normal * weights.x + a1.normal * weights.y + a2.normal * weights.z;
        const float3 worldNormal = nwbSafeNormalize(mul(objectNormal, (float3x3)WorldToObject3x4()), float3(0.0, 0.0, 1.0));

        NwbShadowHit hit;
        hit.meshInstanceIndex = material.meshInstanceIndex;
        hit.shadowTransmittanceModelId = material.shadowTransmittanceModelId;
        hit.materialConstantByteOffset = material.materialConstantByteOffset;
        hit.uv0 = a0.uv0 * weights.x + a1.uv0 * weights.y + a2.uv0 * weights.z;
        hit.worldNormal = worldNormal;
        hit.worldPosition = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
        hit.rayDirection = WorldRayDirection();

        // Resolve the hit's full surface, then fold its per-occluder transmittance into the running transmittance.
        // A `refractive` occluder (instance record FLAG_REFRACTIVE) applies engine-generic Fresnel + Beer-Lambert
        // physics over the surface-supplied ior/thickness/transmission; a non-refractive occluder uses the flat
        // colored-shadow tint exactly as before. Same flag accessor + math as the software traversal.
        const NwbMeshSurface surface = nwbShadowDispatchSurface(material.shadowTransmittanceModelId, hit);
        float3 occluderTransmittance;
        if((material.flags & NWB_RT_INSTANCE_MATERIAL_FLAG_REFRACTIVE) != 0u)
            occluderTransmittance = nwbShadowRefractiveTransmittance(surface.ior, surface.thickness, surface.transmission, hit.rayDirection, hit.worldNormal);
        else
            occluderTransmittance = surface.transmittance;

        payload.transmittance *= occluderTransmittance;
        if(max(max(payload.transmittance.x, payload.transmittance.y), payload.transmittance.z) < NWB_SHADOW_RT_TRANSMITTANCE_EPSILON){
            payload.transmittance = float3(0.0, 0.0, 0.0);
            AcceptHitAndEndSearch();
        }
        IgnoreHit();
    }
    else{
        payload.transmittance = float3(0.0, 0.0, 0.0);
        AcceptHitAndEndSearch();
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

