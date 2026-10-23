// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/text/service.h>

#include <tests/common/capturing_logger.h>
#include <tests/common/font_fixture.h>

#include <global/filesystem.h>
#include <global/simdmath.h>

#include "baked_font_fixture.h"

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_baked_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using namespace NWB::Impl::Ui;

constexpr AStringView s_LatinFontPath = "tests/ui/fonts/latin";
constexpr AStringView s_SkinPath = "tests/ui/skin";
constexpr AStringView s_SampleGlyphText = "A";

class TextBakedTests : public testing::Test{
public:
    TextBakedTests()
        : m_loggerGuard(m_logger, Core::Common::LoggerBreakPolicy::BreakOnFatal)
        , m_arena(Name("tests/ui/text/baked"))
        , m_scratchArena(Name("tests/ui/text/baked_scratch"))
        , m_font(m_arena, Name(s_LatinFontPath))
        , m_atlas(m_arena, Name("tests/ui/fonts/latin_atlas"))
        , m_text(m_arena)
        , m_layout(m_arena)
        , m_skin(m_arena, Name(s_SkinPath))
        , m_paint(m_arena)
    {}


protected:
    virtual void SetUp()override{
        ASSERT_TRUE(Tests::LoadBundledFont(m_font, m_arena, NWB_TEST_FONT_DIRECTORY, "latin.font"));
        const FontSource source{ Core::Assets::AssetRef<Font>(s_LatinFontPath), m_font, 1u };
        ASSERT_TRUE(m_text.setFonts(m_scratchArena, &source, 1u));
        {
            auto layoutResult = m_text.layout({ .text = "A ", .fontSize = 24.f });
            ASSERT_TRUE(layoutResult);
            m_layout = Move(*layoutResult);
        }
        FontAtlasPayload payload(m_arena);
        payload.font = source.identity;
        payload.fontSha256 = ComputeSha256({ m_font.fontBytes().data(), m_font.fontBytes().size() });
        const Core::Assets::AssetBytes& bytes = m_font.fontBytes();
        UiFixtureSupport::PopulateBakedFontTables(bytes, m_arena, payload);
        payload.bakePpem = 32u;
        payload.ascenderUnits = 1000.f;
        payload.descenderUnits = -250.f;
        payload.glyphs.resize(payload.sourceGlyphCount);
        for(u32 id = 0u; id < payload.sourceGlyphCount; ++id)
            payload.glyphs[id].glyphId = id;
        payload.groups.emplace_back(m_arena);
        FontAtlasGroup& group = payload.groups.back();
        group.width = 32u;
        group.height = 32u;
        group.pixels.resize(32u * 32u * 4u, 128u);
        group.sha256 = ComputeSha256({ group.pixels.data(), group.pixels.size() });
        for(const PlacedGlyph& placed : m_layout.glyphs()){
            FontAtlasGlyph& glyph = payload.glyphs[placed.glyphId];
            if(m_layout.utf8()[placed.byteBegin] == ' ' || glyph.drawable != 0u)
                continue;
            glyph.drawable = 1u;
            glyph.x = 1u;
            glyph.y = 1u;
            glyph.width = 2u;
            glyph.height = 2u;
            glyph.planeLeft = -125.f;
            glyph.planeTop = -875.f;
            const SIMDVectorSingle planeExtent = VectorDivide(VectorSet(static_cast<f32>(glyph.width * payload.unitsPerEm), static_cast<f32>(glyph.height * payload.unitsPerEm), 0.0f, 0.0f), VectorReplicate(static_cast<f32>(payload.bakePpem)));
            const SIMDVectorSingle planeEnd = VectorAdd(VectorSet(glyph.planeLeft, glyph.planeTop, 0.0f, 0.0f), planeExtent);
            glyph.planeRight = VectorGetX(planeEnd);
            glyph.planeBottom = VectorGetY(planeEnd);
        }
        m_atlas.setPayload(Move(payload));
        ASSERT_TRUE(m_atlas.validatePayload(m_scratchArena));
        UiSkin::RegionVector regions(m_arena);
        m_skin.setAtlas(Core::Assets::AssetRef<Texture>("tests/ui/texture"), 4u, 4u, 1.f, Move(regions));
    }

