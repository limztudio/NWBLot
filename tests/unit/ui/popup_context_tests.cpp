// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ui/context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_popup_context_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;

struct PopupFrame{
    WidgetState base;
    WidgetState popup;
    WidgetState child;
    PopupScope scope;
};

struct PopupStack{
    PopupFrame lower;
    PopupFrame higher;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiPopupContextTests : public testing::Test{
public:
    UiPopupContextTests()
        : m_arena(Name("tests/ui/popup_context"))
        , m_context(m_arena)
    {}


protected:
    [[nodiscard]] bool begin(const u64 generation){
        return m_context.beginFrame(generation) && m_context.beginRoot({ 401u, 1u });
    }

    [[nodiscard]] bool finish(){
        return m_context.endRoot() && m_context.finishFrame();
    }

    [[nodiscard]] WidgetState declare(const AStringView key, const WidgetKind::Enum kind){
        const WidgetState* state = m_context.declare(key, kind);
        EXPECT_NE(state, nullptr);
        return state ? *state : WidgetState{};
    }

    [[nodiscard]] WidgetState button(const AStringView key, const Rect& bounds, const bool publish = true){
        const WidgetState state = declare(key, WidgetKind::Button);
        if(!state.id.valid())
            return {};
        if(publish){
            HitTarget target;
            target.rectangle = bounds;
            target.clip = { 0.0f, 0.0f, 400.0f, 300.0f };
            target.focusable = true;
            target.activatable = true;
            EXPECT_TRUE(m_context.addTarget(state, target));
        }
        return state;
    }

    [[nodiscard]] PopupScope scope(const WidgetState& owner, const Rect& bounds = { 100.0f, 40.0f, 160.0f, 100.0f },
        const u64 instance = 1u, const u64 epoch = 1u){
        PopupScope result;
        result.token = { owner.id, owner.declarationGeneration, instance, epoch };
        result.bounds = bounds;
        result.viewport = { 0.0f, 0.0f, 400.0f, 300.0f };
        return result;
    }

    void addOwner(const WidgetState& owner, const PopupScope& popup){
        HitTarget target;
        target.rectangle = popup.bounds;
        target.clip = popup.viewport;
        EXPECT_TRUE(m_context.addTarget(owner, target));
    }

    [[nodiscard]] PopupFrame prepare(
        const u64 generation, const bool open = false, const Rect& bounds = { 100.0f, 40.0f, 160.0f, 100.0f },
        const bool keepClosedChild = false, const bool autofocus = true){
        PopupFrame frame;
        EXPECT_TRUE(begin(generation));
        frame.base = button("base", { 10.0f, 10.0f, 60.0f, 24.0f });
        frame.popup = declare("popup", WidgetKind::Popup);
        frame.scope = scope(frame.popup, bounds);
        frame.scope.autofocus = autofocus;
        if(open){
            EXPECT_TRUE(m_context.beginPopupScope(frame.popup, frame.scope));
            addOwner(frame.popup, frame.scope);
        }
        if(open || keepClosedChild){
            EXPECT_TRUE(m_context.pushScope("popup"));
            frame.child = button("child", { bounds.x + 10.0f, bounds.y + 10.0f, 60.0f, 24.0f }, open);
            EXPECT_TRUE(m_context.popScope());
        }
        if(open)
            EXPECT_TRUE(m_context.endPopupScope(true));
        EXPECT_TRUE(finish());
        return frame;
    }

    [[nodiscard]] InputRoutingResult send(const InputEvent& event){
        EXPECT_TRUE(m_context.input().queue(event));
        return m_context.input().process();
    }

    [[nodiscard]] PopupStack prepareStack(const u64 generation){
        PopupStack stack;
        EXPECT_TRUE(begin(generation));
        stack.lower.base = button("base", { 10.0f, 10.0f, 60.0f, 24.0f });
        stack.lower.popup = declare("popup", WidgetKind::Popup);
        stack.lower.scope = scope(stack.lower.popup);
        EXPECT_TRUE(m_context.beginPopupScope(stack.lower.popup, stack.lower.scope));
        addOwner(stack.lower.popup, stack.lower.scope);
        EXPECT_TRUE(m_context.pushScope("popup"));
        stack.lower.child = button("child", { 110.0f, 50.0f, 60.0f, 24.0f });
        EXPECT_TRUE(m_context.popScope());
        EXPECT_TRUE(m_context.endPopupScope(true));
        stack.higher.base = stack.lower.base;
        stack.higher.popup = declare("higher", WidgetKind::Popup);
        stack.higher.scope = scope(stack.higher.popup, { 200.0f, 120.0f, 160.0f, 100.0f });
        EXPECT_TRUE(m_context.beginPopupScope(stack.higher.popup, stack.higher.scope));
        addOwner(stack.higher.popup, stack.higher.scope);
        EXPECT_TRUE(m_context.pushScope("higher"));
        stack.higher.child = button("child", { 210.0f, 130.0f, 60.0f, 24.0f });
        EXPECT_TRUE(m_context.popScope());
        EXPECT_TRUE(m_context.endPopupScope(true));
        EXPECT_TRUE(finish());
        return stack;
    }

    void click(const Point& position){
        EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = position }).pointerConsumed);
        EXPECT_TRUE(send({ .type = InputEventType::PrimaryUp, .position = position }).pointerConsumed);
    }

    [[nodiscard]] PopupFrame focusBase(){
        const PopupFrame frame = prepare(1u);
        EXPECT_TRUE(m_context.commitFrame(1u));
        EXPECT_TRUE(send({ .type = InputEventType::KeyDown, .position = {}, .key = InputKey::Tab }).keyboardConsumed);
        EXPECT_TRUE(send({ .type = InputEventType::KeyUp, .position = {}, .key = InputKey::Tab }).keyboardConsumed);
        EXPECT_EQ(m_context.input().focus(), frame.base.id);
        return frame;
    }

    [[nodiscard]] const HitTarget* target(const WidgetId id)const{
        for(const auto& item : m_context.input().targets()){
            if(item.id == id)
                return &item;
        }
        return nullptr;
    }


