// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ui/context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_context_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;


static HitTarget ButtonTarget(const Rect& rectangle = { 10.0f, 20.0f, 30.0f, 20.0f }){
    HitTarget target;
    target.rectangle = rectangle;
    target.clip = { 0.0f, 0.0f, 200.0f, 100.0f };
    target.focusable = true;
    target.activatable = true;
    return target;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiContextTests : public testing::Test{
public:
    UiContextTests()
        : m_arena(Name("tests/ui/context"))
        , m_context(m_arena)
    {}


protected:
    WidgetState prepare(const u64 generation, const WidgetRoot& root, const WidgetKind::Enum kind = WidgetKind::Button){
        EXPECT_TRUE(m_context.beginFrame(generation));
        EXPECT_TRUE(m_context.beginRoot(root));
        const WidgetState* state = m_context.declare("action", kind);
        EXPECT_NE(state, nullptr);
        if(state == nullptr)
            return {};
        const WidgetState retained = *state;
        EXPECT_TRUE(m_context.addTarget(retained, ButtonTarget()));
        EXPECT_TRUE(m_context.endRoot());
        EXPECT_TRUE(m_context.finishFrame());
        return retained;
    }

    InputRoutingResult click(const Point position = { 15.0f, 25.0f }){
        EXPECT_TRUE(m_context.input().queue({ .type = InputEventType::PrimaryDown, .position = position }));
        EXPECT_TRUE(m_context.input().queue({ .type = InputEventType::PrimaryUp, .position = position }));
        return m_context.input().process();
    }


protected:
    Core::Alloc::GlobalArena m_arena;
    Context m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(UiWidgetIdTests, StableKeysEncodeScopesAndRootLifetimeWithoutIterationOrderOrLabelText){
    const WidgetRoot root{ 7u, 3u };
    const WidgetId rootId = MakeRootId(root);
    ASSERT_TRUE(rootId.valid());
    const WidgetId menu = MakeWidgetId(rootId, "menu");
    const WidgetId tools = MakeWidgetId(rootId, "tools");
    const WidgetId action = MakeWidgetId(menu, "action");
    EXPECT_EQ(action, MakeWidgetId(MakeWidgetId(MakeRootId({ 7u, 3u }), "menu"), "action"));
    EXPECT_NE(action, MakeWidgetId(tools, "action"));
    EXPECT_NE(action, MakeWidgetId(MakeWidgetId(MakeRootId({ 7u, 4u }), "menu"), "action"));
    EXPECT_NE(action, MakeWidgetId(MakeWidgetId(MakeRootId({ 8u, 3u }), "menu"), "action"));
    EXPECT_NE(MakeWidgetId(rootId, AStringView("a\0b", 3u)), MakeWidgetId(rootId, "a"));
    EXPECT_NE(MakeWidgetId(MakeWidgetId(rootId, "a"), "bc"), MakeWidgetId(MakeWidgetId(rootId, "ab"), "c"));
    EXPECT_FALSE(MakeRootId({ 7u, 0u }).valid());
    EXPECT_FALSE(MakeWidgetId({}, "action").valid());
    EXPECT_FALSE(MakeWidgetId(rootId, "").valid());
}

TEST_F(UiContextTests, ReorderedDeclarationsAndScopedKeysRetainTheirExactIdentityAndLifetime){
    const WidgetRoot root{ 11u, 1u };
    ASSERT_TRUE(m_context.beginFrame(1u));
    ASSERT_TRUE(m_context.beginRoot(root));
    const WidgetState* first = m_context.declare("action", WidgetKind::Button);
    ASSERT_NE(first, nullptr);
    const WidgetState original = *first;
    ASSERT_TRUE(m_context.pushScope("nested"));
    const WidgetState* nested = m_context.declare("action", WidgetKind::Button);
    ASSERT_NE(nested, nullptr);
    const WidgetState nestedOriginal = *nested;
    EXPECT_NE(original.id, nestedOriginal.id);
    ASSERT_TRUE(m_context.popScope());
    ASSERT_TRUE(m_context.endRoot());
    ASSERT_TRUE(m_context.finishFrame());
    ASSERT_TRUE(m_context.commitFrame(1u));
    ASSERT_TRUE(m_context.beginFrame(2u));
    ASSERT_TRUE(m_context.beginRoot(root));
    ASSERT_TRUE(m_context.pushScope("nested"));
    nested = m_context.declare("action", WidgetKind::Button);
    ASSERT_NE(nested, nullptr);
    EXPECT_EQ(nested->id, nestedOriginal.id);
    EXPECT_EQ(nested->declarationGeneration, nestedOriginal.declarationGeneration);
    ASSERT_TRUE(m_context.popScope());
    first = m_context.declare("action", WidgetKind::Button);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->id, original.id);
    EXPECT_EQ(first->declarationGeneration, original.declarationGeneration);
    ASSERT_TRUE(m_context.endRoot());
    ASSERT_TRUE(m_context.finishFrame());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_EQ(m_context.states().entries().size(), 2u);
}

TEST_F(UiContextTests, DuplicateStableKeysPoisonTheCurrentBuildAndCannotPublishIt){
    const WidgetState original = prepare(1u, { 21u, 1u });
    ASSERT_TRUE(m_context.commitFrame(1u));
    ASSERT_TRUE(m_context.beginFrame(2u));
    ASSERT_TRUE(m_context.beginRoot(original.root));
    ASSERT_NE(m_context.declare("action", WidgetKind::Button), nullptr);
    EXPECT_EQ(m_context.declare("action", WidgetKind::Button), nullptr);
    EXPECT_TRUE(m_context.failed());
    EXPECT_EQ(m_context.declare("another", WidgetKind::Label), nullptr);
    EXPECT_FALSE(m_context.endRoot());
    EXPECT_FALSE(m_context.finishFrame());
    EXPECT_FALSE(m_context.commitFrame(2u));
    EXPECT_EQ(m_context.input().layoutGeneration(), 1u);
    EXPECT_EQ(m_context.input().hitTest({ 15.0f, 25.0f }), original.id);
    m_context.abandonFrame();
    EXPECT_TRUE(m_context.beginFrame(3u));
}

TEST_F(UiContextTests, UnbalancedOrEmptyScopesPoisonTheBuildUntilAbandonment){
    ASSERT_TRUE(m_context.beginFrame(1u));
    ASSERT_TRUE(m_context.beginRoot({ 31u, 1u }));
    ASSERT_TRUE(m_context.pushScope("panel"));
    ASSERT_NE(m_context.declare("action", WidgetKind::Button), nullptr);
    EXPECT_FALSE(m_context.endRoot());
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.finishFrame());
    EXPECT_FALSE(m_context.ready());
    m_context.abandonFrame();
    ASSERT_TRUE(m_context.beginFrame(2u));
    ASSERT_TRUE(m_context.beginRoot({ 31u, 1u }));
    EXPECT_FALSE(m_context.popScope());
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.finishFrame());
    m_context.abandonFrame();
    ASSERT_TRUE(m_context.beginFrame(3u));
    ASSERT_TRUE(m_context.beginRoot({ 31u, 1u }));
    EXPECT_FALSE(m_context.pushScope(""));
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.finishFrame());
}

