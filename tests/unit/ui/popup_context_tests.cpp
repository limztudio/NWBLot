// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/unit/ui/native_input.h>

#include <impl/ecs_ui/toolkit/context.h>

#include <global/simdmath.h>

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

    [[nodiscard]] Expected<WidgetState> declare(const AStringView key, const WidgetKind::Enum kind){
        const auto state = m_context.declare(key, kind);
        EXPECT_TRUE(state);
        if(!state)
            return MakeUnexpected(state.error());
        return **state;
    }

    [[nodiscard]] Expected<WidgetState> button(const AStringView key, const Rect& bounds, const bool publish = true){
        const auto stateResult = declare(key, WidgetKind::Button);
        EXPECT_TRUE(stateResult);
        if(!stateResult)
            return MakeUnexpected(stateResult.error());
        const WidgetState state = *stateResult;
        if(!state.id.valid())
            return MakeUnexpected(Failure{});
        if(publish){
            HitTarget target;
            target.rectangle = bounds;
            target.clip = { 0.0f, 0.0f, 400.0f, 300.0f };
            target.focusable = true;
            target.activatable = true;
            EXPECT_TRUE(m_context.addTarget(state, target));
        }
        if(m_context.failed())
            return MakeUnexpected(Failure{});
        return state;
    }

    [[nodiscard]] PopupScope scope(const WidgetState& owner, const Rect& bounds = { 100.0f, 40.0f, 160.0f, 100.0f }, const u64 instance = 1u, const u64 epoch = 1u)noexcept{
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

    [[nodiscard]] Expected<PopupFrame> prepare(const u64 generation, const bool open = false, const Rect& bounds = { 100.0f, 40.0f, 160.0f, 100.0f }, const bool keepClosedChild = false, const bool autofocus = true){
        PopupFrame frame;
        EXPECT_TRUE(begin(generation));
        const auto frameBaseResult = button("base", { 10.0f, 10.0f, 60.0f, 24.0f });
        EXPECT_TRUE(frameBaseResult);
        if(!frameBaseResult)
            return MakeUnexpected(frameBaseResult.error());
        frame.base = *frameBaseResult;
        const auto framePopupResult = declare("popup", WidgetKind::Popup);
        EXPECT_TRUE(framePopupResult);
        if(!framePopupResult)
            return MakeUnexpected(framePopupResult.error());
        frame.popup = *framePopupResult;
        frame.scope = scope(frame.popup, bounds);
        frame.scope.autofocus = autofocus;
        if(open){
            EXPECT_TRUE(m_context.beginPopupScope(frame.popup, frame.scope));
            addOwner(frame.popup, frame.scope);
        }
        if(open || keepClosedChild){
            EXPECT_TRUE(m_context.pushScope("popup"));
            const SIMDVectorSingle childOrigin = VectorAdd(VectorSet(bounds.x, bounds.y, 0.0f, 0.0f), VectorReplicate(10.0f));
            const auto frameChildResult = button("child", { VectorGetX(childOrigin), VectorGetY(childOrigin), 60.0f, 24.0f }, open);
            EXPECT_TRUE(frameChildResult);
            if(!frameChildResult)
                return MakeUnexpected(frameChildResult.error());
            frame.child = *frameChildResult;
            EXPECT_TRUE(m_context.popScope());
        }
        if(open)
            EXPECT_TRUE(m_context.endPopupScope(true));
        EXPECT_TRUE(finish());
        if(m_context.failed())
            return MakeUnexpected(Failure{});
        return frame;
    }

    [[nodiscard]] InputRoutingResult send(const InputEvent& event){
        EXPECT_TRUE(m_context.input().queue(UiTestSupport::NativeInput(event)));
        return m_context.input().process();
    }

    [[nodiscard]] Expected<PopupStack> prepareStack(const u64 generation){
        PopupStack stack;
        EXPECT_TRUE(begin(generation));
        const auto stackLowerBaseResult = button("base", { 10.0f, 10.0f, 60.0f, 24.0f });
        EXPECT_TRUE(stackLowerBaseResult);
        if(!stackLowerBaseResult)
            return MakeUnexpected(stackLowerBaseResult.error());
        stack.lower.base = *stackLowerBaseResult;
        const auto stackLowerPopupResult = declare("popup", WidgetKind::Popup);
        EXPECT_TRUE(stackLowerPopupResult);
        if(!stackLowerPopupResult)
            return MakeUnexpected(stackLowerPopupResult.error());
        stack.lower.popup = *stackLowerPopupResult;
        stack.lower.scope = scope(stack.lower.popup);
        EXPECT_TRUE(m_context.beginPopupScope(stack.lower.popup, stack.lower.scope));
        addOwner(stack.lower.popup, stack.lower.scope);
        EXPECT_TRUE(m_context.pushScope("popup"));
        const auto stackLowerChildResult = button("child", { 110.0f, 50.0f, 60.0f, 24.0f });
        EXPECT_TRUE(stackLowerChildResult);
        if(!stackLowerChildResult)
            return MakeUnexpected(stackLowerChildResult.error());
        stack.lower.child = *stackLowerChildResult;
        EXPECT_TRUE(m_context.popScope());
        EXPECT_TRUE(m_context.endPopupScope(true));
        stack.higher.base = stack.lower.base;
        const auto stackHigherPopupResult = declare("higher", WidgetKind::Popup);
        EXPECT_TRUE(stackHigherPopupResult);
        if(!stackHigherPopupResult)
            return MakeUnexpected(stackHigherPopupResult.error());
        stack.higher.popup = *stackHigherPopupResult;
        stack.higher.scope = scope(stack.higher.popup, { 200.0f, 120.0f, 160.0f, 100.0f });
        EXPECT_TRUE(m_context.beginPopupScope(stack.higher.popup, stack.higher.scope));
        addOwner(stack.higher.popup, stack.higher.scope);
        EXPECT_TRUE(m_context.pushScope("higher"));
        const auto stackHigherChildResult = button("child", { 210.0f, 130.0f, 60.0f, 24.0f });
        EXPECT_TRUE(stackHigherChildResult);
        if(!stackHigherChildResult)
            return MakeUnexpected(stackHigherChildResult.error());
        stack.higher.child = *stackHigherChildResult;
        EXPECT_TRUE(m_context.popScope());
        EXPECT_TRUE(m_context.endPopupScope(true));
        EXPECT_TRUE(finish());
        if(m_context.failed())
            return MakeUnexpected(Failure{});
        return stack;
    }

    void click(const Point& position){
        EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = position }).pointerConsumed);
        EXPECT_TRUE(send({ .type = InputEventType::PrimaryUp, .position = position }).pointerConsumed);
    }

    [[nodiscard]] Expected<PopupFrame> focusBase(){
        const auto frameResult = prepare(1u);
        EXPECT_TRUE(frameResult);
        if(!frameResult)
            return MakeUnexpected(frameResult.error());
        const PopupFrame frame = *frameResult;
        const bool committed = m_context.commitFrame(1u);
        EXPECT_TRUE(committed);
        if(!committed)
            return MakeUnexpected(Failure{});
        EXPECT_TRUE(send({ .type = InputEventType::KeyDown, .position = {}, .key = Core::Key::Tab }).commandConsumed);
        EXPECT_TRUE(send({ .type = InputEventType::KeyUp, .position = {}, .key = Core::Key::Tab }).commandConsumed);
        EXPECT_EQ(m_context.input().focus(), frame.base.id);
        if(m_context.input().focus() != frame.base.id)
            return MakeUnexpected(Failure{});
        return frame;
    }


