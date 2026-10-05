// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_shader{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_PS = "ps";
static constexpr AStringView s_PROJECT_SHADERS_TEST_SHADER = "project/shaders/test_shader";
static constexpr AStringView s_PROJECT_SHADERS_STANDALONE_PS = "project/shaders/standalone_ps";
static constexpr AStringView s_MAINCASE = "MainCase";
static constexpr AStringView s_SHADER_ASSET_HEAD = "shader asset;\n\n";
static constexpr AStringView s_ASSET_STAGE_PS = "asset.stage = \"ps\";\n";
static constexpr AStringView s_ASSET_ENTRY_MAIN = "asset.entry_point = \"main\";\n";
static constexpr AStringView s_MAIN = "main";
static constexpr AStringView s_MATERIAL_BIND_INCLUDES = "material_bind_includes";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CapturingLogger = AssetsGraphicsFixture::CapturingLogger;
using Path = AssetsGraphicsFixture::Path;
using TestArena = AssetsGraphicsFixture::TestArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AssetsGraphics, ShaderArchiveVariantLookupIsExact){
    TestArena testArena;
    NWB::Core::GraphicsVector<NWB::Core::ShaderArchive::Record> records(testArena.arena);
    NWB::Core::ShaderArchive::Record defaultRecord(testArena.arena);
    defaultRecord.shaderName = Name(s_PROJECT_SHADERS_TEST_SHADER);
    defaultRecord.variantName.assign(NWB::Core::ShaderArchive::s_DefaultVariant);
    defaultRecord.stage = Name(s_PS);
    defaultRecord.virtualPathHash = Name("shader/test_shader/default/ps").hash();
    records.push_back(Move(defaultRecord));

    Name virtualPath = NAME_NONE;
    EXPECT_TRUE(NWB::Core::ShaderArchive::findVirtualPath(
        records,
        Name(s_PROJECT_SHADERS_TEST_SHADER),
        NWB::Core::ShaderArchive::s_DefaultVariant,
        Name(s_PS),
        virtualPath
    ));
    EXPECT_EQ(virtualPath, Name(records[0].virtualPathHash));
    EXPECT_EQ(NWB::Core::ShaderArchive::buildVirtualPathName(
        Name(s_PROJECT_SHADERS_TEST_SHADER),
        "",
        Name(s_PS)
    ), NAME_NONE);

    virtualPath = NAME_NONE;
    EXPECT_FALSE(NWB::Core::ShaderArchive::findVirtualPath(
        records,
        Name(s_PROJECT_SHADERS_TEST_SHADER),
        "NWB_FEATURE=1",
        Name(s_PS),
        virtualPath
    ));
    EXPECT_EQ(virtualPath, NAME_NONE);

    EXPECT_FALSE(NWB::Core::ShaderArchive::findVirtualPath(
        records,
        Name(s_PROJECT_SHADERS_TEST_SHADER),
        "",
        Name(s_PS),
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
    AStringView entryPoint;

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

    EXPECT_TRUE(NWB::Core::IsValidSpirvModuleWords(words, LengthOf(words)));
    EXPECT_EQ(NWB::Core::ResolveSpirvEntryPointName(
            words,
            LengthOf(words),
            s_MAIN,
            NWB::Core::ShaderType::Pixel,
            entryPoint
        ), NWB::Core::SpirvEntryPointLookupResult::Found);
    EXPECT_EQ(entryPoint, s_MAIN);

    EXPECT_EQ(NWB::Core::ResolveSpirvEntryPointName(
            words,
            LengthOf(words),
            s_MAIN,
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
            s_MAIN,
            NWB::Core::ShaderType::Pixel,
            entryPoint
        ), NWB::Core::SpirvEntryPointLookupResult::InvalidSpirv);
    EXPECT_TRUE(entryPoint.empty());
    EXPECT_FALSE(NWB::Core::IsValidSpirvModuleWords(invalidWords, LengthOf(invalidWords)));

    const u32 malformedOtherStageEntryWords[] = {
        0x07230203u,
        0x00010500u,
        0u,
        16u,
        0u,
        (4u << 16u) | 15u,
        0u,
        1u,
        PackSpirvStringWord('m', 'a', 'i', 'n'),
    };

    EXPECT_FALSE(NWB::Core::IsValidSpirvModuleWords(malformedOtherStageEntryWords, LengthOf(malformedOtherStageEntryWords)));
    EXPECT_EQ(NWB::Core::ResolveSpirvEntryPointName(
            malformedOtherStageEntryWords,
            LengthOf(malformedOtherStageEntryWords),
            s_MAIN,
            NWB::Core::ShaderType::Pixel,
            entryPoint
        ), NWB::Core::SpirvEntryPointLookupResult::InvalidSpirv);
    EXPECT_TRUE(entryPoint.empty());
}

TEST(AssetsGraphics, SpirvEntryPointViewsRebindCopiedWordsAndClearMalformedMatches){
    TestArena testArena;
    constexpr u32 s_Words[] = {
        0x07230203u, 0x00010500u, 0u, 16u, 0u,
        (5u << 16u) | 15u, 4u, 1u, PackSpirvStringWord('m', 'a', 'i', 'n'), 0u,
    };
    constexpr char s_RequestedName[] = { 'm', 'a', 'i', 'n', 'X' };
    const AStringView requestedName(s_RequestedName, 4u);
    NWB::Core::GraphicsVector<u32> copiedWords(testArena.arena);
    AStringView copiedEntryPoint;
    {
        NWB::Core::GraphicsVector<u32> sourceWords(s_Words, s_Words + LengthOf(s_Words), testArena.arena);
        AStringView sourceEntryPoint;
        ASSERT_EQ(NWB::Core::ResolveSpirvEntryPointName(
            sourceWords.data(), sourceWords.size(), requestedName, NWB::Core::ShaderType::Pixel, sourceEntryPoint
        ), NWB::Core::SpirvEntryPointLookupResult::Found);
        EXPECT_EQ(sourceEntryPoint.data(), reinterpret_cast<const char*>(sourceWords.data() + 8u));
        copiedWords = sourceWords;
        ASSERT_EQ(NWB::Core::ResolveSpirvEntryPointName(
            copiedWords.data(), copiedWords.size(), requestedName, NWB::Core::ShaderType::Pixel, copiedEntryPoint
        ), NWB::Core::SpirvEntryPointLookupResult::Found);
        EXPECT_EQ(copiedEntryPoint.data(), reinterpret_cast<const char*>(copiedWords.data() + 8u));
        EXPECT_NE(copiedEntryPoint.data(), sourceEntryPoint.data());
    }
    // Specializations retain their own bytecode, so their entry-name view must survive the original module.
    EXPECT_EQ(copiedEntryPoint, s_MAIN);
    EXPECT_EQ(copiedEntryPoint.data()[copiedEntryPoint.size()], '\0');

    copiedWords.push_back(0u);
    EXPECT_EQ(NWB::Core::ResolveSpirvEntryPointName(
        copiedWords.data(), copiedWords.size(), requestedName, NWB::Core::ShaderType::Pixel, copiedEntryPoint
    ), NWB::Core::SpirvEntryPointLookupResult::InvalidSpirv);
    EXPECT_TRUE(copiedEntryPoint.empty());
    EXPECT_EQ(copiedEntryPoint.data(), nullptr);
}

TEST(AssetsGraphics, ShaderMetadataRejectsDefaultVariantAlias){
#if defined(GLB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    EXPECT_TRUE(AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "shader_default_variant_alias", root));

    const Path assetRoot = root / "assets";
    const Path includeMetaPath = assetRoot / "shaders" / "default_variant_include.nwb";
    EXPECT_TRUE(AssetsGraphicsFixture::WriteTextFile(
        includeMetaPath,
        "include asset;\n\n"
        "asset.default_variant = \"NWB_FEATURE=0\";\n"
        "asset.defines = {\n"
        "    \"NWB_FEATURE\": [\"0\", \"1\"],\n"
        "};\n"
    ));
    EXPECT_TRUE(AssetsGraphicsFixture::WriteTextFile(assetRoot / "shaders" / "default_variant_include.slangi", ""));

    NWB::Impl::ShaderCook shaderCook(testArena.arena);
    NWB::Impl::ShaderCook::IncludeEntry includeEntry(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_ShaderScratchArena);
    EXPECT_FALSE(shaderCook.parseIncludeMeta(includeMetaPath, includeEntry, scratchArena));
    EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT(
        "unsupported asset field 'default_variant'"
    )));

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
#else
#endif
}


