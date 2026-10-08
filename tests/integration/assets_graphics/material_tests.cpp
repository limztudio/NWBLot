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
static constexpr AStringView s_PROJECT_MATERIALS_TEST_MATERIAL = "project/materials/test_material";
#if defined(NWB_FINAL)
static constexpr AStringView s_GENERATED_AVBOIT_ACCUMULATE_PS_PROJECT_M = "generated/avboit_accumulate_ps/project/materials/test_material";
static constexpr AStringView s_GENERATED_AVBOIT_OCCUPANCY_PS_PROJECT_MA = "generated/avboit_occupancy_ps/project/materials/test_material";
static constexpr AStringView s_GENERATED_AVBOIT_EXTINCTION_PS_PROJECT_M = "generated/avboit_extinction_ps/project/materials/test_material";
#endif
static constexpr AStringView s_PS = "ps";
static constexpr AStringView s_PROJECT = "project";
static constexpr AStringView s_TESTS = "tests";
static constexpr AStringView s_MATERIAL_BIND_INCLUDES = "material_bind_includes";
static constexpr AStringView s_MATERIAL_INTERFACES = "material_interfaces";
static constexpr AStringView s_TEST_SURFACE_BIND = "test_surface.bind";
static constexpr AStringView s_SHADERS = "shaders";
static constexpr AStringView s_ENGINE_SAMPLERS_LINEAR_CLAMP = "engine/samplers/linear_clamp";
static constexpr AStringView s_CACHE = "cache";
static constexpr AStringView s_ASSETS_DIR = "assets";
static constexpr AStringView s_MATERIALS = "materials";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CapturingLogger = AssetsGraphicsFixture::CapturingLogger;
using Path = AssetsGraphicsFixture::Path;
using TestArena = AssetsGraphicsFixture::TestArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_FINAL)
static Expected<NWB::Impl::MaterialCookEntry> ParseMaterialEntryFromMetaText(
    const AStringView metaText,
    TestArena& testArena,
    NWB::Core::Alloc::ScratchArena& scratchArena
){
    NWB::Core::Metascript::Document doc(testArena.arena);
    if(!doc.parse(metaText))
        return MakeUnexpected(Failure{});

    const Path assetRoot = AssetsGraphicsFixture::AssetsGraphicsTestCaseRoot(testArena, "material_meta") / s_ASSETS_DIR;
    const Path nwbFilePath = assetRoot / s_MATERIALS / "test_material.nwb";
    return NWB::Impl::ParseMaterialCookMetadata(assetRoot, s_PROJECT, nwbFilePath, doc, testArena.arena, scratchArena);
}