protected:
    Core::Alloc::GlobalArena m_arena;
    Context m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiPopupContextTests, PendingOpenPreservesBaseFocusAndRoutingUntilExactAcceptance){
    const PopupFrame base = focusBase();
    const PopupFrame candidate = prepare(2u, true);
    ASSERT_TRUE(candidate.child.id.valid());
    EXPECT_TRUE(m_context.ready());
    EXPECT_FALSE(m_context.input().hasPopup());
    EXPECT_EQ(m_context.input().focus(), base.base.id);
    EXPECT_EQ(m_context.input().hitTest({ 15.0f, 15.0f }), base.base.id);
    EXPECT_FALSE(m_context.input().hitTest({ 115.0f, 55.0f }).valid());
    EXPECT_FALSE(m_context.input().wouldConsumePointer({ 350.0f, 250.0f }));
    EXPECT_FALSE(m_context.commitFrame(3u));
    EXPECT_FALSE(m_context.input().hasPopup());
    EXPECT_EQ(m_context.input().focus(), base.base.id);
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_TRUE(m_context.input().hasPopup());
    EXPECT_EQ(m_context.input().focus(), candidate.child.id);
    EXPECT_EQ(m_context.input().hitTest({ 115.0f, 55.0f }), candidate.child.id);
    EXPECT_FALSE(m_context.input().hitTest({ 15.0f, 15.0f }).valid());
    EXPECT_TRUE(m_context.input().wouldConsumePointer({ 350.0f, 250.0f }));
}

