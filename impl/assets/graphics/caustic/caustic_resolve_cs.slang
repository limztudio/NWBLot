// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "resolve_binding_slots.h"
#include "splat_constants.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Caustic resolve = an EDGE-AVOIDING A-TROUS WAVELET denoise (Dammertz et al.), run as N ping-pong compute passes. This
// REPLACES the temporal-EMA + variance-clamp + motion-vector reprojection path. Rationale: a refractor's caustic moves
// NON-RIGIDLY (a spinning flat prism MORPHS its caustic), so no motion vector tracks it and any temporal accumulation
// trails it (the ghost). A-trous denoises each frame PURELY SPATIALLY -> the sparse photon splat smooths to a clean
// caustic with NO history -> ghost-free by construction, for any caustic motion. The producer's per-photon emission is
// a deterministic stratified distribution (no per-frame jitter), so a static scene is flicker-free without temporal
// averaging, and the wavelet's geometry edge-stop keeps the smoothing bounded to the receiver surface.
//
// First pass (stepWidth==1 carries isFirstPass): reads the R32_UINT splat accumulator, un-scales it, AREA-NORMALIZES
// (flux density -> irradiance: divide by the receiver area subtended per pixel, the density->brightness conversion) and
// applies the single causticIntensity exposure, then performs the wavelet step. Later passes read the previous pass's
// RGBA16F color and only perform the wavelet step at a doubled dilation (stepWidth 2,4,8,16). The ping-pong is driven
// from C++: input/output binding sets swap each pass; the FINAL pass lands in the irradiance buffer the lighting adds.

[[vk::binding(NWB_CAUSTIC_RESOLVE_BINDING_ACCUMULATOR, NWB_CAUSTIC_RESOLVE_SET)]]
Texture2DArray<uint> g_NwbCausticResolveAccumulator;

[[vk::binding(NWB_CAUSTIC_RESOLVE_BINDING_GBUFFER_WORLD_POSITION, NWB_CAUSTIC_RESOLVE_SET)]]
Texture2D<float4> g_NwbCausticResolveGBufferWorldPosition;

[[vk::binding(NWB_CAUSTIC_RESOLVE_BINDING_GBUFFER_DEPTH, NWB_CAUSTIC_RESOLVE_SET)]]
Texture2D<float4> g_NwbCausticResolveGBufferDepth;

// The wavelet pass output (UAV). On the final pass this is the caustic irradiance the deferred lighting adds; on
// earlier passes it is whichever ping-pong buffer the next pass reads as the input color (the C++ swaps the sets).
[[vk::binding(NWB_CAUSTIC_RESOLVE_BINDING_OUTPUT, NWB_CAUSTIC_RESOLVE_SET)]]
RWTexture2D<float4> g_NwbCausticResolveOutput;

// The previous pass's color (SRV). Unused on the first pass (which reads the accumulator instead); bound to the other
// ping-pong buffer so the descriptor is always valid.
[[vk::binding(NWB_CAUSTIC_RESOLVE_BINDING_INPUT_COLOR, NWB_CAUSTIC_RESOLVE_SET)]]
Texture2D<float4> g_NwbCausticResolveInputColor;

