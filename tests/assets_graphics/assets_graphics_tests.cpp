// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_geometry/skinned_geometry_asset.h>
#include <impl/assets_geometry/geometry_asset.h>
#include <impl/assets_graphics/graphics_asset_cooker.h>
#include <impl/assets_material/material_asset_cook.h>
#include <impl/assets_material/material_binary_payload.h>

#include <tests/capturing_logger.h>
#include <tests/test_context.h>

#include <core/alloc/scratch.h>
#include <core/common/common.h>
#include <core/filesystem/filesystem.h>
#include <core/graphics/common.h>
#include <core/graphics/shader_archive.h>

#include <global/binary.h>
#include <global/compile.h>
#include <global/hash_utils.h>

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

#define NWB_ASSETS_GRAPHICS_TEST_INDEX_TYPE_U16 R"(asset.index_type = "u16";

)"

#define NWB_ASSETS_GRAPHICS_TEST_INDEX_TYPE_U32 R"(asset.index_type = "u32";

)"

#define NWB_ASSETS_GRAPHICS_TEST_STATIC_CLASS R"(asset.geometry_class = "static";

)"

#define NWB_ASSETS_GRAPHICS_TEST_SKINNED_GEOMETRY_CLASS R"(asset.geometry_class = "skinned";

)"

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

#define NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_INDICES R"(asset.indices = [
    [0, 1, 2],
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

)"

#define NWB_ASSETS_GRAPHICS_TEST_SKINNED_GEOMETRY_TRIANGLE_STREAMS(indexType) \
    indexType \
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_POSITIONS \
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_NORMALS \
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_TANGENTS \
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_UV0 \
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_INDICES

#define NWB_ASSETS_GRAPHICS_TEST_SKINNED_GEOMETRY_TRIANGLE_PREFIX(geometryClass, indexType) \
    "geometry asset;\n\n" \
    geometryClass \
    NWB_ASSETS_GRAPHICS_TEST_SKINNED_GEOMETRY_TRIANGLE_STREAMS(indexType)

#define NWB_ASSETS_GRAPHICS_TEST_CLASSLESS_SKINNED_GEOMETRY_TRIANGLE_PREFIX(indexType) \
    "geometry asset;\n\n" \
    NWB_ASSETS_GRAPHICS_TEST_SKINNED_GEOMETRY_TRIANGLE_STREAMS(indexType)

#define NWB_ASSETS_GRAPHICS_TEST_SKINNED_GEOMETRY_TRIANGLE_U16_PREFIX \
    NWB_ASSETS_GRAPHICS_TEST_SKINNED_GEOMETRY_TRIANGLE_PREFIX( \
        NWB_ASSETS_GRAPHICS_TEST_SKINNED_GEOMETRY_CLASS, \
        NWB_ASSETS_GRAPHICS_TEST_INDEX_TYPE_U16 \
    )

#define NWB_ASSETS_GRAPHICS_TEST_SKINNED_GEOMETRY_TRIANGLE_U32_PREFIX \
    NWB_ASSETS_GRAPHICS_TEST_SKINNED_GEOMETRY_TRIANGLE_PREFIX( \
        NWB_ASSETS_GRAPHICS_TEST_SKINNED_GEOMETRY_CLASS, \
        NWB_ASSETS_GRAPHICS_TEST_INDEX_TYPE_U32 \
    )

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


static constexpr AStringView s_MinimalGeometryMeta =
    "geometry asset;\n\n"
    NWB_ASSETS_GRAPHICS_TEST_STATIC_CLASS
    NWB_ASSETS_GRAPHICS_TEST_INDEX_TYPE_U16
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_POSITIONS
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_NORMALS
    R"(asset.colors = [
    [1.0, 0.0, 0.0, 1.0],
    [0.0, 1.0, 0.0, 1.0],
    [0.0, 0.0, 1.0, 1.0],
];

)" NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_INDICES;

static constexpr AStringView s_DefaultColorGeometryMeta =
    "geometry asset;\n\n"
    NWB_ASSETS_GRAPHICS_TEST_STATIC_CLASS
    NWB_ASSETS_GRAPHICS_TEST_INDEX_TYPE_U16
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_POSITIONS
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_NORMALS
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

static constexpr AStringView s_MaterialBindShaderProbeSource = R"NWB_SLANG(#include "mesh_shader_authoring.slangi"
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

#if defined(NWB_FINAL)
static constexpr AStringView s_OtherMaterialBindShaderProbeSource = R"NWB_SLANG(#include "mesh_shader_authoring.slangi"
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
static constexpr AStringView s_UnboundMaterialShaderProbeSource = R"NWB_SLANG(#include "mesh_shader_authoring.slangi"

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
#endif

#if defined(NWB_FINAL)
static constexpr AStringView s_UnknownBlockClassMaterialBindSource = R"NWB_BIND([material_project]
struct NwbTestSurfaceMaterial{
    float base_color;
};

NwbTestSurfaceMaterial surface;

)NWB_BIND";

static constexpr AStringView s_UnsupportedFieldTypeMaterialBindSource = R"NWB_BIND([material_constant]
struct NwbTestSurfaceMaterial{
    half roughness;
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
static constexpr AStringView s_MissingGeometryClassMeta =
    "geometry asset;\n\n"
    NWB_ASSETS_GRAPHICS_TEST_INDEX_TYPE_U16
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_POSITIONS
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_NORMALS
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_INDICES;

static constexpr AStringView s_UnsupportedGeometryFieldsMeta = R"(geometry asset;

asset.geometry_class = "static";

asset.vertex_stride = 24;
asset.index_type = "u16";

asset.vertex_data = [
    [-0.5, -0.5, 0.0, 1.0, 0.0, 0.0],
    [ 0.5, -0.5, 0.0, 0.0, 1.0, 0.0],
    [ 0.0,  0.5, 0.0, 0.0, 0.0, 1.0],
];

asset.index_data = [
    [0, 1, 2],
];
)";

static constexpr AStringView s_MismatchedGeometryMeta =
    "geometry asset;\n\n"
    NWB_ASSETS_GRAPHICS_TEST_STATIC_CLASS
    NWB_ASSETS_GRAPHICS_TEST_INDEX_TYPE_U16
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_POSITIONS
    R"(asset.normals = [
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
];

)" NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_INDICES;
#endif


static constexpr AStringView s_MinimalSkinnedGeometryMeta =
    NWB_ASSETS_GRAPHICS_TEST_SKINNED_GEOMETRY_TRIANGLE_U16_PREFIX
    NWB_ASSETS_GRAPHICS_TEST_ROOT_SKIN;

static constexpr AStringView s_GeneratedFrameSkinnedGeometryMeta =
    "geometry asset;\n\n"
    NWB_ASSETS_GRAPHICS_TEST_SKINNED_GEOMETRY_CLASS
    NWB_ASSETS_GRAPHICS_TEST_INDEX_TYPE_U16
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_POSITIONS
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_UV0
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_INDICES
    NWB_ASSETS_GRAPHICS_TEST_ROOT_SKIN;

static constexpr AStringView s_U32IndexTypeSkinnedGeometryMeta =
    NWB_ASSETS_GRAPHICS_TEST_SKINNED_GEOMETRY_TRIANGLE_U32_PREFIX
    NWB_ASSETS_GRAPHICS_TEST_ROOT_SKIN;

static constexpr AStringView s_EmptyListOptionalSkinnedGeometryMeta =
    NWB_ASSETS_GRAPHICS_TEST_SKINNED_GEOMETRY_TRIANGLE_U16_PREFIX
    R"(asset.colors = [];
)" NWB_ASSETS_GRAPHICS_TEST_ROOT_SKIN;

static constexpr AStringView s_EmptyMapOptionalSkinnedGeometryMeta =
    NWB_ASSETS_GRAPHICS_TEST_SKINNED_GEOMETRY_TRIANGLE_U16_PREFIX
    R"(asset.colors = {};
)" NWB_ASSETS_GRAPHICS_TEST_ROOT_SKIN;

static constexpr AStringView s_NativeCharacterMockSkinnedGeometryMeta = R"(geometry asset;

asset.geometry_class = "skinned";

asset.index_type = "u16";

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

static constexpr AStringView s_NonnormalizedSkinSkinnedGeometryMeta =
    NWB_ASSETS_GRAPHICS_TEST_SKINNED_GEOMETRY_TRIANGLE_U16_PREFIX
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
)";

static constexpr AStringView s_SkinnedOnlySkinnedGeometryMeta =
    NWB_ASSETS_GRAPHICS_TEST_SKINNED_GEOMETRY_TRIANGLE_U16_PREFIX
    NWB_ASSETS_GRAPHICS_TEST_ROOT_SKIN;

#if defined(NWB_FINAL)
static constexpr AStringView s_MissingGeometryClassSkinnedGeometryMeta =
    NWB_ASSETS_GRAPHICS_TEST_CLASSLESS_SKINNED_GEOMETRY_TRIANGLE_PREFIX(NWB_ASSETS_GRAPHICS_TEST_INDEX_TYPE_U16)
    NWB_ASSETS_GRAPHICS_TEST_ROOT_SKIN;

static constexpr AStringView s_StaticClassSkinnedGeometryMeta =
    NWB_ASSETS_GRAPHICS_TEST_SKINNED_GEOMETRY_TRIANGLE_PREFIX(
        NWB_ASSETS_GRAPHICS_TEST_STATIC_CLASS,
        NWB_ASSETS_GRAPHICS_TEST_INDEX_TYPE_U16
    )
    NWB_ASSETS_GRAPHICS_TEST_ROOT_SKIN;

static constexpr AStringView s_MismatchedSkinnedGeometryMeta =
    "geometry asset;\n\n"
    NWB_ASSETS_GRAPHICS_TEST_SKINNED_GEOMETRY_CLASS
    NWB_ASSETS_GRAPHICS_TEST_INDEX_TYPE_U16
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_POSITIONS
    R"(asset.normals = [
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
];

)" NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_TANGENTS
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_UV0
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_INDICES;

static constexpr AStringView s_MissingIndexTypeSkinnedGeometryMeta =
    "geometry asset;\n\n"
    NWB_ASSETS_GRAPHICS_TEST_SKINNED_GEOMETRY_CLASS
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_POSITIONS
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_NORMALS
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_TANGENTS
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_UV0
    NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_INDICES;

static constexpr AStringView s_MismatchedSkinSkinnedGeometryMeta =
    NWB_ASSETS_GRAPHICS_TEST_SKINNED_GEOMETRY_TRIANGLE_U16_PREFIX
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

static constexpr AStringView s_SourceImportSkinnedGeometryMeta = R"(geometry asset;

asset.geometry_class = "skinned";

asset.source = {
    "format": "external",
    "path": "mesh.bin",
};
)";
#endif



#undef NWB_ASSETS_GRAPHICS_TEST_QUAD_TANGENTS
#undef NWB_ASSETS_GRAPHICS_TEST_QUAD_NORMALS
#undef NWB_ASSETS_GRAPHICS_TEST_ROOT_SKIN
#undef NWB_ASSETS_GRAPHICS_TEST_SKINNED_GEOMETRY_TRIANGLE_U16_PREFIX
#undef NWB_ASSETS_GRAPHICS_TEST_SKINNED_GEOMETRY_TRIANGLE_U32_PREFIX
#undef NWB_ASSETS_GRAPHICS_TEST_CLASSLESS_SKINNED_GEOMETRY_TRIANGLE_PREFIX
#undef NWB_ASSETS_GRAPHICS_TEST_SKINNED_GEOMETRY_TRIANGLE_PREFIX
#undef NWB_ASSETS_GRAPHICS_TEST_SKINNED_GEOMETRY_TRIANGLE_STREAMS
#undef NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_INDICES
#undef NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_UV0
#undef NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_TANGENTS
#undef NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_NORMALS
#undef NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_POSITIONS
#undef NWB_ASSETS_GRAPHICS_TEST_SKINNED_GEOMETRY_CLASS
#undef NWB_ASSETS_GRAPHICS_TEST_STATIC_CLASS
#undef NWB_ASSETS_GRAPHICS_TEST_INDEX_TYPE_U32
#undef NWB_ASSETS_GRAPHICS_TEST_INDEX_TYPE_U16


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
    return Path("__build_obj") / "nwb_assets_graphics_tests" / AssetsGraphicsTestConfigurationName() / AString(caseName);
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
    options.repoRoot = ".";
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

