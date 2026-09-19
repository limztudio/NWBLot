// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"


#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_codec{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CapturingLogger = AssetsGraphicsFixture::CapturingLogger;
using Path = AssetsGraphicsFixture::Path;
using TestArena = AssetsGraphicsFixture::TestArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////






TEST(AssetsGraphics, FilesystemAcceptsScratchBytes){
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
            payload.push_back(2u);
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
                        EXPECT_EQ(readback[1], 2u);
                        EXPECT_EQ(readback[2], 3u);
                        EXPECT_EQ(readback[3], 4u);
                    }

                    ErrorCode sizeError;
                    const Path segmentPath = root / "volume" / MakeVolumeSegmentFileName(mountDesc.volumeName.view(), 0u).c_str();
                    const u64 segmentFileSize = FileSize(segmentPath, sizeError);
                    EXPECT_FALSE(sizeError);
                    EXPECT_EQ(segmentFileSize, mountDesc.metadataSize + payload.size());
                    EXPECT_LT(segmentFileSize, mountDesc.segmentSize);

                    mountDesc.createIfMissing = false;
                    mountDesc.usage = NWB::Core::Filesystem::VolumeUsage::RuntimeReadOnly;
                    UniquePtr<NWB::Core::Filesystem::IFilesystem> reloadedFilesystem = NWB::Core::Filesystem::CreateFilesystem(testArena.arena, mountDesc);
                    const bool reloaded = static_cast<bool>(reloadedFilesystem);
                    EXPECT_TRUE(reloaded);
                    if(reloaded){
                        NWB::Core::Assets::AssetBytes reloadedReadback = AssetsGraphicsFixture::MakeAssetBytes(testArena);
                        const bool reloadedData = reloadedFilesystem->readFile(virtualPath, reloadedReadback);
                        EXPECT_TRUE(reloadedData);
                        if(reloadedData){
                            ASSERT_EQ(reloadedReadback.size(), readback.size());
                            for(usize i = 0u; i < readback.size(); ++i)
                                EXPECT_EQ(reloadedReadback[i], readback[i]);
                        }
                    }
                }
            }
        }
    }

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
}

using AssetObjectCachePathVector = Vector<Path, NWB::Core::Alloc::GlobalArena>;

static bool FindAssetObjectCachePaths(TestArena& testArena, const Path& cacheDirectory, AssetObjectCachePathVector& outPaths){
    outPaths.clear();

    ErrorCode errorCode;
    RecursiveDirectoryIterator<Path::Arena> cacheEntries(cacheDirectory, errorCode);
    EXPECT_FALSE(errorCode);
    if(errorCode)
        return false;

    for(const auto& entry : cacheEntries){
        errorCode.clear();
        const bool isRegularFile = entry.is_regular_file(errorCode);
        EXPECT_FALSE(errorCode);
        if(errorCode)
            return false;
        if(!isRegularFile)
            continue;

        const auto extension = PathToString(testArena.arena, entry.path().extension());
        if(extension != ".nwbobj")
            continue;

        outPaths.push_back(entry.path());
    }

    return true;
}

static bool FindSingleAssetObjectCachePath(TestArena& testArena, const Path& cacheDirectory, Path& outPath){
    outPath.clear();

    AssetObjectCachePathVector objectPaths(testArena.arena);
    if(!FindAssetObjectCachePaths(testArena, cacheDirectory, objectPaths))
        return false;

    EXPECT_EQ(objectPaths.size(), 1u);
    if(objectPaths.size() != 1u)
        return false;

    outPath = objectPaths[0u];
    return true;
}

static bool FindNewAssetObjectCachePath(
    TestArena& testArena,
    const Path& cacheDirectory,
    const Path& oldPath,
    Path& outPath
){
    outPath.clear();

    AssetObjectCachePathVector objectPaths(testArena.arena);
    if(!FindAssetObjectCachePaths(testArena, cacheDirectory, objectPaths))
        return false;

    EXPECT_EQ(objectPaths.size(), 2u);
    if(objectPaths.size() != 2u)
        return false;

    usize newPathCount = 0u;
    for(const Path& objectPath : objectPaths){
        if(objectPath == oldPath)
            continue;

        outPath = objectPath;
        ++newPathCount;
    }

    EXPECT_EQ(newPathCount, 1u);
    return newPathCount == 1u;
}

static bool ReadAssetObjectCacheBytes(const Path& objectPath, NWB::Core::Assets::AssetBytes& outBytes){
    ErrorCode errorCode;
    const bool read = ReadBinaryFile(objectPath, outBytes, errorCode);
    EXPECT_TRUE(read);
    EXPECT_FALSE(errorCode);
    EXPECT_FALSE(outBytes.empty());
    return read && !errorCode && !outBytes.empty();
}

