// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "common.slangi"
#include "binding_slots.h"
#include "../mesh/runtime_constants.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::binding(NWB_AVBOIT_BINDING_OPAQUE_DEPTH, NWB_AVBOIT_TRANSPARENT_SET)]] Texture2D<float4> g_OpaqueDepth;
[[vk::binding(NWB_AVBOIT_BINDING_POINT_SAMPLER, NWB_AVBOIT_TRANSPARENT_SET)]] SamplerState g_PointSampler;
[[vk::binding(NWB_AVBOIT_EXTINCTION_BINDING_DEPTH_WARP, NWB_AVBOIT_TRANSPARENT_SET)]] StructuredBuffer<uint, Std430DataLayout> g_DepthWarp;
[[vk::binding(NWB_AVBOIT_EXTINCTION_BINDING_CONTROL, NWB_AVBOIT_TRANSPARENT_SET)]] StructuredBuffer<uint, Std430DataLayout> g_Control;
[[vk::binding(NWB_AVBOIT_EXTINCTION_BINDING_EXTINCTION, NWB_AVBOIT_TRANSPARENT_SET)]] RWStructuredBuffer<uint, Std430DataLayout> g_Extinction;
[[vk::binding(NWB_AVBOIT_EXTINCTION_BINDING_OVERFLOW_DEPTH, NWB_AVBOIT_TRANSPARENT_SET)]] RWStructuredBuffer<uint, Std430DataLayout> g_ExtinctionOverflowDepth;

struct NwbAvboitExtinctionPixelInput{
    [[vk::location(NWB_MESH_RASTER_COLOR_LOCATION)]] half4 color : COLOR0;
#if NWB_CSG_ENABLED
    [[vk::location(NWB_MESH_RASTER_WORLD_POSITION_LOCATION)]] float4 worldPosition : TEXCOORD1;
#endif
};

static half4 inColor;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "extinction_splat.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void main(NwbAvboitExtinctionPixelInput input, float4 fragCoord : SV_Position){
    inColor = input.color;
#if NWB_CSG_ENABLED
    if(nwbAvboitCsgPixelDiscard(
        nwbAvboitFullRasterPositionFromLowFragCoord(fragCoord),
        input.worldPosition
    ))
        discard;
#endif

    const half alpha = nwbAvboitResolveFragmentAlpha(inColor);
    uint virtualSlice;
    if(!nwbAvboitAcceptLowDepthFragment(g_OpaqueDepth, g_PointSampler, fragCoord, alpha, virtualSlice))
        discard;

    const uint2 lowPixel = nwbAvboitLowPixelFromLowFragCoord(fragCoord.xy);
    const float extinction = nwbAvboitExtinctionFromAlpha(alpha);
    nwbAvboitSplatExtinctionFromDepth(lowPixel, fragCoord.z, extinction);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

