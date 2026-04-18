// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_graphics/deformable_geometry_asset.h>
#include <impl/assets_graphics/shader_asset_cooker.h>

#include <core/common/common.h>
#include <core/filesystem/filesystem.h>

#include <global/compile.h>

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TestContext{
    u32 passed = 0;
    u32 failed = 0;

    void checkTrue(const bool condition, const char* expression, const char* file, const int line){
        if(condition){
            ++passed;
            return;
        }

        ++failed;
        NWB_CERR << file << '(' << line << "): check failed: " << expression << '\n';
    }
};


#define NWB_ASSETS_GRAPHICS_TEST_CHECK(context, expression) (context).checkTrue((expression), #expression, __FILE__, __LINE__)


static void* AssetsGraphicsTestAlloc(usize size){
    return NWB::Core::Alloc::CoreAlloc(size, "NWB::Tests::AssetsGraphics::Alloc");
}

static void AssetsGraphicsTestFree(void* ptr){
    NWB::Core::Alloc::CoreFree(ptr, "NWB::Tests::AssetsGraphics::Free");
}

static void* AssetsGraphicsTestAllocAligned(usize size, usize align){
    return NWB::Core::Alloc::CoreAllocAligned(size, align, "NWB::Tests::AssetsGraphics::AllocAligned");
}

static void AssetsGraphicsTestFreeAligned(void* ptr){
    NWB::Core::Alloc::CoreFreeAligned(ptr, "NWB::Tests::AssetsGraphics::FreeAligned");
}

struct TestArena{
    NWB::Core::Alloc::CustomArena arena;

    TestArena()
        : arena(
            &AssetsGraphicsTestAlloc,
            &AssetsGraphicsTestFree,
            &AssetsGraphicsTestAllocAligned,
            &AssetsGraphicsTestFreeAligned
        )
    {}
};


class CapturingLogger final : public NWB::Log::IClient{
public:
    virtual void enqueue(TString&& str, NWB::Log::Type::Enum type = NWB::Log::Type::Info)override{
        record(str, type);
    }
    virtual void enqueue(const TString& str, NWB::Log::Type::Enum type = NWB::Log::Type::Info)override{
        record(str, type);
    }

    [[nodiscard]] u32 errorCount()const{ return m_errorCount; }
    [[nodiscard]] bool sawErrorContaining(const tchar* text)const{
        for(const TString& error : m_errors){
            if(error.find(text) != TString::npos)
                return true;
        }
        return false;
    }

private:
    void record(const TString& str, const NWB::Log::Type::Enum type){
        if(type == NWB::Log::Type::Error){
            ++m_errorCount;
            m_errors.push_back(str);
        }
    }

private:
    u32 m_errorCount = 0;
    Vector<TString> m_errors;
};


static constexpr AStringView s_MinimalDeformableMeta = R"(deformable_geometry asset;

asset.index_type = "u16";

asset.positions = [
    [-0.5, -0.5, 0.0],
    [ 0.5, -0.5, 0.0],
    [ 0.0,  0.5, 0.0],
];

asset.normals = [
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
];

asset.tangents = [
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
];

asset.uv0 = [
    [0.0, 0.0],
    [1.0, 0.0],
    [0.5, 1.0],
];

asset.indices = [
    [0, 1, 2],
];

asset.source_samples = {};
asset.skin = {};
asset.morphs = {};
)";

static constexpr AStringView s_U32IndexTypeDeformableMeta = R"(deformable_geometry asset;

asset.index_type = "u32";

asset.positions = [
    [-0.5, -0.5, 0.0],
    [ 0.5, -0.5, 0.0],
    [ 0.0,  0.5, 0.0],
];

asset.normals = [
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
];

asset.tangents = [
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
];

asset.uv0 = [
    [0.0, 0.0],
    [1.0, 0.0],
    [0.5, 1.0],
];

asset.indices = [
    [0, 1, 2],
];

asset.skin = {};
asset.morphs = {};
)";

static constexpr AStringView s_EmptyListOptionalDeformableMeta = R"(deformable_geometry asset;

asset.index_type = "u16";

asset.positions = [
    [-0.5, -0.5, 0.0],
    [ 0.5, -0.5, 0.0],
    [ 0.0,  0.5, 0.0],
];

asset.normals = [
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
];

asset.tangents = [
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
];

asset.uv0 = [
    [0.0, 0.0],
    [1.0, 0.0],
    [0.5, 1.0],
];

asset.indices = [
    [0, 1, 2],
];

asset.colors = [];
asset.source_samples = [];
asset.skin = [];
asset.morphs = [];
)";

static constexpr AStringView s_EmptyMapOptionalDeformableMeta = R"(deformable_geometry asset;

asset.index_type = "u16";

