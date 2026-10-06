// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(GLB_FINAL)
constexpr u32 s_ExpectedDualCount = 2u;
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_material{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_PROJECT_MATERIAL_INTERFACES_TEST_SURFACE = "project/material_interfaces/test_surface";
static constexpr AStringView s_PROJECT_MATERIALS_TEST_MATERIAL = "project/materials/test_material";
#if defined(GLB_FINAL)
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


#if defined(GLB_FINAL)
static bool ParseMaterialEntryFromMetaText(
    const AStringView metaText,
    TestArena& testArena,
    NWB::Impl::MaterialCookEntry& outEntry,
    NWB::Core::Alloc::ScratchArena& scratchArena
){
    NWB::Core::Metascript::Document doc(testArena.arena);
    if(!doc.parse(metaText))
        return false;

    const Path assetRoot = AssetsGraphicsFixture::AssetsGraphicsTestCaseRoot(testArena, "material_meta") / s_ASSETS_DIR;
    const Path nwbFilePath = assetRoot / s_MATERIALS / "test_material.nwb";
    return NWB::Impl::ParseMaterialCookMetadata(assetRoot, s_PROJECT, nwbFilePath, doc, outEntry, scratchArena);
}

static bool BuildMaterialFromBindAndMeta(
    const AStringView bindText,
    const AStringView materialText,
    const AStringView caseName,
    TestArena& testArena,
    NWB::Impl::Material& outMaterial,
    NWB::Core::Alloc::ScratchArena& scratchArena
){
    NWB::Impl::MaterialCookEntry materialEntry(testArena.arena);
    if(!ParseMaterialEntryFromMetaText(materialText, testArena, materialEntry, scratchArena))
        return false;

    NWB::Impl::MaterialBindEntry bindEntry(testArena.arena);
    Path bindRoot(testArena.arena);
    bool built = false;
    if(AssetsGraphicsFixture::ParseMaterialBindFromText(testArena, bindText, caseName, bindEntry, bindRoot, scratchArena)){
        bindEntry.virtualPath = s_PROJECT_MATERIAL_INTERFACES_TEST_SURFACE;

        NWB::Impl::ShaderCook::CookVector<NWB::Impl::MaterialBindEntry> bindEntries(testArena.arena);
        bindEntries.push_back(Move(bindEntry));
        NWB::Impl::ShaderCook::CookVector<NWB::Impl::MaterialCookEntry> materialEntries(testArena.arena);
        materialEntries.push_back(Move(materialEntry));
        built =
            NWB::Impl::ValidateMaterialCookInterfaces(bindEntries, materialEntries, scratchArena)
            && NWB::Impl::BuildMaterialAsset(materialEntries[0u], outMaterial)
        ;
    }

    if(!bindRoot.empty()){
        ErrorCode errorCode;
        built = RemoveAllIfExists(bindRoot, errorCode) && built;
    }
    return built;
}

static bool RoundTripMaterialAssetCodec(
    TestArena& testArena,
    NWB::Impl::MaterialAssetCodec& codec,
    const NWB::Impl::Material& material,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset
){
    NWB::Core::Assets::AssetBytes binary = AssetsGraphicsFixture::MakeAssetBytes(testArena);
    const bool serialized = codec.serialize(material, binary);
    EXPECT_TRUE(serialized);
    EXPECT_FALSE(binary.empty());
    if(!serialized || binary.empty())
        return false;

    const bool deserialized = codec.deserialize(
        testArena.arena,
        material.virtualPath(),
        binary,
        outLoadedAsset
    );
    EXPECT_TRUE(deserialized);
    EXPECT_NE(outLoadedAsset.get(), nullptr);
    return deserialized && static_cast<bool>(outLoadedAsset);
}

static void SetGeneratedMaterialAvboitPixelShaders(NWB::Impl::Material& material){
    NWB::Core::Assets::AssetRef<NWB::Impl::Shader> accumulatePixelShader;
    accumulatePixelShader.virtualPath = Name(s_GENERATED_AVBOIT_ACCUMULATE_PS_PROJECT_M);
    material.setAvboitAccumulatePixelShader(accumulatePixelShader);

    NWB::Core::Assets::AssetRef<NWB::Impl::Shader> occupancyPixelShader;
    occupancyPixelShader.virtualPath = Name(s_GENERATED_AVBOIT_OCCUPANCY_PS_PROJECT_MA);
    material.setAvboitOccupancyPixelShader(occupancyPixelShader);

    NWB::Core::Assets::AssetRef<NWB::Impl::Shader> extinctionPixelShader;
    extinctionPixelShader.virtualPath = Name(s_GENERATED_AVBOIT_EXTINCTION_PS_PROJECT_M);
    material.setAvboitExtinctionPixelShader(extinctionPixelShader);
}
#endif


TEST(AssetsGraphics, MaterialCookRejectsMissingAvboitPixelShaders){
#if defined(GLB_FINAL)
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
        transparentMaterial,
        scratchArena
    ));

    NWB::Impl::Material refractiveMaterial(testArena.arena);
    EXPECT_FALSE(BuildMaterialFromBindAndMeta(
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        AssetsGraphicsFixture::s_RefractiveMaterialMeta,
        "material_cook_missing_avboit_refractive",
        testArena,
        refractiveMaterial,
        scratchArena
    ));

    EXPECT_EQ(logger.errorCount(), s_ExpectedDualCount);
    EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT(
        "AVBOIT pixel shaders must be present if and only if it is transparent"
    )));
