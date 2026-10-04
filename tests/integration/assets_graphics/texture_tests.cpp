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
constexpr u32 s_ThirdElementIndex = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_texture{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using AString = AssetsGraphicsFixture::AString;
using CapturingLogger = AssetsGraphicsFixture::CapturingLogger;
using Path = AssetsGraphicsFixture::Path;
using TestArena = AssetsGraphicsFixture::TestArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_TextureTestMetadata =
    "texture asset;\n\n"
    "asset.format = \"uastc_ldr_4x4\";\n"
    "asset.color_space = \"srgb\";\n"
    "asset.dimension = \"2d\";\n"
    "asset.width = 7;\n"
    "asset.height = 5;\n"
    "asset.has_alpha = 1;\n"
    "asset.data = \"checker.tex\";\n"
;

static constexpr AStringView s_TextureCubeTestMetadata =
    "texture asset;\n\n"
    "asset.format = \"uastc_ldr_4x4\";\n"
    "asset.color_space = \"srgb\";\n"
    "asset.dimension = \"cube\";\n"
    "asset.width = 2;\n"
    "asset.height = 2;\n"
    "asset.has_alpha = 0;\n"
    "asset.data = \"sky.tex\";\n"
;

static constexpr AStringView s_TextureVolumeTestMetadata =
    "texture asset;\n\n"
    "asset.format = \"uastc_ldr_4x4\";\n"
    "asset.color_space = \"linear\";\n"
    "asset.dimension = \"volume\";\n"
    "asset.depth = 3;\n"
    "asset.width = 4;\n"
    "asset.height = 2;\n"
    "asset.has_alpha = 1;\n"
    "asset.data = \"fog.tex\";\n"
;

static constexpr AStringView s_TextureHdrTestMetadata =
    "texture asset;\n\n"
    "asset.format = \"uastc_hdr_4x4\";\n"
    "asset.dimension = \"2d\";\n"
    "asset.width = 4;\n"
    "asset.height = 2;\n"
    "asset.alpha_mode = \"uastc_ldr_4x4\";\n"
    "asset.data = \"bright.tex\";\n"
;


static NWB::Core::Assets::AssetBytes MakeTextureTestUastcPayload(
    TestArena& testArena,
    const usize byteCount = 96u,
    const u8 initialValue = 0u
){
    NWB::Core::Assets::AssetBytes bytes = AssetsGraphicsFixture::MakeAssetBytes(testArena);
    bytes.resize(byteCount);
    for(usize index = 0u; index < bytes.size(); ++index)
        bytes[index] = static_cast<u8>(initialValue + index);
    return bytes;
}

static NWB::Impl::Texture::MipLevelVector MakeTextureTestMipLevels(TestArena& testArena){
    NWB::Impl::Texture::MipLevelVector mipLevels(testArena.arena);
    mipLevels.reserve(3u);
    mipLevels.push_back(NWB::Impl::TextureMipLevel{ 7u, 5u, s_ExpectedDualCount, s_ExpectedDualCount, 0u, 64u });
    mipLevels.push_back(NWB::Impl::TextureMipLevel{ 3u, s_ExpectedDualCount, 1u, 1u, 64u, 16u });
    mipLevels.push_back(NWB::Impl::TextureMipLevel{ 1u, 1u, 1u, 1u, 80u, 16u });
    return mipLevels;
}

static NWB::Impl::Texture::MipLevelVector MakeTextureCubeTestMipLevels(TestArena& testArena){
    NWB::Impl::Texture::MipLevelVector mipLevels(testArena.arena);
    mipLevels.reserve(s_ExpectedDualCount);
    mipLevels.push_back(NWB::Impl::TextureMipLevel{ s_ExpectedDualCount, s_ExpectedDualCount, 1u, 1u, 0u, 96u, 6u });
    mipLevels.push_back(NWB::Impl::TextureMipLevel{ 1u, 1u, 1u, 1u, 96u, 96u, 6u });
    return mipLevels;
}

static NWB::Impl::Texture::MipLevelVector MakeTextureVolumeTestMipLevels(TestArena& testArena){
    NWB::Impl::Texture::MipLevelVector mipLevels(testArena.arena);
    mipLevels.reserve(3u);
    mipLevels.push_back(NWB::Impl::TextureMipLevel{ 4u, s_ExpectedDualCount, 1u, 1u, 0u, 48u, 3u });
    mipLevels.push_back(NWB::Impl::TextureMipLevel{ s_ExpectedDualCount, 1u, 1u, 1u, 48u, 16u, 1u });
    mipLevels.push_back(NWB::Impl::TextureMipLevel{ 1u, 1u, 1u, 1u, 64u, 16u, 1u });
    return mipLevels;
}

static NWB::Core::Assets::AssetBytes MakeTextureTestHdrPayload(TestArena& testArena){
    NWB::Core::Assets::AssetBytes bytes = AssetsGraphicsFixture::MakeAssetBytes(testArena);
    // HDR RGB blocks come first; the matched LDR alpha blocks deliberately use
    // a different byte range so the tests exercise the trailing-stream boundary.
    bytes.resize(96u);
    for(usize index = 0u; index < 48u; ++index)
        bytes[index] = static_cast<u8>(0x40u + index);
    for(usize index = 0u; index < 48u; ++index)
        bytes[48u + index] = static_cast<u8>(0x80u + index);
    return bytes;
}

static NWB::Impl::Texture::MipLevelVector MakeTextureHdrTestMipLevels(TestArena& testArena){
    NWB::Impl::Texture::MipLevelVector mipLevels(testArena.arena);
    mipLevels.reserve(3u);
    mipLevels.push_back(NWB::Impl::TextureMipLevel{ 4u, s_ExpectedDualCount, 1u, 1u, 0u, 16u });
    mipLevels.push_back(NWB::Impl::TextureMipLevel{ s_ExpectedDualCount, 1u, 1u, 1u, 16u, 16u });
    mipLevels.push_back(NWB::Impl::TextureMipLevel{ 1u, 1u, 1u, 1u, 32u, 16u });
    return mipLevels;
}

TEST(AssetsGraphics, TextureFormatComputesSharedMipAndUastcBlockLayouts){
    u32 mipCount = 0u;
    EXPECT_TRUE(TextureFormat::ComputeCompleteMipCount(
        TextureDimension::Texture2D,
        7u,
        5u,
        1u,
        mipCount
    ));
    EXPECT_EQ(mipCount, 3u);

    EXPECT_TRUE(TextureFormat::ComputeCompleteMipCount(
        TextureDimension::TextureCube,
        s_ExpectedDualCount,
        s_ExpectedDualCount,
        1u,
        mipCount
    ));
    EXPECT_EQ(mipCount, s_ExpectedDualCount);

    EXPECT_TRUE(TextureFormat::ComputeCompleteMipCount(
        TextureDimension::Texture3D,
        4u,
        s_ExpectedDualCount,
        3u,
        mipCount
    ));
    EXPECT_EQ(mipCount, 3u);

    u32 blocksX = 0u;
    u32 blocksY = 0u;
    u64 planeByteCount = 0u;
    EXPECT_TRUE(TextureFormat::ComputeMipPlaneBlockLayout(TexturePayloadFormat::UastcLdr4x4, 7u, 5u, blocksX, blocksY, planeByteCount));
    EXPECT_EQ(blocksX, s_ExpectedDualCount);
    EXPECT_EQ(blocksY, s_ExpectedDualCount);
    EXPECT_EQ(planeByteCount, 64u);

    EXPECT_TRUE(TextureFormat::ComputeMipPlaneBlockLayout(
        TexturePayloadFormat::UastcHdr4x4,
        4u,
        s_ExpectedDualCount,
        blocksX,
        blocksY,
        planeByteCount
    ));
    EXPECT_EQ(blocksX, 1u);
    EXPECT_EQ(blocksY, 1u);
    EXPECT_EQ(planeByteCount, 16u);
}


TEST(AssetsGraphics, TextureCodecRoundTripPreservesCurrentUastcLdrMipPayload){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    NWB::Impl::Texture texture(testArena.arena, Name("project/textures/checker"));
    texture.setPayload(
        NWB::Impl::TextureColorSpace::Srgb,
        true,
        7u,
        5u,
        MakeTextureTestMipLevels(testArena),
        MakeTextureTestUastcPayload(testArena),
        TextureDimension::Texture2D,
        1u,
        TexturePayloadFormat::UastcLdr4x4,
        TextureAlphaMode::EmbeddedLdr,
        TextureFormat::s_OpaqueAlphaUnorm8
    );
    ASSERT_TRUE(texture.validatePayload());

    NWB::Impl::TextureAssetCodec codec;
    NWB::Core::Assets::AssetBytes binary = AssetsGraphicsFixture::MakeAssetBytes(testArena);
    ASSERT_TRUE(codec.serialize(texture, binary));
    ASSERT_EQ(binary.size(), sizeof(NWB::Impl::TextureBinaryPayload::HeaderBinary) + 3u * sizeof(NWB::Impl::TextureBinaryPayload::MipLevelBinary) + 96u);
    usize headerCursor = 0u;
    NWB::Impl::TextureBinaryPayload::HeaderBinary header;
    ASSERT_TRUE(ReadPOD(binary, headerCursor, header));
    EXPECT_EQ(header.magic, NWB::Impl::TextureBinaryPayload::s_TextureMagic);
    EXPECT_EQ(header.version, NWB::Impl::TextureBinaryPayload::s_TextureVersion);
    EXPECT_EQ(header.colorSpace, static_cast<u32>(NWB::Impl::TextureColorSpace::Srgb));
    EXPECT_EQ(header.dimension, static_cast<u32>(TextureDimension::Texture2D));
    EXPECT_EQ(header.width, 7u);
    EXPECT_EQ(header.height, 5u);
    EXPECT_EQ(header.depth, 1u);
    EXPECT_EQ(header.mipCount, 3u);
    EXPECT_EQ(header.alphaInfo, static_cast<u32>(TextureAlphaMode::EmbeddedLdr)
        | (TextureFormat::s_OpaqueAlphaUnorm8 << NWB::Impl::TextureBinaryPayload::s_AlphaInfoConstantShift)
    );
    EXPECT_EQ(header.payloadFormat, static_cast<u32>(TexturePayloadFormat::UastcLdr4x4));
    EXPECT_EQ(header.payloadByteCount, 96u);

    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    ASSERT_TRUE(codec.deserialize(testArena.arena, texture.virtualPath(), binary, loadedAsset));
    ASSERT_NE(loadedAsset.get(), nullptr);

    const NWB::Impl::Texture& loadedTexture = static_cast<const NWB::Impl::Texture&>(*loadedAsset);
    EXPECT_EQ(loadedTexture.colorSpace(), NWB::Impl::TextureColorSpace::Srgb);
    EXPECT_EQ(loadedTexture.payloadFormat(), TexturePayloadFormat::UastcLdr4x4);
    EXPECT_EQ(loadedTexture.alphaMode(), TextureAlphaMode::EmbeddedLdr);
    EXPECT_TRUE(loadedTexture.hasAlpha());
    EXPECT_EQ(loadedTexture.width(), 7u);
    EXPECT_EQ(loadedTexture.height(), 5u);
    EXPECT_EQ(loadedTexture.dimension(), TextureDimension::Texture2D);
    EXPECT_EQ(loadedTexture.depth(), 1u);
    ASSERT_EQ(loadedTexture.mipLevels().size(), 3u);
    EXPECT_EQ(loadedTexture.mipLevels()[0u].sliceCount, 1u);
    EXPECT_EQ(loadedTexture.mipLevels()[0u].sizeBytes, 64u);
    EXPECT_EQ(loadedTexture.mipLevels()[1u].offsetBytes, 64u);
    EXPECT_EQ(loadedTexture.mipLevels()[s_ThirdElementIndex].offsetBytes, 80u);
    ASSERT_EQ(loadedTexture.payloadBytes().size(), 96u);
    for(usize index = 0u; index < loadedTexture.payloadBytes().size(); ++index)
        EXPECT_EQ(loadedTexture.payloadBytes()[index], static_cast<u8>(index));

    EXPECT_EQ(logger.errorCount(), 0u);
}

TEST(AssetsGraphics, TextureCodecRoundTripPreservesV3UastcHdrAndTrailingAlphaPayload){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    NWB::Impl::Texture texture(testArena.arena, Name("project/textures/bright"));
    texture.setPayload(
        NWB::Impl::TextureColorSpace::Linear,
        true,
        4u,
        s_ExpectedDualCount,
        MakeTextureHdrTestMipLevels(testArena),
        MakeTextureTestHdrPayload(testArena),
        TextureDimension::Texture2D,
        1u,
        TexturePayloadFormat::UastcHdr4x4,
        TextureAlphaMode::SeparateUastcLdr4x4,
        TextureFormat::s_OpaqueAlphaUnorm8
    );
    ASSERT_TRUE(texture.validatePayload());

    NWB::Impl::TextureAssetCodec codec;
    NWB::Core::Assets::AssetBytes binary = AssetsGraphicsFixture::MakeAssetBytes(testArena);
    ASSERT_TRUE(codec.serialize(texture, binary));
    ASSERT_EQ(binary.size(), sizeof(NWB::Impl::TextureBinaryPayload::HeaderBinary) + 3u * sizeof(NWB::Impl::TextureBinaryPayload::MipLevelBinary) + 96u);

    usize headerCursor = 0u;
    NWB::Impl::TextureBinaryPayload::HeaderBinary header;
    ASSERT_TRUE(ReadPOD(binary, headerCursor, header));
    EXPECT_EQ(header.version, NWB::Impl::TextureBinaryPayload::s_TextureVersion);
    EXPECT_EQ(header.payloadFormat, static_cast<u32>(TexturePayloadFormat::UastcHdr4x4));
    EXPECT_EQ(
        header.alphaInfo,
        static_cast<u32>(TextureAlphaMode::SeparateUastcLdr4x4)
            | (255u << NWB::Impl::TextureBinaryPayload::s_AlphaInfoConstantShift)
    );
    EXPECT_EQ(header.payloadByteCount, 96u);

    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    ASSERT_TRUE(codec.deserialize(testArena.arena, texture.virtualPath(), binary, loadedAsset));
    ASSERT_NE(loadedAsset.get(), nullptr);

    const NWB::Impl::Texture& loadedTexture = static_cast<const NWB::Impl::Texture&>(*loadedAsset);
    EXPECT_EQ(loadedTexture.colorSpace(), NWB::Impl::TextureColorSpace::Linear);
    EXPECT_EQ(loadedTexture.payloadFormat(), TexturePayloadFormat::UastcHdr4x4);
    EXPECT_EQ(loadedTexture.alphaMode(), TextureAlphaMode::SeparateUastcLdr4x4);
    EXPECT_EQ(loadedTexture.alphaConstantUnorm8(), 255u);
    EXPECT_TRUE(loadedTexture.hasAlpha());
    ASSERT_EQ(loadedTexture.mipLevels().size(), 3u);
    EXPECT_EQ(loadedTexture.mipLevels()[0u].blockCountX, 1u);
    EXPECT_EQ(loadedTexture.mipLevels()[0u].blockCountY, 1u);
    EXPECT_EQ(loadedTexture.mipLevels()[s_ThirdElementIndex].offsetBytes, 32u);
    EXPECT_EQ(loadedTexture.mipLevels()[s_ThirdElementIndex].sizeBytes, 16u);
    EXPECT_EQ(loadedTexture.primaryPayloadByteCount(), 48u);
    ASSERT_EQ(loadedTexture.payloadBytes().size(), 96u);
    ASSERT_NE(loadedTexture.alphaUastcBlocks(), nullptr);
    for(usize index = 0u; index < 48u; ++index)
        EXPECT_EQ(loadedTexture.payloadBytes()[index], static_cast<u8>(0x40u + index));
    for(usize index = 0u; index < 48u; ++index)
        EXPECT_EQ(loadedTexture.alphaUastcBlocks()[index], static_cast<u8>(0x80u + index));

    EXPECT_EQ(logger.errorCount(), 0u);
}

TEST(AssetsGraphics, TextureCodecRoundTripsCubeAndVolumePayloads){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    NWB::Impl::TextureAssetCodec codec;

    {
        NWB::Impl::Texture cube(testArena.arena, Name("project/textures/sky"));
        cube.setPayload(
            NWB::Impl::TextureColorSpace::Srgb,
            false,
            s_ExpectedDualCount,
            s_ExpectedDualCount,
            MakeTextureCubeTestMipLevels(testArena),
            MakeTextureTestUastcPayload(testArena, 192u, 0x40u),
            TextureDimension::TextureCube,
            1u,
            TexturePayloadFormat::UastcLdr4x4,
            TextureAlphaMode::Opaque,
            TextureFormat::s_OpaqueAlphaUnorm8
        );
        ASSERT_TRUE(cube.validatePayload());

        NWB::Core::Assets::AssetBytes binary = AssetsGraphicsFixture::MakeAssetBytes(testArena);
        ASSERT_TRUE(codec.serialize(cube, binary));
        ASSERT_EQ(binary.size(), sizeof(NWB::Impl::TextureBinaryPayload::HeaderBinary) + s_ExpectedDualCount * sizeof(NWB::Impl::TextureBinaryPayload::MipLevelBinary) + 192u);

        UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
        ASSERT_TRUE(codec.deserialize(testArena.arena, cube.virtualPath(), binary, loadedAsset));
        ASSERT_NE(loadedAsset.get(), nullptr);
        const NWB::Impl::Texture& loadedCube = static_cast<const NWB::Impl::Texture&>(*loadedAsset);
        EXPECT_EQ(loadedCube.dimension(), TextureDimension::TextureCube);
        EXPECT_EQ(loadedCube.depth(), 1u);
        ASSERT_EQ(loadedCube.mipLevels().size(), s_ExpectedDualCount);
        EXPECT_EQ(loadedCube.mipLevels()[0u].sliceCount, 6u);
        EXPECT_EQ(loadedCube.mipLevels()[1u].sliceCount, 6u);
        EXPECT_EQ(loadedCube.payloadBytes().size(), 192u);
    }

    {
        NWB::Impl::Texture volume(testArena.arena, Name("project/textures/fog"));
        volume.setPayload(
            NWB::Impl::TextureColorSpace::Linear,
            true,
            4u,
            s_ExpectedDualCount,
            MakeTextureVolumeTestMipLevels(testArena),
            MakeTextureTestUastcPayload(testArena, 80u, 0x80u),
            TextureDimension::Texture3D,
            3u,
            TexturePayloadFormat::UastcLdr4x4,
            TextureAlphaMode::EmbeddedLdr,
            TextureFormat::s_OpaqueAlphaUnorm8
        );
        ASSERT_TRUE(volume.validatePayload());

        NWB::Core::Assets::AssetBytes binary = AssetsGraphicsFixture::MakeAssetBytes(testArena);
        ASSERT_TRUE(codec.serialize(volume, binary));
        ASSERT_EQ(binary.size(), sizeof(NWB::Impl::TextureBinaryPayload::HeaderBinary) + 3u * sizeof(NWB::Impl::TextureBinaryPayload::MipLevelBinary) + 80u);

        UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
        ASSERT_TRUE(codec.deserialize(testArena.arena, volume.virtualPath(), binary, loadedAsset));
        ASSERT_NE(loadedAsset.get(), nullptr);
        const NWB::Impl::Texture& loadedVolume = static_cast<const NWB::Impl::Texture&>(*loadedAsset);
        EXPECT_EQ(loadedVolume.dimension(), TextureDimension::Texture3D);
        EXPECT_EQ(loadedVolume.depth(), 3u);
        ASSERT_EQ(loadedVolume.mipLevels().size(), 3u);
        EXPECT_EQ(loadedVolume.mipLevels()[0u].sliceCount, 3u);
        EXPECT_EQ(loadedVolume.mipLevels()[1u].sliceCount, 1u);
        EXPECT_EQ(loadedVolume.mipLevels()[s_ThirdElementIndex].sliceCount, 1u);
        EXPECT_EQ(loadedVolume.payloadBytes().size(), 80u);
    }

    EXPECT_EQ(logger.errorCount(), 0u);
}

TEST(AssetsGraphics, TextureCodecRejectsUnsupportedBinaryVersions){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(
        logger, NWB::Core::Common::LoggerBreakPolicy::BreakOnFatal
    );

    TestArena testArena;
    NWB::Impl::Texture texture(testArena.arena, Name("project/textures/version_admission"));
    texture.setPayload(
        NWB::Impl::TextureColorSpace::Srgb,
        true,
        7u,
        5u,
        MakeTextureTestMipLevels(testArena),
        MakeTextureTestUastcPayload(testArena),
        TextureDimension::Texture2D,
        1u,
        TexturePayloadFormat::UastcLdr4x4,
        TextureAlphaMode::EmbeddedLdr,
        TextureFormat::s_OpaqueAlphaUnorm8
    );
    NWB::Impl::TextureAssetCodec codec;
    NWB::Core::Assets::AssetBytes binary = AssetsGraphicsFixture::MakeAssetBytes(testArena);
    ASSERT_TRUE(codec.serialize(texture, binary));
    for(const u32 version : { 0u, 1u, 2u, NWB::Impl::TextureBinaryPayload::s_TextureVersion + 1u }){
        GLB_MEMCPY(binary.data() + offsetof(NWB::Impl::TextureBinaryPayload::HeaderBinary, version), sizeof(version), &version, sizeof(version));
        UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
        EXPECT_FALSE(codec.deserialize(testArena.arena, texture.virtualPath(), binary, loadedAsset)) << version;
        EXPECT_EQ(loadedAsset.get(), nullptr);
    }
    EXPECT_EQ(logger.errorCount(), 4u);
    EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT("unsupported texture payload version")));

    const u32 currentVersion = NWB::Impl::TextureBinaryPayload::s_TextureVersion;
    GLB_MEMCPY(binary.data() + offsetof(NWB::Impl::TextureBinaryPayload::HeaderBinary, version), sizeof(currentVersion), &currentVersion, sizeof(currentVersion));
    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    EXPECT_TRUE(codec.deserialize(testArena.arena, texture.virtualPath(), binary, loadedAsset));
}

