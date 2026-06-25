// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "resolve_binding_slots.h"
#include "splat_constants.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::binding(NWB_CAUSTIC_RESOLVE_BINDING_ACCUMULATOR, NWB_CAUSTIC_RESOLVE_SET)]]
Texture2DArray<uint> g_NwbCausticResolveAccumulator;

[[vk::binding(NWB_CAUSTIC_RESOLVE_BINDING_GBUFFER_WORLD_POSITION, NWB_CAUSTIC_RESOLVE_SET)]]
Texture2D<float4> g_NwbCausticResolveGBufferWorldPosition;

[[vk::binding(NWB_CAUSTIC_RESOLVE_BINDING_GBUFFER_DEPTH, NWB_CAUSTIC_RESOLVE_SET)]]
Texture2D<float4> g_NwbCausticResolveGBufferDepth;

[[vk::binding(NWB_CAUSTIC_RESOLVE_BINDING_IRRADIANCE, NWB_CAUSTIC_RESOLVE_SET)]]
RWTexture2D<float4> g_NwbCausticResolveIrradiance;

[[vk::binding(NWB_CAUSTIC_RESOLVE_BINDING_HISTORY, NWB_CAUSTIC_RESOLVE_SET)]]
RWTexture2D<float4> g_NwbCausticResolveHistory;

struct NwbCausticResolvePushConstants{
    uint width;
    uint height;
    float causticIntensity;
    uint blurRadius;
    uint frameIndex;
    float temporalAlpha;
};

