// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/capturing_logger.h>
#include <tests/common/font_fixture.h>
#include <tests/common/test_context.h>
#include <tests/integration/assets_font/name_cases.adhd.h>

#include <impl/assets_font/asset.h>

#include <core/alloc/scratch.h>

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
    Font candidate(testArena.arena, Name(NWB::Tests::FontCases::s_FontCaseEngineUiFontsDefaultLatinFace));
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
    for(usize size = 0u; size < bytes.size(); ++size)
        EXPECT_FALSE(ReadFontSfntDirectory({ bytes.data(), size })) << size;
    ASSERT_TRUE(ReadFontSfntDirectory({ bytes.data(), bytes.size() }));
    bytes[s_SfntTableCountLowByte] = 0u;
    EXPECT_FALSE(ReadFontSfntDirectory({ bytes.data(), bytes.size() }));
    bytes[s_SfntTableCountHighByte] = 1u;
    bytes[s_SfntTableCountLowByte] = 1u;
    EXPECT_FALSE(ReadFontSfntDirectory({ bytes.data(), bytes.size() }));
    bytes[s_SfntTableCountLowByte] = 0u;
    EXPECT_FALSE(ReadFontSfntDirectory({ bytes.data(), bytes.size() }));
    Array<u8, s_FontSfntHeaderBytes + s_FontMaxTableCount * s_SfntTableRecordBytes> maximumDirectory = {};
    maximumDirectory[s_SfntTableCountHighByte] = 1u;
    const Expected<FontSfntDirectory> maximumResult = ReadFontSfntDirectory({ maximumDirectory.data(), maximumDirectory.size() });
    ASSERT_TRUE(maximumResult);
    EXPECT_EQ(maximumResult->tableCount(), s_FontMaxTableCount);
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
    const Expected<FontSfntDirectory> directory = ReadFontSfntDirectory({ bytes.data(), bytes.size() });
    ASSERT_TRUE(directory);
    EXPECT_EQ(directory->signature(), s_TrueTypeSignature);
    EXPECT_EQ(directory->directoryBytes(), s_TableDataOffset);
    EXPECT_EQ(directory->tableCount(), 2u);
    const Expected<FontSfntTable> tableResult = directory->table(0u);
    ASSERT_TRUE(tableResult);
    EXPECT_EQ(tableResult->tag, s_FontSfntHeadTag);
    EXPECT_EQ(tableResult->offset, s_TableDataOffset);
    EXPECT_EQ(tableResult->bytes.data(), bytes.data() + s_TableDataOffset);
    EXPECT_EQ(tableResult->bytes.size(), s_TableDataBytes);
    const auto valueResult = ReadU16BigEndian(BinaryByteView{ tableResult->bytes.data(), tableResult->bytes.size() }, 0u);
    ASSERT_TRUE(valueResult);
    EXPECT_EQ(*valueResult, 0x1234u);
    EXPECT_FALSE(directory->table(1u));
    const Expected<u32> secondTagResult = directory->tableTag(1u);
    ASSERT_TRUE(secondTagResult);
    EXPECT_EQ(*secondTagResult, s_FontSfntMaxpTag);
    EXPECT_FALSE(directory->table(2u));
    EXPECT_FALSE(directory->tableTag(Limit<usize>::s_Max));
    EXPECT_FALSE(ReadU16BigEndian(BinaryByteView{ tableResult->bytes.data(), tableResult->bytes.size() }, 3u));
    EXPECT_FALSE(ReadU16BigEndian(BinaryByteView{ tableResult->bytes.data(), tableResult->bytes.size() }, Limit<usize>::s_Max));
}

