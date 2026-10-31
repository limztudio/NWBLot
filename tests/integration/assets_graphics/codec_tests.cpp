// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"

#include <impl/assets_graphics/components.adhd.h>

#include <core/assets/input_list.h>
#include <core/assets/volume/built_asset.h>

#include <global/sha1.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_codec_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_AssetsDirectory = "assets";
constexpr AStringView s_TestsConfiguration = "tests";
constexpr AStringView s_FirstSamplerPath = "project/samplers/first";
constexpr AStringView s_SecondSamplerPath = "project/samplers/second";

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


using CapturingLogger = AssetsGraphicsFixture::CapturingLogger;
using Path = AssetsGraphicsFixture::Path;
using TestArena = AssetsGraphicsFixture::TestArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AssetsGraphics, DeferredWriteOwnsScratchBytesBeforeCallerMutation){
    TestArena testArena;
    const Path root = AssetsGraphicsFixture::AssetsGraphicsTestCaseRoot(testArena, "volume_scratch_bytes");
    const b8 prepared = AssetsGraphicsFixture::PrepareCleanDirectory(root);
    EXPECT_TRUE(prepared);

    if(prepared){
        NWB::Core::Filesystem::VolumeMountDesc mountDesc(testArena.arena);
        mountDesc.volumeName = "scratch_test";
        mountDesc.segmentSize = 64ull * 1024ull;
        mountDesc.metadataSize = 4ull * 1024ull;

        mountDesc.mountDirectory = root / "volume";
        mountDesc.createIfMissing = true;
        mountDesc.usage = NWB::Core::Filesystem::VolumeUsage::CookWrite;
        auto filesystemResult = NWB::Core::Filesystem::CreateFilesystem(testArena.arena, mountDesc);
        const b8 created = static_cast<b8>(filesystemResult);
        EXPECT_TRUE(created);
        if(created){
            UniquePtr<NWB::Core::Filesystem::IFilesystem> filesystem = Move(*filesystemResult);
            NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_CodecScratchArena);
            ::Vector<u8, NWB::Core::Alloc::ScratchArena> payload{ scratchArena };
            payload.reserve(4u);
            payload.push_back(1u);
            payload.push_back(s_ExpectedDualCount);
            payload.push_back(3u);
            payload.push_back(4u);

            const Name virtualPath("project/tests/scratch_payload");
            const b8 pushed = filesystem->writeFileDeferred(virtualPath, payload);
            EXPECT_TRUE(pushed);
            if(pushed){
                payload[0] = 99u;

                const b8 flushed = filesystem->flush();
                EXPECT_TRUE(flushed);
                if(flushed){
                    NWB::Core::Assets::AssetBytes readback = AssetsGraphicsFixture::MakeAssetBytes(testArena);
                    const b8 loaded = filesystem->readFile(virtualPath, readback);
                    EXPECT_TRUE(loaded);
                    if(loaded){
                        ASSERT_EQ(readback.size(), 4u);
                        EXPECT_EQ(readback[0], 1u);
                    }

                }
            }
        }
    }

    EXPECT_TRUE(RemoveAllIfExists(root));
}

