// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/assets_mesh/asset.h>
#include <impl/assets_mesh/meshlet_ref_codec.h>
#include <impl/assets_mesh/meshlet_payload_packing.h>
#include <impl/assets_csg/cook.h>
#include <impl/assets_graphics/gather.h>
#include <impl/assets_model/asset.h>
#include <core/assets/bunch/cook.h>
#include <pipeline/asset_builder/build.h>
#include <pipeline/asset_gatherer/gather.h>
#include <core/assets/cook_entry_registry.h>
#include <impl/assets_material/cook.h>
#include <impl/assets_material/binary_payload.h>
#include <impl/assets_shader/asset.h>
#include <impl/assets_shader/cook.h>
#include <impl/assets_texture/asset.h>
#include <impl/assets_texture/binary_payload.h>
#include <impl/assets_texture/cook.h>
#include <impl/assets_sampler/asset.h>
#include <impl/assets_sampler/binary_payload.h>
#include <impl/assets_sampler/cook.h>
#include <impl/assets/graphics/mesh/runtime_constants.h>
#include <impl/ecs_csg/shape_registry.h>

#include <core/assets/paths.h>
#include <tests/common/capturing_logger.h>
#include <tests/common/meshlet_ref_test_data.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>

#include <core/alloc/scratch.h>
#include <core/task/cpu/scheduler.h>
#include <core/common/module.h>
#include <core/mesh/classification.h>
#include <core/metascript/parser.h>
#include <core/filesystem/factory.h>
#include <core/graphics/api.h>
#include <core/graphics/shader_archive.h>
#include <core/graphics/spirv_entry_point.h>

#include <global/assert.h>
#include <global/binary.h>
#include <global/compile.h>
#include <global/cpu_topology.h>
#include <global/filesystem.h>
#include <global/hash_utils.h>
#include <global/math/convert.h>
#include <global/simdmath.h>

