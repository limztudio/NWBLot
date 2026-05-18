// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_geometry/skinned_geometry_asset.h>
#include <impl/assets_geometry/geometry_asset.h>
#include <impl/assets_graphics/graphics_asset_cooker.h>

#include <tests/capturing_logger.h>
#include <tests/test_context.h>

#include <core/alloc/scratch.h>
#include <core/common/common.h>
#include <core/filesystem/filesystem.h>
#include <core/graphics/common.h>

#include <global/binary.h>
#include <global/compile.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestContext = NWB::Tests::TestContext;
using CapturingLogger = NWB::Tests::CapturingLogger;
using NWB::Tests::MakeQuadTriangleIndices;
using NWB::Tests::MakeTriangleIndices;


#define NWB_ASSETS_GRAPHICS_TEST_CHECK NWB_TEST_CHECK


struct AssetsGraphicsTestAllocatorTag;
using AssetsGraphicsTestAllocator = NWB::Tests::CountingTestAllocator<AssetsGraphicsTestAllocatorTag>;
using TestArena = NWB::Tests::TestArena<AssetsGraphicsTestAllocator>;

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

#define NWB_ASSETS_GRAPHICS_TEST_SKINNED_TRIANGLE_U16_PREFIX \
    NWB_ASSETS_GRAPHICS_TEST_SKINNED_GEOMETRY_TRIANGLE_PREFIX( \
        NWB_ASSETS_GRAPHICS_TEST_SKINNED_GEOMETRY_CLASS, \
        NWB_ASSETS_GRAPHICS_TEST_INDEX_TYPE_U16 \
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
    NWB_ASSETS_GRAPHICS_TEST_SKINNED_TRIANGLE_U16_PREFIX
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
#undef NWB_ASSETS_GRAPHICS_TEST_SKINNED_TRIANGLE_U16_PREFIX
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

static bool CookSingleGraphicsMeta(
    const AStringView metaText,
    const AStringView caseName,
    const char* assetDirectory,
    const char* assetFilename,
    TestArena& testArena,
    Path& outRoot,
    Path& outOutputDirectory
){
    outRoot = Path("__build_obj") / "nwb_assets_graphics_tests" / AssetsGraphicsTestConfigurationName() / AString(caseName);
    outOutputDirectory = outRoot / "cooked";

    if(!PrepareCleanDirectory(outRoot))
        return false;

    const Path assetRoot = outRoot / "assets";
    const Path metaPath = assetRoot / assetDirectory / assetFilename;
    if(!WriteTextFile(metaPath, metaText))
        return false;

    NWB::Core::Assets::AssetCookOptions options;
    options.repoRoot = ".";
    options.assetRoots.push_back(PathToString(assetRoot));
    options.outputDirectory = PathToString(outOutputDirectory);
    options.cacheDirectory = PathToString(outRoot / "cache");
    if(!options.configuration.assign("tests") || !options.assetType.assign("graphics"))
        return false;

    NWB::Impl::GraphicsAssetCooker cooker(testArena.arena);
    return cooker.cook(options);
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

template<typename AssetCodecT>
static bool LoadCookedAsset(
    TestContext& context,
    TestArena& testArena,
    const Path& outputDirectory,
    const Name assetName,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset){
    NWB::Core::Filesystem::VolumeSession volumeSession(testArena.arena);
    const bool loadedVolume = volumeSession.load("graphics", outputDirectory);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedVolume);
    if(!loadedVolume)
        return false;

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, volumeSession.fileCount() == 2u);

    NWB::Core::Assets::AssetBytes binary;
    const bool loadedBinary = volumeSession.loadData(assetName, binary);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedBinary);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !binary.empty());
    if(!loadedBinary || binary.empty())
        return false;

    AssetCodecT codec;
    const bool deserialized = codec.deserialize(assetName, binary, outLoadedAsset);
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
    const Path root = Path("__build_obj") / "nwb_assets_graphics_tests" / AssetsGraphicsTestConfigurationName() / "volume_scratch_bytes";
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
            Vector<u8, NWB::Core::Alloc::ScratchAllocator<u8>> payload{
                NWB::Core::Alloc::ScratchAllocator<u8>(scratchArena)
            };
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
                    NWB::Core::Assets::AssetBytes readback;
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

#endif

