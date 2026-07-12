// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "common.slangi"
#include "binding_slots.h"
#include "../mesh/runtime_constants.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[vk::binding(NWB_AVBOIT_BINDING_OPAQUE_DEPTH, NWB_AVBOIT_TRANSPARENT_SET)]] Texture2D<float4> g_OpaqueDepth;
[[vk::binding(NWB_AVBOIT_BINDING_POINT_SAMPLER, NWB_AVBOIT_TRANSPARENT_SET)]] SamplerState g_PointSampler;
[[vk::binding(NWB_AVBOIT_OCCUPANCY_BINDING_COVERAGE_WORDS, NWB_AVBOIT_TRANSPARENT_SET)]] RWStructuredBuffer<uint, Std430DataLayout> g_CoverageWords;

struct NwbAvboitOccupancyPixelInput{
    [[vk::location(NWB_MESH_RASTER_COLOR_LOCATION)]] half4 color : COLOR0;
#if NWB_CSG_ENABLED
    [[vk::location(NWB_MESH_RASTER_WORLD_POSITION_LOCATION)]] float4 worldPosition : TEXCOORD1;
#endif
};

static half4 inColor;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "coverage_splat.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void main(NwbAvboitOccupancyPixelInput input, float4 fragCoord : SV_Position){
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

    nwbAvboitMarkCoverageFromDepth(fragCoord.z);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

