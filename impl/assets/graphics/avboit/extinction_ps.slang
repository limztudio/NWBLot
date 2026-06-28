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
    [[vk::location(NWB_MESH_RASTER_COLOR_LOCATION)]] float4 color : COLOR0;
#if NWB_CSG_ENABLED
    [[vk::location(NWB_MESH_RASTER_WORLD_POSITION_LOCATION)]] float4 worldPosition : TEXCOORD1;
#endif
};

static half4 inColor;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void nwbAvboitSplatExtinction(uint2 lowPixel, uint virtualSlice, float extinction){
    if(extinction <= 0.0)
        return;

    const uint activePhysicalSlices = max(min(g_Control[NWB_AVBOIT_CONTROL_ACTIVE_PHYSICAL_SLICES], nwbAvboitPhysicalSliceCount()), 1u);
    const uint physicalSlice = min(g_DepthWarp[virtualSlice], activePhysicalSlices - 1u);
    const uint wordIndex = nwbAvboitExtinctionWordIndex(lowPixel, physicalSlice);
    if(wordIndex >= nwbAvboitExtinctionWordCount())
        return;

    const uint packedExtinction = nwbAvboitPackExtinctionByte(extinction);
    if(packedExtinction == 0u)
        return;

    const uint byteShift = nwbAvboitExtinctionByteShift(physicalSlice);
    uint previousWord;
    InterlockedAdd(g_Extinction[wordIndex], packedExtinction << byteShift, previousWord);
    const uint previousByte = (previousWord >> byteShift) & NWB_AVBOIT_EXTINCTION_BYTE_MASK;
    const bool byteOverflow = previousByte + packedExtinction > NWB_AVBOIT_EXTINCTION_BYTE_MASK;
    const float previousExtinction = float(previousByte) / nwbAvboitExtinctionFixedScale();
    const bool rawOverflow = previousExtinction + extinction > nwbAvboitExtinctionSaturation();
    if(byteOverflow || rawOverflow)
        InterlockedMin(g_ExtinctionOverflowDepth[nwbAvboitLowPixelIndex(lowPixel)], physicalSlice);
}


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

    uint virtualSlice0;
    uint virtualSlice1;
    float sliceWeight0;
    float sliceWeight1;
    nwbAvboitLinearVirtualSlicesFromDepth(fragCoord.z, virtualSlice0, virtualSlice1, sliceWeight0, sliceWeight1);

    if(sliceWeight0 > NWB_AVBOIT_SPLAT_WEIGHT_EPSILON)
        nwbAvboitSplatExtinction(lowPixel, virtualSlice0, extinction * sliceWeight0);
    if(virtualSlice1 != virtualSlice0 && sliceWeight1 > NWB_AVBOIT_SPLAT_WEIGHT_EPSILON)
        nwbAvboitSplatExtinction(lowPixel, virtualSlice1, extinction * sliceWeight1);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