static NWB::Impl::SkinnedGeometry BuildValidSkinnedGeometry(){
    NWB::Impl::SkinnedGeometry geometry(Name("tests/characters/proxy_skinned_geometry"));

    Vector<NWB::Impl::SkinnedGeometryVertex> vertices;
    vertices.push_back(MakeRestVertex(-0.5f, -0.5f, 0.f, 0.f));
    vertices.push_back(MakeRestVertex(0.5f, -0.5f, 1.f, 0.f));
    vertices.push_back(MakeRestVertex(0.5f, 0.5f, 1.f, 1.f));
    vertices.push_back(MakeRestVertex(-0.5f, 0.5f, 0.f, 1.f));

    Vector<u32> indices = MakeQuadTriangleIndices();

    Vector<NWB::Impl::SkinInfluence4> skin;
    skin.resize(vertices.size());
    for(NWB::Impl::SkinInfluence4& influence : skin)
        influence = MakeRootSkin();

    Vector<NWB::Impl::SkinnedGeometryJointMatrix> inverseBindMatrices;
    inverseBindMatrices.push_back(MakeJointMatrix(-0.25f, 0.0f, 0.0f));

    geometry.setGeometryClass(NWB::Impl::GeometryClass::Skinned);
    geometry.setRestVertices(Move(vertices));
    geometry.setIndices(Move(indices));
    geometry.setSkin(Move(skin));
    geometry.setSkeletonJointCount(1u);
    geometry.setInverseBindMatrices(Move(inverseBindMatrices));
    return geometry;
}

static NWB::Impl::SkinnedGeometry BuildMinimalSkinnedGeometry(){
    NWB::Impl::SkinnedGeometry geometry(Name("tests/characters/minimal_skinned_geometry"));

    Vector<NWB::Impl::SkinnedGeometryVertex> vertices;
    vertices.push_back(MakeRestVertex(-0.5f, -0.5f, 0.f, 0.f));
    vertices.push_back(MakeRestVertex(0.5f, -0.5f, 1.f, 0.f));
    vertices.push_back(MakeRestVertex(0.f, 0.5f, 0.5f, 1.f));

    Vector<u32> indices = MakeTriangleIndices();

    Vector<NWB::Impl::SkinInfluence4> skin;
    skin.resize(vertices.size());
    for(NWB::Impl::SkinInfluence4& influence : skin)
        influence = MakeRootSkin();

    geometry.setGeometryClass(NWB::Impl::GeometryClass::Skinned);
    geometry.setRestVertices(Move(vertices));
    geometry.setIndices(Move(indices));
    geometry.setSkin(Move(skin));
    geometry.setSkeletonJointCount(1u);
    return geometry;
}

static NWB::Impl::Geometry BuildMinimalGeometry(){
    NWB::Impl::Geometry geometry(Name("tests/meshes/minimal_geometry"));

    Vector<Float3U> positions;
    positions.push_back(Float3U(-0.5f, -0.5f, 0.f));
    positions.push_back(Float3U(0.5f, -0.5f, 0.f));
    positions.push_back(Float3U(0.f, 0.5f, 0.f));

    Vector<Half4U> normals;
    normals.assign(positions.size(), NWB::Impl::MakeGeometryNormalStreamValue(Float3U(0.f, 0.f, 1.f)));

    Vector<Half4U> colors;
    colors.push_back(NWB::Impl::MakeGeometryColorStreamValue(Float4U(1.f, 0.f, 0.f, 1.f)));
    colors.push_back(NWB::Impl::MakeGeometryColorStreamValue(Float4U(0.f, 1.f, 0.f, 1.f)));
    colors.push_back(NWB::Impl::MakeGeometryColorStreamValue(Float4U(0.f, 0.f, 1.f, 1.f)));

    Vector<u32> indices = MakeTriangleIndices();

    geometry.setStreams(Move(positions), Move(normals), Move(colors));
    geometry.setIndices(Move(indices));
    return geometry;
}

template<typename AssetT, typename CodecT>
static const AssetT& CheckCodecRoundTrip(
    TestContext& context,
    const AssetT& asset,
    const CodecT& codec,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset){
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, asset.validatePayload());

    NWB::Core::Assets::AssetBytes binary;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, codec.serialize(asset, binary));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !binary.empty());

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, codec.deserialize(asset.virtualPath(), binary, outLoadedAsset));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, static_cast<bool>(outLoadedAsset));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, outLoadedAsset->assetType() == AssetT::AssetTypeName());
    return static_cast<const AssetT&>(*outLoadedAsset);
}

template<typename CodecT>
static void CheckCodecRejectsBinary(
    TestContext& context,
    const CodecT& codec,
    const Name& virtualPath,
    const NWB::Core::Assets::AssetBytes& binary){
    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !codec.deserialize(virtualPath, binary, loadedAsset));
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
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    auto asset = buildAsset();
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, asset.validatePayload());

    CodecT codec;
    NWB::Core::Assets::AssetBytes binary;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, codec.serialize(asset, binary));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, OverwriteU32(binary, sizeof(u32), unsupportedVersion));

    CheckCodecRejectsBinary(context, codec, asset.virtualPath(), binary);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 1u);
    const TString expectedError = StringFormat(NWB_TEXT("unsupported version {}"), unsupportedVersion);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(expectedError.c_str()));