TEST(AssetsFont, SfntTableBoundsHandleOverflowAndEmptyEndSpansWithoutAddingFontPolicy){
    Array<u8, 32u> bytes = {};
    WriteBigU32(s_CollectionSignature, MakeNotNull(bytes.data())); // Signature admission remains with Font validation.
    bytes[s_SfntTableCountLowByte] = 1u;
    WriteBigU32(s_FontSfntHeadTag, MakeNotNull(bytes.data() + s_FontSfntHeaderBytes));
    WriteBigU32(s_OneTableDirectoryBytes, MakeNotNull(bytes.data() + s_FirstRecordOffsetField));
    WriteBigU32(Limit<u32>::s_Max, MakeNotNull(bytes.data() + s_FirstRecordLengthField));
    {
        const Expected<FontSfntDirectory> directoryResult = ReadFontSfntDirectory({ bytes.data(), bytes.size() });
        ASSERT_TRUE(directoryResult);
        EXPECT_EQ(directoryResult->signature(), s_CollectionSignature);
        EXPECT_FALSE(directoryResult->table(0u));
    }
    WriteBigU32(5u, MakeNotNull(bytes.data() + s_FirstRecordLengthField));
    {
        const Expected<FontSfntDirectory> directoryResult = ReadFontSfntDirectory({ bytes.data(), bytes.size() });
        ASSERT_TRUE(directoryResult);
        EXPECT_FALSE(directoryResult->table(0u));
    }
    WriteBigU32(static_cast<u32>(bytes.size()), MakeNotNull(bytes.data() + s_FirstRecordOffsetField));
    WriteBigU32(0u, MakeNotNull(bytes.data() + s_FirstRecordLengthField));
    {
        const Expected<FontSfntDirectory> directoryResult = ReadFontSfntDirectory({ bytes.data(), bytes.size() });
        ASSERT_TRUE(directoryResult);
        const Expected<FontSfntTable> emptyTableResult = directoryResult->table(0u);
        ASSERT_TRUE(emptyTableResult);
        EXPECT_TRUE(emptyTableResult->bytes.empty());
        EXPECT_EQ(emptyTableResult->bytes.data(), bytes.data() + bytes.size());
    }
    WriteBigU32(0u, MakeNotNull(bytes.data() + s_FirstRecordOffsetField)); // Directory-overlap admission remains with Font validation.
    WriteBigU32(4u, MakeNotNull(bytes.data() + s_FirstRecordLengthField));
    {
        const Expected<FontSfntDirectory> directoryResult = ReadFontSfntDirectory({ bytes.data(), bytes.size() });
        ASSERT_TRUE(directoryResult);
        const Expected<FontSfntTable> tableResult = directoryResult->table(0u);
        ASSERT_TRUE(tableResult);
        EXPECT_EQ(tableResult->bytes.data(), bytes.data());
    }
}