TEST(AssetsGraphics, TexturePayloadRejectsMismatchedExplicitAlphaMode){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(
        logger, NWB::Core::Common::LoggerBreakPolicy::BreakOnFatal
    );

    TestArena testArena;
    NWB::Impl::Texture texture(testArena.arena, Name("project/textures/invalid_alpha_mode"));
    texture.setPayload(
        NWB::Impl::TextureColorSpace::Srgb,
        true,
        7u,
        5u,
        MakeTextureTestMipLevels(testArena),
        MakeTextureTestUastcPayload(testArena),
        TextureDimension::Texture2D,
        1u,
        TexturePayloadFormat::UastcLdr4x4,
        TextureAlphaMode::Opaque,
        TextureFormat::s_OpaqueAlphaUnorm8
    );
    EXPECT_FALSE(texture.validatePayload());
    EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT("inconsistent alpha metadata")));
}

TEST(AssetsGraphics, TextureCookerBuildsCookedAssetFromTexConverterMetadata){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
    ASSERT_TRUE(AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(
        testArena,
        "texture_cooker_round_trip",
        root,
        outputDirectory
    ));

    const Path assetRoot = root / "assets";
    const Path textureDirectory = assetRoot / "textures";
    const Path metadataPath = textureDirectory / "checker.nwb";
    const Path dataPath = textureDirectory / "checker.tex";
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(metadataPath, s_TextureTestMetadata));
    ASSERT_TRUE(WriteBinaryFile(dataPath, MakeTextureTestUastcPayload(testArena)));
    ASSERT_TRUE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }));

    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    ASSERT_TRUE(AssetsGraphicsFixture::LoadCookedAsset<NWB::Impl::TextureAssetCodec>(
        testArena,
        outputDirectory,
        Name("project/textures/checker"),
        loadedAsset,
        1u
    ));
    ASSERT_NE(loadedAsset.get(), nullptr);

    const NWB::Impl::Texture& texture = static_cast<const NWB::Impl::Texture&>(*loadedAsset);
    EXPECT_EQ(texture.colorSpace(), NWB::Impl::TextureColorSpace::Srgb);
    EXPECT_TRUE(texture.hasAlpha());
    EXPECT_EQ(texture.width(), 7u);
    EXPECT_EQ(texture.height(), 5u);
    ASSERT_EQ(texture.mipLevels().size(), 3u);
    EXPECT_EQ(texture.payloadBytes().size(), 96u);

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
    EXPECT_EQ(logger.errorCount(), 0u);
}

