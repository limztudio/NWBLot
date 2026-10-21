// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_FINAL)
constexpr u32 s_ExpectedDualCount = 2u;
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_material{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_PROJECT_MATERIAL_INTERFACES_TEST_SURFACE = "project/material_interfaces/test_surface";
#if defined(NWB_FINAL)
static constexpr AStringView s_GENERATED_AVBOIT_ACCUMULATE_PS_PROJECT_M = "generated/avboit_accumulate_ps/project/materials/test_material";
static constexpr AStringView s_GENERATED_AVBOIT_OCCUPANCY_PS_PROJECT_MA = "generated/avboit_occupancy_ps/project/materials/test_material";
static constexpr AStringView s_GENERATED_AVBOIT_EXTINCTION_PS_PROJECT_M = "generated/avboit_extinction_ps/project/materials/test_material";
#endif
static constexpr AStringView s_PROJECT = "project";
static constexpr AStringView s_ASSETS_DIR = "assets";
static constexpr AStringView s_MATERIALS = "materials";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CapturingLogger = AssetsGraphicsFixture::CapturingLogger;
using Path = AssetsGraphicsFixture::Path;
using TestArena = AssetsGraphicsFixture::TestArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<NWB::Impl::MaterialCookEntry> ParseMaterialEntryFromMetaText(const AStringView metaText, TestArena& testArena, NWB::Core::Alloc::ScratchArena& scratchArena){
    NWB::Core::Metascript::Document doc(testArena.arena);
    if(!doc.parse(metaText))
        return MakeUnexpected(Failure{});

    const Path assetRoot = AssetsGraphicsFixture::AssetsGraphicsTestCaseRoot(testArena, "material_meta") / s_ASSETS_DIR;
    const Path nwbFilePath = assetRoot / s_MATERIALS / "test_material.nwb";
    return NWB::Impl::ParseMaterialCookMetadata(assetRoot, s_PROJECT, nwbFilePath, doc, testArena.arena, scratchArena);
}

static Expected<NWB::Impl::Material> BuildMaterialFromBindAndMeta(const AStringView bindText, const AStringView materialText, const AStringView caseName, TestArena& testArena, NWB::Core::Alloc::ScratchArena& scratchArena){
    auto materialEntryResult = ParseMaterialEntryFromMetaText(materialText, testArena, scratchArena);
    if(!materialEntryResult)
        return MakeUnexpected(Failure{});

    auto bindRootResult = AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, caseName);
    if(!bindRootResult)
        return MakeUnexpected(bindRootResult.error());
    Path bindRoot = Move(*bindRootResult);
    Expected<NWB::Impl::Material> built = MakeUnexpected(Failure{});
    auto bindEntryResult = AssetsGraphicsFixture::ParseMaterialBindFromText(testArena, bindText, bindRoot, scratchArena);
    if(bindEntryResult){
        NWB::Impl::MaterialBindEntry& bindEntry = *bindEntryResult;
        bindEntry.virtualPath = s_PROJECT_MATERIAL_INTERFACES_TEST_SURFACE;

        NWB::Impl::ShaderCook::CookVector<NWB::Impl::MaterialBindEntry> bindEntries(testArena.arena);
        bindEntries.push_back(Move(bindEntry));
        NWB::Impl::ShaderCook::CookVector<NWB::Impl::MaterialCookEntry> materialEntries(testArena.arena);
        materialEntries.push_back(Move(*materialEntryResult));
        if(NWB::Impl::ValidateMaterialCookInterfaces(bindEntries, materialEntries, scratchArena))
            built = NWB::Impl::BuildMaterialAsset(materialEntries[0u], testArena.arena);
    }

    if(!bindRoot.empty()){
        if(!RemoveAllIfExists(bindRoot))
            return MakeUnexpected(Failure{});
    }
    return built;
}

