// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::Core::Assets::AssetBytes AssetsGraphicsFixture::makeAssetBytes(AssetsGraphicsFixture::TestArena& testArena)
{
    return NWB::Core::Assets::AssetBytes(testArena.arena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(GLB_FINAL)
#endif
void AssetsGraphicsFixture::appendTestMeta(AssetsGraphicsFixture::AString& inOutMeta, const AStringView text)
{
    inOutMeta.append(text.data(), text.size());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(GLB_FINAL)
AssetsGraphicsFixture::AString AssetsGraphicsFixture::buildTriangleMeta(
    const AStringView assetHeader,
    const AStringView normalField,
    const AStringView tangentField,
    const AStringView vertexRefsField,
    const AStringView suffix
)
{
    AString meta;
    meta.reserve(1536u);
    appendTestMeta(meta, assetHeader);
    appendTestMeta(meta, NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_POSITIONS);
    appendTestMeta(meta, normalField);
    appendTestMeta(meta, tangentField);
    appendTestMeta(meta, NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_UV0);
    appendTestMeta(meta, NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_COLORS);
    appendTestMeta(meta, vertexRefsField);
    appendTestMeta(meta, NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_INDICES);
    appendTestMeta(meta, suffix);
    return meta;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AssetsGraphicsFixture::AString AssetsGraphicsFixture::buildMeshTriangleMeta(
    const AStringView normalField,
    const AStringView tangentField,
    const AStringView vertexRefsField
)
{
    return buildTriangleMeta("mesh asset;\n\n", normalField, tangentField, vertexRefsField, "");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif
#if defined(GLB_FINAL)
#endif
#if defined(GLB_FINAL)
#endif
#if defined(GLB_FINAL)
#endif
#if defined(GLB_FINAL)
#endif
#if defined(GLB_FINAL)
#endif
#if defined(GLB_FINAL)
#endif
#if defined(GLB_FINAL)
#endif
#if defined(GLB_FINAL)
#endif
#if defined(GLB_FINAL)
#endif
bool AssetsGraphicsFixture::prepareCleanDirectory(const AssetsGraphicsFixture::Path& directory)
{
    ErrorCode errorCode;
    if(!RemoveAllIfExists(directory, errorCode))
        return false;
    errorCode.clear();
    return EnsureDirectories(directory, errorCode);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::writeTextFile(const AssetsGraphicsFixture::Path& filePath, const AStringView text)
{
    ErrorCode errorCode;
    if(!EnsureDirectories(filePath.parentPath(), errorCode))
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AStringView AssetsGraphicsFixture::assetsGraphicsTestConfigurationName()
{
#if defined(GLB_DEBUG)
    return "dbg";
#elif defined(GLB_FINAL)
    return "fin";
#else
    return "opt";
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AssetsGraphicsFixture::Path AssetsGraphicsFixture::assetsGraphicsTestRepoRoot(AssetsGraphicsFixture::TestArena& testArena)
{
    return Path(testArena.arena, __FILE__).parentPath().parentPath().parentPath().parentPath().lexicallyNormal();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AssetsGraphicsFixture::Path AssetsGraphicsFixture::assetsGraphicsTestCaseRoot(AssetsGraphicsFixture::TestArena& testArena, const AStringView caseName)
{
    // Object-cache paths append a wide asset-type hash and cache key. This target is RUN_SERIAL and each cook
    // fixture clears its root, so retain fixture isolation through a compact config-plus-case key that keeps every
    // generated Windows path below MAX_PATH.
    AString caseKey;
    caseKey.reserve(1u + s_HexU32DigitCount);
    caseKey += assetsGraphicsTestConfigurationName()[0u];
    AppendHexU32(static_cast<u32>(ComputeFnv64Text(caseName)), caseKey);
    return assetsGraphicsTestRepoRoot(testArena) / "__build_obj" / "c" / "a" / caseKey;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::prepareAssetsGraphicsCaseRoot(AssetsGraphicsFixture::TestArena& testArena, const AStringView caseName, AssetsGraphicsFixture::Path& outRoot)
{
    outRoot = assetsGraphicsTestCaseRoot(testArena, caseName);
    return prepareCleanDirectory(outRoot);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::prepareAssetsGraphicsCookCase(
    AssetsGraphicsFixture::TestArena& testArena,
    const AStringView caseName,
    AssetsGraphicsFixture::Path& outRoot,
    AssetsGraphicsFixture::Path& outOutputDirectory
)
{
    if(!prepareAssetsGraphicsCaseRoot(testArena, caseName, outRoot))
        return false;

    outOutputDirectory = outRoot / "cooked";
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::buildPreparedGraphicsAssetRoots(
    AssetsGraphicsFixture::TestArena& testArena,
    const AssetsGraphicsFixture::Path& root,
    const AssetsGraphicsFixture::Path& outputDirectory,
    const InitializerList<AssetsGraphicsFixture::Path> assetRoots,
    const u32 workerThreadCount
)
{
    NWB::Core::CpuTaskScheduler cookCpuTaskScheduler(workerThreadCount);
    NWB::Pipeline::AssetBuilder::AssetBuildOptions options(testArena.arena, cookCpuTaskScheduler);
    options.repoRoot = PathToString(testArena.arena, assetsGraphicsTestRepoRoot(testArena));
    options.assetRoots.reserve(assetRoots.size());
    for(const Path& assetRoot : assetRoots){
        auto parentDirectoryName = PathToString(testArena.arena, assetRoot.lexicallyNormal().parentPath().filename());
        CanonicalizeTextInPlace(parentDirectoryName);

        ACompactString virtualRoot;
        if(!virtualRoot.assign(parentDirectoryName == "impl"
            ? NWB::Core::Assets::s_EngineVirtualRoot
            : NWB::Core::Assets::s_ProjectVirtualRoot
        ))
            return false;

        auto assetRootText = PathToString(testArena.arena, assetRoot);
        options.assetRoots.emplace_back(
            testArena.arena,
            AStringView(assetRootText.data(), assetRootText.size()),
            virtualRoot
        );
    }
    options.outputDirectory = PathToString(testArena.arena, outputDirectory);
    options.cacheDirectory = PathToString(testArena.arena, root / "cache");
    if(!options.configuration.assign("tests") || !options.assetType.assign("graphics"))
        return false;

    return NWB::Pipeline::AssetBuilder::BuildAssets(options);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::cookPreparedGraphicsAssetRoots(
    AssetsGraphicsFixture::TestArena& testArena,
    const AssetsGraphicsFixture::Path& root,
    const AssetsGraphicsFixture::Path& outputDirectory,
    const InitializerList<AssetsGraphicsFixture::Path> assetRoots,
    const u32 workerThreadCount
)
{
    const Path builtDirectory = root / "built";
    if(!buildPreparedGraphicsAssetRoots(testArena, root, builtDirectory, assetRoots, workerThreadCount))
        return false;

    NWB::Pipeline::AssetGatherer::AssetGatherOptions gatherOptions(testArena.arena);
    gatherOptions.inputs.emplace_back(PathToString(testArena.arena, builtDirectory));
    gatherOptions.outputDirectory = PathToString(testArena.arena, outputDirectory);
    gatherOptions.configuration = "tests";
    gatherOptions.mergePayloads = &NWB::Impl::MergeGatheredGraphicsAsset;
    return NWB::Pipeline::AssetGatherer::GatherAssets(gatherOptions);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::cookSingleGraphicsMeta(
    const AStringView metaText,
    const AStringView caseName,
    AStringView assetDirectory,
    AStringView assetFilename,
    AssetsGraphicsFixture::TestArena& testArena,
    AssetsGraphicsFixture::Path& outRoot,
    AssetsGraphicsFixture::Path& outOutputDirectory
)
{
    if(!prepareAssetsGraphicsCookCase(testArena, caseName, outRoot, outOutputDirectory))
        return false;

    const Path assetRoot = outRoot / "assets";
    const Path metaPath = assetRoot / assetDirectory / assetFilename;
    if(!writeTextFile(metaPath, metaText))
        return false;

    return cookPreparedGraphicsAssetRoots(testArena, outRoot, outOutputDirectory, { assetRoot });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::cookSingleMinimalAssetMeta(
    const AStringView metaText,
    const AStringView caseName,
    const AssetsGraphicsFixture::MinimalAssetCookInfo& cookInfo,
    AssetsGraphicsFixture::TestArena& testArena,
    AssetsGraphicsFixture::Path& outRoot,
    AssetsGraphicsFixture::Path& outOutputDirectory
)
{
    return cookSingleGraphicsMeta(
        metaText,
        caseName,
        cookInfo.assetDirectory,
        cookInfo.assetFilename,
        testArena,
        outRoot,
        outOutputDirectory
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::cookSingleMeshMeta(
    const AStringView metaText,
    const AStringView caseName,
    AssetsGraphicsFixture::TestArena& testArena,
    AssetsGraphicsFixture::Path& outRoot,
    AssetsGraphicsFixture::Path& outOutputDirectory
)
{
    static constexpr MinimalAssetCookInfo s_CookInfo{ "meshes", "minimal_mesh.nwb" };
    return cookSingleMinimalAssetMeta(metaText, caseName, s_CookInfo, testArena, outRoot, outOutputDirectory);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::readSmokeAssetMeta(
    AssetsGraphicsFixture::TestArena& testArena,
    AStringView assetDirectory,
    AStringView assetFilename,
    AssetsGraphicsFixture::AString& outMetaText
)
{
    return ReadTextFile(
        assetsGraphicsTestRepoRoot(testArena) / "tests" / "smoke" / "assets" / assetDirectory / assetFilename,
        outMetaText
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::cookSmokeAssetMeta(
    AStringView assetDirectory,
    AStringView assetFilename,
    const AStringView caseName,
    AssetsGraphicsFixture::TestArena& testArena,
    AssetsGraphicsFixture::Path& outRoot,
    AssetsGraphicsFixture::Path& outOutputDirectory
)
{
    AString metaText;
    if(!readSmokeAssetMeta(testArena, assetDirectory, assetFilename, metaText))
        return false;

    return cookSingleGraphicsMeta(
        AStringView(metaText.data(), metaText.size()),
        caseName,
        assetDirectory,
        assetFilename,
        testArena,
        outRoot,
        outOutputDirectory
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::cookSmokeMeshMeta(
    AStringView assetFilename,
    const AStringView caseName,
    AssetsGraphicsFixture::TestArena& testArena,
    AssetsGraphicsFixture::Path& outRoot,
    AssetsGraphicsFixture::Path& outOutputDirectory
)
{
    return cookSmokeAssetMeta("meshes", assetFilename, caseName, testArena, outRoot, outOutputDirectory);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_fixture{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool WriteMaterialBindDiscoveryMetadata(const AssetsGraphicsFixture::Path& assetRoot){
    if(!AssetsGraphicsFixture::writeTextFile(
        assetRoot / "material_interfaces" / "bind_discovery.nwb",
        "include asset;\r\n\r\nasset.defines = { \"NWB_TEST_MATERIAL_BIND_DISCOVERY\": [\"1\"] };\r\n"
    ))
        return false;
    return AssetsGraphicsFixture::writeTextFile(
        assetRoot / "material_interfaces" / "bind_discovery.slangi",
        "// limztudio@gmail.com\r\n"
        "////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////\r\n\r\n\r\n"
        "#ifndef NWB_TEST_MATERIAL_BIND_DISCOVERY_SLANGI\r\n"
        "#define NWB_TEST_MATERIAL_BIND_DISCOVERY_SLANGI\r\n\r\n\r\n"
        "////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////\r\n\r\n\r\n"
        "#endif\r\n\r\n\r\n"
        "////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////\r\n\r\n"
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::cookMinimalMeshWithMaterialBind(
    const AStringView bindText,
    const AStringView caseName,
    AssetsGraphicsFixture::TestArena& testArena,
    AssetsGraphicsFixture::Path& outRoot,
    AssetsGraphicsFixture::Path& outOutputDirectory
)
{
    if(!prepareAssetsGraphicsCookCase(testArena, caseName, outRoot, outOutputDirectory))
        return false;

    const Path assetRoot = outRoot / "assets";
    if(!__hidden_assets_graphics_fixture::WriteMaterialBindDiscoveryMetadata(assetRoot))
        return false;
    if(!writeTextFile(assetRoot / "meshes" / "minimal_mesh.nwb", s_MinimalMeshMeta))
        return false;
    if(!writeTextFile(assetRoot / "material_interfaces" / "test_surface.bind", bindText))
        return false;

    return cookPreparedGraphicsAssetRoots(testArena, outRoot, outOutputDirectory, { assetRoot });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::parseMaterialBindFromText(
    AssetsGraphicsFixture::TestArena& testArena,
    const AStringView bindText,
    const AStringView caseName,
    NWB::Impl::MaterialBindEntry& outEntry,
    AssetsGraphicsFixture::Path& outRoot,
    NWB::Core::Alloc::ScratchArena& scratchArena
)
{
    if(!prepareAssetsGraphicsCaseRoot(testArena, caseName, outRoot))
        return false;

    const Path bindPath = outRoot / "assets" / "material_interfaces" / "test_surface.bind";
    if(!writeTextFile(bindPath, bindText))
        return false;

    return NWB::Impl::ParseMaterialBindSource(bindPath, outEntry, scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(GLB_FINAL)
bool AssetsGraphicsFixture::cookDuplicateGeneratedMaterialBindIncludePath(
    const AStringView caseName,
    AssetsGraphicsFixture::TestArena& testArena,
    AssetsGraphicsFixture::Path& outRoot,
    AssetsGraphicsFixture::Path& outOutputDirectory
)
{
    if(!prepareAssetsGraphicsCookCase(testArena, caseName, outRoot, outOutputDirectory))
        return false;

    const Path firstAssetRoot = outRoot / "first" / "assets";
    const Path secondAssetRoot = outRoot / "second" / "assets";
    if(!__hidden_assets_graphics_fixture::WriteMaterialBindDiscoveryMetadata(firstAssetRoot))
        return false;
    if(!writeTextFile(firstAssetRoot / "meshes" / "minimal_mesh.nwb", s_MinimalMeshMeta))
        return false;
    if(!writeTextFile(firstAssetRoot / "material_interfaces" / "test_surface.bind", s_MinimalMaterialBindSource))
        return false;
    if(!writeTextFile(secondAssetRoot / "material_interfaces" / "test_surface.bind", s_MinimalMaterialBindSource))
        return false;

    return cookPreparedGraphicsAssetRoots(testArena, outRoot, outOutputDirectory, { firstAssetRoot, secondAssetRoot });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif
bool AssetsGraphicsFixture::writeMaterialBindShaderProbeSource(
    AssetsGraphicsFixture::TestArena& testArena,
    const AssetsGraphicsFixture::Path& assetRoot,
    AStringView stage,
    AStringView metaFilename,
    AStringView sourceFilename,
    const AStringView sourceText
)
{
    const Path engineGraphicsIncludeRoot = assetsGraphicsTestRepoRoot(testArena) / "impl" / "assets" / "graphics";
    const NWB::Impl::ShaderCook::CookString engineGraphicsIncludeRootText = PathToString(
        testArena.arena,
        engineGraphicsIncludeRoot
    );
    NWB::Impl::ShaderCook::CookString shaderMeta(testArena.arena);
    shaderMeta += "shader asset;\n\nasset.stage = \"";
    shaderMeta += stage;
    shaderMeta +=
        "\";\n"
        "asset.entry_point = \"main\";\n"
        "asset.include_roots = [\""
    ;
    shaderMeta += engineGraphicsIncludeRootText;
    shaderMeta += "\"];\n";

    if(!writeTextFile(
        assetRoot / "shaders" / metaFilename,
        AStringView(shaderMeta.data(), shaderMeta.size())
    ))
        return false;
    return writeTextFile(assetRoot / "shaders" / sourceFilename, sourceText);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::cookMaterialBindShaderProbe(
    const AStringView bindText,
    const AStringView caseName,
    AssetsGraphicsFixture::TestArena& testArena,
    AssetsGraphicsFixture::Path& outRoot,
    AssetsGraphicsFixture::Path& outOutputDirectory
)
{
    if(!prepareAssetsGraphicsCookCase(testArena, caseName, outRoot, outOutputDirectory))
        return false;

    const Path assetRoot = outRoot / "assets";
    if(!writeTextFile(assetRoot / "material_interfaces" / "test_surface.bind", bindText))
        return false;
    // The typed-binding probe is a pixel shader because it reads typed material data and includes the generated bind.
    if(!writeMaterialBindShaderProbeSource(
        testArena,
        assetRoot,
        "ps",
        "bind_probe.nwb",
        "bind_probe.slang",
        s_MaterialBindShaderProbeSource
    ))
        return false;

    return cookPreparedGraphicsAssetRoots(testArena, outRoot, outOutputDirectory, { assetRoot });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::writeMaterialBindMaterialIntegrationAssetsWithPixelSource(
    AssetsGraphicsFixture::TestArena& testArena,
    const AssetsGraphicsFixture::Path& assetRoot,
    const AStringView bindText,
    const AStringView materialText,
    const AStringView pixelSourceText
)
{
    if(!writeTextFile(assetRoot / "material_interfaces" / "test_surface.bind", bindText))
        return false;
    if(!writeMaterialBindShaderProbeSource(
        testArena,
        assetRoot,
        "mesh",
        "material_mesh.nwb",
        "material_mesh.slang",
        s_MaterialBindMeshSource
    ))
        return false;
    if(!writeMaterialBindShaderProbeSource(
        testArena,
        assetRoot,
        "ps",
        "material_ps.nwb",
        "material_ps.slang",
        pixelSourceText
    ))
        return false;
    if(!writeTextFile(assetRoot / "shaders" / "material_bxdf.bxdf", s_MaterialBindBxdfSource))
        return false;
    return writeTextFile(assetRoot / "materials" / "test_material.nwb", materialText);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::writeMaterialBindMaterialIntegrationAssets(
    AssetsGraphicsFixture::TestArena& testArena,
    const AssetsGraphicsFixture::Path& assetRoot,
    const AStringView bindText,
    const AStringView materialText
)
{
    return writeMaterialBindMaterialIntegrationAssetsWithPixelSource(
        testArena,
        assetRoot,
        bindText,
        materialText,
        s_MaterialBindShaderProbeSource
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::writeMaterialSurfaceIntegrationAssets(
    const AssetsGraphicsFixture::Path& assetRoot,
    const AStringView bindText,
    const AStringView materialText,
    const AStringView surfaceSourceText
)
{
    if(!writeTextFile(assetRoot / "material_interfaces" / "test_surface.bind", bindText))
        return false;
    if(!writeTextFile(assetRoot / "shaders" / "material_bxdf.bxdf", s_MaterialBindBxdfSource))
        return false;
    if(!writeTextFile(assetRoot / "shaders" / "material_surface.surface", surfaceSourceText))
        return false;
    return writeTextFile(assetRoot / "materials" / "test_material.nwb", materialText);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::cookMaterialBindMaterialIntegrationWithPixelSource(
    const AStringView bindText,
    const AStringView materialText,
    const AStringView pixelSourceText,
    const AStringView caseName,
    AssetsGraphicsFixture::TestArena& testArena,
    AssetsGraphicsFixture::Path& outRoot,
    AssetsGraphicsFixture::Path& outOutputDirectory
)
{
    if(!prepareAssetsGraphicsCookCase(testArena, caseName, outRoot, outOutputDirectory))
        return false;

    const Path assetRoot = outRoot / "assets";
    if(!writeMaterialBindMaterialIntegrationAssetsWithPixelSource(
        testArena,
        assetRoot,
        bindText,
        materialText,
        pixelSourceText
    ))
        return false;

    return cookPreparedGraphicsAssetRoots(testArena, outRoot, outOutputDirectory, { assetRoot });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::cookMaterialSurfaceIntegration(
    const AStringView bindText,
    const AStringView materialText,
    const AStringView surfaceSourceText,
    const AStringView caseName,
    AssetsGraphicsFixture::TestArena& testArena,
    AssetsGraphicsFixture::Path& outRoot,
    AssetsGraphicsFixture::Path& outOutputDirectory
)
{
    if(!prepareAssetsGraphicsCookCase(testArena, caseName, outRoot, outOutputDirectory))
        return false;

    const Path assetRoot = outRoot / "assets";
    if(!writeMaterialSurfaceIntegrationAssets(assetRoot, bindText, materialText, surfaceSourceText))
        return false;

    const Path engineAssetRoot = assetsGraphicsTestRepoRoot(testArena) / "impl" / "assets";
    return cookPreparedGraphicsAssetRoots(testArena, outRoot, outOutputDirectory, { engineAssetRoot, assetRoot });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::cookMaterialBindMaterialIntegration(
    const AStringView bindText,
    const AStringView materialText,
    const AStringView caseName,
    AssetsGraphicsFixture::TestArena& testArena,
    AssetsGraphicsFixture::Path& outRoot,
    AssetsGraphicsFixture::Path& outOutputDirectory
)
{
    return cookMaterialBindMaterialIntegrationWithPixelSource(
        bindText,
        materialText,
        s_MaterialBindShaderProbeSource,
        caseName,
        testArena,
        outRoot,
        outOutputDirectory
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::loadCookedMinimalMesh(
    AssetsGraphicsFixture::TestArena& testArena,
    const AssetsGraphicsFixture::Path& outputDirectory,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset)
{
    return loadCookedAsset<NWB::Impl::MeshAssetCodec>(
        testArena,
        outputDirectory,
        Name("project/meshes/minimal_mesh"),
        outLoadedAsset,
        1u
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::loadCookedMesh(
    AssetsGraphicsFixture::TestArena& testArena,
    const AssetsGraphicsFixture::Path& outputDirectory,
    const Name assetName,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset)
{
    return loadCookedAsset<NWB::Impl::MeshAssetCodec>(
        testArena,
        outputDirectory,
        assetName,
        outLoadedAsset,
        1u
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::loadCookedMaterial(
    AssetsGraphicsFixture::TestArena& testArena,
    const AssetsGraphicsFixture::Path& outputDirectory,
    const Name assetName,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset
)
{
    return loadCookedAsset<NWB::Impl::MaterialAssetCodec>(
        testArena,
        outputDirectory,
        assetName,
        outLoadedAsset,
        0u
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::loadCookedShaderArchiveRecords(
    AssetsGraphicsFixture::TestArena& testArena,
    const AssetsGraphicsFixture::Path& outputDirectory,
    NWB::Core::GraphicsVector<NWB::Core::ShaderArchive::Record>& outRecords
)
{
    NWB::Core::Filesystem::VolumeMountDesc mountDesc(testArena.arena);
    mountDesc.volumeName = "graphics";
    mountDesc.mountDirectory = outputDirectory;
    UniquePtr<NWB::Core::Filesystem::IFilesystem> filesystem = NWB::Core::Filesystem::CreateFilesystem(testArena.arena, mountDesc);
    const bool loadedVolume = static_cast<bool>(filesystem);
    EXPECT_TRUE(loadedVolume);
    if(!loadedVolume)
        return false;

    NWB::Core::GraphicsBytes indexBinary(testArena.arena);
    const bool loadedIndex = filesystem->readFile(NWB::Core::ShaderArchive::indexVirtualPathName(), indexBinary);
    EXPECT_TRUE(loadedIndex);
    EXPECT_FALSE(indexBinary.empty());
    if(!loadedIndex || indexBinary.empty())
        return false;

    const bool deserialized = NWB::Core::ShaderArchive::deserializeIndex(indexBinary, outRecords);
    EXPECT_TRUE(deserialized);
    return deserialized;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::cookAndLoadMinimalAsset(
    AssetsGraphicsFixture::TestArena& testArena,
    const AStringView metaText,
    const AStringView caseName,
    AssetsGraphicsFixture::Path& outRoot,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset,
    AssetsGraphicsFixture::CookSingleMetaFn cookSingleMeta,
    AssetsGraphicsFixture::LoadCookedAssetFn loadCookedAsset
)
{
    AssetsGraphicsFixture::Path outputDirectory(testArena.arena);
    const bool cooked = cookSingleMeta(metaText, caseName, testArena, outRoot, outputDirectory);

    EXPECT_TRUE(cooked);
    if(!cooked){
        ErrorCode errorCode;
        EXPECT_TRUE(RemoveAllIfExists(outRoot, errorCode));
        return false;
    }

    if(loadCookedAsset(testArena, outputDirectory, outLoadedAsset))
        return true;

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(outRoot, errorCode));
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::cookAndLoadMinimalAssetByKind(
    AssetsGraphicsFixture::TestArena& testArena,
    const AStringView metaText,
    const AStringView caseName,
    AssetsGraphicsFixture::Path& outRoot,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset,
    const AssetsGraphicsFixture::MinimalAssetKind::Enum assetKind
)
{
    AssetsGraphicsFixture::CookSingleMetaFn cookSingleMeta = nullptr;
    AssetsGraphicsFixture::LoadCookedAssetFn loadCookedAsset = nullptr;
    switch(assetKind){
    case AssetsGraphicsFixture::MinimalAssetKind::Mesh:
        cookSingleMeta = cookSingleMeshMeta;
        loadCookedAsset = loadCookedMinimalMesh;
        break;
    default:
        ADD_FAILURE();
        return false;
    }

    return cookAndLoadMinimalAsset(
        testArena,
        metaText,
        caseName,
        outRoot,
        outLoadedAsset,
        cookSingleMeta,
        loadCookedAsset
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::encodeTestMeshletRefs(
    NWB::Core::Assets::AssetVector<NWB::Impl::MeshletDesc>& meshlets,
    const NWB::Core::Assets::AssetVector<NWB::Impl::MeshletPositionStreamRef>& positionRefs,
    const NWB::Core::Assets::AssetVector<NWB::Impl::MeshletAttributeStreamRef>& attributeRefs,
    NWB::Core::Assets::AssetVector<u8>& outPositionRefDeltas,
    NWB::Core::Assets::AssetVector<u8>& outAttributeRefDeltas,
    const bool skinRequired
)
{
    return NWB::Impl::EncodeMeshletRefDeltas(
        meshlets,
        positionRefs,
        attributeRefs,
        outPositionRefDeltas,
        outAttributeRefDeltas,
        skinRequired,
        [](const usize, const TStringView){ return false; }
    );
}

bool AssetsGraphicsFixture::findMaterialBinaryTypedLayoutOffsets(
    const NWB::Core::Assets::AssetBytes& binary,
    usize& outLayoutHashOffset,
    usize& outBlockByteCountOffset
){
    outLayoutHashOffset = 0u;
    outBlockByteCountOffset = 0u;

    usize typedLayoutBegin = 0u;
    u32 blockCount = 0u;
    u32 fieldCount = 0u;
    if(!NWB::Impl::MaterialBinaryPayload::FindMaterialBinaryPrefixExtents(binary, typedLayoutBegin, blockCount, fieldCount))
        return false;

    outLayoutHashOffset = typedLayoutBegin;
    outBlockByteCountOffset = typedLayoutBegin
        + sizeof(u64)
        + sizeof(u32)
        + sizeof(u32)
        + static_cast<usize>(blockCount) * NWB::Impl::MaterialBinaryPayload::s_TypedLayoutBlockBytes
        + static_cast<usize>(fieldCount) * NWB::Impl::MaterialBinaryPayload::s_TypedLayoutFieldBytes;
    if(outBlockByteCountOffset > binary.size())
        return false;

    return true;
}

bool AssetsGraphicsFixture::findShaderArchiveSourceChecksum(
    const NWB::Core::GraphicsVector<NWB::Core::ShaderArchive::Record>& records,
    const Name shaderName,
    const Name stageName,
    u64& outSourceChecksum
)
{
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

