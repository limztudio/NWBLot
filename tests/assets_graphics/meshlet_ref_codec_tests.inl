// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void TestMeshletRefEncodingWidthRules(TestContext& context){
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Impl::MeshletRefDeltaWidthForMaxDelta(0u) == NWB::Impl::MeshletRefDeltaWidth::U8);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Impl::MeshletRefDeltaWidthForMaxDelta(255u) == NWB::Impl::MeshletRefDeltaWidth::U8);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Impl::MeshletRefDeltaWidthForMaxDelta(256u) == NWB::Impl::MeshletRefDeltaWidth::U16);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Impl::MeshletRefDeltaWidthForMaxDelta(65535u) == NWB::Impl::MeshletRefDeltaWidth::U16);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Impl::MeshletRefDeltaWidthForMaxDelta(65536u) == NWB::Impl::MeshletRefDeltaWidth::U32);
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

static void TestMeshletRefEncodingRoundTrip(TestContext& context){
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
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, encoded);
    if(!encoded)
        return;

    const NWB::Impl::MeshletDesc& meshlet = meshlets[0u];
    NWB_ASSETS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::MeshletRefEncodingWidth(meshlet.encoding, NWB::Impl::s_MeshletRefEncodingPositionShift)
            == NWB::Impl::MeshletRefDeltaWidth::U32
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::MeshletRefEncodingWidth(meshlet.encoding, NWB::Impl::s_MeshletRefEncodingSkinShift)
            == NWB::Impl::MeshletRefDeltaWidth::U8
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::MeshletRefEncodingWidth(meshlet.encoding, NWB::Impl::s_MeshletRefEncodingTangentShift)
            == NWB::Impl::MeshletRefDeltaWidth::U16
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::MeshletRefEncodingWidth(meshlet.encoding, NWB::Impl::s_MeshletRefEncodingUv0Shift)
            == NWB::Impl::MeshletRefDeltaWidth::U32
    );

    for(u32 localPositionIndex = 0u; localPositionIndex < NWB::Impl::MeshletPositionCount(meshlet); ++localPositionIndex){
        NWB::Impl::MeshletPositionStreamRef decodedRef;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Impl::DecodeMeshletPositionRef(
            positionRefDeltas.data(),
            positionRefDeltas.size(),
            meshlet,
            localPositionIndex,
            true,
            decodedRef
        ));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, decodedRef.position == positionRefs[localPositionIndex].position);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, decodedRef.skin == positionRefs[localPositionIndex].skin);
    }

    for(u32 localAttributeIndex = 0u; localAttributeIndex < NWB::Impl::MeshletAttributeCount(meshlet); ++localAttributeIndex){
        NWB::Impl::MeshletAttributeStreamRef decodedRef;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Impl::DecodeMeshletAttributeRef(
            attributeRefDeltas.data(),
            attributeRefDeltas.size(),
            meshlet,
            localAttributeIndex,
            decodedRef
        ));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, decodedRef.normal == attributeRefs[localAttributeIndex].normal);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, decodedRef.tangent == attributeRefs[localAttributeIndex].tangent);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, decodedRef.uv0 == attributeRefs[localAttributeIndex].uv0);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, decodedRef.color == attributeRefs[localAttributeIndex].color);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
