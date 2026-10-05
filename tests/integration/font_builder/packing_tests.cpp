// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <utilities/font_builder/bake.h>

#include <impl/assets_font_atlas/model.h>
#include <tests/common/capturing_logger.h>
#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_atlas_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using namespace NWB::FontBuilderUtility;

void Append16(Core::Assets::AssetBytes& bytes, const u16 value){
    bytes.push_back(static_cast<u8>(value >> 8u));
    bytes.push_back(static_cast<u8>(value));
}

void Append32(Core::Assets::AssetBytes& bytes, const u32 value){
    Append16(bytes, static_cast<u16>(value >> 16u));
    Append16(bytes, static_cast<u16>(value));
}

FontAtlasPositioningTable ClassPairFixture(Core::Assets::AssetArena& arena){
    FontAtlasPositioningTable table(arena);
    table.tag = s_FontAtlasGposTag;
    auto& bytes = table.bytes;
    Append32(bytes, 0x00010000u);
    Append16(bytes, 10u); Append16(bytes, 12u); Append16(bytes, 26u);
    Append16(bytes, 0u);
    Append16(bytes, 1u); Append32(bytes, 0x6b65726eu); Append16(bytes, 8u);
    Append16(bytes, 0u); Append16(bytes, 1u); Append16(bytes, 0u);
    Append16(bytes, 1u); Append16(bytes, 4u);
    Append16(bytes, 2u); Append16(bytes, 0u); Append16(bytes, 1u); Append16(bytes, 8u);
    Append16(bytes, 2u); Append16(bytes, 24u); Append16(bytes, 4u); Append16(bytes, 0u);
    Append16(bytes, 30u); Append16(bytes, 38u); Append16(bytes, 2u); Append16(bytes, 2u);
    Append16(bytes, 0u); Append16(bytes, 0u); Append16(bytes, static_cast<u16>(-20)); Append16(bytes, static_cast<u16>(-40));
    Append16(bytes, 1u); Append16(bytes, 1u); Append16(bytes, 1u);
    Append16(bytes, 1u); Append16(bytes, 1u); Append16(bytes, 1u); Append16(bytes, 1u);
    Append16(bytes, 1u); Append16(bytes, 2u); Append16(bytes, 1u); Append16(bytes, 1u);
    return table;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(FontAtlasPacking, TightRectanglesAndAllChannelCountsPreserveGuardedPixels){
    using namespace __hidden_font_atlas_tests;
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    Core::Alloc::GlobalArena arena(Name("tests/font_atlas/packing"));
    Core::Alloc::ScratchArena scratch(Name("tests/font_atlas/packing/scratch"));
    BakeOptions options(arena);
    options.extent = 32u;
    options.maxGroups = 2u;
    for(u32 tailChannels = 1u; tailChannels <= 4u; ++tailChannels){
        RasterGlyphs glyphs(arena);
        for(u32 index = 0u; index < 4u + tailChannels; ++index){
            RasterGlyph glyph(arena);
            glyph.record.glyphId = index;
            glyph.record.drawable = 1u;
            glyph.record.width = 28u;
            glyph.record.height = 29u;
            glyph.pixels.resize(28u * 29u, static_cast<u8>((index + 1u) * 17u));
            glyphs.push_back(Move(glyph));
        }
        FontAtlasPayload payload(arena);
        ASSERT_TRUE(PackGlyphs(options, glyphs, payload, scratch));
        ASSERT_EQ(payload.groups.size(), 2u);
        EXPECT_EQ(payload.groups[0u].channelCount, 4u);
        EXPECT_EQ(payload.groups[1u].channelCount, tailChannels);
        for(const auto& group : payload.groups){
            EXPECT_EQ(group.width, 30u);
            EXPECT_EQ(group.height, 31u);
            EXPECT_EQ(group.pixels.size(), 30u * 31u * group.channelCount);
        }
        for(u32 index = 0u; index < glyphs.size(); ++index){
            const auto& glyph = payload.glyphs[index];
            EXPECT_EQ(glyph.group, index / 4u);
            EXPECT_EQ(glyph.channel, index % 4u);
            EXPECT_EQ(glyph.x, 1u);
            EXPECT_EQ(glyph.y, 1u);
            const auto& group = payload.groups[glyph.group];
            for(u32 y = 0u; y < group.height; ++y){
                for(u32 x = 0u; x < group.width; ++x){
                    const bool guard = x == 0u || y == 0u || x + 1u == group.width || y + 1u == group.height;
                    const u8 expected = guard ? 0u : static_cast<u8>((index + 1u) * 17u);
                    EXPECT_EQ(group.pixels[(y * group.width + x) * group.channelCount + glyph.channel], expected);
                }
            }
        }
    }
}

TEST(FontAtlasPacking, RejectsCapacityWithoutDroppingGlyphs){
    using namespace __hidden_font_atlas_tests;
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    Core::Alloc::GlobalArena arena(Name("tests/font_atlas/capacity"));
    Core::Alloc::ScratchArena scratch(Name("tests/font_atlas/capacity/scratch"));
    BakeOptions options(arena);
    options.extent = 32u;
    options.maxGroups = 1u;
    RasterGlyphs glyphs(arena);
    for(u32 index = 0u; index < 5u; ++index){
        RasterGlyph glyph(arena);
        glyph.record.glyphId = index;
        glyph.record.drawable = 1u;
        glyph.record.width = 28u;
        glyph.record.height = 28u;
        glyph.pixels.resize(28u * 28u, 128u);
        glyphs.push_back(Move(glyph));
    }
    FontAtlasPayload payload(arena);
    EXPECT_FALSE(PackGlyphs(options, glyphs, payload, scratch));
}

TEST(FontAtlasPositioning, LegacyPairsStayBoundedAndSorted){
    using namespace __hidden_font_atlas_tests;
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    Core::Alloc::GlobalArena arena(Name("tests/font_atlas/kern"));
    FontAtlasPositioningTable table(arena);
    table.tag = s_FontAtlasKernTag;
    auto& bytes = table.bytes;
    Append16(bytes, 0u); Append16(bytes, 1u);
    Append16(bytes, 0u); Append16(bytes, 20u); Append16(bytes, 1u);
    Append16(bytes, 1u); Append16(bytes, 6u); Append16(bytes, 0u); Append16(bytes, 0u);
    Append16(bytes, 1u); Append16(bytes, 2u); Append16(bytes, static_cast<u16>(-40));
    EXPECT_TRUE(ValidateFontAtlasPositioningTable(table, 3u));
    EXPECT_FALSE(ValidateFontAtlasPositioningTable(table, 2u));
    bytes.pop_back();
    EXPECT_FALSE(ValidateFontAtlasPositioningTable(table, 3u));
}

TEST(FontAtlasPositioning, GposClassZeroAndExplicitClassMatrixPreserved){
    using namespace __hidden_font_atlas_tests;
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    Core::Alloc::GlobalArena arena(Name("tests/font_atlas/gpos"));
    auto table = ClassPairFixture(arena);
    ASSERT_EQ(table.bytes.size(), 84u);
    EXPECT_TRUE(ValidateFontAtlasPositioningTable(table, 3u));
    table.bytes[75u] = 2u;
    EXPECT_FALSE(ValidateFontAtlasPositioningTable(table, 3u));
    table.bytes[75u] = 1u;
    table.bytes[53u] = 255u;
    EXPECT_FALSE(ValidateFontAtlasPositioningTable(table, 3u));
}

TEST(FontAtlasPositioning, MalformedTopOffsetsAndExtensionTypeRejected){
    using namespace __hidden_font_atlas_tests;
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    Core::Alloc::GlobalArena arena(Name("tests/font_atlas/gpos_malformed"));
    auto table = ClassPairFixture(arena);
    table.bytes[8u] = 255u;
    EXPECT_FALSE(ValidateFontAtlasPositioningTable(table, 3u));
    table = ClassPairFixture(arena);
    table.bytes[31u] = 9u;
    EXPECT_FALSE(ValidateFontAtlasPositioningTable(table, 3u));
}


TEST(FontAtlasPositioning, PairSetDeviceOffsetsUseImmediateParent){
    using namespace __hidden_font_atlas_tests;
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    Core::Alloc::GlobalArena arena(Name("tests/font_atlas/gpos_device"));
    auto table = ClassPairFixture(arena);
    auto& bytes = table.bytes;
    bytes.resize(38u);
    Append16(bytes, 1u); Append16(bytes, 28u); Append16(bytes, 0x0044u); Append16(bytes, 0u);
    Append16(bytes, 1u); Append16(bytes, 12u);
    Append16(bytes, 1u); Append16(bytes, 2u); Append16(bytes, static_cast<u16>(-40)); Append16(bytes, 8u);
    Append16(bytes, 8u); Append16(bytes, 10u); Append16(bytes, 1u); Append16(bytes, 0u);
    Append16(bytes, 1u); Append16(bytes, 1u); Append16(bytes, 1u);
    ASSERT_EQ(bytes.size(), 72u);
    EXPECT_TRUE(ValidateFontAtlasPositioningTable(table, 3u));
    bytes[57u] = 24u;
    EXPECT_FALSE(ValidateFontAtlasPositioningTable(table, 3u));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

