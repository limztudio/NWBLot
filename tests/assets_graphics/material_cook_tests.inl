// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void TestMaterialBindCookIntegration(TestContext& context){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, CookMaterialBindMaterialIntegration(
        s_MinimalMaterialBindSource,
        s_BlockScopedMaterialMeta,
        "material_bind_material_integration",
        testArena,
        root,
        outputDirectory
    ));

    const Path generatedIncludePath = root / "cache" / "tests" / "material_bind_includes" / "project" / "material_interfaces" / "test_surface.bind";
    const Path generatedCsgIncludeRoot = root / "cache" / "tests" / "csg_modules";
    const Path generatedCsgBuiltInIncludePath = generatedCsgIncludeRoot / "csg" / "generated" / "built_in.slangi";
    NWB::Impl::ShaderCook::CookString generatedSource(testArena.arena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ReadTextFile(generatedIncludePath, generatedSource));
    CheckGeneratedMaterialBindSource(context, AStringView(generatedSource.data(), generatedSource.size()));
    NWB::Impl::ShaderCook::CookString generatedCsgSource(testArena.arena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ReadTextFile(generatedCsgBuiltInIncludePath, generatedCsgSource));

    NWB::Impl::ShaderCook shaderCook(testArena.arena);
    NWB::Impl::ShaderCook::CookVector<Path> includeDirectories(testArena.arena);
    includeDirectories.push_back(root / "cache" / "tests" / "material_bind_includes");
    includeDirectories.push_back(generatedCsgIncludeRoot);
    includeDirectories.push_back(AssetsGraphicsTestRepoRoot(testArena) / "impl" / "assets" / "graphics");
    NWB::Impl::ShaderCook::CookVector<Path> dependencies(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena(s_MaterialCookScratchArena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, shaderCook.gatherShaderDependencies(
        root / "assets" / "shaders" / "material_mesh.slang",
        includeDirectories,
        dependencies,
        scratchArena
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsCanonicalPath(dependencies, generatedIncludePath));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsCanonicalPath(dependencies, generatedCsgBuiltInIncludePath));

    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, LoadCookedMaterial(
        context,
        testArena,
        outputDirectory,
        Name("project/materials/test_material"),
        loadedAsset
    ));
    if(loadedAsset){
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedAsset->assetType() == NWB::Impl::Material::AssetTypeName());
        const NWB::Impl::Material& material = static_cast<const NWB::Impl::Material&>(*loadedAsset);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(
            context,
            material.materialInterface() == Name("project/material_interfaces/test_surface")
        );
        CheckMinimalMaterialTypedLayout(context, material);
        CheckMinimalMaterialTypedBlockBytes(context, material);
        CheckGeneratedMaterialBindBinaryConstants(
            context,
            AStringView(generatedSource.data(), generatedSource.size()),
            material
        );
    }

    Path halfRoot(testArena.arena);
    Path halfOutputDirectory(testArena.arena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, CookMaterialBindMaterialIntegrationWithMeshSource(
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
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ReadTextFile(halfGeneratedIncludePath, halfGeneratedSource));
    const AStringView halfGeneratedSourceView(halfGeneratedSource.data(), halfGeneratedSource.size());
    CheckGeneratedHalfMaterialBindSource(context, halfGeneratedSourceView);

    UniquePtr<NWB::Core::Assets::IAsset> loadedHalfAsset;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, LoadCookedMaterial(
        context,
        testArena,
        halfOutputDirectory,
        Name("project/materials/test_material"),
        loadedHalfAsset
    ));
    if(loadedHalfAsset){
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedHalfAsset->assetType() == NWB::Impl::Material::AssetTypeName());
        const NWB::Impl::Material& halfMaterial = static_cast<const NWB::Impl::Material&>(*loadedHalfAsset);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(
            context,
            halfMaterial.materialInterface() == Name("project/material_interfaces/test_surface")
        );
        CheckHalfMaterialTypedLayoutAndBlockBytes(context, halfMaterial);
        CheckGeneratedMaterialBindBinaryConstants(context, halfGeneratedSourceView, halfMaterial);
    }

    Path compactRoot(testArena.arena);
    Path compactOutputDirectory(testArena.arena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, CookMaterialBindMaterialIntegrationWithMeshSource(
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
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ReadTextFile(compactGeneratedIncludePath, compactGeneratedSource));
    const AStringView compactGeneratedSourceView(compactGeneratedSource.data(), compactGeneratedSource.size());
    CheckGeneratedCompactIntegerMaterialBindSource(context, compactGeneratedSourceView);

    UniquePtr<NWB::Core::Assets::IAsset> loadedCompactAsset;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, LoadCookedMaterial(
        context,
        testArena,
        compactOutputDirectory,
        Name("project/materials/test_material"),
        loadedCompactAsset
    ));
    if(loadedCompactAsset){
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedCompactAsset->assetType() == NWB::Impl::Material::AssetTypeName());
        const NWB::Impl::Material& compactMaterial = static_cast<const NWB::Impl::Material&>(*loadedCompactAsset);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(
            context,
            compactMaterial.materialInterface() == Name("project/material_interfaces/test_surface")
        );
        CheckCompactIntegerMaterialTypedLayoutAndBlockBytes(context, compactMaterial);
        CheckGeneratedMaterialBindBinaryConstants(context, compactGeneratedSourceView, compactMaterial);
    }

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);

    ErrorCode errorCode;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, RemoveAllIfExists(root, errorCode));
    errorCode.clear();
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, RemoveAllIfExists(halfRoot, errorCode));
    errorCode.clear();
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, RemoveAllIfExists(compactRoot, errorCode));

