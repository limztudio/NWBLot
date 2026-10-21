// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"

#include <impl/assets_shader/binary_payload.h>
#include <impl/assets_shader/source_dependencies.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_shader_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_ShadersDirectory = "shaders";
constexpr AStringView s_CompilerProbeIncludeFilename = "bom_compiler_probe_include.slangi";
constexpr AStringView s_AssetsDirectory = "assets";
constexpr AStringView s_ComputeStage = "cs";
constexpr AStringView s_ExactEntryPointShaderPath = "project/shaders/exact_entry_point_ps";
constexpr AStringView s_CacheDirectory = "cache";

static constexpr AStringView s_PS = "ps";
static constexpr AStringView s_PROJECT_SHADERS_STANDALONE_PS = "project/shaders/standalone_ps";
static constexpr AStringView s_MAINCASE = "MainCase";
static constexpr AStringView s_SHADER_ASSET_HEAD = "pixel_shader asset;\n\n";
static constexpr AStringView s_ASSET_ENTRY_MAIN = "asset.entry_point = \"main\";\n";
static constexpr AStringView s_MAIN = "main";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CapturingLogger = AssetsGraphicsFixture::CapturingLogger;
using Path = AssetsGraphicsFixture::Path;
using TestArena = AssetsGraphicsFixture::TestArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// A self-contained pixel shader that includes no material bind interface (this test exercises cooking a shader without any generated material_bind_includes root).
static constexpr AStringView s_StandaloneShaderProbeSource = R"NWB_SLANG(struct NwbStandalonePixelOutput{
    float4 color : SV_Target0;
};

NwbStandalonePixelOutput main(){
    NwbStandalonePixelOutput output;
    output.color = float4(1.0, 1.0, 1.0, 1.0);
    return output;
}

)NWB_SLANG";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool WriteStandaloneShaderProbe(const Path& assetRoot){
    const auto shaderMetadata = StringFormat(assetRoot.arena(), "{}{}", s_SHADER_ASSET_HEAD, s_ASSET_ENTRY_MAIN);
    if(!AssetsGraphicsFixture::WriteTextFile(assetRoot / s_ShadersDirectory / "standalone_ps.nwb", shaderMetadata))
        return false;

    return AssetsGraphicsFixture::WriteTextFile(assetRoot / s_ShadersDirectory / "standalone_ps.slang", s_StandaloneShaderProbeSource);
}

// This reaches Slang after dependency collection.
// Both files deliberately start with a UTF-8 BOM so the compiler input normalization path, not only the dependency scanner, must make the generated invocation valid.
static constexpr AStringView s_BomCompilerProbeIncludeSource = "\xEF\xBB\xBF" R"NWB_SLANG(float4 nwbBomCompilerProbeColor(){
    return float4(0.1, 0.2, 0.3, 1.0);
}

)NWB_SLANG";

static constexpr AStringView s_BomCompilerProbeSource = "\xEF\xBB\xBF" R"NWB_SLANG(#include "bom_compiler_probe_include.slangi"

struct NwbBomCompilerProbeOutput{
    float4 color : SV_Target0;
};

NwbBomCompilerProbeOutput main(){
    NwbBomCompilerProbeOutput output;
    output.color = nwbBomCompilerProbeColor();
    return output;
}

)NWB_SLANG";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool WriteBomCompilerProbe(const Path& assetRoot){
    const auto shaderMetadata = StringFormat(assetRoot.arena(), "{}{}", s_SHADER_ASSET_HEAD, s_ASSET_ENTRY_MAIN);
    if(!AssetsGraphicsFixture::WriteTextFile(assetRoot / s_ShadersDirectory / "bom_compiler_probe_ps.nwb", shaderMetadata))
        return false;
    if(!AssetsGraphicsFixture::WriteTextFile(assetRoot / s_ShadersDirectory / s_CompilerProbeIncludeFilename, s_BomCompilerProbeIncludeSource))
        return false;

    return AssetsGraphicsFixture::WriteTextFile(assetRoot / s_ShadersDirectory / "bom_compiler_probe_ps.slang", s_BomCompilerProbeSource);
}

