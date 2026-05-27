// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_mesh/skinned_mesh_asset.h>
#include <impl/assets_mesh/mesh_asset.h>
#include <impl/assets_mesh/meshlet_payload_packing.h>
#include <impl/assets_graphics/graphics_asset_cooker.h>
#include <impl/assets_material/material_asset_cook.h>
#include <impl/assets_material/material_binary_payload.h>
#include <impl/assets_shader/shader_cook.h>

#include <tests/capturing_logger.h>
#include <tests/test_context.h>

#include <core/alloc/scratch.h>
#include <core/common/common.h>
#include <core/mesh/mesh_class.h>
#include <core/filesystem/filesystem.h>
#include <core/graphics/common.h>
#include <core/graphics/shader_archive.h>
#include <core/graphics/spirv_entry_point.h>

#include <global/binary.h>
#include <global/compile.h>
#include <global/hash_utils.h>
#include <global/math/convert.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestContext = NWB::Tests::TestContext;
using CapturingLogger = NWB::Tests::CapturingLogger;
using NWB::Tests::MakeQuadTriangleIndices;
using NWB::Tests::MakeTriangleIndices;
using AString = NWB::Tests::TestAString;


#define NWB_ASSETS_GRAPHICS_TEST_CHECK NWB_TEST_CHECK


struct AssetsGraphicsTestArenaTag{};
using TestArena = NWB::Tests::TestArena<AssetsGraphicsTestArenaTag>;

template<typename T>
static NWB::Core::Assets::AssetVector<T> MakeAssetVector(TestArena& testArena){
    return NWB::Core::Assets::AssetVector<T>(testArena.arena);
}

template<typename SourceVector>
static auto MakeAssetVectorFrom(TestArena& testArena, const SourceVector& source){
    using ValueType = typename SourceVector::value_type;
    auto output = MakeAssetVector<ValueType>(testArena);
    output.insert(output.end(), source.begin(), source.end());
    return output;
}

static NWB::Core::Assets::AssetBytes MakeAssetBytes(TestArena& testArena){
    return NWB::Core::Assets::AssetBytes(testArena.arena);
}

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
    [0, 0, 4294967295, 0, 0],
    [1, 1, 4294967295, 1, 1],
    [2, 2, 4294967295, 2, 2],
];

)"

#define NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_DEFAULT_COLOR_VERTEX_REFS R"(asset.vertex_refs = [
    [0, 0, 4294967295, 0, 0],
    [1, 1, 4294967295, 1, 0],
    [2, 2, 4294967295, 2, 0],
];

)"

#define NWB_ASSETS_GRAPHICS_TEST_SKINNED_TRIANGLE_VERTEX_REFS R"(asset.vertex_refs = [
    [0, 0, 0, 0, 0, 0],
    [1, 1, 1, 1, 0, 1],
    [2, 2, 2, 2, 0, 2],
];

)"

#define NWB_ASSETS_GRAPHICS_TEST_SKINNED_TRIANGLE_MISSING_TANGENT_VERTEX_REFS R"(asset.vertex_refs = [
    [0, 0, 4294967295, 0, 0, 0],
    [1, 1, 4294967295, 1, 0, 1],
    [2, 2, 4294967295, 2, 0, 2],
];

)"

#define NWB_ASSETS_GRAPHICS_TEST_ROOT_SKIN R"(asset.skeleton_joint_count = 1;

asset.skin = {
    "joints0": [
        [0, 0, 0, 0],
        [0, 0, 0, 0],
        [0, 0, 0, 0],
    ],
    "weights0": [
        [1.0, 0.0, 0.0, 0.0],
        [1.0, 0.0, 0.0, 0.0],
        [1.0, 0.0, 0.0, 0.0],
    ],
};

asset.inverse_bind_matrices = [
    [
        [1.0, 0.0, 0.0, 0.0],
        [0.0, 1.0, 0.0, 0.0],
        [0.0, 0.0, 1.0, 0.0],
        [0.0, 0.0, 0.0, 1.0],
    ],
];

)"

#define NWB_ASSETS_GRAPHICS_TEST_SKINNED_MESH_TRIANGLE_STREAMS \
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_POSITIONS \
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_NORMALS \
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_TANGENTS \
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_UV0 \
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_COLORS \
    NWB_ASSETS_GRAPHICS_TEST_SKINNED_TRIANGLE_VERTEX_REFS \
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_INDICES

#define NWB_ASSETS_GRAPHICS_TEST_SKINNED_MESH_TRIANGLE_PREFIX \
    "skinned_mesh asset;\n\n" \
    NWB_ASSETS_GRAPHICS_TEST_SKINNED_MESH_TRIANGLE_STREAMS

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


static constexpr AStringView s_MinimalMeshMeta =
    "mesh asset;\n\n"
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_POSITIONS
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_NORMALS
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
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_UV0
    R"(asset.colors = [
    [1.0, 1.0, 1.0, 1.0],
];

)"
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_DEFAULT_COLOR_VERTEX_REFS
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_INDICES;

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

static constexpr AStringView s_MaterialBindShaderProbeSource = R"NWB_SLANG(#include "mesh/shader/authoring.slangi"
#include "project/material_interfaces/test_surface.bind"

