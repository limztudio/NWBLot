// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"

#include <impl/assets_shader/binary_payload.h>

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
static constexpr AStringView s_SHADER_ASSET_HEAD = "pixel_shader asset;\n\n";
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

    Name virtualPath = s_NameNone;
    const auto virtualPathResult1 = NWB::Core::ShaderArchive::FindVirtualPath(
records,
        Name(s_PROJECT_SHADERS_TEST_SHADER),
        NWB::Core::ShaderArchive::s_DefaultVariant,
        Name(s_PS)
    );
    ASSERT_TRUE(virtualPathResult1);
    virtualPath = *virtualPathResult1;
    EXPECT_EQ(virtualPath, Name(records[0].virtualPathHash));
    EXPECT_EQ(NWB::Core::ShaderArchive::BuildVirtualPathName(
        Name(s_PROJECT_SHADERS_TEST_SHADER),
        "",
        Name(s_PS)
    ), s_NameNone);

    EXPECT_FALSE(NWB::Core::ShaderArchive::FindVirtualPath(
records,
        Name(s_PROJECT_SHADERS_TEST_SHADER),
        "NWB_FEATURE=1",
        Name(s_PS)
    ));

    EXPECT_FALSE(NWB::Core::ShaderArchive::FindVirtualPath(
records,
        Name(s_PROJECT_SHADERS_TEST_SHADER),
        "",
        Name(s_PS)
    ));
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
    const auto entryPointResult1 = NWB::Core::ResolveSpirvEntryPointName(
words,
            LengthOf(words),
            s_MAIN,
            NWB::Core::ShaderType::Pixel
    );
    ASSERT_TRUE(entryPointResult1);
    entryPoint = *entryPointResult1;
    EXPECT_EQ(entryPoint, s_MAIN);

    const auto entryPointResult2 = NWB::Core::ResolveSpirvEntryPointName(
words,
            LengthOf(words),
            s_MAIN,
            NWB::Core::ShaderType::Compute
    );
    ASSERT_FALSE(entryPointResult2);
    EXPECT_EQ(entryPointResult2.error(), NWB::Core::SpirvEntryPointLookupResult::NotFound);

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

    const auto entryPointResult3 = NWB::Core::ResolveSpirvEntryPointName(
invalidWords,
            LengthOf(invalidWords),
            s_MAIN,
            NWB::Core::ShaderType::Pixel
    );
    ASSERT_FALSE(entryPointResult3);
    EXPECT_EQ(entryPointResult3.error(), NWB::Core::SpirvEntryPointLookupResult::InvalidSpirv);
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
    const auto entryPointResult4 = NWB::Core::ResolveSpirvEntryPointName(
malformedOtherStageEntryWords,
            LengthOf(malformedOtherStageEntryWords),
            s_MAIN,
            NWB::Core::ShaderType::Pixel
    );
    ASSERT_FALSE(entryPointResult4);
    EXPECT_EQ(entryPointResult4.error(), NWB::Core::SpirvEntryPointLookupResult::InvalidSpirv);
}

TEST(AssetsGraphics, SpirvEntryPointViewsRebindCopiedWordsAndRejectMalformedMatches){
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
        const auto entryPointResult5 = NWB::Core::ResolveSpirvEntryPointName(sourceWords.data(), sourceWords.size(), requestedName, NWB::Core::ShaderType::Pixel);
        ASSERT_TRUE(entryPointResult5);
        sourceEntryPoint = *entryPointResult5;
        EXPECT_EQ(sourceEntryPoint.data(), reinterpret_cast<const char*>(sourceWords.data() + 8u));
        copiedWords = sourceWords;
        const auto entryPointResult6 = NWB::Core::ResolveSpirvEntryPointName(copiedWords.data(), copiedWords.size(), requestedName, NWB::Core::ShaderType::Pixel);
        ASSERT_TRUE(entryPointResult6);
        copiedEntryPoint = *entryPointResult6;
        EXPECT_EQ(copiedEntryPoint.data(), reinterpret_cast<const char*>(copiedWords.data() + 8u));
        EXPECT_NE(copiedEntryPoint.data(), sourceEntryPoint.data());
    }
    // Specializations retain their own bytecode, so their entry-name view must survive the original module.
    EXPECT_EQ(copiedEntryPoint, s_MAIN);
    EXPECT_EQ(copiedEntryPoint.data()[copiedEntryPoint.size()], '\0');

    Core::Assets::AssetBytes unalignedBytes(testArena.arena);
    unalignedBytes.resize(1u + copiedWords.size() * sizeof(u32));
    NWB_MEMCPY(unalignedBytes.data() + 1u, unalignedBytes.size() - 1u, copiedWords.data(), copiedWords.size() * sizeof(u32));
    const BinaryByteView unalignedModule{ unalignedBytes.data() + 1u, unalignedBytes.size() - 1u };
    AStringView byteEntryPoint;
    const auto entryPointResult7 = Core::ResolveSpirvEntryPointName(unalignedModule, requestedName, Core::ShaderType::Pixel);
    ASSERT_TRUE(entryPointResult7);
    byteEntryPoint = *entryPointResult7;
    EXPECT_EQ(byteEntryPoint.data(), reinterpret_cast<const char*>(unalignedBytes.data() + 1u + 8u * sizeof(u32)));
    EXPECT_EQ(byteEntryPoint, s_MAIN);
    const auto entryPointResult8 = Core::ResolveSpirvEntryPointName(
BinaryByteView{ unalignedModule.data(), unalignedModule.size() - 1u }, requestedName,
        Core::ShaderType::Pixel
    );
    ASSERT_FALSE(entryPointResult8);
    EXPECT_EQ(entryPointResult8.error(), Core::SpirvEntryPointLookupResult::InvalidSpirv);
    AppendPOD(unalignedBytes, 0u);
    const auto entryPointResult9 = Core::ResolveSpirvEntryPointName(
BinaryByteView{ unalignedBytes.data() + 1u, unalignedBytes.size() - 1u }, requestedName,
        Core::ShaderType::Pixel
    );
    ASSERT_FALSE(entryPointResult9);
    EXPECT_EQ(entryPointResult9.error(), Core::SpirvEntryPointLookupResult::InvalidSpirv);

    copiedWords.push_back(0u);
    const auto entryPointResult10 = NWB::Core::ResolveSpirvEntryPointName(copiedWords.data(), copiedWords.size(), requestedName, NWB::Core::ShaderType::Pixel);
    ASSERT_FALSE(entryPointResult10);
    EXPECT_EQ(entryPointResult10.error(), NWB::Core::SpirvEntryPointLookupResult::InvalidSpirv);
}