#else
    static_cast<void>(context);
    static_cast<void>(buildAsset);
    static_cast<void>(unsupportedVersion);
#endif
}

static void TestGeometryCodecRoundTrip(TestContext& context){
    NWB::Impl::Geometry geometry = BuildMinimalGeometry();

    NWB::Impl::GeometryAssetCodec codec;
    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    const NWB::Impl::Geometry& loadedGeometry = CheckCodecRoundTrip(context, geometry, codec, loadedAsset);
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
    NWB::Impl::SkinnedGeometry geometry = BuildValidSkinnedGeometry();

    NWB::Impl::SkinnedGeometryAssetCodec codec;
    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    const NWB::Impl::SkinnedGeometry& loadedGeometry = CheckCodecRoundTrip(context, geometry, codec, loadedAsset);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.virtualPath() == geometry.virtualPath());
    CheckSkinnedSkinnedGeometryPayload(context, loadedGeometry, 1u, 1u, 0u);
}

static void TestMinimalSkinnedGeometryCodecRoundTrip(TestContext& context){
    NWB::Impl::SkinnedGeometry geometry = BuildMinimalSkinnedGeometry();

    NWB::Impl::SkinnedGeometryAssetCodec codec;
    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    const NWB::Impl::SkinnedGeometry& loadedGeometry = CheckCodecRoundTrip(context, geometry, codec, loadedAsset);
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
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    NWB::Impl::SkinnedGeometry geometry = BuildMinimalSkinnedGeometry();
    NWB::Impl::SkinnedGeometryAssetCodec codec;
    NWB::Core::Assets::AssetBytes binary;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, codec.serialize(geometry, binary));

    const usize skeletonJointCountOffset = SkinnedGeometryHeaderCountOffset(3u);
    const u64 invalidJointCount = static_cast<u64>(Limit<u32>::s_Max) + 1ull;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, OverwriteU64(binary, skeletonJointCountOffset, invalidJointCount));

    CheckCodecRejectsBinary(context, codec, geometry.virtualPath(), binary);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 1u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("payload counts exceed u32 limits")));
#else
    static_cast<void>(context);
#endif
}

static void TestSkinnedGeometryCodecRejectsMalformedDependentCounts(TestContext& context){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    NWB::Impl::SkinnedGeometry geometry = BuildValidSkinnedGeometry();
    NWB::Impl::SkinnedGeometryAssetCodec codec;
    NWB::Core::Assets::AssetBytes binary;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, codec.serialize(geometry, binary));

    {
        NWB::Core::Assets::AssetBytes malformed = binary;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, OverwriteU64(
            malformed,
            SkinnedGeometryHeaderCountOffset(2u),
            static_cast<u64>(geometry.restVertices().size() - 1u)
        ));

        CheckCodecRejectsBinary(context, codec, geometry.virtualPath(), malformed);
    }

    {
        NWB::Core::Assets::AssetBytes malformed = binary;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, OverwriteU64(
            malformed,
            SkinnedGeometryHeaderCountOffset(4u),
            static_cast<u64>(geometry.restVertices().size() - 1u)
        ));

        CheckCodecRejectsBinary(context, codec, geometry.virtualPath(), malformed);
    }

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 2u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("skin count must be empty or match vertex count")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("inverse bind matrix count must be empty or match skeleton joint count")));
#else
    static_cast<void>(context);
#endif
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
    NWB::Impl::SkinnedGeometry geometry = BuildValidSkinnedGeometry();
    mutate(geometry);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
}