NwbMeshGeneratedVertex nwbMeshBuildVertex(
    uint triangleIndex,
    uint corner,
    NwbMeshSourceVertex source,
    const NwbMeshInstanceData instance
){
    NwbMeshGeneratedVertex generatedVertex;
    const uint2 materialLayoutHash = NWB_MATERIAL_BIND_LAYOUT_HASH;
    const uint2 materialInterfaceHash0 = NWB_MATERIAL_BIND_INTERFACE_HASH_0;
    const bool materialBindConstantsValid =
        materialLayoutHash.x != 0u
        && materialInterfaceHash0.x != 0u
        && NWB_MATERIAL_BIND_BLOCK_BYTE_SIZE == 60u
        && NWB_MATERIAL_BIND_RUNTIME_BLOCK_BYTE_OFFSET == 0u
        && NWB_MATERIAL_BIND_RUNTIME_BLOCK_BYTE_SIZE == 4u
        && NWB_MATERIAL_BIND_SURFACE_BLOCK_BYTE_OFFSET == 4u
        && NWB_MATERIAL_BIND_SURFACE_BLOCK_BYTE_SIZE == 56u;
    const float3 worldPosition = nwbMeshTransformPosition(source.position, instance);
    const NwbTestSurfaceMaterial surface = nwbMaterialBindLoadSurface(instance);
    generatedVertex.position = nwbMeshTransformWorldToClip(worldPosition);
    generatedVertex.normal = nwbMeshTransformDirection(source.normal, instance);
    generatedVertex.padding0 = materialBindConstantsValid ? 0.0 : 1.0;
    generatedVertex.tangent = float4(nwbMeshTransformDirection(source.tangent.xyz, instance), source.tangent.w);
    generatedVertex.uv0 = source.uv0;
    generatedVertex.padding1 = float2(0.0);
    generatedVertex.color = surface.base_color;
    generatedVertex.worldPosition = float4(worldPosition, 1.0);
    return generatedVertex;
}

)NWB_SLANG";

static constexpr AStringView s_HalfMaterialBindShaderProbeSource = R"NWB_SLANG(#include "mesh/shader/authoring.slangi"
#include "project/material_interfaces/test_surface.bind"

NwbMeshGeneratedVertex nwbMeshBuildVertex(
    uint triangleIndex,
    uint corner,
    NwbMeshSourceVertex source,
    const NwbMeshInstanceData instance
){
    NwbMeshGeneratedVertex generatedVertex;
    const bool materialBindConstantsValid =
        NWB_MATERIAL_BIND_BLOCK_BYTE_SIZE == 20u
        && NWB_MATERIAL_BIND_SURFACE_BLOCK_BYTE_OFFSET == 0u
        && NWB_MATERIAL_BIND_SURFACE_BLOCK_BYTE_SIZE == 20u
        && NWB_MATERIAL_BIND_SURFACE_ROUGHNESS_BYTE_OFFSET == 0u
        && NWB_MATERIAL_BIND_SURFACE_RANGE_BYTE_OFFSET == 2u
        && NWB_MATERIAL_BIND_SURFACE_TINT_BYTE_OFFSET == 6u
        && NWB_MATERIAL_BIND_SURFACE_BASE_COLOR_BYTE_OFFSET == 12u;
    const float3 worldPosition = nwbMeshTransformPosition(source.position, instance);
    const NwbTestSurfaceMaterial surface = nwbMaterialBindLoadSurface(instance);
    generatedVertex.position = nwbMeshTransformWorldToClip(worldPosition);
    generatedVertex.normal = nwbMeshTransformDirection(source.normal, instance);
    generatedVertex.padding0 = materialBindConstantsValid ? float(surface.roughness) : -1.0;
    generatedVertex.tangent = float4(float(surface.tint.x), float(surface.tint.y), float(surface.tint.z), 1.0);
    generatedVertex.uv0 = source.uv0;
    generatedVertex.padding1 = float2(float(surface.range.x), float(surface.range.y));
    generatedVertex.color = float4(
        float(surface.base_color.x),
        float(surface.base_color.y),
        float(surface.base_color.z),
        float(surface.base_color.w)
    );
    generatedVertex.worldPosition = float4(worldPosition, 1.0);
    return generatedVertex;
}

)NWB_SLANG";

#if defined(NWB_FINAL)
static constexpr AStringView s_OtherMaterialBindShaderProbeSource = R"NWB_SLANG(#include "mesh/shader/authoring.slangi"
#include "project/material_interfaces/other_surface.bind"

NwbMeshGeneratedVertex nwbMeshBuildVertex(
    uint triangleIndex,
    uint corner,
    NwbMeshSourceVertex source,
    const NwbMeshInstanceData instance
){
    NwbMeshGeneratedVertex generatedVertex;
    const float3 worldPosition = nwbMeshTransformPosition(source.position, instance);
    const NwbTestSurfaceMaterial surface = nwbMaterialBindLoadSurface(instance);
    generatedVertex.position = nwbMeshTransformWorldToClip(worldPosition);
    generatedVertex.normal = nwbMeshTransformDirection(source.normal, instance);
    generatedVertex.padding0 = 0.0;
    generatedVertex.tangent = float4(nwbMeshTransformDirection(source.tangent.xyz, instance), source.tangent.w);
    generatedVertex.uv0 = source.uv0;
    generatedVertex.padding1 = float2(0.0);
    generatedVertex.color = surface.base_color;
    generatedVertex.worldPosition = float4(worldPosition, 1.0);
    return generatedVertex;
}

)NWB_SLANG";
#endif

#if defined(NWB_FINAL)
static constexpr AStringView s_UnboundMaterialShaderProbeSource = R"NWB_SLANG(#include "mesh/shader/authoring.slangi"

NwbMeshGeneratedVertex nwbMeshBuildVertex(
    uint triangleIndex,
    uint corner,
    NwbMeshSourceVertex source,
    const NwbMeshInstanceData instance
){
    NwbMeshGeneratedVertex generatedVertex;
    const float3 worldPosition = nwbMeshTransformPosition(source.position, instance);
    generatedVertex.position = nwbMeshTransformWorldToClip(worldPosition);
    generatedVertex.normal = nwbMeshTransformDirection(source.normal, instance);
    generatedVertex.padding0 = 0.0;
    generatedVertex.tangent = float4(nwbMeshTransformDirection(source.tangent.xyz, instance), source.tangent.w);
    generatedVertex.uv0 = source.uv0;
    generatedVertex.padding1 = float2(0.0);
    generatedVertex.color = source.color;
    generatedVertex.worldPosition = float4(worldPosition, 1.0);
    return generatedVertex;
}

)NWB_SLANG";
#endif