#if defined(NWB_FINAL)
static Expected<UniquePtr<NWB::Core::Assets::IAsset>> RoundTripMaterialAssetCodec(TestArena& testArena, NWB::Impl::MaterialAssetCodec& codec, const NWB::Impl::Material& material){
    NWB::Core::Assets::AssetBytes binary = AssetsGraphicsFixture::MakeAssetBytes(testArena);
    const bool serialized = codec.serialize(material, binary);
    EXPECT_TRUE(serialized);
    EXPECT_FALSE(binary.empty());
    if(!serialized || binary.empty())
        return MakeUnexpected(Failure{});

    auto deserialized = codec.deserialize(testArena.arena, material.virtualPath(), binary);
    EXPECT_TRUE(deserialized);
    return deserialized;
}

static void SetGeneratedMaterialAvboitPixelShaders(NWB::Impl::Material& material){
    NWB::Core::Assets::AssetRef<NWB::Impl::PixelShader> accumulatePixelShader;
    accumulatePixelShader.virtualPath = Name(s_GENERATED_AVBOIT_ACCUMULATE_PS_PROJECT_M);
    material.setAvboitAccumulatePixelShader(accumulatePixelShader);

    NWB::Core::Assets::AssetRef<NWB::Impl::PixelShader> occupancyPixelShader;
    occupancyPixelShader.virtualPath = Name(s_GENERATED_AVBOIT_OCCUPANCY_PS_PROJECT_MA);
    material.setAvboitOccupancyPixelShader(occupancyPixelShader);

    NWB::Core::Assets::AssetRef<NWB::Impl::PixelShader> extinctionPixelShader;
    extinctionPixelShader.virtualPath = Name(s_GENERATED_AVBOIT_EXTINCTION_PS_PROJECT_M);
    material.setAvboitExtinctionPixelShader(extinctionPixelShader);
}
#endif


TEST(AssetsGraphics, MaterialCookRejectsMissingAvboitPixelShaders){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_MaterialScratchArena);

    EXPECT_FALSE(BuildMaterialFromBindAndMeta(AssetsGraphicsFixture::s_MinimalMaterialBindSource, AssetsGraphicsFixture::s_TransparentMaterialMeta, "material_cook_missing_avboit_transparent", testArena, scratchArena));

    EXPECT_FALSE(BuildMaterialFromBindAndMeta(AssetsGraphicsFixture::s_MinimalMaterialBindSource, AssetsGraphicsFixture::s_RefractiveMaterialMeta, "material_cook_missing_avboit_refractive", testArena, scratchArena));

    EXPECT_EQ(logger.errorCount(), s_ExpectedDualCount);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("AVBOIT pixel shaders must be present if and only if it is transparent")));
#endif
}

TEST(AssetsGraphics, MaterialMetadataRejectsMissingShaderVariant){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    static constexpr AStringView s_MissingShaderVariantMaterialMeta = R"NWB_META(material asset;

asset.interface = "project/material_interfaces/test_surface.bind";
asset.bxdf = "project/shaders/material_bxdf.bxdf";
asset.transparent = 0;
asset.two_sided = 0;
asset.refractive = 0;

asset.shaders = {
    "mesh": "project/shaders/material_mesh",
    "ps": "project/shaders/material_ps",
};

)NWB_META";

    TestArena testArena;
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_MaterialScratchArena);
    EXPECT_FALSE(ParseMaterialEntryFromMetaText(s_MissingShaderVariantMaterialMeta, testArena, scratchArena));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("field 'shader_variant' is required")));
#endif
}

TEST(AssetsGraphics, MaterialMetadataRejectsMissingRenderProperties){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    static constexpr AStringView s_MissingRefractiveMaterialMeta = R"NWB_META(material asset;

asset.interface = "project/material_interfaces/test_surface.bind";
asset.bxdf = "project/shaders/material_bxdf.bxdf";
asset.transparent = 0;
asset.two_sided = 0;

asset.shaders = {
    "mesh": "project/shaders/material_mesh",
    "ps": "project/shaders/material_ps",
};
asset.shader_variant = "default";

)NWB_META";

    TestArena testArena;
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_MaterialScratchArena);
    EXPECT_FALSE(ParseMaterialEntryFromMetaText(s_MissingRefractiveMaterialMeta, testArena, scratchArena));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("'refractive' is required and must be 0 or 1")));