TEST(AssetsGraphics, ShaderMetadataRejectsObsoleteProfilesAndInvalidConditionalFlags){
    CapturingLogger logger;
    const Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_ShaderScratchArena);
    Path root(testArena.arena);
    ASSERT_TRUE(AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "shader_ray_query_metadata", root));
    const Path metadataPath = root / "shader.nwb";
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(root / "shader.slang", "void main(){}\n"));
    struct MetadataCase{
        AStringView fields;
        bool accepted;
    };
    constexpr MetadataCase cases[] = {
        { "", true },
        { "asset.ray_query = 0;\n", true },
        { "asset.ray_query = 1;\n", true },
        { "asset.ray_query = -1;\n", false },
        { "asset.ray_query = 2;\n", false },
        { "asset.ray_query = 1.0;\n", false },
        { "asset.ray_query = \"1\";\n", false },
        { "asset.ray_query = [];\n", false },
        { "asset.ray_query = {};\n", false },
        { "asset.target_profile = \"spirv_1_5\";\n", false },
        { "asset.target_profile = \"spirv_1_5+spvRayQueryKHR\";\n", false },
        { "asset.emit_mesh_compute_shadow = 0;\n", false },
        { "asset.emit_mesh_compute_shadow = 1;\n", false },
    };
    Impl::ShaderCook shaderCook(testArena.arena);
    Impl::ShaderCook::ShaderEntry entry(testArena.arena);
    for(const MetadataCase& testCase : cases){
        SCOPED_TRACE(testCase.fields);
        Impl::ShaderCook::CookString metadata("shader asset;\nasset.stage = \"cs\";\nasset.entry_point = \"main\";\n", testArena.arena);
        metadata.append(testCase.fields);
        ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(metadataPath, metadata));
        EXPECT_EQ(shaderCook.parseShaderMeta(metadataPath, entry, scratchArena), testCase.accepted);
    }
    constexpr AStringView stages[] = { "vs", "ps", "rgen" };
    for(const AStringView stage : stages){
        const auto metadata = StringFormat(testArena.arena,
            "shader asset;\nasset.stage = \"{}\";\nasset.entry_point = \"main\";\nasset.emit_mesh_compute_shadow = 1;\n", stage);
        ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(metadataPath, metadata));
        EXPECT_FALSE(shaderCook.parseShaderMeta(metadataPath, entry, scratchArena));
    }
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(metadataPath,
        "shader asset;\nasset.stage = \"mesh\";\nasset.entry_point = \"main\";\nasset.emit_mesh_compute_shadow = 0;\n"));
    ASSERT_TRUE(shaderCook.parseShaderMeta(metadataPath, entry, scratchArena));
    ErrorCode error;
    EXPECT_TRUE(RemoveAllIfExists(root, error));
}

