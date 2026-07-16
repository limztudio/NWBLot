// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_mesh/asset.h>
#include <impl/assets_mesh/meshlet_ref_codec.h>
#include <impl/assets_mesh/meshlet_payload_packing.h>
#include <impl/assets_model/asset.h>
#include <core/assets/bunch/cook.h>
#include <core/assets/volume/cooker.h>
#include <core/assets/auto_registration.h>
#include <core/assets/cook_entry_registry.h>
#include <impl/assets_material/cook.h>
#include <impl/assets_material/binary_payload.h>
#include <impl/assets_shader/asset.h>
#include <impl/assets_shader/cook.h>
#include <impl/assets/graphics/mesh/runtime_constants.h>

#include <core/assets/paths.h>
#include <tests/capturing_logger.h>
#include <tests/meshlet_ref_test_data.h>
#include <tests/test_context.h>

#include <gtest/gtest.h>

#include <core/alloc/scratch.h>
#include <core/alloc/thread.h>
#include <core/common/module.h>
#include <core/mesh/classification.h>
#include <core/filesystem/module.h>
#include <core/graphics/api.h>
#include <core/graphics/shader_archive.h>
#include <core/graphics/spirv_entry_point.h>

#include <global/binary.h>
#include <global/compile.h>
#include <global/filesystem.h>
#include <global/hash_utils.h>
#include <global/math/convert.h>
#include <global/simdmath.h>

#include <cmath>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
using CapturingLogger = NWB::Tests::CapturingLogger;
using NWB::Tests::MakeQuadTriangleIndices;
using NWB::Tests::MakeTriangleIndices;
using AString = NWB::Tests::TestAString;
#define NWB_ASSETS_GRAPHICS_TEST_STRINGIFY_IMPL(Value) #Value
#define NWB_ASSETS_GRAPHICS_TEST_STRINGIFY(Value) NWB_ASSETS_GRAPHICS_TEST_STRINGIFY_IMPL(Value)

inline constexpr Name s_MaterialScratchArena("tests/assets_graphics/material");
inline constexpr Name s_MaterialCookScratchArena("tests/assets_graphics/material_cook");
inline constexpr Name s_ShaderScratchArena("tests/assets_graphics/shader");
inline constexpr Name s_ModelFixtureScratchArena("tests/assets_graphics/model_fixture");
inline constexpr Name s_CodecScratchArena("tests/assets_graphics/codec");
inline constexpr Name s_ProjectCookEntryArena("tests/assets_graphics/project_cook_entry");
inline constexpr Name s_AutoRegistrationQueueArena("tests/assets_graphics/auto_registration_queue");


struct AssetsGraphicsTestArenaTag{};
using TestArena = NWB::Tests::TestArena<AssetsGraphicsTestArenaTag>;
using Path = NWB::Path;

template<typename T>
static NWB::Core::Assets::AssetVector<T> MakeAssetVector(TestArena& testArena){
    return NWB::Core::Assets::AssetVector<T>(testArena.arena);
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

static void AppendTestMeta(AString& inOutMeta, const AStringView text){
    inOutMeta.append(text.data(), text.size());
}

#if defined(NWB_FINAL)
static AString BuildTriangleMeta(
    const AStringView assetHeader,
    const AStringView normalField,
    const AStringView tangentField,
    const AStringView vertexRefsField,
    const AStringView suffix
){
    AString meta;
    meta.reserve(1536u);
    AppendTestMeta(meta, assetHeader);
    AppendTestMeta(meta, NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_POSITIONS);
    AppendTestMeta(meta, normalField);
    AppendTestMeta(meta, tangentField);
    AppendTestMeta(meta, NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_UV0);
    AppendTestMeta(meta, NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_COLORS);
    AppendTestMeta(meta, vertexRefsField);
    AppendTestMeta(meta, NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_INDICES);
    AppendTestMeta(meta, suffix);
    return meta;
}

static AString BuildMeshTriangleMeta(
    const AStringView normalField,
    const AStringView tangentField,
    const AStringView vertexRefsField
){
    return BuildTriangleMeta("mesh asset;\n\n", normalField, tangentField, vertexRefsField, "");
}
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
        materialBindConstantsValid ? surface.base_color.rgb : float3(1.0, 0.0, 1.0),
        inNormal
    );
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
        materialBindConstantsValid ? baseColor : float3(1.0, 0.0, 1.0),
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
        materialBindConstantsValid ? baseColor : float3(1.0, 0.0, 1.0),
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
    return nwbMakeMeshSurface(surface.base_color.rgb, inNormal);
}

)NWB_SLANG";
#endif