TEST(AssetsGraphics, ShaderMetadataRejectsDefaultVariantAlias){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    auto rootResult = AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "shader_default_variant_alias");
    ASSERT_TRUE(rootResult);
    Path root = Move(*rootResult);


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
    EXPECT_FALSE(AssetsGraphicsFixture::ParseShaderMetadataFile<NWB::Impl::ShaderCook::IncludeEntry>(testArena, shaderCook, includeMetaPath, scratchArena));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT(
        "unsupported asset field 'default_variant'"
    )));

    EXPECT_TRUE(RemoveAllIfExists(root));
#else
#endif
}


TEST(AssetsGraphics, ShaderMetadataRejectsRetiredGenericTypeAndStageField){
    CapturingLogger logger;
    const Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_ShaderScratchArena);
    auto rootResult = AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "shader_retired_type_and_stage");
    ASSERT_TRUE(rootResult);
    Path root = Move(*rootResult);

    const Path metadataPath = root / "shader.nwb";
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(root / "shader.slang", "[numthreads(1, 1, 1)] void main(){}\n"));
    Impl::ShaderCook shaderCook(testArena.arena);
    Impl::ShaderCook::ShaderEntry entry(testArena.arena);
    constexpr AStringView s_ValidMetadata = "compute_shader asset;\nasset.entry_point = \"main\";\n";
    constexpr AStringView s_RetiredMetadata[] = {
        "shader asset;\nasset.stage = \"cs\";\nasset.entry_point = \"main\";\n",
        "shader asset;\nasset.entry_point = \"main\";\n",
        "compute_shader asset;\nasset.stage = \"cs\";\nasset.entry_point = \"main\";\n",
        "compute_shader asset;\nasset.stage = \"ps\";\nasset.entry_point = \"main\";\n",
    };
    for(const AStringView metadata : s_RetiredMetadata){
        SCOPED_TRACE(metadata);
        ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(metadataPath, s_ValidMetadata));
        auto metadataResult8 = AssetsGraphicsFixture::ParseShaderMetadataFile<NWB::Impl::ShaderCook::ShaderEntry>(testArena, shaderCook, metadataPath, scratchArena);
        ASSERT_TRUE(metadataResult8);
        if(metadataResult8)
            entry = Move(*metadataResult8);
        ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(metadataPath, metadata));
        EXPECT_FALSE(AssetsGraphicsFixture::ParseShaderMetadataFile<NWB::Impl::ShaderCook::ShaderEntry>(testArena, shaderCook, metadataPath, scratchArena));
        ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(metadataPath, s_ValidMetadata));
        auto metadataResult7 = AssetsGraphicsFixture::ParseShaderMetadataFile<NWB::Impl::ShaderCook::ShaderEntry>(testArena, shaderCook, metadataPath, scratchArena);
        EXPECT_TRUE(metadataResult7);
        if(metadataResult7)
            entry = Move(*metadataResult7);
    }
    EXPECT_TRUE(RemoveAllIfExists(root));
}


TEST(AssetsGraphics, ShaderMetadataRejectsObsoleteProfilesAndInvalidConditionalFlags){
    CapturingLogger logger;
    const Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_ShaderScratchArena);
    auto rootResult = AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "shader_ray_query_metadata");
    ASSERT_TRUE(rootResult);
    Path root = Move(*rootResult);

    const Path metadataPath = root / "shader.nwb";
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(root / "shader.slang", "void main(){}\n"));
    struct MetadataCase{
        AStringView fields;
        bool accepted;
    };
    constexpr MetadataCase s_Cases[] = {
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
    for(const MetadataCase& testCase : s_Cases){
        SCOPED_TRACE(testCase.fields);
        Impl::ShaderCook::CookString metadata("compute_shader asset;\nasset.entry_point = \"main\";\n", testArena.arena);
        metadata.append(testCase.fields);
        ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(metadataPath, metadata));
        EXPECT_EQ(
            AssetsGraphicsFixture::ParseShaderMetadataFile<NWB::Impl::ShaderCook::ShaderEntry>(testArena, shaderCook, metadataPath, scratchArena).has_value(),
            testCase.accepted
        );
    }
    constexpr AStringView s_ShaderAssetTypes[] = { "vertex_shader", "pixel_shader", "ray_generation_shader" };
    for(const AStringView shaderAssetType : s_ShaderAssetTypes){
        const auto metadata = StringFormat(testArena.arena,
            "{} asset;\nasset.entry_point = \"main\";\nasset.emit_mesh_compute_shadow = 1;\n", shaderAssetType);
        ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(metadataPath, metadata));
        EXPECT_FALSE(AssetsGraphicsFixture::ParseShaderMetadataFile<NWB::Impl::ShaderCook::ShaderEntry>(testArena, shaderCook, metadataPath, scratchArena));
    }
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(metadataPath,
        "mesh_shader asset;\nasset.entry_point = \"main\";\nasset.emit_mesh_compute_shadow = 0;\n"));
    auto metadataResult6 = AssetsGraphicsFixture::ParseShaderMetadataFile<NWB::Impl::ShaderCook::ShaderEntry>(testArena, shaderCook, metadataPath, scratchArena);
    ASSERT_TRUE(metadataResult6);
    if(metadataResult6)
        entry = Move(*metadataResult6);
    EXPECT_TRUE(RemoveAllIfExists(root));
}