static bool CookSingleSkinnedGeometryMeta(
    const AStringView metaText,
    const AStringView caseName,
    TestArena& testArena,
    Path& outRoot,
    Path& outOutputDirectory
){
    static constexpr MinimalAssetCookInfo s_CookInfo{ "characters", "minimal_skinned_geometry.nwb" };
    return CookSingleMinimalAssetMeta(metaText, caseName, s_CookInfo, testArena, outRoot, outOutputDirectory);
}

static bool CookSingleGeometryMeta(
    const AStringView metaText,
    const AStringView caseName,
    TestArena& testArena,
    Path& outRoot,
    Path& outOutputDirectory
){
    static constexpr MinimalAssetCookInfo s_CookInfo{ "meshes", "minimal_geometry.nwb" };
    return CookSingleMinimalAssetMeta(metaText, caseName, s_CookInfo, testArena, outRoot, outOutputDirectory);
}

static bool CookMinimalGeometryWithMaterialBind(
    const AStringView bindText,
    const AStringView caseName,
    TestArena& testArena,
    Path& outRoot,
    Path& outOutputDirectory
){
    if(!PrepareAssetsGraphicsCookCase(caseName, outRoot, outOutputDirectory))
        return false;

    const Path assetRoot = outRoot / "assets";
    if(!WriteTextFile(assetRoot / "meshes" / "minimal_geometry.nwb", s_MinimalGeometryMeta))
        return false;
    if(!WriteTextFile(assetRoot / "material_interfaces" / "test_surface.bind", bindText))
        return false;

    return CookPreparedGraphicsAssetRoots(testArena, outRoot, outOutputDirectory, { assetRoot });
}

static bool ParseMaterialBindFromText(
    const AStringView bindText,
    const AStringView caseName,
    NWB::Impl::MaterialBindEntry& outEntry,
    Path& outRoot
){
    if(!PrepareAssetsGraphicsCaseRoot(caseName, outRoot))
        return false;

    const Path bindPath = outRoot / "assets" / "material_interfaces" / "test_surface.bind";
    if(!WriteTextFile(bindPath, bindText))
        return false;

    return NWB::Impl::ParseMaterialBindSource(bindPath, outEntry);
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
    if(!WriteTextFile(firstAssetRoot / "meshes" / "minimal_geometry.nwb", s_MinimalGeometryMeta))
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

static bool CookMaterialBindMaterialIntegration(
    const AStringView bindText,
    const AStringView materialText,
    const AStringView caseName,
    TestArena& testArena,
    Path& outRoot,
    Path& outOutputDirectory
){
    if(!PrepareAssetsGraphicsCookCase(caseName, outRoot, outOutputDirectory))
        return false;

    const Path assetRoot = outRoot / "assets";
    if(!WriteMaterialBindMaterialIntegrationAssets(testArena, assetRoot, bindText, materialText))
        return false;

    return CookPreparedGraphicsAssetRoots(testArena, outRoot, outOutputDirectory, { assetRoot });
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

static bool LoadCookedMinimalSkinnedGeometry(
    TestContext& context,
    TestArena& testArena,
    const Path& outputDirectory,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset){
    return LoadCookedAsset<NWB::Impl::SkinnedGeometryAssetCodec>(
        context,
        testArena,
        outputDirectory,
        Name("project/characters/minimal_skinned_geometry"),
        outLoadedAsset
    );
}

static bool LoadCookedMinimalGeometry(
    TestContext& context,
    TestArena& testArena,
    const Path& outputDirectory,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset){
    return LoadCookedAsset<NWB::Impl::GeometryAssetCodec>(
        context,
        testArena,
        outputDirectory,
        Name("project/meshes/minimal_geometry"),
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

using CookSingleMetaFn = bool(*)(AStringView, AStringView, TestArena&, Path&, Path&);
using LoadCookedAssetFn = bool(*)(TestContext&, TestArena&, const Path&, UniquePtr<NWB::Core::Assets::IAsset>&);

namespace MinimalAssetKind{
    enum Enum : u8{
        SkinnedGeometry = 0u,
        Geometry = 1u,
    };
};

static bool CookAndLoadMinimalAsset(
    TestContext& context,
    TestArena& testArena,
    const AStringView metaText,
    const AStringView caseName,
    Path& outRoot,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset,
    CookSingleMetaFn cookSingleMeta,
    LoadCookedAssetFn loadCookedAsset
){
    Path outputDirectory;
    const bool cooked = cookSingleMeta(metaText, caseName, testArena, outRoot, outputDirectory);

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, cooked);
    if(!cooked){
        ErrorCode errorCode;
        static_cast<void>(RemoveAllIfExists(outRoot, errorCode));
        return false;
    }

    if(loadCookedAsset(context, testArena, outputDirectory, outLoadedAsset))
        return true;

    ErrorCode errorCode;
    static_cast<void>(RemoveAllIfExists(outRoot, errorCode));
    return false;
}

static bool CookAndLoadMinimalAssetByKind(
    TestContext& context,
    TestArena& testArena,
    const AStringView metaText,
    const AStringView caseName,
    Path& outRoot,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset,
    const MinimalAssetKind::Enum assetKind
){
    CookSingleMetaFn cookSingleMeta = nullptr;
    LoadCookedAssetFn loadCookedAsset = nullptr;
    switch(assetKind){
    case MinimalAssetKind::SkinnedGeometry:
        cookSingleMeta = CookSingleSkinnedGeometryMeta;
        loadCookedAsset = LoadCookedMinimalSkinnedGeometry;
        break;
    case MinimalAssetKind::Geometry:
        cookSingleMeta = CookSingleGeometryMeta;
        loadCookedAsset = LoadCookedMinimalGeometry;
        break;
    default:
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, false);
        return false;
    }

    return CookAndLoadMinimalAsset(
        context,
        testArena,
        metaText,
        caseName,
        outRoot,
        outLoadedAsset,
        cookSingleMeta,
        loadCookedAsset
    );
}

static void CheckMinimalSkinnedGeometryDefaults(
    TestContext& context,
    const NWB::Core::Assets::IAsset& loadedAsset){
    const NWB::Impl::SkinnedGeometry& loadedGeometry =
        static_cast<const NWB::Impl::SkinnedGeometry&>(loadedAsset)
    ;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.restVertices().size() == 3u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.indices().size() == 3u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.geometryClass() == NWB::Impl::GeometryClass::Skinned);
    const Float4U color0 = LoadHalf4U(loadedGeometry.restVertices()[0].color0);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, color0.x == 1.f);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, color0.w == 1.f);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.skin().size() == 3u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.skeletonJointCount() == 1u);
}

template<typename AssetT, typename CheckLoadedAssetFn>
static void CookAndCheckMinimalTypedAsset(
    TestContext& context,
    const AStringView metaText,
    const AStringView caseName,
    const MinimalAssetKind::Enum assetKind,
    CheckLoadedAssetFn&& checkLoadedAsset
){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root;
    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    if(!CookAndLoadMinimalAssetByKind(
        context,
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
    static_cast<void>(RemoveAllIfExists(root, errorCode));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);
}

static void TestVolumeSessionAcceptsScratchBytes(TestContext& context){
    TestArena testArena;
    const Path root = AssetsGraphicsTestCaseRoot("volume_scratch_bytes");
    const bool prepared = PrepareCleanDirectory(root);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, prepared);

    if(prepared){
        NWB::Core::Filesystem::VolumeBuildConfig config;
        config.volumeName = "scratch_test";
        config.segmentSize = 64ull * 1024ull;
        config.metadataSize = 4ull * 1024ull;

        NWB::Core::Filesystem::VolumeSession volumeSession(testArena.arena);
        const bool created = volumeSession.create(root / "volume", config);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, created);
        if(created){
            NWB::Core::Alloc::ScratchArena<> scratchArena;
            ::Vector<u8, NWB::Core::Alloc::ScratchArena<>> payload{ scratchArena };
            payload.reserve(4u);
            payload.push_back(1u);
            payload.push_back(2u);
            payload.push_back(3u);
            payload.push_back(4u);

            const Name virtualPath("project/tests/scratch_payload");
            const bool pushed = volumeSession.pushDataDeferred(virtualPath, payload);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, pushed);
            if(pushed){
                payload[0] = 99u;

                const bool flushed = volumeSession.flush();
                NWB_ASSETS_GRAPHICS_TEST_CHECK(context, flushed);
                if(flushed){
                    NWB::Core::Assets::AssetBytes readback = MakeAssetBytes(testArena);
                    const bool loaded = volumeSession.loadData(virtualPath, readback);
                    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loaded);
                    if(loaded){
                        NWB_ASSETS_GRAPHICS_TEST_CHECK(
                            context,
                            readback.size() == 4u
                                && readback[0] == 1u
                                && readback[1] == 2u
                                && readback[2] == 3u
                                && readback[3] == 4u
                        );
                    }
                }
            }
        }
    }

    ErrorCode errorCode;
    static_cast<void>(RemoveAllIfExists(root, errorCode));
}

static NWB::Impl::SkinnedGeometryVertex MakeRestVertex(const f32 x, const f32 y, const f32 u, const f32 v){
    return NWB::Impl::MakeSkinnedGeometryVertex(
        Float3U(x, y, 0.f),
        Float3U(0.f, 0.f, 1.f),
        Float4U(1.f, 0.f, 0.f, 1.f),
        Float2U(u, v),
        Float4U(1.f, 1.f, 1.f, 1.f)
    );
}

static NWB::Impl::SkinInfluence4 MakeRootSkin(){
    NWB::Impl::SkinInfluence4 skin;
    skin.joint[0] = 0u;
    skin.weight[0] = 1.f;
    return skin;
}

static NWB::Impl::SkinnedGeometryJointMatrix MakeJointMatrix(const f32 tx, const f32 ty, const f32 tz){
    NWB::Impl::SkinnedGeometryJointMatrix matrix = NWB::Impl::MakeIdentitySkinnedGeometryJointMatrix();
    matrix.rows[3] = Float4(tx, ty, tz, 1.0f);
    return matrix;
}

#if defined(NWB_FINAL)
template<typename T>
static bool OverwritePOD(NWB::Core::Assets::AssetBytes& binary, const usize offset, const T value){
    if(offset > binary.size() || sizeof(value) > binary.size() - offset)
        return false;

    NWB_MEMCPY(binary.data() + offset, sizeof(value), &value, sizeof(value));
    return true;
}

static bool OverwriteU32(NWB::Core::Assets::AssetBytes& binary, const usize offset, const u32 value){
    return OverwritePOD(binary, offset, value);
}

static bool OverwriteU64(NWB::Core::Assets::AssetBytes& binary, const usize offset, const u64 value){
    return OverwritePOD(binary, offset, value);
}

static usize SkinnedGeometryHeaderCountOffset(const usize countIndex){
    return (sizeof(u32) * 3u) + (sizeof(u64) * countIndex);
}

static bool FindMaterialBinaryTypedLayoutOffsets(
    const NWB::Core::Assets::AssetBytes& binary,
    usize& outLayoutHashOffset,
    usize& outBlockByteCountOffset
){
    outLayoutHashOffset = 0u;
    outBlockByteCountOffset = 0u;

    usize cursor = 0u;
    u32 value32 = 0u;
    if(!ReadPOD(binary, cursor, value32) || !ReadPOD(binary, cursor, value32))
        return false;

    u32 shaderVariantByteCount = 0u;
    if(!ReadPOD(binary, cursor, shaderVariantByteCount))
        return false;
    if(cursor > binary.size() || shaderVariantByteCount > binary.size() - cursor)
        return false;
    cursor += shaderVariantByteCount;

    if(cursor > binary.size() || sizeof(NameHash) > binary.size() - cursor)
        return false;
    cursor += sizeof(NameHash);

    outLayoutHashOffset = cursor;

    u64 layoutHash = 0u;
    u32 blockCount = 0u;
    u32 fieldCount = 0u;
    if(
        !ReadPOD(binary, cursor, layoutHash)
        || !ReadPOD(binary, cursor, blockCount)
        || !ReadPOD(binary, cursor, fieldCount)
    )
        return false;

    if(
        cursor > binary.size()
        || blockCount > (binary.size() - cursor) / NWB::Impl::MaterialBinaryPayload::s_TypedLayoutBlockBytes
    )
        return false;
    cursor += static_cast<usize>(blockCount) * NWB::Impl::MaterialBinaryPayload::s_TypedLayoutBlockBytes;

    if(
        cursor > binary.size()
        || fieldCount > (binary.size() - cursor) / NWB::Impl::MaterialBinaryPayload::s_TypedLayoutFieldBytes
    )
        return false;
    cursor += static_cast<usize>(fieldCount) * NWB::Impl::MaterialBinaryPayload::s_TypedLayoutFieldBytes;

    outBlockByteCountOffset = cursor;
    return true;
}

