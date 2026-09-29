// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_visible_fixture.h"

#include <global/simplemath.h>

#include <hb.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_visible_baked_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using namespace NWB::Impl::Ui;
using namespace NWB::UiTextVisibleTests;

class TextVisibleBakedTests : public TextVisibleFixture{
protected:
    [[nodiscard]] static Rect outlineRectangle(const PlacedGlyph& glyph, const Point origin){
        return
            Rect{ origin.x + glyph.position.x + glyph.ink.x, origin.y + glyph.position.y + glyph.ink.y,
                glyph.ink.width, glyph.ink.height }
        ;
    }

    [[nodiscard]] static Rect outlineCenterClip(const PlacedGlyph& glyph, const Point origin){
        const Rect outline = outlineRectangle(glyph, origin);
        return { outline.x + outline.width * 0.5f, outline.y + outline.height * 0.5f, 1.0f, 1.0f };
    }

    [[nodiscard]] static Rect bakedRectangle(const PlacedGlyph& glyph, const Point origin){
        const BakedFontAtlas& atlas = *glyph.face->bakedAtlas();
        const FontAtlasGlyph& record = *atlas.glyph(glyph.glyphId);
        const f32 scale = 32.0f / static_cast<f32>(atlas.unitsPerEm());
        return
            Rect{ origin.x + glyph.position.x + record.planeLeft * scale,
                origin.y + glyph.position.y + record.planeTop * scale,
                (record.planeRight - record.planeLeft) * scale, (record.planeBottom - record.planeTop) * scale }
        ;
    }


public:
    TextVisibleBakedTests()
        : m_baked(m_arena, Name("tests/ui/fonts/visible_atlas"))
    {}


protected:
    [[nodiscard]] bool installBaked(const StringView text){
        if(!installCoverageFonts() || m_text.layout({ .text = text, .fontSize = 32.0f }, m_layout) != TextLayoutStatus::Success)
            return false;
        FontAtlasPayload payload(m_arena);
        payload.font = Core::Assets::AssetRef<Font>("tests/ui/fonts/latin");
        const Core::Assets::AssetBytes& bytes = m_latin.fontBytes();
        payload.fontSha256 = ComputeSha256({ bytes.data(), bytes.size() });
        hb_blob_t* blob = hb_blob_create(
            reinterpret_cast<const char*>(bytes.data()), static_cast<u32>(bytes.size()),
            HB_MEMORY_MODE_READONLY, nullptr, nullptr
        );
        hb_face_t* face = hb_face_create(blob, 0u);
        payload.unitsPerEm = hb_face_get_upem(face);
        payload.sourceGlyphCount = hb_face_get_glyph_count(face);
        const u32 tags[]{ s_FontAtlasGdefTag, s_FontAtlasGposTag, s_FontAtlasKernTag };
        for(const u32 tag : tags){
            hb_blob_t* tableBlob = hb_face_reference_table(face, tag);
            u32 size = 0u;
            const char* data = hb_blob_get_data(tableBlob, &size);
            if(size != 0u){
                payload.positioningTables.emplace_back(m_arena);
                FontAtlasPositioningTable& table = payload.positioningTables.back();
                table.tag = tag;
                table.bytes.assign(reinterpret_cast<const u8*>(data), reinterpret_cast<const u8*>(data) + size);
                table.sha256 = ComputeSha256({ table.bytes.data(), table.bytes.size() });
            }
            hb_blob_destroy(tableBlob);
        }
        hb_face_destroy(face);
        hb_blob_destroy(blob);
        payload.bakePpem = 32u;
        payload.ascenderUnits = 1000.0f;
        payload.descenderUnits = -250.0f;
        payload.glyphs.resize(payload.sourceGlyphCount);
        for(u32 id = 0u; id < payload.sourceGlyphCount; ++id)
            payload.glyphs[id].glyphId = id;
        const f32 unitsPerPixel = static_cast<f32>(payload.unitsPerEm) / 32.0f;
        for(const PlacedGlyph& placed : m_layout.glyphs()){
            if(placed.face->identity() != payload.font || placed.glyphId >= payload.glyphs.size())
                return false;
            FontAtlasGlyph& glyph = payload.glyphs[placed.glyphId];
            if(glyph.drawable != 0u || placed.ink.width <= 0.0f || placed.ink.height <= 0.0f)
                continue;
            if(payload.groups.size() == s_FontAtlasMaxGroupCount)
                return false;
            const f32 left = Floor(placed.ink.x) - 8.0f;
            const f32 top = Floor(placed.ink.y) - 8.0f;
            const u32 width = static_cast<u32>(Ceil(placed.ink.x + placed.ink.width) - left + 8.0f);
            const u32 height = static_cast<u32>(Ceil(placed.ink.y + placed.ink.height) - top + 8.0f);
            if(width > 62u || height > 62u)
                return false;
            glyph.group = static_cast<u32>(payload.groups.size());
            glyph.drawable = 1u;
            glyph.x = 1u;
            glyph.y = 1u;
            glyph.width = width;
            glyph.height = height;
            glyph.planeLeft = left * unitsPerPixel;
            glyph.planeTop = top * unitsPerPixel;
            glyph.planeRight = glyph.planeLeft + static_cast<f32>(width) * unitsPerPixel;
            glyph.planeBottom = glyph.planeTop + static_cast<f32>(height) * unitsPerPixel;
            payload.groups.emplace_back(m_arena);
            FontAtlasGroup& group = payload.groups.back();
            group.width = 64u;
            group.height = 64u;
            group.pixels.resize(64u * 64u * 4u, 192u);
            group.sha256 = ComputeSha256({ group.pixels.data(), group.pixels.size() });
        }
        m_baked.setPayload(Move(payload));
        if(!m_baked.validatePayload())
            return false;
        const FontSource sources[]{
            { Core::Assets::AssetRef<Font>("tests/ui/fonts/latin"), m_latin, 1u, &m_baked },
            { Core::Assets::AssetRef<Font>("tests/ui/fonts/korean"), m_korean, 1u },
        };
        return
            m_text.setFonts(sources, 2u)
            && m_text.layout({ .text = text, .fontSize = 32.0f }, m_layout) == TextLayoutStatus::Success
        ;
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
    const Rect outline = outlineRectangle(glyph, origin);
    const Rect plane = bakedRectangle(glyph, origin);
    const Rect clip{ outline.x - 4.0f, outline.y + outline.height * 0.5f, 2.0f, 1.0f };
    ASSERT_LT(clip.x + clip.width, outline.x);
    ASSERT_GE(clip.x, plane.x);
    ASSERT_GE(clip.y, plane.y);
    ASSERT_LE(clip.x + clip.width, plane.x + plane.width);
    ASSERT_LE(clip.y + clip.height, plane.y + plane.height);
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
    EXPECT_EQ(snapshot.commands()[0u].material, PaintMaterial::SdfGlyph);
    ExpectRectangle(snapshot.commands()[0u].clip, clip);
    ASSERT_EQ(snapshot.vertices().size(), 4u);
    EXPECT_EQ(snapshot.indices().size(), 6u);
    EXPECT_FLOAT_EQ(snapshot.vertices()[0u].position.x, clip.x);
    EXPECT_FLOAT_EQ(snapshot.vertices()[0u].position.y, clip.y);
    EXPECT_FLOAT_EQ(snapshot.vertices()[2u].position.x, clip.x + clip.width);
    EXPECT_FLOAT_EQ(snapshot.vertices()[2u].position.y, clip.y + clip.height);
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
        SharedGlyphPage page = makePage(1000u + index);
        ASSERT_TRUE(page);
        prior.push_back(Move(page));
    }
    const Point origin{ 20.0f, 20.0f };
    const Rect clip = outlineCenterClip(m_layout.glyphs()[0u], origin);
    for(usize index = 1u; index < m_layout.glyphs().size(); ++index)
        ASSERT_GT(bakedRectangle(m_layout.glyphs()[index], origin).y, clip.y + clip.height);
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
    EXPECT_EQ(snapshot.commands()[0u].material, PaintMaterial::SdfGlyph);
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
        EXPECT_EQ(revealed.commands()[index].material, PaintMaterial::SdfGlyph);
    }
    EXPECT_EQ(revealed.vertices().size(), 12u);
    EXPECT_EQ(revealed.indices().size(), 18u);
    EXPECT_TRUE(hidden.sdfPages().empty());
}