protected:
    Core::Alloc::GlobalArena m_arena;
    Context m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiPopupContextTests, PendingOpenPreservesBaseFocusAndRoutingUntilExactAcceptance){
    const auto baseResult = focusBase();
    ASSERT_TRUE(baseResult);
    const PopupFrame base = *baseResult;
    const auto candidateResult = prepare(2u, true);
    ASSERT_TRUE(candidateResult);
    const PopupFrame candidate = *candidateResult;
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
    const auto originalResult = prepare(1u, true);
    ASSERT_TRUE(originalResult);
    const PopupFrame original = *originalResult;
    ASSERT_TRUE(m_context.commitFrame(1u));
    const auto candidateResult = prepare(2u, true, { 200.0f, 160.0f, 160.0f, 100.0f });
    ASSERT_TRUE(candidateResult);
    const PopupFrame candidate = *candidateResult;
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
    const auto baseResult = focusBase();
    ASSERT_TRUE(baseResult);
    const PopupFrame base = *baseResult;
    const auto openedResult = prepare(2u, true);
    ASSERT_TRUE(openedResult);
    const PopupFrame opened = *openedResult;
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_EQ(m_context.input().focus(), opened.child.id);
    const auto closedResult = prepare(3u, false, opened.scope.bounds, true);
    ASSERT_TRUE(closedResult);
    const PopupFrame closed = *closedResult;
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
    const auto originalResult = prepare(1u, true);
    ASSERT_TRUE(originalResult);
    const PopupFrame original = *originalResult;
    ASSERT_TRUE(m_context.commitFrame(1u));
    click({ 115.0f, 55.0f });
    ASSERT_EQ(m_context.input().actions().size(), 1u);
    ASSERT_TRUE(begin(2u));
    const auto baseResult = button("base", { 10.0f, 10.0f, 60.0f, 24.0f });
    ASSERT_TRUE(baseResult);
    const WidgetState base = *baseResult;
    ASSERT_TRUE(finish());
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
    EXPECT_FALSE(m_context.input().hasPopup());
    EXPECT_TRUE(m_context.input().actions().empty());
    EXPECT_FALSE(m_context.input().focus().valid());
    EXPECT_FALSE(m_context.input().hitTest({ 115.0f, 55.0f }).valid());
    EXPECT_EQ(m_context.input().hitTest({ 15.0f, 15.0f }), base.id);
    ASSERT_TRUE(m_context.commitFrame(2u));
    const auto recreatedResult = prepare(3u, true);
    ASSERT_TRUE(recreatedResult);
    const PopupFrame recreated = *recreatedResult;
    EXPECT_EQ(recreated.popup.id, original.popup.id);
    EXPECT_GT(recreated.popup.declarationGeneration, original.popup.declarationGeneration);
    EXPECT_GT(recreated.child.declarationGeneration, original.child.declarationGeneration);
    ASSERT_TRUE(m_context.commitFrame(3u));
    EXPECT_TRUE(m_context.input().actions().empty());
}

