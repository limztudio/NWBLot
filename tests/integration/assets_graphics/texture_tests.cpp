// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"

#include <impl/assets_texture/texture_mip_decoder.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_texture{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CapturingLogger = AssetsGraphicsFixture::CapturingLogger;
using Path = AssetsGraphicsFixture::Path;
using TestArena = AssetsGraphicsFixture::TestArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_TextureTestMetadata =
    "texture asset;\n\n"
    "asset.format = \"uastc_ldr_4x4\";\n"
    "asset.dimension = \"2d\";\n"
    "asset.resolution = [7, 5];\n"
    "asset.alpha_mode = \"embedded_ldr\";\n"
;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static NWB::Core::Assets::AssetBytes MakeTextureTestUastcPayload(TestArena& testArena){
    NWB::Core::Assets::AssetBytes bytes = AssetsGraphicsFixture::MakeAssetBytes(testArena);
    bytes.resize(96u);
    for(usize index = 0u; index < bytes.size(); ++index)
        bytes[index] = static_cast<u8>(index);
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


TEST(AssetsGraphics, TextureFormatRoundsPartialBlocksAndNonPowerOfTwoMips){
    const auto mipCount = TextureFormat::ComputeCompleteMipCount(TextureDimension::Texture2D, 7u, 5u, 1u);
    ASSERT_TRUE(mipCount);
    EXPECT_EQ(*mipCount, 3u);

    const auto ldrPlane = TextureFormat::ComputeMipPlaneBlockLayout(TexturePayloadFormat::UastcLdr4x4, 7u, 5u);
    ASSERT_TRUE(ldrPlane);
    EXPECT_EQ(ldrPlane->blocksX, s_ExpectedDualCount);
    EXPECT_EQ(ldrPlane->blocksY, s_ExpectedDualCount);
    EXPECT_EQ(ldrPlane->planeByteCount, 64u);

    const auto hdrPlane = TextureFormat::ComputeMipPlaneBlockLayout(TexturePayloadFormat::UastcHdr4x4, 4u, s_ExpectedDualCount);
    ASSERT_TRUE(hdrPlane);
    EXPECT_EQ(hdrPlane->blocksX, 1u);
    EXPECT_EQ(hdrPlane->blocksY, 1u);
    EXPECT_EQ(hdrPlane->planeByteCount, 16u);
}


TEST(AssetsGraphics, TextureCodecRejectsUnsupportedBinaryVersions){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger, NWB::Core::Common::LoggerBreakPolicy::BreakOnFatal);

    TestArena testArena;
    NWB::Impl::Texture texture(testArena.arena, Name("project/textures/version_admission"));
    texture.setPayload(7u, 5u, MakeTextureTestMipLevels(testArena), MakeTextureTestUastcPayload(testArena), TextureDimension::Texture2D, 1u, TexturePayloadFormat::UastcLdr4x4, TextureAlphaMode::EmbeddedLdr, TextureFormat::s_OpaqueAlphaUnorm8);
    NWB::Impl::TextureAssetCodec codec;
    NWB::Core::Assets::AssetBytes binary = AssetsGraphicsFixture::MakeAssetBytes(testArena);
    ASSERT_TRUE(codec.serialize(texture, binary));
    for(const u32 version : { 0u, 1u, 2u, 3u, ::TextureBinaryPayload::s_TextureVersion + 1u }){
        NWB_MEMCPY(binary.data() + offsetof(::TextureBinaryPayload::HeaderBinary, version), sizeof(version), &version, sizeof(version));
        EXPECT_FALSE(codec.deserialize(testArena.arena, texture.virtualPath(), binary)) << version;
    }
    EXPECT_EQ(logger.errorCount(), 5u);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("unsupported texture payload version")));

    const u32 currentVersion = ::TextureBinaryPayload::s_TextureVersion;
    NWB_MEMCPY(binary.data() + offsetof(::TextureBinaryPayload::HeaderBinary, version), sizeof(currentVersion), &currentVersion, sizeof(currentVersion));
    const auto loadedAssetResult = codec.deserialize(testArena.arena, texture.virtualPath(), binary);
    ASSERT_TRUE(loadedAssetResult);
}

TEST(AssetsGraphics, TexturePayloadRejectsAlphaTransportIncompatibleWithFormat){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger, NWB::Core::Common::LoggerBreakPolicy::BreakOnFatal);

    TestArena testArena;
    NWB::Impl::Texture texture(testArena.arena, Name("project/textures/invalid_alpha_mode"));
    texture.setPayload(7u, 5u, MakeTextureTestMipLevels(testArena), MakeTextureTestUastcPayload(testArena), TextureDimension::Texture2D, 1u, TexturePayloadFormat::UastcLdr4x4, TextureAlphaMode::ConstantUnorm8, 77u);
    EXPECT_FALSE(texture.validatePayload());
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("invalid alpha transport for its payload format")));
}