TEST(AssetsGraphics, ShaderMetadataRejectsEngineTransportDefinesAndRecoversWithRealVariants){
    CapturingLogger logger;
    const Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_ShaderScratchArena);
    Path root(testArena.arena);
    ASSERT_TRUE(AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "shader_transport_defines", root));
    const Path shaderPath = root / "shader.nwb";
    const Path includePath = root / "include.nwb";
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(root / "shader.slang", "void main(){}\n"));
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(root / "include.slangi", "static const uint sourceValue = 1u;\n"));
    static constexpr AStringView s_ShaderDeclaration = "shader asset;\nasset.stage = \"cs\";\nasset.entry_point = \"main\";\n";
    static constexpr AStringView s_RealDefines = "asset.defines = { \"PROJECT_QUALITY\": [\"0\", \"1\"] };\n";
    const auto validShaderMetadata = StringFormat(testArena.arena, "{}{}", s_ShaderDeclaration, s_RealDefines);
    const auto validIncludeMetadata = StringFormat(testArena.arena, "include asset;\n{}", s_RealDefines);
    Impl::ShaderCook shaderCook(testArena.arena);
    Impl::ShaderCook::ShaderEntry shaderEntry(testArena.arena);
    Impl::ShaderCook::IncludeEntry includeEntry(testArena.arena);

    for(const AStringView retiredValue : { AStringView("0"), AStringView("1") }){
        SCOPED_TRACE(retiredValue);
        ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(shaderPath, validShaderMetadata));
        ASSERT_TRUE(shaderCook.parseShaderMeta(shaderPath, shaderEntry, scratchArena));
        ASSERT_FALSE(shaderEntry.defineValues.empty());
        const auto obsoleteShaderMetadata = StringFormat(
            testArena.arena,
            "{}asset.defines = {{ \"NWB_BINDLESS_TLAS\": [\"{}\"] }};\n",
            s_ShaderDeclaration,
            retiredValue
        );
        ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(shaderPath, obsoleteShaderMetadata));
        EXPECT_FALSE(shaderCook.parseShaderMeta(shaderPath, shaderEntry, scratchArena));
        EXPECT_TRUE(shaderEntry.defineValues.empty());
        EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT("define 'NWB_BINDLESS_TLAS' is an engine transport feature")));
        ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(shaderPath, validShaderMetadata));
        ASSERT_TRUE(shaderCook.parseShaderMeta(shaderPath, shaderEntry, scratchArena));
        EXPECT_TRUE(shaderCook.validateVariantSignature("project/shader", "PROJECT_QUALITY=0", shaderEntry.defineValues, scratchArena));
        EXPECT_TRUE(shaderCook.validateVariantSignature("project/shader", "PROJECT_QUALITY=1", shaderEntry.defineValues, scratchArena));
        EXPECT_FALSE(shaderCook.validateVariantSignature(
            "project/shader", "NWB_BINDLESS_TLAS=1;PROJECT_QUALITY=1", shaderEntry.defineValues, scratchArena
        ));

        ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(includePath, validIncludeMetadata));
        ASSERT_TRUE(shaderCook.parseIncludeMeta(includePath, includeEntry, scratchArena));
        ASSERT_FALSE(includeEntry.defineValues.empty());
        const auto obsoleteIncludeMetadata = StringFormat(
            testArena.arena,
            "include asset;\nasset.defines = {{ \"NWB_BINDLESS_TLAS\": [\"{}\"] }};\n",
            retiredValue
        );
        ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(includePath, obsoleteIncludeMetadata));
        EXPECT_FALSE(shaderCook.parseIncludeMeta(includePath, includeEntry, scratchArena));
        EXPECT_TRUE(includeEntry.defineValues.empty());
        ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(includePath, validIncludeMetadata));
        ASSERT_TRUE(shaderCook.parseIncludeMeta(includePath, includeEntry, scratchArena));
        EXPECT_TRUE(shaderCook.validateVariantSignature("project/include", "PROJECT_QUALITY=1", includeEntry.defineValues, scratchArena));
    }
    ErrorCode error;
    EXPECT_TRUE(RemoveAllIfExists(root, error));
}