#endif
}

TEST(AssetsGraphics, MaterialMetadataRejectsMissingShaderVariant){
#if defined(GLB_FINAL)
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
    EXPECT_FALSE(ParseMaterialEntryFromMetaText(s_MissingShaderVariantMaterialMeta, testArena, materialEntry, scratchArena));
    EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT("field 'shader_variant' is required")));
#endif
}

TEST(AssetsGraphics, MaterialMetadataRejectsMissingRenderProperties){
#if defined(GLB_FINAL)
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
    EXPECT_FALSE(ParseMaterialEntryFromMetaText(s_MissingRefractiveMaterialMeta, testArena, materialEntry, scratchArena));
    EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT("'refractive' is required and must be 0 or 1")));
#endif
}

TEST(AssetsGraphics, MaterialMetadataRejectsExplicitOpticalStages){
#if defined(GLB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_MaterialScratchArena);
    const auto expectRejected = [&](const AStringView metaText){
        NWB::Impl::MaterialCookEntry materialEntry(testArena.arena);
        EXPECT_FALSE(ParseMaterialEntryFromMetaText(metaText, testArena, materialEntry, scratchArena));
        EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT(
            "explicit 'shaders' cannot be used with transparent/refractive materials"
        )));
    };

    expectRejected(AssetsGraphicsFixture::s_ExplicitTransparentMaterialMeta);
    expectRejected(AssetsGraphicsFixture::s_ExplicitRefractiveMaterialMeta);
#endif
}

TEST(AssetsGraphics, MaterialMetadataRejectsEngineRootedPolicySelectors){
#if defined(GLB_FINAL)
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
        EXPECT_FALSE(ParseMaterialEntryFromMetaText(metaText, testArena, materialEntry, scratchArena));
        EXPECT_TRUE(logger.sawErrorContaining(expectedError));
    };

    expectRejected(s_EngineRootedInterfaceMeta, GLB_TEXT("interface must use the project/ virtual root"));
    expectRejected(s_EngineRootedSurfaceMeta, GLB_TEXT("field 'surface' must use the project/ virtual root"));
    expectRejected(s_EngineRootedBxdfMeta, GLB_TEXT("field 'bxdf' must use the project/ virtual root"));
    expectRejected(s_EngineRootedStageShaderMeta, GLB_TEXT("shader stage 'mesh' must use the project/ virtual root"));
    EXPECT_EQ(logger.errorCount(), 4u);
#endif
}

