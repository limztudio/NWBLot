// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_material_bind_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_MaterialPixelShaderPath = "project/shaders/material_ps";

static constexpr AStringView s_ProjectMaterialInterfacesTestSurface = "project/material_interfaces/test_surface";
static constexpr AStringView s_ProjectMaterialsTestMaterial = "project/materials/test_material";
static constexpr AStringView s_Ps = "ps";
static constexpr AStringView s_Project = "project";
static constexpr AStringView s_Tests = "tests";
static constexpr AStringView s_MaterialBindIncludes = "material_bind_includes";
static constexpr AStringView s_MaterialInterfaces = "material_interfaces";
static constexpr AStringView s_TestSurfaceBind = "test_surface.bind";
static constexpr AStringView s_Shaders = "shaders";
static constexpr AStringView s_EngineSamplersLinearClamp = "engine/samplers/linear_clamp";
static constexpr AStringView s_Cache = "cache";
static constexpr AStringView s_AssetsDir = "assets";
static constexpr AStringView s_Materials = "materials";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CapturingLogger = AssetsGraphicsFixture::CapturingLogger;
using Path = AssetsGraphicsFixture::Path;
using TestArena = AssetsGraphicsFixture::TestArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AssetsGraphics, DuplicateMaterialBindIdentityIsRejectedBeforeMaterialMutation){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerGuard(logger, NWB::Core::Common::LoggerBreakPolicy::ReportOnly);
    TestArena testArena;
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_MaterialScratchArena);
    NWB::Impl::ShaderCook::CookVector<NWB::Impl::MaterialBindEntry> bindEntries(testArena.arena);
    for(usize index = 0u; index < 2u; ++index){
        auto& bind = bindEntries.emplace_back(testArena.arena);
        bind.virtualPath = s_ProjectMaterialInterfacesTestSurface;
    }
    NWB::Impl::ShaderCook::CookVector<NWB::Impl::MaterialCookEntry> materialEntries(testArena.arena);
    auto& material = materialEntries.emplace_back(testArena.arena);
    material.materialInterface = s_ProjectMaterialInterfacesTestSurface;
    material.typedLayoutHash = 0xF091u;
    material.typedBlockBytes.push_back(0x2Au);

    EXPECT_FALSE(NWB::Impl::ValidateMaterialCookInterfaces(bindEntries, scratchArena, materialEntries));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("Material bind interface 'project/material_interfaces/test_surface' is duplicated")));
    EXPECT_EQ(material.typedLayoutHash, 0xF091u);
    ASSERT_EQ(material.typedBlockBytes.size(), 1u);
    EXPECT_EQ(material.typedBlockBytes[0u], 0x2Au);
}