#if defined(NWB_FINAL)
// A standalone G-buffer pixel shader that does NOT include the material's generated `.bind`. Because it omits
// the bind (and therefore NWB_MATERIAL_TYPED_BINDING), it cannot include mesh/material_ps_authoring.slangi
// (that header hard-errors without the typed binding), so it writes the three G-buffer render targets directly
// using the engine target locations. This exercises the PIXEL interface validation ("does not include a
// generated material bind").
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
        "roughness": "half(0.25)",
        "range": "half2(0.125, 0.5)",
        "tint": "half3(1.0, 0.75, 0.5)",
        "base_color": "half4(1.0, 0.5, 0.25, 0.0)",
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

)" NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_TANGENTS
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_INDICES;
#endif


#undef NWB_ASSETS_GRAPHICS_TEST_QUAD_TANGENTS
#undef NWB_ASSETS_GRAPHICS_TEST_QUAD_NORMALS
#undef NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_INDICES
#undef NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_COLORS
#undef NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_UV0
#undef NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_TANGENTS
#undef NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_MISSING_TANGENT_VERTEX_REFS
#undef NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_DEFAULT_COLOR_VERTEX_REFS
#undef NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_MISSING_NORMAL_VERTEX_REFS
#undef NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_VERTEX_REFS
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

static Path AssetsGraphicsTestRepoRoot(TestArena& testArena){
    return Path(testArena.arena, __FILE__).parent_path().parent_path().parent_path().lexically_normal();
}

static Path AssetsGraphicsTestCaseRoot(TestArena& testArena, const AStringView caseName){
    return AssetsGraphicsTestRepoRoot(testArena) / "__build_obj" / "nwb_assets_graphics_tests" / AssetsGraphicsTestConfigurationName() / AString(caseName);
}

static bool PrepareAssetsGraphicsCaseRoot(TestArena& testArena, const AStringView caseName, Path& outRoot){
    outRoot = AssetsGraphicsTestCaseRoot(testArena, caseName);
    return PrepareCleanDirectory(outRoot);
}

static bool PrepareAssetsGraphicsCookCase(
    TestArena& testArena,
    const AStringView caseName,
    Path& outRoot,
    Path& outOutputDirectory
){
    if(!PrepareAssetsGraphicsCaseRoot(testArena, caseName, outRoot))
        return false;

    outOutputDirectory = outRoot / "cooked";
    return true;
}