TEST_F(UiContextTests, FinishingAnOpenRootDoesNotPublishItsPartialDeclarations){
    ASSERT_TRUE(m_context.beginFrame(1u));
    ASSERT_TRUE(m_context.beginRoot({ 41u, 1u }));
    ASSERT_NE(m_context.declare("action", WidgetKind::Button), nullptr);
    EXPECT_FALSE(m_context.finishFrame());
    EXPECT_FALSE(m_context.ready());
    EXPECT_FALSE(m_context.commitFrame(1u));
    EXPECT_EQ(m_context.input().layoutGeneration(), 0u);
}

TEST_F(UiContextTests, OnlyTheExactPreparedGenerationCanReplaceTheCommittedHitLayout){
    const WidgetState original = prepare(7u, { 51u, 1u });
    ASSERT_TRUE(m_context.ready());
    EXPECT_EQ(m_context.readyGeneration(), 7u);
    EXPECT_FALSE(m_context.commitFrame(6u));
    EXPECT_FALSE(m_context.commitFrame(8u));
    EXPECT_EQ(m_context.input().layoutGeneration(), 0u);
    ASSERT_TRUE(m_context.commitFrame(7u));
    EXPECT_FALSE(m_context.ready());
    EXPECT_EQ(m_context.input().hitTest({ 15.0f, 25.0f }), original.id);
    ASSERT_TRUE(m_context.beginFrame(9u));
    ASSERT_TRUE(m_context.beginRoot(original.root));
    const WidgetState* state = m_context.declare("action", WidgetKind::Button);
    ASSERT_NE(state, nullptr);
    ASSERT_TRUE(m_context.addTarget(*state, ButtonTarget({ 80.0f, 20.0f, 30.0f, 20.0f })));
    ASSERT_TRUE(m_context.endRoot());
    ASSERT_TRUE(m_context.finishFrame());
    EXPECT_FALSE(m_context.commitFrame(7u));
    EXPECT_FALSE(m_context.commitFrame(10u));
    EXPECT_EQ(m_context.input().layoutGeneration(), 7u);
    EXPECT_EQ(m_context.input().hitTest({ 15.0f, 25.0f }), original.id);
    EXPECT_FALSE(m_context.input().hitTest({ 85.0f, 25.0f }).valid());
    ASSERT_TRUE(m_context.commitFrame(9u));
    EXPECT_FALSE(m_context.input().hitTest({ 15.0f, 25.0f }).valid());
    EXPECT_EQ(m_context.input().hitTest({ 85.0f, 25.0f }), original.id);
    EXPECT_FALSE(m_context.commitFrame(9u));
    EXPECT_FALSE(m_context.beginFrame(9u));
}