#endif
}

TEST(AssetsGraphics, MaterialMetadataRejectsExplicitOpticalStages){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_MaterialScratchArena);
    const auto expectRejected = [&](const AStringView metaText){
        EXPECT_FALSE(ParseMaterialEntryFromMetaText(metaText, testArena, scratchArena));
        EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("explicit 'shaders' cannot be used with transparent/refractive materials")));
    };

    expectRejected(AssetsGraphicsFixture::s_ExplicitTransparentMaterialMeta);
    expectRejected(AssetsGraphicsFixture::s_ExplicitRefractiveMaterialMeta);
#endif
}

TEST(AssetsGraphics, MaterialMetadataRejectsEngineRootedPolicySelectors){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    static constexpr AStringView s_EngineRootedInterfaceMeta = R"NWB_META(material asset;

asset.interface = "engine/material_interfaces/test_surface.bind";
asset.bxdf = "project/shaders/material_bxdf.bxdf";
asset.transparent = 0;
asset.two_sided = 0;
asset.refractive = 0;

asset.shaders = {
    "mesh": "project/shaders/material_mesh",
    "ps": "project/shaders/material_ps",
};
asset.shader_variant = "default";

)NWB_META";

    static constexpr AStringView s_EngineRootedSurfaceMeta = R"NWB_META(material asset;

asset.interface = "project/material_interfaces/test_surface.bind";
asset.surface = "engine/shaders/surface.surface";
asset.bxdf = "project/shaders/material_bxdf.bxdf";
asset.transparent = 0;
asset.two_sided = 0;
asset.refractive = 0;
asset.shader_variant = "default";

)NWB_META";

    static constexpr AStringView s_EngineRootedBxdfMeta = R"NWB_META(material asset;

asset.interface = "project/material_interfaces/test_surface.bind";
asset.bxdf = "engine/shaders/material_bxdf.bxdf";
asset.transparent = 0;
asset.two_sided = 0;
asset.refractive = 0;

asset.shaders = {
    "mesh": "project/shaders/material_mesh",
    "ps": "project/shaders/material_ps",
};
asset.shader_variant = "default";

)NWB_META";

    static constexpr AStringView s_EngineRootedStageShaderMeta = R"NWB_META(material asset;

asset.interface = "project/material_interfaces/test_surface.bind";
asset.bxdf = "project/shaders/material_bxdf.bxdf";
asset.transparent = 0;
asset.two_sided = 0;
asset.refractive = 0;

asset.shaders = {
    "mesh": "engine/graphics/mesh/shared_ms",
    "ps": "project/shaders/material_ps",
};
asset.shader_variant = "default";

)NWB_META";

    TestArena testArena;
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_MaterialScratchArena);
    const auto expectRejected = [&](const AStringView metaText, const TStringView expectedError){
        EXPECT_FALSE(ParseMaterialEntryFromMetaText(metaText, testArena, scratchArena));
        EXPECT_TRUE(logger.sawErrorContaining(expectedError));
    };

    expectRejected(s_EngineRootedInterfaceMeta, NWB_TEXT("interface must use the project/ virtual root"));
    expectRejected(s_EngineRootedSurfaceMeta, NWB_TEXT("field 'surface' must use the project/ virtual root"));
    expectRejected(s_EngineRootedBxdfMeta, NWB_TEXT("field 'bxdf' must use the project/ virtual root"));
    expectRejected(s_EngineRootedStageShaderMeta, NWB_TEXT("shader stage 'mesh' must use the project/ virtual root"));
    EXPECT_EQ(logger.errorCount(), 4u);
#endif
}

