// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_shader_archive_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_Ps = AssetsGraphicsFixture::s_ShaderStagePs;
static constexpr AStringView s_ProjectShadersTestShader = AssetsGraphicsFixture::s_ProjectShadersTestShader;
static constexpr AStringView s_Main = AssetsGraphicsFixture::s_ShaderEntryMain;

using TestArena = AssetsGraphicsFixture::TestArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AssetsGraphics, ShaderArchiveVariantLookupIsExact){
    TestArena testArena;
    NWB::Core::GraphicsVector<NWB::Core::ShaderArchive::Record> records(testArena.arena);
    NWB::Core::ShaderArchive::Record defaultRecord(testArena.arena);
    defaultRecord.shaderName = Name(s_ProjectShadersTestShader);
    defaultRecord.variantName.assign(NWB::Core::ShaderArchive::s_DefaultVariant);
    defaultRecord.stage = Name(s_Ps);
    defaultRecord.virtualPathHash = Name(AssetsGraphicsFixture::s_AssetCaseShaderTestShaderDefaultPs).hash();
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
    record.virtualPathHash = Name(AssetsGraphicsFixture::s_AssetCaseShaderUnalignedDefaultPs).hash();
    record.sourceChecksum = 0x12345678u;
    records.push_back(Move(record));
    Core::Alloc::ScratchArena scratchArena{Name(AssetsGraphicsFixture::s_AssetCaseTestsAssetsGraphicsArchiveAdmission)};
    const auto encoded = Core::ShaderArchive::SerializeIndex(records, testArena.arena, scratchArena);
    ASSERT_TRUE(encoded);

    alignas(u32) Array<u8, 2048u> unalignedStorage = {};
    ASSERT_LT(encoded->size(), unalignedStorage.size());
    u8* const unalignedBytes = unalignedStorage.data() + 1u;
    NWB_MEMCPY(unalignedBytes, unalignedStorage.size() - 1u, encoded->data(), encoded->size());
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
    const Array<u32, 10u> words = { 0x07230203u, 0x00010500u, 0u, 16u, 0u, (5u << 16u) | 15u, 4u, 1u, PackSpirvStringWord('m', 'a', 'i', 'n'), 0u, };

    EXPECT_TRUE(NWB::Core::IsValidSpirvModuleWords(words));
    const auto entryPointResult1 = NWB::Core::ResolveSpirvEntryPointName(words, s_Main, NWB::Core::ShaderType::Pixel);
    ASSERT_TRUE(entryPointResult1);

    const auto entryPointResult2 = NWB::Core::ResolveSpirvEntryPointName(words, s_Main, NWB::Core::ShaderType::Compute);
    ASSERT_FALSE(entryPointResult2);
    EXPECT_EQ(entryPointResult2.error(), NWB::Core::SpirvEntryPointLookupResult::NotFound);

    const Array<u32, 9u> invalidWords = { 0x07230203u, 0x00010500u, 0u, 16u, 0u, (5u << 16u) | 15u, 4u, 1u, PackSpirvStringWord('m', 'a', 'i', 'n'), };

    const auto entryPointResult3 = NWB::Core::ResolveSpirvEntryPointName(invalidWords, s_Main, NWB::Core::ShaderType::Pixel);
    ASSERT_FALSE(entryPointResult3);
    EXPECT_EQ(entryPointResult3.error(), NWB::Core::SpirvEntryPointLookupResult::InvalidSpirv);
    EXPECT_FALSE(NWB::Core::IsValidSpirvModuleWords(invalidWords));

    const Array<u32, 9u> malformedOtherStageEntryWords = { 0x07230203u, 0x00010500u, 0u, 16u, 0u, (4u << 16u) | 15u, 0u, 1u, PackSpirvStringWord('m', 'a', 'i', 'n'), };

    EXPECT_FALSE(NWB::Core::IsValidSpirvModuleWords(malformedOtherStageEntryWords));
    const auto entryPointResult4 = NWB::Core::ResolveSpirvEntryPointName(malformedOtherStageEntryWords, s_Main, NWB::Core::ShaderType::Pixel);
    ASSERT_FALSE(entryPointResult4);
    EXPECT_EQ(entryPointResult4.error(), NWB::Core::SpirvEntryPointLookupResult::InvalidSpirv);
}

