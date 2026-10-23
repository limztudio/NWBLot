// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_visible_fixture.h"
#include "baked_font_fixture.h"

#include <impl/ecs_ui/toolkit/text/atlas.h>
#include <impl/ecs_ui/toolkit/text/glyph_visibility.h>

#include <global/algorithm.h>
#include <global/simdmath.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_visible_coverage_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::UiTextVisibleTests;

constexpr AStringView s_SampleGlyphText = "A";
constexpr AStringView s_WideGlyphText = "W";


class TextVisibleCoverageTests : public TextVisibleFixture{
protected:
    [[nodiscard]] static Rect OutlineRectangle(const PlacedGlyph& glyph, const Point origin)noexcept{
        const SIMDVectorSingle position = VectorAdd(VectorAdd(VectorSet(origin.x, origin.y, 0.0f, 0.0f), VectorSet(glyph.position.x, glyph.position.y, 0.0f, 0.0f)), VectorSet(glyph.ink.x, glyph.ink.y, 0.0f, 0.0f));
        return { VectorGetX(position), VectorGetY(position), glyph.ink.width, glyph.ink.height };
    }

    // Lanes are x and y. A fringe is half of the gap, but at most 0.05.
    [[nodiscard]] static Rect RasterFringe(const Rect& raster, const Rect& outline)noexcept{
        const SIMDVectorSingle rasterStart = VectorSet(raster.x, raster.y, 0.0f, 0.0f);
        const SIMDVectorSingle outlineStart = VectorSet(outline.x, outline.y, 0.0f, 0.0f);
        const SIMDVectorSingle startTest = VectorAdd(rasterStart, VectorReplicate(0.001f));
        const SIMDVectorSingle startFringe = VectorMin(VectorReplicate(0.05f), VectorMultiply(VectorSubtract(outlineStart, rasterStart), VectorReplicate(0.5f)));
        if(VectorGetX(startTest) < outline.x)
            return { raster.x, raster.y, VectorGetX(startFringe), raster.height };
        if(VectorGetY(startTest) < outline.y)
            return { raster.x, raster.y, raster.width, VectorGetY(startFringe) };
        const SIMDVectorSingle outlineSize = VectorSet(outline.width, outline.height, 0.0f, 0.0f);
        const SIMDVectorSingle rasterEnd = VectorAdd(rasterStart, VectorSet(raster.width, raster.height, 0.0f, 0.0f));
        const SIMDVectorSingle endTest = VectorAdd(VectorAdd(outlineStart, outlineSize), VectorReplicate(0.001f));
        const SIMDVectorSingle endFringe = VectorMin(VectorReplicate(0.05f), VectorMultiply(VectorSubtract(VectorSubtract(rasterEnd, outlineStart), outlineSize), VectorReplicate(0.5f)));
        const SIMDVectorSingle endFringeStart = VectorSubtract(rasterEnd, endFringe);
        if(VectorGetX(rasterEnd) > VectorGetX(endTest))
            return { VectorGetX(endFringeStart), raster.y, VectorGetX(endFringe), raster.height };
        if(VectorGetY(rasterEnd) > VectorGetY(endTest))
            return { raster.x, VectorGetY(endFringeStart), raster.width, VectorGetY(endFringe) };
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
        const SIMDVectorSingle physicalSize = VectorCeiling(VectorMultiply(VectorReplicate(fontSize), VectorMax(VectorReplicate(display.pixelScaleX), VectorReplicate(display.pixelScaleY))));
        if(!glyph.face->rasterize(glyph.glyphId, static_cast<u32>(VectorGetX(physicalSize)), m_bitmap))
            return MakeUnexpected(Failure{});
        const SIMDVectorSingle scale = VectorDivide(physicalSize, VectorReplicate(fontSize));
        // Lanes are x and y. X adds the bearing and y subtracts it.
        const SIMDVectorDouble base = VectorDoubleAdd(VectorDoubleSet(origin.x, origin.y, 0.0, 0.0), VectorDoubleSet(glyph.position.x, glyph.position.y, 0.0, 0.0));
        const SIMDVectorDouble bearing = VectorDoubleDivide(VectorDoubleSet(static_cast<f64>(m_bitmap.bearingX), static_cast<f64>(m_bitmap.bearingY), 0.0, 0.0), ConvertVectorSingleToDouble(scale));
        const SIMDVectorDouble unsnapped = VectorDoublePermute<0, 5, 2, 3>(VectorDoubleAdd(base, bearing), VectorDoubleSubtract(base, bearing));
        const SIMDVectorDouble pixelScale = VectorDoubleSet(display.pixelScaleX, display.pixelScaleY, 1.0, 1.0);
        const SIMDVectorDouble snapped = VectorDoubleDivide(VectorDoubleFloor(VectorDoubleMultiplyAddExpression(unsnapped, pixelScale, VectorDoubleReplicate(0.5))), pixelScale);
        const SIMDVectorSingle size = VectorDivide(VectorSet(static_cast<f32>(m_bitmap.width), static_cast<f32>(m_bitmap.height), 0.0f, 0.0f), scale);
        const Rect rectangle{ static_cast<f32>(VectorDoubleGetX(snapped)), static_cast<f32>(VectorDoubleGetY(snapped)), VectorGetX(size), VectorGetY(size) };
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
    const Rect clip{ 0.0f, 0.0f, 100.0f, VectorGetX(VectorAdd(VectorReplicate(20.0f), VectorReplicate(m_layout.lines()[1u].top))) };
    for(const PlacedGlyph& glyph : m_layout.glyphs()){
        if(glyph.byteBegin < 2u)
            continue;
        EXPECT_EQ(glyph.face->identity().name(), Name(s_KoreanFontPath));
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
    EXPECT_EQ(prefix.glyphPages()[0u]->binding().font.name(), Name(s_LatinFontPath));
    EXPECT_EQ(prefix.vertices().size(), 4u);
    EXPECT_EQ(prefix.indices().size(), 6u);
    const PlacedGlyph& reveal = m_layout.glyphs()[hiddenCount / 2u];
    const SIMDVectorSingle originValue = VectorSubtract(VectorSubtract(VectorReplicate(20.0f), VectorSet(reveal.position.x, reveal.position.y, 0.0f, 0.0f)), VectorSet(reveal.ink.x, reveal.ink.y, 0.0f, 0.0f));
    const Point origin{ VectorGetX(originValue), VectorGetY(originValue) };
    beginPaint(2u);
    ASSERT_TRUE(paintClipped(m_layout, { 20.0f, 20.0f, 80.0f, 40.0f }, origin));
    const DrawSnapshot revealed = m_paint.freeze();
    ASSERT_EQ(revealed.glyphPages().size(), 1u);
    EXPECT_EQ(revealed.glyphPages()[0u]->binding().font.name(), Name(s_KoreanFontPath));
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
    const Rect clip{ 0.0f, 0.0f, 100.0f, VectorGetX(VectorAdd(VectorReplicate(20.0f), VectorReplicate(m_layout.lines()[1u].top))) };
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
        auto layoutResult = m_text.layout({ s_SampleGlyphText });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    TextLayout oversized(m_arena);
    {
        auto layoutResult = m_text.layout({ .text = s_WideGlyphText, .fontSize = 1024.0f });
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
        auto layoutResult = m_text.layout({ s_WideGlyphText });
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
        auto layoutResult = m_text.layout({ .text = s_WideGlyphText, .fontSize = 1024.0f });
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
        auto layoutResult = m_text.layout({ s_WideGlyphText });
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
        auto layoutResult = m_text.layout({ .text = s_WideGlyphText, .fontSize = 17.25f });
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
    const SIMDVectorSingle clipEnd = VectorAdd(VectorSet(clip.x, clip.y, 0.0f, 0.0f), VectorSet(clip.width, clip.height, 0.0f, 0.0f));
    EXPECT_NEAR(snapshot.vertices()[2u].position.x, VectorGetX(clipEnd), 0.0001f);
    EXPECT_NEAR(snapshot.vertices()[2u].position.y, VectorGetY(clipEnd), 0.0001f);
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
    const SIMDVectorSingle glyphOrigin = VectorAdd(VectorReplicate(origin.x), VectorReplicate(m_layout.glyphs()[0u].position.x));
    ASSERT_LT(raster->x, VectorGetX(glyphOrigin));
    const Rect clip{ raster->x, raster->y, VectorGetX(VectorMultiply(VectorSubtract(glyphOrigin, VectorReplicate(raster->x)), VectorReplicate(0.5f))), raster->height };
    beginPaint(1u, display);
    ASSERT_TRUE(paintClipped(m_layout, clip, origin));
    const DrawSnapshot snapshot = m_paint.freeze();
    ASSERT_EQ(snapshot.vertices().size(), 4u);
    EXPECT_LT(snapshot.vertices()[2u].position.x, VectorGetX(glyphOrigin));
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
    const Rect clip{ mark->x, mark->y, mark->width, VectorGetX(VectorMultiply(VectorMin(VectorReplicate(mark->height), VectorSubtract(VectorReplicate(base->y), VectorReplicate(mark->y))), VectorReplicate(0.5f))) };
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
        auto pageResult = makePage(Limit<u64>::s_Max - index);
        ASSERT_TRUE(pageResult);
        prior[index] = Move(*pageResult);
        ASSERT_TRUE(prior[index]);
    }
    beginPaint(1u);
    ASSERT_TRUE(m_paint.prepareGlyphPages(prior.data(), prior.size()));
    const Rect clip{ 0.0f, 0.0f, 100.0f, VectorGetX(VectorAdd(VectorReplicate(20.0f), VectorReplicate(m_layout.lines()[1u].top))) };
    ASSERT_TRUE(paintClipped(m_layout, clip));
    const DrawSnapshot snapshot = m_paint.freeze();
    ASSERT_EQ(snapshot.glyphPages().size(), s_PaintMaxImages);
    EXPECT_EQ(snapshot.vertices().size(), 4u);
    EXPECT_TRUE(snapshot.sdfPages().empty());
    for(usize index = 0u; index < prior.size(); ++index)
        EXPECT_EQ(snapshot.glyphPages()[index].get(), prior[index].get());
    EXPECT_EQ(snapshot.glyphPages().back()->binding().font.name(), Name(s_LatinFontPath));
    beginPaint(2u);
    ASSERT_TRUE(m_text.paint(m_paint, m_layout, { 20.0f, 20.0f }));
    const DrawSnapshot revealed = m_paint.freeze();
    EXPECT_EQ(revealed.glyphPages().size(), 2u);
    EXPECT_EQ(revealed.vertices().size(), 8u);
}


TEST_F(TextVisibleCoverageTests, OversizedMeasurementDoesNotAdmitAtlasCoverage){
    const auto layout = m_text.layout({ .text = "Previous gyjpq A\xcc\x81\xcc\x81 g\xcc\xa3 \xed\x95\x9c\xea\xb8\x80" });
    ASSERT_TRUE(layout);
    const auto large = m_text.layout({ .text = s_WideGlyphText, .fontSize = 1024.0f });
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
                const u32 presetSize = static_cast<u32>(VectorGetX(VectorCeiling(VectorMultiply(VectorReplicate(size), VectorMax(VectorReplicate(scale.x), VectorReplicate(scale.y))))));
                for(const f32 phase : { 0.0f, 0.25f, 0.53125f, 0.9375f }){
                    const SIMDVectorSingle contentStart = VectorAdd(VectorSet(32.0f, 24.0f, 0.0f, 0.0f), VectorReplicate(phase));
                    const Rect content{ VectorGetX(contentStart), VectorGetY(contentStart), measured->width, measured->height };
                    const SIMDVectorSingle originValue = VectorSubtract(contentStart, VectorSet(measured->x, measured->y, 0.0f, 0.0f));
                    const Point origin{ VectorGetX(originValue), VectorGetY(originValue) };
                    const SIMDVectorSingle contentLimit = VectorAdd(VectorAdd(contentStart, VectorSet(content.width, content.height, 0.0f, 0.0f)), VectorReplicate(0.0001f));
                    for(const PlacedGlyph& glyph : layout->glyphs()){
                        const auto preset = glyph.face->rasterBounds(glyph.glyphId, presetSize);
                        ASSERT_TRUE(preset);
                        if(preset->width == 0.0f || preset->height == 0.0f)
                            continue;
                        const auto raster = rasterRectangle(glyph, size, display, origin);
                        ASSERT_TRUE(raster);
                        const SIMDVectorSingle rasterStart = VectorSet(raster->x, raster->y, 0.0f, 0.0f);
                        const SIMDVectorSingle rasterTest = VectorAdd(rasterStart, VectorReplicate(0.0001f));
                        const SIMDVectorSingle rasterEnd = VectorAdd(rasterStart, VectorSet(raster->width, raster->height, 0.0f, 0.0f));
                        EXPECT_GE(VectorGetX(rasterTest), content.x);
                        EXPECT_GE(VectorGetY(rasterTest), content.y);
                        EXPECT_LE(VectorGetX(rasterEnd), VectorGetX(contentLimit));
                        EXPECT_LE(VectorGetY(rasterEnd), VectorGetY(contentLimit));
                    }
                }
            }
        }
    }
}


