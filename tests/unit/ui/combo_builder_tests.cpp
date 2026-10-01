// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "combo_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_combo_builder_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;
using namespace NWB::UiComboTests;

class UiComboBuilderTests : public ComboFixture{};

// Behavior resolves the selection first; the subsequent label lookup deliberately violates the stable-key round trip.
class LabelLookupSource final : public ComboSource{
public:
    [[nodiscard]] virtual bool indexOf(const u64 key, u64& index)const override{
        const bool found = ComboSource::indexOf(key, index);
        if(found && lookupCalls >= 4u)
            index = (index + 1u) % count;
        return found;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiComboBuilderTests, SiblingComboFieldsPaintTheirOwnDeclarationStyles){
    m_source.count = 0u;
    ComboState secondState;
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 10.0f, 10.0f, 320.0f, 240.0f }));
    m_builder.comboStyle().normal = Name("button.normal");
    ASSERT_TRUE(m_builder.comboBox("first", m_source, m_state, Options()).valid);
    m_builder.comboStyle().normal = Name("button.hover");
    ASSERT_TRUE(m_builder.comboBox("second", m_source, secondState, Options()).valid);
    ASSERT_TRUE(finishPanel());
    const DrawSnapshot snapshot = m_paint.freeze();
    Rect first;
    Rect second;
    ASSERT_TRUE(skinQuad(snapshot, 6u, first));
    ASSERT_TRUE(skinQuad(snapshot, 7u, second));
    EXPECT_LT(first.y, second.y);
    EXPECT_EQ(first.width, 220.0f);
    EXPECT_EQ(second.width, 220.0f);
}

TEST_F(UiComboBuilderTests, ClosedFieldReadsOnlySelectedTextAndOwnsOneTabStop){
    m_state.select(50000u);
    ASSERT_TRUE(accept(1u));
    EXPECT_FALSE(m_state.isOpen());
    EXPECT_FALSE(m_result.opened);
    EXPECT_FALSE(m_result.committed);
    EXPECT_EQ(m_state.selectedKey(), 50000u);
    EXPECT_EQ(m_source.textCalls, 1u);
    ASSERT_NE(target(host()), nullptr);
    EXPECT_EQ(target(popup()), nullptr);
    EXPECT_EQ(target(list()), nullptr);
    ExpectRect(m_state.bounds(), target(host())->rectangle);
    EXPECT_EQ(target(host())->control.contentGeneration, m_source.generation);
    EXPECT_EQ(target(host())->control.contentRevision, m_source.contentRevision);
    usize tabStops = 0u;
    for(const HitTarget& entry : m_context.input().targets())
        tabStops += entry.focusable ? 1u : 0u;
    EXPECT_EQ(tabStops, 1u);
}

TEST_F(UiComboBuilderTests, PointerOpensAnchoredOverlayAndVirtualizesAHundredThousandRows){
    m_state.select(1u);
    ASSERT_TRUE(accept(1u));
    m_source.resetCounters();
    ASSERT_TRUE(openByPointer(2u));
    EXPECT_TRUE(m_result.opened);
    EXPECT_FALSE(m_result.selectionChanged);
    EXPECT_FALSE(m_result.committed);
    ASSERT_NE(target(popup()), nullptr);
    ASSERT_NE(target(list()), nullptr);
    EXPECT_EQ(target(popup())->layer, 1u);
    EXPECT_EQ(target(list())->layer, 1u);
    EXPECT_EQ(m_context.input().focus(), list());
    EXPECT_EQ(target(list())->control.instanceGeneration, m_state.listState().inputGeneration());
    const Rect& field = target(host())->rectangle;
    const Rect& bounds = m_state.placement().bounds;
    EXPECT_FLOAT_EQ(bounds.x, field.x);
    EXPECT_FLOAT_EQ(bounds.width, field.width);
    EXPECT_GE(bounds.y, field.y + field.height);
    ExpectRect(target(popup())->rectangle, bounds);
    const auto& placement = m_state.listState().placement();
    const u64 visible = placement.endRow - placement.firstRow;
    EXPECT_GT(visible, 0u);
    EXPECT_LE(visible, 7u);
    EXPECT_LE(m_source.textCalls, visible + 1u);
    EXPECT_LT(m_source.keyCalls, 50u);
    EXPECT_LT(m_context.states().entries().size(), 8u);
    EXPECT_LT(m_context.input().targets().size(), 16u);
    const DrawSnapshot snapshot = m_paint.freeze();
    bool overlay = false;
    for(const DrawCommand& command : snapshot.commands()){
        if(command.layer == 1u)
            overlay = true;
        if(overlay)
            EXPECT_EQ(command.layer, 1u);
    }
    EXPECT_TRUE(overlay);
}

