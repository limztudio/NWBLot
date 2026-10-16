// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_area_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_area_scroll_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::UiTextAreaTests;

class UiTextAreaScrollTests : public TextAreaFixture{
protected:
    [[nodiscard]] static AStringView DenseText()noexcept{
        return "aaaaaaaaaaaaaaaaaaaa\naaaaaaaaaaaaaaaaaaaa\naaaaaaaaaaaaaaaaaaaa\n" "aaaaaaaaaaaaaaaaaaaa\naaaaaaaaaaaaaaaaaaaa\naaaaaaaaaaaaaaaaaaaa";
    }

    [[nodiscard]] static TextAreaOptions SmallOptions(){
        TextAreaOptions options;
        options.width = { LayoutSizePolicy::Fixed, 90.0f };
        options.height = { LayoutSizePolicy::Fixed, 42.0f };
        return options;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiTextAreaScrollTests, ExplicitTwoAxisScrollSurvivesTheFirstModelBinding){
    ASSERT_TRUE(m_model.setText(DenseText()));
    auto layout = m_text.layout({ .text = DenseText(), .fontSize = m_builder.style().fontSize });
    ASSERT_TRUE(layout);
    const auto visual = m_text.measure(*layout, { 800.0f, 600.0f, 1.0f, 1.0f });
    ASSERT_TRUE(visual);
    ASSERT_LT(visual->x, 0.0f);
    ASSERT_TRUE(m_state.scrollTo({ 17.0f, 19.0f }));
    const u64 revision = m_state.revision();
    ASSERT_TRUE(frameArea(1u, SmallOptions()));
    EXPECT_FLOAT_EQ(m_state.scroll().x, 17.0f);
    EXPECT_FLOAT_EQ(m_state.scroll().y, 19.0f);
    EXPECT_EQ(m_state.revision(), revision);
    EXPECT_FLOAT_EQ(m_state.placement().textOrigin.x, m_state.placement().content.x - visual->x - 17.0f);
    EXPECT_FLOAT_EQ(m_state.placement().textOrigin.y, m_state.placement().content.y - visual->y - 19.0f);
    EXPECT_GT(m_state.placement().caret.x, m_state.placement().content.x + m_state.placement().content.width);
    EXPECT_GT(m_state.placement().caret.y, m_state.placement().content.y + m_state.placement().content.height);
}

TEST_F(UiTextAreaScrollTests, IdleAndResizeClampExplicitViewportWithoutAdvancingItsIntentEpoch){
    useHost();
    ASSERT_TRUE(m_model.setText(DenseText()));
    auto layout = m_text.layout({ .text = DenseText(), .fontSize = m_builder.style().fontSize });
    ASSERT_TRUE(layout);
    const auto visual = m_text.measure(*layout, { 800.0f, 600.0f, 1.0f, 1.0f });
    ASSERT_TRUE(visual);
    ASSERT_LT(visual->x, 0.0f);
    ASSERT_TRUE(m_state.scrollTo({ 17.0f, 19.0f }));
    const u64 revision = m_state.revision();
    ASSERT_TRUE(frameArea(1u, SmallOptions()));
    ASSERT_TRUE(frameArea(2u, SmallOptions()));
    EXPECT_FLOAT_EQ(m_state.scroll().x, 17.0f);
    EXPECT_FLOAT_EQ(m_state.scroll().y, 19.0f);
    EXPECT_EQ(m_state.revision(), revision);
    TextAreaOptions larger;
    larger.width = { LayoutSizePolicy::Fixed, 500.0f };
    larger.height = { LayoutSizePolicy::Fixed, 300.0f };
    ASSERT_TRUE(frameArea(3u, larger));
    EXPECT_FLOAT_EQ(m_state.scroll().x, 0.0f);
    EXPECT_FLOAT_EQ(m_state.scroll().y, 0.0f);
    EXPECT_EQ(m_state.revision(), revision);
    EXPECT_FLOAT_EQ(m_state.placement().textOrigin.x, m_state.placement().content.x - visual->x);
    EXPECT_FLOAT_EQ(m_state.placement().textOrigin.y, m_state.placement().content.y - visual->y);
    ASSERT_EQ(m_host.publications.size(), 3u);
    EXPECT_FLOAT_EQ(m_host.publications[0u].placement.scroll, 17.0f);
    EXPECT_FLOAT_EQ(m_host.publications[0u].placement.scrollY, 19.0f);
}

TEST_F(UiTextAreaScrollTests, AutomaticRevealSurvivesIdleViewportShrinkWithoutNewModelIntent){
    useHost();
    ASSERT_TRUE(m_model.setText(DenseText()));
    TextAreaOptions larger;
    larger.width = { LayoutSizePolicy::Fixed, 500.0f };
    larger.height = { LayoutSizePolicy::Fixed, 300.0f };
    ASSERT_TRUE(frameArea(1u, larger));
    EXPECT_FLOAT_EQ(m_state.scroll().x, 0.0f);
    EXPECT_FLOAT_EQ(m_state.scroll().y, 0.0f);
    const u64 revision = m_model.revision();
    const u64 selection = m_model.selectionGeneration();
    const u64 stateRevision = m_state.revision();
    TextAreaOptions compact = SmallOptions();
    compact.height.value += m_builder.scrollbarStyle().thickness;
    ASSERT_TRUE(frameArea(2u, compact));
    EXPECT_GT(m_state.scroll().x, 0.0f);
    EXPECT_GT(m_state.scroll().y, 0.0f);
    const EditBoxPlacement& placement = m_state.placement();
    ASSERT_GE(placement.content.height, placement.caret.height);
    EXPECT_GE(placement.caret.x, placement.content.x);
    EXPECT_GE(placement.caret.y, placement.content.y);
    EXPECT_LE(placement.caret.x + placement.caret.width, placement.content.x + placement.content.width + 0.001f);
    EXPECT_LE(placement.caret.y + placement.caret.height, placement.content.y + placement.content.height + 0.001f);
    EXPECT_EQ(m_model.revision(), revision);
    EXPECT_EQ(m_model.selectionGeneration(), selection);
    EXPECT_EQ(m_state.revision(), stateRevision);
    EXPECT_FALSE(m_result.textChanged || m_result.selectionChanged);
}

TEST_F(UiTextAreaScrollTests, ExplicitOverscrollClampsToBothContentExtents){
    ASSERT_TRUE(m_model.setText(DenseText()));
    ASSERT_TRUE(m_state.scrollTo({ 10000.0f, 10000.0f }));
    const u64 revision = m_state.revision();
    ASSERT_TRUE(frameArea(1u, SmallOptions()));
    const EditBoxPlacement& placement = m_state.placement();
    EXPECT_GT(m_state.scroll().x, 0.0f);
    EXPECT_GT(m_state.scroll().y, 0.0f);
    EXPECT_LT(m_state.scroll().x, 10000.0f);
    EXPECT_LT(m_state.scroll().y, 10000.0f);
    EXPECT_NEAR(placement.caret.x + placement.caret.width, placement.content.x + placement.content.width, 0.001f);
    EXPECT_NEAR(placement.caret.y + placement.caret.height, placement.content.y + placement.content.height, 0.001f);
    EXPECT_EQ(m_state.revision(), revision);
}

TEST_F(UiTextAreaScrollTests, AcceptedIdenticalSelectionResumesRevealAfterManualScrolling){
    ASSERT_TRUE(m_model.setText(DenseText()));
    ASSERT_TRUE(frameArea(1u, SmallOptions()));
    EXPECT_GT(m_state.scroll().x, 0.0f);
    EXPECT_GT(m_state.scroll().y, 0.0f);
    ASSERT_TRUE(m_state.scrollTo({ 0.0f, 0.0f }));
    const u64 revision = m_state.revision();
    ASSERT_TRUE(frameArea(2u, SmallOptions()));
    EXPECT_FLOAT_EQ(m_state.scroll().x, 0.0f);
    EXPECT_FLOAT_EQ(m_state.scroll().y, 0.0f);
    const u64 selectionGeneration = m_model.selectionGeneration();
    ASSERT_TRUE(m_model.setSelection(m_model.anchor(), m_model.caret()));
    EXPECT_GT(m_model.selectionGeneration(), selectionGeneration);
    ASSERT_TRUE(frameArea(3u, SmallOptions()));
    EXPECT_GT(m_state.scroll().x, 0.0f);
    EXPECT_GT(m_state.scroll().y, 0.0f);
    EXPECT_EQ(m_state.revision(), revision);
}

TEST_F(UiTextAreaScrollTests, CopyAndUnchangedSubmitPreserveTheExplicitViewport){
    useHost();
    ASSERT_TRUE(m_model.setText(DenseText()));
    ASSERT_TRUE(frameArea(1u, SmallOptions()));
    ASSERT_TRUE(m_state.scrollTo({ 0.0f, 0.0f }));
    const u64 revision = m_state.revision();
    const u64 selectionGeneration = m_model.selectionGeneration();
    m_host.key(Core::Key::C, true);
    m_host.key(Core::Key::Enter, true);
    ASSERT_TRUE(frameArea(2u, SmallOptions()));
    EXPECT_TRUE(m_result.submitted);
    EXPECT_FALSE(m_result.textChanged || m_result.selectionChanged);
    EXPECT_FLOAT_EQ(m_state.scroll().x, 0.0f);
    EXPECT_FLOAT_EQ(m_state.scroll().y, 0.0f);
    EXPECT_EQ(m_state.revision(), revision);
    EXPECT_EQ(m_model.selectionGeneration(), selectionGeneration);
    EXPECT_EQ(m_model.text(), DenseText());
    ASSERT_EQ(m_host.actionTexts.size(), 1u);
    EXPECT_EQ(m_host.actionTexts.front(), DenseText());
    m_host.selection(m_model.anchor(), m_model.caret());
    ASSERT_TRUE(frameArea(3u, SmallOptions()));
    EXPECT_FALSE(m_result.selectionChanged);
    EXPECT_GT(m_model.selectionGeneration(), selectionGeneration);
    EXPECT_GT(m_state.scroll().x, 0.0f);
    EXPECT_GT(m_state.scroll().y, 0.0f);
}

TEST_F(UiTextAreaScrollTests, AcceptedTextResumesRevealAndPublicationOwnsItsBytes){
    useHost();
    ASSERT_TRUE(m_model.setText(DenseText()));
    ASSERT_TRUE(frameArea(1u, SmallOptions()));
    ASSERT_TRUE(m_state.scrollTo({ 0.0f, 0.0f }));
    ASSERT_TRUE(frameArea(2u, SmallOptions()));
    m_host.text("b");
    ASSERT_TRUE(frameArea(3u, SmallOptions()));
    EXPECT_TRUE(m_result.textChanged);
    EXPECT_GT(m_state.scroll().x, 0.0f);
    EXPECT_GT(m_state.scroll().y, 0.0f);
    AString<Core::Alloc::GlobalArena> displayed(m_arena);
    displayed.assign(DenseText().data(), DenseText().size());
    displayed.push_back('b');
    ASSERT_EQ(m_host.publications.size(), 3u);
    EXPECT_EQ(m_host.publications.back().text, displayed);
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), DenseText());
    EXPECT_EQ(m_host.publications.back().text, displayed);
    EXPECT_FALSE(m_model.canUndo());
}