TEST(AssetsGraphics, ShaderDependencyChecksumAliasesGeneratedRoot){
    TestArena testArena;
    Path root(testArena.arena);
    EXPECT_TRUE(AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "shader_dependency_checksum_alias", root));

    const Path relativeIncludePath = Path(testArena.arena, "project") / "material_interfaces" / "test_surface.bind";
    const Path firstGeneratedRoot = root / "first" / s_MATERIAL_BIND_INCLUDES;
    const Path secondGeneratedRoot = root / "second" / s_MATERIAL_BIND_INCLUDES;
    const Path firstGeneratedInclude = firstGeneratedRoot / relativeIncludePath;
    const Path secondGeneratedInclude = secondGeneratedRoot / relativeIncludePath;
    EXPECT_TRUE(AssetsGraphicsFixture::WriteTextFile(firstGeneratedInclude, "generated include\n"));
    EXPECT_TRUE(AssetsGraphicsFixture::WriteTextFile(secondGeneratedInclude, "generated include\n"));

    NWB::Impl::ShaderCook shaderCook(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_ShaderScratchArena);

    NWB::Impl::ShaderCook::CookVector<Path> firstDependencies(testArena.arena);
    NWB::Impl::ShaderCook::CookVector<Path> secondDependencies(testArena.arena);
    firstDependencies.push_back(firstGeneratedInclude);
    secondDependencies.push_back(secondGeneratedInclude);

    u64 firstChecksum = 0u;
    u64 secondChecksum = 0u;
    EXPECT_TRUE(shaderCook.computeDependencyChecksum(
        firstDependencies,
        {
            { firstGeneratedRoot, s_MATERIAL_BIND_INCLUDES }
        },
        firstChecksum,
        scratchArena
    ));
    EXPECT_TRUE(shaderCook.computeDependencyChecksum(
        secondDependencies,
        {
            { secondGeneratedRoot, s_MATERIAL_BIND_INCLUDES }
        },
        secondChecksum,
        scratchArena
    ));
    EXPECT_EQ(firstChecksum, secondChecksum);

    EXPECT_TRUE(AssetsGraphicsFixture::WriteTextFile(secondGeneratedInclude, "changed generated include\n"));
    u64 changedChecksum = 0u;
    EXPECT_TRUE(shaderCook.computeDependencyChecksum(
        secondDependencies,
        {
            { secondGeneratedRoot, s_MATERIAL_BIND_INCLUDES }
        },
        changedChecksum,
        scratchArena
    ));
    EXPECT_NE(firstChecksum, changedChecksum);