#endif

static NWB::Impl::SkinnedGeometry BuildValidSkinnedGeometry(TestArena& testArena){
    NWB::Impl::SkinnedGeometry geometry(testArena.arena, Name("tests/characters/proxy_skinned_geometry"));

    auto vertices = MakeAssetVector<NWB::Impl::SkinnedGeometryVertex>(testArena);
    vertices.push_back(MakeRestVertex(-0.5f, -0.5f, 0.f, 0.f));
    vertices.push_back(MakeRestVertex(0.5f, -0.5f, 1.f, 0.f));
    vertices.push_back(MakeRestVertex(0.5f, 0.5f, 1.f, 1.f));
    vertices.push_back(MakeRestVertex(-0.5f, 0.5f, 0.f, 1.f));

    auto indices = MakeAssetVectorFrom(testArena, MakeQuadTriangleIndices());

    auto skin = MakeAssetVector<NWB::Impl::SkinInfluence4>(testArena);
    skin.assign(vertices.size(), MakeRootSkin());

    auto inverseBindMatrices = MakeAssetVector<NWB::Impl::SkinnedGeometryJointMatrix>(testArena);
    inverseBindMatrices.push_back(MakeJointMatrix(-0.25f, 0.0f, 0.0f));

    geometry.setGeometryClass(NWB::Impl::GeometryClass::Skinned);
    geometry.setRestVertices(Move(vertices));
    geometry.setIndices(Move(indices));
    geometry.setSkin(Move(skin));
    geometry.setSkeletonJointCount(1u);
    geometry.setInverseBindMatrices(Move(inverseBindMatrices));
    return geometry;
}

static NWB::Impl::SkinnedGeometry BuildMinimalSkinnedGeometry(TestArena& testArena){
    NWB::Impl::SkinnedGeometry geometry(testArena.arena, Name("tests/characters/minimal_skinned_geometry"));

    auto vertices = MakeAssetVector<NWB::Impl::SkinnedGeometryVertex>(testArena);
    vertices.push_back(MakeRestVertex(-0.5f, -0.5f, 0.f, 0.f));
    vertices.push_back(MakeRestVertex(0.5f, -0.5f, 1.f, 0.f));
    vertices.push_back(MakeRestVertex(0.f, 0.5f, 0.5f, 1.f));

    auto indices = MakeAssetVectorFrom(testArena, MakeTriangleIndices());

    auto skin = MakeAssetVector<NWB::Impl::SkinInfluence4>(testArena);
    skin.assign(vertices.size(), MakeRootSkin());

    geometry.setGeometryClass(NWB::Impl::GeometryClass::Skinned);
    geometry.setRestVertices(Move(vertices));
    geometry.setIndices(Move(indices));
    geometry.setSkin(Move(skin));
    geometry.setSkeletonJointCount(1u);
    return geometry;
}

static NWB::Impl::Geometry BuildMinimalGeometry(TestArena& testArena){
    NWB::Impl::Geometry geometry(testArena.arena, Name("tests/meshes/minimal_geometry"));

    auto positions = MakeAssetVector<Float3U>(testArena);
    positions.push_back(Float3U(-0.5f, -0.5f, 0.f));
    positions.push_back(Float3U(0.5f, -0.5f, 0.f));
    positions.push_back(Float3U(0.f, 0.5f, 0.f));

    auto normals = MakeAssetVector<Half4U>(testArena);
    normals.assign(positions.size(), NWB::Impl::MakeGeometryNormalStreamValue(Float3U(0.f, 0.f, 1.f)));

    auto colors = MakeAssetVector<Half4U>(testArena);
    colors.push_back(NWB::Impl::MakeGeometryColorStreamValue(Float4U(1.f, 0.f, 0.f, 1.f)));
    colors.push_back(NWB::Impl::MakeGeometryColorStreamValue(Float4U(0.f, 1.f, 0.f, 1.f)));
    colors.push_back(NWB::Impl::MakeGeometryColorStreamValue(Float4U(0.f, 0.f, 1.f, 1.f)));

    auto indices = MakeAssetVectorFrom(testArena, MakeTriangleIndices());

    geometry.setStreams(Move(positions), Move(normals), Move(colors));
    geometry.setIndices(Move(indices));
    return geometry;
}

template<typename AssetT, typename CodecT>
static const AssetT& CheckCodecRoundTrip(
    TestContext& context,
    TestArena& testArena,
    const AssetT& asset,
    const CodecT& codec,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset){
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, asset.validatePayload());

    NWB::Core::Assets::AssetBytes binary = MakeAssetBytes(testArena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, codec.serialize(asset, binary));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !binary.empty());

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, codec.deserialize(testArena.arena, asset.virtualPath(), binary, outLoadedAsset));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, static_cast<bool>(outLoadedAsset));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, outLoadedAsset->assetType() == AssetT::AssetTypeName());
    return static_cast<const AssetT&>(*outLoadedAsset);
}

template<typename CodecT>
static void CheckCodecRejectsBinary(
    TestContext& context,
    TestArena& testArena,
    const CodecT& codec,
    const Name& virtualPath,
    const NWB::Core::Assets::AssetBytes& binary){
    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !codec.deserialize(testArena.arena, virtualPath, binary, loadedAsset));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !loadedAsset);
}

static void CheckSkinnedSkinnedGeometryPayload(
    TestContext& context,
    const NWB::Impl::SkinnedGeometry& loadedGeometry,
    const u32 expectedSkeletonJointCount,
    const u32 expectedInverseBindMatrixCount,
    const u32 expectedInverseBindMatrixIndex){
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.geometryClass() == NWB::Impl::GeometryClass::Skinned);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.restVertices().size() == 4u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.indices().size() == 6u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.skin().size() == 4u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.skeletonJointCount() == expectedSkeletonJointCount);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.inverseBindMatrices().size() == expectedInverseBindMatrixCount);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.inverseBindMatrices()[expectedInverseBindMatrixIndex].rows[3].x == -0.25f);
}

template<typename CodecT, typename BuildAssetFnT>
static void CheckCodecRejectsUnsupportedBinaryVersion(
    TestContext& context,
    BuildAssetFnT buildAsset,
    const u32 unsupportedVersion
){
#if defined(NWB_FINAL)
    TestArena testArena;
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    auto asset = buildAsset(testArena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, asset.validatePayload());

    CodecT codec;
    NWB::Core::Assets::AssetBytes binary = MakeAssetBytes(testArena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, codec.serialize(asset, binary));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, OverwriteU32(binary, sizeof(u32), unsupportedVersion));

    CheckCodecRejectsBinary(context, testArena, codec, asset.virtualPath(), binary);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 1u);
    const auto expectedError = StringFormat(logger.arena(), NWB_TEXT("unsupported version {}"), unsupportedVersion);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(expectedError.c_str()));
#else
    static_cast<void>(context);
    static_cast<void>(buildAsset);
    static_cast<void>(unsupportedVersion);
#endif
}

static void TestGeometryCodecRoundTrip(TestContext& context){
    TestArena testArena;
    NWB::Impl::Geometry geometry = BuildMinimalGeometry(testArena);

    NWB::Impl::GeometryAssetCodec codec;
    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    const NWB::Impl::Geometry& loadedGeometry = CheckCodecRoundTrip(context, testArena, geometry, codec, loadedAsset);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.vertexCount() == 3u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.indices().size() == 3u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.positions()[1].x == 0.5f);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, LoadHalf4U(loadedGeometry.normals()[1]).z == 1.f);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, LoadHalf4U(loadedGeometry.colors()[1]).y == 1.f);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.indices()[2] == 2u);
}

static void TestGeometryCodecRejectsUnsupportedBinaryVersion(TestContext& context){
    CheckCodecRejectsUnsupportedBinaryVersion<NWB::Impl::GeometryAssetCodec>(
        context,
        BuildMinimalGeometry,
        0u
    );
}

static void TestSkinnedGeometryCodecRoundTrip(TestContext& context){
    TestArena testArena;
    NWB::Impl::SkinnedGeometry geometry = BuildValidSkinnedGeometry(testArena);

    NWB::Impl::SkinnedGeometryAssetCodec codec;
    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    const NWB::Impl::SkinnedGeometry& loadedGeometry = CheckCodecRoundTrip(context, testArena, geometry, codec, loadedAsset);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.virtualPath() == geometry.virtualPath());
    CheckSkinnedSkinnedGeometryPayload(context, loadedGeometry, 1u, 1u, 0u);
}

static void TestMinimalSkinnedGeometryCodecRoundTrip(TestContext& context){
    TestArena testArena;
    NWB::Impl::SkinnedGeometry geometry = BuildMinimalSkinnedGeometry(testArena);

    NWB::Impl::SkinnedGeometryAssetCodec codec;
    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    const NWB::Impl::SkinnedGeometry& loadedGeometry = CheckCodecRoundTrip(context, testArena, geometry, codec, loadedAsset);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.restVertices().size() == 3u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.indices().size() == 3u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.geometryClass() == NWB::Impl::GeometryClass::Skinned);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.skin().size() == 3u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.skeletonJointCount() == 1u);
}

static void TestSkinnedGeometryCodecRejectsUnsupportedBinaryVersion(TestContext& context){
    CheckCodecRejectsUnsupportedBinaryVersion<NWB::Impl::SkinnedGeometryAssetCodec>(
        context,
        BuildMinimalSkinnedGeometry,
        0u
    );
}

static void TestSkinnedGeometryCodecRejectsMalformedCounts(TestContext& context){
#if defined(NWB_FINAL)
    TestArena testArena;
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    NWB::Impl::SkinnedGeometry geometry = BuildMinimalSkinnedGeometry(testArena);
    NWB::Impl::SkinnedGeometryAssetCodec codec;
    NWB::Core::Assets::AssetBytes binary = MakeAssetBytes(testArena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, codec.serialize(geometry, binary));

    const usize skeletonJointCountOffset = SkinnedGeometryHeaderCountOffset(3u);
    const u64 invalidJointCount = static_cast<u64>(Limit<u32>::s_Max) + 1ull;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, OverwriteU64(binary, skeletonJointCountOffset, invalidJointCount));

    CheckCodecRejectsBinary(context, testArena, codec, geometry.virtualPath(), binary);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 1u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("payload counts exceed u32 limits")));
#else
    static_cast<void>(context);
#endif
}

static void TestSkinnedGeometryCodecRejectsMalformedDependentCounts(TestContext& context){
#if defined(NWB_FINAL)
    TestArena testArena;
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    NWB::Impl::SkinnedGeometry geometry = BuildValidSkinnedGeometry(testArena);
    NWB::Impl::SkinnedGeometryAssetCodec codec;
    NWB::Core::Assets::AssetBytes binary = MakeAssetBytes(testArena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, codec.serialize(geometry, binary));

    {
        NWB::Core::Assets::AssetBytes malformed = binary;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, OverwriteU64(
            malformed,
            SkinnedGeometryHeaderCountOffset(2u),
            static_cast<u64>(geometry.restVertices().size() - 1u)
        ));

        CheckCodecRejectsBinary(context, testArena, codec, geometry.virtualPath(), malformed);
    }

    {
        NWB::Core::Assets::AssetBytes malformed = binary;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, OverwriteU64(
            malformed,
            SkinnedGeometryHeaderCountOffset(4u),
            static_cast<u64>(geometry.restVertices().size() - 1u)
        ));

        CheckCodecRejectsBinary(context, testArena, codec, geometry.virtualPath(), malformed);
    }

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 2u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("skin count must be empty or match vertex count")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("inverse bind matrix count must be empty or match skeleton joint count")));
#else
    static_cast<void>(context);
#endif
}

static bool ParseMaterialEntryFromMetaText(
    const AStringView metaText,
    TestArena& testArena,
    NWB::Impl::MaterialCookEntry& outEntry
){
    NWB::Core::Metascript::Document doc(testArena.arena);
    if(!doc.parse(metaText))
        return false;

    NWB::Impl::ShaderCook shaderCook(testArena.arena);
    const Path assetRoot = AssetsGraphicsTestCaseRoot("material_meta") / "assets";
    const Path nwbFilePath = assetRoot / "materials" / "test_material.nwb";
    return NWB::Impl::ParseMaterialCookMetadata(shaderCook, assetRoot, "project", nwbFilePath, doc, outEntry);
}

