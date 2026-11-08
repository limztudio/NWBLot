// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"

#include <impl/assets_graphics/components.adhd.h>

#include <core/assets/input_list.h>
#include <core/assets/cook_metadata_internal.h>
#include <core/assets/volume/built_asset.h>

#include <global/filesystem/executable_paths.h>
#include <global/sha1.h>

#include <gtest/gtest.h>

#include <global/atomic.h>
#include <global/sync.h>
#include <global/thread.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_asset_cache_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_AssetsDirectory = AssetsGraphicsFixture::s_AssetsDirectory;
constexpr AStringView s_TestsConfiguration = AssetsGraphicsFixture::s_TestsConfiguration;
static constexpr AStringView s_SamplerMetadata =
    "sampler asset;\n"
    "asset.min_filter = \"linear\";\n"
    "asset.mag_filter = \"linear\";\n"
    "asset.mip_filter = \"linear\";\n"
    "asset.address_u = \"wrap\";\n"
    "asset.address_v = \"wrap\";\n"
    "asset.address_w = \"wrap\";\n"
    "asset.reduction = \"standard\";\n"
    "asset.max_anisotropy = 1.0;\n"
    "asset.mip_bias = 0.0;\n";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = AssetsGraphicsFixture::TestArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AssetsGraphics, AssetCacheWritesLowercaseRelativePathSha1AndInvalidatesOnParamChange){
    static constexpr AStringView s_ModifiedSamplerMetadata =
        "sampler asset;\n"
        "asset.min_filter = \"linear\";\n"
        "asset.mag_filter = \"linear\";\n"
        "asset.mip_filter = \"linear\";\n"
        "asset.address_u = \"wrap\";\n"
        "asset.address_v = \"wrap\";\n"
        "asset.address_w = \"wrap\";\n"
        "asset.reduction = \"standard\";\n"
        "asset.max_anisotropy = 1.0;\n"
        "asset.mip_bias = 3.5;\n";

    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger, NWB::Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;

    NWB::Core::Assets::AssetString emptySha1Hex(testArena.arena);
    AppendSha1Hex(ComputeSha1Text(""), emptySha1Hex);
    EXPECT_EQ(AStringView(emptySha1Hex), AStringView("da39a3ee5e6b4b0d3255bfef95601890afd80709"));
    NWB::Core::Assets::AssetString abcSha1Hex(testArena.arena);
    AppendSha1Hex(ComputeSha1Text("abc"), abcSha1Hex);
    EXPECT_EQ(AStringView(abcSha1Hex), AStringView("a9993e364706816aba3e25717850c26c9cd0d89d"));

    auto cookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, "nwb_asset_cache_sha1_param_checksum");
    ASSERT_TRUE(cookCase);
    const Path& root = cookCase->root;
    const Path assetRoot = root / s_AssetsDirectory;
    const Path mixedCaseNwbPath = assetRoot / "Samplers" / "First_Sampler.nwb";
    const Name virtualPath = NWB::Tests::AssetsGraphicsFixture::s_ProjectSamplersFirstSampler;
    NWB::Core::CpuTaskScheduler cookCpuTaskScheduler(0u);

    const auto buildAndGatherWithRepoRoot = [&](const Path& builtDir, const Path& outDir) -> b8{
        NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_CodecScratchArena);
        NWB::Pipeline::AssetBuilder::AssetBuildOptions buildOptions(testArena.arena, cookCpuTaskScheduler);
        buildOptions.repoRoot = root;
        buildOptions.outputDirectory = builtDir;
        if(!buildOptions.configuration.assign(s_TestsConfiguration) || !buildOptions.assetType.assign("graphics"))
            return false;
        buildOptions.assetRoots.emplace_back(assetRoot, NWB::Core::Assets::s_ProjectVirtualRoot);
        if(!NWB::Pipeline::AssetBuilder::BuildAssets(buildOptions, scratchArena))
            return false;

        NWB::Pipeline::AssetGatherer::AssetGatherOptions gatherOptions(testArena.arena);
        gatherOptions.inputs.emplace_back(PathToString(testArena.arena, builtDir));
        gatherOptions.outputDirectory = PathToString(testArena.arena, outDir);
        gatherOptions.configuration = s_TestsConfiguration.data();
        return NWB::Pipeline::AssetGatherer::GatherAssets(gatherOptions, scratchArena);
    };

    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(mixedCaseNwbPath, s_SamplerMetadata));
    ASSERT_TRUE(buildAndGatherWithRepoRoot(root / "built_first", cookCase->outputDirectory));

    const Sha1Digest expectedDigest = ComputeSha1Text("assets/samplers/first_sampler.nwb");
    NWB::Core::Assets::AssetString expectedSha1Name(testArena.arena);
    AppendSha1Hex(expectedDigest, expectedSha1Name);

    const Path expectedCacheFilePath = root / "__asset_cache" / AStringView(expectedSha1Name);
    NWB::Core::Assets::AssetBytes initialCacheBytes(testArena.arena);
    ASSERT_TRUE(ReadBinaryFile(expectedCacheFilePath, initialCacheBytes));
    usize cursor = 0u;
    const auto initialHeader = ReadPOD<NWB::Core::Assets::NwbAssetCacheFileHeader>(initialCacheBytes, cursor);
    ASSERT_TRUE(initialHeader);
    EXPECT_EQ(initialHeader->magic, NWB::Core::Assets::s_NwbAssetCacheMagic);
    EXPECT_EQ(initialHeader->version, NWB::Core::Assets::s_NwbAssetCacheVersion);
    EXPECT_NE(initialHeader->flags & NWB::Core::Assets::s_NwbAssetCacheFlagStandalone, 0u);
    EXPECT_EQ(initialHeader->entryCount, 1u);

    // A cache written by a different producer must not be reused.
    {
        NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_CodecScratchArena);
        const auto executablePath = ExecutablePaths(scratchArena).path();
        ASSERT_TRUE(executablePath);
        Vector<u8, NWB::Core::Alloc::ScratchArena> executableBytes(scratchArena);
        ASSERT_TRUE(ReadBinaryFile(*executablePath, executableBytes));
        const Sha256Digest producer = ComputeSha256(BinaryByteView{ executableBytes.data(), executableBytes.size() });
        Sha256Digest otherProducer = producer;
        otherProducer.bytes[0u] ^= 1u;
        const auto normalizedPath = PathToString(testArena.arena, mixedCaseNwbPath);
        NWB::Core::Assets::DiscoveredNwbFile discovered(testArena.arena, assetRoot, mixedCaseNwbPath, AStringView(normalizedPath), NWB::Core::Assets::s_ProjectVirtualRoot);
        NWB::Core::Assets::NwbFileAssetCacheRecord originalProducer(testArena.arena);
        NWB::Core::Assets::NwbFileAssetCacheRecord staleProducer(testArena.arena);
        NWB::Core::Assets::ScratchString relativePath(scratchArena);
        NWB::Core::Assets::ScratchString cacheName(scratchArena);
        NWB::Core::Assets::CookMetadataDetail::PrepareNwbFileAssetCacheRecord(root, root / "__asset_cache", discovered, s_SamplerMetadata, producer, scratchArena, relativePath, cacheName, originalProducer);
        NWB::Core::Assets::CookMetadataDetail::PrepareNwbFileAssetCacheRecord(root, root / "__asset_cache", discovered, s_SamplerMetadata, otherProducer, scratchArena, relativePath, cacheName, staleProducer);
        ASSERT_TRUE(originalProducer.canWriteCache);
        ASSERT_TRUE(staleProducer.canWriteCache);
        EXPECT_EQ(originalProducer.paramSha256, initialHeader->paramSha256);
        EXPECT_NE(staleProducer.paramSha256, originalProducer.paramSha256);
        auto staleBytes = initialCacheBytes;
        auto staleHeader = *initialHeader;
        staleHeader.paramSha256 = staleProducer.paramSha256;
        ASSERT_TRUE(AssetsGraphicsFixture::OverwritePOD(0u, staleHeader, staleBytes));
        ASSERT_TRUE(WriteBinaryFile(expectedCacheFilePath, staleBytes));
        ASSERT_TRUE(buildAndGatherWithRepoRoot(root / "built_new_producer", root / "out_new_producer"));
        NWB::Core::Assets::AssetBytes refreshed(testArena.arena);
        ASSERT_TRUE(ReadBinaryFile(expectedCacheFilePath, refreshed));
        EXPECT_EQ(refreshed, initialCacheBytes);

        const Path bunchPath = assetRoot / "Samplers" / "Sidecar_Bunch.nwb";
        const Path fontPath = assetRoot / "Samplers" / "Sidecar_Bunch.font";
        const Path texturePath = assetRoot / "Samplers" / "Sidecar_Bunch.tex";
        ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(fontPath, "font-one"));
        ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(texturePath, "texture-one"));
        const auto bunchPathText = PathToString(testArena.arena, bunchPath);
        NWB::Core::Assets::DiscoveredNwbFile bunch(testArena.arena, assetRoot, bunchPath, AStringView(bunchPathText), NWB::Core::Assets::s_ProjectVirtualRoot);
        NWB::Core::Assets::ScratchString largeMetadata(scratchArena);
        largeMetadata.resize(70000u, ' ');
        largeMetadata.append("ASSET_BUNCH asset; font f; texture t;");
        NWB::Core::Assets::NwbFileAssetCacheRecord initialSidecars(testArena.arena);
        NWB::Core::Assets::NwbFileAssetCacheRecord changedFont(testArena.arena);
        NWB::Core::Assets::NwbFileAssetCacheRecord changedTexture(testArena.arena);
        const auto prepareSidecars = [&](NWB::Core::Assets::NwbFileAssetCacheRecord& record){
            NWB::Core::Assets::CookMetadataDetail::PrepareNwbFileAssetCacheRecord(root, root / "__asset_cache", bunch, AStringView(largeMetadata), producer, scratchArena, relativePath, cacheName, record);
        };
        prepareSidecars(initialSidecars);
        ASSERT_TRUE(initialSidecars.canWriteCache);
        ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(fontPath, "font-two"));
        prepareSidecars(changedFont);
        ASSERT_TRUE(changedFont.canWriteCache);
        EXPECT_NE(initialSidecars.paramSha256, changedFont.paramSha256);
        ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(texturePath, "texture-two"));
        prepareSidecars(changedTexture);
        ASSERT_TRUE(changedTexture.canWriteCache);
        EXPECT_NE(changedFont.paramSha256, changedTexture.paramSha256);
    }

    // Build again into a separate outputDirectory; unchanged .nwb params reuse __asset_cache/<sha1>.
    ASSERT_TRUE(buildAndGatherWithRepoRoot(root / "built_second", root / "out_second"));

    const auto cachedSampler = AssetsGraphicsFixture::LoadCookedAsset<NWB::Impl::SamplerAssetCodec>(root / "out_second", virtualPath, 1u, testArena);
    ASSERT_TRUE(cachedSampler);
    EXPECT_FLOAT_EQ(static_cast<const NWB::Impl::Sampler*>(cachedSampler->get())->description().mipBias, 0.0f);

    // Corrupt the cached payload section hash; build must reject the corrupted cache entry and repair it.
    auto corruptedCacheBytes = initialCacheBytes;
    auto corruptedHeader = *initialHeader;
    corruptedHeader.payloadSectionHashFnv64 ^= 1u;
    ASSERT_TRUE(AssetsGraphicsFixture::OverwritePOD(0u, corruptedHeader, corruptedCacheBytes));
    ASSERT_TRUE(WriteBinaryFile(expectedCacheFilePath, corruptedCacheBytes));
    ASSERT_TRUE(buildAndGatherWithRepoRoot(root / "built_repaired", root / "out_repaired"));
    NWB::Core::Assets::AssetBytes repairedCacheBytes(testArena.arena);
    ASSERT_TRUE(ReadBinaryFile(expectedCacheFilePath, repairedCacheBytes));
    EXPECT_EQ(repairedCacheBytes, initialCacheBytes);

    // A corrupt count must be rejected before reserving memory.
    corruptedHeader = *initialHeader;
    corruptedHeader.entryCount = Limit<u32>::s_Max;
    corruptedCacheBytes = initialCacheBytes;
    ASSERT_TRUE(AssetsGraphicsFixture::OverwritePOD(0u, corruptedHeader, corruptedCacheBytes));
    ASSERT_TRUE(WriteBinaryFile(expectedCacheFilePath, corruptedCacheBytes));
    ASSERT_TRUE(buildAndGatherWithRepoRoot(root / "built_bad_count", root / "out_bad_count"));
    ASSERT_TRUE(ReadBinaryFile(expectedCacheFilePath, repairedCacheBytes));
    EXPECT_EQ(repairedCacheBytes, initialCacheBytes);

    // Modify .nwb parameter; checksum changes and invalidates __asset_cache/<sha1>.
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(mixedCaseNwbPath, s_ModifiedSamplerMetadata));
    ASSERT_TRUE(buildAndGatherWithRepoRoot(root / "built_third", root / "out_third"));

    NWB::Core::Assets::AssetBytes updatedCacheBytes(testArena.arena);
    ASSERT_TRUE(ReadBinaryFile(expectedCacheFilePath, updatedCacheBytes));
    usize updatedCursor = 0u;
    const auto updatedHeader = ReadPOD<NWB::Core::Assets::NwbAssetCacheFileHeader>(updatedCacheBytes, updatedCursor);
    ASSERT_TRUE(updatedHeader);
    EXPECT_NE(updatedHeader->paramSha256, initialHeader->paramSha256);

    const auto updatedSampler = AssetsGraphicsFixture::LoadCookedAsset<NWB::Impl::SamplerAssetCodec>(root / "out_third", virtualPath, 1u, testArena);
    ASSERT_TRUE(updatedSampler);
    EXPECT_FLOAT_EQ(static_cast<const NWB::Impl::Sampler*>(updatedSampler->get())->description().mipBias, 3.5f);
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

