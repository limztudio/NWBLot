// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"

#include <impl/assets_shader/binary_payload.h>
#include <impl/assets_shader/source_dependencies.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_shader_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_Ps = "ps";
static constexpr AStringView s_ProjectShadersTestShader = "project/shaders/test_shader";
static constexpr AStringView s_Main = "main";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CapturingLogger = AssetsGraphicsFixture::CapturingLogger;
using Path = AssetsGraphicsFixture::Path;
using TestArena = AssetsGraphicsFixture::TestArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AssetsGraphics, ShaderArchiveVariantLookupIsExact){
    TestArena testArena;
    NWB::Core::GraphicsVector<NWB::Core::ShaderArchive::Record> records(testArena.arena);
    NWB::Core::ShaderArchive::Record defaultRecord(testArena.arena);
    defaultRecord.shaderName = Name(s_ProjectShadersTestShader);
    defaultRecord.variantName.assign(NWB::Core::ShaderArchive::s_DefaultVariant);
    defaultRecord.stage = Name(s_Ps);
    defaultRecord.virtualPathHash = Name("shader/test_shader/default/ps").hash();
    records.push_back(Move(defaultRecord));

    Name virtualPath = s_NameNone;
    const auto virtualPathResult1 = NWB::Core::ShaderArchive::FindVirtualPath(records, Name(s_ProjectShadersTestShader), NWB::Core::ShaderArchive::s_DefaultVariant, Name(s_Ps));
    ASSERT_TRUE(virtualPathResult1);
    virtualPath = *virtualPathResult1;
    EXPECT_EQ(virtualPath, Name(records[0].virtualPathHash));
    EXPECT_FALSE(NWB::Core::ShaderArchive::BuildVirtualPathName(Name(s_ProjectShadersTestShader), "", Name(s_Ps)));

    EXPECT_FALSE(NWB::Core::ShaderArchive::FindVirtualPath(records, Name(s_ProjectShadersTestShader), "NWB_FEATURE=1", Name(s_Ps)));

    EXPECT_FALSE(NWB::Core::ShaderArchive::FindVirtualPath(records, Name(s_ProjectShadersTestShader), "", Name(s_Ps)));
}

TEST(AssetsGraphics, ShaderArchiveRejectsNullAndTruncatedByteViewsWhileAdmittingUnalignedRecords){
    TestArena testArena;
    CapturingLogger logger;
    const Core::Common::LoggerRegistrationGuard loggerRegistration(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    Core::GraphicsVector<Core::ShaderArchive::Record> records(testArena.arena);
    Core::ShaderArchive::Record record(testArena.arena);
    record.shaderName = Name(s_ProjectShadersTestShader);
    record.variantName.assign(Core::ShaderArchive::s_DefaultVariant);
    record.stage = Name(s_Ps);
    record.virtualPathHash = Name("shader/unaligned/default/ps").hash();
    record.sourceChecksum = 0x12345678u;
    records.push_back(Move(record));
    Core::Alloc::ScratchArena scratchArena(Name("tests/assets_graphics/archive_admission"));
    const auto encoded = Core::ShaderArchive::SerializeIndex(testArena.arena, records, scratchArena);
    ASSERT_TRUE(encoded);

    alignas(u32) u8 unalignedStorage[2048u] = {};
    ASSERT_LT(encoded->size(), LengthOf(unalignedStorage));
    u8* const unalignedBytes = unalignedStorage + 1u;
    NWB_MEMCPY(unalignedBytes, sizeof(unalignedStorage) - 1u, encoded->data(), encoded->size());
    const auto admitted = Core::ShaderArchive::DeserializeIndex(testArena.arena, { unalignedBytes, encoded->size() });
    ASSERT_TRUE(admitted);
    ASSERT_EQ(admitted->size(), 1u);
    EXPECT_EQ(admitted->front().shaderName, records.front().shaderName);
    EXPECT_EQ(admitted->front().sourceChecksum, records.front().sourceChecksum);
    EXPECT_FALSE(Core::ShaderArchive::DeserializeIndex(testArena.arena, { nullptr, encoded->size() }));
    EXPECT_FALSE(Core::ShaderArchive::DeserializeIndex(testArena.arena, { unalignedBytes, encoded->size() - 1u }));
    EXPECT_EQ(logger.errorCount(), 2u);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("nonempty byte view has no data")));
    EXPECT_EQ(admitted->front().sourceChecksum, records.front().sourceChecksum);
}

static constexpr u32 PackSpirvStringWord(const char a, const char b, const char c, const char d)noexcept{
    return static_cast<u32>(static_cast<u8>(a)) | (static_cast<u32>(static_cast<u8>(b)) << 8u) | (static_cast<u32>(static_cast<u8>(c)) << 16u) | (static_cast<u32>(static_cast<u8>(d)) << 24u);
}