static constexpr AStringView s_MaterialBindPixelShaderProbeSource = R"NWB_SLANG(struct NwbMaterialBindPixelInput{
    [[vk::location(0)]] float4 color : COLOR0;
};

struct NwbMaterialBindPixelOutput{
    [[vk::location(0)]] float4 color : SV_Target0;
};

NwbMaterialBindPixelOutput main(NwbMaterialBindPixelInput input){
    NwbMaterialBindPixelOutput output;
    output.color = input.color;
    return output;
}

)NWB_SLANG";

static constexpr AStringView s_BlockScopedMaterialMeta = R"NWB_META(material asset;

asset.interface = "project/material_interfaces/test_surface";

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

asset.interface = "project/material_interfaces/test_surface";

asset.shaders = {
    "mesh": "project/shaders/material_mesh",
    "ps": "project/shaders/material_ps",
};
asset.shader_variant = "default";

asset.parameters = {
    "surface": {
        "roughness": "half(0.25)",
        "range": "half2(0.125, 0.5)",
        "tint": "half3(1.0, 0.75, 0.5)",
        "base_color": "half4(1.0, 0.5, 0.25, 0.0)",
    },
};

)NWB_META";

static constexpr AStringView s_MixedHalfMaterialMeta = R"NWB_META(material asset;

asset.interface = "project/material_interfaces/test_surface";

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

static constexpr AStringView s_TransparentMaterialMeta = R"NWB_META(material asset;

asset.interface = "project/material_interfaces/test_surface";
asset.transparent = 1;

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

static constexpr AStringView s_TwoSidedMaterialMeta = R"NWB_META(material asset;

asset.interface = "project/material_interfaces/test_surface";
asset.two_sided = 1;

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

asset.interface = "project/material_interfaces/test_surface";
asset.compiler = "unsupported";

asset.shaders = {
    "mesh": "project/shaders/material_mesh",
    "ps": "project/shaders/material_ps",
};
asset.shader_variant = "default";

)NWB_META";

static constexpr AStringView s_MissingShaderVariantMaterialMeta = R"NWB_META(material asset;

asset.interface = "project/material_interfaces/test_surface";

asset.shaders = {
    "mesh": "project/shaders/material_mesh",
    "ps": "project/shaders/material_ps",
};

)NWB_META";
#endif

#if defined(NWB_FINAL)
static constexpr AStringView s_UnknownInterfaceParameterMaterialMeta = R"NWB_META(material asset;

asset.interface = "project/material_interfaces/test_surface";

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

asset.interface = "project/material_interfaces/test_surface";

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

asset.interface = "project/material_interfaces/test_surface";

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

asset.interface = "project/material_interfaces/test_surface";

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

#if defined(NWB_FINAL)
static constexpr AStringView s_DuplicateInstanceMaterialBindSource = R"NWB_BIND([material_constant]
struct NwbTestSurfaceMaterial{
    [default("float(0.5)")]
    float roughness;
};

NwbTestSurfaceMaterial surface;
NwbTestSurfaceMaterial surface;

)NWB_BIND";

static constexpr AStringView s_SurfaceOnlyMaterialBindSource = R"NWB_BIND([material_constant]
struct NwbTestSurfaceMaterial{
    [default("float4(1.0, 1.0, 1.0, 1.0)")]
    float4 base_color;

    [default("float(0.5)")]
    float roughness;
};

NwbTestSurfaceMaterial surface;

)NWB_BIND";

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
#endif

static constexpr AStringView s_HalfMaterialBindSource = R"NWB_BIND([material_constant]
struct NwbTestSurfaceMaterial{
    [default("half(0.5)")]
    half roughness;

    [default("half2(0.0, 1.0)")]
    half2 range;

    [default("half3(0.25, 0.5, 0.75)")]
    half3 tint;

    [default("half4(1.0, 1.0, 1.0, 1.0)")]
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

#if defined(NWB_FINAL)
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
#endif

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

)" NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_INDICES;
#endif


static constexpr AStringView s_MinimalSkinnedMeshMeta =
    NWB_ASSETS_GRAPHICS_TEST_SKINNED_MESH_TRIANGLE_PREFIX
    NWB_ASSETS_GRAPHICS_TEST_ROOT_SKIN;

static constexpr AStringView s_GeneratedFrameSkinnedMeshMeta =
    "skinned_mesh asset;\n\n"
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_POSITIONS
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_NORMALS
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_UV0
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_COLORS
    NWB_ASSETS_GRAPHICS_TEST_SKINNED_TRIANGLE_MISSING_TANGENT_VERTEX_REFS
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_INDICES
    NWB_ASSETS_GRAPHICS_TEST_ROOT_SKIN;

#if defined(NWB_FINAL)
static constexpr AStringView s_EmptyListTangentSkinnedMeshMeta =
    "skinned_mesh asset;\n\n"
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_POSITIONS
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_NORMALS
    R"(asset.tangents = [];

)"
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_UV0
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_COLORS
    NWB_ASSETS_GRAPHICS_TEST_SKINNED_TRIANGLE_MISSING_TANGENT_VERTEX_REFS
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_INDICES
    NWB_ASSETS_GRAPHICS_TEST_ROOT_SKIN;