struct NwbCausticResolvePushConstants{
    // All-scalar -> 8 x 4 = 32 bytes, no alignment padding; matches the C++ mirror NwbCausticResolvePushConstants.
    uint width;
    uint height;
    float causticIntensity; // exposure, applied once during the area-normalize on the first pass
    uint stepWidth;         // a-trous dilation for this pass (1,2,4,8,16); taps sit at +-{1,2}*stepWidth
    uint isFirstPass;       // 1 => read+normalize the accumulator; 0 => read the previous pass's input color
    uint passPad0;          // pad the tail to a 16B multiple (32) so C++ + shader sizes match exactly
    uint passPad1;
    uint passPad2;
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


// Per-pixel caustic irradiance for the wavelet input. First pass: un-scale the per-channel fixed-point accumulator,
// area-normalize (flux density -> irradiance) and apply the exposure. Later passes: read the previous pass's color.
// Background pixels (no receiver) carry no caustic -> zero.
float3 nwbCausticResolveSampleInput(const int2 texel){
    if(g_NwbCausticResolveGBufferDepth.Load(int3(texel, 0)).r >= 0.999999)
        return float3(0.0, 0.0, 0.0);
    if(g_NwbCausticResolvePushConstants.isFirstPass != 0u){
        const float3 flux = float3(
            (float)g_NwbCausticResolveAccumulator.Load(int4(texel, 0, 0)).r,
            (float)g_NwbCausticResolveAccumulator.Load(int4(texel, 1, 0)).r,
            (float)g_NwbCausticResolveAccumulator.Load(int4(texel, 2, 0)).r
        ) / NWB_CAUSTIC_FIXED_POINT_SCALE;
        const float area = nwbCausticResolvePixelArea(texel);
        return flux / area * g_NwbCausticResolvePushConstants.causticIntensity;
    }
    return g_NwbCausticResolveInputColor.Load(int3(texel, 0)).rgb;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_CAUSTIC_RESOLVE_GROUP_SIZE, NWB_CAUSTIC_RESOLVE_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint2 pixel = dispatchThreadID.xy;
    if(pixel.x >= g_NwbCausticResolvePushConstants.width || pixel.y >= g_NwbCausticResolvePushConstants.height)
        return;

    const int2 texel = int2(pixel);
    const int w = (int)g_NwbCausticResolvePushConstants.width;
    const int h = (int)g_NwbCausticResolvePushConstants.height;

    // Background (no receiver) carries no caustic. Write black so the additive lighting term is a no-op and a pixel
    // that later becomes a receiver starts from black.
    if(g_NwbCausticResolveGBufferDepth.Load(int3(texel, 0)).r >= 0.999999){
        g_NwbCausticResolveOutput[pixel] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    const float3 centerWorld = g_NwbCausticResolveGBufferWorldPosition.Load(int3(texel, 0)).xyz;
    // Per-pixel world spacing (one texel right) -> the geometry edge-stop sigma is scene-scale relative AND grows with
    // the wavelet dilation, so each coarser pass smooths over a proportionally wider receiver-world neighbourhood.
    const int2 rightTexel = int2(min(texel.x + 1, w - 1), texel.y);
    const float pixelSpacing = max(length(g_NwbCausticResolveGBufferWorldPosition.Load(int3(rightTexel, 0)).xyz - centerWorld), 1e-4);
    const int step = (int)max(g_NwbCausticResolvePushConstants.stepWidth, 1u);
    const float sigmaWorld = pixelSpacing * (float)step * 2.0;
    const float invTwoSigmaWorldSq = 1.0 / (2.0 * sigmaWorld * sigmaWorld);

    // 5x5 B-spline a-trous kernel (separable [1,4,6,4,1]/16), sampled at +-{1,2}*step. Edge-AVOIDING: each tap is
    // weighted by the spatial kernel AND a world-distance edge-stop so the caustic never bleeds across a depth /
    // silhouette discontinuity (the receiver edge), while the sparse inter-photon gaps fill into a smooth caustic.
    // No luminance edge-stop: the sparse splat's bright deposits ARE the noise to remove, so preserving them would
    // defeat the denoise; the focused caustic STRUCTURE survives as the low-frequency content the wavelet preserves.
    const float kernel[5] = { 1.0 / 16.0, 4.0 / 16.0, 6.0 / 16.0, 4.0 / 16.0, 1.0 / 16.0 };

    float3 sum = float3(0.0, 0.0, 0.0);
    float weightSum = 0.0;
    [unroll] for(int ky = -2; ky <= 2; ++ky){
        [unroll] for(int kx = -2; kx <= 2; ++kx){
            const int2 tap = int2(texel.x + kx * step, texel.y + ky * step);
            if(tap.x < 0 || tap.y < 0 || tap.x >= w || tap.y >= h)
                continue;
            if(g_NwbCausticResolveGBufferDepth.Load(int3(tap, 0)).r >= 0.999999)
                continue; // background tap: no receiver -> skip so the dark surround cannot dilute the caustic edge
            const float3 tapWorld = g_NwbCausticResolveGBufferWorldPosition.Load(int3(tap, 0)).xyz;
            const float3 d = tapWorld - centerWorld;
            const float wWorld = exp(-dot(d, d) * invTwoSigmaWorldSq);
            const float wSpatial = kernel[kx + 2] * kernel[ky + 2];
            const float weight = wSpatial * wWorld;
            sum += weight * nwbCausticResolveSampleInput(tap);
            weightSum += weight;
        }
    }

    const float3 result = (weightSum > 0.0) ? (sum / weightSum) : float3(0.0, 0.0, 0.0);
    g_NwbCausticResolveOutput[pixel] = float4(result, 1.0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
