// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void TestShaderArchiveVariantLookupIsExact(TestContext& context){
    TestArena testArena;
    NWB::Core::GraphicsVector<NWB::Core::ShaderArchive::Record> records(testArena.arena);
    NWB::Core::ShaderArchive::Record defaultRecord(testArena.arena);
    defaultRecord.shaderName = Name("project/shaders/test_shader");
    defaultRecord.variantName.assign(NWB::Core::ShaderArchive::s_DefaultVariant);
    defaultRecord.stage = Name("ps");
    defaultRecord.virtualPathHash = Name("shader/test_shader/default/ps").hash();
    records.push_back(Move(defaultRecord));

    Name virtualPath = NAME_NONE;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Core::ShaderArchive::findVirtualPath(
        records,
        Name("project/shaders/test_shader"),
        NWB::Core::ShaderArchive::s_DefaultVariant,
        Name("ps"),
        virtualPath
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, virtualPath == Name(records[0].virtualPathHash));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Core::ShaderArchive::buildVirtualPathName(
        Name("project/shaders/test_shader"),
        "",
        Name("ps")
    ) == NAME_NONE);

    virtualPath = NAME_NONE;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !NWB::Core::ShaderArchive::findVirtualPath(
        records,
        Name("project/shaders/test_shader"),
        "NWB_FEATURE=1",
        Name("ps"),
        virtualPath
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, virtualPath == NAME_NONE);

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !NWB::Core::ShaderArchive::findVirtualPath(
        records,
        Name("project/shaders/test_shader"),
        "",
        Name("ps"),
        virtualPath
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, virtualPath == NAME_NONE);
}

static constexpr u32 PackSpirvStringWord(const char a, const char b, const char c, const char d){
    return
        static_cast<u32>(static_cast<u8>(a))
        | (static_cast<u32>(static_cast<u8>(b)) << 8u)
        | (static_cast<u32>(static_cast<u8>(c)) << 16u)
        | (static_cast<u32>(static_cast<u8>(d)) << 24u)
    ;
}

static void TestSpirvEntryPointLookup(TestContext& context){
    TestArena testArena;
    NWB::Core::GraphicsString entryPoint(testArena.arena);

    const u32 words[] = {
        0x07230203u,
        0x00010500u,
        0u,
        16u,
        0u,
        (5u << 16u) | 15u,
        4u,
        1u,
        PackSpirvStringWord('m', 'a', 'i', 'n'),
        0u,
    };

    NWB_ASSETS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Core::ResolveSpirvEntryPointName(
            words,
            LengthOf(words),
            "main",
            NWB::Core::ShaderType::Pixel,
            entryPoint
        ) == NWB::Core::SpirvEntryPointLookupResult::Found
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, entryPoint == "main");

    NWB_ASSETS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Core::ResolveSpirvEntryPointName(
            words,
            LengthOf(words),
            "main",
            NWB::Core::ShaderType::Compute,
            entryPoint
        ) == NWB::Core::SpirvEntryPointLookupResult::NotFound
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, entryPoint.empty());

    const u32 invalidWords[] = {
        0x07230203u,
        0x00010500u,
        0u,
        16u,
        0u,
        (5u << 16u) | 15u,
        4u,
        1u,
        PackSpirvStringWord('m', 'a', 'i', 'n'),
    };

    NWB_ASSETS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Core::ResolveSpirvEntryPointName(
            invalidWords,
            LengthOf(invalidWords),
            "main",
            NWB::Core::ShaderType::Pixel,
            entryPoint
        ) == NWB::Core::SpirvEntryPointLookupResult::InvalidSpirv
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, entryPoint.empty());
}

static void TestShaderMetadataRejectsDefaultVariantAlias(TestContext& context){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, PrepareAssetsGraphicsCaseRoot("shader_default_variant_alias", root));

    const Path assetRoot = root / "assets";
    const Path includeMetaPath = assetRoot / "shaders" / "default_variant_include.nwb";
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, WriteTextFile(
        includeMetaPath,
        "include asset;\n\n"
        "asset.default_variant = \"NWB_FEATURE=0\";\n"
        "asset.defines = {\n"
        "    \"NWB_FEATURE\": [\"0\", \"1\"],\n"
        "};\n"
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, WriteTextFile(assetRoot / "shaders" / "default_variant_include.slangi", ""));

    NWB::Impl::ShaderCook shaderCook(testArena.arena);
    NWB::Impl::ShaderCook::IncludeEntry includeEntry(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !shaderCook.parseIncludeMeta(includeMetaPath, includeEntry, scratchArena));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT(
        "unsupported asset field 'default_variant'"
    )));

    ErrorCode errorCode;
    static_cast<void>(RemoveAllIfExists(root, errorCode));
