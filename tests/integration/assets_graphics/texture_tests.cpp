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


TEST(AssetsGraphics, TextureFormatRoundsPartialBlocksAndNonPowerOfTwoMips){
    u32 mipCount = 0u;
    EXPECT_TRUE(TextureFormat::ComputeCompleteMipCount(
        TextureDimension::Texture2D,
        7u,
        5u,
        1u,
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


TEST(AssetsGraphics, TextureCookerRejectsObsoleteAndDerivedMetadata){
    struct ObsoleteField{
        AStringView assignment;
        TStringView diagnostic;
    };
    constexpr Array<ObsoleteField, 15u> s_ObsoleteFields = {{
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
        for(const ObsoleteField& field : s_ObsoleteFields){
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
    constexpr Array<InvalidFormat, 4u> s_InvalidFormats = {{
        { "", GLB_TEXT("field 'format' is required") },
        { "asset.format = 17;", GLB_TEXT("field 'format' must be a string") },
        { "asset.format = \"\";", GLB_TEXT("field 'format' must not be empty") },
        { "asset.format = \"uastc_hdr_6x6\";", GLB_TEXT("field 'format' must be 'uastc_ldr_4x4' or 'uastc_hdr_4x4'") },
    }};
    constexpr AStringView s_FormatAssignment = "asset.format = \"uastc_ldr_4x4\";";
    for(const InvalidFormat& invalidFormat : s_InvalidFormats){
        CapturingLogger logger;
        NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(
            logger, NWB::Core::Common::LoggerBreakPolicy::BreakOnFatal
        );

        TestArena testArena;
        NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_CodecScratchArena);
        ::AString<NWB::Core::Alloc::ScratchArena> metadata(s_TextureTestMetadata, scratchArena);
        const usize formatPosition = metadata.find(s_FormatAssignment);
        ASSERT_NE(formatPosition, AString::npos);
        metadata.replace(formatPosition, s_FormatAssignment.size(), invalidFormat.assignment);
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
    constexpr Array<DerivedField, 7u> s_DerivedFields = {{
        { s_TextureTestMetadata, "asset.depth = 1;\n", GLB_TEXT("unsupported asset field 'depth'") },
        { s_TextureCubeTestMetadata, "asset.depth = 1;\n", GLB_TEXT("unsupported asset field 'depth'") },
        { s_TextureHdrTestMetadata, "asset.color_space = \"linear\";\n", GLB_TEXT("unsupported asset field 'color_space'") },
        { s_TextureHdrTestMetadata, "asset.has_alpha = 1;\n", GLB_TEXT("unsupported asset field 'has_alpha'") },
        { s_TextureHdrTestMetadata, "asset.alpha_constant_unorm8 = 128;\n", GLB_TEXT("unsupported asset field 'alpha_constant_unorm8'") },
        { s_TextureTestMetadata, "asset.alpha_mode = \"opaque\";\n", GLB_TEXT("unsupported asset field 'alpha_mode'") },
        { s_TextureTestMetadata, "asset.alpha_constant_unorm8 = 128;\n", GLB_TEXT("unsupported asset field 'alpha_constant_unorm8'") },
    }};
    for(const DerivedField& field : s_DerivedFields){
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
    constexpr Array<SidecarCase, 4u> s_SidecarCases = {{
        { s_TextureTestMetadata, "checker.tex", 96u },
        { s_TextureHdrTestMetadata, "bright.tex", 96u },
        { s_TextureCubeTestMetadata, "sky.tex", 192u },
        { s_TextureVolumeTestMetadata, "fog.tex", 80u },
    }};
    for(const SidecarCase& sidecarCase : s_SidecarCases){
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
                NWB::Impl::Texture texture(testArena.arena, s_NameNone);
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
        u8 alphaConstant;
    };
    constexpr Array<AlphaCase, 4u> s_AlphaCases = {{
        { "asset.alpha_mode = \"opaque\";", true, 255u },
        { "asset.alpha_mode = \"constant_unorm8\"; asset.alpha_constant_unorm8 = 0;", true, 0u },
        { "asset.alpha_mode = \"constant_unorm8\"; asset.alpha_constant_unorm8 = 254;", true, 254u },
        { "asset.alpha_mode = \"constant_unorm8\"; asset.alpha_constant_unorm8 = 255;", false, 255u },
    }};
    constexpr AStringView s_AlphaAssignment = "asset.alpha_mode = \"uastc_ldr_4x4\";";
    TestArena testArena;
    Path root(testArena.arena);
    ASSERT_TRUE(AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "texture_hdr_alpha_inference", root));
    const Path assetRoot = root / "assets";
    const Path metadataPath = assetRoot / "textures" / "bright.nwb";
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(metadataPath, s_TextureHdrTestMetadata));
    ASSERT_TRUE(WriteBinaryFile(assetRoot / "textures" / "bright.tex", MakeTextureTestUastcPayload(testArena, 48u)));
    for(const AlphaCase& alphaCase : s_AlphaCases){
        CapturingLogger logger;
        NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(
            logger, NWB::Core::Common::LoggerBreakPolicy::BreakOnFatal
        );

        NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_CodecScratchArena);
        ::AString<NWB::Core::Alloc::ScratchArena> metadata(s_TextureHdrTestMetadata, scratchArena);
        const usize alphaPosition = metadata.find(s_AlphaAssignment);
        ASSERT_NE(alphaPosition, AString::npos);
        metadata.replace(alphaPosition, s_AlphaAssignment.size(), alphaCase.assignment);
        NWB::Core::Metascript::Document document(testArena.arena);
        ASSERT_TRUE(document.parse(metadata));
        NWB::Impl::TextureCookEntry entry(testArena.arena);
        const bool parsed = NWB::Impl::ParseTextureCookMetadata(assetRoot, "project", metadataPath, document, entry, scratchArena);
        EXPECT_EQ(parsed, alphaCase.valid) << alphaCase.assignment;
        if(parsed){
            NWB::Impl::Texture texture(testArena.arena, s_NameNone);
            ASSERT_TRUE(NWB::Impl::BuildTextureAsset(entry, texture));
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
    constexpr Array<OverflowCase, 4u> s_OverflowCases = {{
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
    for(const OverflowCase& overflowCase : s_OverflowCases){
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