static constexpr AStringView s_EmptyMapTangentSkinnedMeshMeta =
    "skinned_mesh asset;\n\n"
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_POSITIONS
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_NORMALS
    R"(asset.tangents = {};

)"
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_UV0
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_COLORS
    NWB_ASSETS_GRAPHICS_TEST_SKINNED_TRIANGLE_MISSING_TANGENT_VERTEX_REFS
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_INDICES
    NWB_ASSETS_GRAPHICS_TEST_ROOT_SKIN;
#endif

static constexpr AStringView s_NativeCharacterMockSkinnedMeshMeta = R"(skinned_mesh asset;

asset.positions = [
    [-0.5, -0.5, 0.0],
    [ 0.5, -0.5, 0.0],
    [ 0.5,  0.5, 0.0],
    [-0.5,  0.5, 0.0],
];

)" NWB_ASSETS_GRAPHICS_TEST_QUAD_NORMALS
    NWB_ASSETS_GRAPHICS_TEST_QUAD_TANGENTS
    R"(asset.uv0 = [
    [0.0, 0.0],
    [1.0, 0.0],
    [1.0, 1.0],
    [0.0, 1.0],
];

asset.colors = [
    [1.0, 0.0, 0.0, 1.0],
    [0.0, 1.0, 0.0, 1.0],
    [0.0, 0.0, 1.0, 1.0],
    [1.0, 1.0, 1.0, 0.5],
];

asset.vertex_refs = [
    [0, 0, 0, 0, 0, 0],
    [1, 1, 1, 1, 1, 1],
    [2, 2, 2, 2, 2, 2],
    [3, 3, 3, 3, 3, 3],
];

asset.indices = [
    [0, 1, 2],
    [0, 2, 3],
];

asset.skeleton_joint_count = 2;

asset.inverse_bind_matrices = [
    [
        [1.0, 0.0, 0.0, 0.0],
        [0.0, 1.0, 0.0, 0.0],
        [0.0, 0.0, 1.0, 0.0],
        [0.0, 0.0, 0.0, 1.0],
    ],
    [
        [1.0, 0.0, 0.0, 0.0],
        [0.0, 1.0, 0.0, 0.0],
        [0.0, 0.0, 1.0, 0.0],
        [-0.25, 0.0, 0.0, 1.0],
    ],
];

asset.skin = {
    "joints0": [
        [0, 0, 0, 0],
        [0, 1, 0, 0],
        [1, 0, 0, 0],
        [1, 0, 0, 0],
    ],
    "weights0": [
        [1.0, 0.0, 0.0, 0.0],
        [0.75, 0.25, 0.0, 0.0],
        [1.0, 0.0, 0.0, 0.0],
        [1.0, 0.0, 0.0, 0.0],
    ],
};
)";

static constexpr AStringView s_NonnormalizedSkinSkinnedMeshMeta =
    NWB_ASSETS_GRAPHICS_TEST_SKINNED_MESH_TRIANGLE_PREFIX
    R"(asset.skeleton_joint_count = 2;

asset.skin = {
    "joints0": [
        [0, 0, 0, 0],
        [0, 1, 0, 0],
        [0, 1, 0, 0],
    ],
    "weights0": [
        [2.0, 0.0, 0.0, 0.0],
        [3.0, 1.0, 0.0, 0.0],
        [0.0, 4.0, 0.0, 0.0],
    ],
};

asset.inverse_bind_matrices = [
    [
        [1.0, 0.0, 0.0, 0.0],
        [0.0, 1.0, 0.0, 0.0],
        [0.0, 0.0, 1.0, 0.0],
        [0.0, 0.0, 0.0, 1.0],
    ],
    [
        [1.0, 0.0, 0.0, 0.0],
        [0.0, 1.0, 0.0, 0.0],
        [0.0, 0.0, 1.0, 0.0],
        [0.0, 0.0, 0.0, 1.0],
    ],
];
)";

static constexpr AStringView s_SkinnedOnlySkinnedMeshMeta =
    NWB_ASSETS_GRAPHICS_TEST_SKINNED_MESH_TRIANGLE_PREFIX
    NWB_ASSETS_GRAPHICS_TEST_ROOT_SKIN;

#if defined(NWB_FINAL)
static constexpr AStringView s_MismatchedSkinnedMeshMeta =
    "skinned_mesh asset;\n\n"
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_POSITIONS
    R"(asset.normals = [
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
];

)" NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_TANGENTS
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_UV0
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_INDICES;

static constexpr AStringView s_MismatchedSkinSkinnedMeshMeta =
    NWB_ASSETS_GRAPHICS_TEST_SKINNED_MESH_TRIANGLE_PREFIX
    R"(asset.skin = {
    "joints0": [
        [0, 0, 0, 0],
        [0, 0, 0, 0],
        [0, 0, 0, 0],
    ],
    "weights0": [
        [1.0, 0.0, 0.0, 0.0],
        [1.0, 0.0, 0.0, 0.0],
    ],
};
)";

static constexpr AStringView s_SourceImportSkinnedMeshMeta = R"(skinned_mesh asset;

asset.source = {
    "format": "external",
    "path": "mesh.bin",
};
)";
#endif



#undef NWB_ASSETS_GRAPHICS_TEST_QUAD_TANGENTS
#undef NWB_ASSETS_GRAPHICS_TEST_QUAD_NORMALS
#undef NWB_ASSETS_GRAPHICS_TEST_ROOT_SKIN
#undef NWB_ASSETS_GRAPHICS_TEST_SKINNED_MESH_TRIANGLE_PREFIX
#undef NWB_ASSETS_GRAPHICS_TEST_SKINNED_MESH_TRIANGLE_STREAMS
#undef NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_INDICES
#undef NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_COLORS
#undef NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_UV0
#undef NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_TANGENTS
#undef NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_NORMALS
#undef NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_POSITIONS


