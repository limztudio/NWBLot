// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool CookAndLoadMinimalAsset(
    TestContext& context,
    TestArena& testArena,
    const AStringView metaText,
    const AStringView caseName,
    Path& outRoot,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset,
    CookSingleMetaFn cookSingleMeta,
    LoadCookedAssetFn loadCookedAsset
){
    Path outputDirectory(PathArena());
    const bool cooked = cookSingleMeta(metaText, caseName, testArena, outRoot, outputDirectory);

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, cooked);
    if(!cooked){
        ErrorCode errorCode;
        static_cast<void>(RemoveAllIfExists(outRoot, errorCode));
        return false;
    }

    if(loadCookedAsset(context, testArena, outputDirectory, outLoadedAsset))
        return true;

    ErrorCode errorCode;
    static_cast<void>(RemoveAllIfExists(outRoot, errorCode));
    return false;
}

static bool CookAndLoadMinimalAssetByKind(
    TestContext& context,
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
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, false);
        return false;
    }

    return CookAndLoadMinimalAsset(
        context,
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
    TestContext& context,
    const MeshT& loadedMesh
){
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.meshlets().size() == 1u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.meshletBounds().size() == 1u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.meshletLocalVertexRefs().size() == 3u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.meshletPrimitiveIndices().size() == 3u);

    const NWB::Impl::MeshletDesc& meshlet = loadedMesh.meshlets()[0u];
    const bool skinRequired = NWB::Core::Mesh::MeshClassUsesSkinning(loadedMesh.meshClass());
    usize expectedPositionRefBytes = 0u;
    usize expectedAttributeRefBytes = 0u;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Impl::MeshletVertexCount(meshlet) == 3u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Impl::MeshletPrimitiveCount(meshlet) == 1u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Impl::MeshletPositionCount(meshlet) == 3u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Impl::MeshletAttributeCount(meshlet) == 3u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Impl::MeshletEncodedPositionRefByteCount(meshlet, skinRequired, expectedPositionRefBytes));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Impl::MeshletEncodedAttributeRefByteCount(meshlet, expectedAttributeRefBytes));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.meshletPositionRefDeltas().size() == expectedPositionRefBytes);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.meshletAttributeRefDeltas().size() == expectedAttributeRefBytes);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, meshlet.positionBase == 0u);
    const u32 expectedSkinBase = skinRequired ? 0u : NWB::Impl::s_MeshMissingStreamIndex;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, meshlet.skinBase == expectedSkinBase);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, meshlet.normalBase == 0u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, meshlet.tangentBase == 0u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, meshlet.uv0Base == 0u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, meshlet.colorBase == 0u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, meshlet.encoding == 0u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.meshletBounds()[0u].sphere.w > 0.0f);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Impl::MeshletConeEnabled(loadedMesh.meshletBounds()[0u]));
}

template<typename AssetT, typename CheckLoadedAssetFn>
static void CookAndCheckMinimalTypedAsset(
    TestContext& context,
    const AStringView metaText,
    const AStringView caseName,
    const MinimalAssetKind::Enum assetKind,
    CheckLoadedAssetFn&& checkLoadedAsset
){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(PathArena());
    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    if(!CookAndLoadMinimalAssetByKind(
        context,
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
    static_cast<void>(RemoveAllIfExists(root, errorCode));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);
}

static void TestVolumeSessionAcceptsScratchBytes(TestContext& context){
    TestArena testArena;
    const Path root = AssetsGraphicsTestCaseRoot("volume_scratch_bytes");
    const bool prepared = PrepareCleanDirectory(root);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, prepared);

    if(prepared){
        NWB::Core::Filesystem::VolumeBuildConfig config;
        config.volumeName = "scratch_test";
        config.segmentSize = 64ull * 1024ull;
        config.metadataSize = 4ull * 1024ull;

        NWB::Core::Filesystem::VolumeSession volumeSession(testArena.arena);
        const bool created = volumeSession.create(root / "volume", config);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, created);
        if(created){
            NWB::Core::Alloc::ScratchArena scratchArena;
            ::Vector<u8, NWB::Core::Alloc::ScratchArena> payload{ scratchArena };
            payload.reserve(4u);
            payload.push_back(1u);
            payload.push_back(2u);
            payload.push_back(3u);
            payload.push_back(4u);

            const Name virtualPath("project/tests/scratch_payload");
            const bool pushed = volumeSession.pushDataDeferred(virtualPath, payload);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, pushed);
            if(pushed){
                payload[0] = 99u;

                const bool flushed = volumeSession.flush();
                NWB_ASSETS_GRAPHICS_TEST_CHECK(context, flushed);
                if(flushed){
                    NWB::Core::Assets::AssetBytes readback = MakeAssetBytes(testArena);
                    const bool loaded = volumeSession.loadData(virtualPath, readback);
                    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loaded);
                    if(loaded){
                        NWB_ASSETS_GRAPHICS_TEST_CHECK(
                            context,
                            readback.size() == 4u
                                && readback[0] == 1u
                                && readback[1] == 2u
                                && readback[2] == 3u
                                && readback[3] == 4u
                        );
                    }
                }
            }
        }
    }

    ErrorCode errorCode;
    static_cast<void>(RemoveAllIfExists(root, errorCode));
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
    TestContext& context,
    TestArena& testArena,
    const AssetT& asset,
    const CodecT& codec,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset){
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, asset.validatePayload());

    NWB::Core::Assets::AssetBytes binary = MakeAssetBytes(testArena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, codec.serialize(asset, binary));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !binary.empty());

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, codec.deserialize(testArena.arena, asset.virtualPath(), binary, outLoadedAsset));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, static_cast<bool>(outLoadedAsset));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, outLoadedAsset->assetType() == AssetT::AssetTypeName());
    return static_cast<const AssetT&>(*outLoadedAsset);
}

template<typename CodecT>
static void CheckCodecRejectsBinary(
    TestContext& context,
    TestArena& testArena,
    const CodecT& codec,
    const Name& virtualPath,
    const NWB::Core::Assets::AssetBytes& binary){
    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !codec.deserialize(testArena.arena, virtualPath, binary, loadedAsset));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !loadedAsset);
}

static void TestMeshCodecRoundTrip(TestContext& context){
    TestArena testArena;
    NWB::Impl::Mesh mesh = BuildMinimalMesh(testArena);

    NWB::Impl::MeshAssetCodec codec;
    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    const NWB::Impl::Mesh& loadedMesh = CheckCodecRoundTrip(context, testArena, mesh, codec, loadedAsset);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.positionStream().size() == 3u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.meshletPrimitiveIndices().size() == 3u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.positionStream()[1].x == 0.5f);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, LoadHalf4U(loadedMesh.normalStream()[1]).z == 1.f);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, LoadHalf4U(loadedMesh.colorStream()[1]).y == 1.f);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMesh.meshletPrimitiveIndices()[2] == 2u);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