TEST_F(UiPopupContextTests, PendingGeometryKeepsAcceptedHitBoundsAndChildFocus){
    const PopupFrame original = prepare(1u, true);
    ASSERT_TRUE(m_context.commitFrame(1u));
    const PopupFrame candidate = prepare(2u, true, { 200.0f, 160.0f, 160.0f, 100.0f });
    EXPECT_EQ(candidate.child.id, original.child.id);
    EXPECT_EQ(candidate.child.declarationGeneration, original.child.declarationGeneration);
    EXPECT_EQ(m_context.input().focus(), original.child.id);
    EXPECT_EQ(m_context.input().hitTest({ 115.0f, 55.0f }), original.child.id);
    EXPECT_FALSE(m_context.input().hitTest({ 215.0f, 175.0f }).valid());
    EXPECT_FALSE(m_context.commitFrame(1u));
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_EQ(m_context.input().focus(), original.child.id);
    EXPECT_FALSE(m_context.input().hitTest({ 115.0f, 55.0f }).valid());
    EXPECT_EQ(m_context.input().hitTest({ 215.0f, 175.0f }), candidate.child.id);
}

TEST_F(UiPopupContextTests, ScopeOmissionRetainsLiveAcceptedDeclarationsUntilCommitThenRestoresFocus){
    const PopupFrame base = focusBase();
    const PopupFrame opened = prepare(2u, true);
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_EQ(m_context.input().focus(), opened.child.id);
    const PopupFrame closed = prepare(3u, false, opened.scope.bounds, true);
    EXPECT_EQ(closed.child.id, opened.child.id);
    EXPECT_EQ(closed.child.declarationGeneration, opened.child.declarationGeneration);
    EXPECT_TRUE(m_context.input().hasPopup());
    EXPECT_EQ(m_context.input().focus(), opened.child.id);
    EXPECT_TRUE(m_context.input().wouldConsumePointer({ 350.0f, 250.0f }));
    EXPECT_FALSE(m_context.input().hitTest({ 15.0f, 15.0f }).valid());
    ASSERT_TRUE(m_context.commitFrame(3u));
    EXPECT_FALSE(m_context.input().hasPopup());
    EXPECT_EQ(m_context.input().focus(), base.base.id);
    EXPECT_EQ(m_context.input().hitTest({ 15.0f, 15.0f }), base.base.id);
    EXPECT_FALSE(m_context.input().wouldConsumePointer({ 350.0f, 250.0f }));
}

TEST_F(UiPopupContextTests, RemovingPopupDeclarationImmediatelyRetiresAcceptedScopeAndActions){
    const PopupFrame original = prepare(1u, true);
    ASSERT_TRUE(m_context.commitFrame(1u));
    click({ 115.0f, 55.0f });
    ASSERT_EQ(m_context.input().actions().size(), 1u);
    ASSERT_TRUE(begin(2u));
    const WidgetState base = button("base", { 10.0f, 10.0f, 60.0f, 24.0f });
    ASSERT_TRUE(finish());
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
    EXPECT_FALSE(m_context.input().hasPopup());
    EXPECT_TRUE(m_context.input().actions().empty());
    EXPECT_FALSE(m_context.input().focus().valid());
    EXPECT_FALSE(m_context.input().hitTest({ 115.0f, 55.0f }).valid());
    EXPECT_EQ(m_context.input().hitTest({ 15.0f, 15.0f }), base.id);
    ASSERT_TRUE(m_context.commitFrame(2u));
    const PopupFrame recreated = prepare(3u, true);
    EXPECT_EQ(recreated.popup.id, original.popup.id);
    EXPECT_GT(recreated.popup.declarationGeneration, original.popup.declarationGeneration);
    EXPECT_GT(recreated.child.declarationGeneration, original.child.declarationGeneration);
    ASSERT_TRUE(m_context.commitFrame(3u));
    EXPECT_TRUE(m_context.input().actions().empty());
}