TEST(AssetsGraphics, ShaderMetadataRejectsEngineTransportDefinesAndRecoversWithRealVariants){
    CapturingLogger logger;
    const Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_ShaderScratchArena);
    auto rootResult = AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "shader_transport_defines");
    ASSERT_TRUE(rootResult);
    Path root = Move(*rootResult);

    const Path shaderPath = root / "shader.nwb";
    const Path includePath = root / "include.nwb";
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(root / "shader.slang", "void main(){}\n"));
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(root / "include.slangi", "static const uint sourceValue = 1u;\n"));
    static constexpr AStringView s_ShaderDeclaration = "compute_shader asset;\nasset.entry_point = \"main\";\n";
    static constexpr AStringView s_RealDefines = "asset.defines = { \"PROJECT_QUALITY\": [\"0\", \"1\"] };\n";
    const auto validShaderMetadata = StringFormat(testArena.arena, "{}{}", s_ShaderDeclaration, s_RealDefines);
    const auto validIncludeMetadata = StringFormat(testArena.arena, "include asset;\n{}", s_RealDefines);
    Impl::ShaderCook shaderCook(testArena.arena);
    Impl::ShaderCook::ShaderEntry shaderEntry(testArena.arena);
    Impl::ShaderCook::IncludeEntry includeEntry(testArena.arena);

    for(const AStringView retiredValue : { AStringView("0"), AStringView("1") }){
        SCOPED_TRACE(retiredValue);
        ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(shaderPath, validShaderMetadata));
        auto metadataResult5 = AssetsGraphicsFixture::ParseShaderMetadataFile<NWB::Impl::ShaderCook::ShaderEntry>(testArena, shaderCook, shaderPath, scratchArena);
        ASSERT_TRUE(metadataResult5);
        if(metadataResult5)
            shaderEntry = Move(*metadataResult5);
        ASSERT_FALSE(shaderEntry.defineValues.empty());
        const auto obsoleteShaderMetadata = StringFormat(
            testArena.arena,
            "{}asset.defines = {{ \"NWB_BINDLESS_TLAS\": [\"{}\"] }};\n",
            s_ShaderDeclaration,
            retiredValue
        );
        ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(shaderPath, obsoleteShaderMetadata));
        EXPECT_FALSE(AssetsGraphicsFixture::ParseShaderMetadataFile<NWB::Impl::ShaderCook::ShaderEntry>(testArena, shaderCook, shaderPath, scratchArena));
        EXPECT_FALSE(shaderEntry.defineValues.empty());
        EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("define 'NWB_BINDLESS_TLAS' is an engine transport feature")));
        ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(shaderPath, validShaderMetadata));
        auto metadataResult4 = AssetsGraphicsFixture::ParseShaderMetadataFile<NWB::Impl::ShaderCook::ShaderEntry>(testArena, shaderCook, shaderPath, scratchArena);
        ASSERT_TRUE(metadataResult4);
        if(metadataResult4)
            shaderEntry = Move(*metadataResult4);
        EXPECT_TRUE(shaderCook.validateVariantSignature("project/shader", "PROJECT_QUALITY=0", shaderEntry.defineValues, scratchArena));
        EXPECT_TRUE(shaderCook.validateVariantSignature("project/shader", "PROJECT_QUALITY=1", shaderEntry.defineValues, scratchArena));
        EXPECT_FALSE(shaderCook.validateVariantSignature(
            "project/shader", "NWB_BINDLESS_TLAS=1;PROJECT_QUALITY=1", shaderEntry.defineValues, scratchArena
        ));

        ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(includePath, validIncludeMetadata));
        auto metadataResult3 = AssetsGraphicsFixture::ParseShaderMetadataFile<NWB::Impl::ShaderCook::IncludeEntry>(testArena, shaderCook, includePath, scratchArena);
        ASSERT_TRUE(metadataResult3);
        if(metadataResult3)
            includeEntry = Move(*metadataResult3);
        ASSERT_FALSE(includeEntry.defineValues.empty());
        const auto obsoleteIncludeMetadata = StringFormat(
            testArena.arena,
            "include asset;\nasset.defines = {{ \"NWB_BINDLESS_TLAS\": [\"{}\"] }};\n",
            retiredValue
        );
        ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(includePath, obsoleteIncludeMetadata));
        EXPECT_FALSE(AssetsGraphicsFixture::ParseShaderMetadataFile<NWB::Impl::ShaderCook::IncludeEntry>(testArena, shaderCook, includePath, scratchArena));
        EXPECT_FALSE(includeEntry.defineValues.empty());
        ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(includePath, validIncludeMetadata));
        auto metadataResult2 = AssetsGraphicsFixture::ParseShaderMetadataFile<NWB::Impl::ShaderCook::IncludeEntry>(testArena, shaderCook, includePath, scratchArena);
        ASSERT_TRUE(metadataResult2);
        if(metadataResult2)
            includeEntry = Move(*metadataResult2);
        EXPECT_TRUE(shaderCook.validateVariantSignature("project/include", "PROJECT_QUALITY=1", includeEntry.defineValues, scratchArena));
    }
    EXPECT_TRUE(RemoveAllIfExists(root));
}