TEST(AssetsGraphics, MaterialCodecTypedLayoutBoundary){
#if defined(GLB_FINAL)
    TestArena testArena;
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_MaterialScratchArena);
    NWB::Impl::Material material(testArena.arena);

    {
        CapturingLogger logger;
        NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

        const bool built = BuildMaterialFromBindAndMeta(
            AssetsGraphicsFixture::s_MinimalMaterialBindSource,
            AssetsGraphicsFixture::s_BlockScopedMaterialMeta,
            "material_codec_typed_layout_boundary",
            testArena,
            material,
            scratchArena
        );
        EXPECT_TRUE(built);
        if(!built)
            return;

        NWB::Impl::MaterialAssetCodec codec;
        material.setTransparent(true);

#if defined(GLB_FINAL)
        NWB::Core::Assets::AssetBytes invalidBinary = AssetsGraphicsFixture::MakeAssetBytes(testArena);
        EXPECT_FALSE(codec.serialize(material, invalidBinary));
        EXPECT_TRUE(invalidBinary.empty());
        EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT(
            "AVBOIT pixel shaders must be present if and only if the material is transparent"
        )));
#endif

        SetGeneratedMaterialAvboitPixelShaders(material);
        UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
        ASSERT_TRUE(RoundTripMaterialAssetCodec(testArena, codec, material, loadedAsset));
#if defined(GLB_FINAL)
        EXPECT_EQ(logger.errorCount(), 1u);
#else
        EXPECT_EQ(logger.errorCount(), 0u);
#endif
    }