#include <cmath>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_ASSETS_GRAPHICS_TEST_STRINGIFY_IMPL(Value) #Value
#define NWB_ASSETS_GRAPHICS_TEST_STRINGIFY(Value) NWB_ASSETS_GRAPHICS_TEST_STRINGIFY_IMPL(Value)
#define NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_POSITIONS R"(asset.positions = [
    [-0.5, -0.5, 0.0],
    [ 0.5, -0.5, 0.0],
    [ 0.0,  0.5, 0.0],
];

)"
#define NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_NORMALS R"(asset.normals = [
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
];

)"
#define NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_TANGENTS R"(asset.tangents = [
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
];

)"
#define NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_UV0 R"(asset.uv0 = [
    [0.0, 0.0],
    [1.0, 0.0],
    [0.5, 1.0],
];

)"
#define NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_COLORS R"(asset.colors = [
    [1.0, 1.0, 1.0, 1.0],
];

)"
#define NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_INDICES R"(asset.indices = [
    [0, 1, 2],
];

)"
#define NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_VERTEX_REFS R"(asset.vertex_refs = [
    [0, 0, 0, 0, 0],
    [1, 1, 1, 1, 1],
    [2, 2, 2, 2, 2],
];

)"
#define NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_MISSING_NORMAL_VERTEX_REFS R"(asset.vertex_refs = [
    [0, 4294967295, 0, 0, 0],
    [1, 4294967295, 1, 1, 1],
    [2, 4294967295, 2, 2, 2],
];

)"
#define NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_DEFAULT_COLOR_VERTEX_REFS R"(asset.vertex_refs = [
    [0, 0, 0, 0, 0],
    [1, 1, 1, 1, 0],
    [2, 2, 2, 2, 0],
];

)"
#define NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_MISSING_TANGENT_VERTEX_REFS R"(asset.vertex_refs = [
    [0, 0, 4294967295, 0, 0],
    [1, 1, 4294967295, 1, 1],
    [2, 2, 4294967295, 2, 2],
];

)"
#define NWB_ASSETS_GRAPHICS_TEST_QUAD_NORMALS R"(asset.normals = [
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
];

)"
#define NWB_ASSETS_GRAPHICS_TEST_QUAD_TANGENTS R"(asset.tangents = [
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
];

)"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using NWB::Tests::MakeQuadTriangleIndices;
using NWB::Tests::MakeTriangleIndices;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AssetsGraphicsFixture{
public:
    using CapturingLogger = NWB::Tests::CapturingLogger;
    using AString = NWB::Tests::TestAString;
    using Path = NWB::Path;


public:

    struct AssetsGraphicsTestArenaTag{};

    using TestArena = NWB::Tests::TestArena<AssetsGraphicsTestArenaTag>;

    struct MinimalAssetCookInfo{
        const char* assetDirectory = "";
        const char* assetFilename = "";
    };

    using CookSingleMetaFn = bool(*)(AStringView, AStringView, TestArena&, Path&, Path&);
    using LoadCookedAssetFn = bool(*)(TestArena&, const Path&, UniquePtr<NWB::Core::Assets::IAsset>&);

    struct MinimalAssetKind{
        enum Enum : u8{
            Mesh = 0u,
        };
    };


public:

    static constexpr Name s_MaterialScratchArena = Name("tests/integration/assets_graphics/material");
    static constexpr Name s_MaterialCookScratchArena = Name("tests/integration/assets_graphics/material_cook");
    static constexpr Name s_ShaderScratchArena = Name("tests/integration/assets_graphics/shader");
    static constexpr Name s_ModelFixtureScratchArena = Name("tests/integration/assets_graphics/model_fixture");
    static constexpr Name s_CodecScratchArena = Name("tests/integration/assets_graphics/codec");
    static constexpr Name s_ProjectCookEntryArena = Name("tests/integration/assets_graphics/project_cook_entry");
    static constexpr AStringView s_MinimalMeshMeta =
    "mesh asset;\n\n"
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_POSITIONS
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_NORMALS
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_TANGENTS
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_UV0
    R"(asset.colors = [
    [1.0, 0.0, 0.0, 1.0],
    [0.0, 1.0, 0.0, 1.0],
    [0.0, 0.0, 1.0, 1.0],
    ];

    )" NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_VERTEX_REFS
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_INDICES;
    static constexpr AStringView s_DefaultColorMeshMeta =
    "mesh asset;\n\n"
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_POSITIONS
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_NORMALS
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_TANGENTS
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_UV0
    R"(asset.colors = [
    [1.0, 1.0, 1.0, 1.0],
    ];

    )"
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_DEFAULT_COLOR_VERTEX_REFS
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_INDICES;
    #if defined(NWB_FINAL)
    static constexpr AStringView s_TriangleNormalField = NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_NORMALS;
    static constexpr AStringView s_TriangleTangentField = NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_TANGENTS;
    static constexpr AStringView s_TriangleVertexRefsField = NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_VERTEX_REFS;
    static constexpr AStringView s_TriangleMissingNormalVertexRefsField = NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_MISSING_NORMAL_VERTEX_REFS;
    static constexpr AStringView s_TriangleMissingTangentVertexRefsField = NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_MISSING_TANGENT_VERTEX_REFS;
    static constexpr AStringView s_EmptyNormalListField = R"(asset.normals = [];

    )";
    static constexpr AStringView s_EmptyNormalMapField = R"(asset.normals = {};

    )";
    static constexpr AStringView s_EmptyTangentListField = R"(asset.tangents = [];

    )";
    static constexpr AStringView s_EmptyTangentMapField = R"(asset.tangents = {};

    )";
    #endif
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
    static constexpr AStringView s_AssetResourceMaterialBindSource = R"NWB_BIND([material_constant]
    struct NwbAssetResourceSurfaceMaterial{
    [default("float4(1.0, 1.0, 1.0, 1.0)")]
    float4 base_color;

    texture2d base_color_map;

    sampler base_color_sampler;
    };

    [material_mutable]
    struct NwbAssetResourceRuntimeMaterial{
    [default("float(1.0)")]
    float fade_alpha;
    };

    NwbAssetResourceSurfaceMaterial surface;
    NwbAssetResourceRuntimeMaterial runtime;

    )NWB_BIND";
    static constexpr AStringView s_StaticResourceFixtureMaterialBindSource = R"NWB_BIND([material_constant]
    struct NwbFixtureSurfaceMaterial{
    [fixture("builtin/material_fixture/checker_rgba8")]
    texture2d base_color_map;
    [fixture("builtin/material_fixture/linear_clamp")]
    sampler base_color_sampler;
    };

    [material_mutable]
    struct NwbFixtureRuntimeMaterial{
    [default("float(1.0)")]
    float fade_alpha;
    };

    NwbFixtureSurfaceMaterial surface;
    NwbFixtureRuntimeMaterial runtime;

    )NWB_BIND";
    static constexpr AStringView s_SecondAssetResourceMaterialBindSource = R"NWB_BIND([material_constant]
    struct NwbSecondAssetResourceSurfaceMaterial{
    [default("float4(1.0, 1.0, 1.0, 1.0)")]
    float4 base_color;

    texture2d base_color_map;

    sampler base_color_sampler;
    };

    [material_mutable]
    struct NwbSecondAssetResourceRuntimeMaterial{
    [default("float(1.0)")]
    float fade_alpha;
    };

    NwbSecondAssetResourceSurfaceMaterial surface;
    NwbSecondAssetResourceRuntimeMaterial runtime;

    )NWB_BIND";
    static constexpr AStringView s_AssetResourceMaterialMeta = R"NWB_META(material asset;

    asset.interface = "project/material_interfaces/test_surface.bind";
    asset.bxdf = "project/shaders/material_bxdf.bxdf";
    asset.transparent = 0;
    asset.two_sided = 0;
    asset.refractive = 0;

    asset.shaders = {
    "mesh": "project/shaders/material_mesh",
    "ps": "project/shaders/material_ps",
    };
    asset.shader_variant = "default";

    asset.parameters = {
    "surface": {
        "base_color": "float4(0.25, 0.5, 0.75, 1.0)",
        "base_color_map": "project/textures/test_checker",
        "base_color_sampler": "engine/samplers/linear_clamp",
    },
    "runtime": {
        "fade_alpha": "float(0.75)",
    },
    };

    )NWB_META";
    static constexpr AStringView s_StaticResourceFixtureMaterialMeta = R"NWB_META(material asset;

    asset.interface = "project/material_interfaces/test_surface.bind";
    asset.bxdf = "project/shaders/material_bxdf.bxdf";
    asset.transparent = 0;
    asset.two_sided = 0;
    asset.refractive = 0;

    asset.shaders = {
    "mesh": "project/shaders/material_mesh",
    "ps": "project/shaders/material_ps",
    };
    asset.shader_variant = "default";

    asset.parameters = {
    "runtime": {
        "fade_alpha": "float(0.75)",
    },
    };

    )NWB_META";
    static constexpr AStringView s_MaterialBindMeshSource = R"NWB_SLANG(#include "mesh/authoring.slangi"

    NwbMeshGeneratedVertex nwbMeshBuildVertex(
    uint triangleIndex,
    uint corner,
    NwbMeshSourceVertex source,
    const NwbMeshInstanceData instance
    ){
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
    return nwbMakeMeshSurface(
        materialBindConstantsValid ? half3(surface.base_color.rgb) : half3(1.0h, 0.0h, 1.0h),
        inNormal
    );
    }

    )NWB_SLANG";
    static constexpr AStringView s_ViewDependentTransparentMaterialSurfaceSource = R"NWB_SLANG(NwbMeshSurface nwbMaterialSurface(){
    const NwbMeshInstanceData instance = nwbMeshLoadInstance();
    const NwbTestSurfaceMaterial surface = nwbMaterialBindLoadSurface(instance);
    NwbMeshSurface result = nwbMakeMeshSurface(half3(surface.base_color.rgb), inNormal);
    result.renderCoverage = half(saturate(dot(inIncidentDirection, inNormal) * 0.5 + 0.5));
    return result;
    }

    )NWB_SLANG";
    static constexpr AStringView s_ShadowDispatchSharedSurfaceHelperSource = R"NWB_SLANG(#ifndef NWB_TEST_SHADOW_DISPATCH_SHARED_SURFACE_HELPER_SLANGI
    #define NWB_TEST_SHADOW_DISPATCH_SHARED_SURFACE_HELPER_SLANGI

    half3 nwbTestShadowDispatchSharedSurfaceColor(const half3 color){
    return color;
    }

    #endif

    )NWB_SLANG";
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
    static constexpr AStringView s_AssetResourceShaderProbeSource = R"NWB_SLANG(#include "mesh/material_ps_authoring.slangi"
    #include "project/material_interfaces/test_surface.bind"

    NwbMeshSurface nwbMaterialSurface(){
    const NwbMeshInstanceData instance = nwbMeshLoadInstance();
    const NwbAssetResourceSurfaceMaterial surface = nwbMaterialBindLoadSurface(instance);
    const float4 sampledColor = nwbMaterialBindLoadSurfaceBaseColorMap(instance).SampleLevel(
        nwbMaterialBindLoadSurfaceBaseColorSampler(instance),
        inUv0,
        0.0
    );
    return nwbMakeMeshSurface(half3(surface.base_color.rgb * sampledColor.rgb), inNormal);
    }

    )NWB_SLANG";
    static constexpr AStringView s_StaticResourceFixtureShaderProbeSource = R"NWB_SLANG(#include "mesh/material_ps_authoring.slangi"
    #include "project/material_interfaces/test_surface.bind"

    NwbMeshSurface nwbMaterialSurface(){
    const NwbMeshInstanceData instance = nwbMeshLoadInstance();
    const float4 fixtureColor = nwbMaterialBindLoadSurfaceBaseColorMap(instance).SampleLevel(
        nwbMaterialBindLoadSurfaceBaseColorSampler(instance),
        inUv0,
        0.0
    );
    return nwbMakeMeshSurface(half3(fixtureColor.rgb), inNormal);
    }

    )NWB_SLANG";
    static constexpr AStringView s_HalfMaterialBindShaderProbeSource = R"NWB_SLANG(#include "mesh/material_ps_authoring.slangi"
    #include "project/material_interfaces/test_surface.bind"

    NwbMeshSurface nwbMaterialSurface(){
    const bool materialBindConstantsValid =
        NWB_MATERIAL_BIND_CONSTANT_BYTE_SIZE == 20u
        && NWB_MATERIAL_BIND_MUTABLE_BYTE_SIZE == 0u
        && NWB_MATERIAL_BIND_SURFACE_STORAGE == NWB_MATERIAL_BIND_STORAGE_CONSTANT
        && NWB_MATERIAL_BIND_SURFACE_BYTE_OFFSET == 0u
        && NWB_MATERIAL_BIND_SURFACE_BYTE_SIZE == 20u
        && NWB_MATERIAL_BIND_SURFACE_ROUGHNESS_BYTE_OFFSET == 0u
        && NWB_MATERIAL_BIND_SURFACE_RANGE_BYTE_OFFSET == 2u
        && NWB_MATERIAL_BIND_SURFACE_TINT_BYTE_OFFSET == 6u
        && NWB_MATERIAL_BIND_SURFACE_BASE_COLOR_BYTE_OFFSET == 12u;
    const NwbMeshInstanceData instance = nwbMeshLoadInstance();
    const NwbTestSurfaceMaterial surface = nwbMaterialBindLoadSurface(instance);
    const float3 baseColor = float3(
        float(surface.base_color.x),
        float(surface.base_color.y),
        float(surface.base_color.z)
    );
    return nwbMakeMeshSurface(
        materialBindConstantsValid ? half3(baseColor) : half3(1.0h, 0.0h, 1.0h),
        inNormal
    );
    }

    )NWB_SLANG";
    static constexpr AStringView s_CompactIntegerMaterialBindShaderProbeSource = R"NWB_SLANG(#include "mesh/material_ps_authoring.slangi"
    #include "project/material_interfaces/test_surface.bind"

    NwbMeshSurface nwbMaterialSurface(){
    const bool materialBindConstantsValid =
        NWB_MATERIAL_BIND_CONSTANT_BYTE_SIZE == 20u
        && NWB_MATERIAL_BIND_MUTABLE_BYTE_SIZE == 0u
        && NWB_MATERIAL_BIND_SURFACE_STORAGE == NWB_MATERIAL_BIND_STORAGE_CONSTANT
        && NWB_MATERIAL_BIND_SURFACE_BYTE_OFFSET == 0u
        && NWB_MATERIAL_BIND_SURFACE_BYTE_SIZE == 20u
        && NWB_MATERIAL_BIND_SURFACE_ENABLED_BYTE_OFFSET == 0u
        && NWB_MATERIAL_BIND_SURFACE_SIGNED_BYTES_BYTE_OFFSET == 4u
        && NWB_MATERIAL_BIND_SURFACE_BYTES_BYTE_OFFSET == 8u
        && NWB_MATERIAL_BIND_SURFACE_SIGNED_WORDS_BYTE_OFFSET == 12u
        && NWB_MATERIAL_BIND_SURFACE_WORDS_BYTE_OFFSET == 16u;
    const NwbMeshInstanceData instance = nwbMeshLoadInstance();
    const NwbTestSurfaceMaterial surface = nwbMaterialBindLoadSurface(instance);
    const float3 baseColor = float3(
        surface.enabled.x ? 1.0 : 0.0,
        float(surface.bytes.z) / 255.0,
        float(surface.signed_words.y) / 32767.0
    );
    return nwbMakeMeshSurface(
        materialBindConstantsValid ? half3(baseColor) : half3(1.0h, 0.0h, 1.0h),
        inNormal
    );
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
    #endif
    #if defined(NWB_FINAL)
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
    static constexpr AStringView s_BlockScopedMaterialMeta = R"NWB_META(material asset;

    asset.interface = "project/material_interfaces/test_surface.bind";
    asset.bxdf = "project/shaders/material_bxdf.bxdf";
    asset.transparent = 0;
    asset.two_sided = 0;
    asset.refractive = 0;

    asset.shaders = {
    "mesh": "project/shaders/material_mesh",
    "ps": "project/shaders/material_ps",
    };
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
    static constexpr AStringView s_HalfMaterialMeta = R"NWB_META(material asset;

    asset.interface = "project/material_interfaces/test_surface.bind";
    asset.bxdf = "project/shaders/material_bxdf.bxdf";
    asset.transparent = 0;
    asset.two_sided = 0;
    asset.refractive = 0;

    asset.shaders = {
    "mesh": "project/shaders/material_mesh",
    "ps": "project/shaders/material_ps",
    };
    asset.shader_variant = "default";

    asset.parameters = {
    "surface": {
        "roughness": "half(0.25h)",
        "range": "half2(0.125h, 0.5h)",
        "tint": "half3(1.0h, 0.75h, 0.5h)",
        "base_color": "half4(1.0h, 0.5h, 0.25h, 0.0h)",
    },
    };

    )NWB_META";
    static constexpr AStringView s_MixedHalfMaterialMeta = R"NWB_META(material asset;

    asset.interface = "project/material_interfaces/test_surface.bind";
    asset.bxdf = "project/shaders/material_bxdf.bxdf";
    asset.transparent = 0;
    asset.two_sided = 0;
    asset.refractive = 0;

    asset.shaders = {
    "mesh": "project/shaders/material_mesh",
    "ps": "project/shaders/material_ps",
    };
    asset.shader_variant = "default";

    asset.parameters = {
    "surface": {
        "roughness": "half(0.25)",
        "metallic": "float(0.75)",
        "tint": "half3(1.0, 0.5, 0.25)",
        "flags": "uint(42u)",
        "tail": "half(0.875)",
    },
    };

    )NWB_META";
    static constexpr AStringView s_CompactIntegerMaterialMeta = R"NWB_META(material asset;

    asset.interface = "project/material_interfaces/test_surface.bind";
    asset.bxdf = "project/shaders/material_bxdf.bxdf";
    asset.transparent = 0;
    asset.two_sided = 0;
    asset.refractive = 0;

    asset.shaders = {
    "mesh": "project/shaders/material_mesh",
    "ps": "project/shaders/material_ps",
    };
    asset.shader_variant = "default";

    asset.parameters = {
    "surface": {
        "enabled": "bool4(false, true, false, true)",
        "signed_bytes": "char4(-128, -2, 2, 64)",
        "bytes": "uchar4(3u, 4u, 5u, 6u)",
        "signed_words": "short2(-1234, 2345)",
        "words": "ushort2(7u, 65534u)",
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
    static constexpr AStringView s_TwoSidedMaterialMeta = R"NWB_META(material asset;

    asset.interface = "project/material_interfaces/test_surface.bind";
    asset.bxdf = "project/shaders/material_bxdf.bxdf";
    asset.transparent = 0;
    asset.two_sided = 1;
    asset.refractive = 0;

    asset.shaders = {
    "mesh": "project/shaders/material_mesh",
    "ps": "project/shaders/material_ps",
    };
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
    #endif
    #if defined(NWB_FINAL)
    static constexpr AStringView s_ExplicitTransparentMaterialMeta = R"NWB_META(material asset;

    asset.interface = "project/material_interfaces/test_surface.bind";
    asset.bxdf = "project/shaders/material_bxdf.bxdf";
    asset.transparent = 1;
    asset.two_sided = 0;
    asset.refractive = 0;

    asset.shaders = {
    "mesh": "project/shaders/material_mesh",
    "ps": "project/shaders/material_ps",
    };
    asset.shader_variant = "default";

    )NWB_META";
    static constexpr AStringView s_ExplicitRefractiveMaterialMeta = R"NWB_META(material asset;

    asset.interface = "project/material_interfaces/test_surface.bind";
    asset.bxdf = "project/shaders/material_bxdf.bxdf";
    asset.transparent = 0;
    asset.two_sided = 0;
    asset.refractive = 1;

    asset.shaders = {
    "mesh": "project/shaders/material_mesh",
    "ps": "project/shaders/material_ps",
    };
    asset.shader_variant = "default";

    )NWB_META";
    #endif
    #if defined(NWB_FINAL)
    static constexpr AStringView s_MissingInterfaceMaterialMeta = R"NWB_META(material asset;

    asset.shaders = {
    "mesh": "project/shaders/material_mesh",
    "ps": "project/shaders/material_ps",
    };
    asset.shader_variant = "default";

    asset.parameters = {
    "base_color": "float4(0.25, 0.5, 0.75, 1.0)",
    };

    )NWB_META";
    static constexpr AStringView s_UnsupportedMaterialFieldMeta = R"NWB_META(material asset;

    asset.interface = "project/material_interfaces/test_surface.bind";
    asset.compiler = "unsupported";

    asset.shaders = {
    "mesh": "project/shaders/material_mesh",
    "ps": "project/shaders/material_ps",
    };
    asset.shader_variant = "default";

    )NWB_META";
    #endif
    #if defined(NWB_FINAL)
    static constexpr AStringView s_UnknownInterfaceParameterMaterialMeta = R"NWB_META(material asset;

    asset.interface = "project/material_interfaces/test_surface.bind";
    asset.bxdf = "project/shaders/material_bxdf.bxdf";
    asset.transparent = 0;
    asset.two_sided = 0;
    asset.refractive = 0;

    asset.shaders = {
    "mesh": "project/shaders/material_mesh",
    "ps": "project/shaders/material_ps",
    };
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

    asset.shaders = {
    "mesh": "project/shaders/material_mesh",
    "ps": "project/shaders/material_ps",
    };
    asset.shader_variant = "default";

    asset.parameters = {
    "runtime.fade_alpha": "float(0.75)",
    };

    )NWB_META";
    static constexpr AStringView s_UntypedMaterialParameterMeta = R"NWB_META(material asset;

    asset.interface = "project/material_interfaces/test_surface.bind";
    asset.bxdf = "project/shaders/material_bxdf.bxdf";
    asset.transparent = 0;
    asset.two_sided = 0;
    asset.refractive = 0;

    asset.shaders = {
    "mesh": "project/shaders/material_mesh",
    "ps": "project/shaders/material_ps",
    };
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

    asset.shaders = {
    "mesh": "project/shaders/material_mesh",
    "ps": "project/shaders/material_ps",
    };
    asset.shader_variant = "default";

    asset.parameters = {
    "surface": {
        "base_color": "vec4(0.25, 0.5, 0.75, 1.0)",
    },
    };

    )NWB_META";
    #endif
    #if defined(NWB_FINAL)
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
    static constexpr AStringView s_HalfMaterialBindSource = R"NWB_BIND([material_constant]
    struct NwbTestSurfaceMaterial{
    [default("half(0.5h)")]
    half roughness;

    [default("half2(0.0h, 1.0h)")]
    half2 range;

    [default("half3(0.25h, 0.5h, 0.75h)")]
    half3 tint;

    [default("half4(1.0h, 1.0h, 1.0h, 1.0h)")]
    half4 base_color;
    };

    NwbTestSurfaceMaterial surface;

    )NWB_BIND";
    static constexpr AStringView s_MixedHalfMaterialBindSource = R"NWB_BIND([material_constant]
    struct NwbTestSurfaceMaterial{
    [default("half(0.5)")]
    half roughness;

    [default("float(1.0)")]
    float metallic;

    [default("half3(0.25, 0.5, 0.75)")]
    half3 tint;

    [default("uint(7u)")]
    uint flags;

    [default("half(0.125)")]
    half tail;
    };

    NwbTestSurfaceMaterial surface;

    )NWB_BIND";
    static constexpr AStringView s_CompactIntegerMaterialBindSource = R"NWB_BIND([material_constant]
    struct NwbTestSurfaceMaterial{
    [default("bool4(true, false, true, false)")]
    bool4 enabled;

    [default("char4(-1, 0, 1, 127)")]
    char4 signed_bytes;

    [default("uchar4(0u, 1u, 254u, 255u)")]
    uchar4 bytes;

    [default("short2(-32768, 32767)")]
    short2 signed_words;

    [default("ushort2(0u, 65535u)")]
    ushort2 words;
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
    [texture_asset("project/textures/legacy")]
    texture2d base_color_map;
    };

    NwbTestSurfaceMaterial surface;

    )NWB_BIND";
    #if defined(NWB_FINAL)
    static constexpr AStringView s_UnsupportedMeshFieldsMeta = R"(mesh asset;

    asset.vertex_stride = 24;

    asset.vertex_data = [
    [-0.5, -0.5, 0.0, 1.0, 0.0, 0.0],
    [ 0.5, -0.5, 0.0, 0.0, 1.0, 0.0],
    [ 0.0,  0.5, 0.0, 0.0, 0.0, 1.0],
    ];

    asset.index_data = [
    [0, 1, 2],
    ];
    )";
    static constexpr AStringView s_MismatchedMeshMeta =
    "mesh asset;\n\n"
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_POSITIONS
    R"(asset.normals = [
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
    ];

    )" NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_TANGENTS
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_INDICES;
    #endif
    static constexpr AStringView s_MaterialBindBxdfSource =
    "half3 NWB_DEFERRED_BXDF_FUNCTION(NwbBxdfSurface surface, int2 pixel){ return surface.baseColor; }\n";