TEST(AssetsGraphics, MaterialCodecTypedLayoutBoundary){
#if defined(NWB_FINAL)
    TestArena testArena;
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_MaterialScratchArena);
    NWB::Impl::Material material(testArena.arena);

    {
        CapturingLogger logger;
        NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

        auto built = BuildMaterialFromBindAndMeta(AssetsGraphicsFixture::s_MinimalMaterialBindSource, AssetsGraphicsFixture::s_BlockScopedMaterialMeta, "material_codec_typed_layout_boundary", testArena, scratchArena);
        EXPECT_TRUE(built);
        if(!built)
            return;
        material = Move(*built);

        NWB::Impl::MaterialAssetCodec codec;
        material.setTransparent(true);

#if defined(NWB_FINAL)
        NWB::Core::Assets::AssetBytes invalidBinary = AssetsGraphicsFixture::MakeAssetBytes(testArena);
        EXPECT_FALSE(codec.serialize(material, invalidBinary));
        EXPECT_TRUE(invalidBinary.empty());
        EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("AVBOIT pixel shaders must be present if and only if the material is transparent")));
#endif

        SetGeneratedMaterialAvboitPixelShaders(material);
        UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
        auto loadedAssetResult = RoundTripMaterialAssetCodec(testArena, codec, material);
        ASSERT_TRUE(loadedAssetResult);
        loadedAsset = Move(*loadedAssetResult);
#if defined(NWB_FINAL)
        EXPECT_EQ(logger.errorCount(), 1u);
#else
        EXPECT_EQ(logger.errorCount(), 0u);
#endif
    }

#if defined(NWB_FINAL)
    {
        CapturingLogger logger;
        NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

        NWB::Impl::MaterialAssetCodec codec;
        NWB::Core::Assets::AssetBytes binary = AssetsGraphicsFixture::MakeAssetBytes(testArena);
        EXPECT_TRUE(codec.serialize(material, binary));

        const auto typedLayoutOffsets = AssetsGraphicsFixture::FindMaterialBinaryTypedLayoutOffsets(binary);
        ASSERT_TRUE(typedLayoutOffsets);
        const usize layoutHashOffset = typedLayoutOffsets->layoutHashOffset;
        const usize blockByteCountOffset = typedLayoutOffsets->blockByteCountOffset;

        NWB::Core::Assets::AssetBytes retiredFixtureLayoutBinary = binary;
        EXPECT_TRUE(AssetsGraphicsFixture::OverwritePOD(retiredFixtureLayoutBinary, 0u, u32{ 0x4D544C39u }));
        AssetsGraphicsFixture::CheckCodecRejectsBinary(testArena, codec, material.virtualPath(), retiredFixtureLayoutBinary);

        NWB::Core::Assets::AssetBytes hashMismatchBinary = binary;
        const u64 invalidLayoutHash = material.typedLayoutHash() == Limit<u64>::s_Max ? material.typedLayoutHash() - 1u : material.typedLayoutHash() + 1u;
        EXPECT_TRUE(AssetsGraphicsFixture::OverwritePOD(hashMismatchBinary, layoutHashOffset, invalidLayoutHash));
        AssetsGraphicsFixture::CheckCodecRejectsBinary(testArena, codec, material.virtualPath(), hashMismatchBinary);

        NWB::Core::Assets::AssetBytes byteSizeMismatchBinary = binary;
        EXPECT_FALSE(material.typedBlockBytes().empty());
        EXPECT_TRUE(AssetsGraphicsFixture::OverwritePOD(byteSizeMismatchBinary, blockByteCountOffset, static_cast<u32>(material.typedBlockBytes().size() - 1u)));
        AssetsGraphicsFixture::CheckCodecRejectsBinary(testArena, codec, material.virtualPath(), byteSizeMismatchBinary);

        constexpr usize s_AvboitPixelShaderBinaryBytes = sizeof(u32) + sizeof(NameHash);
        ASSERT_GE(binary.size(), s_AvboitPixelShaderBinaryBytes * 3u);
        const usize occupancyPresenceOffset = binary.size() - s_AvboitPixelShaderBinaryBytes * s_ExpectedDualCount;
        NWB::Core::Assets::AssetBytes missingOccupancyBinary = binary;
        EXPECT_TRUE(AssetsGraphicsFixture::OverwritePOD(missingOccupancyBinary, occupancyPresenceOffset, static_cast<u32>(0u)));
        missingOccupancyBinary.erase(missingOccupancyBinary.begin() + occupancyPresenceOffset + sizeof(u32), missingOccupancyBinary.begin() + occupancyPresenceOffset + s_AvboitPixelShaderBinaryBytes);
        AssetsGraphicsFixture::CheckCodecRejectsBinary(testArena, codec, material.virtualPath(), missingOccupancyBinary);

        EXPECT_EQ(logger.errorCount(), 4u);
        EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("typed layout hash mismatch")));
        EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("typed block byte count does not match typed layout")));
        EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("AVBOIT pixel shaders must be present if and only if the material is transparent")));
    }