TEST(AssetsGraphics, MaterialBindSchemaValidation){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_MaterialScratchArena);
    auto expectParseFailure = [&](const AStringView bindText, const AStringView caseName, const TStringView expectedError){
        SCOPED_TRACE(caseName.data());

        auto invalidRootResult = AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, caseName);
        ASSERT_TRUE(invalidRootResult);
        Path invalidRoot = Move(*invalidRootResult);
        {
            CapturingLogger failureLogger;
            NWB::Core::Common::LoggerRegistrationGuard failureLoggerRegistrationGuard(failureLogger, NWB::Core::Common::LoggerBreakPolicy::ReportOnly);

            EXPECT_FALSE(AssetsGraphicsFixture::ParseMaterialBindFromText(testArena, bindText, invalidRoot, scratchArena));
            EXPECT_TRUE(failureLogger.sawErrorContaining(expectedError));
        }

        EXPECT_TRUE(RemoveAllIfExists(invalidRoot));
    };

    expectParseFailure(AssetsGraphicsFixture::s_UnknownBlockClassMaterialBindSource, "material_bind_unknown_block_class", NWB_TEXT("unsupported attribute 'material_project'"));
    expectParseFailure(AssetsGraphicsFixture::s_UnsupportedFieldTypeMaterialBindSource, "material_bind_unsupported_field_type", NWB_TEXT("unsupported type 'double'"));
    expectParseFailure(AssetsGraphicsFixture::s_InvalidDefaultMaterialBindSource, "material_bind_invalid_default", NWB_TEXT("attribute 'default' requires one non-empty string argument"));
    expectParseFailure(AssetsGraphicsFixture::s_MissingDefaultMaterialBindSource, "material_bind_missing_default", NWB_TEXT("must declare a default attribute"));
    expectParseFailure(AssetsGraphicsFixture::s_ResourceAttributeMaterialBindSource, "material_bind_resource_attribute", NWB_TEXT("has unsupported attribute 'texture_asset'"));
    for(const AStringView resourceType : { AStringView("texture2d"), AStringView("sampler") }){
        const auto retiredFixtureBind = StringFormat(testArena.arena, "[material_constant] struct NwbResourceMaterial{{ " "[fixture(\"builtin/material_fixture/checker_rgba8\")] {} resource; }}; NwbResourceMaterial surface;", resourceType);
        expectParseFailure(retiredFixtureBind, resourceType, NWB_TEXT("has unsupported attribute 'fixture'"));
    }
    expectParseFailure(AssetsGraphicsFixture::s_DuplicateInstanceMaterialBindSource, "material_bind_duplicate_instance", NWB_TEXT("duplicate variable declaration"));
    expectParseFailure(AssetsGraphicsFixture::s_InitializedInstanceMaterialBindSource, "material_bind_initialized_instance", NWB_TEXT("instance 'surface' must not have an initializer or assignments"));
    constexpr AStringView s_ValidStruct = "[material_constant] struct Surface { [default(\"float(0.5)\")] float roughness; };\n";
    expectParseFailure(s_ValidStruct, "material_bind_missing_instance", NWB_TEXT("instance declarations must not be empty"));
    const auto unknownInstance = StringFormat(testArena.arena, "{}Unknown surface;", s_ValidStruct);
    expectParseFailure(unknownInstance, "material_bind_unknown_instance_type", NWB_TEXT("instance 'surface' references unknown struct type 'Unknown'"));
    const auto assignedInstance = StringFormat(testArena.arena, "{}Surface surface; surface.roughness = 0.5;", s_ValidStruct);
    expectParseFailure(assignedInstance, "material_bind_assigned_instance", NWB_TEXT("instance 'surface' must not have an initializer or assignments"));

    auto float1DefaultRootResult = AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "material_bind_float1_default");
    ASSERT_TRUE(float1DefaultRootResult);
    Path float1DefaultRoot = Move(*float1DefaultRootResult);
    auto float1DefaultEntryResult = AssetsGraphicsFixture::ParseMaterialBindFromText(testArena, AssetsGraphicsFixture::s_Float1DefaultMaterialBindSource, float1DefaultRoot, scratchArena);
    ASSERT_TRUE(float1DefaultEntryResult);
    NWB::Impl::MaterialBindEntry& float1DefaultEntry = *float1DefaultEntryResult;
    float1DefaultEntry.virtualPath = s_ProjectMaterialInterfacesTestSurface;
    {
        SCOPED_TRACE("material_bind_float1_default_include");

        CapturingLogger failureLogger;
        NWB::Core::Common::LoggerRegistrationGuard failureLoggerRegistrationGuard(failureLogger, NWB::Core::Common::LoggerBreakPolicy::ReportOnly);

        EXPECT_FALSE(NWB::Impl::BuildMaterialBindIncludeSource(testArena.arena, float1DefaultEntry, scratchArena));
        EXPECT_TRUE(failureLogger.sawErrorContaining(NWB_TEXT("default 'float1(1.0)'")));
    }

    const Name cacheInterface("project/material_interfaces/test_surface");
    NWB::Impl::MaterialBindTypedLayoutCache layoutCache(testArena.arena);
    {
        SCOPED_TRACE("material_bind_float1_default_cache");

        CapturingLogger failureLogger;
        NWB::Core::Common::LoggerRegistrationGuard failureLoggerRegistrationGuard(failureLogger, NWB::Core::Common::LoggerBreakPolicy::ReportOnly);

        EXPECT_FALSE(NWB::Impl::FindOrBuildMaterialBindTypedLayout(cacheInterface, float1DefaultEntry, scratchArena, layoutCache));
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
    validCacheEntry.virtualPath = s_ProjectMaterialInterfacesTestSurface;
    const auto cachedLayout = NWB::Impl::FindOrBuildMaterialBindTypedLayout(cacheInterface, validCacheEntry, scratchArena, layoutCache);
    ASSERT_TRUE(cachedLayout);
    EXPECT_EQ(layoutCache.entries.size(), 1u);
    EXPECT_EQ(layoutCache.lookup.size(), 1u);
    EXPECT_EQ(logger.errorCount(), 0u);

    EXPECT_TRUE(RemoveAllIfExists(float1DefaultRoot));
    EXPECT_TRUE(RemoveAllIfExists(validCacheRoot));
}


