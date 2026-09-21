// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"


#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;
constexpr u32 s_ThirdElementIndex = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_mesh_acceptance{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using AString = AssetsGraphicsFixture::AString;
using CapturingLogger = AssetsGraphicsFixture::CapturingLogger;
using Path = AssetsGraphicsFixture::Path;
using TestArena = AssetsGraphicsFixture::TestArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<
    typename MeshT,
    typename PositionRefT
>
static bool TestDecodeMeshletPositionRef(
    const MeshT& mesh,
    const NWB::Impl::MeshletDesc& meshlet,
    const u32 localPositionIndex,
    PositionRefT& outRef
){
    return NWB::Impl::DecodeMeshletPositionRef(
        mesh.meshletPositionRefDeltas().data(),
        mesh.meshletPositionRefDeltas().size(),
        meshlet,
        localPositionIndex,
        NWB::Core::Mesh::MeshClassUsesSkinning(mesh.meshClass()),
        outRef
    );
}

template<
    typename MeshT,
    typename AttributeRefT
>
static bool TestDecodeMeshletAttributeRef(
    const MeshT& mesh,
    const NWB::Impl::MeshletDesc& meshlet,
    const u32 localAttributeIndex,
    AttributeRefT& outRef
){
    return NWB::Impl::DecodeMeshletAttributeRef(
        mesh.meshletAttributeRefDeltas().data(),
        mesh.meshletAttributeRefDeltas().size(),
        meshlet,
        localAttributeIndex,
        outRef
    );
}

template<
    typename MeshT,
    typename PositionStreamT,
    typename NormalStreamT,
    typename LocalRefVectorT
>
static bool TestMeshletHasPositionNormalValue(
    const MeshT& mesh,
    const NWB::Impl::MeshletDesc& meshlet,
    const PositionStreamT& positions,
    const NormalStreamT& normals,
    const LocalRefVectorT& localRefs,
    const Float3U& expectedPosition,
    const Float4U& expectedNormal
){
    for(u32 localVertexIndex = 0u; localVertexIndex < NWB::Impl::MeshletVertexCount(meshlet); ++localVertexIndex){
        const NWB::Impl::MeshletLocalVertexRef& localRef = localRefs[meshlet.localVertexOffset + localVertexIndex];
        NWB::Impl::MeshletPositionStreamRef positionRef;
        NWB::Impl::MeshletAttributeStreamRef attributeRef;
        if(
            !TestDecodeMeshletPositionRef(mesh, meshlet, localRef.localDeformedPosition, positionRef)
            || !TestDecodeMeshletAttributeRef(mesh, meshlet, localRef.localAttribute, attributeRef)
        )
            return false;

        const Float3U& position = positions[positionRef.position];
        const Float4U normal = LoadHalf4U(normals[attributeRef.normal]);
        if(
            position.x == expectedPosition.x
            && position.y == expectedPosition.y
            && position.z == expectedPosition.z
            && normal.x == expectedNormal.x
            && normal.y == expectedNormal.y
            && normal.z == expectedNormal.z
            && normal.w == expectedNormal.w
        )
            return true;
    }

    return false;
}

template<typename MeshT>
static bool TestMeshletPositionRefsAreFirstUseOrdered(
    const NWB::Impl::MeshletDesc& meshlet,
    const MeshT& mesh
){
    for(u32 localPositionIndex = 0u; localPositionIndex < NWB::Impl::MeshletPositionCount(meshlet); ++localPositionIndex){
        NWB::Impl::MeshletPositionStreamRef ref;
        if(!TestDecodeMeshletPositionRef(mesh, meshlet, localPositionIndex, ref))
            return false;
        if(ref.position != localPositionIndex)
            return false;
    }

    return true;
}

template<typename MeshT, typename SelectorT>
static bool TestMeshletAttributeRefsAreFirstUseOrdered(
    const NWB::Impl::MeshletDesc& meshlet,
    const MeshT& mesh,
    SelectorT&& selector
){
    u8 seen[NWB::Impl::s_MeshMaxMeshletVertices] = {};
    u32 nextStreamIndex = 0u;
    for(u32 localAttributeIndex = 0u; localAttributeIndex < NWB::Impl::MeshletAttributeCount(meshlet); ++localAttributeIndex){
        NWB::Impl::MeshletAttributeStreamRef ref;
        if(!TestDecodeMeshletAttributeRef(mesh, meshlet, localAttributeIndex, ref))
            return false;
        const u32 streamIndex = selector(ref);
        if(streamIndex >= NWB::Impl::s_MeshMaxMeshletVertices)
            return false;
        if(seen[streamIndex])
            continue;

        if(streamIndex != nextStreamIndex)
            return false;
        seen[streamIndex] = 1u;
        ++nextStreamIndex;
    }

    return true;
}

template<typename MeshT>
[[nodiscard]] static usize TestMeshletLogicalPositionRefCount(const MeshT& mesh){
    usize count = 0u;
    for(const NWB::Impl::MeshletDesc& meshlet : mesh.meshlets())
        count += NWB::Impl::MeshletPositionCount(meshlet);
    return count;
}

template<typename MeshT>
[[nodiscard]] static usize TestMeshletLogicalAttributeRefCount(const MeshT& mesh){
    usize count = 0u;
    for(const NWB::Impl::MeshletDesc& meshlet : mesh.meshlets())
        count += NWB::Impl::MeshletAttributeCount(meshlet);
    return count;
}

template<typename MeshT>
[[nodiscard]] static usize TestMeshletCompressedReferencePayloadBytes(const MeshT& mesh){
    return mesh.meshlets().size() * sizeof(NWB::Impl::MeshletDesc)
        + mesh.meshletPositionRefDeltas().size()
        + mesh.meshletAttributeRefDeltas().size()
    ;
}

template<typename MeshT>
[[nodiscard]] static usize TestMeshletUncompressedReferencePayloadBytes(const MeshT& mesh){
    static constexpr usize s_PreCompressionMeshletDescBytes = sizeof(u32) * 5u;
    return mesh.meshlets().size() * s_PreCompressionMeshletDescBytes
        + TestMeshletLogicalPositionRefCount(mesh) * sizeof(NWB::Impl::MeshletPositionStreamRef)
        + TestMeshletLogicalAttributeRefCount(mesh) * sizeof(NWB::Impl::MeshletAttributeStreamRef)
    ;
}

template<typename MeshT>
[[nodiscard]] static usize TestMeshletCompressedReferenceBandwidthBytes(const MeshT& mesh){
    return mesh.meshletPositionRefDeltas().size() + mesh.meshletAttributeRefDeltas().size();
}

template<typename MeshT>
[[nodiscard]] static usize TestMeshletUncompressedReferenceBandwidthBytes(const MeshT& mesh){
    return TestMeshletLogicalPositionRefCount(mesh) * sizeof(NWB::Impl::MeshletPositionStreamRef)
        + TestMeshletLogicalAttributeRefCount(mesh) * sizeof(NWB::Impl::MeshletAttributeStreamRef)
    ;
}

template<typename MeshT>
[[nodiscard]] static bool TestMeshletReferenceCompressionShrinksPayload(const MeshT& mesh){
    return TestMeshletCompressedReferencePayloadBytes(mesh) < TestMeshletUncompressedReferencePayloadBytes(mesh);
}

template<typename MeshT>
[[nodiscard]] static bool TestMeshletReferenceCompressionBandwidthNeutralOrBetter(const MeshT& mesh){
    return TestMeshletCompressedReferenceBandwidthBytes(mesh) <= TestMeshletUncompressedReferenceBandwidthBytes(mesh);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool CookAndLoadSmokeMesh(
    TestArena& testArena,
    const char* assetFilename,
    const AStringView caseName,
    const Name assetName,
    Path& outRoot,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset
){
    Path outputDirectory(testArena.arena);
    const bool cooked = AssetsGraphicsFixture::CookSmokeMeshMeta(
        assetFilename,
        caseName,
        testArena,
        outRoot,
        outputDirectory
    );
    EXPECT_TRUE(cooked);
    if(!cooked)
        return false;
    return AssetsGraphicsFixture::LoadCookedMesh(testArena, outputDirectory, assetName, outLoadedAsset);
}

struct MeshletAcceptanceVertexKey{
    u32 position = NWB::Impl::s_MeshMissingStreamIndex;
    u32 normal = NWB::Impl::s_MeshMissingStreamIndex;
    u32 tangent = NWB::Impl::s_MeshMissingStreamIndex;
    u32 uv0 = NWB::Impl::s_MeshMissingStreamIndex;
    u32 color = NWB::Impl::s_MeshMissingStreamIndex;
    u32 skin = NWB::Impl::s_MeshMissingStreamIndex;
};

struct MeshletAcceptanceTriangleKey{
    MeshletAcceptanceVertexKey vertices[3];
};

struct MeshletAcceptanceQualityMetrics{
    u32 meshletCount = 0u;
    u32 coneDisabledCount = 0u;
    f64 radiusSum = 0.0;
    f64 vertexReuseSum = 0.0;
};

[[nodiscard]] static bool operator==(const MeshletAcceptanceVertexKey& lhs, const MeshletAcceptanceVertexKey& rhs){
    return lhs.position == rhs.position
        && lhs.normal == rhs.normal
        && lhs.tangent == rhs.tangent
        && lhs.uv0 == rhs.uv0
        && lhs.color == rhs.color
        && lhs.skin == rhs.skin
    ;
}

[[nodiscard]] static bool operator==(const MeshletAcceptanceTriangleKey& lhs, const MeshletAcceptanceTriangleKey& rhs){
    return lhs.vertices[0u] == rhs.vertices[0u]
        && lhs.vertices[1u] == rhs.vertices[1u]
        && lhs.vertices[s_ThirdElementIndex] == rhs.vertices[s_ThirdElementIndex]
    ;
}

[[nodiscard]] static constexpr MeshletAcceptanceVertexKey MakeMeshletAcceptanceVertexKey(
    const u32 position,
    const u32 normal,
    const u32 tangent,
    const u32 uv0,
    const u32 color
){
    MeshletAcceptanceVertexKey key;
    key.position = position;
    key.normal = normal;
    key.tangent = tangent;
    key.uv0 = uv0;
    key.color = color;
    return key;
}

static constexpr usize s_MeshletAcceptanceAlternatingConeTrianglePairCount = NWB::Impl::s_MeshMaxMeshletTriangles;
static constexpr MeshletAcceptanceVertexKey s_MeshletAcceptanceAlternatingConeVertexRefs[] = {
    MakeMeshletAcceptanceVertexKey(0u, 0u, 0u, 0u, 0u),
    MakeMeshletAcceptanceVertexKey(1u, 0u, 0u, 1u, 0u),
    MakeMeshletAcceptanceVertexKey(s_ExpectedDualCount, 0u, 0u, s_ExpectedDualCount, 0u),
    MakeMeshletAcceptanceVertexKey(3u, 1u, 1u, 0u, 0u),
    MakeMeshletAcceptanceVertexKey(4u, 1u, 1u, 1u, 0u),
    MakeMeshletAcceptanceVertexKey(5u, 1u, 1u, s_ExpectedDualCount, 0u),
};
static constexpr u32 s_MeshletAcceptanceAlternatingConeTriangles[][3] = {
    { 0u, 1u, s_ExpectedDualCount },
    { 3u, 4u, 5u },
};

template<typename FuncT>
static void ForEachMeshletAcceptanceAlternatingConeTriangle(FuncT&& func){
    for(usize trianglePairIndex = 0u; trianglePairIndex < s_MeshletAcceptanceAlternatingConeTrianglePairCount; ++trianglePairIndex){
        for(const auto& triangle : s_MeshletAcceptanceAlternatingConeTriangles)
            func(triangle);
    }
}

template<typename... Args>
static void AppendMeshletAcceptanceFormattedMeta(AString& meta, AFormatString<Args...> format, Args&&... args){
    const auto line = StringFormat(NWB::Tests::TestDetail::Arena(), format, Forward<Args>(args)...);
    AssetsGraphicsFixture::AppendTestMeta(meta, AStringView(line.data(), line.size()));
}

static void AppendMeshletAcceptanceVertexRefMeta(AString& meta, const MeshletAcceptanceVertexKey& vertexRef){
    AppendMeshletAcceptanceFormattedMeta(
        meta,
        "    [{}, {}, {}, {}, {}],\n",
        vertexRef.position,
        vertexRef.normal,
        vertexRef.tangent,
        vertexRef.uv0,
        vertexRef.color
    );
}

static void AppendMeshletAcceptanceTriangleMeta(AString& meta, const u32 (&triangle)[3]){
    AppendMeshletAcceptanceFormattedMeta(
        meta,
        "    [{}, {}, {}],\n",
        triangle[0u],
        triangle[1u],
        triangle[s_ThirdElementIndex]
    );
}

static MeshletAcceptanceTriangleKey BuildMeshletAcceptanceTriangleKey(const u32 (&triangle)[3]){
    MeshletAcceptanceTriangleKey triangleKey;
    for(usize cornerIndex = 0u; cornerIndex < 3u; ++cornerIndex)
        triangleKey.vertices[cornerIndex] = s_MeshletAcceptanceAlternatingConeVertexRefs[triangle[cornerIndex]];
    return triangleKey;
}

static AString BuildMeshletAcceptanceAlternatingConeMeshMeta(){
    AString meta;
    meta.reserve(8192u);
    AssetsGraphicsFixture::AppendTestMeta(meta, R"(mesh asset;

asset.positions = [
    [0.0, 0.0, 0.0],
    [1.0, 0.0, 0.0],
    [0.0, 1.0, 0.0],
    [100.0, 0.0, 0.0],
    [100.0, 1.0, 0.0],
    [101.0, 0.0, 0.0],
];

asset.normals = [
    [0.0, 0.0,  1.0],
    [0.0, 0.0, -1.0],
];

asset.tangents = [
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
];

asset.uv0 = [
    [0.0, 0.0],
    [1.0, 0.0],
    [0.0, 1.0],
];

asset.colors = [
    [1.0, 1.0, 1.0, 1.0],
];

asset.vertex_refs = [
)");

    for(const MeshletAcceptanceVertexKey& vertexRef : s_MeshletAcceptanceAlternatingConeVertexRefs)
        AppendMeshletAcceptanceVertexRefMeta(meta, vertexRef);

    AssetsGraphicsFixture::AppendTestMeta(meta, R"(];

asset.indices = [
)");

    ForEachMeshletAcceptanceAlternatingConeTriangle([&meta](const u32 (&triangle)[3]){
        AppendMeshletAcceptanceTriangleMeta(meta, triangle);
    });

    AssetsGraphicsFixture::AppendTestMeta(meta, "];\n");
    return meta;
}

static void BuildMeshletAcceptanceAlternatingConeSourceTriangles(
    NWB::Core::Assets::AssetVector<MeshletAcceptanceTriangleKey>& outTriangles
){
    outTriangles.clear();
    outTriangles.reserve(s_MeshletAcceptanceAlternatingConeTrianglePairCount * s_ExpectedDualCount);
    ForEachMeshletAcceptanceAlternatingConeTriangle([&outTriangles](const u32 (&triangle)[3]){
        outTriangles.push_back(BuildMeshletAcceptanceTriangleKey(triangle));
    });
}

template<typename MeshT>
[[nodiscard]] static bool TestMeshletAcceptanceLimits(const MeshT& mesh){
    for(const NWB::Impl::MeshletDesc& meshlet : mesh.meshlets()){
        if(
            NWB::Impl::MeshletVertexCount(meshlet) > NWB::Impl::s_MeshMaxMeshletVertices
            || NWB::Impl::MeshletPositionCount(meshlet) > NWB::Impl::s_MeshMaxMeshletVertices
            || NWB::Impl::MeshletAttributeCount(meshlet) > NWB::Impl::s_MeshMaxMeshletVertices
            || NWB::Impl::MeshletPrimitiveCount(meshlet) > NWB::Impl::s_MeshMaxMeshletTriangles
        )
            return false;
    }

    return true;
}

template<typename MeshT>
[[nodiscard]] static MeshletAcceptanceVertexKey BuildMeshletAcceptanceCookedVertexKey(
    const MeshT& mesh,
    const NWB::Impl::MeshletDesc& meshlet,
    const u32 localVertexIndex
){
    const NWB::Impl::MeshletLocalVertexRef& localRef = mesh.meshletLocalVertexRefs()[meshlet.localVertexOffset + localVertexIndex];
    NWB::Impl::MeshletPositionStreamRef positionRef;
    NWB::Impl::MeshletAttributeStreamRef attributeRef;
    const bool decoded =
        TestDecodeMeshletPositionRef(mesh, meshlet, localRef.localDeformedPosition, positionRef)
        && TestDecodeMeshletAttributeRef(mesh, meshlet, localRef.localAttribute, attributeRef)
    ;
    if(!decoded)
        return MeshletAcceptanceVertexKey{};

    MeshletAcceptanceVertexKey key;
    key.position = positionRef.position;
    key.normal = attributeRef.normal;
    key.tangent = attributeRef.tangent;
    key.uv0 = attributeRef.uv0;
    key.color = attributeRef.color;
    key.skin = positionRef.skin;
    return key;
}

template<typename TriangleVectorT>
[[nodiscard]] static bool MarkMeshletAcceptanceMatchedTriangle(
    const TriangleVectorT& sourceTriangles,
    NWB::Core::Assets::AssetVector<u8>& matchedSourceTriangles,
    const MeshletAcceptanceTriangleKey& cookedTriangle
){
    for(usize triangleIndex = 0u; triangleIndex < sourceTriangles.size(); ++triangleIndex){
        if(matchedSourceTriangles[triangleIndex] != 0u || !(sourceTriangles[triangleIndex] == cookedTriangle))
            continue;

        matchedSourceTriangles[triangleIndex] = 1u;
        return true;
    }

    return false;
}

template<typename MeshT, typename TriangleVectorT>
[[nodiscard]] static bool TestMeshletAcceptanceTrianglesMatchSource(
    TestArena& testArena,
    const MeshT& mesh,
    const TriangleVectorT& sourceTriangles
){
    NWB::Core::Assets::AssetVector<u8> matchedSourceTriangles(testArena.arena);
    matchedSourceTriangles.resize(sourceTriangles.size(), 0u);
    usize cookedTriangleCount = 0u;

    for(const NWB::Impl::MeshletDesc& meshlet : mesh.meshlets()){
        for(u32 primitiveIndex = 0u; primitiveIndex < NWB::Impl::MeshletPrimitiveCount(meshlet); ++primitiveIndex){
            const usize primitiveOffset = meshlet.primitiveOffset + static_cast<usize>(primitiveIndex) * 3u;
            MeshletAcceptanceTriangleKey cookedTriangle;
            for(usize cornerIndex = 0u; cornerIndex < 3u; ++cornerIndex){
                const u8 localVertexIndex = mesh.meshletPrimitiveIndices()[primitiveOffset + cornerIndex];
                cookedTriangle.vertices[cornerIndex] = BuildMeshletAcceptanceCookedVertexKey(mesh, meshlet, localVertexIndex);
            }

            if(!MarkMeshletAcceptanceMatchedTriangle(sourceTriangles, matchedSourceTriangles, cookedTriangle))
                return false;
            ++cookedTriangleCount;
        }
    }

    return cookedTriangleCount == sourceTriangles.size();
}

template<typename MeshT>
[[nodiscard]] static MeshletAcceptanceQualityMetrics BuildCookedMeshletAcceptanceQualityMetrics(const MeshT& mesh){
    MeshletAcceptanceQualityMetrics metrics;
    metrics.meshletCount = static_cast<u32>(mesh.meshlets().size());
    for(usize meshletIndex = 0u; meshletIndex < mesh.meshlets().size(); ++meshletIndex){
        const NWB::Impl::MeshletDesc& meshlet = mesh.meshlets()[meshletIndex];
        if(!NWB::Impl::MeshletConeEnabled(mesh.meshletBounds()[meshletIndex]))
            ++metrics.coneDisabledCount;
        metrics.radiusSum += mesh.meshletBounds()[meshletIndex].sphere.w;
        metrics.vertexReuseSum += static_cast<f64>(NWB::Impl::MeshletPrimitiveCount(meshlet) * 3u)
            / static_cast<f64>(NWB::Impl::MeshletVertexCount(meshlet))
        ;
    }

    return metrics;
}

[[nodiscard]] static f64 MeshletAcceptanceAverageRadius(const MeshletAcceptanceQualityMetrics& metrics){
    return metrics.meshletCount != 0u ? metrics.radiusSum / static_cast<f64>(metrics.meshletCount) : 0.0;
}

[[nodiscard]] static f64 MeshletAcceptanceAverageVertexReuse(const MeshletAcceptanceQualityMetrics& metrics){
    return metrics.meshletCount != 0u ? metrics.vertexReuseSum / static_cast<f64>(metrics.meshletCount) : 0.0;
}

template<typename CallbackT>
static void RunSmokeMeshAcceptance(
    const char* assetFilename,
    const AStringView caseName,
    const Name assetName,
    CallbackT&& callback
){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    if(CookAndLoadSmokeMesh(testArena, assetFilename, caseName, assetName, root, loadedAsset)){
        EXPECT_EQ(loadedAsset->assetType(), NWB::Impl::Mesh::AssetTypeName());
        const NWB::Impl::Mesh& loadedMesh = static_cast<const NWB::Impl::Mesh&>(*loadedAsset);
        Forward<CallbackT>(callback)(loadedMesh);
    }
    EXPECT_EQ(logger.errorCount(), 0u);

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
}

TEST(AssetsGraphics, MeshAcceptanceHardEdgeCubeZippedRefs){
    RunSmokeMeshAcceptance(
        "cube_hard_edges.nwb",
        "hard_edge_cube",
        Name("project/meshes/cube_hard_edges"),
        [](const NWB::Impl::Mesh& loadedMesh){
            EXPECT_EQ(loadedMesh.positionStream().size(), 8u);
            EXPECT_EQ(loadedMesh.normalStream().size(), 6u);
            EXPECT_EQ(loadedMesh.tangentStream().size(), 4u);
            EXPECT_EQ(loadedMesh.uv0Stream().size(), 4u);
            EXPECT_EQ(loadedMesh.colorStream().size(), 1u);
            EXPECT_EQ(loadedMesh.meshlets().size(), 1u);
            EXPECT_EQ(loadedMesh.meshletLocalVertexRefs().size(), 24u);
            EXPECT_EQ(loadedMesh.meshletPrimitiveIndices().size(), 36u);
            EXPECT_TRUE(TestMeshletReferenceCompressionShrinksPayload(loadedMesh));
            EXPECT_TRUE(TestMeshletReferenceCompressionBandwidthNeutralOrBetter(loadedMesh));

            const NWB::Impl::MeshletDesc& meshlet = loadedMesh.meshlets()[0];
            EXPECT_EQ(NWB::Impl::MeshletVertexCount(meshlet), 24u);
            EXPECT_EQ(NWB::Impl::MeshletPrimitiveCount(meshlet), 12u);
            EXPECT_EQ(NWB::Impl::MeshletPositionCount(meshlet), 8u);
            EXPECT_EQ(NWB::Impl::MeshletAttributeCount(meshlet), 24u);
            EXPECT_EQ(meshlet.positionBase, 0u);
            EXPECT_EQ(meshlet.skinBase, NWB::Impl::s_MeshMissingStreamIndex);
            EXPECT_EQ(meshlet.normalBase, 0u);
            EXPECT_EQ(meshlet.tangentBase, 0u);
            EXPECT_EQ(meshlet.uv0Base, 0u);
            EXPECT_EQ(meshlet.colorBase, 0u);
            EXPECT_EQ(meshlet.encoding, 0u);

            const auto& localRefs = loadedMesh.meshletLocalVertexRefs();
            const auto& positions = loadedMesh.positionStream();
            const auto& normals = loadedMesh.normalStream();
            EXPECT_TRUE(TestMeshletPositionRefsAreFirstUseOrdered(meshlet, loadedMesh));
            EXPECT_TRUE(TestMeshletAttributeRefsAreFirstUseOrdered(
                meshlet,
                loadedMesh,
                [](const NWB::Impl::MeshletAttributeStreamRef& ref){ return ref.normal; }
            ));
            EXPECT_TRUE(TestMeshletAttributeRefsAreFirstUseOrdered(
                meshlet,
                loadedMesh,
                [](const NWB::Impl::MeshletAttributeStreamRef& ref){ return ref.tangent; }
            ));
            EXPECT_TRUE(TestMeshletAttributeRefsAreFirstUseOrdered(
                meshlet,
                loadedMesh,
                [](const NWB::Impl::MeshletAttributeStreamRef& ref){ return ref.uv0; }
            ));
            EXPECT_TRUE(TestMeshletAttributeRefsAreFirstUseOrdered(
                meshlet,
                loadedMesh,
                [](const NWB::Impl::MeshletAttributeStreamRef& ref){ return ref.color; }
            ));
            EXPECT_TRUE(TestMeshletHasPositionNormalValue(
                loadedMesh,
                meshlet,
                positions,
                normals,
                localRefs,
                Float3U(-0.5f, -0.5f, -0.5f),
                Float4U(0.0f, 0.0f, -1.0f, 0.0f)
            ));
            EXPECT_TRUE(TestMeshletHasPositionNormalValue(
                loadedMesh,
                meshlet,
                positions,
                normals,
                localRefs,
                Float3U(-0.5f, -0.5f, -0.5f),
                Float4U(0.0f, -1.0f, 0.0f, 0.0f)
            ));
            EXPECT_TRUE(TestMeshletHasPositionNormalValue(
                loadedMesh,
                meshlet,
                positions,
                normals,
                localRefs,
                Float3U(-0.5f, -0.5f, -0.5f),
                Float4U(-1.0f, 0.0f, 0.0f, 0.0f)
            ));
        }
    );
}

TEST(AssetsGraphics, MeshAcceptanceQualityBuilderChecks){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
    const AString meta = BuildMeshletAcceptanceAlternatingConeMeshMeta();
    const bool cooked = AssetsGraphicsFixture::CookSingleMeshMeta(
        AStringView(meta.data(), meta.size()),
        "quality_builder_acceptance",
        testArena,
        root,
        outputDirectory
    );
    EXPECT_TRUE(cooked);
    if(cooked){
        UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
        if(AssetsGraphicsFixture::LoadCookedMinimalMesh(testArena, outputDirectory, loadedAsset)){
            const NWB::Impl::Mesh& loadedMesh = static_cast<const NWB::Impl::Mesh&>(*loadedAsset);
            auto sourceTriangles = AssetsGraphicsFixture::MakeAssetVector<MeshletAcceptanceTriangleKey>(testArena);
            BuildMeshletAcceptanceAlternatingConeSourceTriangles(sourceTriangles);
            const MeshletAcceptanceQualityMetrics cookedMetrics = BuildCookedMeshletAcceptanceQualityMetrics(loadedMesh);

            EXPECT_TRUE(TestMeshletAcceptanceLimits(loadedMesh));
            EXPECT_TRUE(TestMeshletAcceptanceTrianglesMatchSource(testArena, loadedMesh, sourceTriangles));
            EXPECT_EQ(cookedMetrics.meshletCount, s_ExpectedDualCount);
            EXPECT_EQ(cookedMetrics.coneDisabledCount, 0u);
            EXPECT_TRUE(NWB::Tests::NearlyEqual(static_cast<f32>(MeshletAcceptanceAverageRadius(cookedMetrics)), 0.88388348f, 0.0001f));
            EXPECT_EQ(MeshletAcceptanceAverageVertexReuse(cookedMetrics), static_cast<f64>(NWB::Impl::s_MeshMaxMeshletTriangles));
        }
    }
    EXPECT_EQ(logger.errorCount(), 0u);

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
}

TEST(AssetsGraphics, MeshAcceptanceSphereSmooth){
    RunSmokeMeshAcceptance(
        "sphere_smooth.nwb",
        "sphere_smooth",
        Name("project/meshes/sphere_smooth"),
        [](const NWB::Impl::Mesh& loadedMesh){
            EXPECT_EQ(loadedMesh.positionStream().size(), 6u);
            EXPECT_EQ(loadedMesh.normalStream().size(), 6u);
            EXPECT_EQ(loadedMesh.meshlets().size(), 1u);
            EXPECT_EQ(loadedMesh.meshletPrimitiveIndices().size(), 24u);

            for(usize i = 0u; i < loadedMesh.positionStream().size(); ++i){
                const Float3U& position = loadedMesh.positionStream()[i];
                const Float4U normal = LoadHalf4U(loadedMesh.normalStream()[i]);
                EXPECT_EQ(normal.x, position.x);
                EXPECT_EQ(normal.y, position.y);
                EXPECT_EQ(normal.z, position.z);
            }

            const NWB::Impl::MeshletDesc& meshlet = loadedMesh.meshlets()[0u];
            EXPECT_EQ(NWB::Impl::MeshletPrimitiveCount(meshlet), 8u);
            EXPECT_FALSE(NWB::Impl::MeshletConeEnabled(loadedMesh.meshletBounds()[0u]));
        }
    );
}

TEST(AssetsGraphics, MeshAcceptanceUvSeamQuad){
    RunSmokeMeshAcceptance(
        "uv_seam_quad.nwb",
        "uv_seam_quad",
        Name("project/meshes/uv_seam_quad"),
        [](const NWB::Impl::Mesh& loadedMesh){
            EXPECT_EQ(loadedMesh.positionStream().size(), 4u);
            EXPECT_EQ(loadedMesh.uv0Stream().size(), 4u);
            EXPECT_EQ(loadedMesh.tangentStream().size(), s_ExpectedDualCount);
            EXPECT_EQ(TestMeshletLogicalPositionRefCount(loadedMesh), 4u);
            EXPECT_EQ(TestMeshletLogicalAttributeRefCount(loadedMesh), 6u);
            EXPECT_TRUE(TestMeshletReferenceCompressionShrinksPayload(loadedMesh));
            EXPECT_TRUE(TestMeshletReferenceCompressionBandwidthNeutralOrBetter(loadedMesh));

            const NWB::Impl::MeshletDesc& meshlet = loadedMesh.meshlets()[0u];
            const auto& localRefs = loadedMesh.meshletLocalVertexRefs();
            EXPECT_EQ(localRefs[0u].localDeformedPosition, localRefs[3u].localDeformedPosition);
            EXPECT_EQ(localRefs[s_ThirdElementIndex].localDeformedPosition, localRefs[4u].localDeformedPosition);
            EXPECT_NE(localRefs[0u].localAttribute, localRefs[3u].localAttribute);
            EXPECT_NE(localRefs[s_ThirdElementIndex].localAttribute, localRefs[4u].localAttribute);

            NWB::Impl::MeshletAttributeStreamRef attributeRef0;
            NWB::Impl::MeshletAttributeStreamRef attributeRef2;
            NWB::Impl::MeshletAttributeStreamRef attributeRef3;
            NWB::Impl::MeshletAttributeStreamRef attributeRef4;
            EXPECT_TRUE(TestDecodeMeshletAttributeRef(loadedMesh, meshlet, localRefs[0u].localAttribute, attributeRef0));
            EXPECT_TRUE(TestDecodeMeshletAttributeRef(loadedMesh, meshlet, localRefs[s_ThirdElementIndex].localAttribute, attributeRef2));
            EXPECT_TRUE(TestDecodeMeshletAttributeRef(loadedMesh, meshlet, localRefs[3u].localAttribute, attributeRef3));
            EXPECT_TRUE(TestDecodeMeshletAttributeRef(loadedMesh, meshlet, localRefs[4u].localAttribute, attributeRef4));
            EXPECT_EQ(attributeRef0.uv0, 0u);
            EXPECT_EQ(attributeRef2.uv0, s_ExpectedDualCount);
            EXPECT_EQ(attributeRef3.uv0, 1u);
            EXPECT_EQ(attributeRef4.uv0, 3u);
            EXPECT_EQ(attributeRef0.tangent, 0u);
            EXPECT_EQ(attributeRef2.tangent, 0u);
            EXPECT_EQ(attributeRef3.tangent, 1u);
            EXPECT_EQ(attributeRef4.tangent, 1u);
        }
    );
}

TEST(AssetsGraphics, MeshAcceptanceMirroredUvQuad){
    RunSmokeMeshAcceptance(
        "mirrored_uv_quad.nwb",
        "mirrored_uv_quad",
        Name("project/meshes/mirrored_uv_quad"),
        [](const NWB::Impl::Mesh& loadedMesh){
            EXPECT_EQ(loadedMesh.tangentStream().size(), s_ExpectedDualCount);
            EXPECT_EQ(LoadHalf4U(loadedMesh.tangentStream()[0u]).w, 1.0f);
            EXPECT_EQ(LoadHalf4U(loadedMesh.tangentStream()[1u]).w, -1.0f);

            const NWB::Impl::MeshletDesc& meshlet = loadedMesh.meshlets()[0u];
            const auto& localRefs = loadedMesh.meshletLocalVertexRefs();
            NWB::Impl::MeshletAttributeStreamRef attributeRef0;
            NWB::Impl::MeshletAttributeStreamRef attributeRef3;
            EXPECT_TRUE(TestDecodeMeshletAttributeRef(loadedMesh, meshlet, localRefs[0u].localAttribute, attributeRef0));
            EXPECT_TRUE(TestDecodeMeshletAttributeRef(loadedMesh, meshlet, localRefs[3u].localAttribute, attributeRef3));
            EXPECT_EQ(attributeRef0.tangent, 0u);
            EXPECT_EQ(attributeRef3.tangent, 1u);
        }
    );
}

TEST(AssetsGraphics, MeshAcceptanceTwoSidedPlane){
    RunSmokeMeshAcceptance(
        "two_sided_plane.nwb",
        "two_sided_plane",
        Name("project/meshes/two_sided_plane"),
        [](const NWB::Impl::Mesh& loadedMesh){
            EXPECT_EQ(loadedMesh.positionStream().size(), 3u);
            EXPECT_EQ(loadedMesh.normalStream().size(), s_ExpectedDualCount);
            EXPECT_EQ(TestMeshletLogicalPositionRefCount(loadedMesh), 3u);
            EXPECT_EQ(TestMeshletLogicalAttributeRefCount(loadedMesh), 6u);
            EXPECT_FALSE(NWB::Impl::MeshletConeEnabled(loadedMesh.meshletBounds()[0u]));
        }
    );
}

TEST(AssetsGraphics, MeshAcceptanceLargeManyMeshlets){
    RunSmokeMeshAcceptance(
        "large_mesh_many_meshlets.nwb",
        "large_mesh_many_meshlets",
        Name("project/meshes/large_mesh_many_meshlets"),
        [](const NWB::Impl::Mesh& loadedMesh){
            const usize expectedPrimitiveCount = static_cast<usize>(NWB::Impl::s_MeshMaxMeshletTriangles) + 1u;
            const usize expectedMeshletCount = s_ExpectedDualCount;
            EXPECT_EQ(loadedMesh.meshlets().size(), s_ExpectedDualCount);
            EXPECT_EQ(loadedMesh.meshletPrimitiveIndices().size(), expectedPrimitiveCount * 3u);
            EXPECT_EQ(TestMeshletLogicalPositionRefCount(loadedMesh), 6u);
            EXPECT_EQ(TestMeshletLogicalAttributeRefCount(loadedMesh), 6u);
            EXPECT_EQ(loadedMesh.meshletLocalVertexRefs().size(), 6u);
            if(loadedMesh.meshlets().size() < expectedMeshletCount)
                return;

            EXPECT_EQ(NWB::Impl::MeshletPrimitiveCount(loadedMesh.meshlets()[0u]), NWB::Impl::s_MeshMaxMeshletTriangles);
            EXPECT_EQ(NWB::Impl::MeshletPrimitiveCount(loadedMesh.meshlets()[1u]), 1u);
        }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