static bool AssetBytesEqual(const NWB::Core::Assets::AssetBytes& lhs, const NWB::Core::Assets::AssetBytes& rhs){
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
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
    EXPECT_TRUE(AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(
        testArena,
        "asset_volume_registry_object_cache",
        root,
        outputDirectory
    ));

    const Path assetRoot = root / "assets";
    const Path metaPath = assetRoot / "meshes" / "minimal_mesh.nwb";
    EXPECT_TRUE(AssetsGraphicsFixture::WriteTextFile(metaPath, AssetsGraphicsFixture::s_MinimalMeshMeta));
    EXPECT_TRUE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }, 2u));

    Path objectPath(testArena.arena);
    ASSERT_TRUE(FindSingleAssetObjectCachePath(testArena, root / "cache", objectPath));

    NWB::Core::Assets::AssetBytes firstObjectBytes = AssetsGraphicsFixture::MakeAssetBytes(testArena);
    ASSERT_TRUE(ReadAssetObjectCacheBytes(objectPath, firstObjectBytes));

    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    EXPECT_TRUE(AssetsGraphicsFixture::LoadCookedMinimalMesh(testArena, outputDirectory, loadedAsset));

    EXPECT_TRUE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }, 2u));
    Path unchangedObjectPath(testArena.arena);
    ASSERT_TRUE(FindSingleAssetObjectCachePath(testArena, root / "cache", unchangedObjectPath));
    EXPECT_EQ(unchangedObjectPath, objectPath);

    NWB::Core::Assets::AssetBytes unchangedObjectBytes = AssetsGraphicsFixture::MakeAssetBytes(testArena);
    ASSERT_TRUE(ReadAssetObjectCacheBytes(unchangedObjectPath, unchangedObjectBytes));
    EXPECT_TRUE(AssetBytesEqual(firstObjectBytes, unchangedObjectBytes));

    EXPECT_TRUE(AssetsGraphicsFixture::WriteTextFile(metaPath, AssetsGraphicsFixture::s_DefaultColorMeshMeta));
    EXPECT_TRUE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }, 2u));
    Path changedObjectPath(testArena.arena);
    ASSERT_TRUE(FindNewAssetObjectCachePath(testArena, root / "cache", objectPath, changedObjectPath));
    EXPECT_NE(changedObjectPath, objectPath);

    NWB::Core::Assets::AssetBytes changedObjectBytes = AssetsGraphicsFixture::MakeAssetBytes(testArena);
    ASSERT_TRUE(ReadAssetObjectCacheBytes(changedObjectPath, changedObjectBytes));
    EXPECT_FALSE(AssetBytesEqual(firstObjectBytes, changedObjectBytes));

    loadedAsset.reset();
    EXPECT_TRUE(AssetsGraphicsFixture::LoadCookedMinimalMesh(testArena, outputDirectory, loadedAsset));

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
    EXPECT_EQ(logger.errorCount(), 0u);
}

static NWB::Impl::MeshletBounds MakeTestMeshletBounds(){
    return NWB::Impl::MeshletBounds{
        Float4U(0.0f, 0.0f, 0.0f, 1.0f),
        NWB::Impl::PackMeshletCone(VectorSet(0.0f, 0.0f, 1.0f, 0.0f), 1.0f),
        0u,
    };
}

#if defined(NWB_FINAL)


#endif

