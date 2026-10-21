// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/text/atlas.h>
#include <impl/ecs_ui/toolkit/text/service.h>

#include <core/common/log.h>

#include <tests/common/font_fixture.h>

#include <global/filesystem.h>
#include <global/simdmath.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_font_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using namespace NWB::Impl::Ui;

constexpr AStringView s_LatinFontPath = "tests/ui/fonts/latin";
constexpr AStringView s_KoreanFontPath = "tests/ui/fonts/korean";
constexpr AStringView s_SkinPath = "tests/ui/skin";
constexpr Name s_FontScratchArenaName = Name("tests/ui/fonts/operation_scratch");
constexpr AStringView s_KoreanLanguage = "ko";
constexpr AStringView s_SampleGlyphText = "A";
constexpr AStringView s_WideGlyphText = "W";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr i32 s_FontConstructionLogFailure = 61;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ThrowingFontConstructionLogger final : public Core::Common::ILogger{
public:
    explicit ThrowingFontConstructionLogger(Core::Common::LogArena& arena)noexcept
        : m_arena(arena)
    {}


public:
    virtual Core::Common::LogArena& arena()noexcept override{ return m_arena; }

    virtual void enqueue(Core::Common::LogString&& text, Core::Common::LogType::Enum)override{ observe(text); }

    virtual void enqueue(const Core::Common::LogString& text, Core::Common::LogType::Enum)override{ observe(text); }

    [[nodiscard]] bool sawFaceFailure()const noexcept{ return m_sawFaceFailure; }


private:
    void observe(const Core::Common::LogString& text){
        if(text.find(NWB_TEXT("UI font face initialization failed")) == Core::Common::LogString::npos)
            return;
        m_sawFaceFailure = true;
        throw s_FontConstructionLogFailure;
    }


private:
    Core::Common::LogArena& m_arena;
    bool m_sawFaceFailure = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TextFontTests : public testing::Test{
public:
    TextFontTests()
        : m_arena(Name("tests/ui/text/font"))
        , m_latin(m_arena, Name(s_LatinFontPath))
        , m_korean(m_arena, Name(s_KoreanFontPath))
        , m_service(m_arena)
        , m_layout(m_arena)
        , m_skin(m_arena, Name(s_SkinPath))
        , m_paint(m_arena)
    {}


protected:
    virtual void SetUp()override{
        Core::Alloc::ScratchArena fontScratchArena(s_FontScratchArenaName);
        ASSERT_TRUE(Tests::LoadBundledFont(m_latin, m_arena, NWB_TEST_FONT_DIRECTORY, "latin.font"));
        ASSERT_TRUE(Tests::LoadBundledFont(m_korean, m_arena, NWB_TEST_FONT_DIRECTORY, "korean.font"));
        const FontSource sources[]{
            { Core::Assets::AssetRef<Font>(s_LatinFontPath), m_latin, 1u },
            { Core::Assets::AssetRef<Font>(s_KoreanFontPath), m_korean, 1u },
        };
        ASSERT_TRUE(m_service.setFonts(fontScratchArena, sources, 2u));
        UiSkin::RegionVector regions(m_arena);
        m_skin.setAtlas(Core::Assets::AssetRef<Texture>("tests/ui/texture"), 4u, 4u, 1.0f, Move(regions));
    }

    void beginPaint(u64 generation, f32 scale = 1.0f){
        m_paint.begin({ 800.0f, 600.0f, scale, scale }, generation, 1u, Core::Assets::AssetRef<UiSkin>(s_SkinPath), m_skin);
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


TEST(TextFontConstruction, ThrowingNativeFaceDiagnosticReleasesAcquiredArenaMemory){
    Core::Alloc::GlobalArena sourceArena(Name("tests/ui/font_construction/source"));
    const Core::Assets::AssetRef<Font> emptyFontRef("tests/ui/font_construction/empty");
    Font emptyFont(sourceArena, emptyFontRef.name());
    Core::Alloc::GlobalArena fontArena(Name("tests/ui/font_construction/native"));
    Core::Alloc::ScratchArena scratchArena(Name("tests/ui/font_construction/scratch"));
    Core::Common::LogArena logArena(Name("tests/ui/font_construction/logger"));
    ThrowingFontConstructionLogger logger(logArena);
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::ReportOnly);
    const FontSource source{ emptyFontRef, emptyFont, 1u };
    const ArenaMemoryStats before = fontArena.memoryStats();
    EXPECT_THROW({ const auto failed = MakeFontFace(scratchArena, fontArena, source); EXPECT_FALSE(failed && (*failed)->valid()); }, i32);
    EXPECT_TRUE(logger.sawFaceFailure());
    const ArenaMemoryStats after = fontArena.memoryStats();
    EXPECT_GT(after.allocationCount, before.allocationCount);
    EXPECT_EQ(after.usedBytes, before.usedBytes);
    EXPECT_EQ(after.reservedBytes, before.reservedBytes);
}

TEST_F(TextFontTests, MissingLatinHangulGlyphsUseKoreanFallbackForBothLabelClusters){
    ShapeRequest request{ "\xed\x95\x9c\xea\xb8\x80" };
    request.scriptTag = TextScriptTag('H', 'a', 'n', 'g');
    request.language = s_KoreanLanguage;
    {
        auto layoutResult = m_service.layout(request);
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    ASSERT_EQ(m_layout.clusters().size(), 2u);
    for(const PlacedGlyph& glyph : m_layout.glyphs())
        EXPECT_EQ(glyph.face->identity().name(), Name(s_KoreanFontPath));
}

TEST_F(TextFontTests, RtlHarfBuzzClustersUseLogicalEndRatherThanNextVisualStart){
    ShapeRequest request{ "abc" };
    request.direction = TextDirection::RightToLeft;
    {
        auto layoutResult = m_service.layout(request);
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    ASSERT_EQ(m_layout.clusters().size(), 3u);
    EXPECT_EQ(m_layout.clusters()[0].byteBegin, 2u);
    EXPECT_EQ(m_layout.clusters()[0].byteEnd, 3u);
    EXPECT_EQ(m_layout.clusters()[1].byteBegin, 1u);
    EXPECT_EQ(m_layout.clusters()[1].byteEnd, 2u);
    EXPECT_GT(m_layout.clusters()[0].leadingX, m_layout.clusters()[0].trailingX);
}

TEST_F(TextFontTests, FontReplacementAndAssetMutationKeepOldCpuFaceAndLayoutUsable){
    Core::Alloc::ScratchArena fontScratchArena(s_FontScratchArenaName);
    {
        auto layoutResult = m_service.layout({ "AV" });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    const SharedFontFace oldFace = m_layout.glyphs()[0].face;
    const f32 width = m_layout.measure().x;
    const u64 oldServiceGeneration = m_service.generation();
    const FontSource replacement{ Core::Assets::AssetRef<Font>(s_LatinFontPath), m_latin, 2u };
    ASSERT_TRUE(m_service.setFonts(fontScratchArena, &replacement, 1u));
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
    EXPECT_FALSE(m_service.setFonts(fontScratchArena, nullptr, 0u));
    EXPECT_EQ(m_service.generation(), oldServiceGeneration + 1u);
}

TEST_F(TextFontTests, AppendingGlyphKeepsEarlierCoverageBytesImmutable){
    {
        auto layoutResult = m_service.layout({ s_SampleGlyphText });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    beginPaint(1u);
    ASSERT_TRUE(m_service.paint(m_paint, m_layout, { 20.0f, 20.0f }));
    const DrawSnapshot first = m_paint.freeze();
    ASSERT_EQ(first.glyphPages().size(), 1u);
    const SharedGlyphPage oldPage = first.glyphPages()[0];
    PaintVector<u8> saved(m_arena);
    saved.assign(oldPage->pixels().begin(), oldPage->pixels().end());
    {
        auto layoutResult = m_service.layout({ "B" });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
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
}

TEST_F(TextFontTests, RasterDpiChangesPagesWithoutChangingLogicalMeasurement){
    {
        auto layoutResult = m_service.layout({ "Ag" });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
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
}

TEST_F(TextFontTests, OversizedGlyphRejectsWholeLabelBeforePaintingAnyQuads){
    ShapeRequest request{ s_WideGlyphText };
    request.fontSize = 1024.0f;
    {
        auto layoutResult = m_service.layout(request);
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    beginPaint(1u);
    m_paint.fillRect({ 1.0f, 1.0f, 10.0f, 10.0f });
    EXPECT_FALSE(m_service.paint(m_paint, m_layout, { 20.0f, 20.0f }));
    const DrawSnapshot snapshot = m_paint.freeze();
    EXPECT_EQ(snapshot.vertices().size(), 4u);
    EXPECT_TRUE(snapshot.glyphPages().empty());
}

TEST_F(TextFontTests, GlyphAtlasIndexKeepsFaceSizeAndEmptyGlyphsDistinctAcrossReset){
    Core::Alloc::ScratchArena fontScratchArena(s_FontScratchArenaName);
    {
        auto layoutResult = m_service.layout({ s_SampleGlyphText });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    ASSERT_EQ(m_layout.glyphs().size(), 1u);
    const SharedFontFace face = m_layout.glyphs()[0].face;
    const u32 glyphId = m_layout.glyphs()[0].glyphId;
    const FontSource source{ Core::Assets::AssetRef<Font>(s_LatinFontPath), m_latin, 1u };
    auto otherFaceResult = MakeFontFace(fontScratchArena, m_arena, source);
    ASSERT_TRUE(otherFaceResult && (*otherFaceResult)->valid());
    const SharedFontFace otherFace = Move(*otherFaceResult);
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

    {
        auto layoutResult = m_service.layout({ " " });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
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

    auto oldPageCreationResult = atlas.page(0u);
    ASSERT_TRUE(oldPageCreationResult);
    const SharedGlyphPage oldPage = Move(*oldPageCreationResult);
    ASSERT_TRUE(oldPage);
    atlas.reset();
    EXPECT_EQ(atlas.glyphCount(), 0u);
    EXPECT_EQ(atlas.pageCount(), 0u);
    EXPECT_EQ(atlas.find(face, glyphId, 24u), nullptr);
    EXPECT_EQ(atlas.find(face, spaceId, 24u), nullptr);
    ASSERT_TRUE(atlas.prepare(face, glyphId, 24u));
    auto newPageCreationResult = atlas.page(0u);
    ASSERT_TRUE(newPageCreationResult);
    const SharedGlyphPage newPage = Move(*newPageCreationResult);
    ASSERT_TRUE(newPage);
    EXPECT_NE(newPage->binding().atlasIdentity, oldPage->binding().atlasIdentity);
}

TEST_F(TextFontTests, AtlasGrowthStaysWithinBoundsAndStopsAtSixteenPages){
    {
        auto layoutResult = m_service.layout({ s_WideGlyphText });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    const PlacedGlyph& glyph = m_layout.glyphs()[0];
    GlyphAtlas atlas(m_arena);
    for(u32 index = 0u; index < s_GlyphAtlasMaxPages; ++index){
        ASSERT_TRUE(atlas.prepare(glyph.face, glyph.glyphId, 460u + index));
        ASSERT_EQ(atlas.pageCount(), index + 1u);
        const AtlasGlyph* record = atlas.find(glyph.face, glyph.glyphId, 460u + index);
        ASSERT_NE(record, nullptr);
        const SIMDVectorSingle pixelEnd = VectorAdd(VectorSet(record->pixels.x, record->pixels.y, 0.0f, 0.0f), VectorSet(record->pixels.width, record->pixels.height, 0.0f, 0.0f));
        EXPECT_LT(VectorGetX(pixelEnd), static_cast<f32>(s_GlyphAtlasPageExtent));
        EXPECT_LT(VectorGetY(pixelEnd), static_cast<f32>(s_GlyphAtlasPageExtent));
        auto pageResult = atlas.page(index);
        ASSERT_TRUE(pageResult);
        const SharedGlyphPage page = Move(*pageResult);
        ASSERT_TRUE(page);
    }
    EXPECT_FALSE(atlas.prepare(glyph.face, glyph.glyphId, 476u));
    EXPECT_EQ(atlas.find(glyph.face, glyph.glyphId, 476u), nullptr);
    EXPECT_TRUE(atlas.prepare(glyph.face, glyph.glyphId, 460u));
    EXPECT_EQ(atlas.glyphCount(), s_GlyphAtlasMaxPages);
    EXPECT_EQ(atlas.pageCount(), s_GlyphAtlasMaxPages);
}

TEST_F(TextFontTests, NativeCffCoverageAndFaceSurviveFontReplacement){
    Core::Alloc::ScratchArena fontScratchArena(s_FontScratchArenaName);
    ShapeRequest request{ "\xed\x95\x9c" };
    request.scriptTag = 0x48616e67u;
    request.language = s_KoreanLanguage;
    {
        auto layoutResult = m_service.layout(request);
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    ASSERT_EQ(m_layout.glyphs().size(), 1u);
    const PlacedGlyph& glyph = m_layout.glyphs()[0];
    EXPECT_FALSE(glyph.face->coverageInkReliable());
    EXPECT_TRUE(glyph.coverage.known);
    EXPECT_GT(glyph.coverage.ink.width, 0.0f);
    EXPECT_GT(glyph.coverage.ink.height, 0.0f);
    const Rect saved = glyph.coverage.ink;
    const FontSource replacement{ Core::Assets::AssetRef<Font>(s_LatinFontPath), m_latin, 2u };
    ASSERT_TRUE(m_service.setFonts(fontScratchArena, &replacement, 1u));
    EXPECT_TRUE(glyph.coverage.known);
    EXPECT_FLOAT_EQ(glyph.coverage.ink.x, saved.x);
    EXPECT_FLOAT_EQ(glyph.coverage.ink.width, saved.width);
    EXPECT_TRUE(glyph.face->valid());
}

TEST_F(TextFontTests, NativeCffBoundsCoverFractionalDpiRasterFringes){
    const f32 sizes[]{ 32.0078125f, 64.21875f, 320.0078125f };
    for(const f32 fontSize : sizes){
        ShapeRequest request{ "\xed\x95\x9c" };
        request.scriptTag = 0x48616e67u;
        request.language = s_KoreanLanguage;
        request.fontSize = fontSize;
        {
            auto layoutResult = m_service.layout(request);
            ASSERT_TRUE(layoutResult);
            m_layout = Move(*layoutResult);
        }
        ASSERT_EQ(m_layout.glyphs().size(), 1u);
        const PlacedGlyph& glyph = m_layout.glyphs()[0];
        ASSERT_TRUE(glyph.coverage.known);
        const u32 pixelSize = static_cast<u32>(VectorGetX(VectorCeiling(VectorMultiply(VectorReplicate(fontSize), VectorReplicate(1.375f)))));
        GlyphBitmap bitmap(m_arena);
        ASSERT_TRUE(glyph.face->rasterize(glyph.glyphId, pixelSize, bitmap));
        const SIMDVectorSingle scale = VectorDivide(VectorReplicate(static_cast<f32>(pixelSize)), VectorReplicate(fontSize));
        const SIMDVectorSingle fringe = VectorDivide(VectorReplicate(2.0f), scale);
        const Rect& bounds = glyph.coverage.ink;
        const SIMDVectorSingle boundsStart = VectorSet(bounds.x, bounds.y, 0.0f, 0.0f);
        const SIMDVectorSingle bearing = VectorXorInt(VectorSet(static_cast<f32>(bitmap.bearingX), static_cast<f32>(bitmap.bearingY), 0.0f, 0.0f), s_SIMDFlipY);
        const SIMDVectorSingle rasterStart = VectorDivide(bearing, scale);
        const SIMDVectorSingle rasterEnd = VectorDivide(VectorAdd(bearing, VectorSet(static_cast<f32>(bitmap.width), static_cast<f32>(bitmap.height), 0.0f, 0.0f)), scale);
        const SIMDVectorSingle limitStart = VectorSubtract(boundsStart, fringe);
        const SIMDVectorSingle limitEnd = VectorAdd(VectorAdd(boundsStart, VectorSet(bounds.width, bounds.height, 0.0f, 0.0f)), fringe);
        EXPECT_GE(VectorGetX(rasterStart), VectorGetX(limitStart));
        EXPECT_GE(VectorGetY(rasterStart), VectorGetY(limitStart));
        EXPECT_LE(VectorGetX(rasterEnd), VectorGetX(limitEnd));
        EXPECT_LE(VectorGetY(rasterEnd), VectorGetY(limitEnd));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