public:

    AssetsGraphicsFixture() = delete;
    AssetsGraphicsFixture(const AssetsGraphicsFixture&) = delete;
    AssetsGraphicsFixture& operator=(const AssetsGraphicsFixture&) = delete;
    ~AssetsGraphicsFixture() = delete;


public:

    template<typename T>
    static NWB::Core::Assets::AssetVector<T> MakeAssetVector(TestArena& testArena){
        return NWB::Core::Assets::AssetVector<T>(testArena.arena);
    }
    static NWB::Core::Assets::AssetBytes MakeAssetBytes(TestArena& testArena);
    static void AppendTestMeta(AString& inOutMeta, const AStringView text);
    #if defined(NWB_FINAL)
    static AString BuildTriangleMeta(
        const AStringView assetHeader,
        const AStringView normalField,
        const AStringView tangentField,
        const AStringView vertexRefsField,
        const AStringView suffix
    );
    static AString BuildMeshTriangleMeta(
        const AStringView normalField,
        const AStringView tangentField,
        const AStringView vertexRefsField
    );
    #endif
    static bool PrepareCleanDirectory(const Path& directory);
    static bool WriteTextFile(const Path& filePath, const AStringView text);
    static const char* AssetsGraphicsTestConfigurationName();
    static Path AssetsGraphicsTestRepoRoot(TestArena& testArena);
    static Path AssetsGraphicsTestCaseRoot(TestArena& testArena, const AStringView caseName);
    static bool PrepareAssetsGraphicsCaseRoot(TestArena& testArena, const AStringView caseName, Path& outRoot);
    static bool PrepareAssetsGraphicsCookCase(
        TestArena& testArena,
        const AStringView caseName,
        Path& outRoot,
        Path& outOutputDirectory
    );
    static bool BuildPreparedGraphicsAssetRoots(
        TestArena& testArena,
        const Path& root,
        const Path& outputDirectory,
        const InitializerList<Path> assetRoots,
        const u32 workerThreadCount = 0u
    );
    static bool CookPreparedGraphicsAssetRoots(
        TestArena& testArena,
        const Path& root,
        const Path& outputDirectory,
        const InitializerList<Path> assetRoots,
        const u32 workerThreadCount = 0u
    );
    static bool CookSingleGraphicsMeta(
        const AStringView metaText,
        const AStringView caseName,
        const char* assetDirectory,
        const char* assetFilename,
        TestArena& testArena,
        Path& outRoot,
        Path& outOutputDirectory
    );
    static bool CookSingleMinimalAssetMeta(
        const AStringView metaText,
        const AStringView caseName,
        const MinimalAssetCookInfo& cookInfo,
        TestArena& testArena,
        Path& outRoot,
        Path& outOutputDirectory
    );
    static bool CookSingleMeshMeta(
        const AStringView metaText,
        const AStringView caseName,
        TestArena& testArena,
        Path& outRoot,
        Path& outOutputDirectory
    );
    static bool ReadSmokeAssetMeta(
        TestArena& testArena,
        const char* assetDirectory,
        const char* assetFilename,
        AString& outMetaText
    );
    static bool CookSmokeAssetMeta(
        const char* assetDirectory,
        const char* assetFilename,
        const AStringView caseName,
        TestArena& testArena,
        Path& outRoot,
        Path& outOutputDirectory
    );
    static bool CookSmokeMeshMeta(
        const char* assetFilename,
        const AStringView caseName,
        TestArena& testArena,
        Path& outRoot,
        Path& outOutputDirectory
    );
    static bool CookMinimalMeshWithMaterialBind(
        const AStringView bindText,
        const AStringView caseName,
        TestArena& testArena,
        Path& outRoot,
        Path& outOutputDirectory
    );
    static bool ParseMaterialBindFromText(
        TestArena& testArena,
        const AStringView bindText,
        const AStringView caseName,
        NWB::Impl::MaterialBindEntry& outEntry,
        Path& outRoot,
        NWB::Core::Alloc::ScratchArena& scratchArena
    );
    #if defined(NWB_FINAL)
    static bool CookDuplicateGeneratedMaterialBindIncludePath(
        const AStringView caseName,
        TestArena& testArena,
        Path& outRoot,
        Path& outOutputDirectory
    );
    #endif
    static bool WriteMaterialBindShaderProbeSource(
        TestArena& testArena,
        const Path& assetRoot,
        const char* stage,
        const char* metaFilename,
        const char* sourceFilename,
        const AStringView sourceText
    );
    static bool CookMaterialBindShaderProbe(
        const AStringView bindText,
        const AStringView caseName,
        TestArena& testArena,
        Path& outRoot,
        Path& outOutputDirectory
    );
    static bool WriteMaterialBindMaterialIntegrationAssetsWithPixelSource(
        TestArena& testArena,
        const Path& assetRoot,
        const AStringView bindText,
        const AStringView materialText,
        const AStringView pixelSourceText
    );
    static bool WriteMaterialBindMaterialIntegrationAssets(
        TestArena& testArena,
        const Path& assetRoot,
        const AStringView bindText,
        const AStringView materialText
    );
    static bool WriteMaterialSurfaceIntegrationAssets(
        const Path& assetRoot,
        const AStringView bindText,
        const AStringView materialText,
        const AStringView surfaceSourceText
    );
    static bool CookMaterialBindMaterialIntegrationWithPixelSource(
        const AStringView bindText,
        const AStringView materialText,
        const AStringView pixelSourceText,
        const AStringView caseName,
        TestArena& testArena,
        Path& outRoot,
        Path& outOutputDirectory
    );
    static bool CookMaterialSurfaceIntegration(
        const AStringView bindText,
        const AStringView materialText,
        const AStringView surfaceSourceText,
        const AStringView caseName,
        TestArena& testArena,
        Path& outRoot,
        Path& outOutputDirectory
    );
    static bool CookMaterialBindMaterialIntegration(
        const AStringView bindText,
        const AStringView materialText,
        const AStringView caseName,
        TestArena& testArena,
        Path& outRoot,
        Path& outOutputDirectory
    );
    template<typename AssetCodecT>
    static bool LoadCookedAsset(
        TestArena& testArena,
        const Path& outputDirectory,
        const Name assetName,
        UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset,
        const usize expectedVolumeFileCount = 2u
    ){
        NWB::Core::Filesystem::VolumeMountDesc mountDesc(testArena.arena);
        mountDesc.volumeName = "graphics";
        mountDesc.mountDirectory = outputDirectory;
        UniquePtr<NWB::Core::Filesystem::IFilesystem> filesystem = NWB::Core::Filesystem::CreateFilesystem(testArena.arena, mountDesc);
        const bool loadedVolume = static_cast<bool>(filesystem);
        EXPECT_TRUE(loadedVolume);
        if(!loadedVolume)
            return false;

        if(expectedVolumeFileCount != 0u)
            EXPECT_EQ(filesystem->fileCount(), expectedVolumeFileCount);

        NWB::Core::Assets::AssetBytes binary = MakeAssetBytes(testArena);
        const bool loadedBinary = filesystem->readFile(assetName, binary);
        EXPECT_TRUE(loadedBinary);
        EXPECT_FALSE(binary.empty());
        if(!loadedBinary || binary.empty())
            return false;

        AssetCodecT codec;
        const bool deserialized = codec.deserialize(testArena.arena, assetName, binary, outLoadedAsset);
        EXPECT_TRUE(deserialized);
        EXPECT_NE(outLoadedAsset.get(), nullptr);
        return deserialized && static_cast<bool>(outLoadedAsset);
    }
    static bool LoadCookedMinimalMesh(
        TestArena& testArena,
        const Path& outputDirectory,
        UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset);
    static bool LoadCookedMesh(
        TestArena& testArena,
        const Path& outputDirectory,
        const Name assetName,
        UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset);
    static bool LoadCookedMaterial(
        TestArena& testArena,
        const Path& outputDirectory,
        const Name assetName,
        UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset
    );
    static bool LoadCookedShaderArchiveRecords(
        TestArena& testArena,
        const Path& outputDirectory,
        NWB::Core::GraphicsVector<NWB::Core::ShaderArchive::Record>& outRecords
    );
    static bool FindShaderArchiveSourceChecksum(
        const NWB::Core::GraphicsVector<NWB::Core::ShaderArchive::Record>& records,
        const Name shaderName,
        const Name stageName,
        u64& outSourceChecksum
    );