TEST_F(UiTextAreaScrollTests, KnownModelRebindingResetsAndRevealsTheNewCaret){
    ASSERT_TRUE(m_model.setText(DenseText()));
    ASSERT_TRUE(m_state.scrollTo({ 17.0f, 19.0f }));
    ASSERT_TRUE(frameArea(1u, SmallOptions()));
    EditModel replacement(m_arena, {}, EditTextMode::Multiline);
    ASSERT_TRUE(replacement.setText(DenseText()));
    const u64 revision = m_state.revision();
    ASSERT_TRUE(beginArea(2u));
    ASSERT_TRUE(m_builder.textArea("area", replacement, m_state, SmallOptions()).valid);
    ASSERT_TRUE(acceptArea());
    EXPECT_GT(m_state.scroll().x, 17.0f);
    EXPECT_GT(m_state.scroll().y, 19.0f);
    EXPECT_EQ(m_state.revision(), revision);
}

TEST_F(UiTextAreaScrollTests, ResetThenScrollSeedsANewInitialBinding){
    ASSERT_TRUE(m_model.setText(DenseText()));
    ASSERT_TRUE(frameArea(1u, SmallOptions()));
    m_state.reset();
    ASSERT_TRUE(m_state.scrollTo({ 17.0f, 19.0f }));
    const u64 revision = m_state.revision();
    ASSERT_TRUE(frameArea(2u, SmallOptions()));
    EXPECT_FLOAT_EQ(m_state.scroll().x, 17.0f);
    EXPECT_FLOAT_EQ(m_state.scroll().y, 19.0f);
    EXPECT_EQ(m_state.revision(), revision);
}