TEST(AssetsGraphics, MaterialBindEngineAndProjectResourceValidation){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    EXPECT_FALSE(NWB::Impl::IsSupportedMaterialResourceReference(NWB::Impl::MaterialResourceKind::SampledImage2D, "builtin/textures/checker"));
    EXPECT_FALSE(NWB::Impl::IsSupportedMaterialResourceReference(NWB::Impl::MaterialResourceKind::Sampler, "project/samplers/../linear_clamp"));
    EXPECT_FALSE(NWB::Impl::IsSupportedMaterialResourceReference(NWB::Impl::MaterialResourceKind::Sampler, "engine/samplers/../linear_clamp"));
    EXPECT_TRUE(NWB::Impl::IsSupportedMaterialResourceReference(NWB::Impl::MaterialResourceKind::Sampler, "project/samplers/linear_clamp"));
    EXPECT_TRUE(NWB::Impl::IsSupportedMaterialResourceReference(NWB::Impl::MaterialResourceKind::Sampler, s_EngineSamplersLinearClamp));

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
    EXPECT_FALSE(AssetsGraphicsFixture::CookMaterialBindMaterialIntegration(AssetsGraphicsFixture::s_MinimalMaterialBindSource, AssetsGraphicsFixture::s_UnknownInterfaceParameterMaterialMeta, testArena, *invalidCookCase));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("parameter 'surface.missing' is not declared by interface")));

    EXPECT_TRUE(RemoveAllIfExists(invalidRoot));

    auto flatCookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, "material_bind_flat_interface_parameter");
    ASSERT_TRUE(flatCookCase);
    Path& flatRoot = flatCookCase->root;
    EXPECT_FALSE(AssetsGraphicsFixture::CookMaterialBindMaterialIntegration(AssetsGraphicsFixture::s_MinimalMaterialBindSource, AssetsGraphicsFixture::s_FlatInterfaceParameterMaterialMeta, testArena, *flatCookCase));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("interface parameter 'runtime.fade_alpha' must be declared inside a block map")));

    EXPECT_TRUE(RemoveAllIfExists(flatRoot));

    auto untypedParameterCookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, "material_bind_untyped_material_parameter");
    ASSERT_TRUE(untypedParameterCookCase);
    Path& untypedParameterRoot = untypedParameterCookCase->root;
    EXPECT_FALSE(AssetsGraphicsFixture::CookMaterialBindMaterialIntegration(AssetsGraphicsFixture::s_MinimalMaterialBindSource, AssetsGraphicsFixture::s_UntypedMaterialParameterMeta, testArena, *untypedParameterCookCase));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("has invalid value '0.25, 0.5, 0.75, 1.0'")));

    EXPECT_TRUE(RemoveAllIfExists(untypedParameterRoot));

    auto vectorAliasParameterCookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, "material_bind_vector_alias_material_parameter");
    ASSERT_TRUE(vectorAliasParameterCookCase);
    Path& vectorAliasParameterRoot = vectorAliasParameterCookCase->root;
    EXPECT_FALSE(AssetsGraphicsFixture::CookMaterialBindMaterialIntegration(AssetsGraphicsFixture::s_MinimalMaterialBindSource, AssetsGraphicsFixture::s_VectorAliasMaterialParameterMeta, testArena, *vectorAliasParameterCookCase));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("has invalid value 'vec4(0.25, 0.5, 0.75, 1.0)'")));

    EXPECT_TRUE(RemoveAllIfExists(vectorAliasParameterRoot));

    auto unsupportedFieldCookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, "material_bind_unsupported_material_field");
    ASSERT_TRUE(unsupportedFieldCookCase);
    Path& unsupportedFieldRoot = unsupportedFieldCookCase->root;
    EXPECT_FALSE(AssetsGraphicsFixture::CookMaterialBindMaterialIntegration(AssetsGraphicsFixture::s_MinimalMaterialBindSource, AssetsGraphicsFixture::s_UnsupportedMaterialFieldMeta, testArena, *unsupportedFieldCookCase));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("unsupported asset field 'compiler'")));

    EXPECT_TRUE(RemoveAllIfExists(unsupportedFieldRoot));

    auto incompleteBindCookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, "material_bind_incomplete_block_scoped");
    ASSERT_TRUE(incompleteBindCookCase);
    Path& incompleteBindRoot = incompleteBindCookCase->root;
    EXPECT_FALSE(AssetsGraphicsFixture::CookMaterialBindMaterialIntegration(AssetsGraphicsFixture::s_SurfaceOnlyMaterialBindSource, AssetsGraphicsFixture::s_BlockScopedMaterialMeta, testArena, *incompleteBindCookCase));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("typed parameter 'runtime.fade_alpha' is not declared by interface")));

    EXPECT_TRUE(RemoveAllIfExists(incompleteBindRoot));

    auto interfaceShaderMismatchCookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, "material_bind_interface_without_bind_shader");
    ASSERT_TRUE(interfaceShaderMismatchCookCase);
    Path& interfaceShaderMismatchRoot = interfaceShaderMismatchCookCase->root;
    Path& interfaceShaderMismatchOutputDirectory = interfaceShaderMismatchCookCase->outputDirectory;

    const Path interfaceShaderMismatchAssetRoot = interfaceShaderMismatchRoot / s_AssetsDir;
    EXPECT_TRUE(AssetsGraphicsFixture::WriteMaterialBindMaterialIntegrationAssetsWithPixelSource(
        testArena,
        interfaceShaderMismatchAssetRoot,
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        AssetsGraphicsFixture::s_BlockScopedMaterialMeta,
        AssetsGraphicsFixture::s_UnboundMaterialShaderProbeSource
    ));
    EXPECT_FALSE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, interfaceShaderMismatchRoot, interfaceShaderMismatchOutputDirectory, { interfaceShaderMismatchAssetRoot }));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("does not include a generated material bind")));

    EXPECT_TRUE(RemoveAllIfExists(interfaceShaderMismatchRoot));

    auto interfaceIdentityMismatchCookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, "material_bind_interface_identity_mismatch");
    ASSERT_TRUE(interfaceIdentityMismatchCookCase);
    Path& interfaceIdentityMismatchRoot = interfaceIdentityMismatchCookCase->root;
    Path& interfaceIdentityMismatchOutputDirectory = interfaceIdentityMismatchCookCase->outputDirectory;

    const Path interfaceIdentityMismatchAssetRoot = interfaceIdentityMismatchRoot / s_AssetsDir;
    EXPECT_TRUE(AssetsGraphicsFixture::WriteMaterialBindMaterialIntegrationAssetsWithPixelSource(
        testArena,
        interfaceIdentityMismatchAssetRoot,
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        AssetsGraphicsFixture::s_BlockScopedMaterialMeta,
        AssetsGraphicsFixture::s_OtherMaterialBindShaderProbeSource
    ));
    EXPECT_TRUE(AssetsGraphicsFixture::WriteTextFile(interfaceIdentityMismatchAssetRoot / s_MaterialInterfaces / "other_surface.bind", AssetsGraphicsFixture::s_MinimalMaterialBindSource));
    EXPECT_FALSE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, interfaceIdentityMismatchRoot, interfaceIdentityMismatchOutputDirectory, { interfaceIdentityMismatchAssetRoot }));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("includes generated material bind interface 'project/material_interfaces/other_surface'")));

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


    const Path assetRoot = root / s_AssetsDir;
    const bool assetsWritten =
        AssetsGraphicsFixture::WriteTextFile(assetRoot / s_MaterialInterfaces / "shadow_dispatch_first.bind", AssetsGraphicsFixture::s_ShadowDispatchFirstMaterialBindSource)
        && AssetsGraphicsFixture::WriteTextFile(assetRoot / s_MaterialInterfaces / "shadow_dispatch_second.bind", AssetsGraphicsFixture::s_ShadowDispatchSecondMaterialBindSource)
        && AssetsGraphicsFixture::WriteTextFile(assetRoot / s_Shaders / "shadow_dispatch_shared_surface_helper.slangi", AssetsGraphicsFixture::s_ShadowDispatchSharedSurfaceHelperSource)
        && AssetsGraphicsFixture::WriteTextFile(assetRoot / s_Shaders / "shadow_dispatch_first.surface", AssetsGraphicsFixture::s_ShadowDispatchFirstMaterialSurfaceSource)
        && AssetsGraphicsFixture::WriteTextFile(assetRoot / s_Shaders / "shadow_dispatch_second.surface", AssetsGraphicsFixture::s_ShadowDispatchSecondMaterialSurfaceSource)
        && AssetsGraphicsFixture::WriteTextFile(assetRoot / s_Shaders / "shadow_dispatch.bxdf", AssetsGraphicsFixture::s_MaterialBindBxdfSource)
        && AssetsGraphicsFixture::WriteTextFile(assetRoot / s_Materials / "shadow_dispatch_first.nwb", AssetsGraphicsFixture::s_ShadowDispatchFirstMaterialMeta)
        && AssetsGraphicsFixture::WriteTextFile(assetRoot / s_Materials / "shadow_dispatch_second.nwb", AssetsGraphicsFixture::s_ShadowDispatchSecondMaterialMeta)
    ;
    ASSERT_TRUE(assetsWritten);

    const Path engineAssetRoot = AssetsGraphicsFixture::AssetsGraphicsTestRepoRoot(testArena) / "impl" / s_AssetsDir;
    const bool cooked = AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { engineAssetRoot, assetRoot });
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

    const Path assetRoot = root / s_AssetsDir;
    EXPECT_TRUE(AssetsGraphicsFixture::WriteMaterialBindMaterialIntegrationAssetsWithPixelSource(
        testArena,
        assetRoot,
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        AssetsGraphicsFixture::s_MissingInterfaceMaterialMeta,
        AssetsGraphicsFixture::s_UnboundMaterialShaderProbeSource
    ));
    EXPECT_FALSE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }));
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

    const Path assetRoot = root / s_AssetsDir;
    ASSERT_TRUE(AssetsGraphicsFixture::WriteMaterialBindMaterialIntegrationAssets(testArena, assetRoot, AssetsGraphicsFixture::s_MinimalMaterialBindSource, AssetsGraphicsFixture::s_BlockScopedMaterialMeta));

    EXPECT_TRUE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }));


    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    auto loadedAssetLoadResult = AssetsGraphicsFixture::LoadCookedMaterial(testArena, outputDirectory, Name(s_ProjectMaterialsTestMaterial));
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
    auto initialPixelSourceChecksumLoadResult = AssetsGraphicsFixture::FindShaderArchiveSourceChecksum(records, Name(s_MaterialPixelShaderPath), Name(s_Ps));
    ASSERT_TRUE(initialPixelSourceChecksumLoadResult);
    initialPixelSourceChecksum = *initialPixelSourceChecksumLoadResult;

    EXPECT_TRUE(AssetsGraphicsFixture::WriteTextFile(assetRoot / s_MaterialInterfaces / s_TestSurfaceBind, AssetsGraphicsFixture::s_UpdatedDefaultMaterialBindSource));
    EXPECT_TRUE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }));

    loadedAsset.reset();
    auto loadedAssetLoadResult2 = AssetsGraphicsFixture::LoadCookedMaterial(testArena, outputDirectory, Name(s_ProjectMaterialsTestMaterial));
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
    auto updatedPixelSourceChecksumLoadResult = AssetsGraphicsFixture::FindShaderArchiveSourceChecksum(records, Name(s_MaterialPixelShaderPath), Name(s_Ps));
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
    EXPECT_TRUE(AssetsGraphicsFixture::CookMinimalMeshWithMaterialBind(AssetsGraphicsFixture::s_MinimalMaterialBindSource, testArena, *cookCase));

    const Path generatedIncludePath = root / s_Cache / s_Tests / s_MaterialBindIncludes / s_Project / s_MaterialInterfaces / s_TestSurfaceBind;
    // Removing the last bind source must clear its generated include on a recook with the same cache.
    const Path assetRoot = root / "assets";
    const auto removedBind = RemoveFile(assetRoot / s_MaterialInterfaces / s_TestSurfaceBind);
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
    EXPECT_FALSE(AssetsGraphicsFixture::EmitDuplicateGeneratedMaterialBindIncludePath(testArena, *duplicateIncludeCookCase));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("duplicate material bind include path 'project/material_interfaces/test_surface.bind'")));

    EXPECT_TRUE(RemoveAllIfExists(duplicateIncludeRoot));

    auto invalidCookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, "material_bind_duplicate_field");
    ASSERT_TRUE(invalidCookCase);
    Path& invalidRoot = invalidCookCase->root;
    EXPECT_FALSE(AssetsGraphicsFixture::CookMinimalMeshWithMaterialBind(AssetsGraphicsFixture::s_DuplicateFieldMaterialBindSource, testArena, *invalidCookCase));
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