static Expected<NWB::Impl::Material> BuildMaterialFromBindAndMeta(
    const AStringView bindText,
    const AStringView materialText,
    const AStringView caseName,
    TestArena& testArena,
    NWB::Core::Alloc::ScratchArena& scratchArena
){
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

static Expected<UniquePtr<NWB::Core::Assets::IAsset>> RoundTripMaterialAssetCodec(
    TestArena& testArena,
    NWB::Impl::MaterialAssetCodec& codec,
    const NWB::Impl::Material& material
){
    NWB::Core::Assets::AssetBytes binary = AssetsGraphicsFixture::MakeAssetBytes(testArena);
    const bool serialized = codec.serialize(material, binary);
    EXPECT_TRUE(serialized);
    EXPECT_FALSE(binary.empty());
    if(!serialized || binary.empty())
        return MakeUnexpected(Failure{});

    auto deserialized = codec.deserialize(
        testArena.arena,
        material.virtualPath(),
        binary
    );
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

    NWB::Impl::Material transparentMaterial(testArena.arena);
    EXPECT_FALSE(BuildMaterialFromBindAndMeta(
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        AssetsGraphicsFixture::s_TransparentMaterialMeta,
        "material_cook_missing_avboit_transparent",
        testArena,
        scratchArena
    ));

    NWB::Impl::Material refractiveMaterial(testArena.arena);
    EXPECT_FALSE(BuildMaterialFromBindAndMeta(
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        AssetsGraphicsFixture::s_RefractiveMaterialMeta,
        "material_cook_missing_avboit_refractive",
        testArena,
        scratchArena
    ));

    EXPECT_EQ(logger.errorCount(), s_ExpectedDualCount);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT(
        "AVBOIT pixel shaders must be present if and only if it is transparent"
    )));
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
    NWB::Impl::MaterialCookEntry materialEntry(testArena.arena);
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
    NWB::Impl::MaterialCookEntry materialEntry(testArena.arena);
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
        NWB::Impl::MaterialCookEntry materialEntry(testArena.arena);
        EXPECT_FALSE(ParseMaterialEntryFromMetaText(metaText, testArena, scratchArena));
        EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT(
            "explicit 'shaders' cannot be used with transparent/refractive materials"
        )));
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
        NWB::Impl::MaterialCookEntry materialEntry(testArena.arena);
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

        auto built = BuildMaterialFromBindAndMeta(
            AssetsGraphicsFixture::s_MinimalMaterialBindSource,
            AssetsGraphicsFixture::s_BlockScopedMaterialMeta,
            "material_codec_typed_layout_boundary",
            testArena,
            scratchArena
        );
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
        EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT(
            "AVBOIT pixel shaders must be present if and only if the material is transparent"
        )));
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
        EXPECT_TRUE(AssetsGraphicsFixture::OverwritePOD(
            byteSizeMismatchBinary,
            blockByteCountOffset,
            static_cast<u32>(material.typedBlockBytes().size() - 1u)
        ));
        AssetsGraphicsFixture::CheckCodecRejectsBinary(testArena, codec, material.virtualPath(), byteSizeMismatchBinary);

        constexpr usize s_AvboitPixelShaderBinaryBytes = sizeof(u32) + sizeof(NameHash);
        ASSERT_GE(binary.size(), s_AvboitPixelShaderBinaryBytes * 3u);
        const usize occupancyPresenceOffset = binary.size() - s_AvboitPixelShaderBinaryBytes * s_ExpectedDualCount;
        NWB::Core::Assets::AssetBytes missingOccupancyBinary = binary;
        EXPECT_TRUE(AssetsGraphicsFixture::OverwritePOD(missingOccupancyBinary, occupancyPresenceOffset, static_cast<u32>(0u)));
        missingOccupancyBinary.erase(
            missingOccupancyBinary.begin() + occupancyPresenceOffset + sizeof(u32),
            missingOccupancyBinary.begin() + occupancyPresenceOffset + s_AvboitPixelShaderBinaryBytes
        );
        AssetsGraphicsFixture::CheckCodecRejectsBinary(testArena, codec, material.virtualPath(), missingOccupancyBinary);

        EXPECT_EQ(logger.errorCount(), 4u);
        EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("typed layout hash mismatch")));
        EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT(
            "typed block byte count does not match typed layout"
        )));
        EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT(
            "AVBOIT pixel shaders must be present if and only if the material is transparent"
        )));
    }
#endif
#endif
}