TEST(AssetsGraphics, WrongStageAndMalformedEntryInstructionsAreRejected){
    const u32 words[] = { 0x07230203u, 0x00010500u, 0u, 16u, 0u, (5u << 16u) | 15u, 4u, 1u, PackSpirvStringWord('m', 'a', 'i', 'n'), 0u, };

    EXPECT_TRUE(NWB::Core::IsValidSpirvModuleWords(words, LengthOf(words)));
    const auto entryPointResult1 = NWB::Core::ResolveSpirvEntryPointName(words, LengthOf(words), s_Main, NWB::Core::ShaderType::Pixel);
    ASSERT_TRUE(entryPointResult1);

    const auto entryPointResult2 = NWB::Core::ResolveSpirvEntryPointName(words, LengthOf(words), s_Main, NWB::Core::ShaderType::Compute);
    ASSERT_FALSE(entryPointResult2);
    EXPECT_EQ(entryPointResult2.error(), NWB::Core::SpirvEntryPointLookupResult::NotFound);

    const u32 invalidWords[] = { 0x07230203u, 0x00010500u, 0u, 16u, 0u, (5u << 16u) | 15u, 4u, 1u, PackSpirvStringWord('m', 'a', 'i', 'n'), };

    const auto entryPointResult3 = NWB::Core::ResolveSpirvEntryPointName(invalidWords, LengthOf(invalidWords), s_Main, NWB::Core::ShaderType::Pixel);
    ASSERT_FALSE(entryPointResult3);
    EXPECT_EQ(entryPointResult3.error(), NWB::Core::SpirvEntryPointLookupResult::InvalidSpirv);
    EXPECT_FALSE(NWB::Core::IsValidSpirvModuleWords(invalidWords, LengthOf(invalidWords)));

    const u32 malformedOtherStageEntryWords[] = { 0x07230203u, 0x00010500u, 0u, 16u, 0u, (4u << 16u) | 15u, 0u, 1u, PackSpirvStringWord('m', 'a', 'i', 'n'), };

    EXPECT_FALSE(NWB::Core::IsValidSpirvModuleWords(malformedOtherStageEntryWords, LengthOf(malformedOtherStageEntryWords)));
    const auto entryPointResult4 = NWB::Core::ResolveSpirvEntryPointName(malformedOtherStageEntryWords, LengthOf(malformedOtherStageEntryWords), s_Main, NWB::Core::ShaderType::Pixel);
    ASSERT_FALSE(entryPointResult4);
    EXPECT_EQ(entryPointResult4.error(), NWB::Core::SpirvEntryPointLookupResult::InvalidSpirv);
}

