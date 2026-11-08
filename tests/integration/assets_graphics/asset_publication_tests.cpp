// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"

#include <impl/assets_graphics/components.adhd.h>

#include <core/assets/input_list.h>
#include <core/assets/cook_metadata_internal.h>
#include <core/assets/volume/built_asset.h>
#include <core/assets/volume/asset_file_publisher.h>

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


namespace __hidden_assets_graphics_asset_publication_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_AssetsDirectory = AssetsGraphicsFixture::s_AssetsDirectory;
constexpr AStringView s_TestsConfiguration = AssetsGraphicsFixture::s_TestsConfiguration;
constexpr AStringView s_FirstSamplerPath = AssetsGraphicsFixture::s_FirstSamplerPath;
constexpr AStringView s_SecondSamplerPath = AssetsGraphicsFixture::s_SecondSamplerPath;
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
        results[index] = NWB::Core::Assets::AssetFilePublisher::Write(path, bytes, false, existing);
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

            const Name virtualPath = NWB::Tests::AssetsGraphicsFixture::s_ProjectTestsScratchPayload;
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
    constexpr u64 s_SharedCookKeyHash = AssetsGraphicsFixture::s_SharedCookKeyHash;

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
    ASSERT_TRUE(Pack::AppendPayloadBytesToManifest(Name(NWB::Tests::AssetsGraphicsFixture::s_AssetInputNameProjectSamplersFailing), samplerPayload, reordered, s_SharedCookKeyHash));
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