TEST_F(UiPopupContextTests, RootRetirementRemovesAcceptedAndPendingPopupOwnershipImmediately){
    const auto originalResult = prepare(1u, true);
    ASSERT_TRUE(originalResult);
    const PopupFrame original = *originalResult;
    ASSERT_TRUE(m_context.commitFrame(1u));
    click({ 115.0f, 55.0f });
    ASSERT_EQ(m_context.input().actions().size(), 1u);
    const auto pendingResult = prepare(2u, true);
    ASSERT_TRUE(pendingResult);
    const PopupFrame pending = *pendingResult;
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
    const auto originalResult = prepare(1u, true);
    ASSERT_TRUE(originalResult);
    const PopupFrame original = *originalResult;
    ASSERT_TRUE(m_context.commitFrame(1u));
    click({ 115.0f, 55.0f });
    ASSERT_EQ(m_context.input().actions().size(), 1u);
    ASSERT_TRUE(begin(2u));
    const auto baseResult = button("base", { 10.0f, 10.0f, 60.0f, 24.0f });
    ASSERT_TRUE(baseResult);
    const WidgetState base = *baseResult;
    EXPECT_EQ(base.id, original.base.id);
    const auto ownerResult = declare("popup", WidgetKind::Popup);
    ASSERT_TRUE(ownerResult);
    const WidgetState owner = *ownerResult;
    EXPECT_EQ(owner.id, original.popup.id);
    EXPECT_EQ(owner.declarationGeneration, original.popup.declarationGeneration);
    const PopupScope reopened = scope(owner, original.scope.bounds, 1u, 2u);
    ASSERT_TRUE(m_context.beginPopupScope(owner, reopened));
    addOwner(owner, reopened);
    ASSERT_TRUE(m_context.pushScope("popup"));
    const auto childResult = button("child", { 110.0f, 50.0f, 60.0f, 24.0f });
    ASSERT_TRUE(childResult);
    const WidgetState child = *childResult;
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
    const auto freshBaseResult = button("base", { 10.0f, 10.0f, 60.0f, 24.0f });
    ASSERT_TRUE(freshBaseResult);
    const WidgetState freshBase = *freshBaseResult;
    EXPECT_EQ(freshBase.id, original.base.id);
    const auto freshOwnerResult = declare("popup", WidgetKind::Popup);
    ASSERT_TRUE(freshOwnerResult);
    const WidgetState freshOwner = *freshOwnerResult;
    const PopupScope current = scope(freshOwner, original.scope.bounds, 1u, 2u);
    ASSERT_TRUE(m_context.beginPopupScope(freshOwner, current));
    addOwner(freshOwner, current);
    ASSERT_TRUE(m_context.pushScope("popup"));
    const auto freshChildResult = button("child", { 110.0f, 50.0f, 60.0f, 24.0f });
    ASSERT_TRUE(freshChildResult);
    const WidgetState freshChild = *freshChildResult;
    EXPECT_TRUE(m_context.takeActivation(freshChild, true));
    EXPECT_FALSE(m_context.takeActivation(freshChild, true));
    ASSERT_TRUE(m_context.popScope());
    ASSERT_TRUE(m_context.endPopupScope(true));
    ASSERT_TRUE(finish());
    ASSERT_TRUE(m_context.commitFrame(3u));
}

