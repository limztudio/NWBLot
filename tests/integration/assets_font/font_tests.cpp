// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_font/asset.h>
#include <impl/assets_font/binary_payload.h>
#include <impl/assets_font/font_validation.h>
#include <impl/assets_font/source_directory.h>

#include <tests/common/capturing_logger.h>
#include <tests/common/font_fixture.h>
#include <tests/common/test_context.h>

#include <global/binary.h>
#include <global/filesystem.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_font_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Impl;

struct FontTestArenaTag{};
using FontTestArena = TestArena<FontTestArenaTag>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<Font> LoadLatin(FontTestArena& testArena){
    const Path path = Path(testArena.arena, NWB_REPO_ROOT) / "impl" / "assets" / "ui" / "fonts" / "default" / "latin.font";
    Core::Assets::AssetBytes source(testArena.arena);
    Font candidate(testArena.arena, Name("engine/ui/fonts/default/latin/face"));
    auto sourceResult = ReadBundledFontBytes(path, source.get_allocator().arena());
    if(!sourceResult)
        return MakeUnexpected(Failure{});
    source = Move(*sourceResult);
    candidate.setFontBytes(Move(source));
    if(!candidate.validatePayload())
        return MakeUnexpected(Failure{});
    return candidate;
}

[[nodiscard]] static Core::Assets::AssetBytes MakeBinary(
    FontTestArena& testArena,
    const FontBinaryPayload::HeaderBinary& header,
    const Core::Assets::AssetBytes& source
){
    Core::Assets::AssetBytes binary(testArena.arena);
    binary.reserve(sizeof(header) + source.size());
    AppendPOD(binary, header);
    binary.insert(binary.end(), source.begin(), source.end());
    return binary;
}

[[nodiscard]] static u32 ReadBigU32(const u8* bytes){
    return
        (static_cast<u32>(bytes[0u]) << 24u)
        | (static_cast<u32>(bytes[1u]) << 16u)
        | (static_cast<u32>(bytes[2u]) << 8u)
        | static_cast<u32>(bytes[3u])
    ;
}

static void WriteBigU32(u8* bytes, const u32 value)noexcept{
    bytes[0u] = static_cast<u8>(value >> 24u);
    bytes[1u] = static_cast<u8>(value >> 16u);
    bytes[2u] = static_cast<u8>(value >> 8u);
    bytes[3u] = static_cast<u8>(value);
}

[[nodiscard]] static usize FindTableRecord(const Core::Assets::AssetBytes& bytes, const u32 tag){
    const u32 tableCount = (static_cast<u32>(bytes[4u]) << 8u) | bytes[5u];
    for(u32 index = 0u; index < tableCount; ++index){
        const usize record = 12u + static_cast<usize>(index) * 16u;
        if(ReadBigU32(bytes.data() + record) == tag)
            return record;
    }
    return 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AssetsFont, SfntDirectoryRejectsTruncatedRecordsAndUnsupportedTableCounts){
    u8 bytes[28u] = {};
    WriteBigU32(bytes, 0x00010000u);
    bytes[5u] = 1u;
    for(usize size = 0u; size < sizeof(bytes); ++size)
        EXPECT_FALSE(FontSfntDirectory::Read({ bytes, size })) << size;
    ASSERT_TRUE(FontSfntDirectory::Read({ bytes, sizeof(bytes) }));
    bytes[5u] = 0u;
    EXPECT_FALSE(FontSfntDirectory::Read({ bytes, sizeof(bytes) }));
    bytes[4u] = 1u;
    bytes[5u] = 1u;
    EXPECT_FALSE(FontSfntDirectory::Read({ bytes, sizeof(bytes) }));
    bytes[5u] = 0u;
    EXPECT_FALSE(FontSfntDirectory::Read({ bytes, sizeof(bytes) }));
    u8 maximumDirectory[s_FontSfntHeaderBytes + s_FontMaxTableCount * 16u] = {};
    maximumDirectory[4u] = 1u;
    const auto maximumResult = FontSfntDirectory::Read({ maximumDirectory, sizeof(maximumDirectory) });
    ASSERT_TRUE(maximumResult);
    EXPECT_EQ(maximumResult->tableCount(), s_FontMaxTableCount);
}