TEST(AssetsGraphics, TextureCookerBuildsUastcHdrAssetWithTrailingAlphaFromMetadata){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
    ASSERT_TRUE(AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(
        testArena,
        "texture_hdr_cooker_round_trip",
        root,
        outputDirectory
    ));

    const Path assetRoot = root / "assets";
    const Path textureDirectory = assetRoot / "textures";
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(textureDirectory / "bright.nwb", s_TextureHdrTestMetadata));
    ASSERT_TRUE(WriteBinaryFile(textureDirectory / "bright.tex", MakeTextureTestHdrPayload(testArena)));
    ASSERT_TRUE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }));

    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    ASSERT_TRUE(AssetsGraphicsFixture::LoadCookedAsset<NWB::Impl::TextureAssetCodec>(
        testArena,
        outputDirectory,
        Name("project/textures/bright"),
        loadedAsset,
        1u
    ));
    ASSERT_NE(loadedAsset.get(), nullptr);

    const NWB::Impl::Texture& texture = static_cast<const NWB::Impl::Texture&>(*loadedAsset);
    EXPECT_EQ(texture.colorSpace(), NWB::Impl::TextureColorSpace::Linear);
    EXPECT_EQ(texture.payloadFormat(), TexturePayloadFormat::UastcHdr4x4);
    EXPECT_EQ(texture.alphaMode(), TextureAlphaMode::SeparateUastcLdr4x4);
    EXPECT_EQ(texture.alphaConstantUnorm8(), 255u);
    EXPECT_TRUE(texture.hasAlpha());
    EXPECT_EQ(texture.width(), 4u);
    EXPECT_EQ(texture.height(), s_ExpectedDualCount);
    ASSERT_EQ(texture.mipLevels().size(), 3u);
    EXPECT_EQ(texture.mipLevels()[0u].blockCountX, 1u);
    EXPECT_EQ(texture.mipLevels()[0u].blockCountY, 1u);
    EXPECT_EQ(texture.mipLevels()[s_ThirdElementIndex].offsetBytes, 32u);
    EXPECT_EQ(texture.mipLevels()[s_ThirdElementIndex].sizeBytes, 16u);
    EXPECT_EQ(texture.primaryPayloadByteCount(), 48u);
    EXPECT_EQ(texture.payloadBytes().size(), 96u);
    ASSERT_NE(texture.alphaUastcBlocks(), nullptr);
    EXPECT_EQ(texture.alphaUastcBlocks()[0u], 0x80u);

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
    EXPECT_EQ(logger.errorCount(), 0u);
}

