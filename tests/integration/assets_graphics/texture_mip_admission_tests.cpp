// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_texture_mip_admission{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = AssetsGraphicsFixture::TestArena;
using CapturingLogger = AssetsGraphicsFixture::CapturingLogger;

static constexpr usize s_MipCount = 3u;
static constexpr usize s_PrimaryPayloadBytes = 96u;
static constexpr usize s_MipTableBytes = sizeof(Impl::TextureBinaryPayload::MipLevelBinary) * s_MipCount;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Core::Assets::AssetBytes BuildPartialBlockTextureBinary(TestArena& testArena){
    Core::Assets::AssetBytes binary(testArena.arena);
    binary.reserve(sizeof(Impl::TextureBinaryPayload::HeaderBinary) + s_MipTableBytes + s_PrimaryPayloadBytes);
    Impl::TextureBinaryPayload::HeaderBinary header;
    header.colorSpace = Impl::TextureColorSpace::Srgb;
    header.dimension = TextureDimension::Texture2D;
    header.width = 7u;
    header.height = 5u;
    header.depth = 1u;
    header.mipCount = s_MipCount;
    header.alphaInfo = static_cast<u32>(TextureAlphaMode::Opaque) | (static_cast<u32>(TextureFormat::s_OpaqueAlphaUnorm8) << Impl::TextureBinaryPayload::s_AlphaInfoConstantShift);
    header.payloadFormat = TexturePayloadFormat::UastcLdr4x4;
    header.payloadByteCount = s_PrimaryPayloadBytes;
    AppendPOD(binary, header);
    const Impl::TextureBinaryPayload::MipLevelBinary mips[]{
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


using namespace __hidden_texture_mip_admission;


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
    Core::Assets::AssetBytes oversized(validBinary.begin(), validBinary.begin() + sizeof(Impl::TextureBinaryPayload::HeaderBinary), testArena.arena);
    const u32 oversizedCount = Limit<u32>::s_Max;
    NWB_MEMCPY(oversized.data() + offsetof(Impl::TextureBinaryPayload::HeaderBinary, mipCount), sizeof(oversizedCount), &oversizedCount, sizeof(oversizedCount));
    EXPECT_FALSE(texture.loadBinary(oversized));
    Core::Assets::AssetBytes retained(testArena.arena);
    ASSERT_TRUE(codec.serialize(texture, retained));
    EXPECT_EQ(retained, validBinary);
    EXPECT_TRUE(texture.loadBinary(validBinary));
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
        { offsetof(Impl::TextureBinaryPayload::MipLevelBinary, reserved), 1u },
        { offsetof(Impl::TextureBinaryPayload::MipLevelBinary, width), 8u },
        { offsetof(Impl::TextureBinaryPayload::MipLevelBinary, blockCountX), 3u },
        { offsetof(Impl::TextureBinaryPayload::MipLevelBinary, sliceCount), 2u },
        { offsetof(Impl::TextureBinaryPayload::MipLevelBinary, offsetBytes), 1u },
        { offsetof(Impl::TextureBinaryPayload::MipLevelBinary, sizeBytes), 48u },
    };
    for(const Corruption& corruption : corruptions){
        Core::Assets::AssetBytes malformed(validBinary.begin(), validBinary.end(), testArena.arena);
        NWB_MEMCPY(malformed.data() + sizeof(Impl::TextureBinaryPayload::HeaderBinary) + corruption.fieldOffset, sizeof(corruption.value), &corruption.value, sizeof(corruption.value));
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

