// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_font/source_directory.h>
#include <impl/assets_font_atlas/asset.h>
#include <impl/assets_font_atlas/binary_payload.h>

#include <tests/common/capturing_logger.h>
#include <tests/common/font_fixture.h>
#include <tests/common/test_context.h>

#include <global/filesystem.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_atlas_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Impl;

struct FontAtlasTestArenaTag{};
using AtlasTestArena = TestArena<FontAtlasTestArenaTag>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static FontAtlasPayload MakePayload(AtlasTestArena& testArena){
    FontAtlasPayload payload(testArena.arena);
    payload.font = Core::Assets::AssetRef<Font>("project/fonts/body");
    static constexpr AStringView s_Source = "exact source font identity fixture";
    payload.fontSha256 = ComputeSha256({ reinterpret_cast<const u8*>(s_Source.data()), s_Source.size() });
    payload.unitsPerEm = 1000u;
    payload.sourceGlyphCount = 5u;
    payload.ascenderUnits = 800.f;
    payload.descenderUnits = -200.f;
    FontAtlasGroup group(testArena.arena);
    group.width = 8u;
    group.height = 8u;
    group.pixels.resize(8u * 8u * 4u);
    for(usize index = 0u; index < group.pixels.size(); ++index)
        group.pixels[index] = static_cast<u8>(index);
    group.sha256 = ComputeSha256({ group.pixels.data(), group.pixels.size() });
    payload.groups.push_back(Move(group));
    for(u32 index = 0u; index < 5u; ++index){
        FontAtlasGlyph glyph;
        glyph.glyphId = index;
        glyph.advanceUnits = 400.f + static_cast<f32>(index);
        if(index < 4u){
            glyph.channel = index;
            glyph.x = 1u;
            glyph.y = 1u;
            glyph.width = 4u;
            glyph.height = 4u;
            glyph.planeLeft = -10.f;
            glyph.planeTop = -50.f;
            glyph.planeRight = 52.5f;
            glyph.planeBottom = 12.5f;
            glyph.drawable = 1u;
        }
        payload.glyphs.push_back(glyph);
    }
    static constexpr u8 s_KernBytes[] = {
        0x00u, 0x00u, 0x00u, 0x01u, 0x00u, 0x00u, 0x00u, 0x14u,
        0x00u, 0x01u, 0x00u, 0x01u, 0x00u, 0x06u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x01u, 0xffu, 0xecu,
    };
    FontAtlasPositioningTable table(testArena.arena);
    table.tag = s_FontAtlasKernTag;
    table.bytes.assign(s_KernBytes, s_KernBytes + LengthOf(s_KernBytes));
    table.sha256 = ComputeSha256({ table.bytes.data(), table.bytes.size() });
    payload.positioningTables.push_back(Move(table));
    return payload;
}

