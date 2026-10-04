// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_font/asset.h>
#include <impl/assets_font/prepared_source.h>

#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>

#include <global/filesystem.h>

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

struct SourceFixture{
    Core::Assets::AssetVector<PreparedFontImageGroup> groups;
    PreparedFontImageView views[s_GroupCount];
    u8 sfnt[s_FontBytes] = { 0u, 1u, 4u, 0u, 127u, 128u, 255u };


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
    bool rejectsBeforeDirectoryAllocation = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void WriteLittleU32(Core::Assets::AssetBytes& bytes, const usize offset, const u32 value){
    for(u32 index = 0u; index < 4u; ++index)
        bytes[offset + index] = static_cast<u8>(value >> (index * 8u));
}

[[nodiscard]] static bool WriteCase(
    SourceTestArena& testArena,
    const AStringView filename,
    const Core::Assets::AssetBytes& binary,
    Path& outPath){
    Path candidate = Path(testArena.arena, NWB_REPO_ROOT) / "__artifacts" / "font_source_tests" / filename;
    ErrorCode error;
    if(!EnsureDirectories(candidate.parent_path(), error) || error || !WriteBinaryFile(candidate, binary))
        return false;
    outPath = Move(candidate);
    return true;
}

static void ExpectSourceUnchanged(const PreparedFontSource& source, const PreparedFontSource& original){
    EXPECT_EQ(source.faceIndex, original.faceIndex);
    EXPECT_TRUE(source.fontSha256 == original.fontSha256);
    EXPECT_EQ(source.fontBytes, original.fontBytes);
    ASSERT_EQ(source.groups.size(), original.groups.size());
    for(usize index = 0u; index < original.groups.size(); ++index){
        const PreparedFontImageGroup& group = source.groups[index];
        const PreparedFontImageGroup& expected = original.groups[index];
        EXPECT_EQ(group.width, expected.width);
        EXPECT_EQ(group.height, expected.height);
        EXPECT_EQ(group.channelCount, expected.channelCount);
        EXPECT_TRUE(group.sha256 == expected.sha256);
        EXPECT_EQ(group.pixels, expected.pixels);
    }
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
    Core::Assets::AssetBytes binary(testArena.arena);
    ASSERT_TRUE(SerializePreparedFontSource({ fixture.sfnt, sizeof(fixture.sfnt) }, 0u, fixture.views, s_GroupCount, binary));
    ASSERT_EQ(binary.size(), s_PixelOffset + 150u);
    Path path(testArena.arena);
    ASSERT_TRUE(WriteCase(testArena, "image_integrity_control.font", binary, path));
    PreparedFontSource original(testArena.arena);
    ASSERT_TRUE(ReadPreparedFontSource(path, original, true));
    ASSERT_EQ(original.groups.size(), s_GroupCount);
    for(usize index = 0u; index < s_GroupCount; ++index){
        EXPECT_EQ(original.groups[index].pixels, fixture.groups[index].pixels);
        EXPECT_EQ(original.groups[index].pixels.size(), 15u * (index + 1u));
    }
    PreparedFontSource output(testArena.arena);
    ASSERT_TRUE(ReadPreparedFontSource(path, output, true));
    static constexpr AStringView s_Filenames[] = { "image_r.font", "image_rg.font", "image_rgb.font", "image_rgba.font", "image_hash.font" };
    usize imageOffset = s_PixelOffset;
    for(usize index = 0u; index < LengthOf(s_Filenames); ++index){
        Core::Assets::AssetBytes corrupted(binary.begin(), binary.end(), testArena.arena);
        const usize corruptionOffset = index < s_GroupCount ? imageOffset : s_HeaderBytes + 16u;
        corrupted[corruptionOffset] ^= 0x80u;
        ASSERT_TRUE(WriteCase(testArena, s_Filenames[index], corrupted, path));
        EXPECT_FALSE(ReadPreparedFontSource(path, output, true)) << index;
        ExpectSourceUnchanged(output, original);
        PreparedFontSource fontOnly(testArena.arena);
        ASSERT_TRUE(ReadPreparedFontSource(path, fontOnly, false)) << index;
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
    EXPECT_EQ(logger.errorCount(), LengthOf(s_Filenames));
    EXPECT_TRUE(logger.sawErrorContaining(GLOBAL_TEXT("image content hash differs")));
}

TEST(PreparedFontSource, SfntByteAndDigestCorruptionRejectBothReadModesWithoutReplacingSource){
    using namespace __hidden_prepared_source_tests;
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SourceTestArena testArena;
    SourceFixture fixture(testArena.arena);
    Core::Assets::AssetBytes binary(testArena.arena);
    ASSERT_TRUE(SerializePreparedFontSource({ fixture.sfnt, sizeof(fixture.sfnt) }, 0u, fixture.views, s_GroupCount, binary));
    Path path(testArena.arena);
    ASSERT_TRUE(WriteCase(testArena, "font_integrity_control.font", binary, path));
    PreparedFontSource original(testArena.arena);
    PreparedFontSource output(testArena.arena);
    ASSERT_TRUE(ReadPreparedFontSource(path, original, true));
    ASSERT_TRUE(ReadPreparedFontSource(path, output, true));
    static constexpr AStringView s_Filenames[] = { "font_corrupt_bytes.font", "font_corrupt_hash.font" };
    static constexpr usize s_Offsets[] = { s_FontOffset, 24u };
    for(usize index = 0u; index < LengthOf(s_Filenames); ++index){
        Core::Assets::AssetBytes corrupted(binary.begin(), binary.end(), testArena.arena);
        corrupted[s_Offsets[index]] ^= 0x40u;
        ASSERT_TRUE(WriteCase(testArena, s_Filenames[index], corrupted, path));
        for(const bool includePixels : { false, true }){
            EXPECT_FALSE(ReadPreparedFontSource(path, output, includePixels));
            ExpectSourceUnchanged(output, original);
        }
    }
    EXPECT_EQ(logger.errorCount(), 4u);
    EXPECT_TRUE(logger.sawErrorContaining(GLOBAL_TEXT("SFNT content hash differs")));
}

TEST(PreparedFontSource, MalformedDirectoryCountsAndOverflowRejectBeforePayloadAllocation){
    using namespace __hidden_prepared_source_tests;
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SourceTestArena testArena;
    Core::Alloc::GlobalArena outputArena(Name("tests/integration/assets_font/prepared_output"));
    SourceFixture fixture(testArena.arena);
    Core::Assets::AssetBytes binary(testArena.arena);
    ASSERT_TRUE(SerializePreparedFontSource({ fixture.sfnt, sizeof(fixture.sfnt) }, 0u, fixture.views, s_GroupCount, binary));
    Path path(testArena.arena);
    ASSERT_TRUE(WriteCase(testArena, "directory_control.font", binary, path));
    PreparedFontSource original(testArena.arena);
    PreparedFontSource output(outputArena);
    ASSERT_TRUE(ReadPreparedFontSource(path, original, true));
    ASSERT_TRUE(ReadPreparedFontSource(path, output, true));
    static constexpr MalformedField s_Fields[] = {
        { "magic.font", 0u, 0u, true },
        { "retired_fon1.font", 0u, 0x464f4e31u, true },
        { "old_version.font", 4u, 0u, true },
        { "future_version.font", 4u, 2u, true },
        { "face_index.font", 8u, 1u, true },
        { "reserved.font", 20u, 1u, true },
        { "font_zero.font", 12u, 0u, true },
        { "font_limit.font", 12u, s_FontMaxSourceBytes + 1u, true },
        { "font_overflow.font", 12u, Limit<u32>::s_Max, true },
        { "font_length.font", 12u, s_FontBytes + 1u, false },
        { "groups_zero.font", 16u, 0u, true },
        { "groups_limit.font", 16u, 9u, true },
        { "groups_overflow.font", 16u, Limit<u32>::s_Max, true },
        { "missing_group_header.font", 16u, 5u, false },
        { "width_zero.font", s_HeaderBytes, 0u, false },
        { "width_limit.font", s_HeaderBytes, 2049u, false },
        { "width_overflow.font", s_HeaderBytes, Limit<u32>::s_Max, false },
        { "height_zero.font", s_HeaderBytes + 4u, 0u, false },
        { "height_limit.font", s_HeaderBytes + 4u, 2049u, false },
        { "height_overflow.font", s_HeaderBytes + 4u, Limit<u32>::s_Max, false },
        { "channels_zero.font", s_HeaderBytes + 8u, 0u, false },
        { "channels_limit.font", s_HeaderBytes + 8u, 5u, false },
        { "channels_overflow.font", s_HeaderBytes + 8u, Limit<u32>::s_Max, false },
        { "image_bytes_zero.font", s_HeaderBytes + 12u, 0u, false },
        { "image_bytes_short.font", s_HeaderBytes + 12u, 14u, false },
        { "image_bytes_long.font", s_HeaderBytes + 12u, 16u, false },
        { "image_bytes_overflow.font", s_HeaderBytes + 12u, Limit<u32>::s_Max, false },
    };
    for(const MalformedField& field : s_Fields){
        Core::Assets::AssetBytes malformed(binary.begin(), binary.end(), testArena.arena);
        WriteLittleU32(malformed, field.offset, field.value);
        ASSERT_TRUE(WriteCase(testArena, field.filename, malformed, path));
        for(const bool includePixels : { false, true }){
            const ArenaMemoryStats before = outputArena.memoryStats();
            EXPECT_FALSE(ReadPreparedFontSource(path, output, includePixels)) << field.filename;
            const ArenaMemoryStats after = outputArena.memoryStats();
            ExpectSourceUnchanged(output, original);
            EXPECT_EQ(after.usedBytes, before.usedBytes);
            EXPECT_LE(after.peakUsedBytes, before.usedBytes + 8u * sizeof(PreparedFontImageGroup));
            if(field.rejectsBeforeDirectoryAllocation)
                EXPECT_EQ(after.allocationCount, before.allocationCount);
        }
    }
    // Valid maximum dimensions still require their full payload length before any SFNT/image allocation.
    Core::Assets::AssetBytes oversized(binary.begin(), binary.end(), testArena.arena);
    WriteLittleU32(oversized, s_HeaderBytes, 2048u);
    WriteLittleU32(oversized, s_HeaderBytes + 4u, 2048u);
    WriteLittleU32(oversized, s_HeaderBytes + 8u, 4u);
    WriteLittleU32(oversized, s_HeaderBytes + 12u, 2048u * 2048u * 4u);
    ASSERT_TRUE(WriteCase(testArena, "missing_large_image.font", oversized, path));
    for(const bool includePixels : { false, true }){
        const ArenaMemoryStats before = outputArena.memoryStats();
        EXPECT_FALSE(ReadPreparedFontSource(path, output, includePixels));
        ExpectSourceUnchanged(output, original);
        EXPECT_LE(outputArena.memoryStats().peakUsedBytes, before.usedBytes + 8u * sizeof(PreparedFontImageGroup));
    }
    EXPECT_EQ(logger.errorCount(), 2u * (LengthOf(s_Fields) + 1u));
}

TEST(PreparedFontSource, TruncatedSectionsAndTrailingBytesRejectBothReadModesAtomically){
    using namespace __hidden_prepared_source_tests;
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SourceTestArena testArena;
    SourceFixture fixture(testArena.arena);
    Core::Assets::AssetBytes binary(testArena.arena);
    ASSERT_TRUE(SerializePreparedFontSource({ fixture.sfnt, sizeof(fixture.sfnt) }, 0u, fixture.views, s_GroupCount, binary));
    Path path(testArena.arena);
    ASSERT_TRUE(WriteCase(testArena, "length_control.font", binary, path));
    PreparedFontSource original(testArena.arena);
    PreparedFontSource output(testArena.arena);
    ASSERT_TRUE(ReadPreparedFontSource(path, original, true));
    ASSERT_TRUE(ReadPreparedFontSource(path, output, true));
    struct Cut{
        AStringView filename;
        usize byteCount;
    };
    const Cut cuts[] = {
        { "short_header.font", s_HeaderBytes - 1u },
        { "missing_directory.font", s_HeaderBytes },
        { "short_directory.font", s_FontOffset - 1u },
        { "short_font.font", s_PixelOffset - 1u },
        { "missing_images.font", s_PixelOffset },
        { "short_images.font", binary.size() - 1u },
        { "trailing_byte.font", binary.size() + 1u },
    };
    for(const Cut& cut : cuts){
        Core::Assets::AssetBytes malformed(binary.begin(), binary.end(), testArena.arena);
        malformed.resize(cut.byteCount);
        ASSERT_TRUE(WriteCase(testArena, cut.filename, malformed, path));
        for(const bool includePixels : { false, true }){
            EXPECT_FALSE(ReadPreparedFontSource(path, output, includePixels)) << cut.filename;
            ExpectSourceUnchanged(output, original);
        }
    }
    EXPECT_EQ(logger.errorCount(), 2u * LengthOf(cuts));
}

TEST(PreparedFontSource, InvalidSerializationInputsPreservePreviouslyPreparedBytesWithoutAllocating){
    using namespace __hidden_prepared_source_tests;
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SourceTestArena testArena;
    Core::Alloc::GlobalArena outputArena(Name("tests/integration/assets_font/prepared_serialize_output"));
    SourceFixture fixture(testArena.arena);
    Core::Assets::AssetBytes output(outputArena);
    ASSERT_TRUE(SerializePreparedFontSource({ fixture.sfnt, sizeof(fixture.sfnt) }, 0u, fixture.views, s_GroupCount, output));
    const Core::Assets::AssetBytes original(output.begin(), output.end(), testArena.arena);
    constexpr u32 s_InvalidCaseCount = 20u;
    for(u32 variant = 0u; variant < s_InvalidCaseCount; ++variant){
        BinaryByteView sfnt{ fixture.sfnt, sizeof(fixture.sfnt) };
        PreparedFontImageView views[s_GroupCount];
        for(usize index = 0u; index < s_GroupCount; ++index)
            views[index] = fixture.views[index];
        const PreparedFontImageView* groups = views;
        usize groupCount = s_GroupCount;
        u32 faceIndex = 0u;
        switch(variant){
        case 0u: sfnt.byteCount = 0u; break;
        case 1u: sfnt.bytes = nullptr; break;
        case 2u: sfnt.byteCount = s_FontMaxSourceBytes + 1u; break;
        case 3u: faceIndex = 1u; break;
        case 4u: groupCount = 0u; break;
        case 5u: groupCount = 9u; break;
        case 6u: groupCount = Limit<usize>::s_Max; break;
        case 7u: groups = nullptr; break;
        case 8u: views[0u].width = 0u; break;
        case 9u: views[0u].width = 2049u; break;
        case 10u: views[0u].width = Limit<u32>::s_Max; break;
        case 11u: views[0u].height = 0u; break;
        case 12u: views[0u].height = 2049u; break;
        case 13u: views[0u].height = Limit<u32>::s_Max; break;
        case 14u: views[0u].channelCount = 0u; break;
        case 15u: views[0u].channelCount = 5u; break;
        case 16u: views[0u].channelCount = Limit<u32>::s_Max; break;
        case 17u: views[0u].pixels.bytes = nullptr; break;
        case 18u: --views[0u].pixels.byteCount; break;
        case 19u: ++views[0u].pixels.byteCount; break;
        }
        const ArenaMemoryStats before = outputArena.memoryStats();
        EXPECT_FALSE(SerializePreparedFontSource(sfnt, faceIndex, groups, groupCount, output)) << variant;
        EXPECT_EQ(output, original);
        EXPECT_EQ(outputArena.memoryStats().allocationCount, before.allocationCount);
    }
    EXPECT_EQ(logger.errorCount(), s_InvalidCaseCount);
}

TEST(PreparedFontSource, SerializerConsumesAliasedSfntAndCompactPixelsBeforeReplacingOutput){
    using namespace __hidden_prepared_source_tests;
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SourceTestArena testArena;
    SourceFixture fixture(testArena.arena);
    Core::Assets::AssetBytes output(testArena.arena);
    ASSERT_TRUE(SerializePreparedFontSource({ fixture.sfnt, sizeof(fixture.sfnt) }, 0u, fixture.views, s_GroupCount, output));
    const Core::Assets::AssetBytes original(output.begin(), output.end(), testArena.arena);
    PreparedFontImageView views[s_GroupCount];
    usize imageOffset = s_PixelOffset;
    for(usize index = 0u; index < s_GroupCount; ++index){
        views[index] = fixture.views[index];
        views[index].pixels.bytes = output.data() + imageOffset;
        imageOffset += views[index].pixels.size();
    }
    ASSERT_TRUE(SerializePreparedFontSource({ output.data() + s_FontOffset, s_FontBytes }, 0u, views, s_GroupCount, output));
    EXPECT_EQ(output, original);
    EXPECT_EQ(logger.errorCount(), 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