TEST(AssetsFont, SfntTableViewsCheckBoundsWithoutCopyingOrScanningOtherRecords){
    u8 bytes[48u] = {};
    WriteBigU32(bytes, 0x00010000u);
    bytes[5u] = 2u;
    WriteBigU32(bytes + 12u, s_FontSfntHeadTag);
    WriteBigU32(bytes + 20u, 44u);
    WriteBigU32(bytes + 24u, 4u);
    WriteBigU32(bytes + 28u, s_FontSfntMaxpTag);
    WriteBigU32(bytes + 36u, Limit<u32>::s_Max);
    WriteBigU32(bytes + 40u, 2u);
    WriteBigU32(bytes + 44u, 0x12345678u);
    const auto directoryResult = FontSfntDirectory::Read({ bytes, sizeof(bytes) });
    ASSERT_TRUE(directoryResult);
    const FontSfntDirectory& directory = *directoryResult;
    EXPECT_EQ(directory.signature(), 0x00010000u);
    EXPECT_EQ(directory.directoryBytes(), 44u);
    EXPECT_EQ(directory.tableCount(), 2u);
    const auto tableResult = directory.table(0u);
    ASSERT_TRUE(tableResult);
    EXPECT_EQ(tableResult->tag, s_FontSfntHeadTag);
    EXPECT_EQ(tableResult->offset, 44u);
    EXPECT_EQ(tableResult->bytes.data(), bytes + 44u);
    EXPECT_EQ(tableResult->bytes.size(), 4u);
    const auto valueResult = ReadFontSfntU16(tableResult->bytes, 0u);
    ASSERT_TRUE(valueResult);
    EXPECT_EQ(*valueResult, 0x1234u);
    EXPECT_FALSE(directory.table(1u));
    const auto secondTagResult = directory.tableTag(1u);
    ASSERT_TRUE(secondTagResult);
    EXPECT_EQ(*secondTagResult, s_FontSfntMaxpTag);
    EXPECT_FALSE(directory.table(2u));
    EXPECT_FALSE(directory.tableTag(Limit<usize>::s_Max));
    EXPECT_FALSE(ReadFontSfntU16(tableResult->bytes, 3u));
    EXPECT_FALSE(ReadFontSfntU16(tableResult->bytes, Limit<usize>::s_Max));
}

TEST(AssetsFont, SfntTableBoundsHandleOverflowAndEmptyEndSpansWithoutAddingFontPolicy){
    u8 bytes[32u] = {};
    WriteBigU32(bytes, 0x74746366u); // Signature admission remains with Font validation.
    bytes[5u] = 1u;
    WriteBigU32(bytes + 12u, s_FontSfntHeadTag);
    WriteBigU32(bytes + 20u, 28u);
    WriteBigU32(bytes + 24u, Limit<u32>::s_Max);
    {
        const auto directoryResult = FontSfntDirectory::Read({ bytes, sizeof(bytes) });
        ASSERT_TRUE(directoryResult);
        EXPECT_EQ(directoryResult->signature(), 0x74746366u);
        EXPECT_FALSE(directoryResult->table(0u));
    }
    WriteBigU32(bytes + 24u, 5u);
    {
        const auto directoryResult = FontSfntDirectory::Read({ bytes, sizeof(bytes) });
        ASSERT_TRUE(directoryResult);
        EXPECT_FALSE(directoryResult->table(0u));
    }
    WriteBigU32(bytes + 20u, sizeof(bytes));
    WriteBigU32(bytes + 24u, 0u);
    {
        const auto directoryResult = FontSfntDirectory::Read({ bytes, sizeof(bytes) });
        ASSERT_TRUE(directoryResult);
        const auto emptyTableResult = directoryResult->table(0u);
        ASSERT_TRUE(emptyTableResult);
        EXPECT_TRUE(emptyTableResult->bytes.empty());
        EXPECT_EQ(emptyTableResult->bytes.data(), bytes + sizeof(bytes));
    }
    WriteBigU32(bytes + 20u, 0u); // Directory-overlap admission remains with Font validation.
    WriteBigU32(bytes + 24u, 4u);
    {
        const auto directoryResult = FontSfntDirectory::Read({ bytes, sizeof(bytes) });
        ASSERT_TRUE(directoryResult);
        const auto tableResult = directoryResult->table(0u);
        ASSERT_TRUE(tableResult);
        EXPECT_EQ(tableResult->bytes.data(), bytes);
    }
}