static bool PrepareCleanDirectory(const Path& directory){
    ErrorCode errorCode;
    if(!RemoveAllIfExists(directory, errorCode))
        return false;
    errorCode.clear();
    return EnsureDirectories(directory, errorCode);
}

static bool WriteTextFile(const Path& filePath, const AStringView text){
    ErrorCode errorCode;
    if(!EnsureDirectories(filePath.parent_path(), errorCode))
        return false;

    GlobalFilesystemDetail::OutputFileStream file(
        filePath,
        GlobalFilesystemDetail::OutputFileStream::binary | GlobalFilesystemDetail::OutputFileStream::trunc
    );
    if(!file)
        return false;

    file.write(text.data(), static_cast<GlobalFilesystemDetail::StreamSize>(text.size()));
    return static_cast<bool>(file);
}

static const char* AssetsGraphicsTestConfigurationName(){
#if defined(NWB_DEBUG)
    return "dbg";
#elif defined(NWB_FINAL)
    return "fin";
#else
    return "opt";
#endif
}

static Path AssetsGraphicsTestRepoRoot(){
    return Path(__FILE__).parent_path().parent_path().parent_path().lexically_normal();
}

static Path AssetsGraphicsTestCaseRoot(const AStringView caseName){
    return AssetsGraphicsTestRepoRoot() / "__build_obj" / "nwb_assets_graphics_tests" / AssetsGraphicsTestConfigurationName() / AString(caseName);
}

static bool PrepareAssetsGraphicsCaseRoot(const AStringView caseName, Path& outRoot){
    outRoot = AssetsGraphicsTestCaseRoot(caseName);
    return PrepareCleanDirectory(outRoot);
}

static bool PrepareAssetsGraphicsCookCase(
    const AStringView caseName,
    Path& outRoot,
    Path& outOutputDirectory
){
    if(!PrepareAssetsGraphicsCaseRoot(caseName, outRoot))
        return false;

    outOutputDirectory = outRoot / "cooked";
    return true;
}

static bool CookPreparedGraphicsAssetRoots(
    TestArena& testArena,
    const Path& root,
    const Path& outputDirectory,
    const InitializerList<Path> assetRoots
){
    NWB::Core::Assets::AssetCookOptions options(testArena.arena);
    options.repoRoot = PathToString(testArena.arena, AssetsGraphicsTestRepoRoot());
    options.assetRoots.reserve(assetRoots.size());
    for(const Path& assetRoot : assetRoots)
        options.assetRoots.push_back(PathToString(testArena.arena, assetRoot));
    options.outputDirectory = PathToString(testArena.arena, outputDirectory);
    options.cacheDirectory = PathToString(testArena.arena, root / "cache");
    if(!options.configuration.assign("tests") || !options.assetType.assign("graphics"))
        return false;

    NWB::Impl::GraphicsAssetCooker cooker(testArena.arena);
    return cooker.cook(options);
}

static bool CookSingleGraphicsMeta(
    const AStringView metaText,
    const AStringView caseName,
    const char* assetDirectory,
    const char* assetFilename,
    TestArena& testArena,
    Path& outRoot,
    Path& outOutputDirectory
){
    if(!PrepareAssetsGraphicsCookCase(caseName, outRoot, outOutputDirectory))
        return false;

    const Path assetRoot = outRoot / "assets";
    const Path metaPath = assetRoot / assetDirectory / assetFilename;
    if(!WriteTextFile(metaPath, metaText))
        return false;

    return CookPreparedGraphicsAssetRoots(testArena, outRoot, outOutputDirectory, { assetRoot });
}

struct MinimalAssetCookInfo{
    const char* assetDirectory = "";
    const char* assetFilename = "";
};

static bool CookSingleMinimalAssetMeta(
    const AStringView metaText,
    const AStringView caseName,
    const MinimalAssetCookInfo& cookInfo,
    TestArena& testArena,
    Path& outRoot,
    Path& outOutputDirectory
){
    return CookSingleGraphicsMeta(
        metaText,
        caseName,
        cookInfo.assetDirectory,
        cookInfo.assetFilename,
        testArena,
        outRoot,
        outOutputDirectory
    );
}

static bool CookSingleSkinnedMeshMeta(
    const AStringView metaText,
    const AStringView caseName,
    TestArena& testArena,
    Path& outRoot,
    Path& outOutputDirectory
){
    static constexpr MinimalAssetCookInfo s_CookInfo{ "characters", "minimal_skinned_mesh.nwb" };
    return CookSingleMinimalAssetMeta(metaText, caseName, s_CookInfo, testArena, outRoot, outOutputDirectory);
}

static bool CookSingleMeshMeta(
    const AStringView metaText,
    const AStringView caseName,
    TestArena& testArena,
    Path& outRoot,
    Path& outOutputDirectory
){
    static constexpr MinimalAssetCookInfo s_CookInfo{ "meshes", "minimal_mesh.nwb" };
    return CookSingleMinimalAssetMeta(metaText, caseName, s_CookInfo, testArena, outRoot, outOutputDirectory);
}

static bool ReadSmokeAssetMeta(const char* assetDirectory, const char* assetFilename, AString& outMetaText){
    return ReadTextFile(
        AssetsGraphicsTestRepoRoot() / "tests" / "smoke" / "assets" / assetDirectory / assetFilename,
        outMetaText
    );
}

static bool CookSmokeAssetMeta(
    const char* assetDirectory,
    const char* assetFilename,
    const AStringView caseName,
    TestArena& testArena,
    Path& outRoot,
    Path& outOutputDirectory
){
    AString metaText;
    if(!ReadSmokeAssetMeta(assetDirectory, assetFilename, metaText))
        return false;

    return CookSingleGraphicsMeta(
        AStringView(metaText.data(), metaText.size()),
        caseName,
        assetDirectory,
        assetFilename,
        testArena,
        outRoot,
        outOutputDirectory
    );
}