    [[nodiscard]] bool installAtlas(){
        const FontSource source{ Core::Assets::AssetRef<Font>(s_LatinFontPath), m_font, 1u, &m_atlas };
        return m_text.setFonts(m_scratchArena, &source, 1u);
    }

    [[nodiscard]] DrawSnapshot paint(f32 scale = 1.f){
        m_paint.begin({ 800.f, 600.f, scale, scale }, 1u, 1u, Core::Assets::AssetRef<UiSkin>(s_SkinPath), m_skin);
        EXPECT_TRUE(m_text.paint(m_paint, m_layout, { 20.f, 20.f }));
        return m_paint.freeze();
    }


protected:
    Tests::CapturingLogger m_logger;
    Core::Common::LoggerRegistrationGuard m_loggerGuard;
    Core::Alloc::GlobalArena m_arena;
    Core::Alloc::ScratchArena m_scratchArena;
    Font m_font;
    FontAtlas m_atlas;
    TextService m_text;
    TextLayout m_layout;
    UiSkin m_skin;
    PaintBuilder m_paint;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(TextBakedTests, FontHashMismatchUsesCoverageForTheAlreadySelectedFace){
    const auto binary = SerializeFontAtlasPayload(m_atlas.payload(), m_arena, m_scratchArena);
    ASSERT_TRUE(binary);
    auto payload = DeserializeFontAtlasPayload(*binary, m_arena, m_scratchArena);
    ASSERT_TRUE(payload);
    payload->fontSha256.bytes[0u] ^= 1u;
    m_atlas.setPayload(Move(*payload));
    ASSERT_TRUE(installAtlas());
    {
        auto layoutResult = m_text.layout({ s_SampleGlyphText });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    EXPECT_FALSE(m_layout.glyphs()[0u].face->bakedAtlas());
    const DrawSnapshot snapshot = paint();
    EXPECT_TRUE(snapshot.sdfPages().empty());
    EXPECT_FALSE(snapshot.glyphPages().empty());
    EXPECT_EQ(m_layout.glyphs()[0u].face->identity().name(), Name(s_LatinFontPath));
}

TEST_F(TextBakedTests, LogicalFontIdentityMustMatchEvenWhenTheBytesMatch){
    const auto binary = SerializeFontAtlasPayload(m_atlas.payload(), m_arena, m_scratchArena);
    ASSERT_TRUE(binary);
    auto payload = DeserializeFontAtlasPayload(*binary, m_arena, m_scratchArena);
    ASSERT_TRUE(payload);
    payload->font = Core::Assets::AssetRef<Font>("tests/ui/fonts/other");
    m_atlas.setPayload(Move(*payload));
    ASSERT_TRUE(installAtlas());
    {
        auto layoutResult = m_text.layout({ .text = s_SampleGlyphText, .fontSize = 24.f });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    EXPECT_FALSE(m_layout.glyphs()[0u].face->bakedAtlas());
    EXPECT_TRUE(paint().sdfPages().empty());
}

TEST_F(TextBakedTests, OldLayoutAndSnapshotOwnTheirAtlasAfterFontReplacementAndAssetRelease){
    ASSERT_TRUE(installAtlas());
    {
        auto layoutResult = m_text.layout({ .text = s_SampleGlyphText, .fontSize = 24.f });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    const SharedBakedFontAtlas retained = m_layout.glyphs()[0u].face->bakedAtlas();
    const DrawSnapshot first = paint();
    ASSERT_EQ(first.sdfPages().size(), 1u);
    const FontSource source{ Core::Assets::AssetRef<Font>(s_LatinFontPath), m_font, 2u };
    ASSERT_TRUE(m_text.setFonts(m_scratchArena, &source, 1u));
    m_atlas.setPayload(FontAtlasPayload(m_arena));
    const DrawSnapshot second = paint();
    ASSERT_EQ(second.sdfPages().size(), 1u);
    EXPECT_EQ(first.sdfPages()[0u].get(), second.sdfPages()[0u].get());
    EXPECT_EQ(second.sdfPages()[0u]->binding().fontGeneration, 1u);
    EXPECT_EQ(retained.get(), m_layout.glyphs()[0u].face->bakedAtlas().get());
    EXPECT_EQ(first.sdfPages()[0u]->pixels()[0u], 128u);
}

TEST_F(TextBakedTests, OutsideBakedSizeRangeUsesCoverageWithLowerThresholdControl){
    ASSERT_TRUE(installAtlas());
    {
        auto layoutResult = m_text.layout({ .text = s_SampleGlyphText, .fontSize = 24.f });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    const DrawSnapshot first = paint();
    ASSERT_EQ(first.sdfPages().size(), 1u);
    {
        auto layoutResult = m_text.layout({ .text = s_SampleGlyphText, .fontSize = 8.f });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    const DrawSnapshot small = paint();
    EXPECT_TRUE(small.sdfPages().empty());
    EXPECT_FALSE(small.glyphPages().empty());
    {
        auto layoutResult = m_text.layout({ .text = s_SampleGlyphText, .fontSize = 129.f });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    EXPECT_TRUE(paint().sdfPages().empty());
}

TEST_F(TextBakedTests, TextBelowThreeQuarterBakeSizeUsesCoverageAtNormalDpi){
    ASSERT_TRUE(installAtlas());
    {
        auto layoutResult = m_text.layout({ .text = s_SampleGlyphText, .fontSize = 22.f });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    ASSERT_TRUE(m_layout.glyphs()[0u].face->bakedAtlas());
    const DrawSnapshot small = paint();
    EXPECT_TRUE(small.sdfPages().empty());
    ASSERT_EQ(small.glyphPages().size(), 1u);
    ASSERT_EQ(small.commands().size(), 1u);
    EXPECT_EQ(small.commands()[0u].material, PaintMaterial::Glyph);

    {
        auto layoutResult = m_text.layout({ .text = s_SampleGlyphText, .fontSize = 24.f });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    const DrawSnapshot threshold = paint();
    EXPECT_TRUE(threshold.glyphPages().empty());
    ASSERT_EQ(threshold.sdfPages().size(), 1u);
    ASSERT_EQ(threshold.commands().size(), 1u);
    EXPECT_EQ(threshold.commands()[0u].material, PaintMaterial::SdfGlyph);
}

TEST_F(TextBakedTests, FractionalPhysicalSizeBelowSdfThresholdUsesCoverage){
    ASSERT_TRUE(installAtlas());
    {
        auto layoutResult = m_text.layout({ .text = s_SampleGlyphText, .fontSize = 23.5f });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    const DrawSnapshot fractionalFont = paint();
    EXPECT_TRUE(fractionalFont.sdfPages().empty());
    ASSERT_EQ(fractionalFont.glyphPages().size(), 1u);
    ASSERT_EQ(fractionalFont.commands().size(), 1u);
    EXPECT_EQ(fractionalFont.commands()[0u].material, PaintMaterial::Glyph);

    {
        auto layoutResult = m_text.layout({ .text = s_SampleGlyphText, .fontSize = 16.0f });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    const DrawSnapshot fractionalDpi = paint(1.49f);
    EXPECT_TRUE(fractionalDpi.sdfPages().empty());
    ASSERT_EQ(fractionalDpi.glyphPages().size(), 1u);
    ASSERT_EQ(fractionalDpi.commands().size(), 1u);
    EXPECT_EQ(fractionalDpi.commands()[0u].material, PaintMaterial::Glyph);

    const DrawSnapshot thresholdDpi = paint(1.5f);
    EXPECT_TRUE(thresholdDpi.glyphPages().empty());
    ASSERT_EQ(thresholdDpi.sdfPages().size(), 1u);
    ASSERT_EQ(thresholdDpi.commands().size(), 1u);
    EXPECT_EQ(thresholdDpi.commands()[0u].material, PaintMaterial::SdfGlyph);
}

TEST_F(TextBakedTests, WhitespaceKeepsItsAdvanceWithoutAdmittingAnImage){
    ASSERT_TRUE(installAtlas());
    {
        auto layoutResult = m_text.layout({ .text = "   ", .fontSize = 24.f });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    EXPECT_GT(m_layout.measure().x, 0.f);
    const DrawSnapshot snapshot = paint();
    EXPECT_TRUE(snapshot.sdfPages().empty());
    EXPECT_TRUE(snapshot.glyphPages().empty());
    EXPECT_TRUE(snapshot.vertices().empty());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

