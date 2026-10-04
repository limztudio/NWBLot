// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_sampler{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using AString = AssetsGraphicsFixture::AString;
using CapturingLogger = AssetsGraphicsFixture::CapturingLogger;
using Path = AssetsGraphicsFixture::Path;
using TestArena = AssetsGraphicsFixture::TestArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_SamplerTestMetadata =
    "sampler asset;\n\n"
    "asset.min_filter = \"nearest\";\n"
    "asset.mag_filter = \"linear\";\n"
    "asset.mip_filter = \"nearest\";\n"
    "asset.address_u = \"wrap\";\n"
    "asset.address_v = \"mirror\";\n"
    "asset.address_w = \"border\";\n"
    "asset.reduction = \"standard\";\n"
    "asset.max_anisotropy = 4.0;\n"
    "asset.mip_bias = -0.25;\n"
;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AssetsGraphics, SamplerCodecRejectsUnsupportedReductionAndFixedBorderColor){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(
        logger, NWB::Core::Common::LoggerBreakPolicy::BreakOnFatal
    );

    TestArena testArena;
    NWB::Impl::Sampler sampler(testArena.arena, Name("project/samplers/binary_admission"));
    NWB::Core::SamplerDesc description;
    description.borderColor = NWB::Core::Color(0.0f, 0.0f, 0.0f, 0.0f);
    description.addressW = NWB::Core::SamplerAddressMode::Border;
    sampler.setDescription(description);

    NWB::Impl::SamplerAssetCodec codec;
    NWB::Core::Assets::AssetBytes binary = AssetsGraphicsFixture::MakeAssetBytes(testArena);
    ASSERT_TRUE(codec.serialize(sampler, binary));
    ASSERT_EQ(binary.size(), sizeof(NWB::Impl::SamplerBinaryPayload::HeaderBinary));
    const NWB::Core::Assets::AssetBytes validBinary(binary);

    for(const NWB::Core::SamplerReductionType::Enum reduction : {
        NWB::Core::SamplerReductionType::Minimum, NWB::Core::SamplerReductionType::Maximum
    }){
        binary = validBinary;
        const u32 reductionValue = static_cast<u32>(reduction);
        GLB_MEMCPY(
            binary.data() + offsetof(NWB::Impl::SamplerBinaryPayload::HeaderBinary, reductionType),
            sizeof(reductionValue),
            &reductionValue,
            sizeof(reductionValue)
        );
        UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
        EXPECT_FALSE(codec.deserialize(testArena.arena, sampler.virtualPath(), binary, loadedAsset));
        EXPECT_EQ(loadedAsset.get(), nullptr);

        description.reductionType = reduction;
        sampler.setDescription(description);
        EXPECT_FALSE(codec.serialize(sampler, binary));
    }

    for(const usize borderOffset : {
        offsetof(NWB::Impl::SamplerBinaryPayload::HeaderBinary, borderColorR),
        offsetof(NWB::Impl::SamplerBinaryPayload::HeaderBinary, borderColorG),
        offsetof(NWB::Impl::SamplerBinaryPayload::HeaderBinary, borderColorB),
        offsetof(NWB::Impl::SamplerBinaryPayload::HeaderBinary, borderColorA)
    }){
        binary = validBinary;
        const f32 nonblack = 1.0f;
        GLB_MEMCPY(binary.data() + borderOffset, sizeof(nonblack), &nonblack, sizeof(nonblack));
        UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
        EXPECT_FALSE(codec.deserialize(testArena.arena, sampler.virtualPath(), binary, loadedAsset));
        EXPECT_EQ(loadedAsset.get(), nullptr);
    }

    description.reductionType = NWB::Core::SamplerReductionType::Standard;
    description.borderColor = NWB::Core::Color(0.0f, 0.0f, 0.0f, 1.0f);
    sampler.setDescription(description);
    EXPECT_FALSE(codec.serialize(sampler, binary));

    for(const NWB::Core::SamplerReductionType::Enum reduction : {
        NWB::Core::SamplerReductionType::Standard, NWB::Core::SamplerReductionType::Comparison
    }){
        description.reductionType = reduction;
        description.borderColor = NWB::Core::Color(0.0f, 0.0f, 0.0f, 0.0f);
        sampler.setDescription(description);
        ASSERT_TRUE(codec.serialize(sampler, binary));
        UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
        ASSERT_TRUE(codec.deserialize(testArena.arena, sampler.virtualPath(), binary, loadedAsset));
        ASSERT_NE(loadedAsset.get(), nullptr);
        const NWB::Impl::Sampler& loaded = static_cast<const NWB::Impl::Sampler&>(*loadedAsset);
        EXPECT_EQ(loaded.description().reductionType, reduction);
        EXPECT_EQ(loaded.description().borderColor, NWB::Core::Color(0.0f, 0.0f, 0.0f, 0.0f));
    }
    EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT("sampler description is invalid")));
}