TEST_F(UiComboBuilderTests, TabExitsAnExplicitlyOpenedComboRelativeToItsFieldWithoutCommittingPreview){
    m_source.count = 4u;
    m_state.select(1u);
    const auto acceptNeighbors = [this](const u64 generation){
        if(!begin(generation) || !m_builder.beginPanel("panel", { 10.0f, 10.0f, 320.0f, 240.0f }))
            return false;
        const bool beforeActivated = m_builder.button("before", "Before");
        m_result = m_builder.comboBox("combo", m_source, m_state, Options());
        const bool afterActivated = m_builder.button("after", "After");
        static_cast<void>(beforeActivated);
        static_cast<void>(afterActivated);
        return m_result.valid && finishPanel() && m_context.commitFrame(generation);
    };
    m_state.open();
    ASSERT_TRUE(acceptNeighbors(1u));
    ASSERT_EQ(m_context.input().focus(), list());
    press(InputKey::Down);
    ASSERT_TRUE(acceptNeighbors(2u));
    ASSERT_EQ(m_state.listState().cursorKey(), 2u);
    press(InputKey::Tab);
    ASSERT_TRUE(acceptNeighbors(3u));
    EXPECT_TRUE(m_result.closed);
    EXPECT_FALSE(m_result.committed);
    EXPECT_EQ(m_state.selectedKey(), 1u);
    EXPECT_EQ(m_state.listState().cursorKey(), 1u);
    EXPECT_EQ(m_context.input().focus(), id("after", "panel"));
    m_state.open();
    ASSERT_TRUE(acceptNeighbors(4u));
    ASSERT_EQ(m_context.input().focus(), list());
    const InputEvent reverseDown{ .type = InputEventType::KeyDown, .position = {}, .key = InputKey::Tab, .shift = true };
    const InputEvent reverseUp{ .type = InputEventType::KeyUp, .position = {}, .key = InputKey::Tab, .shift = true };
    EXPECT_TRUE(send(reverseDown).keyboardConsumed);
    EXPECT_TRUE(send(reverseUp).keyboardConsumed);
    ASSERT_TRUE(acceptNeighbors(5u));
    EXPECT_TRUE(m_result.closed);
    EXPECT_EQ(m_state.selectedKey(), 1u);
    EXPECT_EQ(m_context.input().focus(), id("before", "panel"));
}

TEST_F(UiComboBuilderTests, TabExitsAnEmptyComboWithoutASelectionOrActivation){
    m_source.count = 0u;
    ASSERT_TRUE(accept(1u));
    ASSERT_TRUE(openByPointer(2u));
    ASSERT_EQ(m_state.selectedKey(), 0u);
    press(InputKey::Tab);
    ASSERT_TRUE(accept(3u));
    EXPECT_TRUE(m_result.closed);
    EXPECT_FALSE(m_result.committed);
    EXPECT_FALSE(m_result.selectionChanged);
    EXPECT_EQ(m_state.selectedKey(), 0u);
    EXPECT_EQ(m_state.listState().cursorKey(), 0u);
    EXPECT_EQ(m_context.input().focus(), host());
}

TEST_F(UiComboBuilderTests, OutsideDismissalConsumesTheWholePointerSequence){
    m_state.select(1u);
    ASSERT_TRUE(accept(1u));
    ASSERT_TRUE(openByPointer(2u));
    EXPECT_TRUE(send({ InputEventType::PrimaryDown, { 790.0f, 590.0f } }).pointerConsumed);
    EXPECT_TRUE(send({ InputEventType::PrimaryUp, { 790.0f, 590.0f } }).pointerConsumed);
    EXPECT_TRUE(m_context.input().actions().empty());
    ASSERT_TRUE(accept(3u));
    EXPECT_TRUE(m_result.closed);
    EXPECT_FALSE(m_result.committed);
    EXPECT_FALSE(m_result.selectionChanged);
    EXPECT_EQ(m_state.selectedKey(), 1u);
    EXPECT_FALSE(m_state.isOpen());
}