TEST(AssetsFont, PreparedAtlasPixelsAreExcludedFromCookedFontsAndRejectedByRuntimeImport){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    FontTestArena testArena;
    const Path bundledRoot = DefaultBundledFontRoot(testArena);
    static constexpr Array<AStringView, 2u> s_BundledNames = { "latin.font", "korean.font" };
    static constexpr Array<Name, 2u> s_Names = { Name(NWB::Tests::FontCases::s_FontCaseEngineUiFontsDefaultLatinFace), Name(NWB::Tests::FontCases::s_FontCaseEngineUiFontsDefaultKoreanFace) };
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
    Font font(testArena.arena, Name(NWB::Tests::FontCases::s_FontCaseProjectFontsTest));
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
    constexpr Name s_DiagnosticScratch = NWB::Tests::FontCases::s_OneOffNameTestsAssetsFontNativeValidationDiagnostic;
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/capturing_logger.h>
#include <tests/common/font_fixture.h>
#include <tests/common/test_context.h>
#include <tests/integration/assets_font/name_cases.adhd.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_prepared_source_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Impl;

struct PreparedSourceTestArenaTag{};
using SourceTestArena = TestArena<PreparedSourceTestArenaTag>;

static constexpr usize s_HeaderBytes = 56u;
static constexpr usize s_GroupHeaderBytes = 48u;
static constexpr usize s_GroupCount = 4u;
static constexpr usize s_FontBytes = 7u;
static constexpr usize s_FontOffset = s_HeaderBytes + s_GroupCount * s_GroupHeaderBytes;
static constexpr usize s_PixelOffset = s_FontOffset + s_FontBytes;
static constexpr usize s_GroupHeightField = 4u;
static constexpr usize s_GroupChannelsField = 8u;
static constexpr usize s_GroupImageBytesField = 12u;
static constexpr u32 s_MaxImageSide = 2048u;
static constexpr u32 s_MaxChannelCount = 4u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SourceFixture{
    Core::Assets::AssetVector<PreparedFontImageGroup> groups;
    Array<PreparedFontImageView, s_GroupCount> views = {};
    Array<u8, s_FontBytes> sfnt = { 0u, 1u, 4u, 0u, 127u, 128u, 255u };


public:
    explicit SourceFixture(Core::Assets::AssetArena& arena)
        : groups(arena)
    {
        groups.reserve(s_GroupCount);
        for(u32 index = 0u; index < s_GroupCount; ++index){
            groups.emplace_back(arena);
            PreparedFontImageGroup& group = groups.back();
            group.width = 3u;
            group.height = 5u;
            group.channelCount = index + 1u;
            group.pixels.resize(group.width * group.height * group.channelCount);
            for(usize byte = 0u; byte < group.pixels.size(); ++byte)
                group.pixels[byte] = static_cast<u8>(index * 53u + byte * 17u);
            group.sha256 = ComputeSha256({ group.pixels.data(), group.pixels.size() });
            views[index] = { group.width, group.height, group.channelCount, { group.pixels.data(), group.pixels.size() } };
        }
    }
};

struct MalformedField{
    AStringView filename;
    usize offset = 0u;
    u32 value = 0u;
    b8 rejectsBeforeDirectoryAllocation = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<Path> WriteCase(const AStringView filename, const Span<const u8> binary, SourceTestArena& testArena){
    Path candidate = Path(testArena.arena, NWB_REPO_ROOT) / "__artifacts" / "font_source_tests" / filename;
    if(!EnsureDirectories(candidate.parentPath()) || !WriteBinaryFile(candidate, binary))
        return MakeUnexpected(Failure{});
    return candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(PreparedFontSource, ImageCorruptionRejectsFullReadWhileFontOnlySkipsCompactPlanes){
    using namespace __hidden_prepared_source_tests;
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SourceTestArena testArena;
    SourceFixture fixture(testArena.arena);
    auto binaryResult = SerializePreparedFontSource({ fixture.sfnt.data(), fixture.sfnt.size() }, 0u, fixture.views, testArena.arena);
    ASSERT_TRUE(binaryResult);
    Core::Assets::AssetBytes& binary = *binaryResult;
    ASSERT_EQ(binary.size(), s_PixelOffset + 150u);
    auto pathResult = WriteCase("image_integrity_control.font", binary, testArena);
    ASSERT_TRUE(pathResult);
    Path path = Move(*pathResult);
    PreparedFontSource original(testArena.arena);
    auto originalResult = ReadPreparedFontSource(path, true, original.fontBytes.get_allocator().arena());
    ASSERT_TRUE(originalResult);
    original = Move(*originalResult);
    ASSERT_EQ(original.groups.size(), s_GroupCount);
    static constexpr Array<AStringView, 5> s_Filenames = { "image_r.font", "image_rg.font", "image_rgb.font", "image_rgba.font", "image_hash.font" };
    usize imageOffset = s_PixelOffset;
    for(usize index = 0u; index < s_Filenames.size(); ++index){
        Core::Assets::AssetBytes corrupted(binary.begin(), binary.end(), testArena.arena);
        const usize corruptionOffset = index < s_GroupCount ? imageOffset : s_HeaderBytes + 16u;
        corrupted[corruptionOffset] ^= 0x80u;
        auto corruptedPathResult = WriteCase(s_Filenames[index], corrupted, testArena);
        ASSERT_TRUE(corruptedPathResult);
        path = Move(*corruptedPathResult);
        EXPECT_FALSE(ReadPreparedFontSource(path, true, testArena.arena)) << index;
        PreparedFontSource fontOnly(testArena.arena);
        auto fontOnlyResult = ReadPreparedFontSource(path, false, fontOnly.fontBytes.get_allocator().arena());
        ASSERT_TRUE(fontOnlyResult) << index;
        fontOnly = Move(*fontOnlyResult);
        EXPECT_EQ(fontOnly.fontBytes, original.fontBytes);
        EXPECT_TRUE(fontOnly.fontSha256 == original.fontSha256);
        ASSERT_EQ(fontOnly.groups.size(), s_GroupCount);
        for(usize group = 0u; group < s_GroupCount; ++group){
            EXPECT_TRUE(fontOnly.groups[group].pixels.empty());
            EXPECT_EQ(fontOnly.groups[group].width, 3u);
            EXPECT_EQ(fontOnly.groups[group].height, 5u);
            EXPECT_EQ(fontOnly.groups[group].channelCount, group + 1u);
        }
        if(index < s_GroupCount)
            imageOffset += fixture.groups[index].pixels.size();
    }
    EXPECT_EQ(logger.errorCount(), s_Filenames.size());
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("image content hash differs")));
}

TEST(PreparedFontSource, SfntByteAndDigestCorruptionRejectBothReadModes){
    using namespace __hidden_prepared_source_tests;
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SourceTestArena testArena;
    SourceFixture fixture(testArena.arena);
    auto binaryResult = SerializePreparedFontSource({ fixture.sfnt.data(), fixture.sfnt.size() }, 0u, fixture.views, testArena.arena);
    ASSERT_TRUE(binaryResult);
    Core::Assets::AssetBytes& binary = *binaryResult;
    auto pathResult = WriteCase("font_integrity_control.font", binary, testArena);
    ASSERT_TRUE(pathResult);
    Path path = Move(*pathResult);
    ASSERT_TRUE(ReadPreparedFontSource(path, true, testArena.arena));
    static constexpr Array<AStringView, 2> s_Filenames = { "font_corrupt_bytes.font", "font_corrupt_hash.font" };
    static constexpr Array<usize, 2> s_Offsets = { s_FontOffset, 24u };
    for(usize index = 0u; index < s_Filenames.size(); ++index){
        Core::Assets::AssetBytes corrupted(binary.begin(), binary.end(), testArena.arena);
        corrupted[s_Offsets[index]] ^= 0x40u;
        auto corruptedPathResult = WriteCase(s_Filenames[index], corrupted, testArena);
        ASSERT_TRUE(corruptedPathResult);
        path = Move(*corruptedPathResult);
        for(const b8 includePixels : { false, true })
            EXPECT_FALSE(ReadPreparedFontSource(path, includePixels, testArena.arena));
    }
    EXPECT_EQ(logger.errorCount(), 4u);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("SFNT content hash differs")));
}

