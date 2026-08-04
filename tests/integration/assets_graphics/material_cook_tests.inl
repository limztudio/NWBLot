// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AssetsGraphics, MaterialBindCookIntegration){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
    EXPECT_TRUE(CookMaterialBindMaterialIntegration(
        s_MinimalMaterialBindSource,
        s_BlockScopedMaterialMeta,
        "material_bind_material_integration",
        testArena,
        root,
        outputDirectory
    ));

    const Path generatedIncludePath = root / "cache" / "tests" / "material_bind_includes" / "project" / "material_interfaces" / "test_surface.bind";
    const Path generatedCsgIncludeRoot = root / "cache" / "tests" / "csg_modules";
    const Path generatedCsgBuiltInIncludePath = generatedCsgIncludeRoot / "engine" / "csg" / "generated" / "built_in.slangi";
    NWB::Impl::ShaderCook::CookString generatedSource(testArena.arena);
    EXPECT_TRUE(ReadTextFile(generatedIncludePath, generatedSource));
    CheckGeneratedMaterialBindSource(AStringView(generatedSource.data(), generatedSource.size()));
    NWB::Impl::ShaderCook::CookString generatedCsgSource(testArena.arena);
    EXPECT_TRUE(ReadTextFile(generatedCsgBuiltInIncludePath, generatedCsgSource));

    NWB::Impl::ShaderCook shaderCook(testArena.arena);
    NWB::Impl::ShaderCook::CookVector<Path> includeDirectories(testArena.arena);
    includeDirectories.push_back(root / "cache" / "tests" / "material_bind_includes");
    includeDirectories.push_back(generatedCsgIncludeRoot);
    includeDirectories.push_back(AssetsGraphicsTestRepoRoot(testArena) / "impl" / "assets" / "graphics");
    NWB::Impl::ShaderCook::CookVector<Path> dependencies(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena(s_MaterialCookScratchArena);
    EXPECT_TRUE(shaderCook.gatherShaderDependencies(
        root / "assets" / "shaders" / "material_ps.slang",
        includeDirectories,
        dependencies,
        scratchArena
    ));
    EXPECT_TRUE(ContainsCanonicalPath(dependencies, generatedIncludePath));
    EXPECT_TRUE(ContainsCanonicalPath(dependencies, generatedCsgBuiltInIncludePath));

    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    EXPECT_TRUE(LoadCookedMaterial(
        testArena,
        outputDirectory,
        Name("project/materials/test_material"),
        loadedAsset
    ));
    if(loadedAsset){
        EXPECT_EQ(loadedAsset->assetType(), NWB::Impl::Material::AssetTypeName());
        const NWB::Impl::Material& material = static_cast<const NWB::Impl::Material&>(*loadedAsset);
        EXPECT_EQ(material.materialInterface(), Name("project/material_interfaces/test_surface"));
        CheckMinimalMaterialTypedLayout(material);
        CheckMinimalMaterialTypedBlockBytes(material);
        CheckGeneratedMaterialBindBinaryConstants(
            AStringView(generatedSource.data(), generatedSource.size()),
            material
        );
    }

    Path halfRoot(testArena.arena);
    Path halfOutputDirectory(testArena.arena);
    EXPECT_TRUE(CookMaterialBindMaterialIntegrationWithPixelSource(
        s_HalfMaterialBindSource,
        s_HalfMaterialMeta,
        s_HalfMaterialBindShaderProbeSource,
        "material_bind_half_material_integration",
        testArena,
        halfRoot,
        halfOutputDirectory
    ));

    const Path halfGeneratedIncludePath =
        halfRoot / "cache" / "tests" / "material_bind_includes"
        / "project" / "material_interfaces" / "test_surface.bind"
    ;
    NWB::Impl::ShaderCook::CookString halfGeneratedSource(testArena.arena);
    EXPECT_TRUE(ReadTextFile(halfGeneratedIncludePath, halfGeneratedSource));
    const AStringView halfGeneratedSourceView(halfGeneratedSource.data(), halfGeneratedSource.size());
    CheckGeneratedHalfMaterialBindSource(halfGeneratedSourceView);

    UniquePtr<NWB::Core::Assets::IAsset> loadedHalfAsset;
    EXPECT_TRUE(LoadCookedMaterial(
        testArena,
        halfOutputDirectory,
        Name("project/materials/test_material"),
        loadedHalfAsset
    ));
    if(loadedHalfAsset){
        EXPECT_EQ(loadedHalfAsset->assetType(), NWB::Impl::Material::AssetTypeName());
        const NWB::Impl::Material& halfMaterial = static_cast<const NWB::Impl::Material&>(*loadedHalfAsset);
        EXPECT_EQ(halfMaterial.materialInterface(), Name("project/material_interfaces/test_surface"));
        CheckHalfMaterialTypedLayoutAndBlockBytes(halfMaterial);
        CheckGeneratedMaterialBindBinaryConstants(halfGeneratedSourceView, halfMaterial);
    }

    Path compactRoot(testArena.arena);
    Path compactOutputDirectory(testArena.arena);
    EXPECT_TRUE(CookMaterialBindMaterialIntegrationWithPixelSource(
        s_CompactIntegerMaterialBindSource,
        s_CompactIntegerMaterialMeta,
        s_CompactIntegerMaterialBindShaderProbeSource,
        "material_bind_compact_integer_material_integration",
        testArena,
        compactRoot,
        compactOutputDirectory
    ));

    const Path compactGeneratedIncludePath =
        compactRoot / "cache" / "tests" / "material_bind_includes"
        / "project" / "material_interfaces" / "test_surface.bind"
    ;
    NWB::Impl::ShaderCook::CookString compactGeneratedSource(testArena.arena);
    EXPECT_TRUE(ReadTextFile(compactGeneratedIncludePath, compactGeneratedSource));
    const AStringView compactGeneratedSourceView(compactGeneratedSource.data(), compactGeneratedSource.size());
    CheckGeneratedCompactIntegerMaterialBindSource(compactGeneratedSourceView);

    UniquePtr<NWB::Core::Assets::IAsset> loadedCompactAsset;
    EXPECT_TRUE(LoadCookedMaterial(
        testArena,
        compactOutputDirectory,
        Name("project/materials/test_material"),
        loadedCompactAsset
    ));
    if(loadedCompactAsset){
        EXPECT_EQ(loadedCompactAsset->assetType(), NWB::Impl::Material::AssetTypeName());
        const NWB::Impl::Material& compactMaterial = static_cast<const NWB::Impl::Material&>(*loadedCompactAsset);
        EXPECT_EQ(compactMaterial.materialInterface(), Name("project/material_interfaces/test_surface"));
        CheckCompactIntegerMaterialTypedLayoutAndBlockBytes(compactMaterial);
        CheckGeneratedMaterialBindBinaryConstants(compactGeneratedSourceView, compactMaterial);
    }

    Path resourceRoot(testArena.arena);
    Path resourceOutputDirectory(testArena.arena);
    EXPECT_TRUE(CookMaterialBindMaterialIntegrationWithPixelSource(
        s_AssetResourceMaterialBindSource,
        s_AssetResourceMaterialMeta,
        s_AssetResourceShaderProbeSource,
        "material_bind_asset_resource_integration",
        testArena,
        resourceRoot,
        resourceOutputDirectory
    ));

    const Path resourceGeneratedIncludePath =
        resourceRoot / "cache" / "tests" / "material_bind_includes"
        / "project" / "material_interfaces" / "test_surface.bind"
    ;
    NWB::Impl::ShaderCook::CookString resourceGeneratedSource(testArena.arena);
    EXPECT_TRUE(ReadTextFile(resourceGeneratedIncludePath, resourceGeneratedSource));
    const AStringView resourceGeneratedSourceView(resourceGeneratedSource.data(), resourceGeneratedSource.size());
    const AStringView resourceGeneratedSnippets[] = {
        "Texture2D<float4> nwbMaterialBindLoadSurfaceBaseColorMap",
        "SamplerState nwbMaterialBindLoadSurfaceBaseColorSampler",
        "NwbHeapSampledImage2DNonUniform",
        "NwbHeapSamplerNonUniform",
    };
    CheckGeneratedSourceContainsAll(resourceGeneratedSourceView, resourceGeneratedSnippets);

    UniquePtr<NWB::Core::Assets::IAsset> loadedResourceAsset;
    EXPECT_TRUE(LoadCookedMaterial(
        testArena,
        resourceOutputDirectory,
        Name("project/materials/test_material"),
        loadedResourceAsset
    ));
    if(loadedResourceAsset){
        EXPECT_EQ(loadedResourceAsset->assetType(), NWB::Impl::Material::AssetTypeName());
        const NWB::Impl::Material& resourceMaterial = static_cast<const NWB::Impl::Material&>(*loadedResourceAsset);
        ASSERT_EQ(resourceMaterial.resourceReferences().size(), 2u);
        EXPECT_EQ(
            resourceMaterial.resourceReferences()[0u].resourceName,
            Name("project/textures/test_checker")
        );
        EXPECT_EQ(
            resourceMaterial.resourceReferences()[0u].resourceSource,
            NWB::Impl::MaterialResourceSource::Asset
        );
        EXPECT_EQ(
            resourceMaterial.resourceReferences()[1u].resourceName,
            Name("engine/samplers/linear_clamp")
        );
        EXPECT_EQ(
            resourceMaterial.resourceReferences()[1u].resourceSource,
            NWB::Impl::MaterialResourceSource::Asset
        );
    }

    EXPECT_EQ(logger.errorCount(), 0u);

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
    errorCode.clear();
    EXPECT_TRUE(RemoveAllIfExists(halfRoot, errorCode));
    errorCode.clear();
    EXPECT_TRUE(RemoveAllIfExists(compactRoot, errorCode));
    errorCode.clear();
    EXPECT_TRUE(RemoveAllIfExists(resourceRoot, errorCode));

#if defined(NWB_FINAL)
    Path invalidRoot(testArena.arena);
    Path invalidOutputDirectory(testArena.arena);
    EXPECT_FALSE(CookMaterialBindMaterialIntegration(
        s_MinimalMaterialBindSource,
        s_UnknownInterfaceParameterMaterialMeta,
        "material_bind_unknown_interface_parameter",
        testArena,
        invalidRoot,
        invalidOutputDirectory
    ));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT(
        "parameter 'surface.missing' is not declared by interface"
    )));

    errorCode.clear();
    EXPECT_TRUE(RemoveAllIfExists(invalidRoot, errorCode));

    Path flatRoot(testArena.arena);
    Path flatOutputDirectory(testArena.arena);
    EXPECT_FALSE(CookMaterialBindMaterialIntegration(
        s_MinimalMaterialBindSource,
        s_FlatInterfaceParameterMaterialMeta,
        "material_bind_flat_interface_parameter",
        testArena,
        flatRoot,
        flatOutputDirectory
    ));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT(
        "interface parameter 'runtime.fade_alpha' must be declared inside a block map"
    )));

    errorCode.clear();
    EXPECT_TRUE(RemoveAllIfExists(flatRoot, errorCode));

    Path untypedParameterRoot(testArena.arena);
    Path untypedParameterOutputDirectory(testArena.arena);
    EXPECT_FALSE(CookMaterialBindMaterialIntegration(
        s_MinimalMaterialBindSource,
        s_UntypedMaterialParameterMeta,
        "material_bind_untyped_material_parameter",
        testArena,
        untypedParameterRoot,
        untypedParameterOutputDirectory
    ));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT(
        "has invalid value '0.25, 0.5, 0.75, 1.0'"
    )));

    errorCode.clear();
    EXPECT_TRUE(RemoveAllIfExists(untypedParameterRoot, errorCode));

    Path vectorAliasParameterRoot(testArena.arena);
    Path vectorAliasParameterOutputDirectory(testArena.arena);
    EXPECT_FALSE(CookMaterialBindMaterialIntegration(
        s_MinimalMaterialBindSource,
        s_VectorAliasMaterialParameterMeta,
        "material_bind_vector_alias_material_parameter",
        testArena,
        vectorAliasParameterRoot,
        vectorAliasParameterOutputDirectory
    ));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT(
        "has invalid value 'vec4(0.25, 0.5, 0.75, 1.0)'"
    )));

    errorCode.clear();
    EXPECT_TRUE(RemoveAllIfExists(vectorAliasParameterRoot, errorCode));

    Path unsupportedFieldRoot(testArena.arena);
    Path unsupportedFieldOutputDirectory(testArena.arena);
    EXPECT_FALSE(CookMaterialBindMaterialIntegration(
        s_MinimalMaterialBindSource,
        s_UnsupportedMaterialFieldMeta,
        "material_bind_unsupported_material_field",
        testArena,
        unsupportedFieldRoot,
        unsupportedFieldOutputDirectory
    ));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("unsupported asset field 'compiler'")));

    errorCode.clear();
    EXPECT_TRUE(RemoveAllIfExists(unsupportedFieldRoot, errorCode));

    Path incompleteBindRoot(testArena.arena);
    Path incompleteBindOutputDirectory(testArena.arena);
    EXPECT_FALSE(CookMaterialBindMaterialIntegration(
        s_SurfaceOnlyMaterialBindSource,
        s_BlockScopedMaterialMeta,
        "material_bind_incomplete_block_scoped",
        testArena,
        incompleteBindRoot,
        incompleteBindOutputDirectory
    ));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT(
        "typed parameter 'runtime.fade_alpha' is not declared by interface"
    )));

    errorCode.clear();
    EXPECT_TRUE(RemoveAllIfExists(incompleteBindRoot, errorCode));

    Path interfaceShaderMismatchRoot(testArena.arena);
    Path interfaceShaderMismatchOutputDirectory(testArena.arena);
    EXPECT_TRUE(PrepareAssetsGraphicsCookCase(
        testArena,
        "material_bind_interface_without_bind_shader",
        interfaceShaderMismatchRoot,
        interfaceShaderMismatchOutputDirectory
    ));
    const Path interfaceShaderMismatchAssetRoot = interfaceShaderMismatchRoot / "assets";
    EXPECT_TRUE(WriteMaterialBindMaterialIntegrationAssetsWithPixelSource(
        testArena,
        interfaceShaderMismatchAssetRoot,
        s_MinimalMaterialBindSource,
        s_BlockScopedMaterialMeta,
        s_UnboundMaterialShaderProbeSource
    ));
    EXPECT_FALSE(CookPreparedGraphicsAssetRoots(
        testArena,
        interfaceShaderMismatchRoot,
        interfaceShaderMismatchOutputDirectory,
        { interfaceShaderMismatchAssetRoot }
    ));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("does not include a generated material bind")));

    errorCode.clear();
    EXPECT_TRUE(RemoveAllIfExists(interfaceShaderMismatchRoot, errorCode));

    Path interfaceIdentityMismatchRoot(testArena.arena);
    Path interfaceIdentityMismatchOutputDirectory(testArena.arena);
    EXPECT_TRUE(PrepareAssetsGraphicsCookCase(
        testArena,
        "material_bind_interface_identity_mismatch",
        interfaceIdentityMismatchRoot,
        interfaceIdentityMismatchOutputDirectory
    ));
    const Path interfaceIdentityMismatchAssetRoot = interfaceIdentityMismatchRoot / "assets";
    EXPECT_TRUE(WriteMaterialBindMaterialIntegrationAssetsWithPixelSource(
        testArena,
        interfaceIdentityMismatchAssetRoot,
        s_MinimalMaterialBindSource,
        s_BlockScopedMaterialMeta,
        s_OtherMaterialBindShaderProbeSource
    ));
    EXPECT_TRUE(WriteTextFile(
        interfaceIdentityMismatchAssetRoot / "material_interfaces" / "other_surface.bind",
        s_MinimalMaterialBindSource
    ));
    EXPECT_FALSE(CookPreparedGraphicsAssetRoots(
        testArena,
        interfaceIdentityMismatchRoot,
        interfaceIdentityMismatchOutputDirectory,
        { interfaceIdentityMismatchAssetRoot }
    ));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT(
        "includes generated material bind interface 'project/material_interfaces/other_surface'"
    )));

    errorCode.clear();
    EXPECT_TRUE(RemoveAllIfExists(interfaceIdentityMismatchRoot, errorCode));

