// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "caustic_hw_common.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The hardware caustic closest-hit. Fills the payload from the nearest triangle the raygen's TraceRay committed:
// the world hit point + ray distance, the REFRACTIVE flag, and -- for a refractive crossing -- the occluder's
// optical params from the SAME surface dispatch the SW trace + the shadow trace use. The load-bearing parity
// addition vs the HW shadow any-hit is the GEOMETRIC face normal computed from the per-mesh object-space positions
// (the BLAS owns positions for the trace but does not expose them; the bounce loop must bend on the geometric
// normal -- cross(v1-v0, v2-v0) -- so the HW and SW intersectors agree). The normal is left UN-oriented; the shared
// bounce loop (caustic_trace.slangi nwbCausticOrientNormal) orients it against the incident ray per crossing.
[shader("closesthit")]
void main(inout NwbCausticHwPayload payload, in BuiltInTriangleIntersectionAttributes attributes){
    const NwbRtInstanceMaterial material = g_NwbCausticHwInstanceMaterial[InstanceID()];

    // Hit barycentrics: BuiltInTriangleIntersectionAttributes carries (u, v) = weights of v1, v2; v0's weight is
    // w = 1 - u - v, so an attribute interpolates as a0*w + a1*u + a2*v -- matching the SW trace exactly.
    const float2 bary = attributes.barycentrics;
    const float3 weights = float3(1.0 - bary.x - bary.y, bary.x, bary.y);

    const uint prim = PrimitiveIndex();
    const uint indexBase = prim * 3u;
    const uint i0 = g_NwbCausticHwMeshIndices[material.meshSlot].Load(indexBase * 4u);
    const uint i1 = g_NwbCausticHwMeshIndices[material.meshSlot].Load((indexBase + 1u) * 4u);
    const uint i2 = g_NwbCausticHwMeshIndices[material.meshSlot].Load((indexBase + 2u) * 4u);

    // GEOMETRIC face normal from the object-space vertices -- the SAME cross-product the SW trace computes
    // (caustic_photon_sw_cs.slang nwbCausticSwInstanceClosest), to world via the inverse-transpose of object->world
    // (== mul(n, (float3x3)WorldToObject3x4()) == the SW nwbCausticSwTransformNormal). UN-oriented.
    const float3 v0 = asfloat(g_NwbCausticHwMeshPositions[material.meshSlot].Load3(i0 * 12u));
    const float3 v1 = asfloat(g_NwbCausticHwMeshPositions[material.meshSlot].Load3(i1 * 12u));
    const float3 v2 = asfloat(g_NwbCausticHwMeshPositions[material.meshSlot].Load3(i2 * 12u));
    const float3 objectGeomNormal = cross(v1 - v0, v2 - v0);

    payload.hit = 1u;
    payload.t = RayTCurrent();
    payload.worldPos = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
    payload.geomNormal = mul(objectGeomNormal, (float3x3)WorldToObject3x4());
    payload.isRefractive = ((material.flags & NWB_RT_INSTANCE_MATERIAL_FLAG_REFRACTIVE) != 0u) ? 1u : 0u;
    payload.ior = 1.0;
    payload.transmission = float3(1.0, 1.0, 1.0);

    if(payload.isRefractive != 0u){
        // Evaluate the occluder's surface hook for ior / transmission, identically to the SW trace: interpolate the
        // object shading normal -> world, interpolate uv0, carry the material-constants context the instance record
        // holds, dispatch by shadowTransmittanceModelId.
        const NwbCausticHwVertexAttrib a0 = nwbCausticHwLoadVertexAttrib(material.meshSlot, i0);
        const NwbCausticHwVertexAttrib a1 = nwbCausticHwLoadVertexAttrib(material.meshSlot, i1);
        const NwbCausticHwVertexAttrib a2 = nwbCausticHwLoadVertexAttrib(material.meshSlot, i2);
        const float3 objectShadingNormal = a0.normal * weights.x + a1.normal * weights.y + a2.normal * weights.z;

        NwbShadowHit hit;
        hit.meshInstanceIndex = material.meshInstanceIndex;
        hit.shadowTransmittanceModelId = material.shadowTransmittanceModelId;
        hit.materialConstantByteOffset = material.materialConstantByteOffset;
        hit.uv0 = a0.uv0 * weights.x + a1.uv0 * weights.y + a2.uv0 * weights.z;
        hit.worldNormal = nwbSafeNormalize(mul(objectShadingNormal, (float3x3)WorldToObject3x4()), float3(0.0, 0.0, 1.0));
        hit.worldPosition = payload.worldPos;
        hit.rayDirection = WorldRayDirection();

        const NwbMeshSurface surface = nwbShadowDispatchSurface(material.shadowTransmittanceModelId, hit);
        payload.ior = surface.ior;
        payload.transmission = surface.transmission;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

