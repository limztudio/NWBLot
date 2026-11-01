// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_meshlet_ref_codec_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = AssetsGraphicsFixture::TestArena;

using MeshletView = Span<Impl::MeshletDesc>;
using PositionRefView = Span<const Impl::MeshletPositionStreamRef>;
using AttributeRefView = Span<const Impl::MeshletAttributeStreamRef>;


// These declarations check throw paths without calling them.
struct NoThrowMeshletBytes{
    [[nodiscard]] usize size()const noexcept;
    [[nodiscard]] const u8* data()const noexcept;
    void clear()noexcept;
    void reserve(usize)noexcept;
    void push_back(u8)noexcept;
};

struct ThrowingMeshletAppendResult{
    ~ThrowingMeshletAppendResult()noexcept(false);
};

struct ThrowingMeshletAppend : NoThrowMeshletBytes{
    ThrowingMeshletAppendResult push_back(u8)noexcept;
};

struct NoThrowMeshletFailure{
    [[nodiscard]] b8 operator()(usize, TStringView)const noexcept;
};

struct ThrowingMeshletFailureBool{
    operator b8()const;
};

struct ThrowingMeshletFailureConversion{
    [[nodiscard]] ThrowingMeshletFailureBool operator()(usize, TStringView)const noexcept;
};

static_assert(noexcept(Impl::AppendMeshletRefDelta(0u, Impl::MeshletRefDeltaWidth::U8, DeclVal<NoThrowMeshletBytes&>())));
static_assert(!noexcept(Impl::AppendMeshletRefDelta(0u, Impl::MeshletRefDeltaWidth::U8, DeclVal<ThrowingMeshletAppend&>())));
static_assert(noexcept(Impl::EncodeMeshletRefDeltas(DeclVal<const PositionRefView&>(), DeclVal<const AttributeRefView&>(), false, DeclVal<NoThrowMeshletFailure>(), DeclVal<MeshletView&>(), DeclVal<NoThrowMeshletBytes&>(), DeclVal<NoThrowMeshletBytes&>())));
static_assert(!noexcept(Impl::EncodeMeshletRefDeltas(
    DeclVal<const PositionRefView&>(),
    DeclVal<const AttributeRefView&>(),
    false,
    DeclVal<NoThrowMeshletFailure>(),
    DeclVal<MeshletView&>(),
    DeclVal<ThrowingMeshletAppend&>(),
    DeclVal<NoThrowMeshletBytes&>()
)));
static_assert(!noexcept(Impl::EncodeMeshletRefDeltas(
    DeclVal<const PositionRefView&>(),
    DeclVal<const AttributeRefView&>(),
    false,
    DeclVal<ThrowingMeshletFailureConversion>(),
    DeclVal<MeshletView&>(),
    DeclVal<NoThrowMeshletBytes&>(),
    DeclVal<NoThrowMeshletBytes&>()
)));