TEST(AssetsGraphics, SharedCodecPayloadsKeepDistinctVirtualPathsUnderLongBuildDirectories){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger, NWB::Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    auto cookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, "shared_codec_payloads");
    ASSERT_TRUE(cookCase);
    const Path& root = cookCase->root;
    const Path& outputDirectory = cookCase->outputDirectory;
    const Path assetRoot = root / s_AssetsDirectory;
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(assetRoot / "samplers" / "first.nwb", s_SamplerMetadata));
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(assetRoot / "samplers" / "second.nwb", s_SamplerMetadata));

    // A 200-character directory leaves room for compact artifacts within the traditional Windows path limit.
    const usize rootTextLength = PathToString(testArena.arena, root).size();
    ASSERT_LT(rootTextLength, 199u);
    NWB::Core::Assets::AssetString directoryName(testArena.arena);
    directoryName.resize(199u - rootTextLength, 'b');
    const Path builtDirectory = root / directoryName;
    ASSERT_EQ(PathToString(testArena.arena, builtDirectory).size(), 200u);
    ASSERT_TRUE(AssetsGraphicsFixture::BuildPreparedGraphicsAssetRoots(testArena, root, builtDirectory, { assetRoot }, s_ExpectedDualCount));

    NWB::Pipeline::AssetGatherer::AssetGatherOptions options(testArena.arena);
    options.inputs.emplace_back(PathToString(testArena.arena, builtDirectory));
    options.outputDirectory = PathToString(testArena.arena, outputDirectory);
    options.configuration = s_TestsConfiguration.data();
    ASSERT_TRUE(NWB::Pipeline::AssetGatherer::GatherAssets(options));
    for(const Name& virtualPath : { Name(s_FirstSamplerPath), Name(s_SecondSamplerPath) }){
        const auto asset = AssetsGraphicsFixture::LoadCookedAsset<NWB::Impl::SamplerAssetCodec>(testArena, outputDirectory, virtualPath, s_ExpectedDualCount);
        ASSERT_TRUE(asset);
        EXPECT_EQ((*asset)->virtualPath(), virtualPath);
    }
    EXPECT_EQ(logger.errorCount(), 0u);
    EXPECT_TRUE(RemoveAllIfExists(root));
}

TEST(AssetsGraphics, FailedReorderedSharedPayloadBuildPreservesPublishedVirtualPaths){
    namespace Pack = NWB::Core::Assets::AssetsVolumeCookDetail;
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger, NWB::Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    auto cookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, "failed_reordered_shared_payloads");
    ASSERT_TRUE(cookCase);
    const Path& root = cookCase->root;
    const Path publishedDirectory = root / "published";
    const Name firstPath(s_FirstSamplerPath);
    const Name secondPath(s_SecondSamplerPath);
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_CodecScratchArena);

    NWB::Impl::Sampler samplerAsset(testArena.arena, firstPath);
    NWB::Core::SamplerDesc samplerDesc;
    samplerDesc.borderColor = NWB::Core::Color(0.0f);
    samplerAsset.setDescription(samplerDesc);
    const NWB::Impl::SamplerAssetCodec samplerCodec;
    NWB::Core::Assets::AssetBytes samplerPayload(testArena.arena);
    ASSERT_TRUE(samplerCodec.serialize(samplerAsset, samplerPayload));
    constexpr u64 s_SharedCookKeyHash = 0x123456789abcdef0ull;

    Pack::AssetVolumePackManifest initial(testArena.arena);
    ASSERT_TRUE(Pack::ReserveAssetVolumePackManifest(initial, s_ExpectedDualCount));
    ASSERT_TRUE(Pack::AppendPayloadBytesToManifest(initial, firstPath, samplerPayload, s_SharedCookKeyHash));
    ASSERT_TRUE(Pack::AppendPayloadBytesToManifest(initial, secondPath, samplerPayload, s_SharedCookKeyHash));
    ASSERT_TRUE(NWB::Core::Assets::BuiltAssetDetail::WriteBuiltAssets(publishedDirectory, initial, scratchArena));
    NWB::Core::Assets::AssetBytes originalList(testArena.arena);
    ASSERT_TRUE(ReadBinaryFile(publishedDirectory / NWB::Core::Assets::BuiltAssetDetail::s_ManifestFilename, originalList));
    NWB::Core::Assets::AssetVector<NWB::Core::Assets::AssetString> originalArtifactPaths(testArena.arena);
    ASSERT_TRUE(NWB::Core::Assets::ReadAssetInputList(originalArtifactPaths, publishedDirectory / NWB::Core::Assets::BuiltAssetDetail::s_ManifestFilename, scratchArena, true));
    ASSERT_EQ(originalArtifactPaths.size(), s_ExpectedDualCount);
    NWB::Core::Assets::AssetVector<NWB::Core::Assets::AssetBytes> originalArtifactBytes(testArena.arena);
    originalArtifactBytes.reserve(originalArtifactPaths.size());
    for(const auto& artifactPath : originalArtifactPaths){
        originalArtifactBytes.emplace_back(testArena.arena);
        ASSERT_TRUE(ReadBinaryFile(Path(testArena.arena, artifactPath), originalArtifactBytes.back()));
    }

    Pack::AssetVolumePackManifest reordered(testArena.arena);
    ASSERT_TRUE(Pack::ReserveAssetVolumePackManifest(reordered, s_ExpectedDualCount + 1u));
    ASSERT_TRUE(Pack::AppendPayloadBytesToManifest(reordered, secondPath, samplerPayload, s_SharedCookKeyHash));
    ASSERT_TRUE(Pack::AppendPayloadBytesToManifest(reordered, firstPath, samplerPayload, s_SharedCookKeyHash));
    // Reorder both shared-payload entries before failing, so publication also has to create a new artifact slot.
    ASSERT_TRUE(Pack::AppendPayloadBytesToManifest(reordered, Name("project/samplers/failing"), samplerPayload, s_SharedCookKeyHash));
    reordered.entries.back().identity.payloadHash ^= 1u;
    EXPECT_FALSE(NWB::Core::Assets::BuiltAssetDetail::WriteBuiltAssets(publishedDirectory, reordered, scratchArena));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("invalid payload identity")));
    NWB::Core::Assets::AssetBytes retainedList(testArena.arena);
    ASSERT_TRUE(ReadBinaryFile(publishedDirectory / NWB::Core::Assets::BuiltAssetDetail::s_ManifestFilename, retainedList));
    EXPECT_EQ(retainedList, originalList);
    NWB::Core::Assets::AssetBytes retainedArtifactBytes(testArena.arena);
    for(usize index = 0u; index < originalArtifactPaths.size(); ++index){
        ASSERT_TRUE(ReadBinaryFile(Path(testArena.arena, originalArtifactPaths[index]), retainedArtifactBytes));
        EXPECT_EQ(retainedArtifactBytes, originalArtifactBytes[index]);
    }

    NWB::Pipeline::AssetGatherer::AssetGatherOptions options(testArena.arena);
    options.inputs.emplace_back(PathToString(testArena.arena, publishedDirectory));
    options.outputDirectory = PathToString(testArena.arena, cookCase->outputDirectory);
    options.configuration = s_TestsConfiguration.data();
    ASSERT_TRUE(NWB::Pipeline::AssetGatherer::GatherAssets(options));
    for(const Name& virtualPath : { firstPath, secondPath }){
        const auto asset = AssetsGraphicsFixture::LoadCookedAsset<NWB::Impl::SamplerAssetCodec>(testArena, cookCase->outputDirectory, virtualPath, s_ExpectedDualCount);
        ASSERT_TRUE(asset);
        EXPECT_EQ((*asset)->virtualPath(), virtualPath);
    }
    EXPECT_EQ(logger.errorCount(), 1u);
    EXPECT_TRUE(RemoveAllIfExists(root));
}