TEST_F(UiTextAreaScrollTests, DocumentEdgesRevealCombiningMarksAndFallbackInkBeyondLogicalLines){
    ASSERT_TRUE(installKoreanFallback());
    useHost();
    m_builder.style().fontSize = 24.0f;
    const StringView text = "A\xCC\x81\xCC\x81\n\xED\x95\x9C\xEA\xB8\x80 \xEC\xA1\xB0\xED\x95\xA9\nMMMMMMMMMMMMMMMMMMMMMMMMg\xCC\xA3";
    ASSERT_TRUE(m_model.setText(text));
    ASSERT_TRUE(m_model.setSelection(0u, 0u));
    EditBoxView reference(m_arena);
    ASSERT_TRUE(reference.snapshot(m_model));
    ASSERT_EQ(reference.shape(m_text, { .text = {}, .fontSize = 24.0f }), TextLayoutStatus::Success);
    ASSERT_TRUE(reference.measure(m_text, { 800.0f, 600.0f, 1.0f, 1.0f }));
    const Rect visual = reference.visualBounds();
    const Point logical = reference.layout().measure();
    ASSERT_LT(visual.y, 0.0f);
    ASSERT_GT(visual.y + visual.height, logical.y);
    bool sawLatin = false;
    bool sawKorean = false;
    for(const PlacedGlyph& glyph : reference.layout().glyphs()){
        ASSERT_TRUE(glyph.face);
        sawLatin = sawLatin || glyph.face->identity().name() == Name("tests/ui/fonts/latin");
        sawKorean = sawKorean || glyph.face->identity().name() == Name("tests/ui/fonts/korean");
    }
    ASSERT_TRUE(sawLatin && sawKorean);
    TextAreaOptions options;
    options.width = { LayoutSizePolicy::Fixed, 120.0f };
    options.height = { LayoutSizePolicy::Fixed, 90.0f };
    ASSERT_TRUE(frameArea(1u, options));
    const EditBoxPlacement first = m_state.placement();
    EXPECT_FLOAT_EQ(first.scroll, 0.0f);
    EXPECT_FLOAT_EQ(first.scrollY, 0.0f);
    EXPECT_NEAR(first.textOrigin.x + visual.x, first.content.x, 0.001f);
    EXPECT_NEAR(first.textOrigin.y + visual.y, first.content.y, 0.001f);
    EXPECT_GE(first.caret.y, first.content.y);
    EXPECT_LE(first.caret.y + first.caret.height, first.content.y + first.content.height);
    m_host.key(Core::Key::End, true);
    ASSERT_TRUE(frameArea(2u, options));
    EXPECT_EQ(m_model.caret(), text.size());
    const EditBoxPlacement last = m_state.placement();
    ASSERT_GT(last.scroll, 0.0f);
    ASSERT_GT(last.scrollY, 0.0f);
    EXPECT_NEAR(last.scroll, m_state.scrollbars().horizontal.maximum, 0.001);
    EXPECT_NEAR(last.scrollY, m_state.scrollbars().vertical.maximum, 0.001);
    EXPECT_NEAR(last.textOrigin.x + visual.x + visual.width + last.caret.width, last.content.x + last.content.width, 0.001f);
    EXPECT_NEAR(last.textOrigin.y + visual.y + visual.height, last.content.y + last.content.height, 0.001f);
    const auto endCaret = reference.caretGeometry().caretRect(text.size());
    ASSERT_TRUE(endCaret);
    EXPECT_FLOAT_EQ(last.caret.x, last.textOrigin.x + endCaret->x);
    EXPECT_FLOAT_EQ(last.caret.y, last.textOrigin.y + endCaret->y);
    const auto hit = reference.hitTest({ last.caret.x, last.caret.y + last.caret.height * 0.5f }, last);
    ASSERT_TRUE(hit);
    EXPECT_EQ(*hit, text.size());
    ASSERT_EQ(m_host.publications.size(), 2u);
    ExpectRect(m_host.publications.back().placement.caret, last.caret);
    m_host.key(Core::Key::Home, true);
    ASSERT_TRUE(frameArea(3u, options));
    EXPECT_EQ(m_model.caret(), 0u);
    EXPECT_FLOAT_EQ(m_state.scroll().x, 0.0f);
    EXPECT_FLOAT_EQ(m_state.scroll().y, 0.0f);
    EXPECT_NEAR(m_state.placement().textOrigin.y + visual.y, m_state.placement().content.y, 0.001f);
    ASSERT_TRUE(m_state.scrollTo({ 10000.0f, 10000.0f }));
    ASSERT_TRUE(frameArea(4u, options));
    EXPECT_EQ(m_model.caret(), 0u);
    EXPECT_NEAR(m_state.scroll().x, last.scroll, 0.001f);
    EXPECT_NEAR(m_state.scroll().y, last.scrollY, 0.001f);
    EXPECT_NEAR(m_state.placement().textOrigin.y + visual.y + visual.height, m_state.placement().content.y + m_state.placement().content.height, 0.001f);
}

