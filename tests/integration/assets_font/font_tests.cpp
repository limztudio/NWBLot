// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_font/asset.h>

#include <core/alloc/scratch.h>

#include <tests/common/capturing_logger.h>
#include <tests/common/font_fixture.h>
#include <tests/common/test_context.h>

#include <global/binary.h>
#include <global/binary_big_endian.h>
#include <global/filesystem.h>

#include <ft2build.h>
#include FT_FREETYPE_H

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

static constexpr u32 s_CollectionSignature = 0x74746366u;

// SFNT directory holds a big-endian u16 table count at byte 4 followed by 16-byte table records.
static constexpr usize s_SfntTableCountHighByte = 4u;
static constexpr usize s_SfntTableCountLowByte = 5u;
static constexpr usize s_SfntTableRecordBytes = 16u;
static constexpr usize s_SfntRecordOffsetField = 8u;
static constexpr usize s_SfntRecordLengthField = 12u;
static constexpr usize s_OneTableDirectoryBytes = s_FontSfntHeaderBytes + s_SfntTableRecordBytes;
static constexpr usize s_FirstRecordOffsetField = s_FontSfntHeaderBytes + s_SfntRecordOffsetField;
static constexpr usize s_FirstRecordLengthField = s_FontSfntHeaderBytes + s_SfntRecordLengthField;
static constexpr usize s_SecondRecordOffsetField = s_OneTableDirectoryBytes + s_SfntRecordOffsetField;
static constexpr usize s_SecondRecordLengthField = s_OneTableDirectoryBytes + s_SfntRecordLengthField;