TEST_F(UiPopupContextTests, TargetsInSequentialScopesReceiveDistinctLayersThenRestoreBaseLayer){
    ASSERT_TRUE(begin(1u));
    const auto baseResult = declare("base", WidgetKind::Button);
    ASSERT_TRUE(baseResult);
    const WidgetState base = *baseResult;
    HitTarget spoofed;
    spoofed.rectangle = { 10.0f, 10.0f, 60.0f, 24.0f };
    spoofed.clip = { 0.0f, 0.0f, 400.0f, 300.0f };
    spoofed.focusable = true;
    spoofed.activatable = true;
    spoofed.popup = { { 999u }, 7u, 8u, 9u };
    spoofed.layer = 99u;
    ASSERT_TRUE(m_context.addTarget(base, spoofed));
    EXPECT_EQ(m_context.popupLayer(), 0u);
    const auto firstOwnerResult = declare("first", WidgetKind::Popup);
    ASSERT_TRUE(firstOwnerResult);
    const WidgetState firstOwner = *firstOwnerResult;
    const PopupScope first = scope(firstOwner);
    ASSERT_TRUE(m_context.beginPopupScope(firstOwner, first));
    EXPECT_EQ(m_context.popupLayer(), 1u);
    addOwner(firstOwner, first);
    ASSERT_TRUE(m_context.pushScope("first"));
    const auto firstChildResult = button("child", { 110.0f, 50.0f, 60.0f, 24.0f });
    ASSERT_TRUE(firstChildResult);
    const WidgetState firstChild = *firstChildResult;
    ASSERT_TRUE(m_context.popScope());
    ASSERT_TRUE(m_context.endPopupScope(true));
    EXPECT_EQ(m_context.popupLayer(), 0u);
    const auto secondOwnerResult = declare("second", WidgetKind::Popup);
    ASSERT_TRUE(secondOwnerResult);
    const WidgetState secondOwner = *secondOwnerResult;
    const PopupScope second = scope(secondOwner, { 200.0f, 120.0f, 160.0f, 100.0f });
    ASSERT_TRUE(m_context.beginPopupScope(secondOwner, second));
    EXPECT_EQ(m_context.popupLayer(), 2u);
    addOwner(secondOwner, second);
    ASSERT_TRUE(m_context.pushScope("second"));
    const auto secondChildResult = button("child", { 210.0f, 130.0f, 60.0f, 24.0f });
    ASSERT_TRUE(secondChildResult);
    const WidgetState secondChild = *secondChildResult;
    ASSERT_TRUE(m_context.popScope());
    ASSERT_TRUE(m_context.endPopupScope(true));
    EXPECT_EQ(m_context.popupLayer(), 0u);
    const auto tailResult = button("tail", { 80.0f, 10.0f, 60.0f, 24.0f });
    ASSERT_TRUE(tailResult);
    const WidgetState tail = *tailResult;
    ASSERT_TRUE(finish());
    ASSERT_TRUE(m_context.commitFrame(1u));
    ASSERT_NE(UiTestSupport::FindHitTarget(m_context.input().targets(), base.id), nullptr);
    ASSERT_NE(UiTestSupport::FindHitTarget(m_context.input().targets(), firstOwner.id), nullptr);
    ASSERT_NE(UiTestSupport::FindHitTarget(m_context.input().targets(), firstChild.id), nullptr);
    ASSERT_NE(UiTestSupport::FindHitTarget(m_context.input().targets(), secondOwner.id), nullptr);
    ASSERT_NE(UiTestSupport::FindHitTarget(m_context.input().targets(), secondChild.id), nullptr);
    ASSERT_NE(UiTestSupport::FindHitTarget(m_context.input().targets(), tail.id), nullptr);
    EXPECT_FALSE(UiTestSupport::FindHitTarget(m_context.input().targets(), base.id)->popup.valid());
    EXPECT_EQ(UiTestSupport::FindHitTarget(m_context.input().targets(), base.id)->layer, 0u);
    EXPECT_TRUE(UiTestSupport::FindHitTarget(m_context.input().targets(), firstOwner.id)->popup == first.token);
    EXPECT_TRUE(UiTestSupport::FindHitTarget(m_context.input().targets(), firstChild.id)->popup == first.token);
    EXPECT_EQ(UiTestSupport::FindHitTarget(m_context.input().targets(), firstOwner.id)->layer, 1u);
    EXPECT_EQ(UiTestSupport::FindHitTarget(m_context.input().targets(), firstChild.id)->layer, 1u);
    EXPECT_TRUE(UiTestSupport::FindHitTarget(m_context.input().targets(), secondOwner.id)->popup == second.token);
    EXPECT_TRUE(UiTestSupport::FindHitTarget(m_context.input().targets(), secondChild.id)->popup == second.token);
    EXPECT_EQ(UiTestSupport::FindHitTarget(m_context.input().targets(), secondOwner.id)->layer, 2u);
    EXPECT_EQ(UiTestSupport::FindHitTarget(m_context.input().targets(), secondChild.id)->layer, 2u);
    EXPECT_FALSE(UiTestSupport::FindHitTarget(m_context.input().targets(), tail.id)->popup.valid());
    EXPECT_EQ(UiTestSupport::FindHitTarget(m_context.input().targets(), tail.id)->layer, 0u);
    EXPECT_EQ(m_context.input().focus(), secondChild.id);
}