#if defined(NWB_FINAL)
    Path invalidRoot(testArena.arena);
    Path invalidOutputDirectory(testArena.arena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !CookMaterialBindMaterialIntegration(
        s_MinimalMaterialBindSource,
        s_UnknownInterfaceParameterMaterialMeta,
        "material_bind_unknown_interface_parameter",
        testArena,
        invalidRoot,
        invalidOutputDirectory
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT(
        "parameter 'surface.missing' is not declared by interface"
    )));

    errorCode.clear();
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, RemoveAllIfExists(invalidRoot, errorCode));

    Path flatRoot(testArena.arena);
    Path flatOutputDirectory(testArena.arena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !CookMaterialBindMaterialIntegration(
        s_MinimalMaterialBindSource,
        s_FlatInterfaceParameterMaterialMeta,
        "material_bind_flat_interface_parameter",
        testArena,
        flatRoot,
        flatOutputDirectory
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT(
        "interface parameter 'runtime.fade_alpha' must be declared inside a block map"
    )));

    errorCode.clear();
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, RemoveAllIfExists(flatRoot, errorCode));

    Path untypedParameterRoot(testArena.arena);
    Path untypedParameterOutputDirectory(testArena.arena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !CookMaterialBindMaterialIntegration(
        s_MinimalMaterialBindSource,
        s_UntypedMaterialParameterMeta,
        "material_bind_untyped_material_parameter",
        testArena,
        untypedParameterRoot,
        untypedParameterOutputDirectory
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT(
        "has invalid value '0.25, 0.5, 0.75, 1.0'"
    )));

    errorCode.clear();
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, RemoveAllIfExists(untypedParameterRoot, errorCode));

    Path vectorAliasParameterRoot(testArena.arena);
    Path vectorAliasParameterOutputDirectory(testArena.arena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !CookMaterialBindMaterialIntegration(
        s_MinimalMaterialBindSource,
        s_VectorAliasMaterialParameterMeta,
        "material_bind_vector_alias_material_parameter",
        testArena,
        vectorAliasParameterRoot,
        vectorAliasParameterOutputDirectory
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT(
        "has invalid value 'vec4(0.25, 0.5, 0.75, 1.0)'"
    )));

    errorCode.clear();
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, RemoveAllIfExists(vectorAliasParameterRoot, errorCode));

    Path unsupportedFieldRoot(testArena.arena);
    Path unsupportedFieldOutputDirectory(testArena.arena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !CookMaterialBindMaterialIntegration(
        s_MinimalMaterialBindSource,
        s_UnsupportedMaterialFieldMeta,
        "material_bind_unsupported_material_field",
        testArena,
        unsupportedFieldRoot,
        unsupportedFieldOutputDirectory
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("unsupported asset field 'compiler'")));

    errorCode.clear();
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, RemoveAllIfExists(unsupportedFieldRoot, errorCode));

    Path missingShaderVariantRoot(testArena.arena);
    Path missingShaderVariantOutputDirectory(testArena.arena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !CookMaterialBindMaterialIntegration(
        s_MinimalMaterialBindSource,
        s_MissingShaderVariantMaterialMeta,
        "material_bind_missing_shader_variant",
        testArena,
        missingShaderVariantRoot,
        missingShaderVariantOutputDirectory
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("field 'shader_variant' is required")));

    errorCode.clear();
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, RemoveAllIfExists(missingShaderVariantRoot, errorCode));

    Path incompleteBindRoot(testArena.arena);
    Path incompleteBindOutputDirectory(testArena.arena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !CookMaterialBindMaterialIntegration(
        s_SurfaceOnlyMaterialBindSource,
        s_BlockScopedMaterialMeta,
        "material_bind_incomplete_block_scoped",
        testArena,
        incompleteBindRoot,
        incompleteBindOutputDirectory
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT(
        "typed parameter 'runtime.fade_alpha' is not declared by interface"
    )));

    errorCode.clear();
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, RemoveAllIfExists(incompleteBindRoot, errorCode));

    Path interfaceShaderMismatchRoot(testArena.arena);
    Path interfaceShaderMismatchOutputDirectory(testArena.arena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, PrepareAssetsGraphicsCookCase(
        testArena,
        "material_bind_interface_without_bind_shader",
        interfaceShaderMismatchRoot,
        interfaceShaderMismatchOutputDirectory
    ));
    const Path interfaceShaderMismatchAssetRoot = interfaceShaderMismatchRoot / "assets";
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, WriteMaterialBindMaterialIntegrationAssetsWithMeshSource(
        testArena,
        interfaceShaderMismatchAssetRoot,
        s_MinimalMaterialBindSource,
        s_BlockScopedMaterialMeta,
        s_UnboundMaterialShaderProbeSource
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !CookPreparedGraphicsAssetRoots(
        testArena,
        interfaceShaderMismatchRoot,
        interfaceShaderMismatchOutputDirectory,
        { interfaceShaderMismatchAssetRoot }
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("does not include a generated material bind")));

    errorCode.clear();
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, RemoveAllIfExists(interfaceShaderMismatchRoot, errorCode));

    Path interfaceIdentityMismatchRoot(testArena.arena);
    Path interfaceIdentityMismatchOutputDirectory(testArena.arena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, PrepareAssetsGraphicsCookCase(
        testArena,
        "material_bind_interface_identity_mismatch",
        interfaceIdentityMismatchRoot,
        interfaceIdentityMismatchOutputDirectory
    ));
    const Path interfaceIdentityMismatchAssetRoot = interfaceIdentityMismatchRoot / "assets";
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, WriteMaterialBindMaterialIntegrationAssetsWithMeshSource(
        testArena,
        interfaceIdentityMismatchAssetRoot,
        s_MinimalMaterialBindSource,
        s_BlockScopedMaterialMeta,
        s_OtherMaterialBindShaderProbeSource
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, WriteTextFile(
        interfaceIdentityMismatchAssetRoot / "material_interfaces" / "other_surface.bind",
        s_MinimalMaterialBindSource
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !CookPreparedGraphicsAssetRoots(
        testArena,
        interfaceIdentityMismatchRoot,
        interfaceIdentityMismatchOutputDirectory,
        { interfaceIdentityMismatchAssetRoot }
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT(
        "includes generated material bind interface 'project/material_interfaces/other_surface'"
    )));

    errorCode.clear();
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, RemoveAllIfExists(interfaceIdentityMismatchRoot, errorCode));

