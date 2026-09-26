// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "caustic_hw_common.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// HW caustic closest-hit: world hit + distance, REFRACTIVE flag, and for refractive crossings the interpolated shading
// normal (smooth caustic; vertex normals only) + optical params from the shared surface dispatch. Un-oriented here;
// nwbCausticOrientNormal orients it. Same a0*w+a1*u+a2*v interpolation as the SW trace (~ULP intersector difference).
[shader("closesthit")]
void main(inout NwbCausticHwPayload payload, in BuiltInTriangleIntersectionAttributes attributes){
    const NwbRtInstanceMaterial material = nwbCausticHwInstanceMaterials()[InstanceID()];

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
        const uint attributeBase = prim * NWB_RAYTRACE_TRIANGLE_CORNER_COUNT;
        // material.attributeSlot is this mesh's per-corner attribute buffer's global heap slot.
        const NwbRayTraceVertexAttrib a0 = nwbCausticHwLoadVertexAttrib(material.attributeSlot, attributeBase);
        const NwbRayTraceVertexAttrib a1 = nwbCausticHwLoadVertexAttrib(material.attributeSlot, attributeBase + 1u);
        const NwbRayTraceVertexAttrib a2 = nwbCausticHwLoadVertexAttrib(material.attributeSlot, attributeBase + 2u);
        const float3 objectShadingNormal = float3(a0.normal) * weights.x + float3(a1.normal) * weights.y + float3(a2.normal) * weights.z;

        // The interpolated SHADING normal in world space (inverse-transpose of object->world == mul(n,
        // (float3x3)WorldToObject3x4()) == the SW nwbSwBvhTransformNormal). The bend normal AND the surface
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
        payload.ior = surface.refractionIor;
        payload.transmission = surface.shadowAbsorptionTint;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

