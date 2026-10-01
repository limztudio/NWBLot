// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "widget_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_list_builder_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using namespace NWB::Impl::Ui;
using namespace NWB::UiWidgetTests;


// This source represents a large dataset arithmetically; no allocation or full scan depends on rowCount().
class ArithmeticSource final : public IListDataSource{
public:
    virtual ~ArithmeticSource()override = default;


public:
    [[nodiscard]] virtual u64 instanceGeneration()const override{ invalidateText(); return generation; }
    [[nodiscard]] virtual u64 revision()const override{ invalidateText(); return contentRevision; }
    [[nodiscard]] virtual u64 rowCount()const override{ invalidateText(); return count; }

    [[nodiscard]] virtual u64 key(const u64 index)const override{
        invalidateText();
        ++keyCalls;
        if(duplicate)
            return 1u;
        return rawKey(index);
    }

    [[nodiscard]] virtual bool indexOf(const u64 value, u64& index)const override{
        invalidateText();
        ++lookupCalls;
        if(malformedLookup){
            index = count;
            return true;
        }
        if(value == 0u || value == removed || value > count + (removed != 0u ? 1u : 0u))
            return false;
        const u64 forward = value - 1u - (removed != 0u && value > removed ? 1u : 0u);
        index = reverse ? count - 1u - forward : forward;
        return index < count;
    }

    [[nodiscard]] virtual bool findEnabled(const u64 start, const bool backwards, u64& index)const override{
        invalidateText();
        ++searchCalls;
        if(start >= count)
            return false;
        index = start;
        if(rawKey(index) != disabled)
            return true;
        if(backwards){
            if(index == 0u)
                return false;
            --index;
        }
        else{
            if(index + 1u == count)
                return false;
            ++index;
        }
        return true;
    }

    [[nodiscard]] virtual StringView text(const u64 index)const override{
        ++textCalls;
        temporary[0u] = static_cast<char>('A' + index % 26u);
        temporary[1u] = 'x';
        return { temporary.data(), 2u };
    }

    [[nodiscard]] virtual bool enabled(const u64 index)const override{
        invalidateText();
        ++enabledCalls;
        return rawKey(index) != disabled;
    }

    void resetCounters()const{
        keyCalls = 0u;
        lookupCalls = 0u;
        searchCalls = 0u;
        textCalls = 0u;
        enabledCalls = 0u;
    }


private:
    [[nodiscard]] u64 rawKey(const u64 index)const{
        const u64 forward = reverse ? count - 1u - index : index;
        const u64 value = forward + 1u;
        return value + (removed != 0u && value >= removed ? 1u : 0u);
    }

    void invalidateText()const{
        temporary.fill('?');
    }


public:
    u64 count = 100000u;
    u64 generation = 301u;
    u64 contentRevision = 1u;
    u64 removed = 0u;
    u64 disabled = 0u;
    bool reverse = false;
    bool duplicate = false;
    bool malformedLookup = false;
    mutable u64 keyCalls = 0u;
    mutable u64 lookupCalls = 0u;
    mutable u64 searchCalls = 0u;
    mutable u64 textCalls = 0u;
    mutable u64 enabledCalls = 0u;
    mutable Array<char, 2u> temporary{};
};

[[nodiscard]] static ListOptions Options(){
    ListOptions options;
    options.width = { LayoutSizePolicy::Fixed, 280.0f };
    options.height = { LayoutSizePolicy::Fixed, 120.0f };
    options.rowHeight = 24.0f;
    return options;
}