TEST(PreparedFontSource, MalformedDirectoryCountsAndOverflowRejectBeforePayloadAllocation){
    using namespace __hidden_prepared_source_tests;
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SourceTestArena testArena;
    Core::Alloc::GlobalArena outputArena{Name(NWB::Tests::FontCases::s_FontCaseTestsIntegrationAssetsFontPreparedOutput)};
    SourceFixture fixture(testArena.arena);
    auto binaryResult = SerializePreparedFontSource({ fixture.sfnt.data(), fixture.sfnt.size() }, 0u, fixture.views, testArena.arena);
    ASSERT_TRUE(binaryResult);
    Core::Assets::AssetBytes& binary = *binaryResult;
    auto pathResult = WriteCase("directory_control.font", binary, testArena);
    ASSERT_TRUE(pathResult);
    Path path = Move(*pathResult);
    ASSERT_TRUE(ReadPreparedFontSource(path, true, outputArena));
    static constexpr Array<MalformedField, 27> s_Fields = {
        MalformedField{ "magic.font", 0u, 0u, true },
        MalformedField{ "retired_fon1.font", 0u, 0x464f4e31u, true },
        MalformedField{ "old_version.font", 4u, 0u, true },
        MalformedField{ "future_version.font", 4u, 2u, true },
        MalformedField{ "face_index.font", 8u, 1u, true },
        MalformedField{ "reserved.font", 20u, 1u, true },
        MalformedField{ "font_zero.font", 12u, 0u, true },
        MalformedField{ "font_limit.font", 12u, s_FontMaxSourceBytes + 1u, true },
        MalformedField{ "font_overflow.font", 12u, Limit<u32>::s_Max, true },
        MalformedField{ "font_length.font", 12u, s_FontBytes + 1u, false },
        MalformedField{ "groups_zero.font", 16u, 0u, true },
        MalformedField{ "groups_limit.font", 16u, 9u, true },
        MalformedField{ "groups_overflow.font", 16u, Limit<u32>::s_Max, true },
        MalformedField{ "missing_group_header.font", 16u, 5u, false },
        MalformedField{ "width_zero.font", s_HeaderBytes, 0u, false },
        MalformedField{ "width_limit.font", s_HeaderBytes, s_MaxImageSide + 1u, false },
        MalformedField{ "width_overflow.font", s_HeaderBytes, Limit<u32>::s_Max, false },
        MalformedField{ "height_zero.font", s_HeaderBytes + s_GroupHeightField, 0u, false },
        MalformedField{ "height_limit.font", s_HeaderBytes + s_GroupHeightField, s_MaxImageSide + 1u, false },
        MalformedField{ "height_overflow.font", s_HeaderBytes + s_GroupHeightField, Limit<u32>::s_Max, false },
        MalformedField{ "channels_zero.font", s_HeaderBytes + s_GroupChannelsField, 0u, false },
        MalformedField{ "channels_limit.font", s_HeaderBytes + s_GroupChannelsField, s_MaxChannelCount + 1u, false },
        MalformedField{ "channels_overflow.font", s_HeaderBytes + s_GroupChannelsField, Limit<u32>::s_Max, false },
        MalformedField{ "image_bytes_zero.font", s_HeaderBytes + s_GroupImageBytesField, 0u, false },
        MalformedField{ "image_bytes_short.font", s_HeaderBytes + s_GroupImageBytesField, 14u, false },
        MalformedField{ "image_bytes_long.font", s_HeaderBytes + s_GroupImageBytesField, 16u, false },
        MalformedField{ "image_bytes_overflow.font", s_HeaderBytes + s_GroupImageBytesField, Limit<u32>::s_Max, false },
    };
    for(const MalformedField& field : s_Fields){
        Core::Assets::AssetBytes malformed(binary.begin(), binary.end(), testArena.arena);
        WriteLittleU32(field.offset, field.value, malformed);
        auto malformedPathResult = WriteCase(field.filename, malformed, testArena);
        ASSERT_TRUE(malformedPathResult);
        path = Move(*malformedPathResult);
        for(const b8 includePixels : { false, true }){
            const ArenaMemoryStats before = outputArena.memoryStats();
            EXPECT_FALSE(ReadPreparedFontSource(path, includePixels, outputArena)) << field.filename;
            const ArenaMemoryStats after = outputArena.memoryStats();
            EXPECT_EQ(after.usedBytes, before.usedBytes);
            EXPECT_LE(after.peakUsedBytes, before.usedBytes + 8u * sizeof(PreparedFontImageGroup));
            if(field.rejectsBeforeDirectoryAllocation)
                EXPECT_EQ(after.allocationCount, before.allocationCount);
        }
    }
    // Valid maximum dimensions still require their full payload length before any SFNT/image allocation.
    Core::Assets::AssetBytes oversized(binary.begin(), binary.end(), testArena.arena);
    WriteLittleU32(s_HeaderBytes, s_MaxImageSide, oversized);
    WriteLittleU32(s_HeaderBytes + s_GroupHeightField, s_MaxImageSide, oversized);
    WriteLittleU32(s_HeaderBytes + s_GroupChannelsField, s_MaxChannelCount, oversized);
    WriteLittleU32(s_HeaderBytes + s_GroupImageBytesField, s_MaxImageSide * s_MaxImageSide * s_MaxChannelCount, oversized);
    auto oversizedPathResult = WriteCase("missing_large_image.font", oversized, testArena);
    ASSERT_TRUE(oversizedPathResult);
    path = Move(*oversizedPathResult);
    for(const b8 includePixels : { false, true }){
        const ArenaMemoryStats before = outputArena.memoryStats();
        EXPECT_FALSE(ReadPreparedFontSource(path, includePixels, outputArena));
        EXPECT_LE(outputArena.memoryStats().peakUsedBytes, before.usedBytes + 8u * sizeof(PreparedFontImageGroup));
    }
    EXPECT_EQ(logger.errorCount(), 2u * (s_Fields.size() + 1u));
}

