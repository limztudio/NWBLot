// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Reduced-res to full-res joint-bilateral upsample gated by surface similarity (distance + normal; distance-only misses
// creases like the Cornell corner). Mirrors caustic stage-2 plus a normal gate; plain bilinear would smear coverage.
// Output: rgb = HDR irradiance (no [0,1] clamp), a = coverage (0 falls back to hemiAmbient).


#include "surfel_heap_resources.slangi"
#include "surfel_gbuffer.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define g_NwbSurfelUpsampleOutput (NwbHeapStorageImage2DFloat4(g_NwbSurfelHeapPushConstants.outputStorageHeapSlot))


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Texture2D<float4> nwbSurfelUpsampleHalfIrradiance(){
    return NwbHeapSampledImage2D(g_NwbSurfelHeapPushConstants.halfIrradianceSlot);
}

Texture2D<float4> nwbSurfelUpsampleGbufferNormal(){
    return NwbHeapSampledImage2D(g_NwbSurfelHeapPushConstants.normalSlot);
}

Texture2D<float4> nwbSurfelUpsampleGbufferWorldPosition(){
    return NwbHeapSampledImage2D(g_NwbSurfelHeapPushConstants.worldPositionSlot);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_SURFEL_UPSAMPLE_GROUP_SIZE, NWB_SURFEL_UPSAMPLE_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    uint fullWidth, fullHeight;
    nwbSurfelUpsampleGbufferNormal().GetDimensions(fullWidth, fullHeight);
    const uint2 pixel = dispatchThreadID.xy;
    if(pixel.x >= fullWidth || pixel.y >= fullHeight)
        return;

    const int3 fullCoord = int3(int2(pixel), 0);
    // G-buffer vector attachments are RGBA16F. Keep their fetched transport compact, then widen at the normal and
    // world-distance calculations where filtering precision matters.
    const half3 rawNormalSample = half3(nwbSurfelUpsampleGbufferNormal().Load(fullCoord).xyz);
    const float3 rawNormal = float3(rawNormalSample);
    if(dot(rawNormal, rawNormal) < NWB_SURFEL_GBUFFER_NORMAL_LENGTH_SQUARED_MIN){
        g_NwbSurfelUpsampleOutput[pixel] = float4(0.0, 0.0, 0.0, 0.0);   // background -> hemiAmbient in lighting
        return;
    }
    const float3 centerNormal = float3(nwbSurfelDecodeGbufferNormal(rawNormalSample));
    const half3 centerWorldSample = half3(nwbSurfelUpsampleGbufferWorldPosition().Load(fullCoord).xyz);
    const float3 centerWorld = float3(centerWorldSample);

    // World-distance edge-stop sigma from this pixel's own world spacing (max of +x/+y to survive a grazing/foreshortened
    // surface). A few pixels of tolerance keeps same-surface taps but rejects a silhouette jump to a far surface.
    const int2 rightFull = int2(min(int(pixel.x) + 1, int(fullWidth) - 1), int(pixel.y));
    const int2 downFull = int2(int(pixel.x), min(int(pixel.y) + 1, int(fullHeight) - 1));
    const half3 rightWorldSample = half3(nwbSurfelUpsampleGbufferWorldPosition().Load(int3(rightFull, 0)).xyz);
    const half3 downWorldSample = half3(nwbSurfelUpsampleGbufferWorldPosition().Load(int3(downFull, 0)).xyz);
    const float3 dxWorld = float3(rightWorldSample) - centerWorld;
    const float3 dyWorld = float3(downWorldSample) - centerWorld;
    const float spacing = max(max(length(dxWorld), length(dyWorld)), NWB_SURFEL_UPSAMPLE_WORLD_SPACING_MIN);
    const float sigmaWorld = spacing * NWB_SURFEL_UPSAMPLE_WORLD_SIGMA_SCALE;
    const float invTwoSigmaWorldSq = 1.0 / (2.0 * sigmaWorld * sigmaWorld);

    uint halfWidth, halfHeight;
    nwbSurfelUpsampleHalfIrradiance().GetDimensions(halfWidth, halfHeight);
    const uint factor = uint(g_NwbHeapSurfelConstantBuffers[g_NwbSurfelHeapPushConstants.constantsHeapSlot].screenHeightPad.y);

    // Reduced-grid index follows the resolve's top-left point sample at p/factor.
    const float2 src = float2(pixel) / float(factor);
    const int2 baseTexel = int2(floor(src));
    const float2 lerpFrac = src - float2(baseTexel);

    float3 sum = float3(0.0, 0.0, 0.0);
    // Bilinear and edge-stop weights are (0, 1) bounded factors, so half arithmetic is sufficient here.
    half weightSum = half(0.0h);
    [unroll] for(int j = 0; j < NWB_SURFEL_UPSAMPLE_TAP_SIDE; ++j){
        [unroll] for(int i = 0; i < NWB_SURFEL_UPSAMPLE_TAP_SIDE; ++i){
            const int2 tap = int2(clamp(baseTexel.x + i, 0, int(halfWidth) - 1), clamp(baseTexel.y + j, 0, int(halfHeight) - 1));
            // The resolve target is RGBA16F. Keep its irradiance/coverage payload native-half while it is only
            // transported through this filter; geometry and the weighted accumulation remain FP32 below.
            const half4 tapValue = half4(nwbSurfelUpsampleHalfIrradiance().Load(int3(tap, 0)));
            if(tapValue.a < half(NWB_SURFEL_UPSAMPLE_COVERAGE_MIN))
                continue;   // This reduced-grid texel has no surfel coverage.
            // Recover the tap's geometry from the SAME full texel the resolve gathered at (tap * factor, top-left).
            const int2 tapFull = int2(min(uint(tap.x) * factor, fullWidth - 1u), min(uint(tap.y) * factor, fullHeight - 1u));
            const half3 tapRawNormalSample = half3(nwbSurfelUpsampleGbufferNormal().Load(int3(tapFull, 0)).xyz);
            const float3 tapRawNormal = float3(tapRawNormalSample);
            if(dot(tapRawNormal, tapRawNormal) < NWB_SURFEL_GBUFFER_NORMAL_LENGTH_SQUARED_MIN)
                continue;   // tap's geometry is background
            const float3 tapNormal = float3(nwbSurfelDecodeGbufferNormal(tapRawNormalSample));
            if(dot(tapNormal, centerNormal) < NWB_SURFEL_UPSAMPLE_NORMAL_GATE)
                continue;   // different surface (crease) -> would leak GI across the edge
            const half3 tapWorldSample = half3(nwbSurfelUpsampleGbufferWorldPosition().Load(int3(tapFull, 0)).xyz);
            const float3 tapWorld = float3(tapWorldSample);
            const float3 d = tapWorld - centerWorld;
            const half bilinearWeight = (i == 0 ? half(1.0 - lerpFrac.x) : half(lerpFrac.x)) * (j == 0 ? half(1.0 - lerpFrac.y) : half(lerpFrac.y));
            const half weight = bilinearWeight * half(exp(-dot(d, d) * invTwoSigmaWorldSq));
            sum += float(weight) * float3(tapValue.rgb);
            weightSum += weight;
        }
    }

    // If all four reduced-grid taps lack coverage or belong to another surface, retain the ambient fallback.
    g_NwbSurfelUpsampleOutput[pixel] = (weightSum > half(0.0h)) ? float4(sum / float(weightSum), 1.0) : float4(0.0, 0.0, 0.0, 0.0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

