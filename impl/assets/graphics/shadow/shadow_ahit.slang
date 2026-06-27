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
// interpolates uv0/normal at the hit barycentrics, evaluates the material's surface hook (routed by
// shadowTransmittanceModelId) for its optical params, and integrates this surface crossing into the running
// visibility -- per-crossing Fresnel into payload.transmittance + a signed Beer-Lambert optical depth into
// payload.opticalDepth -- then ignores the hit so the ray keeps gathering further occluders, ending the search
// early once the Fresnel product is effectively zero. An opaque occluder zeroes the transmittance and ends the
// search. Geometry is built non-opaque with NoDuplicateAnyHitInvocation so this fires exactly once per occluder.
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

        // Evaluate this material's surface hook for its optical params. The hook reads the interpolated normal/uv0 +
        // the material-constants context carried by the instance record; dispatched by shadowTransmittanceModelId.
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

        // Integrate this surface crossing into the running visibility the engine way: per-crossing Fresnel folds
        // into transmittance, and a signed Beer-Lambert optical depth telescopes the real inside path length. The
        // hook supplies only the optical params (ior / transmission); the engine -- which sees both the entry and
        // exit faces of an occluder -- owns the integration. worldNormal (above) stays the SHADING normal the hook
        // reads; the per-crossing PHYSICS below uses the GEOMETRIC face normal instead.
        const NwbMeshSurface surface = nwbShadowDispatchSurface(material.shadowTransmittanceModelId, hit);

        // Per-crossing faceSign + cosI from the GEOMETRIC face normal, NOT the interpolated shading normal. The
        // shading normal varies smoothly across a triangle, so near a smooth mesh's silhouette (cylinder / cone)
        // it sweeps front<->back over a WIDE band and flips sign(dot(rayDir, normal)) -- which inverts faceSign (an
        // entry counted as an exit) and craters cosI. A sign-flipped face is catastrophic here: the optical-depth
        // term is absorptionCoeff * t * faceSign with t the ABSOLUTE ray distance from the receiver (>> the chord
        // through the volume), so one wrong sign turns exp(-opticalDepth) into a near-black -- or, clamped from a
        // negative partial, an over-bright -- speckle exactly where two refractors' shadows overlap. The per-triangle
        // geometric normal cross(p1-p0, p2-p0) is constant over the face, so it only grazes at the measure-zero
        // tangent line (where the chord, hence the contribution, is ~0). Derived in OBJECT space from the BLAS-
        // matching positions + ObjectRayDirection(), making it byte-identical to the software traversal; under
        // uniform instance scale the object-space incidence angle equals world space (the rasterizer's case).
        const float3 p0 = asfloat(g_NwbShadowMeshPositions[material.meshSlot].Load3(i0 * 12u));
        const float3 p1 = asfloat(g_NwbShadowMeshPositions[material.meshSlot].Load3(i1 * 12u));
        const float3 p2 = asfloat(g_NwbShadowMeshPositions[material.meshSlot].Load3(i2 * 12u));
        const float3 geometricNormal = cross(p1 - p0, p2 - p0);
        const float3 objectRayDirection = ObjectRayDirection();
        const float ndotd = dot(objectRayDirection, geometricNormal);
        // faceSign: +1 on an exit face (ray leaving along the outward normal), -1 on an entry face, so the signed-t
        // sum telescopes to the true inside path length per volume.
        const float faceSign = (ndotd >= 0.0) ? 1.0 : -1.0;
        // cosI = |cos| of incidence against the geometric face (normalize both operands).
        const float cosI = saturate(abs(ndotd) * rsqrt(max(dot(geometricNormal, geometricNormal), 1e-20)) * rsqrt(max(dot(objectRayDirection, objectRayDirection), 1e-20)));
        const float t = RayTCurrent();
        const float3 absorptionCoeff = nwbShadowAbsorptionCoefficient(surface.transmission);
        // Fresnel: fade to a NO-OP at grazing. A tangential silhouette clip is a phantom hit on the faceted
        // approximation of a smooth surface; an unfaded Schlick term there (cosI -> 0 -> transmitted fraction -> 0)
        // would spuriously darken it. Fading the factor toward 1 makes the clip the no-op the smooth surface would
        // be; a real near-silhouette pair just gets a slightly lighter rim. (Fresnel only attenuates -> never brightens.)
        const float grazeWeight = smoothstep(0.0, 0.25, cosI);
        payload.transmittance *= lerp(1.0, nwbShadowFresnelTransmittedFraction(surface.ior, cosI), grazeWeight);
        // Signed Beer-Lambert optical depth + the absolute-t bias-correction accumulators (see the payload doc and the
        // raygen finalize). The optical depth itself is deliberately NOT grazing-weighted: weighting it would unbalance
        // a near-silhouette pair (one face kept, one faded) into a spike of the OPPOSITE sign. Instead the coeff-
        // weighted face-sign sum + crossing stats let the finalize subtract tBias * sign -- inert for balanced
        // occluders, exact cancellation for an unbalanced clip, with no threshold.
        payload.opticalDepth += absorptionCoeff * t * faceSign;
        payload.opticalDepthSign += absorptionCoeff * faceSign;
        payload.crossingTSum += t;
        payload.crossingCount += 1.0;
        // Early-out ONLY as a cost safeguard for deeply-stacked transparency: end the search but KEEP the accumulated
        // (already near-zero) value -- do NOT hard-snap it to pure black. The old `= 0` was order-dependent (any-hit
        // fires in arbitrary order), so a single grazing crossing firing first manufactured a hard-black pixel, and
        // different rays snapped at different times -> the structural blur-surviving dark speckle in the overlap.
        if(max(max(payload.transmittance.x, payload.transmittance.y), payload.transmittance.z) < NWB_SHADOW_RT_TRANSMITTANCE_EPSILON)
            AcceptHitAndEndSearch();
        IgnoreHit();
    }
    else{
        payload.transmittance = float3(0.0, 0.0, 0.0);
        AcceptHitAndEndSearch();
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

