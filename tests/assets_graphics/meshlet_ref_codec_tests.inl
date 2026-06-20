// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void TestMeshletRefEncodingWidthRules(){
    EXPECT_TRUE((NWB::Impl::MeshletRefDeltaWidthForMaxDelta(0u) == NWB::Impl::MeshletRefDeltaWidth::U8));
    EXPECT_TRUE((NWB::Impl::MeshletRefDeltaWidthForMaxDelta(255u) == NWB::Impl::MeshletRefDeltaWidth::U8));
    EXPECT_TRUE((NWB::Impl::MeshletRefDeltaWidthForMaxDelta(256u) == NWB::Impl::MeshletRefDeltaWidth::U16));
    EXPECT_TRUE((NWB::Impl::MeshletRefDeltaWidthForMaxDelta(65535u) == NWB::Impl::MeshletRefDeltaWidth::U16));
    EXPECT_TRUE((NWB::Impl::MeshletRefDeltaWidthForMaxDelta(65536u) == NWB::Impl::MeshletRefDeltaWidth::U32));
}

static bool EncodeTestMeshletRefs(
    NWB::Core::Assets::AssetVector<NWB::Impl::MeshletDesc>& meshlets,
    const NWB::Core::Assets::AssetVector<NWB::Impl::MeshletPositionStreamRef>& positionRefs,
    const NWB::Core::Assets::AssetVector<NWB::Impl::MeshletAttributeStreamRef>& attributeRefs,
    NWB::Core::Assets::AssetVector<u8>& positionRefDeltas,
    NWB::Core::Assets::AssetVector<u8>& attributeRefDeltas,
    const bool skinRequired
){
    return NWB::Impl::EncodeMeshletRefDeltas(
        meshlets,
        positionRefs,
        attributeRefs,
        positionRefDeltas,
        attributeRefDeltas,
        skinRequired,
        [](const usize, const tchar*){ return false; }
    );
}

