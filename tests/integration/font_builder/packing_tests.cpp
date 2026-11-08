// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/capturing_logger.h>
#include <tests/integration/font_builder/bake_name_cases.adhd.h>

#include <utilities/font_builder/bake.h>

#include <impl/assets_font_atlas/model.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_packing_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using namespace NWB::FontBuilderUtility;

void Append16(const u16 value, Core::Assets::AssetBytes& bytes){
    bytes.push_back(static_cast<u8>(value >> 8u));
    bytes.push_back(static_cast<u8>(value));
}

void Append32(const u32 value, Core::Assets::AssetBytes& bytes){
    Append16(static_cast<u16>(value >> 16u), bytes);
    Append16(static_cast<u16>(value), bytes);
}

FontAtlasPositioningTable ClassPairFixture(Core::Assets::AssetArena& arena){
    FontAtlasPositioningTable table(arena);
    table.tag = s_FontAtlasGposTag;
    auto& bytes = table.bytes;
    Append32(0x00010000u, bytes);
    Append16(10u, bytes); Append16(12u, bytes); Append16(26u, bytes);
    Append16(0u, bytes);
    Append16(1u, bytes); Append32(0x6b65726eu, bytes); Append16(8u, bytes);
    Append16(0u, bytes); Append16(1u, bytes); Append16(0u, bytes);
    Append16(1u, bytes); Append16(4u, bytes);
    Append16(2u, bytes); Append16(0u, bytes); Append16(1u, bytes); Append16(8u, bytes);
    Append16(2u, bytes); Append16(24u, bytes); Append16(4u, bytes); Append16(0u, bytes);
    Append16(30u, bytes); Append16(38u, bytes); Append16(2u, bytes); Append16(2u, bytes);
    Append16(0u, bytes); Append16(0u, bytes); Append16(static_cast<u16>(-20), bytes); Append16(static_cast<u16>(-40), bytes);
    Append16(1u, bytes); Append16(1u, bytes); Append16(1u, bytes);
    Append16(1u, bytes); Append16(1u, bytes); Append16(1u, bytes); Append16(1u, bytes);
    Append16(1u, bytes); Append16(2u, bytes); Append16(1u, bytes); Append16(1u, bytes);
    return table;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(FontAtlasPacking, RejectsInvalidBakeBoundsAndMissingPaths){
    using namespace __hidden_packing_tests;
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::ReportOnly);
    Core::Alloc::GlobalArena arena{Name(NWB::Tests::FontBakeCases::s_FontBakeNameTestsFontAtlasOptions)};
    BakeOptions options(arena);
    options.source = AStringView("font.ttf");
    options.output = AStringView("font.nwb");
    options.ppem = s_FontAtlasMinBakePpem - 1u;
    EXPECT_FALSE(ValidateOptions(options));
    options.ppem = s_FontAtlasMinBakePpem;
    options.source = AStringView();
    EXPECT_FALSE(ValidateOptions(options));
    options.source = AStringView("font.ttf");
    options.output = AStringView();
    EXPECT_FALSE(ValidateOptions(options));
    EXPECT_EQ(logger.errorCount(), 3u);
    EXPECT_EQ(ComputeBakePixelCapacity(2048u, 8u), 128ull * 1024ull * 1024ull);
}

TEST(FontAtlasPacking, TightRectanglesAndAllChannelCountsPreserveGuardedPixels){
    using namespace __hidden_packing_tests;
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    Core::Alloc::GlobalArena arena{Name(NWB::Tests::FontBakeCases::s_FontBakeNameTestsFontAtlasPacking)};
    Core::Alloc::ScratchArena scratch{Name(NWB::Tests::FontBakeCases::s_FontBakeNameTestsFontAtlasPackingScratch)};
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
        ASSERT_TRUE(PackGlyphs(options, glyphs, scratch, payload));
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
                    const b8 guard = x == 0u || y == 0u || x + 1u == group.width || y + 1u == group.height;
                    const u8 expected = guard ? 0u : static_cast<u8>((index + 1u) * 17u);
                    EXPECT_EQ(group.pixels[(y * group.width + x) * group.channelCount + glyph.channel], expected);
                }
            }
        }
    }
}

TEST(FontAtlasPacking, RejectsCapacityWithoutDroppingGlyphs){
    using namespace __hidden_packing_tests;
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    Core::Alloc::GlobalArena arena{Name(NWB::Tests::FontBakeCases::s_FontBakeNameTestsFontAtlasCapacity)};
    Core::Alloc::ScratchArena scratch{Name(NWB::Tests::FontBakeCases::s_FontBakeNameTestsFontAtlasCapacityScratch)};
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
    EXPECT_FALSE(PackGlyphs(options, glyphs, scratch, payload));
}

