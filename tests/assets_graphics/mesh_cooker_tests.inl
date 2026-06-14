// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_FINAL)
static void ExpectCookFailure(
    TestContext& context,
    TestArena& testArena,
    const CookSingleMetaFn cookSingleMeta,
    const AStringView metaText,
    const AStringView caseName
){
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !cookSingleMeta(
        metaText,
        caseName,
        testArena,
        root,
        outputDirectory
    ));

    ErrorCode errorCode;
    static_cast<void>(RemoveAllIfExists(root, errorCode));
}

static void ExpectCookFailure(
    TestContext& context,
    TestArena& testArena,
    const CookSingleMetaFn cookSingleMeta,
    const AString& metaText,
    const AStringView caseName
){
    ExpectCookFailure(context, testArena, cookSingleMeta, AStringView(metaText.data(), metaText.size()), caseName);
}
#endif

static void TestMeshCookerTypedStreams(TestContext& context){
    CookAndCheckMinimalTypedAsset<NWB::Impl::Mesh>(
        context,
        s_MinimalMeshMeta,
        "minimal_mesh",
        MinimalAssetKind::Mesh,
        [&](const NWB::Impl::Mesh& loadedMesh){
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.meshClass() == NWB::Core::Mesh::MeshClass::Static);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.positionStream().size() == 3u);
            CheckMinimalRuntimeMeshletPayload(context, loadedMesh);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.positionStream()[0].x == -0.5f);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, LoadHalf4U(loadedMesh.normalStream()[0]).z == 1.f);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, LoadHalf4U(loadedMesh.colorStream()[2]).z == 1.f);
        }
    );
}

static void TestMeshCookerDefaultColors(TestContext& context){
    CookAndCheckMinimalTypedAsset<NWB::Impl::Mesh>(
        context,
        s_DefaultColorMeshMeta,
        "default_color_mesh",
        MinimalAssetKind::Mesh,
        [&](const NWB::Impl::Mesh& loadedMesh){
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.positionStream().size() == 3u);
            const Float4U color0 = LoadHalf4U(loadedMesh.colorStream()[0]);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, color0.x == 1.f);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, color0.w == 1.f);
        }
    );
}

#include "meshlet_ref_acceptance_helpers.inl"
#include "acceptance_tests.inl"

static void TestMeshCookerValidationFailures(TestContext& context){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    ExpectCookFailure(context, testArena, CookSingleMeshMeta, s_UnsupportedMeshFieldsMeta, "unsupported_mesh_fields");
    ExpectCookFailure(context, testArena, CookSingleMeshMeta, s_MismatchedMeshMeta, "mismatched_mesh_streams");
    ExpectCookFailure(
        context,
        testArena,
        CookSingleMeshMeta,
        BuildMeshTriangleMeta("", s_TriangleTangentField, s_TriangleVertexRefsField),
        "missing_mesh_normal_field"
    );
    ExpectCookFailure(
        context,
        testArena,
        CookSingleMeshMeta,
        BuildMeshTriangleMeta(
            s_TriangleNormalField,
            s_TriangleTangentField,
            s_TriangleMissingNormalVertexRefsField
        ),
        "missing_mesh_normal_vertex_ref"
    );
    ExpectCookFailure(
        context,
        testArena,
        CookSingleMeshMeta,
        BuildMeshTriangleMeta(s_EmptyNormalListField, s_TriangleTangentField, s_TriangleVertexRefsField),
        "empty_list_mesh_normal"
    );
    ExpectCookFailure(
        context,
        testArena,
        CookSingleMeshMeta,
        BuildMeshTriangleMeta(s_EmptyNormalMapField, s_TriangleTangentField, s_TriangleVertexRefsField),
        "empty_map_mesh_normal"
    );
    ExpectCookFailure(
        context,
        testArena,
        CookSingleMeshMeta,
        BuildMeshTriangleMeta(s_TriangleNormalField, "", s_TriangleVertexRefsField),
        "missing_mesh_tangent_field"
    );
    ExpectCookFailure(
        context,
        testArena,
        CookSingleMeshMeta,
        BuildMeshTriangleMeta(
            s_TriangleNormalField,
            s_TriangleTangentField,
            s_TriangleMissingTangentVertexRefsField
        ),
        "missing_mesh_tangent_vertex_ref"
    );
    ExpectCookFailure(
        context,
        testArena,
        CookSingleMeshMeta,
        BuildMeshTriangleMeta(s_TriangleNormalField, s_EmptyTangentListField, s_TriangleVertexRefsField),
        "empty_list_mesh_tangent"
    );
    ExpectCookFailure(
        context,
        testArena,
        CookSingleMeshMeta,
        BuildMeshTriangleMeta(s_TriangleNormalField, s_EmptyTangentMapField, s_TriangleVertexRefsField),
        "empty_map_mesh_tangent"
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() >= 10u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("unsupported asset field")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("'uv0' must be a list")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("'normals' must be a list")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("vertex_ref normal index is out of range")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("'normals' must not be empty")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("'tangents' must be a list")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("vertex_ref tangent index is out of range")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("'tangents' must not be empty")));
#else
    static_cast<void>(context);
#endif
}

static void TestMeshClassPolicyHelpers(TestContext& context){
    using namespace NWB::Core::Mesh;

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, MeshClassMatchesSkinPayload(MeshClass::Static, false));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !MeshClassMatchesSkinPayload(MeshClass::Static, true));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, MeshClassMatchesSkinPayload(MeshClass::Skinned, true));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !MeshClassMatchesSkinPayload(MeshClass::Skinned, false));
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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