TEST(AssetsFont, PreparedAtlasPixelsAreExcludedFromCookedFontsAndRejectedByRuntimeImport){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    FontTestArena testArena;
    const Path bundledRoot = Path(testArena.arena, NWB_REPO_ROOT) / "impl" / "assets" / "ui" / "fonts" / "default";
    static constexpr AStringView s_BundledNames[] = { "latin.font", "korean.font" };
    static constexpr Name s_Names[] = { Name("engine/ui/fonts/default/latin/face"), Name("engine/ui/fonts/default/korean/face") };
    for(usize index = 0u; index < LengthOf(s_BundledNames); ++index){
        const Path bundledPath = bundledRoot / s_BundledNames[index];
        Core::Assets::AssetBytes source(testArena.arena);
        auto sourceResult = ReadBundledFontBytes(bundledPath, source.get_allocator().arena());
        ASSERT_TRUE(sourceResult);
        source = Move(*sourceResult);
        Font font(testArena.arena, s_Names[index]);
        Core::Assets::AssetBytes sourceCopy(source.begin(), source.end(), testArena.arena);
        font.setFontBytes(Move(sourceCopy));
        ASSERT_TRUE(font.validatePayload());

        Core::Assets::AssetBytes prepared(testArena.arena);
        ASSERT_TRUE(ReadBinaryFile(bundledPath, prepared));
        FontAssetCodec codec;
        Core::Assets::AssetBytes binary(testArena.arena);
        ASSERT_TRUE(codec.serialize(font, binary));
        EXPECT_EQ(binary.size(), sizeof(FontBinaryPayload::HeaderBinary) + source.size());
        EXPECT_LT(binary.size(), prepared.size());
        EXPECT_FALSE(font.loadBinary(prepared));
        EXPECT_EQ(font.fontBytes(), source);
    }
    EXPECT_EQ(logger.errorCount(), LengthOf(s_BundledNames));
}


TEST(AssetsFont, MalformedBinaryPreservesThePreviouslyLoadedFont){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    FontTestArena testArena;
    auto fontResult = LoadLatin(testArena);
    ASSERT_TRUE(fontResult);
    Font& font = *fontResult;
    const Core::Assets::AssetBytes original(font.fontBytes().begin(), font.fontBytes().end(), testArena.arena);
    FontBinaryPayload::HeaderBinary validHeader;
    validHeader.byteCount = original.size();
    for(u32 variant = 0u; variant < 7u; ++variant){
        FontBinaryPayload::HeaderBinary header = validHeader;
        switch(variant){
        case 0u: header.magic = 0u; break;
        case 1u: header.version = 2u; break;
        case 2u: header.faceIndex = 1u; break;
        case 3u: header.reserved = 1u; break;
        case 4u: header.byteCount = 0u; break;
        case 5u: --header.byteCount; break;
        case 6u: ++header.byteCount; break;
        }
        EXPECT_FALSE(font.loadBinary(MakeBinary(testArena, header, original))) << variant;
        EXPECT_EQ(font.fontBytes(), original);
        EXPECT_EQ(font.faceIndex(), 0u);
    }
    Core::Assets::AssetBytes binary = MakeBinary(testArena, validHeader, original);
    binary.resize(sizeof(validHeader) - 1u);
    EXPECT_FALSE(font.loadBinary(binary));
    binary = MakeBinary(testArena, validHeader, original);
    binary.push_back(0u);
    EXPECT_FALSE(font.loadBinary(binary));
    binary = MakeBinary(testArena, validHeader, original);
    binary.pop_back();
    EXPECT_FALSE(font.loadBinary(binary));
    EXPECT_EQ(font.fontBytes(), original);
}