TEST(AssetsGraphics, ShaderMetadataRejectsDefinesThatCannotRoundTripVariantSignatures){
    CapturingLogger logger;
    const Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_ShaderScratchArena);
    auto rootResult = AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "shader_invalid_define_tokens");
    ASSERT_TRUE(rootResult);
    Path root = Move(*rootResult);

    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(root / "shader.slang", "void main(){}\n"));
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(root / "include.slangi", "static const uint sourceValue = 1u;\n"));
    static constexpr AStringView s_InvalidDefines[] = {
        "\"\": [\"1\"]",
        "\"9QUALITY\": [\"1\"]",
        "\"PROJECT-QUALITY\": [\"1\"]",
        "\"PROJECT_QUALITY\": [\"1;OTHER=0\"]",
        "\"PROJECT_QUALITY\": [\" 1\"]",
        "\"PROJECT_QUALITY\": [\"1 \"]",
    };
    Impl::ShaderCook shaderCook(testArena.arena);
    Impl::ShaderCook::ShaderEntry shaderEntry(testArena.arena);
    Impl::ShaderCook::IncludeEntry includeEntry(testArena.arena);
    for(const bool include : { false, true }){
        SCOPED_TRACE(include ? "include" : "shader");
        const Path metadataPath = root / (include ? "include.nwb" : "shader.nwb");
        const AStringView declaration = include ? "include asset;\n" : "compute_shader asset;\nasset.entry_point = \"main\";\n";
        const auto validMetadata = StringFormat(testArena.arena, "{}asset.defines = {{ \"PROJECT_QUALITY\": [\"1\"] }};\n", declaration);
        const auto parse = [&](){
            if(include){
                auto metadataResult1 = AssetsGraphicsFixture::ParseShaderMetadataFile<NWB::Impl::ShaderCook::IncludeEntry>(testArena, shaderCook, metadataPath, scratchArena);
                if(metadataResult1)
                    includeEntry = Move(*metadataResult1);
                return metadataResult1.has_value();
            }
            auto metadataResult0 = AssetsGraphicsFixture::ParseShaderMetadataFile<NWB::Impl::ShaderCook::ShaderEntry>(testArena, shaderCook, metadataPath, scratchArena);
            if(metadataResult0)
                shaderEntry = Move(*metadataResult0);
            return metadataResult0.has_value();
        };
        for(const AStringView invalidDefines : s_InvalidDefines){
            SCOPED_TRACE(invalidDefines);
            ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(metadataPath, validMetadata));
            ASSERT_TRUE(parse());
            ASSERT_FALSE(include ? includeEntry.defineValues.empty() : shaderEntry.defineValues.empty());
            const auto invalidMetadata = StringFormat(testArena.arena, "{}asset.defines = {{ {} }};\n", declaration, invalidDefines);
            ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(metadataPath, invalidMetadata));
            EXPECT_FALSE(parse());
            EXPECT_FALSE(include ? includeEntry.defineValues.empty() : shaderEntry.defineValues.empty());
            ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(metadataPath, validMetadata));
            ASSERT_TRUE(parse());
            const auto& defineValues = include ? includeEntry.defineValues : shaderEntry.defineValues;
            EXPECT_TRUE(shaderCook.validateVariantSignature("project/define_probe", "PROJECT_QUALITY=1", defineValues, scratchArena));
        }
    }
    EXPECT_TRUE(RemoveAllIfExists(root));
}

TEST(AssetsGraphics, ShaderDependenciesTrackContinuedIncludesAndRejectUnplannedIncludes){
    CapturingLogger logger;
    const Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_ShaderScratchArena);
    auto rootResult = AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "shader_dependency_directives");
    ASSERT_TRUE(rootResult);
    Path root = Move(*rootResult);

    const Path sourcePath = root / "source.slang";
    const Path trackedInclude = root / "tracked.slangi";
    static constexpr AStringView s_Source =
        "/*\n#include \"missing_block.slangi\"\n*/\n"
        "// #include \"missing_line.slangi\"\n"
        "#include \\\r\n\"tracked.slangi\"\n"
    ;
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(sourcePath, s_Source));
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(trackedInclude, "static const uint trackedValue = 1u;\n"));
    Impl::ShaderCook shaderCook(testArena.arena);
    Impl::ShaderCook::CookVector<Path> includeDirectories(testArena.arena);
    Impl::ShaderCook::CookVector<Path> dependencies(testArena.arena);
    ASSERT_TRUE(shaderCook.gatherShaderDependencies(sourcePath, includeDirectories, {}, dependencies, scratchArena));
    ASSERT_EQ(dependencies.size(), 2u);
    ASSERT_NE(FindIf(dependencies.begin(), dependencies.end(), [&trackedInclude](const Path& path)noexcept{
        return path == trackedInclude;
    }), dependencies.end());
    u64 beforeChecksum = 0u;
    u64 afterChecksum = 0u;
    const auto dependencyResult4 = shaderCook.computeDependencyChecksum(dependencies, { { root, "source" } }, scratchArena);
    ASSERT_TRUE(dependencyResult4);
    beforeChecksum = dependencyResult4->checksum;
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(trackedInclude, "static const uint trackedValue = 2u;\n"));
    const auto dependencyResult3 = shaderCook.computeDependencyChecksum(dependencies, { { root, "source" } }, scratchArena);
    ASSERT_TRUE(dependencyResult3);
    afterChecksum = dependencyResult3->checksum;
    EXPECT_NE(beforeChecksum, afterChecksum);
    EXPECT_EQ(logger.errorCount(), 0u);

    static constexpr AStringView s_UnplannedIncludes[] = {
        "#define GENERATED_INCLUDE \"tracked.slangi\"\n#include GENERATED_INCLUDE\n",
        "#include_next \"tracked.slangi\"\n",
        "#import \"tracked.slangi\"\n",
    };
    for(const AStringView source : s_UnplannedIncludes){
        SCOPED_TRACE(source);
        ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(sourcePath, source));
        EXPECT_FALSE(shaderCook.gatherShaderDependencies(sourcePath, includeDirectories, {}, dependencies, scratchArena));
    }
    const usize failureErrors = logger.errorCount();
    EXPECT_GT(failureErrors, 0u);
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(sourcePath, s_Source));
    EXPECT_TRUE(shaderCook.gatherShaderDependencies(sourcePath, includeDirectories, {}, dependencies, scratchArena));
    EXPECT_EQ(logger.errorCount(), failureErrors);
    EXPECT_TRUE(RemoveAllIfExists(root));
}

TEST(AssetsGraphics, ShaderDependencyChecksumAliasesGeneratedRoot){
    TestArena testArena;
    auto rootResult = AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "shader_dependency_checksum_alias");
    ASSERT_TRUE(rootResult);
    Path root = Move(*rootResult);


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
    const auto dependencyResult2 = shaderCook.computeDependencyChecksum(firstDependencies, {
            { firstGeneratedRoot, s_MATERIAL_BIND_INCLUDES }
        }, scratchArena);
    ASSERT_TRUE(dependencyResult2);
    firstChecksum = dependencyResult2->checksum;
    const auto dependencyResult1 = shaderCook.computeDependencyChecksum(secondDependencies, {
            { secondGeneratedRoot, s_MATERIAL_BIND_INCLUDES }
        }, scratchArena);
    ASSERT_TRUE(dependencyResult1);
    secondChecksum = dependencyResult1->checksum;
    EXPECT_EQ(firstChecksum, secondChecksum);

    EXPECT_TRUE(AssetsGraphicsFixture::WriteTextFile(secondGeneratedInclude, "changed generated include\n"));
    u64 changedChecksum = 0u;
    const auto dependencyResult0 = shaderCook.computeDependencyChecksum(secondDependencies, {
            { secondGeneratedRoot, s_MATERIAL_BIND_INCLUDES }
        }, scratchArena);
    ASSERT_TRUE(dependencyResult0);
    changedChecksum = dependencyResult0->checksum;
    EXPECT_NE(firstChecksum, changedChecksum);