asset.positions = [
    [-0.5, -0.5, 0.0],
    [ 0.5, -0.5, 0.0],
    [ 0.0,  0.5, 0.0],
];

asset.normals = [
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
];

asset.tangents = [
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
];

asset.uv0 = [
    [0.0, 0.0],
    [1.0, 0.0],
    [0.5, 1.0],
];

asset.indices = [
    [0, 1, 2],
];

asset.colors = {};
asset.source_samples = {};
asset.skin = {};
asset.morphs = {};
)";

static constexpr AStringView s_FullDeformableMeta = R"(deformable_geometry asset;

asset.index_type = "u16";

asset.positions = [
    [-0.5, -0.5, 0.0],
    [ 0.5, -0.5, 0.0],
    [ 0.5,  0.5, 0.0],
    [-0.5,  0.5, 0.0],
];

asset.normals = [
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
];

asset.tangents = [
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
];

asset.uv0 = [
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

asset.source_samples = {
    "source_tri": [0, 0, 0, 1],
    "bary": [
        [1.0, 0.0, 0.0],
        [0.0, 1.0, 0.0],
        [0.0, 0.0, 1.0],
        [0.0, 0.0, 1.0],
    ],
};

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

asset.displacement = {
    "space": "tangent",
    "mode": "scalar",
    "field": "uv_ramp",
    "amplitude": 0.125,
};

asset.morphs = {
    "lift": {
        "vertex_ids": [1, 2],
        "delta_position": [
            [0.0, 0.0, 0.25],
            [0.0, 0.0, 0.5],
        ],
        "delta_normal": [
            [0.0, 0.0, 0.0],
            [0.0, 0.0, 0.0],
        ],
        "delta_tangent": [
            [0.0, 0.0, 0.0, 0.0],
            [0.0, 0.0, 0.0, 0.0],
        ],
    },
};
)";

#if defined(NWB_FINAL)
static constexpr AStringView s_MismatchedDeformableMeta = R"(deformable_geometry asset;

asset.index_type = "u16";

asset.positions = [
    [-0.5, -0.5, 0.0],
    [ 0.5, -0.5, 0.0],
    [ 0.0,  0.5, 0.0],
];

asset.normals = [
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
];

asset.tangents = [
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
];

asset.uv0 = [
    [0.0, 0.0],
    [1.0, 0.0],
    [0.5, 1.0],
];

asset.indices = [
    [0, 1, 2],
];
)";

static constexpr AStringView s_MissingIndexTypeDeformableMeta = R"(deformable_geometry asset;

asset.positions = [
    [-0.5, -0.5, 0.0],
    [ 0.5, -0.5, 0.0],
    [ 0.0,  0.5, 0.0],
];

asset.normals = [
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
];

asset.tangents = [
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
];

asset.uv0 = [
    [0.0, 0.0],
    [1.0, 0.0],
    [0.5, 1.0],
];

asset.indices = [
    [0, 1, 2],
];
)";

static constexpr AStringView s_MismatchedSkinDeformableMeta = R"(deformable_geometry asset;

asset.index_type = "u16";

asset.positions = [
    [-0.5, -0.5, 0.0],
    [ 0.5, -0.5, 0.0],
    [ 0.0,  0.5, 0.0],
];

asset.normals = [
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
];

asset.tangents = [
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
];

asset.uv0 = [
    [0.0, 0.0],
    [1.0, 0.0],
    [0.5, 1.0],
];

asset.indices = [
    [0, 1, 2],
];

asset.skin = {
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

static constexpr AStringView s_MismatchedSourceSamplesDeformableMeta = R"(deformable_geometry asset;

asset.index_type = "u16";

asset.positions = [
    [-0.5, -0.5, 0.0],
    [ 0.5, -0.5, 0.0],
    [ 0.0,  0.5, 0.0],
];

asset.normals = [
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
];

asset.tangents = [
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
];

asset.uv0 = [
    [0.0, 0.0],
    [1.0, 0.0],
    [0.5, 1.0],
];

asset.indices = [
    [0, 1, 2],
];

asset.source_samples = {
    "source_tri": [0, 0, 0],
    "bary": [
        [1.0, 0.0, 0.0],
        [0.0, 1.0, 0.0],
    ],
};
)";

static constexpr AStringView s_MismatchedMorphDeformableMeta = R"(deformable_geometry asset;

asset.index_type = "u16";

asset.positions = [
    [-0.5, -0.5, 0.0],
    [ 0.5, -0.5, 0.0],
    [ 0.0,  0.5, 0.0],
];

asset.normals = [
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
];

asset.tangents = [
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
];

asset.uv0 = [
    [0.0, 0.0],
    [1.0, 0.0],
    [0.5, 1.0],
];

asset.indices = [
    [0, 1, 2],
];