#if defined(GLB_FINAL)
    const Path unaliasedDependency = root / "outside" / "unaliased.slangi";
    EXPECT_TRUE(AssetsGraphicsFixture::WriteTextFile(unaliasedDependency, "outside alias root\n"));
    NWB::Impl::ShaderCook::CookVector<Path> unaliasedDependencies(testArena.arena);
    unaliasedDependencies.push_back(unaliasedDependency);

    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);
    u64 rejectedChecksum = 0u;
    EXPECT_FALSE(shaderCook.computeDependencyChecksum(
        unaliasedDependencies,
        {
            { firstGeneratedRoot, s_MATERIAL_BIND_INCLUDES }
        },
        rejectedChecksum,
        scratchArena
    ));
    EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT(
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
    const auto shaderMetadata = StringFormat(
        assetRoot.arena(),
        "{}{}{}",
        s_SHADER_ASSET_HEAD,
        s_ASSET_STAGE_PS,
        s_ASSET_ENTRY_MAIN
    );
    if(!AssetsGraphicsFixture::WriteTextFile(assetRoot / "shaders" / "standalone_ps.nwb", shaderMetadata))
        return false;

    return AssetsGraphicsFixture::WriteTextFile(assetRoot / "shaders" / "standalone_ps.slang", s_StandaloneShaderProbeSource);
}

// This reaches Slang after dependency collection.  Both files deliberately start with a UTF-8 BOM so the compiler
// input normalization path, not only the dependency scanner, must make the generated invocation valid.
static constexpr AStringView s_BomCompilerProbeIncludeSource = "\xEF\xBB\xBF" R"NWB_SLANG(float4 nwbBomCompilerProbeColor(){
    return float4(0.1, 0.2, 0.3, 1.0);
}

)NWB_SLANG";

static constexpr AStringView s_BomCompilerProbeSource = "\xEF\xBB\xBF" R"NWB_SLANG(#include "bom_compiler_probe_include.slangi"

struct NwbBomCompilerProbeOutput{
    float4 color : SV_Target0;
};

NwbBomCompilerProbeOutput main(){
    NwbBomCompilerProbeOutput output;
    output.color = nwbBomCompilerProbeColor();
    return output;
}

)NWB_SLANG";

static bool WriteBomCompilerProbe(const Path& assetRoot){
    const auto shaderMetadata = StringFormat(
        assetRoot.arena(),
        "{}{}{}",
        s_SHADER_ASSET_HEAD,
        s_ASSET_STAGE_PS,
        s_ASSET_ENTRY_MAIN
    );
    if(!AssetsGraphicsFixture::WriteTextFile(assetRoot / "shaders" / "bom_compiler_probe_ps.nwb", shaderMetadata))
        return false;
    if(!AssetsGraphicsFixture::WriteTextFile(assetRoot / "shaders" / "bom_compiler_probe_include.slangi", s_BomCompilerProbeIncludeSource))
        return false;

    return AssetsGraphicsFixture::WriteTextFile(assetRoot / "shaders" / "bom_compiler_probe_ps.slang", s_BomCompilerProbeSource);
}

