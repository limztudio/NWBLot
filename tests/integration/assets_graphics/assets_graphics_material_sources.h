// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/global.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Material bind interfaces and material metadata, including the invalid variants the cook must reject.
struct AssetsGraphicsMaterialSources{
    static constexpr AStringView s_MinimalMaterialBindSource = R"NWB_BIND([material_constant]
    struct NwbTestSurfaceMaterial{
    [default("float4(1.0, 1.0, 1.0, 1.0)")]
    float4 base_color;

    [default("float(0.5)")]
    float roughness;

    [default("int2(1, 2)")]
    int2 layer_ids;

    [default("uint3(4u, 5u, 6u)")]
    uint3 feature_mask;

    [default("bool4(true, false, true, false)")]
    bool4 channel_enabled;
    };

    [material_mutable]
    struct NwbTestRuntimeMaterial{
    [default("float(1.0)")]
    float fade_alpha;
    };

    NwbTestSurfaceMaterial surface;
    NwbTestRuntimeMaterial runtime;

    )NWB_BIND";
    static constexpr AStringView s_UpdatedDefaultMaterialBindSource = R"NWB_BIND([material_constant]
    struct NwbTestSurfaceMaterial{
    [default("float4(1.0, 1.0, 1.0, 1.0)")]
    float4 base_color;

    [default("float(0.5)")]
    float roughness;

    [default("int2(1, 2)")]
    int2 layer_ids;

    [default("uint3(7u, 8u, 9u)")]
    uint3 feature_mask;

    [default("bool4(true, false, true, false)")]
    bool4 channel_enabled;
    };

    [material_mutable]
    struct NwbTestRuntimeMaterial{
    [default("float(1.0)")]
    float fade_alpha;
    };

    NwbTestSurfaceMaterial surface;
    NwbTestRuntimeMaterial runtime;

    )NWB_BIND";
    static constexpr AStringView s_ShadowDispatchFirstMaterialBindSource = R"NWB_BIND([material_constant]
    struct NwbShadowDispatchFirstSurfaceMaterial{
    [default("half4(0.25h, 0.5h, 0.75h, 1.0h)")]
    half4 base_color;
    };

    NwbShadowDispatchFirstSurfaceMaterial surface;

    )NWB_BIND";
    static constexpr AStringView s_ShadowDispatchSecondMaterialBindSource = R"NWB_BIND([material_constant]
    struct NwbShadowDispatchSecondSurfaceMaterial{
    [default("half4(0.75h, 0.5h, 0.25h, 1.0h)")]
    half4 base_color;
    };

    NwbShadowDispatchSecondSurfaceMaterial surface;

    )NWB_BIND";
    static constexpr AStringView s_ShadowDispatchFirstMaterialMeta = R"NWB_META(material asset;

    asset.interface = "project/material_interfaces/shadow_dispatch_first.bind";
    asset.surface = "project/shaders/shadow_dispatch_first.surface";
    asset.bxdf = "project/shaders/shadow_dispatch.bxdf";
    asset.transparent = 0;
    asset.two_sided = 0;
    asset.refractive = 0;
    asset.shader_variant = "default";

    asset.parameters = {
    "surface": {
        "base_color": "half4(0.25h, 0.5h, 0.75h, 1.0h)",
    },
    };

    )NWB_META";
    static constexpr AStringView s_ShadowDispatchSecondMaterialMeta = R"NWB_META(material asset;

    asset.interface = "project/material_interfaces/shadow_dispatch_second.bind";
    asset.surface = "project/shaders/shadow_dispatch_second.surface";
    asset.bxdf = "project/shaders/shadow_dispatch.bxdf";
    asset.transparent = 0;
    asset.two_sided = 0;
    asset.refractive = 0;
    asset.shader_variant = "default";

    asset.parameters = {
    "surface": {
        "base_color": "half4(0.75h, 0.5h, 0.25h, 1.0h)",
    },
    };

    )NWB_META";
    static constexpr AStringView s_BlockScopedMaterialMeta = R"NWB_META(material asset;

    asset.interface = "project/material_interfaces/test_surface.bind";
    asset.bxdf = "project/shaders/material_bxdf.bxdf";
    asset.transparent = 0;
    asset.two_sided = 0;
    asset.refractive = 0;

    asset.shaders = { "mesh": "project/shaders/material_mesh", "ps": "project/shaders/material_ps", };
    asset.shader_variant = "default";

    asset.parameters = {
    "surface": {
        "base_color": "float4(0.25, 0.5, 0.75, 1.0)",
        "roughness": "float(0.25)",
    },
    "runtime": {
        "fade_alpha": "float(0.75)",
    },
    };

    )NWB_META";
    static constexpr AStringView s_TransparentMaterialMeta = R"NWB_META(material asset;

    asset.interface = "project/material_interfaces/test_surface.bind";
    asset.bxdf = "project/shaders/material_bxdf.bxdf";
    asset.surface = "project/shaders/material_surface.surface";
    asset.transparent = 1;
    asset.two_sided = 0;
    asset.refractive = 0;
    asset.shader_variant = "default";

    asset.parameters = {
    "surface": {
        "base_color": "float4(0.25, 0.5, 0.75, 1.0)",
        "roughness": "float(0.25)",
    },
    "runtime": {
        "fade_alpha": "float(0.75)",
    },
    };

    )NWB_META";
    #if defined(NWB_FINAL)
    static constexpr AStringView s_RefractiveMaterialMeta = R"NWB_META(material asset;

    asset.interface = "project/material_interfaces/test_surface.bind";
    asset.bxdf = "project/shaders/material_bxdf.bxdf";
    asset.surface = "project/shaders/material_surface.surface";
    asset.transparent = 1;
    asset.two_sided = 0;
    asset.refractive = 1;
    asset.shader_variant = "default";

    asset.parameters = {
    "surface": {
        "base_color": "float4(0.25, 0.5, 0.75, 1.0)",
        "roughness": "float(0.25)",
    },
    "runtime": {
        "fade_alpha": "float(0.75)",
    },
    };

    )NWB_META";
    static constexpr AStringView s_ExplicitTransparentMaterialMeta = R"NWB_META(material asset;

    asset.interface = "project/material_interfaces/test_surface.bind";
    asset.bxdf = "project/shaders/material_bxdf.bxdf";
    asset.transparent = 1;
    asset.two_sided = 0;
    asset.refractive = 0;

    asset.shaders = { "mesh": "project/shaders/material_mesh", "ps": "project/shaders/material_ps", };
    asset.shader_variant = "default";

    )NWB_META";
    static constexpr AStringView s_ExplicitRefractiveMaterialMeta = R"NWB_META(material asset;

    asset.interface = "project/material_interfaces/test_surface.bind";
    asset.bxdf = "project/shaders/material_bxdf.bxdf";
    asset.transparent = 0;
    asset.two_sided = 0;
    asset.refractive = 1;

    asset.shaders = { "mesh": "project/shaders/material_mesh", "ps": "project/shaders/material_ps", };
    asset.shader_variant = "default";

    )NWB_META";
    static constexpr AStringView s_MissingInterfaceMaterialMeta = R"NWB_META(material asset;

    asset.shaders = { "mesh": "project/shaders/material_mesh", "ps": "project/shaders/material_ps", };
    asset.shader_variant = "default";

    asset.parameters = { "base_color": "float4(0.25, 0.5, 0.75, 1.0)", };

    )NWB_META";
    static constexpr AStringView s_UnsupportedMaterialFieldMeta = R"NWB_META(material asset;

    asset.interface = "project/material_interfaces/test_surface.bind";
    asset.compiler = "unsupported";

    asset.shaders = { "mesh": "project/shaders/material_mesh", "ps": "project/shaders/material_ps", };
    asset.shader_variant = "default";

    )NWB_META";
    static constexpr AStringView s_UnknownInterfaceParameterMaterialMeta = R"NWB_META(material asset;

    asset.interface = "project/material_interfaces/test_surface.bind";
    asset.bxdf = "project/shaders/material_bxdf.bxdf";
    asset.transparent = 0;
    asset.two_sided = 0;
    asset.refractive = 0;

    asset.shaders = { "mesh": "project/shaders/material_mesh", "ps": "project/shaders/material_ps", };
    asset.shader_variant = "default";

    asset.parameters = {
    "surface": {
        "missing": "float(1.0)",
    },
    };

    )NWB_META";
    static constexpr AStringView s_FlatInterfaceParameterMaterialMeta = R"NWB_META(material asset;

    asset.interface = "project/material_interfaces/test_surface.bind";
    asset.bxdf = "project/shaders/material_bxdf.bxdf";
    asset.transparent = 0;
    asset.two_sided = 0;
    asset.refractive = 0;

    asset.shaders = { "mesh": "project/shaders/material_mesh", "ps": "project/shaders/material_ps", };
    asset.shader_variant = "default";

    asset.parameters = { "runtime.fade_alpha": "float(0.75)", };

    )NWB_META";
    static constexpr AStringView s_UntypedMaterialParameterMeta = R"NWB_META(material asset;

    asset.interface = "project/material_interfaces/test_surface.bind";
    asset.bxdf = "project/shaders/material_bxdf.bxdf";
    asset.transparent = 0;
    asset.two_sided = 0;
    asset.refractive = 0;

    asset.shaders = { "mesh": "project/shaders/material_mesh", "ps": "project/shaders/material_ps", };
    asset.shader_variant = "default";

    asset.parameters = {
    "surface": {
        "base_color": "0.25, 0.5, 0.75, 1.0",
    },
    };

    )NWB_META";
    static constexpr AStringView s_VectorAliasMaterialParameterMeta = R"NWB_META(material asset;

    asset.interface = "project/material_interfaces/test_surface.bind";
    asset.bxdf = "project/shaders/material_bxdf.bxdf";
    asset.transparent = 0;
    asset.two_sided = 0;
    asset.refractive = 0;

    asset.shaders = { "mesh": "project/shaders/material_mesh", "ps": "project/shaders/material_ps", };
    asset.shader_variant = "default";

    asset.parameters = {
    "surface": {
        "base_color": "vec4(0.25, 0.5, 0.75, 1.0)",
    },
    };

    )NWB_META";
    static constexpr AStringView s_DuplicateFieldMaterialBindSource = R"NWB_BIND([material_constant]
    struct NwbTestSurfaceMaterial{
    float base_color;
    float base_color;
    };

    NwbTestSurfaceMaterial surface;

    )NWB_BIND";
    #endif
    static constexpr AStringView s_DuplicateInstanceMaterialBindSource = R"NWB_BIND([material_constant]
    struct NwbTestSurfaceMaterial{
    [default("float(0.5)")]
    float roughness;
    };

    NwbTestSurfaceMaterial surface;
    NwbTestSurfaceMaterial surface;

    )NWB_BIND";
    #if defined(NWB_FINAL)
    static constexpr AStringView s_SurfaceOnlyMaterialBindSource = R"NWB_BIND([material_constant]
    struct NwbTestSurfaceMaterial{
    [default("float4(1.0, 1.0, 1.0, 1.0)")]
    float4 base_color;

    [default("float(0.5)")]
    float roughness;
    };

    NwbTestSurfaceMaterial surface;

    )NWB_BIND";
    #endif
    static constexpr AStringView s_InstanceOverrideMaterialBindSource = R"NWB_BIND(asset.instance_override = "unsupported";

    [material_constant]
    struct NwbTestSurfaceMaterial{
    [default("float(0.5)")]
    float roughness;
    };

    NwbTestSurfaceMaterial surface;

    )NWB_BIND";
    static constexpr AStringView s_Float1DefaultMaterialBindSource = R"NWB_BIND([material_constant]
    struct NwbTestSurfaceMaterial{
    [default("float1(1.0)")]
    float base_color;
    };

    NwbTestSurfaceMaterial surface;

    )NWB_BIND";
    static constexpr AStringView s_UnknownBlockClassMaterialBindSource = R"NWB_BIND([material_project]
    struct NwbTestSurfaceMaterial{
    float base_color;
    };

    NwbTestSurfaceMaterial surface;

    )NWB_BIND";
    static constexpr AStringView s_UnsupportedFieldTypeMaterialBindSource = R"NWB_BIND([material_constant]
    struct NwbTestSurfaceMaterial{
    [default("double(0.5)")]
    double roughness;
    };

    NwbTestSurfaceMaterial surface;

    )NWB_BIND";
    static constexpr AStringView s_InvalidDefaultMaterialBindSource = R"NWB_BIND([material_constant]
    struct NwbTestSurfaceMaterial{
    [default("")]
    float roughness;
    };

    NwbTestSurfaceMaterial surface;

    )NWB_BIND";
    static constexpr AStringView s_MissingDefaultMaterialBindSource = R"NWB_BIND([material_constant]
    struct NwbTestSurfaceMaterial{
    float roughness;
    };

    NwbTestSurfaceMaterial surface;

    )NWB_BIND";
    static constexpr AStringView s_ResourceAttributeMaterialBindSource = R"NWB_BIND([material_constant]
    struct NwbTestSurfaceMaterial{
    [texture_asset("project/textures/legacy")] texture2d base_color_map;
    };

    NwbTestSurfaceMaterial surface;

    )NWB_BIND";
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