TEST(PreparedFontSource, TruncatedSectionsAndTrailingBytesRejectBothReadModes){
    using namespace __hidden_prepared_source_tests;
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SourceTestArena testArena;
    SourceFixture fixture(testArena.arena);
    auto binaryResult = SerializePreparedFontSource({ fixture.sfnt.data(), fixture.sfnt.size() }, 0u, fixture.views, testArena.arena);
    ASSERT_TRUE(binaryResult);
    Core::Assets::AssetBytes& binary = *binaryResult;
    auto pathResult = WriteCase("length_control.font", binary, testArena);
    ASSERT_TRUE(pathResult);
    Path path = Move(*pathResult);
    ASSERT_TRUE(ReadPreparedFontSource(path, true, testArena.arena));
    struct Cut{
        AStringView filename;
        usize byteCount = 0u;
    };
    const Array<Cut, 7> cuts = {
        Cut{ "short_header.font", s_HeaderBytes - 1u },
        Cut{ "missing_directory.font", s_HeaderBytes },
        Cut{ "short_directory.font", s_FontOffset - 1u },
        Cut{ "short_font.font", s_PixelOffset - 1u },
        Cut{ "missing_images.font", s_PixelOffset },
        Cut{ "short_images.font", binary.size() - 1u },
        Cut{ "trailing_byte.font", binary.size() + 1u },
    };
    for(const Cut& cut : cuts){
        Core::Assets::AssetBytes malformed(binary.begin(), binary.end(), testArena.arena);
        malformed.resize(cut.byteCount);
        auto malformedPathResult = WriteCase(cut.filename, malformed, testArena);
        ASSERT_TRUE(malformedPathResult);
        path = Move(*malformedPathResult);
        for(const b8 includePixels : { false, true })
            EXPECT_FALSE(ReadPreparedFontSource(path, includePixels, testArena.arena)) << cut.filename;
    }
    EXPECT_EQ(logger.errorCount(), 2u * cuts.size());
}