#if defined(NWB_FINAL)
    const Path unaliasedDependency = root / "outside" / "unaliased.slangi";
    EXPECT_TRUE(AssetsGraphicsFixture::WriteTextFile(unaliasedDependency, "outside alias root\n"));
    NWB::Impl::ShaderCook::CookVector<Path> unaliasedDependencies(testArena.arena);
    unaliasedDependencies.push_back(unaliasedDependency);

    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);
    EXPECT_FALSE(shaderCook.computeDependencyChecksum(unaliasedDependencies, {
            { firstGeneratedRoot, s_MATERIAL_BIND_INCLUDES }
        }, scratchArena));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT(
        "outside the declared dependency root aliases"
    )));
#endif

    EXPECT_TRUE(RemoveAllIfExists(root));
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
        "{}{}",
        s_SHADER_ASSET_HEAD,
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
        "{}{}",
        s_SHADER_ASSET_HEAD,
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
        "{}{}",
        s_SHADER_ASSET_HEAD,
        "asset.entry_point = \"MainCase\";\n"
    );
    if(!AssetsGraphicsFixture::WriteTextFile(assetRoot / "shaders" / "exact_entry_point_ps.nwb", shaderMetadata))
        return false;

    return AssetsGraphicsFixture::WriteTextFile(assetRoot / "shaders" / "exact_entry_point_ps.slang", s_ExactEntryPointShaderProbeSource);
}

TEST(AssetsGraphics, ShaderCookCompilesAmplificationStageWithoutMeshDependency){
    CapturingLogger logger;
    const Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    auto cookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, "standalone_amplification_stage");
    ASSERT_TRUE(cookCase);
    Path& root = cookCase->root;
    Path& outputDirectory = cookCase->outputDirectory;

    const Path assetRoot = root / "assets";
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(
        assetRoot / "shaders" / "standalone_task.nwb",
        "amplification_shader asset;\nasset.entry_point = \"main\";\n"
    ));
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(
        assetRoot / "shaders" / "standalone_task.slang",
        "[shader(\"amplification\")]\n[numthreads(1, 1, 1)]\nvoid main(){}\n"
    ));
    ASSERT_TRUE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }));
    const Name virtualPath = Core::ShaderArchive::BuildVirtualPathName(
        Name("project/shaders/standalone_task"), Core::ShaderArchive::s_DefaultVariant, Name("task")
    );
    UniquePtr<Core::Assets::IAsset> loaded;
    auto loadedLoadResult = AssetsGraphicsFixture::LoadCookedAsset<Impl::AmplificationShaderAssetCodec>(testArena, outputDirectory, virtualPath, s_ExpectedDualCount);
    ASSERT_TRUE(loadedLoadResult);
    loaded = Move(*loadedLoadResult);
    EXPECT_EQ(logger.errorCount(), 0u);
    EXPECT_TRUE(RemoveAllIfExists(root));
}


TEST(AssetsGraphics, ConcreteShaderAdmissionRejectsWrongStageAndPreservesLoadedState){
    CapturingLogger logger;
    const Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    auto cookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, "concrete_shader_stage_admission");
    ASSERT_TRUE(cookCase);
    Path& root = cookCase->root;
    Path& outputDirectory = cookCase->outputDirectory;

    const Path assetRoot = root / "assets";
    ASSERT_TRUE(WriteStandaloneShaderProbe(assetRoot));
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(
        assetRoot / "shaders" / "stage_cs.nwb",
        "compute_shader asset;\nasset.entry_point = \"MainC\";\n"
    ));
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(
        assetRoot / "shaders" / "stage_cs.slang",
        "[numthreads(1, 1, 1)] void MainC(){}\n"
    ));
    ASSERT_TRUE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }));
    const Name pixelPath = Core::ShaderArchive::BuildVirtualPathName(
        Name(s_PROJECT_SHADERS_STANDALONE_PS), Core::ShaderArchive::s_DefaultVariant, Name(s_PS)
    );
    const Name computePath = Core::ShaderArchive::BuildVirtualPathName(
        Name("project/shaders/stage_cs"), Core::ShaderArchive::s_DefaultVariant, Name("cs")
    );
    UniquePtr<Core::Assets::IAsset> pixelAsset;
    UniquePtr<Core::Assets::IAsset> computeAsset;
    auto pixelAssetLoadResult = AssetsGraphicsFixture::LoadCookedAsset<Impl::PixelShaderAssetCodec>(testArena, outputDirectory, pixelPath, 3u);
    ASSERT_TRUE(pixelAssetLoadResult);
    pixelAsset = Move(*pixelAssetLoadResult);
    auto computeAssetLoadResult = AssetsGraphicsFixture::LoadCookedAsset<Impl::ComputeShaderAssetCodec>(testArena, outputDirectory, computePath, 3u);
    ASSERT_TRUE(computeAssetLoadResult);
    computeAsset = Move(*computeAssetLoadResult);
    EXPECT_EQ(logger.errorCount(), 0u);
    const Impl::PixelShader& cookedPixel = static_cast<const Impl::PixelShader&>(*pixelAsset);
    const Impl::ComputeShader& cookedCompute = static_cast<const Impl::ComputeShader&>(*computeAsset);
    Core::Assets::AssetBytes pixelBinary(testArena.arena);
    Core::Assets::AssetBytes computeBinary(testArena.arena);
    ASSERT_EQ(Impl::ShaderBinaryPayload::EncodeAssetPayload(
        AStringView(cookedPixel.entryPoint()), cookedPixel.bytecode(), pixelBinary
    ), Impl::ShaderBinaryPayload::AssetPayloadEncodeFailure::None);
    ASSERT_EQ(Impl::ShaderBinaryPayload::EncodeAssetPayload(
        AStringView(cookedCompute.entryPoint()), cookedCompute.bytecode(), computeBinary
    ), Impl::ShaderBinaryPayload::AssetPayloadEncodeFailure::None);

    Core::Assets::IAsset* const preservedPixel = pixelAsset.get();
    Core::Assets::IAsset* const preservedCompute = computeAsset.get();
    const Impl::PixelShaderAssetCodec pixelCodec;
    const Impl::ComputeShaderAssetCodec computeCodec;
    EXPECT_FALSE(pixelCodec.deserialize(testArena.arena, pixelPath, computeBinary));
    EXPECT_FALSE(computeCodec.deserialize(testArena.arena, computePath, pixelBinary));
    EXPECT_EQ(pixelAsset.get(), preservedPixel);
    EXPECT_EQ(computeAsset.get(), preservedCompute);
    UniquePtr<Core::Assets::IAsset> rejectedAsset;
    EXPECT_FALSE(pixelCodec.deserialize(testArena.arena, pixelPath, computeBinary));
    EXPECT_FALSE(rejectedAsset);

    Impl::PixelShader pixel(testArena.arena, pixelPath);
    Impl::ComputeShader compute(testArena.arena, computePath);
    ASSERT_TRUE(pixel.loadBinary(pixelBinary));
    // A five-byte entry point puts SPIR-V at an unaligned payload offset.
    ASSERT_TRUE(compute.loadBinary(computeBinary));
    const u8* const pixelBytes = pixel.bytecode().data();
    const u8* const computeBytes = compute.bytecode().data();
    const char* const pixelEntry = pixel.entryPoint().data();
    const char* const computeEntry = compute.entryPoint().data();
    EXPECT_FALSE(pixel.loadBinary(computeBinary));
    EXPECT_FALSE(compute.loadBinary(pixelBinary));
    Core::Assets::AssetBytes malformedPixelBinary = pixelBinary;
    AppendPOD(malformedPixelBinary, 0u);
    EXPECT_FALSE(pixel.loadBinary(malformedPixelBinary));
    Core::Assets::AssetBytes missingEntryBinary(testArena.arena);
    ASSERT_EQ(Impl::ShaderBinaryPayload::EncodeAssetPayload(
        "absent", cookedCompute.bytecode(), missingEntryBinary
    ), Impl::ShaderBinaryPayload::AssetPayloadEncodeFailure::None);
    EXPECT_FALSE(compute.loadBinary(missingEntryBinary));
    EXPECT_EQ(pixel.bytecode().data(), pixelBytes);
    EXPECT_EQ(compute.bytecode().data(), computeBytes);
    EXPECT_EQ(pixel.entryPoint().data(), pixelEntry);
    EXPECT_EQ(compute.entryPoint().data(), computeEntry);
    EXPECT_EQ(pixel.bytecode(), cookedPixel.bytecode());
    EXPECT_EQ(compute.bytecode(), cookedCompute.bytecode());
    EXPECT_EQ(AStringView(pixel.entryPoint()), AStringView(cookedPixel.entryPoint()));
    EXPECT_EQ(AStringView(compute.entryPoint()), AStringView(cookedCompute.entryPoint()));
    EXPECT_TRUE(RemoveAllIfExists(root));
}