TEST_F(UiPopupContextTests, RootRetirementRemovesAcceptedAndPendingPopupOwnershipImmediately){
    const PopupFrame original = prepare(1u, true);
    ASSERT_TRUE(m_context.commitFrame(1u));
    click({ 115.0f, 55.0f });
    ASSERT_EQ(m_context.input().actions().size(), 1u);
    const PopupFrame pending = prepare(2u, true);
    EXPECT_EQ(pending.popup.id, original.popup.id);
    m_context.retainRoots(nullptr, 0u);
    EXPECT_TRUE(m_context.states().entries().empty());
    EXPECT_FALSE(m_context.input().hasPopup());
    EXPECT_FALSE(m_context.input().focus().valid());
    EXPECT_FALSE(m_context.input().capture().valid());
    EXPECT_TRUE(m_context.input().actions().empty());
    EXPECT_FALSE(m_context.input().hitTest({ 115.0f, 55.0f }).valid());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_TRUE(m_context.input().targets().empty());
    EXPECT_FALSE(m_context.input().hasPopup());
}

TEST_F(UiPopupContextTests, ReopeningSameWidgetCannotBorrowAnActivationFromItsPreviousOpenEpoch){
    const PopupFrame original = prepare(1u, true);
    ASSERT_TRUE(m_context.commitFrame(1u));
    click({ 115.0f, 55.0f });
    ASSERT_EQ(m_context.input().actions().size(), 1u);
    ASSERT_TRUE(begin(2u));
    const WidgetState base = button("base", { 10.0f, 10.0f, 60.0f, 24.0f });
    EXPECT_EQ(base.id, original.base.id);
    const WidgetState owner = declare("popup", WidgetKind::Popup);
    EXPECT_EQ(owner.id, original.popup.id);
    EXPECT_EQ(owner.declarationGeneration, original.popup.declarationGeneration);
    const PopupScope reopened = scope(owner, original.scope.bounds, 1u, 2u);
    ASSERT_TRUE(m_context.beginPopupScope(owner, reopened));
    addOwner(owner, reopened);
    ASSERT_TRUE(m_context.pushScope("popup"));
    const WidgetState child = button("child", { 110.0f, 50.0f, 60.0f, 24.0f });
    EXPECT_EQ(child.id, original.child.id);
    EXPECT_EQ(child.declarationGeneration, original.child.declarationGeneration);
    EXPECT_FALSE(m_context.takeActivation(child, true));
    ASSERT_TRUE(m_context.popScope());
    ASSERT_TRUE(m_context.endPopupScope(true));
    ASSERT_TRUE(finish());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_TRUE(m_context.input().actions().empty());
    click({ 115.0f, 55.0f });
    ASSERT_TRUE(begin(3u));
    const WidgetState freshBase = button("base", { 10.0f, 10.0f, 60.0f, 24.0f });
    EXPECT_EQ(freshBase.id, original.base.id);
    const WidgetState freshOwner = declare("popup", WidgetKind::Popup);
    const PopupScope current = scope(freshOwner, original.scope.bounds, 1u, 2u);
    ASSERT_TRUE(m_context.beginPopupScope(freshOwner, current));
    addOwner(freshOwner, current);
    ASSERT_TRUE(m_context.pushScope("popup"));
    const WidgetState freshChild = button("child", { 110.0f, 50.0f, 60.0f, 24.0f });
    EXPECT_TRUE(m_context.takeActivation(freshChild, true));
    EXPECT_FALSE(m_context.takeActivation(freshChild, true));
    ASSERT_TRUE(m_context.popScope());
    ASSERT_TRUE(m_context.endPopupScope(true));
    ASSERT_TRUE(finish());
    ASSERT_TRUE(m_context.commitFrame(3u));
}

