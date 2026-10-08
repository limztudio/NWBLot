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
    [[nodiscard]] virtual Expected<ShapedRun, TextLayoutStatus::Enum> shape(const ShapeRequest& request)override{
        ShapedRun run(m_arena);
        run.metrics = { 8.0f, 2.0f, 2.0f };
        for(usize index = 0u; index < request.text.size(); ++index){
            run.glyphs.push_back({ {}, 1u, static_cast<u32>(index), static_cast<u32>(index + 1u),
                index == 0u ? m_offset : m_secondOffset, m_advance, m_ink, m_coverage });
        }
        return run;
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


TEST_F(TextLayoutInkTests, EmptyShapingInkRemainsAnUnknownPerGlyphRectangle){
    m_shaper.m_ink = {};
    {
        auto layoutResult = m_builder.layout({ "a" });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    ASSERT_EQ(m_layout.glyphs().size(), 1u);
    EXPECT_FLOAT_EQ(m_layout.glyphs()[0].ink.width, 0.0f);
    EXPECT_FLOAT_EQ(m_layout.glyphs()[0].ink.height, 0.0f);
    EXPECT_FLOAT_EQ(m_layout.inkBounds().width, 0.0f);
    EXPECT_FLOAT_EQ(m_layout.measure().x, 10.0f);
}

TEST_F(TextLayoutInkTests, UnrepresentableRelativeInkEndpointRejectsTheShaperWithoutReplacingLayout){
    {
        auto layoutResult = m_builder.layout({ "a" });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    m_shaper.m_ink = { Limit<f32>::s_Max, 0.0f, Limit<f32>::s_Max, 1.0f };
    {
        const auto layoutResult = m_builder.layout({ "b" });
        ASSERT_FALSE(layoutResult);
        EXPECT_EQ(layoutResult.error(), TextLayoutStatus::FontFailure);
    }
    EXPECT_EQ(m_layout.utf8(), "a");
    EXPECT_FLOAT_EQ(m_layout.glyphs()[0].ink.x, -2.0f);
}

TEST_F(TextLayoutInkTests, UnrepresentableTranslatedInkRejectsAtomicallyEvenWithEmptyInk){
    {
        auto layoutResult = m_builder.layout({ "a" });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    m_shaper.m_offset.x = Limit<f32>::s_Max;
    m_shaper.m_ink = { Limit<f32>::s_Max, 0.0f, 0.0f, 0.0f };
    {
        const auto layoutResult = m_builder.layout({ "b" });
        ASSERT_FALSE(layoutResult);
        EXPECT_EQ(layoutResult.error(), TextLayoutStatus::InvalidParameters);
    }
    EXPECT_EQ(m_layout.utf8(), "a");
    EXPECT_FLOAT_EQ(m_layout.glyphs()[0].position.x, 0.0f);
}

TEST_F(TextLayoutInkTests, UnrepresentableUnionAcrossValidGlyphsRejectsAtomically){
    {
        auto layoutResult = m_builder.layout({ "a" });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    m_shaper.m_offset.x = -Limit<f32>::s_Max * 0.75f;
    m_shaper.m_secondOffset.x = Limit<f32>::s_Max * 0.75f;
    m_shaper.m_ink = { 0.0f, -8.0f, 1.0f, 10.0f };
    m_shaper.m_advance.x = 0.0f;
    {
        const auto layoutResult = m_builder.layout({ "bc" });
        ASSERT_FALSE(layoutResult);
        EXPECT_EQ(layoutResult.error(), TextLayoutStatus::InvalidParameters);
    }
    EXPECT_EQ(m_layout.utf8(), "a");
    EXPECT_FLOAT_EQ(m_layout.inkBounds().x, -2.0f);
}

TEST_F(TextLayoutInkTests, UnrepresentablePositionOrAdvanceRejectsBeforeGlyphPublication){
    {
        auto layoutResult = m_builder.layout({ "a" });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    m_shaper.m_ink = {};
    m_shaper.m_advance.x = Limit<f32>::s_Max;
    m_shaper.m_secondOffset.x = Limit<f32>::s_Max;
    {
        const auto layoutResult = m_builder.layout({ "bc" });
        ASSERT_FALSE(layoutResult);
        EXPECT_EQ(layoutResult.error(), TextLayoutStatus::InvalidParameters);
    }
    EXPECT_EQ(m_layout.utf8(), "a");
    ASSERT_EQ(m_layout.glyphs().size(), 1u);
}

TEST_F(TextLayoutInkTests, NativeCoverageBoundsAreCopiedSeparatelyFromShapedInk){
    m_shaper.m_coverage = { { -4.0f, -10.0f, 15.0f, 14.0f }, true };
    {
        auto layoutResult = m_builder.layout({ "a" });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    ASSERT_EQ(m_layout.glyphs().size(), 1u);
    const PlacedGlyph& glyph = m_layout.glyphs()[0];
    m_shaper.m_coverage = {};
    EXPECT_TRUE(glyph.coverage.known);
    EXPECT_FLOAT_EQ(glyph.coverage.ink.x, -4.0f);
    EXPECT_FLOAT_EQ(glyph.coverage.ink.y, -10.0f);
    EXPECT_FLOAT_EQ(glyph.coverage.ink.width, 15.0f);
}

TEST_F(TextLayoutInkTests, MalformedOrUnrepresentableKnownCoverageRejectsAtomically){
    {
        auto layoutResult = m_builder.layout({ "a" });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    m_shaper.m_coverage = { { 0.0f, 0.0f, -1.0f, 1.0f }, true };
    {
        const auto layoutResult = m_builder.layout({ "b" });
        ASSERT_FALSE(layoutResult);
        EXPECT_EQ(layoutResult.error(), TextLayoutStatus::FontFailure);
    }
    EXPECT_EQ(m_layout.utf8(), "a");
    m_shaper.m_coverage = { { Limit<f32>::s_Max, 0.0f, 0.0f, 0.0f }, true };
    m_shaper.m_offset.x = Limit<f32>::s_Max;
    m_shaper.m_ink = {};
    {
        const auto layoutResult = m_builder.layout({ "b" });
        ASSERT_FALSE(layoutResult);
        EXPECT_EQ(layoutResult.error(), TextLayoutStatus::InvalidParameters);
    }
    EXPECT_EQ(m_layout.utf8(), "a");
    EXPECT_FALSE(m_layout.glyphs()[0].coverage.known);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