// The big-endian u16 unitsPerEm field of the 'head' table.
static constexpr usize s_HeadUnitsPerEmOffset = 18u;
static constexpr usize s_HeadUnitsPerEmEnd = 20u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<Font> LoadLatin(FontTestArena& testArena){
    const Path path = DefaultBundledFontRoot(testArena) / "latin.font";
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

[[nodiscard]] static Core::Assets::AssetBytes MakeBinary(const FontBinaryHeader& header, const Span<const u8> source, FontTestArena& testArena){
    Core::Assets::AssetBytes binary(testArena.arena);
    binary.reserve(sizeof(header) + source.size());
    AppendPOD(binary, header);
    binary.insert(binary.end(), source.begin(), source.end());
    return binary;
}

static void WriteBigU32(const u32 value, const NotNull<u8*> bytes)noexcept{
    bytes.get()[0u] = static_cast<u8>(value >> 24u);
    bytes.get()[1u] = static_cast<u8>(value >> 16u);
    bytes.get()[2u] = static_cast<u8>(value >> 8u);
    bytes.get()[3u] = static_cast<u8>(value);
}

[[nodiscard]] static usize FindTableRecord(const Span<const u8> bytes, const u32 tag)noexcept{
    const u32 tableCount = (static_cast<u32>(bytes[s_SfntTableCountHighByte]) << 8u) | bytes[s_SfntTableCountLowByte];
    for(u32 index = 0u; index < tableCount; ++index){
        const usize record = s_FontSfntHeaderBytes + static_cast<usize>(index) * s_SfntTableRecordBytes;
        if(ReadU32BigEndian(NotNull<const u8*>(bytes.data() + record)) == tag)
            return record;
    }
    return 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AssetsFont, SfntDirectoryRejectsTruncatedRecordsAndUnsupportedTableCounts){
    Array<u8, s_OneTableDirectoryBytes> bytes = {};
    WriteBigU32(s_TrueTypeSignature, MakeNotNull(bytes.data()));
    bytes[s_SfntTableCountLowByte] = 1u;
    FontSfntDirectory directory;
    for(usize size = 0u; size < bytes.size(); ++size)
        EXPECT_FALSE(ReadFontSfntDirectory({ bytes.data(), size }, directory)) << size;
    ASSERT_TRUE(ReadFontSfntDirectory({ bytes.data(), bytes.size() }, directory));
    bytes[s_SfntTableCountLowByte] = 0u;
    EXPECT_FALSE(ReadFontSfntDirectory({ bytes.data(), bytes.size() }, directory));
    bytes[s_SfntTableCountHighByte] = 1u;
    bytes[s_SfntTableCountLowByte] = 1u;
    EXPECT_FALSE(ReadFontSfntDirectory({ bytes.data(), bytes.size() }, directory));
    bytes[s_SfntTableCountLowByte] = 0u;
    EXPECT_FALSE(ReadFontSfntDirectory({ bytes.data(), bytes.size() }, directory));
    Array<u8, s_FontSfntHeaderBytes + s_FontMaxTableCount * s_SfntTableRecordBytes> maximumDirectory = {};
    maximumDirectory[s_SfntTableCountHighByte] = 1u;
    FontSfntDirectory maximumResult;
    ASSERT_TRUE(ReadFontSfntDirectory({ maximumDirectory.data(), maximumDirectory.size() }, maximumResult));
    EXPECT_EQ(maximumResult.tableCount(), s_FontMaxTableCount);
}

TEST(AssetsFont, SfntTableViewsCheckBoundsWithoutCopyingOrScanningOtherRecords){
    // Two records, then the first table's data right after the directory.
    constexpr u32 s_TableDataOffset = 44u;
    constexpr u32 s_TableDataBytes = 4u;
    Array<u8, s_TableDataOffset + s_TableDataBytes> bytes = {};
    WriteBigU32(s_TrueTypeSignature, MakeNotNull(bytes.data()));
    bytes[s_SfntTableCountLowByte] = 2u;
    WriteBigU32(s_FontSfntHeadTag, MakeNotNull(bytes.data() + s_FontSfntHeaderBytes));
    WriteBigU32(s_TableDataOffset, MakeNotNull(bytes.data() + s_FirstRecordOffsetField));
    WriteBigU32(s_TableDataBytes, MakeNotNull(bytes.data() + s_FirstRecordLengthField));
    WriteBigU32(s_FontSfntMaxpTag, MakeNotNull(bytes.data() + s_OneTableDirectoryBytes));
    WriteBigU32(Limit<u32>::s_Max, MakeNotNull(bytes.data() + s_SecondRecordOffsetField));
    WriteBigU32(2u, MakeNotNull(bytes.data() + s_SecondRecordLengthField));
    WriteBigU32(0x12345678u, MakeNotNull(bytes.data() + s_TableDataOffset));
    FontSfntDirectory directory;
    ASSERT_TRUE(ReadFontSfntDirectory({ bytes.data(), bytes.size() }, directory));
    EXPECT_EQ(directory.signature(), s_TrueTypeSignature);
    EXPECT_EQ(directory.directoryBytes(), s_TableDataOffset);
    EXPECT_EQ(directory.tableCount(), 2u);
    FontSfntTable tableResult;
    ASSERT_TRUE(directory.table(0u, tableResult));
    EXPECT_EQ(tableResult.tag, s_FontSfntHeadTag);
    EXPECT_EQ(tableResult.offset, s_TableDataOffset);
    EXPECT_EQ(tableResult.bytes.data(), bytes.data() + s_TableDataOffset);
    EXPECT_EQ(tableResult.bytes.size(), s_TableDataBytes);
    const auto valueResult = ReadU16BigEndian(BinaryByteView{ tableResult.bytes.data(), tableResult.bytes.size() }, 0u);
    ASSERT_TRUE(valueResult);
    EXPECT_EQ(*valueResult, 0x1234u);
    FontSfntTable unusedTable;
    EXPECT_FALSE(directory.table(1u, unusedTable));
    u32 secondTagResult = 0u;
    ASSERT_TRUE(directory.tableTag(1u, secondTagResult));
    EXPECT_EQ(secondTagResult, s_FontSfntMaxpTag);
    EXPECT_FALSE(directory.table(2u, unusedTable));
    u32 unusedTag = 0u;
    EXPECT_FALSE(directory.tableTag(Limit<usize>::s_Max, unusedTag));
    EXPECT_FALSE(ReadU16BigEndian(BinaryByteView{ tableResult.bytes.data(), tableResult.bytes.size() }, 3u));
    EXPECT_FALSE(ReadU16BigEndian(BinaryByteView{ tableResult.bytes.data(), tableResult.bytes.size() }, Limit<usize>::s_Max));
}

TEST(AssetsFont, SfntTableBoundsHandleOverflowAndEmptyEndSpansWithoutAddingFontPolicy){
    Array<u8, 32u> bytes = {};
    WriteBigU32(s_CollectionSignature, MakeNotNull(bytes.data())); // Signature admission remains with Font validation.
    bytes[s_SfntTableCountLowByte] = 1u;
    WriteBigU32(s_FontSfntHeadTag, MakeNotNull(bytes.data() + s_FontSfntHeaderBytes));
    WriteBigU32(s_OneTableDirectoryBytes, MakeNotNull(bytes.data() + s_FirstRecordOffsetField));
    WriteBigU32(Limit<u32>::s_Max, MakeNotNull(bytes.data() + s_FirstRecordLengthField));
    {
        FontSfntDirectory directoryResult;
        ASSERT_TRUE(ReadFontSfntDirectory({ bytes.data(), bytes.size() }, directoryResult));
        EXPECT_EQ(directoryResult.signature(), s_CollectionSignature);
        FontSfntTable unusedTable;
        EXPECT_FALSE(directoryResult.table(0u, unusedTable));
    }
    WriteBigU32(5u, MakeNotNull(bytes.data() + s_FirstRecordLengthField));
    {
        FontSfntDirectory directoryResult;
        ASSERT_TRUE(ReadFontSfntDirectory({ bytes.data(), bytes.size() }, directoryResult));
        FontSfntTable unusedTable;
        EXPECT_FALSE(directoryResult.table(0u, unusedTable));
    }
    WriteBigU32(static_cast<u32>(bytes.size()), MakeNotNull(bytes.data() + s_FirstRecordOffsetField));
    WriteBigU32(0u, MakeNotNull(bytes.data() + s_FirstRecordLengthField));
    {
        FontSfntDirectory directoryResult;
        ASSERT_TRUE(ReadFontSfntDirectory({ bytes.data(), bytes.size() }, directoryResult));
        FontSfntTable emptyTableResult;
        ASSERT_TRUE(directoryResult.table(0u, emptyTableResult));
        EXPECT_TRUE(emptyTableResult.bytes.empty());
        EXPECT_EQ(emptyTableResult.bytes.data(), bytes.data() + bytes.size());
    }
    WriteBigU32(0u, MakeNotNull(bytes.data() + s_FirstRecordOffsetField)); // Directory-overlap admission remains with Font validation.
    WriteBigU32(4u, MakeNotNull(bytes.data() + s_FirstRecordLengthField));
    {
        FontSfntDirectory directoryResult;
        ASSERT_TRUE(ReadFontSfntDirectory({ bytes.data(), bytes.size() }, directoryResult));
        FontSfntTable tableResult;
        ASSERT_TRUE(directoryResult.table(0u, tableResult));
        EXPECT_EQ(tableResult.bytes.data(), bytes.data());
    }
}

TEST(AssetsFont, PreparedAtlasPixelsAreExcludedFromCookedFontsAndRejectedByRuntimeImport){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    FontTestArena testArena;
    const Path bundledRoot = DefaultBundledFontRoot(testArena);
    static constexpr Array<AStringView, 2u> s_BundledNames = { "latin.font", "korean.font" };
    static constexpr Array<Name, 2u> s_Names = { Name("engine/ui/fonts/default/latin/face"), Name("engine/ui/fonts/default/korean/face") };
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
        EXPECT_EQ(binary.size(), sizeof(FontBinaryHeader) + source.size());
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
    FontBinaryHeader validHeader;
    validHeader.byteCount = original.size();
    for(u32 variant = 0u; variant < 7u; ++variant){
        FontBinaryHeader header = validHeader;
        switch(variant){
        case 0u: header.magic = 0u; break;
        case 1u: header.version = 2u; break;
        case 2u: header.faceIndex = 1u; break;
        case 3u: header.reserved = 1u; break;
        case 4u: header.byteCount = 0u; break;
        case 5u: --header.byteCount; break;
        case 6u: ++header.byteCount; break;
        }
        EXPECT_FALSE(font.loadBinary(MakeBinary(header, original, testArena))) << variant;
        EXPECT_EQ(font.fontBytes(), original);
        EXPECT_EQ(font.faceIndex(), 0u);
    }
    Core::Assets::AssetBytes binary = MakeBinary(validHeader, original, testArena);
    binary.resize(sizeof(validHeader) - 1u);
    EXPECT_FALSE(font.loadBinary(binary));
    binary = MakeBinary(validHeader, original, testArena);
    binary.push_back(0u);
    EXPECT_FALSE(font.loadBinary(binary));
    binary = MakeBinary(validHeader, original, testArena);
    binary.pop_back();
    EXPECT_FALSE(font.loadBinary(binary));
    EXPECT_EQ(font.fontBytes(), original);
}

TEST(AssetsFont, OversizedCountIsRejectedBeforeSourceAllocation){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    FontTestArena testArena;
    FontBinaryHeader header;
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
    static constexpr Array<u32, 4u> s_UnsupportedSignatures = { s_CollectionSignature, 0x774f4646u, 0x774f4632u, 0x74727565u };
    for(const u32 signature : s_UnsupportedSignatures){
        Core::Assets::AssetBytes bytes(font.fontBytes().begin(), font.fontBytes().end(), testArena.arena);
        WriteBigU32(signature, MakeNotNull(bytes.data()));
        EXPECT_FALSE(ValidateFontSource(bytes, 0u));
    }
    static constexpr Array<u32, 2u> s_VariableTags = { 0x66766172u, 0x43464632u };
    for(const u32 tag : s_VariableTags){
        Core::Assets::AssetBytes bytes(font.fontBytes().begin(), font.fontBytes().end(), testArena.arena);
        WriteBigU32(tag, MakeNotNull(bytes.data() + s_FontSfntHeaderBytes));
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
        case 0u: bytes[s_SfntTableCountHighByte] = 0u; bytes[s_SfntTableCountLowByte] = 0u; break;
        case 1u: bytes[s_SfntTableCountHighByte] = 1u; bytes[s_SfntTableCountLowByte] = 1u; break;
        case 2u: WriteBigU32(Limit<u32>::s_Max, MakeNotNull(bytes.data() + s_FirstRecordOffsetField)); break;
        case 3u: WriteBigU32(Limit<u32>::s_Max, MakeNotNull(bytes.data() + s_FirstRecordLengthField)); break;
        case 4u: WriteBigU32(ReadU32BigEndian(NotNull<const u8*>(bytes.data() + s_FontSfntHeaderBytes)), MakeNotNull(bytes.data() + s_OneTableDirectoryBytes)); break;
        case 5u: {
            const usize head = FindTableRecord(bytes, s_FontSfntHeadTag);
            ASSERT_NE(head, 0u);
            WriteBigU32(0x62616421u, MakeNotNull(bytes.data() + head));
            break;
        }
        case 6u: {
            const usize head = FindTableRecord(bytes, s_FontSfntHeadTag);
            ASSERT_NE(head, 0u);
            const u32 offset = ReadU32BigEndian(NotNull<const u8*>(bytes.data() + head + s_SfntRecordOffsetField));
            bytes[offset + s_HeadUnitsPerEmOffset] = 0u;
            bytes[offset + s_HeadUnitsPerEmOffset + 1u] = 0u;
            break;
        }
        }
        EXPECT_FALSE(ValidateFontSource(bytes, 0u)) << variant;
    }
}