#if defined(GLB_FINAL)
    {
        CapturingLogger logger;
        NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

        NWB::Impl::MaterialAssetCodec codec;
        NWB::Core::Assets::AssetBytes binary = AssetsGraphicsFixture::MakeAssetBytes(testArena);
        EXPECT_TRUE(codec.serialize(material, binary));

        usize layoutHashOffset = 0u;
        usize blockByteCountOffset = 0u;
        EXPECT_TRUE(AssetsGraphicsFixture::FindMaterialBinaryTypedLayoutOffsets(
            binary,
            layoutHashOffset,
            blockByteCountOffset
        ));

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

        EXPECT_EQ(logger.errorCount(), 3u);
        EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT("typed layout hash mismatch")));
        EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT(
            "typed block byte count does not match typed layout"
        )));
        EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT(
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

        Path invalidRoot(testArena.arena);
        NWB::Impl::MaterialBindEntry invalidEntry(testArena.arena);
        {
            CapturingLogger failureLogger;
            NWB::Core::Common::LoggerRegistrationGuard failureLoggerRegistrationGuard(
                failureLogger,
                NWB::Core::Common::LoggerBreakPolicy::ReportOnly
            );

            EXPECT_FALSE(AssetsGraphicsFixture::ParseMaterialBindFromText(
                testArena,
                bindText,
                caseName,
                invalidEntry,
                invalidRoot,
                scratchArena
            ));
            EXPECT_TRUE(failureLogger.sawErrorContaining(expectedError));
        }

        ErrorCode removeErrorCode;
        EXPECT_TRUE(RemoveAllIfExists(invalidRoot, removeErrorCode));
    };

    expectParseFailure(
        AssetsGraphicsFixture::s_UnknownBlockClassMaterialBindSource,
        "material_bind_unknown_block_class",
        GLB_TEXT("unsupported attribute 'material_project'")
    );
    expectParseFailure(
        AssetsGraphicsFixture::s_UnsupportedFieldTypeMaterialBindSource,
        "material_bind_unsupported_field_type",
        GLB_TEXT("unsupported type 'double'")
    );
    expectParseFailure(
        AssetsGraphicsFixture::s_InvalidDefaultMaterialBindSource,
        "material_bind_invalid_default",
        GLB_TEXT("attribute 'default' requires one non-empty string argument")
    );
    expectParseFailure(
        AssetsGraphicsFixture::s_MissingDefaultMaterialBindSource,
        "material_bind_missing_default",
        GLB_TEXT("must declare a default attribute")
    );
    expectParseFailure(
        AssetsGraphicsFixture::s_ResourceAttributeMaterialBindSource,
        "material_bind_resource_attribute",
        GLB_TEXT("has unsupported attribute 'texture_asset'")
    );
    expectParseFailure(
        AssetsGraphicsFixture::s_DuplicateInstanceMaterialBindSource,
        "material_bind_duplicate_instance",
        GLB_TEXT("duplicate struct instance declaration")
    );
    expectParseFailure(
        AssetsGraphicsFixture::s_InstanceOverrideMaterialBindSource,
        "material_bind_instance_override",
        GLB_TEXT("unsupported asset field 'instance_override'")
    );

    Path float1DefaultRoot(testArena.arena);
    NWB::Impl::MaterialBindEntry float1DefaultEntry(testArena.arena);
    EXPECT_TRUE(AssetsGraphicsFixture::ParseMaterialBindFromText(
        testArena,
        AssetsGraphicsFixture::s_Float1DefaultMaterialBindSource,
        "material_bind_float1_default",
        float1DefaultEntry,
        float1DefaultRoot,
        scratchArena
    ));
    float1DefaultEntry.virtualPath = s_PROJECT_MATERIAL_INTERFACES_TEST_SURFACE;
    {
        SCOPED_TRACE("material_bind_float1_default_include");

        CapturingLogger failureLogger;
        NWB::Core::Common::LoggerRegistrationGuard failureLoggerRegistrationGuard(
            failureLogger,
            NWB::Core::Common::LoggerBreakPolicy::ReportOnly
        );

        NWB::Impl::ShaderCook::CookString generatedSource(testArena.arena);
        EXPECT_FALSE(NWB::Impl::BuildMaterialBindIncludeSource(
            testArena.arena,
            float1DefaultEntry,
            generatedSource,
            scratchArena
        ));
        EXPECT_TRUE(failureLogger.sawErrorContaining(GLB_TEXT("default 'float1(1.0)'")));
    }

    const Name cacheInterface("project/material_interfaces/test_surface");
    NWB::Impl::MaterialBindTypedLayoutCache layoutCache(testArena.arena);
    const NWB::Impl::MaterialBindTypedLayout* cachedLayout = nullptr;
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
            cachedLayout,
            scratchArena
        ));
        EXPECT_TRUE(failureLogger.sawErrorContaining(GLB_TEXT("default 'float1(1.0)'")));
    }
    EXPECT_EQ(cachedLayout, nullptr);
    EXPECT_TRUE(layoutCache.entries.empty());
    EXPECT_TRUE(layoutCache.lookup.empty());

    Path validCacheRoot(testArena.arena);
    NWB::Impl::MaterialBindEntry validCacheEntry(testArena.arena);
    EXPECT_TRUE(AssetsGraphicsFixture::ParseMaterialBindFromText(
        testArena,
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        "material_bind_cache_valid_after_failed_layout",
        validCacheEntry,
        validCacheRoot,
        scratchArena
    ));
    validCacheEntry.virtualPath = s_PROJECT_MATERIAL_INTERFACES_TEST_SURFACE;
    EXPECT_TRUE(NWB::Impl::FindOrBuildMaterialBindTypedLayout(
        cacheInterface,
        validCacheEntry,
        layoutCache,
        cachedLayout,
        scratchArena
    ));
    EXPECT_NE(cachedLayout, nullptr);
    EXPECT_EQ(layoutCache.entries.size(), 1u);
    EXPECT_EQ(layoutCache.lookup.size(), 1u);
    EXPECT_EQ(logger.errorCount(), 0u);

    ErrorCode removeErrorCode;
    EXPECT_TRUE(RemoveAllIfExists(float1DefaultRoot, removeErrorCode));
    EXPECT_TRUE(RemoveAllIfExists(validCacheRoot, removeErrorCode));
}


