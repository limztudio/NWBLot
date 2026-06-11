// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_ModelFixtureMeshMeta =
R"(mesh mesh;

mesh.positions = [
    [-0.5, -0.5, 0.0],
    [ 0.5, -0.5, 0.0],
    [ 0.0,  0.5, 0.0],
];
mesh.normals = [
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
    [0.0, 0.0, 1.0],
];
mesh.tangents = [
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
    [1.0, 0.0, 0.0, 1.0],
];
mesh.uv0 = [
    [0.0, 0.0],
    [1.0, 0.0],
    [0.5, 1.0],
];
mesh.colors = [
    [1.0, 1.0, 1.0, 1.0],
];
mesh.vertex_refs = [
    [0, 0, 0, 0, 0],
    [1, 1, 1, 1, 0],
    [2, 2, 2, 2, 0],
];
mesh.indices = [
    [0, 1, 2],
];

)";

static constexpr AStringView s_ModelFixtureSkeletonMeta =
R"(skeleton skeleton;

skeleton.joints = [
    { "name": "root" },
    {
        "name": "hand",
        "parent": "root",
        "local_bind_pose": [
            [1, 0, 0, 0.25],
            [0, 1, 0, 0],
            [0, 0, 1, 0],
        ],
    },
];

)";

static constexpr AStringView s_ModelFixtureSkinMeta =
R"(skin skin;

skin.mesh = mesh;
skin.skeleton = skeleton;
skin.influences = [
    { "joints": [0, 0, 0, 0], "weights": [1, 0, 0, 0] },
    { "joints": [0, 0, 0, 0], "weights": [1, 0, 0, 0] },
    { "joints": [0, 0, 0, 0], "weights": [1, 0, 0, 0] },
];
skin.inverse_bind_matrices = [
    [
        [1, 0, 0, 0],
        [0, 1, 0, 0],
        [0, 0, 1, 0],
    ],
    [
        [1, 0, 0, 0],
        [0, 1, 0, 0],
        [0, 0, 1, 0],
    ],
];

)";

static constexpr AStringView s_ModelFixtureWrapperMeta =
R"(skinned_mesh body_wrapper;
body_wrapper.mesh = mesh;
body_wrapper.skin = skin;
body_wrapper.skeleton = skeleton;

skinned_mesh detail_wrapper;
detail_wrapper.mesh = mesh;
detail_wrapper.skin = skin;
detail_wrapper.skeleton = skeleton;

)";

static void AppendModelFixtureBase(AString& inOutMeta){
    AppendTestMeta(inOutMeta, s_ModelFixtureMeshMeta);
    AppendTestMeta(inOutMeta, s_ModelFixtureSkeletonMeta);
    AppendTestMeta(inOutMeta, s_ModelFixtureSkinMeta);
    AppendTestMeta(inOutMeta, s_ModelFixtureWrapperMeta);
}

static AString BuildValidModelBunchFixture(){
    AString meta;
    meta.reserve(4096u);
    AppendModelFixtureBase(meta);
    AppendTestMeta(meta, R"(model model;

model.skeletons = {
    "rig": skeleton,
};

model.skinned_meshes = {
    "body": body_wrapper,
    "detail": detail_wrapper,
};

asset_bunch bunch = [
    mesh,
    skeleton,
    skin,
    model,
];
)");
    return meta;
}

static const NWB::Impl::ModelSkinnedMeshObject* FindSkinnedModelObject(
    const NWB::Impl::Model& model,
    const Name objectName
){
    for(const NWB::Impl::ModelSkinnedMeshObject& object : model.skinnedMeshObjects()){
        if(object.name == objectName)
            return &object;
    }
    return nullptr;
}

static const NWB::Impl::ModelStaticMeshObject* FindStaticModelObject(
    const NWB::Impl::Model& model,
    const Name objectName
){
    for(const NWB::Impl::ModelStaticMeshObject& object : model.staticMeshObjects()){
        if(object.name == objectName)
            return &object;
    }
    return nullptr;
}

static bool LoadCookedModel(
    TestContext& context,
    TestArena& testArena,
    const Path& outputDirectory,
    const Name assetName,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset
){
    return LoadCookedAsset<NWB::Impl::ModelAssetCodec>(
        context,
        testArena,
        outputDirectory,
        assetName,
        outLoadedAsset,
        0u
    );
}

static void TestModelBunchLocalReferencesAndWrapperExpansion(TestContext& context){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root;
    Path outputDirectory;
    const AString meta = BuildValidModelBunchFixture();
    const bool cooked = CookSingleGraphicsMeta(
        AStringView(meta.data(), meta.size()),
        "model_bunch_local_references",
        "characters",
        "model_fixture.nwb",
        testArena,
        root,
        outputDirectory
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, cooked);
    if(!cooked)
        return;

    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    if(!LoadCookedModel(
        context,
        testArena,
        outputDirectory,
        Name("project/characters/model_fixture/model"),
        loadedAsset
    ))
        return;

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedAsset->assetType() == NWB::Impl::Model::AssetTypeName());
    const NWB::Impl::Model& model = static_cast<const NWB::Impl::Model&>(*loadedAsset);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, model.skeletonObjects().size() == 1u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, model.skinnedMeshObjects().size() == 2u);
    if(model.skeletonObjects().size() != 1u)
        return;

    const Name expectedMesh("project/characters/model_fixture/mesh");
    const Name expectedSkin("project/characters/model_fixture/skin");
    const Name expectedSkeleton("project/characters/model_fixture/skeleton");
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, model.skeletonObjects()[0].name == Name("rig"));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, model.skeletonObjects()[0].skeleton.name() == expectedSkeleton);

    const NWB::Impl::ModelSkinnedMeshObject* body = FindSkinnedModelObject(model, Name("body"));
    const NWB::Impl::ModelSkinnedMeshObject* detail = FindSkinnedModelObject(model, Name("detail"));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, body != nullptr);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, detail != nullptr);
    if(body){
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, body->mesh.name() == expectedMesh);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, body->skin.name() == expectedSkin);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, body->skeletonObject == Name("rig"));
    }
    if(detail){
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, detail->mesh.name() == expectedMesh);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, detail->skin.name() == expectedSkin);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, detail->skeletonObject == Name("rig"));
    }
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);
}