TEST(AssetsGraphics, MaterialBindSchemaValidation){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_MaterialScratchArena);
    auto expectParseFailure = [&](
        const AStringView bindText,
        const AStringView caseName,
        const TStringView expectedError
    ){
        SCOPED_TRACE(caseName.data());

        auto invalidRootResult = AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, caseName);
        ASSERT_TRUE(invalidRootResult);
        Path invalidRoot = Move(*invalidRootResult);
        {
            CapturingLogger failureLogger;
            NWB::Core::Common::LoggerRegistrationGuard failureLoggerRegistrationGuard(
                failureLogger,
                NWB::Core::Common::LoggerBreakPolicy::ReportOnly
            );

            EXPECT_FALSE(AssetsGraphicsFixture::ParseMaterialBindFromText(testArena, bindText, invalidRoot, scratchArena));
            EXPECT_TRUE(failureLogger.sawErrorContaining(expectedError));
        }

        EXPECT_TRUE(RemoveAllIfExists(invalidRoot));
    };

    expectParseFailure(
        AssetsGraphicsFixture::s_UnknownBlockClassMaterialBindSource,
        "material_bind_unknown_block_class",
        NWB_TEXT("unsupported attribute 'material_project'")
    );
    expectParseFailure(
        AssetsGraphicsFixture::s_UnsupportedFieldTypeMaterialBindSource,
        "material_bind_unsupported_field_type",
        NWB_TEXT("unsupported type 'double'")
    );
    expectParseFailure(
        AssetsGraphicsFixture::s_InvalidDefaultMaterialBindSource,
        "material_bind_invalid_default",
        NWB_TEXT("attribute 'default' requires one non-empty string argument")
    );
    expectParseFailure(
        AssetsGraphicsFixture::s_MissingDefaultMaterialBindSource,
        "material_bind_missing_default",
        NWB_TEXT("must declare a default attribute")
    );
    expectParseFailure(
        AssetsGraphicsFixture::s_ResourceAttributeMaterialBindSource,
        "material_bind_resource_attribute",
        NWB_TEXT("has unsupported attribute 'texture_asset'")
    );
    for(const AStringView resourceType : { AStringView("texture2d"), AStringView("sampler") }){
        const auto retiredFixtureBind = StringFormat(
            testArena.arena,
            "[material_constant] struct NwbResourceMaterial{{ "
            "[fixture(\"builtin/material_fixture/checker_rgba8\")] {} resource; }}; NwbResourceMaterial surface;",
            resourceType
        );
        expectParseFailure(retiredFixtureBind, resourceType, NWB_TEXT("has unsupported attribute 'fixture'"));
    }
    expectParseFailure(
        AssetsGraphicsFixture::s_DuplicateInstanceMaterialBindSource,
        "material_bind_duplicate_instance",
        NWB_TEXT("duplicate struct instance declaration")
    );
    expectParseFailure(
        AssetsGraphicsFixture::s_InstanceOverrideMaterialBindSource,
        "material_bind_instance_override",
        NWB_TEXT("unsupported asset field 'instance_override'")
    );

    auto float1DefaultRootResult = AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "material_bind_float1_default");
    ASSERT_TRUE(float1DefaultRootResult);
    Path float1DefaultRoot = Move(*float1DefaultRootResult);
    auto float1DefaultEntryResult = AssetsGraphicsFixture::ParseMaterialBindFromText(testArena, AssetsGraphicsFixture::s_Float1DefaultMaterialBindSource, float1DefaultRoot, scratchArena);
    ASSERT_TRUE(float1DefaultEntryResult);
    NWB::Impl::MaterialBindEntry& float1DefaultEntry = *float1DefaultEntryResult;
    float1DefaultEntry.virtualPath = s_PROJECT_MATERIAL_INTERFACES_TEST_SURFACE;
    {
        SCOPED_TRACE("material_bind_float1_default_include");

        CapturingLogger failureLogger;
        NWB::Core::Common::LoggerRegistrationGuard failureLoggerRegistrationGuard(
            failureLogger,
            NWB::Core::Common::LoggerBreakPolicy::ReportOnly
        );

        EXPECT_FALSE(NWB::Impl::BuildMaterialBindIncludeSource(
            testArena.arena,
            float1DefaultEntry,
            scratchArena
        ));
        EXPECT_TRUE(failureLogger.sawErrorContaining(NWB_TEXT("default 'float1(1.0)'")));
    }

    const Name cacheInterface("project/material_interfaces/test_surface");
    NWB::Impl::MaterialBindTypedLayoutCache layoutCache(testArena.arena);
    {
        SCOPED_TRACE("material_bind_float1_default_cache");

        CapturingLogger failureLogger;
        NWB::Core::Common::LoggerRegistrationGuard failureLoggerRegistrationGuard(
            failureLogger,
            NWB::Core::Common::LoggerBreakPolicy::ReportOnly
        );

        EXPECT_FALSE(NWB::Impl::FindOrBuildMaterialBindTypedLayout(
            cacheInterface,
            float1DefaultEntry,
            layoutCache,
            scratchArena
        ));
        EXPECT_TRUE(failureLogger.sawErrorContaining(NWB_TEXT("default 'float1(1.0)'")));
    }
    EXPECT_TRUE(layoutCache.entries.empty());
    EXPECT_TRUE(layoutCache.lookup.empty());

    auto validCacheRootResult = AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "material_bind_cache_valid_after_failed_layout");
    ASSERT_TRUE(validCacheRootResult);
    Path validCacheRoot = Move(*validCacheRootResult);
    auto validCacheEntryResult = AssetsGraphicsFixture::ParseMaterialBindFromText(testArena, AssetsGraphicsFixture::s_MinimalMaterialBindSource, validCacheRoot, scratchArena);
    ASSERT_TRUE(validCacheEntryResult);
    NWB::Impl::MaterialBindEntry& validCacheEntry = *validCacheEntryResult;
    validCacheEntry.virtualPath = s_PROJECT_MATERIAL_INTERFACES_TEST_SURFACE;
    const auto cachedLayout = NWB::Impl::FindOrBuildMaterialBindTypedLayout(
        cacheInterface,
        validCacheEntry,
        layoutCache,
        scratchArena
    );
    ASSERT_TRUE(cachedLayout);
    EXPECT_NE(*cachedLayout, nullptr);
    EXPECT_EQ(layoutCache.entries.size(), 1u);
    EXPECT_EQ(layoutCache.lookup.size(), 1u);
    EXPECT_EQ(logger.errorCount(), 0u);

    EXPECT_TRUE(RemoveAllIfExists(float1DefaultRoot));
    EXPECT_TRUE(RemoveAllIfExists(validCacheRoot));
}