#endif
}

TEST(AssetsGraphics, TransparentMaterialCookUsesViewDependentSurface){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
    const bool cooked = CookMaterialSurfaceIntegration(
        s_MinimalMaterialBindSource,
        s_TransparentMaterialMeta,
        s_ViewDependentTransparentMaterialSurfaceSource,
        "material_view_dependent_transparent_surface",
        testArena,
        root,
        outputDirectory
    );
    EXPECT_TRUE(cooked);
    if(cooked){
        UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
        EXPECT_TRUE(LoadCookedMaterial(
            testArena,
            outputDirectory,
            Name("project/materials/test_material"),
            loadedAsset
        ));
        if(loadedAsset){
            const NWB::Impl::Material& material = static_cast<const NWB::Impl::Material&>(*loadedAsset);
            EXPECT_TRUE(material.transparent());
            EXPECT_EQ(
                material.avboitAccumulatePixelShader().virtualPath,
                Name("generated/avboit_accumulate_ps/project/materials/test_material")
            );
            EXPECT_EQ(
                material.avboitOccupancyPixelShader().virtualPath,
                Name("generated/avboit_occupancy_ps/project/materials/test_material")
            );
            EXPECT_EQ(
                material.avboitExtinctionPixelShader().virtualPath,
                Name("generated/avboit_extinction_ps/project/materials/test_material")
            );
            EXPECT_TRUE(NWB::Impl::HasValidMaterialAvboitPixelShaderContract(
                material.transparent(),
                material.avboitAccumulatePixelShader(),
                material.avboitOccupancyPixelShader(),
                material.avboitExtinctionPixelShader()
            ));
        }

        NWB::Core::GraphicsVector<NWB::Core::ShaderArchive::Record> records(testArena.arena);
        EXPECT_TRUE(LoadCookedShaderArchiveRecords(testArena, outputDirectory, records));
        const Name pixelStageName("ps");
        const Name generatedPixelShaderName("generated/material_ps/project/materials/test_material");
        const Name accumulatePixelShaderName("generated/avboit_accumulate_ps/project/materials/test_material");
        const Name occupancyPixelShaderName("generated/avboit_occupancy_ps/project/materials/test_material");
        const Name extinctionPixelShaderName("generated/avboit_extinction_ps/project/materials/test_material");
        u64 sourceChecksum = 0u;
        EXPECT_TRUE(FindShaderArchiveSourceChecksum(records, generatedPixelShaderName, pixelStageName, sourceChecksum));
        EXPECT_TRUE(FindShaderArchiveSourceChecksum(records, accumulatePixelShaderName, pixelStageName, sourceChecksum));
        EXPECT_TRUE(FindShaderArchiveSourceChecksum(records, occupancyPixelShaderName, pixelStageName, sourceChecksum));
        EXPECT_TRUE(FindShaderArchiveSourceChecksum(records, extinctionPixelShaderName, pixelStageName, sourceChecksum));
    }

    EXPECT_EQ(logger.errorCount(), 0u);

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
}

