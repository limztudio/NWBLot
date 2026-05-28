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

static bool CookAndLoadSmokeSkinnedMesh(
    TestContext& context,
    TestArena& testArena,
    const char* assetFilename,
    const AStringView caseName,
    const Name assetName,
    Path& outRoot,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset
){
    Path outputDirectory;
    const bool cooked = CookSmokeSkinnedMeshMeta(
        assetFilename,
        caseName,
        testArena,
        outRoot,
        outputDirectory
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, cooked);
    if(!cooked)
        return false;
    return LoadCookedSkinnedMesh(context, testArena, outputDirectory, assetName, outLoadedAsset);
}

static void TestMeshAcceptanceHardEdgeCubeZippedRefs(TestContext& context){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root;
    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    if(CookAndLoadSmokeMesh(
        context,
        testArena,
        "cube_hard_edges.nwb",
        "hard_edge_cube",
        Name("project/meshes/cube_hard_edges"),
        root,
        loadedAsset
    )){
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedAsset->assetType() == NWB::Impl::Mesh::AssetTypeName());
        const NWB::Impl::Mesh& loadedMesh = static_cast<const NWB::Impl::Mesh&>(*loadedAsset);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.positionStream().size() == 8u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.normalStream().size() == 6u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.tangentStream().size() == 6u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.uv0Stream().size() == 4u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.colorStream().size() == 1u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.meshlets().size() == 1u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.meshletPositionRefs().size() == 8u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.meshletAttributeRefs().size() == 24u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.meshletLocalVertexRefs().size() == 24u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.meshletPrimitiveIndices().size() == 36u);

        const NWB::Impl::MeshletDesc& meshlet = loadedMesh.meshlets()[0];
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Impl::MeshletVertexCount(meshlet) == 24u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Impl::MeshletPrimitiveCount(meshlet) == 12u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Impl::MeshletPositionCount(meshlet) == 8u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Impl::MeshletAttributeCount(meshlet) == 24u);

        const auto& localRefs = loadedMesh.meshletLocalVertexRefs();
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, localRefs[0u].localDeformedPosition == localRefs[8u].localDeformedPosition);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, localRefs[0u].localDeformedPosition == localRefs[16u].localDeformedPosition);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, localRefs[0u].localAttribute != localRefs[8u].localAttribute);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, localRefs[0u].localAttribute != localRefs[16u].localAttribute);

        const auto& attributeRefs = loadedMesh.meshletAttributeRefs();
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, attributeRefs[localRefs[0u].localAttribute].normal == 0u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, attributeRefs[localRefs[8u].localAttribute].normal == 2u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, attributeRefs[localRefs[16u].localAttribute].normal == 4u);
    }
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);

    ErrorCode errorCode;
    static_cast<void>(RemoveAllIfExists(root, errorCode));
}

static void TestMeshAcceptanceSphereSmooth(TestContext& context){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root;
    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    if(CookAndLoadSmokeMesh(
        context,
        testArena,
        "sphere_smooth.nwb",
        "sphere_smooth",
        Name("project/meshes/sphere_smooth"),
        root,
        loadedAsset
    )){
        const NWB::Impl::Mesh& loadedMesh = static_cast<const NWB::Impl::Mesh&>(*loadedAsset);
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
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);

    ErrorCode errorCode;
    static_cast<void>(RemoveAllIfExists(root, errorCode));
}

static void TestMeshAcceptanceUvSeamQuad(TestContext& context){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root;
    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    if(CookAndLoadSmokeMesh(
        context,
        testArena,
        "uv_seam_quad.nwb",
        "uv_seam_quad",
        Name("project/meshes/uv_seam_quad"),
        root,
        loadedAsset
    )){
        const NWB::Impl::Mesh& loadedMesh = static_cast<const NWB::Impl::Mesh&>(*loadedAsset);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.positionStream().size() == 4u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.uv0Stream().size() == 6u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.tangentStream().size() == 2u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.meshletPositionRefs().size() == 4u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.meshletAttributeRefs().size() == 6u);

        const auto& localRefs = loadedMesh.meshletLocalVertexRefs();
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, localRefs[0u].localDeformedPosition == localRefs[3u].localDeformedPosition);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, localRefs[2u].localDeformedPosition == localRefs[4u].localDeformedPosition);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, localRefs[0u].localAttribute != localRefs[3u].localAttribute);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, localRefs[2u].localAttribute != localRefs[4u].localAttribute);

        const auto& attributeRefs = loadedMesh.meshletAttributeRefs();
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, attributeRefs[localRefs[0u].localAttribute].uv0 == 0u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, attributeRefs[localRefs[3u].localAttribute].uv0 == 3u);
    }
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);

    ErrorCode errorCode;
    static_cast<void>(RemoveAllIfExists(root, errorCode));
}

