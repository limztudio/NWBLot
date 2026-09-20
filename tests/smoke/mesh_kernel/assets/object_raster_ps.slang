// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <mesh/raster_fields.slangi>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NwbMeshObjectRasterOutputs{
    float4 world : SV_Target0;
    float4 normal : SV_Target1;
    float4 uvColor : SV_Target2;
    float4 tangentColor : SV_Target3;
};

NwbMeshObjectRasterOutputs main(NwbMeshRasterVertex input){
    NwbMeshObjectRasterOutputs output;
    output.world = float4(input.worldPosition.xyz, 1.0);
    output.normal = float4(input.normal, input.tangent.w);
    output.uvColor = float4(input.uv0, input.color.xy);
    output.tangentColor = float4(input.tangent.xyz, input.color.z);
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

