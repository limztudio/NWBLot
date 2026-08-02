// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_TextureTestMetadata =
    "texture asset;\n\n"
    "asset.version = 1;\n"
    "asset.format = \"uastc_ldr_4x4\";\n"
    "asset.uastc_spec_revision = \"b624c07ad3c659e7b0f0badcb36e9a6b8820a99d\";\n"
    "asset.color_space = \"srgb\";\n"
    "asset.dimension = \"2d\";\n"
    "asset.depth = 1;\n"
    "asset.width = 7;\n"
    "asset.height = 5;\n"
    "asset.block_width = 4;\n"
    "asset.block_height = 4;\n"
    "asset.bytes_per_block = 16;\n"
    "asset.payload_layout = \"mip_major_slice_major_blocks\";\n"
    "asset.mip_address_mode = \"clamp\";\n"
    "asset.has_alpha = 1;\n"
    "asset.mip_count = 3;\n"
    "asset.data = \"checker.tex\";\n"
    "asset.mips = [\n"
    "    { \"level\": 0, \"width\": 7, \"height\": 5, \"blocks_x\": 2, \"blocks_y\": 2, \"offset_bytes\": 0, \"size_bytes\": 64, \"slices\": 1 },\n"
    "    { \"level\": 1, \"width\": 3, \"height\": 2, \"blocks_x\": 1, \"blocks_y\": 1, \"offset_bytes\": 64, \"size_bytes\": 16, \"slices\": 1 },\n"
    "    { \"level\": 2, \"width\": 1, \"height\": 1, \"blocks_x\": 1, \"blocks_y\": 1, \"offset_bytes\": 80, \"size_bytes\": 16, \"slices\": 1 },\n"
    "];\n"
;

static constexpr AStringView s_TextureCubeTestMetadata =
    "texture asset;\n\n"
    "asset.version = 1;\n"
    "asset.format = \"uastc_ldr_4x4\";\n"
    "asset.uastc_spec_revision = \"b624c07ad3c659e7b0f0badcb36e9a6b8820a99d\";\n"
    "asset.color_space = \"srgb\";\n"
    "asset.dimension = \"cube\";\n"
    "asset.depth = 1;\n"
    "asset.width = 2;\n"
    "asset.height = 2;\n"
    "asset.block_width = 4;\n"
    "asset.block_height = 4;\n"
    "asset.bytes_per_block = 16;\n"
    "asset.payload_layout = \"mip_major_slice_major_blocks\";\n"
    "asset.mip_address_mode = \"clamp\";\n"
    "asset.has_alpha = 0;\n"
    "asset.mip_count = 2;\n"
    "asset.data = \"sky.tex\";\n"
    "asset.mips = [\n"
    "    { \"level\": 0, \"width\": 2, \"height\": 2, \"slices\": 6, \"blocks_x\": 1, \"blocks_y\": 1, \"offset_bytes\": 0, \"size_bytes\": 96 },\n"
    "    { \"level\": 1, \"width\": 1, \"height\": 1, \"slices\": 6, \"blocks_x\": 1, \"blocks_y\": 1, \"offset_bytes\": 96, \"size_bytes\": 96 },\n"
    "];\n"
;

static constexpr AStringView s_TextureVolumeTestMetadata =
    "texture asset;\n\n"
    "asset.version = 1;\n"
    "asset.format = \"uastc_ldr_4x4\";\n"
    "asset.uastc_spec_revision = \"b624c07ad3c659e7b0f0badcb36e9a6b8820a99d\";\n"
    "asset.color_space = \"linear\";\n"
    "asset.dimension = \"volume\";\n"
    "asset.depth = 3;\n"
    "asset.width = 4;\n"
    "asset.height = 2;\n"
    "asset.block_width = 4;\n"
    "asset.block_height = 4;\n"
    "asset.bytes_per_block = 16;\n"
    "asset.payload_layout = \"mip_major_slice_major_blocks\";\n"
    "asset.mip_address_mode = \"clamp\";\n"
    "asset.has_alpha = 1;\n"
    "asset.mip_count = 3;\n"
    "asset.data = \"fog.tex\";\n"
    "asset.mips = [\n"
    "    { \"level\": 0, \"width\": 4, \"height\": 2, \"slices\": 3, \"blocks_x\": 1, \"blocks_y\": 1, \"offset_bytes\": 0, \"size_bytes\": 48 },\n"
    "    { \"level\": 1, \"width\": 2, \"height\": 1, \"slices\": 1, \"blocks_x\": 1, \"blocks_y\": 1, \"offset_bytes\": 48, \"size_bytes\": 16 },\n"
    "    { \"level\": 2, \"width\": 1, \"height\": 1, \"slices\": 1, \"blocks_x\": 1, \"blocks_y\": 1, \"offset_bytes\": 64, \"size_bytes\": 16 },\n"
    "];\n"
