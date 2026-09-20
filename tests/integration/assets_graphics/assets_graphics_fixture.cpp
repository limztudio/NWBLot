// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::Core::Assets::AssetBytes AssetsGraphicsFixture::MakeAssetBytes(AssetsGraphicsFixture::TestArena& testArena)
{
    return NWB::Core::Assets::AssetBytes(testArena.arena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_FINAL)
#endif
void AssetsGraphicsFixture::AppendTestMeta(AssetsGraphicsFixture::AString& inOutMeta, const AStringView text)
{
    inOutMeta.append(text.data(), text.size());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_FINAL)
AssetsGraphicsFixture::AString AssetsGraphicsFixture::BuildTriangleMeta(
    const AStringView assetHeader,
    const AStringView normalField,
    const AStringView tangentField,
    const AStringView vertexRefsField,
    const AStringView suffix
)
{
    AString meta;
    meta.reserve(1536u);
    AppendTestMeta(meta, assetHeader);
    AppendTestMeta(meta, NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_POSITIONS);
    AppendTestMeta(meta, normalField);
    AppendTestMeta(meta, tangentField);
    AppendTestMeta(meta, NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_UV0);
    AppendTestMeta(meta, NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_COLORS);
    AppendTestMeta(meta, vertexRefsField);
    AppendTestMeta(meta, NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_INDICES);
    AppendTestMeta(meta, suffix);
    return meta;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AssetsGraphicsFixture::AString AssetsGraphicsFixture::BuildMeshTriangleMeta(
    const AStringView normalField,
    const AStringView tangentField,
    const AStringView vertexRefsField
)
{
    return BuildTriangleMeta("mesh asset;\n\n", normalField, tangentField, vertexRefsField, "");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif
#if defined(NWB_FINAL)
#endif
#if defined(NWB_FINAL)
#endif
#if defined(NWB_FINAL)
#endif
#if defined(NWB_FINAL)
#endif
#if defined(NWB_FINAL)
#endif
#if defined(NWB_FINAL)
#endif
#if defined(NWB_FINAL)
#endif
#if defined(NWB_FINAL)
#endif
#if defined(NWB_FINAL)
#endif
bool AssetsGraphicsFixture::PrepareCleanDirectory(const AssetsGraphicsFixture::Path& directory)
{
    ErrorCode errorCode;
    if(!RemoveAllIfExists(directory, errorCode))
        return false;
    errorCode.clear();
    return EnsureDirectories(directory, errorCode);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::WriteTextFile(const AssetsGraphicsFixture::Path& filePath, const AStringView text)
{
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


const char* AssetsGraphicsFixture::AssetsGraphicsTestConfigurationName()
{
#if defined(NWB_DEBUG)
    return "dbg";
#elif defined(NWB_FINAL)
    return "fin";
#else
    return "opt";
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AssetsGraphicsFixture::Path AssetsGraphicsFixture::AssetsGraphicsTestRepoRoot(AssetsGraphicsFixture::TestArena& testArena)
{
    return Path(testArena.arena, __FILE__).parent_path().parent_path().parent_path().parent_path().lexically_normal();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AssetsGraphicsFixture::Path AssetsGraphicsFixture::AssetsGraphicsTestCaseRoot(AssetsGraphicsFixture::TestArena& testArena, const AStringView caseName)
{
    // Object-cache paths append a wide asset-type hash and cache key. This target is RUN_SERIAL and each cook
    // fixture clears its root, so retain fixture isolation through a compact config-plus-case key that keeps every
    // generated Windows path below MAX_PATH.
    AString caseKey;
    caseKey.reserve(1u + s_HexU32DigitCount);
    caseKey += AssetsGraphicsTestConfigurationName()[0u];
    AppendHexU32(static_cast<u32>(ComputeFnv64Text(caseName)), caseKey);
    return AssetsGraphicsTestRepoRoot(testArena) / "__build_obj" / "c" / "a" / caseKey;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(AssetsGraphicsFixture::TestArena& testArena, const AStringView caseName, AssetsGraphicsFixture::Path& outRoot)
{
    outRoot = AssetsGraphicsTestCaseRoot(testArena, caseName);
    return PrepareCleanDirectory(outRoot);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(
    AssetsGraphicsFixture::TestArena& testArena,
    const AStringView caseName,
    AssetsGraphicsFixture::Path& outRoot,
    AssetsGraphicsFixture::Path& outOutputDirectory
)
{
    if(!PrepareAssetsGraphicsCaseRoot(testArena, caseName, outRoot))
        return false;

    outOutputDirectory = outRoot / "cooked";
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::BuildPreparedGraphicsAssetRoots(
    AssetsGraphicsFixture::TestArena& testArena,
    const AssetsGraphicsFixture::Path& root,
    const AssetsGraphicsFixture::Path& outputDirectory,
    const InitializerList<AssetsGraphicsFixture::Path> assetRoots,
    const u32 workerThreadCount
)
{
    NWB::Core::CpuTaskScheduler cookCpuTaskScheduler(workerThreadCount);
    NWB::Pipeline::AssetBuilder::AssetBuildOptions options(testArena.arena, cookCpuTaskScheduler);
    options.repoRoot = PathToString(testArena.arena, AssetsGraphicsTestRepoRoot(testArena));
    options.assetRoots.reserve(assetRoots.size());
    for(const Path& assetRoot : assetRoots){
        auto parentDirectoryName = PathToString(testArena.arena, assetRoot.lexically_normal().parent_path().filename());
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


bool AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(
    AssetsGraphicsFixture::TestArena& testArena,
    const AssetsGraphicsFixture::Path& root,
    const AssetsGraphicsFixture::Path& outputDirectory,
    const InitializerList<AssetsGraphicsFixture::Path> assetRoots,
    const u32 workerThreadCount
)
{
    const Path builtDirectory = root / "built";
    if(!BuildPreparedGraphicsAssetRoots(testArena, root, builtDirectory, assetRoots, workerThreadCount))
        return false;

    NWB::Pipeline::AssetGatherer::AssetGatherOptions gatherOptions(testArena.arena);
    gatherOptions.inputs.emplace_back(PathToString(testArena.arena, builtDirectory));
    gatherOptions.outputDirectory = PathToString(testArena.arena, outputDirectory);
    gatherOptions.configuration = "tests";
    gatherOptions.mergePayloads = &NWB::Impl::MergeGatheredGraphicsAsset;
    return NWB::Pipeline::AssetGatherer::GatherAssets(gatherOptions);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::CookSingleGraphicsMeta(
    const AStringView metaText,
    const AStringView caseName,
    const char* assetDirectory,
    const char* assetFilename,
    AssetsGraphicsFixture::TestArena& testArena,
    AssetsGraphicsFixture::Path& outRoot,
    AssetsGraphicsFixture::Path& outOutputDirectory
)
{
    if(!PrepareAssetsGraphicsCookCase(testArena, caseName, outRoot, outOutputDirectory))
        return false;

    const Path assetRoot = outRoot / "assets";
    const Path metaPath = assetRoot / assetDirectory / assetFilename;
    if(!WriteTextFile(metaPath, metaText))
        return false;

    return CookPreparedGraphicsAssetRoots(testArena, outRoot, outOutputDirectory, { assetRoot });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::CookSingleMinimalAssetMeta(
    const AStringView metaText,
    const AStringView caseName,
    const AssetsGraphicsFixture::MinimalAssetCookInfo& cookInfo,
    AssetsGraphicsFixture::TestArena& testArena,
    AssetsGraphicsFixture::Path& outRoot,
    AssetsGraphicsFixture::Path& outOutputDirectory
)
{
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::CookSingleMeshMeta(
    const AStringView metaText,
    const AStringView caseName,
    AssetsGraphicsFixture::TestArena& testArena,
    AssetsGraphicsFixture::Path& outRoot,
    AssetsGraphicsFixture::Path& outOutputDirectory
)
{
    static constexpr MinimalAssetCookInfo s_CookInfo{ "meshes", "minimal_mesh.nwb" };
    return CookSingleMinimalAssetMeta(metaText, caseName, s_CookInfo, testArena, outRoot, outOutputDirectory);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::ReadSmokeAssetMeta(
    AssetsGraphicsFixture::TestArena& testArena,
    const char* assetDirectory,
    const char* assetFilename,
    AssetsGraphicsFixture::AString& outMetaText
)
{
    return ReadTextFile(
        AssetsGraphicsTestRepoRoot(testArena) / "tests" / "smoke" / "assets" / assetDirectory / assetFilename,
        outMetaText
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::CookSmokeAssetMeta(
    const char* assetDirectory,
    const char* assetFilename,
    const AStringView caseName,
    AssetsGraphicsFixture::TestArena& testArena,
    AssetsGraphicsFixture::Path& outRoot,
    AssetsGraphicsFixture::Path& outOutputDirectory
)
{
    AString metaText;
    if(!ReadSmokeAssetMeta(testArena, assetDirectory, assetFilename, metaText))
        return false;

    return CookSingleGraphicsMeta(
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


bool AssetsGraphicsFixture::CookSmokeMeshMeta(
    const char* assetFilename,
    const AStringView caseName,
    AssetsGraphicsFixture::TestArena& testArena,
    AssetsGraphicsFixture::Path& outRoot,
    AssetsGraphicsFixture::Path& outOutputDirectory
)
{
    return CookSmokeAssetMeta("meshes", assetFilename, caseName, testArena, outRoot, outOutputDirectory);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::CookMinimalMeshWithMaterialBind(
    const AStringView bindText,
    const AStringView caseName,
    AssetsGraphicsFixture::TestArena& testArena,
    AssetsGraphicsFixture::Path& outRoot,
    AssetsGraphicsFixture::Path& outOutputDirectory
)
{
    if(!PrepareAssetsGraphicsCookCase(testArena, caseName, outRoot, outOutputDirectory))
        return false;

    const Path assetRoot = outRoot / "assets";
    if(!WriteTextFile(assetRoot / "meshes" / "minimal_mesh.nwb", s_MinimalMeshMeta))
        return false;
    if(!WriteTextFile(assetRoot / "material_interfaces" / "test_surface.bind", bindText))
        return false;

    return CookPreparedGraphicsAssetRoots(testArena, outRoot, outOutputDirectory, { assetRoot });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::ParseMaterialBindFromText(
    AssetsGraphicsFixture::TestArena& testArena,
    const AStringView bindText,
    const AStringView caseName,
    NWB::Impl::MaterialBindEntry& outEntry,
    AssetsGraphicsFixture::Path& outRoot,
    NWB::Core::Alloc::ScratchArena& scratchArena
)
{
    if(!PrepareAssetsGraphicsCaseRoot(testArena, caseName, outRoot))
        return false;

    const Path bindPath = outRoot / "assets" / "material_interfaces" / "test_surface.bind";
    if(!WriteTextFile(bindPath, bindText))
        return false;

    return NWB::Impl::ParseMaterialBindSource(bindPath, outEntry, scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_FINAL)
bool AssetsGraphicsFixture::CookDuplicateGeneratedMaterialBindIncludePath(
    const AStringView caseName,
    AssetsGraphicsFixture::TestArena& testArena,
    AssetsGraphicsFixture::Path& outRoot,
    AssetsGraphicsFixture::Path& outOutputDirectory
)
{
    if(!PrepareAssetsGraphicsCookCase(testArena, caseName, outRoot, outOutputDirectory))
        return false;

    const Path firstAssetRoot = outRoot / "first" / "assets";
    const Path secondAssetRoot = outRoot / "second" / "assets";
    if(!WriteTextFile(firstAssetRoot / "meshes" / "minimal_mesh.nwb", s_MinimalMeshMeta))
        return false;
    if(!WriteTextFile(firstAssetRoot / "material_interfaces" / "test_surface.bind", s_MinimalMaterialBindSource))
        return false;
    if(!WriteTextFile(secondAssetRoot / "material_interfaces" / "test_surface.bind", s_MinimalMaterialBindSource))
        return false;

    return CookPreparedGraphicsAssetRoots(testArena, outRoot, outOutputDirectory, { firstAssetRoot, secondAssetRoot });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif
bool AssetsGraphicsFixture::WriteMaterialBindShaderProbeSource(
    AssetsGraphicsFixture::TestArena& testArena,
    const AssetsGraphicsFixture::Path& assetRoot,
    const char* stage,
    const char* metaFilename,
    const char* sourceFilename,
    const AStringView sourceText
)
{
    const Path engineGraphicsIncludeRoot = AssetsGraphicsTestRepoRoot(testArena) / "impl" / "assets" / "graphics";
    const NWB::Impl::ShaderCook::CookString engineGraphicsIncludeRootText = PathToString(
        testArena.arena,
        engineGraphicsIncludeRoot
    );
    NWB::Impl::ShaderCook::CookString shaderMeta(testArena.arena);
    shaderMeta += "shader asset;\n\nasset.stage = \"";
    shaderMeta += stage;
    shaderMeta +=
        "\";\n"
        "asset.target_profile = \"spirv_1_5\";\n"
        "asset.entry_point = \"main\";\n"
        "asset.include_roots = [\""
    ;
    shaderMeta += engineGraphicsIncludeRootText;
    shaderMeta += "\"];\n";

    if(!WriteTextFile(
        assetRoot / "shaders" / metaFilename,
        AStringView(shaderMeta.data(), shaderMeta.size())
    ))
        return false;
    return WriteTextFile(assetRoot / "shaders" / sourceFilename, sourceText);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::CookMaterialBindShaderProbe(
    const AStringView bindText,
    const AStringView caseName,
    AssetsGraphicsFixture::TestArena& testArena,
    AssetsGraphicsFixture::Path& outRoot,
    AssetsGraphicsFixture::Path& outOutputDirectory
)
{
    if(!PrepareAssetsGraphicsCookCase(testArena, caseName, outRoot, outOutputDirectory))
        return false;

    const Path assetRoot = outRoot / "assets";
    if(!WriteTextFile(assetRoot / "material_interfaces" / "test_surface.bind", bindText))
        return false;
    // The typed-binding probe is a pixel shader because it reads typed material data and includes the generated bind.
    if(!WriteMaterialBindShaderProbeSource(
        testArena,
        assetRoot,
        "ps",
        "bind_probe.nwb",
        "bind_probe.slang",
        s_MaterialBindShaderProbeSource
    ))
        return false;

    return CookPreparedGraphicsAssetRoots(testArena, outRoot, outOutputDirectory, { assetRoot });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::WriteMaterialBindMaterialIntegrationAssetsWithPixelSource(
    AssetsGraphicsFixture::TestArena& testArena,
    const AssetsGraphicsFixture::Path& assetRoot,
    const AStringView bindText,
    const AStringView materialText,
    const AStringView pixelSourceText
)
{
    if(!WriteTextFile(assetRoot / "material_interfaces" / "test_surface.bind", bindText))
        return false;
    if(!WriteMaterialBindShaderProbeSource(
        testArena,
        assetRoot,
        "mesh",
        "material_mesh.nwb",
        "material_mesh.slang",
        s_MaterialBindMeshSource
    ))
        return false;
    if(!WriteMaterialBindShaderProbeSource(
        testArena,
        assetRoot,
        "ps",
        "material_ps.nwb",
        "material_ps.slang",
        pixelSourceText
    ))
        return false;
    if(!WriteTextFile(assetRoot / "shaders" / "material_bxdf.bxdf", s_MaterialBindBxdfSource))
        return false;
    return WriteTextFile(assetRoot / "materials" / "test_material.nwb", materialText);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::WriteMaterialBindMaterialIntegrationAssets(
    AssetsGraphicsFixture::TestArena& testArena,
    const AssetsGraphicsFixture::Path& assetRoot,
    const AStringView bindText,
    const AStringView materialText
)
{
    return WriteMaterialBindMaterialIntegrationAssetsWithPixelSource(
        testArena,
        assetRoot,
        bindText,
        materialText,
        s_MaterialBindShaderProbeSource
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::WriteMaterialSurfaceIntegrationAssets(
    const AssetsGraphicsFixture::Path& assetRoot,
    const AStringView bindText,
    const AStringView materialText,
    const AStringView surfaceSourceText
)
{
    if(!WriteTextFile(assetRoot / "material_interfaces" / "test_surface.bind", bindText))
        return false;
    if(!WriteTextFile(assetRoot / "shaders" / "material_bxdf.bxdf", s_MaterialBindBxdfSource))
        return false;
    if(!WriteTextFile(assetRoot / "shaders" / "material_surface.surface", surfaceSourceText))
        return false;
    return WriteTextFile(assetRoot / "materials" / "test_material.nwb", materialText);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::CookMaterialBindMaterialIntegrationWithPixelSource(
    const AStringView bindText,
    const AStringView materialText,
    const AStringView pixelSourceText,
    const AStringView caseName,
    AssetsGraphicsFixture::TestArena& testArena,
    AssetsGraphicsFixture::Path& outRoot,
    AssetsGraphicsFixture::Path& outOutputDirectory
)
{
    if(!PrepareAssetsGraphicsCookCase(testArena, caseName, outRoot, outOutputDirectory))
        return false;

    const Path assetRoot = outRoot / "assets";
    if(!WriteMaterialBindMaterialIntegrationAssetsWithPixelSource(
        testArena,
        assetRoot,
        bindText,
        materialText,
        pixelSourceText
    ))
        return false;

    return CookPreparedGraphicsAssetRoots(testArena, outRoot, outOutputDirectory, { assetRoot });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::CookMaterialSurfaceIntegration(
    const AStringView bindText,
    const AStringView materialText,
    const AStringView surfaceSourceText,
    const AStringView caseName,
    AssetsGraphicsFixture::TestArena& testArena,
    AssetsGraphicsFixture::Path& outRoot,
    AssetsGraphicsFixture::Path& outOutputDirectory
)
{
    if(!PrepareAssetsGraphicsCookCase(testArena, caseName, outRoot, outOutputDirectory))
        return false;

    const Path assetRoot = outRoot / "assets";
    if(!WriteMaterialSurfaceIntegrationAssets(assetRoot, bindText, materialText, surfaceSourceText))
        return false;

    const Path engineAssetRoot = AssetsGraphicsTestRepoRoot(testArena) / "impl" / "assets";
    return CookPreparedGraphicsAssetRoots(testArena, outRoot, outOutputDirectory, { engineAssetRoot, assetRoot });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::CookMaterialBindMaterialIntegration(
    const AStringView bindText,
    const AStringView materialText,
    const AStringView caseName,
    AssetsGraphicsFixture::TestArena& testArena,
    AssetsGraphicsFixture::Path& outRoot,
    AssetsGraphicsFixture::Path& outOutputDirectory
)
{
    return CookMaterialBindMaterialIntegrationWithPixelSource(
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


bool AssetsGraphicsFixture::LoadCookedMinimalMesh(
    AssetsGraphicsFixture::TestArena& testArena,
    const AssetsGraphicsFixture::Path& outputDirectory,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset)
{
    return LoadCookedAsset<NWB::Impl::MeshAssetCodec>(
        testArena,
        outputDirectory,
        Name("project/meshes/minimal_mesh"),
        outLoadedAsset
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::LoadCookedMesh(
    AssetsGraphicsFixture::TestArena& testArena,
    const AssetsGraphicsFixture::Path& outputDirectory,
    const Name assetName,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset)
{
    return LoadCookedAsset<NWB::Impl::MeshAssetCodec>(
        testArena,
        outputDirectory,
        assetName,
        outLoadedAsset
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::LoadCookedMaterial(
    AssetsGraphicsFixture::TestArena& testArena,
    const AssetsGraphicsFixture::Path& outputDirectory,
    const Name assetName,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset
)
{
    return LoadCookedAsset<NWB::Impl::MaterialAssetCodec>(
        testArena,
        outputDirectory,
        assetName,
        outLoadedAsset,
        0u
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::LoadCookedShaderArchiveRecords(
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
    const bool loadedIndex = filesystem->readFile(NWB::Core::ShaderArchive::IndexVirtualPathName(), indexBinary);
    EXPECT_TRUE(loadedIndex);
    EXPECT_FALSE(indexBinary.empty());
    if(!loadedIndex || indexBinary.empty())
        return false;

    const bool deserialized = NWB::Core::ShaderArchive::deserializeIndex(indexBinary, outRecords);
    EXPECT_TRUE(deserialized);
    return deserialized;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetsGraphicsFixture::CookAndLoadMinimalAsset(
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


bool AssetsGraphicsFixture::CookAndLoadMinimalAssetByKind(
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
        cookSingleMeta = CookSingleMeshMeta;
        loadCookedAsset = LoadCookedMinimalMesh;
        break;
    default:
        ADD_FAILURE();
        return false;
    }

    return CookAndLoadMinimalAsset(
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


bool AssetsGraphicsFixture::EncodeTestMeshletRefs(
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
        [](const usize, const tchar*){ return false; }
    );
}

bool AssetsGraphicsFixture::FindMaterialBinaryTypedLayoutOffsets(
    const NWB::Core::Assets::AssetBytes& binary,
    usize& outLayoutHashOffset,
    usize& outBlockByteCountOffset
){
    outLayoutHashOffset = 0u;
    outBlockByteCountOffset = 0u;

    usize cursor = 0u;
    u32 value32 = 0u;
    if(!ReadPOD(binary, cursor, value32))
        return false;

    u32 shaderVariantByteCount = 0u;
    if(!ReadPOD(binary, cursor, shaderVariantByteCount))
        return false;
    if(cursor > binary.size() || shaderVariantByteCount > binary.size() - cursor)
        return false;
    cursor += shaderVariantByteCount;

    if(cursor > binary.size() || sizeof(NameHash) > binary.size() - cursor)
        return false;
    cursor += sizeof(NameHash);

    outLayoutHashOffset = cursor;

    u64 layoutHash = 0u;
    u32 blockCount = 0u;
    u32 fieldCount = 0u;
    if(
        !ReadPOD(binary, cursor, layoutHash)
        || !ReadPOD(binary, cursor, blockCount)
        || !ReadPOD(binary, cursor, fieldCount)
    )
        return false;

    if(
        cursor > binary.size()
        || blockCount > (binary.size() - cursor) / NWB::Impl::MaterialBinaryPayload::s_TypedLayoutBlockBytes
    )
        return false;
    cursor += static_cast<usize>(blockCount) * NWB::Impl::MaterialBinaryPayload::s_TypedLayoutBlockBytes;

    if(
        cursor > binary.size()
        || fieldCount > (binary.size() - cursor) / NWB::Impl::MaterialBinaryPayload::s_TypedLayoutFieldBytes
    )
        return false;
    cursor += static_cast<usize>(fieldCount) * NWB::Impl::MaterialBinaryPayload::s_TypedLayoutFieldBytes;

    outBlockByteCountOffset = cursor;
    return true;
}

bool AssetsGraphicsFixture::FindShaderArchiveSourceChecksum(
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