TEST(AssetsGraphics, TextureCookerBuildsCubeAndVolumeAssetsFromCurrentMetadata){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
    ASSERT_TRUE(AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(
        testArena,
        "texture_cube_volume_cooker_round_trip",
        root,
        outputDirectory
    ));

    const Path assetRoot = root / "assets";
    const Path textureDirectory = assetRoot / "textures";
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(textureDirectory / "sky.nwb", s_TextureCubeTestMetadata));
    ASSERT_TRUE(WriteBinaryFile(textureDirectory / "sky.tex", MakeTextureTestUastcPayload(testArena, 192u, 0x40u)));
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(textureDirectory / "fog.nwb", s_TextureVolumeTestMetadata));
    ASSERT_TRUE(WriteBinaryFile(textureDirectory / "fog.tex", MakeTextureTestUastcPayload(testArena, 80u, 0x80u)));
    ASSERT_TRUE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }));

    UniquePtr<NWB::Core::Assets::IAsset> cubeAsset;
    ASSERT_TRUE(AssetsGraphicsFixture::LoadCookedAsset<NWB::Impl::TextureAssetCodec>(
        testArena,
        outputDirectory,
        Name("project/textures/sky"),
        cubeAsset,
        s_ExpectedDualCount
    ));
    ASSERT_NE(cubeAsset.get(), nullptr);
    const NWB::Impl::Texture& cube = static_cast<const NWB::Impl::Texture&>(*cubeAsset);
    EXPECT_EQ(cube.dimension(), TextureDimension::TextureCube);
    EXPECT_EQ(cube.depth(), 1u);
    ASSERT_EQ(cube.mipLevels().size(), s_ExpectedDualCount);
    EXPECT_EQ(cube.mipLevels()[0u].sliceCount, 6u);

    UniquePtr<NWB::Core::Assets::IAsset> volumeAsset;
    ASSERT_TRUE(AssetsGraphicsFixture::LoadCookedAsset<NWB::Impl::TextureAssetCodec>(
        testArena,
        outputDirectory,
        Name("project/textures/fog"),
        volumeAsset,
        s_ExpectedDualCount
    ));
    ASSERT_NE(volumeAsset.get(), nullptr);
    const NWB::Impl::Texture& volume = static_cast<const NWB::Impl::Texture&>(*volumeAsset);
    EXPECT_EQ(volume.dimension(), TextureDimension::Texture3D);
    EXPECT_EQ(volume.depth(), 3u);
    ASSERT_EQ(volume.mipLevels().size(), 3u);
    EXPECT_EQ(volume.mipLevels()[0u].sliceCount, 3u);
    EXPECT_EQ(volume.payloadBytes().size(), 80u);

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
    EXPECT_EQ(logger.errorCount(), 0u);
}