TEST_F(UiPopupContextTests, NestedScopeWithoutItsParentTokenPoisonsCandidateWithoutPublishingIt){
    const auto acceptedResult = focusBase();
    ASSERT_TRUE(acceptedResult);
    const PopupFrame accepted = *acceptedResult;
    ASSERT_TRUE(begin(2u));
    const auto ownerResult = declare("popup", WidgetKind::Popup);
    ASSERT_TRUE(ownerResult);
    const WidgetState owner = *ownerResult;
    const PopupScope popup = scope(owner);
    ASSERT_TRUE(m_context.beginPopupScope(owner, popup));
    const auto nestedOwnerResult = declare("nested", WidgetKind::Popup);
    ASSERT_TRUE(nestedOwnerResult);
    const WidgetState nestedOwner = *nestedOwnerResult;
    EXPECT_FALSE(m_context.beginPopupScope(nestedOwner, scope(nestedOwner)));
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.finishFrame());
    EXPECT_FALSE(m_context.commitFrame(2u));
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
    EXPECT_EQ(m_context.input().focus(), accepted.base.id);
    EXPECT_EQ(m_context.input().hitTest({ 15.0f, 15.0f }), accepted.base.id);
}

TEST_F(UiPopupContextTests, UnbalancedPopupScopeCannotPublishItsCandidate){
    const auto acceptedResult = focusBase();
    ASSERT_TRUE(acceptedResult);
    const PopupFrame accepted = *acceptedResult;
    ASSERT_TRUE(begin(2u));
    const auto unbalancedOwnerResult = declare("popup", WidgetKind::Popup);
    ASSERT_TRUE(unbalancedOwnerResult);
    const WidgetState unbalancedOwner = *unbalancedOwnerResult;
    ASSERT_TRUE(m_context.beginPopupScope(unbalancedOwner, scope(unbalancedOwner)));
    EXPECT_FALSE(m_context.endRoot());
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.finishFrame());
    EXPECT_FALSE(m_context.commitFrame(2u));
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
    EXPECT_EQ(m_context.input().focus(), accepted.base.id);
}