static AString BuildStaticAttachmentModelBunchFixture(){
    AString meta;
    meta.reserve(4096u);
    AppendTestMeta(meta, s_ModelFixtureMeshMeta);
    AppendTestMeta(meta, s_ModelFixtureSkeletonMeta);
    AppendTestMeta(meta, R"(model model;

model.skeletons = {
    "rig": skeleton,
};

model.static_meshes = {
    "tool": {
        "mesh": mesh,
        "parent_object": "rig",
        "parent_joint": "hand",
        "transform": [
            [1, 0, 0, 0.5],
            [0, 1, 0, 0.125],
            [0, 0, 1, -0.25],
        ],
    },
};

asset_bunch bunch = [
    mesh,
    skeleton,
    model,
];
)");
    return meta;
}

static void TestModelBunchStaticMeshAttachmentToNamedJoint(TestContext& context){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root;
    Path outputDirectory;
    const AString meta = BuildStaticAttachmentModelBunchFixture();
    const bool cooked = CookSingleGraphicsMeta(
        AStringView(meta.data(), meta.size()),
        "model_bunch_static_attachment",
        "characters",
        "model_attachment_fixture.nwb",
        testArena,
        root,
        outputDirectory
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, cooked);
    if(!cooked)
        return;

    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    if(!LoadCookedModel(
        context,
        testArena,
        outputDirectory,
        Name("project/characters/model_attachment_fixture/model"),
        loadedAsset
    ))
        return;

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedAsset->assetType() == NWB::Impl::Model::AssetTypeName());
    const NWB::Impl::Model& model = static_cast<const NWB::Impl::Model&>(*loadedAsset);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, model.skeletonObjects().size() == 1u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, model.staticMeshObjects().size() == 1u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, model.skinnedMeshObjects().empty());

    const NWB::Impl::ModelStaticMeshObject* tool = FindStaticModelObject(model, Name("tool"));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, tool != nullptr);
    if(tool){
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, tool->mesh.name() == Name("project/characters/model_attachment_fixture/mesh"));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !tool->material.valid());
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, tool->parentObject == Name("rig"));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, tool->parentJoint == Name("hand"));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, tool->transform._14 == 0.5f);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, tool->transform._24 == 0.125f);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, tool->transform._34 == -0.25f);
    }
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);
}

#if defined(NWB_FINAL)
static bool ExpandModelBunchFixture(
    const AStringView meta,
    const AStringView caseName,
    NWB::Core::Metascript::Document& doc,
    NWB::Impl::AssetsBunchCook::ExpandedAssetVector& outAssets,
    NWB::Core::Alloc::ScratchArena& scratchArena
){
    if(!doc.parse(meta))
        return false;

    const Path assetRoot = AssetsGraphicsTestCaseRoot(caseName) / "assets";
    const Path nwbFilePath = assetRoot / "characters" / "model_fixture.nwb";
    return NWB::Impl::AssetsBunchCook::ExpandAssetBunch(
        assetRoot,
        "project",
        nwbFilePath,
        doc,
        outAssets,
        scratchArena
    );
}
#endif

static void TestModelBunchRejectsDuplicateLocalReference(TestContext& context){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    AString meta;
    meta.reserve(2048u);
    AppendTestMeta(meta, s_ModelFixtureMeshMeta);
    AppendTestMeta(meta, R"(model model;

model.skeletons = {
    "rig": "project/characters/shared/skeleton",
};

asset_bunch bunch = [
    mesh,
    mesh,
    model,
];
)");

    TestArena testArena;
    NWB::Core::Metascript::Document doc(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena;
    NWB::Impl::AssetsBunchCook::ExpandedAssetVector expandedAssets(scratchArena);
    const bool expanded = ExpandModelBunchFixture(
        AStringView(meta.data(), meta.size()),
        "model_bunch_duplicate_local_reference",
        doc,
        expandedAssets,
        scratchArena
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !expanded);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("variable 'mesh' is listed more than once")));
#else
    static_cast<void>(context);
#endif
}

static void TestModelBunchRejectsMissingLocalReference(TestContext& context){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    AString meta;
    meta.reserve(2048u);
    AppendTestMeta(meta, s_ModelFixtureMeshMeta);
    AppendTestMeta(meta, R"(model model;

model.skeletons = {
    "rig": missing_skeleton,
};

asset_bunch bunch = [
    mesh,
    model,
];
)");

    TestArena testArena;
    NWB::Core::Metascript::Document doc(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena;
    NWB::Impl::AssetsBunchCook::ExpandedAssetVector expandedAssets(scratchArena);
    const bool expanded = ExpandModelBunchFixture(
        AStringView(meta.data(), meta.size()),
        "model_bunch_missing_local_reference",
        doc,
        expandedAssets,
        scratchArena
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !expanded);
#else
    static_cast<void>(context);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
