// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool CookAndLoadSmokeMesh(
    TestContext& context,
    TestArena& testArena,
    const char* assetFilename,
    const AStringView caseName,
    const Name assetName,
    Path& outRoot,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset
){
    Path outputDirectory;
    const bool cooked = CookSmokeMeshMeta(
        assetFilename,
        caseName,
        testArena,
        outRoot,
        outputDirectory
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, cooked);
    if(!cooked)
        return false;
    return LoadCookedMesh(context, testArena, outputDirectory, assetName, outLoadedAsset);
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
        && lhs.vertices[2u] == rhs.vertices[2u]
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
    MakeMeshletAcceptanceVertexKey(2u, 0u, 0u, 2u, 0u),
    MakeMeshletAcceptanceVertexKey(3u, 1u, 1u, 0u, 0u),
    MakeMeshletAcceptanceVertexKey(4u, 1u, 1u, 1u, 0u),
    MakeMeshletAcceptanceVertexKey(5u, 1u, 1u, 2u, 0u),
};
static constexpr u32 s_MeshletAcceptanceAlternatingConeTriangles[][3] = {
    { 0u, 1u, 2u },
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
    AppendTestMeta(meta, AStringView(line.data(), line.size()));
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
        triangle[2u]
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
    AppendTestMeta(meta, R"(mesh asset;

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

    AppendTestMeta(meta, R"(];

asset.indices = [
)");

    ForEachMeshletAcceptanceAlternatingConeTriangle([&meta](const u32 (&triangle)[3]){
        AppendMeshletAcceptanceTriangleMeta(meta, triangle);
    });

    AppendTestMeta(meta, "];\n");
    return meta;
}

static void BuildMeshletAcceptanceAlternatingConeSourceTriangles(
    NWB::Core::Assets::AssetVector<MeshletAcceptanceTriangleKey>& outTriangles
){
    outTriangles.clear();
    outTriangles.reserve(s_MeshletAcceptanceAlternatingConeTrianglePairCount * 2u);
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
    TestContext& context,
    const char* assetFilename,
    const AStringView caseName,
    const Name assetName,
    CallbackT&& callback
){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root;
    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    if(CookAndLoadSmokeMesh(context, testArena, assetFilename, caseName, assetName, root, loadedAsset)){
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedAsset->assetType() == NWB::Impl::Mesh::AssetTypeName());
        const NWB::Impl::Mesh& loadedMesh = static_cast<const NWB::Impl::Mesh&>(*loadedAsset);
        Forward<CallbackT>(callback)(loadedMesh);
    }
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);

    ErrorCode errorCode;
    static_cast<void>(RemoveAllIfExists(root, errorCode));
}