TEST_F(UiComboBuilderTests, OpeningSubmitHeldAcrossFocusTransferCannotCommitItsRepeat){
    m_state.select(1u);
    ASSERT_TRUE(accept(1u));
    press(InputKey::Tab);
    InputEvent event;
    event.type = InputEventType::KeyDown;
    event.key = InputKey::Enter;
    EXPECT_TRUE(send(event).keyboardConsumed);
    ASSERT_TRUE(accept(2u));
    ASSERT_TRUE(m_state.isOpen());
    EXPECT_EQ(m_context.input().focus(), list());
    event.repeat = true;
    EXPECT_TRUE(send(event).keyboardConsumed);
    ASSERT_TRUE(prepare(3u));
    EXPECT_TRUE(m_state.isOpen());
    EXPECT_FALSE(m_result.committed);
    EXPECT_EQ(m_state.selectedKey(), 1u);
    ASSERT_TRUE(m_context.commitFrame(3u));
    event.type = InputEventType::KeyUp;
    event.repeat = false;
    EXPECT_TRUE(send(event).keyboardConsumed);
    press(InputKey::Enter);
    ASSERT_TRUE(accept(4u));
    EXPECT_TRUE(m_result.committed);
    EXPECT_FALSE(m_result.selectionChanged);
    EXPECT_FALSE(m_state.isOpen());
}

TEST_F(UiComboBuilderTests, DisabledFieldFencesQueuedOpeningAndHasNoTabStop){
    m_state.select(1u);
    ASSERT_TRUE(accept(1u));
    click(Center(target(host())->rectangle));
    ComboOptions options = Options();
    options.enabled = false;
    ASSERT_TRUE(accept(2u, options));
    EXPECT_FALSE(m_state.isOpen());
    EXPECT_FALSE(m_result.opened);
    EXPECT_FALSE(m_result.committed);
    EXPECT_FALSE(m_result.focused);
    ASSERT_NE(target(host()), nullptr);
    EXPECT_FALSE(target(host())->enabled);
    EXPECT_FALSE(target(host())->focusable);
    EXPECT_EQ(target(list()), nullptr);
}

TEST_F(UiComboBuilderTests, EmptySourceShowsPlaceholderAndSubmitCannotCommitAKey){
    m_source.count = 0u;
    ASSERT_TRUE(accept(1u));
    EXPECT_EQ(m_source.textCalls, 0u);
    EXPECT_EQ(m_state.selectedKey(), 0u);
    ASSERT_TRUE(openByPointer(2u));
    EXPECT_EQ(m_source.textCalls, 0u);
    EXPECT_EQ(m_state.listState().cursorKey(), 0u);
    press(InputKey::Enter);
    ASSERT_TRUE(accept(3u));
    EXPECT_FALSE(m_result.committed);
    EXPECT_FALSE(m_result.selectionChanged);
    EXPECT_EQ(m_state.selectedKey(), 0u);
}

TEST_F(UiComboBuilderTests, NavigationSkipsDisabledRowsAndCannotWrapTheBoundary){
    m_source.count = 3u;
    m_source.disabled = 2u;
    m_state.select(1u);
    ASSERT_TRUE(accept(1u));
    ASSERT_TRUE(openByPointer(2u));
    press(InputKey::Down);
    ASSERT_TRUE(accept(3u));
    EXPECT_EQ(m_state.listState().cursorKey(), 3u);
    EXPECT_EQ(m_state.selectedKey(), 1u);
    press(InputKey::Down);
    ASSERT_TRUE(accept(4u));
    EXPECT_EQ(m_state.listState().cursorKey(), 3u);
    press(InputKey::Up);
    ASSERT_TRUE(accept(5u));
    EXPECT_EQ(m_state.listState().cursorKey(), 1u);
    press(InputKey::Up);
    ASSERT_TRUE(accept(6u));
    EXPECT_EQ(m_state.listState().cursorKey(), 1u);
}