[[nodiscard]] static Expected<Core::Assets::AssetBytes> MakeTextureTestRuntimeBinary(TestArena& testArena){
    Impl::Texture texture(testArena.arena, Name("project/textures/prepared"));
    texture.setPayload(7u, 5u, MakeTextureTestMipLevels(testArena), MakeTextureTestUastcPayload(testArena), TextureDimension::Texture2D, 1u, TexturePayloadFormat::UastcLdr4x4, TextureAlphaMode::EmbeddedLdr, TextureFormat::s_OpaqueAlphaUnorm8);
    Impl::TextureAssetCodec codec;
    Core::Assets::AssetBytes bytes(testArena.arena);
    if(!codec.serialize(texture, bytes))
        return MakeUnexpected(Failure{});
    return bytes;
}

TEST(AssetsGraphics, TextureCookerPreservesImplicitRawSidecarWithoutEncodingItAgain){
    TestArena testArena;
    const auto original = MakeTextureTestUastcPayload(testArena);
    const auto rootResult = AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "texture_raw_pair_identity");
    ASSERT_TRUE(rootResult);
    const Path root = *rootResult;
    const Path assetRoot = root / "assets";
    const Path metadataPath = assetRoot / "textures" / "checker.nwb";
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(metadataPath, s_TextureTestMetadata));
    ASSERT_TRUE(WriteBinaryFile(assetRoot / "textures" / "checker.tex", original));
    Core::Metascript::Document document(testArena.arena);
    ASSERT_TRUE(document.parse(s_TextureTestMetadata));
    Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_CodecScratchArena);
    const auto entry = Impl::ParseTextureCookMetadata(assetRoot, "project", metadataPath, document, testArena.arena, scratchArena);
    ASSERT_TRUE(entry);
    EXPECT_EQ(entry->payloadBytes, original);
    const auto texture = Impl::BuildTextureAsset(*entry, testArena.arena);
    ASSERT_TRUE(texture);
    EXPECT_EQ(texture->virtualPath(), Name("project/textures/checker"));
    EXPECT_EQ(texture->payloadBytes(), original);
    EXPECT_EQ(entry->payloadBytes, original);
    Impl::TextureAssetCodec codec;
    Core::Assets::AssetBytes packaged(testArena.arena);
    ASSERT_TRUE(codec.serialize(*texture, packaged));
    const usize rawOffset = sizeof(::TextureBinaryPayload::HeaderBinary) + texture->mipLevels().size() * sizeof(::TextureBinaryPayload::MipLevelBinary);
    ASSERT_EQ(packaged.size(), rawOffset + original.size());
    EXPECT_EQ(NWB_MEMCMP(packaged.data() + rawOffset, original.data(), original.size()), 0);
    EXPECT_TRUE(RemoveAllIfExists(root));
}

TEST(AssetsGraphics, TextureCookerRejectsEveryTruncatedRawPayloadAndExtraBytes){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    const auto original = MakeTextureTestUastcPayload(testArena);
    Impl::TextureCookEntry entry(testArena.arena);
    entry.virtualPath = Name("project/textures/truncated_raw");
    entry.width = 7u;
    entry.height = 5u;
    entry.alphaMode = TextureAlphaMode::EmbeddedLdr;
    for(usize prefix = 0u; prefix < original.size(); ++prefix){
        entry.payloadBytes.assign(original.begin(), original.begin() + prefix);
        EXPECT_FALSE(Impl::BuildTextureAsset(entry, testArena.arena)) << prefix;
    }
    entry.payloadBytes = original;
    entry.payloadBytes.push_back(0u);
    EXPECT_FALSE(Impl::BuildTextureAsset(entry, testArena.arena));
    entry.payloadBytes = original;
    entry.payloadBytes.insert(entry.payloadBytes.end(), original.begin(), original.end());
    EXPECT_FALSE(Impl::BuildTextureAsset(entry, testArena.arena));
    const auto framedRuntimeBinary = MakeTextureTestRuntimeBinary(testArena);
    ASSERT_TRUE(framedRuntimeBinary);
    entry.payloadBytes = *framedRuntimeBinary;
    EXPECT_FALSE(Impl::BuildTextureAsset(entry, testArena.arena));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("raw texture byte count does not match the declared mip and alpha layout")));
    entry.payloadBytes = original;
    EXPECT_TRUE(Impl::BuildTextureAsset(entry, testArena.arena));
}