TEST(AssetsGraphics, MaterialBindEngineAndProjectResourceValidation){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    EXPECT_FALSE(NWB::Impl::IsSupportedMaterialResourceReference(
        NWB::Impl::MaterialResourceKind::SampledImage2D,
        "builtin/textures/checker"
    ));
    EXPECT_FALSE(NWB::Impl::IsSupportedMaterialResourceReference(
        NWB::Impl::MaterialResourceKind::Sampler,
        "project/samplers/../linear_clamp"
    ));
    EXPECT_FALSE(NWB::Impl::IsSupportedMaterialResourceReference(
        NWB::Impl::MaterialResourceKind::Sampler,
        "engine/samplers/../linear_clamp"
    ));
    EXPECT_TRUE(NWB::Impl::IsSupportedMaterialResourceReference(
        NWB::Impl::MaterialResourceKind::Sampler,
        "project/samplers/linear_clamp"
    ));
    EXPECT_TRUE(NWB::Impl::IsSupportedMaterialResourceReference(
        NWB::Impl::MaterialResourceKind::Sampler,
        s_ENGINE_SAMPLERS_LINEAR_CLAMP
    ));

    EXPECT_EQ(logger.errorCount(), 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AssetsGraphics, MaterialBindCookRejectsMalformedParametersAndInterfaceMismatch){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
#if defined(NWB_FINAL)
    auto invalidCookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, "material_bind_unknown_interface_parameter");
    ASSERT_TRUE(invalidCookCase);
    Path& invalidRoot = invalidCookCase->root;
    EXPECT_FALSE(AssetsGraphicsFixture::CookMaterialBindMaterialIntegration(
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        AssetsGraphicsFixture::s_UnknownInterfaceParameterMaterialMeta,
        testArena,
        *invalidCookCase
    ));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT(
        "parameter 'surface.missing' is not declared by interface"
    )));

    EXPECT_TRUE(RemoveAllIfExists(invalidRoot));

    auto flatCookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, "material_bind_flat_interface_parameter");
    ASSERT_TRUE(flatCookCase);
    Path& flatRoot = flatCookCase->root;
    EXPECT_FALSE(AssetsGraphicsFixture::CookMaterialBindMaterialIntegration(
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        AssetsGraphicsFixture::s_FlatInterfaceParameterMaterialMeta,
        testArena,
        *flatCookCase
    ));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT(
        "interface parameter 'runtime.fade_alpha' must be declared inside a block map"
    )));

    EXPECT_TRUE(RemoveAllIfExists(flatRoot));

    auto untypedParameterCookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, "material_bind_untyped_material_parameter");
    ASSERT_TRUE(untypedParameterCookCase);
    Path& untypedParameterRoot = untypedParameterCookCase->root;
    EXPECT_FALSE(AssetsGraphicsFixture::CookMaterialBindMaterialIntegration(
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        AssetsGraphicsFixture::s_UntypedMaterialParameterMeta,
        testArena,
        *untypedParameterCookCase
    ));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT(
        "has invalid value '0.25, 0.5, 0.75, 1.0'"
    )));

    EXPECT_TRUE(RemoveAllIfExists(untypedParameterRoot));

    auto vectorAliasParameterCookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, "material_bind_vector_alias_material_parameter");
    ASSERT_TRUE(vectorAliasParameterCookCase);
    Path& vectorAliasParameterRoot = vectorAliasParameterCookCase->root;
    EXPECT_FALSE(AssetsGraphicsFixture::CookMaterialBindMaterialIntegration(
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        AssetsGraphicsFixture::s_VectorAliasMaterialParameterMeta,
        testArena,
        *vectorAliasParameterCookCase
    ));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT(
        "has invalid value 'vec4(0.25, 0.5, 0.75, 1.0)'"
    )));

    EXPECT_TRUE(RemoveAllIfExists(vectorAliasParameterRoot));

    auto unsupportedFieldCookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, "material_bind_unsupported_material_field");
    ASSERT_TRUE(unsupportedFieldCookCase);
    Path& unsupportedFieldRoot = unsupportedFieldCookCase->root;
    EXPECT_FALSE(AssetsGraphicsFixture::CookMaterialBindMaterialIntegration(
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        AssetsGraphicsFixture::s_UnsupportedMaterialFieldMeta,
        testArena,
        *unsupportedFieldCookCase
    ));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("unsupported asset field 'compiler'")));

    EXPECT_TRUE(RemoveAllIfExists(unsupportedFieldRoot));

    auto incompleteBindCookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, "material_bind_incomplete_block_scoped");
    ASSERT_TRUE(incompleteBindCookCase);
    Path& incompleteBindRoot = incompleteBindCookCase->root;
    EXPECT_FALSE(AssetsGraphicsFixture::CookMaterialBindMaterialIntegration(
        AssetsGraphicsFixture::s_SurfaceOnlyMaterialBindSource,
        AssetsGraphicsFixture::s_BlockScopedMaterialMeta,
        testArena,
        *incompleteBindCookCase
    ));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT(
        "typed parameter 'runtime.fade_alpha' is not declared by interface"
    )));

    EXPECT_TRUE(RemoveAllIfExists(incompleteBindRoot));

    auto interfaceShaderMismatchCookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, "material_bind_interface_without_bind_shader");
    ASSERT_TRUE(interfaceShaderMismatchCookCase);
    Path& interfaceShaderMismatchRoot = interfaceShaderMismatchCookCase->root;
    Path& interfaceShaderMismatchOutputDirectory = interfaceShaderMismatchCookCase->outputDirectory;

    const Path interfaceShaderMismatchAssetRoot = interfaceShaderMismatchRoot / s_ASSETS_DIR;
    EXPECT_TRUE(AssetsGraphicsFixture::WriteMaterialBindMaterialIntegrationAssetsWithPixelSource(
        testArena,
        interfaceShaderMismatchAssetRoot,
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        AssetsGraphicsFixture::s_BlockScopedMaterialMeta,
        AssetsGraphicsFixture::s_UnboundMaterialShaderProbeSource
    ));
    EXPECT_FALSE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(
        testArena,
        interfaceShaderMismatchRoot,
        interfaceShaderMismatchOutputDirectory,
        { interfaceShaderMismatchAssetRoot }
    ));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("does not include a generated material bind")));

    EXPECT_TRUE(RemoveAllIfExists(interfaceShaderMismatchRoot));

    auto interfaceIdentityMismatchCookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, "material_bind_interface_identity_mismatch");
    ASSERT_TRUE(interfaceIdentityMismatchCookCase);
    Path& interfaceIdentityMismatchRoot = interfaceIdentityMismatchCookCase->root;
    Path& interfaceIdentityMismatchOutputDirectory = interfaceIdentityMismatchCookCase->outputDirectory;

    const Path interfaceIdentityMismatchAssetRoot = interfaceIdentityMismatchRoot / s_ASSETS_DIR;
    EXPECT_TRUE(AssetsGraphicsFixture::WriteMaterialBindMaterialIntegrationAssetsWithPixelSource(
        testArena,
        interfaceIdentityMismatchAssetRoot,
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        AssetsGraphicsFixture::s_BlockScopedMaterialMeta,
        AssetsGraphicsFixture::s_OtherMaterialBindShaderProbeSource
    ));
    EXPECT_TRUE(AssetsGraphicsFixture::WriteTextFile(
        interfaceIdentityMismatchAssetRoot / s_MATERIAL_INTERFACES / "other_surface.bind",
        AssetsGraphicsFixture::s_MinimalMaterialBindSource
    ));
    EXPECT_FALSE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(
        testArena,
        interfaceIdentityMismatchRoot,
        interfaceIdentityMismatchOutputDirectory,
        { interfaceIdentityMismatchAssetRoot }
    ));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT(
        "includes generated material bind interface 'project/material_interfaces/other_surface'"
    )));

    EXPECT_TRUE(RemoveAllIfExists(interfaceIdentityMismatchRoot));