[[nodiscard]] static Point Center(const Rect& bounds){
    return { bounds.x + bounds.width * 0.5f, bounds.y + bounds.height * 0.5f };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiListBuilderTests : public WidgetFixture{
protected:
    [[nodiscard]] bool prepare(const u64 generation, const ListOptions& options = Options()){
        return prepareSource(generation, m_source, m_state, options);
    }

    [[nodiscard]] bool prepareSource(const u64 generation, const IListDataSource& source, ListState& state,
        const ListOptions& options = Options()){
        if(!begin(generation) || !m_builder.beginPanel("panel", { 10.0f, 10.0f, 320.0f, 260.0f }))
            return false;
        m_result = m_builder.virtualList("list", source, state, options);
        return m_result.valid && finishPanel();
    }

    [[nodiscard]] bool accept(const u64 generation, const ListOptions& options = Options()){
        return prepare(generation, options) && m_context.commitFrame(generation);
    }

    [[nodiscard]] bool preparePopups(const u64 generation, PopupState& lowerPopup, ListState& lowerState,
        PopupState* upperPopup = nullptr, ListState* upperState = nullptr){
        if(!begin(generation) || !m_builder.beginPanel("panel", { 10.0f, 10.0f, 320.0f, 260.0f }))
            return false;
        if(!m_builder.virtualList("list", m_source, m_state, Options()).valid || !m_builder.endPanel())
            return false;
        PopupOptions popup;
        popup.anchor = { 350.0f, 10.0f, 10.0f, 10.0f };
        popup.size = { 320.0f, 180.0f };
        if(m_builder.beginPopup("popup", lowerPopup, popup)){
            if(!m_builder.virtualList("list", m_source, lowerState, Options()).valid || !m_builder.endPopup())
                return false;
        }
        if(upperPopup && upperState){
            popup.anchor = { 400.0f, 220.0f, 10.0f, 10.0f };
            if(m_builder.beginPopup("upper", *upperPopup, popup)){
                if(!m_builder.virtualList("list", m_source, *upperState, Options()).valid || !m_builder.endPopup())
                    return false;
            }
        }
        return m_context.endRoot() && m_context.finishFrame();
    }

    [[nodiscard]] WidgetId host()const{ return id("list", "panel"); }

    [[nodiscard]] WidgetId row(const u64 key)const{
        return MakeWidgetPartId(MakeWidgetId(host(), "rows"), key);
    }

    [[nodiscard]] bool wheel(const f64 delta = -1.0){
        const HitTarget* accepted = target(host());
        if(!accepted)
            return false;
        InputEvent event;
        event.type = InputEventType::PointerWheel;
        event.position = Center(accepted->rectangle);
        event.scrollY = delta;
        return send(event).pointerConsumed;
    }

    void key(const InputKey::Enum value, const bool repeat = false){
        InputEvent event;
        event.type = InputEventType::KeyDown;
        event.key = value;
        event.repeat = repeat;
        EXPECT_TRUE(send(event).keyboardConsumed);
        if(!repeat){
            event.type = InputEventType::KeyUp;
            EXPECT_TRUE(send(event).keyboardConsumed);
        }
    }

    [[nodiscard]] usize glyphIndices(const DrawSnapshot& snapshot)const{
        usize count = 0u;
        for(const auto& command : snapshot.commands()){
            if(command.material == PaintMaterial::Glyph || command.material == PaintMaterial::SdfGlyph)
                count += command.indexCount;
        }
        return count;
    }


protected:
    ArithmeticSource m_source;
    ListState m_state;
    ListResult m_result;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiListBuilderTests, SiblingListsPaintTheirOwnDeferredBackgroundStyles){
    m_source.count = 0u;
    ListState secondState;
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 10.0f, 10.0f, 320.0f, 260.0f }));
    m_builder.listStyle().background = Name("button.normal");
    ASSERT_TRUE(m_builder.virtualList("first", m_source, m_state, Options()).valid);
    m_builder.listStyle().background = Name("button.hover");
    ASSERT_TRUE(m_builder.virtualList("second", m_source, secondState, Options()).valid);
    ASSERT_TRUE(finishPanel());
    const DrawSnapshot snapshot = m_paint.freeze();
    Rect first;
    Rect second;
    ASSERT_TRUE(skinQuad(snapshot, 6u, first));
    ASSERT_TRUE(skinQuad(snapshot, 7u, second));
    EXPECT_LT(first.y, second.y);
    EXPECT_EQ(first.width, 280.0f);
    EXPECT_EQ(second.width, 280.0f);
}

