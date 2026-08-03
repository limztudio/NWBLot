// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_SamplerTestMetadata =
    "sampler asset;\n\n"
    "asset.version = 1;\n"
    "asset.min_filter = \"nearest\";\n"
    "asset.mag_filter = \"linear\";\n"
    "asset.mip_filter = \"nearest\";\n"
    "asset.address_u = \"wrap\";\n"
    "asset.address_v = \"mirror\";\n"
    "asset.address_w = \"border\";\n"
    "asset.reduction = \"standard\";\n"
    "asset.max_anisotropy = 4.0;\n"
    "asset.mip_bias = -0.25;\n"
    "asset.border_color = [0.125, 0.25, 0.5, 1.0];\n"
;


TEST(AssetsGraphics, SamplerCodecRoundTripPreservesDescription){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    NWB::Impl::Sampler sampler(testArena.arena, Name("project/samplers/test"));
    NWB::Core::SamplerDesc description;
    description.borderColor = NWB::Core::Color(0.125f, 0.25f, 0.5f, 1.0f);
    description.maxAnisotropy = 4.0f;
    description.mipBias = -0.25f;
    description.minFilter = false;
    description.magFilter = true;
    description.mipFilter = false;
    description.addressU = NWB::Core::SamplerAddressMode::Wrap;
    description.addressV = NWB::Core::SamplerAddressMode::Mirror;
    description.addressW = NWB::Core::SamplerAddressMode::Border;
    sampler.setDescription(description);
    ASSERT_TRUE(sampler.validatePayload());

    NWB::Impl::SamplerAssetCodec codec;
    NWB::Core::Assets::AssetBytes binary = MakeAssetBytes(testArena);
    ASSERT_TRUE(codec.serialize(sampler, binary));
    ASSERT_EQ(binary.size(), sizeof(NWB::Impl::SamplerBinaryPayload::HeaderBinary));

    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    ASSERT_TRUE(codec.deserialize(testArena.arena, sampler.virtualPath(), binary, loadedAsset));
    ASSERT_NE(loadedAsset.get(), nullptr);
    const NWB::Impl::Sampler& loadedSampler = static_cast<const NWB::Impl::Sampler&>(*loadedAsset);
    const NWB::Core::SamplerDesc& loaded = loadedSampler.description();
    EXPECT_FALSE(loaded.minFilter);
    EXPECT_TRUE(loaded.magFilter);
    EXPECT_FALSE(loaded.mipFilter);
    EXPECT_EQ(loaded.addressU, NWB::Core::SamplerAddressMode::Wrap);
    EXPECT_EQ(loaded.addressV, NWB::Core::SamplerAddressMode::Mirror);
    EXPECT_EQ(loaded.addressW, NWB::Core::SamplerAddressMode::Border);
    EXPECT_EQ(loaded.maxAnisotropy, 4.0f);
    EXPECT_EQ(loaded.mipBias, -0.25f);
    EXPECT_EQ(loaded.borderColor, NWB::Core::Color(0.125f, 0.25f, 0.5f, 1.0f));
    EXPECT_EQ(logger.errorCount(), 0u);
}


TEST(AssetsGraphics, SamplerCookerBuildsSamplerAsset){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
    ASSERT_TRUE(PrepareAssetsGraphicsCookCase(
        testArena,
        "sampler_cooker_round_trip",
        root,
        outputDirectory
    ));

    const Path assetRoot = root / "assets";
    ASSERT_TRUE(WriteTextFile(assetRoot / "samplers" / "linear_clamp.nwb", s_SamplerTestMetadata));
    ASSERT_TRUE(CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }));

    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    ASSERT_TRUE(LoadCookedAsset<NWB::Impl::SamplerAssetCodec>(
        testArena,
        outputDirectory,
        Name("project/samplers/linear_clamp"),
        loadedAsset
    ));
    ASSERT_NE(loadedAsset.get(), nullptr);
    const NWB::Impl::Sampler& sampler = static_cast<const NWB::Impl::Sampler&>(*loadedAsset);
    const NWB::Core::SamplerDesc& description = sampler.description();
    EXPECT_FALSE(description.minFilter);
    EXPECT_TRUE(description.magFilter);
    EXPECT_FALSE(description.mipFilter);
    EXPECT_EQ(description.addressU, NWB::Core::SamplerAddressMode::Wrap);
    EXPECT_EQ(description.addressV, NWB::Core::SamplerAddressMode::Mirror);
    EXPECT_EQ(description.addressW, NWB::Core::SamplerAddressMode::Border);
    EXPECT_EQ(description.maxAnisotropy, 4.0f);
    EXPECT_EQ(description.mipBias, -0.25f);

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
    EXPECT_EQ(logger.errorCount(), 0u);
}