// Texel lanes are x and y. Every lane of the result holds the distance.
[[nodiscard]] static SIMDVectorSingle SampleSdfDistance(const SdfAtlasPage& page, const u32 channel, const SIMDVectorSingle texel)noexcept{
    const SdfAtlasPageBinding& binding = page.binding();
    const auto& pixels = page.pixels();
    const SIMDVectorSingle position = VectorSubtract(texel, VectorReplicate(0.5f));
    const SIMDVectorSingle firstTexel = VectorFloor(position);
    const i32 firstX = static_cast<i32>(VectorGetX(firstTexel));
    const i32 firstY = static_cast<i32>(VectorGetY(firstTexel));
    const SIMDVectorSingle fraction = VectorSubtract(position, firstTexel);
    const SIMDVectorSingle remainder = VectorSubtract(VectorSplatOne(), fraction);
    const auto sample = [&](const i32 column, const i32 row)noexcept -> f32{
        const u32 clampedX = static_cast<u32>(Clamp(column, 0, static_cast<i32>(binding.width) - 1));
        const u32 clampedY = static_cast<u32>(Clamp(row, 0, static_cast<i32>(binding.height) - 1));
        return static_cast<f32>(pixels[(static_cast<usize>(clampedY) * binding.width + clampedX) * binding.channelCount + channel]);
    };
    // Lanes are top left, bottom left, top right, bottom right.
    const SIMDVectorSingle samples = VectorDivide(VectorSet(sample(firstX, firstY), sample(firstX, firstY + 1), sample(firstX + 1, firstY), sample(firstX + 1, firstY + 1)), VectorReplicate(255.0f));
    // Lanes are the top and bottom rows.
    const SIMDVectorSingle rows = VectorMultiplyAddExpression(samples, VectorSplatX(remainder), VectorMultiply(VectorSwizzle<2, 3, 2, 3>(samples), VectorSplatX(fraction)));
    const SIMDVectorSingle encoded = VectorMultiplyAddExpression(VectorSplatX(rows), VectorSplatY(remainder), VectorMultiply(VectorSplatY(rows), VectorSplatY(fraction)));
    return VectorDivide(VectorMultiply(VectorMultiplyAddExpression(encoded, VectorReplicate(255.0f), VectorNegate(VectorReplicate(128.0f))), VectorReplicate(static_cast<f32>(binding.spreadPixels))), VectorReplicate(128.0f));
}