TEST(AssetsGraphics, ShadowTransmittanceDispatchIsolatesOverlappingBindApis){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
    ASSERT_TRUE(PrepareAssetsGraphicsCookCase(
        testArena,
        "shadow_transmittance_dispatch_overlapping_bind_apis",
        root,
        outputDirectory
    ));

    const Path assetRoot = root / "assets";
    const bool assetsWritten =
        WriteTextFile(
            assetRoot / "material_interfaces" / "shadow_dispatch_first.bind",
            s_ShadowDispatchFirstMaterialBindSource
        )
        && WriteTextFile(
            assetRoot / "material_interfaces" / "shadow_dispatch_second.bind",
            s_ShadowDispatchSecondMaterialBindSource
        )
        && WriteTextFile(
            assetRoot / "shaders" / "shadow_dispatch_shared_surface_helper.slangi",
            s_ShadowDispatchSharedSurfaceHelperSource
        )
        && WriteTextFile(
            assetRoot / "shaders" / "shadow_dispatch_first.surface",
            s_ShadowDispatchFirstMaterialSurfaceSource
        )
        && WriteTextFile(
            assetRoot / "shaders" / "shadow_dispatch_second.surface",
            s_ShadowDispatchSecondMaterialSurfaceSource
        )
        && WriteTextFile(assetRoot / "shaders" / "shadow_dispatch.bxdf", s_MaterialBindBxdfSource)
        && WriteTextFile(
            assetRoot / "materials" / "shadow_dispatch_first.nwb",
            s_ShadowDispatchFirstMaterialMeta
        )
        && WriteTextFile(
            assetRoot / "materials" / "shadow_dispatch_second.nwb",
            s_ShadowDispatchSecondMaterialMeta
        )
    ;
    ASSERT_TRUE(assetsWritten);

    const Path engineAssetRoot = AssetsGraphicsTestRepoRoot(testArena) / "impl" / "assets";
    const bool cooked = CookPreparedGraphicsAssetRoots(
        testArena,
        root,
        outputDirectory,
        { engineAssetRoot, assetRoot }
    );
    EXPECT_TRUE(cooked);
    if(cooked){
        const Path generatedDispatchPath =
            root / "cache" / "tests" / "shadow_modules" / "shadow" / "generated" / "transmittance_dispatch.slangi"
        ;
        NWB::Impl::ShaderCook::CookString generatedDispatchSource(testArena.arena);
        EXPECT_TRUE(ReadTextFile(generatedDispatchPath, generatedDispatchSource));
        const AStringView generatedDispatch(generatedDispatchSource.data(), generatedDispatchSource.size());
        EXPECT_FALSE(ContainsText(generatedDispatch, "using namespace nwbShadowBindModel"));
        EXPECT_TRUE(ContainsText(
            generatedDispatch,
            "#define nwbMaterialBindLoadSurface nwbShadowBindModel"
        ));
        EXPECT_TRUE(ContainsText(generatedDispatch, "#undef nwbMaterialBindLoadSurface"));
    }
    EXPECT_EQ(logger.errorCount(), 0u);

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
}