static bool CookSmokeMeshMeta(
    const char* assetFilename,
    const AStringView caseName,
    TestArena& testArena,
    Path& outRoot,
    Path& outOutputDirectory
){
    return CookSmokeAssetMeta("meshes", assetFilename, caseName, testArena, outRoot, outOutputDirectory);
}

static bool CookSmokeSkinnedMeshMeta(
    const char* assetFilename,
    const AStringView caseName,
    TestArena& testArena,
    Path& outRoot,
    Path& outOutputDirectory
){
    return CookSmokeAssetMeta("characters", assetFilename, caseName, testArena, outRoot, outOutputDirectory);
}

static bool CookMinimalMeshWithMaterialBind(
    const AStringView bindText,
    const AStringView caseName,
    TestArena& testArena,
    Path& outRoot,
    Path& outOutputDirectory
){
    if(!PrepareAssetsGraphicsCookCase(caseName, outRoot, outOutputDirectory))
        return false;

    const Path assetRoot = outRoot / "assets";
    if(!WriteTextFile(assetRoot / "meshes" / "minimal_mesh.nwb", s_MinimalMeshMeta))
        return false;
    if(!WriteTextFile(assetRoot / "material_interfaces" / "test_surface.bind", bindText))
        return false;

    return CookPreparedGraphicsAssetRoots(testArena, outRoot, outOutputDirectory, { assetRoot });
}

static bool ParseMaterialBindFromText(
    const AStringView bindText,
    const AStringView caseName,
    NWB::Impl::MaterialBindEntry& outEntry,
    Path& outRoot,
    NWB::Core::Alloc::ScratchArena& scratchArena
){
    if(!PrepareAssetsGraphicsCaseRoot(caseName, outRoot))
        return false;

    const Path bindPath = outRoot / "assets" / "material_interfaces" / "test_surface.bind";
    if(!WriteTextFile(bindPath, bindText))
        return false;

    return NWB::Impl::ParseMaterialBindSource(bindPath, outEntry, scratchArena);
}

#if defined(NWB_FINAL)
static bool CookDuplicateGeneratedMaterialBindIncludePath(
    const AStringView caseName,
    TestArena& testArena,
    Path& outRoot,
    Path& outOutputDirectory
){
    if(!PrepareAssetsGraphicsCookCase(caseName, outRoot, outOutputDirectory))
        return false;

    const Path firstAssetRoot = outRoot / "first" / "assets";
    const Path secondAssetRoot = outRoot / "second" / "assets";
    if(!WriteTextFile(firstAssetRoot / "meshes" / "minimal_mesh.nwb", s_MinimalMeshMeta))
        return false;
    if(!WriteTextFile(firstAssetRoot / "material_interfaces" / "test_surface.bind", s_MinimalMaterialBindSource))
        return false;
    if(!WriteTextFile(secondAssetRoot / "material_interfaces" / "test_surface.bind", s_MinimalMaterialBindSource))
        return false;

    return CookPreparedGraphicsAssetRoots(testArena, outRoot, outOutputDirectory, { firstAssetRoot, secondAssetRoot });
}
#endif

static bool WriteMaterialBindMeshShaderProbeSource(
    TestArena& testArena,
    const Path& assetRoot,
    const char* metaFilename,
    const char* sourceFilename,
    const AStringView sourceText
){
    const Path engineGraphicsIncludeRoot = AssetsGraphicsTestRepoRoot() / "impl" / "assets" / "graphics";
    const NWB::Impl::ShaderCook::CookString engineGraphicsIncludeRootText = PathToString(
        testArena.arena,
        engineGraphicsIncludeRoot
    );
    NWB::Impl::ShaderCook::CookString meshShaderMeta(testArena.arena);
    meshShaderMeta +=
        "shader asset;\n\n"
        "asset.stage = \"mesh\";\n"
        "asset.target_profile = \"spirv_1_5\";\n"
        "asset.entry_point = \"main\";\n"
        "asset.include_roots = [\""
    ;
    meshShaderMeta += engineGraphicsIncludeRootText;
    meshShaderMeta += "\"];\n";

    if(!WriteTextFile(
        assetRoot / "shaders" / metaFilename,
        AStringView(meshShaderMeta.data(), meshShaderMeta.size())
    ))
        return false;
    return WriteTextFile(assetRoot / "shaders" / sourceFilename, sourceText);
}

static bool WriteMaterialBindMeshShaderProbe(
    TestArena& testArena,
    const Path& assetRoot,
    const char* metaFilename,
    const char* sourceFilename
){
    return WriteMaterialBindMeshShaderProbeSource(
        testArena,
        assetRoot,
        metaFilename,
        sourceFilename,
        s_MaterialBindShaderProbeSource
    );
}

static bool CookMaterialBindShaderProbe(
    const AStringView bindText,
    const AStringView caseName,
    TestArena& testArena,
    Path& outRoot,
    Path& outOutputDirectory
){
    if(!PrepareAssetsGraphicsCookCase(caseName, outRoot, outOutputDirectory))
        return false;

    const Path assetRoot = outRoot / "assets";
    if(!WriteTextFile(assetRoot / "material_interfaces" / "test_surface.bind", bindText))
        return false;
    if(!WriteMaterialBindMeshShaderProbe(testArena, assetRoot, "bind_probe.nwb", "bind_probe.slang"))
        return false;

    return CookPreparedGraphicsAssetRoots(testArena, outRoot, outOutputDirectory, { assetRoot });
}