TEST(AssetsGraphics, GatherIndependentShaderBuildsWithoutSources){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger, NWB::Core::Common::LoggerBreakPolicy::BreakOnFatal);

    TestArena testArena;
    auto rootResult = AssetsGraphicsFixture::PrepareAssetsGraphicsCaseRoot(testArena, "gather_independent_shaders");
    ASSERT_TRUE(rootResult);
    Path root = Move(*rootResult);

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

    ASSERT_TRUE(RemoveAllIfExists(firstAssetRoot));
    ASSERT_TRUE(RemoveAllIfExists(secondAssetRoot));

    NWB::Pipeline::AssetGatherer::AssetGatherOptions options(testArena.arena);
    options.inputs.emplace_back(PathToString(testArena.arena, firstBuilt));
    options.inputs.emplace_back(PathToString(testArena.arena, secondBuilt));
    options.outputDirectory = PathToString(testArena.arena, outputDirectory);
    options.configuration = "tests";
    options.mergePayloads = &NWB::Impl::MergeGatheredGraphicsAsset;
    ASSERT_TRUE(NWB::Pipeline::AssetGatherer::GatherAssets(options));

    NWB::Core::GraphicsVector<NWB::Core::ShaderArchive::Record> records(testArena.arena);
    auto recordsLoadResult = AssetsGraphicsFixture::LoadCookedShaderArchiveRecords(testArena, outputDirectory);
    ASSERT_TRUE(recordsLoadResult);
    records = Move(*recordsLoadResult);
    ASSERT_EQ(records.size(), s_ExpectedDualCount);
    const Name shaderNames[] = { Name(s_PROJECT_SHADERS_STANDALONE_PS), Name("project/shaders/exact_entry_point_ps") };
    for(const Name& shaderName : shaderNames){
        Name virtualPath;
        const auto virtualPathResult4 = NWB::Core::ShaderArchive::FindVirtualPath(records, shaderName, NWB::Core::ShaderArchive::s_DefaultVariant, Name(s_PS));
        ASSERT_TRUE(virtualPathResult4);
        virtualPath = *virtualPathResult4;
        UniquePtr<NWB::Core::Assets::IAsset> shader;
        auto shaderLoadResult = AssetsGraphicsFixture::LoadCookedAsset<NWB::Impl::PixelShaderAssetCodec>(testArena, outputDirectory, virtualPath, 3u);
        ASSERT_TRUE(shaderLoadResult);
        shader = Move(*shaderLoadResult);
        EXPECT_FALSE(static_cast<const NWB::Impl::PixelShader&>(*shader).bytecode().empty());
    }
    EXPECT_EQ(logger.errorCount(), 0u);

    u64 originalChecksum = 0u;
    auto originalChecksumLoadResult = AssetsGraphicsFixture::FindShaderArchiveSourceChecksum(records, shaderNames[0], Name(s_PS));
    ASSERT_TRUE(originalChecksumLoadResult);
    originalChecksum = *originalChecksumLoadResult;
    options.inputs.emplace_back(PathToString(testArena.arena, conflictingBuilt));
    EXPECT_FALSE(NWB::Pipeline::AssetGatherer::GatherAssets(options));
    EXPECT_GT(logger.errorCount(), 0u);
    records.clear();
    auto recordsLoadResult2 = AssetsGraphicsFixture::LoadCookedShaderArchiveRecords(testArena, outputDirectory);
    ASSERT_TRUE(recordsLoadResult2);
    records = Move(*recordsLoadResult2);
    ASSERT_EQ(records.size(), s_ExpectedDualCount);
    u64 preservedChecksum = 0u;
    auto preservedChecksumLoadResult = AssetsGraphicsFixture::FindShaderArchiveSourceChecksum(records, shaderNames[0], Name(s_PS));
    ASSERT_TRUE(preservedChecksumLoadResult);
    preservedChecksum = *preservedChecksumLoadResult;
    EXPECT_EQ(preservedChecksum, originalChecksum);
    EXPECT_TRUE(RemoveAllIfExists(root));
}