static void TestMeshletRefEncodingRoundTrip(){
    TestArena testArena;
    auto meshlets = MakeAssetVector<NWB::Impl::MeshletDesc>(testArena);
    meshlets.push_back(NWB::Impl::MeshletDesc{
        0u,
        0u,
        0u,
        0u,
        NWB::Impl::PackMeshletCounts(3u, 1u, 3u, 3u),
    });

    auto positionRefs = MakeAssetVector<NWB::Impl::MeshletPositionStreamRef>(testArena);
    positionRefs.push_back(NWB::Impl::MeshletPositionStreamRef{ 0u, 1000u });
    positionRefs.push_back(NWB::Impl::MeshletPositionStreamRef{ 256u, 1001u });
    positionRefs.push_back(NWB::Impl::MeshletPositionStreamRef{ 65536u, 1002u });

    auto attributeRefs = MakeAssetVector<NWB::Impl::MeshletAttributeStreamRef>(testArena);
    attributeRefs.push_back(NWB::Impl::MeshletAttributeStreamRef{ 5u, 1000u, 0u, 20u });
    attributeRefs.push_back(NWB::Impl::MeshletAttributeStreamRef{ 6u, 1256u, 65536u, 21u });
    attributeRefs.push_back(NWB::Impl::MeshletAttributeStreamRef{ 7u, 1257u, 65537u, 22u });

    auto positionRefDeltas = MakeAssetVector<u8>(testArena);
    auto attributeRefDeltas = MakeAssetVector<u8>(testArena);
    const bool encoded = EncodeTestMeshletRefs(
        meshlets,
        positionRefs,
        attributeRefs,
        positionRefDeltas,
        attributeRefDeltas,
        true
    );
    EXPECT_TRUE((encoded));
    if(!encoded)
        return;

    const NWB::Impl::MeshletDesc& meshlet = meshlets[0u];
    EXPECT_TRUE((NWB::Impl::MeshletRefEncodingWidth(meshlet.encoding, NWB::Impl::s_MeshletRefEncodingPositionShift)
            == NWB::Impl::MeshletRefDeltaWidth::U32));
    EXPECT_TRUE((NWB::Impl::MeshletRefEncodingWidth(meshlet.encoding, NWB::Impl::s_MeshletRefEncodingSkinShift)
            == NWB::Impl::MeshletRefDeltaWidth::U8));
    EXPECT_TRUE((NWB::Impl::MeshletRefEncodingWidth(meshlet.encoding, NWB::Impl::s_MeshletRefEncodingTangentShift)
            == NWB::Impl::MeshletRefDeltaWidth::U16));
    EXPECT_TRUE((NWB::Impl::MeshletRefEncodingWidth(meshlet.encoding, NWB::Impl::s_MeshletRefEncodingUv0Shift)
            == NWB::Impl::MeshletRefDeltaWidth::U32));

    for(u32 localPositionIndex = 0u; localPositionIndex < NWB::Impl::MeshletPositionCount(meshlet); ++localPositionIndex){
        NWB::Impl::MeshletPositionStreamRef decodedRef;
        EXPECT_TRUE((NWB::Impl::DecodeMeshletPositionRef(
            positionRefDeltas.data(),
            positionRefDeltas.size(),
            meshlet,
            localPositionIndex,
            true,
            decodedRef
        )));
        EXPECT_TRUE((decodedRef.position == positionRefs[localPositionIndex].position));
        EXPECT_TRUE((decodedRef.skin == positionRefs[localPositionIndex].skin));
    }

    for(u32 localAttributeIndex = 0u; localAttributeIndex < NWB::Impl::MeshletAttributeCount(meshlet); ++localAttributeIndex){
        NWB::Impl::MeshletAttributeStreamRef decodedRef;
        EXPECT_TRUE((NWB::Impl::DecodeMeshletAttributeRef(
            attributeRefDeltas.data(),
            attributeRefDeltas.size(),
            meshlet,
            localAttributeIndex,
            decodedRef
        )));
        EXPECT_TRUE((decodedRef.normal == attributeRefs[localAttributeIndex].normal));
        EXPECT_TRUE((decodedRef.tangent == attributeRefs[localAttributeIndex].tangent));
        EXPECT_TRUE((decodedRef.uv0 == attributeRefs[localAttributeIndex].uv0));
        EXPECT_TRUE((decodedRef.color == attributeRefs[localAttributeIndex].color));
    }
}

static void TestMeshletConeOctPackingRoundTrip(){
    const u32 centeredPacked =
        (128u << NWB::Impl::s_MeshletConeAxisXShift)
        | (128u << NWB::Impl::s_MeshletConeAxisYShift);
    EXPECT_TRUE((NWB::Impl::PackMeshletConeOct16(VectorSet(0.0f, 0.0f, 1.0f, 0.0f)) == centeredPacked));
    EXPECT_TRUE((NWB::Impl::PackMeshletConeOct16(VectorZero()) == NWB::Impl::s_MeshletConeAxisFallback));

    const SIMDVector axes[] = {
        Vector3Normalize(VectorSet(0.25f, -0.50f, -0.75f, 0.0f)),
        Vector3Normalize(VectorSet(-0.60f, 0.35f, -0.45f, 0.0f)),
        Vector3Normalize(VectorSet(0.35f, 0.40f, 0.85f, 0.0f)),
    };
    for(const SIMDVector axis : axes){
        const u32 packed = NWB::Impl::PackMeshletConeOct16(axis);
        const SIMDVector unpacked = NWB::Impl::UnpackMeshletConeOct16Axis(packed);
        EXPECT_TRUE((VectorGetX(Vector3Dot(axis, unpacked)) > 0.999f));
        if(VectorGetZ(axis) < 0.0f)
            EXPECT_TRUE((VectorGetZ(unpacked) < 0.0f));
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