TEST_F(UiListBuilderTests, HundredThousandRowsBuildOnlyVisibleTextTargetsAndGlyphs){
    ASSERT_TRUE(prepare(1u));
    const u64 visible = m_state.placement().endRow - m_state.placement().firstRow;
    ASSERT_GT(visible, 0u);
    EXPECT_LE(visible, 6u);
    EXPECT_EQ(m_source.textCalls, visible);
    EXPECT_LE(m_source.keyCalls, visible * 2u + 2u);
    EXPECT_LE(m_source.lookupCalls, visible + 2u);
    EXPECT_EQ(m_source.searchCalls, 0u);
    const DrawSnapshot snapshot = m_paint.freeze();
    EXPECT_EQ(glyphIndices(snapshot), static_cast<usize>(visible) * 12u);
    EXPECT_LT(snapshot.vertices().size(), 200u);
    EXPECT_LT(snapshot.indices().size(), 400u);
    ASSERT_TRUE(m_context.commitFrame(1u));
    ASSERT_EQ(m_context.states().entries().size(), 2u);
    EXPECT_EQ(target(host())->control.contentGeneration, m_source.generation);
    EXPECT_EQ(target(host())->control.contentRevision, m_source.contentRevision);
    EXPECT_EQ(m_context.input().targets().size(), static_cast<usize>(visible) + 4u);
    usize tabStops = 0u;
    for(const auto& accepted : m_context.input().targets()){
        tabStops += accepted.focusable ? 1u : 0u;
        if(accepted.owner == host()){
            EXPECT_FALSE(accepted.focusable);
            EXPECT_EQ(accepted.ownerDeclarationGeneration, target(host())->declarationGeneration);
            EXPECT_EQ(accepted.control, target(host())->control);
        }
    }
    EXPECT_EQ(tabStops, 1u);
}

TEST_F(UiListBuilderTests, ScrollingKeepsDisplayedRowsUntilTheMatchingCandidateIsAccepted){
    ASSERT_TRUE(accept(1u));
    ASSERT_NE(target(row(1u)), nullptr);
    const Rect oldBounds = target(row(1u))->rectangle;
    ASSERT_TRUE(wheel());
    ASSERT_TRUE(prepare(2u));
    EXPECT_DOUBLE_EQ(m_state.scrollOffset(), 72.0);
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
    ASSERT_NE(target(row(1u)), nullptr);
    EXPECT_FLOAT_EQ(target(row(1u))->rectangle.y, oldBounds.y);
    EXPECT_EQ(m_context.states().entries().size(), 2u);
    const Point oldRow = Center(oldBounds);
    EXPECT_EQ(m_context.input().hitTest(oldRow), row(1u));
    EXPECT_FALSE(m_context.commitFrame(3u));
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_EQ(target(row(1u)), nullptr);
    ASSERT_NE(target(row(4u)), nullptr);
    EXPECT_EQ(m_context.input().hitTest(oldRow), row(4u));
}

TEST_F(UiListBuilderTests, InitialProgrammaticSelectionSurvivesAndEndNavigatesToVisibleLastKey){
    m_state.select(50000u);
    ASSERT_TRUE(accept(1u));
    EXPECT_EQ(m_state.selectedKey(), 50000u);
    ASSERT_NE(target(row(50000u)), nullptr);
    key(InputKey::Tab);
    key(InputKey::End);
    ASSERT_TRUE(prepare(2u));
    EXPECT_EQ(m_state.selectedKey(), 100000u);
    EXPECT_FALSE(m_result.activated);
    EXPECT_TRUE(m_result.selectionChanged);
    EXPECT_GT(m_state.scrollOffset(), 2399800.0);
    ASSERT_TRUE(m_context.commitFrame(2u));
    ASSERT_NE(target(row(100000u)), nullptr);
    EXPECT_LE(m_source.textCalls, 12u);
}

TEST_F(UiListBuilderTests, ReorderPreservesSelectionByKeyAndRemovalClearsIt){
    m_state.select(50000u);
    ASSERT_TRUE(accept(1u));
    m_source.reverse = true;
    ++m_source.contentRevision;
    ASSERT_TRUE(prepare(2u));
    EXPECT_FALSE(m_result.selectionChanged);
    EXPECT_EQ(m_state.selectedKey(), 50000u);
    ASSERT_TRUE(m_context.commitFrame(2u));
    ASSERT_NE(target(row(50000u)), nullptr);
    m_source.removed = 50000u;
    --m_source.count;
    ++m_source.contentRevision;
    ASSERT_TRUE(prepare(3u));
    EXPECT_TRUE(m_result.selectionChanged);
    EXPECT_EQ(m_state.selectedKey(), 0u);
    EXPECT_EQ(m_state.cursorKey(), 0u);
    ASSERT_TRUE(m_context.commitFrame(3u));
    EXPECT_EQ(target(row(50000u)), nullptr);
}