TEST_F(UiPopupContextTests, TargetsInSequentialScopesReceiveDistinctLayersThenRestoreBaseLayer){
    ASSERT_TRUE(begin(1u));
    const WidgetState base = declare("base", WidgetKind::Button);
    HitTarget spoofed;
    spoofed.rectangle = { 10.0f, 10.0f, 60.0f, 24.0f };
    spoofed.clip = { 0.0f, 0.0f, 400.0f, 300.0f };
    spoofed.focusable = true;
    spoofed.activatable = true;
    spoofed.popup = { { 999u }, 7u, 8u, 9u };
    spoofed.layer = 99u;
    ASSERT_TRUE(m_context.addTarget(base, spoofed));
    EXPECT_EQ(m_context.popupLayer(), 0u);
    const WidgetState firstOwner = declare("first", WidgetKind::Popup);
    const PopupScope first = scope(firstOwner);
    ASSERT_TRUE(m_context.beginPopupScope(firstOwner, first));
    EXPECT_EQ(m_context.popupLayer(), 1u);
    addOwner(firstOwner, first);
    ASSERT_TRUE(m_context.pushScope("first"));
    const WidgetState firstChild = button("child", { 110.0f, 50.0f, 60.0f, 24.0f });
    ASSERT_TRUE(m_context.popScope());
    ASSERT_TRUE(m_context.endPopupScope(true));
    EXPECT_EQ(m_context.popupLayer(), 0u);
    const WidgetState secondOwner = declare("second", WidgetKind::Popup);
    const PopupScope second = scope(secondOwner, { 200.0f, 120.0f, 160.0f, 100.0f });
    ASSERT_TRUE(m_context.beginPopupScope(secondOwner, second));
    EXPECT_EQ(m_context.popupLayer(), 2u);
    addOwner(secondOwner, second);
    ASSERT_TRUE(m_context.pushScope("second"));
    const WidgetState secondChild = button("child", { 210.0f, 130.0f, 60.0f, 24.0f });
    ASSERT_TRUE(m_context.popScope());
    ASSERT_TRUE(m_context.endPopupScope(true));
    EXPECT_EQ(m_context.popupLayer(), 0u);
    const WidgetState tail = button("tail", { 80.0f, 10.0f, 60.0f, 24.0f });
    ASSERT_TRUE(finish());
    ASSERT_TRUE(m_context.commitFrame(1u));
    ASSERT_NE(target(base.id), nullptr);
    ASSERT_NE(target(firstOwner.id), nullptr);
    ASSERT_NE(target(firstChild.id), nullptr);
    ASSERT_NE(target(secondOwner.id), nullptr);
    ASSERT_NE(target(secondChild.id), nullptr);
    ASSERT_NE(target(tail.id), nullptr);
    EXPECT_FALSE(target(base.id)->popup.valid());
    EXPECT_EQ(target(base.id)->layer, 0u);
    EXPECT_TRUE(target(firstOwner.id)->popup == first.token);
    EXPECT_TRUE(target(firstChild.id)->popup == first.token);
    EXPECT_EQ(target(firstOwner.id)->layer, 1u);
    EXPECT_EQ(target(firstChild.id)->layer, 1u);
    EXPECT_TRUE(target(secondOwner.id)->popup == second.token);
    EXPECT_TRUE(target(secondChild.id)->popup == second.token);
    EXPECT_EQ(target(secondOwner.id)->layer, 2u);
    EXPECT_EQ(target(secondChild.id)->layer, 2u);
    EXPECT_FALSE(target(tail.id)->popup.valid());
    EXPECT_EQ(target(tail.id)->layer, 0u);
    EXPECT_EQ(m_context.input().focus(), secondChild.id);
}

TEST_F(UiPopupContextTests, NestedOrUnbalancedScopesPoisonCandidateWithoutPublishingIt){
    const PopupFrame accepted = focusBase();
    ASSERT_TRUE(begin(2u));
    const WidgetState owner = declare("popup", WidgetKind::Popup);
    const PopupScope popup = scope(owner);
    ASSERT_TRUE(m_context.beginPopupScope(owner, popup));
    const WidgetState nestedOwner = declare("nested", WidgetKind::Popup);
    EXPECT_FALSE(m_context.beginPopupScope(nestedOwner, scope(nestedOwner)));
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.finishFrame());
    EXPECT_FALSE(m_context.commitFrame(2u));
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
    EXPECT_EQ(m_context.input().focus(), accepted.base.id);
    EXPECT_EQ(m_context.input().hitTest({ 15.0f, 15.0f }), accepted.base.id);
    m_context.abandonFrame();
    ASSERT_TRUE(begin(3u));
    const WidgetState unbalancedOwner = declare("popup", WidgetKind::Popup);
    ASSERT_TRUE(m_context.beginPopupScope(unbalancedOwner, scope(unbalancedOwner)));
    EXPECT_FALSE(m_context.endRoot());
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.finishFrame());
    EXPECT_FALSE(m_context.commitFrame(3u));
}