static constexpr AStringView s_ExactEntryPointShaderProbeSource = R"NWB_SLANG(struct NwbExactEntryPointPixelOutput{
    float4 color : SV_Target0;
};

NwbExactEntryPointPixelOutput MainCase(){
    NwbExactEntryPointPixelOutput output;
    output.color = float4(1.0, 1.0, 1.0, 1.0);
    return output;
}

)NWB_SLANG";

static bool WriteExactEntryPointShaderProbe(const Path& assetRoot){
    const auto shaderMetadata = StringFormat(
        assetRoot.arena(),
        "{}{}{}",
        s_SHADER_ASSET_HEAD,
        s_ASSET_STAGE_PS,
        "asset.entry_point = \"MainCase\";\n"
    );
    if(!AssetsGraphicsFixture::WriteTextFile(assetRoot / "shaders" / "exact_entry_point_ps.nwb", shaderMetadata))
        return false;

    return AssetsGraphicsFixture::WriteTextFile(assetRoot / "shaders" / "exact_entry_point_ps.slang", s_ExactEntryPointShaderProbeSource);
}

TEST(AssetsGraphics, GatherIndependentShaderBuildsWithoutSources){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger, NWB::Core::Common::LoggerBreakPolicy::BreakOnFatal);

    TestArena testArena;
    Path root(testArena.arena);
    ASSERT_TRUE(AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "gather_independent_shaders", root));
    const Path firstAssetRoot = root / "first" / "assets";
    const Path secondAssetRoot = root / "second" / "assets";
    const Path firstBuilt = root / "first_built";
    const Path secondBuilt = root / "second_built";
    const Path conflictingBuilt = root / "conflicting_built";
    const Path outputDirectory = root / "gathered";
    ASSERT_TRUE(WriteStandaloneShaderProbe(firstAssetRoot));
    ASSERT_TRUE(WriteExactEntryPointShaderProbe(secondAssetRoot));
    ASSERT_TRUE(AssetsGraphicsFixture::BuildPreparedGraphicsAssetRoots(testArena, root, firstBuilt, { firstAssetRoot }));
    ASSERT_TRUE(AssetsGraphicsFixture::BuildPreparedGraphicsAssetRoots(testArena, root, secondBuilt, { secondAssetRoot }));

    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(
        firstAssetRoot / "shaders" / "standalone_ps.slang",
        "float4 main() : SV_Target0{ return float4(0.0, 1.0, 0.0, 1.0); }\n"
    ));
    ASSERT_TRUE(AssetsGraphicsFixture::BuildPreparedGraphicsAssetRoots(testArena, root, conflictingBuilt, { firstAssetRoot }));

    ErrorCode errorCode;
    ASSERT_TRUE(RemoveAllIfExists(firstAssetRoot, errorCode));
    ASSERT_TRUE(RemoveAllIfExists(secondAssetRoot, errorCode));

    NWB::Pipeline::AssetGatherer::AssetGatherOptions options(testArena.arena);
    options.inputs.emplace_back(PathToString(testArena.arena, firstBuilt));
    options.inputs.emplace_back(PathToString(testArena.arena, secondBuilt));
    options.outputDirectory = PathToString(testArena.arena, outputDirectory);
    options.configuration = "tests";
    options.mergePayloads = &NWB::Impl::MergeGatheredGraphicsAsset;
    ASSERT_TRUE(NWB::Pipeline::AssetGatherer::GatherAssets(options));

    NWB::Core::GraphicsVector<NWB::Core::ShaderArchive::Record> records(testArena.arena);
    ASSERT_TRUE(AssetsGraphicsFixture::LoadCookedShaderArchiveRecords(testArena, outputDirectory, records));
    ASSERT_EQ(records.size(), s_ExpectedDualCount);
    const Name shaderNames[] = { Name(s_PROJECT_SHADERS_STANDALONE_PS), Name("project/shaders/exact_entry_point_ps") };
    for(const Name& shaderName : shaderNames){
        Name virtualPath;
        ASSERT_TRUE(NWB::Core::ShaderArchive::findVirtualPath(
            records, shaderName, NWB::Core::ShaderArchive::s_DefaultVariant, Name(s_PS), virtualPath
        ));
        UniquePtr<NWB::Core::Assets::IAsset> shader;
        ASSERT_TRUE(AssetsGraphicsFixture::LoadCookedAsset<NWB::Impl::ShaderAssetCodec>(testArena, outputDirectory, virtualPath, shader, 3u));
        EXPECT_FALSE(static_cast<const NWB::Impl::Shader&>(*shader).bytecode().empty());
    }
    EXPECT_EQ(logger.errorCount(), 0u);

    u64 originalChecksum = 0u;
    ASSERT_TRUE(AssetsGraphicsFixture::FindShaderArchiveSourceChecksum(records, shaderNames[0], Name(s_PS), originalChecksum));
    options.inputs.emplace_back(PathToString(testArena.arena, conflictingBuilt));
    EXPECT_FALSE(NWB::Pipeline::AssetGatherer::GatherAssets(options));
    EXPECT_GT(logger.errorCount(), 0u);
    records.clear();
    ASSERT_TRUE(AssetsGraphicsFixture::LoadCookedShaderArchiveRecords(testArena, outputDirectory, records));
    ASSERT_EQ(records.size(), s_ExpectedDualCount);
    u64 preservedChecksum = 0u;
    ASSERT_TRUE(AssetsGraphicsFixture::FindShaderArchiveSourceChecksum(records, shaderNames[0], Name(s_PS), preservedChecksum));
    EXPECT_EQ(preservedChecksum, originalChecksum);
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
}

