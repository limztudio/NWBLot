// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_texture_mip_admission_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = AssetsGraphicsFixture::TestArena;
using CapturingLogger = AssetsGraphicsFixture::CapturingLogger;

static constexpr usize s_MipCount = 3u;
static constexpr usize s_PrimaryPayloadBytes = 96u;
static constexpr usize s_MipTableBytes = sizeof(::TextureBinaryPayload::MipLevelBinary) * s_MipCount;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Core::Assets::AssetBytes BuildPartialBlockTextureBinary(TestArena& testArena){
    Core::Assets::AssetBytes binary(testArena.arena);
    binary.reserve(sizeof(::TextureBinaryPayload::HeaderBinary) + s_MipTableBytes + s_PrimaryPayloadBytes);
    ::TextureBinaryPayload::HeaderBinary header;
    header.dimension = TextureDimension::Texture2D;
    header.width = 7u;
    header.height = 5u;
    header.depth = 1u;
    header.mipCount = s_MipCount;
    header.alphaInfo = static_cast<u32>(TextureAlphaMode::Opaque) | (static_cast<u32>(TextureFormat::s_OpaqueAlphaUnorm8) << ::TextureBinaryPayload::s_AlphaInfoConstantShift);
    header.payloadFormat = TexturePayloadFormat::UastcLdr4x4;
    header.payloadByteCount = s_PrimaryPayloadBytes;
    AppendPOD(binary, header);
    const ::TextureBinaryPayload::MipLevelBinary mips[]{
        { 7u, 5u, 1u, 2u, 2u, 0u, 0u, 64u },
        { 3u, 2u, 1u, 1u, 1u, 0u, 64u, 16u },
        { 1u, 1u, 1u, 1u, 1u, 0u, 80u, 16u },
    };
    for(const auto& mip : mips)
        AppendPOD(binary, mip);
    for(usize byteIndex = 0u; byteIndex < s_PrimaryPayloadBytes; ++byteIndex)
        binary.push_back(static_cast<u8>(byteIndex));
    return binary;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace __hidden_texture_mip_admission_tests;


TEST(AssetsGraphics, TextureMipAdmissionRejectsEveryTruncatedPayloadPrefixAndOversizedCount){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    AssetsGraphicsFixture::TestArena testArena;
    const auto validBinary = BuildPartialBlockTextureBinary(testArena);
    Impl::Texture texture(testArena.arena, Name("project/textures/mip_table_admission"));
    ASSERT_TRUE(texture.loadBinary(validBinary));
    Impl::TextureAssetCodec codec;

    for(usize availableBytes = 0u; availableBytes < validBinary.size(); ++availableBytes){
        Core::Assets::AssetBytes truncated(validBinary.begin(), validBinary.begin() + availableBytes, testArena.arena);
        EXPECT_FALSE(texture.loadBinary(truncated)) << availableBytes;
        Core::Assets::AssetBytes retained(testArena.arena);
        ASSERT_TRUE(codec.serialize(texture, retained));
        EXPECT_EQ(retained, validBinary) << availableBytes;
    }
    Core::Assets::AssetBytes oversized(validBinary.begin(), validBinary.begin() + sizeof(::TextureBinaryPayload::HeaderBinary), testArena.arena);
    const u32 oversizedCount = Limit<u32>::s_Max;
    NWB_MEMCPY(oversized.data() + offsetof(::TextureBinaryPayload::HeaderBinary, mipCount), sizeof(oversizedCount), &oversizedCount, sizeof(oversizedCount));
    EXPECT_FALSE(texture.loadBinary(oversized));
    Core::Assets::AssetBytes retained(testArena.arena);
    ASSERT_TRUE(codec.serialize(texture, retained));
    EXPECT_EQ(retained, validBinary);
    EXPECT_TRUE(texture.loadBinary(validBinary));
}

TEST(AssetsGraphics, TextureCodecRejectsPayloadFormatNarrowingWithoutReplacingPublishedPayload){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    AssetsGraphicsFixture::TestArena testArena;
    const auto original = BuildPartialBlockTextureBinary(testArena);
    Impl::Texture texture(testArena.arena, Name("project/textures/payload_format_admission"));
    ASSERT_TRUE(texture.loadBinary(original));
    Impl::TextureAssetCodec codec;
    for(const u32 payloadFormat : { 2u, 255u, 256u, 257u, 0xffffff00u, 0xffffff01u, Limit<u32>::s_Max }){
        SCOPED_TRACE(payloadFormat);
        Core::Assets::AssetBytes malformed(original);
        NWB_MEMCPY(malformed.data() + offsetof(::TextureBinaryPayload::HeaderBinary, payloadFormat), sizeof(payloadFormat), &payloadFormat, sizeof(payloadFormat));
        EXPECT_FALSE(codec.deserialize(testArena.arena, texture.virtualPath(), malformed));
        EXPECT_FALSE(texture.loadBinary(malformed));
        Core::Assets::AssetBytes retained(testArena.arena);
        ASSERT_TRUE(codec.serialize(texture, retained));
        EXPECT_EQ(retained, original);
    }
    EXPECT_TRUE(texture.loadBinary(original));
}

TEST(AssetsGraphics, TextureCodecRejectsOpaqueValueInConstantAlphaModeWithoutReplacingPublishedPayload){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    AssetsGraphicsFixture::TestArena testArena;
    Core::Assets::AssetBytes original = BuildPartialBlockTextureBinary(testArena);
    const u32 hdrFormat = TexturePayloadFormat::UastcHdr4x4;
    const u32 alphaInfo = static_cast<u32>(TextureAlphaMode::ConstantUnorm8) | (TextureFormat::s_MaxConstantAlphaUnorm8 << ::TextureBinaryPayload::s_AlphaInfoConstantShift);
    NWB_MEMCPY(original.data() + offsetof(::TextureBinaryPayload::HeaderBinary, payloadFormat), sizeof(hdrFormat), &hdrFormat, sizeof(hdrFormat));
    NWB_MEMCPY(original.data() + offsetof(::TextureBinaryPayload::HeaderBinary, alphaInfo), sizeof(alphaInfo), &alphaInfo, sizeof(alphaInfo));
    Impl::Texture texture(testArena.arena, Name("project/textures/constant_alpha_admission"));
    ASSERT_TRUE(texture.loadBinary(original));
    Impl::TextureAssetCodec codec;
    Core::Assets::AssetBytes malformed(original);
    const u32 opaqueConstantInfo = static_cast<u32>(TextureAlphaMode::ConstantUnorm8) | (TextureFormat::s_OpaqueAlphaUnorm8 << ::TextureBinaryPayload::s_AlphaInfoConstantShift);
    NWB_MEMCPY(malformed.data() + offsetof(::TextureBinaryPayload::HeaderBinary, alphaInfo), sizeof(opaqueConstantInfo), &opaqueConstantInfo, sizeof(opaqueConstantInfo));
    EXPECT_FALSE(codec.deserialize(testArena.arena, texture.virtualPath(), malformed));
    EXPECT_FALSE(texture.loadBinary(malformed));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("constant alpha must be below opaque alpha")));
    Core::Assets::AssetBytes retained(testArena.arena);
    ASSERT_TRUE(codec.serialize(texture, retained));
    EXPECT_EQ(retained, original);
    EXPECT_TRUE(texture.loadBinary(original));
}

