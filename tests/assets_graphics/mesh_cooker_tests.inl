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
    Path root;
    Path outputDirectory;
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

struct SkinnedMeshPayloadEdit{
    NWB::Core::Assets::AssetVector<Float3U> positions;
    NWB::Core::Assets::AssetVector<Half4U> normals;
    NWB::Core::Assets::AssetVector<Half4U> tangents;
    NWB::Core::Assets::AssetVector<Float2U> uv0;
    NWB::Core::Assets::AssetVector<Half4U> colors;
    NWB::Core::Assets::AssetVector<NWB::Impl::SkinInfluence4> skin;
    NWB::Core::Assets::AssetVector<NWB::Impl::SkeletonJointMatrix> inverseBindMatrices;
    NWB::Core::Assets::AssetVector<NWB::Impl::MeshletDesc> meshlets;
    NWB::Core::Assets::AssetVector<NWB::Impl::MeshletBounds> meshletBounds;
    NWB::Core::Assets::AssetVector<u8> meshletPositionRefDeltas;
    NWB::Core::Assets::AssetVector<u8> meshletAttributeRefDeltas;
    NWB::Core::Assets::AssetVector<NWB::Impl::MeshletLocalVertexRef> meshletLocalVertexRefs;
    NWB::Core::Assets::AssetVector<u8> meshletPrimitiveIndices;
    u32 meshClass = NWB::Core::Mesh::MeshClass::Invalid;
    u32 skeletonJointCount = 0u;

    SkinnedMeshPayloadEdit(TestArena& testArena, const NWB::Impl::SkinnedMesh& mesh)
        : positions(MakeAssetVectorFrom(testArena, mesh.positionStream()))
        , normals(MakeAssetVectorFrom(testArena, mesh.normalStream()))
        , tangents(MakeAssetVectorFrom(testArena, mesh.tangentStream()))
        , uv0(MakeAssetVectorFrom(testArena, mesh.uv0Stream()))
        , colors(MakeAssetVectorFrom(testArena, mesh.colorStream()))
        , skin(MakeAssetVectorFrom(testArena, mesh.skinStream()))
        , inverseBindMatrices(MakeAssetVectorFrom(testArena, mesh.inverseBindMatrices()))
        , meshlets(MakeAssetVectorFrom(testArena, mesh.meshlets()))
        , meshletBounds(MakeAssetVectorFrom(testArena, mesh.meshletBounds()))
        , meshletPositionRefDeltas(MakeAssetVectorFrom(testArena, mesh.meshletPositionRefDeltas()))
        , meshletAttributeRefDeltas(MakeAssetVectorFrom(testArena, mesh.meshletAttributeRefDeltas()))
        , meshletLocalVertexRefs(MakeAssetVectorFrom(testArena, mesh.meshletLocalVertexRefs()))
        , meshletPrimitiveIndices(MakeAssetVectorFrom(testArena, mesh.meshletPrimitiveIndices()))
        , meshClass(mesh.meshClass())
        , skeletonJointCount(mesh.skeletonJointCount())
    {}

    void applyTo(NWB::Impl::SkinnedMesh& mesh){
        SetSkinnedMeshPayload(
            mesh, meshClass, skeletonJointCount,
            positions, normals, tangents, uv0, colors, skin, inverseBindMatrices,
            meshlets, meshletBounds, meshletPositionRefDeltas, meshletAttributeRefDeltas, meshletLocalVertexRefs,
            meshletPrimitiveIndices
        );
    }
};

template<typename MutateFnT>
static void CheckInvalidSkinnedMesh(TestContext& context, MutateFnT mutate){
    TestArena testArena;
    NWB::Impl::SkinnedMesh mesh = BuildValidSkinnedMesh(testArena);
    SkinnedMeshPayloadEdit edit(testArena, mesh);
    mutate(edit);
    edit.applyTo(mesh);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !mesh.validatePayload());
}

static void TestSkinnedMeshValidationFailures(TestContext& context){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    CheckInvalidSkinnedMesh(context, [](SkinnedMeshPayloadEdit& edit){
        edit.normals[0u] = MakeHalf4U(0.0f, 0.0f, 0.0f, 0.0f);
    });

    CheckInvalidSkinnedMesh(context, [](SkinnedMeshPayloadEdit& edit){
        edit.tangents[0u] = MakeHalf4U(1.0f, 0.0f, 0.0f, 0.0f);
    });

    CheckInvalidSkinnedMesh(context, [](SkinnedMeshPayloadEdit& edit){
        edit.tangents[0u] = MakeHalf4U(0.0f, 0.0f, 0.0f, 1.0f);
    });

    CheckInvalidSkinnedMesh(context, [](SkinnedMeshPayloadEdit& edit){
        edit.skin[0u].weight[0u] = 0.5f;
    });

    CheckInvalidSkinnedMesh(context, [](SkinnedMeshPayloadEdit& edit){
        edit.skin.pop_back();
    });

    CheckInvalidSkinnedMesh(context, [](SkinnedMeshPayloadEdit& edit){
        edit.skeletonJointCount = 0u;
    });

    CheckInvalidSkinnedMesh(context, [](SkinnedMeshPayloadEdit& edit){
        edit.inverseBindMatrices.push_back(MakeJointMatrix(0.0f, 0.0f, 0.0f));
    });

    CheckInvalidSkinnedMesh(context, [](SkinnedMeshPayloadEdit& edit){
        edit.inverseBindMatrices[0u].rows[0] = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    });

    CheckInvalidSkinnedMesh(context, [](SkinnedMeshPayloadEdit& edit){
        edit.skin[0u].joint[0u] = 1u;
    });

    CheckInvalidSkinnedMesh(context, [](SkinnedMeshPayloadEdit& edit){
        edit.meshletPrimitiveIndices.pop_back();
    });

    CheckInvalidSkinnedMesh(context, [](SkinnedMeshPayloadEdit& edit){
        edit.meshletPrimitiveIndices[2u] = 99u;
    });

    CheckInvalidSkinnedMesh(context, [](SkinnedMeshPayloadEdit& edit){
        edit.meshletBounds[0u].sphere.w = -1.0f;
    });

    CheckInvalidSkinnedMesh(context, [](SkinnedMeshPayloadEdit& edit){
        edit.meshlets[0u].encoding |= 3u << NWB::Impl::s_MeshletRefEncodingPositionShift;
    });

    CheckInvalidSkinnedMesh(context, [](SkinnedMeshPayloadEdit& edit){
        ++edit.meshlets[0u].positionRefOffset;
    });

    CheckInvalidSkinnedMesh(context, [](SkinnedMeshPayloadEdit& edit){
        edit.meshletPositionRefDeltas.push_back(0u);
    });

    CheckInvalidSkinnedMesh(context, [](SkinnedMeshPayloadEdit& edit){
        edit.meshletLocalVertexRefs[1u].localAttribute = 0u;
    });

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() >= 15u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("normal 0 is invalid")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("tangent 0 is invalid")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("skin influence 0 is invalid")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("meshlet 0 has invalid encoded position ref")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("has skin but no skeleton joint count")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("inverse bind matrix count must match skeleton joint count")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("inverse bind matrices are invalid")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("meshlet 0 exceeds meshlet stream bounds")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("meshlet 0 primitive 0 has an out-of-range local vertex")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("meshlet 0 has invalid bounds")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("meshlet 0 has invalid ref encoding width")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("meshlet 0 has non-contiguous offsets")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("meshlet streams contain trailing data")));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("shared across skin identities")));
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