TEST(AssetsGraphics, SamplerCookerBuildsSamplerAsset){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
    ASSERT_TRUE(AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(
        testArena,
        "sampler_cooker_round_trip",
        root,
        outputDirectory
    ));

    const Path assetRoot = root / "assets";
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(assetRoot / "samplers" / "linear_clamp.nwb", s_SamplerTestMetadata));
    ASSERT_TRUE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }));

    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    ASSERT_TRUE(AssetsGraphicsFixture::LoadCookedAsset<NWB::Impl::SamplerAssetCodec>(
        testArena,
        outputDirectory,
        Name("project/samplers/linear_clamp"),
        loadedAsset,
        1u
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
    EXPECT_EQ(description.borderColor, NWB::Core::Color(0.0f, 0.0f, 0.0f, 0.0f));

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
    EXPECT_EQ(logger.errorCount(), 0u);
}

TEST(AssetsGraphics, SamplerCookerRejectsFixedBorderColorAndUnsupportedReductionMetadata){
    static constexpr AStringView s_UnsupportedAssignments[] = {
        "asset.border_color = [0.0, 0.0, 0.0, 0.0];\n",
        "asset.reduction = \"minimum\";\n",
        "asset.reduction = \"maximum\";\n",
    };

    for(const AStringView assignment : s_UnsupportedAssignments){
        CapturingLogger logger;
        NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(
            logger, NWB::Core::Common::LoggerBreakPolicy::BreakOnFatal
        );
        TestArena testArena;
        AString metadata(s_SamplerTestMetadata);
        const AStringView reductionAssignment = "asset.reduction = \"standard\";\n";
        if(assignment.starts_with("asset.reduction")){
            const usize reductionPosition = metadata.find(reductionAssignment);
            ASSERT_NE(reductionPosition, AString::npos);
            metadata.replace(reductionPosition, reductionAssignment.size(), assignment);
        }
        else
            metadata += assignment;

        NWB::Core::Metascript::Document document(testArena.arena);
        ASSERT_TRUE(document.parse(AStringView(metadata)));
        const Path assetRoot = AssetsGraphicsFixture::AssetsGraphicsTestCaseRoot(testArena, "sampler_unsupported_metadata") / "assets";
        const Path metadataPath = assetRoot / "samplers" / "linear_clamp.nwb";
        NWB::Impl::SamplerCookEntry entry(testArena.arena);
        NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_CodecScratchArena);
        EXPECT_FALSE(NWB::Impl::ParseSamplerCookMetadata(
            assetRoot,
            "project",
            metadataPath,
            document,
            entry,
            scratchArena
        )) << assignment;
        const TStringView expectedDiagnostic = assignment.starts_with("asset.reduction")
            ? GLB_TEXT("unsupported reduction type")
            : GLB_TEXT("unsupported asset field 'border_color'")
        ;
        EXPECT_TRUE(logger.sawErrorContaining(expectedDiagnostic)) << assignment;
    }
}

TEST(AssetsGraphics, SamplerCookerRejectsDeprecatedVersionMetadata){
#if defined(GLB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    AString metadata(s_SamplerTestMetadata);
    const usize firstFieldPosition = metadata.find("asset.min_filter");
    ASSERT_NE(firstFieldPosition, AString::npos);
    metadata.replace(firstFieldPosition, 0u, "asset.version = 1;\n");

    NWB::Core::Metascript::Document document(testArena.arena);
    ASSERT_TRUE(document.parse(AStringView(metadata.data(), metadata.size())));

    const Path assetRoot = AssetsGraphicsFixture::AssetsGraphicsTestCaseRoot(testArena, "sampler_unsupported_metadata") / "assets";
    const Path metadataPath = assetRoot / "samplers" / "linear_clamp.nwb";
    NWB::Impl::SamplerCookEntry entry(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_CodecScratchArena);
    EXPECT_FALSE(NWB::Impl::ParseSamplerCookMetadata(
        assetRoot,
        "project",
        metadataPath,
        document,
        entry,
        scratchArena
    ));
    EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT("unsupported asset field 'version'")));
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