TEST(AssetsGraphics, MaterialBindEngineAndProjectResourceValidation){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    EXPECT_FALSE(NWB::Impl::IsSupportedMaterialResourceReference(
        NWB::Impl::MaterialResourceKind::SampledImage2D,
        NWB::Impl::MaterialResourceSource::Asset,
        "builtin/material_fixture/checker_rgba8"
    ));
    EXPECT_FALSE(NWB::Impl::IsSupportedMaterialResourceReference(
        NWB::Impl::MaterialResourceKind::Sampler,
        NWB::Impl::MaterialResourceSource::Asset,
        "project/samplers/../linear_clamp"
    ));
    EXPECT_FALSE(NWB::Impl::IsSupportedMaterialResourceReference(
        NWB::Impl::MaterialResourceKind::Sampler,
        NWB::Impl::MaterialResourceSource::Asset,
        "engine/samplers/../linear_clamp"
    ));
    EXPECT_TRUE(NWB::Impl::IsSupportedMaterialResourceReference(
        NWB::Impl::MaterialResourceKind::Sampler,
        NWB::Impl::MaterialResourceSource::Asset,
        "project/samplers/linear_clamp"
    ));
    EXPECT_TRUE(NWB::Impl::IsSupportedMaterialResourceReference(
        NWB::Impl::MaterialResourceKind::Sampler,
        NWB::Impl::MaterialResourceSource::Asset,
        s_ENGINE_SAMPLERS_LINEAR_CLAMP
    ));

    EXPECT_EQ(logger.errorCount(), 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AssetsGraphics, MaterialBindCookRejectsMalformedParametersAndInterfaceMismatch){
#if defined(GLB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    ErrorCode errorCode;
#if defined(GLB_FINAL)
    Path invalidRoot(testArena.arena);
    Path invalidOutputDirectory(testArena.arena);
    EXPECT_FALSE(AssetsGraphicsFixture::CookMaterialBindMaterialIntegration(
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        AssetsGraphicsFixture::s_UnknownInterfaceParameterMaterialMeta,
        "material_bind_unknown_interface_parameter",
        testArena,
        invalidRoot,
        invalidOutputDirectory
    ));
    EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT(
        "parameter 'surface.missing' is not declared by interface"
    )));

    errorCode.clear();
    EXPECT_TRUE(RemoveAllIfExists(invalidRoot, errorCode));

    Path flatRoot(testArena.arena);
    Path flatOutputDirectory(testArena.arena);
    EXPECT_FALSE(AssetsGraphicsFixture::CookMaterialBindMaterialIntegration(
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        AssetsGraphicsFixture::s_FlatInterfaceParameterMaterialMeta,
        "material_bind_flat_interface_parameter",
        testArena,
        flatRoot,
        flatOutputDirectory
    ));
    EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT(
        "interface parameter 'runtime.fade_alpha' must be declared inside a block map"
    )));

    errorCode.clear();
    EXPECT_TRUE(RemoveAllIfExists(flatRoot, errorCode));

    Path untypedParameterRoot(testArena.arena);
    Path untypedParameterOutputDirectory(testArena.arena);
    EXPECT_FALSE(AssetsGraphicsFixture::CookMaterialBindMaterialIntegration(
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        AssetsGraphicsFixture::s_UntypedMaterialParameterMeta,
        "material_bind_untyped_material_parameter",
        testArena,
        untypedParameterRoot,
        untypedParameterOutputDirectory
    ));
    EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT(
        "has invalid value '0.25, 0.5, 0.75, 1.0'"
    )));

    errorCode.clear();
    EXPECT_TRUE(RemoveAllIfExists(untypedParameterRoot, errorCode));

    Path vectorAliasParameterRoot(testArena.arena);
    Path vectorAliasParameterOutputDirectory(testArena.arena);
    EXPECT_FALSE(AssetsGraphicsFixture::CookMaterialBindMaterialIntegration(
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        AssetsGraphicsFixture::s_VectorAliasMaterialParameterMeta,
        "material_bind_vector_alias_material_parameter",
        testArena,
        vectorAliasParameterRoot,
        vectorAliasParameterOutputDirectory
    ));
    EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT(
        "has invalid value 'vec4(0.25, 0.5, 0.75, 1.0)'"
    )));

    errorCode.clear();
    EXPECT_TRUE(RemoveAllIfExists(vectorAliasParameterRoot, errorCode));

    Path unsupportedFieldRoot(testArena.arena);
    Path unsupportedFieldOutputDirectory(testArena.arena);
    EXPECT_FALSE(AssetsGraphicsFixture::CookMaterialBindMaterialIntegration(
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        AssetsGraphicsFixture::s_UnsupportedMaterialFieldMeta,
        "material_bind_unsupported_material_field",
        testArena,
        unsupportedFieldRoot,
        unsupportedFieldOutputDirectory
    ));
    EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT("unsupported asset field 'compiler'")));

    errorCode.clear();
    EXPECT_TRUE(RemoveAllIfExists(unsupportedFieldRoot, errorCode));

    Path incompleteBindRoot(testArena.arena);
    Path incompleteBindOutputDirectory(testArena.arena);
    EXPECT_FALSE(AssetsGraphicsFixture::CookMaterialBindMaterialIntegration(
        AssetsGraphicsFixture::s_SurfaceOnlyMaterialBindSource,
        AssetsGraphicsFixture::s_BlockScopedMaterialMeta,
        "material_bind_incomplete_block_scoped",
        testArena,
        incompleteBindRoot,
        incompleteBindOutputDirectory
    ));
    EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT(
        "typed parameter 'runtime.fade_alpha' is not declared by interface"
    )));

    errorCode.clear();
    EXPECT_TRUE(RemoveAllIfExists(incompleteBindRoot, errorCode));

    Path interfaceShaderMismatchRoot(testArena.arena);
    Path interfaceShaderMismatchOutputDirectory(testArena.arena);
    EXPECT_TRUE(AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(
        testArena,
        "material_bind_interface_without_bind_shader",
        interfaceShaderMismatchRoot,
        interfaceShaderMismatchOutputDirectory
    ));
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
    EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT("does not include a generated material bind")));

    errorCode.clear();
    EXPECT_TRUE(RemoveAllIfExists(interfaceShaderMismatchRoot, errorCode));

    Path interfaceIdentityMismatchRoot(testArena.arena);
    Path interfaceIdentityMismatchOutputDirectory(testArena.arena);
    EXPECT_TRUE(AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(
        testArena,
        "material_bind_interface_identity_mismatch",
        interfaceIdentityMismatchRoot,
        interfaceIdentityMismatchOutputDirectory
    ));
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
    EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT(
        "includes generated material bind interface 'project/material_interfaces/other_surface'"
    )));

    errorCode.clear();
    EXPECT_TRUE(RemoveAllIfExists(interfaceIdentityMismatchRoot, errorCode));
