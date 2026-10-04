// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_visible_fixture.h"

#include <impl/ecs_ui/toolkit/text/atlas.h>

#include <global/algorithm.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_visible_coverage_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::UiTextVisibleTests;


class TextVisibleCoverageTests : public TextVisibleFixture{
protected:
    [[nodiscard]] static Rect outlineRectangle(const PlacedGlyph& glyph, const Point origin){
        return { origin.x + glyph.position.x + glyph.ink.x, origin.y + glyph.position.y + glyph.ink.y,
            glyph.ink.width, glyph.ink.height };
    }

    [[nodiscard]] static Rect rasterFringe(const Rect& raster, const Rect& outline){
        if(raster.x + 0.001f < outline.x)
            return { raster.x, raster.y, Min(0.05f, (outline.x - raster.x) * 0.5f), raster.height };
        if(raster.y + 0.001f < outline.y)
            return { raster.x, raster.y, raster.width, Min(0.05f, (outline.y - raster.y) * 0.5f) };
        const f32 right = raster.x + raster.width;
        if(right > outline.x + outline.width + 0.001f){
            const f32 width = Min(0.05f, (right - outline.x - outline.width) * 0.5f);
            return { right - width, raster.y, width, raster.height };
        }
        const f32 bottom = raster.y + raster.height;
        if(bottom > outline.y + outline.height + 0.001f){
            const f32 height = Min(0.05f, (bottom - outline.y - outline.height) * 0.5f);
            return { raster.x, bottom - height, raster.width, height };
        }
        return {};
    }


public:
    TextVisibleCoverageTests()
        : m_bitmap(m_arena)
    {}


protected:
    [[nodiscard]] bool paintClipped(const TextLayout& layout, const Rect& clip, const Point origin = { 20.0f, 20.0f }){
        m_paint.pushClip(clip);
        const bool painted = m_text.paint(m_paint, layout, origin);
        const bool popped = m_paint.popClip();
        return painted && popped;
    }