TEST_F(UiContextTests, PendingPresentationCannotReplayConsumedActionsAndRetainsNewInputExactlyOnce){
    const WidgetState original = prepare(1u, { 61u, 1u });
    ASSERT_TRUE(m_context.commitFrame(1u));
    EXPECT_TRUE(click().pointerConsumed);
    ASSERT_EQ(m_context.input().actions().size(), 1u);
    const InputActionId firstAction = m_context.input().actions()[0u].id;
    ASSERT_TRUE(firstAction.valid());
    ASSERT_TRUE(m_context.beginFrame(2u));
    ASSERT_TRUE(m_context.beginRoot(original.root));
    const WidgetState* state = m_context.declare("action", WidgetKind::Button);
    ASSERT_NE(state, nullptr);
    EXPECT_TRUE(m_context.takeActivation(*state, true));
    EXPECT_FALSE(m_context.takeActivation(*state, true));
    ASSERT_TRUE(m_context.addTarget(*state, ButtonTarget()));
    ASSERT_TRUE(m_context.endRoot());
    ASSERT_TRUE(m_context.finishFrame());
    EXPECT_FALSE(m_context.beginFrame(3u));
    EXPECT_FALSE(m_context.beginFrame(2u));
    EXPECT_TRUE(m_context.input().actions().empty());
    EXPECT_FALSE(m_context.input().process().pointerConsumed);
    EXPECT_TRUE(click().pointerConsumed);
    ASSERT_EQ(m_context.input().actions().size(), 1u);
    EXPECT_GT(m_context.input().actions()[0u].id.sequence, firstAction.sequence);
    EXPECT_EQ(m_context.input().actions()[0u].id.layoutGeneration, 1u);
    EXPECT_FALSE(m_context.input().process().pointerConsumed);
    EXPECT_EQ(m_context.input().actions().size(), 1u);
    ASSERT_TRUE(m_context.commitFrame(2u));
    ASSERT_TRUE(m_context.beginFrame(3u));
    ASSERT_TRUE(m_context.beginRoot(original.root));
    state = m_context.declare("action", WidgetKind::Button);
    ASSERT_NE(state, nullptr);
    EXPECT_TRUE(m_context.takeActivation(*state, true));
    EXPECT_FALSE(m_context.takeActivation(*state, true));
    ASSERT_TRUE(m_context.addTarget(*state, ButtonTarget()));
    ASSERT_TRUE(m_context.endRoot());
    ASSERT_TRUE(m_context.finishFrame());
    ASSERT_TRUE(m_context.commitFrame(3u));
    EXPECT_TRUE(m_context.input().actions().empty());
}