;


static NWB::Core::Assets::AssetBytes MakeTextureTestUastcPayload(
    TestArena& testArena,
    const usize byteCount = 96u,
    const u8 initialValue = 0u
){
    NWB::Core::Assets::AssetBytes bytes = MakeAssetBytes(testArena);
    bytes.resize(byteCount);
    for(usize index = 0u; index < bytes.size(); ++index)
        bytes[index] = static_cast<u8>(initialValue + index);
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

static NWB::Impl::Texture::MipLevelVector MakeTextureCubeTestMipLevels(TestArena& testArena){
    NWB::Impl::Texture::MipLevelVector mipLevels(testArena.arena);
    mipLevels.reserve(2u);
    mipLevels.push_back(NWB::Impl::TextureMipLevel{ 2u, 2u, 1u, 1u, 0u, 96u, 6u });
    mipLevels.push_back(NWB::Impl::TextureMipLevel{ 1u, 1u, 1u, 1u, 96u, 96u, 6u });
    return mipLevels;
}

static NWB::Impl::Texture::MipLevelVector MakeTextureVolumeTestMipLevels(TestArena& testArena){
    NWB::Impl::Texture::MipLevelVector mipLevels(testArena.arena);
    mipLevels.reserve(3u);
    mipLevels.push_back(NWB::Impl::TextureMipLevel{ 4u, 2u, 1u, 1u, 0u, 48u, 3u });
    mipLevels.push_back(NWB::Impl::TextureMipLevel{ 2u, 1u, 1u, 1u, 48u, 16u, 1u });
    mipLevels.push_back(NWB::Impl::TextureMipLevel{ 1u, 1u, 1u, 1u, 64u, 16u, 1u });
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
    EXPECT_EQ(loadedTexture.dimension(), NWB::Impl::TextureDimension::Texture2D);
    EXPECT_EQ(loadedTexture.depth(), 1u);
    ASSERT_EQ(loadedTexture.mipLevels().size(), 3u);
    EXPECT_EQ(loadedTexture.mipLevels()[0u].sliceCount, 1u);
    EXPECT_EQ(loadedTexture.mipLevels()[0u].sizeBytes, 64u);
    EXPECT_EQ(loadedTexture.mipLevels()[1u].offsetBytes, 64u);
    EXPECT_EQ(loadedTexture.mipLevels()[2u].offsetBytes, 80u);
    ASSERT_EQ(loadedTexture.uastcBlocks().size(), 96u);
    for(usize index = 0u; index < loadedTexture.uastcBlocks().size(); ++index)
        EXPECT_EQ(loadedTexture.uastcBlocks()[index], static_cast<u8>(index));

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
            2u,
            2u,
            MakeTextureCubeTestMipLevels(testArena),
            MakeTextureTestUastcPayload(testArena, 192u, 0x40u),
            NWB::Impl::TextureDimension::TextureCube,
            1u
        );
        ASSERT_TRUE(cube.validatePayload());

        NWB::Core::Assets::AssetBytes binary = MakeAssetBytes(testArena);
        ASSERT_TRUE(codec.serialize(cube, binary));
        ASSERT_EQ(binary.size(), sizeof(NWB::Impl::TextureBinaryPayload::HeaderBinary) + 2u * sizeof(NWB::Impl::TextureBinaryPayload::MipLevelBinary) + 192u);

        UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
        ASSERT_TRUE(codec.deserialize(testArena.arena, cube.virtualPath(), binary, loadedAsset));
        ASSERT_NE(loadedAsset.get(), nullptr);
        const NWB::Impl::Texture& loadedCube = static_cast<const NWB::Impl::Texture&>(*loadedAsset);
        EXPECT_EQ(loadedCube.dimension(), NWB::Impl::TextureDimension::TextureCube);
        EXPECT_EQ(loadedCube.depth(), 1u);
        ASSERT_EQ(loadedCube.mipLevels().size(), 2u);
        EXPECT_EQ(loadedCube.mipLevels()[0u].sliceCount, 6u);
        EXPECT_EQ(loadedCube.mipLevels()[1u].sliceCount, 6u);
        EXPECT_EQ(loadedCube.uastcBlocks().size(), 192u);
    }

    {
        NWB::Impl::Texture volume(testArena.arena, Name("project/textures/fog"));
        volume.setPayload(
            NWB::Impl::TextureColorSpace::Linear,
            true,
            4u,
            2u,
            MakeTextureVolumeTestMipLevels(testArena),
            MakeTextureTestUastcPayload(testArena, 80u, 0x80u),
            NWB::Impl::TextureDimension::Texture3D,
            3u
        );
        ASSERT_TRUE(volume.validatePayload());

        NWB::Core::Assets::AssetBytes binary = MakeAssetBytes(testArena);
        ASSERT_TRUE(codec.serialize(volume, binary));
        ASSERT_EQ(binary.size(), sizeof(NWB::Impl::TextureBinaryPayload::HeaderBinary) + 3u * sizeof(NWB::Impl::TextureBinaryPayload::MipLevelBinary) + 80u);

        UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
        ASSERT_TRUE(codec.deserialize(testArena.arena, volume.virtualPath(), binary, loadedAsset));
        ASSERT_NE(loadedAsset.get(), nullptr);
        const NWB::Impl::Texture& loadedVolume = static_cast<const NWB::Impl::Texture&>(*loadedAsset);
        EXPECT_EQ(loadedVolume.dimension(), NWB::Impl::TextureDimension::Texture3D);
        EXPECT_EQ(loadedVolume.depth(), 3u);
        ASSERT_EQ(loadedVolume.mipLevels().size(), 3u);
        EXPECT_EQ(loadedVolume.mipLevels()[0u].sliceCount, 3u);
        EXPECT_EQ(loadedVolume.mipLevels()[1u].sliceCount, 1u);
        EXPECT_EQ(loadedVolume.mipLevels()[2u].sliceCount, 1u);
        EXPECT_EQ(loadedVolume.uastcBlocks().size(), 80u);
    }

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

