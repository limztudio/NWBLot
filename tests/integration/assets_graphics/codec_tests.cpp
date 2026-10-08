// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_codec{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_CACHE = "cache";


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
        UniquePtr<NWB::Core::Filesystem::IFilesystem> filesystem = NWB::Core::Filesystem::CreateFilesystem(testArena.arena, mountDesc);
        const bool created = static_cast<bool>(filesystem);
        EXPECT_TRUE(created);
        if(created){
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

[[nodiscard]] static Expected<Path> FindNewAssetObjectCachePath(
    TestArena& testArena,
    const Path& cacheDirectory,
    const Path& oldPath
){
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


    const Path assetRoot = root / "assets";
    const Path metaPath = assetRoot / "meshes" / "minimal_mesh.nwb";
    EXPECT_TRUE(AssetsGraphicsFixture::WriteTextFile(metaPath, AssetsGraphicsFixture::s_MinimalMeshMeta));
    EXPECT_TRUE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }, s_ExpectedDualCount));

    const auto objectPath = FindSingleAssetObjectCachePath(testArena, root / s_CACHE);
    ASSERT_TRUE(objectPath);

    const auto firstObjectBytes = ReadAssetObjectCacheBytes(testArena, *objectPath);
    ASSERT_TRUE(firstObjectBytes);

    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    auto loadedAssetLoadResult = AssetsGraphicsFixture::LoadCookedMinimalMesh(testArena, outputDirectory);
    ASSERT_TRUE(loadedAssetLoadResult);
    loadedAsset = Move(*loadedAssetLoadResult);

    EXPECT_TRUE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }, s_ExpectedDualCount));
    const auto unchangedObjectPath = FindSingleAssetObjectCachePath(testArena, root / s_CACHE);
    ASSERT_TRUE(unchangedObjectPath);
    EXPECT_EQ(*unchangedObjectPath, *objectPath);

    const auto unchangedObjectBytes = ReadAssetObjectCacheBytes(testArena, *unchangedObjectPath);
    ASSERT_TRUE(unchangedObjectBytes);
    EXPECT_TRUE(AssetBytesEqual(*firstObjectBytes, *unchangedObjectBytes));

    EXPECT_TRUE(AssetsGraphicsFixture::WriteTextFile(metaPath, AssetsGraphicsFixture::s_DefaultColorMeshMeta));
    EXPECT_TRUE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }, s_ExpectedDualCount));
    const auto changedObjectPath = FindNewAssetObjectCachePath(testArena, root / s_CACHE, *objectPath);
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