    [[nodiscard]] bool rasterRectangle(const PlacedGlyph& glyph, const f32 fontSize,
        const DisplayMetrics& display, const Point origin, Rect& rectangle){
        const f32 physicalSize = Ceil(fontSize * Max(display.pixelScaleX, display.pixelScaleY));
        if(!glyph.face->rasterize(glyph.glyphId, static_cast<u32>(physicalSize), m_bitmap))
            return false;
        const f32 scale = physicalSize / fontSize;
        const f64 unsnappedX = static_cast<f64>(origin.x) + glyph.position.x + static_cast<f64>(m_bitmap.bearingX) / scale;
        const f64 unsnappedY = static_cast<f64>(origin.y) + glyph.position.y - static_cast<f64>(m_bitmap.bearingY) / scale;
        rectangle = { static_cast<f32>(Floor(unsnappedX * display.pixelScaleX + 0.5) / display.pixelScaleX),
            static_cast<f32>(Floor(unsnappedY * display.pixelScaleY + 0.5) / display.pixelScaleY),
            static_cast<f32>(m_bitmap.width) / scale, static_cast<f32>(m_bitmap.height) / scale };
        return rectangle.width > 0.0f && rectangle.height > 0.0f;
    }


protected:
    GlyphBitmap m_bitmap;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(TextVisibleCoverageTests, MoreThan4096DistinctHiddenHangulDoNotExhaustTheVisiblePrefixCache){
    AString<Core::Alloc::GlobalArena> text("A\n", m_arena);
    const usize hiddenCount = s_GlyphAtlasMaxGlyphs + 1u;
    text.reserve(2u + hiddenCount * 3u);
    for(u32 index = 0u; index < hiddenCount; ++index){
        const u32 scalar = 0xac00u + index;
        text.push_back(static_cast<char>(0xe0u | (scalar >> 12u)));
        text.push_back(static_cast<char>(0x80u | ((scalar >> 6u) & 0x3fu)));
        text.push_back(static_cast<char>(0x80u | (scalar & 0x3fu)));
    }
    ASSERT_EQ(m_text.layout({ text }, m_layout), TextLayoutStatus::Success);
    ASSERT_EQ(m_layout.lines().size(), 2u);
    ASSERT_EQ(m_layout.glyphs().size(), hiddenCount + 1u);
    Core::Alloc::ScratchArena scratch(Name("tests/ui/text/visible_hangul"));
    Vector<u32, Core::Alloc::ScratchArena> ids(scratch);
    ids.reserve(hiddenCount);
    const Rect clip{ 0.0f, 0.0f, 100.0f, 20.0f + m_layout.lines()[1u].top };
    for(const PlacedGlyph& glyph : m_layout.glyphs()){
        if(glyph.byteBegin < 2u)
            continue;
        EXPECT_EQ(glyph.face->identity().name(), Name("tests/ui/fonts/korean"));
        EXPECT_TRUE(glyph.coverage.known);
        EXPECT_GT(glyph.coverage.ink.width, 0.0f);
        EXPECT_GT(glyph.coverage.ink.height, 0.0f);
        EXPECT_GE(outlineRectangle(glyph, { 20.0f, 20.0f }).y, clip.height);
        ids.push_back(glyph.glyphId);
    }
    Sort(ids.begin(), ids.end());
    ASSERT_EQ(ids.size(), hiddenCount);
    for(usize index = 1u; index < ids.size(); ++index)
        ASSERT_NE(ids[index], ids[index - 1u]);
    PaintVector<RawShapedGlyph> native(m_arena);
    ShapeRequest firstHangul{ "\xea\xb0\x80" };
    firstHangul.scriptTag = TextScriptTag('H', 'a', 'n', 'g');
    firstHangul.language = "ko";
    const PlacedGlyph& retained = m_layout.glyphs()[1u];
    ASSERT_TRUE(retained.face->shape(firstHangul, 0u, 3u, native));
    ASSERT_EQ(native.size(), 1u);
    EXPECT_EQ(retained.glyphId, native[0u].glyphId);
    EXPECT_EQ(retained.coverage.known, native[0u].coverage.known);
    ExpectRectangle(retained.coverage.ink, native[0u].coverage.ink);
    ExpectRectangle(retained.ink, native[0u].ink);
    beginPaint(1u);
    ASSERT_TRUE(paintClipped(m_layout, clip));
    const DrawSnapshot prefix = m_paint.freeze();
    ASSERT_EQ(prefix.glyphPages().size(), 1u);
    EXPECT_EQ(prefix.glyphPages()[0u]->binding().font.name(), Name("tests/ui/fonts/latin"));
    EXPECT_EQ(prefix.vertices().size(), 4u);
    EXPECT_EQ(prefix.indices().size(), 6u);
    const PlacedGlyph& reveal = m_layout.glyphs()[hiddenCount / 2u];
    const Point origin{ 20.0f - reveal.position.x - reveal.ink.x, 20.0f - reveal.position.y - reveal.ink.y };
    beginPaint(2u);
    ASSERT_TRUE(paintClipped(m_layout, { 20.0f, 20.0f, 80.0f, 40.0f }, origin));
    const DrawSnapshot revealed = m_paint.freeze();
    ASSERT_EQ(revealed.glyphPages().size(), 1u);
    EXPECT_EQ(revealed.glyphPages()[0u]->binding().font.name(), Name("tests/ui/fonts/korean"));
    EXPECT_FALSE(revealed.vertices().empty());
    beginPaint(3u);
    ASSERT_TRUE(paintClipped(m_layout, clip));
    const DrawSnapshot warm = m_paint.freeze();
    ASSERT_EQ(warm.glyphPages().size(), 1u);
    EXPECT_EQ(warm.glyphPages()[0u].get(), prefix.glyphPages()[0u].get());
}

TEST_F(TextVisibleCoverageTests, HiddenGlyphKeepsTheWarmPageExactUntilItIsRevealed){
    ASSERT_EQ(m_text.layout({ "A\nB" }, m_layout), TextLayoutStatus::Success);
    ASSERT_EQ(m_layout.lines().size(), 2u);
    const Rect clip{ 0.0f, 0.0f, 100.0f, 20.0f + m_layout.lines()[1u].top };
    beginPaint(1u);
    ASSERT_TRUE(paintClipped(m_layout, clip));
    const DrawSnapshot first = m_paint.freeze();
    ASSERT_EQ(first.glyphPages().size(), 1u);
    ASSERT_EQ(first.vertices().size(), 4u);
    const SharedGlyphPage original = first.glyphPages()[0u];
    Core::Alloc::ScratchArena scratch(Name("tests/ui/text/visible_retained_pixels"));
    Vector<u8, Core::Alloc::ScratchArena> pixels(scratch);
    pixels.assign(original->pixels().begin(), original->pixels().end());
    beginPaint(2u);
    ASSERT_TRUE(paintClipped(m_layout, clip));
    const DrawSnapshot hidden = m_paint.freeze();
    ExpectSamePaint(hidden, first);
    beginPaint(3u);
    ASSERT_TRUE(m_text.paint(m_paint, m_layout, { 20.0f, 20.0f }));
    const DrawSnapshot revealed = m_paint.freeze();
    ASSERT_EQ(revealed.glyphPages().size(), 1u);
    EXPECT_NE(revealed.glyphPages()[0u].get(), original.get());
    EXPECT_GT(revealed.glyphPages()[0u]->binding().generation, original->binding().generation);
    EXPECT_EQ(revealed.vertices().size(), 8u);
    ASSERT_EQ(original->pixels().size(), pixels.size());
    EXPECT_EQ(GLOBAL_MEMCMP(original->pixels().data(), pixels.data(), pixels.size()), 0);
}

TEST_F(TextVisibleCoverageTests, HiddenOversizedGlyphCannotRejectOrUpgradePriorVisibleCoverage){
    ASSERT_EQ(m_text.layout({ "A" }, m_layout), TextLayoutStatus::Success);
    TextLayout oversized(m_arena);
    ASSERT_EQ(m_text.layout({ .text = "W", .fontSize = 1024.0f }, oversized), TextLayoutStatus::Success);
    beginPaint(1u);
    ASSERT_TRUE(m_text.paint(m_paint, m_layout, { 20.0f, 20.0f }));
    const DrawSnapshot before = m_paint.freeze();
    beginPaint(2u);
    ASSERT_TRUE(m_text.paint(m_paint, m_layout, { 20.0f, 20.0f }));
    ASSERT_TRUE(m_text.paint(m_paint, oversized, { 10000.0f, 10000.0f }));
    const DrawSnapshot hidden = m_paint.freeze();
    ExpectSamePaint(hidden, before);
    ASSERT_EQ(m_text.layout({ "W" }, m_layout), TextLayoutStatus::Success);
    beginPaint(3u);
    ASSERT_TRUE(m_text.paint(m_paint, m_layout, { 20.0f, 20.0f }));
    const DrawSnapshot ordinary = m_paint.freeze();
    ASSERT_EQ(ordinary.glyphPages().size(), 1u);
    EXPECT_GT(ordinary.glyphPages()[0u]->binding().generation, before.glyphPages()[0u]->binding().generation);
    EXPECT_EQ(ordinary.vertices().size(), 4u);
}

TEST_F(TextVisibleCoverageTests, EmptyIntersectedClipAndZeroAlphaNeverPrepareOversizedCoverage){
    ASSERT_EQ(m_text.layout({ .text = "W", .fontSize = 1024.0f }, m_layout), TextLayoutStatus::Success);
    beginPaint(1u);
    m_paint.fillRect({ 1.0f, 1.0f, 4.0f, 4.0f });
    m_paint.pushClip({ 10.0f, 10.0f, 20.0f, 20.0f });
    m_paint.pushClip({ 40.0f, 40.0f, 20.0f, 20.0f });
    EXPECT_TRUE(m_text.paint(m_paint, m_layout, { 20.0f, 20.0f }));
    ASSERT_TRUE(m_paint.popClip());
    ASSERT_TRUE(m_paint.popClip());
    EXPECT_TRUE(m_text.paint(m_paint, m_layout, { 20.0f, 20.0f }, { 1.0f, 1.0f, 1.0f, 0.0f }));
    const DrawSnapshot snapshot = m_paint.freeze();
    EXPECT_TRUE(snapshot.glyphPages().empty());
    EXPECT_TRUE(snapshot.sdfPages().empty());
    EXPECT_EQ(snapshot.vertices().size(), 4u);
    EXPECT_EQ(snapshot.indices().size(), 6u);
    ASSERT_EQ(m_text.layout({ "W" }, m_layout), TextLayoutStatus::Success);
    beginPaint(2u);
    ASSERT_TRUE(m_text.paint(m_paint, m_layout, { 20.0f, 20.0f }));
    EXPECT_FALSE(m_paint.freeze().glyphPages().empty());
}

TEST_F(TextVisibleCoverageTests, FractionalUnequalDpiKeepsAHintedRasterSliverOutsideTheShapedOutline){
    const DisplayMetrics display{ 200.0f, 100.0f, 1.25f, 1.5f };
    const Point origin{ 40.375f, 20.125f };
    ASSERT_EQ(m_text.layout({ .text = "W", .fontSize = 17.25f }, m_layout), TextLayoutStatus::Success);
    ASSERT_EQ(m_layout.glyphs().size(), 1u);
    Rect raster;
    ASSERT_TRUE(rasterRectangle(m_layout.glyphs()[0u], m_layout.fontSize(), display, origin, raster));
    const Rect clip = rasterFringe(raster, outlineRectangle(m_layout.glyphs()[0u], origin));
    ASSERT_GT(clip.width, 0.0f);
    ASSERT_GT(clip.height, 0.0f);
    const Point measured = m_layout.measure();
    beginPaint(1u, display);
    ASSERT_TRUE(paintClipped(m_layout, clip, origin));
    const DrawSnapshot snapshot = m_paint.freeze();
    ASSERT_EQ(snapshot.glyphPages().size(), 1u);
    ASSERT_EQ(snapshot.vertices().size(), 4u);
    EXPECT_FLOAT_EQ(snapshot.vertices()[0u].position.x, clip.x);
    EXPECT_FLOAT_EQ(snapshot.vertices()[0u].position.y, clip.y);
    EXPECT_NEAR(snapshot.vertices()[2u].position.x, clip.x + clip.width, 0.0001f);
    EXPECT_NEAR(snapshot.vertices()[2u].position.y, clip.y + clip.height, 0.0001f);
    EXPECT_FLOAT_EQ(m_layout.measure().x, measured.x);
    EXPECT_FLOAT_EQ(m_layout.measure().y, measured.y);
}

TEST_F(TextVisibleCoverageTests, FractionalOriginPlacesNativeCoverageOnThePhysicalPixelGrid){
    const DisplayMetrics display{ 200.0f, 100.0f, 1.25f, 1.5f };
    const Point origin{ 40.375f, 20.125f };
    ASSERT_EQ(m_text.layout({ .text = "g", .fontSize = 17.25f }, m_layout), TextLayoutStatus::Success);
    ASSERT_EQ(m_layout.glyphs().size(), 1u);
    Rect raster;
    ASSERT_TRUE(rasterRectangle(m_layout.glyphs()[0u], m_layout.fontSize(), display, origin, raster));
    beginPaint(1u, display);
    ASSERT_TRUE(m_text.paint(m_paint, m_layout, origin));
    const DrawSnapshot snapshot = m_paint.freeze();
    ASSERT_EQ(snapshot.glyphPages().size(), 1u);
    ASSERT_EQ(snapshot.vertices().size(), 4u);
    EXPECT_NEAR(snapshot.vertices()[0u].position.x, raster.x, 0.0001f);
    EXPECT_NEAR(snapshot.vertices()[0u].position.y, raster.y, 0.0001f);
    EXPECT_NEAR(raster.x * display.pixelScaleX, Floor(raster.x * display.pixelScaleX + 0.5f), 0.0001f);
    EXPECT_NEAR(raster.y * display.pixelScaleY, Floor(raster.y * display.pixelScaleY + 0.5f), 0.0001f);
}

TEST_F(TextVisibleCoverageTests, NegativeBearingRemainsVisibleToTheLeftOfTheGlyphOrigin){
    const Point origin{ 50.0f, 20.0f };
    const DisplayMetrics display{ 200.0f, 100.0f, 1.25f, 1.25f };
    ASSERT_EQ(m_text.layout({ .text = "j", .fontSize = 32.0f }, m_layout), TextLayoutStatus::Success);
    ASSERT_EQ(m_layout.glyphs().size(), 1u);
    Rect raster;
    ASSERT_TRUE(rasterRectangle(m_layout.glyphs()[0u], m_layout.fontSize(), display, origin, raster));
    const f32 glyphOrigin = origin.x + m_layout.glyphs()[0u].position.x;
    ASSERT_LT(raster.x, glyphOrigin);
    const Rect clip{ raster.x, raster.y, (glyphOrigin - raster.x) * 0.5f, raster.height };
    beginPaint(1u, display);
    ASSERT_TRUE(paintClipped(m_layout, clip, origin));
    const DrawSnapshot snapshot = m_paint.freeze();
    ASSERT_EQ(snapshot.vertices().size(), 4u);
    EXPECT_LT(snapshot.vertices()[2u].position.x, glyphOrigin);
    EXPECT_EQ(snapshot.glyphPages().size(), 1u);
}

TEST_F(TextVisibleCoverageTests, CombiningMarkOnlyClipKeepsItsOffsetGlyphAndExcludesTheBaseQuad){
    const Point origin{ 50.0f, 20.0f };
    const DisplayMetrics display{ 200.0f, 100.0f, 1.5f, 1.5f };
    ASSERT_EQ(m_text.layout({ .text = "x\xcc\x81", .fontSize = 32.0f }, m_layout), TextLayoutStatus::Success);
    ASSERT_EQ(m_layout.glyphs().size(), 2u);
    ASSERT_EQ(m_layout.clusters().size(), 1u);
    Rect base;
    Rect mark;
    ASSERT_TRUE(rasterRectangle(m_layout.glyphs()[0u], m_layout.fontSize(), display, origin, base));
    ASSERT_TRUE(rasterRectangle(m_layout.glyphs()[1u], m_layout.fontSize(), display, origin, mark));
    ASSERT_LT(mark.y, base.y);
    const Rect clip{ mark.x, mark.y, mark.width, Min(mark.height, base.y - mark.y) * 0.5f };
    beginPaint(1u, display);
    ASSERT_TRUE(paintClipped(m_layout, clip, origin));
    const DrawSnapshot snapshot = m_paint.freeze();
    ASSERT_EQ(snapshot.vertices().size(), 4u);
    ASSERT_EQ(snapshot.indices().size(), 6u);
    EXPECT_LT(snapshot.vertices()[2u].position.y, base.y);
    EXPECT_EQ(snapshot.glyphPages().size(), 1u);
}

TEST_F(TextVisibleCoverageTests, SixtyThreePriorImagesLeaveRoomForOnlyTheVisibleCoverageFace){
    ASSERT_EQ(m_text.layout({ "A\n\xed\x95\x9c" }, m_layout), TextLayoutStatus::Success);
    ASSERT_EQ(m_layout.lines().size(), 2u);
    Array<SharedGlyphPage, s_PaintMaxImages - 1u> prior{};
    for(usize index = 0u; index < prior.size(); ++index){
        prior[index] = makePage(Limit<u64>::s_Max - index);
        ASSERT_TRUE(prior[index]);
    }
    beginPaint(1u);
    ASSERT_TRUE(m_paint.prepareGlyphPages(prior.data(), prior.size()));
    const Rect clip{ 0.0f, 0.0f, 100.0f, 20.0f + m_layout.lines()[1u].top };
    ASSERT_TRUE(paintClipped(m_layout, clip));
    const DrawSnapshot snapshot = m_paint.freeze();
    ASSERT_EQ(snapshot.glyphPages().size(), s_PaintMaxImages);
    EXPECT_EQ(snapshot.vertices().size(), 4u);
    EXPECT_TRUE(snapshot.sdfPages().empty());
    for(usize index = 0u; index < prior.size(); ++index)
        EXPECT_EQ(snapshot.glyphPages()[index].get(), prior[index].get());
    EXPECT_EQ(snapshot.glyphPages().back()->binding().font.name(), Name("tests/ui/fonts/latin"));
    beginPaint(2u);
    ASSERT_TRUE(m_text.paint(m_paint, m_layout, { 20.0f, 20.0f }));
    const DrawSnapshot revealed = m_paint.freeze();
    EXPECT_EQ(revealed.glyphPages().size(), 2u);
    EXPECT_EQ(revealed.vertices().size(), 8u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

