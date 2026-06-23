// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AssetsGraphics, ShaderArchiveVariantLookupIsExact){
    TestArena testArena;
    NWB::Core::GraphicsVector<NWB::Core::ShaderArchive::Record> records(testArena.arena);
    NWB::Core::ShaderArchive::Record defaultRecord(testArena.arena);
    defaultRecord.shaderName = Name("project/shaders/test_shader");
    defaultRecord.variantName.assign(NWB::Core::ShaderArchive::s_DefaultVariant);
    defaultRecord.stage = Name("ps");
    defaultRecord.virtualPathHash = Name("shader/test_shader/default/ps").hash();
    records.push_back(Move(defaultRecord));

    Name virtualPath = NAME_NONE;
    EXPECT_TRUE(NWB::Core::ShaderArchive::findVirtualPath(
        records,
        Name("project/shaders/test_shader"),
        NWB::Core::ShaderArchive::s_DefaultVariant,
        Name("ps"),
        virtualPath
    ));
    EXPECT_EQ(virtualPath, Name(records[0].virtualPathHash));
    EXPECT_EQ(NWB::Core::ShaderArchive::buildVirtualPathName(
        Name("project/shaders/test_shader"),
        "",
        Name("ps")
    ), NAME_NONE);

    virtualPath = NAME_NONE;
    EXPECT_FALSE(NWB::Core::ShaderArchive::findVirtualPath(
        records,
        Name("project/shaders/test_shader"),
        "NWB_FEATURE=1",
        Name("ps"),
        virtualPath
    ));
    EXPECT_EQ(virtualPath, NAME_NONE);

    EXPECT_FALSE(NWB::Core::ShaderArchive::findVirtualPath(
        records,
        Name("project/shaders/test_shader"),
        "",
        Name("ps"),
        virtualPath
    ));
    EXPECT_EQ(virtualPath, NAME_NONE);
}

static constexpr u32 PackSpirvStringWord(const char a, const char b, const char c, const char d){
    return
        static_cast<u32>(static_cast<u8>(a))
        | (static_cast<u32>(static_cast<u8>(b)) << 8u)
        | (static_cast<u32>(static_cast<u8>(c)) << 16u)
        | (static_cast<u32>(static_cast<u8>(d)) << 24u)
    ;
}

TEST(AssetsGraphics, SpirvEntryPointLookup){
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

    EXPECT_EQ(NWB::Core::ResolveSpirvEntryPointName(
            words,
            LengthOf(words),
            "main",
            NWB::Core::ShaderType::Pixel,
            entryPoint
        ), NWB::Core::SpirvEntryPointLookupResult::Found);
    EXPECT_EQ(entryPoint, "main");

    EXPECT_EQ(NWB::Core::ResolveSpirvEntryPointName(
            words,
            LengthOf(words),
            "main",
            NWB::Core::ShaderType::Compute,
            entryPoint
        ), NWB::Core::SpirvEntryPointLookupResult::NotFound);
    EXPECT_TRUE(entryPoint.empty());

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

    EXPECT_EQ(NWB::Core::ResolveSpirvEntryPointName(
            invalidWords,
            LengthOf(invalidWords),
            "main",
            NWB::Core::ShaderType::Pixel,
            entryPoint
        ), NWB::Core::SpirvEntryPointLookupResult::InvalidSpirv);
    EXPECT_TRUE(entryPoint.empty());
}

TEST(AssetsGraphics, ShaderMetadataRejectsDefaultVariantAlias){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    EXPECT_TRUE(PrepareAssetsGraphicsCaseRoot(testArena, "shader_default_variant_alias", root));

    const Path assetRoot = root / "assets";
    const Path includeMetaPath = assetRoot / "shaders" / "default_variant_include.nwb";
    EXPECT_TRUE(WriteTextFile(
        includeMetaPath,
        "include asset;\n\n"
        "asset.default_variant = \"NWB_FEATURE=0\";\n"
        "asset.defines = {\n"
        "    \"NWB_FEATURE\": [\"0\", \"1\"],\n"
        "};\n"
    ));
    EXPECT_TRUE(WriteTextFile(assetRoot / "shaders" / "default_variant_include.slangi", ""));

    NWB::Impl::ShaderCook shaderCook(testArena.arena);
    NWB::Impl::ShaderCook::IncludeEntry includeEntry(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena(s_ShaderScratchArena);
    EXPECT_FALSE(shaderCook.parseIncludeMeta(includeMetaPath, includeEntry, scratchArena));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT(
        "unsupported asset field 'default_variant'"
    )));

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
#else
#endif
}