asset.morphs = {
    "lift": {
        "vertex_ids": [1, 2],
        "delta_position": [
            [0.0, 0.0, 0.25],
        ],
        "delta_normal": [
            [0.0, 0.0, 0.0],
            [0.0, 0.0, 0.0],
        ],
        "delta_tangent": [
            [0.0, 0.0, 0.0, 0.0],
            [0.0, 0.0, 0.0, 0.0],
        ],
    },
};
)";

static constexpr AStringView s_MissingMorphTangentDeformableMeta = R"(deformable_geometry asset;

asset.index_type = "u16";

asset.positions = [
    [-0.5, -0.5, 0.0],
    [ 0.5, -0.5, 0.0],
    [ 0.0,  0.5, 0.0],
];

asset.normals = [
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
];

asset.tangents = [
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
];

asset.uv0 = [
    [0.0, 0.0],
    [1.0, 0.0],
    [0.5, 1.0],
];

asset.indices = [
    [0, 1, 2],
];

asset.morphs = {
    "lift": {
        "vertex_ids": [1, 2],
        "delta_position": [
            [0.0, 0.0, 0.25],
            [0.0, 0.0, 0.5],
        ],
        "delta_normal": [
            [0.0, 0.0, 0.0],
            [0.0, 0.0, 0.0],
        ],
    },
};
)";
#endif


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

static bool CookSingleDeformableMeta(
    const AStringView metaText,
    const AStringView caseName,
    TestArena& testArena,
    Path& outRoot,
    Path& outOutputDirectory
){
    outRoot = Path("__build_obj") / "nwb_assets_graphics_tests" / AssetsGraphicsTestConfigurationName() / AString(caseName);
    outOutputDirectory = outRoot / "cooked";

    if(!PrepareCleanDirectory(outRoot))
        return false;

    const Path assetRoot = outRoot / "assets";
    const Path metaPath = assetRoot / "characters" / "minimal_deformable.nwb";
    if(!WriteTextFile(metaPath, metaText))
        return false;

    NWB::Core::Assets::AssetCookOptions options;
    options.repoRoot = ".";
    options.assetRoots.push_back(PathToString(assetRoot));
    options.outputDirectory = PathToString(outOutputDirectory);
    options.cacheDirectory = PathToString(outRoot / "cache");
    if(!options.configuration.assign("tests") || !options.assetType.assign("shader"))
        return false;

    NWB::Impl::ShaderAssetCooker cooker(testArena.arena);
    return cooker.cook(options);
}


static NWB::Impl::DeformableVertexRest MakeRestVertex(
    const f32 x,
    const f32 y,
    const f32 u,
    const f32 v
){
    NWB::Impl::DeformableVertexRest vertex;
    vertex.position = Float3Data(x, y, 0.f);
    vertex.normal = Float3Data(0.f, 0.f, 1.f);
    vertex.tangent = Float4Data(1.f, 0.f, 0.f, 1.f);
    vertex.uv0 = Float2Data(u, v);
    vertex.color0 = Float4Data(1.f, 1.f, 1.f, 1.f);
    return vertex;
}

static NWB::Impl::SkinInfluence4 MakeRootSkin(){
    NWB::Impl::SkinInfluence4 skin;
    skin.joint[0] = 0u;
    skin.weight[0] = 1.f;
    return skin;
}

static NWB::Impl::SourceSample MakeSourceSample(const u32 sourceTri, const f32 a, const f32 b, const f32 c){
    NWB::Impl::SourceSample sample;
    sample.sourceTri = sourceTri;
    sample.bary[0] = a;
    sample.bary[1] = b;
    sample.bary[2] = c;
    return sample;
}

static NWB::Impl::DeformableMorphDelta MakeMorphDelta(const u32 vertexId, const f32 zDelta){
    NWB::Impl::DeformableMorphDelta delta;
    delta.vertexId = vertexId;
    delta.deltaPosition = Float3Data(0.f, 0.f, zDelta);
    delta.deltaNormal = Float3Data(0.f, 0.f, 0.f);
    delta.deltaTangent = Float4Data(0.f, 0.f, 0.f, 0.f);
    return delta;
}

#if defined(NWB_FINAL)
static bool OverwriteU64(NWB::Core::Assets::AssetBytes& binary, const usize offset, const u64 value){
    if(offset > binary.size() || sizeof(value) > binary.size() - offset)
        return false;

    NWB_MEMCPY(binary.data() + offset, sizeof(value), &value, sizeof(value));
    return true;
}

static usize DeformableHeaderCountOffset(const usize countIndex){
    return (sizeof(u32) * 2u) + (sizeof(u64) * countIndex);
}