TEST(AssetsGraphics, TextureCookerRejectsObsoleteAndDerivedMetadata){
    struct ObsoleteField{
        AStringView assignment;
        TStringView diagnostic;
    };
    constexpr Array<ObsoleteField, 15u> obsoleteFields = {{
        { "asset.version = 1;\n", GLB_TEXT("unsupported asset field 'version'") },
        { "asset.uastc_spec_revision = \"b624c07ad3c659e7b0f0badcb36e9a6b8820a99d\";\n",
            GLB_TEXT("unsupported asset field 'uastc_spec_revision'") },
        { "asset.uastc_hdr_spec_revision = \"b624c07ad3c659e7b0f0badcb36e9a6b8820a99d\";\n",
            GLB_TEXT("unsupported asset field 'uastc_hdr_spec_revision'") },
        { "asset.alpha_uastc_spec_revision = \"b624c07ad3c659e7b0f0badcb36e9a6b8820a99d\";\n",
            GLB_TEXT("unsupported asset field 'alpha_uastc_spec_revision'") },
        { "asset.schema_version = 1;\n", GLB_TEXT("unsupported asset field 'schema_version'") },
        { "asset.revision = 1;\n", GLB_TEXT("unsupported asset field 'revision'") },
        { "asset.block_width = 4;\n", GLB_TEXT("unsupported asset field 'block_width'") },
        { "asset.block_height = 4;\n", GLB_TEXT("unsupported asset field 'block_height'") },
        { "asset.bytes_per_block = 16;\n", GLB_TEXT("unsupported asset field 'bytes_per_block'") },
        { "asset.payload_layout = \"mip_major_slice_major_blocks\";\n", GLB_TEXT("unsupported asset field 'payload_layout'") },
        { "asset.mip_address_mode = \"clamp\";\n", GLB_TEXT("unsupported asset field 'mip_address_mode'") },
        { "asset.mip_count = 3;\n", GLB_TEXT("unsupported asset field 'mip_count'") },
        { "asset.mips = [];\n", GLB_TEXT("unsupported asset field 'mips'") },
        { "asset.alpha_payload_offset_bytes = 48;\n", GLB_TEXT("unsupported asset field 'alpha_payload_offset_bytes'") },
        { "asset.alpha_payload_byte_count = 48;\n", GLB_TEXT("unsupported asset field 'alpha_payload_byte_count'") },
    }};
    for(const AStringView source : { s_TextureTestMetadata, s_TextureHdrTestMetadata }){
        for(const ObsoleteField& field : obsoleteFields){
            CapturingLogger logger;
            NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(
                logger, NWB::Core::Common::LoggerBreakPolicy::BreakOnFatal
            );

            TestArena testArena;
            NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_CodecScratchArena);
            ::AString<NWB::Core::Alloc::ScratchArena> metadata(scratchArena);
            metadata.reserve(source.size() + field.assignment.size());
            metadata.append(source);
            metadata.append(field.assignment);
            NWB::Core::Metascript::Document document(testArena.arena);
            ASSERT_TRUE(document.parse(metadata));

            const Path assetRoot = AssetsGraphicsFixture::AssetsGraphicsTestCaseRoot(testArena, "texture_obsolete_metadata") / "assets";
            const Path metadataPath = assetRoot / "textures" / "checker.nwb";
            NWB::Impl::TextureCookEntry entry(testArena.arena);
            EXPECT_FALSE(NWB::Impl::ParseTextureCookMetadata(
                assetRoot,
                "project",
                metadataPath,
                document,
                entry,
                scratchArena
            )) << field.assignment;
            EXPECT_TRUE(logger.sawErrorContaining(field.diagnostic)) << field.assignment;
        }
    }
}

