// Surfel upsample (U6): reconstruct the full-res surfelIrradiance from the HALF-res surfel_resolve output. The costly
// (2*EXTENT+1)^3 = 125-cell gather runs at half res; this pass restores full res with a JOINT-BILINEAR filter GATED by
// surface similarity, so GI stays confined to the surface it belongs to (no bleed across a silhouette or a crease). A
// plain bilinear upsample would smear irradiance -- and worse, smear the binary COVERAGE flag -- across a covered/
// uncovered edge (the exact reason deferred lighting point-Loads, never bilinear-samples, the resolved texture). Model:
// caustic_resolve_cs stage-2 upsample (2x2 bilinear footprint + world-distance edge-stop + 0 identity) PLUS a normal gate
// (creases where the world position is near-equal but the normals differ -- e.g. the Cornell red<->blue corner -- which a
// distance-only stop misses).
//
// Output contract (UNCHANGED for lighting): rgb = irradiance, a = coverage. a = 1 when >=1 covered same-surface tap fed
// the filter; a = 0 (background, or every tap rejected/uncovered) so lighting falls back to hemiAmbient. Irradiance is HDR
// -- NO [0,1] clamp (the shadow upsample's clamp would clip the bounce).


#include "surfel_binding_slots.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::binding(NWB_SURFEL_UPSAMPLE_BINDING_HALF_IRRADIANCE, NWB_SURFEL_UPSAMPLE_SET)]]
Texture2D<float4> g_NwbSurfelUpsampleHalfIrradiance;

[[vk::binding(NWB_SURFEL_UPSAMPLE_BINDING_GBUFFER_NORMAL, NWB_SURFEL_UPSAMPLE_SET)]]
Texture2D<float4> g_NwbSurfelUpsampleGbufferNormal;

[[vk::binding(NWB_SURFEL_UPSAMPLE_BINDING_GBUFFER_WORLD_POSITION, NWB_SURFEL_UPSAMPLE_SET)]]
Texture2D<float4> g_NwbSurfelUpsampleGbufferWorldPosition;

[[vk::binding(NWB_SURFEL_UPSAMPLE_BINDING_OUTPUT, NWB_SURFEL_UPSAMPLE_SET)]]
RWTexture2D<float4> g_NwbSurfelUpsampleOutput;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SURFEL_UPSAMPLE_GROUP_SIZE, NWB_SURFEL_UPSAMPLE_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    uint fullWidth, fullHeight;
    g_NwbSurfelUpsampleGbufferNormal.GetDimensions(fullWidth, fullHeight);
    const uint2 pixel = dispatchThreadID.xy;
    if(pixel.x >= fullWidth || pixel.y >= fullHeight)
        return;

    const int3 fullCoord = int3(int2(pixel), 0);
    const float3 rawNormal = g_NwbSurfelUpsampleGbufferNormal.Load(fullCoord).xyz;
    if(dot(rawNormal, rawNormal) < 0.01){
        g_NwbSurfelUpsampleOutput[pixel] = float4(0.0, 0.0, 0.0, 0.0);   // background -> hemiAmbient in lighting
        return;
    }
    const float3 centerNormal = normalize(rawNormal * 2.0 - 1.0);
    const float3 centerWorld = g_NwbSurfelUpsampleGbufferWorldPosition.Load(fullCoord).xyz;

    // World-distance edge-stop sigma from this pixel's own world spacing (max of +x/+y to survive a grazing/foreshortened
    // surface). A few pixels of tolerance keeps same-surface taps but rejects a silhouette jump to a far surface.
    const int2 rightFull = int2(min(int(pixel.x) + 1, int(fullWidth) - 1), int(pixel.y));
    const int2 downFull = int2(int(pixel.x), min(int(pixel.y) + 1, int(fullHeight) - 1));
    const float3 dxWorld = g_NwbSurfelUpsampleGbufferWorldPosition.Load(int3(rightFull, 0)).xyz - centerWorld;
    const float3 dyWorld = g_NwbSurfelUpsampleGbufferWorldPosition.Load(int3(downFull, 0)).xyz - centerWorld;
    const float spacing = max(max(length(dxWorld), length(dyWorld)), 1e-4);
    const float sigmaWorld = spacing * 3.0;
    const float invTwoSigmaWorldSq = 1.0 / (2.0 * sigmaWorld * sigmaWorld);

    uint halfWidth, halfHeight;
    g_NwbSurfelUpsampleHalfIrradiance.GetDimensions(halfWidth, halfHeight);
    const uint factor = uint(NWB_SURFEL_RESOLVE_HALF_FACTOR);

    // Map the full pixel to half-index space so that a block-aligned pixel lands EXACTLY on the half texel the resolve
    // gathered for it: the resolve point-samples half texel h at the block TOP-LEFT full pixel (h*factor), so half texel h
    // represents full pixel h*factor -> the half index of full pixel p is p/factor (NOT the texel-centre (p+0.5)/f-0.5,
    // which assumes a block-centre sample and mis-registers by ~half a tap). p = factor*h -> src = h -> tap h at weight 1.
    const float2 src = float2(pixel) / float(factor);
    const int2 baseTexel = int2(floor(src));
    const float2 lerpFrac = src - float2(baseTexel);

    float3 sum = float3(0.0, 0.0, 0.0);
    float weightSum = 0.0;
    [unroll] for(int j = 0; j < 2; ++j){
        [unroll] for(int i = 0; i < 2; ++i){
            const int2 tap = int2(clamp(baseTexel.x + i, 0, int(halfWidth) - 1), clamp(baseTexel.y + j, 0, int(halfHeight) - 1));
            const float4 tapValue = g_NwbSurfelUpsampleHalfIrradiance.Load(int3(tap, 0));
            if(tapValue.a < 0.5)
                continue;   // this half texel had no surfel coverage -> nothing to upsample from
            // Recover the tap's geometry from the SAME full texel the resolve gathered at (tap * factor, top-left).
            const int2 tapFull = int2(min(uint(tap.x) * factor, fullWidth - 1u), min(uint(tap.y) * factor, fullHeight - 1u));
            const float3 tapRawNormal = g_NwbSurfelUpsampleGbufferNormal.Load(int3(tapFull, 0)).xyz;
            if(dot(tapRawNormal, tapRawNormal) < 0.01)
                continue;   // tap's geometry is background
            const float3 tapNormal = normalize(tapRawNormal * 2.0 - 1.0);
            if(dot(tapNormal, centerNormal) < NWB_SURFEL_UPSAMPLE_NORMAL_GATE)
                continue;   // different surface (crease) -> would leak GI across the edge
            const float3 tapWorld = g_NwbSurfelUpsampleGbufferWorldPosition.Load(int3(tapFull, 0)).xyz;
            const float3 d = tapWorld - centerWorld;
            const float bilinearWeight = (i == 0 ? (1.0 - lerpFrac.x) : lerpFrac.x) * (j == 0 ? (1.0 - lerpFrac.y) : lerpFrac.y);
            const float weight = bilinearWeight * exp(-dot(d, d) * invTwoSigmaWorldSq);
            sum += weight * tapValue.rgb;
            weightSum += weight;
        }
    }

    // Every tap rejected (a full-res surface sliver whose 2x2 half neighbours are all uncovered/other-surface): no GI here
    // -> a = 0 -> lighting uses hemiAmbient, exactly as an uncovered full-res gather would have.
    g_NwbSurfelUpsampleOutput[pixel] = (weightSum > 0.0) ? float4(sum / weightSum, 1.0) : float4(0.0, 0.0, 0.0, 0.0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

