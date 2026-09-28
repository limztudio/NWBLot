// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "output_push_constants.slangi"
#include "../bindless/bindless_heap.slangi"
#include "../common/hdr10.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbUiOutputPixelInput{
    [[vk::location(NWB_UI_FULLSCREEN_UV_LOCATION)]] float2 uv : TEXCOORD0;
};

struct NwbUiOutputPixelOutput{
    [[vk::location(NWB_UI_COLOR_TARGET_LOCATION)]] float4 color : SV_Target0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbUiOutputPixelOutput main(NwbUiOutputPixelInput input){
    const Texture2D<float4> texture = NwbHeapSampledImage2D(g_NwbUiOutputPushConstants.textureSlot);
    const SamplerState sampler = NwbHeapSampler(g_NwbUiOutputPushConstants.samplerSlot);
    // The sampled RGB already includes UI coverage over black. SDR encoding is supplied by the sRGB attachment.
    const float3 linearUi = texture.SampleLevel(sampler, input.uv, 0.0).rgb;
    const float3 presentationColor = g_NwbUiOutputPushConstants.presentationMode == NWB_UI_PRESENTATION_HDR10
        ? nwbHdr10EncodeUiLinear(linearUi)
        : linearUi
    ;

    NwbUiOutputPixelOutput output;
    output.color = float4(presentationColor, 1.0);
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