static void ExpectNoShaderCompilerWorkDirectories(const Path& cacheDirectory){
    const auto entries = RecursiveDirectoryIterator<Path::Arena>::Create(cacheDirectory);
    ASSERT_TRUE(entries);
    for(const auto& entry : *entries){
        const auto filename = PathToString(cacheDirectory.arena(), entry.path().filename());
        EXPECT_FALSE(AStringView(filename).starts_with(".nwb_shader_")) << filename;
    }
}

TEST(AssetsGraphics, ShaderCookCompilesBomPrefixedSourceAndExternalIncludeAndCleansFailedInvocation){
    CapturingLogger logger;
    const Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    auto cookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, "shader_cook_bom_compiler_inputs");
    ASSERT_TRUE(cookCase);
    Path& root = cookCase->root;
    Path& outputDirectory = cookCase->outputDirectory;

    const Path assetRoot = root / "assets";
    ASSERT_TRUE(WriteBomCompilerProbe(assetRoot));
    const Path externalIncludeRoot = root / "external" / "includes";
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(
        externalIncludeRoot / "bom_compiler_probe_include.slangi", s_BomCompilerProbeIncludeSource
    ));
    const auto removedInclude = RemoveFile(assetRoot / "shaders" / "bom_compiler_probe_include.slangi");
    ASSERT_TRUE(removedInclude);
    ASSERT_TRUE(*removedInclude);
    const auto externalIncludeRootText = PathToString(testArena.arena, externalIncludeRoot);
    const auto shaderMetadata = StringFormat(testArena.arena, "{}{}asset.include_roots = [\"{}\"];\n",
        s_SHADER_ASSET_HEAD, s_ASSET_ENTRY_MAIN, externalIncludeRootText
    );
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(assetRoot / "shaders" / "bom_compiler_probe_ps.nwb", shaderMetadata));
    ASSERT_TRUE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }));
    EXPECT_EQ(logger.errorCount(), 0u);
    ExpectNoShaderCompilerWorkDirectories(root / "cache");
    const Name shaderPath = Core::ShaderArchive::BuildVirtualPathName(
        Name("project/shaders/bom_compiler_probe_ps"), Core::ShaderArchive::s_DefaultVariant, Name(s_PS)
    );
    UniquePtr<Core::Assets::IAsset> loadedShader;
    auto loadedShaderLoadResult = AssetsGraphicsFixture::LoadCookedAsset<Impl::PixelShaderAssetCodec>(testArena, outputDirectory, shaderPath, s_ExpectedDualCount);
    ASSERT_TRUE(loadedShaderLoadResult);
    loadedShader = Move(*loadedShaderLoadResult);

    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(
        assetRoot / "shaders" / "bom_compiler_probe_ps.slang", "\xEF\xBB\xBF#error BOM_CLEANUP_REGRESSION\n"
    ));
    EXPECT_FALSE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("BOM_CLEANUP_REGRESSION")));
    ExpectNoShaderCompilerWorkDirectories(root / "cache");
    const usize failureErrors = logger.errorCount();
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(assetRoot / "shaders" / "bom_compiler_probe_ps.slang", s_BomCompilerProbeSource));
    EXPECT_TRUE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }));
    EXPECT_EQ(logger.errorCount(), failureErrors);
    ExpectNoShaderCompilerWorkDirectories(root / "cache");
    EXPECT_TRUE(RemoveAllIfExists(root));
}


TEST(AssetsGraphics, ShaderCookPreservesExactEntryPoint){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    auto cookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, "shader_cook_exact_entry_point");
    ASSERT_TRUE(cookCase);
    Path& root = cookCase->root;
    Path& outputDirectory = cookCase->outputDirectory;


    const Path assetRoot = root / "assets";
    EXPECT_TRUE(WriteExactEntryPointShaderProbe(assetRoot));

    const bool cooked = AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot });
    EXPECT_TRUE(cooked);
    if(cooked){
        const Name shaderVirtualPath = NWB::Core::ShaderArchive::BuildVirtualPathName(
            Name("project/shaders/exact_entry_point_ps"),
            NWB::Core::ShaderArchive::s_DefaultVariant,
            Name(s_PS)
        );
        UniquePtr<NWB::Core::Assets::IAsset> loadedShader;
        auto loadedShaderLoadResult2 = AssetsGraphicsFixture::LoadCookedAsset<NWB::Impl::PixelShaderAssetCodec>(testArena, outputDirectory, shaderVirtualPath, s_ExpectedDualCount);
        ASSERT_TRUE(loadedShaderLoadResult2);
        loadedShader = Move(*loadedShaderLoadResult2);
        if(loadedShader){
            const NWB::Impl::PixelShader& shader = static_cast<const NWB::Impl::PixelShader&>(*loadedShader);
            EXPECT_EQ(AStringView(shader.entryPoint()), s_MAINCASE);

            AStringView resolvedEntryPoint;
            NWB::Core::GraphicsVector<u32> words(testArena.arena);
            words.resize(shader.bytecode().size() / sizeof(u32));
            NWB_MEMCPY(words.data(), shader.bytecode().size(), shader.bytecode().data(), shader.bytecode().size());
            const auto entryPointResult11 = NWB::Core::ResolveSpirvEntryPointName(
words.data(),
                    words.size(),
                    AStringView(shader.entryPoint()),
                    NWB::Core::ShaderType::Pixel
            );
            ASSERT_TRUE(entryPointResult11);
            resolvedEntryPoint = *entryPointResult11;
            EXPECT_EQ(resolvedEntryPoint, s_MAINCASE);
            const auto entryPointResult12 = NWB::Core::ResolveSpirvEntryPointName(
words.data(),
                    words.size(),
                    s_MAIN,
                    NWB::Core::ShaderType::Pixel
            );
            ASSERT_FALSE(entryPointResult12);
            EXPECT_EQ(entryPointResult12.error(), NWB::Core::SpirvEntryPointLookupResult::NotFound);

        }
    }
    EXPECT_EQ(logger.errorCount(), 0u);

    EXPECT_TRUE(RemoveAllIfExists(root));
}

