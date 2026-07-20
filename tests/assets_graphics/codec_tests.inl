static bool CookAndLoadMinimalAsset(
    TestArena& testArena,
    const AStringView metaText,
    const AStringView caseName,
    Path& outRoot,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset,
    CookSingleMetaFn cookSingleMeta,
    LoadCookedAssetFn loadCookedAsset
){
    Path outputDirectory(testArena.arena);
    const bool cooked = cookSingleMeta(metaText, caseName, testArena, outRoot, outputDirectory);

    EXPECT_TRUE(cooked);
    if(!cooked){
        ErrorCode errorCode;
        EXPECT_TRUE(RemoveAllIfExists(outRoot, errorCode));
        return false;
    }

    if(loadCookedAsset(testArena, outputDirectory, outLoadedAsset))
        return true;

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(outRoot, errorCode));
    return false;
}

static bool CookAndLoadMinimalAssetByKind(
    TestArena& testArena,
    const AStringView metaText,
    const AStringView caseName,
    Path& outRoot,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset,
    const MinimalAssetKind::Enum assetKind
){
    CookSingleMetaFn cookSingleMeta = nullptr;
    LoadCookedAssetFn loadCookedAsset = nullptr;
    switch(assetKind){
    case MinimalAssetKind::Mesh:
        cookSingleMeta = CookSingleMeshMeta;
        loadCookedAsset = LoadCookedMinimalMesh;
        break;
    default:
        ADD_FAILURE();
        return false;
    }

    return CookAndLoadMinimalAsset(
        testArena,
        metaText,
        caseName,
        outRoot,
        outLoadedAsset,
        cookSingleMeta,
        loadCookedAsset
    );
}

template<typename MeshT>
static void CheckMinimalRuntimeMeshletPayload(
    const MeshT& loadedMesh
){
    EXPECT_EQ(loadedMesh.meshlets().size(), 1u);
    EXPECT_EQ(loadedMesh.meshletBounds().size(), 1u);
    EXPECT_EQ(loadedMesh.meshletLocalVertexRefs().size(), 3u);
    EXPECT_EQ(loadedMesh.meshletPrimitiveIndices().size(), 3u);

    const NWB::Impl::MeshletDesc& meshlet = loadedMesh.meshlets()[0u];
    const bool skinRequired = NWB::Core::Mesh::MeshClassUsesSkinning(loadedMesh.meshClass());
    usize expectedPositionRefBytes = 0u;
    usize expectedAttributeRefBytes = 0u;
    EXPECT_EQ(NWB::Impl::MeshletVertexCount(meshlet), 3u);
    EXPECT_EQ(NWB::Impl::MeshletPrimitiveCount(meshlet), 1u);
    EXPECT_EQ(NWB::Impl::MeshletPositionCount(meshlet), 3u);
    EXPECT_EQ(NWB::Impl::MeshletAttributeCount(meshlet), 3u);
    EXPECT_TRUE(NWB::Impl::MeshletEncodedPositionRefByteCount(meshlet, skinRequired, expectedPositionRefBytes));
    EXPECT_TRUE(NWB::Impl::MeshletEncodedAttributeRefByteCount(meshlet, expectedAttributeRefBytes));
    EXPECT_EQ(loadedMesh.meshletPositionRefDeltas().size(), expectedPositionRefBytes);
    EXPECT_EQ(loadedMesh.meshletAttributeRefDeltas().size(), expectedAttributeRefBytes);
    EXPECT_EQ(meshlet.positionBase, 0u);
    const u32 expectedSkinBase = skinRequired ? 0u : NWB::Impl::s_MeshMissingStreamIndex;
    EXPECT_EQ(meshlet.skinBase, expectedSkinBase);
    EXPECT_EQ(meshlet.normalBase, 0u);
    EXPECT_EQ(meshlet.tangentBase, 0u);
    EXPECT_EQ(meshlet.uv0Base, 0u);
    EXPECT_EQ(meshlet.colorBase, 0u);
    EXPECT_EQ(meshlet.encoding, 0u);
    EXPECT_GT(loadedMesh.meshletBounds()[0u].sphere.w, 0.0f);
    EXPECT_TRUE(NWB::Impl::MeshletConeEnabled(loadedMesh.meshletBounds()[0u]));
}