TEST(AssetsGraphics, ShaderDependencyChecksumAliasesGeneratedRoot){
    TestArena testArena;
    Path root(testArena.arena);
    EXPECT_TRUE(PrepareAssetsGraphicsCaseRoot(testArena, "shader_dependency_checksum_alias", root));

    const Path relativeIncludePath = Path(testArena.arena, "project") / "material_interfaces" / "test_surface.bind";
    const Path firstGeneratedRoot = root / "first" / "material_bind_includes";
    const Path secondGeneratedRoot = root / "second" / "material_bind_includes";
    const Path firstGeneratedInclude = firstGeneratedRoot / relativeIncludePath;
    const Path secondGeneratedInclude = secondGeneratedRoot / relativeIncludePath;
    EXPECT_TRUE(WriteTextFile(firstGeneratedInclude, "generated include\n"));
    EXPECT_TRUE(WriteTextFile(secondGeneratedInclude, "generated include\n"));

    NWB::Impl::ShaderCook shaderCook(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena(s_ShaderScratchArena);

    NWB::Impl::ShaderCook::CookVector<Path> firstDependencies(testArena.arena);
    NWB::Impl::ShaderCook::CookVector<Path> secondDependencies(testArena.arena);
    firstDependencies.push_back(firstGeneratedInclude);
    secondDependencies.push_back(secondGeneratedInclude);

    u64 firstChecksum = 0u;
    u64 secondChecksum = 0u;
    EXPECT_TRUE(shaderCook.computeDependencyChecksum(
        firstDependencies,
        {
            { firstGeneratedRoot, "material_bind_includes" }
        },
        firstChecksum,
        scratchArena
    ));
    EXPECT_TRUE(shaderCook.computeDependencyChecksum(
        secondDependencies,
        {
            { secondGeneratedRoot, "material_bind_includes" }
        },
        secondChecksum,
        scratchArena
    ));
    EXPECT_EQ(firstChecksum, secondChecksum);

    EXPECT_TRUE(WriteTextFile(secondGeneratedInclude, "changed generated include\n"));
    u64 changedChecksum = 0u;
    EXPECT_TRUE(shaderCook.computeDependencyChecksum(
        secondDependencies,
        {
            { secondGeneratedRoot, "material_bind_includes" }
        },
        changedChecksum,
        scratchArena
    ));
    EXPECT_NE(firstChecksum, changedChecksum);

#if defined(NWB_FINAL)
    const Path unaliasedDependency = root / "outside" / "unaliased.slangi";
    EXPECT_TRUE(WriteTextFile(unaliasedDependency, "outside alias root\n"));
    NWB::Impl::ShaderCook::CookVector<Path> unaliasedDependencies(testArena.arena);
    unaliasedDependencies.push_back(unaliasedDependency);

    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);
    u64 rejectedChecksum = 0u;
    EXPECT_FALSE(shaderCook.computeDependencyChecksum(
        unaliasedDependencies,
        {
            { firstGeneratedRoot, "material_bind_includes" }
        },
        rejectedChecksum,
        scratchArena
    ));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT(
        "outside the declared dependency root aliases"
    )));
#endif

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
}

// A self-contained pixel shader that includes no material bind interface (this test exercises cooking a shader
// without any generated material_bind_includes root).
static constexpr AStringView s_StandaloneShaderProbeSource = R"NWB_SLANG(struct NwbStandalonePixelOutput{
    float4 color : SV_Target0;
};

NwbStandalonePixelOutput main(){
    NwbStandalonePixelOutput output;
    output.color = float4(1.0, 1.0, 1.0, 1.0);
    return output;
}

)NWB_SLANG";