public:

    static bool CookAndLoadMinimalAsset(
        TestArena& testArena,
        const AStringView metaText,
        const AStringView caseName,
        Path& outRoot,
        UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset,
        CookSingleMetaFn cookSingleMeta,
        LoadCookedAssetFn loadCookedAsset
    );
    static bool CookAndLoadMinimalAssetByKind(
        TestArena& testArena,
        const AStringView metaText,
        const AStringView caseName,
        Path& outRoot,
        UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset,
        const MinimalAssetKind::Enum assetKind
    );
    template<typename MeshT>
    static void CheckMinimalRuntimeMeshletPayload(
        const MeshT& loadedMesh
    ){
        EXPECT_EQ(loadedMesh.meshlets().size(), 1u);
        EXPECT_EQ(loadedMesh.meshletBounds().size(), 1u);
        EXPECT_EQ(loadedMesh.meshletLocalVertexRefs().size(), 3u);
        EXPECT_EQ(loadedMesh.meshletPrimitiveIndices().size(), 3u);

        const NWB::Impl::MeshletDesc& meshlet = loadedMesh.meshlets()[0u];
        const bool skinRequired = NWB::Core::Mesh::MeshClassUsesSkinning(loadedMesh.meshClass());
        usize expectedPositionRefBytes = 0u;
        usize expectedAttributeRefBytes = 0u;
        EXPECT_EQ(NWB::Impl::MeshletVertexCount(meshlet), 3u);
        EXPECT_EQ(NWB::Impl::MeshletPrimitiveCount(meshlet), 1u);
        EXPECT_EQ(NWB::Impl::MeshletPositionCount(meshlet), 3u);
        EXPECT_EQ(NWB::Impl::MeshletAttributeCount(meshlet), 3u);
        EXPECT_TRUE(NWB::Impl::MeshletEncodedPositionRefByteCount(meshlet, skinRequired, expectedPositionRefBytes));
        EXPECT_TRUE(NWB::Impl::MeshletEncodedAttributeRefByteCount(meshlet, expectedAttributeRefBytes));
        EXPECT_EQ(loadedMesh.meshletPositionRefDeltas().size(), expectedPositionRefBytes);
        EXPECT_EQ(loadedMesh.meshletAttributeRefDeltas().size(), expectedAttributeRefBytes);
        EXPECT_EQ(meshlet.positionBase, 0u);
        const u32 expectedSkinBase = skinRequired ? 0u : NWB::Impl::s_MeshMissingStreamIndex;
        EXPECT_EQ(meshlet.skinBase, expectedSkinBase);
        EXPECT_EQ(meshlet.normalBase, 0u);
        EXPECT_EQ(meshlet.tangentBase, 0u);
        EXPECT_EQ(meshlet.uv0Base, 0u);
        EXPECT_EQ(meshlet.colorBase, 0u);
        EXPECT_EQ(meshlet.encoding, 0u);
        EXPECT_GT(loadedMesh.meshletBounds()[0u].sphere.w, 0.0f);
        EXPECT_TRUE(NWB::Impl::MeshletConeEnabled(loadedMesh.meshletBounds()[0u]));
    }
    template<typename AssetT, typename CheckLoadedAssetFn>
    static void CookAndCheckMinimalTypedAsset(
        const AStringView metaText,
        const AStringView caseName,
        const MinimalAssetKind::Enum assetKind,
        CheckLoadedAssetFn&& checkLoadedAsset
    ){
        CapturingLogger logger;
        NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

        TestArena testArena;
        Path root(testArena.arena);
        UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
        if(!CookAndLoadMinimalAssetByKind(
            testArena,
            metaText,
            caseName,
            root,
            loadedAsset,
            assetKind
        ))
            return;

        const AssetT& loadedTypedAsset = static_cast<const AssetT&>(*loadedAsset);
        checkLoadedAsset(loadedTypedAsset);

        ErrorCode errorCode;
        EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
        EXPECT_EQ(logger.errorCount(), 0u);
    }
    template<typename T>
    static bool OverwritePOD(NWB::Core::Assets::AssetBytes& binary, const usize offset, const T value){
        if(offset > binary.size() || sizeof(value) > binary.size() - offset)
            return false;

        NWB_MEMCPY(binary.data() + offset, sizeof(value), &value, sizeof(value));
        return true;
    }
    static bool FindMaterialBinaryTypedLayoutOffsets(
        const NWB::Core::Assets::AssetBytes& binary,
        usize& outLayoutHashOffset,
        usize& outBlockByteCountOffset
    );
    template<typename AssetT, typename CodecT>
    static const AssetT& CheckCodecRoundTrip(
        TestArena& testArena,
        const AssetT& asset,
        const CodecT& codec,
        UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset){
        EXPECT_TRUE(asset.validatePayload());

        NWB::Core::Assets::AssetBytes binary = MakeAssetBytes(testArena);
        EXPECT_TRUE(codec.serialize(asset, binary));
        EXPECT_FALSE(binary.empty());

        EXPECT_TRUE(codec.deserialize(testArena.arena, asset.virtualPath(), binary, outLoadedAsset));
        EXPECT_NE(outLoadedAsset.get(), nullptr);
        EXPECT_EQ(outLoadedAsset->assetType(), AssetT::AssetTypeName());
        return static_cast<const AssetT&>(*outLoadedAsset);
    }
    static bool EncodeTestMeshletRefs(
        NWB::Core::Assets::AssetVector<NWB::Impl::MeshletDesc>& meshlets,
        const NWB::Core::Assets::AssetVector<NWB::Impl::MeshletPositionStreamRef>& positionRefs,
        const NWB::Core::Assets::AssetVector<NWB::Impl::MeshletAttributeStreamRef>& attributeRefs,
        NWB::Core::Assets::AssetVector<u8>& outPositionRefDeltas,
        NWB::Core::Assets::AssetVector<u8>& outAttributeRefDeltas,
        const bool skinRequired
    );
    template<typename CodecT>
    static void CheckCodecRejectsBinary(
        TestArena& testArena,
        const CodecT& codec,
        const Name& virtualPath,
        const NWB::Core::Assets::AssetBytes& binary){
        UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
        EXPECT_FALSE(codec.deserialize(testArena.arena, virtualPath, binary, loadedAsset));
        EXPECT_FALSE(loadedAsset);
    }


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