TEST(AssetsGraphics, ShaderCookWithoutMaterialBindIncludes){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
    EXPECT_TRUE(AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(
        testArena,
        "shader_cook_without_material_bind_includes",
        root,
        outputDirectory
    ));

    const Path assetRoot = root / "assets";
    EXPECT_TRUE(WriteStandaloneShaderProbe(assetRoot));

    const bool cooked = AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot });
    EXPECT_TRUE(cooked);
    if(cooked){
        NWB::Core::GraphicsVector<NWB::Core::ShaderArchive::Record> records(testArena.arena);
        EXPECT_TRUE(AssetsGraphicsFixture::LoadCookedShaderArchiveRecords(
            testArena,
            outputDirectory,
            records
        ));

        u64 sourceChecksum = 0u;
        EXPECT_TRUE(AssetsGraphicsFixture::FindShaderArchiveSourceChecksum(
            records,
            Name(s_PROJECT_SHADERS_STANDALONE_PS),
            Name(s_PS),
            sourceChecksum
        ));
    }

    EXPECT_EQ(logger.errorCount(), 0u);

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
}

TEST(AssetsGraphics, ShaderCookCompilesBomPrefixedSourceAndInclude){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
    EXPECT_TRUE(AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(
        testArena,
        "shader_cook_bom_compiler_inputs",
        root,
        outputDirectory
    ));

    const Path assetRoot = root / "assets";
    EXPECT_TRUE(WriteBomCompilerProbe(assetRoot));

    const bool cooked = AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot });
    EXPECT_TRUE(cooked);
    if(cooked){
        NWB::Core::GraphicsVector<NWB::Core::ShaderArchive::Record> records(testArena.arena);
        EXPECT_TRUE(AssetsGraphicsFixture::LoadCookedShaderArchiveRecords(testArena, outputDirectory, records));

        u64 sourceChecksum = 0u;
        EXPECT_TRUE(AssetsGraphicsFixture::FindShaderArchiveSourceChecksum(
            records,
            Name("project/shaders/bom_compiler_probe_ps"),
            Name(s_PS),
            sourceChecksum
        ));
    }

    EXPECT_EQ(logger.errorCount(), 0u);

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
}