TEST(AssetsGraphics, TextureMipAdmissionRejectsCorruptRecordsAndRetainsExactEncodingAfterRejectedLoads){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    AssetsGraphicsFixture::TestArena testArena;
    const auto validBinary = BuildPartialBlockTextureBinary(testArena);
    Impl::Texture texture(testArena.arena, Name("project/textures/mip_record_admission"));
    ASSERT_TRUE(texture.loadBinary(validBinary));
    Impl::TextureAssetCodec codec;
    struct Corruption{
        usize fieldOffset;
        u32 value;
    };
    const Corruption corruptions[]{
        { offsetof(::TextureBinaryPayload::MipLevelBinary, reserved), 1u },
        { offsetof(::TextureBinaryPayload::MipLevelBinary, width), 8u },
        { offsetof(::TextureBinaryPayload::MipLevelBinary, blockCountX), 3u },
        { offsetof(::TextureBinaryPayload::MipLevelBinary, sliceCount), 2u },
        { offsetof(::TextureBinaryPayload::MipLevelBinary, offsetBytes), 1u },
        { offsetof(::TextureBinaryPayload::MipLevelBinary, sizeBytes), 48u },
    };
    for(const Corruption& corruption : corruptions){
        Core::Assets::AssetBytes malformed(validBinary.begin(), validBinary.end(), testArena.arena);
        NWB_MEMCPY(malformed.data() + sizeof(::TextureBinaryPayload::HeaderBinary) + corruption.fieldOffset, sizeof(corruption.value), &corruption.value, sizeof(corruption.value));
        EXPECT_FALSE(texture.loadBinary(malformed)) << corruption.fieldOffset;
        Core::Assets::AssetBytes retained(testArena.arena);
        ASSERT_TRUE(codec.serialize(texture, retained));
        EXPECT_EQ(retained, validBinary) << corruption.fieldOffset;
    }
    Core::Assets::AssetBytes trailing(validBinary.begin(), validBinary.end(), testArena.arena);
    trailing.push_back(0u);
    EXPECT_FALSE(texture.loadBinary(trailing));
    Core::Assets::AssetBytes encoded(testArena.arena);
    ASSERT_TRUE(codec.serialize(texture, encoded));
    ASSERT_EQ(encoded.size(), validBinary.size());
    EXPECT_EQ(NWB_MEMCMP(encoded.data(), validBinary.data(), validBinary.size()), 0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

