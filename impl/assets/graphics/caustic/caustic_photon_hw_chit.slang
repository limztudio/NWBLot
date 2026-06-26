// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "caustic_hw_common.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The hardware caustic closest-hit. Fills the payload from the nearest triangle the raygen's TraceRay committed:
// the world hit point + ray distance, the REFRACTIVE flag, and -- for a refractive crossing -- the interpolated
// SHADING normal (the bounce loop's bend normal) + the occluder's optical params from the SAME surface dispatch the
// SW trace + the shadow trace use. Bending on the interpolated shading normal (vs the per-facet geometric normal)
// yields a SMOOTH caustic on a tessellated sphere; it needs only the per-vertex normals from the attribute buffer
// (no positions). The normal is left UN-oriented; the shared bounce loop (nwbCausticOrientNormal) orients +
// normalizes it. The SAME interpolation (a0*w + a1*u + a2*v) the SW trace uses, so the two agree up to the ~ULP
// barycentric difference between the intersectors (accepted within the converged-image A/B tolerance).
[shader("closesthit")]
void main(inout NwbCausticHwPayload payload, in BuiltInTriangleIntersectionAttributes attributes){
    const NwbRtInstanceMaterial material = g_NwbCausticHwInstanceMaterial[InstanceID()];

    payload.hit = 1u;
    payload.t = RayTCurrent();
    payload.worldPos = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
    payload.shadingNormal = float3(0.0, 0.0, 1.0); // filled with the real interpolated normal for a refractive hit below
    payload.isRefractive = ((material.flags & NWB_RT_INSTANCE_MATERIAL_FLAG_REFRACTIVE) != 0u) ? 1u : 0u;
    payload.ior = 1.0;
    payload.transmission = float3(1.0, 1.0, 1.0);

    if(payload.isRefractive != 0u){
        // Hit barycentrics: BuiltInTriangleIntersectionAttributes carries (u, v) = weights of v1, v2; v0's weight is
        // w = 1 - u - v, so an attribute interpolates as a0*w + a1*u + a2*v -- matching the SW trace exactly.
        const float2 bary = attributes.barycentrics;
        const float3 weights = float3(1.0 - bary.x - bary.y, bary.x, bary.y);

        const uint prim = PrimitiveIndex();
        const uint indexBase = prim * 3u;
        // Contiguous triangle indices -> one Load3 (bit-identical to three Loads; mirrors the SW producer).
        const uint3 triIndices = g_NwbCausticHwMeshIndices[material.meshSlot].Load3(indexBase * 4u);
        const uint i0 = triIndices.x;
        const uint i1 = triIndices.y;
        const uint i2 = triIndices.z;

        const NwbCausticHwVertexAttrib a0 = nwbCausticHwLoadVertexAttrib(material.meshSlot, i0);
        const NwbCausticHwVertexAttrib a1 = nwbCausticHwLoadVertexAttrib(material.meshSlot, i1);
        const NwbCausticHwVertexAttrib a2 = nwbCausticHwLoadVertexAttrib(material.meshSlot, i2);
        const float3 objectShadingNormal = a0.normal * weights.x + a1.normal * weights.y + a2.normal * weights.z;

        // The interpolated SHADING normal in world space (inverse-transpose of object->world == mul(n,
        // (float3x3)WorldToObject3x4()) == the SW nwbCausticSwTransformNormal). The bend normal AND the surface
        // dispatch normal.
        const float3 worldShadingNormal = nwbSafeNormalize(mul(objectShadingNormal, (float3x3)WorldToObject3x4()), float3(0.0, 0.0, 1.0));
        payload.shadingNormal = worldShadingNormal;

        NwbShadowHit hit;
        hit.meshInstanceIndex = material.meshInstanceIndex;
        hit.shadowTransmittanceModelId = material.shadowTransmittanceModelId;
        hit.materialConstantByteOffset = material.materialConstantByteOffset;
        hit.uv0 = a0.uv0 * weights.x + a1.uv0 * weights.y + a2.uv0 * weights.z;
        hit.worldNormal = worldShadingNormal;
        hit.worldPosition = payload.worldPos;
        hit.rayDirection = WorldRayDirection();

        const NwbMeshSurface surface = nwbShadowDispatchSurface(material.shadowTransmittanceModelId, hit);
        payload.ior = surface.ior;
        payload.transmission = surface.transmission;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