static bool WriteStandaloneShaderProbe(const Path& assetRoot){
    if(!WriteTextFile(
        assetRoot / "shaders" / "standalone_ps.nwb",
        "shader asset;\n\n"
        "asset.stage = \"ps\";\n"
        "asset.target_profile = \"spirv_1_5\";\n"
        "asset.entry_point = \"main\";\n"
    ))
        return false;

    return WriteTextFile(assetRoot / "shaders" / "standalone_ps.slang", s_StandaloneShaderProbeSource);
}

TEST(AssetsGraphics, ShaderCookWithoutMaterialBindIncludes){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
    EXPECT_TRUE(PrepareAssetsGraphicsCookCase(
        testArena,
        "shader_cook_without_material_bind_includes",
        root,
        outputDirectory
    ));

    const Path assetRoot = root / "assets";
    EXPECT_TRUE(WriteStandaloneShaderProbe(assetRoot));

    const bool cooked = CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot });
    EXPECT_TRUE(cooked);
    if(cooked){
        NWB::Core::GraphicsVector<NWB::Core::ShaderArchive::Record> records(testArena.arena);
        EXPECT_TRUE(LoadCookedShaderArchiveRecords(
            testArena,
            outputDirectory,
            records
        ));

        u64 sourceChecksum = 0u;
        EXPECT_TRUE(FindShaderArchiveSourceChecksum(
            records,
            Name("project/shaders/standalone_ps"),
            Name("ps"),
            sourceChecksum
        ));
    }

    EXPECT_EQ(logger.errorCount(), 0u);

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
}

static bool FindSingleShaderBytecodeCachePath(TestArena& testArena, const Path& cacheDirectory, Path& outPath){
    outPath.clear();

    ErrorCode errorCode;
    RecursiveDirectoryIterator<Path::Arena> cacheEntries(cacheDirectory, errorCode);
    EXPECT_FALSE(errorCode);
    if(errorCode)
        return false;

    usize foundCount = 0u;
    for(const auto& entry : cacheEntries){
        errorCode.clear();
        const bool isRegularFile = entry.is_regular_file(errorCode);
        EXPECT_FALSE(errorCode);
        if(errorCode)
            return false;
        if(!isRegularFile)
            continue;

        const auto extension = PathToString(testArena.arena, entry.path().extension());
        if(extension != ".spv")
            continue;

        outPath = entry.path();
        ++foundCount;
    }

    EXPECT_EQ(foundCount, 1u);
    return foundCount == 1u;
}

TEST(AssetsGraphics, ShaderCookIgnoresInvalidBytecodeCache){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
    EXPECT_TRUE(PrepareAssetsGraphicsCookCase(
        testArena,
        "shader_cook_invalid_bytecode_cache",
        root,
        outputDirectory
    ));

    const Path assetRoot = root / "assets";
    EXPECT_TRUE(WriteStandaloneShaderProbe(assetRoot));
    EXPECT_TRUE(CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }));

    Path bytecodeCachePath(testArena.arena);
    if(FindSingleShaderBytecodeCachePath(testArena, root / "cache", bytecodeCachePath)){
        EXPECT_TRUE(WriteTextFile(bytecodeCachePath, "BAD!"));
        EXPECT_TRUE(CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }));

        const Name shaderVirtualPath = NWB::Core::ShaderArchive::buildVirtualPathName(
            Name("project/shaders/standalone_ps"),
            NWB::Core::ShaderArchive::s_DefaultVariant,
            Name("ps")
        );
        UniquePtr<NWB::Core::Assets::IAsset> loadedShader;
        EXPECT_TRUE(LoadCookedAsset<NWB::Impl::ShaderAssetCodec>(
            testArena,
            outputDirectory,
            shaderVirtualPath,
            loadedShader
        ));
    }

    EXPECT_EQ(logger.errorCount(), 0u);

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
}

using CookSingleMetaFn = bool(*)(AStringView, AStringView, TestArena&, Path&, Path&);
using LoadCookedAssetFn = bool(*)(TestArena&, const Path&, UniquePtr<NWB::Core::Assets::IAsset>&);

namespace MinimalAssetKind{
    enum Enum : u8{
        Mesh = 0u,
    };
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