TEST(AssetsGraphics, MaterialRejectsMissingInterfaceCookIntegration){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
    EXPECT_TRUE(PrepareAssetsGraphicsCookCase(
        testArena,
        "material_missing_interface_rejection",
        root,
        outputDirectory
    ));
    const Path assetRoot = root / "assets";
    EXPECT_TRUE(WriteMaterialBindMaterialIntegrationAssetsWithPixelSource(
        testArena,
        assetRoot,
        s_MinimalMaterialBindSource,
        s_MissingInterfaceMaterialMeta,
        s_UnboundMaterialShaderProbeSource
    ));
    EXPECT_FALSE(CookPreparedGraphicsAssetRoots(
        testArena,
        root,
        outputDirectory,
        { assetRoot }
    ));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("interface is required")));

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
#else
#endif
}

TEST(AssetsGraphics, MaterialBindDependencyInvalidation){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
    EXPECT_TRUE(PrepareAssetsGraphicsCookCase(
        testArena,
        "material_bind_dependency_invalidation",
        root,
        outputDirectory
    ));
    const Path assetRoot = root / "assets";
    if(!WriteMaterialBindMaterialIntegrationAssets(
        testArena,
        assetRoot,
        s_MinimalMaterialBindSource,
        s_BlockScopedMaterialMeta
    ))
        return;

    EXPECT_TRUE(CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }));

    const Path generatedIncludePath = root / "cache" / "tests" / "material_bind_includes" / "project" / "material_interfaces" / "test_surface.bind";
    NWB::Impl::ShaderCook::CookString generatedSource(testArena.arena);
    EXPECT_TRUE(ReadTextFile(generatedIncludePath, generatedSource));

    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    EXPECT_TRUE(LoadCookedMaterial(
        testArena,
        outputDirectory,
        Name("project/materials/test_material"),
        loadedAsset
    ));
    if(!loadedAsset)
        return;

    const NWB::Impl::Material& material = static_cast<const NWB::Impl::Material&>(*loadedAsset);
    CheckMinimalMaterialTypedLayout(material);
    CheckMinimalMaterialTypedBlockBytes(material);
    CheckGeneratedMaterialBindBinaryConstants(AStringView(generatedSource.data(), generatedSource.size()), material);
    const u64 initialLayoutHash = material.typedLayoutHash();

    NWB::Core::GraphicsVector<NWB::Core::ShaderArchive::Record> records(testArena.arena);
    EXPECT_TRUE(LoadCookedShaderArchiveRecords(testArena, outputDirectory, records));
    u64 initialPixelSourceChecksum = 0u;
    EXPECT_TRUE(FindShaderArchiveSourceChecksum(
        records,
        Name("project/shaders/material_ps"),
        Name("ps"),
        initialPixelSourceChecksum
    ));

    EXPECT_TRUE(WriteTextFile(
        assetRoot / "material_interfaces" / "test_surface.bind",
        s_UpdatedDefaultMaterialBindSource
    ));
    EXPECT_TRUE(CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }));

    generatedSource.clear();
    EXPECT_TRUE(ReadTextFile(generatedIncludePath, generatedSource));
    const AStringView updatedGeneratedSource(generatedSource.data(), generatedSource.size());
    EXPECT_TRUE(ContainsText(
        updatedGeneratedSource,
        "static const uint3 NWB_MATERIAL_BIND_SURFACE_FEATURE_MASK_DEFAULT = uint3(7u, 8u, 9u);"
    ));

    loadedAsset.reset();
    EXPECT_TRUE(LoadCookedMaterial(
        testArena,
        outputDirectory,
        Name("project/materials/test_material"),
        loadedAsset
    ));
    if(loadedAsset){
        const NWB::Impl::Material& updatedMaterial = static_cast<const NWB::Impl::Material&>(*loadedAsset);
        CheckMinimalMaterialTypedLayout(updatedMaterial, 7u, 8u, 9u);
        CheckMinimalMaterialTypedBlockBytes(updatedMaterial, 7u, 8u, 9u);
        CheckGeneratedMaterialBindBinaryConstants(updatedGeneratedSource, updatedMaterial);
        EXPECT_NE(updatedMaterial.typedLayoutHash(), initialLayoutHash);
    }

    records.clear();
    EXPECT_TRUE(LoadCookedShaderArchiveRecords(testArena, outputDirectory, records));
    u64 updatedPixelSourceChecksum = 0u;
    EXPECT_TRUE(FindShaderArchiveSourceChecksum(
        records,
        Name("project/shaders/material_ps"),
        Name("ps"),
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
    EXPECT_TRUE(CookMinimalMeshWithMaterialBind(
        s_MinimalMaterialBindSource,
        "material_bind_valid",
        testArena,
        root,
        outputDirectory
    ));

    const Path generatedIncludePath = root / "cache" / "tests" / "material_bind_includes" / "project" / "material_interfaces" / "test_surface.bind";
    NWB::Impl::ShaderCook::CookString generatedSource(testArena.arena);
    EXPECT_TRUE(ReadTextFile(generatedIncludePath, generatedSource));
    const AStringView generatedSourceView(generatedSource.data(), generatedSource.size());
    CheckGeneratedMaterialBindSource(generatedSourceView);

    const Path shaderIncludeProbePath = root / "shader_include_probe.slang";
    EXPECT_TRUE(WriteTextFile(
        shaderIncludeProbePath,
        "#include \"project/material_interfaces/test_surface.bind\"\n"
    ));
    NWB::Impl::ShaderCook shaderCook(testArena.arena);
    NWB::Impl::ShaderCook::CookVector<Path> includeDirectories(testArena.arena);
    includeDirectories.push_back(root / "cache" / "tests" / "material_bind_includes");
    NWB::Impl::ShaderCook::CookVector<Path> dependencies(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena(s_MaterialCookScratchArena);
    EXPECT_TRUE(shaderCook.gatherShaderDependencies(
        shaderIncludeProbePath,
        includeDirectories,
        dependencies,
        scratchArena
    ));
    EXPECT_TRUE(ContainsCanonicalPath(dependencies, generatedIncludePath));
    EXPECT_EQ(logger.errorCount(), 0u);

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));

    Path shaderProbeRoot(testArena.arena);
    Path shaderProbeOutputDirectory(testArena.arena);
    EXPECT_TRUE(CookMaterialBindShaderProbe(
        s_MinimalMaterialBindSource,
        "material_bind_shader_probe",
        testArena,
        shaderProbeRoot,
        shaderProbeOutputDirectory
    ));
    EXPECT_EQ(logger.errorCount(), 0u);

    errorCode.clear();
    EXPECT_TRUE(RemoveAllIfExists(shaderProbeRoot, errorCode));

#if defined(NWB_FINAL)
    Path duplicateIncludeRoot(testArena.arena);
    Path duplicateIncludeOutputDirectory(testArena.arena);
    EXPECT_FALSE(CookDuplicateGeneratedMaterialBindIncludePath(
        "material_bind_duplicate_include_path",
        testArena,
        duplicateIncludeRoot,
        duplicateIncludeOutputDirectory
    ));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT(
        "duplicate material bind include path 'project/material_interfaces/test_surface.bind'"
    )));

    errorCode.clear();
    EXPECT_TRUE(RemoveAllIfExists(duplicateIncludeRoot, errorCode));

    Path invalidRoot(testArena.arena);
    Path invalidOutputDirectory(testArena.arena);
    EXPECT_FALSE(CookMinimalMeshWithMaterialBind(
        s_DuplicateFieldMaterialBindSource,
        "material_bind_duplicate_field",
        testArena,
        invalidRoot,
        invalidOutputDirectory
    ));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("duplicate struct field declaration")));

    errorCode.clear();
    EXPECT_TRUE(RemoveAllIfExists(invalidRoot, errorCode));
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

