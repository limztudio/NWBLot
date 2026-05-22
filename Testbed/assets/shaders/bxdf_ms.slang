// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh_shader_authoring.slangi"
#include "bxdf.slangi"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static const uint2 NWB_PROJECT_MATERIAL_BASE_COLOR = uint2(0x75bad3d8u, 0x5c10c7e2u);
static const uint2 NWB_PROJECT_MATERIAL_COLOR_TINT = uint2(0x360abc2cu, 0xca7492edu);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


float4 nwbProjectBuildClipPosition(float3 worldPosition){
    return nwbMeshTransformWorldToClip(worldPosition);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NwbMeshGeneratedVertex nwbMeshBuildVertex(
    uint triangleIndex,
    uint corner,
    NwbMeshSourceVertex source,
    const NwbMeshInstanceData instance
){
    NwbMeshGeneratedVertex generatedVertex;
    const float3 worldPosition = nwbMeshTransformPosition(source.position, instance);
    generatedVertex.position = nwbProjectBuildClipPosition(worldPosition);
    generatedVertex.normal = nwbMeshTransformDirection(source.normal, instance);
    generatedVertex.padding0 = 0.0;
    generatedVertex.tangent = float4(nwbMeshTransformDirection(source.tangent.xyz, instance), source.tangent.w);
    generatedVertex.uv0 = source.uv0;
    generatedVertex.padding1 = float2(0.0);
    const float4 materialColor = nwbMaterialFindFloat4(instance, NWB_PROJECT_MATERIAL_BASE_COLOR, source.color);
    const float4 tintedColor = materialColor * nwbMaterialFindFloat4(instance, NWB_PROJECT_MATERIAL_COLOR_TINT, float4(1.0));
    generatedVertex.color = float4(
        clamp(nwbProjectBxdfVertex(tintedColor.rgb), float3(0.0), float3(1.0)),
        tintedColor.a
    );
    generatedVertex.worldPosition = float4(worldPosition, 1.0);
    return generatedVertex;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