TEST(AssetsGraphics, TextureCookerRejectsOverflowingRawLayoutWithoutRetainingAllocations){
    TestArena testArena;
    struct OverflowLayout{
        TextureDimension::Enum dimension;
        u32 width;
        u32 height;
        u32 depth;
        TStringView diagnostic;
    };
    const OverflowLayout layouts[] = {
        { TextureDimension::Texture2D, Limit<u32>::s_Max, Limit<u32>::s_Max, 1u, NWB_TEXT("block layout exceeds supported limits") },
        { TextureDimension::TextureCube, 2147483648u, 2147483648u, 1u, NWB_TEXT("block layout exceeds supported limits") },
        { TextureDimension::Texture3D, 65536u, 65536u, Limit<u32>::s_Max, NWB_TEXT("mip byte offsets overflow") },
        { TextureDimension::Texture3D, 131072u, 131072u, Limit<u32>::s_Max, NWB_TEXT("block layout exceeds supported limits") },
    };
    for(const OverflowLayout& layout : layouts){
        CapturingLogger logger;
        Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
        Impl::TextureCookEntry entry(testArena.arena);
        entry.virtualPath = Name("project/textures/overflowing_raw_layout");
        entry.dimension = layout.dimension;
        entry.width = layout.width;
        entry.height = layout.height;
        entry.depth = layout.depth;
        const usize allocatedBytes = testArena.arena.memoryStats().usedBytes;
        EXPECT_FALSE(Impl::BuildTextureAsset(entry, testArena.arena)) << layout.dimension << ": " << layout.width << ", " << layout.height << ", " << layout.depth;
        EXPECT_EQ(testArena.arena.memoryStats().usedBytes, allocatedBytes);
        EXPECT_TRUE(logger.sawErrorContaining(layout.diagnostic)) << layout.dimension << ": " << layout.width << ", " << layout.height << ", " << layout.depth;
        EXPECT_FALSE(logger.sawErrorContaining(NWB_TEXT("raw texture byte count does not match")));
    }
}

TEST(AssetsGraphics, TextureCookerRejectsMissingImplicitSidecarInsteadOfSelectingAnotherFile){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    const auto rootResult = AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "texture_missing_raw_pair");
    ASSERT_TRUE(rootResult);
    const Path root = *rootResult;
    const Path assetRoot = root / "assets";
    const Path metadataPath = assetRoot / "textures" / "checker.nwb";
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(metadataPath, s_TextureTestMetadata));
    ASSERT_TRUE(WriteBinaryFile(assetRoot / "textures" / "another.tex", MakeTextureTestUastcPayload(testArena)));
    Core::Metascript::Document document(testArena.arena);
    ASSERT_TRUE(document.parse(s_TextureTestMetadata));
    Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_CodecScratchArena);
    EXPECT_FALSE(Impl::ParseTextureCookMetadata(assetRoot, "project", metadataPath, document, testArena.arena, scratchArena));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("failed to read raw texture sidecar")));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("checker.tex")));
    EXPECT_TRUE(RemoveAllIfExists(root));
}

TEST(AssetsGraphics, TextureMipDecoderRejectsSrgbAndUnsupportedLdrUploadFormatsBeforeDecoding){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    const auto prepared = MakeTextureTestRuntimeBinary(testArena);
    ASSERT_TRUE(prepared);
    Impl::Texture texture(testArena.arena, Name("project/textures/linear_decode"));
    ASSERT_TRUE(texture.loadBinary(*prepared));
    Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_CodecScratchArena);
    Impl::TextureMipDecoder decoder;
    for(const auto format : { Core::Format::ASTC_4x4_UNORM_SRGB, Core::Format::BC7_UNORM_SRGB, Core::Format::RGBA8_UNORM_SRGB, Core::Format::UNKNOWN, Core::Format::R8_UNORM, Core::Format::RGBA16_FLOAT })
        EXPECT_FALSE(decoder.decode(texture, texture.mipLevels().front(), 0u, format, scratchArena)) << format;
    EXPECT_EQ(logger.errorCount(), 6u);
}