static void TestMeshAcceptanceMirroredUvQuad(TestContext& context){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root;
    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    if(CookAndLoadSmokeMesh(
        context,
        testArena,
        "mirrored_uv_quad.nwb",
        "mirrored_uv_quad",
        Name("project/meshes/mirrored_uv_quad"),
        root,
        loadedAsset
    )){
        const NWB::Impl::Mesh& loadedMesh = static_cast<const NWB::Impl::Mesh&>(*loadedAsset);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.tangentStream().size() == 2u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, LoadHalf4U(loadedMesh.tangentStream()[0u]).w == 1.0f);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, LoadHalf4U(loadedMesh.tangentStream()[1u]).w == -1.0f);

        const auto& localRefs = loadedMesh.meshletLocalVertexRefs();
        const auto& attributeRefs = loadedMesh.meshletAttributeRefs();
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, attributeRefs[localRefs[0u].localAttribute].tangent == 0u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, attributeRefs[localRefs[3u].localAttribute].tangent == 1u);
    }
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);

    ErrorCode errorCode;
    static_cast<void>(RemoveAllIfExists(root, errorCode));
}

static void TestMeshAcceptanceTwoSidedPlane(TestContext& context){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root;
    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    if(CookAndLoadSmokeMesh(
        context,
        testArena,
        "two_sided_plane.nwb",
        "two_sided_plane",
        Name("project/meshes/two_sided_plane"),
        root,
        loadedAsset
    )){
        const NWB::Impl::Mesh& loadedMesh = static_cast<const NWB::Impl::Mesh&>(*loadedAsset);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.positionStream().size() == 3u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.normalStream().size() == 2u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.meshletPositionRefs().size() == 3u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.meshletAttributeRefs().size() == 6u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::MeshletConeEnabled(loadedMesh.meshletBounds()[0u]));
    }
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);

    ErrorCode errorCode;
    static_cast<void>(RemoveAllIfExists(root, errorCode));
}

static void TestSkinnedMeshAcceptanceBendingStrip(TestContext& context){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root;
    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    if(CookAndLoadSmokeSkinnedMesh(
        context,
        testArena,
        "skinned_bending_strip.nwb",
        "skinned_bending_strip",
        Name("project/characters/skinned_bending_strip"),
        root,
        loadedAsset
    )){
        const NWB::Impl::SkinnedMesh& loadedMesh = static_cast<const NWB::Impl::SkinnedMesh&>(*loadedAsset);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.positionStream().size() == 4u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.skinStream().size() == 2u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.skeletonJointCount() == 2u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.meshletPositionRefs().size() == 6u);

        const auto& localRefs = loadedMesh.meshletLocalVertexRefs();
        const auto& positionRefs = loadedMesh.meshletPositionRefs();
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, positionRefs[localRefs[1u].localDeformedPosition].position == 1u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, positionRefs[localRefs[1u].localDeformedPosition].skin == 0u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, positionRefs[localRefs[3u].localDeformedPosition].position == 1u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, positionRefs[localRefs[3u].localDeformedPosition].skin == 1u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, localRefs[1u].localDeformedPosition != localRefs[3u].localDeformedPosition);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, localRefs[2u].localDeformedPosition != localRefs[5u].localDeformedPosition);
    }
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);

    ErrorCode errorCode;
    static_cast<void>(RemoveAllIfExists(root, errorCode));
}

static void TestMeshAcceptanceLargeManyMeshlets(TestContext& context){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root;
    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    if(CookAndLoadSmokeMesh(
        context,
        testArena,
        "large_mesh_many_meshlets.nwb",
        "large_mesh_many_meshlets",
        Name("project/meshes/large_mesh_many_meshlets"),
        root,
        loadedAsset
    )){
        const NWB::Impl::Mesh& loadedMesh = static_cast<const NWB::Impl::Mesh&>(*loadedAsset);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.meshlets().size() == 2u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.meshletPrimitiveIndices().size() == 291u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.meshletPositionRefs().size() == 6u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.meshletAttributeRefs().size() == 6u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.meshletLocalVertexRefs().size() == 6u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Impl::MeshletPrimitiveCount(loadedMesh.meshlets()[0u]) == 96u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Impl::MeshletPrimitiveCount(loadedMesh.meshlets()[1u]) == 1u);
    }
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);

    ErrorCode errorCode;
    static_cast<void>(RemoveAllIfExists(root, errorCode));
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