TEST_F(UiPopupContextTests, CapacityRejectsNinthScopeBeforePublishingAnyCandidate){
    const PopupFrame accepted = focusBase();
    ASSERT_TRUE(begin(2u));
    const AStringView keys[]{ "popup0", "popup1", "popup2", "popup3", "popup4", "popup5", "popup6", "popup7" };
    for(usize index = 0u; index < s_InputMaxPopups; ++index){
        const WidgetState owner = declare(keys[index], WidgetKind::Popup);
        const PopupScope popup = scope(owner);
        ASSERT_TRUE(m_context.beginPopupScope(owner, popup));
        EXPECT_EQ(m_context.popupLayer(), static_cast<u32>(index + 1u));
        addOwner(owner, popup);
        ASSERT_TRUE(m_context.endPopupScope(true));
    }
    const WidgetState overflow = declare("popup8", WidgetKind::Popup);
    EXPECT_FALSE(m_context.beginPopupScope(overflow, scope(overflow)));
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.finishFrame());
    EXPECT_FALSE(m_context.commitFrame(2u));
    EXPECT_FALSE(m_context.input().hasPopup());
    EXPECT_EQ(m_context.input().focus(), accepted.base.id);
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
}

TEST_F(UiPopupContextTests, DuplicateOwnerOrMismatchedTokenCannotPublishAScope){
    const PopupFrame accepted = focusBase();
    ASSERT_TRUE(begin(2u));
    const WidgetState owner = declare("popup", WidgetKind::Popup);
    const PopupScope popup = scope(owner);
    ASSERT_TRUE(m_context.beginPopupScope(owner, popup));
    addOwner(owner, popup);
    ASSERT_TRUE(m_context.endPopupScope(true));
    EXPECT_FALSE(m_context.beginPopupScope(owner, popup));
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.finishFrame());
    EXPECT_FALSE(m_context.commitFrame(2u));
    EXPECT_EQ(m_context.input().focus(), accepted.base.id);
    EXPECT_FALSE(m_context.input().hasPopup());
    m_context.abandonFrame();
    ASSERT_TRUE(begin(3u));
    const WidgetState current = declare("popup", WidgetKind::Popup);
    PopupScope stale = scope(current);
    ++stale.token.declarationGeneration;
    EXPECT_FALSE(m_context.beginPopupScope(current, stale));
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.finishFrame());
    EXPECT_FALSE(m_context.commitFrame(3u));
}

TEST_F(UiPopupContextTests, AutofocusDisabledChangesFocusOnlyAtAcceptanceAndTabEntersPopup){
    const PopupFrame accepted = focusBase();
    const PopupFrame candidate = prepare(2u, true, { 100.0f, 40.0f, 160.0f, 100.0f }, false, false);
    EXPECT_EQ(m_context.input().focus(), accepted.base.id);
    EXPECT_FALSE(m_context.input().hasPopup());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_TRUE(m_context.input().hasPopup());
    EXPECT_FALSE(m_context.input().focus().valid());
    EXPECT_TRUE(m_context.input().wantsKeyboard());
    EXPECT_TRUE(send({ .type = InputEventType::KeyDown, .position = {}, .key = InputKey::Tab }).keyboardConsumed);
    EXPECT_TRUE(send({ .type = InputEventType::KeyUp, .position = {}, .key = InputKey::Tab }).keyboardConsumed);
    EXPECT_EQ(m_context.input().focus(), candidate.child.id);
}

