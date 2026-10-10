// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cap_surface.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbCsgIntervalCapFillPixelOutput{
    [[vk::location(NWB_MESH_GBUFFER_BASE_COLOR_LOCATION)]] float4 baseColor : SV_Target0;
    [[vk::location(NWB_MESH_GBUFFER_NORMAL_LOCATION)]] float4 normal : SV_Target1;
    [[vk::location(NWB_MESH_GBUFFER_WORLD_POSITION_LOCATION)]] float4 worldPosition : SV_Target2;
    [[vk::location(NWB_MESH_GBUFFER_SPECULAR_ROUGHNESS_LOCATION)]] float4 specularRoughness : SV_Target3;
    float depth : SV_Depth;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


half3 nwbCsgCapFillEncodeNormal(const float3 normal){
    const float lengthSquared = dot(normal, normal);
    const float3 normalVector = lengthSquared > NWB_CSG_CAP_NORMALIZATION_LENGTH_SQUARED_EPSILON
        ? normal * rsqrt(lengthSquared)
        : float3(0.0, 0.0, 1.0)
    ;
    return half3(normalVector * 0.5 + 0.5);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbCsgIntervalCapFillPixelOutput main(const NwbCsgCapFillPixelInput input){
    const NwbCsgIntervalCapPixel cap = nwbCsgSampleNearestIntervalCap(input.position, NWB_CSG_INVALID_RECEIVER_INDEX);
    if(!cap.visible)
        discard;

    const NwbCsgReceiverRange receiver = g_NwbCsgReceiverRanges[cap.receiverIndex];
    const NwbMeshSurface surface = nwbCsgCapSurface(cap, receiver, -cap.normal);

    NwbCsgIntervalCapFillPixelOutput output;
    // baseColor.a carries the deferred BXDF id, while the other packed fields exactly match the mesh G-buffer.
    output.baseColor = half4(clamp(surface.baseColor, half3(0.0h), half3(1.0h)), half(float(receiver.rangeInfo.w)));
    output.normal = half4(nwbCsgCapFillEncodeNormal(surface.normal), surface.param0);
    output.worldPosition = half4(half3(cap.worldPosition), surface.param1);
    output.specularRoughness = nwbPackMeshSurfaceReflection(surface.specularF0, surface.perceptualRoughness);
    output.depth = cap.depth;
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