static bool WriteMaterialBindMaterialIntegrationAssetsWithMeshSource(
    TestArena& testArena,
    const Path& assetRoot,
    const AStringView bindText,
    const AStringView materialText,
    const AStringView meshSourceText
){
    if(!WriteTextFile(assetRoot / "material_interfaces" / "test_surface.bind", bindText))
        return false;
    if(!WriteMaterialBindMeshShaderProbeSource(
        testArena,
        assetRoot,
        "material_mesh.nwb",
        "material_mesh.slang",
        meshSourceText
    ))
        return false;
    if(!WriteTextFile(
        assetRoot / "shaders" / "material_ps.nwb",
        "shader asset;\n\n"
        "asset.stage = \"ps\";\n"
        "asset.target_profile = \"spirv_1_5\";\n"
        "asset.entry_point = \"main\";\n"
    ))
        return false;
    if(!WriteTextFile(assetRoot / "shaders" / "material_ps.slang", s_MaterialBindPixelShaderProbeSource))
        return false;
    return WriteTextFile(assetRoot / "materials" / "test_material.nwb", materialText);
}

static bool WriteMaterialBindMaterialIntegrationAssets(
    TestArena& testArena,
    const Path& assetRoot,
    const AStringView bindText,
    const AStringView materialText
){
    return WriteMaterialBindMaterialIntegrationAssetsWithMeshSource(
        testArena,
        assetRoot,
        bindText,
        materialText,
        s_MaterialBindShaderProbeSource
    );
}

static bool CookMaterialBindMaterialIntegrationWithMeshSource(
    const AStringView bindText,
    const AStringView materialText,
    const AStringView meshSourceText,
    const AStringView caseName,
    TestArena& testArena,
    Path& outRoot,
    Path& outOutputDirectory
){
    if(!PrepareAssetsGraphicsCookCase(caseName, outRoot, outOutputDirectory))
        return false;

    const Path assetRoot = outRoot / "assets";
    if(!WriteMaterialBindMaterialIntegrationAssetsWithMeshSource(
        testArena,
        assetRoot,
        bindText,
        materialText,
        meshSourceText
    ))
        return false;

    return CookPreparedGraphicsAssetRoots(testArena, outRoot, outOutputDirectory, { assetRoot });
}

static bool CookMaterialBindMaterialIntegration(
    const AStringView bindText,
    const AStringView materialText,
    const AStringView caseName,
    TestArena& testArena,
    Path& outRoot,
    Path& outOutputDirectory
){
    return CookMaterialBindMaterialIntegrationWithMeshSource(
        bindText,
        materialText,
        s_MaterialBindShaderProbeSource,
        caseName,
        testArena,
        outRoot,
        outOutputDirectory
    );
}

template<typename AssetCodecT>
static bool LoadCookedAsset(
    TestContext& context,
    TestArena& testArena,
    const Path& outputDirectory,
    const Name assetName,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset,
    const usize expectedVolumeFileCount = 2u
){
    NWB::Core::Filesystem::VolumeSession volumeSession(testArena.arena);
    const bool loadedVolume = volumeSession.load("graphics", outputDirectory);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedVolume);
    if(!loadedVolume)
        return false;

    if(expectedVolumeFileCount != 0u)
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, volumeSession.fileCount() == expectedVolumeFileCount);

    NWB::Core::Assets::AssetBytes binary = MakeAssetBytes(testArena);
    const bool loadedBinary = volumeSession.loadData(assetName, binary);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedBinary);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !binary.empty());
    if(!loadedBinary || binary.empty())
        return false;

    AssetCodecT codec;
    const bool deserialized = codec.deserialize(testArena.arena, assetName, binary, outLoadedAsset);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, deserialized);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, static_cast<bool>(outLoadedAsset));
    return deserialized && static_cast<bool>(outLoadedAsset);
}

static bool LoadCookedMinimalSkinnedMesh(
    TestContext& context,
    TestArena& testArena,
    const Path& outputDirectory,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset){
    return LoadCookedAsset<NWB::Impl::SkinnedMeshAssetCodec>(
        context,
        testArena,
        outputDirectory,
        Name("project/characters/minimal_skinned_mesh"),
        outLoadedAsset
    );
}

static bool LoadCookedMinimalMesh(
    TestContext& context,
    TestArena& testArena,
    const Path& outputDirectory,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset){
    return LoadCookedAsset<NWB::Impl::MeshAssetCodec>(
        context,
        testArena,
        outputDirectory,
        Name("project/meshes/minimal_mesh"),
        outLoadedAsset
    );
}

static bool LoadCookedMesh(
    TestContext& context,
    TestArena& testArena,
    const Path& outputDirectory,
    const Name assetName,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset){
    return LoadCookedAsset<NWB::Impl::MeshAssetCodec>(
        context,
        testArena,
        outputDirectory,
        assetName,
        outLoadedAsset
    );
}

static bool LoadCookedSkinnedMesh(
    TestContext& context,
    TestArena& testArena,
    const Path& outputDirectory,
    const Name assetName,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset){
    return LoadCookedAsset<NWB::Impl::SkinnedMeshAssetCodec>(
        context,
        testArena,
        outputDirectory,
        assetName,
        outLoadedAsset
    );
}

static bool LoadCookedMaterial(
    TestContext& context,
    TestArena& testArena,
    const Path& outputDirectory,
    const Name assetName,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset
){
    return LoadCookedAsset<NWB::Impl::MaterialAssetCodec>(
        context,
        testArena,
        outputDirectory,
        assetName,
        outLoadedAsset,
        0u
    );
}