static bool CookPreparedGraphicsAssetRoots(
    TestArena& testArena,
    const Path& root,
    const Path& outputDirectory,
    const InitializerList<Path> assetRoots,
    const u32 workerThreadCount = 0u
){
    NWB::Core::Alloc::ThreadPool cookThreadPool(workerThreadCount, NWB::Core::Alloc::CoreAffinity::Any);
    NWB::Core::Assets::AssetCookOptions options(testArena.arena, cookThreadPool);
    options.repoRoot = PathToString(testArena.arena, AssetsGraphicsTestRepoRoot(testArena));
    options.assetRoots.reserve(assetRoots.size());
    for(const Path& assetRoot : assetRoots){
        auto parentDirectoryName = PathToString(testArena.arena, assetRoot.lexically_normal().parent_path().filename());
        CanonicalizeTextInPlace(parentDirectoryName);

        ACompactString virtualRoot;
        if(!virtualRoot.assign(parentDirectoryName == "impl"
            ? NWB::Core::Assets::s_EngineVirtualRoot
            : NWB::Core::Assets::s_ProjectVirtualRoot
        ))
            return false;

        auto assetRootText = PathToString(testArena.arena, assetRoot);
        options.assetRoots.emplace_back(
            testArena.arena,
            AStringView(assetRootText.data(), assetRootText.size()),
            virtualRoot
        );
    }
    options.outputDirectory = PathToString(testArena.arena, outputDirectory);
    options.cacheDirectory = PathToString(testArena.arena, root / "cache");
    if(!options.configuration.assign("tests") || !options.assetType.assign("graphics"))
        return false;

    NWB::Core::Assets::AssetVolumeCooker cooker(testArena.arena);
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
    if(!PrepareAssetsGraphicsCookCase(testArena, caseName, outRoot, outOutputDirectory))
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

static bool ReadSmokeAssetMeta(
    TestArena& testArena,
    const char* assetDirectory,
    const char* assetFilename,
    AString& outMetaText
){
    return ReadTextFile(
        AssetsGraphicsTestRepoRoot(testArena) / "tests" / "smoke" / "assets" / assetDirectory / assetFilename,
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
    if(!ReadSmokeAssetMeta(testArena, assetDirectory, assetFilename, metaText))
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

static bool CookMinimalMeshWithMaterialBind(
    const AStringView bindText,
    const AStringView caseName,
    TestArena& testArena,
    Path& outRoot,
    Path& outOutputDirectory
){
    if(!PrepareAssetsGraphicsCookCase(testArena, caseName, outRoot, outOutputDirectory))
        return false;

    const Path assetRoot = outRoot / "assets";
    if(!WriteTextFile(assetRoot / "meshes" / "minimal_mesh.nwb", s_MinimalMeshMeta))
        return false;
    if(!WriteTextFile(assetRoot / "material_interfaces" / "test_surface.bind", bindText))
        return false;

    return CookPreparedGraphicsAssetRoots(testArena, outRoot, outOutputDirectory, { assetRoot });
}

static bool ParseMaterialBindFromText(
    TestArena& testArena,
    const AStringView bindText,
    const AStringView caseName,
    NWB::Impl::MaterialBindEntry& outEntry,
    Path& outRoot,
    NWB::Core::Alloc::ScratchArena& scratchArena
){
    if(!PrepareAssetsGraphicsCaseRoot(testArena, caseName, outRoot))
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
    if(!PrepareAssetsGraphicsCookCase(testArena, caseName, outRoot, outOutputDirectory))
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

// Writes a shader `.nwb` + source pair. Both the mesh and pixel material shaders include engine graphics
// headers (mesh/authoring.slangi, mesh/material_ps_authoring.slangi), so the meta always declares the engine
// graphics include root.
static bool WriteMaterialBindShaderProbeSource(
    TestArena& testArena,
    const Path& assetRoot,
    const char* stage,
    const char* metaFilename,
    const char* sourceFilename,
    const AStringView sourceText
){
    const Path engineGraphicsIncludeRoot = AssetsGraphicsTestRepoRoot(testArena) / "impl" / "assets" / "graphics";
    const NWB::Impl::ShaderCook::CookString engineGraphicsIncludeRootText = PathToString(
        testArena.arena,
        engineGraphicsIncludeRoot
    );
    NWB::Impl::ShaderCook::CookString shaderMeta(testArena.arena);
    shaderMeta += "shader asset;\n\nasset.stage = \"";
    shaderMeta += stage;
    shaderMeta +=
        "\";\n"
        "asset.target_profile = \"spirv_1_5\";\n"
        "asset.entry_point = \"main\";\n"
        "asset.include_roots = [\""
    ;
    shaderMeta += engineGraphicsIncludeRootText;
    shaderMeta += "\"];\n";

    if(!WriteTextFile(
        assetRoot / "shaders" / metaFilename,
        AStringView(shaderMeta.data(), shaderMeta.size())
    ))
        return false;
    return WriteTextFile(assetRoot / "shaders" / sourceFilename, sourceText);
}

static bool CookMaterialBindShaderProbe(
    const AStringView bindText,
    const AStringView caseName,
    TestArena& testArena,
    Path& outRoot,
    Path& outOutputDirectory
){
    if(!PrepareAssetsGraphicsCookCase(testArena, caseName, outRoot, outOutputDirectory))
        return false;

    const Path assetRoot = outRoot / "assets";
    if(!WriteTextFile(assetRoot / "material_interfaces" / "test_surface.bind", bindText))
        return false;
    // The typed-binding probe is a pixel shader because it reads typed material data and includes the generated bind.
    if(!WriteMaterialBindShaderProbeSource(
        testArena,
        assetRoot,
        "ps",
        "bind_probe.nwb",
        "bind_probe.slang",
        s_MaterialBindShaderProbeSource
    ))
        return false;

    return CookPreparedGraphicsAssetRoots(testArena, outRoot, outOutputDirectory, { assetRoot });
}

// Minimal deferred-lighting BXDF fragment (the deferred lighting framework is in scope; the fragment includes
// nothing). Required at cook for every material.
static constexpr AStringView s_MaterialBindBxdfSource =
    "half3 NWB_DEFERRED_BXDF_FUNCTION(NwbBxdfSurface surface, int2 pixel){ return surface.baseColor; }\n";

// Writes the full material-integration asset set: a fixed generic mesh shader (material_mesh), a varying pixel
// shader (material_ps, which reads the typed .bind), the required bxdf fragment, the .bind interface, and the
// material. The pixel source varies per test (it carries the typed-binding constant assertions).
static bool WriteMaterialBindMaterialIntegrationAssetsWithPixelSource(
    TestArena& testArena,
    const Path& assetRoot,
    const AStringView bindText,
    const AStringView materialText,
    const AStringView pixelSourceText
){
    if(!WriteTextFile(assetRoot / "material_interfaces" / "test_surface.bind", bindText))
        return false;
    if(!WriteMaterialBindShaderProbeSource(
        testArena,
        assetRoot,
        "mesh",
        "material_mesh.nwb",
        "material_mesh.slang",
        s_MaterialBindMeshSource
    ))
        return false;
    if(!WriteMaterialBindShaderProbeSource(
        testArena,
        assetRoot,
        "ps",
        "material_ps.nwb",
        "material_ps.slang",
        pixelSourceText
    ))
        return false;
    if(!WriteTextFile(assetRoot / "shaders" / "material_bxdf.bxdf", s_MaterialBindBxdfSource))
        return false;
    return WriteTextFile(assetRoot / "materials" / "test_material.nwb", materialText);
}

static bool WriteMaterialBindMaterialIntegrationAssets(
    TestArena& testArena,
    const Path& assetRoot,
    const AStringView bindText,
    const AStringView materialText
){
    return WriteMaterialBindMaterialIntegrationAssetsWithPixelSource(
        testArena,
        assetRoot,
        bindText,
        materialText,
        s_MaterialBindShaderProbeSource
    );
}

static bool CookMaterialBindMaterialIntegrationWithPixelSource(
    const AStringView bindText,
    const AStringView materialText,
    const AStringView pixelSourceText,
    const AStringView caseName,
    TestArena& testArena,
    Path& outRoot,
    Path& outOutputDirectory
){
    if(!PrepareAssetsGraphicsCookCase(testArena, caseName, outRoot, outOutputDirectory))
        return false;

    const Path assetRoot = outRoot / "assets";
    if(!WriteMaterialBindMaterialIntegrationAssetsWithPixelSource(
        testArena,
        assetRoot,
        bindText,
        materialText,
        pixelSourceText
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
    return CookMaterialBindMaterialIntegrationWithPixelSource(
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
    TestArena& testArena,
    const Path& outputDirectory,
    const Name assetName,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset,
    const usize expectedVolumeFileCount = 2u
){
    NWB::Core::Filesystem::VolumeSession volumeSession(testArena.arena);
    const bool loadedVolume = volumeSession.load("graphics", outputDirectory);
    EXPECT_TRUE(loadedVolume);
    if(!loadedVolume)
        return false;

    if(expectedVolumeFileCount != 0u)
        EXPECT_EQ(volumeSession.fileCount(), expectedVolumeFileCount);

    NWB::Core::Assets::AssetBytes binary = MakeAssetBytes(testArena);
    const bool loadedBinary = volumeSession.loadData(assetName, binary);
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
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset){
    return LoadCookedAsset<NWB::Impl::MeshAssetCodec>(
        testArena,
        outputDirectory,
        Name("project/meshes/minimal_mesh"),
        outLoadedAsset
    );
}

static bool LoadCookedMesh(
    TestArena& testArena,
    const Path& outputDirectory,
    const Name assetName,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset){
    return LoadCookedAsset<NWB::Impl::MeshAssetCodec>(
        testArena,
        outputDirectory,
        assetName,
        outLoadedAsset
    );
}

static bool LoadCookedMaterial(
    TestArena& testArena,
    const Path& outputDirectory,
    const Name assetName,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset
){
    return LoadCookedAsset<NWB::Impl::MaterialAssetCodec>(
        testArena,
        outputDirectory,
        assetName,
        outLoadedAsset,
        0u
    );
}

static bool LoadCookedShaderArchiveRecords(
    TestArena& testArena,
    const Path& outputDirectory,
    NWB::Core::GraphicsVector<NWB::Core::ShaderArchive::Record>& outRecords
){
    NWB::Core::Filesystem::VolumeSession volumeSession(testArena.arena);
    const bool loadedVolume = volumeSession.load("graphics", outputDirectory);
    EXPECT_TRUE(loadedVolume);
    if(!loadedVolume)
        return false;

    NWB::Core::GraphicsBytes indexBinary(testArena.arena);
    const bool loadedIndex = volumeSession.loadData(NWB::Core::ShaderArchive::IndexVirtualPathName(), indexBinary);
    EXPECT_TRUE(loadedIndex);
    EXPECT_FALSE(indexBinary.empty());
    if(!loadedIndex || indexBinary.empty())
        return false;

    const bool deserialized = NWB::Core::ShaderArchive::deserializeIndex(indexBinary, outRecords);
    EXPECT_TRUE(deserialized);
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

#include "shader_tests.inl"
#include "meshlet_ref_codec_tests.inl"
#include "codec_tests.inl"

#include "caustic_refract_tests.inl"

#include "material_tests.inl"
#include "material_cook_tests.inl"

#include "volume_extensibility_tests.inl"

#include "model_fixture_tests.inl"

#include "mesh_cooker_tests.inl"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