TEST_F(UiListBuilderTests, ProgrammaticSameSelectionFencesQueuedActivationAndWheel){
    m_state.select(1u);
    ASSERT_TRUE(accept(1u));
    click(Center(target(row(2u))->rectangle));
    ASSERT_TRUE(wheel());
    const u64 inputGeneration = m_state.inputGeneration();
    m_state.select(1u);
    EXPECT_NE(m_state.inputGeneration(), inputGeneration);
    ASSERT_TRUE(prepare(2u));
    EXPECT_EQ(m_state.selectedKey(), 1u);
    EXPECT_FALSE(m_result.selectionChanged);
    EXPECT_FALSE(m_result.activated);
    EXPECT_DOUBLE_EQ(m_state.scrollOffset(), 0.0);
    EXPECT_TRUE(m_context.input().controlActions().empty());
    EXPECT_FALSE(m_context.input().focus().valid());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_EQ(m_context.input().focus(), host());
}

TEST_F(UiListBuilderTests, SourceRevisionFencesHeldCaptureAndRestoresHostFocusAtAcceptance){
    ASSERT_TRUE(accept(1u));
    const Point point = Center(target(row(1u))->rectangle);
    EXPECT_TRUE(send({ InputEventType::PrimaryDown, point }).pointerConsumed);
    ++m_source.contentRevision;
    ASSERT_TRUE(prepare(2u));
    EXPECT_FALSE(m_context.input().capture().valid());
    EXPECT_FALSE(m_context.input().focus().valid());
    EXPECT_TRUE(send({ InputEventType::PrimaryUp, point }).pointerConsumed);
    EXPECT_TRUE(m_context.input().controlActions().empty());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_EQ(m_context.input().focus(), host());
    ASSERT_TRUE(prepare(3u));
    EXPECT_FALSE(m_result.activated);
    EXPECT_EQ(m_state.selectedKey(), 0u);
}

TEST_F(UiListBuilderTests, HeldNavigationCannotMutateAnAcceptedReorderedDataset){
    m_state.select(1u);
    ASSERT_TRUE(accept(1u));
    key(InputKey::Tab);
    InputEvent event;
    event.type = InputEventType::KeyDown;
    event.key = InputKey::Down;
    EXPECT_TRUE(send(event).keyboardConsumed);
    m_source.reverse = true;
    ++m_source.contentRevision;
    ASSERT_TRUE(prepare(2u));
    EXPECT_EQ(m_state.selectedKey(), 1u);
    ASSERT_TRUE(m_context.commitFrame(2u));
    event.repeat = true;
    EXPECT_TRUE(send(event).keyboardConsumed);
    ASSERT_TRUE(prepare(3u));
    EXPECT_EQ(m_state.selectedKey(), 1u);
    EXPECT_FALSE(m_result.selectionChanged);
    ASSERT_TRUE(m_context.commitFrame(3u));
    event.type = InputEventType::KeyUp;
    EXPECT_TRUE(send(event).keyboardConsumed);
}

TEST_F(UiListBuilderTests, ReplacementSourceResetsSelectionAndScroll){
    m_state.select(60000u);
    ASSERT_TRUE(accept(1u));
    ASSERT_GT(m_state.scrollOffset(), 0.0);
    ++m_source.generation;
    ASSERT_TRUE(prepare(2u));
    EXPECT_EQ(m_state.selectedKey(), 0u);
    EXPECT_EQ(m_state.cursorKey(), 0u);
    EXPECT_DOUBLE_EQ(m_state.scrollOffset(), 0.0);
    EXPECT_TRUE(m_result.selectionChanged);
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_NE(target(row(1u)), nullptr);
}

