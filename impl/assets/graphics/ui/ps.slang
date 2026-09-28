// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "push_constants.slangi"
#include "glyph_coverage.slangi"
#include "../bindless/bindless_heap.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbUiPixelInput{
    [[vk::location(NWB_UI_RASTER_UV_LOCATION)]] float2 uv : TEXCOORD0;
    [[vk::location(NWB_UI_RASTER_COLOR_LOCATION)]] float4 color : COLOR0;
};

struct NwbUiPixelOutput{
    [[vk::location(NWB_UI_COLOR_TARGET_LOCATION)]] float4 color : SV_Target0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbUiPixelOutput main(NwbUiPixelInput input){
    NwbUiPixelOutput output;
    output.color = input.color;
    if(g_NwbUiPushConstants.material == NWB_UI_MATERIAL_SOLID)
        return output;

    const Texture2D<float4> texture = NwbHeapSampledImage2D(g_NwbUiPushConstants.textureSlot);
    const SamplerState sampler = NwbHeapSampler(g_NwbUiPushConstants.samplerSlot);
    // Base mip sampling preserves atlas gutters. R8 glyph views contain linear coverage without sRGB conversion.
    const float4 sampled = texture.SampleLevel(sampler, input.uv, 0.0);
    if(g_NwbUiPushConstants.material == NWB_UI_MATERIAL_GLYPH){
        output.color = nwbUiGlyphCoverageColor(input.color, sampled.r);
        return output;
    }
    // The skin sRGB view decodes RGB while alpha stays linear.
    // Vertex RGB already includes tint alpha. Premultiply the straight texture sample once for the linear UI layer.
    output.color = float4(sampled.rgb * sampled.a * input.color.rgb, sampled.a * input.color.a);
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

