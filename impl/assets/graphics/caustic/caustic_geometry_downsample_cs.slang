// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "resolve_binding_slots.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Geometry downsample pre-pass for the half-res caustic resolve. The resolve runs at half resolution but its edge-stop +
// area Jacobian need the receiver world position, and the wavelet re-reads it many times per pixel (5x5 taps x N passes).
// Reading the FULL-res world-position + depth G-buffer at the half pixel's 2x location every tap is a scattered, cache-
// unfriendly load that dominates the resolve's bandwidth (especially on a bandwidth-bound integrated GPU). This pass
// resolves that ONCE: each half-res texel point-samples the full-res G-buffer at its top-left full texel and writes a
// single compact half-res RGBA16F record -- xyz = world position, w = receiver validity (1 = receiver, 0 = background) --
// that every resolve pass then taps as one half-res read. Same geometry the per-tap full-res reads would have used (a 2x
// point sample), so there is no quality change; it only removes redundant full-res bandwidth.

[[vk::binding(NWB_CAUSTIC_GEOMETRY_DOWNSAMPLE_BINDING_GBUFFER_WORLD_POSITION, NWB_CAUSTIC_GEOMETRY_DOWNSAMPLE_SET)]]
Texture2D<float4> g_NwbCausticGeometryWorldPosition;

[[vk::binding(NWB_CAUSTIC_GEOMETRY_DOWNSAMPLE_BINDING_GBUFFER_DEPTH, NWB_CAUSTIC_GEOMETRY_DOWNSAMPLE_SET)]]
Texture2D<float4> g_NwbCausticGeometryDepth;

[[vk::binding(NWB_CAUSTIC_GEOMETRY_DOWNSAMPLE_BINDING_GEOMETRY_OUTPUT, NWB_CAUSTIC_GEOMETRY_DOWNSAMPLE_SET)]]
RWTexture2D<float4> g_NwbCausticGeometryOutput;

struct NwbCausticGeometryDownsamplePushConstants{
    uint width;       // full-res G-buffer width
    uint height;      // full-res G-buffer height
    uint halfWidth;   // half-res output width
    uint halfHeight;  // half-res output height
};

[[vk::push_constant]]
ConstantBuffer<NwbCausticGeometryDownsamplePushConstants> g_NwbCausticGeometryDownsamplePushConstants;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[numthreads(NWB_CAUSTIC_RESOLVE_GROUP_SIZE, NWB_CAUSTIC_RESOLVE_GROUP_SIZE, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID){
    const uint2 halfTexel = dispatchThreadID.xy;
    if(halfTexel.x >= g_NwbCausticGeometryDownsamplePushConstants.halfWidth || halfTexel.y >= g_NwbCausticGeometryDownsamplePushConstants.halfHeight)
        return;

    // Top-left full-res texel of this half-res pixel's 2x2 block (clamped), matching the resolve's half->full point sample.
    const int2 full = int2(
        min((int)halfTexel.x * 2, (int)g_NwbCausticGeometryDownsamplePushConstants.width - 1),
        min((int)halfTexel.y * 2, (int)g_NwbCausticGeometryDownsamplePushConstants.height - 1)
    );

    const float3 world = g_NwbCausticGeometryWorldPosition.Load(int3(full, 0)).xyz;
    const float depth = g_NwbCausticGeometryDepth.Load(int3(full, 0)).r;
    const float validity = (depth >= 0.999999) ? 0.0 : 1.0; // background (no receiver) -> 0
    g_NwbCausticGeometryOutput[halfTexel] = float4(world, validity);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