TEST_F(UiPopupContextTests, CapacityRejectsNinthScopeBeforePublishingAnyCandidate){
    const auto acceptedResult = focusBase();
    ASSERT_TRUE(acceptedResult);
    const PopupFrame accepted = *acceptedResult;
    ASSERT_TRUE(begin(2u));
    const AStringView keys[]{ "popup0", "popup1", "popup2", "popup3", "popup4", "popup5", "popup6", "popup7" };
    for(usize index = 0u; index < s_InputMaxPopups; ++index){
        const auto ownerResult = declare(keys[index], WidgetKind::Popup);
        ASSERT_TRUE(ownerResult);
        const WidgetState owner = *ownerResult;
        const PopupScope popup = scope(owner);
        ASSERT_TRUE(m_context.beginPopupScope(owner, popup));
        EXPECT_EQ(m_context.popupLayer(), static_cast<u32>(index + 1u));
        addOwner(owner, popup);
        ASSERT_TRUE(m_context.endPopupScope(true));
    }
    const auto overflowResult = declare("popup8", WidgetKind::Popup);
    ASSERT_TRUE(overflowResult);
    const WidgetState overflow = *overflowResult;
    EXPECT_FALSE(m_context.beginPopupScope(overflow, scope(overflow)));
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.finishFrame());
    EXPECT_FALSE(m_context.commitFrame(2u));
    EXPECT_FALSE(m_context.input().hasPopup());
    EXPECT_EQ(m_context.input().focus(), accepted.base.id);
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
}

TEST_F(UiPopupContextTests, DuplicateOwnerOrMismatchedTokenCannotPublishAScope){
    const auto acceptedResult = focusBase();
    ASSERT_TRUE(acceptedResult);
    const PopupFrame accepted = *acceptedResult;
    ASSERT_TRUE(begin(2u));
    const auto ownerResult = declare("popup", WidgetKind::Popup);
    ASSERT_TRUE(ownerResult);
    const WidgetState owner = *ownerResult;
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
    const auto currentResult = declare("popup", WidgetKind::Popup);
    ASSERT_TRUE(currentResult);
    const WidgetState current = *currentResult;
    PopupScope stale = scope(current);
    ++stale.token.declarationGeneration;
    EXPECT_FALSE(m_context.beginPopupScope(current, stale));
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.finishFrame());
    EXPECT_FALSE(m_context.commitFrame(3u));
}

TEST_F(UiPopupContextTests, AutofocusDisabledChangesFocusOnlyAtAcceptanceAndTabEntersPopup){
    const auto acceptedResult = focusBase();
    ASSERT_TRUE(acceptedResult);
    const PopupFrame accepted = *acceptedResult;
    const auto candidateResult = prepare(2u, true, { 100.0f, 40.0f, 160.0f, 100.0f }, false, false);
    ASSERT_TRUE(candidateResult);
    const PopupFrame candidate = *candidateResult;
    EXPECT_EQ(m_context.input().focus(), accepted.base.id);
    EXPECT_FALSE(m_context.input().hasPopup());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_TRUE(m_context.input().hasPopup());
    EXPECT_FALSE(m_context.input().focus().valid());
    EXPECT_TRUE(m_context.input().wantsCommands());
    EXPECT_TRUE(send({ .type = InputEventType::KeyDown, .position = {}, .key = Core::Key::Tab }).commandConsumed);
    EXPECT_TRUE(send({ .type = InputEventType::KeyUp, .position = {}, .key = Core::Key::Tab }).commandConsumed);
    EXPECT_EQ(m_context.input().focus(), candidate.child.id);
}

