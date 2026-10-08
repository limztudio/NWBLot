// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/text/layout.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_layout_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;

class FixtureShaper final : public ITextShaper{
public:
    explicit FixtureShaper(Core::Alloc::GlobalArena& arena)
        : m_arena(arena)
    {}
    virtual ~FixtureShaper()override = default;


public:
    [[nodiscard]] virtual Expected<ShapedRun, TextLayoutStatus::Enum> shape(const ShapeRequest& request)override{
        if(request.text == "FAIL")
            return MakeUnexpected(TextLayoutStatus::MissingGlyph);
        ShapedRun run(m_arena);
        run.metrics = { 8.0f, 2.0f, 2.0f };
        if(request.text == "ffi")
            run.glyphs.push_back({ {}, 100u, 0u, 3u, {}, { 22.0f, 0.0f }, { -1.0f, -8.0f, 24.0f, 10.0f } });
        else if(request.text == "A\xcc\x81"){
            run.glyphs.push_back({ {}, 1u, 0u, 3u, {}, { 10.0f, 0.0f }, { -1.0f, -8.0f, 12.0f, 10.0f } });
            run.glyphs.push_back({ {}, 2u, 0u, 3u, { -4.0f, -2.0f }, {}, { 0.0f, -8.0f, 3.0f, 2.0f } });
        }
        else{
            usize begin = 0u;
            while(begin < request.text.size()){
                usize end = begin + 1u;
                while(end < request.text.size() && (static_cast<u8>(request.text[end]) & 0xc0u) == 0x80u)
                    ++end;
                run.glyphs.push_back({ {}, 1u, static_cast<u32>(begin), static_cast<u32>(end), {},
                    { 10.0f, 0.0f }, { -1.0f, -8.0f, 12.0f, 10.0f } });
                begin = end;
            }
            if(request.direction == TextDirection::RightToLeft){
                for(usize index = 0u; index < run.glyphs.size() / 2u; ++index)
                    Swap(run.glyphs[index], run.glyphs[run.glyphs.size() - index - 1u]);
            }
        }
        return run;
    }


private:
    Core::Alloc::GlobalArena& m_arena;
};