#endif
}

static void TestMaterialRejectsMissingInterfaceCookIntegration(TestContext& context){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, PrepareAssetsGraphicsCookCase(
        testArena,
        "material_missing_interface_rejection",
        root,
        outputDirectory
    ));
    const Path assetRoot = root / "assets";
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, WriteMaterialBindMaterialIntegrationAssetsWithMeshSource(
        testArena,
        assetRoot,
        s_MinimalMaterialBindSource,
        s_MissingInterfaceMaterialMeta,
        s_UnboundMaterialShaderProbeSource
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !CookPreparedGraphicsAssetRoots(
        testArena,
        root,
        outputDirectory,
        { assetRoot }
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("interface is required")));

    ErrorCode errorCode;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, RemoveAllIfExists(root, errorCode));
#else
    static_cast<void>(context);
#endif
}

static void TestMaterialBindDependencyInvalidation(TestContext& context){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, PrepareAssetsGraphicsCookCase(
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

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }));

    const Path generatedIncludePath = root / "cache" / "tests" / "material_bind_includes" / "project" / "material_interfaces" / "test_surface.bind";
    NWB::Impl::ShaderCook::CookString generatedSource(testArena.arena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ReadTextFile(generatedIncludePath, generatedSource));

    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, LoadCookedMaterial(
        context,
        testArena,
        outputDirectory,
        Name("project/materials/test_material"),
        loadedAsset
    ));
    if(!loadedAsset)
        return;

    const NWB::Impl::Material& material = static_cast<const NWB::Impl::Material&>(*loadedAsset);
    CheckMinimalMaterialTypedLayout(context, material);
    CheckMinimalMaterialTypedBlockBytes(context, material);
    CheckGeneratedMaterialBindBinaryConstants(context, AStringView(generatedSource.data(), generatedSource.size()), material);
    const u64 initialLayoutHash = material.typedLayoutHash();

    NWB::Core::GraphicsVector<NWB::Core::ShaderArchive::Record> records(testArena.arena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, LoadCookedShaderArchiveRecords(context, testArena, outputDirectory, records));
    u64 initialMeshSourceChecksum = 0u;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, FindShaderArchiveSourceChecksum(
        records,
        Name("project/shaders/material_mesh"),
        Name("mesh"),
        initialMeshSourceChecksum
    ));

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, WriteTextFile(
        assetRoot / "material_interfaces" / "test_surface.bind",
        s_UpdatedDefaultMaterialBindSource
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }));

    generatedSource.clear();
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ReadTextFile(generatedIncludePath, generatedSource));
    const AStringView updatedGeneratedSource(generatedSource.data(), generatedSource.size());
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsText(
        updatedGeneratedSource,
        "static const uint3 NWB_MATERIAL_BIND_SURFACE_FEATURE_MASK_DEFAULT = uint3(7u, 8u, 9u);"
    ));

    loadedAsset.reset();
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, LoadCookedMaterial(
        context,
        testArena,
        outputDirectory,
        Name("project/materials/test_material"),
        loadedAsset
    ));
    if(loadedAsset){
        const NWB::Impl::Material& updatedMaterial = static_cast<const NWB::Impl::Material&>(*loadedAsset);
        CheckMinimalMaterialTypedLayout(context, updatedMaterial, 7u, 8u, 9u);
        CheckMinimalMaterialTypedBlockBytes(context, updatedMaterial, 7u, 8u, 9u);
        CheckGeneratedMaterialBindBinaryConstants(context, updatedGeneratedSource, updatedMaterial);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, updatedMaterial.typedLayoutHash() != initialLayoutHash);
    }

    records.clear();
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, LoadCookedShaderArchiveRecords(context, testArena, outputDirectory, records));
    u64 updatedMeshSourceChecksum = 0u;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, FindShaderArchiveSourceChecksum(
        records,
        Name("project/shaders/material_mesh"),
        Name("mesh"),
        updatedMeshSourceChecksum
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, updatedMeshSourceChecksum != initialMeshSourceChecksum);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);

    ErrorCode errorCode;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, RemoveAllIfExists(root, errorCode));
}