static void WriteLittleU32(Core::Assets::AssetBytes& bytes, const usize offset, const u32 value){
    for(u32 index = 0u; index < 4u; ++index)
        bytes[offset + index] = static_cast<u8>(value >> (index * 8u));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AssetsFontAtlas, EmptyAndPaddingBoundaryDigestsRejectMalformedText){
    const auto emptyHash = ParseSha256("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    ASSERT_TRUE(emptyHash);
    EXPECT_EQ(ComputeSha256({}), *emptyHash);
    // A 56-byte input requires a second block for the SHA-256 padding.
    const auto paddedHash = ParseSha256("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    ASSERT_TRUE(paddedHash);
    static constexpr AStringView s_MultiBlock = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    EXPECT_EQ(ComputeSha256({ reinterpret_cast<const u8*>(s_MultiBlock.data()), s_MultiBlock.size() }), *paddedHash);
    EXPECT_FALSE(ParseSha256("BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD"));
    EXPECT_FALSE(ParseSha256("ba78"));
}

TEST(AssetsFontAtlas, EmptyGlyphRetainsAdvanceAndFirstOutOfRangeLookupMissesAfterLoad){
    using namespace __hidden_font_atlas_tests;
    AtlasTestArena testArena;
    FontAtlas atlas(testArena.arena, Name("project/fonts/body_atlas"));
    atlas.setPayload(MakePayload(testArena));
    FontAtlasAssetCodec codec;
    Core::Assets::AssetBytes binary(testArena.arena);
    ASSERT_TRUE(codec.serialize(atlas, binary));
    FontAtlas loaded(testArena.arena, atlas.virtualPath());
    ASSERT_TRUE(loaded.loadBinary(binary));
    ASSERT_NE(loaded.glyph(4u), nullptr);
    EXPECT_EQ(loaded.glyph(4u)->drawable, 0u);
    EXPECT_FLOAT_EQ(loaded.glyph(4u)->advanceUnits, 404.f);
    EXPECT_EQ(loaded.glyph(5u), nullptr);
}

TEST(AssetsFontAtlas, MalformedBinaryDoesNotReplacePreviouslyLoadedAtlas){
    using namespace __hidden_font_atlas_tests;
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    AtlasTestArena testArena;
    FontAtlasPayload payload = MakePayload(testArena);
    auto valid = SerializeFontAtlasPayload(payload, testArena.arena);
    ASSERT_TRUE(valid);
    FontAtlas atlas(testArena.arena, Name("project/fonts/body_atlas"));
    ASSERT_TRUE(atlas.loadBinary(*valid));
    const usize corruptOffsets[] = { 0u, 4u, 8u, 12u, 112u, 120u, 136u, 152u, 156u, 160u,
        FontAtlasBinaryPayload::s_HeaderBytes + 48u,
        FontAtlasBinaryPayload::s_HeaderBytes + 5u * FontAtlasBinaryPayload::s_GlyphBytes,
        FontAtlasBinaryPayload::s_HeaderBytes + 5u * FontAtlasBinaryPayload::s_GlyphBytes + 4u,
        FontAtlasBinaryPayload::s_HeaderBytes + 5u * FontAtlasBinaryPayload::s_GlyphBytes + 8u,
        FontAtlasBinaryPayload::s_HeaderBytes + 5u * FontAtlasBinaryPayload::s_GlyphBytes + 12u };
    for(const usize offset : corruptOffsets){
        Core::Assets::AssetBytes invalid(*valid);
        WriteLittleU32(invalid, offset, Limit<u32>::s_Max);
        EXPECT_FALSE(atlas.loadBinary(invalid)) << offset;
        EXPECT_EQ(atlas.payload().fontSha256, payload.fontSha256);
        EXPECT_EQ(atlas.payload().groups[0u].pixels, payload.groups[0u].pixels);
    }
    Core::Assets::AssetBytes changed(*valid);
    changed.back() ^= 1u;
    EXPECT_FALSE(atlas.loadBinary(changed));
    EXPECT_EQ(atlas.payload().groups[0u].pixels, payload.groups[0u].pixels);
    Core::Assets::AssetBytes truncated(*valid);
    truncated.pop_back();
    EXPECT_FALSE(atlas.loadBinary(truncated));
    Core::Assets::AssetBytes trailing(*valid);
    trailing.push_back(0u);
    EXPECT_FALSE(atlas.loadBinary(trailing));
}

TEST(AssetsFontAtlas, CompactNonPowerOfTwoGroupsRejectMissingChannelsAndLostGuards){
    using namespace __hidden_font_atlas_tests;
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    AtlasTestArena testArena;
    for(u32 channelCount = 1u; channelCount <= 4u; ++channelCount){
        FontAtlasPayload payload = MakePayload(testArena);
        FontAtlasGroup& group = payload.groups[0u];
        group.width = 7u;
        group.height = 9u;
        group.channelCount = channelCount;
        group.pixels.resize(static_cast<usize>(group.width) * group.height * channelCount);
        for(usize index = 0u; index < group.pixels.size(); ++index)
            group.pixels[index] = static_cast<u8>(index * 17u + channelCount);
        group.sha256 = ComputeSha256({ group.pixels.data(), group.pixels.size() });
        payload.sourceGlyphCount = channelCount + 1u;
        payload.glyphs.resize(payload.sourceGlyphCount);
        for(u32 index = 0u; index < channelCount; ++index){
            payload.glyphs[index].x = 2u;
            payload.glyphs[index].y = 4u;
        }
        payload.glyphs[channelCount] = {};
        payload.glyphs[channelCount].glyphId = channelCount;
        auto binary = SerializeFontAtlasPayload(payload, testArena.arena);
        ASSERT_TRUE(binary) << channelCount;
        auto loaded = DeserializeFontAtlasPayload(*binary, testArena.arena);
        ASSERT_TRUE(loaded) << channelCount;
        FontAtlasPayload invalid(payload);
        invalid.glyphs[0u].channel = channelCount;
        EXPECT_FALSE(ValidateFontAtlasPayload(invalid));
        invalid = payload;
        ++invalid.glyphs[0u].x;
        EXPECT_FALSE(ValidateFontAtlasPayload(invalid)); // The right exterior guard was removed by cropping.
        invalid = payload;
        ++invalid.glyphs[0u].y;
        EXPECT_FALSE(ValidateFontAtlasPayload(invalid)); // The lower exterior guard was removed by cropping.
        invalid = payload;
        invalid.groups[0u].pixels.push_back(0u);
        invalid.groups[0u].sha256 = ComputeSha256({ invalid.groups[0u].pixels.data(), invalid.groups[0u].pixels.size() });
        EXPECT_FALSE(SerializeFontAtlasPayload(invalid, testArena.arena));
        EXPECT_EQ(loaded->groups[0u].pixels, group.pixels);
    }
}

TEST(AssetsFontAtlas, UnsupportedVersionsAndCompactGroupHeadersPreservePublishedPayload){
    using namespace __hidden_font_atlas_tests;
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    AtlasTestArena testArena;
    const FontAtlasPayload payload = MakePayload(testArena);
    auto binary = SerializeFontAtlasPayload(payload, testArena.arena);
    ASSERT_TRUE(binary);
    auto loaded = DeserializeFontAtlasPayload(*binary, testArena.arena);
    ASSERT_TRUE(loaded);
    const usize groupOffset = FontAtlasBinaryPayload::s_HeaderBytes + payload.glyphs.size() * FontAtlasBinaryPayload::s_GlyphBytes;
    const Pair<usize, u32> invalidFields[] = {
        { 4u, 1u }, { 4u, 0u }, { 4u, FontAtlasBinaryPayload::s_Version + 1u },
        { groupOffset, Limit<u32>::s_Max }, { groupOffset + 4u, Limit<u32>::s_Max },
        { groupOffset + 8u, 0u }, { groupOffset + 8u, 5u }, { groupOffset + 8u, Limit<u32>::s_Max },
        { groupOffset + 12u, 0u }, { groupOffset + 12u, 255u }, { groupOffset + 12u, 257u },
    };
    for(const auto& field : invalidFields){
        const usize offset = field.first();
        const u32 value = field.second();
        Core::Assets::AssetBytes invalid(*binary);
        WriteLittleU32(invalid, offset, value);
        EXPECT_FALSE(DeserializeFontAtlasPayload(invalid, testArena.arena)) << offset << ": " << value;
        EXPECT_EQ(loaded->groups[0u].pixels, payload.groups[0u].pixels);
        EXPECT_EQ(loaded->groups[0u].channelCount, 4u);
    }
    FontAtlasPayload invalid(payload);
    invalid.groups[0u].channelCount = 0u;
    EXPECT_FALSE(ValidateFontAtlasPayload(invalid));
    invalid.groups[0u].channelCount = 5u;
    EXPECT_FALSE(ValidateFontAtlasPayload(invalid));
    invalid.groups[0u].channelCount = Limit<u32>::s_Max;
    invalid.groups[0u].width = Limit<u32>::s_Max;
    invalid.groups[0u].height = Limit<u32>::s_Max;
    EXPECT_FALSE(ValidateFontAtlasPayload(invalid));
}

TEST(AssetsFontAtlas, CodecRejectsUnnamedOrCorruptAtlasWithoutReplacingOutput){
    using namespace __hidden_font_atlas_tests;
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    AtlasTestArena testArena;
    FontAtlasAssetCodec codec;
    Core::Assets::AssetBytes binary(testArena.arena);
    binary.assign(3u, 42u);
    const Core::Assets::AssetBytes expected(binary);
    FontAtlas unnamed(testArena.arena);
    unnamed.setPayload(MakePayload(testArena));
    EXPECT_FALSE(codec.serialize(unnamed, binary));
    EXPECT_EQ(binary, expected);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("FontAtlas::validatePayload failed: virtual path is empty")));

    FontAtlas corrupt(testArena.arena, Name("project/fonts/body_atlas"));
    FontAtlasPayload payload = MakePayload(testArena);
    payload.groups[0u].pixels[0u] ^= 1u;
    corrupt.setPayload(Move(payload));
    EXPECT_FALSE(codec.serialize(corrupt, binary));
    EXPECT_EQ(binary, expected);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("pixel content hash mismatch")));
}