TEST(AssetsGraphics, RendererComponentCookAndSerializePreservesOpticalFields){
    static constexpr AStringView s_RendererComponentMetadata =
        "renderer_component asset;\n"
        "asset.material = \"project/materials/glass\";\n"
        "asset.visible = 1;\n"
        "asset.optical_volume_coincidence = \"shared_group\";\n"
        "asset.optical_volume_group = \"lens_assembly\";\n"
        "asset.optical_volume_priority = 4;\n"
        "asset.optical_boundary_mode = \"closed_priority\";\n"
        "asset.optical_medium_priority = 9;\n";

    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger, NWB::Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    auto cookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, "renderer_component_optical_fields");
    ASSERT_TRUE(cookCase);
    const Path& root = cookCase->root;
    const Path& outputDirectory = cookCase->outputDirectory;
    const Path assetRoot = root / s_AssetsDirectory;
    const Name virtualPath("project/entities/lens/renderer");

    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(assetRoot / "entities" / "lens" / "renderer.nwb", s_RendererComponentMetadata));
    ASSERT_TRUE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }, 1u));

    const auto loadedAsset = AssetsGraphicsFixture::LoadCookedAsset<NWB::Impl::ComponentAssetCodec>(testArena, outputDirectory, virtualPath, 1u);
    ASSERT_TRUE(loadedAsset);
    ASSERT_NE(loadedAsset->get(), nullptr);
    EXPECT_EQ((*loadedAsset)->assetType(), NWB::Impl::RendererComponentAsset::AssetTypeName());
    const auto* rendererAsset = static_cast<const NWB::Impl::RendererComponentAsset*>(loadedAsset->get());
    EXPECT_EQ(rendererAsset->component.material.name(), Name("project/materials/glass"));
    EXPECT_TRUE(rendererAsset->component.visible);
    EXPECT_EQ(rendererAsset->component.opticalVolumeCoincidence, NWB::Impl::OpticalVolumeCoincidence::SharedGroup);
    EXPECT_EQ(rendererAsset->component.opticalVolumeGroup, Name("lens_assembly"));
    EXPECT_EQ(rendererAsset->component.opticalVolumePriority, 4);
    EXPECT_EQ(rendererAsset->component.opticalBoundaryMode, NWB::Impl::OpticalBoundaryMode::ClosedPriority);
    EXPECT_EQ(rendererAsset->component.opticalMediumPriority, 9);
    EXPECT_EQ(logger.errorCount(), 0u);
    EXPECT_TRUE(RemoveAllIfExists(root));
}

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
    const Name virtualPath("project/samplers/first_sampler");
    NWB::Core::CpuTaskScheduler cookCpuTaskScheduler(0u);

    const auto buildAndGatherWithRepoRoot = [&](const Path& builtDir, const Path& outDir) -> b8{
        NWB::Pipeline::AssetBuilder::AssetBuildOptions buildOptions(testArena.arena, cookCpuTaskScheduler);
        buildOptions.repoRoot = PathToString(testArena.arena, root);
        buildOptions.outputDirectory = PathToString(testArena.arena, builtDir);
        if(!buildOptions.configuration.assign(s_TestsConfiguration) || !buildOptions.assetType.assign("graphics"))
            return false;
        const auto assetRootText = PathToString(testArena.arena, assetRoot);
        buildOptions.assetRoots.emplace_back(testArena.arena, assetRootText, NWB::Core::Assets::s_ProjectVirtualRoot);
        if(!NWB::Pipeline::AssetBuilder::BuildAssets(buildOptions))
            return false;

        NWB::Pipeline::AssetGatherer::AssetGatherOptions gatherOptions(testArena.arena);
        gatherOptions.inputs.emplace_back(PathToString(testArena.arena, builtDir));
        gatherOptions.outputDirectory = PathToString(testArena.arena, outDir);
        gatherOptions.configuration = s_TestsConfiguration.data();
        return NWB::Pipeline::AssetGatherer::GatherAssets(gatherOptions);
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

    // Build again into a separate outputDirectory; unchanged .nwb params reuse __asset_cache/<sha1>.
    ASSERT_TRUE(buildAndGatherWithRepoRoot(root / "built_second", root / "out_second"));

    const auto cachedSampler = AssetsGraphicsFixture::LoadCookedAsset<NWB::Impl::SamplerAssetCodec>(testArena, root / "out_second", virtualPath, 1u);
    ASSERT_TRUE(cachedSampler);
    EXPECT_FLOAT_EQ(static_cast<const NWB::Impl::Sampler*>(cachedSampler->get())->description().mipBias, 0.0f);

    // Corrupt the cached payload section hash; build must reject the corrupted cache entry and repair it.
    auto corruptedCacheBytes = initialCacheBytes;
    auto corruptedHeader = *initialHeader;
    corruptedHeader.payloadSectionHashFnv64 ^= 1u;
    ASSERT_TRUE(AssetsGraphicsFixture::OverwritePOD(corruptedCacheBytes, 0u, corruptedHeader));
    ASSERT_TRUE(WriteBinaryFile(expectedCacheFilePath, corruptedCacheBytes));
    ASSERT_TRUE(buildAndGatherWithRepoRoot(root / "built_repaired", root / "out_repaired"));
    NWB::Core::Assets::AssetBytes repairedCacheBytes(testArena.arena);
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

    const auto updatedSampler = AssetsGraphicsFixture::LoadCookedAsset<NWB::Impl::SamplerAssetCodec>(testArena, root / "out_third", virtualPath, 1u);
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