struct OctEncodingBoundary{
    Float3U axis;
    u32 packed;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u32 s_Oct16ChannelMask = 255u;
static constexpr u32 s_Oct16YShift = 8u;
static constexpr f32 s_Oct16UnormScale = 1.0f / 255.0f;
static constexpr f32 s_Oct16SignedScale = 2.0f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static SIMDVectorSingle ScalarOct16Reference(const u32 packed)noexcept{
    const f32 unormX = static_cast<f32>(packed & s_Oct16ChannelMask) * s_Oct16UnormScale;
    const f32 unormY = static_cast<f32>((packed >> s_Oct16YShift) & s_Oct16ChannelMask) * s_Oct16UnormScale;
    f32 x = unormX * s_Oct16SignedScale - 1.0f;
    f32 y = unormY * s_Oct16SignedScale - 1.0f;
    const f32 z = 1.0f - (Abs(x) + Abs(y));
    if(z < 0.0f){
        const f32 foldedX = (1.0f - Abs(y)) * (x >= 0.0f ? 1.0f : -1.0f);
        y = (1.0f - Abs(x)) * (y >= 0.0f ? 1.0f : -1.0f);
        x = foldedX;
    }
    return Vector3NormalizeOr(VectorSet(x, y, z, 0.0f), VectorSet(0.0f, 0.0f, 1.0f, 0.0f), NWB::Impl::s_MeshletConeAxisLengthSquaredEpsilon);
}

TEST(AssetsGraphics, MeshletRefEncodingCrossesIndexWidthBoundaries){
    TestArena testArena;
    auto meshlets = AssetsGraphicsFixture::MakeAssetVector<NWB::Impl::MeshletDesc>(testArena);
    meshlets.push_back(NWB::Impl::MeshletDesc{ 0u, 0u, 0u, 0u, NWB::Impl::PackMeshletCounts(3u, 1u, 3u, 3u), });

    auto positionRefs = AssetsGraphicsFixture::MakeAssetVector<NWB::Impl::MeshletPositionStreamRef>(testArena);
    positionRefs.push_back(NWB::Impl::MeshletPositionStreamRef{ 0u, 1000u });
    positionRefs.push_back(NWB::Impl::MeshletPositionStreamRef{ 256u, 1001u });
    positionRefs.push_back(NWB::Impl::MeshletPositionStreamRef{ 65536u, 1002u });

    auto attributeRefs = AssetsGraphicsFixture::MakeAssetVector<NWB::Impl::MeshletAttributeStreamRef>(testArena);
    attributeRefs.push_back(NWB::Impl::MeshletAttributeStreamRef{ 5u, 1000u, 0u, 20u });
    attributeRefs.push_back(NWB::Impl::MeshletAttributeStreamRef{ 6u, 1256u, 65536u, 21u });
    attributeRefs.push_back(NWB::Impl::MeshletAttributeStreamRef{ 7u, 1257u, 65537u, 22u });

    auto positionRefDeltas = AssetsGraphicsFixture::MakeAssetVector<u8>(testArena);
    auto attributeRefDeltas = AssetsGraphicsFixture::MakeAssetVector<u8>(testArena);
    const b8 encoded = AssetsGraphicsFixture::EncodeTestMeshletRefs(positionRefs, attributeRefs, true, meshlets, positionRefDeltas, attributeRefDeltas);
    EXPECT_TRUE(encoded);
    if(!encoded)
        return;

    const NWB::Impl::MeshletDesc& meshlet = meshlets[0u];
    EXPECT_EQ(NWB::Impl::MeshletRefEncodingWidth(meshlet.encoding, NWB::Impl::s_MeshletRefEncodingPositionShift), NWB::Impl::MeshletRefDeltaWidth::U32);
    EXPECT_EQ(NWB::Impl::MeshletRefEncodingWidth(meshlet.encoding, NWB::Impl::s_MeshletRefEncodingSkinShift), NWB::Impl::MeshletRefDeltaWidth::U8);
    EXPECT_EQ(NWB::Impl::MeshletRefEncodingWidth(meshlet.encoding, NWB::Impl::s_MeshletRefEncodingTangentShift), NWB::Impl::MeshletRefDeltaWidth::U16);
    EXPECT_EQ(NWB::Impl::MeshletRefEncodingWidth(meshlet.encoding, NWB::Impl::s_MeshletRefEncodingUv0Shift), NWB::Impl::MeshletRefDeltaWidth::U32);

    for(u32 localPositionIndex = 0u; localPositionIndex < NWB::Impl::MeshletPositionCount(meshlet); ++localPositionIndex){
        NWB::Impl::MeshletPositionStreamRef decodedRef;
        ASSERT_TRUE(NWB::Impl::TryDecodeMeshletPositionRef(positionRefDeltas, meshlet, localPositionIndex, true, decodedRef));
        EXPECT_EQ(decodedRef.position, positionRefs[localPositionIndex].position);
        EXPECT_EQ(decodedRef.skin, positionRefs[localPositionIndex].skin);
    }

    for(u32 localAttributeIndex = 0u; localAttributeIndex < NWB::Impl::MeshletAttributeCount(meshlet); ++localAttributeIndex){
        NWB::Impl::MeshletAttributeStreamRef decodedRef;
        ASSERT_TRUE(NWB::Impl::TryDecodeMeshletAttributeRef(attributeRefDeltas, meshlet, localAttributeIndex, decodedRef));
        EXPECT_EQ(decodedRef.normal, attributeRefs[localAttributeIndex].normal);
        EXPECT_EQ(decodedRef.tangent, attributeRefs[localAttributeIndex].tangent);
        EXPECT_EQ(decodedRef.uv0, attributeRefs[localAttributeIndex].uv0);
        EXPECT_EQ(decodedRef.color, attributeRefs[localAttributeIndex].color);
    }
}

TEST(AssetsGraphics, MeshletOct16DecodePreservesQuantizationAndFoldBits){
    // Scalar octahedral decode fixes UNORM endpoint and fold rounding.
    for(u32 packed = 0u; packed < 65536u; ++packed){
        const SIMDVectorSingle reference = ScalarOct16Reference(packed);
        const SIMDVectorSingle decoded = NWB::Impl::UnpackMeshletConeOct16Axis(packed);
        ASSERT_EQ(VectorGetIntX(decoded), VectorGetIntX(reference)) << packed;
        ASSERT_EQ(VectorGetIntY(decoded), VectorGetIntY(reference)) << packed;
        ASSERT_EQ(VectorGetIntZ(decoded), VectorGetIntZ(reference)) << packed;
        ASSERT_EQ(VectorGetIntW(decoded), VectorGetIntW(reference)) << packed;
    }
}

TEST(AssetsGraphics, MeshletOct16EncodingRetainsHalfStepAndFoldBoundaries){
    const Array<OctEncodingBoundary, 11u> boundaries = {{
        { { -0x1p-23f, 0.0f, 0x1.fffffcp-1f }, 0x807fu },
        { { 0.0f, 0.0f, 1.0f }, 0x8080u },
        { { 0x1p-23f, 0.0f, 0x1.fffffcp-1f }, 0x8080u },
        { { 0.0f, -0x1p-23f, 0x1.fffffcp-1f }, 0x7f80u },
        { { 0.0f, 0x1p-23f, 0x1.fffffcp-1f }, 0x8080u },
        { { -0.0f, -0.0f, -1.0f }, 0xffffu },
        { { 1.0f, 0.0f, -1.0f }, 0xbfffu },
        { { 0.0f, -1.0f, -1.0f }, 0x00bfu },
        { { 1.0f, 1.0f, 0.0f }, 0xbfbfu },
        { { 1.0f, 1.0f, -0x1p-23f }, 0xbfbfu },
        { { 1.0f, 1.0f, 0x1p-23f }, 0xbfbfu },
    }};
    for(const OctEncodingBoundary& boundary : boundaries){
        volatile f32 x = boundary.axis.x, y = boundary.axis.y, z = boundary.axis.z;
        EXPECT_EQ(NWB::Impl::PackMeshletConeOct16(VectorSet(x, y, z, 0.0f)), boundary.packed) << boundary.packed;
    }
    EXPECT_EQ(NWB::Impl::PackMeshletConeOct16(VectorZero()), NWB::Impl::s_MeshletConeAxisFallback);
}


TEST(AssetsGraphics, MeshletPositionAndSkinDeltasRejectU32WraparoundAtEveryWidth){
    using namespace NWB::Impl;
    for(const MeshletRefDeltaWidth::Enum width : { MeshletRefDeltaWidth::U8, MeshletRefDeltaWidth::U16, MeshletRefDeltaWidth::U32 }){
        MeshletDesc meshlet;
        meshlet.counts = PackMeshletCounts(1u, 1u, 1u, 1u);
        meshlet.encoding = PackMeshletRefEncoding(width, width, width, width, width, width);
        meshlet.positionBase = Limit<u32>::s_Max - 1u;
        const u32 byteWidth = MeshletRefDeltaByteWidth(width);
        Array<u8, 8u> bytes = { 1u };
        MeshletPositionStreamRef boundary;
        ASSERT_TRUE(TryDecodeMeshletPositionRef(Span<const u8>(bytes.data(), byteWidth), meshlet, 0u, false, boundary));
        EXPECT_EQ(boundary.position, Limit<u32>::s_Max);
        meshlet.positionBase = Limit<u32>::s_Max;
        EXPECT_FALSE(TryDecodeMeshletPositionRef(Span<const u8>(bytes.data(), byteWidth), meshlet, 0u, false, boundary));

        bytes[0u] = 0u;
        bytes[byteWidth] = 1u;
        meshlet.positionBase = 0u;
        meshlet.skinBase = Limit<u32>::s_Max - 1u;
        MeshletPositionStreamRef skinBoundary;
        ASSERT_TRUE(TryDecodeMeshletPositionRef(Span<const u8>(bytes.data(), byteWidth * 2u), meshlet, 0u, true, skinBoundary));
        EXPECT_EQ(skinBoundary.skin, Limit<u32>::s_Max);
        meshlet.skinBase = Limit<u32>::s_Max;
        EXPECT_FALSE(TryDecodeMeshletPositionRef(Span<const u8>(bytes.data(), byteWidth * 2u), meshlet, 0u, true, skinBoundary));
    }
}

TEST(AssetsGraphics, MeshletAttributeDeltasRejectU32WraparoundInEveryChannelAndWidth){
    using namespace NWB::Impl;
    const Array<u32 MeshletDesc::*, 4u> baseMembers = { &MeshletDesc::normalBase, &MeshletDesc::tangentBase, &MeshletDesc::uv0Base, &MeshletDesc::colorBase };
    const Array<u32 MeshletAttributeStreamRef::*, 4u> indexMembers = { &MeshletAttributeStreamRef::normal, &MeshletAttributeStreamRef::tangent, &MeshletAttributeStreamRef::uv0, &MeshletAttributeStreamRef::color };
    for(const MeshletRefDeltaWidth::Enum width : { MeshletRefDeltaWidth::U8, MeshletRefDeltaWidth::U16, MeshletRefDeltaWidth::U32 }){
        const u32 byteWidth = MeshletRefDeltaByteWidth(width);
        for(usize channel = 0u; channel < baseMembers.size(); ++channel){
            MeshletDesc meshlet;
            meshlet.counts = PackMeshletCounts(1u, 1u, 1u, 1u);
            meshlet.encoding = PackMeshletRefEncoding(width, width, width, width, width, width);
            meshlet.*baseMembers[channel] = Limit<u32>::s_Max - 1u;
            Array<u8, 16u> bytes = {};
            bytes[channel * byteWidth] = 1u;
            MeshletAttributeStreamRef boundary;
            ASSERT_TRUE(TryDecodeMeshletAttributeRef(Span<const u8>(bytes.data(), byteWidth * 4u), meshlet, 0u, boundary));
            EXPECT_EQ(boundary.*indexMembers[channel], Limit<u32>::s_Max);
            meshlet.*baseMembers[channel] = Limit<u32>::s_Max;
            EXPECT_FALSE(TryDecodeMeshletAttributeRef(Span<const u8>(bytes.data(), byteWidth * 4u), meshlet, 0u, boundary));
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