TEST(AssetsFontAtlas, InvalidHashAndGeometryAreRejectedBySerialization){
    using namespace __hidden_font_atlas_tests;
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    AtlasTestArena testArena;
    FontAtlasPayload original = MakePayload(testArena);
    FontAtlasPayload candidate(original);
    candidate.groups[0u].pixels[0u] ^= 1u;
    EXPECT_FALSE(SerializeFontAtlasPayload(candidate, testArena.arena));
    candidate = original;
    candidate.glyphs[0u].channel = 4u;
    EXPECT_FALSE(ValidateFontAtlasPayload(candidate));
    candidate = original;
    candidate.glyphs[0u].x = Limit<u32>::s_Max;
    EXPECT_FALSE(ValidateFontAtlasPayload(candidate));
    candidate = original;
    candidate.glyphs[0u].planeRight += 1.f;
    EXPECT_FALSE(ValidateFontAtlasPayload(candidate));
    candidate = original;
    candidate.glyphs[0u].advanceUnits = Limit<f32>::s_Infinity;
    EXPECT_FALSE(ValidateFontAtlasPayload(candidate));
    candidate = original;
    candidate.glyphs[1u].channel = 0u;
    EXPECT_FALSE(ValidateFontAtlasPayload(candidate));
    candidate = original;
    candidate.glyphs[4u].x = 1u;
    EXPECT_FALSE(ValidateFontAtlasPayload(candidate));
}