static bool ContainsText(const AStringView text, const AStringView expected){
    return text.find(expected) != AStringView::npos;
}

static void AppendHexU32SlangText(const u32 value, AString& inOutText){
    static constexpr char s_HexDigits[] = "0123456789abcdef";
    inOutText += "0x";
    for(u32 nibbleIndex = 0u; nibbleIndex < 8u; ++nibbleIndex){
        const u32 shift = (7u - nibbleIndex) * 4u;
        inOutText += s_HexDigits[(value >> shift) & 0xfu];
    }
    inOutText += 'u';
}

static void AppendU32DecimalText(const u32 value, AString& inOutText){
    char digits[10u];
    u32 digitCount = 0u;
    u32 remaining = value;
    do{
        digits[digitCount] = static_cast<char>('0' + (remaining % 10u));
        remaining /= 10u;
        ++digitCount;
    } while(remaining != 0u);

    while(digitCount > 0u){
        --digitCount;
        inOutText += digits[digitCount];
    }
}

static AString BuildGeneratedUint2ConstantText(const AStringView symbol, const u64 value){
    AString text("static const uint2 ");
    text.append(symbol.data(), symbol.size());
    text += " = uint2(";
    AppendHexU32SlangText(static_cast<u32>(value & 0xffffffffull), text);
    text += ", ";
    AppendHexU32SlangText(static_cast<u32>(value >> 32u), text);
    text += ");";
    return text;
}

static AString BuildGeneratedUintConstantText(const AStringView symbol, const u32 value){
    AString text("static const uint ");
    text.append(symbol.data(), symbol.size());
    text += " = ";
    AppendU32DecimalText(value, text);
    text += "u;";
    return text;
}

static bool ContainsGeneratedUint2Constant(const AStringView generatedSourceView, const AStringView symbol, const u64 value){
    const AString expected = BuildGeneratedUint2ConstantText(symbol, value);
    return ContainsText(generatedSourceView, AStringView(expected.data(), expected.size()));
}

static bool ContainsGeneratedUintConstant(const AStringView generatedSourceView, const AStringView symbol, const u32 value){
    const AString expected = BuildGeneratedUintConstantText(symbol, value);
    return ContainsText(generatedSourceView, AStringView(expected.data(), expected.size()));
}

static bool ContainsCanonicalPath(const NWB::Impl::ShaderCook::CookVector<Path>& paths, const Path& expectedPath){
    ErrorCode errorCode;
    const Path expectedAbsolutePath = AbsolutePath(expectedPath, errorCode).lexically_normal();
    if(errorCode)
        return false;

    for(const Path& path : paths){
        errorCode.clear();
        const Path absolutePath = AbsolutePath(path, errorCode).lexically_normal();
        if(!errorCode && absolutePath == expectedAbsolutePath)
            return true;
    }

    return false;
}

static void CheckGeneratedMaterialBindSource(TestContext& context, const AStringView generatedSourceView){
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsText(
        generatedSourceView,
        "#ifndef NWB_GENERATED_MATERIAL_BIND_PROJECT_MATERIAL_INTERFACES_TEST_SURFACE_BIND"
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsText(
        generatedSourceView,
        "static const uint2 NWB_MATERIAL_BIND_INTERFACE_HASH_0 = uint2("
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsText(
        generatedSourceView,
        "static const uint2 NWB_MATERIAL_BIND_LAYOUT_HASH = uint2("
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsText(
        generatedSourceView,
        "#if NWB_MATERIAL_TYPED_BINDING != 1"
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsText(
        generatedSourceView,
        "static const uint NWB_MATERIAL_BIND_BLOCK_COUNT = 2u;"
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsText(
        generatedSourceView,
        "static const uint NWB_MATERIAL_BIND_FIELD_COUNT = 6u;"
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsText(
        generatedSourceView,
        "static const uint NWB_MATERIAL_BIND_BLOCK_BYTE_SIZE = 60u;"
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsText(
        generatedSourceView,
        "static const uint NWB_MATERIAL_BIND_RUNTIME_BLOCK_BYTE_OFFSET = 0u;"
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsText(
        generatedSourceView,
        "static const uint NWB_MATERIAL_BIND_RUNTIME_BLOCK_BYTE_SIZE = 4u;"
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsText(
        generatedSourceView,
        "static const uint NWB_MATERIAL_BIND_SURFACE_BLOCK_BYTE_OFFSET = 4u;"
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsText(
        generatedSourceView,
        "static const uint NWB_MATERIAL_BIND_SURFACE_BLOCK_BYTE_SIZE = 56u;"
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsText(generatedSourceView, "struct NwbTestSurfaceMaterial"));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsText(
        generatedSourceView,
        "static const uint2 NWB_MATERIAL_BIND_SURFACE_BASE_COLOR_KEY = uint2("
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsText(
        generatedSourceView,
        "static const float4 NWB_MATERIAL_BIND_SURFACE_BASE_COLOR_DEFAULT = float4(1.0, 1.0, 1.0, 1.0);"
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsText(
        generatedSourceView,
        "static const uint NWB_MATERIAL_BIND_SURFACE_BASE_COLOR_BYTE_OFFSET = 4u;"
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsText(
        generatedSourceView,
        "static const uint NWB_MATERIAL_BIND_RUNTIME_FADE_ALPHA_BYTE_OFFSET = 0u;"
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsText(
        generatedSourceView,
        "nwbMaterialLoadFloat4(instance, NWB_MATERIAL_BIND_SURFACE_BASE_COLOR_BYTE_OFFSET)"
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsText(
        generatedSourceView,
        "nwbMaterialLoadInt2(instance, NWB_MATERIAL_BIND_SURFACE_LAYER_IDS_BYTE_OFFSET)"
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsText(
        generatedSourceView,
        "nwbMaterialLoadUInt3(instance, NWB_MATERIAL_BIND_SURFACE_FEATURE_MASK_BYTE_OFFSET)"
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsText(
        generatedSourceView,
        "nwbMaterialLoadBool4(instance, NWB_MATERIAL_BIND_SURFACE_CHANNEL_ENABLED_BYTE_OFFSET)"
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !ContainsText(
        generatedSourceView,
        "nwbMaterialFind"
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsText(generatedSourceView, "float4 nwbMaterialBindLoadSurfaceBaseColor"));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsText(generatedSourceView, "NwbTestSurfaceMaterial nwbMaterialBindLoadSurface"));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsText(
        generatedSourceView,
        "static const float NWB_MATERIAL_BIND_RUNTIME_FADE_ALPHA_DEFAULT = float(1.0);"
    ));
}

static const NWB::Impl::MaterialTypedLayoutBlock* FindMaterialTypedLayoutBlock(
    const NWB::Impl::Material& material,
    const AStringView blockName
){
    const Name blockNameHash(blockName);
    for(const NWB::Impl::MaterialTypedLayoutBlock& block : material.typedLayoutBlocks()){
        if(block.blockName == blockNameHash)
            return &block;
    }
    return nullptr;
}

static const NWB::Impl::MaterialTypedLayoutField* FindMaterialTypedLayoutField(
    const NWB::Impl::Material& material,
    const NWB::Impl::MaterialTypedLayoutBlock& block,
    const AStringView fieldName
){
    const Name fieldNameHash(fieldName);
    for(u32 fieldOffset = 0u; fieldOffset < block.fieldCount; ++fieldOffset){
        const usize fieldIndex = static_cast<usize>(block.fieldBegin) + fieldOffset;
        if(fieldIndex >= material.typedLayoutFields().size())
            return nullptr;

        const NWB::Impl::MaterialTypedLayoutField& field = material.typedLayoutFields()[fieldIndex];
        if(field.fieldName == fieldNameHash)
            return &field;
    }
    return nullptr;
}

static f32 LoadMaterialTypedLayoutDefaultFloat(const NWB::Impl::MaterialTypedLayoutField& field, const u32 componentIndex){
    f32 value = 0.f;
    NWB_MEMCPY(&value, sizeof(value), &field.defaultValue.raw[componentIndex], sizeof(field.defaultValue.raw[componentIndex]));
    return value;
}

static bool LoadMaterialTypedBlockU32(
    const NWB::Impl::Material& material,
    const AStringView blockName,
    const u32 byteOffset,
    u32& outValue
){
    outValue = 0u;

    const Name blockNameHash(blockName);
    usize blockByteBegin = 0u;
    for(const NWB::Impl::MaterialTypedLayoutBlock& block : material.typedLayoutBlocks()){
        if(block.blockName == blockNameHash){
            const usize valueOffset = blockByteBegin + byteOffset;
            if(
                valueOffset > material.typedBlockBytes().size()
                || sizeof(outValue) > material.typedBlockBytes().size() - valueOffset
            )
                return false;

            NWB_MEMCPY(&outValue, sizeof(outValue), material.typedBlockBytes().data() + valueOffset, sizeof(outValue));
            return true;
        }

        blockByteBegin += block.byteSize;
    }

    return false;
}

static bool LoadMaterialTypedBlockFloat(
    const NWB::Impl::Material& material,
    const AStringView blockName,
    const u32 byteOffset,
    f32& outValue
){
    u32 rawValue = 0u;
    if(!LoadMaterialTypedBlockU32(material, blockName, byteOffset, rawValue))
        return false;

    NWB_MEMCPY(&outValue, sizeof(outValue), &rawValue, sizeof(rawValue));
    return true;
}

static void CheckMinimalMaterialTypedLayout(
    TestContext& context,
    const NWB::Impl::Material& material,
    const u32 expectedFeatureMaskX = 4u,
    const u32 expectedFeatureMaskY = 5u,
    const u32 expectedFeatureMaskZ = 6u
){
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, material.typedLayoutHash() != 0u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, material.typedLayoutBlocks().size() == 2u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, material.typedLayoutFields().size() == 6u);

    const NWB::Impl::MaterialTypedLayoutBlock* runtimeBlock = FindMaterialTypedLayoutBlock(material, "runtime");
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, runtimeBlock != nullptr);
    if(runtimeBlock){
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, runtimeBlock->blockClass == NWB::Impl::MaterialBlockClass::MaterialMutable);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, runtimeBlock->fieldCount == 1u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, runtimeBlock->byteSize == 4u);

        const NWB::Impl::MaterialTypedLayoutField* fadeAlpha = FindMaterialTypedLayoutField(
            material,
            *runtimeBlock,
            "fade_alpha"
        );
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, fadeAlpha != nullptr);
        if(fadeAlpha){
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, fadeAlpha->fieldType == NWB::Impl::MaterialLayoutFieldType::Float);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, fadeAlpha->offset == 0u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, LoadMaterialTypedLayoutDefaultFloat(*fadeAlpha, 0u) == 1.0f);
        }
    }

    const NWB::Impl::MaterialTypedLayoutBlock* surfaceBlock = FindMaterialTypedLayoutBlock(material, "surface");
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, surfaceBlock != nullptr);
    if(surfaceBlock){
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, surfaceBlock->blockClass == NWB::Impl::MaterialBlockClass::MaterialConstant);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, surfaceBlock->fieldCount == 5u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, surfaceBlock->byteSize == 56u);

        const NWB::Impl::MaterialTypedLayoutField* baseColor = FindMaterialTypedLayoutField(
            material,
            *surfaceBlock,
            "base_color"
        );
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, baseColor != nullptr);
        if(baseColor){
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, baseColor->fieldType == NWB::Impl::MaterialLayoutFieldType::Float4);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, baseColor->offset == 0u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, LoadMaterialTypedLayoutDefaultFloat(*baseColor, 0u) == 1.0f);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, LoadMaterialTypedLayoutDefaultFloat(*baseColor, 3u) == 1.0f);
        }

        const NWB::Impl::MaterialTypedLayoutField* roughness = FindMaterialTypedLayoutField(
            material,
            *surfaceBlock,
            "roughness"
        );
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, roughness != nullptr);
        if(roughness){
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, roughness->fieldType == NWB::Impl::MaterialLayoutFieldType::Float);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, roughness->offset == 16u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, LoadMaterialTypedLayoutDefaultFloat(*roughness, 0u) == 0.5f);
        }

        const NWB::Impl::MaterialTypedLayoutField* layerIds = FindMaterialTypedLayoutField(
            material,
            *surfaceBlock,
            "layer_ids"
        );
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, layerIds != nullptr);
        if(layerIds){
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, layerIds->fieldType == NWB::Impl::MaterialLayoutFieldType::Int2);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, layerIds->offset == 20u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, layerIds->defaultValue.x == 1u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, layerIds->defaultValue.y == 2u);
        }

        const NWB::Impl::MaterialTypedLayoutField* featureMask = FindMaterialTypedLayoutField(
            material,
            *surfaceBlock,
            "feature_mask"
        );
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, featureMask != nullptr);
        if(featureMask){
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, featureMask->fieldType == NWB::Impl::MaterialLayoutFieldType::UInt3);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, featureMask->offset == 28u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, featureMask->defaultValue.x == expectedFeatureMaskX);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, featureMask->defaultValue.y == expectedFeatureMaskY);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, featureMask->defaultValue.z == expectedFeatureMaskZ);
        }

        const NWB::Impl::MaterialTypedLayoutField* channelEnabled = FindMaterialTypedLayoutField(
            material,
            *surfaceBlock,
            "channel_enabled"
        );
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, channelEnabled != nullptr);
        if(channelEnabled){
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, channelEnabled->fieldType == NWB::Impl::MaterialLayoutFieldType::Bool4);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, channelEnabled->offset == 40u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, channelEnabled->defaultValue.x == 1u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, channelEnabled->defaultValue.y == 0u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, channelEnabled->defaultValue.z == 1u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, channelEnabled->defaultValue.w == 0u);
        }
    }
}