static constexpr AStringView s_ExactEntryPointShaderProbeSource = R"NWB_SLANG(struct NwbExactEntryPointPixelOutput{
    float4 color : SV_Target0;
};

NwbExactEntryPointPixelOutput MainCase(){
    NwbExactEntryPointPixelOutput output;
    output.color = float4(1.0, 1.0, 1.0, 1.0);
    return output;
}

)NWB_SLANG";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool WriteExactEntryPointShaderProbe(const Path& assetRoot){
    const auto shaderMetadata = StringFormat(assetRoot.arena(), "{}{}", s_SHADER_ASSET_HEAD, "asset.entry_point = \"MainCase\";\n");
    if(!AssetsGraphicsFixture::WriteTextFile(assetRoot / s_ShadersDirectory / "exact_entry_point_ps.nwb", shaderMetadata))
        return false;

    return AssetsGraphicsFixture::WriteTextFile(assetRoot / s_ShadersDirectory / "exact_entry_point_ps.slang", s_ExactEntryPointShaderProbeSource);
}


TEST(AssetsGraphics, ConcreteShaderAdmissionRejectsWrongStageAndPreservesLoadedState){
    CapturingLogger logger;
    const Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    auto cookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, "concrete_shader_stage_admission");
    ASSERT_TRUE(cookCase);
    Path& root = cookCase->root;
    Path& outputDirectory = cookCase->outputDirectory;

    const Path assetRoot = root / s_AssetsDirectory;
    ASSERT_TRUE(WriteStandaloneShaderProbe(assetRoot));
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(assetRoot / "shaders" / "stage_cs.nwb", "compute_shader asset;\nasset.entry_point = \"MainC\";\n"));
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(assetRoot / "shaders" / "stage_cs.slang", "[numthreads(1, 1, 1)] void MainC(){}\n"));
    ASSERT_TRUE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }));
    const Expected<Name> pixelPathResult = Core::ShaderArchive::BuildVirtualPathName(Name(s_PROJECT_SHADERS_STANDALONE_PS), Core::ShaderArchive::s_DefaultVariant, Name(s_PS));
    const Expected<Name> computePathResult = Core::ShaderArchive::BuildVirtualPathName(Name("project/shaders/stage_cs"), Core::ShaderArchive::s_DefaultVariant, Name(s_ComputeStage));
    ASSERT_TRUE(pixelPathResult);
    ASSERT_TRUE(computePathResult);
    const Name& pixelPath = *pixelPathResult;
    const Name& computePath = *computePathResult;
    UniquePtr<Core::Assets::IAsset> pixelAsset;
    UniquePtr<Core::Assets::IAsset> computeAsset;
    auto pixelAssetLoadResult = AssetsGraphicsFixture::LoadCookedAsset<Impl::PixelShaderAssetCodec>(testArena, outputDirectory, pixelPath, 3u);
    ASSERT_TRUE(pixelAssetLoadResult);
    pixelAsset = Move(*pixelAssetLoadResult);
    auto computeAssetLoadResult = AssetsGraphicsFixture::LoadCookedAsset<Impl::ComputeShaderAssetCodec>(testArena, outputDirectory, computePath, 3u);
    ASSERT_TRUE(computeAssetLoadResult);
    computeAsset = Move(*computeAssetLoadResult);
    EXPECT_EQ(logger.errorCount(), 0u);
    const Impl::PixelShader& cookedPixel = static_cast<const Impl::PixelShader&>(*pixelAsset);
    const Impl::ComputeShader& cookedCompute = static_cast<const Impl::ComputeShader&>(*computeAsset);
    Core::Assets::AssetBytes pixelBinary(testArena.arena);
    Core::Assets::AssetBytes computeBinary(testArena.arena);
    ASSERT_EQ(Impl::ShaderBinaryPayload::EncodeAssetPayload(AStringView(cookedPixel.entryPoint()), cookedPixel.bytecode(), pixelBinary), Impl::ShaderBinaryPayload::AssetPayloadEncodeFailure::None);
    ASSERT_EQ(Impl::ShaderBinaryPayload::EncodeAssetPayload(AStringView(cookedCompute.entryPoint()), cookedCompute.bytecode(), computeBinary), Impl::ShaderBinaryPayload::AssetPayloadEncodeFailure::None);

    const Impl::PixelShaderAssetCodec pixelCodec;
    const Impl::ComputeShaderAssetCodec computeCodec;
    EXPECT_FALSE(pixelCodec.deserialize(testArena.arena, pixelPath, computeBinary));
    EXPECT_FALSE(computeCodec.deserialize(testArena.arena, computePath, pixelBinary));

    Impl::PixelShader pixel(testArena.arena, pixelPath);
    Impl::ComputeShader compute(testArena.arena, computePath);
    ASSERT_TRUE(pixel.loadBinary(pixelBinary));
    // A five-byte entry point puts SPIR-V at an unaligned payload offset.
    ASSERT_TRUE(compute.loadBinary(computeBinary));
    const u8* const pixelBytes = pixel.bytecode().data();
    const u8* const computeBytes = compute.bytecode().data();
    const char* const pixelEntry = pixel.entryPoint().data();
    const char* const computeEntry = compute.entryPoint().data();
    EXPECT_FALSE(pixel.loadBinary(computeBinary));
    EXPECT_FALSE(compute.loadBinary(pixelBinary));
    Core::Assets::AssetBytes malformedPixelBinary = pixelBinary;
    AppendPOD(malformedPixelBinary, 0u);
    EXPECT_FALSE(pixel.loadBinary(malformedPixelBinary));
    Core::Assets::AssetBytes missingEntryBinary(testArena.arena);
    ASSERT_EQ(Impl::ShaderBinaryPayload::EncodeAssetPayload("absent", cookedCompute.bytecode(), missingEntryBinary), Impl::ShaderBinaryPayload::AssetPayloadEncodeFailure::None);
    EXPECT_FALSE(compute.loadBinary(missingEntryBinary));
    EXPECT_EQ(pixel.bytecode().data(), pixelBytes);
    EXPECT_EQ(compute.bytecode().data(), computeBytes);
    EXPECT_EQ(pixel.entryPoint().data(), pixelEntry);
    EXPECT_EQ(compute.entryPoint().data(), computeEntry);
    EXPECT_EQ(pixel.bytecode(), cookedPixel.bytecode());
    EXPECT_EQ(compute.bytecode(), cookedCompute.bytecode());
    EXPECT_EQ(AStringView(pixel.entryPoint()), AStringView(cookedPixel.entryPoint()));
    EXPECT_EQ(AStringView(compute.entryPoint()), AStringView(cookedCompute.entryPoint()));
    EXPECT_TRUE(RemoveAllIfExists(root));
}


