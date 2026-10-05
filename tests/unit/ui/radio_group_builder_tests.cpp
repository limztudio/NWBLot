// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "radio_group_fixture.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_radio_group_builder_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;
using namespace NWB::UiRadioGroupTests;

class UiRadioGroupBuilderTests : public RadioGroupFixture{};


static void CopyGlyphVertices(const DrawSnapshot& snapshot, Vector<Vertex, Core::Alloc::ScratchArena>& vertices){
    vertices.reserve(snapshot.indices().size());
    for(const DrawCommand& command : snapshot.commands()){
        if(command.material != PaintMaterial::Glyph)
            continue;
        for(u32 index = command.firstIndex; index < command.firstIndex + command.indexCount; ++index)
            vertices.push_back(snapshot.vertices()[snapshot.indices()[index]]);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiRadioGroupBuilderTests, TemporaryLabelsAreOwnedBeforeTheNextSourceCallback){
    ASSERT_TRUE(accept(1u));
    const DrawSnapshot actual = m_paint.freeze();
    PaintBuilder reference(m_arena);
    reference.begin({ 800.0f, 600.0f, 1.0f, 1.0f }, 2u, 1u, Core::Assets::AssetRef<Impl::UiSkin>("tests/ui/skin"), m_skin);
    for(u32 index = 0u; index < 5u; ++index){
        const Array<char, 4u> bytes{ static_cast<char>('A' + index), 'b', 'c', 'd' };
        TextLayout layout(m_arena);
        ASSERT_EQ(m_text.layout({ { bytes.data(), bytes.size() }, 14.0f }, layout), TextLayoutStatus::Success);
        const RadioGroupChoicePlacement& row = m_state.placement().rows[index];
        const Point origin{ row.indicator.x + row.indicator.width + m_builder.radioGroupStyle().gap,
            row.rectangle.y + Max(0.0f, (row.rectangle.height - layout.measure().y) * 0.5f) };
        reference.pushClip(row.clip);
        reference.pushClip(row.textClip);
        const Color& color = row.enabled ? m_builder.style().text : m_builder.style().disabledText;
        ASSERT_TRUE(m_text.paint(reference, layout, origin, color));
        ASSERT_TRUE(reference.popClip());
        ASSERT_TRUE(reference.popClip());
    }
    const DrawSnapshot expected = reference.freeze();
    Core::Alloc::ScratchArena scratch(Name("tests/ui/radio/temporary_labels"));
    Vector<Vertex, Core::Alloc::ScratchArena> actualGlyphs(scratch);
    Vector<Vertex, Core::Alloc::ScratchArena> expectedGlyphs(scratch);
    CopyGlyphVertices(actual, actualGlyphs);
    CopyGlyphVertices(expected, expectedGlyphs);
    ASSERT_EQ(actualGlyphs.size(), expectedGlyphs.size());
    ASSERT_GT(actualGlyphs.size(), 0u);
    for(usize index = 0u; index < actualGlyphs.size(); ++index){
        EXPECT_FLOAT_EQ(actualGlyphs[index].position.x, expectedGlyphs[index].position.x);
        EXPECT_FLOAT_EQ(actualGlyphs[index].position.y, expectedGlyphs[index].position.y);
        EXPECT_FLOAT_EQ(actualGlyphs[index].texCoord.x, expectedGlyphs[index].texCoord.x);
        EXPECT_FLOAT_EQ(actualGlyphs[index].texCoord.y, expectedGlyphs[index].texCoord.y);
    }
}

TEST_F(UiRadioGroupBuilderTests, SelectionCommitsOnReleaseAndNotOnPress){
    ASSERT_TRUE(accept(1u));
    const Point point = choicePoint(20u);
    EXPECT_TRUE(send({ InputEventType::PrimaryDown, point }).pointerConsumed);
    ASSERT_TRUE(accept(2u));
    EXPECT_EQ(m_state.selectedKey(), 10u);
    EXPECT_FALSE(m_result.selectionChanged);
    EXPECT_FALSE(m_result.activated);
    EXPECT_TRUE(send({ InputEventType::PrimaryUp, point }).pointerConsumed);
    ASSERT_TRUE(accept(3u));
    EXPECT_EQ(m_state.selectedKey(), 20u);
    EXPECT_TRUE(m_result.selectionChanged);
    EXPECT_TRUE(m_result.activated);
    click(choicePoint(20u));
    ASSERT_TRUE(accept(4u));
    EXPECT_FALSE(m_result.selectionChanged);
    EXPECT_TRUE(m_result.activated);
}

TEST_F(UiRadioGroupBuilderTests, ReleaseOutsideAndDisabledChoiceCannotActivate){
    ASSERT_TRUE(accept(1u));
    drag(choicePoint(20u), { 700.0f, 500.0f });
    ASSERT_TRUE(accept(2u));
    EXPECT_EQ(m_state.selectedKey(), 10u);
    EXPECT_FALSE(m_result.activated);
    click(choicePoint(30u));
    ASSERT_TRUE(accept(3u));
    EXPECT_EQ(m_state.selectedKey(), 10u);
    EXPECT_FALSE(m_result.selectionChanged);
    EXPECT_FALSE(m_result.activated);
}

TEST_F(UiRadioGroupBuilderTests, HorizontalAndVerticalKeysShareTheHostAndSkipDisabledChoices){
    ASSERT_TRUE(accept(1u));
    press(Core::Key::Tab);
    const u64 token = m_state.inputGeneration();
    press(Core::Key::Right);
    press(Core::Key::Down);
    ASSERT_TRUE(accept(2u));
    EXPECT_EQ(m_state.selectedKey(), 40u);
    EXPECT_TRUE(m_result.selectionChanged);
    EXPECT_FALSE(m_result.activated);
    EXPECT_EQ(m_state.inputGeneration(), token);
    press(Core::Key::End);
    press(Core::Key::Right);
    ASSERT_TRUE(accept(3u));
    EXPECT_EQ(m_state.selectedKey(), 10u);
    press(Core::Key::Left);
    ASSERT_TRUE(accept(4u));
    EXPECT_EQ(m_state.selectedKey(), 50u);
    press(Core::Key::Home);
    press(Core::Key::Up);
    ASSERT_TRUE(accept(5u));
    EXPECT_EQ(m_state.selectedKey(), 50u);
}

TEST_F(UiRadioGroupBuilderTests, HeldSubmitActivatesOnceWhileAnUnchangedChoiceStaysSelected){
    ASSERT_TRUE(accept(1u));
    press(Core::Key::Tab);
    InputEvent event;
    event.type = InputEventType::KeyDown;
    event.key = Core::Key::Enter;
    EXPECT_TRUE(send(event).keyboardConsumed);
    ASSERT_TRUE(accept(2u));
    EXPECT_TRUE(m_result.activated);
    EXPECT_FALSE(m_result.selectionChanged);
    event.repeat = true;
    EXPECT_TRUE(send(event).keyboardConsumed);
    ASSERT_TRUE(accept(3u));
    EXPECT_FALSE(m_result.activated);
    event.type = InputEventType::KeyUp;
    EXPECT_TRUE(send(event).keyboardConsumed);
    press(Core::Key::Space);
    ASSERT_TRUE(accept(4u));
    EXPECT_TRUE(m_result.activated);
    EXPECT_FALSE(m_result.selectionChanged);
}

TEST_F(UiRadioGroupBuilderTests, SelectedDisabledChoiceKeepsItsCheckedFrameAndMark){
    m_state.select(30u);
    ASSERT_TRUE(accept(1u));
    EXPECT_EQ(m_state.selectedKey(), 30u);
    EXPECT_EQ(m_state.cursorKey(), 40u);
    const WidgetId part = MakeWidgetPartId(MakeWidgetId(host(), "choices"), 30u);
    ASSERT_NE(target(part), nullptr);
    EXPECT_FALSE(target(part)->enabled);
    const DrawSnapshot snapshot = m_paint.freeze();
    EXPECT_EQ(regionQuads(snapshot, Name("radio.checked")), 1u);
    EXPECT_EQ(regionQuads(snapshot, Name("radio.mark")), 1u);
    EXPECT_EQ(regionQuads(snapshot, Name("radio.normal")), 4u);
}

TEST_F(UiRadioGroupBuilderTests, WholeDisabledGroupKeepsThePanelBarrierAndRetiresQueuedNavigation){
    ASSERT_TRUE(accept(1u));
    press(Core::Key::Tab);
    press(Core::Key::Right);
    RadioGroupOptions options;
    options.enabled = false;
    ASSERT_TRUE(accept(2u, options));
    EXPECT_EQ(m_state.selectedKey(), 10u);
    EXPECT_FALSE(m_result.activated);
    ASSERT_NE(target(host()), nullptr);
    EXPECT_FALSE(target(host())->enabled);
    EXPECT_FALSE(target(host())->focusable);
    EXPECT_EQ(target(MakeWidgetPartId(MakeWidgetId(host(), "choices"), 10u)), nullptr);
    const Point point = choicePoint(10u);
    EXPECT_EQ(m_context.input().hitTest(point), MakeWidgetId(MakeRootId(m_root), "panel"));
    EXPECT_TRUE(send({ InputEventType::PrimaryDown, point }).pointerConsumed);
    EXPECT_TRUE(send({ InputEventType::PrimaryUp, point }).pointerConsumed);
    ASSERT_TRUE(accept(3u, options));
    EXPECT_EQ(m_state.selectedKey(), 10u);
    EXPECT_FALSE(m_result.selectionChanged);
}

TEST_F(UiRadioGroupBuilderTests, EmptyAndAllDisabledSourcesAreValidWithoutATabStop){
    m_source.m_count = 0u;
    ASSERT_TRUE(accept(1u));
    EXPECT_EQ(m_state.placement().count, 0u);
    EXPECT_EQ(m_state.selectedKey(), 0u);
    ASSERT_NE(target(host()), nullptr);
    EXPECT_FALSE(target(host())->focusable);
    m_source.m_count = 5u;
    m_source.m_allDisabled = true;
    ++m_source.m_contentRevision;
    m_state.select(30u);
    ASSERT_TRUE(accept(2u));
    EXPECT_EQ(m_state.selectedKey(), 30u);
    EXPECT_EQ(m_state.cursorKey(), 0u);
    EXPECT_FALSE(target(host())->enabled);
    EXPECT_FALSE(m_result.activated);
}

TEST_F(UiRadioGroupBuilderTests, SixtyFourChoicesSucceedAndTheSixtyFifthRejectsAtomically){
    m_source.m_count = 64u;
    ASSERT_TRUE(accept(1u));
    EXPECT_EQ(m_state.placement().count, 64u);
    const RadioAcceptedFrame displayed = accepted();
    const RadioGroupSnapshot saved = m_state.snapshot();
    m_source.resetCounters();
    m_source.m_count = 65u;
    ++m_source.m_contentRevision;
    EXPECT_FALSE(declare(2u));
    EXPECT_TRUE(m_context.failed());
    EXPECT_TRUE(m_state.matches(saved));
    EXPECT_EQ(m_source.m_textCalls, 0u);
    EXPECT_FALSE(m_context.commitFrame(2u));
    expectAccepted(displayed);
}

TEST_F(UiRadioGroupBuilderTests, DuplicateStableKeysRejectBeforeAnyLabelBorrow){
    ASSERT_TRUE(accept(1u));
    const RadioAcceptedFrame displayed = accepted();
    const RadioGroupSnapshot saved = m_state.snapshot();
    m_source.resetCounters();
    m_source.m_duplicate = true;
    ++m_source.m_contentRevision;
    EXPECT_FALSE(declare(2u));
    EXPECT_TRUE(m_context.failed());
    EXPECT_TRUE(m_state.matches(saved));
    EXPECT_EQ(m_source.m_textCalls, 0u);
    expectAccepted(displayed);
}

TEST_F(UiRadioGroupBuilderTests, SourceRevisionFencesQueuedKeysAndReorderKeepsStableSelection){
    ASSERT_TRUE(accept(1u));
    press(Core::Key::Tab);
    press(Core::Key::Right);
    m_source.m_reverse = true;
    ++m_source.m_contentRevision;
    ASSERT_TRUE(accept(2u));
    EXPECT_EQ(m_state.selectedKey(), 10u);
    EXPECT_FALSE(m_result.selectionChanged);
    EXPECT_EQ(m_state.placement().rows[4u].key, 10u);
    press(Core::Key::Right);
    ASSERT_TRUE(accept(3u));
    EXPECT_EQ(m_state.selectedKey(), 50u);
    EXPECT_TRUE(m_result.selectionChanged);
}

TEST_F(UiRadioGroupBuilderTests, RemovalAndKnownSourceReplacementClearTheCommittedSelection){
    m_state.select(20u);
    ASSERT_TRUE(accept(1u));
    m_source.m_removed = 20u;
    ++m_source.m_contentRevision;
    ASSERT_TRUE(accept(2u));
    EXPECT_EQ(m_state.selectedKey(), 0u);
    EXPECT_TRUE(m_result.selectionChanged);
    m_state.select(40u);
    ++m_source.m_generation;
    ++m_source.m_contentRevision;
    m_source.m_removed = 0u;
    ASSERT_TRUE(accept(3u));
    EXPECT_EQ(m_state.selectedKey(), 0u);
    EXPECT_TRUE(m_result.selectionChanged);
}

TEST_F(UiRadioGroupBuilderTests, PreparedGeometryDoesNotReplaceAcceptedChoiceTargetsUntilCommit){
    ASSERT_TRUE(accept(1u));
    const Rect oldBounds = target(host())->rectangle;
    ASSERT_TRUE(prepare(2u, {}, { 80.0f, 40.0f, 420.0f, 360.0f }));
    ExpectRadioRect(target(host())->rectangle, oldBounds);
    EXPECT_NE(m_state.placement().bounds.x, oldBounds.x);
    ASSERT_TRUE(m_context.commitFrame(2u));
    ExpectRadioRect(target(host())->rectangle, m_state.placement().bounds);
}

TEST_F(UiRadioGroupBuilderTests, DeclarationFreezesStyleMetricsBeforeTheOwningPanelEnds){
    ASSERT_TRUE(declare(1u));
    m_builder.radioGroupStyle().indicatorExtent = 40.0f;
    m_builder.style().fontSize = 28.0f;
    ASSERT_TRUE(finishPanel());
    ASSERT_TRUE(m_context.commitFrame(1u));
    ASSERT_NE(choice(10u, m_state), nullptr);
    EXPECT_FLOAT_EQ(choice(10u, m_state)->indicator.width, 24.0f);
    EXPECT_FLOAT_EQ(choice(10u, m_state)->rectangle.height, 32.0f);
}

TEST_F(UiRadioGroupBuilderTests, InvalidRowHeightRejectsBeforeSourceCallbacksOrStateMutation){
    const RadioGroupSnapshot saved = m_state.snapshot();
    RadioGroupOptions options;
    options.rowHeight = 31.0f;
    EXPECT_FALSE(declare(1u, options));
    EXPECT_TRUE(m_context.failed());
    EXPECT_TRUE(m_state.matches(saved));
    EXPECT_EQ(m_source.m_textCalls, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