TEST(AssetsFontAtlas, GuardedRegionsRejectSameChannelOverlapAndAllowIndependentChannels){
    using namespace __hidden_font_atlas_tests;
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    AtlasTestArena testArena;
    FontAtlasPayload payload = MakePayload(testArena);
    ASSERT_TRUE(ValidateFontAtlasPayload(payload));
    FontAtlasGroup& group = payload.groups[0u];
    group.width = 16u;
    group.pixels.resize(16u * 8u * 4u, 0u);
    group.sha256 = ComputeSha256({ group.pixels.data(), group.pixels.size() });
    FontAtlasGlyph& second = payload.glyphs[1u];
    second.channel = 0u;
    second.x = 7u;
    ASSERT_TRUE(ValidateFontAtlasPayload(payload));
    second.x = 6u;
    EXPECT_FALSE(ValidateFontAtlasPayload(payload)); // Bitmaps are disjoint, but the one-texel guards overlap.
    second.x = 4u;
    EXPECT_FALSE(ValidateFontAtlasPayload(payload));
    second.channel = 1u;
    EXPECT_TRUE(ValidateFontAtlasPayload(payload));
}

TEST(AssetsFontAtlas, PaddedPlaneSizeMustMatchBitmapExtentAtTheDeclaredBakeSize){
    using namespace __hidden_font_atlas_tests;
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    AtlasTestArena testArena;
    FontAtlasPayload original = MakePayload(testArena);
    ASSERT_TRUE(ValidateFontAtlasPayload(original));
    FontAtlasPayload candidate(original);
    candidate.glyphs[0u].planeRight += 1.f;
    EXPECT_FALSE(ValidateFontAtlasPayload(candidate));
    candidate = original;
    candidate.glyphs[0u].planeBottom += 1.f;
    EXPECT_FALSE(ValidateFontAtlasPayload(candidate));
    candidate = original;
    candidate.bakePpem *= 2u;
    EXPECT_FALSE(ValidateFontAtlasPayload(candidate));
    for(FontAtlasGlyph& glyph : candidate.glyphs){
        if(glyph.drawable == 0u)
            continue;
        glyph.planeRight = glyph.planeLeft + 31.25f;
        glyph.planeBottom = glyph.planeTop + 31.25f;
    }
    EXPECT_TRUE(ValidateFontAtlasPayload(candidate));
}