static void CheckMinimalMaterialTypedBlockBytes(
    TestContext& context,
    const NWB::Impl::Material& material,
    const u32 expectedFeatureMaskX = 4u,
    const u32 expectedFeatureMaskY = 5u,
    const u32 expectedFeatureMaskZ = 6u
){
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, material.typedBlockBytes().size() == 60u);

    f32 floatValue = 0.f;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context,
        LoadMaterialTypedBlockFloat(material, "runtime", 0u, floatValue)
        && floatValue == 0.75f
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context,
        LoadMaterialTypedBlockFloat(material, "surface", 0u, floatValue)
        && floatValue == 0.25f
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context,
        LoadMaterialTypedBlockFloat(material, "surface", 4u, floatValue)
        && floatValue == 0.5f
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context,
        LoadMaterialTypedBlockFloat(material, "surface", 8u, floatValue)
        && floatValue == 0.75f
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context,
        LoadMaterialTypedBlockFloat(material, "surface", 12u, floatValue)
        && floatValue == 1.0f
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context,
        LoadMaterialTypedBlockFloat(material, "surface", 16u, floatValue)
        && floatValue == 0.25f
    );

    u32 rawValue = 0u;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, LoadMaterialTypedBlockU32(material, "surface", 20u, rawValue) && rawValue == 1u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, LoadMaterialTypedBlockU32(material, "surface", 24u, rawValue) && rawValue == 2u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context,
        LoadMaterialTypedBlockU32(material, "surface", 28u, rawValue)
        && rawValue == expectedFeatureMaskX
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context,
        LoadMaterialTypedBlockU32(material, "surface", 32u, rawValue)
        && rawValue == expectedFeatureMaskY
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context,
        LoadMaterialTypedBlockU32(material, "surface", 36u, rawValue)
        && rawValue == expectedFeatureMaskZ
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, LoadMaterialTypedBlockU32(material, "surface", 40u, rawValue) && rawValue == 1u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, LoadMaterialTypedBlockU32(material, "surface", 44u, rawValue) && rawValue == 0u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, LoadMaterialTypedBlockU32(material, "surface", 48u, rawValue) && rawValue == 1u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, LoadMaterialTypedBlockU32(material, "surface", 52u, rawValue) && rawValue == 0u);
}

static void CheckGeneratedMaterialBindBinaryConstants(
    TestContext& context,
    const AStringView generatedSourceView,
    const NWB::Impl::Material& material
){
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsGeneratedUint2Constant(
        generatedSourceView,
        "NWB_MATERIAL_BIND_LAYOUT_HASH",
        material.typedLayoutHash()
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsGeneratedUintConstant(
        generatedSourceView,
        "NWB_MATERIAL_BIND_BLOCK_COUNT",
        static_cast<u32>(material.typedLayoutBlocks().size())
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsGeneratedUintConstant(
        generatedSourceView,
        "NWB_MATERIAL_BIND_FIELD_COUNT",
        static_cast<u32>(material.typedLayoutFields().size())
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsGeneratedUintConstant(
        generatedSourceView,
        "NWB_MATERIAL_BIND_BLOCK_BYTE_SIZE",
        static_cast<u32>(material.typedBlockBytes().size())
    ));

    const NameHash& materialInterfaceHash = material.materialInterface().hash();
    for(u32 lane = 0u; lane < NameDetail::s_HashLaneCount; ++lane){
        AString symbol("NWB_MATERIAL_BIND_INTERFACE_HASH_");
        AppendU32DecimalText(lane, symbol);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsGeneratedUint2Constant(
            generatedSourceView,
            AStringView(symbol.data(), symbol.size()),
            materialInterfaceHash.qwords[lane]
        ));
    }
}

static bool BuildMaterialFromBindAndMeta(
    const AStringView bindText,
    const AStringView materialText,
    const AStringView caseName,
    TestArena& testArena,
    NWB::Impl::Material& outMaterial
){
    NWB::Impl::MaterialCookEntry materialEntry(testArena.arena);
    if(!ParseMaterialEntryFromMetaText(materialText, testArena, materialEntry))
        return false;

    NWB::Impl::MaterialBindEntry bindEntry(testArena.arena);
    Path bindRoot;
    bool built = false;
    if(ParseMaterialBindFromText(bindText, caseName, bindEntry, bindRoot)){
        bindEntry.virtualPath = "project/material_interfaces/test_surface";

        NWB::Impl::ShaderCook::CookVector<NWB::Impl::MaterialBindEntry> bindEntries(testArena.arena);
        bindEntries.push_back(Move(bindEntry));
        NWB::Impl::ShaderCook::CookVector<NWB::Impl::MaterialCookEntry> materialEntries(testArena.arena);
        materialEntries.push_back(Move(materialEntry));
        built =
            NWB::Impl::ValidateMaterialCookInterfaces(bindEntries, materialEntries)
            && NWB::Impl::BuildMaterialAsset(materialEntries[0u], outMaterial)
        ;
    }

    if(!bindRoot.empty()){
        ErrorCode errorCode;
        static_cast<void>(RemoveAllIfExists(bindRoot, errorCode));
    }
    return built;
}

static void TestMaterialMetadataInterfaceAndBlockParameters(TestContext& context){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    NWB::Impl::MaterialCookEntry materialEntry(testArena.arena);
    const bool parsed = ParseMaterialEntryFromMetaText(s_BlockScopedMaterialMeta, testArena, materialEntry);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, parsed);
    if(!parsed)
        return;

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, materialEntry.materialInterface == Name("project/material_interfaces/test_surface"));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, materialEntry.parameters.size() == 3u);

    NWB::Impl::MaterialBindEntry bindEntry(testArena.arena);
    Path bindRoot;
    const bool parsedBind = ParseMaterialBindFromText(
        s_MinimalMaterialBindSource,
        "material_meta_bind_validation",
        bindEntry,
        bindRoot
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, parsedBind);
    if(!parsedBind)
        return;
    bindEntry.virtualPath = "project/material_interfaces/test_surface";

    NWB::Impl::ShaderCook::CookVector<NWB::Impl::MaterialBindEntry> bindEntries(testArena.arena);
    bindEntries.push_back(Move(bindEntry));
    NWB::Impl::ShaderCook::CookVector<NWB::Impl::MaterialCookEntry> materialEntries(testArena.arena);
    materialEntries.push_back(Move(materialEntry));
    const bool validated = NWB::Impl::ValidateMaterialCookInterfaces(bindEntries, materialEntries);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, validated);
    if(!validated)
        return;

    NWB::Impl::Material material(testArena.arena);
    const bool built = NWB::Impl::BuildMaterialAsset(materialEntries[0u], material);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, built);
    if(!built)
        return;

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, material.alpha() == 1.0f);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !material.transparent());
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, material.materialInterface() == Name("project/material_interfaces/test_surface"));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);
}

static void TestMaterialCodecTypedLayoutBoundary(TestContext& context){
    TestArena testArena;
    NWB::Impl::Material material(testArena.arena);

    {
        CapturingLogger logger;
        NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

        const bool built = BuildMaterialFromBindAndMeta(
            s_MinimalMaterialBindSource,
            s_BlockScopedMaterialMeta,
            "material_codec_typed_layout_boundary",
            testArena,
            material
        );
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, built);
        if(!built)
            return;

        NWB::Impl::MaterialAssetCodec codec;
        NWB::Core::Assets::AssetBytes binary = MakeAssetBytes(testArena);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, codec.serialize(material, binary));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !binary.empty());

        UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, codec.deserialize(
            testArena.arena,
            material.virtualPath(),
            binary,
            loadedAsset
        ));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, static_cast<bool>(loadedAsset));
        if(loadedAsset){
            const NWB::Impl::Material& loadedMaterial = static_cast<const NWB::Impl::Material&>(*loadedAsset);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMaterial.materialInterface() == material.materialInterface());
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMaterial.typedLayoutHash() == material.typedLayoutHash());
            CheckMinimalMaterialTypedLayout(context, loadedMaterial);
            CheckMinimalMaterialTypedBlockBytes(context, loadedMaterial);
        }

        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);
    }

#if defined(NWB_FINAL)
    {
        CapturingLogger logger;
        NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

        NWB::Impl::MaterialAssetCodec codec;
        NWB::Core::Assets::AssetBytes binary = MakeAssetBytes(testArena);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, codec.serialize(material, binary));

        usize layoutHashOffset = 0u;
        usize blockByteCountOffset = 0u;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, FindMaterialBinaryTypedLayoutOffsets(
            binary,
            layoutHashOffset,
            blockByteCountOffset
        ));

        NWB::Core::Assets::AssetBytes hashMismatchBinary = binary;
        const u64 invalidLayoutHash =
            material.typedLayoutHash() == Limit<u64>::s_Max
                ? material.typedLayoutHash() - 1u
                : material.typedLayoutHash() + 1u
        ;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, OverwriteU64(hashMismatchBinary, layoutHashOffset, invalidLayoutHash));
        CheckCodecRejectsBinary(context, testArena, codec, material.virtualPath(), hashMismatchBinary);

        NWB::Core::Assets::AssetBytes byteSizeMismatchBinary = binary;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !material.typedBlockBytes().empty());
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, OverwriteU32(
            byteSizeMismatchBinary,
            blockByteCountOffset,
            static_cast<u32>(material.typedBlockBytes().size() - 1u)
        ));
        CheckCodecRejectsBinary(context, testArena, codec, material.virtualPath(), byteSizeMismatchBinary);

        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 2u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("typed layout hash mismatch")));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT(
            "typed block byte count does not match typed layout"
        )));
    }
#endif
}

static void TestMaterialBindSchemaValidation(TestContext& context){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root;
    NWB::Impl::MaterialBindEntry entry(testArena.arena);
    const bool parsed = ParseMaterialBindFromText(
        s_MinimalMaterialBindSource,
        "material_bind_schema_valid",
        entry,
        root
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, parsed);
    if(parsed){
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, entry.structs.size() == 2u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, entry.instances.size() == 2u);

        const NWB::Impl::MaterialBindStruct* surfaceStruct = entry.findStruct("NwbTestSurfaceMaterial");
        const NWB::Impl::MaterialBindStruct* runtimeStruct = entry.findStruct("NwbTestRuntimeMaterial");
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, surfaceStruct != nullptr);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, runtimeStruct != nullptr);
        if(surfaceStruct){
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, surfaceStruct->fields.size() == 5u);

            const NWB::Impl::MaterialBindField* baseColorField = surfaceStruct->findField("base_color");
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, baseColorField != nullptr);
            if(baseColorField){
                NWB_ASSETS_GRAPHICS_TEST_CHECK(context, AStringView(baseColorField->type) == "float4");
                const NWB::Impl::MaterialBindAttribute* defaultAttribute = baseColorField->findAttribute("default");
                NWB_ASSETS_GRAPHICS_TEST_CHECK(context, defaultAttribute != nullptr);
                NWB_ASSETS_GRAPHICS_TEST_CHECK(context, defaultAttribute && defaultAttribute->arguments.size() == 1u);
                if(defaultAttribute && defaultAttribute->arguments.size() == 1u){
                    NWB_ASSETS_GRAPHICS_TEST_CHECK(
                        context,
                        AStringView(defaultAttribute->arguments[0u]) == "float4(1.0, 1.0, 1.0, 1.0)"
                    );
                }
            }
        }

        const NWB::Impl::MaterialBindInstance* surfaceInstance = entry.findInstance("surface");
        const NWB::Impl::MaterialBindInstance* runtimeInstance = entry.findInstance("runtime");
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, surfaceInstance != nullptr);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, runtimeInstance != nullptr);
        if(surfaceInstance)
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, AStringView(surfaceInstance->type) == "NwbTestSurfaceMaterial");
        if(runtimeInstance)
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, AStringView(runtimeInstance->type) == "NwbTestRuntimeMaterial");
    }

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);

    ErrorCode errorCode;
    static_cast<void>(RemoveAllIfExists(root, errorCode));