static usize DeformableMorphDeltaCountOffset(const NWB::Impl::DeformableGeometry& geometry){
    return (sizeof(u32) * 2u)
        + (sizeof(u64) * 5u)
        + (geometry.restVertices().size() * sizeof(NWB::Impl::DeformableVertexRest))
        + (geometry.indices().size() * sizeof(u32))
        + (geometry.skin().size() * sizeof(NWB::Impl::SkinInfluence4))
        + (geometry.sourceSamples().size() * sizeof(NWB::Impl::SourceSample))
        + sizeof(NameHash)
    ;
}
#endif

static NWB::Impl::DeformableGeometry BuildValidDeformableGeometry(){
    NWB::Impl::DeformableGeometry geometry(Name("tests/characters/proxy_deformable"));

    Vector<NWB::Impl::DeformableVertexRest> vertices;
    vertices.push_back(MakeRestVertex(-0.5f, -0.5f, 0.f, 0.f));
    vertices.push_back(MakeRestVertex(0.5f, -0.5f, 1.f, 0.f));
    vertices.push_back(MakeRestVertex(0.5f, 0.5f, 1.f, 1.f));
    vertices.push_back(MakeRestVertex(-0.5f, 0.5f, 0.f, 1.f));

    Vector<u32> indices;
    indices.push_back(0u);
    indices.push_back(1u);
    indices.push_back(2u);
    indices.push_back(0u);
    indices.push_back(2u);
    indices.push_back(3u);

    Vector<NWB::Impl::SkinInfluence4> skin;
    skin.resize(vertices.size());
    for(NWB::Impl::SkinInfluence4& influence : skin)
        influence = MakeRootSkin();

    Vector<NWB::Impl::SourceSample> sourceSamples;
    sourceSamples.push_back(MakeSourceSample(0u, 1.f, 0.f, 0.f));
    sourceSamples.push_back(MakeSourceSample(0u, 0.f, 1.f, 0.f));
    sourceSamples.push_back(MakeSourceSample(0u, 0.f, 0.f, 1.f));
    sourceSamples.push_back(MakeSourceSample(1u, 0.f, 0.f, 1.f));

    Vector<NWB::Impl::DeformableMorph> morphs;
    morphs.resize(1u);
    morphs[0].name = Name("lift");
    morphs[0].deltas.push_back(MakeMorphDelta(1u, 0.25f));
    morphs[0].deltas.push_back(MakeMorphDelta(2u, 0.5f));

    NWB::Impl::DeformableDisplacement displacement;
    displacement.mode = NWB::Impl::DeformableDisplacementMode::ScalarUvRamp;
    displacement.amplitude = 0.125f;

    geometry.setRestVertices(Move(vertices));
    geometry.setIndices(Move(indices));
    geometry.setSkin(Move(skin));
    geometry.setSourceSamples(Move(sourceSamples));
    geometry.setDisplacement(displacement);
    geometry.setMorphs(Move(morphs));
    return geometry;
}

static NWB::Impl::DeformableGeometry BuildMinimalDeformableGeometry(){
    NWB::Impl::DeformableGeometry geometry(Name("tests/characters/minimal_deformable"));

    Vector<NWB::Impl::DeformableVertexRest> vertices;
    vertices.push_back(MakeRestVertex(-0.5f, -0.5f, 0.f, 0.f));
    vertices.push_back(MakeRestVertex(0.5f, -0.5f, 1.f, 0.f));
    vertices.push_back(MakeRestVertex(0.f, 0.5f, 0.5f, 1.f));

    Vector<u32> indices;
    indices.push_back(0u);
    indices.push_back(1u);
    indices.push_back(2u);

    geometry.setRestVertices(Move(vertices));
    geometry.setIndices(Move(indices));
    return geometry;
}

static void TestDeformableGeometryCodecRoundTrip(TestContext& context){
    NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, geometry.validatePayload());

    NWB::Impl::DeformableGeometryAssetCodec codec;
    NWB::Core::Assets::AssetBytes binary;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, codec.serialize(geometry, binary));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !binary.empty());

    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, codec.deserialize(geometry.virtualPath(), binary, loadedAsset));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, static_cast<bool>(loadedAsset));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedAsset->assetType() == NWB::Impl::DeformableGeometry::AssetTypeName());

    const NWB::Impl::DeformableGeometry& loadedGeometry =
        static_cast<const NWB::Impl::DeformableGeometry&>(*loadedAsset)
    ;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.virtualPath() == geometry.virtualPath());
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.restVertices().size() == 4u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.indices().size() == 6u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.skin().size() == 4u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.sourceSamples().size() == 4u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(
        context,
        loadedGeometry.displacement().mode == NWB::Impl::DeformableDisplacementMode::ScalarUvRamp
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.displacement().amplitude == 0.125f);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.morphs().size() == 1u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.morphs()[0].name == Name("lift"));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.morphs()[0].deltas.size() == 2u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.morphs()[0].deltas[1].vertexId == 2u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.morphs()[0].deltas[1].deltaPosition.z == 0.5f);
}