TEST(AssetsGraphics, GatherIndependentShaderBuildsWithoutSources){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger, NWB::Core::Common::LoggerBreakPolicy::BreakOnFatal);

    TestArena testArena;
    auto rootResult = AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "gather_independent_shaders");
    ASSERT_TRUE(rootResult);
    Path root = Move(*rootResult);

    const Path firstAssetRoot = root / "first" / s_AssetsDirectory;
    const Path secondAssetRoot = root / "second" / s_AssetsDirectory;
    const Path firstBuilt = root / "first_built";
    const Path secondBuilt = root / "second_built";
    const Path conflictingBuilt = root / "conflicting_built";
    const Path outputDirectory = root / "gathered";
    ASSERT_TRUE(WriteStandaloneShaderProbe(firstAssetRoot));
    ASSERT_TRUE(WriteExactEntryPointShaderProbe(secondAssetRoot));
    ASSERT_TRUE(AssetsGraphicsFixture::BuildPreparedGraphicsAssetRoots(testArena, root, firstBuilt, { firstAssetRoot }));
    ASSERT_TRUE(AssetsGraphicsFixture::BuildPreparedGraphicsAssetRoots(testArena, root, secondBuilt, { secondAssetRoot }));

    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(firstAssetRoot / "shaders" / "standalone_ps.slang", "float4 main() : SV_Target0{ return float4(0.0, 1.0, 0.0, 1.0); }\n"));
    ASSERT_TRUE(AssetsGraphicsFixture::BuildPreparedGraphicsAssetRoots(testArena, root, conflictingBuilt, { firstAssetRoot }));

    ASSERT_TRUE(RemoveAllIfExists(firstAssetRoot));
    ASSERT_TRUE(RemoveAllIfExists(secondAssetRoot));

    NWB::Pipeline::AssetGatherer::AssetGatherOptions options(testArena.arena);
    options.inputs.emplace_back(PathToString(testArena.arena, firstBuilt));
    options.inputs.emplace_back(PathToString(testArena.arena, secondBuilt));
    options.outputDirectory = PathToString(testArena.arena, outputDirectory);
    options.configuration = "tests";
    options.mergePayloads = &NWB::Impl::MergeGatheredGraphicsAsset;
    ASSERT_TRUE(NWB::Pipeline::AssetGatherer::GatherAssets(options));

    NWB::Core::GraphicsVector<NWB::Core::ShaderArchive::Record> records(testArena.arena);
    auto recordsLoadResult = AssetsGraphicsFixture::LoadCookedShaderArchiveRecords(testArena, outputDirectory);
    ASSERT_TRUE(recordsLoadResult);
    records = Move(*recordsLoadResult);
    ASSERT_EQ(records.size(), s_ExpectedDualCount);
    const Name shaderNames[] = { Name(s_PROJECT_SHADERS_STANDALONE_PS), Name(s_ExactEntryPointShaderPath) };
    for(const Name& shaderName : shaderNames){
        Name virtualPath;
        const auto virtualPathResult4 = NWB::Core::ShaderArchive::FindVirtualPath(records, shaderName, NWB::Core::ShaderArchive::s_DefaultVariant, Name(s_PS));
        ASSERT_TRUE(virtualPathResult4);
        virtualPath = *virtualPathResult4;
        UniquePtr<NWB::Core::Assets::IAsset> shader;
        auto shaderLoadResult = AssetsGraphicsFixture::LoadCookedAsset<NWB::Impl::PixelShaderAssetCodec>(testArena, outputDirectory, virtualPath, 3u);
        ASSERT_TRUE(shaderLoadResult);
        shader = Move(*shaderLoadResult);
        EXPECT_FALSE(static_cast<const NWB::Impl::PixelShader&>(*shader).bytecode().empty());
    }
    EXPECT_EQ(logger.errorCount(), 0u);

    u64 originalChecksum = 0u;
    auto originalChecksumLoadResult = AssetsGraphicsFixture::FindShaderArchiveSourceChecksum(records, shaderNames[0], Name(s_PS));
    ASSERT_TRUE(originalChecksumLoadResult);
    originalChecksum = *originalChecksumLoadResult;
    options.inputs.emplace_back(PathToString(testArena.arena, conflictingBuilt));
    EXPECT_FALSE(NWB::Pipeline::AssetGatherer::GatherAssets(options));
    EXPECT_GT(logger.errorCount(), 0u);
    records.clear();
    auto recordsLoadResult2 = AssetsGraphicsFixture::LoadCookedShaderArchiveRecords(testArena, outputDirectory);
    ASSERT_TRUE(recordsLoadResult2);
    records = Move(*recordsLoadResult2);
    ASSERT_EQ(records.size(), s_ExpectedDualCount);
    u64 preservedChecksum = 0u;
    auto preservedChecksumLoadResult = AssetsGraphicsFixture::FindShaderArchiveSourceChecksum(records, shaderNames[0], Name(s_PS));
    ASSERT_TRUE(preservedChecksumLoadResult);
    preservedChecksum = *preservedChecksumLoadResult;
    EXPECT_EQ(preservedChecksum, originalChecksum);
    EXPECT_TRUE(RemoveAllIfExists(root));
}


