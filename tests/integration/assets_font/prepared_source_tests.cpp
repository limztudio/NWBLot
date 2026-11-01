// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/capturing_logger.h>
#include <tests/common/font_fixture.h>
#include <tests/common/test_context.h>

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
    Core::Alloc::GlobalArena outputArena(Name("tests/integration/assets_font/prepared_output"));
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
    Core::Alloc::GlobalArena outputArena(Name("tests/integration/assets_font/prepared_serialize_output"));
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

