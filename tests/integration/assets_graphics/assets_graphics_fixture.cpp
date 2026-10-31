// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_fixture{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_EngineDirectory = "impl";
#if defined(NWB_FINAL)
constexpr AStringView s_CacheDirectory = "__asset_cache";
#endif
constexpr AStringView s_TestsConfiguration = "tests";
constexpr AStringView s_GraphicsDirectory = "graphics";
constexpr AStringView s_AssetsDirectory = "assets";
constexpr AStringView s_MeshesDirectory = "meshes";
constexpr AStringView s_MaterialInterfacesDirectory = "material_interfaces";
constexpr AStringView s_SurfaceBindFilename = "test_surface.bind";
constexpr AStringView s_ShadersDirectory = "shaders";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::Core::Assets::AssetBytes AssetsGraphicsFixture::MakeAssetBytes(AssetsGraphicsFixture::TestArena& testArena)noexcept(noexcept(NWB::Core::Assets::AssetBytes(testArena.arena))){
    return NWB::Core::Assets::AssetBytes(testArena.arena);
}

void AssetsGraphicsFixture::AppendTestMeta(AssetsGraphicsFixture::AString& inOutMeta, const AStringView text){
    inOutMeta.append(text.data(), text.size());
}

#if defined(NWB_FINAL)
AssetsGraphicsFixture::AString AssetsGraphicsFixture::BuildTriangleMeta(const AStringView assetHeader, const AStringView normalField, const AStringView tangentField, const AStringView vertexRefsField, const AStringView suffix){
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

AssetsGraphicsFixture::AString AssetsGraphicsFixture::BuildMeshTriangleMeta(const AStringView normalField, const AStringView tangentField, const AStringView vertexRefsField){
    return BuildTriangleMeta("mesh asset;\n\n", normalField, tangentField, vertexRefsField, "");
}
#endif

b8 AssetsGraphicsFixture::PrepareCleanDirectory(const AssetsGraphicsFixture::Path& directory){
    if(!RemoveAllIfExists(directory))
        return false;
    return EnsureDirectories(directory).has_value();
}

b8 AssetsGraphicsFixture::WriteTextFile(const AssetsGraphicsFixture::Path& filePath, const AStringView text){
    if(!EnsureDirectories(filePath.parentPath()))
        return false;

    return ::WriteTextFile(filePath, text);
}

AStringView AssetsGraphicsFixture::AssetsGraphicsTestConfigurationName()noexcept{
#if defined(NWB_DEBUG)
    return "dbg";
#elif defined(NWB_FINAL)
    return "fin";
#else
    return "opt";
#endif
}

AssetsGraphicsFixture::Path AssetsGraphicsFixture::AssetsGraphicsTestRepoRoot(AssetsGraphicsFixture::TestArena& testArena){
    return NWB::Tests::RepoRootOf(testArena.arena, __FILE__);
}

AssetsGraphicsFixture::Path AssetsGraphicsFixture::AssetsGraphicsTestCaseRoot(AssetsGraphicsFixture::TestArena& testArena, const AStringView caseName){
    // Use a short config-and-case key so object-cache paths stay below Windows MAX_PATH.
    AString caseKey;
    caseKey.reserve(1u + s_HexU32DigitCount);
    caseKey += AssetsGraphicsTestConfigurationName()[0u];
    AppendHexU32(static_cast<u32>(ComputeFnv64Text(caseName)), caseKey);
    return AssetsGraphicsTestRepoRoot(testArena) / "__build_obj" / "tests" / caseKey;
}

Expected<AssetsGraphicsFixture::Path> AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(AssetsGraphicsFixture::TestArena& testArena, const AStringView caseName){
    Path root = AssetsGraphicsTestCaseRoot(testArena, caseName);
    if(!PrepareCleanDirectory(root))
        return MakeUnexpected(Failure{});
    return root;
}

Expected<AssetsGraphicsFixture::CookCase> AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(AssetsGraphicsFixture::TestArena& testArena, const AStringView caseName){
    auto root = PrepareAssetsGraphicsCaseRoot(testArena, caseName);
    if(!root)
        return MakeUnexpected(root.error());
    Path outputDirectory = *root / "cooked";
    return CookCase{ Move(*root), Move(outputDirectory) };
}

b8 AssetsGraphicsFixture::BuildPreparedGraphicsAssetRoots(
    AssetsGraphicsFixture::TestArena& testArena,
    const AssetsGraphicsFixture::Path& root,
    const AssetsGraphicsFixture::Path& outputDirectory,
    const InitializerList<AssetsGraphicsFixture::Path> assetRoots,
    const u32 workerThreadCount
){
    NWB::Core::CpuTaskScheduler cookCpuTaskScheduler(workerThreadCount);
    NWB::Pipeline::AssetBuilder::AssetBuildOptions options(testArena.arena, cookCpuTaskScheduler);
    const Path normalRoot = root.lexicallyNormal();
    b8 useCaseRootAsRepoRoot = !FileExists(root / __hidden_assets_graphics_fixture::s_AssetsDirectory / __hidden_assets_graphics_fixture::s_ShadersDirectory / "material_mesh.nwb").value_or(false);
    for(const Path& assetRoot : assetRoots){
        const Path normalAssetRoot = assetRoot.lexicallyNormal();
        if(normalAssetRoot != normalRoot && !PathHasDirectoryAncestor(normalAssetRoot, normalRoot)){
            useCaseRootAsRepoRoot = false;
            break;
        }
    }
    options.repoRoot = PathToString(testArena.arena, useCaseRootAsRepoRoot ? root : AssetsGraphicsTestRepoRoot(testArena));
    options.assetRoots.reserve(assetRoots.size());
    for(const Path& assetRoot : assetRoots){
        auto parentDirectoryName = PathToString(testArena.arena, assetRoot.lexicallyNormal().parentPath().filenameView());
        CanonicalizeTextInPlace(parentDirectoryName);

        const AStringView virtualRoot = parentDirectoryName == __hidden_assets_graphics_fixture::s_EngineDirectory ? NWB::Core::Assets::s_EngineVirtualRoot : NWB::Core::Assets::s_ProjectVirtualRoot;

        auto assetRootText = PathToString(testArena.arena, assetRoot);
        options.assetRoots.emplace_back(testArena.arena, assetRootText, virtualRoot);
    }
    options.outputDirectory = PathToString(testArena.arena, outputDirectory);
    if(!options.configuration.assign(__hidden_assets_graphics_fixture::s_TestsConfiguration) || !options.assetType.assign(__hidden_assets_graphics_fixture::s_GraphicsDirectory))
        return false;

    return NWB::Pipeline::AssetBuilder::BuildAssets(options);
}

b8 AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(
    AssetsGraphicsFixture::TestArena& testArena,
    const AssetsGraphicsFixture::Path& root,
    const AssetsGraphicsFixture::Path& outputDirectory,
    const InitializerList<AssetsGraphicsFixture::Path> assetRoots,
    const u32 workerThreadCount
){
    const Path builtDirectory = root / "built";
    if(!BuildPreparedGraphicsAssetRoots(testArena, root, builtDirectory, assetRoots, workerThreadCount))
        return false;

    NWB::Pipeline::AssetGatherer::AssetGatherOptions gatherOptions(testArena.arena);
    gatherOptions.inputs.emplace_back(PathToString(testArena.arena, builtDirectory));
    gatherOptions.outputDirectory = PathToString(testArena.arena, outputDirectory);
    gatherOptions.configuration = __hidden_assets_graphics_fixture::s_TestsConfiguration.data();
    gatherOptions.mergePayloads = &NWB::Impl::MergeGatheredGraphicsAsset;
    return NWB::Pipeline::AssetGatherer::GatherAssets(gatherOptions);
}

b8 AssetsGraphicsFixture::CookSingleGraphicsMeta(const AStringView metaText, AStringView assetDirectory, AStringView assetFilename, AssetsGraphicsFixture::TestArena& testArena, const AssetsGraphicsFixture::CookCase& cookCase){
    const Path assetRoot = cookCase.root / __hidden_assets_graphics_fixture::s_AssetsDirectory;
    const Path metaPath = assetRoot / assetDirectory / assetFilename;
    if(!WriteTextFile(metaPath, metaText))
        return false;

    return CookPreparedGraphicsAssetRoots(testArena, cookCase.root, cookCase.outputDirectory, { assetRoot });
}

b8 AssetsGraphicsFixture::CookSingleMinimalAssetMeta(const AStringView metaText, const AssetsGraphicsFixture::MinimalAssetCookInfo& cookInfo, AssetsGraphicsFixture::TestArena& testArena, const AssetsGraphicsFixture::CookCase& cookCase){
    return CookSingleGraphicsMeta(metaText, cookInfo.assetDirectory, cookInfo.assetFilename, testArena, cookCase);
}

b8 AssetsGraphicsFixture::CookSingleMeshMeta(const AStringView metaText, AssetsGraphicsFixture::TestArena& testArena, const AssetsGraphicsFixture::CookCase& cookCase){
    static constexpr MinimalAssetCookInfo s_CookInfo{ "meshes", "minimal_mesh.nwb" };
    return CookSingleMinimalAssetMeta(metaText, s_CookInfo, testArena, cookCase);
}

Expected<AssetsGraphicsFixture::AString> AssetsGraphicsFixture::ReadSmokeAssetMeta(AssetsGraphicsFixture::TestArena& testArena, AStringView assetDirectory, AStringView assetFilename){
    AString metaText;
    if(!ReadTextFile(AssetsGraphicsTestRepoRoot(testArena) / __hidden_assets_graphics_fixture::s_TestsConfiguration / "smoke" / __hidden_assets_graphics_fixture::s_AssetsDirectory / assetDirectory / assetFilename, metaText))
        return MakeUnexpected(Failure{});
    return metaText;
}

b8 AssetsGraphicsFixture::CookSmokeAssetMeta(AStringView assetDirectory, AStringView assetFilename, AssetsGraphicsFixture::TestArena& testArena, const AssetsGraphicsFixture::CookCase& cookCase){
    const auto metaText = ReadSmokeAssetMeta(testArena, assetDirectory, assetFilename);
    if(!metaText)
        return false;

    return CookSingleGraphicsMeta(AStringView(metaText->data(), metaText->size()), assetDirectory, assetFilename, testArena, cookCase);
}

b8 AssetsGraphicsFixture::CookSmokeMeshMeta(AStringView assetFilename, AssetsGraphicsFixture::TestArena& testArena, const AssetsGraphicsFixture::CookCase& cookCase){
    return CookSmokeAssetMeta(__hidden_assets_graphics_fixture::s_MeshesDirectory, assetFilename, testArena, cookCase);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_fixture{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static b8 WriteMaterialBindDiscoveryMetadata(const AssetsGraphicsFixture::Path& assetRoot){
    if(!AssetsGraphicsFixture::WriteTextFile(assetRoot / __hidden_assets_graphics_fixture::s_MaterialInterfacesDirectory / "bind_discovery.nwb", "include asset;\r\n\r\nasset.defines = { \"NWB_TEST_MATERIAL_BIND_DISCOVERY\": [\"1\"] };\r\n"))
        return false;
    return AssetsGraphicsFixture::WriteTextFile(
        assetRoot / __hidden_assets_graphics_fixture::s_MaterialInterfacesDirectory / "bind_discovery.slangi",
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


b8 AssetsGraphicsFixture::CookMinimalMeshWithMaterialBind(const AStringView bindText, AssetsGraphicsFixture::TestArena& testArena, const AssetsGraphicsFixture::CookCase& cookCase){
    const Path assetRoot = cookCase.root / __hidden_assets_graphics_fixture::s_AssetsDirectory;
    if(!__hidden_assets_graphics_fixture::WriteMaterialBindDiscoveryMetadata(assetRoot))
        return false;
    if(!WriteTextFile(assetRoot / __hidden_assets_graphics_fixture::s_MeshesDirectory / "minimal_mesh.nwb", s_MinimalMeshMeta))
        return false;
    if(!WriteTextFile(assetRoot / __hidden_assets_graphics_fixture::s_MaterialInterfacesDirectory / __hidden_assets_graphics_fixture::s_SurfaceBindFilename, bindText))
        return false;

    return CookPreparedGraphicsAssetRoots(testArena, cookCase.root, cookCase.outputDirectory, { assetRoot });
}

Expected<NWB::Impl::MaterialBindEntry> AssetsGraphicsFixture::ParseMaterialBindFromText(AssetsGraphicsFixture::TestArena& testArena, const AStringView bindText, const AssetsGraphicsFixture::Path& root, NWB::Core::Alloc::ScratchArena& scratchArena){
    const Path bindPath = root / __hidden_assets_graphics_fixture::s_AssetsDirectory / __hidden_assets_graphics_fixture::s_MaterialInterfacesDirectory / __hidden_assets_graphics_fixture::s_SurfaceBindFilename;
    if(!WriteTextFile(bindPath, bindText))
        return MakeUnexpected(Failure{});

    return NWB::Impl::ParseMaterialBindSource(bindPath, testArena.arena, scratchArena);
}

#if defined(NWB_FINAL)
Expected<AssetsGraphicsFixture::Path> AssetsGraphicsFixture::EmitDuplicateGeneratedMaterialBindIncludePath(AssetsGraphicsFixture::TestArena& testArena, const AssetsGraphicsFixture::CookCase& cookCase){
    const Path firstAssetRoot = cookCase.root / "first" / __hidden_assets_graphics_fixture::s_AssetsDirectory;
    const Path secondAssetRoot = cookCase.root / "second" / __hidden_assets_graphics_fixture::s_AssetsDirectory;
    NWB::Core::Alloc::ScratchArena scratchArena(s_MaterialScratchArena);
    NWB::Impl::MaterialCookVector<NWB::Impl::MaterialBindEntry> bindEntries(testArena.arena);
    for(const Path& assetRoot : InitializerList<Path>{ firstAssetRoot, secondAssetRoot }){
        const Path bindPath = assetRoot / __hidden_assets_graphics_fixture::s_MaterialInterfacesDirectory / __hidden_assets_graphics_fixture::s_SurfaceBindFilename;
        if(!WriteTextFile(bindPath, s_MinimalMaterialBindSource))
            return MakeUnexpected(Failure{});

        auto bindEntry = NWB::Impl::ParseMaterialBindSource(bindPath, testArena.arena, scratchArena);
        if(!bindEntry)
            return MakeUnexpected(bindEntry.error());
        auto virtualPath = NWB::Core::Assets::BuildDerivedAssetVirtualPath(testArena.arena, assetRoot, NWB::Core::Assets::s_ProjectVirtualRoot, bindPath, scratchArena);
        if(!virtualPath)
            return MakeUnexpected(virtualPath.error());
        bindEntry->virtualPath = Move(*virtualPath);
        bindEntries.push_back(Move(*bindEntry));
    }

    return NWB::Impl::EmitMaterialBindIncludes(testArena.arena, cookCase.root / __hidden_assets_graphics_fixture::s_CacheDirectory, __hidden_assets_graphics_fixture::s_TestsConfiguration, bindEntries, scratchArena);
}
#endif

b8 AssetsGraphicsFixture::WriteMaterialBindShaderProbeSource(
    AssetsGraphicsFixture::TestArena& testArena,
    const AssetsGraphicsFixture::Path& assetRoot,
    AStringView shaderAssetType,
    AStringView metaFilename,
    AStringView sourceFilename,
    const AStringView sourceText
){
    const Path engineGraphicsIncludeRoot = AssetsGraphicsTestRepoRoot(testArena) / __hidden_assets_graphics_fixture::s_EngineDirectory / __hidden_assets_graphics_fixture::s_AssetsDirectory / __hidden_assets_graphics_fixture::s_GraphicsDirectory;
    const NWB::Impl::ShaderCook::CookString engineGraphicsIncludeRootText = PathToString(testArena.arena, engineGraphicsIncludeRoot);
    NWB::Impl::ShaderCook::CookString shaderMeta(testArena.arena);
    shaderMeta += shaderAssetType;
    shaderMeta += " asset;\n\n" "asset.entry_point = \"main\";\n" "asset.include_roots = [\"";
    shaderMeta += engineGraphicsIncludeRootText;
    shaderMeta += "\"];\n";

    if(!WriteTextFile(assetRoot / __hidden_assets_graphics_fixture::s_ShadersDirectory / metaFilename, AStringView(shaderMeta.data(), shaderMeta.size())))
        return false;
    return WriteTextFile(assetRoot / __hidden_assets_graphics_fixture::s_ShadersDirectory / sourceFilename, sourceText);
}

b8 AssetsGraphicsFixture::WriteMaterialBindMaterialIntegrationAssetsWithPixelSource(
    AssetsGraphicsFixture::TestArena& testArena,
    const AssetsGraphicsFixture::Path& assetRoot,
    const AStringView bindText,
    const AStringView materialText,
    const AStringView pixelSourceText
){
    if(!WriteTextFile(assetRoot / __hidden_assets_graphics_fixture::s_MaterialInterfacesDirectory / __hidden_assets_graphics_fixture::s_SurfaceBindFilename, bindText))
        return false;
    if(!WriteMaterialBindShaderProbeSource(testArena, assetRoot, "mesh_shader", "material_mesh.nwb", "material_mesh.slang", s_MaterialBindMeshSource))
        return false;
    if(!WriteMaterialBindShaderProbeSource(testArena, assetRoot, "pixel_shader", "material_ps.nwb", "material_ps.slang", pixelSourceText))
        return false;
    if(!WriteTextFile(assetRoot / __hidden_assets_graphics_fixture::s_ShadersDirectory / "material_bxdf.bxdf", s_MaterialBindBxdfSource))
        return false;
    return WriteTextFile(assetRoot / "materials" / "test_material.nwb", materialText);
}

b8 AssetsGraphicsFixture::WriteMaterialBindMaterialIntegrationAssets(AssetsGraphicsFixture::TestArena& testArena, const AssetsGraphicsFixture::Path& assetRoot, const AStringView bindText, const AStringView materialText){
    return WriteMaterialBindMaterialIntegrationAssetsWithPixelSource(testArena, assetRoot, bindText, materialText, s_MaterialBindShaderProbeSource);
}

b8 AssetsGraphicsFixture::CookMaterialBindMaterialIntegrationWithPixelSource(
    const AStringView bindText,
    const AStringView materialText,
    const AStringView pixelSourceText,
    AssetsGraphicsFixture::TestArena& testArena,
    const AssetsGraphicsFixture::CookCase& cookCase
){
    const Path assetRoot = cookCase.root / __hidden_assets_graphics_fixture::s_AssetsDirectory;
    if(!WriteMaterialBindMaterialIntegrationAssetsWithPixelSource(testArena, assetRoot, bindText, materialText, pixelSourceText))
        return false;

    return CookPreparedGraphicsAssetRoots(testArena, cookCase.root, cookCase.outputDirectory, { assetRoot });
}

b8 AssetsGraphicsFixture::CookMaterialBindMaterialIntegration(const AStringView bindText, const AStringView materialText, AssetsGraphicsFixture::TestArena& testArena, const AssetsGraphicsFixture::CookCase& cookCase){
    return CookMaterialBindMaterialIntegrationWithPixelSource(bindText, materialText, s_MaterialBindShaderProbeSource, testArena, cookCase);
}

Expected<UniquePtr<NWB::Core::Assets::IAsset>> AssetsGraphicsFixture::LoadCookedMinimalMesh(AssetsGraphicsFixture::TestArena& testArena, const AssetsGraphicsFixture::Path& outputDirectory){
    return LoadCookedAsset<NWB::Impl::MeshAssetCodec>(testArena, outputDirectory, Name("project/meshes/minimal_mesh"), 1u);
}

Expected<UniquePtr<NWB::Core::Assets::IAsset>> AssetsGraphicsFixture::LoadCookedMesh(AssetsGraphicsFixture::TestArena& testArena, const AssetsGraphicsFixture::Path& outputDirectory, const Name assetName){
    return LoadCookedAsset<NWB::Impl::MeshAssetCodec>(testArena, outputDirectory, assetName, 1u);
}

Expected<UniquePtr<NWB::Core::Assets::IAsset>> AssetsGraphicsFixture::LoadCookedMaterial(AssetsGraphicsFixture::TestArena& testArena, const AssetsGraphicsFixture::Path& outputDirectory, const Name assetName){
    return LoadCookedAsset<NWB::Impl::MaterialAssetCodec>(testArena, outputDirectory, assetName, 0u);
}

Expected<NWB::Core::GraphicsVector<NWB::Core::ShaderArchive::Record>> AssetsGraphicsFixture::LoadCookedShaderArchiveRecords(AssetsGraphicsFixture::TestArena& testArena, const AssetsGraphicsFixture::Path& outputDirectory){
    NWB::Core::Filesystem::VolumeMountDesc mountDesc(testArena.arena);
    mountDesc.volumeName = __hidden_assets_graphics_fixture::s_GraphicsDirectory.data();
    mountDesc.mountDirectory = outputDirectory;
    auto filesystemResult = NWB::Core::Filesystem::CreateFilesystem(testArena.arena, mountDesc);
    const b8 loadedVolume = static_cast<b8>(filesystemResult);
    EXPECT_TRUE(loadedVolume);
    if(!loadedVolume)
        return MakeUnexpected(filesystemResult.error());
    UniquePtr<NWB::Core::Filesystem::IFilesystem> filesystem = Move(*filesystemResult);

    NWB::Core::GraphicsBytes indexBinary(testArena.arena);
    const b8 loadedIndex = filesystem->readFile(NWB::Core::ShaderArchive::IndexVirtualPathName(), indexBinary);
    EXPECT_TRUE(loadedIndex);
    EXPECT_FALSE(indexBinary.empty());
    if(!loadedIndex || indexBinary.empty())
        return MakeUnexpected(Failure{});

    auto deserialized = NWB::Core::ShaderArchive::DeserializeIndex(testArena.arena, { indexBinary.data(), indexBinary.size() });
    EXPECT_TRUE(deserialized);
    return deserialized;
}

b8 AssetsGraphicsFixture::EncodeTestMeshletRefs(const Span<const NWB::Impl::MeshletPositionStreamRef> positionRefs, const Span<const NWB::Impl::MeshletAttributeStreamRef> attributeRefs, const b8 skinRequired, const Span<NWB::Impl::MeshletDesc> meshlets, NWB::Core::Assets::AssetVector<u8>& outPositionRefDeltas, NWB::Core::Assets::AssetVector<u8>& outAttributeRefDeltas){
    return NWB::Impl::EncodeMeshletRefDeltas(positionRefs, attributeRefs, skinRequired, [](const usize, const TStringView)noexcept{ return false; }, meshlets, outPositionRefDeltas, outAttributeRefDeltas);
}

Expected<AssetsGraphicsFixture::MaterialBinaryTypedLayoutOffsets> AssetsGraphicsFixture::FindMaterialBinaryTypedLayoutOffsets(const Span<const u8> binary)noexcept{
    usize cursor = 0u;
    const auto magic = ReadPOD<u32>(binary, cursor);
    if(!magic || *magic != NWB::Impl::s_MaterialBinaryMagic)
        return MakeUnexpected(Failure{});

    const auto shaderVariantView = ReadString(binary, cursor);
    if(!shaderVariantView || shaderVariantView->empty())
        return MakeUnexpected(Failure{});

    const auto materialInterfaceHash = ReadPOD<NameHash>(binary, cursor);
    if(!materialInterfaceHash || !Name(*materialInterfaceHash))
        return MakeUnexpected(Failure{});

    const usize typedLayoutBegin = cursor;
    const auto layoutHash = ReadPOD<u64>(binary, cursor);
    if(!layoutHash || *layoutHash == 0u)
        return MakeUnexpected(Failure{});
    const auto blockCount = ReadPOD<u32>(binary, cursor);
    if(!blockCount)
        return MakeUnexpected(Failure{});
    const auto fieldCount = ReadPOD<u32>(binary, cursor);
    if(!fieldCount)
        return MakeUnexpected(Failure{});

    if(*blockCount > (binary.size() - cursor) / NWB::Impl::s_MaterialTypedLayoutBlockBytes)
        return MakeUnexpected(Failure{});
    cursor += static_cast<usize>(*blockCount) * NWB::Impl::s_MaterialTypedLayoutBlockBytes;

    if(*fieldCount > (binary.size() - cursor) / NWB::Impl::s_MaterialTypedLayoutFieldBytes)
        return MakeUnexpected(Failure{});
    const usize fieldBytes = static_cast<usize>(*fieldCount) * NWB::Impl::s_MaterialTypedLayoutFieldBytes;
    if(!SkipBytes(binary, fieldBytes, cursor))
        return MakeUnexpected(Failure{});

    return MaterialBinaryTypedLayoutOffsets{ typedLayoutBegin, cursor };
}

Expected<u64> AssetsGraphicsFixture::FindShaderArchiveSourceChecksum(const Span<const NWB::Core::ShaderArchive::Record> records, const Name shaderName, const Name stageName)noexcept{
    for(const NWB::Core::ShaderArchive::Record& record : records){
        const AStringView variantName(record.variantName.data(), record.variantName.size());
        if(record.shaderName == shaderName && record.stage == stageName && variantName == NWB::Core::ShaderArchive::s_DefaultVariant){
            if(record.sourceChecksum == 0u)
                return MakeUnexpected(Failure{});
            return record.sourceChecksum;
        }
    }

    return MakeUnexpected(Failure{});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