#else
    static_cast<void>(context);
#endif
}

static void TestShaderDependencyChecksumAliasesGeneratedRoot(TestContext& context){
    TestArena testArena;
    Path root;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, PrepareAssetsGraphicsCaseRoot("shader_dependency_checksum_alias", root));

    const Path relativeIncludePath = Path("project") / "material_interfaces" / "test_surface.bind";
    const Path firstGeneratedRoot = root / "first" / "material_bind_includes";
    const Path secondGeneratedRoot = root / "second" / "material_bind_includes";
    const Path firstGeneratedInclude = firstGeneratedRoot / relativeIncludePath;
    const Path secondGeneratedInclude = secondGeneratedRoot / relativeIncludePath;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, WriteTextFile(firstGeneratedInclude, "generated include\n"));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, WriteTextFile(secondGeneratedInclude, "generated include\n"));

    NWB::Impl::ShaderCook shaderCook(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena;

    NWB::Impl::ShaderCook::CookVector<Path> firstDependencies(testArena.arena);
    NWB::Impl::ShaderCook::CookVector<Path> secondDependencies(testArena.arena);
    firstDependencies.push_back(firstGeneratedInclude);
    secondDependencies.push_back(secondGeneratedInclude);

    u64 firstChecksum = 0u;
    u64 secondChecksum = 0u;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, shaderCook.computeDependencyChecksum(
        firstDependencies,
        {
            { firstGeneratedRoot, "material_bind_includes" }
        },
        firstChecksum,
        scratchArena
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, shaderCook.computeDependencyChecksum(
        secondDependencies,
        {
            { secondGeneratedRoot, "material_bind_includes" }
        },
        secondChecksum,
        scratchArena
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, firstChecksum == secondChecksum);

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, WriteTextFile(secondGeneratedInclude, "changed generated include\n"));
    u64 changedChecksum = 0u;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, shaderCook.computeDependencyChecksum(
        secondDependencies,
        {
            { secondGeneratedRoot, "material_bind_includes" }
        },
        changedChecksum,
        scratchArena
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, firstChecksum != changedChecksum);

#if defined(NWB_FINAL)
    const Path unaliasedDependency = root / "outside" / "unaliased.slangi";
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, WriteTextFile(unaliasedDependency, "outside alias root\n"));
    NWB::Impl::ShaderCook::CookVector<Path> unaliasedDependencies(testArena.arena);
    unaliasedDependencies.push_back(unaliasedDependency);

    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);
    u64 rejectedChecksum = 0u;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !shaderCook.computeDependencyChecksum(
        unaliasedDependencies,
        {
            { firstGeneratedRoot, "material_bind_includes" }
        },
        rejectedChecksum,
        scratchArena
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT(
        "outside the declared dependency root aliases"
    )));
#endif

    ErrorCode errorCode;
    static_cast<void>(RemoveAllIfExists(root, errorCode));
}

static void TestShaderCookWithoutMaterialBindIncludes(TestContext& context){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root;
    Path outputDirectory;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, PrepareAssetsGraphicsCookCase(
        "shader_cook_without_material_bind_includes",
        root,
        outputDirectory
    ));

    const Path assetRoot = root / "assets";
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, WriteTextFile(
        assetRoot / "shaders" / "standalone_ps.nwb",
        "shader asset;\n\n"
        "asset.stage = \"ps\";\n"
        "asset.target_profile = \"spirv_1_5\";\n"
        "asset.entry_point = \"main\";\n"
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, WriteTextFile(
        assetRoot / "shaders" / "standalone_ps.slang",
        s_MaterialBindPixelShaderProbeSource
    ));

    const bool cooked = CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot });
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, cooked);
    if(cooked){
        NWB::Core::GraphicsVector<NWB::Core::ShaderArchive::Record> records(testArena.arena);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, LoadCookedShaderArchiveRecords(
            context,
            testArena,
            outputDirectory,
            records
        ));

        u64 sourceChecksum = 0u;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, FindShaderArchiveSourceChecksum(
            records,
            Name("project/shaders/standalone_ps"),
            Name("ps"),
            sourceChecksum
        ));
    }

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);

    ErrorCode errorCode;
    static_cast<void>(RemoveAllIfExists(root, errorCode));
}

using CookSingleMetaFn = bool(*)(AStringView, AStringView, TestArena&, Path&, Path&);
using LoadCookedAssetFn = bool(*)(TestContext&, TestArena&, const Path&, UniquePtr<NWB::Core::Assets::IAsset>&);

namespace MinimalAssetKind{
    enum Enum : u8{
        Mesh = 0u,
    };
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