TEST_F(TextVisibleBakedTests, VisibleMixedCapacityFailurePreservesPriorPageVersionBindingsAndGeometry){
    ASSERT_TRUE(installBaked("A"));
    ASSERT_EQ(m_text.layout({ .text = "\xED\x95\x9C", .fontSize = 32.0f }, m_layout), TextLayoutStatus::Success);
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
        SharedGlyphPage page = makePage(2000u + index);
        ASSERT_TRUE(page);
        prior.push_back(Move(page));
    }
    ASSERT_EQ(m_text.layout({ .text = "\xEA\xB8\x80" "A", .fontSize = 32.0f }, m_layout), TextLayoutStatus::Success);
    ASSERT_EQ(m_layout.glyphs().size(), 2u);
    ASSERT_NE(m_layout.glyphs()[0u].glyphId, warmGlyph);
    ASSERT_FALSE(m_layout.glyphs()[0u].face->bakedAtlas());
    ASSERT_TRUE(m_layout.glyphs()[1u].face->bakedAtlas());
    beginPaint(2u);
    ASSERT_TRUE(m_paint.prepareGlyphPages(prior.data(), prior.size()));
    m_paint.fillRect({ 2.0f, 3.0f, 5.0f, 7.0f }, { 0.6f, 0.3f, 0.2f, 0.5f });
    ASSERT_TRUE(m_paint.drawGlyph(oldPage, { 12.0f, 13.0f, 5.0f, 7.0f }, { 0.0f, 0.0f, 0.25f, 0.25f }));
    PaintBuilder reference(m_arena);
    reference.begin({ 800.0f, 600.0f, 1.0f, 1.0f }, 2u, 1u, Core::Assets::AssetRef<UiSkin>("tests/ui/skin"), m_skin);
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
    ASSERT_EQ(m_text.layout({ .text = "\xEA\xB8\x80", .fontSize = 32.0f }, m_layout), TextLayoutStatus::Success);
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

