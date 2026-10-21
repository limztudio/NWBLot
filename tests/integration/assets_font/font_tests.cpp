// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_font/asset.h>
#include <impl/assets_font/binary_payload.h>
#include <impl/assets_font/font_validation.h>
#include <impl/assets_font/source_directory.h>

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

static constexpr u32 s_TrueTypeSignature = 0x00010000u;
static constexpr u32 s_CollectionSignature = 0x74746366u;

// SFNT directory layout: table count is a big-endian u16 at byte 4; each 16-byte table record holds tag, checksum, offset and length.
static constexpr usize s_SfntTableCountHighByte = 4u;
static constexpr usize s_SfntTableCountLowByte = 5u;
static constexpr usize s_SfntTableRecordBytes = 16u;
static constexpr usize s_SfntRecordOffsetField = 8u;
static constexpr usize s_SfntRecordLengthField = 12u;
static constexpr usize s_OneTableDirectoryBytes = s_FontSfntHeaderBytes + s_SfntTableRecordBytes;
static constexpr usize s_FirstRecordOffsetField = s_FontSfntHeaderBytes + s_SfntRecordOffsetField;
static constexpr usize s_FirstRecordLengthField = s_FontSfntHeaderBytes + s_SfntRecordLengthField;
static constexpr usize s_SecondRecordTagOffset = s_OneTableDirectoryBytes;
static constexpr usize s_SecondRecordOffsetField = s_OneTableDirectoryBytes + s_SfntRecordOffsetField;
static constexpr usize s_SecondRecordLengthField = s_OneTableDirectoryBytes + s_SfntRecordLengthField;

// The big-endian u16 unitsPerEm field of the 'head' table.
static constexpr usize s_HeadUnitsPerEmOffset = 18u;
static constexpr usize s_HeadUnitsPerEmEnd = 20u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Path BundledFontRoot(FontTestArena& testArena){
    return Path(testArena.arena, NWB_REPO_ROOT) / "impl" / "assets" / "ui" / "fonts" / "default";
}