TEST(AssetsGraphics, TextureCookerBuildsCubeAndVolumeAssetsFromCurrentMetadata){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
    ASSERT_TRUE(PrepareAssetsGraphicsCookCase(
        testArena,
        "texture_cube_volume_cooker_round_trip",
        root,
        outputDirectory
    ));

    const Path assetRoot = root / "assets";
    const Path textureDirectory = assetRoot / "textures";
    ASSERT_TRUE(WriteTextFile(textureDirectory / "sky.nwb", s_TextureCubeTestMetadata));
    ASSERT_TRUE(WriteBinaryFile(textureDirectory / "sky.tex", MakeTextureTestUastcPayload(testArena, 192u, 0x40u)));
    ASSERT_TRUE(WriteTextFile(textureDirectory / "fog.nwb", s_TextureVolumeTestMetadata));
    ASSERT_TRUE(WriteBinaryFile(textureDirectory / "fog.tex", MakeTextureTestUastcPayload(testArena, 80u, 0x80u)));
    ASSERT_TRUE(CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }));

    UniquePtr<NWB::Core::Assets::IAsset> cubeAsset;
    ASSERT_TRUE(LoadCookedAsset<NWB::Impl::TextureAssetCodec>(
        testArena,
        outputDirectory,
        Name("project/textures/sky"),
        cubeAsset,
        3u
    ));
    ASSERT_NE(cubeAsset.get(), nullptr);
    const NWB::Impl::Texture& cube = static_cast<const NWB::Impl::Texture&>(*cubeAsset);
    EXPECT_EQ(cube.dimension(), NWB::Impl::TextureDimension::TextureCube);
    EXPECT_EQ(cube.depth(), 1u);
    ASSERT_EQ(cube.mipLevels().size(), 2u);
    EXPECT_EQ(cube.mipLevels()[0u].sliceCount, 6u);

    UniquePtr<NWB::Core::Assets::IAsset> volumeAsset;
    ASSERT_TRUE(LoadCookedAsset<NWB::Impl::TextureAssetCodec>(
        testArena,
        outputDirectory,
        Name("project/textures/fog"),
        volumeAsset,
        3u
    ));
    ASSERT_NE(volumeAsset.get(), nullptr);
    const NWB::Impl::Texture& volume = static_cast<const NWB::Impl::Texture&>(*volumeAsset);
    EXPECT_EQ(volume.dimension(), NWB::Impl::TextureDimension::Texture3D);
    EXPECT_EQ(volume.depth(), 3u);
    ASSERT_EQ(volume.mipLevels().size(), 3u);
    EXPECT_EQ(volume.mipLevels()[0u].sliceCount, 3u);
    EXPECT_EQ(volume.uastcBlocks().size(), 80u);

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
    EXPECT_EQ(logger.errorCount(), 0u);
}

TEST(AssetsGraphics, TextureCookerRejectsUnsupportedMetadataVersion){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    AString metadata(s_TextureTestMetadata);
    const usize versionPosition = metadata.find("asset.version = 1;");
    ASSERT_NE(versionPosition, AString::npos);
    metadata.replace(versionPosition, 18u, "asset.version = 2;");

    NWB::Core::Metascript::Document document(testArena.arena);
    ASSERT_TRUE(document.parse(AStringView(metadata.data(), metadata.size())));

    const Path assetRoot = AssetsGraphicsTestCaseRoot(testArena, "texture_legacy_metadata") / "assets";
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
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("outside the supported range")));
#else
#endif
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