static void TestSkinnedGeometryValidationFailures(TestContext& context){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    CheckInvalidSkinnedGeometry(context, [](NWB::Impl::SkinnedGeometry& geometry){
        Vector<NWB::Impl::SkinnedGeometryVertex> vertices = geometry.restVertices();
        NWB::Impl::StoreSkinnedGeometryVertexNormal(vertices[0], Float3U(0.f, 0.f, 0.f));
        geometry.setRestVertices(Move(vertices));
    });

    CheckInvalidSkinnedGeometry(context, [](NWB::Impl::SkinnedGeometry& geometry){
        Vector<NWB::Impl::SkinnedGeometryVertex> vertices = geometry.restVertices();
        NWB::Impl::StoreSkinnedGeometryVertexTangent(vertices[0], Float4U(1.f, 0.f, 0.f, 0.f));
        geometry.setRestVertices(Move(vertices));
    });

    CheckInvalidSkinnedGeometry(context, [](NWB::Impl::SkinnedGeometry& geometry){
        Vector<NWB::Impl::SkinnedGeometryVertex> vertices = geometry.restVertices();
        NWB::Impl::StoreSkinnedGeometryVertexTangent(vertices[0], Float4U(0.f, 0.f, 1.f, 1.f));
        geometry.setRestVertices(Move(vertices));
    });

    CheckInvalidSkinnedGeometry(context, [](NWB::Impl::SkinnedGeometry& geometry){
        Vector<NWB::Impl::SkinnedGeometryVertex> vertices = geometry.restVertices();
        NWB::Impl::StoreSkinnedGeometryVertexTangent(vertices[0], Float4U(1.f, 0.f, 0.f, 2.f));
        geometry.setRestVertices(Move(vertices));
    });

    CheckInvalidSkinnedGeometry(context, [](NWB::Impl::SkinnedGeometry& geometry){
        Vector<NWB::Impl::SkinnedGeometryVertex> vertices = geometry.restVertices();
        NWB::Impl::StoreSkinnedGeometryVertexNormal(vertices[0], Float3U(0.f, 0.f, 2.f));
        geometry.setRestVertices(Move(vertices));
    });

    CheckInvalidSkinnedGeometry(context, [](NWB::Impl::SkinnedGeometry& geometry){
        Vector<NWB::Impl::SkinnedGeometryVertex> vertices = geometry.restVertices();
        NWB::Impl::StoreSkinnedGeometryVertexTangent(vertices[0], Float4U(0.70710677f, 0.f, 0.70710677f, 1.f));
        geometry.setRestVertices(Move(vertices));
    });

    CheckInvalidSkinnedGeometry(context, [](NWB::Impl::SkinnedGeometry& geometry){
        Vector<NWB::Impl::SkinInfluence4> skin = geometry.skin();
        skin[0].weight[0] = 0.5f;
        geometry.setSkin(Move(skin));
    });

    CheckInvalidSkinnedGeometry(context, [](NWB::Impl::SkinnedGeometry& geometry){
        Vector<NWB::Impl::SkinInfluence4> skin = geometry.skin();
        skin.pop_back();
        geometry.setSkin(Move(skin));
    });

    CheckInvalidSkinnedGeometry(context, [](NWB::Impl::SkinnedGeometry& geometry){
        geometry.setSkeletonJointCount(0u);
    });

    CheckInvalidSkinnedGeometry(context, [](NWB::Impl::SkinnedGeometry& geometry){
        Vector<NWB::Impl::SkinnedGeometryJointMatrix> inverseBindMatrices = geometry.inverseBindMatrices();
        inverseBindMatrices.push_back(MakeJointMatrix(0.0f, 0.0f, 0.0f));
        geometry.setInverseBindMatrices(Move(inverseBindMatrices));
    });

    CheckInvalidSkinnedGeometry(context, [](NWB::Impl::SkinnedGeometry& geometry){
        Vector<NWB::Impl::SkinnedGeometryJointMatrix> inverseBindMatrices = geometry.inverseBindMatrices();
        inverseBindMatrices[0u].rows[3].w = 0.0f;
        geometry.setInverseBindMatrices(Move(inverseBindMatrices));
    });

    CheckInvalidSkinnedGeometry(context, [](NWB::Impl::SkinnedGeometry& geometry){
        Vector<NWB::Impl::SkinInfluence4> skin = geometry.skin();
        skin[0].joint[0] = 1u;
        geometry.setSkin(Move(skin));
    });

    CheckInvalidSkinnedGeometry(context, [](NWB::Impl::SkinnedGeometry& geometry){
        Vector<u32> indices = geometry.indices();
        indices.pop_back();
        geometry.setIndices(Move(indices));
    });

    CheckInvalidSkinnedGeometry(context, [](NWB::Impl::SkinnedGeometry& geometry){
        Vector<u32> indices = geometry.indices();
        indices[2] = 99u;
        geometry.setIndices(Move(indices));
    });

    CheckInvalidSkinnedGeometry(context, [](NWB::Impl::SkinnedGeometry& geometry){
        Vector<u32> indices = geometry.indices();
        indices[2] = indices[1];
        geometry.setIndices(Move(indices));
    });

    CheckInvalidSkinnedGeometry(context, [](NWB::Impl::SkinnedGeometry& geometry){
        Vector<NWB::Impl::SkinnedGeometryVertex> vertices = geometry.restVertices();
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

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, GeometryClassAcceptsSkinnedGeometryPayload(GeometryClass::Static, false));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !GeometryClassAcceptsSkinnedGeometryPayload(GeometryClass::Static, true));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, GeometryClassAcceptsSkinnedGeometryPayload(GeometryClass::Skinned, false));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, GeometryClassAcceptsSkinnedGeometryPayload(GeometryClass::Skinned, true));
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
    __hidden_assets_graphics_tests::TestGeometryCookerTypedStreams(context);
    __hidden_assets_graphics_tests::TestGeometryCookerDefaultColors(context);
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