[[nodiscard]] static Expected<Font> LoadLatin(FontTestArena& testArena){
    const Path path = BundledFontRoot(testArena) / "latin.font";
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

[[nodiscard]] static Core::Assets::AssetBytes MakeBinary(FontTestArena& testArena, const FontBinaryPayload::HeaderBinary& header, const Core::Assets::AssetBytes& source){
    Core::Assets::AssetBytes binary(testArena.arena);
    binary.reserve(sizeof(header) + source.size());
    AppendPOD(binary, header);
    binary.insert(binary.end(), source.begin(), source.end());
    return binary;
}

static void WriteBigU32(const NotNull<u8*> bytes, const u32 value)noexcept{
    bytes.get()[0u] = static_cast<u8>(value >> 24u);
    bytes.get()[1u] = static_cast<u8>(value >> 16u);
    bytes.get()[2u] = static_cast<u8>(value >> 8u);
    bytes.get()[3u] = static_cast<u8>(value);
}

[[nodiscard]] static usize FindTableRecord(const Core::Assets::AssetBytes& bytes, const u32 tag)noexcept{
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
    u8 bytes[s_OneTableDirectoryBytes] = {};
    WriteBigU32(MakeNotNull(bytes), s_TrueTypeSignature);
    bytes[s_SfntTableCountLowByte] = 1u;
    for(usize size = 0u; size < sizeof(bytes); ++size)
        EXPECT_FALSE(FontSfntDirectory::Read({ bytes, size })) << size;
    ASSERT_TRUE(FontSfntDirectory::Read({ bytes, sizeof(bytes) }));
    bytes[s_SfntTableCountLowByte] = 0u;
    EXPECT_FALSE(FontSfntDirectory::Read({ bytes, sizeof(bytes) }));
    bytes[s_SfntTableCountHighByte] = 1u;
    bytes[s_SfntTableCountLowByte] = 1u;
    EXPECT_FALSE(FontSfntDirectory::Read({ bytes, sizeof(bytes) }));
    bytes[s_SfntTableCountLowByte] = 0u;
    EXPECT_FALSE(FontSfntDirectory::Read({ bytes, sizeof(bytes) }));
    u8 maximumDirectory[s_FontSfntHeaderBytes + s_FontMaxTableCount * s_SfntTableRecordBytes] = {};
    maximumDirectory[s_SfntTableCountHighByte] = 1u;
    const auto maximumResult = FontSfntDirectory::Read({ maximumDirectory, sizeof(maximumDirectory) });
    ASSERT_TRUE(maximumResult);
    EXPECT_EQ(maximumResult->tableCount(), s_FontMaxTableCount);
}

TEST(AssetsFont, SfntTableViewsCheckBoundsWithoutCopyingOrScanningOtherRecords){
    // Two records, then the first table's data right after the directory.
    constexpr u32 s_TableDataOffset = 44u;
    constexpr u32 s_TableDataBytes = 4u;
    u8 bytes[s_TableDataOffset + s_TableDataBytes] = {};
    WriteBigU32(MakeNotNull(bytes), s_TrueTypeSignature);
    bytes[s_SfntTableCountLowByte] = 2u;
    WriteBigU32(MakeNotNull(bytes + s_FontSfntHeaderBytes), s_FontSfntHeadTag);
    WriteBigU32(MakeNotNull(bytes + s_FirstRecordOffsetField), s_TableDataOffset);
    WriteBigU32(MakeNotNull(bytes + s_FirstRecordLengthField), s_TableDataBytes);
    WriteBigU32(MakeNotNull(bytes + s_SecondRecordTagOffset), s_FontSfntMaxpTag);
    WriteBigU32(MakeNotNull(bytes + s_SecondRecordOffsetField), Limit<u32>::s_Max);
    WriteBigU32(MakeNotNull(bytes + s_SecondRecordLengthField), 2u);
    WriteBigU32(MakeNotNull(bytes + s_TableDataOffset), 0x12345678u);
    const auto directoryResult = FontSfntDirectory::Read({ bytes, sizeof(bytes) });
    ASSERT_TRUE(directoryResult);
    const FontSfntDirectory& directory = *directoryResult;
    EXPECT_EQ(directory.signature(), s_TrueTypeSignature);
    EXPECT_EQ(directory.directoryBytes(), s_TableDataOffset);
    EXPECT_EQ(directory.tableCount(), 2u);
    const auto tableResult = directory.table(0u);
    ASSERT_TRUE(tableResult);
    EXPECT_EQ(tableResult->tag, s_FontSfntHeadTag);
    EXPECT_EQ(tableResult->offset, s_TableDataOffset);
    EXPECT_EQ(tableResult->bytes.data(), bytes + s_TableDataOffset);
    EXPECT_EQ(tableResult->bytes.size(), s_TableDataBytes);
    const auto valueResult = ReadU16BigEndian(tableResult->bytes, 0u);
    ASSERT_TRUE(valueResult);
    EXPECT_EQ(*valueResult, 0x1234u);
    EXPECT_FALSE(directory.table(1u));
    const auto secondTagResult = directory.tableTag(1u);
    ASSERT_TRUE(secondTagResult);
    EXPECT_EQ(*secondTagResult, s_FontSfntMaxpTag);
    EXPECT_FALSE(directory.table(2u));
    EXPECT_FALSE(directory.tableTag(Limit<usize>::s_Max));
    EXPECT_FALSE(ReadU16BigEndian(tableResult->bytes, 3u));
    EXPECT_FALSE(ReadU16BigEndian(tableResult->bytes, Limit<usize>::s_Max));
}

TEST(AssetsFont, SfntTableBoundsHandleOverflowAndEmptyEndSpansWithoutAddingFontPolicy){
    u8 bytes[32u] = {};
    WriteBigU32(MakeNotNull(bytes), s_CollectionSignature); // Signature admission remains with Font validation.
    bytes[s_SfntTableCountLowByte] = 1u;
    WriteBigU32(MakeNotNull(bytes + s_FontSfntHeaderBytes), s_FontSfntHeadTag);
    WriteBigU32(MakeNotNull(bytes + s_FirstRecordOffsetField), s_OneTableDirectoryBytes);
    WriteBigU32(MakeNotNull(bytes + s_FirstRecordLengthField), Limit<u32>::s_Max);
    {
        const auto directoryResult = FontSfntDirectory::Read({ bytes, sizeof(bytes) });
        ASSERT_TRUE(directoryResult);
        EXPECT_EQ(directoryResult->signature(), s_CollectionSignature);
        EXPECT_FALSE(directoryResult->table(0u));
    }
    WriteBigU32(MakeNotNull(bytes + s_FirstRecordLengthField), 5u);
    {
        const auto directoryResult = FontSfntDirectory::Read({ bytes, sizeof(bytes) });
        ASSERT_TRUE(directoryResult);
        EXPECT_FALSE(directoryResult->table(0u));
    }
    WriteBigU32(MakeNotNull(bytes + s_FirstRecordOffsetField), sizeof(bytes));
    WriteBigU32(MakeNotNull(bytes + s_FirstRecordLengthField), 0u);
    {
        const auto directoryResult = FontSfntDirectory::Read({ bytes, sizeof(bytes) });
        ASSERT_TRUE(directoryResult);
        const auto emptyTableResult = directoryResult->table(0u);
        ASSERT_TRUE(emptyTableResult);
        EXPECT_TRUE(emptyTableResult->bytes.empty());
        EXPECT_EQ(emptyTableResult->bytes.data(), bytes + sizeof(bytes));
    }
    WriteBigU32(MakeNotNull(bytes + s_FirstRecordOffsetField), 0u); // Directory-overlap admission remains with Font validation.
    WriteBigU32(MakeNotNull(bytes + s_FirstRecordLengthField), 4u);
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
    const Path bundledRoot = BundledFontRoot(testArena);
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
    static constexpr u32 s_UnsupportedSignatures[] = { s_CollectionSignature, 0x774f4646u, 0x774f4632u, 0x74727565u };
    for(const u32 signature : s_UnsupportedSignatures){
        Core::Assets::AssetBytes bytes(font.fontBytes().begin(), font.fontBytes().end(), testArena.arena);
        WriteBigU32(MakeNotNull(bytes.data()), signature);
        EXPECT_FALSE(ValidateFontSource(bytes, 0u));
    }
    static constexpr u32 s_VariableTags[] = { 0x66766172u, 0x43464632u };
    for(const u32 tag : s_VariableTags){
        Core::Assets::AssetBytes bytes(font.fontBytes().begin(), font.fontBytes().end(), testArena.arena);
        WriteBigU32(MakeNotNull(bytes.data() + s_FontSfntHeaderBytes), tag);
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
        case 2u: WriteBigU32(MakeNotNull(bytes.data() + s_FirstRecordOffsetField), Limit<u32>::s_Max); break;
        case 3u: WriteBigU32(MakeNotNull(bytes.data() + s_FirstRecordLengthField), Limit<u32>::s_Max); break;
        case 4u: WriteBigU32(MakeNotNull(bytes.data() + s_SecondRecordTagOffset), ReadU32BigEndian(NotNull<const u8*>(bytes.data() + s_FontSfntHeaderBytes))); break;
        case 5u: {
            const usize head = FindTableRecord(bytes, s_FontSfntHeadTag);
            ASSERT_NE(head, 0u);
            WriteBigU32(MakeNotNull(bytes.data() + head), 0x62616421u);
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
    char expectedCode[s_ExpectedCodeCapacity] = {};
    CopyFixedBuffer(expectedCode, "native_code=");
    if(expectedNativeError < 0)
        AppendFixedBuffer(expectedCode, "-");
    const i64 signedCode = expectedNativeError;
    AppendUnsignedToFixedBuffer(expectedCode, static_cast<u64>(signedCode < 0 ? -signedCode : signedCode));
    constexpr Name s_DiagnosticScratch("tests/assets_font/native_validation_diagnostic");
    Core::Alloc::ScratchArena scratchArena(s_DiagnosticScratch);
    const auto expectedDiagnostic = StringConvert(scratchArena, AStringView(expectedCode));
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