static void TestMeshAcceptanceHardEdgeCubeZippedRefs(TestContext& context){
    RunSmokeMeshAcceptance(
        context,
        "cube_hard_edges.nwb",
        "hard_edge_cube",
        Name("project/meshes/cube_hard_edges"),
        [&context](const NWB::Impl::Mesh& loadedMesh){
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.positionStream().size() == 8u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.normalStream().size() == 6u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.tangentStream().size() == 6u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.uv0Stream().size() == 4u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.colorStream().size() == 1u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.meshlets().size() == 1u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.meshletLocalVertexRefs().size() == 24u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.meshletPrimitiveIndices().size() == 36u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, TestMeshletReferenceCompressionShrinksPayload(loadedMesh));
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, TestMeshletReferenceCompressionBandwidthNeutralOrBetter(loadedMesh));

            const NWB::Impl::MeshletDesc& meshlet = loadedMesh.meshlets()[0];
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Impl::MeshletVertexCount(meshlet) == 24u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Impl::MeshletPrimitiveCount(meshlet) == 12u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Impl::MeshletPositionCount(meshlet) == 8u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Impl::MeshletAttributeCount(meshlet) == 24u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, meshlet.positionBase == 0u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, meshlet.skinBase == NWB::Impl::s_MeshMissingStreamIndex);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, meshlet.normalBase == 0u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, meshlet.tangentBase == 0u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, meshlet.uv0Base == 0u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, meshlet.colorBase == 0u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, meshlet.encoding == 0u);

            const auto& localRefs = loadedMesh.meshletLocalVertexRefs();
            const auto& positions = loadedMesh.positionStream();
            const auto& normals = loadedMesh.normalStream();
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, TestMeshletPositionRefsAreFirstUseOrdered(meshlet, loadedMesh));
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, TestMeshletAttributeRefsAreFirstUseOrdered(
                meshlet,
                loadedMesh,
                [](const NWB::Impl::MeshletAttributeStreamRef& ref){ return ref.normal; }
            ));
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, TestMeshletAttributeRefsAreFirstUseOrdered(
                meshlet,
                loadedMesh,
                [](const NWB::Impl::MeshletAttributeStreamRef& ref){ return ref.tangent; }
            ));
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, TestMeshletAttributeRefsAreFirstUseOrdered(
                meshlet,
                loadedMesh,
                [](const NWB::Impl::MeshletAttributeStreamRef& ref){ return ref.uv0; }
            ));
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, TestMeshletAttributeRefsAreFirstUseOrdered(
                meshlet,
                loadedMesh,
                [](const NWB::Impl::MeshletAttributeStreamRef& ref){ return ref.color; }
            ));
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, TestMeshletHasPositionNormalValue(
                loadedMesh,
                meshlet,
                positions,
                normals,
                localRefs,
                Float3U(-0.5f, -0.5f, -0.5f),
                Float4U(0.0f, 0.0f, -1.0f, 0.0f)
            ));
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, TestMeshletHasPositionNormalValue(
                loadedMesh,
                meshlet,
                positions,
                normals,
                localRefs,
                Float3U(-0.5f, -0.5f, -0.5f),
                Float4U(0.0f, -1.0f, 0.0f, 0.0f)
            ));
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, TestMeshletHasPositionNormalValue(
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

static void TestMeshAcceptanceQualityBuilderChecks(TestContext& context){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root;
    Path outputDirectory;
    const AString meta = BuildMeshletAcceptanceAlternatingConeMeshMeta();
    const bool cooked = CookSingleMeshMeta(
        AStringView(meta.data(), meta.size()),
        "quality_builder_acceptance",
        testArena,
        root,
        outputDirectory
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, cooked);
    if(cooked){
        UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
        if(LoadCookedMinimalMesh(context, testArena, outputDirectory, loadedAsset)){
            const NWB::Impl::Mesh& loadedMesh = static_cast<const NWB::Impl::Mesh&>(*loadedAsset);
            auto sourceTriangles = MakeAssetVector<MeshletAcceptanceTriangleKey>(testArena);
            BuildMeshletAcceptanceAlternatingConeSourceTriangles(sourceTriangles);
            const MeshletAcceptanceQualityMetrics cookedMetrics = BuildCookedMeshletAcceptanceQualityMetrics(loadedMesh);

            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, TestMeshletAcceptanceLimits(loadedMesh));
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, TestMeshletAcceptanceTrianglesMatchSource(testArena, loadedMesh, sourceTriangles));
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, cookedMetrics.meshletCount == 2u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, cookedMetrics.coneDisabledCount == 0u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Tests::NearlyEqual(static_cast<f32>(MeshletAcceptanceAverageRadius(cookedMetrics)), 0.88388348f, 0.0001f));
            NWB_ASSETS_GRAPHICS_TEST_CHECK(
                context,
                MeshletAcceptanceAverageVertexReuse(cookedMetrics) == static_cast<f64>(NWB::Impl::s_MeshMaxMeshletTriangles)
            );
        }
    }
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);

    ErrorCode errorCode;
    static_cast<void>(RemoveAllIfExists(root, errorCode));
}

static void TestMeshAcceptanceSphereSmooth(TestContext& context){
    RunSmokeMeshAcceptance(
        context,
        "sphere_smooth.nwb",
        "sphere_smooth",
        Name("project/meshes/sphere_smooth"),
        [&context](const NWB::Impl::Mesh& loadedMesh){
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.positionStream().size() == 6u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.normalStream().size() == 6u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.meshlets().size() == 1u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.meshletPrimitiveIndices().size() == 24u);

            for(usize i = 0u; i < loadedMesh.positionStream().size(); ++i){
                const Float3U& position = loadedMesh.positionStream()[i];
                const Float4U normal = LoadHalf4U(loadedMesh.normalStream()[i]);
                NWB_ASSETS_GRAPHICS_TEST_CHECK(context, normal.x == position.x);
                NWB_ASSETS_GRAPHICS_TEST_CHECK(context, normal.y == position.y);
                NWB_ASSETS_GRAPHICS_TEST_CHECK(context, normal.z == position.z);
            }

            const NWB::Impl::MeshletDesc& meshlet = loadedMesh.meshlets()[0u];
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Impl::MeshletPrimitiveCount(meshlet) == 8u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::MeshletConeEnabled(loadedMesh.meshletBounds()[0u]));
        }
    );
}