TEST(AssetsGraphics, ShaderCookPreservesExactEntryPoint){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
    EXPECT_TRUE(AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(
        testArena,
        "shader_cook_exact_entry_point",
        root,
        outputDirectory
    ));

    const Path assetRoot = root / "assets";
    EXPECT_TRUE(WriteExactEntryPointShaderProbe(assetRoot));

    const bool cooked = AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot });
    EXPECT_TRUE(cooked);
    if(cooked){
        const Name shaderVirtualPath = NWB::Core::ShaderArchive::buildVirtualPathName(
            Name("project/shaders/exact_entry_point_ps"),
            NWB::Core::ShaderArchive::s_DefaultVariant,
            Name(s_PS)
        );
        UniquePtr<NWB::Core::Assets::IAsset> loadedShader;
        EXPECT_TRUE(AssetsGraphicsFixture::LoadCookedAsset<NWB::Impl::ShaderAssetCodec>(
            testArena,
            outputDirectory,
            shaderVirtualPath,
            loadedShader,
            s_ExpectedDualCount
        ));
        if(loadedShader){
            const NWB::Impl::Shader& shader = static_cast<const NWB::Impl::Shader&>(*loadedShader);
            EXPECT_EQ(AStringView(shader.entryPoint()), s_MAINCASE);

            AStringView resolvedEntryPoint;
            NWB::Core::GraphicsVector<u32> words(testArena.arena);
            words.resize(shader.bytecode().size() / sizeof(u32));
            GLB_MEMCPY(words.data(), shader.bytecode().size(), shader.bytecode().data(), shader.bytecode().size());
            EXPECT_EQ(NWB::Core::ResolveSpirvEntryPointName(
                    words.data(),
                    words.size(),
                    AStringView(shader.entryPoint()),
                    NWB::Core::ShaderType::Pixel,
                    resolvedEntryPoint
                ), NWB::Core::SpirvEntryPointLookupResult::Found);
            EXPECT_EQ(resolvedEntryPoint, s_MAINCASE);
            EXPECT_EQ(NWB::Core::ResolveSpirvEntryPointName(
                    words.data(),
                    words.size(),
                    s_MAIN,
                    NWB::Core::ShaderType::Pixel,
                    resolvedEntryPoint
                ), NWB::Core::SpirvEntryPointLookupResult::NotFound);
            EXPECT_TRUE(resolvedEntryPoint.empty());

            NWB::Impl::ShaderAssetCodec codec;
            NWB::Core::Assets::AssetBytes serializedShader = AssetsGraphicsFixture::MakeAssetBytes(testArena);
            EXPECT_TRUE(codec.serialize(shader, serializedShader));
            UniquePtr<NWB::Core::Assets::IAsset> reserializedShader;
            EXPECT_TRUE(codec.deserialize(
                testArena.arena,
                shaderVirtualPath,
                serializedShader,
                reserializedShader
            ));
            if(reserializedShader){
                const NWB::Impl::Shader& decodedShader = static_cast<const NWB::Impl::Shader&>(*reserializedShader);
                EXPECT_EQ(AStringView(decodedShader.entryPoint()), s_MAINCASE);
            }
        }
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
    EXPECT_TRUE(AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(
        testArena,
        "shader_cook_invalid_bytecode_cache",
        root,
        outputDirectory
    ));

    const Path assetRoot = root / "assets";
    EXPECT_TRUE(WriteStandaloneShaderProbe(assetRoot));
    EXPECT_TRUE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }));

    Path bytecodeCachePath(testArena.arena);
    if(FindSingleShaderBytecodeCachePath(testArena, root / "cache", bytecodeCachePath)){
        EXPECT_TRUE(AssetsGraphicsFixture::WriteTextFile(bytecodeCachePath, "BAD!"));
        EXPECT_TRUE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }));

        const Name shaderVirtualPath = NWB::Core::ShaderArchive::buildVirtualPathName(
            Name(s_PROJECT_SHADERS_STANDALONE_PS),
            NWB::Core::ShaderArchive::s_DefaultVariant,
            Name(s_PS)
        );
        UniquePtr<NWB::Core::Assets::IAsset> loadedShader;
        EXPECT_TRUE(AssetsGraphicsFixture::LoadCookedAsset<NWB::Impl::ShaderAssetCodec>(
            testArena,
            outputDirectory,
            shaderVirtualPath,
            loadedShader,
            s_ExpectedDualCount
        ));
    }

    EXPECT_EQ(logger.errorCount(), 0u);

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