TEST(AssetsGraphics, TextureCookerRejectsObsoleteAndDerivedMetadata){
    struct ObsoleteField{
        AStringView assignment;
        TStringView diagnostic;
    };
    constexpr Array<ObsoleteField, 24u> s_ObsoleteFields = {{
        { "asset.version = 1;\n", NWB_TEXT("unsupported asset field 'version'") },
        { "asset.mip_count = 3;\n", NWB_TEXT("unsupported asset field 'mip_count'") },
        { "asset.data = \"checker.tex\";\n", NWB_TEXT("unsupported asset field 'data'") },
        { "asset.file = \"checker.tex\";\n", NWB_TEXT("unsupported asset field 'file'") },
        { "asset.mip_filter = \"linear\";\n", NWB_TEXT("unsupported asset field 'mip_filter'") },
        { "asset.uastc_spec_revision = \"old\";\n", NWB_TEXT("unsupported asset field 'uastc_spec_revision'") },
        { "asset.uastc_hdr_spec_revision = \"old\";\n", NWB_TEXT("unsupported asset field 'uastc_hdr_spec_revision'") },
        { "asset.alpha_uastc_spec_revision = \"old\";\n", NWB_TEXT("unsupported asset field 'alpha_uastc_spec_revision'") },
        { "asset.schema_version = 1;\n", NWB_TEXT("unsupported asset field 'schema_version'") },
        { "asset.revision = 1;\n", NWB_TEXT("unsupported asset field 'revision'") },
        { "asset.block_width = 4;\n", NWB_TEXT("unsupported asset field 'block_width'") },
        { "asset.block_height = 4;\n", NWB_TEXT("unsupported asset field 'block_height'") },
        { "asset.bytes_per_block = 16;\n", NWB_TEXT("unsupported asset field 'bytes_per_block'") },
        { "asset.payload_layout = \"mip_major_slice_major_blocks\";\n", NWB_TEXT("unsupported asset field 'payload_layout'") },
        { "asset.mip_address_mode = \"clamp\";\n", NWB_TEXT("unsupported asset field 'mip_address_mode'") },
        { "asset.mips = [];\n", NWB_TEXT("unsupported asset field 'mips'") },
        { "asset.has_alpha = 1;\n", NWB_TEXT("unsupported asset field 'has_alpha'") },
        { "asset.alpha_constant_unorm8 = 128;\n", NWB_TEXT("unsupported asset field 'alpha_constant_unorm8'") },
        { "asset.alpha_payload_offset_bytes = 48;\n", NWB_TEXT("unsupported asset field 'alpha_payload_offset_bytes'") },
        { "asset.alpha_payload_byte_count = 48;\n", NWB_TEXT("unsupported asset field 'alpha_payload_byte_count'") },
        { "asset.color_space = \"srgb\";\n", NWB_TEXT("unsupported asset field 'color_space'") },
        { "asset.width = 7;\n", NWB_TEXT("unsupported asset field 'width'") },
        { "asset.height = 5;\n", NWB_TEXT("unsupported asset field 'height'") },
        { "asset.depth = 1;\n", NWB_TEXT("unsupported asset field 'depth'") },
    }};
    for(const ObsoleteField& field : s_ObsoleteFields){
        CapturingLogger logger;
        Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
        TestArena testArena;
        Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_CodecScratchArena);
        ::AString<Core::Alloc::ScratchArena> metadata(s_TextureTestMetadata, scratchArena);
        metadata.append(field.assignment);
        Core::Metascript::Document document(testArena.arena);
        ASSERT_TRUE(document.parse(metadata));
        const Path assetRoot = AssetsGraphicsFixture::AssetsGraphicsTestCaseRoot(testArena, "texture_obsolete_metadata") / "assets";
        const Path metadataPath = assetRoot / "textures" / "checker.nwb";
        EXPECT_FALSE(Impl::ParseTextureCookMetadata(assetRoot, "project", metadataPath, document, testArena.arena, scratchArena)) << field.assignment;
        EXPECT_TRUE(logger.sawErrorContaining(field.diagnostic)) << field.assignment;
    }
}