[[nodiscard]] static Expected<Path> FindSingleShaderBytecodeCachePath(TestArena& testArena, const Path& cacheDirectory){
    Path path(testArena.arena);

    const auto cacheEntries = RecursiveDirectoryIterator<Path::Arena>::Create(cacheDirectory);
    EXPECT_TRUE(cacheEntries);
    if(!cacheEntries)
        return MakeUnexpected(Failure{});

    usize foundCount = 0u;
    for(const auto& entry : *cacheEntries){
        const auto isRegularFile = entry.isRegularFile();
        EXPECT_TRUE(isRegularFile);
        if(!isRegularFile)
            return MakeUnexpected(Failure{});
        if(!*isRegularFile)
            continue;

        const auto extension = PathToString(testArena.arena, entry.path().extension());
        if(extension != ".spv")
            continue;

        path = entry.path();
        ++foundCount;
    }

    EXPECT_EQ(foundCount, 1u);
    if(foundCount != 1u)
        return MakeUnexpected(Failure{});
    return path;
}

TEST(AssetsGraphics, ShaderCookRebuildsTruncatedMalformedAndWrongStageBytecodeCaches){
    CapturingLogger logger;
    const Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    TestArena testArena;
    auto cookCase = AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(testArena, "shader_cook_invalid_bytecode_cache");
    ASSERT_TRUE(cookCase);
    Path& root = cookCase->root;
    Path& outputDirectory = cookCase->outputDirectory;

    const Path assetRoot = root / "assets";
    ASSERT_TRUE(WriteStandaloneShaderProbe(assetRoot));
    ASSERT_TRUE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }));
    const auto bytecodeCachePath = FindSingleShaderBytecodeCachePath(testArena, root / "cache");
    ASSERT_TRUE(bytecodeCachePath);
    Core::Assets::AssetBytes originalBytes(testArena.arena);
    ASSERT_TRUE(ReadBinaryFile(*bytecodeCachePath, originalBytes));
    ASSERT_GT(originalBytes.size(), sizeof(u32));

    const Path computeRoot = root / "compute_probe";
    const Path computeAssets = computeRoot / "assets";
    const Path computeOutput = computeRoot / "cooked";
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(
        computeAssets / "shaders" / "cache_stage_cs.nwb", "compute_shader asset;\nasset.entry_point = \"main\";\n"
    ));
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(
        computeAssets / "shaders" / "cache_stage_cs.slang", "[numthreads(1, 1, 1)] void main(){}\n"
    ));
    ASSERT_TRUE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, computeRoot, computeOutput, { computeAssets }));
    const Name computePath = Core::ShaderArchive::BuildVirtualPathName(
        Name("project/shaders/cache_stage_cs"), Core::ShaderArchive::s_DefaultVariant, Name("cs")
    );
    UniquePtr<Core::Assets::IAsset> computeAsset;
    auto computeAssetLoadResult2 = AssetsGraphicsFixture::LoadCookedAsset<Impl::ComputeShaderAssetCodec>(testArena, computeOutput, computePath, s_ExpectedDualCount);
    ASSERT_TRUE(computeAssetLoadResult2);
    computeAsset = Move(*computeAssetLoadResult2);
    const auto& compute = static_cast<const Impl::ComputeShader&>(*computeAsset);
    const Name pixelPath = Core::ShaderArchive::BuildVirtualPathName(
        Name(s_PROJECT_SHADERS_STANDALONE_PS), Core::ShaderArchive::s_DefaultVariant, Name(s_PS)
    );
    static constexpr AStringView s_CorruptionNames[] = { "magic", "truncated", "malformed tail", "wrong physical stage" };
    Core::Assets::AssetBytes corrupted(testArena.arena);
    Core::Assets::AssetBytes repaired(testArena.arena);
    for(usize corruption = 0u; corruption < LengthOf(s_CorruptionNames); ++corruption){
        SCOPED_TRACE(s_CorruptionNames[corruption]);
        corrupted.assign(originalBytes.begin(), originalBytes.end());
        switch(corruption){
        case 0u:
            corrupted.assign({ 'B', 'A', 'D', '!' });
            break;
        case 1u:
            corrupted.pop_back();
            break;
        case 2u:
            corrupted.resize(originalBytes.size() + sizeof(u32), 0u);
            break;
        case 3u:
            corrupted.assign(compute.bytecode().begin(), compute.bytecode().end());
            break;
        }
        ASSERT_TRUE(WriteBinaryFile(*bytecodeCachePath, corrupted));
        ASSERT_TRUE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }));
        ASSERT_TRUE(ReadBinaryFile(*bytecodeCachePath, repaired));
        AStringView resolvedEntryPoint;
        const auto entryPointResult13 = Core::ResolveSpirvEntryPointName({ repaired.data(), repaired.size() }, s_MAIN, Core::ShaderType::Pixel);
        ASSERT_TRUE(entryPointResult13);
        resolvedEntryPoint = *entryPointResult13;
        EXPECT_EQ(resolvedEntryPoint, s_MAIN);
        UniquePtr<Core::Assets::IAsset> pixelAsset;
        auto pixelAssetLoadResult2 = AssetsGraphicsFixture::LoadCookedAsset<Impl::PixelShaderAssetCodec>(testArena, outputDirectory, pixelPath, s_ExpectedDualCount);
        ASSERT_TRUE(pixelAssetLoadResult2);
        pixelAsset = Move(*pixelAssetLoadResult2);
        const auto& pixel = static_cast<const Impl::PixelShader&>(*pixelAsset);
        EXPECT_EQ(AStringView(pixel.entryPoint()), s_MAIN);
    }
    EXPECT_EQ(logger.errorCount(), 0u);
    EXPECT_TRUE(RemoveAllIfExists(root));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

