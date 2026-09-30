// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/text/atlas.h>
#include <impl/ecs_ui/toolkit/text/service.h>

#include <tests/common/font_fixture.h>

#include <global/filesystem.h>
#include <global/simplemath.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_font_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using namespace NWB::Impl::Ui;

class TextFontTests : public testing::Test{
public:
    TextFontTests()
        : m_arena(Name("tests/ui/text/font"))
        , m_latin(m_arena, Name("tests/ui/fonts/latin"))
        , m_korean(m_arena, Name("tests/ui/fonts/korean"))
        , m_service(m_arena)
        , m_layout(m_arena)
        , m_skin(m_arena, Name("tests/ui/skin"))
        , m_paint(m_arena)
    {}


protected:
    virtual void SetUp()override{
        ASSERT_TRUE(loadFont(m_latin, "latin.font"));
        ASSERT_TRUE(loadFont(m_korean, "korean.font"));
        const FontSource sources[]{
            { Core::Assets::AssetRef<Font>("tests/ui/fonts/latin"), m_latin, 1u },
            { Core::Assets::AssetRef<Font>("tests/ui/fonts/korean"), m_korean, 1u },
        };
        ASSERT_TRUE(m_service.setFonts(sources, 2u));
        UiSkin::RegionVector regions(m_arena);
        m_skin.setAtlas(Core::Assets::AssetRef<Texture>("tests/ui/texture"), 4u, 4u, 1.0f, Move(regions));
    }

    [[nodiscard]] bool loadFont(Font& font, StringView filename){
        const ::Path<Core::Alloc::GlobalArena> path = ::Path<Core::Alloc::GlobalArena>(m_arena, NWB_TEST_FONT_DIRECTORY) / filename;
        Core::Assets::AssetBytes bytes(m_arena);
        if(!Tests::ReadBundledFontBytes(path, bytes))
            return false;
        font.setFontBytes(Move(bytes));
        return font.validatePayload();
    }

