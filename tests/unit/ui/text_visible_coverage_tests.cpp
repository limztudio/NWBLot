// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_visible_fixture.h"
#include "baked_font_fixture.h"

#include <impl/ecs_ui/toolkit/text/atlas.h>
#include <impl/ecs_ui/toolkit/text/glyph_visibility.h>

#include <global/algorithm.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_visible_coverage_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::UiTextVisibleTests;


class TextVisibleCoverageTests : public TextVisibleFixture{
protected:
    [[nodiscard]] static Rect OutlineRectangle(const PlacedGlyph& glyph, const Point origin){
        return { origin.x + glyph.position.x + glyph.ink.x, origin.y + glyph.position.y + glyph.ink.y, glyph.ink.width, glyph.ink.height };
    }

    [[nodiscard]] static Rect RasterFringe(const Rect& raster, const Rect& outline){
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

    [[nodiscard]] Expected<Rect> rasterRectangle(const PlacedGlyph& glyph, const f32 fontSize, const DisplayMetrics& display, const Point origin){
        const f32 physicalSize = Ceil(fontSize * Max(display.pixelScaleX, display.pixelScaleY));
        if(!glyph.face->rasterize(glyph.glyphId, static_cast<u32>(physicalSize), m_bitmap))
            return MakeUnexpected(Failure{});
        const f32 scale = physicalSize / fontSize;
        const f64 unsnappedX = static_cast<f64>(origin.x) + glyph.position.x + static_cast<f64>(m_bitmap.bearingX) / scale;
        const f64 unsnappedY = static_cast<f64>(origin.y) + glyph.position.y - static_cast<f64>(m_bitmap.bearingY) / scale;
        const Rect rectangle{ static_cast<f32>(Floor(unsnappedX * display.pixelScaleX + 0.5) / display.pixelScaleX),
            static_cast<f32>(Floor(unsnappedY * display.pixelScaleY + 0.5) / display.pixelScaleY),
            static_cast<f32>(m_bitmap.width) / scale, static_cast<f32>(m_bitmap.height) / scale };
        if(!(rectangle.width > 0.0f && rectangle.height > 0.0f))
            return MakeUnexpected(Failure{});
        return rectangle;
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
    {
        auto layoutResult = m_text.layout({ text });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    ASSERT_EQ(m_layout.lines().size(), 2u);
    ASSERT_EQ(m_layout.glyphs().size(), hiddenCount + 1u);
    ASSERT_TRUE(m_text.measure(m_layout, { 800.0f, 600.0f, 1.0f, 1.0f }));
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
        EXPECT_GE(OutlineRectangle(glyph, { 20.0f, 20.0f }).y, clip.height);
        ids.push_back(glyph.glyphId);
    }
    Sort(ids.begin(), ids.end());
    ASSERT_EQ(ids.size(), hiddenCount);
    for(usize index = 1u; index < ids.size(); ++index)
        ASSERT_NE(ids[index], ids[index - 1u]);
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
    {
        auto layoutResult = m_text.layout({ "A\nB" });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
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
    EXPECT_EQ(NWB_MEMCMP(original->pixels().data(), pixels.data(), pixels.size()), 0);
}

TEST_F(TextVisibleCoverageTests, HiddenOversizedGlyphCannotRejectOrUpgradePriorVisibleCoverage){
    {
        auto layoutResult = m_text.layout({ "A" });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    TextLayout oversized(m_arena);
    {
        auto layoutResult = m_text.layout({ .text = "W", .fontSize = 1024.0f });
        ASSERT_TRUE(layoutResult);
        oversized = Move(*layoutResult);
    }
    beginPaint(1u);
    ASSERT_TRUE(m_text.paint(m_paint, m_layout, { 20.0f, 20.0f }));
    const DrawSnapshot before = m_paint.freeze();
    beginPaint(2u);
    ASSERT_TRUE(m_text.paint(m_paint, m_layout, { 20.0f, 20.0f }));
    ASSERT_TRUE(m_text.paint(m_paint, oversized, { 10000.0f, 10000.0f }));
    const DrawSnapshot hidden = m_paint.freeze();
    ExpectSamePaint(hidden, before);
    {
        auto layoutResult = m_text.layout({ "W" });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    beginPaint(3u);
    ASSERT_TRUE(m_text.paint(m_paint, m_layout, { 20.0f, 20.0f }));
    const DrawSnapshot ordinary = m_paint.freeze();
    ASSERT_EQ(ordinary.glyphPages().size(), 1u);
    EXPECT_GT(ordinary.glyphPages()[0u]->binding().generation, before.glyphPages()[0u]->binding().generation);
    EXPECT_EQ(ordinary.vertices().size(), 4u);
}

TEST_F(TextVisibleCoverageTests, EmptyIntersectedClipAndZeroAlphaNeverPrepareOversizedCoverage){
    {
        auto layoutResult = m_text.layout({ .text = "W", .fontSize = 1024.0f });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
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
    {
        auto layoutResult = m_text.layout({ "W" });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    beginPaint(2u);
    ASSERT_TRUE(m_text.paint(m_paint, m_layout, { 20.0f, 20.0f }));
    EXPECT_FALSE(m_paint.freeze().glyphPages().empty());
}

TEST_F(TextVisibleCoverageTests, FractionalUnequalDpiKeepsAHintedRasterSliverOutsideTheShapedOutline){
    const DisplayMetrics display{ 200.0f, 100.0f, 1.25f, 1.5f };
    const Point origin{ 40.375f, 20.125f };
    {
        auto layoutResult = m_text.layout({ .text = "W", .fontSize = 17.25f });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    ASSERT_EQ(m_layout.glyphs().size(), 1u);
    const auto raster = rasterRectangle(m_layout.glyphs()[0u], m_layout.fontSize(), display, origin);
    ASSERT_TRUE(raster);
    const Rect clip = RasterFringe(*raster, OutlineRectangle(m_layout.glyphs()[0u], origin));
    ASSERT_GT(clip.width, 0.0f);
    ASSERT_GT(clip.height, 0.0f);
    beginPaint(1u, display);
    ASSERT_TRUE(paintClipped(m_layout, clip, origin));
    const DrawSnapshot snapshot = m_paint.freeze();
    ASSERT_EQ(snapshot.glyphPages().size(), 1u);
    ASSERT_EQ(snapshot.vertices().size(), 4u);
    EXPECT_FLOAT_EQ(snapshot.vertices()[0u].position.x, clip.x);
    EXPECT_FLOAT_EQ(snapshot.vertices()[0u].position.y, clip.y);
    EXPECT_NEAR(snapshot.vertices()[2u].position.x, clip.x + clip.width, 0.0001f);
    EXPECT_NEAR(snapshot.vertices()[2u].position.y, clip.y + clip.height, 0.0001f);
}

TEST_F(TextVisibleCoverageTests, FractionalOriginPlacesNativeCoverageOnThePhysicalPixelGrid){
    const DisplayMetrics display{ 200.0f, 100.0f, 1.25f, 1.5f };
    const Point origin{ 40.375f, 20.125f };
    {
        auto layoutResult = m_text.layout({ .text = "g", .fontSize = 17.25f });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    ASSERT_EQ(m_layout.glyphs().size(), 1u);
    const auto raster = rasterRectangle(m_layout.glyphs()[0u], m_layout.fontSize(), display, origin);
    ASSERT_TRUE(raster);
    beginPaint(1u, display);
    ASSERT_TRUE(m_text.paint(m_paint, m_layout, origin));
    const DrawSnapshot snapshot = m_paint.freeze();
    ASSERT_EQ(snapshot.glyphPages().size(), 1u);
    ASSERT_EQ(snapshot.vertices().size(), 4u);
    EXPECT_NEAR(snapshot.vertices()[0u].position.x, raster->x, 0.0001f);
    EXPECT_NEAR(snapshot.vertices()[0u].position.y, raster->y, 0.0001f);
}

TEST_F(TextVisibleCoverageTests, NegativeBearingRemainsVisibleToTheLeftOfTheGlyphOrigin){
    const Point origin{ 50.0f, 20.0f };
    const DisplayMetrics display{ 200.0f, 100.0f, 1.25f, 1.25f };
    {
        auto layoutResult = m_text.layout({ .text = "j", .fontSize = 32.0f });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    ASSERT_EQ(m_layout.glyphs().size(), 1u);
    const auto raster = rasterRectangle(m_layout.glyphs()[0u], m_layout.fontSize(), display, origin);
    ASSERT_TRUE(raster);
    const f32 glyphOrigin = origin.x + m_layout.glyphs()[0u].position.x;
    ASSERT_LT(raster->x, glyphOrigin);
    const Rect clip{ raster->x, raster->y, (glyphOrigin - raster->x) * 0.5f, raster->height };
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
    {
        auto layoutResult = m_text.layout({ .text = "x\xcc\x81", .fontSize = 32.0f });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    ASSERT_EQ(m_layout.glyphs().size(), 2u);
    ASSERT_EQ(m_layout.clusters().size(), 1u);
    const auto base = rasterRectangle(m_layout.glyphs()[0u], m_layout.fontSize(), display, origin);
    ASSERT_TRUE(base);
    const auto mark = rasterRectangle(m_layout.glyphs()[1u], m_layout.fontSize(), display, origin);
    ASSERT_TRUE(mark);
    ASSERT_LT(mark->y, base->y);
    const Rect clip{ mark->x, mark->y, mark->width, Min(mark->height, base->y - mark->y) * 0.5f };
    beginPaint(1u, display);
    ASSERT_TRUE(paintClipped(m_layout, clip, origin));
    const DrawSnapshot snapshot = m_paint.freeze();
    ASSERT_EQ(snapshot.vertices().size(), 4u);
    ASSERT_EQ(snapshot.indices().size(), 6u);
    EXPECT_LT(snapshot.vertices()[2u].position.y, base->y);
    EXPECT_EQ(snapshot.glyphPages().size(), 1u);
}

TEST_F(TextVisibleCoverageTests, SixtyThreePriorImagesLeaveRoomForOnlyTheVisibleCoverageFace){
    {
        auto layoutResult = m_text.layout({ "A\n\xed\x95\x9c" });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
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


TEST_F(TextVisibleCoverageTests, NaturalMeasurementUsesFontLineMetricsAndPreservesLogicalMeasurement){
    const auto fallback = m_text.layout({ .text = "Edit me: \xed\x95\x9c\xea\xb8\x80", .fontSize = 14.0f });
    ASSERT_TRUE(fallback);
    EXPECT_FLOAT_EQ(fallback->measure().y, 20.34375f);
    const Point logical = fallback->measure();
    for(const f32 scale : { 1.0f, 1.25f, 1.5f, 2.0f }){
        const auto bounds = m_text.measure(*fallback, { 800.0f, 600.0f, scale, scale });
        ASSERT_TRUE(bounds);
        EXPECT_GE(bounds->height, logical.y);
        EXPECT_FLOAT_EQ(fallback->measure().x, logical.x);
        EXPECT_FLOAT_EQ(fallback->measure().y, logical.y);
    }
}

TEST_F(TextVisibleCoverageTests, NativePresetMeasurementMatchesRenderedBitmapWithoutAtlasAdmission){
    const auto layout = m_text.layout({ .text = "Previous gyjpq A\xcc\x81\xcc\x81 g\xcc\xa3 \xed\x95\x9c\xea\xb8\x80" });
    ASSERT_TRUE(layout);
    for(const u32 pixelSize : { 12u, 14u, 16u, 18u, 21u, 24u, 32u, 48u }){
        for(const PlacedGlyph& glyph : layout->glyphs()){
            const auto preset = glyph.face->rasterBounds(glyph.glyphId, pixelSize);
            ASSERT_TRUE(preset);
            ASSERT_TRUE(glyph.face->rasterize(glyph.glyphId, pixelSize, m_bitmap));
            EXPECT_FLOAT_EQ(preset->x, static_cast<f32>(m_bitmap.bearingX));
            EXPECT_FLOAT_EQ(preset->y, -static_cast<f32>(m_bitmap.bearingY));
            EXPECT_FLOAT_EQ(preset->width, static_cast<f32>(m_bitmap.width));
            EXPECT_FLOAT_EQ(preset->height, static_cast<f32>(m_bitmap.height));
        }
    }
    const auto large = m_text.layout({ .text = "W", .fontSize = 1024.0f });
    ASSERT_TRUE(large);
    ASSERT_TRUE(m_text.measure(*large, { 800.0f, 600.0f, 1.0f, 1.0f }));
    beginPaint(1u);
    ASSERT_TRUE(m_text.paint(m_paint, *layout, { 20.0f, 20.0f }));
    const DrawSnapshot snapshot = m_paint.freeze();
    ASSERT_FALSE(snapshot.glyphPages().empty());
    EXPECT_EQ(snapshot.glyphPages()[0u]->binding().generation, 1u);
}

TEST_F(TextVisibleCoverageTests, NaturalMeasurementContainsNativeMarksFallbackAndEveryTestedPixelPhase){
    const StringView samples[]{ "Previous", "Next", "gyjpq", "\xc3\x81\xc3\x89\xc3\x8d\xc3\x93\xc3\x9a \xc3\x87", "A\xcc\x81\xcc\x81", "g\xcc\xa3", "\xed\x95\x9c\xea\xb8\x80", "Edit me: \xed\x95\x9c\xea\xb8\x80" };
    for(const f32 size : { 12.0f, 14.0f, 16.0f }){
        for(const StringView sample : samples){
            const auto layout = m_text.layout({ .text = sample, .fontSize = size });
            ASSERT_TRUE(layout);
            for(const Point scale : { Point{ 1.0f, 1.0f }, Point{ 1.25f, 1.25f }, Point{ 1.5f, 1.5f }, Point{ 2.0f, 2.0f }, Point{ 1.25f, 1.5f } }){
                const DisplayMetrics display{ 800.0f, 600.0f, scale.x, scale.y };
                const auto measured = m_text.measure(*layout, display);
                ASSERT_TRUE(measured);
                for(const f32 phase : { 0.0f, 0.25f, 0.53125f, 0.9375f }){
                    const Rect content{ 32.0f + phase, 24.0f + phase, measured->width, measured->height };
                    const Point origin{ content.x - measured->x, content.y - measured->y };
                    for(const PlacedGlyph& glyph : layout->glyphs()){
                        const auto preset = glyph.face->rasterBounds(glyph.glyphId, static_cast<u32>(Ceil(size * Max(scale.x, scale.y))));
                        ASSERT_TRUE(preset);
                        if(preset->width == 0.0f || preset->height == 0.0f)
                            continue;
                        const auto raster = rasterRectangle(glyph, size, display, origin);
                        ASSERT_TRUE(raster);
                        EXPECT_GE(raster->x + 0.0001f, content.x);
                        EXPECT_GE(raster->y + 0.0001f, content.y);
                        EXPECT_LE(raster->x + raster->width, content.x + content.width + 0.0001f);
                        EXPECT_LE(raster->y + raster->height, content.y + content.height + 0.0001f);
                    }
                }
            }
        }
    }
}



[[nodiscard]] static f32 SampleSdfDistance(const SdfAtlasPage& page, const u32 channel, const f32 texelX, const f32 texelY)noexcept{
    const SdfAtlasPageBinding& binding = page.binding();
    const auto& pixels = page.pixels();
    const f32 x = texelX - 0.5f;
    const f32 y = texelY - 0.5f;
    const i32 firstX = static_cast<i32>(Floor(x));
    const i32 firstY = static_cast<i32>(Floor(y));
    const f32 fractionX = x - static_cast<f32>(firstX);
    const f32 fractionY = y - static_cast<f32>(firstY);
    const auto sample = [&](const i32 column, const i32 row)noexcept -> f32{
        const u32 clampedX = static_cast<u32>(Clamp(column, 0, static_cast<i32>(binding.width) - 1));
        const u32 clampedY = static_cast<u32>(Clamp(row, 0, static_cast<i32>(binding.height) - 1));
        return static_cast<f32>(pixels[(static_cast<usize>(clampedY) * binding.width + clampedX) * binding.channelCount + channel]) / 255.0f;
    };
    const f32 top = sample(firstX, firstY) * (1.0f - fractionX) + sample(firstX + 1, firstY) * fractionX;
    const f32 bottom = sample(firstX, firstY + 1) * (1.0f - fractionX) + sample(firstX + 1, firstY + 1) * fractionX;
    const f32 encoded = top * (1.0f - fractionY) + bottom * fractionY;
    return (encoded * 255.0f - 128.0f) * static_cast<f32>(binding.spreadPixels) / 128.0f;
}

TEST_F(TextVisibleCoverageTests, NaturalSdfMeasurementExcludesTransparentSpreadAndContainsShaderCoverage){
    const auto initial = m_text.layout({ .text = "A", .fontSize = 32.0f });
    ASSERT_TRUE(initial);
    ASSERT_EQ(initial->glyphs().size(), 1u);
    FontAtlas baked(m_arena, Name("tests/ui/fonts/measured_sdf"));
    FontAtlasPayload payload(m_arena);
    payload.font = Core::Assets::AssetRef<Font>("tests/ui/fonts/latin");
    const Core::Assets::AssetBytes& bytes = m_latin.fontBytes();
    payload.fontSha256 = ComputeSha256({ bytes.data(), bytes.size() });
    UiFixtureSupport::PopulateBakedFontTables(bytes, payload, m_arena);
    payload.bakePpem = 32u;
    payload.ascenderUnits = 1069.0f;
    payload.descenderUnits = -293.0f;
    payload.glyphs.resize(payload.sourceGlyphCount);
    for(u32 index = 0u; index < payload.sourceGlyphCount; ++index)
        payload.glyphs[index].glyphId = index;
    FontAtlasGlyph& glyph = payload.glyphs[initial->glyphs()[0u].glyphId];
    glyph.drawable = 1u;
    glyph.channel = 3u;
    glyph.x = 1u;
    glyph.y = 1u;
    glyph.width = 48u;
    glyph.height = 48u;
    glyph.planeLeft = -1000.0f;
    glyph.planeTop = -2000.0f;
    glyph.planeRight = glyph.planeLeft + static_cast<f32>(glyph.width * payload.unitsPerEm) / payload.bakePpem;
    glyph.planeBottom = glyph.planeTop + static_cast<f32>(glyph.height * payload.unitsPerEm) / payload.bakePpem;
    payload.groups.emplace_back(m_arena);
    FontAtlasGroup& group = payload.groups.back();
    group.width = 64u;
    group.height = 64u;
    // Other channels are fully opaque: measurement must follow the selected glyph channel.
    group.pixels.resize(64u * 64u * 4u, 255u);
    for(u32 y = 0u; y < group.height; ++y){
        for(u32 x = 0u; x < group.width; ++x){
            const i32 localX = static_cast<i32>(x) - static_cast<i32>(glyph.x);
            const i32 localY = static_cast<i32>(y) - static_cast<i32>(glyph.y);
            const i32 distance = Min(Min(localX - 15, 25 - localX), Min(localY - 13, 23 - localY));
            group.pixels[(static_cast<usize>(y) * group.width + x) * 4u + glyph.channel] = static_cast<u8>(Clamp(128 + 16 * distance, 0, 255));
        }
    }
    group.sha256 = ComputeSha256({ group.pixels.data(), group.pixels.size() });
    baked.setPayload(Move(payload));
    Core::Alloc::ScratchArena scratch(Name("tests/ui/text/measure_sdf"));
    ASSERT_TRUE(baked.validatePayload(scratch));
    const FontSource source{ Core::Assets::AssetRef<Font>("tests/ui/fonts/latin"), m_latin, 1u, &baked };
    ASSERT_TRUE(m_text.setFonts(&source, 1u));
    usize visibleSamples = 0u;
    for(const f32 fontSize : { 16.0f, 24.0f, 32.0f }){
        const auto layout = m_text.layout({ .text = "A", .fontSize = fontSize });
        ASSERT_TRUE(layout);
        const PlacedGlyph& placed = layout->glyphs()[0u];
        ASSERT_TRUE(placed.face->bakedAtlas());
        const BakedFontAtlas& atlas = *placed.face->bakedAtlas();
        const FontAtlasGlyph& record = *atlas.glyph(placed.glyphId);
        const SdfAtlasPage& page = *atlas.page(record.group);
        for(const Point scale : { Point{ 1.0f, 1.0f }, Point{ 1.5f, 1.5f }, Point{ 2.0f, 2.0f }, Point{ 1.25f, 2.0f }, Point{ 2.0f, 0.75f } }){
            const f32 physicalSize = fontSize * Max(scale.x, scale.y);
            if(physicalSize < 24.0f || physicalSize > 48.0f)
                continue;
            const DisplayMetrics display{ 800.0f, 600.0f, scale.x, scale.y };
            const auto measured = m_text.measure(*layout, display);
            ASSERT_TRUE(measured);
            const auto relativePlane = TextGlyphVisibility::AtlasRectangle(placed, atlas, fontSize, {});
            ASSERT_TRUE(relativePlane);
            EXPECT_GT(measured->x, relativePlane->x);
            EXPECT_GT(measured->y, relativePlane->y);
            for(const f32 phase : { 0.0f, 0.25f, 0.53125f, 0.9375f }){
                const Rect content{ 100.0f + phase, 100.0f + phase * 0.5f, measured->width, measured->height };
                const Point origin{ content.x - measured->x, content.y - measured->y };
                const auto plane = TextGlyphVisibility::AtlasRectangle(placed, atlas, fontSize, origin);
                ASSERT_TRUE(plane);
                const i32 firstX = static_cast<i32>(Floor(plane->x * scale.x));
                const i32 firstY = static_cast<i32>(Floor(plane->y * scale.y));
                const i32 lastX = static_cast<i32>(Ceil((plane->x + plane->width) * scale.x));
                const i32 lastY = static_cast<i32>(Ceil((plane->y + plane->height) * scale.y));
                const auto distance = [&](const i32 x, const i32 y)noexcept -> f32{
                    const f32 localX = (static_cast<f32>(x) + 0.5f) / scale.x - plane->x;
                    const f32 localY = (static_cast<f32>(y) + 0.5f) / scale.y - plane->y;
                    return SampleSdfDistance(page, record.channel, static_cast<f32>(record.x) + localX * record.width / plane->width, static_cast<f32>(record.y) + localY * record.height / plane->height);
                };
                for(i32 y = firstY; y < lastY; ++y){
                    for(i32 x = firstX; x < lastX; ++x){
                        const f32 pointX = (static_cast<f32>(x) + 0.5f) / scale.x;
                        const f32 pointY = (static_cast<f32>(y) + 0.5f) / scale.y;
                        if(pointX < plane->x || pointY < plane->y || pointX >= plane->x + plane->width || pointY >= plane->y + plane->height)
                            continue;
                        const f32 current = distance(x, y);
                        // Quad lanes share a finite difference with their horizontal and vertical partners, including helpers outside the plane.
                        const f32 derivative = Abs(distance(x ^ 1, y) - current) + Abs(distance(x, y ^ 1) - current);
                        if(0.5f + current / Max(derivative, 0.001f) <= 0.0f)
                            continue;
                        ++visibleSamples;
                        EXPECT_GE(pointX + 0.0001f, content.x);
                        EXPECT_GE(pointY + 0.0001f, content.y);
                        EXPECT_LE(pointX, content.x + content.width + 0.0001f);
                        EXPECT_LE(pointY, content.y + content.height + 0.0001f);
                    }
                }
            }
        }
    }
    EXPECT_GT(visibleSamples, 100u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

