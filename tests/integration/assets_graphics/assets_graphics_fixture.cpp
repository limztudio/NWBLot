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


#if defined(NWB_FINAL)
TestAString AssetsGraphicsFixture::BuildTriangleMeta(const AStringView assetHeader, const AStringView normalField, const AStringView tangentField, const AStringView vertexRefsField, const AStringView suffix){
    TestAString meta;
    meta.reserve(1536u);
    AppendTestMeta(assetHeader, meta);
    AppendTestMeta(NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_POSITIONS, meta);
    AppendTestMeta(normalField, meta);
    AppendTestMeta(tangentField, meta);
    AppendTestMeta(NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_UV0, meta);
    AppendTestMeta(NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_COLORS, meta);
    AppendTestMeta(vertexRefsField, meta);
    AppendTestMeta(NWB_ASSETS_GRAPHICS_TEST_TRIANGLE_INDICES, meta);
    AppendTestMeta(suffix, meta);
    return meta;
}
#endif

b8 AssetsGraphicsFixture::PrepareCleanDirectory(const Path& directory){
    if(!RemoveAllIfExists(directory))
        return false;
    return EnsureDirectories(directory).has_value();
}

b8 AssetsGraphicsFixture::WriteTextFile(const Path& filePath, const AStringView text){
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

Path AssetsGraphicsFixture::AssetsGraphicsTestCaseRoot(AssetsGraphicsFixture::TestArena& testArena, const AStringView caseName){
    // Use a short config-and-case key so object-cache paths stay below Windows MAX_PATH.
    TestAString caseKey;
    caseKey.reserve(1u + s_HexU32DigitCount);
    caseKey += AssetsGraphicsTestConfigurationName()[0u];
    AppendHexU32(static_cast<u32>(ComputeFnv64Text(caseName)), caseKey);
    return AssetsGraphicsTestRepoRoot(testArena) / "__build_obj" / "tests" / caseKey;
}

Expected<Path> AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(AssetsGraphicsFixture::TestArena& testArena, const AStringView caseName){
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
    const Path& root,
    const Path& outputDirectory,
    const InitializerList<Path> assetRoots,
    const u32 workerThreadCount
){
    NWB::Core::CpuTaskScheduler cookCpuTaskScheduler(workerThreadCount);
    NWB::Pipeline::AssetBuilder::AssetBuildOptions options(testArena.arena, cookCpuTaskScheduler);
    const Path normalRoot = root.lexicallyNormal();
    const auto materialExists = FileExists(root / __hidden_assets_graphics_fixture::s_AssetsDirectory / __hidden_assets_graphics_fixture::s_ShadersDirectory / "material_mesh.nwb");
    if(!materialExists)
        return false;
    b8 useCaseRootAsRepoRoot = !*materialExists;
    for(const Path& assetRoot : assetRoots){
        const Path normalAssetRoot = assetRoot.lexicallyNormal();
        if(normalAssetRoot != normalRoot && !PathHasDirectoryAncestor(normalAssetRoot, normalRoot)){
            useCaseRootAsRepoRoot = false;
            break;
        }
    }
    options.repoRoot = useCaseRootAsRepoRoot ? root : AssetsGraphicsTestRepoRoot(testArena);
    options.assetRoots.reserve(assetRoots.size());
    for(const Path& assetRoot : assetRoots){
        auto parentDirectoryName = PathToString(testArena.arena, assetRoot.lexicallyNormal().parentPath().filenameView());
        CanonicalizeTextInPlace(parentDirectoryName);

        const AStringView virtualRoot = parentDirectoryName == __hidden_assets_graphics_fixture::s_EngineDirectory ? NWB::Core::Assets::s_EngineVirtualRoot : NWB::Core::Assets::s_ProjectVirtualRoot;

        options.assetRoots.emplace_back(assetRoot, virtualRoot);
    }
    options.outputDirectory = outputDirectory;
    if(!options.configuration.assign(__hidden_assets_graphics_fixture::s_TestsConfiguration) || !options.assetType.assign(__hidden_assets_graphics_fixture::s_GraphicsDirectory))
        return false;

    NWB::Core::Alloc::ScratchArena scratchArena(s_BuildScratchArena);
    return NWB::Pipeline::AssetBuilder::BuildAssets(options, scratchArena);
}

b8 AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(
    AssetsGraphicsFixture::TestArena& testArena,
    const Path& root,
    const Path& outputDirectory,
    const InitializerList<Path> assetRoots,
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
    NWB::Core::Alloc::ScratchArena scratchArena(s_GatherScratchArena);
    return NWB::Pipeline::AssetGatherer::GatherAssets(gatherOptions, scratchArena);
}

b8 AssetsGraphicsFixture::CookSingleGraphicsMeta(const AStringView metaText, AStringView assetDirectory, AStringView assetFilename, const AssetsGraphicsFixture::CookCase& cookCase, AssetsGraphicsFixture::TestArena& testArena){
    const Path assetRoot = cookCase.root / __hidden_assets_graphics_fixture::s_AssetsDirectory;
    const Path metaPath = assetRoot / assetDirectory / assetFilename;
    if(!WriteTextFile(metaPath, metaText))
        return false;

    return CookPreparedGraphicsAssetRoots(testArena, cookCase.root, cookCase.outputDirectory, { assetRoot });
}

Expected<TestAString> AssetsGraphicsFixture::ReadSmokeAssetMeta(AssetsGraphicsFixture::TestArena& testArena, AStringView assetDirectory, AStringView assetFilename){
    TestAString metaText;
    if(!ReadTextFile(AssetsGraphicsTestRepoRoot(testArena) / __hidden_assets_graphics_fixture::s_TestsConfiguration / "smoke" / __hidden_assets_graphics_fixture::s_AssetsDirectory / assetDirectory / assetFilename, metaText))
        return MakeUnexpected(Failure{});
    return metaText;
}

b8 AssetsGraphicsFixture::CookSmokeAssetMeta(AStringView assetDirectory, AStringView assetFilename, const AssetsGraphicsFixture::CookCase& cookCase, AssetsGraphicsFixture::TestArena& testArena){
    const auto metaText = ReadSmokeAssetMeta(testArena, assetDirectory, assetFilename);
    if(!metaText)
        return false;

    return CookSingleGraphicsMeta(AStringView(metaText->data(), metaText->size()), assetDirectory, assetFilename, cookCase, testArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_fixture{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static b8 WriteMaterialBindDiscoveryMetadata(const Path& assetRoot){
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


b8 AssetsGraphicsFixture::CookMinimalMeshWithMaterialBind(const AStringView bindText, const AssetsGraphicsFixture::CookCase& cookCase, AssetsGraphicsFixture::TestArena& testArena){
    const Path assetRoot = cookCase.root / __hidden_assets_graphics_fixture::s_AssetsDirectory;
    if(!__hidden_assets_graphics_fixture::WriteMaterialBindDiscoveryMetadata(assetRoot))
        return false;
    if(!WriteTextFile(assetRoot / __hidden_assets_graphics_fixture::s_MeshesDirectory / "minimal_mesh.nwb", s_MinimalMeshMeta))
        return false;
    if(!WriteTextFile(assetRoot / __hidden_assets_graphics_fixture::s_MaterialInterfacesDirectory / __hidden_assets_graphics_fixture::s_SurfaceBindFilename, bindText))
        return false;

    return CookPreparedGraphicsAssetRoots(testArena, cookCase.root, cookCase.outputDirectory, { assetRoot });
}

Expected<NWB::Impl::MaterialBindEntry> AssetsGraphicsFixture::ParseMaterialBindFromText(const AStringView bindText, const Path& root, AssetsGraphicsFixture::TestArena& testArena, NWB::Core::Alloc::ScratchArena& scratchArena){
    const Path bindPath = root / __hidden_assets_graphics_fixture::s_AssetsDirectory / __hidden_assets_graphics_fixture::s_MaterialInterfacesDirectory / __hidden_assets_graphics_fixture::s_SurfaceBindFilename;
    if(!WriteTextFile(bindPath, bindText))
        return MakeUnexpected(Failure{});

    return NWB::Impl::ParseMaterialBindSource(bindPath, testArena.arena, scratchArena);
}

#if defined(NWB_FINAL)
Expected<Path> AssetsGraphicsFixture::EmitDuplicateGeneratedMaterialBindIncludePath(AssetsGraphicsFixture::TestArena& testArena, const AssetsGraphicsFixture::CookCase& cookCase){
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
        auto virtualPath = NWB::Core::Assets::AssetPathsDetail::BuildDerivedAssetVirtualPathText(assetRoot, NWB::Core::Assets::s_ProjectVirtualRoot, bindPath, testArena.arena, scratchArena);
        if(!virtualPath)
            return MakeUnexpected(virtualPath.error());
        bindEntry->virtualPath = Move(*virtualPath);
        bindEntries.push_back(Move(*bindEntry));
    }

    return NWB::Impl::EmitMaterialBindIncludes(cookCase.root / __hidden_assets_graphics_fixture::s_CacheDirectory, __hidden_assets_graphics_fixture::s_TestsConfiguration, bindEntries, testArena.arena, scratchArena);
}
#endif

b8 AssetsGraphicsFixture::WriteMaterialBindShaderProbeSource(
    AssetsGraphicsFixture::TestArena& testArena,
    const Path& assetRoot,
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
    const Path& assetRoot,
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

b8 AssetsGraphicsFixture::CookMaterialBindMaterialIntegrationWithPixelSource(
    const AStringView bindText,
    const AStringView materialText,
    const AStringView pixelSourceText,
    const AssetsGraphicsFixture::CookCase& cookCase,
    AssetsGraphicsFixture::TestArena& testArena
){
    const Path assetRoot = cookCase.root / __hidden_assets_graphics_fixture::s_AssetsDirectory;
    if(!WriteMaterialBindMaterialIntegrationAssetsWithPixelSource(testArena, assetRoot, bindText, materialText, pixelSourceText))
        return false;

    return CookPreparedGraphicsAssetRoots(testArena, cookCase.root, cookCase.outputDirectory, { assetRoot });
}

Expected<NWB::Core::GraphicsVector<NWB::Core::ShaderArchive::Record>> AssetsGraphicsFixture::LoadCookedShaderArchiveRecords(AssetsGraphicsFixture::TestArena& testArena, const Path& outputDirectory){
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
    const b8 loadedIndex = filesystem->readFile(NWB::Core::ShaderArchive::s_IndexVirtualPathName, indexBinary);
    EXPECT_TRUE(loadedIndex);
    EXPECT_FALSE(indexBinary.empty());
    if(!loadedIndex || indexBinary.empty())
        return MakeUnexpected(Failure{});

    auto deserialized = NWB::Core::ShaderArchive::DeserializeIndex(testArena.arena, { indexBinary.data(), indexBinary.size() });
    EXPECT_TRUE(deserialized);
    return deserialized;
}

b8 AssetsGraphicsFixture::EncodeTestMeshletRefs(
    const Span<const NWB::Impl::MeshletPositionStreamRef> positionRefs,
    const Span<const NWB::Impl::MeshletAttributeStreamRef> attributeRefs,
    const b8 skinRequired,
    const Span<NWB::Impl::MeshletDesc> meshlets,
    NWB::Core::Assets::AssetVector<u8>& outPositionRefDeltas,
    NWB::Core::Assets::AssetVector<u8>& outAttributeRefDeltas
){
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