TEST(AssetsGraphics, TextureCookerRejectsMissingMalformedAndUnsupportedFormats){
    struct InvalidFormat{
        AStringView assignment;
        TStringView diagnostic;
    };
    constexpr Array<InvalidFormat, 4u> invalidFormats = {{
        { "", GLB_TEXT("field 'format' is required") },
        { "asset.format = 17;", GLB_TEXT("field 'format' must be a string") },
        { "asset.format = \"\";", GLB_TEXT("field 'format' must not be empty") },
        { "asset.format = \"uastc_hdr_6x6\";", GLB_TEXT("field 'format' must be 'uastc_ldr_4x4' or 'uastc_hdr_4x4'") },
    }};
    constexpr AStringView formatAssignment = "asset.format = \"uastc_ldr_4x4\";";
    for(const InvalidFormat& invalidFormat : invalidFormats){
        CapturingLogger logger;
        NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(
            logger, NWB::Core::Common::LoggerBreakPolicy::BreakOnFatal
        );

        TestArena testArena;
        NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_CodecScratchArena);
        ::AString<NWB::Core::Alloc::ScratchArena> metadata(s_TextureTestMetadata, scratchArena);
        const usize formatPosition = metadata.find(formatAssignment);
        ASSERT_NE(formatPosition, AString::npos);
        metadata.replace(formatPosition, formatAssignment.size(), invalidFormat.assignment);
        NWB::Core::Metascript::Document document(testArena.arena);
        ASSERT_TRUE(document.parse(metadata));

        const Path assetRoot = AssetsGraphicsFixture::AssetsGraphicsTestCaseRoot(testArena, "texture_invalid_format") / "assets";
        const Path metadataPath = assetRoot / "textures" / "checker.nwb";
        NWB::Impl::TextureCookEntry entry(testArena.arena);
        EXPECT_FALSE(NWB::Impl::ParseTextureCookMetadata(
            assetRoot,
            "project",
            metadataPath,
            document,
            entry,
            scratchArena
        )) << invalidFormat.assignment;
        EXPECT_TRUE(logger.sawErrorContaining(invalidFormat.diagnostic)) << invalidFormat.assignment;
    }
}