#endif
#endif
}


TEST(AssetsGraphics, ShadowSurfaceDispatchIsolatesOverlappingBindApis){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    auto cookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, "shadow_transmittance_dispatch_overlapping_bind_apis");
    ASSERT_TRUE(cookCase);
    Path& root = cookCase->root;
    Path& outputDirectory = cookCase->outputDirectory;


    const Path assetRoot = root / s_ASSETS_DIR;
    const bool assetsWritten =
        AssetsGraphicsFixture::WriteTextFile(
            assetRoot / s_MATERIAL_INTERFACES / "shadow_dispatch_first.bind",
            AssetsGraphicsFixture::s_ShadowDispatchFirstMaterialBindSource
        )
        && AssetsGraphicsFixture::WriteTextFile(
            assetRoot / s_MATERIAL_INTERFACES / "shadow_dispatch_second.bind",
            AssetsGraphicsFixture::s_ShadowDispatchSecondMaterialBindSource
        )
        && AssetsGraphicsFixture::WriteTextFile(
            assetRoot / s_SHADERS / "shadow_dispatch_shared_surface_helper.slangi",
            AssetsGraphicsFixture::s_ShadowDispatchSharedSurfaceHelperSource
        )
        && AssetsGraphicsFixture::WriteTextFile(
            assetRoot / s_SHADERS / "shadow_dispatch_first.surface",
            AssetsGraphicsFixture::s_ShadowDispatchFirstMaterialSurfaceSource
        )
        && AssetsGraphicsFixture::WriteTextFile(
            assetRoot / s_SHADERS / "shadow_dispatch_second.surface",
            AssetsGraphicsFixture::s_ShadowDispatchSecondMaterialSurfaceSource
        )
        && AssetsGraphicsFixture::WriteTextFile(assetRoot / s_SHADERS / "shadow_dispatch.bxdf", AssetsGraphicsFixture::s_MaterialBindBxdfSource)
        && AssetsGraphicsFixture::WriteTextFile(
            assetRoot / s_MATERIALS / "shadow_dispatch_first.nwb",
            AssetsGraphicsFixture::s_ShadowDispatchFirstMaterialMeta
        )
        && AssetsGraphicsFixture::WriteTextFile(
            assetRoot / s_MATERIALS / "shadow_dispatch_second.nwb",
            AssetsGraphicsFixture::s_ShadowDispatchSecondMaterialMeta
        )
    ;
    ASSERT_TRUE(assetsWritten);

    const Path engineAssetRoot = AssetsGraphicsFixture::AssetsGraphicsTestRepoRoot(testArena) / "impl" / s_ASSETS_DIR;
    const bool cooked = AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(
        testArena,
        root,
        outputDirectory,
        { engineAssetRoot, assetRoot }
    );
    EXPECT_TRUE(cooked);
    EXPECT_EQ(logger.errorCount(), 0u);

    EXPECT_TRUE(RemoveAllIfExists(root));
}