static void ExpectNoShaderCompilerWorkDirectories(const Path& cacheDirectory){
    const auto entries = RecursiveDirectoryIterator<Path::Arena>::Create(cacheDirectory);
    ASSERT_TRUE(entries);
    for(const auto& entry : *entries){
        const auto filename = PathToString(cacheDirectory.arena(), entry.path().filename());
        EXPECT_FALSE(AStringView(filename).starts_with(".nwb_shader_")) << filename;
    }
}

TEST(AssetsGraphics, ShaderCookCompilesBomPrefixedSourceAndExternalIncludeAndCleansFailedInvocation){
    CapturingLogger logger;
    const Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    auto cookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, "shader_cook_bom_compiler_inputs");
    ASSERT_TRUE(cookCase);
    Path& root = cookCase->root;
    Path& outputDirectory = cookCase->outputDirectory;

    const Path assetRoot = root / s_AssetsDirectory;
    ASSERT_TRUE(WriteBomCompilerProbe(assetRoot));
    const Path externalIncludeRoot = root / "external" / "includes";
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(externalIncludeRoot / "bom_compiler_probe_include.slangi", s_BomCompilerProbeIncludeSource));
    const auto removedInclude = RemoveFile(assetRoot / s_ShadersDirectory / s_CompilerProbeIncludeFilename);
    ASSERT_TRUE(removedInclude);
    ASSERT_TRUE(*removedInclude);
    const auto externalIncludeRootText = PathToString(testArena.arena, externalIncludeRoot);
    const auto shaderMetadata = StringFormat(testArena.arena, "{}{}asset.include_roots = [\"{}\"];\n", s_SHADER_ASSET_HEAD, s_ASSET_ENTRY_MAIN, externalIncludeRootText);
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(assetRoot / "shaders" / "bom_compiler_probe_ps.nwb", shaderMetadata));
    ASSERT_TRUE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }));
    EXPECT_EQ(logger.errorCount(), 0u);
    ExpectNoShaderCompilerWorkDirectories(root / s_CacheDirectory);
    const Expected<Name> shaderPathResult = Core::ShaderArchive::BuildVirtualPathName(Name("project/shaders/bom_compiler_probe_ps"), Core::ShaderArchive::s_DefaultVariant, Name(s_PS));
    ASSERT_TRUE(shaderPathResult);
    const Name& shaderPath = *shaderPathResult;
    UniquePtr<Core::Assets::IAsset> loadedShader;
    auto loadedShaderLoadResult = AssetsGraphicsFixture::LoadCookedAsset<Impl::PixelShaderAssetCodec>(testArena, outputDirectory, shaderPath, s_ExpectedDualCount);
    ASSERT_TRUE(loadedShaderLoadResult);
    loadedShader = Move(*loadedShaderLoadResult);

    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(assetRoot / "shaders" / "bom_compiler_probe_ps.slang", "\xEF\xBB\xBF#error BOM_CLEANUP_REGRESSION\n"));
    EXPECT_FALSE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("BOM_CLEANUP_REGRESSION")));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("compiler exit code")));
    ExpectNoShaderCompilerWorkDirectories(root / s_CacheDirectory);
    const usize failureErrors = logger.errorCount();
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(assetRoot / "shaders" / "bom_compiler_probe_ps.slang", s_BomCompilerProbeSource));
    EXPECT_TRUE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }));
    EXPECT_EQ(logger.errorCount(), failureErrors);
    ExpectNoShaderCompilerWorkDirectories(root / s_CacheDirectory);
    EXPECT_TRUE(RemoveAllIfExists(root));
}