TEST(AssetsGraphics, TextureCookerRejectsFieldsDerivedFromFormatAndDimension){
    struct DerivedField{
        AStringView source;
        AStringView assignment;
        TStringView diagnostic;
    };
    constexpr Array<DerivedField, 7u> derivedFields = {{
        { s_TextureTestMetadata, "asset.depth = 1;\n", GLB_TEXT("unsupported asset field 'depth'") },
        { s_TextureCubeTestMetadata, "asset.depth = 1;\n", GLB_TEXT("unsupported asset field 'depth'") },
        { s_TextureHdrTestMetadata, "asset.color_space = \"linear\";\n", GLB_TEXT("unsupported asset field 'color_space'") },
        { s_TextureHdrTestMetadata, "asset.has_alpha = 1;\n", GLB_TEXT("unsupported asset field 'has_alpha'") },
        { s_TextureHdrTestMetadata, "asset.alpha_constant_unorm8 = 128;\n", GLB_TEXT("unsupported asset field 'alpha_constant_unorm8'") },
        { s_TextureTestMetadata, "asset.alpha_mode = \"opaque\";\n", GLB_TEXT("unsupported asset field 'alpha_mode'") },
        { s_TextureTestMetadata, "asset.alpha_constant_unorm8 = 128;\n", GLB_TEXT("unsupported asset field 'alpha_constant_unorm8'") },
    }};
    for(const DerivedField& field : derivedFields){
        CapturingLogger logger;
        NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(
            logger, NWB::Core::Common::LoggerBreakPolicy::BreakOnFatal
        );

        TestArena testArena;
        NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_CodecScratchArena);
        ::AString<NWB::Core::Alloc::ScratchArena> metadata(scratchArena);
        metadata.reserve(field.source.size() + field.assignment.size());
        metadata.append(field.source);
        metadata.append(field.assignment);
        NWB::Core::Metascript::Document document(testArena.arena);
        ASSERT_TRUE(document.parse(metadata));

        const Path assetRoot = AssetsGraphicsFixture::AssetsGraphicsTestCaseRoot(testArena, "texture_derived_fields") / "assets";
        const Path metadataPath = assetRoot / "textures" / "checker.nwb";
        NWB::Impl::TextureCookEntry entry(testArena.arena);
        EXPECT_FALSE(NWB::Impl::ParseTextureCookMetadata(assetRoot, "project", metadataPath, document, entry, scratchArena));
        EXPECT_TRUE(logger.sawErrorContaining(field.diagnostic)) << field.assignment;
    }
}

TEST(AssetsGraphics, TextureCookerRequiresExactDerivedSidecarSize){
    struct SidecarCase{
        AStringView metadata;
        AStringView filename;
        usize byteCount;
    };
    constexpr Array<SidecarCase, 4u> sidecarCases = {{
        { s_TextureTestMetadata, "checker.tex", 96u },
        { s_TextureHdrTestMetadata, "bright.tex", 96u },
        { s_TextureCubeTestMetadata, "sky.tex", 192u },
        { s_TextureVolumeTestMetadata, "fog.tex", 80u },
    }};
    for(const SidecarCase& sidecarCase : sidecarCases){
        TestArena testArena;
        Path root(testArena.arena);
        ASSERT_TRUE(AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "texture_derived_sidecar_size", root));
        const Path assetRoot = root / "assets";
        const Path metadataPath = assetRoot / "textures" / "checker.nwb";
        ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(metadataPath, sidecarCase.metadata));
        NWB::Core::Metascript::Document document(testArena.arena);
        ASSERT_TRUE(document.parse(sidecarCase.metadata));

        for(const usize byteCount : { sidecarCase.byteCount - 1u, sidecarCase.byteCount, sidecarCase.byteCount + 1u }){
            CapturingLogger logger;
            NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(
                logger, NWB::Core::Common::LoggerBreakPolicy::BreakOnFatal
            );

            ASSERT_TRUE(WriteBinaryFile(assetRoot / "textures" / sidecarCase.filename, MakeTextureTestUastcPayload(testArena, byteCount)));
            NWB::Impl::TextureCookEntry entry(testArena.arena);
            NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_CodecScratchArena);
            const bool parsed = NWB::Impl::ParseTextureCookMetadata(assetRoot, "project", metadataPath, document, entry, scratchArena);
            EXPECT_EQ(parsed, byteCount == sidecarCase.byteCount) << sidecarCase.filename << ": " << byteCount;
            if(parsed){
                NWB::Impl::Texture texture(testArena.arena, NAME_NONE);
                EXPECT_TRUE(NWB::Impl::BuildTextureAsset(entry, texture));
                EXPECT_EQ(logger.errorCount(), 0u);
            }
            else
                EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT("sidecar size does not match the derived mip and alpha layout")));
        }
        ErrorCode errorCode;
        EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
    }
}