TEST(AssetsGraphics, MaterialRejectsMissingInterfaceCookIntegration){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    auto cookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, "material_missing_interface_rejection");
    ASSERT_TRUE(cookCase);
    Path& root = cookCase->root;
    Path& outputDirectory = cookCase->outputDirectory;

    const Path assetRoot = root / s_ASSETS_DIR;
    EXPECT_TRUE(AssetsGraphicsFixture::WriteMaterialBindMaterialIntegrationAssetsWithPixelSource(
        testArena,
        assetRoot,
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        AssetsGraphicsFixture::s_MissingInterfaceMaterialMeta,
        AssetsGraphicsFixture::s_UnboundMaterialShaderProbeSource
    ));
    EXPECT_FALSE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(
        testArena,
        root,
        outputDirectory,
        { assetRoot }
    ));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("interface is required")));

    EXPECT_TRUE(RemoveAllIfExists(root));
#endif
}

TEST(AssetsGraphics, MaterialBindDependencyInvalidation){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    auto cookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, "material_bind_dependency_invalidation");
    ASSERT_TRUE(cookCase);
    Path& root = cookCase->root;
    Path& outputDirectory = cookCase->outputDirectory;

    const Path assetRoot = root / s_ASSETS_DIR;
    if(!AssetsGraphicsFixture::WriteMaterialBindMaterialIntegrationAssets(
        testArena,
        assetRoot,
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        AssetsGraphicsFixture::s_BlockScopedMaterialMeta
    ))
        return;

    EXPECT_TRUE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }));


    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    auto loadedAssetLoadResult = AssetsGraphicsFixture::LoadCookedMaterial(testArena, outputDirectory, Name(s_PROJECT_MATERIALS_TEST_MATERIAL));
    ASSERT_TRUE(loadedAssetLoadResult);
    loadedAsset = Move(*loadedAssetLoadResult);
    if(!loadedAsset)
        return;

    const NWB::Impl::Material& material = static_cast<const NWB::Impl::Material&>(*loadedAsset);
    const u64 initialLayoutHash = material.typedLayoutHash();

    NWB::Core::GraphicsVector<NWB::Core::ShaderArchive::Record> records(testArena.arena);
    auto recordsLoadResult = AssetsGraphicsFixture::LoadCookedShaderArchiveRecords(testArena, outputDirectory);
    ASSERT_TRUE(recordsLoadResult);
    records = Move(*recordsLoadResult);
    u64 initialPixelSourceChecksum = 0u;
    auto initialPixelSourceChecksumLoadResult = AssetsGraphicsFixture::FindShaderArchiveSourceChecksum(records, Name("project/shaders/material_ps"), Name(s_PS));
    ASSERT_TRUE(initialPixelSourceChecksumLoadResult);
    initialPixelSourceChecksum = *initialPixelSourceChecksumLoadResult;

    EXPECT_TRUE(AssetsGraphicsFixture::WriteTextFile(
        assetRoot / s_MATERIAL_INTERFACES / s_TEST_SURFACE_BIND,
        AssetsGraphicsFixture::s_UpdatedDefaultMaterialBindSource
    ));
    EXPECT_TRUE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }));

    loadedAsset.reset();
    auto loadedAssetLoadResult2 = AssetsGraphicsFixture::LoadCookedMaterial(testArena, outputDirectory, Name(s_PROJECT_MATERIALS_TEST_MATERIAL));
    ASSERT_TRUE(loadedAssetLoadResult2);
    loadedAsset = Move(*loadedAssetLoadResult2);
    if(loadedAsset){
        const NWB::Impl::Material& updatedMaterial = static_cast<const NWB::Impl::Material&>(*loadedAsset);
        EXPECT_NE(updatedMaterial.typedLayoutHash(), initialLayoutHash);
    }

    records.clear();
    auto recordsLoadResult2 = AssetsGraphicsFixture::LoadCookedShaderArchiveRecords(testArena, outputDirectory);
    ASSERT_TRUE(recordsLoadResult2);
    records = Move(*recordsLoadResult2);
    u64 updatedPixelSourceChecksum = 0u;
    auto updatedPixelSourceChecksumLoadResult = AssetsGraphicsFixture::FindShaderArchiveSourceChecksum(records, Name("project/shaders/material_ps"), Name(s_PS));
    ASSERT_TRUE(updatedPixelSourceChecksumLoadResult);
    updatedPixelSourceChecksum = *updatedPixelSourceChecksumLoadResult;
    EXPECT_NE(updatedPixelSourceChecksum, initialPixelSourceChecksum);
    EXPECT_EQ(logger.errorCount(), 0u);

    EXPECT_TRUE(RemoveAllIfExists(root));
}