TEST(AssetsFont, OversizedCountIsRejectedBeforeSourceAllocation){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    FontTestArena testArena;
    FontBinaryPayload::HeaderBinary header;
    header.byteCount = static_cast<u64>(s_FontMaxSourceBytes) + 1u;
    Core::Assets::AssetBytes binary(testArena.arena);
    AppendPOD(binary, header);
    Font font(testArena.arena, Name("project/fonts/test"));
    EXPECT_FALSE(font.loadBinary(binary));
    EXPECT_TRUE(font.fontBytes().empty());
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("source size must be 1..33554432 bytes")));
    header.byteCount = Limit<u64>::s_Max;
    binary.clear();
    AppendPOD(binary, header);
    EXPECT_FALSE(font.loadBinary(binary));
    EXPECT_TRUE(font.fontBytes().empty());
}

TEST(AssetsFont, RejectsCollectionsCompressedFontsAndVariableTables){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    FontTestArena testArena;
    auto fontResult = LoadLatin(testArena);
    ASSERT_TRUE(fontResult);
    Font& font = *fontResult;
    static constexpr u32 s_UnsupportedSignatures[] = { 0x74746366u, 0x774f4646u, 0x774f4632u, 0x74727565u };
    for(const u32 signature : s_UnsupportedSignatures){
        Core::Assets::AssetBytes bytes(font.fontBytes().begin(), font.fontBytes().end(), testArena.arena);
        WriteBigU32(bytes.data(), signature);
        EXPECT_FALSE(ValidateFontSource(bytes, 0u));
    }
    static constexpr u32 s_VariableTags[] = { 0x66766172u, 0x43464632u };
    for(const u32 tag : s_VariableTags){
        Core::Assets::AssetBytes bytes(font.fontBytes().begin(), font.fontBytes().end(), testArena.arena);
        WriteBigU32(bytes.data() + 12u, tag);
        EXPECT_FALSE(ValidateFontSource(bytes, 0u));
    }
    EXPECT_FALSE(ValidateFontSource(font.fontBytes(), 1u));
}

TEST(AssetsFont, RejectsMalformedDirectoriesAndNativeFaceMetrics){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    FontTestArena testArena;
    auto fontResult = LoadLatin(testArena);
    ASSERT_TRUE(fontResult);
    Font& font = *fontResult;
    for(u32 variant = 0u; variant < 7u; ++variant){
        Core::Assets::AssetBytes bytes(font.fontBytes().begin(), font.fontBytes().end(), testArena.arena);
        switch(variant){
        case 0u: bytes[4u] = 0u; bytes[5u] = 0u; break;
        case 1u: bytes[4u] = 1u; bytes[5u] = 1u; break;
        case 2u: WriteBigU32(bytes.data() + 20u, Limit<u32>::s_Max); break;
        case 3u: WriteBigU32(bytes.data() + 24u, Limit<u32>::s_Max); break;
        case 4u: WriteBigU32(bytes.data() + 28u, ReadBigU32(bytes.data() + 12u)); break;
        case 5u: {
            const usize head = FindTableRecord(bytes, 0x68656164u);
            ASSERT_NE(head, 0u);
            WriteBigU32(bytes.data() + head, 0x62616421u);
            break;
        }
        case 6u: {
            const usize head = FindTableRecord(bytes, 0x68656164u);
            ASSERT_NE(head, 0u);
            const u32 offset = ReadBigU32(bytes.data() + head + 8u);
            bytes[offset + 18u] = 0u;
            bytes[offset + 19u] = 0u;
            break;
        }
        }
        EXPECT_FALSE(ValidateFontSource(bytes, 0u)) << variant;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

