// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_visible_fixture.h"

#include <global/simdmath.h>

#include "baked_font_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_visible_baked_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using namespace NWB::Impl::Ui;
using namespace NWB::UiTextVisibleTests;

class TextVisibleBakedTests : public TextVisibleFixture{
protected:
    [[nodiscard]] static Rect OutlineRectangle(const PlacedGlyph& glyph, const Point origin)noexcept{
        const SIMDVectorSingle position = VectorAdd(VectorAdd(VectorSet(origin.x, origin.y, 0.0f, 0.0f), VectorSet(glyph.position.x, glyph.position.y, 0.0f, 0.0f)), VectorSet(glyph.ink.x, glyph.ink.y, 0.0f, 0.0f));
        return Rect{ VectorGetX(position), VectorGetY(position), glyph.ink.width, glyph.ink.height };
    }

    [[nodiscard]] static Rect OutlineCenterClip(const PlacedGlyph& glyph, const Point origin)noexcept{
        const Rect outline = OutlineRectangle(glyph, origin);
        const SIMDVectorSingle center = VectorMultiplyAddExpression(VectorSet(outline.width, outline.height, 0.0f, 0.0f), VectorReplicate(0.5f), VectorSet(outline.x, outline.y, 0.0f, 0.0f));
        return { VectorGetX(center), VectorGetY(center), 1.0f, 1.0f };
    }