static void TestMaterialBindDiscoveryValidation(TestContext& context){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, CookMinimalMeshWithMaterialBind(
        s_MinimalMaterialBindSource,
        "material_bind_valid",
        testArena,
        root,
        outputDirectory
    ));

    const Path generatedIncludePath = root / "cache" / "tests" / "material_bind_includes" / "project" / "material_interfaces" / "test_surface.bind";
    NWB::Impl::ShaderCook::CookString generatedSource(testArena.arena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ReadTextFile(generatedIncludePath, generatedSource));
    const AStringView generatedSourceView(generatedSource.data(), generatedSource.size());
    CheckGeneratedMaterialBindSource(context, generatedSourceView);

    const Path shaderIncludeProbePath = root / "shader_include_probe.slang";
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, WriteTextFile(
        shaderIncludeProbePath,
        "#include \"project/material_interfaces/test_surface.bind\"\n"
    ));
    NWB::Impl::ShaderCook shaderCook(testArena.arena);
    NWB::Impl::ShaderCook::CookVector<Path> includeDirectories(testArena.arena);
    includeDirectories.push_back(root / "cache" / "tests" / "material_bind_includes");
    NWB::Impl::ShaderCook::CookVector<Path> dependencies(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena(s_MaterialCookScratchArena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, shaderCook.gatherShaderDependencies(
        shaderIncludeProbePath,
        includeDirectories,
        dependencies,
        scratchArena
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsCanonicalPath(dependencies, generatedIncludePath));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);

    ErrorCode errorCode;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, RemoveAllIfExists(root, errorCode));

    Path shaderProbeRoot(testArena.arena);
    Path shaderProbeOutputDirectory(testArena.arena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, CookMaterialBindShaderProbe(
        s_MinimalMaterialBindSource,
        "material_bind_shader_probe",
        testArena,
        shaderProbeRoot,
        shaderProbeOutputDirectory
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);

    errorCode.clear();
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, RemoveAllIfExists(shaderProbeRoot, errorCode));

#if defined(NWB_FINAL)
    Path duplicateIncludeRoot(testArena.arena);
    Path duplicateIncludeOutputDirectory(testArena.arena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !CookDuplicateGeneratedMaterialBindIncludePath(
        "material_bind_duplicate_include_path",
        testArena,
        duplicateIncludeRoot,
        duplicateIncludeOutputDirectory
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT(
        "duplicate material bind include path 'project/material_interfaces/test_surface.bind'"
    )));

    errorCode.clear();
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, RemoveAllIfExists(duplicateIncludeRoot, errorCode));

    Path invalidRoot(testArena.arena);
    Path invalidOutputDirectory(testArena.arena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !CookMinimalMeshWithMaterialBind(
        s_DuplicateFieldMaterialBindSource,
        "material_bind_duplicate_field",
        testArena,
        invalidRoot,
        invalidOutputDirectory
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("duplicate struct field declaration")));

    errorCode.clear();
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, RemoveAllIfExists(invalidRoot, errorCode));
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