TEST(PreparedFontSource, InvalidSerializationInputsRejectWithoutAllocating){
    using namespace __hidden_prepared_source_tests;
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SourceTestArena testArena;
    Core::Alloc::GlobalArena outputArena{Name(NWB::Tests::FontCases::s_FontCaseTestsIntegrationAssetsFontPreparedSerializeOutput)};
    SourceFixture fixture(testArena.arena);
    auto outputResult = SerializePreparedFontSource({ fixture.sfnt.data(), fixture.sfnt.size() }, 0u, fixture.views, outputArena);
    ASSERT_TRUE(outputResult);
    constexpr u32 s_InvalidCaseCount = 18u;
    constexpr usize s_OverLimitGroupCount = 9u;
    for(u32 variant = 0u; variant < s_InvalidCaseCount; ++variant){
        BinaryByteView sfnt{ fixture.sfnt.data(), fixture.sfnt.size() };
        Array<PreparedFontImageView, s_OverLimitGroupCount> views = {};
        for(usize index = 0u; index < s_GroupCount; ++index)
            views[index] = fixture.views[index];
        usize groupCount = s_GroupCount;
        u32 faceIndex = 0u;
        switch(variant){
        case 0u: sfnt.byteCount = 0u; break;
        case 1u: sfnt.bytes = nullptr; break;
        case 2u: sfnt.byteCount = s_FontMaxSourceBytes + 1u; break;
        case 3u: faceIndex = 1u; break;
        case 4u: groupCount = 0u; break;
        case 5u: groupCount = s_OverLimitGroupCount; break;
        case 6u: views[0u].width = 0u; break;
        case 7u: views[0u].width = 2049u; break;
        case 8u: views[0u].width = Limit<u32>::s_Max; break;
        case 9u: views[0u].height = 0u; break;
        case 10u: views[0u].height = 2049u; break;
        case 11u: views[0u].height = Limit<u32>::s_Max; break;
        case 12u: views[0u].channelCount = 0u; break;
        case 13u: views[0u].channelCount = 5u; break;
        case 14u: views[0u].channelCount = Limit<u32>::s_Max; break;
        case 15u: views[0u].pixels = Span<const u8>(); break;
        case 16u: views[0u].pixels = views[0u].pixels.first(views[0u].pixels.size() - 1u); break;
        case 17u: views[0u].pixels = fixture.views[1u].pixels.first(views[0u].pixels.size() + 1u); break;
        }
        const ArenaMemoryStats before = outputArena.memoryStats();
        EXPECT_FALSE(SerializePreparedFontSource(sfnt, faceIndex, Span<const PreparedFontImageView>(views.data(), groupCount), outputArena)) << variant;
        EXPECT_EQ(outputArena.memoryStats().allocationCount, before.allocationCount);
    }
    EXPECT_EQ(logger.errorCount(), s_InvalidCaseCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