TEST_F(UiPopupContextTests, MissingOwnerTargetRejectsCommitWithoutReplacingAcceptedFocusOrLayout){
    const PopupFrame accepted = focusBase();
    ASSERT_TRUE(begin(2u));
    const WidgetState base = button("base", { 10.0f, 10.0f, 60.0f, 24.0f });
    EXPECT_EQ(base.id, accepted.base.id);
    const WidgetState owner = declare("popup", WidgetKind::Popup);
    ASSERT_TRUE(m_context.beginPopupScope(owner, scope(owner)));
    ASSERT_TRUE(m_context.pushScope("popup"));
    const WidgetState child = button("child", { 110.0f, 50.0f, 60.0f, 24.0f });
    EXPECT_TRUE(child.id.valid());
    ASSERT_TRUE(m_context.popScope());
    ASSERT_TRUE(m_context.endPopupScope(true));
    ASSERT_TRUE(finish());
    EXPECT_FALSE(m_context.commitFrame(2u));
    EXPECT_TRUE(m_context.ready());
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
    EXPECT_EQ(m_context.input().focus(), accepted.base.id);
    EXPECT_FALSE(m_context.input().hasPopup());
    EXPECT_EQ(m_context.input().hitTest({ 15.0f, 15.0f }), accepted.base.id);
    EXPECT_FALSE(m_context.input().wouldConsumePointer({ 350.0f, 250.0f }));
}

TEST_F(UiPopupContextTests, FocusLossFencesPreparedOpenBeforeItsFirstAcceptance){
    const PopupFrame base = focusBase();
    const PopupFrame candidate = prepare(2u, true);
    EXPECT_EQ(m_context.input().focus(), base.base.id);
    EXPECT_FALSE(m_context.input().hasPopup());
    EXPECT_TRUE(send({ .type = InputEventType::FocusLost, .position = {} }).keyboardConsumed);
    EXPECT_FALSE(m_context.input().focus().valid());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_EQ(m_context.input().layoutGeneration(), 2u);
    EXPECT_TRUE(m_context.input().hasPopup());
    EXPECT_FALSE(m_context.input().focus().valid());
    EXPECT_FALSE(m_context.input().hitTest({ 115.0f, 55.0f }).valid());
    EXPECT_FALSE(m_context.input().hitTest({ 15.0f, 15.0f }).valid());
    EXPECT_TRUE(m_context.input().actions().empty());
    PopupDismissReason::Enum reason = PopupDismissReason::None;
    EXPECT_TRUE(m_context.input().consumePopupDismissal(candidate.scope.token, reason));
    EXPECT_EQ(reason, PopupDismissReason::FocusLost);
    EXPECT_FALSE(m_context.input().consumePopupDismissal(candidate.scope.token, reason));
}

TEST_F(UiPopupContextTests, FocusLossFencesPreparedHigherPopupAboveAcceptedLowerScope){
    const PopupFrame original = prepare(1u, true);
    ASSERT_TRUE(m_context.commitFrame(1u));
    ASSERT_EQ(m_context.input().focus(), original.child.id);
    const PopupStack candidate = prepareStack(2u);
    EXPECT_TRUE(candidate.lower.scope.token == original.scope.token);
    EXPECT_EQ(m_context.input().focus(), original.child.id);
    EXPECT_TRUE(send({ .type = InputEventType::FocusLost, .position = {} }).keyboardConsumed);
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_FALSE(m_context.input().focus().valid());
    EXPECT_FALSE(m_context.input().hitTest({ 115.0f, 55.0f }).valid());
    EXPECT_FALSE(m_context.input().hitTest({ 215.0f, 135.0f }).valid());
    EXPECT_TRUE(m_context.input().actions().empty());
    PopupDismissReason::Enum lower = PopupDismissReason::None;
    PopupDismissReason::Enum higher = PopupDismissReason::None;
    EXPECT_TRUE(m_context.input().consumePopupDismissal(candidate.lower.scope.token, lower));
    EXPECT_TRUE(m_context.input().consumePopupDismissal(candidate.higher.scope.token, higher));
    EXPECT_EQ(lower, PopupDismissReason::FocusLost);
    EXPECT_EQ(higher, PopupDismissReason::FocusLost);
    EXPECT_FALSE(m_context.input().consumePopupDismissal(candidate.lower.scope.token, lower));
    EXPECT_FALSE(m_context.input().consumePopupDismissal(candidate.higher.scope.token, higher));
}