TEST_F(UiComboBuilderTests, ReorderPreservesCommittedKeyAndRemovalClearsIt){
    m_state.select(50000u);
    ASSERT_TRUE(accept(1u));
    m_source.reverse = true;
    ++m_source.contentRevision;
    ASSERT_TRUE(accept(2u));
    EXPECT_EQ(m_state.selectedKey(), 50000u);
    EXPECT_FALSE(m_result.selectionChanged);
    m_source.removed = 50000u;
    --m_source.count;
    ++m_source.contentRevision;
    ASSERT_TRUE(accept(3u));
    EXPECT_EQ(m_state.selectedKey(), 0u);
    EXPECT_TRUE(m_result.selectionChanged);
    EXPECT_EQ(m_source.textCalls, 2u);
}

TEST_F(UiComboBuilderTests, SourceRevisionFencesAcceptedRowActivationBeforeTheNewLayout){
    m_state.select(1u);
    ASSERT_TRUE(accept(1u));
    ASSERT_TRUE(openByPointer(2u));
    click(Center(target(row(2u))->rectangle));
    m_source.reverse = true;
    ++m_source.contentRevision;
    ASSERT_TRUE(accept(3u));
    EXPECT_EQ(m_state.selectedKey(), 1u);
    EXPECT_FALSE(m_result.committed);
    EXPECT_TRUE(m_context.input().controlActions().empty());
}

TEST_F(UiComboBuilderTests, ReplacementSourceCannotBorrowThePreviousSelectionOrPopup){
    m_state.select(1u);
    ASSERT_TRUE(accept(1u));
    ASSERT_TRUE(openByPointer(2u));
    click(Center(target(row(2u))->rectangle));
    ComboSource replacement;
    replacement.generation = m_source.generation + 1u;
    ASSERT_TRUE(declareSource(3u, replacement, m_state));
    EXPECT_EQ(m_state.selectedKey(), 0u);
    EXPECT_FALSE(m_state.isOpen());
    EXPECT_FALSE(m_result.committed);
    EXPECT_TRUE(m_result.selectionChanged);
    ASSERT_TRUE(finishPanel());
    ASSERT_TRUE(m_context.commitFrame(3u));
    EXPECT_EQ(target(host())->control.contentGeneration, replacement.generation);
    EXPECT_EQ(target(popup()), nullptr);
}

TEST_F(UiComboBuilderTests, APreparedMoveKeepsAcceptedFieldAndPopupGeometryUntilCommit){
    m_state.select(1u);
    ASSERT_TRUE(accept(1u));
    ASSERT_TRUE(openByPointer(2u));
    const Rect acceptedField = target(host())->rectangle;
    const Rect acceptedPopup = target(popup())->rectangle;
    ASSERT_TRUE(prepare(3u, Options(), { 300.0f, 300.0f, 320.0f, 240.0f }));
    EXPECT_EQ(m_context.input().layoutGeneration(), 2u);
    ExpectRect(target(host())->rectangle, acceptedField);
    ExpectRect(target(popup())->rectangle, acceptedPopup);
    EXPECT_NE(m_state.bounds().x, acceptedField.x);
    EXPECT_FALSE(m_context.commitFrame(4u));
    ASSERT_TRUE(m_context.commitFrame(3u));
    ExpectRect(target(host())->rectangle, m_state.bounds());
    ExpectRect(target(popup())->rectangle, m_state.placement().bounds);
    EXPECT_EQ(m_context.input().layoutGeneration(), 3u);
}

TEST_F(UiComboBuilderTests, MultipleFieldsHaveIndependentStableInternalIdsAndState){
    ComboState other;
    m_state.select(1u);
    other.select(2u);
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 10.0f, 10.0f, 320.0f, 240.0f }));
    ASSERT_TRUE(m_builder.comboBox("combo", m_source, m_state, Options()).valid);
    ASSERT_TRUE(m_builder.comboBox("other", m_source, other, Options()).valid);
    ASSERT_TRUE(finishPanel());
    ASSERT_TRUE(m_context.commitFrame(1u));
    ASSERT_NE(target(host()), nullptr);
    ASSERT_NE(target(host("other")), nullptr);
    EXPECT_NE(host(), host("other"));
    EXPECT_NE(list(), list("other"));
    EXPECT_NE(popup(), popup("other"));
    click(Center(target(host("other"))->rectangle));
    ASSERT_TRUE(begin(2u));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 10.0f, 10.0f, 320.0f, 240.0f }));
    ASSERT_TRUE(m_builder.comboBox("combo", m_source, m_state, Options()).valid);
    const ComboResult result = m_builder.comboBox("other", m_source, other, Options());
    EXPECT_TRUE(result.valid);
    EXPECT_TRUE(result.opened);
    ASSERT_TRUE(finishPanel());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_FALSE(m_state.isOpen());
    EXPECT_TRUE(other.isOpen());
    EXPECT_EQ(m_state.selectedKey(), 1u);
    EXPECT_EQ(other.selectedKey(), 2u);
    EXPECT_EQ(target(popup()), nullptr);
    EXPECT_NE(target(popup("other")), nullptr);
    EXPECT_EQ(m_context.input().focus(), list("other"));
}