#endif
#endif
}

TEST(AssetsGraphics, MaterialFailedReloadPreservesTypedLayoutStagesFlagsAndAvboitReferences){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerGuard(logger, NWB::Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_MaterialScratchArena);
    auto built = BuildMaterialFromBindAndMeta(AssetsGraphicsFixture::s_MinimalMaterialBindSource, AssetsGraphicsFixture::s_BlockScopedMaterialMeta, "material_reload_admission", testArena, scratchArena);
    ASSERT_TRUE(built);
    NWB::Impl::Material material = Move(*built);
    material.setTransparent(true);
    material.setTwoSided(true);
    material.setAvboitAccumulatePixelShader(NWB::Core::Assets::AssetRef<NWB::Impl::PixelShader>("project/shaders/reload_accumulate"));
    material.setAvboitOccupancyPixelShader(NWB::Core::Assets::AssetRef<NWB::Impl::PixelShader>("project/shaders/reload_occupancy"));
    material.setAvboitExtinctionPixelShader(NWB::Core::Assets::AssetRef<NWB::Impl::PixelShader>("project/shaders/reload_extinction"));
    NWB::Impl::MaterialAssetCodec codec;
    NWB::Core::Assets::AssetBytes binary(testArena.arena);
    ASSERT_TRUE(codec.serialize(material, binary));
    ASSERT_TRUE(material.loadBinary(binary));
    for(usize prefix = 0u; prefix < binary.size(); ++prefix){
        NWB::Core::Assets::AssetBytes malformed(binary);
        malformed.resize(prefix);
        EXPECT_FALSE(material.loadBinary(malformed)) << prefix;
        NWB::Core::Assets::AssetBytes retained(testArena.arena);
        ASSERT_TRUE(codec.serialize(material, retained));
        EXPECT_EQ(retained, binary) << prefix;
    }
    const auto layoutOffsets = AssetsGraphicsFixture::FindMaterialBinaryTypedLayoutOffsets(binary);
    ASSERT_TRUE(layoutOffsets);
    for(u32 invalidCase = 0u; invalidCase < 4u; ++invalidCase){
        NWB::Core::Assets::AssetBytes malformed(binary);
        switch(invalidCase){
        case 0u:
            ASSERT_TRUE(AssetsGraphicsFixture::OverwritePOD(malformed, 0u, u32{ 0u }));
            break;
        case 1u:
            ASSERT_TRUE(AssetsGraphicsFixture::OverwritePOD(malformed, layoutOffsets->layoutHashOffset, u64{ 0u }));
            break;
        case 2u:
            ASSERT_TRUE(AssetsGraphicsFixture::OverwritePOD(malformed, binary.size() - sizeof(NameHash) - sizeof(u32), u32{ 2u }));
            break;
        case 3u:
            malformed.push_back(0u);
            break;
        }
        EXPECT_FALSE(material.loadBinary(malformed)) << invalidCase;
        NWB::Core::Assets::AssetBytes retained(testArena.arena);
        ASSERT_TRUE(codec.serialize(material, retained));
        EXPECT_EQ(retained, binary) << invalidCase;
    }
    EXPECT_EQ(logger.errorCount(), binary.size() + 4u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