TEST_F(UiPopupContextTests, NativeFocusGainCannotReviveCandidatePreparedBeforeFocusLoss){
    const PopupFrame base = focusBase();
    const PopupFrame candidate = prepare(2u, true);
    EXPECT_EQ(m_context.input().focus(), base.base.id);
    EXPECT_TRUE(send({ .type = InputEventType::FocusLost, .position = {} }).keyboardConsumed);
    EXPECT_FALSE(send({ .type = InputEventType::FocusGained, .position = {} }).focus.valid());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_FALSE(m_context.input().focus().valid());
    EXPECT_FALSE(m_context.input().hitTest({ 115.0f, 55.0f }).valid());
    PopupDismissReason::Enum reason = PopupDismissReason::None;
    EXPECT_TRUE(m_context.input().consumePopupDismissal(candidate.scope.token, reason));
    EXPECT_EQ(reason, PopupDismissReason::FocusLost);
    EXPECT_TRUE(m_context.input().actions().empty());
}

TEST_F(UiPopupContextTests, FreshPopupPreparedAfterNativeFocusGainMayAutofocus){
    const PopupFrame base = focusBase();
    EXPECT_TRUE(send({ .type = InputEventType::FocusLost, .position = {} }).keyboardConsumed);
    EXPECT_FALSE(send({ .type = InputEventType::FocusGained, .position = {} }).focus.valid());
    const PopupFrame fresh = prepare(2u, true);
    EXPECT_EQ(fresh.base.id, base.base.id);
    EXPECT_FALSE(m_context.input().focus().valid());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_EQ(m_context.input().focus(), fresh.child.id);
    EXPECT_EQ(m_context.input().hitTest({ 115.0f, 55.0f }), fresh.child.id);
    PopupDismissReason::Enum reason = PopupDismissReason::None;
    EXPECT_FALSE(m_context.input().consumePopupDismissal(fresh.scope.token, reason));
    click({ 115.0f, 55.0f });
    EXPECT_EQ(m_context.input().actions().size(), 1u);
}

TEST_F(UiPopupContextTests, NativeFocusGainNeverRestoresAlreadyClosingScopes){
    const PopupFrame base = focusBase();
    const PopupFrame original = prepare(2u, true);
    ASSERT_TRUE(m_context.commitFrame(2u));
    ASSERT_EQ(m_context.input().focus(), original.child.id);
    EXPECT_TRUE(send({ .type = InputEventType::FocusLost, .position = {} }).keyboardConsumed);
    EXPECT_FALSE(send({ .type = InputEventType::FocusGained, .position = {} }).focus.valid());
    EXPECT_FALSE(m_context.input().hitTest({ 115.0f, 55.0f }).valid());
    const PopupFrame retained = prepare(3u, true);
    EXPECT_TRUE(retained.scope.token == original.scope.token);
    ASSERT_TRUE(m_context.commitFrame(3u));
    EXPECT_FALSE(m_context.input().focus().valid());
    EXPECT_FALSE(m_context.input().hitTest({ 115.0f, 55.0f }).valid());
    PopupDismissReason::Enum reason = PopupDismissReason::None;
    EXPECT_TRUE(m_context.input().consumePopupDismissal(original.scope.token, reason));
    EXPECT_EQ(reason, PopupDismissReason::FocusLost);
    const PopupFrame closed = prepare(4u, false, original.scope.bounds, true);
    EXPECT_EQ(closed.base.id, base.base.id);
    ASSERT_TRUE(m_context.commitFrame(4u));
    EXPECT_FALSE(m_context.input().hasPopup());
    EXPECT_FALSE(m_context.input().focus().valid());
    EXPECT_FALSE(send({ .type = InputEventType::FocusGained, .position = {} }).focus.valid());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