TEST_F(UiListBuilderTests, ReplacementStateRejectsOldActivationEvenForTheSameSourceAndHost){
    ASSERT_TRUE(accept(1u));
    click(Center(target(row(2u))->rectangle));
    ListState replacement;
    ASSERT_TRUE(prepareSource(2u, m_source, replacement));
    EXPECT_FALSE(m_result.activated);
    EXPECT_EQ(replacement.selectedKey(), 0u);
    EXPECT_TRUE(m_context.input().controlActions().empty());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_EQ(target(host())->control.instanceGeneration, replacement.inputGeneration());
}

TEST_F(UiListBuilderTests, DisabledAndOmittedListsRetirePartsWithoutBorrowingQueuedActions){
    ASSERT_TRUE(accept(1u));
    click(Center(target(row(2u))->rectangle));
    ListOptions disabled = Options();
    disabled.enabled = false;
    ASSERT_TRUE(prepare(2u, disabled));
    EXPECT_FALSE(m_result.activated);
    EXPECT_EQ(m_state.selectedKey(), 0u);
    EXPECT_FALSE(m_context.input().focus().valid());
    EXPECT_TRUE(m_context.input().controlActions().empty());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_EQ(m_context.input().targets().size(), 2u);
    EXPECT_FALSE(target(host())->enabled);
    ASSERT_TRUE(begin(3u));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 10.0f, 10.0f, 320.0f, 260.0f }));
    ASSERT_TRUE(finishPanel());
    EXPECT_EQ(target(host()), nullptr);
    EXPECT_EQ(m_context.states().entries().size(), 1u);
    ASSERT_TRUE(m_context.commitFrame(3u));
    EXPECT_EQ(m_context.input().targets().size(), 1u);
}

TEST_F(UiListBuilderTests, SourceRevisionChangedDuringScopeRejectsTheDeferredLoan){
    ASSERT_TRUE(accept(1u));
    ASSERT_TRUE(begin(2u));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 10.0f, 10.0f, 320.0f, 260.0f }));
    const ListResult result = m_builder.virtualList("list", m_source, m_state, Options());
    ASSERT_TRUE(result.valid);
    ++m_source.contentRevision;
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(m_context.failed());
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
    EXPECT_FALSE(m_context.ready());
}

TEST_F(UiListBuilderTests, DuplicateOrMalformedVisibleKeysRejectTheFrame){
    m_source.duplicate = true;
    EXPECT_FALSE(prepare(1u));
    EXPECT_TRUE(m_context.failed());
    EXPECT_TRUE(m_context.input().targets().empty());
    m_context.abandonFrame();
    m_builder.reset();
    m_source.duplicate = false;
    m_source.malformedLookup = true;
    EXPECT_FALSE(prepare(2u));
    EXPECT_TRUE(m_context.failed());
    EXPECT_TRUE(m_context.input().targets().empty());
}

TEST_F(UiListBuilderTests, EmptyDatasetBuildsOnlyItsHostAndProvidesOneKeyboardStop){
    m_source.count = 0u;
    ASSERT_TRUE(accept(1u));
    EXPECT_EQ(m_source.textCalls, 0u);
    EXPECT_EQ(m_context.input().targets().size(), 2u);
    EXPECT_FALSE(m_state.placement().scrollbarVisible);
    key(InputKey::Tab);
    key(InputKey::Down);
    ASSERT_TRUE(prepare(2u));
    EXPECT_EQ(m_state.selectedKey(), 0u);
    EXPECT_FALSE(m_result.activated);
    EXPECT_FALSE(m_result.selectionChanged);
}

TEST_F(UiListBuilderTests, ThumbUsesItsAcceptedTrackMaximumWhenCandidateHeightChanges){
    ASSERT_TRUE(accept(1u));
    const WidgetId thumb = MakeWidgetId(host(), "scrollbar");
    ASSERT_NE(target(thumb), nullptr);
    const HitTarget accepted = *target(thumb);
    const Point origin = Center(accepted.rectangle);
    const Point destination{ origin.x, origin.y + 10.0f };
    drag(origin, destination);
    ListOptions resized = Options();
    resized.height.value = 200.0f;
    ASSERT_TRUE(prepare(2u, resized));
    const f64 travel = static_cast<f64>(accepted.gestureReference.height) - accepted.rectangle.height;
    EXPECT_DOUBLE_EQ(m_state.scrollOffset(), 10.0 / travel * accepted.gestureMaximum);
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_FLOAT_EQ(target(thumb)->gestureReference.height, 190.0f);
}