TEST_F(UiPopupContextTests, MissingOwnerTargetRejectsCommitWithoutReplacingAcceptedFocusOrLayout){
    const auto acceptedResult = focusBase();
    ASSERT_TRUE(acceptedResult);
    const PopupFrame accepted = *acceptedResult;
    ASSERT_TRUE(begin(2u));
    const auto baseResult = button("base", { 10.0f, 10.0f, 60.0f, 24.0f });
    ASSERT_TRUE(baseResult);
    const WidgetState base = *baseResult;
    EXPECT_EQ(base.id, accepted.base.id);
    const auto ownerResult = declare("popup", WidgetKind::Popup);
    ASSERT_TRUE(ownerResult);
    const WidgetState owner = *ownerResult;
    ASSERT_TRUE(m_context.beginPopupScope(owner, scope(owner)));
    ASSERT_TRUE(m_context.pushScope("popup"));
    const auto childResult = button("child", { 110.0f, 50.0f, 60.0f, 24.0f });
    ASSERT_TRUE(childResult);
    const WidgetState child = *childResult;
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
    const auto baseResult = focusBase();
    ASSERT_TRUE(baseResult);
    const PopupFrame base = *baseResult;
    const auto candidateResult = prepare(2u, true);
    ASSERT_TRUE(candidateResult);
    const PopupFrame candidate = *candidateResult;
    EXPECT_EQ(m_context.input().focus(), base.base.id);
    EXPECT_FALSE(m_context.input().hasPopup());
    EXPECT_TRUE(send({ .type = InputEventType::FocusLost, .position = {} }).commandConsumed);
    EXPECT_FALSE(m_context.input().focus().valid());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_EQ(m_context.input().layoutGeneration(), 2u);
    EXPECT_TRUE(m_context.input().hasPopup());
    EXPECT_FALSE(m_context.input().focus().valid());
    EXPECT_FALSE(m_context.input().hitTest({ 115.0f, 55.0f }).valid());
    EXPECT_FALSE(m_context.input().hitTest({ 15.0f, 15.0f }).valid());
    EXPECT_TRUE(m_context.input().actions().empty());
    PopupDismissReason::Enum reason = PopupDismissReason::None;
    const auto reasonResult = m_context.input().consumePopupDismissal(candidate.scope.token);
    ASSERT_TRUE(reasonResult);
    reason = *reasonResult;
    EXPECT_EQ(reason, PopupDismissReason::FocusLost);
    EXPECT_FALSE(m_context.input().consumePopupDismissal(candidate.scope.token));
}

TEST_F(UiPopupContextTests, FocusLossFencesPreparedHigherPopupAboveAcceptedLowerScope){
    const auto originalResult = prepare(1u, true);
    ASSERT_TRUE(originalResult);
    const PopupFrame original = *originalResult;
    ASSERT_TRUE(m_context.commitFrame(1u));
    ASSERT_EQ(m_context.input().focus(), original.child.id);
    const auto candidateResult = prepareStack(2u);
    ASSERT_TRUE(candidateResult);
    const PopupStack candidate = *candidateResult;
    EXPECT_TRUE(candidate.lower.scope.token == original.scope.token);
    EXPECT_EQ(m_context.input().focus(), original.child.id);
    EXPECT_TRUE(send({ .type = InputEventType::FocusLost, .position = {} }).commandConsumed);
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_FALSE(m_context.input().focus().valid());
    EXPECT_FALSE(m_context.input().hitTest({ 115.0f, 55.0f }).valid());
    EXPECT_FALSE(m_context.input().hitTest({ 215.0f, 135.0f }).valid());
    EXPECT_TRUE(m_context.input().actions().empty());
    PopupDismissReason::Enum lower = PopupDismissReason::None;
    PopupDismissReason::Enum higher = PopupDismissReason::None;
    const auto lowerResult = m_context.input().consumePopupDismissal(candidate.lower.scope.token);
    ASSERT_TRUE(lowerResult);
    lower = *lowerResult;
    const auto higherResult = m_context.input().consumePopupDismissal(candidate.higher.scope.token);
    ASSERT_TRUE(higherResult);
    higher = *higherResult;
    EXPECT_EQ(lower, PopupDismissReason::FocusLost);
    EXPECT_EQ(higher, PopupDismissReason::FocusLost);
    EXPECT_FALSE(m_context.input().consumePopupDismissal(candidate.lower.scope.token));
    EXPECT_FALSE(m_context.input().consumePopupDismissal(candidate.higher.scope.token));
}