TEST(AssetsGraphics, MaterialBindDiscoveryValidation){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    auto cookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, "material_bind_valid");
    ASSERT_TRUE(cookCase);
    Path& root = cookCase->root;
    Path& outputDirectory = cookCase->outputDirectory;
    EXPECT_TRUE(AssetsGraphicsFixture::CookMinimalMeshWithMaterialBind(
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        testArena,
        *cookCase
    ));

    const Path generatedIncludePath = root / s_CACHE / s_TESTS / s_MATERIAL_BIND_INCLUDES / s_PROJECT / s_MATERIAL_INTERFACES / s_TEST_SURFACE_BIND;
    // Removing the last bind source must clear its generated include on a recook with the same cache.
    const Path assetRoot = root / "assets";
    const auto removedBind = RemoveFile(assetRoot / s_MATERIAL_INTERFACES / s_TEST_SURFACE_BIND);
    ASSERT_TRUE(removedBind);
    ASSERT_TRUE(*removedBind);
    ASSERT_TRUE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }));
    const auto generatedIncludeExists = FileExists(generatedIncludePath);
    ASSERT_TRUE(generatedIncludeExists);
    EXPECT_FALSE(*generatedIncludeExists);
    EXPECT_EQ(logger.errorCount(), 0u);
    EXPECT_TRUE(RemoveAllIfExists(root));
#if defined(NWB_FINAL)
    auto duplicateIncludeCookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, "material_bind_duplicate_include_path");
    ASSERT_TRUE(duplicateIncludeCookCase);
    Path& duplicateIncludeRoot = duplicateIncludeCookCase->root;
    EXPECT_FALSE(AssetsGraphicsFixture::CookDuplicateGeneratedMaterialBindIncludePath(
        testArena,
        *duplicateIncludeCookCase
    ));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT(
        "duplicate material bind include path 'project/material_interfaces/test_surface.bind'"
    )));

    EXPECT_TRUE(RemoveAllIfExists(duplicateIncludeRoot));

    auto invalidCookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, "material_bind_duplicate_field");
    ASSERT_TRUE(invalidCookCase);
    Path& invalidRoot = invalidCookCase->root;
    EXPECT_FALSE(AssetsGraphicsFixture::CookMinimalMeshWithMaterialBind(
        AssetsGraphicsFixture::s_DuplicateFieldMaterialBindSource,
        testArena,
        *invalidCookCase
    ));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("duplicate struct field declaration")));

    EXPECT_TRUE(RemoveAllIfExists(invalidRoot));
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