static void TestMinimalDeformableGeometryCodecRoundTrip(TestContext& context){
    NWB::Impl::DeformableGeometry geometry = BuildMinimalDeformableGeometry();
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, geometry.validatePayload());

    NWB::Impl::DeformableGeometryAssetCodec codec;
    NWB::Core::Assets::AssetBytes binary;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, codec.serialize(geometry, binary));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !binary.empty());

    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, codec.deserialize(geometry.virtualPath(), binary, loadedAsset));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, static_cast<bool>(loadedAsset));

    const NWB::Impl::DeformableGeometry& loadedGeometry =
        static_cast<const NWB::Impl::DeformableGeometry&>(*loadedAsset)
    ;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.restVertices().size() == 3u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.indices().size() == 3u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.skin().empty());
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.sourceSamples().empty());
    NWB_ASSETS_GRAPHICS_TEST_CHECK(
        context,
        loadedGeometry.displacement().mode == NWB::Impl::DeformableDisplacementMode::None
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.morphs().empty());
}

static void TestDeformableGeometryCodecRejectsMalformedCounts(TestContext& context){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB_LOGGER_REGISTER(&logger);

    NWB::Impl::DeformableGeometry geometry = BuildMinimalDeformableGeometry();
    NWB::Impl::DeformableGeometryAssetCodec codec;
    NWB::Core::Assets::AssetBytes binary;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, codec.serialize(geometry, binary));

    const usize morphCountOffset = DeformableHeaderCountOffset(4u);
    const u64 invalidMorphCount = static_cast<u64>(Limit<u32>::s_Max) + 1ull;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, OverwriteU64(binary, morphCountOffset, invalidMorphCount));

    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !codec.deserialize(geometry.virtualPath(), binary, loadedAsset));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !loadedAsset);

    NWB_LOGGER_REGISTER(nullptr);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 1u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("payload counts exceed u32 limits")));
#else
    (void)context;
#endif
}

static void TestDeformableGeometryCodecRejectsMalformedDependentCounts(TestContext& context){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB_LOGGER_REGISTER(&logger);

    NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
    NWB::Impl::DeformableGeometryAssetCodec codec;
    NWB::Core::Assets::AssetBytes binary;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, codec.serialize(geometry, binary));

    {
        NWB::Core::Assets::AssetBytes malformed = binary;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, OverwriteU64(
            malformed,
            DeformableHeaderCountOffset(2u),
            static_cast<u64>(geometry.restVertices().size() - 1u)
        ));

        UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !codec.deserialize(geometry.virtualPath(), malformed, loadedAsset));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !loadedAsset);
    }

    {
        NWB::Core::Assets::AssetBytes malformed = binary;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, OverwriteU64(
            malformed,
            DeformableHeaderCountOffset(3u),
            static_cast<u64>(geometry.restVertices().size() - 1u)
        ));

        UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !codec.deserialize(geometry.virtualPath(), malformed, loadedAsset));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !loadedAsset);
    }

    {
        NWB::Core::Assets::AssetBytes malformed = binary;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, OverwriteU64(
            malformed,
            DeformableMorphDeltaCountOffset(geometry),
            static_cast<u64>(Limit<u32>::s_Max) + 1ull
        ));

        UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !codec.deserialize(geometry.virtualPath(), malformed, loadedAsset));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !loadedAsset);
    }

    NWB_LOGGER_REGISTER(nullptr);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 3u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("skin count must be empty or match vertex count")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("source sample count must be empty or match vertex count")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("morph delta count exceeds u32 limits")));
#else
    (void)context;
#endif
}

static void TestDeformableGeometryCookerMinimalAsset(TestContext& context){
    CapturingLogger logger;
    NWB_LOGGER_REGISTER(&logger);

    TestArena testArena;
    Path root;
    Path outputDirectory;
    const bool cooked = CookSingleDeformableMeta(
        s_MinimalDeformableMeta,
        "minimal",
        testArena,
        root,
        outputDirectory
    );

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, cooked);
    if(!cooked){
        ErrorCode errorCode;
        (void)RemoveAllIfExists(root, errorCode);
        NWB_LOGGER_REGISTER(nullptr);
        return;
    }

    {
        NWB::Core::Filesystem::VolumeSession volumeSession(testArena.arena);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, volumeSession.load("graphics", outputDirectory));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, volumeSession.fileCount() == 2u);

        NWB::Core::Assets::AssetBytes binary;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(
            context,
            volumeSession.loadData(Name("project/characters/minimal_deformable"), binary)
        );
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !binary.empty());

        NWB::Impl::DeformableGeometryAssetCodec codec;
        UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(
            context,
            codec.deserialize(Name("project/characters/minimal_deformable"), binary, loadedAsset)
        );
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, static_cast<bool>(loadedAsset));

        const NWB::Impl::DeformableGeometry& loadedGeometry =
            static_cast<const NWB::Impl::DeformableGeometry&>(*loadedAsset)
        ;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.restVertices().size() == 3u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.indices().size() == 3u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.restVertices()[0].color0.x == 1.f);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.restVertices()[0].color0.w == 1.f);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.skin().empty());
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.sourceSamples().empty());
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.morphs().empty());
    }

    ErrorCode errorCode;
    (void)RemoveAllIfExists(root, errorCode);
    NWB_LOGGER_REGISTER(nullptr);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);
}