TEST_F(UiComboBuilderTests, NativeFocusLossCancelsPreviewAndDoesNotRestoreFocusOnCommit){
    m_state.select(1u);
    ASSERT_TRUE(accept(1u));
    ASSERT_TRUE(openByPointer(2u));
    press(InputKey::Down);
    ASSERT_TRUE(accept(3u));
    InputEvent focusLost;
    focusLost.type = InputEventType::FocusLost;
    EXPECT_FALSE(send(focusLost).focus.valid());
    ASSERT_TRUE(accept(4u));
    EXPECT_FALSE(m_state.isOpen());
    EXPECT_FALSE(m_result.committed);
    EXPECT_EQ(m_state.selectedKey(), 1u);
    EXPECT_FALSE(m_context.input().focus().valid());
}

TEST_F(UiComboBuilderTests, FullyClippedFieldCannotPublishItsPopupAboveTheParent){
    m_state.select(1u);
    m_state.open();
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 10.0f, 10.0f, 320.0f, 40.0f }));
    WidgetOptions filler;
    filler.height = { LayoutSizePolicy::Fixed, 100.0f };
    ASSERT_TRUE(m_builder.label("filler", "Clipped combo below", filler));
    ASSERT_TRUE(m_builder.comboBox("combo", m_source, m_state, Options()).valid);
    ASSERT_TRUE(finishPanel());
    ASSERT_TRUE(m_context.commitFrame(1u));
    ASSERT_NE(target(host()), nullptr);
    EXPECT_GE(target(host())->rectangle.y, target(host())->clip.y + target(host())->clip.height);
    EXPECT_FLOAT_EQ(target(host())->clip.height, 0.0f);
    EXPECT_EQ(target(popup()), nullptr);
    EXPECT_EQ(target(list()), nullptr);
    EXPECT_FALSE(m_context.input().hasPopup());
    EXPECT_FALSE(m_state.isOpen());
    EXPECT_EQ(m_state.selectedKey(), 1u);
}

TEST_F(UiComboBuilderTests, SharingOneStateAcrossSequentialPanelsRejectsTheSecondDeclaration){
    m_state.select(1u);
    ASSERT_TRUE(declareCombo(1u));
    ASSERT_TRUE(m_builder.endPanel());
    const u64 inputGeneration = m_state.inputGeneration();
    m_source.resetCounters();
    ASSERT_TRUE(m_builder.beginPanel("later", { 400.0f, 10.0f, 300.0f, 200.0f }));
    const ComboResult result = m_builder.comboBox("other", m_source, m_state, Options());
    EXPECT_FALSE(result.valid);
    EXPECT_TRUE(m_context.failed());
    EXPECT_EQ(m_state.inputGeneration(), inputGeneration);
    EXPECT_EQ(m_source.textCalls, 0u);
    EXPECT_EQ(m_source.keyCalls, 0u);
    EXPECT_FALSE(m_context.commitFrame(1u));
}

TEST_F(UiComboBuilderTests, SharingOneStateAcrossRootsRejectsBeforeLendingTheSecondSource){
    m_state.select(1u);
    ASSERT_TRUE(declareCombo(1u));
    ASSERT_TRUE(m_builder.endPanel());
    ASSERT_TRUE(m_context.endRoot());
    const WidgetRoot later{ 87u, 2u };
    ASSERT_TRUE(m_context.beginRoot(later));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 400.0f, 10.0f, 300.0f, 200.0f }));
    ComboSource replacement;
    replacement.generation = 703u;
    const ComboResult result = m_builder.comboBox("combo", replacement, m_state, Options());
    EXPECT_FALSE(result.valid);
    EXPECT_TRUE(m_context.failed());
    EXPECT_EQ(m_state.selectedKey(), 1u);
    EXPECT_EQ(replacement.textCalls, 0u);
    EXPECT_EQ(replacement.keyCalls, 0u);
    EXPECT_FALSE(m_context.commitFrame(1u));
}