    void beginPaint(u64 generation, f32 scale = 1.0f){
        m_paint.begin({ 800.0f, 600.0f, scale, scale }, generation, 1u,
            Core::Assets::AssetRef<UiSkin>("tests/ui/skin"), m_skin);
    }


protected:
    Core::Alloc::GlobalArena m_arena;
    Font m_latin;
    Font m_korean;
    TextService m_service;
    TextLayout m_layout;
    UiSkin m_skin;
    PaintBuilder m_paint;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(TextFontTests, BundledLatinShapingHasKerningLigaturesAndCombiningClusters){
    ASSERT_EQ(m_service.layout({ "A" }, m_layout), TextLayoutStatus::Success);
    const f32 aWidth = m_layout.measure().x;
    ASSERT_EQ(m_service.layout({ "V" }, m_layout), TextLayoutStatus::Success);
    const f32 vWidth = m_layout.measure().x;
    ASSERT_EQ(m_service.layout({ "AV" }, m_layout), TextLayoutStatus::Success);
    EXPECT_LT(m_layout.measure().x, aWidth + vWidth);
    ASSERT_EQ(m_service.layout({ "ffi" }, m_layout), TextLayoutStatus::Success);
    ASSERT_EQ(m_layout.clusters().size(), 1u);
    EXPECT_EQ(m_layout.clusters()[0].byteEnd, 3u);
    ASSERT_EQ(m_service.layout({ "A\xcc\x81" }, m_layout), TextLayoutStatus::Success);
    ASSERT_EQ(m_layout.clusters().size(), 1u);
    EXPECT_EQ(m_layout.clusters()[0].byteBegin, 0u);
    EXPECT_EQ(m_layout.clusters()[0].byteEnd, 3u);
    ASSERT_FALSE(m_layout.glyphs().empty());
    EXPECT_EQ(m_layout.glyphs()[0].face->identity().name(), Name("tests/ui/fonts/latin"));
}

TEST_F(TextFontTests, KoreanFallbackPreservesSyllableBytesAndComposesDecomposedJamo){
    ShapeRequest request{ "\xed\x95\x9c\xea\xb8\x80" };
    request.scriptTag = TextScriptTag('H', 'a', 'n', 'g');
    request.language = "ko";
    ASSERT_EQ(m_service.layout(request, m_layout), TextLayoutStatus::Success);
    ASSERT_EQ(m_layout.clusters().size(), 2u);
    EXPECT_EQ(m_layout.clusters()[0].byteBegin, 0u);
    EXPECT_EQ(m_layout.clusters()[0].byteEnd, 3u);
    EXPECT_EQ(m_layout.clusters()[1].byteEnd, 6u);
    for(const PlacedGlyph& glyph : m_layout.glyphs())
        EXPECT_EQ(glyph.face->identity().name(), Name("tests/ui/fonts/korean"));
    request.text = "\xe1\x84\x92\xe1\x85\xa1\xe1\x86\xab";
    ASSERT_EQ(m_service.layout(request, m_layout), TextLayoutStatus::Success);
    ASSERT_EQ(m_layout.clusters().size(), 1u);
    EXPECT_EQ(m_layout.clusters()[0].byteEnd, 9u);
    EXPECT_EQ(m_layout.glyphs().size(), 1u);
}

TEST_F(TextFontTests, RtlHarfBuzzClustersUseLogicalEndRatherThanNextVisualStart){
    ShapeRequest request{ "abc" };
    request.direction = TextDirection::RightToLeft;
    ASSERT_EQ(m_service.layout(request, m_layout), TextLayoutStatus::Success);
    ASSERT_EQ(m_layout.clusters().size(), 3u);
    EXPECT_EQ(m_layout.clusters()[0].byteBegin, 2u);
    EXPECT_EQ(m_layout.clusters()[0].byteEnd, 3u);
    EXPECT_EQ(m_layout.clusters()[1].byteBegin, 1u);
    EXPECT_EQ(m_layout.clusters()[1].byteEnd, 2u);
    EXPECT_GT(m_layout.clusters()[0].leadingX, m_layout.clusters()[0].trailingX);
}

TEST_F(TextFontTests, FontReplacementAndAssetMutationKeepOldCpuFaceAndLayoutUsable){
    ASSERT_EQ(m_service.layout({ "AV" }, m_layout), TextLayoutStatus::Success);
    const SharedFontFace oldFace = m_layout.glyphs()[0].face;
    const f32 width = m_layout.measure().x;
    const u64 oldServiceGeneration = m_service.generation();
    const FontSource replacement{ Core::Assets::AssetRef<Font>("tests/ui/fonts/latin"), m_latin, 2u };
    ASSERT_TRUE(m_service.setFonts(&replacement, 1u));
    EXPECT_EQ(m_service.generation(), oldServiceGeneration + 1u);
    Core::Assets::AssetBytes cleared(m_arena);
    m_latin.setFontBytes(Move(cleared));
    beginPaint(1u);
    ASSERT_TRUE(m_service.paint(m_paint, m_layout, { 20.0f, 20.0f }));
    const DrawSnapshot snapshot = m_paint.freeze();
    ASSERT_FALSE(snapshot.glyphPages().empty());
    EXPECT_EQ(snapshot.glyphPages()[0]->binding().fontGeneration, 1u);
    EXPECT_EQ(oldFace->generation(), 1u);
    EXPECT_FLOAT_EQ(m_layout.measure().x, width);
    EXPECT_FALSE(m_service.setFonts(nullptr, 0u));
    EXPECT_EQ(m_service.generation(), oldServiceGeneration + 1u);
}

TEST_F(TextFontTests, GlyphPagesAppendImmutablyAndWarmFramesReuseTheirExactVersion){
    ASSERT_EQ(m_service.layout({ "A" }, m_layout), TextLayoutStatus::Success);
    beginPaint(1u);
    ASSERT_TRUE(m_service.paint(m_paint, m_layout, { 20.0f, 20.0f }));
    const DrawSnapshot first = m_paint.freeze();
    ASSERT_EQ(first.glyphPages().size(), 1u);
    const SharedGlyphPage oldPage = first.glyphPages()[0];
    PaintVector<u8> saved(m_arena);
    saved.assign(oldPage->pixels().begin(), oldPage->pixels().end());
    ASSERT_EQ(m_service.layout({ "B" }, m_layout), TextLayoutStatus::Success);
    beginPaint(2u);
    ASSERT_TRUE(m_service.paint(m_paint, m_layout, { 20.0f, 20.0f }));
    const DrawSnapshot second = m_paint.freeze();
    ASSERT_EQ(second.glyphPages().size(), 1u);
    const SharedGlyphPage newPage = second.glyphPages()[0];
    EXPECT_EQ(newPage->binding().atlasIdentity, oldPage->binding().atlasIdentity);
    EXPECT_GT(newPage->binding().generation, oldPage->binding().generation);
    EXPECT_EQ(oldPage->pixels(), saved);
    for(usize pixel = 0u; pixel < saved.size(); ++pixel){
        if(saved[pixel] != 0u)
            EXPECT_EQ(newPage->pixels()[pixel], saved[pixel]);
    }
    beginPaint(3u);
    ASSERT_TRUE(m_service.paint(m_paint, m_layout, { 20.0f, 20.0f }));
    const DrawSnapshot warm = m_paint.freeze();
    ASSERT_EQ(warm.glyphPages().size(), 1u);
    EXPECT_EQ(warm.glyphPages()[0].get(), newPage.get());
}

TEST_F(TextFontTests, RasterDpiChangesPagesWithoutChangingLogicalMeasurement){
    ASSERT_EQ(m_service.layout({ "Ag" }, m_layout), TextLayoutStatus::Success);
    const Point measured = m_layout.measure();
    beginPaint(1u);
    ASSERT_TRUE(m_service.paint(m_paint, m_layout, { 20.0f, 20.0f }));
    const DrawSnapshot first = m_paint.freeze();
    beginPaint(2u, 1.5f);
    ASSERT_TRUE(m_service.paint(m_paint, m_layout, { 20.0f, 20.0f }));
    const DrawSnapshot second = m_paint.freeze();
    EXPECT_FLOAT_EQ(m_layout.measure().x, measured.x);
    EXPECT_FLOAT_EQ(m_layout.measure().y, measured.y);
    ASSERT_EQ(first.glyphPages().size(), 1u);
    ASSERT_EQ(second.glyphPages().size(), 1u);
    EXPECT_GT(second.glyphPages()[0]->binding().generation, first.glyphPages()[0]->binding().generation);
    EXPECT_EQ(first.commands()[0].material, PaintMaterial::Glyph);
}

TEST_F(TextFontTests, OversizedGlyphRejectsWholeLabelBeforePaintingAnyQuads){
    ShapeRequest request{ "W" };
    request.fontSize = 1024.0f;
    ASSERT_EQ(m_service.layout(request, m_layout), TextLayoutStatus::Success);
    beginPaint(1u);
    m_paint.fillRect({ 1.0f, 1.0f, 10.0f, 10.0f });
    EXPECT_FALSE(m_service.paint(m_paint, m_layout, { 20.0f, 20.0f }));
    const DrawSnapshot snapshot = m_paint.freeze();
    EXPECT_EQ(snapshot.vertices().size(), 4u);
    EXPECT_TRUE(snapshot.glyphPages().empty());
}

TEST_F(TextFontTests, GlyphAtlasIndexKeepsFaceSizeAndEmptyGlyphsDistinctAcrossReset){
    ASSERT_EQ(m_service.layout({ "A" }, m_layout), TextLayoutStatus::Success);
    ASSERT_EQ(m_layout.glyphs().size(), 1u);
    const SharedFontFace face = m_layout.glyphs()[0].face;
    const u32 glyphId = m_layout.glyphs()[0].glyphId;
    const FontSource source{ Core::Assets::AssetRef<Font>("tests/ui/fonts/latin"), m_latin, 1u };
    const SharedFontFace otherFace = MakeFontFace(m_arena, source);
    ASSERT_TRUE(otherFace && otherFace->valid());
    ASSERT_NE(face.get(), otherFace.get());
    GlyphAtlas atlas(m_arena);
    ASSERT_TRUE(atlas.prepare(face, glyphId, 24u));
    const AtlasGlyph* first = atlas.find(face, glyphId, 24u);
    ASSERT_NE(first, nullptr);
    ASSERT_TRUE(atlas.prepare(face, glyphId, 24u));
    EXPECT_EQ(atlas.glyphCount(), 1u);
    EXPECT_EQ(atlas.find(face, glyphId, 24u), first);
    ASSERT_TRUE(atlas.prepare(face, glyphId, 32u));
    ASSERT_TRUE(atlas.prepare(otherFace, glyphId, 24u));
    EXPECT_EQ(atlas.glyphCount(), 3u);
    EXPECT_NE(atlas.find(face, glyphId, 32u), first);
    EXPECT_NE(atlas.find(otherFace, glyphId, 24u), first);

    ASSERT_EQ(m_service.layout({ " " }, m_layout), TextLayoutStatus::Success);
    ASSERT_EQ(m_layout.glyphs().size(), 1u);
    const u32 spaceId = m_layout.glyphs()[0].glyphId;
    GlyphBitmap bitmap(m_arena);
    ASSERT_TRUE(face->rasterize(spaceId, 24u, bitmap));
    ASSERT_TRUE(bitmap.width == 0u || bitmap.height == 0u);
    ASSERT_TRUE(atlas.prepare(face, spaceId, 24u));
    const AtlasGlyph* space = atlas.find(face, spaceId, 24u);
    ASSERT_NE(space, nullptr);
    EXPECT_EQ(space->pageIndex, s_GlyphAtlasNoPage);
    ASSERT_TRUE(atlas.prepare(face, spaceId, 24u));
    EXPECT_EQ(atlas.glyphCount(), 4u);

    const SharedGlyphPage oldPage = atlas.page(0u);
    ASSERT_TRUE(oldPage);
    atlas.reset();
    EXPECT_EQ(atlas.glyphCount(), 0u);
    EXPECT_EQ(atlas.pageCount(), 0u);
    EXPECT_EQ(atlas.find(face, glyphId, 24u), nullptr);
    EXPECT_EQ(atlas.find(face, spaceId, 24u), nullptr);
    ASSERT_TRUE(atlas.prepare(face, glyphId, 24u));
    const SharedGlyphPage newPage = atlas.page(0u);
    ASSERT_TRUE(newPage);
    EXPECT_NE(newPage->binding().atlasIdentity, oldPage->binding().atlasIdentity);
}

TEST_F(TextFontTests, AtlasGrowthStaysWithinBoundsAndStopsAtSixteenPages){
    ASSERT_EQ(m_service.layout({ "W" }, m_layout), TextLayoutStatus::Success);
    const PlacedGlyph& glyph = m_layout.glyphs()[0];
    GlyphAtlas atlas(m_arena);
    for(u32 index = 0u; index < s_GlyphAtlasMaxPages; ++index){
        ASSERT_TRUE(atlas.prepare(glyph.face, glyph.glyphId, 460u + index));
        ASSERT_EQ(atlas.pageCount(), index + 1u);
        const AtlasGlyph* record = atlas.find(glyph.face, glyph.glyphId, 460u + index);
        ASSERT_NE(record, nullptr);
        EXPECT_LT(record->pixels.x + record->pixels.width, static_cast<f32>(s_GlyphAtlasPageExtent));
        EXPECT_LT(record->pixels.y + record->pixels.height, static_cast<f32>(s_GlyphAtlasPageExtent));
        ASSERT_TRUE(atlas.page(index));
    }
    EXPECT_FALSE(atlas.prepare(glyph.face, glyph.glyphId, 476u));
    EXPECT_EQ(atlas.find(glyph.face, glyph.glyphId, 476u), nullptr);
    EXPECT_TRUE(atlas.prepare(glyph.face, glyph.glyphId, 460u));
    EXPECT_EQ(atlas.glyphCount(), s_GlyphAtlasMaxPages);
    EXPECT_EQ(atlas.pageCount(), s_GlyphAtlasMaxPages);
}

TEST_F(TextFontTests, OrdinaryTrueTypeInkAndNativeCffCoverageRemainDistinct){
    ASSERT_EQ(m_service.layout({ "A" }, m_layout), TextLayoutStatus::Success);
    ASSERT_EQ(m_layout.glyphs().size(), 1u);
    EXPECT_TRUE(m_layout.glyphs()[0].face->coverageInkReliable());
    EXPECT_GT(m_layout.glyphs()[0].face->unitsPerEm(), 0u);
    EXPECT_FALSE(m_layout.glyphs()[0].coverage.known);
    ShapeRequest request{ "\xed\x95\x9c" };
    request.scriptTag = 0x48616e67u;
    request.language = "ko";
    ASSERT_EQ(m_service.layout(request, m_layout), TextLayoutStatus::Success);
    ASSERT_EQ(m_layout.glyphs().size(), 1u);
    const PlacedGlyph& glyph = m_layout.glyphs()[0];
    EXPECT_FALSE(glyph.face->coverageInkReliable());
    EXPECT_TRUE(glyph.coverage.known);
    EXPECT_GT(glyph.coverage.ink.width, 0.0f);
    EXPECT_GT(glyph.coverage.ink.height, 0.0f);
    EXPECT_FLOAT_EQ(m_layout.inkBounds().x, glyph.position.x + glyph.ink.x);
    EXPECT_FLOAT_EQ(m_layout.inkBounds().y, glyph.position.y + glyph.ink.y);
    const Rect saved = glyph.coverage.ink;
    const FontSource replacement{ Core::Assets::AssetRef<Font>("tests/ui/fonts/latin"), m_latin, 2u };
    ASSERT_TRUE(m_service.setFonts(&replacement, 1u));
    EXPECT_TRUE(glyph.coverage.known);
    EXPECT_FLOAT_EQ(glyph.coverage.ink.x, saved.x);
    EXPECT_FLOAT_EQ(glyph.coverage.ink.width, saved.width);
    EXPECT_TRUE(glyph.face->valid());
}

TEST_F(TextFontTests, NativeCffBoundsCoverFractionalDpiRasterFringesWithoutChangingInk){
    const f32 sizes[]{ 32.0078125f, 64.21875f, 320.0078125f };
    for(const f32 fontSize : sizes){
        ShapeRequest request{ "\xed\x95\x9c" };
        request.scriptTag = 0x48616e67u;
        request.language = "ko";
        request.fontSize = fontSize;
        ASSERT_EQ(m_service.layout(request, m_layout), TextLayoutStatus::Success);
        ASSERT_EQ(m_layout.glyphs().size(), 1u);
        const PlacedGlyph& glyph = m_layout.glyphs()[0];
        ASSERT_TRUE(glyph.coverage.known);
        const u32 pixelSize = static_cast<u32>(Ceil(fontSize * 1.375f));
        GlyphBitmap bitmap(m_arena);
        ASSERT_TRUE(glyph.face->rasterize(glyph.glyphId, pixelSize, bitmap));
        const f32 scale = static_cast<f32>(pixelSize) / fontSize;
        const f32 fringe = 2.0f / scale;
        const Rect& bounds = glyph.coverage.ink;
        EXPECT_GE(static_cast<f32>(bitmap.bearingX) / scale, bounds.x - fringe);
        EXPECT_GE(-static_cast<f32>(bitmap.bearingY) / scale, bounds.y - fringe);
        EXPECT_LE((static_cast<f32>(bitmap.bearingX) + bitmap.width) / scale, bounds.x + bounds.width + fringe);
        EXPECT_LE((-static_cast<f32>(bitmap.bearingY) + bitmap.height) / scale, bounds.y + bounds.height + fringe);
        EXPECT_FLOAT_EQ(m_layout.inkBounds().width, glyph.ink.width);
        EXPECT_FLOAT_EQ(m_layout.inkBounds().height, glyph.ink.height);
    }
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