TEST_F(UiListBuilderTests, StandaloneSelectableUsesLegacyActivationAndSkinFallback){
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 10.0f, 10.0f, 320.0f, 260.0f }));
    WidgetOptions options;
    options.width = { LayoutSizePolicy::Fixed, 100.0f };
    options.height = { LayoutSizePolicy::Fixed, 30.0f };
    EXPECT_FALSE(m_builder.selectable("choice", "Choice", true, options));
    ASSERT_TRUE(finishPanel());
    const DrawSnapshot snapshot = m_paint.freeze();
    Rect selected;
    EXPECT_TRUE(skinQuad(snapshot, 8u, selected));
    ASSERT_TRUE(m_context.commitFrame(1u));
    const WidgetId choice = id("choice", "panel");
    ASSERT_NE(target(choice), nullptr);
    EXPECT_TRUE(target(choice)->focusable);
    click(Center(target(choice)->rectangle));
    ASSERT_TRUE(begin(2u));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 10.0f, 10.0f, 320.0f, 260.0f }));
    EXPECT_TRUE(m_builder.selectable("choice", "Choice", false, options));
    ASSERT_TRUE(finishPanel());
}

TEST_F(UiListBuilderTests, LatestThumbMotionFollowsAnInterveningWheelInNativeInputOrder){
    ASSERT_TRUE(accept(1u));
    const HitTarget accepted = *target(MakeWidgetId(host(), "scrollbar"));
    const Point origin = Center(accepted.rectangle);
    const Point destination{ origin.x, accepted.gestureReference.y + accepted.gestureReference.height
        - accepted.rectangle.height * 0.5f };
    EXPECT_TRUE(send({ InputEventType::PrimaryDown, origin }).pointerConsumed);
    InputEvent event;
    event.type = InputEventType::PointerWheel;
    event.position = origin;
    event.scrollY = 1.0;
    EXPECT_TRUE(send(event).pointerConsumed);
    EXPECT_TRUE(send({ InputEventType::PointerMove, destination }).pointerConsumed);
    EXPECT_TRUE(send({ InputEventType::PrimaryUp, destination }).pointerConsumed);
    ASSERT_TRUE(prepare(2u));
    EXPECT_DOUBLE_EQ(m_state.scrollOffset(), accepted.gestureMaximum);
    EXPECT_EQ(m_state.placement().endRow, 100000u);
}

TEST_F(UiListBuilderTests, TinyPositiveRowHeightWithEmptyDatasetKeepsABoundedPageCount){
    m_source.count = 0u;
    ListOptions options = Options();
    options.rowHeight = 1.0e-30f;
    ASSERT_TRUE(accept(1u, options));
    ASSERT_NE(target(host()), nullptr);
    EXPECT_EQ(target(host())->pageRows, 1u);
    EXPECT_EQ(m_source.textCalls, 0u);
}

TEST_F(UiListBuilderTests, TinyPositiveRowHeightWithSmallDatasetBoundsPageSizeByRows){
    m_source.count = 2u;
    ListOptions options = Options();
    options.rowHeight = 1.0e-30f;
    ASSERT_TRUE(accept(1u, options));
    ASSERT_NE(target(host()), nullptr);
    EXPECT_EQ(target(host())->pageRows, 2u);
    EXPECT_EQ(m_source.textCalls, 2u);
    EXPECT_EQ(m_state.placement().endRow, 2u);
}