TEST_F(UiComboBuilderTests, OmissionRetiresThePopupAndHeldNavigationCannotOpenItsReplacement){
    m_state.select(1u);
    ASSERT_TRUE(accept(1u));
    ASSERT_TRUE(openByPointer(2u));
    const u64 oldDeclaration = target(host())->declarationGeneration;
    InputEvent held;
    held.type = InputEventType::KeyDown;
    held.key = InputKey::Down;
    EXPECT_TRUE(send(held).keyboardConsumed);
    ASSERT_TRUE(begin(3u));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 10.0f, 10.0f, 320.0f, 240.0f }));
    ASSERT_TRUE(m_builder.label("label", "Combo hidden"));
    ASSERT_TRUE(finishPanel());
    ASSERT_TRUE(m_context.commitFrame(3u));
    EXPECT_FALSE(m_context.input().hasPopup());
    EXPECT_EQ(target(host()), nullptr);
    EXPECT_EQ(target(list()), nullptr);
    EXPECT_TRUE(m_context.input().controlActions().empty());
    ASSERT_TRUE(accept(4u));
    EXPECT_FALSE(m_state.isOpen());
    EXPECT_TRUE(m_result.closed);
    EXPECT_EQ(m_state.selectedKey(), 1u);
    EXPECT_EQ(m_state.listState().cursorKey(), 1u);
    ASSERT_NE(target(host()), nullptr);
    EXPECT_NE(target(host())->declarationGeneration, oldDeclaration);
    press(InputKey::Tab);
    EXPECT_EQ(m_context.input().focus(), host());
    held.repeat = true;
    EXPECT_TRUE(send(held).keyboardConsumed);
    ASSERT_TRUE(accept(5u));
    EXPECT_FALSE(m_state.isOpen());
    EXPECT_FALSE(m_result.opened);
    held.type = InputEventType::KeyUp;
    held.repeat = false;
    EXPECT_TRUE(send(held).keyboardConsumed);
    press(InputKey::Down);
    ASSERT_TRUE(accept(6u));
    EXPECT_TRUE(m_state.isOpen());
    EXPECT_TRUE(m_result.opened);
    EXPECT_EQ(m_state.selectedKey(), 1u);
    EXPECT_EQ(m_state.listState().cursorKey(), 2u);
}

TEST_F(UiComboBuilderTests, ReplacementStateDiscardsAcceptedRowsAndItsQueuedActivation){
    m_state.select(1u);
    ASSERT_TRUE(accept(1u));
    ASSERT_TRUE(openByPointer(2u));
    click(Center(target(row(2u))->rectangle));
    ComboState replacement;
    ASSERT_TRUE(declareSource(3u, m_source, replacement));
    EXPECT_TRUE(m_result.valid);
    EXPECT_FALSE(m_result.committed);
    EXPECT_EQ(replacement.selectedKey(), 0u);
    EXPECT_FALSE(replacement.isOpen());
    EXPECT_EQ(m_state.selectedKey(), 1u);
    EXPECT_TRUE(m_state.isOpen());
    ASSERT_TRUE(finishPanel());
    ASSERT_TRUE(m_context.commitFrame(3u));
    EXPECT_EQ(target(popup()), nullptr);
    EXPECT_TRUE(m_context.input().controlActions().empty());
}

TEST_F(UiComboBuilderTests, SelectedLabelLookupMustRoundTripItsStableKeyBeforeReadingText){
    m_state.select(1u);
    LabelLookupSource source;
    EXPECT_FALSE(declareSource(1u, source, m_state));
    EXPECT_FALSE(m_result.valid);
    EXPECT_TRUE(m_context.failed());
    EXPECT_EQ(m_state.selectedKey(), 1u);
    EXPECT_EQ(source.textCalls, 0u);
    EXPECT_FALSE(m_context.commitFrame(1u));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