[[vk::push_constant]]
ConstantBuffer<NwbCausticResolvePushConstants> g_NwbCausticResolvePushConstants;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Estimates the world-space receiver area subtended by one pixel from the screen-space spacing of the G-buffer
// world position to the +x / +y neighbours (the area Jacobian of the screen->receiver map). This is what converts
// photon DENSITY into physical BRIGHTNESS: dividing accumulated flux by this area yields irradiance, so where the
// lens focuses many photons into a small receiver area the irradiance spikes (the bright crescent). Clamped away
// from zero so a degenerate (silhouette / coincident-neighbour) pixel cannot divide-by-zero.
float nwbCausticResolvePixelArea(const int2 pixel){
    const float3 center = g_NwbCausticResolveGBufferWorldPosition.Load(int3(pixel, 0)).xyz;
    const int2 right = int2(min(pixel.x + 1, (int)g_NwbCausticResolvePushConstants.width - 1), pixel.y);
    const int2 down = int2(pixel.x, min(pixel.y + 1, (int)g_NwbCausticResolvePushConstants.height - 1));
    const float3 dx = g_NwbCausticResolveGBufferWorldPosition.Load(int3(right, 0)).xyz - center;
    const float3 dy = g_NwbCausticResolveGBufferWorldPosition.Load(int3(down, 0)).xyz - center;
    const float area = length(cross(dx, dy));
    return max(area, 1e-4);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Un-scales the per-channel fixed-point accumulator at one texel back to floating-point flux.
float3 nwbCausticResolveLoadFlux(const int2 texel){
    return float3(
        (float)g_NwbCausticResolveAccumulator.Load(int4(texel, 0, 0)).r,
        (float)g_NwbCausticResolveAccumulator.Load(int4(texel, 1, 0)).r,
        (float)g_NwbCausticResolveAccumulator.Load(int4(texel, 2, 0)).r
    ) / NWB_CAUSTIC_FIXED_POINT_SCALE;
}

// Edge-aware bilateral gather of the splat accumulator -- the resolve-side caustic denoise (P5). A sparse photon
// splat reads as speckle (isolated bright deposits with dark gaps between them); this blends the per-channel flux
// over a (2R+1)^2 neighbourhood weighted by a spatial Gaussian AND a world-distance edge-stop, so the inter-photon
// gaps fill into a smooth caustic while flux never bleeds across a depth/silhouette discontinuity (the edge term
// collapses the weight where the receiver world position jumps). Background taps (no receiver) are skipped. The
// world sigma scales with the receiver's per-pixel world spacing, so the filter is scene-scale robust at any grazing
// angle. Returns the blended flux as a weight-normalized AVERAGE (a low-pass that preserves the local mean), NOT yet
// area-normalized; the caller divides by the receiver area to get irradiance. blurRadius 0 = no blur (the raw
// single-tap flux), for an A/B against the un-denoised splat.
float3 nwbCausticResolveGatherFlux(const int2 center){
    const int radius = (int)g_NwbCausticResolvePushConstants.blurRadius;
    if(radius <= 0)
        return nwbCausticResolveLoadFlux(center);

    const int width = (int)g_NwbCausticResolvePushConstants.width;
    const int height = (int)g_NwbCausticResolvePushConstants.height;
    const float3 centerWorld = g_NwbCausticResolveGBufferWorldPosition.Load(int3(center, 0)).xyz;
    // World spacing one texel right -> the edge-stop sigma tracks the receiver's per-pixel world size.
    const int2 rightTexel = int2(min(center.x + 1, width - 1), center.y);
    const float pixelSpacing = max(length(g_NwbCausticResolveGBufferWorldPosition.Load(int3(rightTexel, 0)).xyz - centerWorld), 1e-4);

    const float sigmaSpatial = max((float)radius * 0.5, 0.5);
    const float sigmaWorld = pixelSpacing * ((float)radius + 1.0);
    const float invTwoSigmaSpatialSq = 1.0 / (2.0 * sigmaSpatial * sigmaSpatial);
    const float invTwoSigmaWorldSq = 1.0 / (2.0 * sigmaWorld * sigmaWorld);

    float3 sumFlux = float3(0.0, 0.0, 0.0);
    float sumWeight = 0.0;
    const int radiusSq = radius * radius;
    for(int dy = -radius; dy <= radius; ++dy){
        for(int dx = -radius; dx <= radius; ++dx){
            // Circular kernel support: drop taps outside the inscribed disc so an isolated bright deposit smooths to
            // a ROUND blob. A square (2R+1)^2 box with a hard corner cutoff leaves faint blocky corners on a hot
            // texel (the spatial Gaussian is still ~exp(-4) at the square corner) -- which reads as the blocky look.
            const int spatialSqI = dx * dx + dy * dy;
            if(spatialSqI > radiusSq)
                continue;
            const int2 tap = int2(clamp(center.x + dx, 0, width - 1), clamp(center.y + dy, 0, height - 1));
            // Skip background taps (no receiver) so the sky cannot dilute the edge of a ground caustic.
            if(g_NwbCausticResolveGBufferDepth.Load(int3(tap, 0)).r >= 0.999999)
                continue;
            const float3 worldDelta = g_NwbCausticResolveGBufferWorldPosition.Load(int3(tap, 0)).xyz - centerWorld;
            const float spatialSq = (float)spatialSqI;
            const float worldSq = dot(worldDelta, worldDelta);
            const float weight = exp(-spatialSq * invTwoSigmaSpatialSq) * exp(-worldSq * invTwoSigmaWorldSq);
            sumFlux += weight * nwbCausticResolveLoadFlux(tap);
            sumWeight += weight;
        }
    }
    return (sumWeight > 0.0) ? (sumFlux / sumWeight) : float3(0.0, 0.0, 0.0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_CAUSTIC_RESOLVE_GROUP_SIZE, NWB_CAUSTIC_RESOLVE_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint2 pixel = dispatchThreadID.xy;
    if(pixel.x >= g_NwbCausticResolvePushConstants.width || pixel.y >= g_NwbCausticResolvePushConstants.height)
        return;

    const int2 texel = int2(pixel);

    // Background pixels (no receiver) carry no caustic. Zero BOTH the sampled irradiance and the temporal history so
    // a pixel that later becomes a receiver starts its accumulation from black rather than stale flux.
    if(g_NwbCausticResolveGBufferDepth.Load(int3(texel, 0)).r >= 0.999999){
        g_NwbCausticResolveIrradiance[pixel] = float4(0.0, 0.0, 0.0, 1.0);
        g_NwbCausticResolveHistory[pixel] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // Edge-aware bilateral gather of the splat accumulator (the resolve-side spatial denoise), then convert the
    // blended flux density to this-frame irradiance: divide by the receiver area subtended per pixel (density ->
    // brightness, so a focused pile-up spikes) and scale by the artist multiplier.
    const float3 blendedFlux = nwbCausticResolveGatherFlux(texel);
    const float area = nwbCausticResolvePixelArea(texel);
    const float3 current = blendedFlux / area * g_NwbCausticResolvePushConstants.causticIntensity;

    // Temporal accumulation (the primary denoise): exponential-moving-average the jittered per-frame splat into the
    // persistent history. Because the emission jitters each frame, successive frames are independent samples of the
    // same static caustic, so the EMA converges the sparse splat to a smooth result over ~1/temporalAlpha frames.
    // Frame 0 seeds the history with the raw frame (no read of the uninitialized buffer -> no NaN propagation).
    const float3 accumulated =
        (g_NwbCausticResolvePushConstants.frameIndex == 0u)
            ? current
            : lerp(g_NwbCausticResolveHistory[pixel].rgb, current, g_NwbCausticResolvePushConstants.temporalAlpha);

    g_NwbCausticResolveHistory[pixel] = float4(accumulated, 1.0);
    g_NwbCausticResolveIrradiance[pixel] = float4(accumulated, 1.0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