TEST(AssetsGraphics, TextureCookerRejectsMissingRequiredRawLayoutDescriptors){
    struct RequiredField{
        AStringView assignment;
        TStringView diagnostic;
    };
    constexpr Array<RequiredField, 4u> s_RequiredFields = {{
        { "asset.format = \"uastc_ldr_4x4\";\n", NWB_TEXT("field 'format' is required") },
        { "asset.dimension = \"2d\";\n", NWB_TEXT("field 'dimension' is required") },
        { "asset.resolution = [7, 5];\n", NWB_TEXT("resolution must contain exactly") },
        { "asset.alpha_mode = \"embedded_ldr\";\n", NWB_TEXT("field 'alpha_mode' is required") },
    }};
    for(const RequiredField& field : s_RequiredFields){
        CapturingLogger logger;
        Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
        TestArena testArena;
        Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_CodecScratchArena);
        ::AString<Core::Alloc::ScratchArena> metadata(s_TextureTestMetadata, scratchArena);
        const usize fieldOffset = metadata.find(field.assignment);
        ASSERT_NE(fieldOffset, AStringView::npos);
        metadata.erase(fieldOffset, field.assignment.size());
        Core::Metascript::Document document(testArena.arena);
        ASSERT_TRUE(document.parse(metadata));
        const Path assetRoot = AssetsGraphicsFixture::AssetsGraphicsTestCaseRoot(testArena, "texture_missing_layout") / "assets";
        const Path metadataPath = assetRoot / "textures" / "checker.nwb";
        EXPECT_FALSE(Impl::ParseTextureCookMetadata(assetRoot, "project", metadataPath, document, testArena.arena, scratchArena));
        EXPECT_TRUE(logger.sawErrorContaining(field.diagnostic)) << field.assignment;
    }
}

TEST(AssetsGraphics, TextureCookerRejectsInvalidRawLayoutDescriptorsBeforeReadingSidecar){
    struct InvalidLayout{
        AStringView assignment;
        TStringView diagnostic;
    };
    constexpr Array<InvalidLayout, 19u> s_InvalidLayouts = {{
        { "asset.format = 17;", NWB_TEXT("field 'format' must be a string") },
        { "asset.format = \"rgba8\";", NWB_TEXT("unsupported texture format") },
        { "asset.dimension = \"3d\";", NWB_TEXT("unsupported texture dimension") },
        { "asset.dimension = \"cube\";", NWB_TEXT("cube resolution must be square") },
        { "asset.dimension = \"volume\";", NWB_TEXT("resolution must contain exactly") },
        { "asset.resolution = [7];", NWB_TEXT("resolution must contain exactly") },
        { "asset.resolution = [7, 5, 1];", NWB_TEXT("resolution must contain exactly") },
        { "asset.resolution = [0, 5];", NWB_TEXT("positive u32 integer") },
        { "asset.resolution = [-1, 5];", NWB_TEXT("positive u32 integer") },
        { "asset.resolution = [7.5, 5];", NWB_TEXT("positive u32 integer") },
        { "asset.resolution = [4294967296, 5];", NWB_TEXT("positive u32 integer") },
        { "asset.mip_count = 0;", NWB_TEXT("unsupported asset field 'mip_count'") },
        { "asset.mip_count = 2;", NWB_TEXT("unsupported asset field 'mip_count'") },
        { "asset.mip_count = 4;", NWB_TEXT("unsupported asset field 'mip_count'") },
        { "asset.alpha_mode = \"straight\";", NWB_TEXT("unsupported alpha_mode") },
        { "asset.alpha_mode = \"constant_unorm8\";", NWB_TEXT("field 'alpha_constant_unorm8' is required") },
        { "asset.alpha_mode = \"constant_unorm8\"; asset.alpha_constant_unorm8 = 255;", NWB_TEXT("field 'alpha_constant_unorm8' must be an integer in [0, 254]") },
        { "asset.alpha_mode = \"constant_unorm8\"; asset.alpha_constant_unorm8 = -1;", NWB_TEXT("field 'alpha_constant_unorm8' must be an integer in [0, 254]") },
        { "asset.alpha_mode = \"constant_unorm8\"; asset.alpha_constant_unorm8 = 0.5;", NWB_TEXT("field 'alpha_constant_unorm8' must be an integer in [0, 254]") },
    }};
    for(const InvalidLayout& invalid : s_InvalidLayouts){
        CapturingLogger logger;
        Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
        TestArena testArena;
        Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_CodecScratchArena);
        ::AString<Core::Alloc::ScratchArena> metadata(s_TextureTestMetadata, scratchArena);
        metadata.append(invalid.assignment);
        Core::Metascript::Document document(testArena.arena);
        ASSERT_TRUE(document.parse(metadata));
        const Path assetRoot = AssetsGraphicsFixture::AssetsGraphicsTestCaseRoot(testArena, "texture_invalid_layout") / "assets";
        const Path metadataPath = assetRoot / "textures" / "checker.nwb";
        EXPECT_FALSE(Impl::ParseTextureCookMetadata(assetRoot, "project", metadataPath, document, testArena.arena, scratchArena));
        EXPECT_TRUE(logger.sawErrorContaining(invalid.diagnostic)) << invalid.assignment;
        EXPECT_FALSE(logger.sawErrorContaining(NWB_TEXT("failed to read raw texture sidecar"))) << invalid.assignment;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