TEST_F(UiContextTests, HiddenRootsRetireCaptureWhileTheHeldPointerSequenceStillOwnsItsRelease){
    const WidgetState original = prepare(1u, { 71u, 1u });
    ASSERT_TRUE(m_context.commitFrame(1u));
    ASSERT_TRUE(m_context.input().queue({ .type = InputEventType::PrimaryDown, .position = { 15.0f, 25.0f } }));
    EXPECT_TRUE(m_context.input().process().pointerConsumed);
    EXPECT_EQ(m_context.input().capture(), original.id);
    EXPECT_TRUE(m_context.input().primaryDown());
    prepare(2u, original.root);
    ASSERT_TRUE(m_context.ready());
    m_context.retainRoots(nullptr, 0u);
    EXPECT_TRUE(m_context.states().entries().empty());
    EXPECT_FALSE(m_context.input().capture().valid());
    EXPECT_FALSE(m_context.input().focus().valid());
    EXPECT_TRUE(m_context.input().primaryDown());
    EXPECT_TRUE(m_context.input().wouldConsumePointer({ 150.0f, 80.0f }));
    EXPECT_FALSE(m_context.input().hitTest({ 15.0f, 25.0f }).valid());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_TRUE(m_context.input().targets().empty());
    ASSERT_TRUE(m_context.input().queue({ .type = InputEventType::PrimaryUp, .position = { 15.0f, 25.0f } }));
    EXPECT_TRUE(m_context.input().process().pointerConsumed);
    EXPECT_FALSE(m_context.input().primaryDown());
    EXPECT_TRUE(m_context.input().actions().empty());
    const WidgetState recreated = prepare(3u, original.root);
    EXPECT_EQ(recreated.id, original.id);
    EXPECT_GT(recreated.declarationGeneration, original.declarationGeneration);
}

TEST_F(UiContextTests, ChangingWidgetKindInvalidatesQueuedActivationAndItsTypedLifetime){
    const WidgetState original = prepare(1u, { 81u, 1u });
    ASSERT_TRUE(m_context.commitFrame(1u));
    EXPECT_TRUE(click().pointerConsumed);
    ASSERT_EQ(m_context.input().actions().size(), 1u);
    const WidgetState changed = prepare(2u, original.root, WidgetKind::Checkbox);
    EXPECT_EQ(changed.id, original.id);
    EXPECT_GT(changed.declarationGeneration, original.declarationGeneration);
    EXPECT_EQ(changed.kind, WidgetKind::Checkbox);
    EXPECT_TRUE(m_context.input().actions().empty());
    EXPECT_FALSE(m_context.input().capture().valid());
    EXPECT_FALSE(m_context.input().focus().valid());
    EXPECT_FALSE(m_context.input().hitTest({ 15.0f, 25.0f }).valid());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_TRUE(click().pointerConsumed);
    ASSERT_EQ(m_context.input().actions().size(), 1u);
    EXPECT_EQ(m_context.input().actions()[0u].id.declarationGeneration, changed.declarationGeneration);
    ASSERT_TRUE(m_context.beginFrame(3u));
    ASSERT_TRUE(m_context.beginRoot(original.root));
    const WidgetState* current = m_context.declare("action", WidgetKind::Checkbox);
    ASSERT_NE(current, nullptr);
    EXPECT_EQ(current->declarationGeneration, changed.declarationGeneration);
    EXPECT_TRUE(m_context.takeActivation(*current, true));
    EXPECT_FALSE(m_context.takeActivation(*current, true));
}

TEST_F(UiContextTests, RemovedDeclarationsRecreateWithFreshLifetimeEvenWhenTheirStableIdReturns){
    const WidgetState original = prepare(1u, { 91u, 1u });
    ASSERT_TRUE(m_context.commitFrame(1u));
    EXPECT_TRUE(click().pointerConsumed);
    ASSERT_TRUE(m_context.beginFrame(2u));
    ASSERT_TRUE(m_context.finishFrame());
    EXPECT_TRUE(m_context.states().entries().empty());
    EXPECT_TRUE(m_context.input().actions().empty());
    EXPECT_FALSE(m_context.input().hitTest({ 15.0f, 25.0f }).valid());
    ASSERT_TRUE(m_context.commitFrame(2u));
    const WidgetState recreated = prepare(3u, original.root);
    EXPECT_EQ(recreated.id, original.id);
    EXPECT_GT(recreated.declarationGeneration, original.declarationGeneration);
    ASSERT_TRUE(m_context.commitFrame(3u));
    EXPECT_TRUE(m_context.input().actions().empty());
}