#if defined(NWB_FINAL)
    auto expectParseFailure = [&](
        const AStringView bindText,
        const AStringView caseName,
        const tchar* expectedError
    ){
        Path invalidRoot;
        NWB::Impl::MaterialBindEntry invalidEntry(testArena.arena);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !ParseMaterialBindFromText(bindText, caseName, invalidEntry, invalidRoot));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(expectedError));

        ErrorCode removeErrorCode;
        static_cast<void>(RemoveAllIfExists(invalidRoot, removeErrorCode));
    };

    expectParseFailure(
        s_UnknownBlockClassMaterialBindSource,
        "material_bind_unknown_block_class",
        NWB_TEXT("unsupported attribute 'material_project'")
    );
    expectParseFailure(
        s_UnsupportedFieldTypeMaterialBindSource,
        "material_bind_unsupported_field_type",
        NWB_TEXT("unsupported type 'half'")
    );
    expectParseFailure(
        s_InvalidDefaultMaterialBindSource,
        "material_bind_invalid_default",
        NWB_TEXT("default attribute requires one non-empty string argument")
    );
    expectParseFailure(
        s_MissingDefaultMaterialBindSource,
        "material_bind_missing_default",
        NWB_TEXT("must declare a default attribute")
    );
    expectParseFailure(
        s_DuplicateInstanceMaterialBindSource,
        "material_bind_duplicate_instance",
        NWB_TEXT("duplicate struct instance declaration")
    );
    expectParseFailure(
        s_InstanceOverrideMaterialBindSource,
        "material_bind_instance_override",
        NWB_TEXT("unsupported asset field 'instance_override'")
    );
#endif
}

static void TestMaterialBindGeneratedSlangText(TestContext& context){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root;
    NWB::Impl::MaterialBindEntry entry(testArena.arena);
    const bool parsed = ParseMaterialBindFromText(
        s_MinimalMaterialBindSource,
        "material_bind_generated_text",
        entry,
        root
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, parsed);
    if(parsed){
        entry.virtualPath = "project/material_interfaces/test_surface";

        NWB::Impl::ShaderCook::CookString generatedSource(testArena.arena);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BuildMaterialBindIncludeSource(
            testArena.arena,
            entry,
            generatedSource
        ));
        CheckGeneratedMaterialBindSource(context, AStringView(generatedSource.data(), generatedSource.size()));
    }

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);

    ErrorCode errorCode;
    static_cast<void>(RemoveAllIfExists(root, errorCode));
}

static void TestMaterialBindCookIntegration(TestContext& context){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root;
    Path outputDirectory;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, CookMaterialBindMaterialIntegration(
        s_MinimalMaterialBindSource,
        s_BlockScopedMaterialMeta,
        "material_bind_material_integration",
        testArena,
        root,
        outputDirectory
    ));

    const Path generatedIncludePath =
        root / "cache" / "tests" / "material_bind_includes"
        / "project" / "material_interfaces" / "test_surface.bind"
    ;
    NWB::Impl::ShaderCook::CookString generatedSource(testArena.arena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ReadTextFile(generatedIncludePath, generatedSource));
    CheckGeneratedMaterialBindSource(context, AStringView(generatedSource.data(), generatedSource.size()));

    NWB::Impl::ShaderCook shaderCook(testArena.arena);
    NWB::Impl::ShaderCook::CookVector<Path> includeDirectories(testArena.arena);
    includeDirectories.push_back(root / "cache" / "tests" / "material_bind_includes");
    includeDirectories.push_back(AssetsGraphicsTestRepoRoot() / "impl" / "assets" / "graphics");
    NWB::Impl::ShaderCook::CookVector<Path> dependencies(testArena.arena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, shaderCook.gatherShaderDependencies(
        root / "assets" / "shaders" / "material_mesh.slang",
        includeDirectories,
        dependencies
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsCanonicalPath(dependencies, generatedIncludePath));

    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, LoadCookedMaterial(
        context,
        testArena,
        outputDirectory,
        Name("project/materials/test_material"),
        loadedAsset
    ));
    if(loadedAsset){
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedAsset->assetType() == NWB::Impl::Material::AssetTypeName());
        const NWB::Impl::Material& material = static_cast<const NWB::Impl::Material&>(*loadedAsset);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, material.alpha() == 1.0f);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(
            context,
            material.materialInterface() == Name("project/material_interfaces/test_surface")
        );
        CheckMinimalMaterialTypedLayout(context, material);
        CheckMinimalMaterialTypedBlockBytes(context, material);
        CheckGeneratedMaterialBindBinaryConstants(
            context,
            AStringView(generatedSource.data(), generatedSource.size()),
            material
        );
    }

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);

    ErrorCode errorCode;
    static_cast<void>(RemoveAllIfExists(root, errorCode));

#if defined(NWB_FINAL)
    Path invalidRoot;
    Path invalidOutputDirectory;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !CookMaterialBindMaterialIntegration(
        s_MinimalMaterialBindSource,
        s_UnknownInterfaceParameterMaterialMeta,
        "material_bind_unknown_interface_parameter",
        testArena,
        invalidRoot,
        invalidOutputDirectory
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT(
        "parameter 'surface.missing' is not declared by interface"
    )));

    errorCode.clear();
    static_cast<void>(RemoveAllIfExists(invalidRoot, errorCode));

    Path flatRoot;
    Path flatOutputDirectory;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !CookMaterialBindMaterialIntegration(
        s_MinimalMaterialBindSource,
        s_FlatInterfaceParameterMaterialMeta,
        "material_bind_flat_interface_parameter",
        testArena,
        flatRoot,
        flatOutputDirectory
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT(
        "interface parameter 'runtime.fade_alpha' must be declared inside a block map"
    )));

    errorCode.clear();
    static_cast<void>(RemoveAllIfExists(flatRoot, errorCode));

    Path unsupportedFieldRoot;
    Path unsupportedFieldOutputDirectory;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !CookMaterialBindMaterialIntegration(
        s_MinimalMaterialBindSource,
        s_UnsupportedMaterialFieldMeta,
        "material_bind_unsupported_material_field",
        testArena,
        unsupportedFieldRoot,
        unsupportedFieldOutputDirectory
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("unsupported asset field 'compiler'")));

    errorCode.clear();
    static_cast<void>(RemoveAllIfExists(unsupportedFieldRoot, errorCode));

    Path missingShaderVariantRoot;
    Path missingShaderVariantOutputDirectory;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !CookMaterialBindMaterialIntegration(
        s_MinimalMaterialBindSource,
        s_MissingShaderVariantMaterialMeta,
        "material_bind_missing_shader_variant",
        testArena,
        missingShaderVariantRoot,
        missingShaderVariantOutputDirectory
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("field 'shader_variant' is required")));

    errorCode.clear();
    static_cast<void>(RemoveAllIfExists(missingShaderVariantRoot, errorCode));

    Path incompleteBindRoot;
    Path incompleteBindOutputDirectory;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !CookMaterialBindMaterialIntegration(
        s_SurfaceOnlyMaterialBindSource,
        s_BlockScopedMaterialMeta,
        "material_bind_incomplete_block_scoped",
        testArena,
        incompleteBindRoot,
        incompleteBindOutputDirectory
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT(
        "typed parameter 'runtime.fade_alpha' is not declared by interface"
    )));

    errorCode.clear();
    static_cast<void>(RemoveAllIfExists(incompleteBindRoot, errorCode));

    Path interfaceShaderMismatchRoot;
    Path interfaceShaderMismatchOutputDirectory;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, PrepareAssetsGraphicsCookCase(
        "material_bind_interface_without_bind_shader",
        interfaceShaderMismatchRoot,
        interfaceShaderMismatchOutputDirectory
    ));
    const Path interfaceShaderMismatchAssetRoot = interfaceShaderMismatchRoot / "assets";
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, WriteMaterialBindMaterialIntegrationAssetsWithMeshSource(
        testArena,
        interfaceShaderMismatchAssetRoot,
        s_MinimalMaterialBindSource,
        s_BlockScopedMaterialMeta,
        s_UnboundMaterialShaderProbeSource
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !CookPreparedGraphicsAssetRoots(
        testArena,
        interfaceShaderMismatchRoot,
        interfaceShaderMismatchOutputDirectory,
        { interfaceShaderMismatchAssetRoot }
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("does not include a generated material bind")));

    errorCode.clear();
    static_cast<void>(RemoveAllIfExists(interfaceShaderMismatchRoot, errorCode));

    Path interfaceIdentityMismatchRoot;
    Path interfaceIdentityMismatchOutputDirectory;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, PrepareAssetsGraphicsCookCase(
        "material_bind_interface_identity_mismatch",
        interfaceIdentityMismatchRoot,
        interfaceIdentityMismatchOutputDirectory
    ));
    const Path interfaceIdentityMismatchAssetRoot = interfaceIdentityMismatchRoot / "assets";
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, WriteMaterialBindMaterialIntegrationAssetsWithMeshSource(
        testArena,
        interfaceIdentityMismatchAssetRoot,
        s_MinimalMaterialBindSource,
        s_BlockScopedMaterialMeta,
        s_OtherMaterialBindShaderProbeSource
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, WriteTextFile(
        interfaceIdentityMismatchAssetRoot / "material_interfaces" / "other_surface.bind",
        s_MinimalMaterialBindSource
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !CookPreparedGraphicsAssetRoots(
        testArena,
        interfaceIdentityMismatchRoot,
        interfaceIdentityMismatchOutputDirectory,
        { interfaceIdentityMismatchAssetRoot }
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT(
        "includes generated material bind interface 'project/material_interfaces/other_surface'"
    )));

    errorCode.clear();
    static_cast<void>(RemoveAllIfExists(interfaceIdentityMismatchRoot, errorCode));

#endif
}

static void TestMaterialRejectsMissingInterfaceCookIntegration(TestContext& context){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root;
    Path outputDirectory;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, PrepareAssetsGraphicsCookCase(
        "material_missing_interface_rejection",
        root,
        outputDirectory
    ));
    const Path assetRoot = root / "assets";
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, WriteMaterialBindMaterialIntegrationAssetsWithMeshSource(
        testArena,
        assetRoot,
        s_MinimalMaterialBindSource,
        s_MissingInterfaceMaterialMeta,
        s_UnboundMaterialShaderProbeSource
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !CookPreparedGraphicsAssetRoots(
        testArena,
        root,
        outputDirectory,
        { assetRoot }
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("interface is required")));

    ErrorCode errorCode;
    static_cast<void>(RemoveAllIfExists(root, errorCode));
#else
    static_cast<void>(context);
#endif
}

