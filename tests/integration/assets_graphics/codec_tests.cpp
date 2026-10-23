// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"

#include <core/assets/input_list.h>
#include <core/assets/volume/built_asset.h>
#include <core/assets/volume/cooked_object_cache.h>

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

static constexpr AStringView s_Cache = "cache";
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
    const bool prepared = AssetsGraphicsFixture::PrepareCleanDirectory(root);
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
        const bool created = static_cast<bool>(filesystemResult);
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
            const bool pushed = filesystem->writeFileDeferred(virtualPath, payload);
            EXPECT_TRUE(pushed);
            if(pushed){
                payload[0] = 99u;

                const bool flushed = filesystem->flush();
                EXPECT_TRUE(flushed);
                if(flushed){
                    NWB::Core::Assets::AssetBytes readback = AssetsGraphicsFixture::MakeAssetBytes(testArena);
                    const bool loaded = filesystem->readFile(virtualPath, readback);
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

using AssetObjectCachePathVector = Vector<Path, NWB::Core::Alloc::GlobalArena>;

[[nodiscard]] static Expected<AssetObjectCachePathVector> FindAssetObjectCachePaths(TestArena& testArena, const Path& cacheDirectory){
    AssetObjectCachePathVector paths(testArena.arena);

    const auto cacheEntries = RecursiveDirectoryIterator<Path::Arena>::Create(cacheDirectory);
    EXPECT_TRUE(cacheEntries);
    if(!cacheEntries)
        return MakeUnexpected(Failure{});

    for(const auto& entry : *cacheEntries){
        const auto isRegularFile = entry.isRegularFile();
        EXPECT_TRUE(isRegularFile);
        if(!isRegularFile)
            return MakeUnexpected(Failure{});
        if(!*isRegularFile)
            continue;

        const auto extension = PathToString(testArena.arena, entry.path().extension());
        if(extension != ".nwbobj")
            continue;

        paths.push_back(entry.path());
    }

    return paths;
}

[[nodiscard]] static Expected<Path> FindSingleAssetObjectCachePath(TestArena& testArena, const Path& cacheDirectory){
    auto objectPaths = FindAssetObjectCachePaths(testArena, cacheDirectory);
    if(!objectPaths)
        return MakeUnexpected(Failure{});

    EXPECT_EQ(objectPaths->size(), 1u);
    if(objectPaths->size() != 1u)
        return MakeUnexpected(Failure{});

    return Move((*objectPaths)[0u]);
}

[[nodiscard]] static Expected<Path> FindNewAssetObjectCachePath(TestArena& testArena, const Path& cacheDirectory, const Path& oldPath){
    auto objectPaths = FindAssetObjectCachePaths(testArena, cacheDirectory);
    if(!objectPaths)
        return MakeUnexpected(Failure{});

    EXPECT_EQ(objectPaths->size(), s_ExpectedDualCount);
    if(objectPaths->size() != s_ExpectedDualCount)
        return MakeUnexpected(Failure{});

    usize newPathCount = 0u;
    usize newPathIndex = 0u;
    for(usize index = 0u; index < objectPaths->size(); ++index){
        if((*objectPaths)[index] == oldPath)
            continue;

        newPathIndex = index;
        ++newPathCount;
    }

    EXPECT_EQ(newPathCount, 1u);
    if(newPathCount != 1u)
        return MakeUnexpected(Failure{});
    return Move((*objectPaths)[newPathIndex]);
}

[[nodiscard]] static Expected<NWB::Core::Assets::AssetBytes> ReadAssetObjectCacheBytes(TestArena& testArena, const Path& objectPath){
    auto bytes = AssetsGraphicsFixture::MakeAssetBytes(testArena);
    const auto read = ReadBinaryFile(objectPath, bytes);
    EXPECT_TRUE(read);
    EXPECT_FALSE(bytes.empty());
    if(!read || bytes.empty())
        return MakeUnexpected(Failure{});
    return bytes;
}

static bool AssetBytesEqual(const NWB::Core::Assets::AssetBytes& lhs, const NWB::Core::Assets::AssetBytes& rhs)noexcept{
    if(lhs.size() != rhs.size())
        return false;
    for(usize i = 0u; i < lhs.size(); ++i){
        if(lhs[i] != rhs[i])
            return false;
    }
    return true;
}

TEST(AssetsGraphics, AssetBuildWritesRegistryObjectCache){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    auto cookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, "asset_volume_registry_object_cache");
    ASSERT_TRUE(cookCase);
    Path& root = cookCase->root;
    Path& outputDirectory = cookCase->outputDirectory;


    const Path assetRoot = root / s_AssetsDirectory;
    const Path metaPath = assetRoot / "meshes" / "minimal_mesh.nwb";
    EXPECT_TRUE(AssetsGraphicsFixture::WriteTextFile(metaPath, AssetsGraphicsFixture::s_MinimalMeshMeta));
    EXPECT_TRUE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }, s_ExpectedDualCount));

    const auto objectPath = FindSingleAssetObjectCachePath(testArena, root / s_Cache);
    ASSERT_TRUE(objectPath);

    const auto firstObjectBytes = ReadAssetObjectCacheBytes(testArena, *objectPath);
    ASSERT_TRUE(firstObjectBytes);

    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    auto loadedAssetLoadResult = AssetsGraphicsFixture::LoadCookedMinimalMesh(testArena, outputDirectory);
    ASSERT_TRUE(loadedAssetLoadResult);
    loadedAsset = Move(*loadedAssetLoadResult);

    EXPECT_TRUE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }, s_ExpectedDualCount));
    const auto unchangedObjectPath = FindSingleAssetObjectCachePath(testArena, root / s_Cache);
    ASSERT_TRUE(unchangedObjectPath);
    EXPECT_EQ(*unchangedObjectPath, *objectPath);

    const auto unchangedObjectBytes = ReadAssetObjectCacheBytes(testArena, *unchangedObjectPath);
    ASSERT_TRUE(unchangedObjectBytes);
    EXPECT_TRUE(AssetBytesEqual(*firstObjectBytes, *unchangedObjectBytes));

    // The compact path omits this type-hash lane, so the full header must still reject it as a cache hit.
    auto mismatchedObjectBytes = *firstObjectBytes;
    constexpr usize s_ObjectAssetTypeOffset = sizeof(u32) + sizeof(u16) * 2u;
    usize headerCursor = s_ObjectAssetTypeOffset;
    const auto cachedAssetType = ReadPOD<NameHash>(mismatchedObjectBytes, headerCursor);
    ASSERT_TRUE(cachedAssetType);
    NameHash differentAssetType = *cachedAssetType;
    differentAssetType.qwords[NameDetail::s_HashLaneCount - 1u] ^= 1u;
    ASSERT_TRUE(AssetsGraphicsFixture::OverwritePOD(mismatchedObjectBytes, s_ObjectAssetTypeOffset, differentAssetType));
    ASSERT_TRUE(WriteBinaryFile(*objectPath, mismatchedObjectBytes));
    ASSERT_TRUE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }, s_ExpectedDualCount));
    const auto repairedObjectBytes = ReadAssetObjectCacheBytes(testArena, *objectPath);
    ASSERT_TRUE(repairedObjectBytes);
    EXPECT_TRUE(AssetBytesEqual(*firstObjectBytes, *repairedObjectBytes));

    EXPECT_TRUE(AssetsGraphicsFixture::WriteTextFile(metaPath, AssetsGraphicsFixture::s_DefaultColorMeshMeta));
    EXPECT_TRUE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }, s_ExpectedDualCount));
    const auto changedObjectPath = FindNewAssetObjectCachePath(testArena, root / s_Cache, *objectPath);
    ASSERT_TRUE(changedObjectPath);
    EXPECT_NE(*changedObjectPath, *objectPath);

    const auto changedObjectBytes = ReadAssetObjectCacheBytes(testArena, *changedObjectPath);
    ASSERT_TRUE(changedObjectBytes);
    EXPECT_FALSE(AssetBytesEqual(*firstObjectBytes, *changedObjectBytes));

    loadedAsset.reset();
    auto loadedAssetLoadResult2 = AssetsGraphicsFixture::LoadCookedMinimalMesh(testArena, outputDirectory);
    ASSERT_TRUE(loadedAssetLoadResult2);
    loadedAsset = Move(*loadedAssetLoadResult2);

    EXPECT_TRUE(RemoveAllIfExists(root));
    EXPECT_EQ(logger.errorCount(), 0u);
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
    ASSERT_TRUE(FindSingleAssetObjectCachePath(testArena, root / s_Cache));

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

