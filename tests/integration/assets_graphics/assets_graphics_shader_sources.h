// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/global.h>
#include <impl/assets/graphics/mesh/runtime_constants.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_ASSETS_GRAPHICS_TEST_STRINGIFY_IMPL(Value) #Value
#define NWB_ASSETS_GRAPHICS_TEST_STRINGIFY(Value) NWB_ASSETS_GRAPHICS_TEST_STRINGIFY_IMPL(Value)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Slang probe sources compiled against the generated material bind includes.
struct AssetsGraphicsShaderSources{
    static constexpr AStringView s_MaterialBindMeshSource = R"NWB_SLANG(#include "mesh/authoring.slangi"

    NwbMeshGeneratedVertex nwbMeshBuildVertex(uint triangleIndex, uint corner, NwbMeshSourceVertex source, const NwbMeshInstanceData instance){
    NwbMeshGeneratedVertex generatedVertex;
    const float3 worldPosition = nwbMeshTransformPosition(source.position, instance);
    generatedVertex.position = nwbMeshTransformWorldToClip(worldPosition);
    nwbMeshSetGeneratedVertexNormal(generatedVertex, half4(half3(nwbMeshTransformDirection(source.normal, instance)), half(0.0)));
    nwbMeshSetGeneratedVertexTangent(generatedVertex, half4(float4(nwbMeshTransformDirection(source.tangent.xyz, instance), source.tangent.w)));
    generatedVertex.uv0 = source.uv0;
    nwbMeshSetGeneratedVertexColor(generatedVertex, source.color);
    generatedVertex.worldPosition = float4(worldPosition, 1.0);
    return generatedVertex;
    }

    )NWB_SLANG";
    static constexpr AStringView s_MaterialBindShaderProbeSource = R"NWB_SLANG(#include "mesh/material_ps_authoring.slangi"
    #include "project/material_interfaces/test_surface.bind"

    NwbMeshSurface nwbMaterialSurface(){
    const uint2 materialLayoutHash = NWB_MATERIAL_BIND_LAYOUT_HASH;
    const uint2 materialInterfaceHash0 = NWB_MATERIAL_BIND_INTERFACE_HASH_0;
    const bool materialBindConstantsValid =
        materialLayoutHash.x != 0u
        && materialInterfaceHash0.x != 0u
        && NWB_MATERIAL_BIND_CONSTANT_BYTE_SIZE == 44u
        && NWB_MATERIAL_BIND_MUTABLE_BYTE_SIZE == 4u
        && NWB_MATERIAL_BIND_RUNTIME_STORAGE == NWB_MATERIAL_BIND_STORAGE_MUTABLE
        && NWB_MATERIAL_BIND_RUNTIME_BYTE_OFFSET == 0u
        && NWB_MATERIAL_BIND_RUNTIME_BYTE_SIZE == 4u
        && NWB_MATERIAL_BIND_SURFACE_STORAGE == NWB_MATERIAL_BIND_STORAGE_CONSTANT
        && NWB_MATERIAL_BIND_SURFACE_BYTE_OFFSET == 0u
        && NWB_MATERIAL_BIND_SURFACE_BYTE_SIZE == 44u;
    const NwbMeshInstanceData instance = nwbMeshLoadInstance();
    const NwbTestSurfaceMaterial surface = nwbMaterialBindLoadSurface(instance);
    return nwbMakeMeshSurface(materialBindConstantsValid ? half3(surface.base_color.rgb) : half3(1.0h, 0.0h, 1.0h), inNormal);
    }

    )NWB_SLANG";
    static constexpr AStringView s_ShadowDispatchSharedSurfaceHelperSource = R"NWB_SLANG(#ifndef NWB_TEST_SHADOW_DISPATCH_SHARED_SURFACE_HELPER_SLANGI
    #define NWB_TEST_SHADOW_DISPATCH_SHARED_SURFACE_HELPER_SLANGI

    half3 nwbTestShadowDispatchSharedSurfaceColor(const half3 color){
    return color;
    }

    #endif

    )NWB_SLANG";
    static constexpr AStringView s_ShadowDispatchFirstMaterialSurfaceSource = R"NWB_SLANG(#include "shadow_dispatch_shared_surface_helper.slangi"

    NwbMeshSurface nwbMaterialSurface(){
    const NwbMeshInstanceData instance = nwbMeshLoadInstance();
    const NwbShadowDispatchFirstSurfaceMaterial surface = nwbMaterialBindLoadSurface(instance);
    return nwbMakeMeshSurface(nwbTestShadowDispatchSharedSurfaceColor(surface.base_color.rgb), inNormal);
    }

    )NWB_SLANG";
    static constexpr AStringView s_ShadowDispatchSecondMaterialSurfaceSource = R"NWB_SLANG(#include "shadow_dispatch_shared_surface_helper.slangi"

    NwbMeshSurface nwbMaterialSurface(){
    const NwbMeshInstanceData instance = nwbMeshLoadInstance();
    const NwbShadowDispatchSecondSurfaceMaterial surface = nwbMaterialBindLoadSurface(instance);
    return nwbMakeMeshSurface(nwbTestShadowDispatchSharedSurfaceColor(surface.base_color.rgb), inNormal);
    }

    )NWB_SLANG";
    #if defined(NWB_FINAL)
    static constexpr AStringView s_OtherMaterialBindShaderProbeSource = R"NWB_SLANG(#include "mesh/material_ps_authoring.slangi"
    #include "project/material_interfaces/other_surface.bind"

    NwbMeshSurface nwbMaterialSurface(){
    const NwbMeshInstanceData instance = nwbMeshLoadInstance();
    const NwbTestSurfaceMaterial surface = nwbMaterialBindLoadSurface(instance);
    return nwbMakeMeshSurface(half3(surface.base_color.rgb), inNormal);
    }

    )NWB_SLANG";
    static constexpr AStringView s_UnboundMaterialShaderProbeSource =
    "#define NWB_MESH_GBUFFER_BASE_COLOR_LOCATION " NWB_ASSETS_GRAPHICS_TEST_STRINGIFY(NWB_MESH_GBUFFER_BASE_COLOR_LOCATION) "\n"
    "#define NWB_MESH_GBUFFER_NORMAL_LOCATION " NWB_ASSETS_GRAPHICS_TEST_STRINGIFY(NWB_MESH_GBUFFER_NORMAL_LOCATION) "\n"
    "#define NWB_MESH_GBUFFER_WORLD_POSITION_LOCATION " NWB_ASSETS_GRAPHICS_TEST_STRINGIFY(NWB_MESH_GBUFFER_WORLD_POSITION_LOCATION) "\n"
    R"NWB_SLANG(
    struct NwbUnboundMaterialPixelOutput{
    [[vk::location(NWB_MESH_GBUFFER_BASE_COLOR_LOCATION)]] float4 baseColor : SV_Target0;
    [[vk::location(NWB_MESH_GBUFFER_NORMAL_LOCATION)]] float4 normal : SV_Target1;
    [[vk::location(NWB_MESH_GBUFFER_WORLD_POSITION_LOCATION)]] float4 worldPosition : SV_Target2;
    };

    NwbUnboundMaterialPixelOutput main(){
    NwbUnboundMaterialPixelOutput output;
    output.baseColor = float4(1.0, 1.0, 1.0, 0.0);
    output.normal = float4(0.5, 0.5, 1.0, 0.0);
    output.worldPosition = float4(0.0, 0.0, 0.0, 0.0);
    return output;
    }

    )NWB_SLANG";
    #endif
    static constexpr AStringView s_MaterialBindBxdfSource = "half3 NWB_DEFERRED_BXDF_FUNCTION(NwbBxdfSurface surface, int2 pixel){ return surface.baseColor; }\n";
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