class TextLayoutTests : public testing::Test{
public:
    TextLayoutTests()
        : m_arena(Name("tests/ui/text/layout"))
        , m_shaper(m_arena)
        , m_builder(m_arena, m_shaper)
        , m_layout(m_arena)
    {}


protected:
    Core::Alloc::GlobalArena m_arena;
    FixtureShaper m_shaper;
    TextLayoutBuilder m_builder;
    TextLayout m_layout;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(TextLayoutTests, CrLfAndTrailingNewlinePreserveSourceRangesAndEmptyFinalLine){
    {
        auto layoutResult = m_builder.layout({ "A\r\nB\n" });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    ASSERT_EQ(m_layout.lines().size(), 3u);
    EXPECT_EQ(m_layout.lines()[0].byteEnd, 1u);
    EXPECT_EQ(m_layout.lines()[0].breakEnd, 3u);
    EXPECT_EQ(m_layout.lines()[1].byteBegin, 3u);
    EXPECT_EQ(m_layout.lines()[2].byteBegin, 5u);
    EXPECT_EQ(m_layout.lines()[2].glyphCount, 0u);
    EXPECT_FLOAT_EQ(m_layout.measure().y, 36.0f);
    EXPECT_EQ(m_layout.hitTest({ 20.0f, 100.0f }).byteOffset, 5u);
    Expected<Rect> caret = MakeUnexpected(Failure{});
    EXPECT_FALSE(m_layout.caretRect(2u, TextCaretEdge::Leading));
    caret = m_layout.caretRect(5u, TextCaretEdge::Leading);
    ASSERT_TRUE(caret);
    EXPECT_FLOAT_EQ(caret->y, 24.0f);
    EXPECT_FLOAT_EQ(caret->height, 12.0f);
}

TEST_F(TextLayoutTests, LigatureAndCombiningClustersHaveOnlyRealSourceEdges){
    {
        auto layoutResult = m_builder.layout({ "ffi" });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    ASSERT_EQ(m_layout.clusters().size(), 1u);
    EXPECT_EQ(m_layout.clusters()[0].byteBegin, 0u);
    EXPECT_EQ(m_layout.clusters()[0].byteEnd, 3u);
    EXPECT_EQ(m_layout.hitTest({ 11.0f, 4.0f }).byteOffset, 0u);
    EXPECT_EQ(m_layout.hitTest({ 11.01f, 4.0f }).byteOffset, 3u);
    EXPECT_FALSE(m_layout.caretRect(1u, TextCaretEdge::Leading));
    {
        auto layoutResult = m_builder.layout({ "A\xcc\x81" });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    ASSERT_EQ(m_layout.clusters().size(), 1u);
    EXPECT_EQ(m_layout.clusters()[0].glyphCount, 2u);
    EXPECT_FLOAT_EQ(m_layout.measure().x, 10.0f);
    EXPECT_FALSE(m_layout.caretRect(1u, TextCaretEdge::Leading));
    EXPECT_FALSE(m_layout.caretRect(2u, TextCaretEdge::Leading));
    EXPECT_EQ(m_layout.hitTest({ 10.0f, 4.0f }).byteOffset, 3u);
}

TEST_F(TextLayoutTests, NonBmpAndHangulClustersMapUtf8BytesRatherThanScalarIndices){
    constexpr StringView s_Text = "A\xed\x95\x9c\xf0\x9f\x98\x80";
    {
        auto layoutResult = m_builder.layout({ s_Text });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    ASSERT_EQ(m_layout.clusters().size(), 3u);
    EXPECT_EQ(m_layout.clusters()[0].byteEnd, 1u);
    EXPECT_EQ(m_layout.clusters()[1].byteBegin, 1u);
    EXPECT_EQ(m_layout.clusters()[1].byteEnd, 4u);
    EXPECT_EQ(m_layout.clusters()[2].byteBegin, 4u);
    EXPECT_EQ(m_layout.clusters()[2].byteEnd, 8u);
    Expected<Rect> caret = MakeUnexpected(Failure{});
    EXPECT_FALSE(m_layout.caretRect(2u, TextCaretEdge::Trailing));
    EXPECT_FALSE(m_layout.caretRect(6u, TextCaretEdge::Leading));
    caret = m_layout.caretRect(4u, TextCaretEdge::Leading);
    ASSERT_TRUE(caret);
    EXPECT_FLOAT_EQ(caret->x, 20.0f);
}

TEST_F(TextLayoutTests, RtlVisualOrderReversesLogicalClusterEdgesAndPreservesByteEnds){
    ShapeRequest request{ "abc" };
    request.direction = TextDirection::RightToLeft;
    {
        auto layoutResult = m_builder.layout(request);
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    ASSERT_EQ(m_layout.clusters().size(), 3u);
    EXPECT_EQ(m_layout.clusters()[0].byteBegin, 2u);
    EXPECT_EQ(m_layout.clusters()[0].byteEnd, 3u);
    EXPECT_FLOAT_EQ(m_layout.clusters()[0].leadingX, 10.0f);
    EXPECT_FLOAT_EQ(m_layout.clusters()[0].trailingX, 0.0f);
    EXPECT_EQ(m_layout.hitTest({ -1.0f, 4.0f }).byteOffset, 3u);
    EXPECT_EQ(m_layout.hitTest({ 31.0f, 4.0f }).byteOffset, 0u);
    EXPECT_EQ(m_layout.hitTest({ 5.0f, 4.0f }).byteOffset, 2u);
    Expected<Rect> caret = MakeUnexpected(Failure{});
    caret = m_layout.caretRect(0u, TextCaretEdge::Leading);
    ASSERT_TRUE(caret);
    EXPECT_FLOAT_EQ(caret->x, 30.0f);
    caret = m_layout.caretRect(3u, TextCaretEdge::Trailing);
    ASSERT_TRUE(caret);
    EXPECT_FLOAT_EQ(caret->x, 0.0f);
}

TEST_F(TextLayoutTests, FailedLaterLineAndSourceMutationPreserveCommittedLayout){
    AString<Core::Alloc::GlobalArena> source(m_arena);
    source = "AB";
    {
        auto layoutResult = m_builder.layout({ { source.data(), source.size() } });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    source = "changed";
    EXPECT_EQ(m_layout.utf8(), "AB");
    {
        const auto layoutResult = m_builder.layout({ "C\nFAIL" });
        ASSERT_FALSE(layoutResult);
        EXPECT_EQ(layoutResult.error(), TextLayoutStatus::MissingGlyph);
    }
    EXPECT_EQ(m_layout.utf8(), "AB");
    EXPECT_FLOAT_EQ(m_layout.measure().x, 20.0f);
    ASSERT_EQ(m_layout.lines().size(), 1u);
    {
        const auto layoutResult = m_builder.layout({ "A\tB" });
        ASSERT_FALSE(layoutResult);
        EXPECT_EQ(layoutResult.error(), TextLayoutStatus::UnsupportedControl);
    }
    EXPECT_EQ(m_layout.utf8(), "AB");
}

TEST_F(TextLayoutTests, EmptyTextHasOneTypographicLineAndNoInk){
    {
        auto layoutResult = m_builder.layout({});
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    EXPECT_EQ(m_layout.lines().size(), 1u);
    EXPECT_TRUE(m_layout.glyphs().empty());
    EXPECT_FLOAT_EQ(m_layout.measure().x, 0.0f);
    EXPECT_FLOAT_EQ(m_layout.measure().y, 12.0f);
    EXPECT_FLOAT_EQ(m_layout.inkBounds().width, 0.0f);
    Expected<Rect> caret = MakeUnexpected(Failure{});
    caret = m_layout.caretRect(0u, TextCaretEdge::Leading);
    ASSERT_TRUE(caret);
    EXPECT_FLOAT_EQ(caret->height, 12.0f);
}

TEST_F(TextLayoutTests, StrictUtf8AndHorizontalRequestValidationAreFailureAtomic){
    {
        auto layoutResult = m_builder.layout({ "AB" });
        ASSERT_TRUE(layoutResult);
        m_layout = Move(*layoutResult);
    }
    const StringView invalid[]{ "\xc0\x80", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xe2\x82", { "\0", 1u } };
    for(const StringView text : invalid){
        {
            const auto layoutResult = m_builder.layout({ text });
            ASSERT_FALSE(layoutResult);
            EXPECT_EQ(layoutResult.error(), TextLayoutStatus::InvalidUtf8);
        }
        EXPECT_EQ(m_layout.utf8(), "AB");
    }
    {
        const auto layoutResult = m_builder.layout({ "A\rB" });
        ASSERT_FALSE(layoutResult);
        EXPECT_EQ(layoutResult.error(), TextLayoutStatus::UnsupportedControl);
    }
    ShapeRequest request{ "AB" };
    request.fontSize = Limit<f32>::s_QuietNaN;
    {
        const auto layoutResult = m_builder.layout(request);
        ASSERT_FALSE(layoutResult);
        EXPECT_EQ(layoutResult.error(), TextLayoutStatus::InvalidParameters);
    }
    request.fontSize = 16.0f;
    request.scriptTag = 0u;
    {
        const auto layoutResult = m_builder.layout(request);
        ASSERT_FALSE(layoutResult);
        EXPECT_EQ(layoutResult.error(), TextLayoutStatus::InvalidParameters);
    }
    EXPECT_EQ(m_layout.utf8(), "AB");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