static void TestDeformableGeometryCookerU32IndexType(TestContext& context){
    CapturingLogger logger;
    NWB_LOGGER_REGISTER(&logger);

    TestArena testArena;
    Path root;
    Path outputDirectory;
    const bool cooked = CookSingleDeformableMeta(
        s_U32IndexTypeDeformableMeta,
        "u32_index_type",
        testArena,
        root,
        outputDirectory
    );

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, cooked);
    if(!cooked){
        ErrorCode errorCode;
        (void)RemoveAllIfExists(root, errorCode);
        NWB_LOGGER_REGISTER(nullptr);
        return;
    }

    {
        NWB::Core::Filesystem::VolumeSession volumeSession(testArena.arena);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, volumeSession.load("graphics", outputDirectory));

        NWB::Core::Assets::AssetBytes binary;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(
            context,
            volumeSession.loadData(Name("project/characters/minimal_deformable"), binary)
        );

        NWB::Impl::DeformableGeometryAssetCodec codec;
        UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(
            context,
            codec.deserialize(Name("project/characters/minimal_deformable"), binary, loadedAsset)
        );
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, static_cast<bool>(loadedAsset));

        const NWB::Impl::DeformableGeometry& loadedGeometry =
            static_cast<const NWB::Impl::DeformableGeometry&>(*loadedAsset)
        ;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.indices().size() == 3u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.indices()[2] == 2u);
    }

    ErrorCode errorCode;
    (void)RemoveAllIfExists(root, errorCode);
    NWB_LOGGER_REGISTER(nullptr);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);
}

static void TestDeformableGeometryCookerExplicitEmptyOptionalLists(TestContext& context){
    CapturingLogger logger;
    NWB_LOGGER_REGISTER(&logger);

    TestArena testArena;
    auto expectCookedDefaultOptionals = [&](const AStringView metaText, const AStringView caseName){
        Path root;
        Path outputDirectory;
        const bool cooked = CookSingleDeformableMeta(
            metaText,
            caseName,
            testArena,
            root,
            outputDirectory
        );

        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, cooked);
        if(!cooked){
            ErrorCode errorCode;
            (void)RemoveAllIfExists(root, errorCode);
            return;
        }

        {
            NWB::Core::Filesystem::VolumeSession volumeSession(testArena.arena);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, volumeSession.load("graphics", outputDirectory));
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, volumeSession.fileCount() == 2u);

            NWB::Core::Assets::AssetBytes binary;
            NWB_ASSETS_GRAPHICS_TEST_CHECK(
                context,
                volumeSession.loadData(Name("project/characters/minimal_deformable"), binary)
            );
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !binary.empty());

            NWB::Impl::DeformableGeometryAssetCodec codec;
            UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
            NWB_ASSETS_GRAPHICS_TEST_CHECK(
                context,
                codec.deserialize(Name("project/characters/minimal_deformable"), binary, loadedAsset)
            );
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, static_cast<bool>(loadedAsset));

            const NWB::Impl::DeformableGeometry& loadedGeometry =
                static_cast<const NWB::Impl::DeformableGeometry&>(*loadedAsset)
            ;
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.restVertices().size() == 3u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.indices().size() == 3u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.restVertices()[0].color0.x == 1.f);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.restVertices()[0].color0.w == 1.f);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.skin().empty());
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.sourceSamples().empty());
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.morphs().empty());
        }

        ErrorCode errorCode;
        (void)RemoveAllIfExists(root, errorCode);
    };

    expectCookedDefaultOptionals(s_EmptyListOptionalDeformableMeta, "empty_optional_lists");
    expectCookedDefaultOptionals(s_EmptyMapOptionalDeformableMeta, "empty_optional_maps");

    NWB_LOGGER_REGISTER(nullptr);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);
}

