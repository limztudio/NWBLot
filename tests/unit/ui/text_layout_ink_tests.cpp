// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/text/layout.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_layout_ink_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;

class GeometryShaper final : public ITextShaper{
public:
    explicit GeometryShaper(Core::Alloc::GlobalArena& arena)
        : m_arena(arena)
    {}
    virtual ~GeometryShaper()override = default;


public:
    [[nodiscard]] virtual TextLayoutStatus::Enum shape(const ShapeRequest& request, ShapedRun& output)override{
        ShapedRun run(m_arena);
        run.metrics = { 8.0f, 2.0f, 2.0f };
        for(usize index = 0u; index < request.text.size(); ++index){
            run.glyphs.push_back({ {}, 1u, static_cast<u32>(index), static_cast<u32>(index + 1u),
                index == 0u ? m_offset : m_secondOffset, m_advance, m_ink, m_coverage });
        }
        output = Move(run);
        return TextLayoutStatus::Success;
    }


public:
    Point m_offset;
    Point m_secondOffset;
    Point m_advance{ 10.0f, 0.0f };
    Rect m_ink{ -2.0f, -8.0f, 9.0f, 10.0f };
    GlyphCoverageBounds m_coverage{};


private:
    Core::Alloc::GlobalArena& m_arena;
};