TEST_F(UiContextTests, RootRetentionMatchesLifetimeGenerationAndKeepsOtherRootsActive){
    const WidgetRoot firstRoot{ 101u, 1u };
    const WidgetRoot secondRoot{ 102u, 1u };
    ASSERT_TRUE(m_context.beginFrame(1u));
    ASSERT_TRUE(m_context.beginRoot(firstRoot));
    const WidgetState* first = m_context.declare("action", WidgetKind::Button);
    ASSERT_NE(first, nullptr);
    const WidgetState firstState = *first;
    ASSERT_TRUE(m_context.addTarget(firstState, ButtonTarget()));
    ASSERT_TRUE(m_context.endRoot());
    ASSERT_TRUE(m_context.beginRoot(secondRoot));
    const WidgetState* second = m_context.declare("action", WidgetKind::Button);
    ASSERT_NE(second, nullptr);
    const WidgetState secondState = *second;
    ASSERT_TRUE(m_context.addTarget(secondState, ButtonTarget({ 80.0f, 20.0f, 30.0f, 20.0f })));
    ASSERT_TRUE(m_context.endRoot());
    ASSERT_TRUE(m_context.finishFrame());
    ASSERT_TRUE(m_context.commitFrame(1u));
    const WidgetRoot retained[]{ { firstRoot.value, firstRoot.generation + 1u }, secondRoot };
    m_context.retainRoots(retained, 2u);
    ASSERT_EQ(m_context.states().entries().size(), 1u);
    EXPECT_EQ(m_context.states().entries()[0u].id, secondState.id);
    EXPECT_FALSE(m_context.input().hitTest({ 15.0f, 25.0f }).valid());
    EXPECT_EQ(m_context.input().hitTest({ 85.0f, 25.0f }), secondState.id);
}

TEST_F(UiContextTests, DisabledDeclarationDropsOldFocusAndActionsBeforePublication){
    const WidgetState original = prepare(1u, { 111u, 1u });
    ASSERT_TRUE(m_context.commitFrame(1u));
    EXPECT_TRUE(click().pointerConsumed);
    ASSERT_EQ(m_context.input().actions().size(), 1u);
    ASSERT_TRUE(m_context.beginFrame(2u));
    ASSERT_TRUE(m_context.beginRoot(original.root));
    const WidgetState* state = m_context.declare("action", WidgetKind::Button);
    ASSERT_NE(state, nullptr);
    EXPECT_FALSE(m_context.takeActivation(*state, false));
    EXPECT_TRUE(m_context.input().actions().empty());
    EXPECT_FALSE(m_context.input().focus().valid());
    HitTarget target = ButtonTarget();
    target.enabled = false;
    ASSERT_TRUE(m_context.addTarget(*state, target));
    ASSERT_TRUE(m_context.endRoot());
    ASSERT_TRUE(m_context.finishFrame());
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_FALSE(m_context.input().wouldConsumePointer({ 15.0f, 25.0f }));
    EXPECT_FALSE(click().pointerConsumed);
    EXPECT_TRUE(m_context.input().actions().empty());
}