TEST(AssetsGraphics, ShaderCookPreservesExactEntryPoint){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    auto cookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, "shader_cook_exact_entry_point");
    ASSERT_TRUE(cookCase);
    Path& root = cookCase->root;
    Path& outputDirectory = cookCase->outputDirectory;


    const Path assetRoot = root / s_AssetsDirectory;
    EXPECT_TRUE(WriteExactEntryPointShaderProbe(assetRoot));

    const bool cooked = AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot });
    EXPECT_TRUE(cooked);
    if(cooked){
        const Expected<Name> shaderVirtualPathResult = NWB::Core::ShaderArchive::BuildVirtualPathName(Name(s_ExactEntryPointShaderPath), NWB::Core::ShaderArchive::s_DefaultVariant, Name(s_PS));
        ASSERT_TRUE(shaderVirtualPathResult);
        const Name& shaderVirtualPath = *shaderVirtualPathResult;
        UniquePtr<NWB::Core::Assets::IAsset> loadedShader;
        auto loadedShaderLoadResult2 = AssetsGraphicsFixture::LoadCookedAsset<NWB::Impl::PixelShaderAssetCodec>(testArena, outputDirectory, shaderVirtualPath, s_ExpectedDualCount);
        ASSERT_TRUE(loadedShaderLoadResult2);
        loadedShader = Move(*loadedShaderLoadResult2);
        if(loadedShader){
            const NWB::Impl::PixelShader& shader = static_cast<const NWB::Impl::PixelShader&>(*loadedShader);
            EXPECT_EQ(AStringView(shader.entryPoint()), s_MAINCASE);

            AStringView resolvedEntryPoint;
            NWB::Core::GraphicsVector<u32> words(testArena.arena);
            words.resize(shader.bytecode().size() / sizeof(u32));
            NWB_MEMCPY(words.data(), shader.bytecode().size(), shader.bytecode().data(), shader.bytecode().size());
            const auto entryPointResult11 = NWB::Core::ResolveSpirvEntryPointName(words.data(), words.size(), AStringView(shader.entryPoint()), NWB::Core::ShaderType::Pixel);
            ASSERT_TRUE(entryPointResult11);
            resolvedEntryPoint = *entryPointResult11;
            EXPECT_EQ(resolvedEntryPoint, s_MAINCASE);
            const auto entryPointResult12 = NWB::Core::ResolveSpirvEntryPointName(words.data(), words.size(), s_MAIN, NWB::Core::ShaderType::Pixel);
            ASSERT_FALSE(entryPointResult12);
            EXPECT_EQ(entryPointResult12.error(), NWB::Core::SpirvEntryPointLookupResult::NotFound);

        }
    }
    EXPECT_EQ(logger.errorCount(), 0u);

    EXPECT_TRUE(RemoveAllIfExists(root));
}