TEST(FontAtlasPositioning, LegacyPairsStayBoundedAndSorted){
    using namespace __hidden_packing_tests;
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    Core::Alloc::GlobalArena arena{Name(NWB::Tests::FontBakeCases::s_FontBakeNameTestsFontAtlasKern)};
    FontAtlasPositioningTable table(arena);
    table.tag = s_FontAtlasKernTag;
    auto& bytes = table.bytes;
    Append16(0u, bytes); Append16(1u, bytes);
    Append16(0u, bytes); Append16(20u, bytes); Append16(1u, bytes);
    Append16(1u, bytes); Append16(6u, bytes); Append16(0u, bytes); Append16(0u, bytes);
    Append16(1u, bytes); Append16(2u, bytes); Append16(static_cast<u16>(-40), bytes);
    EXPECT_TRUE(ValidateFontAtlasPositioningTableBytes(table.tag, table.bytes, 3u));
    EXPECT_FALSE(ValidateFontAtlasPositioningTableBytes(table.tag, table.bytes, 2u));
    bytes.pop_back();
    EXPECT_FALSE(ValidateFontAtlasPositioningTableBytes(table.tag, table.bytes, 3u));
}

TEST(FontAtlasPositioning, GposClassZeroAndExplicitClassMatrixPreserved){
    using namespace __hidden_packing_tests;
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    Core::Alloc::GlobalArena arena{Name(NWB::Tests::FontBakeCases::s_FontBakeNameTestsFontAtlasGpos)};
    auto table = ClassPairFixture(arena);
    ASSERT_EQ(table.bytes.size(), 84u);
    EXPECT_TRUE(ValidateFontAtlasPositioningTableBytes(table.tag, table.bytes, 3u));
    table.bytes[75u] = 2u;
    EXPECT_FALSE(ValidateFontAtlasPositioningTableBytes(table.tag, table.bytes, 3u));
    table.bytes[75u] = 1u;
    table.bytes[53u] = 255u;
    EXPECT_FALSE(ValidateFontAtlasPositioningTableBytes(table.tag, table.bytes, 3u));
}

TEST(FontAtlasPositioning, TruncatedGdefMarkGlyphSetsAreRejected){
    using namespace __hidden_packing_tests;
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    Core::Alloc::GlobalArena arena{Name(NWB::Tests::FontBakeCases::s_FontBakeNameTestsFontAtlasGdefTruncated)};
    FontAtlasPositioningTable table(arena);
    table.tag = s_FontAtlasGdefTag;
    for(const u16 minor : { u16(2), u16(3) }){
        table.bytes.clear();
        Append16(1u, table.bytes); Append16(minor, table.bytes);
        for(u32 field = 0u; field < 4u; ++field)
            Append16(0u, table.bytes);
        Append16(0xffffu, table.bytes);
        if(minor == 3u)
            Append32(0u, table.bytes);
        EXPECT_FALSE(ValidateFontAtlasPositioningTableBytes(table.tag, table.bytes, 3u));
    }
}

TEST(FontAtlasPositioning, MalformedTopOffsetsAndExtensionTypeRejected){
    using namespace __hidden_packing_tests;
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    Core::Alloc::GlobalArena arena{Name(NWB::Tests::FontBakeCases::s_FontBakeNameTestsFontAtlasGposMalformed)};
    auto table = ClassPairFixture(arena);
    table.bytes[8u] = 255u;
    EXPECT_FALSE(ValidateFontAtlasPositioningTableBytes(table.tag, table.bytes, 3u));
    table = ClassPairFixture(arena);
    table.bytes[31u] = 9u;
    EXPECT_FALSE(ValidateFontAtlasPositioningTableBytes(table.tag, table.bytes, 3u));
}


TEST(FontAtlasPositioning, PairSetDeviceOffsetsUseImmediateParent){
    using namespace __hidden_packing_tests;
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    Core::Alloc::GlobalArena arena{Name(NWB::Tests::FontBakeCases::s_FontBakeNameTestsFontAtlasGposDevice)};
    auto table = ClassPairFixture(arena);
    auto& bytes = table.bytes;
    bytes.resize(38u);
    Append16(1u, bytes); Append16(28u, bytes); Append16(0x0044u, bytes); Append16(0u, bytes);
    Append16(1u, bytes); Append16(12u, bytes);
    Append16(1u, bytes); Append16(2u, bytes); Append16(static_cast<u16>(-40), bytes); Append16(8u, bytes);
    Append16(8u, bytes); Append16(10u, bytes); Append16(1u, bytes); Append16(0u, bytes);
    Append16(1u, bytes); Append16(1u, bytes); Append16(1u, bytes);
    ASSERT_EQ(bytes.size(), 72u);
    EXPECT_TRUE(ValidateFontAtlasPositioningTableBytes(table.tag, table.bytes, 3u));
    bytes[57u] = 24u;
    EXPECT_FALSE(ValidateFontAtlasPositioningTableBytes(table.tag, table.bytes, 3u));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