TEST_F(UiContextTests, AbandonedCandidateClearsInteractionAndCannotReuseItsFrameGeneration){
    const WidgetState original = prepare(1u, { 121u, 1u });
    ASSERT_TRUE(m_context.commitFrame(1u));
    EXPECT_TRUE(click().pointerConsumed);
    prepare(2u, original.root);
    ASSERT_TRUE(m_context.ready());
    m_context.abandonFrame();
    EXPECT_FALSE(m_context.ready());
    EXPECT_EQ(m_context.input().layoutGeneration(), 0u);
    EXPECT_TRUE(m_context.input().actions().empty());
    EXPECT_FALSE(m_context.commitFrame(2u));
    EXPECT_FALSE(m_context.beginFrame(2u));
    EXPECT_FALSE(m_context.beginFrame(0u));
    const WidgetState recreated = prepare(3u, original.root);
    EXPECT_EQ(recreated.id, original.id);
    EXPECT_EQ(recreated.declarationGeneration, original.declarationGeneration);
    ASSERT_TRUE(m_context.commitFrame(3u));
    EXPECT_TRUE(m_context.input().actions().empty());
}

TEST_F(UiContextTests, StaleStateCannotConsumeAReplacementActionOrConsumeOutsideAnActiveBuild){
    const WidgetState original = prepare(1u, { 131u, 1u });
    ASSERT_TRUE(m_context.commitFrame(1u));
    const WidgetState changed = prepare(2u, original.root, WidgetKind::Checkbox);
    ASSERT_TRUE(m_context.commitFrame(2u));
    EXPECT_TRUE(click().pointerConsumed);
    ASSERT_EQ(m_context.input().actions().size(), 1u);
    EXPECT_FALSE(m_context.takeActivation(changed, true));
    EXPECT_EQ(m_context.input().actions().size(), 1u);
    ASSERT_TRUE(m_context.beginFrame(3u));
    ASSERT_TRUE(m_context.beginRoot(original.root));
    const WidgetState* current = m_context.declare("action", WidgetKind::Checkbox);
    ASSERT_NE(current, nullptr);
    EXPECT_FALSE(m_context.takeActivation(original, true));
    EXPECT_EQ(m_context.input().actions().size(), 1u);
    WidgetState staleLifetime = original;
    staleLifetime.lastSeenFrame = 3u;
    staleLifetime.kind = WidgetKind::Checkbox;
    EXPECT_FALSE(m_context.takeActivation(staleLifetime, true));
    EXPECT_EQ(m_context.input().actions().size(), 1u);
    WidgetState wrongRoot = *current;
    ++wrongRoot.root.generation;
    EXPECT_FALSE(m_context.takeActivation(wrongRoot, true));
    EXPECT_EQ(m_context.input().actions().size(), 1u);
    EXPECT_TRUE(m_context.takeActivation(*current, true));
    EXPECT_TRUE(m_context.input().actions().empty());
    ASSERT_TRUE(m_context.addTarget(*current, ButtonTarget()));
    ASSERT_TRUE(m_context.endRoot());
    ASSERT_TRUE(m_context.finishFrame());
    ASSERT_TRUE(m_context.commitFrame(3u));
}

TEST_F(UiContextTests, TargetAdmissionRejectsForeignRootPriorFrameAndReplacedLifetimeSnapshots){
    const WidgetState original = prepare(1u, { 141u, 1u });
    ASSERT_TRUE(m_context.commitFrame(1u));
    ASSERT_TRUE(m_context.beginFrame(2u));
    ASSERT_TRUE(m_context.beginRoot({ 142u, 1u }));
    EXPECT_FALSE(m_context.addTarget(original, ButtonTarget()));
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.finishFrame());
    m_context.abandonFrame();
    ASSERT_TRUE(m_context.beginFrame(3u));
    ASSERT_TRUE(m_context.beginRoot(original.root));
    EXPECT_FALSE(m_context.addTarget(original, ButtonTarget()));
    EXPECT_TRUE(m_context.failed());
    m_context.abandonFrame();
    ASSERT_TRUE(m_context.beginFrame(4u));
    ASSERT_TRUE(m_context.beginRoot(original.root));
    const WidgetState* current = m_context.declare("action", WidgetKind::Checkbox);
    ASSERT_NE(current, nullptr);
    EXPECT_GT(current->declarationGeneration, original.declarationGeneration);
    WidgetState staleLifetime = original;
    staleLifetime.lastSeenFrame = 4u;
    staleLifetime.kind = WidgetKind::Checkbox;
    EXPECT_FALSE(m_context.addTarget(staleLifetime, ButtonTarget()));
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.finishFrame());
    EXPECT_FALSE(m_context.commitFrame(4u));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