TEST(AssetsGraphics, SpirvEntryPointViewsRebindCopiedWordsAndRejectMalformedMatches){
    TestArena testArena;
    constexpr u32 s_Words[] = { 0x07230203u, 0x00010500u, 0u, 16u, 0u, (5u << 16u) | 15u, 4u, 1u, PackSpirvStringWord('m', 'a', 'i', 'n'), 0u, };
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
    EXPECT_EQ(copiedEntryPoint, s_Main);
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
    EXPECT_EQ(byteEntryPoint, s_Main);
    const auto entryPointResult8 = Core::ResolveSpirvEntryPointName(BinaryByteView{ unalignedModule.data(), unalignedModule.size() - 1u }, requestedName, Core::ShaderType::Pixel);
    ASSERT_FALSE(entryPointResult8);
    EXPECT_EQ(entryPointResult8.error(), Core::SpirvEntryPointLookupResult::InvalidSpirv);
    AppendPOD(unalignedBytes, 0u);
    const auto entryPointResult9 = Core::ResolveSpirvEntryPointName(BinaryByteView{ unalignedBytes.data() + 1u, unalignedBytes.size() - 1u }, requestedName, Core::ShaderType::Pixel);
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
    EXPECT_TRUE(AssetsGraphicsFixture::WriteTextFile(includeMetaPath, "include asset;\n\n" "asset.default_variant = \"NWB_FEATURE=0\";\n" "asset.defines = {\n" "    \"NWB_FEATURE\": [\"0\", \"1\"],\n" "};\n"));
    EXPECT_TRUE(AssetsGraphicsFixture::WriteTextFile(assetRoot / "shaders" / "default_variant_include.slangi", ""));

    NWB::Impl::ShaderCook shaderCook(testArena.arena);
    NWB::Impl::ShaderCook::IncludeEntry includeEntry(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_ShaderScratchArena);
    EXPECT_FALSE(AssetsGraphicsFixture::ParseShaderMetadataFile<NWB::Impl::ShaderCook::IncludeEntry>(testArena, shaderCook, includeMetaPath, scratchArena));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("unsupported asset field 'default_variant'")));

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
        EXPECT_EQ(AssetsGraphicsFixture::ParseShaderMetadataFile<NWB::Impl::ShaderCook::ShaderEntry>(testArena, shaderCook, metadataPath, scratchArena).has_value(), testCase.accepted);
    }
    constexpr AStringView s_ShaderAssetTypes[] = { "vertex_shader", "pixel_shader", "ray_generation_shader" };
    for(const AStringView shaderAssetType : s_ShaderAssetTypes){
        const auto metadata = StringFormat(testArena.arena, "{} asset;\nasset.entry_point = \"main\";\nasset.emit_mesh_compute_shadow = 1;\n", shaderAssetType);
        ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(metadataPath, metadata));
        EXPECT_FALSE(AssetsGraphicsFixture::ParseShaderMetadataFile<NWB::Impl::ShaderCook::ShaderEntry>(testArena, shaderCook, metadataPath, scratchArena));
    }
    ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(metadataPath, "mesh_shader asset;\nasset.entry_point = \"main\";\nasset.emit_mesh_compute_shadow = 0;\n"));
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

    for(const AStringView transportName : { AStringView("NWB_BINDLESS_TLAS"), AStringView("NWB_CAUSTIC_PHOTON_TRANSPORT") }){
        SCOPED_TRACE(transportName);
        for(const AStringView retiredValue : { AStringView("0"), AStringView("1") }){
            SCOPED_TRACE(retiredValue);
            ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(shaderPath, validShaderMetadata));
            auto metadataResult5 = AssetsGraphicsFixture::ParseShaderMetadataFile<NWB::Impl::ShaderCook::ShaderEntry>(testArena, shaderCook, shaderPath, scratchArena);
            ASSERT_TRUE(metadataResult5);
            if(metadataResult5)
                shaderEntry = Move(*metadataResult5);
            ASSERT_FALSE(shaderEntry.defineValues.empty());
            const auto obsoleteShaderMetadata = StringFormat(testArena.arena, "{}asset.defines = {{ \"{}\": [\"{}\"] }};\n", s_ShaderDeclaration, transportName, retiredValue);
            ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(shaderPath, obsoleteShaderMetadata));
            EXPECT_FALSE(AssetsGraphicsFixture::ParseShaderMetadataFile<NWB::Impl::ShaderCook::ShaderEntry>(testArena, shaderCook, shaderPath, scratchArena));
            EXPECT_FALSE(shaderEntry.defineValues.empty());
            const auto expectedDiagnostic = StringFormat(testArena.arena, NWB_TEXT("define '{}' is an engine transport feature"), StringConvert(transportName));
            EXPECT_TRUE(logger.sawErrorContaining(expectedDiagnostic));
            ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(shaderPath, validShaderMetadata));
            auto metadataResult4 = AssetsGraphicsFixture::ParseShaderMetadataFile<NWB::Impl::ShaderCook::ShaderEntry>(testArena, shaderCook, shaderPath, scratchArena);
            ASSERT_TRUE(metadataResult4);
            if(metadataResult4)
                shaderEntry = Move(*metadataResult4);
            EXPECT_TRUE(shaderCook.validateVariantSignature("project/shader", "PROJECT_QUALITY=0", shaderEntry.defineValues, scratchArena));
            EXPECT_TRUE(shaderCook.validateVariantSignature("project/shader", "PROJECT_QUALITY=1", shaderEntry.defineValues, scratchArena));
            const auto invalidSignature = StringFormat(testArena.arena, "{}=1;PROJECT_QUALITY=1", transportName);
            EXPECT_FALSE(shaderCook.validateVariantSignature("project/shader", invalidSignature, shaderEntry.defineValues, scratchArena));

            ASSERT_TRUE(AssetsGraphicsFixture::WriteTextFile(includePath, validIncludeMetadata));
            auto metadataResult3 = AssetsGraphicsFixture::ParseShaderMetadataFile<NWB::Impl::ShaderCook::IncludeEntry>(testArena, shaderCook, includePath, scratchArena);
            ASSERT_TRUE(metadataResult3);
            if(metadataResult3)
                includeEntry = Move(*metadataResult3);
            ASSERT_FALSE(includeEntry.defineValues.empty());
            const auto obsoleteIncludeMetadata = StringFormat(testArena.arena, "include asset;\nasset.defines = {{ \"{}\": [\"{}\"] }};\n", transportName, retiredValue);
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
    static constexpr AStringView s_InvalidDefines[] = { "\"\": [\"1\"]", "\"9QUALITY\": [\"1\"]", "\"PROJECT-QUALITY\": [\"1\"]", "\"PROJECT_QUALITY\": [\"1;OTHER=0\"]", "\"PROJECT_QUALITY\": [\" 1\"]", "\"PROJECT_QUALITY\": [\"1 \"]", };
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