TEST(AssetsGraphics, BuiltArtifactsRejectMismatchedObjectCookKeys){
    namespace Pack = NWB::Core::Assets::AssetsVolumeCookDetail;
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger, NWB::Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    auto rootResult = AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "built_object_cook_key_mismatch");
    ASSERT_TRUE(rootResult);
    const Path& root = *rootResult;
    const Path assetRoot = root / s_AssetsDirectory;
    const Name virtualPath(s_FirstSamplerPath);
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(assetRoot / "samplers" / "first.nwb", s_SamplerMetadata));
    ASSERT_TRUE(AssetsGraphicsFixture::BuildPreparedGraphicsAssetRoots(testArena, root, root / "built", { assetRoot }, s_ExpectedDualCount));
    const auto objectPath = FindSingleAssetObjectCachePath(testArena, root / s_Cache);
    ASSERT_TRUE(objectPath);
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_CodecScratchArena);
    Vector<u8, NWB::Core::Alloc::ScratchArena> objectBytes(scratchArena);
    const auto payload = Pack::ReadCookedObjectPayload(*objectPath, virtualPath, objectBytes);
    ASSERT_TRUE(payload);
    auto mismatchedIdentity = payload->identity;
    mismatchedIdentity.cookKeyHash = mismatchedIdentity.cookKeyHash == 1u ? 2u : 1u;
    Pack::AssetVolumePackManifest manifest(testArena.arena);
    ASSERT_TRUE(Pack::ReserveAssetVolumePackManifest(manifest, 1u));
    ASSERT_TRUE(Pack::AppendObjectFilePayloadToManifest(manifest, virtualPath, *objectPath, mismatchedIdentity));
    EXPECT_FALSE(NWB::Core::Assets::BuiltAssetDetail::WriteBuiltAssets(root / "rejected", manifest, scratchArena));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("object cache identity mismatch")));
    EXPECT_EQ(logger.errorCount(), 1u);
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
    const Path assetRoot = root / s_AssetsDirectory;
    const Path publishedDirectory = root / "published";
    const Name firstPath(s_FirstSamplerPath);
    const Name secondPath(s_SecondSamplerPath);
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(assetRoot / "samplers" / "first.nwb", s_SamplerMetadata));
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(assetRoot / "samplers" / "second.nwb", s_SamplerMetadata));
    ASSERT_TRUE(AssetsGraphicsFixture::BuildPreparedGraphicsAssetRoots(testArena, root, root / "seed_built", { assetRoot }, s_ExpectedDualCount));
    const auto objectPath = FindSingleAssetObjectCachePath(testArena, root / s_Cache);
    ASSERT_TRUE(objectPath);
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_CodecScratchArena);
    Vector<u8, NWB::Core::Alloc::ScratchArena> objectBytes(scratchArena);
    const auto payload = Pack::ReadCookedObjectPayload(*objectPath, firstPath, objectBytes);
    ASSERT_TRUE(payload);

    Pack::AssetVolumePackManifest initial(testArena.arena);
    ASSERT_TRUE(Pack::ReserveAssetVolumePackManifest(initial, s_ExpectedDualCount));
    ASSERT_TRUE(Pack::AppendObjectFilePayloadToManifest(initial, firstPath, *objectPath, payload->identity));
    ASSERT_TRUE(Pack::AppendObjectFilePayloadToManifest(initial, secondPath, *objectPath, payload->identity));
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
    ASSERT_TRUE(Pack::AppendObjectFilePayloadToManifest(reordered, secondPath, *objectPath, payload->identity));
    ASSERT_TRUE(Pack::AppendObjectFilePayloadToManifest(reordered, firstPath, *objectPath, payload->identity));
    auto mismatchedIdentity = payload->identity;
    mismatchedIdentity.cookKeyHash = mismatchedIdentity.cookKeyHash == 1u ? 2u : 1u;
    // Reorder both shared-payload entries before failing, so publication also has to create a new artifact slot.
    ASSERT_TRUE(Pack::AppendObjectFilePayloadToManifest(reordered, Name("project/samplers/failing"), *objectPath, mismatchedIdentity));
    EXPECT_FALSE(NWB::Core::Assets::BuiltAssetDetail::WriteBuiltAssets(publishedDirectory, reordered, scratchArena));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("object cache identity mismatch")));
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