static NWB::Impl::Mesh BuildMinimalMesh(TestArena& testArena){
    NWB::Impl::Mesh mesh(testArena.arena, Name("tests/meshes/minimal_mesh"));

    auto positions = AssetsGraphicsFixture::MakeAssetVector<Float3U>(testArena);
    positions.push_back(Float3U(-0.5f, -0.5f, 0.f));
    positions.push_back(Float3U(0.5f, -0.5f, 0.f));
    positions.push_back(Float3U(0.f, 0.5f, 0.f));

    auto normals = AssetsGraphicsFixture::MakeAssetVector<Half4U>(testArena);
    normals.assign(positions.size(), MakeHalf4U(0.0f, 0.0f, 1.0f, 0.0f));

    auto colors = AssetsGraphicsFixture::MakeAssetVector<Half4U>(testArena);
    colors.push_back(MakeHalf4U(1.0f, 0.0f, 0.0f, 1.0f));
    colors.push_back(MakeHalf4U(0.0f, 1.0f, 0.0f, 1.0f));
    colors.push_back(MakeHalf4U(0.0f, 0.0f, 1.0f, 1.0f));

    auto tangents = AssetsGraphicsFixture::MakeAssetVector<Half4U>(testArena);
    tangents.push_back(MakeHalf4U(1.0f, 0.0f, 0.0f, 1.0f));

    auto uv0 = AssetsGraphicsFixture::MakeAssetVector<Float2U>(testArena);
    uv0.push_back(Float2U(0.0f, 0.0f));
    uv0.push_back(Float2U(1.0f, 0.0f));
    uv0.push_back(Float2U(0.5f, 1.0f));

    auto meshlets = AssetsGraphicsFixture::MakeAssetVector<NWB::Impl::MeshletDesc>(testArena);
    meshlets.push_back(NWB::Impl::MeshletDesc{ 0u, 0u, 0u, 0u, NWB::Impl::PackMeshletCounts(3u, 1u, 3u, 3u) });
    auto meshletBounds = AssetsGraphicsFixture::MakeAssetVector<NWB::Impl::MeshletBounds>(testArena);
    meshletBounds.push_back(MakeTestMeshletBounds());

    auto meshletPositionStreamRefs = AssetsGraphicsFixture::MakeAssetVector<NWB::Impl::MeshletPositionStreamRef>(testArena);
    auto meshletAttributeStreamRefs = AssetsGraphicsFixture::MakeAssetVector<NWB::Impl::MeshletAttributeStreamRef>(testArena);
    auto meshletLocalVertexRefs = AssetsGraphicsFixture::MakeAssetVector<NWB::Impl::MeshletLocalVertexRef>(testArena);
    for(usize vertexIndex = 0u; vertexIndex < positions.size(); ++vertexIndex){
        meshletPositionStreamRefs.push_back(NWB::Impl::MeshletPositionStreamRef{
            static_cast<u32>(vertexIndex),
            NWB::Impl::s_MeshMissingStreamIndex,
        });
        meshletAttributeStreamRefs.push_back(NWB::Impl::MeshletAttributeStreamRef{
            static_cast<u32>(vertexIndex),
            0u,
            static_cast<u32>(vertexIndex),
            static_cast<u32>(vertexIndex),
        });
        meshletLocalVertexRefs.push_back(NWB::Impl::MeshletLocalVertexRef{
            static_cast<u16>(vertexIndex),
            static_cast<u16>(vertexIndex),
        });
    }

    auto meshletPrimitiveIndices = AssetsGraphicsFixture::MakeAssetVector<u8>(testArena);
    meshletPrimitiveIndices.push_back(0u);
    meshletPrimitiveIndices.push_back(1u);
    meshletPrimitiveIndices.push_back(2u);

    auto meshletPositionRefDeltas = AssetsGraphicsFixture::MakeAssetVector<u8>(testArena);
    auto meshletAttributeRefDeltas = AssetsGraphicsFixture::MakeAssetVector<u8>(testArena);
    const bool meshletRefsEncoded = AssetsGraphicsFixture::EncodeTestMeshletRefs(
        meshlets,
        meshletPositionStreamRefs,
        meshletAttributeStreamRefs,
        meshletPositionRefDeltas,
        meshletAttributeRefDeltas,
        false
    );
    NWB_FATAL_ASSERT(meshletRefsEncoded);

    mesh.setPayload(
        Move(positions),
        Move(normals),
        Move(tangents),
        Move(uv0),
        Move(colors),
        Move(meshlets),
        Move(meshletBounds),
        Move(meshletPositionRefDeltas),
        Move(meshletAttributeRefDeltas),
        Move(meshletLocalVertexRefs),
        Move(meshletPrimitiveIndices)
    );
    return mesh;
}



TEST(AssetsGraphics, MeshCodecRoundTrip){
    TestArena testArena;
    NWB::Impl::Mesh mesh = BuildMinimalMesh(testArena);

    NWB::Impl::MeshAssetCodec codec;
    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    const NWB::Impl::Mesh& loadedMesh = AssetsGraphicsFixture::CheckCodecRoundTrip(testArena, mesh, codec, loadedAsset);
    EXPECT_EQ(loadedMesh.positionStream().size(), 3u);
    EXPECT_EQ(loadedMesh.meshletPrimitiveIndices().size(), 3u);
    EXPECT_EQ(loadedMesh.positionStream()[1].x, 0.5f);
    EXPECT_EQ(LoadHalf4U(loadedMesh.normalStream()[1]).z, 1.f);
    EXPECT_EQ(LoadHalf4U(loadedMesh.colorStream()[1]).y, 1.f);
    EXPECT_EQ(loadedMesh.meshletPrimitiveIndices()[2], 2u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

