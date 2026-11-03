// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"

#include <impl/assets_graphics/components.adhd.h>

#include <core/assets/input_list.h>
#include <core/assets/cook_metadata_internal.h>
#include <core/assets/volume/built_asset.h>

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


using TestArena = AssetsGraphicsFixture::TestArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AssetsGraphics, ConcurrentImmutablePublicationPreservesTheWinner){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger, NWB::Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    const Path root = AssetsGraphicsFixture::AssetsGraphicsTestCaseRoot(testArena, "immutable_publication_race");
    ASSERT_TRUE(AssetsGraphicsFixture::PrepareCleanDirectory(root));
    const Path destination = root / "shared.nwba";
    Atomic<u32> ready{0u};
    Atomic<b8> start{false};
    b8 results[2]{};
    const auto publish = [&](const usize index){
        NWB::Core::Alloc::ScratchArena scratch(AssetsGraphicsFixture::s_CodecScratchArena);
        ::Path<NWB::Core::Alloc::ScratchArena> path(scratch, destination);
        Vector<u8, NWB::Core::Alloc::ScratchArena> bytes(scratch);
        Vector<u8, NWB::Core::Alloc::ScratchArena> existing(scratch);
        bytes.resize(1024u * 1024u, static_cast<u8>(index + 1u));
        ready.fetch_add(1u);
        while(!start.load())
            YieldThread();
        results[index] = NWB::Core::Assets::BuiltAssetDetail::WriteBinaryFileIfChanged(path, bytes, false, existing);
    };
    Thread first(publish, 0u);
    Thread second(publish, 1u);
    while(ready.load() != 2u)
        YieldThread();
    start.store(true);
    first.join();
    second.join();
    ASSERT_NE(results[0], results[1]);
    NWB::Core::Assets::AssetBytes stored(testArena.arena);
    ASSERT_TRUE(ReadBinaryFile(destination, stored));
    ASSERT_EQ(stored.size(), 1024u * 1024u);
    const u8 expected = results[0] ? 1u : 2u;
    for(const u8 byte : stored)
        ASSERT_EQ(byte, expected);
    EXPECT_TRUE(RemoveAllIfExists(root));
}

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
            payload.push_back(2u);
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
    ASSERT_TRUE(AssetsGraphicsFixture::BuildPreparedGraphicsAssetRoots(testArena, root, builtDirectory, { assetRoot }, 2u));

    NWB::Pipeline::AssetGatherer::AssetGatherOptions options(testArena.arena);
    options.inputs.emplace_back(PathToString(testArena.arena, builtDirectory));
    options.outputDirectory = PathToString(testArena.arena, outputDirectory);
    options.configuration = s_TestsConfiguration.data();
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_GatherScratchArena);
    ASSERT_TRUE(NWB::Pipeline::AssetGatherer::GatherAssets(options, scratchArena));
    for(const Name& virtualPath : { Name(s_FirstSamplerPath), Name(s_SecondSamplerPath) }){
        const auto asset = AssetsGraphicsFixture::LoadCookedAsset<NWB::Impl::SamplerAssetCodec>(outputDirectory, virtualPath, 2u, testArena);
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
    ASSERT_TRUE(Pack::ReserveAssetVolumePackManifest(2u, initial));
    ASSERT_TRUE(Pack::AppendPayloadBytesToManifest(firstPath, samplerPayload, initial, s_SharedCookKeyHash));
    ASSERT_TRUE(Pack::AppendPayloadBytesToManifest(secondPath, samplerPayload, initial, s_SharedCookKeyHash));
    ASSERT_TRUE(NWB::Core::Assets::BuiltAssetDetail::WriteBuiltAssets(publishedDirectory, initial, scratchArena));
    NWB::Core::Assets::AssetBytes originalList(testArena.arena);
    ASSERT_TRUE(ReadBinaryFile(publishedDirectory / NWB::Core::Assets::BuiltAssetDetail::s_ManifestFilename, originalList));
    NWB::Core::Assets::AssetVector<NWB::Core::Assets::AssetString> originalArtifactPaths(testArena.arena);
    ASSERT_TRUE(NWB::Core::Assets::ReadAssetInputList(publishedDirectory / NWB::Core::Assets::BuiltAssetDetail::s_ManifestFilename, scratchArena, originalArtifactPaths, true));
    ASSERT_EQ(originalArtifactPaths.size(), 2u);
    NWB::Core::Assets::AssetVector<NWB::Core::Assets::AssetBytes> originalArtifactBytes(testArena.arena);
    originalArtifactBytes.reserve(originalArtifactPaths.size());
    for(const auto& artifactPath : originalArtifactPaths){
        originalArtifactBytes.emplace_back(testArena.arena);
        ASSERT_TRUE(ReadBinaryFile(Path(testArena.arena, artifactPath), originalArtifactBytes.back()));
    }

    Pack::AssetVolumePackManifest reordered(testArena.arena);
    ASSERT_TRUE(Pack::ReserveAssetVolumePackManifest(2u + 1u, reordered));
    ASSERT_TRUE(Pack::AppendPayloadBytesToManifest(secondPath, samplerPayload, reordered, s_SharedCookKeyHash));
    ASSERT_TRUE(Pack::AppendPayloadBytesToManifest(firstPath, samplerPayload, reordered, s_SharedCookKeyHash));
    // Reorder both shared-payload entries before failing, so publication also has to create a new artifact slot.
    ASSERT_TRUE(Pack::AppendPayloadBytesToManifest(Name("project/samplers/failing"), samplerPayload, reordered, s_SharedCookKeyHash));
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
    ASSERT_TRUE(NWB::Pipeline::AssetGatherer::GatherAssets(options, scratchArena));
    for(const Name& virtualPath : { firstPath, secondPath }){
        const auto asset = AssetsGraphicsFixture::LoadCookedAsset<NWB::Impl::SamplerAssetCodec>(cookCase->outputDirectory, virtualPath, 2u, testArena);
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

    const auto loadedAsset = AssetsGraphicsFixture::LoadCookedAsset<NWB::Impl::ComponentAssetCodec>(outputDirectory, virtualPath, 1u, testArena);
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
        const auto executablePath = GetExecutablePath(scratchArena);
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

TEST(AssetsGraphics, AssetRegistrySupportsReentrantDeserializeAndDrainsBeforeUnregister){
    class ProbeAsset final : public NWB::Core::Assets::IAsset{
    public:
        ProbeAsset(const Name& assetType, const Name& virtualPath)noexcept
            : IAsset(assetType, virtualPath)
        {}
    };

    class LeafProbeCodec final : public NWB::Core::Assets::IAssetCodec{
    public:
        explicit LeafProbeCodec(const Name& assetType)noexcept
            : IAssetCodec(assetType)
        {}


    public:
        virtual Expected<UniquePtr<NWB::Core::Assets::IAsset>> deserialize(const Name& virtualPath, const Span<const u8>, NWB::Core::Assets::AssetArena&)const override{
            return UniquePtr<NWB::Core::Assets::IAsset>(MakeUnique<ProbeAsset>(assetType(), virtualPath));
        }

#if defined(NWB_COOK)
        virtual b8 serialize(const NWB::Core::Assets::IAsset&, NWB::Core::Assets::AssetBytes&)const override{
            return true;
        }
#endif
    };

    class ReentrantBlockingCodec final : public NWB::Core::Assets::IAssetCodec{
    public:
        ReentrantBlockingCodec(const Name& assetType, const NWB::Core::Assets::AssetRegistry& registry, const Name& leafType, Atomic<b8>& entered, Atomic<b8>& release, Atomic<b8>& destroyedWhileActive)noexcept
            : IAssetCodec(assetType)
            , m_registry(registry)
            , m_leafType(leafType)
            , m_entered(entered)
            , m_release(release)
            , m_destroyedWhileActive(destroyedWhileActive)
        {}

        virtual ~ReentrantBlockingCodec()noexcept override{
            if(m_inFlight.load())
                m_destroyedWhileActive.store(true);
        }


    public:
        virtual Expected<UniquePtr<NWB::Core::Assets::IAsset>> deserialize(const Name& virtualPath, const Span<const u8> binary, NWB::Core::Assets::AssetArena&)const override{
            m_inFlight.store(true);
            auto nested = m_registry.deserializeAsset(m_leafType, Name("project/probe/leaf"), binary);
            if(!nested){
                m_inFlight.store(false);
                return MakeUnexpected(Failure{});
            }
            m_entered.store(true);
            while(!m_release.load())
                YieldThread();
            m_inFlight.store(false);
            return UniquePtr<NWB::Core::Assets::IAsset>(MakeUnique<ProbeAsset>(assetType(), virtualPath));
        }

#if defined(NWB_COOK)
        virtual b8 serialize(const NWB::Core::Assets::IAsset&, NWB::Core::Assets::AssetBytes&)const override{
            return true;
        }
#endif


    private:
        const NWB::Core::Assets::AssetRegistry& m_registry;
        Name m_leafType;
        Atomic<b8>& m_entered;
        Atomic<b8>& m_release;
        Atomic<b8>& m_destroyedWhileActive;
        mutable Atomic<b8> m_inFlight{false};
    };

    TestArena testArena;
    NWB::Core::Assets::AssetRegistry registry(testArena.arena);
    const Name parentType("probe_parent_asset");
    const Name leafType("probe_leaf_asset");
    Atomic<b8> entered{false};
    Atomic<b8> release{false};
    Atomic<b8> destroyedWhileActive{false};
    Atomic<b8> unregisterFinished{false};

    ASSERT_TRUE(registry.registerCodec(MakeUnique<LeafProbeCodec>(leafType)));
    ASSERT_TRUE(registry.registerCodec(MakeUnique<ReentrantBlockingCodec>(parentType, registry, leafType, entered, release, destroyedWhileActive)));

    Expected<UniquePtr<NWB::Core::Assets::IAsset>> parentResult = MakeUnexpected(Failure{});
    Thread deserializeThread([&](){
        parentResult = registry.deserializeAsset(parentType, Name("project/probe/parent"), Span<const u8>{});
    });

    while(!entered.load())
        YieldThread();

    b8 unregistered = false;
    Thread unregisterThread([&](){
        unregistered = registry.unregisterCodec(parentType);
        unregisterFinished.store(true);
    });

    for(u32 spin = 0u; spin < 64u; ++spin)
        YieldThread();
    EXPECT_FALSE(unregisterFinished.load());

    release.store(true);
    deserializeThread.join();
    unregisterThread.join();

    ASSERT_TRUE(parentResult);
    ASSERT_NE(parentResult->get(), nullptr);
    EXPECT_EQ((*parentResult)->assetType(), parentType);
    EXPECT_TRUE(unregistered);
    EXPECT_TRUE(unregisterFinished.load());
    EXPECT_FALSE(destroyedWhileActive.load());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