TEST_F(UiTextAreaScrollTests, SingleLineVisualMeasurementRefreshesAcrossDpiAndTextChangesWithoutMovingCaretStops){
    EditModel model(m_arena);
    EditBoxView view(m_arena);
    ASSERT_TRUE(model.setText("A\xCC\x81\xCC\x81 g\xCC\xA3"));
    ASSERT_TRUE(view.snapshot(model));
    ASSERT_EQ(view.shape(m_text, { .text = {}, .fontSize = 17.3f }), TextLayoutStatus::Success);
    const Point logical = view.layout().measure();
    ExpectRect(view.visualBounds(), { 0.0f, 0.0f, logical.x, logical.y });
    const auto logicalCaret = view.caretGeometry().caretRect(view.displayCaret());
    ASSERT_TRUE(logicalCaret);
    Rect previous;
    bool changedWithScale = false;
    for(const f32 scale : { 1.0f, 1.5f, 2.0f }){
        const DisplayMetrics display{ 800.0f, 600.0f, scale, scale };
        const auto expected = m_text.measure(view.layout(), display);
        ASSERT_TRUE(expected);
        ASSERT_TRUE(view.measure(m_text, display));
        ExpectRect(view.visualBounds(), *expected);
        EXPECT_FLOAT_EQ(view.layout().measure().x, logical.x);
        EXPECT_FLOAT_EQ(view.layout().measure().y, logical.y);
        const auto caret = view.caretGeometry().caretRect(view.displayCaret());
        ASSERT_TRUE(caret);
        ExpectRect(*caret, *logicalCaret);
        const f32 caretWidth = 1.0f / scale;
        const Rect bounds{ 20.25f, 30.75f, expected->width + caretWidth, expected->height };
        const auto placement = view.arrange(bounds, {}, { 0.0f, 0.0f, 800.0f, 600.0f }, {}, caretWidth);
        ASSERT_TRUE(placement);
        EXPECT_NEAR(placement->textOrigin.x + expected->x, bounds.x, 0.001f);
        EXPECT_NEAR(placement->textOrigin.y + expected->y, bounds.y, 0.001f);
        EXPECT_NEAR(placement->textOrigin.y + expected->y + expected->height, bounds.y + bounds.height, 0.001f);
        EXPECT_FLOAT_EQ(placement->caret.x, placement->textOrigin.x + logicalCaret->x);
        EXPECT_FLOAT_EQ(placement->caret.y, placement->textOrigin.y + logicalCaret->y);
        if(scale != 1.0f)
            changedWithScale = changedWithScale || previous.x != expected->x || previous.y != expected->y || previous.width != expected->width || previous.height != expected->height;
        previous = *expected;
    }
    EXPECT_TRUE(changedWithScale);
    EXPECT_FALSE(view.measure(m_text, { 800.0f, 600.0f, 0.0f, 1.0f }));
    ExpectRect(view.visualBounds(), previous);
    ASSERT_TRUE(model.setText("x"));
    ASSERT_TRUE(view.snapshot(model));
    EXPECT_FALSE(view.ready());
    ASSERT_EQ(view.shape(m_text, { .text = {}, .fontSize = 17.3f }), TextLayoutStatus::Success);
    const Point replacementLogical = view.layout().measure();
    ExpectRect(view.visualBounds(), { 0.0f, 0.0f, replacementLogical.x, replacementLogical.y });
    ASSERT_TRUE(view.measure(m_text, { 800.0f, 600.0f, 1.0f, 1.0f }));
    const auto replacement = m_text.measure(view.layout(), { 800.0f, 600.0f, 1.0f, 1.0f });
    ASSERT_TRUE(replacement);
    ExpectRect(view.visualBounds(), *replacement);
    EXPECT_LT(view.visualBounds().height, previous.height);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