template<typename AssetT, typename CheckLoadedAssetFn>
static void CookAndCheckMinimalTypedAsset(
    const AStringView metaText,
    const AStringView caseName,
    const MinimalAssetKind::Enum assetKind,
    CheckLoadedAssetFn&& checkLoadedAsset
){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    if(!CookAndLoadMinimalAssetByKind(
        testArena,
        metaText,
        caseName,
        root,
        loadedAsset,
        assetKind
    ))
        return;

    const AssetT& loadedTypedAsset = static_cast<const AssetT&>(*loadedAsset);
    checkLoadedAsset(loadedTypedAsset);

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
    EXPECT_EQ(logger.errorCount(), 0u);
}

TEST(AssetsGraphics, VolumeSessionAcceptsScratchBytes){
    TestArena testArena;
    const Path root = AssetsGraphicsTestCaseRoot(testArena, "volume_scratch_bytes");
    const bool prepared = PrepareCleanDirectory(root);
    EXPECT_TRUE(prepared);

    if(prepared){
        NWB::Core::Filesystem::VolumeBuildConfig config;
        config.volumeName = "scratch_test";
        config.segmentSize = 64ull * 1024ull;
        config.metadataSize = 4ull * 1024ull;

        NWB::Core::Filesystem::VolumeSession volumeSession(testArena.arena);
        const bool created = volumeSession.create(root / "volume", config);
        EXPECT_TRUE(created);
        if(created){
            NWB::Core::Alloc::ScratchArena scratchArena(s_CodecScratchArena);
            ::Vector<u8, NWB::Core::Alloc::ScratchArena> payload{ scratchArena };
            payload.reserve(4u);
            payload.push_back(1u);
            payload.push_back(2u);
            payload.push_back(3u);
            payload.push_back(4u);

            const Name virtualPath("project/tests/scratch_payload");
            const bool pushed = volumeSession.pushDataDeferred(virtualPath, payload);
            EXPECT_TRUE(pushed);
            if(pushed){
                payload[0] = 99u;

                const bool flushed = volumeSession.flush();
                EXPECT_TRUE(flushed);
                if(flushed){
                    NWB::Core::Assets::AssetBytes readback = MakeAssetBytes(testArena);
                    const bool loaded = volumeSession.loadData(virtualPath, readback);
                    EXPECT_TRUE(loaded);
                    if(loaded){
                        ASSERT_EQ(readback.size(), 4u);
                        EXPECT_EQ(readback[0], 1u);
                        EXPECT_EQ(readback[1], 2u);
                        EXPECT_EQ(readback[2], 3u);
                        EXPECT_EQ(readback[3], 4u);
                    }

                    ErrorCode sizeError;
                    const Path segmentPath = root / "volume" / MakeVolumeSegmentFileName(config.volumeName.view(), 0u).c_str();
                    const u64 segmentFileSize = FileSize(segmentPath, sizeError);
                    EXPECT_FALSE(sizeError);
                    EXPECT_EQ(segmentFileSize, config.metadataSize + payload.size());
                    EXPECT_LT(segmentFileSize, config.segmentSize);

                    NWB::Core::Filesystem::VolumeSession reloadedSession(testArena.arena);
                    const bool reloaded = reloadedSession.load(config.volumeName.view(), root / "volume");
                    EXPECT_TRUE(reloaded);
                    if(reloaded){
                        NWB::Core::Assets::AssetBytes reloadedReadback = MakeAssetBytes(testArena);
                        const bool reloadedData = reloadedSession.loadData(virtualPath, reloadedReadback);
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

TEST(AssetsGraphics, AssetVolumeCookWritesRegistryObjectCache){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
    EXPECT_TRUE(PrepareAssetsGraphicsCookCase(
        testArena,
        "asset_volume_registry_object_cache",
        root,
        outputDirectory
    ));

    const Path assetRoot = root / "assets";
    const Path metaPath = assetRoot / "meshes" / "minimal_mesh.nwb";
    EXPECT_TRUE(WriteTextFile(metaPath, s_MinimalMeshMeta));
    EXPECT_TRUE(CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }, 2u));

    Path objectPath(testArena.arena);
    ASSERT_TRUE(FindSingleAssetObjectCachePath(testArena, root / "cache", objectPath));

    NWB::Core::Assets::AssetBytes firstObjectBytes = MakeAssetBytes(testArena);
    ASSERT_TRUE(ReadAssetObjectCacheBytes(objectPath, firstObjectBytes));

    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    EXPECT_TRUE(LoadCookedMinimalMesh(testArena, outputDirectory, loadedAsset));

    EXPECT_TRUE(CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }, 2u));
    Path unchangedObjectPath(testArena.arena);
    ASSERT_TRUE(FindSingleAssetObjectCachePath(testArena, root / "cache", unchangedObjectPath));
    EXPECT_EQ(unchangedObjectPath, objectPath);

    NWB::Core::Assets::AssetBytes unchangedObjectBytes = MakeAssetBytes(testArena);
    ASSERT_TRUE(ReadAssetObjectCacheBytes(unchangedObjectPath, unchangedObjectBytes));
    EXPECT_TRUE(AssetBytesEqual(firstObjectBytes, unchangedObjectBytes));

    EXPECT_TRUE(WriteTextFile(metaPath, s_DefaultColorMeshMeta));
    EXPECT_TRUE(CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }, 2u));
    Path changedObjectPath(testArena.arena);
    ASSERT_TRUE(FindNewAssetObjectCachePath(testArena, root / "cache", objectPath, changedObjectPath));
    EXPECT_NE(changedObjectPath, objectPath);

    NWB::Core::Assets::AssetBytes changedObjectBytes = MakeAssetBytes(testArena);
    ASSERT_TRUE(ReadAssetObjectCacheBytes(changedObjectPath, changedObjectBytes));
    EXPECT_FALSE(AssetBytesEqual(firstObjectBytes, changedObjectBytes));

    loadedAsset.reset();
    EXPECT_TRUE(LoadCookedMinimalMesh(testArena, outputDirectory, loadedAsset));

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
template<typename T>
static bool OverwritePOD(NWB::Core::Assets::AssetBytes& binary, const usize offset, const T value){
    if(offset > binary.size() || sizeof(value) > binary.size() - offset)
        return false;

    NWB_MEMCPY(binary.data() + offset, sizeof(value), &value, sizeof(value));
    return true;
}

static bool FindMaterialBinaryTypedLayoutOffsets(
    const NWB::Core::Assets::AssetBytes& binary,
    usize& outLayoutHashOffset,
    usize& outBlockByteCountOffset
){
    outLayoutHashOffset = 0u;
    outBlockByteCountOffset = 0u;

    usize cursor = 0u;
    u32 value32 = 0u;
    if(!ReadPOD(binary, cursor, value32))
        return false;

    u32 shaderVariantByteCount = 0u;
    if(!ReadPOD(binary, cursor, shaderVariantByteCount))
        return false;
    if(cursor > binary.size() || shaderVariantByteCount > binary.size() - cursor)
        return false;
    cursor += shaderVariantByteCount;

    if(cursor > binary.size() || sizeof(NameHash) > binary.size() - cursor)
        return false;
    cursor += sizeof(NameHash);

    outLayoutHashOffset = cursor;

    u64 layoutHash = 0u;
    u32 blockCount = 0u;
    u32 fieldCount = 0u;
    if(
        !ReadPOD(binary, cursor, layoutHash)
        || !ReadPOD(binary, cursor, blockCount)
        || !ReadPOD(binary, cursor, fieldCount)
    )
        return false;

    if(
        cursor > binary.size()
        || blockCount > (binary.size() - cursor) / NWB::Impl::MaterialBinaryPayload::s_TypedLayoutBlockBytes
    )
        return false;
    cursor += static_cast<usize>(blockCount) * NWB::Impl::MaterialBinaryPayload::s_TypedLayoutBlockBytes;

    if(
        cursor > binary.size()
        || fieldCount > (binary.size() - cursor) / NWB::Impl::MaterialBinaryPayload::s_TypedLayoutFieldBytes
    )
        return false;
    cursor += static_cast<usize>(fieldCount) * NWB::Impl::MaterialBinaryPayload::s_TypedLayoutFieldBytes;

    outBlockByteCountOffset = cursor;
    return true;
}

#endif

static NWB::Impl::Mesh BuildMinimalMesh(TestArena& testArena){
    NWB::Impl::Mesh mesh(testArena.arena, Name("tests/meshes/minimal_mesh"));

    auto positions = MakeAssetVector<Float3U>(testArena);
    positions.push_back(Float3U(-0.5f, -0.5f, 0.f));
    positions.push_back(Float3U(0.5f, -0.5f, 0.f));
    positions.push_back(Float3U(0.f, 0.5f, 0.f));

    auto normals = MakeAssetVector<Half4U>(testArena);
    normals.assign(positions.size(), MakeHalf4U(0.0f, 0.0f, 1.0f, 0.0f));

    auto colors = MakeAssetVector<Half4U>(testArena);
    colors.push_back(MakeHalf4U(1.0f, 0.0f, 0.0f, 1.0f));
    colors.push_back(MakeHalf4U(0.0f, 1.0f, 0.0f, 1.0f));
    colors.push_back(MakeHalf4U(0.0f, 0.0f, 1.0f, 1.0f));

    auto tangents = MakeAssetVector<Half4U>(testArena);
    tangents.push_back(MakeHalf4U(1.0f, 0.0f, 0.0f, 1.0f));

    auto uv0 = MakeAssetVector<Float2U>(testArena);
    uv0.push_back(Float2U(0.0f, 0.0f));
    uv0.push_back(Float2U(1.0f, 0.0f));
    uv0.push_back(Float2U(0.5f, 1.0f));

    auto meshlets = MakeAssetVector<NWB::Impl::MeshletDesc>(testArena);
    meshlets.push_back(NWB::Impl::MeshletDesc{ 0u, 0u, 0u, 0u, NWB::Impl::PackMeshletCounts(3u, 1u, 3u, 3u) });
    auto meshletBounds = MakeAssetVector<NWB::Impl::MeshletBounds>(testArena);
    meshletBounds.push_back(MakeTestMeshletBounds());

    auto meshletPositionStreamRefs = MakeAssetVector<NWB::Impl::MeshletPositionStreamRef>(testArena);
    auto meshletAttributeStreamRefs = MakeAssetVector<NWB::Impl::MeshletAttributeStreamRef>(testArena);
    auto meshletLocalVertexRefs = MakeAssetVector<NWB::Impl::MeshletLocalVertexRef>(testArena);
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

    auto meshletPrimitiveIndices = MakeAssetVector<u8>(testArena);
    meshletPrimitiveIndices.push_back(0u);
    meshletPrimitiveIndices.push_back(1u);
    meshletPrimitiveIndices.push_back(2u);

    auto meshletPositionRefDeltas = MakeAssetVector<u8>(testArena);
    auto meshletAttributeRefDeltas = MakeAssetVector<u8>(testArena);
    const bool meshletRefsEncoded = EncodeTestMeshletRefs(
        meshlets,
        meshletPositionStreamRefs,
        meshletAttributeStreamRefs,
        meshletPositionRefDeltas,
        meshletAttributeRefDeltas,
        false
    );
    NWB_ASSERT(meshletRefsEncoded);
    static_cast<void>(meshletRefsEncoded);

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

template<typename AssetT, typename CodecT>
static const AssetT& CheckCodecRoundTrip(
    TestArena& testArena,
    const AssetT& asset,
    const CodecT& codec,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset){
    EXPECT_TRUE(asset.validatePayload());

    NWB::Core::Assets::AssetBytes binary = MakeAssetBytes(testArena);
    EXPECT_TRUE(codec.serialize(asset, binary));
    EXPECT_FALSE(binary.empty());

    EXPECT_TRUE(codec.deserialize(testArena.arena, asset.virtualPath(), binary, outLoadedAsset));
    EXPECT_NE(outLoadedAsset.get(), nullptr);
    EXPECT_EQ(outLoadedAsset->assetType(), AssetT::AssetTypeName());
    return static_cast<const AssetT&>(*outLoadedAsset);
}

template<typename CodecT>
static void CheckCodecRejectsBinary(
    TestArena& testArena,
    const CodecT& codec,
    const Name& virtualPath,
    const NWB::Core::Assets::AssetBytes& binary){
    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    EXPECT_FALSE(codec.deserialize(testArena.arena, virtualPath, binary, loadedAsset));
    EXPECT_FALSE(loadedAsset);
}

TEST(AssetsGraphics, MeshCodecRoundTrip){
    TestArena testArena;
    NWB::Impl::Mesh mesh = BuildMinimalMesh(testArena);

    NWB::Impl::MeshAssetCodec codec;
    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    const NWB::Impl::Mesh& loadedMesh = CheckCodecRoundTrip(testArena, mesh, codec, loadedAsset);
    EXPECT_EQ(loadedMesh.positionStream().size(), 3u);
    EXPECT_EQ(loadedMesh.meshletPrimitiveIndices().size(), 3u);
    EXPECT_EQ(loadedMesh.positionStream()[1].x, 0.5f);
    EXPECT_EQ(LoadHalf4U(loadedMesh.normalStream()[1]).z, 1.f);
    EXPECT_EQ(LoadHalf4U(loadedMesh.colorStream()[1]).y, 1.f);
    EXPECT_EQ(loadedMesh.meshletPrimitiveIndices()[2], 2u);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