static bool LoadCookedShaderArchiveRecords(
    TestContext& context,
    TestArena& testArena,
    const Path& outputDirectory,
    NWB::Core::GraphicsVector<NWB::Core::ShaderArchive::Record>& outRecords
){
    NWB::Core::Filesystem::VolumeSession volumeSession(testArena.arena);
    const bool loadedVolume = volumeSession.load("graphics", outputDirectory);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedVolume);
    if(!loadedVolume)
        return false;

    NWB::Core::GraphicsBytes indexBinary(testArena.arena);
    const bool loadedIndex = volumeSession.loadData(NWB::Core::ShaderArchive::IndexVirtualPathName(), indexBinary);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedIndex);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !indexBinary.empty());
    if(!loadedIndex || indexBinary.empty())
        return false;

    const bool deserialized = NWB::Core::ShaderArchive::deserializeIndex(indexBinary, outRecords);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, deserialized);
    return deserialized;
}

static bool FindShaderArchiveSourceChecksum(
    const NWB::Core::GraphicsVector<NWB::Core::ShaderArchive::Record>& records,
    const Name shaderName,
    const Name stageName,
    u64& outSourceChecksum
){
    outSourceChecksum = 0u;
    for(const NWB::Core::ShaderArchive::Record& record : records){
        const AStringView variantName(record.variantName.data(), record.variantName.size());
        if(
            record.shaderName == shaderName
            && record.stage == stageName
            && variantName == NWB::Core::ShaderArchive::s_DefaultVariant
        ){
            outSourceChecksum = record.sourceChecksum;
            return outSourceChecksum != 0u;
        }
    }

    return false;
}

#include "assets_graphics_shader_tests.inl"
#include "assets_graphics_codec_tests.inl"

#include "assets_graphics_material_tests.inl"
#include "assets_graphics_material_cook_tests.inl"

#include "assets_graphics_mesh_cooker_tests.inl"

NWB_DEFINE_TEST_ENTRY_POINT("assets graphics", [](NWB::Tests::TestContext& context){
    __hidden_assets_graphics_tests::TestVolumeSessionAcceptsScratchBytes(context);
    __hidden_assets_graphics_tests::TestMeshCodecRoundTrip(context);
    __hidden_assets_graphics_tests::TestSkinnedMeshCodecRoundTrip(context);
    __hidden_assets_graphics_tests::TestMinimalSkinnedMeshCodecRoundTrip(context);
    __hidden_assets_graphics_tests::TestSkinnedMeshCodecRejectsMalformedCounts(context);
    __hidden_assets_graphics_tests::TestSkinnedMeshCodecRejectsMalformedDependentCounts(context);
    __hidden_assets_graphics_tests::TestShaderArchiveVariantLookupIsExact(context);
    __hidden_assets_graphics_tests::TestSpirvEntryPointLookup(context);
    __hidden_assets_graphics_tests::TestShaderMetadataRejectsDefaultVariantAlias(context);
    __hidden_assets_graphics_tests::TestShaderDependencyChecksumAliasesGeneratedRoot(context);
    __hidden_assets_graphics_tests::TestShaderCookWithoutMaterialBindIncludes(context);
    __hidden_assets_graphics_tests::TestMaterialMetadataInterfaceAndBlockParameters(context);
    __hidden_assets_graphics_tests::TestMaterialCodecTypedLayoutBoundary(context);
    __hidden_assets_graphics_tests::TestMaterialBindSchemaValidation(context);
    __hidden_assets_graphics_tests::TestMaterialBindHalfTypedLayoutValues(context);
    __hidden_assets_graphics_tests::TestMaterialBindGeneratedSlangText(context);
    __hidden_assets_graphics_tests::TestMaterialBindCookIntegration(context);
    __hidden_assets_graphics_tests::TestMaterialRejectsMissingInterfaceCookIntegration(context);
    __hidden_assets_graphics_tests::TestMaterialBindDependencyInvalidation(context);
    __hidden_assets_graphics_tests::TestMeshCookerTypedStreams(context);
    __hidden_assets_graphics_tests::TestMeshCookerDefaultColors(context);
    __hidden_assets_graphics_tests::TestMeshAcceptanceHardEdgeCubeZippedRefs(context);
    __hidden_assets_graphics_tests::TestMeshAcceptanceSphereSmooth(context);
    __hidden_assets_graphics_tests::TestMeshAcceptanceUvSeamQuad(context);
    __hidden_assets_graphics_tests::TestMeshAcceptanceMirroredUvQuad(context);
    __hidden_assets_graphics_tests::TestMeshAcceptanceTwoSidedPlane(context);
    __hidden_assets_graphics_tests::TestSkinnedMeshAcceptanceBendingStrip(context);
    __hidden_assets_graphics_tests::TestMeshAcceptanceLargeManyMeshlets(context);
    __hidden_assets_graphics_tests::TestMaterialBindDiscoveryValidation(context);
    __hidden_assets_graphics_tests::TestMeshCookerValidationFailures(context);
    __hidden_assets_graphics_tests::TestSkinnedMeshCookerMinimalAsset(context);
    __hidden_assets_graphics_tests::TestSkinnedMeshCookerGeneratesMissingFrames(context);
    __hidden_assets_graphics_tests::TestSkinnedMeshCookerNativeCharacterMock(context);
    __hidden_assets_graphics_tests::TestSkinnedMeshCookerNormalizesSkinWeights(context);
    __hidden_assets_graphics_tests::TestSkinnedMeshCookerSkinnedClass(context);
    __hidden_assets_graphics_tests::TestSkinnedMeshCookerValidationFailures(context);
    __hidden_assets_graphics_tests::TestSkinnedMeshValidationFailures(context);
    __hidden_assets_graphics_tests::TestMeshClassPolicyHelpers(context);
    __hidden_assets_graphics_tests::TestFormatBlockDimensions(context);
})


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

