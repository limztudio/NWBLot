// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/text/atlas.h>
#include <impl/ecs_ui/toolkit/text/glyph_visibility.h>

#include <tests/common/font_fixture.h>

#include <global/filesystem.h>
#include <global/simplemath.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_glyph_visibility_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Impl;
using namespace Impl::Ui;


static void ExpectRectangle(const Rect& actual, const Rect& expected){
    EXPECT_FLOAT_EQ(actual.x, expected.x);
    EXPECT_FLOAT_EQ(actual.y, expected.y);
    EXPECT_FLOAT_EQ(actual.width, expected.width);
    EXPECT_FLOAT_EQ(actual.height, expected.height);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TextGlyphVisibilityFontTests : public testing::Test{
public:
    TextGlyphVisibilityFontTests()
        : m_arena(Name("tests/ui/text/glyph_visibility/fonts"))
        , m_latin(m_arena, Name("tests/ui/fonts/latin"))
        , m_korean(m_arena, Name("tests/ui/fonts/korean"))
    {}


protected:
    virtual void SetUp()override{
        ASSERT_TRUE(loadFont(m_latin, "latin.font"));
        ASSERT_TRUE(loadFont(m_korean, "korean.font"));
        m_face = MakeFontFace(m_arena, { Core::Assets::AssetRef<Font>("tests/ui/fonts/latin"), m_latin, 1u });
        m_koreanFace = MakeFontFace(m_arena, { Core::Assets::AssetRef<Font>("tests/ui/fonts/korean"), m_korean, 1u });
        ASSERT_TRUE(m_face);
        ASSERT_TRUE(m_koreanFace);
        ASSERT_TRUE(m_face->coverageInkReliable());
        ASSERT_FALSE(m_koreanFace->coverageInkReliable());
    }

    [[nodiscard]] bool loadFont(Font& font, const StringView filename){
        const ::Path<Core::Alloc::GlobalArena> directory(m_arena, NWB_TEST_FONT_DIRECTORY);
        const ::Path<Core::Alloc::GlobalArena> path = directory / filename;
        Core::Assets::AssetBytes bytes(m_arena);
        if(!Tests::ReadBundledFontBytes(path, bytes))
            return false;
        font.setFontBytes(Move(bytes));
        return font.validatePayload();
    }


protected:
    Core::Alloc::GlobalArena m_arena;
    Font m_latin;
    Font m_korean;
    SharedFontFace m_face;
    SharedFontFace m_koreanFace;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TextGlyphVisibilityAtlasTests : public testing::Test{
public:
    TextGlyphVisibilityAtlasTests()
        : m_arena(Name("tests/ui/text/glyph_visibility/atlas"))
        , m_asset(m_arena, Name("tests/ui/glyph_visibility/atlas"))
    {}


protected:
    virtual void SetUp()override{
        FontAtlasPayload payload(m_arena);
        payload.font = Core::Assets::AssetRef<Font>("tests/ui/glyph_visibility/font");
        const u8 source[]{ 1u };
        payload.fontSha256 = ComputeSha256({ source, 1u });
        payload.unitsPerEm = 1000u;
        payload.sourceGlyphCount = 2u;
        payload.bakePpem = 32u;
        payload.ascenderUnits = 800.0f;
        payload.descenderUnits = -200.0f;
        payload.glyphs.resize(2u);
        payload.glyphs[0u] = {
            .glyphId = 0u,
            .x = 1u,
            .y = 1u,
            .width = 2u,
            .height = 4u,
            .planeLeft = -125.0f,
            .planeTop = -500.0f,
            .planeRight = -62.5f,
            .planeBottom = -375.0f,
            .drawable = 1u,
        };
        payload.glyphs[1u].glyphId = 1u;
        payload.groups.emplace_back(m_arena);
        FontAtlasGroup& group = payload.groups.back();
        group.width = 8u;
        group.height = 8u;
        group.pixels.resize(8u * 8u * 4u, 128u);
        group.sha256 = ComputeSha256({ group.pixels.data(), group.pixels.size() });
        m_asset.setPayload(Move(payload));
        ASSERT_TRUE(m_asset.validatePayload());
        m_atlas = CreateBakedFontAtlas(m_arena, m_asset, 1u);
        ASSERT_TRUE(m_atlas);
    }


protected:
    Core::Alloc::GlobalArena m_arena;
    FontAtlas m_asset;
    SharedBakedFontAtlas m_atlas;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(TextGlyphVisibilityTests, TouchingEdgesAndEmptyAreasAreInvisible){
    const Rect clip{ 10.0f, 20.0f, 30.0f, 40.0f };
    const Rect rectangles[]{
        { 0.0f, 20.0f, 10.0f, 1.0f }, { 40.0f, 20.0f, 1.0f, 1.0f },
        { 10.0f, 10.0f, 1.0f, 10.0f }, { 10.0f, 60.0f, 1.0f, 1.0f },
        { 10.0f, 20.0f, 0.0f, 1.0f }, { 10.0f, 20.0f, 1.0f, 0.0f },
    };
    for(const Rect& rectangle : rectangles)
        EXPECT_EQ(TextGlyphVisibility::intersect(rectangle, clip), TextGlyphIntersection::Invisible);
    EXPECT_EQ(TextGlyphVisibility::intersect(clip, { 10.0f, 20.0f, 0.0f, 40.0f }), TextGlyphIntersection::Invisible);
}

TEST(TextGlyphVisibilityTests, InvalidRectanglesAndOverflowingEndpointsAreRejected){
    const Rect clip{ 0.0f, 0.0f, 100.0f, 100.0f };
    const Rect invalid[]{
        { Limit<f32>::s_QuietNaN, 0.0f, 1.0f, 1.0f },
        { 0.0f, Limit<f32>::s_Infinity, 1.0f, 1.0f },
        { 0.0f, 0.0f, -1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f, -1.0f },
        { Limit<f32>::s_Max, 0.0f, Limit<f32>::s_Max, 1.0f },
    };
    for(const Rect& rectangle : invalid){
        EXPECT_EQ(TextGlyphVisibility::intersect(rectangle, clip), TextGlyphIntersection::Invalid);
        EXPECT_EQ(TextGlyphVisibility::intersect(clip, rectangle), TextGlyphIntersection::Invalid);
    }
}

TEST(TextGlyphVisibilityTests, CoverageRectangleUsesNegativeBearingsAndLogicalOrigin){
    PlacedGlyph glyph;
    glyph.position = { 20.0f, 30.0f };
    AtlasGlyph record;
    record.pageIndex = 0u;
    record.pixels = { 50.0f, 70.0f, 8.0f, 10.0f };
    record.bearingX = -4;
    record.bearingY = 6;
    Rect rectangle;
    ASSERT_TRUE(TextGlyphVisibility::coverageRectangle(glyph, record, 2.0f, { 100.0f, 200.0f }, rectangle));
    ExpectRectangle(rectangle, { 118.0f, 227.0f, 4.0f, 5.0f });
}

TEST(TextGlyphVisibilityTests, CoverageRectangleSupportsFractionalRasterScale){
    PlacedGlyph glyph;
    AtlasGlyph record;
    record.pageIndex = 0u;
    record.pixels = { 0.0f, 0.0f, 3.0f, 6.0f };
    record.bearingX = -3;
    record.bearingY = 6;
    Rect rectangle;
    ASSERT_TRUE(TextGlyphVisibility::coverageRectangle(glyph, record, 1.5f, { 4.0f, 8.0f }, rectangle));
    ExpectRectangle(rectangle, { 2.0f, 4.0f, 2.0f, 4.0f });
}

TEST(TextGlyphVisibilityTests, NativeCoverageOriginSnapsToWholePhysicalPixelsAtOneToOneScale){
    PlacedGlyph glyph;
    glyph.position = { 0.25f, 0.25f };
    AtlasGlyph record;
    record.pageIndex = 0u;
    record.pixels = { 0.0f, 0.0f, 5.0f, 7.0f };
    record.bearingX = -2;
    record.bearingY = 3;
    Rect rectangle;
    ASSERT_TRUE(TextGlyphVisibility::coverageRectangle(glyph, record, 1.0f, { 10.0f, 20.0f }, rectangle, { 1.0f, 1.0f }));
    ExpectRectangle(rectangle, { 8.0f, 17.0f, 5.0f, 7.0f });
    ASSERT_TRUE(TextGlyphVisibility::coverageRectangle(glyph, record, 1.0f, { 10.5f, 20.5f }, rectangle, { 1.0f, 1.0f }));
    ExpectRectangle(rectangle, { 9.0f, 18.0f, 5.0f, 7.0f });
}

TEST(TextGlyphVisibilityTests, NativeCoverageOriginSnapsUsingEachPhysicalPixelScale){
    PlacedGlyph glyph;
    glyph.position = { 0.25f, 0.25f };
    AtlasGlyph record;
    record.pageIndex = 0u;
    record.pixels = { 0.0f, 0.0f, 6.0f, 9.0f };
    record.bearingX = -3;
    record.bearingY = 6;
    Rect rectangle;
    ASSERT_TRUE(TextGlyphVisibility::coverageRectangle(glyph, record, 1.5f, { 4.25f, 8.25f }, rectangle, { 1.5f, 1.5f }));
    ExpectRectangle(rectangle, { 4.0f / 1.5f, 7.0f / 1.5f, 4.0f, 6.0f });
    ASSERT_TRUE(TextGlyphVisibility::coverageRectangle(glyph, record, 1.5f, { 4.25f, 8.25f }, rectangle, { 1.0f, 1.5f }));
    ExpectRectangle(rectangle, { 3.0f, 7.0f / 1.5f, 4.0f, 6.0f });
}

TEST(TextGlyphVisibilityTests, CoverageRecordWithoutAnImageProducesAnEmptyExactRectangle){
    PlacedGlyph glyph;
    AtlasGlyph record;
    Rect rectangle{ 1.0f, 2.0f, 3.0f, 4.0f };
    ASSERT_TRUE(TextGlyphVisibility::coverageRectangle(glyph, record, 1.0f, {}, rectangle));
    ExpectRectangle(rectangle, {});
    EXPECT_EQ(TextGlyphVisibility::intersect(rectangle, { 0.0f, 0.0f, 100.0f, 100.0f }), TextGlyphIntersection::Invisible);
}

TEST(TextGlyphVisibilityTests, InvalidCoverageGeometryPreservesTheOutput){
    PlacedGlyph glyph;
    AtlasGlyph record;
    record.pageIndex = 0u;
    record.pixels = { 0.0f, 0.0f, 1.0f, 1.0f };
    const Rect original{ 1.0f, 2.0f, 3.0f, 4.0f };
    Rect rectangle = original;
    const f32 invalidScales[]{ 0.0f, -1.0f, Limit<f32>::s_Infinity, Limit<f32>::s_QuietNaN };
    for(const f32 scale : invalidScales){
        EXPECT_FALSE(TextGlyphVisibility::coverageRectangle(glyph, record, scale, {}, rectangle));
        ExpectRectangle(rectangle, original);
    }
    record.pixels.width = -1.0f;
    EXPECT_FALSE(TextGlyphVisibility::coverageRectangle(glyph, record, 1.0f, {}, rectangle));
    ExpectRectangle(rectangle, original);
    record.pixels.width = 1.0f;
    glyph.position.x = Limit<f32>::s_QuietNaN;
    EXPECT_FALSE(TextGlyphVisibility::coverageRectangle(glyph, record, 1.0f, {}, rectangle));
    ExpectRectangle(rectangle, original);
}

TEST(TextGlyphVisibilityTests, InvalidPhysicalPixelScalesRejectNativeCoverageWithoutChangingTheOutput){
    PlacedGlyph glyph;
    glyph.coverage = { { 1.0f, 2.0f, 3.0f, 4.0f }, true };
    AtlasGlyph record;
    record.pageIndex = 0u;
    record.pixels = { 0.0f, 0.0f, 3.0f, 4.0f };
    const Rect original{ 1.0f, 2.0f, 3.0f, 4.0f };
    Rect rectangle = original;
    const Point invalidScales[]{
        { 0.0f, 1.0f }, { -1.0f, 1.0f }, { Limit<f32>::s_Infinity, 1.0f },
        { 1.0f, 0.0f }, { 1.0f, -1.0f }, { 1.0f, Limit<f32>::s_QuietNaN },
    };
    for(const Point& pixelScale : invalidScales){
        EXPECT_FALSE(TextGlyphVisibility::coverageRectangle(glyph, record, 1.0f, {}, rectangle, pixelScale));
        ExpectRectangle(rectangle, original);
        EXPECT_EQ(
            TextGlyphVisibility::candidate(glyph, nullptr, 16.0f, 16.0f, {}, { 0.0f, 0.0f, 10.0f, 10.0f }, pixelScale),
            TextGlyphIntersection::Invalid
        );
    }
}

TEST(TextGlyphVisibilityTests, UnrepresentableCoverageGeometryPreservesTheOutput){
    PlacedGlyph glyph;
    glyph.position = { Limit<f32>::s_Max, 0.0f };
    AtlasGlyph record;
    record.pageIndex = 0u;
    record.pixels = { 0.0f, 0.0f, 2.0f, 2.0f };
    const Rect original{ 1.0f, 2.0f, 3.0f, 4.0f };
    Rect rectangle = original;
    EXPECT_FALSE(TextGlyphVisibility::coverageRectangle(glyph, record, 1.0f, { Limit<f32>::s_Max, 0.0f }, rectangle));
    ExpectRectangle(rectangle, original);
    glyph.position = {};
    EXPECT_FALSE(TextGlyphVisibility::coverageRectangle(glyph, record, 1.0e-39f, {}, rectangle));
    ExpectRectangle(rectangle, original);
}

TEST(TextGlyphVisibilityTests, UnknownLegacyInkRemainsAConservativeCandidate){
    PlacedGlyph glyph;
    glyph.position = { 10000.0f, 10000.0f };
    const Rect clip{ 0.0f, 0.0f, 100.0f, 100.0f };
    EXPECT_EQ(TextGlyphVisibility::candidate(glyph, nullptr, 16.0f, 16.0f, {}, clip), TextGlyphIntersection::Visible);
    glyph.ink = { -1.0f, -2.0f, 3.0f, 4.0f };
    EXPECT_EQ(TextGlyphVisibility::candidate(glyph, nullptr, 16.0f, 16.0f, {}, clip), TextGlyphIntersection::Visible);
}

TEST(TextGlyphVisibilityTests, KnownNativeBoundsCanCullWithoutHarfBuzzInkOrFaceMetadata){
    PlacedGlyph glyph;
    glyph.position = { 10000.0f, 10000.0f };
    glyph.coverage = { { -1.0f, -2.0f, 3.0f, 4.0f }, true };
    const Rect clip{ 0.0f, 0.0f, 100.0f, 100.0f };
    EXPECT_EQ(TextGlyphVisibility::candidate(glyph, nullptr, 16.0f, 16.0f, {}, clip), TextGlyphIntersection::Invisible);
    glyph.position = { 10.0f, 20.0f };
    EXPECT_EQ(TextGlyphVisibility::candidate(glyph, nullptr, 16.0f, 16.0f, {}, clip), TextGlyphIntersection::Visible);
}

TEST(TextGlyphVisibilityTests, KnownEmptyNativeOutlinesDoNotRequirePreparation){
    PlacedGlyph glyph;
    glyph.coverage.known = true;
    const Rect clip{ 0.0f, 0.0f, 100.0f, 100.0f };
    EXPECT_EQ(TextGlyphVisibility::candidate(glyph, nullptr, 16.0f, 16.0f, {}, clip), TextGlyphIntersection::Invisible);
    glyph.coverage.ink = { 20.0f, 30.0f, 0.0f, 10.0f };
    EXPECT_EQ(TextGlyphVisibility::candidate(glyph, nullptr, 16.0f, 16.0f, {}, clip), TextGlyphIntersection::Invisible);
    glyph.coverage.ink = { 20.0f, 30.0f, 10.0f, 0.0f };
    EXPECT_EQ(TextGlyphVisibility::candidate(glyph, nullptr, 16.0f, 16.0f, {}, clip), TextGlyphIntersection::Invisible);
}

TEST(TextGlyphVisibilityTests, NativeBoundsTakePriorityOverCompetingShapingInk){
    PlacedGlyph glyph;
    glyph.ink = { 10000.0f, 10000.0f, 1.0f, 1.0f };
    glyph.coverage = { { 10.0f, 10.0f, 1.0f, 1.0f }, true };
    const Rect clip{ 0.0f, 0.0f, 20.0f, 20.0f };
    EXPECT_EQ(TextGlyphVisibility::candidate(glyph, nullptr, 16.0f, 16.0f, {}, clip), TextGlyphIntersection::Visible);
    glyph.ink = { 10.0f, 10.0f, 1.0f, 1.0f };
    glyph.coverage.ink = { 10000.0f, 10000.0f, 1.0f, 1.0f };
    EXPECT_EQ(TextGlyphVisibility::candidate(glyph, nullptr, 16.0f, 16.0f, {}, clip), TextGlyphIntersection::Invisible);
}

TEST(TextGlyphVisibilityTests, NativeRasterAllowanceTracksPhysicalScale){
    PlacedGlyph glyph;
    glyph.coverage = { { 10.0f, 10.0f, 1.0f, 1.0f }, true };
    const Rect clip{ 8.5f, 10.0f, 0.25f, 1.0f };
    EXPECT_EQ(TextGlyphVisibility::candidate(glyph, nullptr, 16.0f, 16.0f, {}, clip), TextGlyphIntersection::Visible);
    EXPECT_EQ(TextGlyphVisibility::candidate(glyph, nullptr, 16.0f, 64.0f, {}, clip), TextGlyphIntersection::Invisible);
}

TEST(TextGlyphVisibilityTests, AnisotropicPhysicalScaleRetainsSnappedNativeFringeAsCandidate){
    PlacedGlyph glyph;
    glyph.position = { 10.32f, 20.0f };
    glyph.coverage = { { 0.0f, 0.0f, 1.0f, 1.0f }, true };
    AtlasGlyph record;
    record.pageIndex = 0u;
    record.pixels = { 0.0f, 0.0f, 3.0f, 3.0f };
    record.bearingX = -3;
    const Point pixelScale{ 1.5f, 1.0f };
    const Rect clip{ 8.6f, 20.0f, 0.1f, 1.0f };
    Rect rectangle;
    ASSERT_TRUE(TextGlyphVisibility::coverageRectangle(glyph, record, 1.5f, {}, rectangle, pixelScale));
    ExpectRectangle(rectangle, { 8.0f, 20.0f, 2.0f, 2.0f });
    EXPECT_EQ(TextGlyphVisibility::intersect(rectangle, clip), TextGlyphIntersection::Visible);
    EXPECT_EQ(
        TextGlyphVisibility::candidate(glyph, nullptr, 16.0f, 24.0f, {}, clip, pixelScale),
        TextGlyphIntersection::Visible
    );
}

TEST(TextGlyphVisibilityTests, CandidateRejectsInvalidParametersAndKnownBounds){
    PlacedGlyph glyph;
    glyph.coverage = { { 1.0f, 2.0f, 3.0f, 4.0f }, true };
    const Rect clip{ 0.0f, 0.0f, 100.0f, 100.0f };
    EXPECT_EQ(TextGlyphVisibility::candidate(glyph, nullptr, 0.0f, 16.0f, {}, clip), TextGlyphIntersection::Invalid);
    EXPECT_EQ(TextGlyphVisibility::candidate(glyph, nullptr, 2049.0f, 16.0f, {}, clip), TextGlyphIntersection::Invalid);
    EXPECT_EQ(TextGlyphVisibility::candidate(glyph, nullptr, 16.0f, 0.0f, {}, clip), TextGlyphIntersection::Invalid);
    EXPECT_EQ(TextGlyphVisibility::candidate(glyph, nullptr, 16.0f, 4097.0f, {}, clip), TextGlyphIntersection::Invalid);
    glyph.coverage.ink.width = -1.0f;
    EXPECT_EQ(TextGlyphVisibility::candidate(glyph, nullptr, 16.0f, 16.0f, {}, clip), TextGlyphIntersection::Invalid);
    glyph.coverage.ink.width = 3.0f;
    glyph.position.y = Limit<f32>::s_Infinity;
    EXPECT_EQ(TextGlyphVisibility::candidate(glyph, nullptr, 16.0f, 16.0f, {}, clip), TextGlyphIntersection::Invalid);
}

TEST(TextGlyphVisibilityTests, CandidateRejectsFiniteUnrepresentableNativePlacement){
    PlacedGlyph glyph;
    glyph.position.x = Limit<f32>::s_Max;
    glyph.coverage = { { 1.0f, 2.0f, 3.0f, 4.0f }, true };
    const Rect clip{ 0.0f, 0.0f, 100.0f, 100.0f };
    EXPECT_EQ(
        TextGlyphVisibility::candidate(glyph, nullptr, 16.0f, 16.0f, { Limit<f32>::s_Max, 0.0f }, clip),
        TextGlyphIntersection::Invalid
    );
}

TEST(TextGlyphVisibilityTests, EmptyClipSkipsValidCandidatesAfterParameterValidation){
    PlacedGlyph glyph;
    EXPECT_EQ(TextGlyphVisibility::candidate(glyph, nullptr, 16.0f, 16.0f, {}, {}), TextGlyphIntersection::Invisible);
    EXPECT_EQ(TextGlyphVisibility::candidate(glyph, nullptr, 0.0f, 16.0f, {}, {}), TextGlyphIntersection::Invalid);
    EXPECT_FALSE(TextGlyphVisibility::selectAtlas(glyph, 16.0f));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(TextGlyphVisibilityFontTests, ReliableShapingInkSkipsDistantGlyphsWithoutPreparation){
    PlacedGlyph glyph;
    glyph.face = m_face;
    glyph.position = { 1000.0f, 1000.0f };
    glyph.ink = { -2.0f, -10.0f, 12.0f, 12.0f };
    EXPECT_EQ(
        TextGlyphVisibility::candidate(glyph, nullptr, 16.0f, 16.0f, {}, { 0.0f, 0.0f, 100.0f, 100.0f }),
        TextGlyphIntersection::Invisible
    );
    EXPECT_FALSE(TextGlyphVisibility::selectAtlas(glyph, 16.0f));
}

TEST_F(TextGlyphVisibilityFontTests, NegativeBearingsAndMarksCanReachTheClipOutsideTheirOrigin){
    PlacedGlyph glyph;
    glyph.face = m_face;
    glyph.position = { 20.0f, 20.0f };
    glyph.ink = { -25.0f, -10.0f, 8.0f, 20.0f };
    EXPECT_EQ(
        TextGlyphVisibility::candidate(glyph, nullptr, 16.0f, 16.0f, {}, { -5.0f, 11.0f, 2.0f, 2.0f }),
        TextGlyphIntersection::Visible
    );
    glyph.position.x = 10.0f;
    glyph.ink = { -1.0f, -30.0f, 2.0f, 2.0f };
    EXPECT_EQ(
        TextGlyphVisibility::candidate(glyph, nullptr, 16.0f, 16.0f, {}, { 8.0f, -11.0f, 5.0f, 4.0f }),
        TextGlyphIntersection::Visible
    );
}

TEST_F(TextGlyphVisibilityFontTests, HarfBuzzScaleCorrectionRetainsScaledAndOriginalInk){
    PlacedGlyph glyph;
    glyph.face = m_face;
    glyph.ink = { 1000.0f, -1.0f, 1.0f, 1.0f };
    EXPECT_EQ(
        TextGlyphVisibility::candidate(glyph, nullptr, 1.004f, 1.0f, {}, { 1006.5f, -1.0f, 0.25f, 0.25f }),
        TextGlyphIntersection::Visible
    );
    EXPECT_EQ(
        TextGlyphVisibility::candidate(glyph, nullptr, 1.011f, 1.0f, {}, { 1000.5f, -1.0f, 0.25f, 0.25f }),
        TextGlyphIntersection::Visible
    );
    glyph.coverage = { glyph.ink, true };
    EXPECT_EQ(
        TextGlyphVisibility::candidate(glyph, nullptr, 1.004f, 1.0f, {}, { 1006.5f, -1.0f, 0.25f, 0.25f }),
        TextGlyphIntersection::Invisible
    );
}

TEST_F(TextGlyphVisibilityFontTests, UntrustedUnknownInkRetainsAConservativeCandidate){
    PlacedGlyph glyph;
    glyph.face = m_koreanFace;
    glyph.position = { 1000.0f, 1000.0f };
    glyph.ink = { -2.0f, -10.0f, 12.0f, 12.0f };
    const Rect clip{ 0.0f, 0.0f, 100.0f, 100.0f };
    EXPECT_EQ(TextGlyphVisibility::candidate(glyph, nullptr, 16.0f, 16.0f, {}, clip), TextGlyphIntersection::Visible);
    glyph.coverage = { glyph.ink, true };
    EXPECT_EQ(TextGlyphVisibility::candidate(glyph, nullptr, 16.0f, 16.0f, {}, clip), TextGlyphIntersection::Invisible);
}

TEST_F(TextGlyphVisibilityFontTests, NativeCffOutlineBoundsAreAvailableForRealCoverageCandidates){
    ShapeRequest request{ "\xed\x95\x9c" };
    request.fontSize = 24.0f;
    request.scriptTag = TextScriptTag('H', 'a', 'n', 'g');
    request.language = "ko";
    PaintVector<RawShapedGlyph> shaped(m_arena);
    ASSERT_TRUE(m_koreanFace->shape(request, 0u, 3u, shaped));
    ASSERT_FALSE(shaped.empty());
    ASSERT_TRUE(shaped[0u].coverage.known);
    EXPECT_GT(shaped[0u].coverage.ink.width, 0.0f);
    EXPECT_GT(shaped[0u].coverage.ink.height, 0.0f);
    PlacedGlyph glyph;
    glyph.face = m_koreanFace;
    glyph.position = { 1000.0f, 1000.0f };
    glyph.ink = shaped[0u].ink;
    glyph.coverage = shaped[0u].coverage;
    EXPECT_EQ(
        TextGlyphVisibility::candidate(glyph, nullptr, 24.0f, 24.0f, {}, { 0.0f, 0.0f, 100.0f, 100.0f }),
        TextGlyphIntersection::Invisible
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(TextGlyphVisibilityAtlasTests, AtlasRectangleIncludesPaddedPlanesAtTheLogicalOrigin){
    PlacedGlyph glyph;
    glyph.position = { 10.0f, 20.0f };
    Rect rectangle;
    ASSERT_TRUE(TextGlyphVisibility::atlasRectangle(glyph, *m_atlas, 32.0f, { 100.0f, 200.0f }, rectangle));
    ExpectRectangle(rectangle, { 106.0f, 204.0f, 2.0f, 4.0f });
}

TEST_F(TextGlyphVisibilityAtlasTests, SdfCandidateUsesExactPlanesRatherThanShapingInk){
    PlacedGlyph glyph;
    glyph.position = { 10.0f, 20.0f };
    glyph.ink = { 1000.0f, 1000.0f, 1.0f, 1.0f };
    EXPECT_EQ(
        TextGlyphVisibility::candidate(
            glyph, m_atlas.get(), 32.0f, 32.0f, { 100.0f, 200.0f }, { 106.25f, 204.25f, 0.25f, 0.25f }
        ),
        TextGlyphIntersection::Visible
    );
    EXPECT_EQ(
        TextGlyphVisibility::candidate(
            glyph, m_atlas.get(), 32.0f, 32.0f, { 100.0f, 200.0f }, { 108.0f, 204.0f, 1.0f, 4.0f }
        ),
        TextGlyphIntersection::Invisible
    );
}

TEST_F(TextGlyphVisibilityAtlasTests, NondrawableAtlasGlyphHasNoCandidateImage){
    PlacedGlyph glyph;
    glyph.glyphId = 1u;
    Rect rectangle{ 1.0f, 2.0f, 3.0f, 4.0f };
    ASSERT_TRUE(TextGlyphVisibility::atlasRectangle(glyph, *m_atlas, 32.0f, {}, rectangle));
    ExpectRectangle(rectangle, {});
    EXPECT_EQ(
        TextGlyphVisibility::candidate(glyph, m_atlas.get(), 32.0f, 32.0f, {}, { 0.0f, 0.0f, 100.0f, 100.0f }),
        TextGlyphIntersection::Invisible
    );
}

TEST_F(TextGlyphVisibilityAtlasTests, RejectedAtlasRectanglesPreserveTheOutput){
    PlacedGlyph glyph;
    const Rect original{ 1.0f, 2.0f, 3.0f, 4.0f };
    Rect rectangle = original;
    glyph.glyphId = 2u;
    EXPECT_FALSE(TextGlyphVisibility::atlasRectangle(glyph, *m_atlas, 32.0f, {}, rectangle));
    ExpectRectangle(rectangle, original);
    glyph.glyphId = 0u;
    EXPECT_FALSE(TextGlyphVisibility::atlasRectangle(glyph, *m_atlas, 0.0f, {}, rectangle));
    ExpectRectangle(rectangle, original);
    glyph.position.x = Limit<f32>::s_Max;
    EXPECT_FALSE(TextGlyphVisibility::atlasRectangle(glyph, *m_atlas, 32.0f, { Limit<f32>::s_Max, 0.0f }, rectangle));
    ExpectRectangle(rectangle, original);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