static void TestMeshAcceptanceUvSeamQuad(TestContext& context){
    RunSmokeMeshAcceptance(
        context,
        "uv_seam_quad.nwb",
        "uv_seam_quad",
        Name("project/meshes/uv_seam_quad"),
        [&context](const NWB::Impl::Mesh& loadedMesh){
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.positionStream().size() == 4u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.uv0Stream().size() == 6u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.tangentStream().size() == 2u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, TestMeshletLogicalPositionRefCount(loadedMesh) == 4u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, TestMeshletLogicalAttributeRefCount(loadedMesh) == 6u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, TestMeshletReferenceCompressionShrinksPayload(loadedMesh));
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, TestMeshletReferenceCompressionBandwidthNeutralOrBetter(loadedMesh));

            const NWB::Impl::MeshletDesc& meshlet = loadedMesh.meshlets()[0u];
            const auto& localRefs = loadedMesh.meshletLocalVertexRefs();
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, localRefs[0u].localDeformedPosition == localRefs[3u].localDeformedPosition);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, localRefs[2u].localDeformedPosition == localRefs[4u].localDeformedPosition);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, localRefs[0u].localAttribute != localRefs[3u].localAttribute);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, localRefs[2u].localAttribute != localRefs[4u].localAttribute);

            NWB::Impl::MeshletAttributeStreamRef attributeRef0;
            NWB::Impl::MeshletAttributeStreamRef attributeRef3;
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, TestDecodeMeshletAttributeRef(loadedMesh, meshlet, localRefs[0u].localAttribute, attributeRef0));
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, TestDecodeMeshletAttributeRef(loadedMesh, meshlet, localRefs[3u].localAttribute, attributeRef3));
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, attributeRef0.uv0 == 0u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, attributeRef3.uv0 == 3u);
        }
    );
}

static void TestMeshAcceptanceMirroredUvQuad(TestContext& context){
    RunSmokeMeshAcceptance(
        context,
        "mirrored_uv_quad.nwb",
        "mirrored_uv_quad",
        Name("project/meshes/mirrored_uv_quad"),
        [&context](const NWB::Impl::Mesh& loadedMesh){
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.tangentStream().size() == 2u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, LoadHalf4U(loadedMesh.tangentStream()[0u]).w == 1.0f);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, LoadHalf4U(loadedMesh.tangentStream()[1u]).w == -1.0f);

            const NWB::Impl::MeshletDesc& meshlet = loadedMesh.meshlets()[0u];
            const auto& localRefs = loadedMesh.meshletLocalVertexRefs();
            NWB::Impl::MeshletAttributeStreamRef attributeRef0;
            NWB::Impl::MeshletAttributeStreamRef attributeRef3;
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, TestDecodeMeshletAttributeRef(loadedMesh, meshlet, localRefs[0u].localAttribute, attributeRef0));
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, TestDecodeMeshletAttributeRef(loadedMesh, meshlet, localRefs[3u].localAttribute, attributeRef3));
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, attributeRef0.tangent == 0u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, attributeRef3.tangent == 1u);
        }
    );
}

static void TestMeshAcceptanceTwoSidedPlane(TestContext& context){
    RunSmokeMeshAcceptance(
        context,
        "two_sided_plane.nwb",
        "two_sided_plane",
        Name("project/meshes/two_sided_plane"),
        [&context](const NWB::Impl::Mesh& loadedMesh){
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.positionStream().size() == 3u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.normalStream().size() == 2u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, TestMeshletLogicalPositionRefCount(loadedMesh) == 3u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, TestMeshletLogicalAttributeRefCount(loadedMesh) == 6u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::MeshletConeEnabled(loadedMesh.meshletBounds()[0u]));
        }
    );
}

static void TestMeshAcceptanceLargeManyMeshlets(TestContext& context){
    RunSmokeMeshAcceptance(
        context,
        "large_mesh_many_meshlets.nwb",
        "large_mesh_many_meshlets",
        Name("project/meshes/large_mesh_many_meshlets"),
        [&context](const NWB::Impl::Mesh& loadedMesh){
            const usize expectedPrimitiveCount = static_cast<usize>(NWB::Impl::s_MeshMaxMeshletTriangles) + 1u;
            const usize expectedMeshletCount = 2u;
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.meshlets().size() == 2u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.meshletPrimitiveIndices().size() == expectedPrimitiveCount * 3u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, TestMeshletLogicalPositionRefCount(loadedMesh) == 6u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, TestMeshletLogicalAttributeRefCount(loadedMesh) == 6u);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.meshletLocalVertexRefs().size() == 6u);
            if(loadedMesh.meshlets().size() < expectedMeshletCount)
                return;

            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Impl::MeshletPrimitiveCount(loadedMesh.meshlets()[0u]) == NWB::Impl::s_MeshMaxMeshletTriangles);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Impl::MeshletPrimitiveCount(loadedMesh.meshlets()[1u]) == 1u);
        }
    );
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