TEST(AssetsFont, NativeFaceFailurePreservesErrorCodeAndReclaimsValidationAllocations){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    FontTestArena testArena;
    auto fontResult = LoadLatin(testArena);
    ASSERT_TRUE(fontResult);
    const Font& font = *fontResult;
    Core::Assets::AssetBytes bytes(font.fontBytes().begin(), font.fontBytes().end(), testArena.arena);
    const usize head = FindTableRecord(bytes, s_FontSfntHeadTag);
    ASSERT_NE(head, 0u);
    const u32 offset = ReadU32BigEndian(NotNull<const u8*>(bytes.data() + head + s_SfntRecordOffsetField));
    ASSERT_LE(static_cast<usize>(offset) + s_HeadUnitsPerEmEnd, bytes.size());
    bytes[offset + s_HeadUnitsPerEmOffset] = 0u;
    bytes[offset + s_HeadUnitsPerEmOffset + 1u] = 0u;

    FT_Library library = nullptr;
    ASSERT_EQ(FT_Init_FreeType(&library), 0);
    FT_Face nativeFace = nullptr;
    const FT_Error expectedNativeError = FT_New_Memory_Face(library, bytes.data(), static_cast<FT_Long>(bytes.size()), 0, &nativeFace);
    const FT_Error faceClosed = nativeFace ? FT_Done_Face(nativeFace) : 0;
    const FT_Error libraryClosed = FT_Done_FreeType(library);
    ASSERT_EQ(faceClosed, 0);
    ASSERT_EQ(libraryClosed, 0);
    ASSERT_NE(expectedNativeError, 0);

    const u64 baselineUsedBytes = testArena.arena.memoryStats().usedBytes;
    EXPECT_FALSE(ValidateFontSource(bytes, 0u));
    EXPECT_EQ(testArena.arena.memoryStats().usedBytes, baselineUsedBytes);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("native_operation=FT_New_Memory_Face native_domain=FreeType")));
    constexpr usize s_ExpectedCodeCapacity = 64u;
    Array<char, s_ExpectedCodeCapacity> expectedCode = {};
    CopyFixedBuffer(expectedCode, "native_code=");
    if(expectedNativeError < 0)
        AppendFixedBuffer(expectedCode, "-");
    const i64 signedCode = expectedNativeError;
    AppendUnsignedToFixedBuffer(expectedCode, static_cast<u64>(signedCode < 0 ? -signedCode : signedCode));
    constexpr Name s_DiagnosticScratch("tests/assets_font/native_validation_diagnostic");
    Core::Alloc::ScratchArena scratchArena(s_DiagnosticScratch);
    const auto expectedDiagnostic = ConvertString(scratchArena, FixedBufferView(expectedCode));
    EXPECT_TRUE(logger.sawErrorContaining(expectedDiagnostic));
    EXPECT_EQ(logger.errorCount(), 1u);
    EXPECT_TRUE(ValidateFontSource(font.fontBytes(), 0u));
    EXPECT_EQ(testArena.arena.memoryStats().usedBytes, baselineUsedBytes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