    [[nodiscard]] static Rect BakedRectangle(const PlacedGlyph& glyph, const Point origin)noexcept{
        const BakedFontAtlas& atlas = *glyph.face->bakedAtlas();
        const FontAtlasGlyph& record = *atlas.glyph(glyph.glyphId);
        const SIMDVectorSingle scale = VectorDivide(VectorReplicate(32.0f), VectorReplicate(static_cast<f32>(atlas.unitsPerEm())));
        const SIMDVectorSingle position = VectorMultiplyAddExpression(VectorSet(record.planeLeft, record.planeTop, 0.0f, 0.0f), scale, VectorAdd(VectorSet(origin.x, origin.y, 0.0f, 0.0f), VectorSet(glyph.position.x, glyph.position.y, 0.0f, 0.0f)));
        const SIMDVectorSingle size = VectorMultiply(VectorSubtract(VectorSet(record.planeRight, record.planeBottom, 0.0f, 0.0f), VectorSet(record.planeLeft, record.planeTop, 0.0f, 0.0f)), scale);
        return Rect{ VectorGetX(position), VectorGetY(position), VectorGetX(size), VectorGetY(size) };
    }


public:
    TextVisibleBakedTests()
        : m_baked(m_arena, Name("tests/ui/fonts/visible_atlas"))
    {}


protected:
    [[nodiscard]] bool installBaked(const StringView text){
        if(!installCoverageFonts())
            return false;
        auto layout = m_text.layout({ .text = text, .fontSize = 32.0f });
        if(!layout)
            return false;
        m_layout = Move(*layout);
        FontAtlasPayload payload(m_arena);
        payload.font = Core::Assets::AssetRef<Font>(s_LatinFontPath);
        const Core::Assets::AssetBytes& bytes = m_latin.fontBytes();
        payload.fontSha256 = ComputeSha256({ bytes.data(), bytes.size() });
        UiFixtureSupport::PopulateBakedFontTables(bytes, payload, m_arena);
        payload.bakePpem = 32u;
        payload.ascenderUnits = 1000.0f;
        payload.descenderUnits = -250.0f;
        payload.glyphs.resize(payload.sourceGlyphCount);
        for(u32 id = 0u; id < payload.sourceGlyphCount; ++id)
            payload.glyphs[id].glyphId = id;
        const SIMDVectorSingle unitsPerPixel = VectorDivide(VectorReplicate(static_cast<f32>(payload.unitsPerEm)), VectorReplicate(32.0f));
        for(const PlacedGlyph& placed : m_layout.glyphs()){
            if(placed.face->identity() != payload.font || placed.glyphId >= payload.glyphs.size())
                return false;
            FontAtlasGlyph& glyph = payload.glyphs[placed.glyphId];
            if(glyph.drawable != 0u || placed.ink.width <= 0.0f || placed.ink.height <= 0.0f)
                continue;
            if(payload.groups.size() == s_FontAtlasMaxGroupCount)
                return false;
            // Lanes are x and y. The plane has 8 pixels of room on each side.
            const SIMDVectorSingle inkStart = VectorSet(placed.ink.x, placed.ink.y, 0.0f, 0.0f);
            const SIMDVectorSingle start = VectorSubtract(VectorFloor(inkStart), VectorReplicate(8.0f));
            const SIMDVectorSingle extent = VectorAdd(VectorSubtract(VectorCeiling(VectorAdd(inkStart, VectorSet(placed.ink.width, placed.ink.height, 0.0f, 0.0f))), start), VectorReplicate(8.0f));
            const u32 width = static_cast<u32>(VectorGetX(extent));
            const u32 height = static_cast<u32>(VectorGetY(extent));
            if(width > 62u || height > 62u)
                return false;
            glyph.group = static_cast<u32>(payload.groups.size());
            glyph.drawable = 1u;
            glyph.x = 1u;
            glyph.y = 1u;
            glyph.width = width;
            glyph.height = height;
            const SIMDVectorSingle planeStart = VectorMultiply(start, unitsPerPixel);
            const SIMDVectorSingle planeEnd = VectorMultiplyAddExpression(VectorSet(static_cast<f32>(width), static_cast<f32>(height), 0.0f, 0.0f), unitsPerPixel, planeStart);
            glyph.planeLeft = VectorGetX(planeStart);
            glyph.planeTop = VectorGetY(planeStart);
            glyph.planeRight = VectorGetX(planeEnd);
            glyph.planeBottom = VectorGetY(planeEnd);
            payload.groups.emplace_back(m_arena);
            FontAtlasGroup& group = payload.groups.back();
            group.width = 64u;
            group.height = 64u;
            group.pixels.resize(64u * 64u * 4u, 192u);
            group.sha256 = ComputeSha256({ group.pixels.data(), group.pixels.size() });
        }
        m_baked.setPayload(Move(payload));
        Core::Alloc::ScratchArena scratchArena(Name("tests/ui/text/visible_baked/validation_scratch"));
        if(!m_baked.validatePayload(scratchArena))
            return false;
        const FontSource sources[]{
            { Core::Assets::AssetRef<Font>(s_LatinFontPath), m_latin, 1u, &m_baked },
            { Core::Assets::AssetRef<Font>(s_KoreanFontPath), m_korean, 1u },
        };
        if(!m_text.setFonts(scratchArena, sources, 2u))
            return false;
        layout = m_text.layout({ .text = text, .fontSize = 32.0f });
        if(!layout)
            return false;
        m_layout = Move(*layout);
        return true;
    }


protected:
    FontAtlas m_baked;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(TextVisibleBakedTests, PaddedSdfPlaneIntersectingClipSurvivesWhenShapingOutlineIsOutside){
    ASSERT_TRUE(installBaked("A"));
    ASSERT_EQ(m_layout.glyphs().size(), 1u);
    const PlacedGlyph& glyph = m_layout.glyphs()[0u];
    ASSERT_TRUE(glyph.face->bakedAtlas());
    ASSERT_GT(glyph.ink.width, 0.0f);
    ASSERT_GT(glyph.ink.height, 0.0f);
    const Point origin{ 40.0f, 40.0f };
    const Rect outline = OutlineRectangle(glyph, origin);
    const Rect plane = BakedRectangle(glyph, origin);
    const SIMDVectorSingle clipX = VectorSubtract(VectorReplicate(outline.x), VectorReplicate(4.0f));
    const SIMDVectorSingle clipY = VectorMultiplyAddExpression(VectorReplicate(outline.height), VectorReplicate(0.5f), VectorReplicate(outline.y));
    const Rect clip{ VectorGetX(clipX), VectorGetX(clipY), 2.0f, 1.0f };
    // Lanes are clip right, clip bottom, plane right, plane bottom.
    const SIMDVectorSingle ends = VectorAdd(VectorSet(clip.x, clip.y, plane.x, plane.y), VectorSet(clip.width, clip.height, plane.width, plane.height));
    ASSERT_LT(VectorGetX(ends), outline.x);
    ASSERT_GE(clip.x, plane.x);
    ASSERT_GE(clip.y, plane.y);
    ASSERT_LE(VectorGetX(ends), VectorGetZ(ends));
    ASSERT_LE(VectorGetY(ends), VectorGetW(ends));
    beginPaint(1u);
    m_paint.pushClip(clip);
    ASSERT_TRUE(m_text.paint(m_paint, m_layout, origin));
    ASSERT_TRUE(m_paint.popClip());
    const DrawSnapshot snapshot = m_paint.freeze();
    EXPECT_TRUE(snapshot.glyphPages().empty());
    ASSERT_EQ(snapshot.sdfPages().size(), 1u);
    const FontAtlasGlyph& record = *glyph.face->bakedAtlas()->glyph(glyph.glyphId);
    EXPECT_EQ(snapshot.sdfPages()[0u].get(), glyph.face->bakedAtlas()->page(record.group).get());
    ASSERT_EQ(snapshot.commands().size(), 1u);
    ExpectRectangle(snapshot.commands()[0u].clip, clip);
    ASSERT_EQ(snapshot.vertices().size(), 4u);
    EXPECT_EQ(snapshot.indices().size(), 6u);
    EXPECT_FLOAT_EQ(snapshot.vertices()[0u].position.x, clip.x);
    EXPECT_FLOAT_EQ(snapshot.vertices()[0u].position.y, clip.y);
    EXPECT_FLOAT_EQ(snapshot.vertices()[2u].position.x, VectorGetX(ends));
    EXPECT_FLOAT_EQ(snapshot.vertices()[2u].position.y, VectorGetY(ends));
}

TEST_F(TextVisibleBakedTests, EmptyClipAndZeroAlphaAdmitNoBakedImagesOrGeometry){
    ASSERT_TRUE(installBaked("A\nB\nC"));
    ASSERT_EQ(m_baked.payload().groups.size(), 3u);
    for(u64 scenario = 0u; scenario < 2u; ++scenario){
        beginPaint(scenario + 1u);
        m_paint.pushClip({ 20.0f, 20.0f, scenario == 0u ? 0.0f : 100.0f, 100.0f });
        const Color color{ 0.7f, 0.2f, 0.5f, scenario == 0u ? 1.0f : 0.0f };
        ASSERT_TRUE(m_text.paint(m_paint, m_layout, { 20.0f, 20.0f }, color));
        ASSERT_TRUE(m_paint.popClip());
        const DrawSnapshot snapshot = m_paint.freeze();
        EXPECT_TRUE(snapshot.glyphPages().empty());
        EXPECT_TRUE(snapshot.sdfPages().empty());
        EXPECT_TRUE(snapshot.vertices().empty());
        EXPECT_TRUE(snapshot.indices().empty());
        EXPECT_TRUE(snapshot.commands().empty());
    }
}

TEST_F(TextVisibleBakedTests, SixtyThreePriorImagesLeaveOneSlotForOnlyTheVisibleBakedGroup){
    ASSERT_TRUE(installBaked("A\nB\nC"));
    ASSERT_EQ(m_layout.glyphs().size(), 3u);
    ASSERT_EQ(m_baked.payload().groups.size(), 3u);
    FixedVector<SharedGlyphPage, s_PaintMaxImages - 1u> prior;
    for(usize index = 0u; index < prior.max_size(); ++index){
        auto pageCreationResult = makePage(1000u + index);
        ASSERT_TRUE(pageCreationResult);
        SharedGlyphPage page = Move(*pageCreationResult);
        ASSERT_TRUE(page);
        prior.push_back(Move(page));
    }
    const Point origin{ 20.0f, 20.0f };
    const Rect clip = OutlineCenterClip(m_layout.glyphs()[0u], origin);
    const f32 clipBottom = VectorGetX(VectorAdd(VectorReplicate(clip.y), VectorReplicate(clip.height)));
    for(usize index = 1u; index < m_layout.glyphs().size(); ++index)
        ASSERT_GT(BakedRectangle(m_layout.glyphs()[index], origin).y, clipBottom);
    beginPaint(1u);
    ASSERT_TRUE(m_paint.prepareGlyphPages(prior.data(), prior.size()));
    m_paint.pushClip(clip);
    ASSERT_TRUE(m_text.paint(m_paint, m_layout, origin));
    ASSERT_TRUE(m_paint.popClip());
    const DrawSnapshot snapshot = m_paint.freeze();
    ASSERT_EQ(snapshot.glyphPages().size(), prior.size());
    for(usize index = 0u; index < prior.size(); ++index)
        EXPECT_EQ(snapshot.glyphPages()[index].get(), prior[index].get());
    ASSERT_EQ(snapshot.sdfPages().size(), 1u);
    const PlacedGlyph& visible = m_layout.glyphs()[0u];
    const FontAtlasGlyph& record = *visible.face->bakedAtlas()->glyph(visible.glyphId);
    EXPECT_EQ(snapshot.sdfPages()[0u].get(), visible.face->bakedAtlas()->page(record.group).get());
    EXPECT_EQ(snapshot.glyphPages().size() + snapshot.sdfPages().size(), s_PaintMaxImages);
    ASSERT_EQ(snapshot.commands().size(), 1u);
    EXPECT_EQ(snapshot.vertices().size(), 4u);
    EXPECT_EQ(snapshot.indices().size(), 6u);
}

TEST_F(TextVisibleBakedTests, FullyOutsideBakedGroupsRemainAbsentUntilALaterVisiblePaint){
    ASSERT_TRUE(installBaked("A\nB\nC"));
    ASSERT_EQ(m_layout.glyphs().size(), 3u);
    beginPaint(1u);
    m_paint.pushClip({ 700.0f, 500.0f, 2.0f, 2.0f });
    ASSERT_TRUE(m_text.paint(m_paint, m_layout, { 20.0f, 20.0f }));
    ASSERT_TRUE(m_paint.popClip());
    const DrawSnapshot hidden = m_paint.freeze();
    EXPECT_TRUE(hidden.glyphPages().empty());
    EXPECT_TRUE(hidden.sdfPages().empty());
    EXPECT_TRUE(hidden.vertices().empty());
    EXPECT_TRUE(hidden.indices().empty());
    EXPECT_TRUE(hidden.commands().empty());
    beginPaint(2u);
    ASSERT_TRUE(m_text.paint(m_paint, m_layout, { 20.0f, 20.0f }));
    const DrawSnapshot revealed = m_paint.freeze();
    EXPECT_TRUE(revealed.glyphPages().empty());
    ASSERT_EQ(revealed.sdfPages().size(), 3u);
    ASSERT_EQ(revealed.commands().size(), 3u);
    for(usize index = 0u; index < m_layout.glyphs().size(); ++index){
        const PlacedGlyph& glyph = m_layout.glyphs()[index];
        const FontAtlasGlyph& record = *glyph.face->bakedAtlas()->glyph(glyph.glyphId);
        EXPECT_EQ(revealed.sdfPages()[index].get(), glyph.face->bakedAtlas()->page(record.group).get());
    }
    EXPECT_EQ(revealed.vertices().size(), 12u);
    EXPECT_EQ(revealed.indices().size(), 18u);
    EXPECT_TRUE(hidden.sdfPages().empty());
}

TEST_F(TextVisibleBakedTests, VisibleMixedCapacityFailurePreservesPriorPageVersionBindingsAndGeometry){
    ASSERT_TRUE(installBaked("A"));
    {
        auto layoutResult = m_text.layout({ .text = "\xED\x95\x9C", .fontSize = 32.0f });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    ASSERT_EQ(m_layout.glyphs().size(), 1u);
    ASSERT_FALSE(m_layout.glyphs()[0u].face->bakedAtlas());
    const u32 warmGlyph = m_layout.glyphs()[0u].glyphId;
    beginPaint(1u);
    ASSERT_TRUE(m_text.paint(m_paint, m_layout, { 20.0f, 20.0f }));
    const DrawSnapshot warm = m_paint.freeze();
    ASSERT_EQ(warm.glyphPages().size(), 1u);
    ASSERT_TRUE(warm.sdfPages().empty());
    const SharedGlyphPage oldPage = warm.glyphPages()[0u];
    FixedVector<SharedGlyphPage, s_PaintMaxImages> prior;
    prior.push_back(oldPage);
    for(usize index = 1u; index < prior.max_size(); ++index){
        auto pageCreationResult = makePage(2000u + index);
        ASSERT_TRUE(pageCreationResult);
        SharedGlyphPage page = Move(*pageCreationResult);
        ASSERT_TRUE(page);
        prior.push_back(Move(page));
    }
    {
        auto layoutResult = m_text.layout({ .text = "\xEA\xB8\x80" "A", .fontSize = 32.0f });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    ASSERT_EQ(m_layout.glyphs().size(), 2u);
    ASSERT_NE(m_layout.glyphs()[0u].glyphId, warmGlyph);
    ASSERT_FALSE(m_layout.glyphs()[0u].face->bakedAtlas());
    ASSERT_TRUE(m_layout.glyphs()[1u].face->bakedAtlas());
    beginPaint(2u);
    ASSERT_TRUE(m_paint.prepareGlyphPages(prior.data(), prior.size()));
    m_paint.fillRect({ 2.0f, 3.0f, 5.0f, 7.0f }, { 0.6f, 0.3f, 0.2f, 0.5f });
    ASSERT_TRUE(m_paint.drawGlyph(oldPage, { 12.0f, 13.0f, 5.0f, 7.0f }, { 0.0f, 0.0f, 0.25f, 0.25f }));
    PaintBuilder reference(m_arena);
    reference.begin({ 800.0f, 600.0f, 1.0f, 1.0f }, 2u, 1u, Core::Assets::AssetRef<UiSkin>(s_SkinPath), m_skin);
    ASSERT_TRUE(reference.prepareGlyphPages(prior.data(), prior.size()));
    reference.fillRect({ 2.0f, 3.0f, 5.0f, 7.0f }, { 0.6f, 0.3f, 0.2f, 0.5f });
    ASSERT_TRUE(reference.drawGlyph(oldPage, { 12.0f, 13.0f, 5.0f, 7.0f }, { 0.0f, 0.0f, 0.25f, 0.25f }));
    const DrawSnapshot expected = reference.freeze();
    EXPECT_FALSE(m_text.paint(m_paint, m_layout, { 80.0f, 80.0f }));
    const DrawSnapshot rejected = m_paint.freeze();
    ExpectSamePaint(rejected, expected);
    EXPECT_TRUE(rejected.sdfPages().empty());
    ASSERT_EQ(rejected.glyphPages().size(), s_PaintMaxImages);
    EXPECT_EQ(rejected.glyphPages()[0u].get(), oldPage.get());
    EXPECT_EQ(rejected.glyphPages()[0u]->binding().generation, oldPage->binding().generation);
    {
        auto layoutResult = m_text.layout({ .text = "\xEA\xB8\x80", .fontSize = 32.0f });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    beginPaint(3u);
    ASSERT_TRUE(m_text.paint(m_paint, m_layout, { 20.0f, 20.0f }));
    const DrawSnapshot upgraded = m_paint.freeze();
    ASSERT_EQ(upgraded.glyphPages().size(), 1u);
    EXPECT_EQ(upgraded.glyphPages()[0u]->binding().atlasIdentity, oldPage->binding().atlasIdentity);
    EXPECT_GT(upgraded.glyphPages()[0u]->binding().generation, oldPage->binding().generation);
    EXPECT_NE(upgraded.glyphPages()[0u].get(), oldPage.get());
    EXPECT_TRUE(upgraded.sdfPages().empty());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