static void TestMaterialBindDependencyInvalidation(TestContext& context){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root;
    Path outputDirectory;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, PrepareAssetsGraphicsCookCase(
        "material_bind_dependency_invalidation",
        root,
        outputDirectory
    ));
    const Path assetRoot = root / "assets";
    if(!WriteMaterialBindMaterialIntegrationAssets(
        testArena,
        assetRoot,
        s_MinimalMaterialBindSource,
        s_BlockScopedMaterialMeta
    ))
        return;

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }));

    const Path generatedIncludePath =
        root / "cache" / "tests" / "material_bind_includes"
        / "project" / "material_interfaces" / "test_surface.bind"
    ;
    NWB::Impl::ShaderCook::CookString generatedSource(testArena.arena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ReadTextFile(generatedIncludePath, generatedSource));

    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, LoadCookedMaterial(
        context,
        testArena,
        outputDirectory,
        Name("project/materials/test_material"),
        loadedAsset
    ));
    if(!loadedAsset)
        return;

    const NWB::Impl::Material& material = static_cast<const NWB::Impl::Material&>(*loadedAsset);
    CheckMinimalMaterialTypedLayout(context, material);
    CheckMinimalMaterialTypedBlockBytes(context, material);
    CheckGeneratedMaterialBindBinaryConstants(context, AStringView(generatedSource.data(), generatedSource.size()), material);
    const u64 initialLayoutHash = material.typedLayoutHash();

    NWB::Core::GraphicsVector<NWB::Core::ShaderArchive::Record> records(testArena.arena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, LoadCookedShaderArchiveRecords(context, testArena, outputDirectory, records));
    u64 initialMeshSourceChecksum = 0u;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, FindShaderArchiveSourceChecksum(
        records,
        Name("project/shaders/material_mesh"),
        Name("mesh"),
        initialMeshSourceChecksum
    ));

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, WriteTextFile(
        assetRoot / "material_interfaces" / "test_surface.bind",
        s_UpdatedDefaultMaterialBindSource
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }));

    generatedSource.clear();
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ReadTextFile(generatedIncludePath, generatedSource));
    const AStringView updatedGeneratedSource(generatedSource.data(), generatedSource.size());
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsText(
        updatedGeneratedSource,
        "static const uint3 NWB_MATERIAL_BIND_SURFACE_FEATURE_MASK_DEFAULT = uint3(7u, 8u, 9u);"
    ));

    loadedAsset.reset();
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, LoadCookedMaterial(
        context,
        testArena,
        outputDirectory,
        Name("project/materials/test_material"),
        loadedAsset
    ));
    if(loadedAsset){
        const NWB::Impl::Material& updatedMaterial = static_cast<const NWB::Impl::Material&>(*loadedAsset);
        CheckMinimalMaterialTypedLayout(context, updatedMaterial, 7u, 8u, 9u);
        CheckMinimalMaterialTypedBlockBytes(context, updatedMaterial, 7u, 8u, 9u);
        CheckGeneratedMaterialBindBinaryConstants(context, updatedGeneratedSource, updatedMaterial);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, updatedMaterial.typedLayoutHash() != initialLayoutHash);
    }

    records.clear();
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, LoadCookedShaderArchiveRecords(context, testArena, outputDirectory, records));
    u64 updatedMeshSourceChecksum = 0u;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, FindShaderArchiveSourceChecksum(
        records,
        Name("project/shaders/material_mesh"),
        Name("mesh"),
        updatedMeshSourceChecksum
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, updatedMeshSourceChecksum != initialMeshSourceChecksum);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);

    ErrorCode errorCode;
    static_cast<void>(RemoveAllIfExists(root, errorCode));
}

static void TestGeometryCookerTypedStreams(TestContext& context){
    CookAndCheckMinimalTypedAsset<NWB::Impl::Geometry>(
        context,
        s_MinimalGeometryMeta,
        "minimal_geometry",
        MinimalAssetKind::Geometry,
        [&](const NWB::Impl::Geometry& loadedGeometry){
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.geometryClass() == NWB::Impl::GeometryClass::Static);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.vertexCount() == 3u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.indices().size() == 3u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.positions()[0].x == -0.5f);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, LoadHalf4U(loadedGeometry.normals()[0]).z == 1.f);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, LoadHalf4U(loadedGeometry.colors()[2]).z == 1.f);
        }
    );
}

static void TestGeometryCookerDefaultColors(TestContext& context){
    CookAndCheckMinimalTypedAsset<NWB::Impl::Geometry>(
        context,
        s_DefaultColorGeometryMeta,
        "default_color_geometry",
        MinimalAssetKind::Geometry,
        [&](const NWB::Impl::Geometry& loadedGeometry){
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.vertexCount() == 3u);
            const Float4U color0 = LoadHalf4U(loadedGeometry.colors()[0]);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, color0.x == 1.f);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, color0.w == 1.f);
        }
    );
}

static void TestMaterialBindDiscoveryValidation(TestContext& context){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root;
    Path outputDirectory;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, CookMinimalGeometryWithMaterialBind(
        s_MinimalMaterialBindSource,
        "material_bind_valid",
        testArena,
        root,
        outputDirectory
    ));

    const Path generatedIncludePath =
        root / "cache" / "tests" / "material_bind_includes"
        / "project" / "material_interfaces" / "test_surface.bind"
    ;
    NWB::Impl::ShaderCook::CookString generatedSource(testArena.arena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ReadTextFile(generatedIncludePath, generatedSource));
    const AStringView generatedSourceView(generatedSource.data(), generatedSource.size());
    CheckGeneratedMaterialBindSource(context, generatedSourceView);

    const Path shaderIncludeProbePath = root / "shader_include_probe.slang";
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, WriteTextFile(
        shaderIncludeProbePath,
        "#include \"project/material_interfaces/test_surface.bind\"\n"
    ));
    NWB::Impl::ShaderCook shaderCook(testArena.arena);
    NWB::Impl::ShaderCook::CookVector<Path> includeDirectories(testArena.arena);
    includeDirectories.push_back(root / "cache" / "tests" / "material_bind_includes");
    NWB::Impl::ShaderCook::CookVector<Path> dependencies(testArena.arena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, shaderCook.gatherShaderDependencies(
        shaderIncludeProbePath,
        includeDirectories,
        dependencies
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsCanonicalPath(dependencies, generatedIncludePath));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);

    ErrorCode errorCode;
    static_cast<void>(RemoveAllIfExists(root, errorCode));

    Path shaderProbeRoot;
    Path shaderProbeOutputDirectory;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, CookMaterialBindShaderProbe(
        s_MinimalMaterialBindSource,
        "material_bind_shader_probe",
        testArena,
        shaderProbeRoot,
        shaderProbeOutputDirectory
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);

    errorCode.clear();
    static_cast<void>(RemoveAllIfExists(shaderProbeRoot, errorCode));

#if defined(NWB_FINAL)
    Path duplicateIncludeRoot;
    Path duplicateIncludeOutputDirectory;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !CookDuplicateGeneratedMaterialBindIncludePath(
        "material_bind_duplicate_include_path",
        testArena,
        duplicateIncludeRoot,
        duplicateIncludeOutputDirectory
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT(
        "duplicate material bind include path 'project/material_interfaces/test_surface.bind'"
    )));

    errorCode.clear();
    static_cast<void>(RemoveAllIfExists(duplicateIncludeRoot, errorCode));

    Path invalidRoot;
    Path invalidOutputDirectory;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !CookMinimalGeometryWithMaterialBind(
        s_DuplicateFieldMaterialBindSource,
        "material_bind_duplicate_field",
        testArena,
        invalidRoot,
        invalidOutputDirectory
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("duplicate struct field declaration")));

    errorCode.clear();
    static_cast<void>(RemoveAllIfExists(invalidRoot, errorCode));
#endif
}

static void TestGeometryCookerValidationFailures(TestContext& context){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    auto expectCookFailure = [&](const AStringView metaText, const AStringView caseName){
        Path root;
        Path outputDirectory;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !CookSingleGeometryMeta(
            metaText,
            caseName,
            testArena,
            root,
            outputDirectory
        ));

        ErrorCode errorCode;
        static_cast<void>(RemoveAllIfExists(root, errorCode));
    };

    expectCookFailure(s_MissingGeometryClassMeta, "missing_geometry_class");
    expectCookFailure(s_UnsupportedGeometryFieldsMeta, "unsupported_geometry_fields");
    expectCookFailure(s_MismatchedGeometryMeta, "mismatched_geometry_streams");
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() >= 3u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("'geometry_class' is required")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("unsupported geometry fields are present")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("vertex stream counts must match")));
#else
    static_cast<void>(context);
#endif
}

static void TestSkinnedGeometryCookerMinimalAsset(TestContext& context){
    CookAndCheckMinimalTypedAsset<NWB::Impl::SkinnedGeometry>(
        context,
        s_MinimalSkinnedGeometryMeta,
        "minimal",
        MinimalAssetKind::SkinnedGeometry,
        [&](const NWB::Impl::SkinnedGeometry& loadedGeometry){
            CheckMinimalSkinnedGeometryDefaults(context, loadedGeometry);
        }
    );
}

static void TestSkinnedGeometryCookerGeneratesMissingFrames(TestContext& context){
    CookAndCheckMinimalTypedAsset<NWB::Impl::SkinnedGeometry>(
        context,
        s_GeneratedFrameSkinnedGeometryMeta,
        "generated_frames",
        MinimalAssetKind::SkinnedGeometry,
        [&](const NWB::Impl::SkinnedGeometry& loadedGeometry){
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.restVertices().size() == 3u);
            for(const NWB::Impl::SkinnedGeometryVertex& vertex : loadedGeometry.restVertices()){
                const Float4U normal = LoadHalf4U(vertex.normal);
                const Float4U tangent = LoadHalf4U(vertex.tangent);
                NWB_ASSETS_GRAPHICS_TEST_CHECK(context, normal.x == 0.0f);
                NWB_ASSETS_GRAPHICS_TEST_CHECK(context, normal.y == 0.0f);
                NWB_ASSETS_GRAPHICS_TEST_CHECK(context, normal.z == 1.0f);
                NWB_ASSETS_GRAPHICS_TEST_CHECK(context, tangent.x == 1.0f);
                NWB_ASSETS_GRAPHICS_TEST_CHECK(context, tangent.y == 0.0f);
                NWB_ASSETS_GRAPHICS_TEST_CHECK(context, tangent.z == 0.0f);
                NWB_ASSETS_GRAPHICS_TEST_CHECK(context, tangent.w == 1.0f);
            }
        }
    );
}

static void TestSkinnedGeometryCookerU32IndexType(TestContext& context){
    CookAndCheckMinimalTypedAsset<NWB::Impl::SkinnedGeometry>(
        context,
        s_U32IndexTypeSkinnedGeometryMeta,
        "u32_index_type",
        MinimalAssetKind::SkinnedGeometry,
        [&](const NWB::Impl::SkinnedGeometry& loadedGeometry){
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.indices().size() == 3u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.indices()[2] == 2u);
        }
    );
}

static void TestSkinnedGeometryCookerExplicitEmptyOptionalLists(TestContext& context){
    auto expectCookedDefaultOptionals = [&](const NWB::Impl::SkinnedGeometry& loadedGeometry){
        CheckMinimalSkinnedGeometryDefaults(context, loadedGeometry);
    };

    CookAndCheckMinimalTypedAsset<NWB::Impl::SkinnedGeometry>(
        context,
        s_EmptyListOptionalSkinnedGeometryMeta,
        "empty_optional_lists",
        MinimalAssetKind::SkinnedGeometry,
        expectCookedDefaultOptionals
    );
    CookAndCheckMinimalTypedAsset<NWB::Impl::SkinnedGeometry>(
        context,
        s_EmptyMapOptionalSkinnedGeometryMeta,
        "empty_optional_maps",
        MinimalAssetKind::SkinnedGeometry,
        expectCookedDefaultOptionals
    );
}

static void TestSkinnedGeometryCookerNativeCharacterMock(TestContext& context){
    CookAndCheckMinimalTypedAsset<NWB::Impl::SkinnedGeometry>(
        context,
        s_NativeCharacterMockSkinnedGeometryMeta,
        "native_character_mock",
        MinimalAssetKind::SkinnedGeometry,
        [&](const NWB::Impl::SkinnedGeometry& loadedGeometry){
            CheckSkinnedSkinnedGeometryPayload(context, loadedGeometry, 2u, 2u, 1u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, LoadHalf4U(loadedGeometry.restVertices()[3].color0).w == 0.5f);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.skin()[1].joint[1] == 1u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.skin()[1].weight[0] == 0.75f);
        }
    );
}

static void TestSkinnedGeometryCookerNormalizesSkinWeights(TestContext& context){
    CookAndCheckMinimalTypedAsset<NWB::Impl::SkinnedGeometry>(
        context,
        s_NonnormalizedSkinSkinnedGeometryMeta,
        "nonnormalized_skin",
        MinimalAssetKind::SkinnedGeometry,
        [&](const NWB::Impl::SkinnedGeometry& loadedGeometry){
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.skin().size() == 3u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.skeletonJointCount() == 2u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.skin()[0u].weight[0] == 1.0f);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.skin()[1u].weight[0] == 0.75f);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.skin()[1u].weight[1] == 0.25f);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.skin()[2u].weight[0] == 0.0f);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.skin()[2u].weight[1] == 1.0f);
        }
    );
}