#endif
#endif
}


TEST(AssetsGraphics, ShadowSurfaceDispatchIsolatesOverlappingBindApis){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
    ASSERT_TRUE(AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(
        testArena,
        "shadow_transmittance_dispatch_overlapping_bind_apis",
        root,
        outputDirectory
    ));

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

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
}

TEST(AssetsGraphics, MaterialRejectsMissingInterfaceCookIntegration){
#if defined(GLB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
    EXPECT_TRUE(AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(
        testArena,
        "material_missing_interface_rejection",
        root,
        outputDirectory
    ));
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
    EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT("interface is required")));

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
#endif
}

TEST(AssetsGraphics, MaterialBindDependencyInvalidation){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
    EXPECT_TRUE(AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(
        testArena,
        "material_bind_dependency_invalidation",
        root,
        outputDirectory
    ));
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
    EXPECT_TRUE(AssetsGraphicsFixture::LoadCookedMaterial(
        testArena,
        outputDirectory,
        Name(s_PROJECT_MATERIALS_TEST_MATERIAL),
        loadedAsset
    ));
    if(!loadedAsset)
        return;

    const NWB::Impl::Material& material = static_cast<const NWB::Impl::Material&>(*loadedAsset);
    const u64 initialLayoutHash = material.typedLayoutHash();

    NWB::Core::GraphicsVector<NWB::Core::ShaderArchive::Record> records(testArena.arena);
    EXPECT_TRUE(AssetsGraphicsFixture::LoadCookedShaderArchiveRecords(testArena, outputDirectory, records));
    u64 initialPixelSourceChecksum = 0u;
    EXPECT_TRUE(AssetsGraphicsFixture::FindShaderArchiveSourceChecksum(
        records,
        Name("project/shaders/material_ps"),
        Name(s_PS),
        initialPixelSourceChecksum
    ));

    EXPECT_TRUE(AssetsGraphicsFixture::WriteTextFile(
        assetRoot / s_MATERIAL_INTERFACES / s_TEST_SURFACE_BIND,
        AssetsGraphicsFixture::s_UpdatedDefaultMaterialBindSource
    ));
    EXPECT_TRUE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }));

    loadedAsset.reset();
    EXPECT_TRUE(AssetsGraphicsFixture::LoadCookedMaterial(
        testArena,
        outputDirectory,
        Name(s_PROJECT_MATERIALS_TEST_MATERIAL),
        loadedAsset
    ));
    if(loadedAsset){
        const NWB::Impl::Material& updatedMaterial = static_cast<const NWB::Impl::Material&>(*loadedAsset);
        EXPECT_NE(updatedMaterial.typedLayoutHash(), initialLayoutHash);
    }

    records.clear();
    EXPECT_TRUE(AssetsGraphicsFixture::LoadCookedShaderArchiveRecords(testArena, outputDirectory, records));
    u64 updatedPixelSourceChecksum = 0u;
    EXPECT_TRUE(AssetsGraphicsFixture::FindShaderArchiveSourceChecksum(
        records,
        Name("project/shaders/material_ps"),
        Name(s_PS),
        updatedPixelSourceChecksum
    ));
    EXPECT_NE(updatedPixelSourceChecksum, initialPixelSourceChecksum);
    EXPECT_EQ(logger.errorCount(), 0u);

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
}