TEST_F(TextVisibleCoverageTests, NaturalSdfMeasurementExcludesTransparentSpreadAndContainsShaderCoverage){
    const auto initial = m_text.layout({ .text = s_SampleGlyphText, .fontSize = 32.0f });
    ASSERT_TRUE(initial);
    ASSERT_EQ(initial->glyphs().size(), 1u);
    FontAtlas baked(m_arena, Name("tests/ui/fonts/measured_sdf"));
    FontAtlasPayload payload(m_arena);
    payload.font = Core::Assets::AssetRef<Font>(s_LatinFontPath);
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
    const SIMDVectorSingle planeExtent = VectorDivide(VectorSet(static_cast<f32>(glyph.width * payload.unitsPerEm), static_cast<f32>(glyph.height * payload.unitsPerEm), 0.0f, 0.0f), VectorReplicate(static_cast<f32>(payload.bakePpem)));
    const SIMDVectorSingle planeEnd = VectorAdd(VectorSet(glyph.planeLeft, glyph.planeTop, 0.0f, 0.0f), planeExtent);
    glyph.planeRight = VectorGetX(planeEnd);
    glyph.planeBottom = VectorGetY(planeEnd);
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
    const FontSource source{ Core::Assets::AssetRef<Font>(s_LatinFontPath), m_latin, 1u, &baked };
    ASSERT_TRUE(m_text.setFonts(scratch, &source, 1u));
    usize visibleSamples = 0u;
    for(const f32 fontSize : { 16.0f, 24.0f, 32.0f }){
        const auto layout = m_text.layout({ .text = s_SampleGlyphText, .fontSize = fontSize });
        ASSERT_TRUE(layout);
        const PlacedGlyph& placed = layout->glyphs()[0u];
        ASSERT_TRUE(placed.face->bakedAtlas());
        const BakedFontAtlas& atlas = *placed.face->bakedAtlas();
        const FontAtlasGlyph& record = *atlas.glyph(placed.glyphId);
        const SdfAtlasPage& page = *atlas.page(record.group);
        for(const Point scale : { Point{ 1.0f, 1.0f }, Point{ 1.5f, 1.5f }, Point{ 2.0f, 2.0f }, Point{ 1.25f, 2.0f }, Point{ 2.0f, 0.75f } }){
            const f32 physicalSize = VectorGetX(VectorMultiply(VectorReplicate(fontSize), VectorMax(VectorReplicate(scale.x), VectorReplicate(scale.y))));
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
                // Lanes are x and y. X moves by the full phase and y by half of it.
                const SIMDVectorSingle contentStart = VectorMultiplyAddExpression(VectorReplicate(phase), VectorSet(1.0f, 0.5f, 0.0f, 0.0f), VectorReplicate(100.0f));
                const Rect content{ VectorGetX(contentStart), VectorGetY(contentStart), measured->width, measured->height };
                const SIMDVectorSingle originValue = VectorSubtract(contentStart, VectorSet(measured->x, measured->y, 0.0f, 0.0f));
                const Point origin{ VectorGetX(originValue), VectorGetY(originValue) };
                const SIMDVectorSingle contentLimit = VectorAdd(VectorAdd(contentStart, VectorSet(content.width, content.height, 0.0f, 0.0f)), VectorReplicate(0.0001f));
                const auto plane = TextGlyphVisibility::AtlasRectangle(placed, atlas, fontSize, origin);
                ASSERT_TRUE(plane);
                const SIMDVectorSingle pixelScale = VectorSet(scale.x, scale.y, 1.0f, 1.0f);
                const SIMDVectorSingle planeStart = VectorSet(plane->x, plane->y, 0.0f, 0.0f);
                const SIMDVectorSingle planeEnd = VectorAdd(planeStart, VectorSet(plane->width, plane->height, 0.0f, 0.0f));
                const SIMDVectorSingle firstPixel = VectorFloor(VectorMultiply(planeStart, pixelScale));
                const SIMDVectorSingle lastPixel = VectorCeiling(VectorMultiply(planeEnd, pixelScale));
                const i32 firstX = static_cast<i32>(VectorGetX(firstPixel));
                const i32 firstY = static_cast<i32>(VectorGetY(firstPixel));
                const i32 lastX = static_cast<i32>(VectorGetX(lastPixel));
                const i32 lastY = static_cast<i32>(VectorGetY(lastPixel));
                const SIMDVectorSingle recordStart = VectorSet(static_cast<f32>(record.x), static_cast<f32>(record.y), 0.0f, 0.0f);
                const SIMDVectorSingle recordSize = VectorSet(static_cast<f32>(record.width), static_cast<f32>(record.height), 0.0f, 0.0f);
                const SIMDVectorSingle planeSize = VectorSet(plane->width, plane->height, 1.0f, 1.0f);
                // Pixel centers are in logical units. Lanes are x and y.
                const auto pixelCenter = [&](const i32 x, const i32 y)noexcept -> SIMDVectorSingle{ return VectorDivide(VectorAdd(VectorSet(static_cast<f32>(x), static_cast<f32>(y), 0.0f, 0.0f), VectorReplicate(0.5f)), pixelScale); };
                const auto distance = [&](const i32 x, const i32 y)noexcept -> SIMDVectorSingle{
                    const SIMDVectorSingle local = VectorSubtract(pixelCenter(x, y), planeStart);
                    return SampleSdfDistance(page, record.channel, VectorAdd(recordStart, VectorDivide(VectorMultiply(local, recordSize), planeSize)));
                };
                for(i32 y = firstY; y < lastY; ++y){
                    for(i32 x = firstX; x < lastX; ++x){
                        const SIMDVectorSingle point = pixelCenter(x, y);
                        const f32 pointX = VectorGetX(point);
                        const f32 pointY = VectorGetY(point);
                        if(pointX < plane->x || pointY < plane->y || pointX >= VectorGetX(planeEnd) || pointY >= VectorGetY(planeEnd))
                            continue;
                        const SIMDVectorSingle current = distance(x, y);
                        // Quad lanes share a finite difference with their horizontal and vertical partners, including helpers outside the plane.
                        const SIMDVectorSingle derivative = VectorAdd(VectorAbs(VectorSubtract(distance(x ^ 1, y), current)), VectorAbs(VectorSubtract(distance(x, y ^ 1), current)));
                        if(VectorGetX(VectorAdd(VectorReplicate(0.5f), VectorDivide(current, VectorMax(derivative, VectorReplicate(0.001f))))) <= 0.0f)
                            continue;
                        ++visibleSamples;
                        const SIMDVectorSingle pointTest = VectorAdd(point, VectorReplicate(0.0001f));
                        EXPECT_GE(VectorGetX(pointTest), content.x);
                        EXPECT_GE(VectorGetY(pointTest), content.y);
                        EXPECT_LE(pointX, VectorGetX(contentLimit));
                        EXPECT_LE(pointY, VectorGetY(contentLimit));
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