class TextLayoutInkTests : public testing::Test{
public:
    TextLayoutInkTests()
        : m_arena(Name("tests/ui/text/layout_ink"))
        , m_shaper(m_arena)
        , m_builder(m_arena, m_shaper)
        , m_layout(m_arena)
    {}


protected:
    Core::Alloc::GlobalArena m_arena;
    GeometryShaper m_shaper;
    TextLayoutBuilder m_builder;
    TextLayout m_layout;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(TextLayoutInkTests, PerGlyphInkKeepsBaselineRelativeOffsetsWithoutChangingCaretOrMeasurement){
    m_shaper.m_offset = { 3.0f, -1.0f };
    ASSERT_EQ(m_builder.layout({ "a\nb" }, m_layout), TextLayoutStatus::Success);
    ASSERT_EQ(m_layout.glyphs().size(), 2u);
    const PlacedGlyph& first = m_layout.glyphs()[0];
    const PlacedGlyph& second = m_layout.glyphs()[1];
    EXPECT_FLOAT_EQ(first.position.x, 3.0f);
    EXPECT_FLOAT_EQ(first.position.y, 7.0f);
    EXPECT_FLOAT_EQ(first.ink.x, -2.0f);
    EXPECT_FLOAT_EQ(first.ink.y, -8.0f);
    EXPECT_FLOAT_EQ(first.ink.width, 9.0f);
    EXPECT_FLOAT_EQ(first.ink.height, 10.0f);
    EXPECT_FLOAT_EQ(second.position.y, 19.0f);
    EXPECT_FLOAT_EQ(second.ink.y, -8.0f);
    EXPECT_FLOAT_EQ(m_layout.measure().x, 10.0f);
    EXPECT_FLOAT_EQ(m_layout.measure().y, 24.0f);
    EXPECT_FLOAT_EQ(m_layout.inkBounds().x, 1.0f);
    EXPECT_FLOAT_EQ(m_layout.inkBounds().y, -1.0f);
    EXPECT_FLOAT_EQ(m_layout.inkBounds().height, 22.0f);
    Rect caret;
    ASSERT_TRUE(m_layout.caretRect(0u, TextCaretEdge::Leading, caret));
    EXPECT_FLOAT_EQ(caret.x, 0.0f);
    EXPECT_FLOAT_EQ(caret.y, 0.0f);
    EXPECT_FLOAT_EQ(caret.height, 12.0f);
}

TEST_F(TextLayoutInkTests, EmptyShapingInkRemainsAnUnknownPerGlyphRectangle){
    m_shaper.m_ink = {};
    ASSERT_EQ(m_builder.layout({ "a" }, m_layout), TextLayoutStatus::Success);
    ASSERT_EQ(m_layout.glyphs().size(), 1u);
    EXPECT_FLOAT_EQ(m_layout.glyphs()[0].ink.width, 0.0f);
    EXPECT_FLOAT_EQ(m_layout.glyphs()[0].ink.height, 0.0f);
    EXPECT_FLOAT_EQ(m_layout.inkBounds().width, 0.0f);
    EXPECT_FLOAT_EQ(m_layout.measure().x, 10.0f);
}

TEST_F(TextLayoutInkTests, UnrepresentableRelativeInkEndpointRejectsTheShaperWithoutReplacingLayout){
    ASSERT_EQ(m_builder.layout({ "a" }, m_layout), TextLayoutStatus::Success);
    m_shaper.m_ink = { Limit<f32>::s_Max, 0.0f, Limit<f32>::s_Max, 1.0f };
    EXPECT_EQ(m_builder.layout({ "b" }, m_layout), TextLayoutStatus::FontFailure);
    EXPECT_EQ(m_layout.utf8(), "a");
    EXPECT_FLOAT_EQ(m_layout.glyphs()[0].ink.x, -2.0f);
}

TEST_F(TextLayoutInkTests, UnrepresentableTranslatedInkRejectsAtomicallyEvenWithEmptyInk){
    ASSERT_EQ(m_builder.layout({ "a" }, m_layout), TextLayoutStatus::Success);
    m_shaper.m_offset.x = Limit<f32>::s_Max;
    m_shaper.m_ink = { Limit<f32>::s_Max, 0.0f, 0.0f, 0.0f };
    EXPECT_EQ(m_builder.layout({ "b" }, m_layout), TextLayoutStatus::InvalidParameters);
    EXPECT_EQ(m_layout.utf8(), "a");
    EXPECT_FLOAT_EQ(m_layout.glyphs()[0].position.x, 0.0f);
}

TEST_F(TextLayoutInkTests, UnrepresentableUnionAcrossValidGlyphsRejectsAtomically){
    ASSERT_EQ(m_builder.layout({ "a" }, m_layout), TextLayoutStatus::Success);
    m_shaper.m_offset.x = -Limit<f32>::s_Max * 0.75f;
    m_shaper.m_secondOffset.x = Limit<f32>::s_Max * 0.75f;
    m_shaper.m_ink = { 0.0f, -8.0f, 1.0f, 10.0f };
    m_shaper.m_advance.x = 0.0f;
    EXPECT_EQ(m_builder.layout({ "bc" }, m_layout), TextLayoutStatus::InvalidParameters);
    EXPECT_EQ(m_layout.utf8(), "a");
    EXPECT_FLOAT_EQ(m_layout.inkBounds().x, -2.0f);
}

TEST_F(TextLayoutInkTests, UnrepresentablePositionOrAdvanceRejectsBeforeGlyphPublication){
    ASSERT_EQ(m_builder.layout({ "a" }, m_layout), TextLayoutStatus::Success);
    m_shaper.m_ink = {};
    m_shaper.m_advance.x = Limit<f32>::s_Max;
    m_shaper.m_secondOffset.x = Limit<f32>::s_Max;
    EXPECT_EQ(m_builder.layout({ "bc" }, m_layout), TextLayoutStatus::InvalidParameters);
    EXPECT_EQ(m_layout.utf8(), "a");
    ASSERT_EQ(m_layout.glyphs().size(), 1u);
}

TEST_F(TextLayoutInkTests, NativeCoverageBoundsAreCopiedSeparatelyFromShapedInk){
    m_shaper.m_coverage = { { -4.0f, -10.0f, 15.0f, 14.0f }, true };
    ASSERT_EQ(m_builder.layout({ "a" }, m_layout), TextLayoutStatus::Success);
    ASSERT_EQ(m_layout.glyphs().size(), 1u);
    const PlacedGlyph& glyph = m_layout.glyphs()[0];
    EXPECT_TRUE(glyph.coverage.known);
    EXPECT_FLOAT_EQ(glyph.coverage.ink.x, -4.0f);
    EXPECT_FLOAT_EQ(glyph.coverage.ink.y, -10.0f);
    EXPECT_FLOAT_EQ(glyph.coverage.ink.width, 15.0f);
    EXPECT_FLOAT_EQ(glyph.ink.x, -2.0f);
    EXPECT_FLOAT_EQ(m_layout.inkBounds().x, -2.0f);
    EXPECT_FLOAT_EQ(m_layout.inkBounds().width, 9.0f);
    m_shaper.m_coverage = {};
    EXPECT_TRUE(glyph.coverage.known);
}

TEST_F(TextLayoutInkTests, MalformedOrUnrepresentableKnownCoverageRejectsAtomically){
    ASSERT_EQ(m_builder.layout({ "a" }, m_layout), TextLayoutStatus::Success);
    m_shaper.m_coverage = { { 0.0f, 0.0f, -1.0f, 1.0f }, true };
    EXPECT_EQ(m_builder.layout({ "b" }, m_layout), TextLayoutStatus::FontFailure);
    EXPECT_EQ(m_layout.utf8(), "a");
    m_shaper.m_coverage = { { Limit<f32>::s_Max, 0.0f, 0.0f, 0.0f }, true };
    m_shaper.m_offset.x = Limit<f32>::s_Max;
    m_shaper.m_ink = {};
    EXPECT_EQ(m_builder.layout({ "b" }, m_layout), TextLayoutStatus::InvalidParameters);
    EXPECT_EQ(m_layout.utf8(), "a");
    EXPECT_FALSE(m_layout.glyphs()[0].coverage.known);
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