TEST_F(UiPopupContextTests, NativeFocusGainCannotReviveCandidatePreparedBeforeFocusLoss){
    const auto baseResult = focusBase();
    ASSERT_TRUE(baseResult);
    const PopupFrame base = *baseResult;
    const auto candidateResult = prepare(2u, true);
    ASSERT_TRUE(candidateResult);
    const PopupFrame candidate = *candidateResult;
    EXPECT_EQ(m_context.input().focus(), base.base.id);
    EXPECT_TRUE(send({ .type = InputEventType::FocusLost, .position = {} }).commandConsumed);
    EXPECT_FALSE(send({ .type = InputEventType::FocusGained, .position = {} }).focus.valid());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_FALSE(m_context.input().focus().valid());
    EXPECT_FALSE(m_context.input().hitTest({ 115.0f, 55.0f }).valid());
    PopupDismissReason::Enum reason = PopupDismissReason::None;
    const auto reasonResult = m_context.input().consumePopupDismissal(candidate.scope.token);
    ASSERT_TRUE(reasonResult);
    reason = *reasonResult;
    EXPECT_EQ(reason, PopupDismissReason::FocusLost);
    EXPECT_TRUE(m_context.input().actions().empty());
}

TEST_F(UiPopupContextTests, FreshPopupPreparedAfterNativeFocusGainMayAutofocus){
    const auto baseResult = focusBase();
    ASSERT_TRUE(baseResult);
    const PopupFrame base = *baseResult;
    EXPECT_TRUE(send({ .type = InputEventType::FocusLost, .position = {} }).commandConsumed);
    EXPECT_FALSE(send({ .type = InputEventType::FocusGained, .position = {} }).focus.valid());
    const auto freshResult = prepare(2u, true);
    ASSERT_TRUE(freshResult);
    const PopupFrame fresh = *freshResult;
    EXPECT_EQ(fresh.base.id, base.base.id);
    EXPECT_FALSE(m_context.input().focus().valid());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_EQ(m_context.input().focus(), fresh.child.id);
    EXPECT_EQ(m_context.input().hitTest({ 115.0f, 55.0f }), fresh.child.id);
    EXPECT_FALSE(m_context.input().consumePopupDismissal(fresh.scope.token));
    click({ 115.0f, 55.0f });
    EXPECT_EQ(m_context.input().actions().size(), 1u);
}

TEST_F(UiPopupContextTests, NativeFocusGainNeverRestoresAlreadyClosingScopes){
    const auto baseResult = focusBase();
    ASSERT_TRUE(baseResult);
    const PopupFrame base = *baseResult;
    const auto originalResult = prepare(2u, true);
    ASSERT_TRUE(originalResult);
    const PopupFrame original = *originalResult;
    ASSERT_TRUE(m_context.commitFrame(2u));
    ASSERT_EQ(m_context.input().focus(), original.child.id);
    EXPECT_TRUE(send({ .type = InputEventType::FocusLost, .position = {} }).commandConsumed);
    EXPECT_FALSE(send({ .type = InputEventType::FocusGained, .position = {} }).focus.valid());
    EXPECT_FALSE(m_context.input().hitTest({ 115.0f, 55.0f }).valid());
    const auto retainedResult = prepare(3u, true);
    ASSERT_TRUE(retainedResult);
    const PopupFrame retained = *retainedResult;
    EXPECT_TRUE(retained.scope.token == original.scope.token);
    ASSERT_TRUE(m_context.commitFrame(3u));
    EXPECT_FALSE(m_context.input().focus().valid());
    EXPECT_FALSE(m_context.input().hitTest({ 115.0f, 55.0f }).valid());
    PopupDismissReason::Enum reason = PopupDismissReason::None;
    const auto reasonResult = m_context.input().consumePopupDismissal(original.scope.token);
    ASSERT_TRUE(reasonResult);
    reason = *reasonResult;
    EXPECT_EQ(reason, PopupDismissReason::FocusLost);
    const auto closedResult = prepare(4u, false, original.scope.bounds, true);
    ASSERT_TRUE(closedResult);
    const PopupFrame closed = *closedResult;
    EXPECT_EQ(closed.base.id, base.base.id);
    ASSERT_TRUE(m_context.commitFrame(4u));
    EXPECT_FALSE(m_context.input().hasPopup());
    EXPECT_FALSE(m_context.input().focus().valid());
    EXPECT_FALSE(send({ .type = InputEventType::FocusGained, .position = {} }).focus.valid());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