[[nodiscard]] static Expected<Path> FindSingleShaderBytecodeCachePath(TestArena& testArena, const Path& cacheDirectory){
    Path path(testArena.arena);

    const auto cacheEntries = RecursiveDirectoryIterator<Path::Arena>::Create(cacheDirectory);
    EXPECT_TRUE(cacheEntries);
    if(!cacheEntries)
        return MakeUnexpected(Failure{});

    usize foundCount = 0u;
    for(const auto& entry : *cacheEntries){
        const auto isRegularFile = entry.isRegularFile();
        EXPECT_TRUE(isRegularFile);
        if(!isRegularFile)
            return MakeUnexpected(Failure{});
        if(!*isRegularFile)
            continue;

        const auto extension = PathToString(testArena.arena, entry.path().extension());
        if(extension != ".spv")
            continue;

        path = entry.path();
        ++foundCount;
    }

    EXPECT_EQ(foundCount, 1u);
    if(foundCount != 1u)
        return MakeUnexpected(Failure{});
    return path;
}

TEST(AssetsGraphics, ShaderCookRebuildsTruncatedMalformedAndWrongStageBytecodeCaches){
    CapturingLogger logger;
    const Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    auto cookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, "shader_cook_invalid_bytecode_cache");
    ASSERT_TRUE(cookCase);
    Path& root = cookCase->root;
    Path& outputDirectory = cookCase->outputDirectory;

    const Path assetRoot = root / s_AssetsDirectory;
    ASSERT_TRUE(WriteStandaloneShaderProbe(assetRoot));
    ASSERT_TRUE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }));
    const auto bytecodeCachePath = FindSingleShaderBytecodeCachePath(testArena, root / s_CacheDirectory);
    ASSERT_TRUE(bytecodeCachePath);
    Core::Assets::AssetBytes originalBytes(testArena.arena);
    ASSERT_TRUE(ReadBinaryFile(*bytecodeCachePath, originalBytes));
    ASSERT_GT(originalBytes.size(), sizeof(u32));

    const Path computeRoot = root / "compute_probe";
    const Path computeAssets = computeRoot / s_AssetsDirectory;
    const Path computeOutput = computeRoot / "cooked";
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(computeAssets / "shaders" / "cache_stage_cs.nwb", "compute_shader asset;\nasset.entry_point = \"main\";\n"));
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(computeAssets / "shaders" / "cache_stage_cs.slang", "[numthreads(1, 1, 1)] void main(){}\n"));
    ASSERT_TRUE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, computeRoot, computeOutput, { computeAssets }));
    const Expected<Name> computePathResult = Core::ShaderArchive::BuildVirtualPathName(Name("project/shaders/cache_stage_cs"), Core::ShaderArchive::s_DefaultVariant, Name(s_ComputeStage));
    ASSERT_TRUE(computePathResult);
    const Name& computePath = *computePathResult;
    UniquePtr<Core::Assets::IAsset> computeAsset;
    auto computeAssetLoadResult2 = AssetsGraphicsFixture::LoadCookedAsset<Impl::ComputeShaderAssetCodec>(testArena, computeOutput, computePath, s_ExpectedDualCount);
    ASSERT_TRUE(computeAssetLoadResult2);
    computeAsset = Move(*computeAssetLoadResult2);
    const auto& compute = static_cast<const Impl::ComputeShader&>(*computeAsset);
    const Expected<Name> pixelPathResult = Core::ShaderArchive::BuildVirtualPathName(Name(s_PROJECT_SHADERS_STANDALONE_PS), Core::ShaderArchive::s_DefaultVariant, Name(s_PS));
    ASSERT_TRUE(pixelPathResult);
    const Name& pixelPath = *pixelPathResult;
    static constexpr AStringView s_CorruptionNames[] = { "magic", "truncated", "malformed tail", "wrong physical stage" };
    Core::Assets::AssetBytes corrupted(testArena.arena);
    Core::Assets::AssetBytes repaired(testArena.arena);
    for(usize corruption = 0u; corruption < LengthOf(s_CorruptionNames); ++corruption){
        SCOPED_TRACE(s_CorruptionNames[corruption]);
        corrupted.assign(originalBytes.begin(), originalBytes.end());
        switch(corruption){
        case 0u:
            corrupted.assign({ 'B', 'A', 'D', '!' });
            break;
        case 1u:
            corrupted.pop_back();
            break;
        case 2u:
            corrupted.resize(originalBytes.size() + sizeof(u32), 0u);
            break;
        case 3u:
            corrupted.assign(compute.bytecode().begin(), compute.bytecode().end());
            break;
        }
        ASSERT_TRUE(WriteBinaryFile(*bytecodeCachePath, corrupted));
        ASSERT_TRUE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }));
        ASSERT_TRUE(ReadBinaryFile(*bytecodeCachePath, repaired));
        AStringView resolvedEntryPoint;
        const auto entryPointResult13 = Core::ResolveSpirvEntryPointName({ repaired.data(), repaired.size() }, s_MAIN, Core::ShaderType::Pixel);
        ASSERT_TRUE(entryPointResult13);
        resolvedEntryPoint = *entryPointResult13;
        EXPECT_EQ(resolvedEntryPoint, s_MAIN);
        UniquePtr<Core::Assets::IAsset> pixelAsset;
        auto pixelAssetLoadResult2 = AssetsGraphicsFixture::LoadCookedAsset<Impl::PixelShaderAssetCodec>(testArena, outputDirectory, pixelPath, s_ExpectedDualCount);
        ASSERT_TRUE(pixelAssetLoadResult2);
        pixelAsset = Move(*pixelAssetLoadResult2);
        const auto& pixel = static_cast<const Impl::PixelShader&>(*pixelAsset);
        EXPECT_EQ(AStringView(pixel.entryPoint()), s_MAIN);
    }
    EXPECT_EQ(logger.errorCount(), 0u);
    EXPECT_TRUE(RemoveAllIfExists(root));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