static void TestDeformableGeometryCookerFullAsset(TestContext& context){
    CapturingLogger logger;
    NWB_LOGGER_REGISTER(&logger);

    TestArena testArena;
    Path root;
    Path outputDirectory;
    const bool cooked = CookSingleDeformableMeta(
        s_FullDeformableMeta,
        "full_streams",
        testArena,
        root,
        outputDirectory
    );

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, cooked);
    if(!cooked){
        ErrorCode errorCode;
        (void)RemoveAllIfExists(root, errorCode);
        NWB_LOGGER_REGISTER(nullptr);
        return;
    }

    {
        NWB::Core::Filesystem::VolumeSession volumeSession(testArena.arena);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, volumeSession.load("graphics", outputDirectory));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, volumeSession.fileCount() == 2u);

        NWB::Core::Assets::AssetBytes binary;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(
            context,
            volumeSession.loadData(Name("project/characters/minimal_deformable"), binary)
        );
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !binary.empty());

        NWB::Impl::DeformableGeometryAssetCodec codec;
        UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(
            context,
            codec.deserialize(Name("project/characters/minimal_deformable"), binary, loadedAsset)
        );
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, static_cast<bool>(loadedAsset));

        const NWB::Impl::DeformableGeometry& loadedGeometry =
            static_cast<const NWB::Impl::DeformableGeometry&>(*loadedAsset)
        ;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.restVertices().size() == 4u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.indices().size() == 6u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.restVertices()[3].color0.w == 0.5f);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.skin().size() == 4u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.skin()[1].joint[1] == 1u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.skin()[1].weight[0] == 0.75f);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.sourceSamples().size() == 4u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.sourceSamples()[3].sourceTri == 1u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.sourceSamples()[3].bary[2] == 1.f);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(
            context,
            loadedGeometry.displacement().mode == NWB::Impl::DeformableDisplacementMode::ScalarUvRamp
        );
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.displacement().amplitude == 0.125f);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.morphs().size() == 1u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.morphs()[0].name == Name("lift"));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.morphs()[0].deltas.size() == 2u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.morphs()[0].deltas[1].vertexId == 2u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedGeometry.morphs()[0].deltas[1].deltaPosition.z == 0.5f);
    }

    ErrorCode errorCode;
    (void)RemoveAllIfExists(root, errorCode);
    NWB_LOGGER_REGISTER(nullptr);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);
}

static void TestDeformableGeometryCookerValidationFailures(TestContext& context){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB_LOGGER_REGISTER(&logger);

    TestArena testArena;
    auto expectCookFailure = [&](const AStringView metaText, const AStringView caseName){
        Path root;
        Path outputDirectory;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !CookSingleDeformableMeta(
            metaText,
            caseName,
            testArena,
            root,
            outputDirectory
        ));

        ErrorCode errorCode;
        (void)RemoveAllIfExists(root, errorCode);
    };

    expectCookFailure(s_MismatchedDeformableMeta, "mismatched_streams");
    expectCookFailure(s_MissingIndexTypeDeformableMeta, "missing_index_type");
    expectCookFailure(s_MismatchedSkinDeformableMeta, "mismatched_skin");
    expectCookFailure(s_MismatchedSourceSamplesDeformableMeta, "mismatched_source_samples");
    expectCookFailure(s_MismatchedMorphDeformableMeta, "mismatched_morph");
    expectCookFailure(s_MissingMorphTangentDeformableMeta, "missing_morph_tangent");

    NWB_LOGGER_REGISTER(nullptr);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() >= 6u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("rest vertex stream counts must match")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("'index_type' must be 'u16' or 'u32'")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("skin streams must match vertex count")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("source samples must match vertex count")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(
        context,
        logger.sawErrorContaining(NWB_TEXT("morph 'lift' stream counts must match and must not be empty"))
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("morph 'lift' requires 'delta_tangent' list")));
#else
    (void)context;
#endif
}

