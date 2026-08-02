// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_TextureTestMetadata =
    "texture asset;\n\n"
    "asset.version = 1;\n"
    "asset.format = \"uastc_ldr_4x4\";\n"
    "asset.uastc_spec_revision = \"b624c07ad3c659e7b0f0badcb36e9a6b8820a99d\";\n"
    "asset.color_space = \"srgb\";\n"
    "asset.width = 7;\n"
    "asset.height = 5;\n"
    "asset.block_width = 4;\n"
    "asset.block_height = 4;\n"
    "asset.bytes_per_block = 16;\n"
    "asset.payload_layout = \"mip_major_blocks\";\n"
    "asset.mip_address_mode = \"clamp\";\n"
    "asset.has_alpha = 1;\n"
    "asset.mip_count = 3;\n"
    "asset.data = \"checker.tex\";\n"
    "asset.mips = [\n"
    "    { \"level\": 0, \"width\": 7, \"height\": 5, \"blocks_x\": 2, \"blocks_y\": 2, \"offset_bytes\": 0, \"size_bytes\": 64 },\n"
    "    { \"level\": 1, \"width\": 3, \"height\": 2, \"blocks_x\": 1, \"blocks_y\": 1, \"offset_bytes\": 64, \"size_bytes\": 16 },\n"
    "    { \"level\": 2, \"width\": 1, \"height\": 1, \"blocks_x\": 1, \"blocks_y\": 1, \"offset_bytes\": 80, \"size_bytes\": 16 },\n"
    "];\n"
;


static NWB::Core::Assets::AssetBytes MakeTextureTestUastcPayload(TestArena& testArena){
    NWB::Core::Assets::AssetBytes bytes = MakeAssetBytes(testArena);
    bytes.resize(96u);
    for(usize index = 0u; index < bytes.size(); ++index)
        bytes[index] = static_cast<u8>(index);
    return bytes;
}

static NWB::Impl::Texture::MipLevelVector MakeTextureTestMipLevels(TestArena& testArena){
    NWB::Impl::Texture::MipLevelVector mipLevels(testArena.arena);
    mipLevels.reserve(3u);
    mipLevels.push_back(NWB::Impl::TextureMipLevel{ 7u, 5u, 2u, 2u, 0u, 64u });
    mipLevels.push_back(NWB::Impl::TextureMipLevel{ 3u, 2u, 1u, 1u, 64u, 16u });
    mipLevels.push_back(NWB::Impl::TextureMipLevel{ 1u, 1u, 1u, 1u, 80u, 16u });
    return mipLevels;
}


TEST(AssetsGraphics, TextureCodecRoundTripPreservesUastcMipPayload){
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
        MakeTextureTestUastcPayload(testArena)
    );
    ASSERT_TRUE(texture.validatePayload());

    NWB::Impl::TextureAssetCodec codec;
    NWB::Core::Assets::AssetBytes binary = MakeAssetBytes(testArena);
    ASSERT_TRUE(codec.serialize(texture, binary));
    ASSERT_EQ(binary.size(), sizeof(NWB::Impl::TextureBinaryPayload::HeaderBinary) + 3u * sizeof(NWB::Impl::TextureBinaryPayload::MipLevelBinary) + 96u);

    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    ASSERT_TRUE(codec.deserialize(testArena.arena, texture.virtualPath(), binary, loadedAsset));
    ASSERT_NE(loadedAsset.get(), nullptr);

    const NWB::Impl::Texture& loadedTexture = static_cast<const NWB::Impl::Texture&>(*loadedAsset);
    EXPECT_EQ(loadedTexture.colorSpace(), NWB::Impl::TextureColorSpace::Srgb);
    EXPECT_TRUE(loadedTexture.hasAlpha());
    EXPECT_EQ(loadedTexture.width(), 7u);
    EXPECT_EQ(loadedTexture.height(), 5u);
    ASSERT_EQ(loadedTexture.mipLevels().size(), 3u);
    EXPECT_EQ(loadedTexture.mipLevels()[0u].sizeBytes, 64u);
    EXPECT_EQ(loadedTexture.mipLevels()[1u].offsetBytes, 64u);
    EXPECT_EQ(loadedTexture.mipLevels()[2u].offsetBytes, 80u);
    ASSERT_EQ(loadedTexture.uastcBlocks().size(), 96u);
    for(usize index = 0u; index < loadedTexture.uastcBlocks().size(); ++index)
        EXPECT_EQ(loadedTexture.uastcBlocks()[index], static_cast<u8>(index));

    EXPECT_EQ(logger.errorCount(), 0u);
}

TEST(AssetsGraphics, TextureCookerBuildsCookedAssetFromTexConverterMetadata){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
    ASSERT_TRUE(PrepareAssetsGraphicsCookCase(
        testArena,
        "texture_cooker_round_trip",
        root,
        outputDirectory
    ));

    const Path assetRoot = root / "assets";
    const Path textureDirectory = assetRoot / "textures";
    const Path metadataPath = textureDirectory / "checker.nwb";
    const Path dataPath = textureDirectory / "checker.tex";
    ASSERT_TRUE(WriteTextFile(metadataPath, s_TextureTestMetadata));
    ASSERT_TRUE(WriteBinaryFile(dataPath, MakeTextureTestUastcPayload(testArena)));
    ASSERT_TRUE(CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }));

    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    ASSERT_TRUE(LoadCookedAsset<NWB::Impl::TextureAssetCodec>(
        testArena,
        outputDirectory,
        Name("project/textures/checker"),
        loadedAsset
    ));
    ASSERT_NE(loadedAsset.get(), nullptr);

    const NWB::Impl::Texture& texture = static_cast<const NWB::Impl::Texture&>(*loadedAsset);
    EXPECT_EQ(texture.colorSpace(), NWB::Impl::TextureColorSpace::Srgb);
    EXPECT_TRUE(texture.hasAlpha());
    EXPECT_EQ(texture.width(), 7u);
    EXPECT_EQ(texture.height(), 5u);
    ASSERT_EQ(texture.mipLevels().size(), 3u);
    EXPECT_EQ(texture.uastcBlocks().size(), 96u);

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
    EXPECT_EQ(logger.errorCount(), 0u);
}

TEST(AssetsGraphics, TextureCookerRejectsSidecarPathTraversal){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    AString metadata(s_TextureTestMetadata);
    const usize dataPosition = metadata.find("checker.tex");
    ASSERT_NE(dataPosition, AString::npos);
    metadata.replace(dataPosition, 11u, "../checker.tex");

    NWB::Core::Metascript::Document document(testArena.arena);
    ASSERT_TRUE(document.parse(AStringView(metadata.data(), metadata.size())));

    const Path assetRoot = AssetsGraphicsTestCaseRoot(testArena, "texture_path_traversal") / "assets";
    const Path metadataPath = assetRoot / "textures" / "checker.nwb";
    NWB::Impl::TextureCookEntry entry(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena(s_CodecScratchArena);
    EXPECT_FALSE(NWB::Impl::ParseTextureCookMetadata(
        assetRoot,
        "project",
        metadataPath,
        document,
        entry,
        scratchArena
    ));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("sidecar filename without path components")));
#else
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