TEST(AssetsGraphics, MaterialBindDiscoveryValidation){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
    EXPECT_TRUE(AssetsGraphicsFixture::CookMinimalMeshWithMaterialBind(
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        "material_bind_valid",
        testArena,
        root,
        outputDirectory
    ));

    const Path generatedIncludePath = root / s_CACHE / s_TESTS / s_MATERIAL_BIND_INCLUDES / s_PROJECT / s_MATERIAL_INTERFACES / s_TEST_SURFACE_BIND;
    // Removing the last bind source must clear its generated include on a recook with the same cache.
    const Path assetRoot = root / "assets";
    ErrorCode errorCode;
    ASSERT_TRUE(RemoveFile(assetRoot / s_MATERIAL_INTERFACES / s_TEST_SURFACE_BIND, errorCode));
    ASSERT_FALSE(errorCode);
    ASSERT_TRUE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }));
    errorCode.clear();
    EXPECT_FALSE(FileExists(generatedIncludePath, errorCode));
    EXPECT_FALSE(errorCode);
    EXPECT_EQ(logger.errorCount(), 0u);
    errorCode.clear();
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
#if defined(GLB_FINAL)
    Path duplicateIncludeRoot(testArena.arena);
    Path duplicateIncludeOutputDirectory(testArena.arena);
    EXPECT_FALSE(AssetsGraphicsFixture::CookDuplicateGeneratedMaterialBindIncludePath(
        "material_bind_duplicate_include_path",
        testArena,
        duplicateIncludeRoot,
        duplicateIncludeOutputDirectory
    ));
    EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT(
        "duplicate material bind include path 'project/material_interfaces/test_surface.bind'"
    )));

    errorCode.clear();
    EXPECT_TRUE(RemoveAllIfExists(duplicateIncludeRoot, errorCode));

    Path invalidRoot(testArena.arena);
    Path invalidOutputDirectory(testArena.arena);
    EXPECT_FALSE(AssetsGraphicsFixture::CookMinimalMeshWithMaterialBind(
        AssetsGraphicsFixture::s_DuplicateFieldMaterialBindSource,
        "material_bind_duplicate_field",
        testArena,
        invalidRoot,
        invalidOutputDirectory
    ));
    EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT("duplicate struct field declaration")));

    errorCode.clear();
    EXPECT_TRUE(RemoveAllIfExists(invalidRoot, errorCode));
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