static void TestDeformableGeometryValidationFailures(TestContext& context){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB_LOGGER_REGISTER(&logger);

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        Vector<NWB::Impl::DeformableVertexRest> vertices = geometry.restVertices();
        vertices[0].normal = Float3Data(0.f, 0.f, 0.f);
        geometry.setRestVertices(Move(vertices));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        Vector<NWB::Impl::DeformableVertexRest> vertices = geometry.restVertices();
        vertices[0].tangent = Float4Data(1.f, 0.f, 0.f, 0.f);
        geometry.setRestVertices(Move(vertices));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        Vector<NWB::Impl::DeformableVertexRest> vertices = geometry.restVertices();
        vertices[0].tangent = Float4Data(0.f, 0.f, 1.f, 1.f);
        geometry.setRestVertices(Move(vertices));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        Vector<NWB::Impl::DeformableVertexRest> vertices = geometry.restVertices();
        vertices[0].tangent = Float4Data(1.f, 0.f, 0.f, 2.f);
        geometry.setRestVertices(Move(vertices));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        Vector<NWB::Impl::DeformableVertexRest> vertices = geometry.restVertices();
        vertices[0].normal = Float3Data(0.f, 0.f, 2.f);
        geometry.setRestVertices(Move(vertices));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        Vector<NWB::Impl::DeformableVertexRest> vertices = geometry.restVertices();
        vertices[0].tangent = Float4Data(0.70710677f, 0.f, 0.70710677f, 1.f);
        geometry.setRestVertices(Move(vertices));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        Vector<NWB::Impl::SkinInfluence4> skin = geometry.skin();
        skin[0].weight[0] = 0.5f;
        geometry.setSkin(Move(skin));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        Vector<NWB::Impl::SkinInfluence4> skin = geometry.skin();
        skin.pop_back();
        geometry.setSkin(Move(skin));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        Vector<u32> indices = geometry.indices();
        indices.pop_back();
        geometry.setIndices(Move(indices));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        Vector<u32> indices = geometry.indices();
        indices[2] = 99u;
        geometry.setIndices(Move(indices));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        Vector<u32> indices = geometry.indices();
        indices[2] = indices[1];
        geometry.setIndices(Move(indices));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        Vector<NWB::Impl::DeformableVertexRest> vertices = geometry.restVertices();
        vertices[2].position = Float3Data(0.0f, -0.5f, 0.0f);
        geometry.setRestVertices(Move(vertices));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        Vector<NWB::Impl::SourceSample> sourceSamples = geometry.sourceSamples();
        sourceSamples[0].sourceTri = 99u;
        geometry.setSourceSamples(Move(sourceSamples));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        Vector<NWB::Impl::SourceSample> sourceSamples = geometry.sourceSamples();
        sourceSamples[0] = MakeSourceSample(0u, 0.25f, 0.25f, 0.25f);
        geometry.setSourceSamples(Move(sourceSamples));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        Vector<NWB::Impl::SourceSample> sourceSamples = geometry.sourceSamples();
        sourceSamples.pop_back();
        geometry.setSourceSamples(Move(sourceSamples));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        NWB::Impl::DeformableDisplacement displacement = geometry.displacement();
        displacement.mode = 99u;
        geometry.setDisplacement(displacement);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        NWB::Impl::DeformableDisplacement displacement;
        displacement.amplitude = 0.25f;
        geometry.setDisplacement(displacement);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        Vector<NWB::Impl::DeformableMorph> morphs = geometry.morphs();
        morphs[0].deltas[1].vertexId = morphs[0].deltas[0].vertexId;
        geometry.setMorphs(Move(morphs));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    {
        NWB::Impl::DeformableGeometry geometry = BuildValidDeformableGeometry();
        Vector<NWB::Impl::DeformableMorph> morphs = geometry.morphs();
        morphs[0].deltas[0].vertexId = 99u;
        geometry.setMorphs(Move(morphs));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !geometry.validatePayload());
    }

    NWB_LOGGER_REGISTER(nullptr);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 19u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("degenerate normal/tangent frame")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("invalid normal/tangent frame")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("triangle 0 is degenerate")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("triangle 0 has zero area")));
#else
    (void)context;
#endif
}


#undef NWB_ASSETS_GRAPHICS_TEST_CHECK


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int main(){
    NWB::Core::Common::InitializerGuard commonInitializerGuard;
    if(!commonInitializerGuard.initialize()){
        NWB_CERR << "assets graphics tests failed: common initialization failed\n";
        return 1;
    }

    __hidden_assets_graphics_tests::TestContext context;
    __hidden_assets_graphics_tests::TestDeformableGeometryCodecRoundTrip(context);
    __hidden_assets_graphics_tests::TestMinimalDeformableGeometryCodecRoundTrip(context);
    __hidden_assets_graphics_tests::TestDeformableGeometryCodecRejectsMalformedCounts(context);
    __hidden_assets_graphics_tests::TestDeformableGeometryCodecRejectsMalformedDependentCounts(context);
    __hidden_assets_graphics_tests::TestDeformableGeometryCookerMinimalAsset(context);
    __hidden_assets_graphics_tests::TestDeformableGeometryCookerU32IndexType(context);
    __hidden_assets_graphics_tests::TestDeformableGeometryCookerExplicitEmptyOptionalLists(context);
    __hidden_assets_graphics_tests::TestDeformableGeometryCookerFullAsset(context);
    __hidden_assets_graphics_tests::TestDeformableGeometryCookerValidationFailures(context);
    __hidden_assets_graphics_tests::TestDeformableGeometryValidationFailures(context);

    if(context.failed != 0){
        NWB_CERR
            << "assets graphics tests failed: "
            << context.failed
            << " of "
            << (context.passed + context.failed)
            << '\n'
        ;
        return 1;
    }

    NWB_COUT << "assets graphics tests passed: " << context.passed << '\n';
    return 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