TEST(AssetsGraphics, TextureCookerInfersHdrAlphaAndChecksConstantBounds){
    struct AlphaCase{
        AStringView assignment;
        bool valid;
        TextureAlphaMode::Enum alphaMode;
        u8 alphaConstant;
    };
    constexpr Array<AlphaCase, 4u> alphaCases = {{
        { "asset.alpha_mode = \"opaque\";", true, TextureAlphaMode::Opaque, 255u },
        { "asset.alpha_mode = \"constant_unorm8\"; asset.alpha_constant_unorm8 = 0;", true, TextureAlphaMode::ConstantUnorm8, 0u },
        { "asset.alpha_mode = \"constant_unorm8\"; asset.alpha_constant_unorm8 = 254;", true, TextureAlphaMode::ConstantUnorm8, 254u },
        { "asset.alpha_mode = \"constant_unorm8\"; asset.alpha_constant_unorm8 = 255;", false, TextureAlphaMode::ConstantUnorm8, 255u },
    }};
    constexpr AStringView alphaAssignment = "asset.alpha_mode = \"uastc_ldr_4x4\";";
    TestArena testArena;
    Path root(testArena.arena);
    ASSERT_TRUE(AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "texture_hdr_alpha_inference", root));
    const Path assetRoot = root / "assets";
    const Path metadataPath = assetRoot / "textures" / "bright.nwb";
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(metadataPath, s_TextureHdrTestMetadata));
    ASSERT_TRUE(WriteBinaryFile(assetRoot / "textures" / "bright.tex", MakeTextureTestUastcPayload(testArena, 48u)));
    for(const AlphaCase& alphaCase : alphaCases){
        CapturingLogger logger;
        NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(
            logger, NWB::Core::Common::LoggerBreakPolicy::BreakOnFatal
        );

        NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_CodecScratchArena);
        ::AString<NWB::Core::Alloc::ScratchArena> metadata(s_TextureHdrTestMetadata, scratchArena);
        const usize alphaPosition = metadata.find(alphaAssignment);
        ASSERT_NE(alphaPosition, AString::npos);
        metadata.replace(alphaPosition, alphaAssignment.size(), alphaCase.assignment);
        NWB::Core::Metascript::Document document(testArena.arena);
        ASSERT_TRUE(document.parse(metadata));
        NWB::Impl::TextureCookEntry entry(testArena.arena);
        const bool parsed = NWB::Impl::ParseTextureCookMetadata(assetRoot, "project", metadataPath, document, entry, scratchArena);
        EXPECT_EQ(parsed, alphaCase.valid) << alphaCase.assignment;
        if(parsed){
            NWB::Impl::Texture texture(testArena.arena, NAME_NONE);
            ASSERT_TRUE(NWB::Impl::BuildTextureAsset(entry, texture));
            EXPECT_EQ(texture.colorSpace(), NWB::Impl::TextureColorSpace::Linear);
            EXPECT_EQ(texture.hasAlpha(), alphaCase.alphaMode != TextureAlphaMode::Opaque);
            EXPECT_EQ(texture.alphaMode(), alphaCase.alphaMode);
            EXPECT_EQ(texture.alphaConstantUnorm8(), alphaCase.alphaConstant);
            EXPECT_EQ(logger.errorCount(), 0u);
        }
        else
            EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT("field 'alpha_constant_unorm8' is outside the supported range")));
    }
    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
}

TEST(AssetsGraphics, TextureCookerRejectsDerivedMipAndAlphaSizeOverflow){
    struct OverflowCase{
        AStringView fields;
        TStringView diagnostic;
    };
    constexpr Array<OverflowCase, 4u> overflowCases = {{
        {
            "asset.format = \"uastc_ldr_4x4\"; asset.color_space = \"linear\"; asset.has_alpha = 0;\n"
            "asset.dimension = \"2d\"; asset.width = 4294967295; asset.height = 4294967295;\n",
            GLB_TEXT("block grid exceeds runtime limits")
        },
        {
            "asset.format = \"uastc_ldr_4x4\"; asset.color_space = \"linear\"; asset.has_alpha = 0;\n"
            "asset.dimension = \"volume\"; asset.width = 2147483648; asset.height = 2147483648; asset.depth = 4;\n",
            GLB_TEXT("byte size overflows")
        },
        {
            "asset.format = \"uastc_ldr_4x4\"; asset.color_space = \"linear\"; asset.has_alpha = 0;\n"
            "asset.dimension = \"volume\"; asset.width = 1073741824; asset.height = 1073741824; asset.depth = 15;\n",
            GLB_TEXT("mip payload offsets overflow")
        },
        {
            "asset.format = \"uastc_hdr_4x4\"; asset.alpha_mode = \"uastc_ldr_4x4\";\n"
            "asset.dimension = \"cube\"; asset.width = 1073741824; asset.height = 1073741824;\n",
            GLB_TEXT("separate HDR alpha payload size overflows")
        },
    }};
    for(const OverflowCase& overflowCase : overflowCases){
        CapturingLogger logger;
        NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(
            logger, NWB::Core::Common::LoggerBreakPolicy::BreakOnFatal
        );

        TestArena testArena;
        NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_CodecScratchArena);
        ::AString<NWB::Core::Alloc::ScratchArena> metadata("texture asset;\n", scratchArena);
        metadata.append(overflowCase.fields);
        metadata.append("asset.data = \"overflow.tex\";\n");
        NWB::Core::Metascript::Document document(testArena.arena);
        ASSERT_TRUE(document.parse(metadata));
        const Path assetRoot = AssetsGraphicsFixture::AssetsGraphicsTestCaseRoot(testArena, "texture_derived_size_overflow") / "assets";
        const Path metadataPath = assetRoot / "textures" / "overflow.nwb";
        NWB::Impl::TextureCookEntry entry(testArena.arena);
        EXPECT_FALSE(NWB::Impl::ParseTextureCookMetadata(assetRoot, "project", metadataPath, document, entry, scratchArena));
        EXPECT_TRUE(logger.sawErrorContaining(overflowCase.diagnostic));
    }
}

TEST(AssetsGraphics, TextureCookerRejectsSidecarPathTraversal){
#if defined(GLB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    AString metadata(s_TextureTestMetadata);
    const usize dataPosition = metadata.find("checker.tex");
    ASSERT_NE(dataPosition, AString::npos);
    metadata.replace(dataPosition, 11u, "../checker.tex");

    NWB::Core::Metascript::Document document(testArena.arena);
    ASSERT_TRUE(document.parse(AStringView(metadata.data(), metadata.size())));

    const Path assetRoot = AssetsGraphicsFixture::AssetsGraphicsTestCaseRoot(testArena, "texture_path_traversal") / "assets";
    const Path metadataPath = assetRoot / "textures" / "checker.nwb";
    NWB::Impl::TextureCookEntry entry(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_CodecScratchArena);
    EXPECT_FALSE(NWB::Impl::ParseTextureCookMetadata(
        assetRoot,
        "project",
        metadataPath,
        document,
        entry,
        scratchArena
    ));
    EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT("sidecar filename without path components")));
#else
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