TEST(AssetsGraphics, SpirvExecutionModelsAcceptMeshStageAliasesAndRejectUnknown){
    constexpr Array<AssetsGraphicsFixture::SpirvModelStageCase, 4u> s_Models = {{
        { .stage = Core::ShaderType::Amplification, .model = 5267u }, { .stage = Core::ShaderType::Mesh, .model = 5268u },
        { .stage = Core::ShaderType::Amplification, .model = 5364u }, { .stage = Core::ShaderType::Mesh, .model = 5365u },
    }};
    Array<u32, 10u> words = { 0x07230203u, 0x00010500u, 0u, 16u, 0u, (5u << 16u) | 15u, 0u, 1u, PackSpirvStringWord('m', 'a', 'i', 'n'), 0u, };
    for(const AssetsGraphicsFixture::SpirvModelStageCase& model : s_Models){
        words[6u] = model.model;
        const auto entry = Core::ResolveSpirvEntryPointName(words, s_Main, model.stage);
        ASSERT_TRUE(entry) << model.model;
        EXPECT_EQ(*entry, s_Main);
    }
    words[6u] = Limit<u32>::s_Max;
    const auto unknown = Core::ResolveSpirvEntryPointName(words, s_Main, Core::ShaderType::Pixel);
    ASSERT_FALSE(unknown);
    EXPECT_EQ(unknown.error(), Core::SpirvEntryPointLookupResult::NotFound);
}

TEST(AssetsGraphics, SpirvEntryPointViewsRebindCopiedWordsAndRejectMalformedMatches){
    TestArena testArena;
    constexpr Array<u32, 10u> s_Words = { 0x07230203u, 0x00010500u, 0u, 16u, 0u, (5u << 16u) | 15u, 4u, 1u, PackSpirvStringWord('m', 'a', 'i', 'n'), 0u, };
    constexpr Array<char, 5u> s_RequestedName = { 'm', 'a', 'i', 'n', 'X' };
    const AStringView requestedName(s_RequestedName.data(), 4u);
    NWB::Core::GraphicsVector<u32> copiedWords(testArena.arena);
    AStringView copiedEntryPoint;
    {
        NWB::Core::GraphicsVector<u32> sourceWords(s_Words.begin(), s_Words.end(), testArena.arena);
        AStringView sourceEntryPoint;
        const auto entryPointResult5 = NWB::Core::ResolveSpirvEntryPointName(sourceWords, requestedName, NWB::Core::ShaderType::Pixel);
        ASSERT_TRUE(entryPointResult5);
        sourceEntryPoint = *entryPointResult5;
        EXPECT_EQ(sourceEntryPoint.data(), reinterpret_cast<const char*>(sourceWords.data() + 8u));
        copiedWords = sourceWords;
        const auto entryPointResult6 = NWB::Core::ResolveSpirvEntryPointName(copiedWords, requestedName, NWB::Core::ShaderType::Pixel);
        ASSERT_TRUE(entryPointResult6);
        copiedEntryPoint = *entryPointResult6;
        EXPECT_EQ(copiedEntryPoint.data(), static_cast<const char*>(static_cast<const void*>(copiedWords.data() + 8u)));
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
    EXPECT_EQ(byteEntryPoint.data(), static_cast<const char*>(static_cast<const void*>(unalignedBytes.data() + 1u + 8u * sizeof(u32))));
    EXPECT_EQ(byteEntryPoint, s_Main);
    const auto entryPointResult8 = Core::ResolveSpirvEntryPointName(BinaryByteView{ unalignedModule.data(), unalignedModule.size() - 1u }, requestedName, Core::ShaderType::Pixel);
    ASSERT_FALSE(entryPointResult8);
    EXPECT_EQ(entryPointResult8.error(), Core::SpirvEntryPointLookupResult::InvalidSpirv);
    AppendPOD(unalignedBytes, 0u);
    const auto entryPointResult9 = Core::ResolveSpirvEntryPointName(BinaryByteView{ unalignedBytes.data() + 1u, unalignedBytes.size() - 1u }, requestedName, Core::ShaderType::Pixel);
    ASSERT_FALSE(entryPointResult9);
    EXPECT_EQ(entryPointResult9.error(), Core::SpirvEntryPointLookupResult::InvalidSpirv);

    copiedWords.push_back(0u);
    const auto entryPointResult10 = NWB::Core::ResolveSpirvEntryPointName(copiedWords, requestedName, NWB::Core::ShaderType::Pixel);
    ASSERT_FALSE(entryPointResult10);
    EXPECT_EQ(entryPointResult10.error(), NWB::Core::SpirvEntryPointLookupResult::InvalidSpirv);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