static void TestSkinnedGeometryCookerSkinnedClass(TestContext& context){
    CookAndCheckMinimalTypedAsset<NWB::Impl::SkinnedGeometry>(
        context,
        s_SkinnedOnlySkinnedGeometryMeta,
        "skinned_only",
        MinimalAssetKind::SkinnedGeometry,
        [&](const NWB::Impl::SkinnedGeometry& loadedGeometry){
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.geometryClass() == NWB::Impl::GeometryClass::Skinned);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.skin().size() == 3u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.skeletonJointCount() == 1u);
        }
    );
}

static void TestSkinnedGeometryCookerValidationFailures(TestContext& context){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    auto expectCookFailure = [&](const AStringView metaText, const AStringView caseName){
        Path root;
        Path outputDirectory;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !CookSingleSkinnedGeometryMeta(
            metaText,
            caseName,
            testArena,
            root,
            outputDirectory
        ));

        ErrorCode errorCode;
        static_cast<void>(RemoveAllIfExists(root, errorCode));
    };

    expectCookFailure(s_MissingGeometryClassSkinnedGeometryMeta, "missing_geometry_class");
    expectCookFailure(s_StaticClassSkinnedGeometryMeta, "static_class");
    expectCookFailure(s_MismatchedSkinnedGeometryMeta, "mismatched_streams");
    expectCookFailure(s_MissingIndexTypeSkinnedGeometryMeta, "missing_index_type");
    expectCookFailure(s_MismatchedSkinSkinnedGeometryMeta, "mismatched_skin");
    expectCookFailure(s_SourceImportSkinnedGeometryMeta, "source_import");
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() >= 6u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("'geometry_class' is required")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("geometry_class must be skinned")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("rest vertex stream counts must match")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("'index_type' must be 'u16' or 'u32'")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("skin streams must match vertex count")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("offline converter to emit native .nwb streams")));
#else
    static_cast<void>(context);
#endif
}

template<typename MutateFnT>
static void CheckInvalidSkinnedGeometry(TestContext& context, MutateFnT mutate){
    TestArena testArena;
    NWB::Impl::SkinnedGeometry geometry = BuildValidSkinnedGeometry(testArena);
    mutate(geometry);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
}

static void TestSkinnedGeometryValidationFailures(TestContext& context){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    CheckInvalidSkinnedGeometry(context, [](NWB::Impl::SkinnedGeometry& geometry){
        auto vertices = geometry.restVertices();
        NWB::Impl::StoreSkinnedGeometryVertexNormal(vertices[0], Float3U(0.f, 0.f, 0.f));
        geometry.setRestVertices(Move(vertices));
    });

    CheckInvalidSkinnedGeometry(context, [](NWB::Impl::SkinnedGeometry& geometry){
        auto vertices = geometry.restVertices();
        NWB::Impl::StoreSkinnedGeometryVertexTangent(vertices[0], Float4U(1.f, 0.f, 0.f, 0.f));
        geometry.setRestVertices(Move(vertices));
    });

    CheckInvalidSkinnedGeometry(context, [](NWB::Impl::SkinnedGeometry& geometry){
        auto vertices = geometry.restVertices();
        NWB::Impl::StoreSkinnedGeometryVertexTangent(vertices[0], Float4U(0.f, 0.f, 1.f, 1.f));
        geometry.setRestVertices(Move(vertices));
    });

    CheckInvalidSkinnedGeometry(context, [](NWB::Impl::SkinnedGeometry& geometry){
        auto vertices = geometry.restVertices();
        NWB::Impl::StoreSkinnedGeometryVertexTangent(vertices[0], Float4U(1.f, 0.f, 0.f, 2.f));
        geometry.setRestVertices(Move(vertices));
    });

    CheckInvalidSkinnedGeometry(context, [](NWB::Impl::SkinnedGeometry& geometry){
        auto vertices = geometry.restVertices();
        NWB::Impl::StoreSkinnedGeometryVertexNormal(vertices[0], Float3U(0.f, 0.f, 2.f));
        geometry.setRestVertices(Move(vertices));
    });

    CheckInvalidSkinnedGeometry(context, [](NWB::Impl::SkinnedGeometry& geometry){
        auto vertices = geometry.restVertices();
        NWB::Impl::StoreSkinnedGeometryVertexTangent(vertices[0], Float4U(0.70710677f, 0.f, 0.70710677f, 1.f));
        geometry.setRestVertices(Move(vertices));
    });

    CheckInvalidSkinnedGeometry(context, [](NWB::Impl::SkinnedGeometry& geometry){
        auto skin = geometry.skin();
        skin[0].weight[0] = 0.5f;
        geometry.setSkin(Move(skin));
    });

    CheckInvalidSkinnedGeometry(context, [](NWB::Impl::SkinnedGeometry& geometry){
        auto skin = geometry.skin();
        skin.pop_back();
        geometry.setSkin(Move(skin));
    });

    CheckInvalidSkinnedGeometry(context, [](NWB::Impl::SkinnedGeometry& geometry){
        geometry.setSkeletonJointCount(0u);
    });

    CheckInvalidSkinnedGeometry(context, [](NWB::Impl::SkinnedGeometry& geometry){
        auto inverseBindMatrices = geometry.inverseBindMatrices();
        inverseBindMatrices.push_back(MakeJointMatrix(0.0f, 0.0f, 0.0f));
        geometry.setInverseBindMatrices(Move(inverseBindMatrices));
    });

    CheckInvalidSkinnedGeometry(context, [](NWB::Impl::SkinnedGeometry& geometry){
        auto inverseBindMatrices = geometry.inverseBindMatrices();
        inverseBindMatrices[0u].rows[3].w = 0.0f;
        geometry.setInverseBindMatrices(Move(inverseBindMatrices));
    });

    CheckInvalidSkinnedGeometry(context, [](NWB::Impl::SkinnedGeometry& geometry){
        auto skin = geometry.skin();
        skin[0].joint[0] = 1u;
        geometry.setSkin(Move(skin));
    });

    CheckInvalidSkinnedGeometry(context, [](NWB::Impl::SkinnedGeometry& geometry){
        auto indices = geometry.indices();
        indices.pop_back();
        geometry.setIndices(Move(indices));
    });

    CheckInvalidSkinnedGeometry(context, [](NWB::Impl::SkinnedGeometry& geometry){
        auto indices = geometry.indices();
        indices[2] = 99u;
        geometry.setIndices(Move(indices));
    });

    CheckInvalidSkinnedGeometry(context, [](NWB::Impl::SkinnedGeometry& geometry){
        auto indices = geometry.indices();
        indices[2] = indices[1];
        geometry.setIndices(Move(indices));
    });

    CheckInvalidSkinnedGeometry(context, [](NWB::Impl::SkinnedGeometry& geometry){
        auto vertices = geometry.restVertices();
        vertices[2].position = Float3U(0.0f, -0.5f, 0.0f);
        geometry.setRestVertices(Move(vertices));
    });
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 16u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("degenerate normal/tangent frame")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("invalid normal/tangent frame")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("triangle 0 is degenerate")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("triangle 0 has zero area")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("no skeleton joint count")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("inverse bind matrices are invalid")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("exceeds skeleton joint count")));
#else
    static_cast<void>(context);
#endif
}

static void TestGeometryClassPolicyHelpers(TestContext& context){
    using namespace NWB::Impl;

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, GeometryClassMatchesSkinPayload(GeometryClass::Static, false));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !GeometryClassMatchesSkinPayload(GeometryClass::Static, true));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, GeometryClassMatchesSkinPayload(GeometryClass::Skinned, true));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !GeometryClassMatchesSkinPayload(GeometryClass::Skinned, false));

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, GeometryClassAcceptsSkinPayload(GeometryClass::Static, false));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !GeometryClassAcceptsSkinPayload(GeometryClass::Static, true));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, GeometryClassAcceptsSkinPayload(GeometryClass::Skinned, false));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, GeometryClassAcceptsSkinPayload(GeometryClass::Skinned, true));
}

static void TestFormatBlockDimensions(TestContext& context){
    const NWB::Core::FormatInfo& rgba8 = NWB::Core::GetFormatInfo(NWB::Core::Format::RGBA8_UNORM);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Core::GetFormatBlockWidth(rgba8) == 1u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Core::GetFormatBlockHeight(rgba8) == 1u);

    const NWB::Core::FormatInfo& bc1 = NWB::Core::GetFormatInfo(NWB::Core::Format::BC1_UNORM);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Core::GetFormatBlockWidth(bc1) == 4u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Core::GetFormatBlockHeight(bc1) == 4u);

    const NWB::Core::FormatInfo& astc8x5 = NWB::Core::GetFormatInfo(NWB::Core::Format::ASTC_8x5_UNORM);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Core::GetFormatBlockWidth(astc8x5) == 8u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Core::GetFormatBlockHeight(astc8x5) == 5u);

    const NWB::Core::FormatInfo& astc12x10 = NWB::Core::GetFormatInfo(NWB::Core::Format::ASTC_12x10_FLOAT);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Core::GetFormatBlockWidth(astc12x10) == 12u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Core::GetFormatBlockHeight(astc12x10) == 10u);
}


#undef NWB_ASSETS_GRAPHICS_TEST_CHECK


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_TEST_ENTRY_POINT("assets graphics", [](NWB::Tests::TestContext& context){
    __hidden_assets_graphics_tests::TestVolumeSessionAcceptsScratchBytes(context);
    __hidden_assets_graphics_tests::TestGeometryCodecRoundTrip(context);
    __hidden_assets_graphics_tests::TestGeometryCodecRejectsUnsupportedBinaryVersion(context);
    __hidden_assets_graphics_tests::TestSkinnedGeometryCodecRoundTrip(context);
    __hidden_assets_graphics_tests::TestMinimalSkinnedGeometryCodecRoundTrip(context);
    __hidden_assets_graphics_tests::TestSkinnedGeometryCodecRejectsUnsupportedBinaryVersion(context);
    __hidden_assets_graphics_tests::TestSkinnedGeometryCodecRejectsMalformedCounts(context);
    __hidden_assets_graphics_tests::TestSkinnedGeometryCodecRejectsMalformedDependentCounts(context);
    __hidden_assets_graphics_tests::TestMaterialMetadataInterfaceAndBlockParameters(context);
    __hidden_assets_graphics_tests::TestMaterialCodecTypedLayoutBoundary(context);
    __hidden_assets_graphics_tests::TestMaterialBindSchemaValidation(context);
    __hidden_assets_graphics_tests::TestMaterialBindGeneratedSlangText(context);
    __hidden_assets_graphics_tests::TestMaterialBindCookIntegration(context);
    __hidden_assets_graphics_tests::TestMaterialRejectsMissingInterfaceCookIntegration(context);
    __hidden_assets_graphics_tests::TestMaterialBindDependencyInvalidation(context);
    __hidden_assets_graphics_tests::TestGeometryCookerTypedStreams(context);
    __hidden_assets_graphics_tests::TestGeometryCookerDefaultColors(context);
    __hidden_assets_graphics_tests::TestMaterialBindDiscoveryValidation(context);
    __hidden_assets_graphics_tests::TestGeometryCookerValidationFailures(context);
    __hidden_assets_graphics_tests::TestSkinnedGeometryCookerMinimalAsset(context);
    __hidden_assets_graphics_tests::TestSkinnedGeometryCookerGeneratesMissingFrames(context);
    __hidden_assets_graphics_tests::TestSkinnedGeometryCookerU32IndexType(context);
    __hidden_assets_graphics_tests::TestSkinnedGeometryCookerExplicitEmptyOptionalLists(context);
    __hidden_assets_graphics_tests::TestSkinnedGeometryCookerNativeCharacterMock(context);
    __hidden_assets_graphics_tests::TestSkinnedGeometryCookerNormalizesSkinWeights(context);
    __hidden_assets_graphics_tests::TestSkinnedGeometryCookerSkinnedClass(context);
    __hidden_assets_graphics_tests::TestSkinnedGeometryCookerValidationFailures(context);
    __hidden_assets_graphics_tests::TestSkinnedGeometryValidationFailures(context);
    __hidden_assets_graphics_tests::TestGeometryClassPolicyHelpers(context);
    __hidden_assets_graphics_tests::TestFormatBlockDimensions(context);
})


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

