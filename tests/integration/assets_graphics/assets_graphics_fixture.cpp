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
constexpr AStringView s_TestsConfiguration = "tests";
constexpr AStringView s_GraphicsDirectory = "graphics";
constexpr AStringView s_AssetsDirectory = "assets";
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

    return MaterialBinaryTypedLayoutOffsets{ .layoutHashOffset = typedLayoutBegin, .blockByteCountOffset = cursor };
}




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

