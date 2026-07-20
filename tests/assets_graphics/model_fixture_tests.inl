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

static void AppendModelFixtureBase(AString& inOutMeta){
    AppendTestMeta(inOutMeta, s_ModelFixtureMeshMeta);
    AppendTestMeta(inOutMeta, s_ModelFixtureSkeletonMeta);
    AppendTestMeta(inOutMeta, s_ModelFixtureSkinMeta);
}

static AString BuildValidModelBunchFixture(){
    AString meta;
    meta.reserve(4096u);
    AppendModelFixtureBase(meta);
    AppendTestMeta(meta, R"(model model;

model.skeletons = {
    "rig": {
        "skeleton": skeleton,
    },
};

model.skinned_meshes = {
    "body": {
        "mesh": mesh,
        "skin": skin,
        "skeleton": "rig",
    },
    "detail": {
        "mesh": mesh,
        "skin": skin,
        "skeleton": "rig",
    },
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
    TestArena& testArena,
    const Path& outputDirectory,
    const Name assetName,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset
){
    return LoadCookedAsset<NWB::Impl::ModelAssetCodec>(
        testArena,
        outputDirectory,
        assetName,
        outLoadedAsset,
        0u
    );
}

TEST(AssetsGraphics, ModelBunchLocalReferencesAndWrapperExpansion){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
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
    EXPECT_TRUE(cooked);
    if(!cooked)
        return;

    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    if(!LoadCookedModel(
        testArena,
        outputDirectory,
        Name("project/characters/model_fixture/model"),
        loadedAsset
    ))
        return;

    EXPECT_EQ(loadedAsset->assetType(), NWB::Impl::Model::AssetTypeName());
    const NWB::Impl::Model& model = static_cast<const NWB::Impl::Model&>(*loadedAsset);
    EXPECT_EQ(model.skeletonObjects().size(), 1u);
    EXPECT_EQ(model.skinnedMeshObjects().size(), 2u);
    if(model.skeletonObjects().size() != 1u)
        return;

    const Name expectedMesh("project/characters/model_fixture/mesh");
    const Name expectedSkin("project/characters/model_fixture/skin");
    const Name expectedSkeleton("project/characters/model_fixture/skeleton");
    EXPECT_EQ(model.skeletonObjects()[0].name, Name("rig"));
    EXPECT_EQ(model.skeletonObjects()[0].skeleton.name(), expectedSkeleton);

    const NWB::Impl::ModelSkinnedMeshObject* body = FindSkinnedModelObject(model, Name("body"));
    const NWB::Impl::ModelSkinnedMeshObject* detail = FindSkinnedModelObject(model, Name("detail"));
    EXPECT_NE(body, nullptr);
    EXPECT_NE(detail, nullptr);
    if(body){
        EXPECT_EQ(body->mesh.name(), expectedMesh);
        EXPECT_EQ(body->skin.name(), expectedSkin);
        EXPECT_EQ(body->skeletonObject, Name("rig"));
    }
    if(detail){
        EXPECT_EQ(detail->mesh.name(), expectedMesh);
        EXPECT_EQ(detail->skin.name(), expectedSkin);
        EXPECT_EQ(detail->skeletonObject, Name("rig"));
    }
    EXPECT_EQ(logger.errorCount(), 0u);
}

static AString BuildStaticAttachmentModelBunchFixture(){
    AString meta;
    meta.reserve(4096u);
    AppendTestMeta(meta, s_ModelFixtureMeshMeta);
    AppendTestMeta(meta, s_ModelFixtureSkeletonMeta);
    AppendTestMeta(meta, R"(model model;

model.skeletons = {
    "rig": {
        "skeleton": skeleton,
    },
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

TEST(AssetsGraphics, ModelBunchStaticMeshAttachmentToNamedJoint){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
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
    EXPECT_TRUE(cooked);
    if(!cooked)
        return;

    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    if(!LoadCookedModel(
        testArena,
        outputDirectory,
        Name("project/characters/model_attachment_fixture/model"),
        loadedAsset
    ))
        return;

    EXPECT_EQ(loadedAsset->assetType(), NWB::Impl::Model::AssetTypeName());
    const NWB::Impl::Model& model = static_cast<const NWB::Impl::Model&>(*loadedAsset);
    EXPECT_EQ(model.skeletonObjects().size(), 1u);
    EXPECT_EQ(model.staticMeshObjects().size(), 1u);
    EXPECT_TRUE(model.skinnedMeshObjects().empty());

    const NWB::Impl::ModelStaticMeshObject* tool = FindStaticModelObject(model, Name("tool"));
    EXPECT_NE(tool, nullptr);
    if(tool){
        EXPECT_EQ(tool->mesh.name(), Name("project/characters/model_attachment_fixture/mesh"));
        EXPECT_FALSE(tool->material.valid());
        EXPECT_EQ(tool->parentObject, Name("rig"));
        EXPECT_EQ(tool->parentJoint, Name("hand"));
        EXPECT_EQ(tool->transform._14, 0.5f);
        EXPECT_EQ(tool->transform._24, 0.125f);
        EXPECT_EQ(tool->transform._34, -0.25f);
    }
    EXPECT_EQ(logger.errorCount(), 0u);
}

#if defined(NWB_FINAL)
static bool ExpandModelBunchFixture(
    TestArena& testArena,
    const AStringView meta,
    const AStringView caseName,
    NWB::Core::Metascript::Document& doc,
    NWB::Core::Assets::AssetsBunchCook::ExpandedAssetVector& outAssets,
    NWB::Core::Alloc::ScratchArena& scratchArena
){
    if(!doc.parse(meta))
        return false;

    const Path assetRoot = AssetsGraphicsTestCaseRoot(testArena, caseName) / "assets";
    const Path nwbFilePath = assetRoot / "characters" / "model_fixture.nwb";
    return NWB::Core::Assets::AssetsBunchCook::ExpandAssetBunch(
        assetRoot,
        "project",
        nwbFilePath,
        doc,
        outAssets,
        scratchArena
    );
}
#endif

TEST(AssetsGraphics, ModelBunchRejectsDuplicateLocalReference){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    AString meta;
    meta.reserve(2048u);
    AppendTestMeta(meta, s_ModelFixtureMeshMeta);
    AppendTestMeta(meta, R"(model model;

model.skeletons = {
    "rig": {
        "skeleton": "project/characters/shared/skeleton",
    },
};

asset_bunch bunch = [
    mesh,
    mesh,
    model,
];
)");

    TestArena testArena;
    NWB::Core::Metascript::Document doc(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena(s_ModelFixtureScratchArena);
    NWB::Core::Assets::AssetsBunchCook::ExpandedAssetVector expandedAssets(scratchArena);
    const bool expanded = ExpandModelBunchFixture(
        testArena,
        AStringView(meta.data(), meta.size()),
        "model_bunch_duplicate_local_reference",
        doc,
        expandedAssets,
        scratchArena
    );
    EXPECT_FALSE(expanded);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("variable 'mesh' is listed more than once")));
#else
#endif
}

TEST(AssetsGraphics, ModelBunchRejectsMissingLocalReference){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    AString meta;
    meta.reserve(2048u);
    AppendTestMeta(meta, s_ModelFixtureMeshMeta);
    AppendTestMeta(meta, R"(model model;

model.skeletons = {
    "rig": {
        "skeleton": missing_skeleton,
    },
};

asset_bunch bunch = [
    mesh,
    model,
];
)");

    TestArena testArena;
    NWB::Core::Metascript::Document doc(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena(s_ModelFixtureScratchArena);
    NWB::Core::Assets::AssetsBunchCook::ExpandedAssetVector expandedAssets(scratchArena);
    const bool expanded = ExpandModelBunchFixture(
        testArena,
        AStringView(meta.data(), meta.size()),
        "model_bunch_missing_local_reference",
        doc,
        expandedAssets,
        scratchArena
    );
    EXPECT_FALSE(expanded);
#else
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