TEST(AssetsFontAtlas, RejectsIncompleteOrDuplicateGlyphPolicyAndUnsupportedLimits){
    using namespace __hidden_font_atlas_tests;
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    AtlasTestArena testArena;
    FontAtlasPayload original = MakePayload(testArena);
    FontAtlasPayload candidate(original);
    candidate.glyphs.pop_back();
    EXPECT_FALSE(ValidateFontAtlasPayload(candidate));
    candidate = original;
    candidate.glyphs[1u].glyphId = 0u;
    EXPECT_FALSE(ValidateFontAtlasPayload(candidate));
    candidate = original;
    candidate.bakePpem = 15u;
    EXPECT_FALSE(ValidateFontAtlasPayload(candidate));
    candidate = original;
    candidate.spreadPixels = 33u;
    EXPECT_FALSE(ValidateFontAtlasPayload(candidate));
    candidate = original;
    candidate.groups[0u].width = 2049u;
    EXPECT_FALSE(ValidateFontAtlasPayload(candidate));
    candidate = original;
    candidate.fontSha256 = {};
    EXPECT_FALSE(ValidateFontAtlasPayload(candidate));
}

TEST(AssetsFontAtlas, OriginalSourcePositioningSetAndBytesMustMatchTheShapingFont){
    using namespace __hidden_font_atlas_tests;
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    AtlasTestArena testArena;
    const Path sourcePath = Path(testArena.arena, NWB_REPO_ROOT) / "impl" / "assets" / "ui" / "fonts" / "default" / "latin.font";
    Core::Assets::AssetBytes bytes(testArena.arena);
    auto bytesResult = ReadBundledFontBytes(sourcePath, bytes.get_allocator().arena());
    ASSERT_TRUE(bytesResult);
    bytes = Move(*bytesResult);
    Font font(testArena.arena, Name("project/fonts/body"));
    font.setFontBytes(Move(bytes));
    ASSERT_TRUE(font.validatePayload());
    FontAtlasPayload payload = MakePayload(testArena);
    payload.fontSha256 = ComputeSha256({ font.fontBytes().data(), font.fontBytes().size() });
    payload.positioningTables.clear();
    const Core::Assets::AssetBytes& source = font.fontBytes();
    const auto directoryResult = FontSfntDirectory::Read({ source.data(), source.size() });
    ASSERT_TRUE(directoryResult);
    const FontSfntDirectory& directory = *directoryResult;
    for(u16 index = 0u; index < directory.tableCount(); ++index){
        const auto sourceTableResult = directory.table(index);
        ASSERT_TRUE(sourceTableResult);
        const FontSfntTable& sourceTable = *sourceTableResult;
        if(sourceTable.tag == s_FontSfntHeadTag){
            const auto unitsPerEmResult = ReadFontSfntU16(sourceTable.bytes, 18u);
            ASSERT_TRUE(unitsPerEmResult);
            payload.unitsPerEm = *unitsPerEmResult;
        }
        if(sourceTable.tag == s_FontSfntMaxpTag){
            const auto glyphCountResult = ReadFontSfntU16(sourceTable.bytes, 4u);
            ASSERT_TRUE(glyphCountResult);
            payload.sourceGlyphCount = *glyphCountResult;
        }
        if(sourceTable.tag == s_FontAtlasKernTag || sourceTable.tag == s_FontAtlasGposTag || sourceTable.tag == s_FontAtlasGdefTag){
            FontAtlasPositioningTable table(testArena.arena);
            table.tag = sourceTable.tag;
            table.bytes.assign(sourceTable.bytes.begin(), sourceTable.bytes.end());
            table.sha256 = ComputeSha256({ table.bytes.data(), table.bytes.size() });
            payload.positioningTables.push_back(Move(table));
        }
    }
    ASSERT_FALSE(payload.positioningTables.empty());
    ASSERT_TRUE(ValidateFontAtlasSourceMatch(payload, font));
    payload.positioningTables[0u].bytes.back() ^= 1u;
    EXPECT_FALSE(ValidateFontAtlasSourceMatch(payload, font));
    payload.positioningTables[0u].bytes.back() ^= 1u;
    payload.positioningTables.pop_back();
    EXPECT_FALSE(ValidateFontAtlasSourceMatch(payload, font));
    payload.font.virtualPath = Name("project/fonts/different");
    EXPECT_FALSE(ValidateFontAtlasSourceMatch(payload, font));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