TEST_F(UiListBuilderTests, PopupListsAutofocusAndRestoreEachCoveredListHost){
    m_state.select(1u);
    ASSERT_TRUE(accept(1u));
    key(InputKey::Tab);
    EXPECT_EQ(m_context.input().focus(), host());
    PopupState lowerPopup;
    PopupState upperPopup;
    ListState lowerState;
    ListState upperState;
    lowerState.select(2u);
    upperState.select(3u);
    lowerPopup.open();
    ASSERT_TRUE(preparePopups(2u, lowerPopup, lowerState));
    ASSERT_TRUE(m_context.commitFrame(2u));
    const WidgetId lowerHost = id("list", "popup");
    EXPECT_EQ(m_context.input().focus(), lowerHost);
    upperPopup.open();
    ASSERT_TRUE(preparePopups(3u, lowerPopup, lowerState, &upperPopup, &upperState));
    ASSERT_TRUE(m_context.commitFrame(3u));
    EXPECT_EQ(m_context.input().focus(), id("list", "upper"));
    key(InputKey::Down);
    ASSERT_TRUE(preparePopups(4u, lowerPopup, lowerState, &upperPopup, &upperState));
    EXPECT_EQ(upperState.selectedKey(), 4u);
    EXPECT_EQ(lowerState.selectedKey(), 2u);
    EXPECT_EQ(m_state.selectedKey(), 1u);
    ASSERT_TRUE(m_context.commitFrame(4u));
    upperPopup.close();
    ASSERT_TRUE(preparePopups(5u, lowerPopup, lowerState, &upperPopup, &upperState));
    ASSERT_TRUE(m_context.commitFrame(5u));
    EXPECT_EQ(m_context.input().focus(), lowerHost);
    key(InputKey::Down);
    ASSERT_TRUE(preparePopups(6u, lowerPopup, lowerState, &upperPopup, &upperState));
    EXPECT_EQ(lowerState.selectedKey(), 3u);
    ASSERT_TRUE(m_context.commitFrame(6u));
    lowerPopup.close();
    ASSERT_TRUE(preparePopups(7u, lowerPopup, lowerState));
    ASSERT_TRUE(m_context.commitFrame(7u));
    EXPECT_EQ(m_context.input().focus(), host());
    key(InputKey::Down);
    ASSERT_TRUE(prepare(8u));
    EXPECT_EQ(m_state.selectedKey(), 2u);
}

TEST_F(UiListBuilderTests, TwoCompletedThumbDragsAreConsumedInOneDeclarationUsingTheirOwnBaseline){
    ASSERT_TRUE(accept(1u));
    const WidgetId thumb = MakeWidgetId(host(), "scrollbar");
    ASSERT_NE(target(thumb), nullptr);
    const HitTarget accepted = *target(thumb);
    const Point origin = Center(accepted.rectangle);
    drag(origin, { origin.x, origin.y + 10.0f });
    drag(origin, { origin.x, origin.y + 20.0f });
    ASSERT_TRUE(prepare(2u));
    const f64 travel = static_cast<f64>(accepted.gestureReference.height) - accepted.rectangle.height;
    EXPECT_DOUBLE_EQ(m_state.scrollOffset(), 20.0 / travel * accepted.gestureMaximum);
    PointerGesture pending;
    EXPECT_FALSE(m_context.input().consumePointerGesture(thumb, accepted.declarationGeneration, pending));
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_EQ(m_context.input().focus(), host());
}

TEST_F(UiListBuilderTests, FullHeightMinimumThumbFocusesItsHostWithoutStartingAnUnusableGesture){
    ListOptions options = Options();
    options.height.value = 20.0f;
    ASSERT_TRUE(accept(1u, options));
    const WidgetId thumb = MakeWidgetId(host(), "scrollbar");
    ASSERT_NE(target(thumb), nullptr);
    const HitTarget accepted = *target(thumb);
    EXPECT_FLOAT_EQ(m_state.placement().track.height, m_state.placement().thumb.height);
    EXPECT_FALSE(accepted.pointerGesture);
    click(Center(accepted.rectangle));
    EXPECT_EQ(m_context.input().focus(), host());
    PointerGesture pending;
    EXPECT_FALSE(m_context.input().consumePointerGesture(thumb, accepted.declarationGeneration, pending));
    ASSERT_TRUE(prepare(2u, options));
    EXPECT_TRUE(m_result.valid);
    EXPECT_TRUE(m_result.focused);
    EXPECT_FALSE(m_result.activated);
    EXPECT_DOUBLE_EQ(m_state.scrollOffset(), 0.0);
    EXPECT_FALSE(m_context.failed());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

