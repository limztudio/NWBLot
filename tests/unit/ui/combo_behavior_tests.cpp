// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/widgets/combo.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_combo_behavior_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;

namespace CallbackStage{
    enum Enum : u8{ None, Generation, Revision, Count, Lookup, Key, Enabled };
};

namespace CallbackMutation{
    enum Enum : u8{ Select, Open, Close, Generation, Revision, Count };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ComboSource final : public IListDataSource{
public:
    explicit ComboSource(ComboState* state = nullptr)
        : m_state(state)
    {}


public:
    [[nodiscard]] virtual u64 instanceGeneration()const override{
        mutate(CallbackStage::Generation);
        return generation;
    }

    [[nodiscard]] virtual u64 revision()const override{
        mutate(CallbackStage::Revision);
        return revisionValue;
    }

    [[nodiscard]] virtual u64 rowCount()const override{
        mutate(CallbackStage::Count);
        return count;
    }

    [[nodiscard]] virtual u64 key(const u64 index)const override{
        ++keyCalls;
        mutate(CallbackStage::Key);
        if(index >= count)
            return 0u;
        return reversed ? count - index : index + 1u;
    }

    [[nodiscard]] virtual Expected<u64> indexOf(const u64 keyValue)const override{
        u64 index = 0u;
        ++lookupCalls;
        mutate(CallbackStage::Lookup);
        if(keyValue == 0u || keyValue > count)
            return MakeUnexpected(Failure{});
        index = badLookup ? count : wrongLookup ? 0u : reversed ? count - keyValue : keyValue - 1u;
        return index;
    }

    [[nodiscard]] virtual Expected<u64> findEnabled(const u64 start, const bool reverse)const override{
        u64 index = 0u;
        if(start >= count)
            return MakeUnexpected(Failure{});
        index = start;
        if(index + 1u == disabledKey){
            if(reverse){
                if(index == 0u)
                    return MakeUnexpected(Failure{});
                --index;
            }
            else{
                ++index;
                if(index >= count)
                    return MakeUnexpected(Failure{});
            }
        }
        return index;
    }

    [[nodiscard]] virtual StringView text(u64)const override{ return "row"; }

    [[nodiscard]] virtual bool enabled(const u64 index)const override{
        ++enabledCalls;
        mutate(CallbackStage::Enabled);
        const u64 keyValue = reversed ? count - index : index + 1u;
        return index < count && keyValue != disabledKey;
    }

    void arm(const CallbackStage::Enum stage, const CallbackMutation::Enum mutation, const u64 selected = 8u){
        m_stage = stage;
        m_mutation = mutation;
        m_selected = selected;
    }


private:
    void mutate(const CallbackStage::Enum stage)const{
        if(m_stage != stage)
            return;
        m_stage = CallbackStage::None;
        switch(m_mutation){
        case CallbackMutation::Select:
            if(m_state)
                m_state->select(m_selected);
            break;
        case CallbackMutation::Open:
            if(m_state)
                m_state->open();
            break;
        case CallbackMutation::Close:
            if(m_state)
                m_state->close();
            break;
        case CallbackMutation::Generation:
            ++generation;
            break;
        case CallbackMutation::Revision:
            ++revisionValue;
            break;
        case CallbackMutation::Count:
            if(count != 0u)
                --count;
            break;
        }
    }


public:
    mutable u64 generation = 51u;
    mutable u64 revisionValue = 1u;
    mutable u64 count = 10u;
    u64 disabledKey = 0u;
    bool reversed = false;
    bool badLookup = false;
    bool wrongLookup = false;
    mutable u64 lookupCalls = 0u;
    mutable u64 keyCalls = 0u;
    mutable u64 enabledCalls = 0u;


private:
    ComboState* m_state = nullptr;
    mutable CallbackStage::Enum m_stage = CallbackStage::None;
    CallbackMutation::Enum m_mutation = CallbackMutation::Select;
    u64 m_selected = 8u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool Navigate(ComboState& state, const ComboSource& source, const ControlActionKind::Enum kind){
    ListState& preview = ComboBehavior::Preview(state);
    if(!ListBehavior::Reconcile(preview, source))
        return false;
    ListOptions options;
    options.selectOnNavigate = false;
    ControlAction action;
    action.kind = kind;
    action.control = { preview.inputGeneration(), source.instanceGeneration(), source.revision() };
    ListResult result;
    return ListBehavior::Apply(preview, source, options, action, result);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(UiComboBehaviorTests, FirstBindingPreservesAnExplicitlyOpenStateAndSameBindingIsStable){
    ComboState state;
    state.select(4u);
    state.open();
    const u64 inputGeneration = state.inputGeneration();
    const u64 previewGeneration = state.listState().inputGeneration();
    ASSERT_TRUE(ComboBehavior::Bind(state, WidgetId{ 201u }, 17u));
    EXPECT_TRUE(state.isOpen());
    EXPECT_EQ(state.selectedKey(), 4u);
    EXPECT_EQ(state.inputGeneration(), inputGeneration);
    EXPECT_EQ(state.listState().inputGeneration(), previewGeneration);
    ASSERT_TRUE(ComboBehavior::Bind(state, WidgetId{ 201u }, 17u));
    EXPECT_TRUE(state.isOpen());
    EXPECT_EQ(state.listState().inputGeneration(), previewGeneration);
}

TEST(UiComboBehaviorTests, ARecreatedDeclarationCancelsPreviewAndPreservesTheCommittedSelection){
    ComboSource source;
    ComboState state;
    state.select(4u);
    ASSERT_TRUE(ComboBehavior::Bind(state, WidgetId{ 201u }, 17u));
    ASSERT_TRUE(ComboBehavior::Reconcile(state, source));
    ComboBehavior::Open(state);
    ASSERT_TRUE(Navigate(state, source, ControlActionKind::Down));
    const u64 inputGeneration = state.inputGeneration();
    const u64 previewGeneration = state.listState().inputGeneration();
    ASSERT_TRUE(ComboBehavior::Bind(state, WidgetId{ 201u }, 18u));
    EXPECT_FALSE(state.isOpen());
    EXPECT_EQ(state.selectedKey(), 4u);
    EXPECT_EQ(state.listState().cursorKey(), 4u);
    EXPECT_NE(state.listState().inputGeneration(), previewGeneration);
    EXPECT_EQ(state.inputGeneration(), inputGeneration);
}

TEST(UiComboBehaviorTests, MovingStateToAnotherOwnerClosesAndEachOwnerSwitchRetiresThePreviewLifetime){
    ComboState state;
    state.select(4u);
    ASSERT_TRUE(ComboBehavior::Bind(state, WidgetId{ 201u }, 17u));
    state.open();
    const u64 previewGeneration = state.listState().inputGeneration();
    ASSERT_TRUE(ComboBehavior::Bind(state, WidgetId{ 202u }, 17u));
    EXPECT_FALSE(state.isOpen());
    EXPECT_EQ(state.selectedKey(), 4u);
    EXPECT_NE(state.listState().inputGeneration(), previewGeneration);
    state.open();
    const u64 returnedPreview = state.listState().inputGeneration();
    ASSERT_TRUE(ComboBehavior::Bind(state, WidgetId{ 201u }, 17u));
    EXPECT_FALSE(state.isOpen());
    EXPECT_EQ(state.selectedKey(), 4u);
    EXPECT_NE(state.listState().inputGeneration(), returnedPreview);
}

TEST(UiComboBehaviorTests, InvalidBindingFailsAtomicallyAndDoesNotReplaceTheExistingOwner){
    ComboState state;
    state.select(4u);
    ASSERT_TRUE(ComboBehavior::Bind(state, WidgetId{ 201u }, 17u));
    state.open();
    const u64 previewGeneration = state.listState().inputGeneration();
    EXPECT_FALSE(ComboBehavior::Bind(state, {}, 18u));
    EXPECT_FALSE(ComboBehavior::Bind(state, WidgetId{ 202u }, 0u));
    ASSERT_TRUE(ComboBehavior::Bind(state, WidgetId{ 201u }, 17u));
    EXPECT_TRUE(state.isOpen());
    EXPECT_EQ(state.selectedKey(), 4u);
    EXPECT_EQ(state.listState().inputGeneration(), previewGeneration);
}

TEST(UiComboBehaviorTests, MissingAndDisabledInitialSelectionClearWithoutChoosingAnotherRow){
    ComboSource source;
    ComboState missing;
    missing.select(99u);
    ASSERT_TRUE(ComboBehavior::Reconcile(missing, source));
    EXPECT_EQ(missing.selectedKey(), 0u);
    ComboState disabled;
    disabled.select(5u);
    source.disabledKey = 5u;
    ASSERT_TRUE(ComboBehavior::Reconcile(disabled, source));
    EXPECT_EQ(disabled.selectedKey(), 0u);
}

TEST(UiComboBehaviorTests, ReorderPreservesCommittedSelectionAndOpenPreviewCursor){
    ComboSource source;
    ComboState state;
    state.select(4u);
    ASSERT_TRUE(ComboBehavior::Reconcile(state, source));
    ComboBehavior::Open(state);
    ASSERT_TRUE(Navigate(state, source, ControlActionKind::Down));
    ASSERT_EQ(state.listState().cursorKey(), 5u);
    source.reversed = true;
    ++source.revisionValue;
    ASSERT_TRUE(ComboBehavior::Reconcile(state, source));
    ASSERT_TRUE(ListBehavior::Reconcile(ComboBehavior::Preview(state), source));
    EXPECT_EQ(state.selectedKey(), 4u);
    EXPECT_EQ(state.listState().cursorKey(), 5u);
    EXPECT_TRUE(state.isOpen());
}

TEST(UiComboBehaviorTests, CommittingTheCurrentSelectionIsStillAnActivationAndCloses){
    ComboSource source;
    ComboState state;
    state.select(4u);
    ASSERT_TRUE(ComboBehavior::Reconcile(state, source));
    ComboBehavior::Open(state);
    EXPECT_TRUE(ComboBehavior::Commit(state, source, 4u));
    EXPECT_EQ(state.selectedKey(), 4u);
    EXPECT_FALSE(state.isOpen());
}

TEST(UiComboBehaviorTests, ZeroMissingAndDisabledCommitFailWithoutClosingOrChangingSelection){
    ComboSource source;
    ComboState state;
    state.select(4u);
    ASSERT_TRUE(ComboBehavior::Reconcile(state, source));
    ComboBehavior::Open(state);
    source.disabledKey = 5u;
    EXPECT_FALSE(ComboBehavior::Commit(state, source, 0u));
    EXPECT_FALSE(ComboBehavior::Commit(state, source, 99u));
    EXPECT_FALSE(ComboBehavior::Commit(state, source, 5u));
    EXPECT_EQ(state.selectedKey(), 4u);
    EXPECT_EQ(state.listState().cursorKey(), 4u);
    EXPECT_TRUE(state.isOpen());
}

TEST(UiComboBehaviorTests, ClosedAndUnboundStateRejectCommit){
    ComboSource source;
    ComboState state;
    state.select(4u);
    EXPECT_FALSE(ComboBehavior::Commit(state, source, 5u));
    ComboBehavior::Open(state);
    EXPECT_FALSE(ComboBehavior::Commit(state, source, 5u));
    ASSERT_TRUE(ComboBehavior::Reconcile(state, source));
    ComboBehavior::Close(state);
    EXPECT_FALSE(ComboBehavior::Commit(state, source, 5u));
    EXPECT_EQ(state.selectedKey(), 4u);
}

TEST(UiComboBehaviorTests, ExplicitSelectRenewsInputEvenWhenUnchangedAndCancelsPreview){
    ComboSource source;
    ComboState state;
    state.select(4u);
    ASSERT_TRUE(ComboBehavior::Reconcile(state, source));
    ComboBehavior::Open(state);
    ASSERT_TRUE(Navigate(state, source, ControlActionKind::Down));
    const u64 inputGeneration = state.inputGeneration();
    state.select(4u);
    EXPECT_NE(state.inputGeneration(), inputGeneration);
    EXPECT_EQ(state.selectedKey(), 4u);
    EXPECT_EQ(state.listState().cursorKey(), 4u);
    EXPECT_FALSE(state.isOpen());
}

TEST(UiComboBehaviorTests, ExplicitOpenRenewsFieldAndPreviewLifetimesOnEveryCall){
    ComboState state;
    state.select(4u);
    const u64 inputGeneration = state.inputGeneration();
    const u64 previewGeneration = state.listState().inputGeneration();
    state.open();
    EXPECT_NE(state.inputGeneration(), inputGeneration);
    EXPECT_NE(state.listState().inputGeneration(), previewGeneration);
    const u64 reopenedInput = state.inputGeneration();
    const u64 reopenedPreview = state.listState().inputGeneration();
    state.open();
    EXPECT_NE(state.inputGeneration(), reopenedInput);
    EXPECT_NE(state.listState().inputGeneration(), reopenedPreview);
    EXPECT_EQ(state.listState().cursorKey(), 4u);
    EXPECT_TRUE(state.isOpen());
}

TEST(UiComboBehaviorTests, ExplicitCloseRenewsFieldAndPreviewEvenWhenAlreadyClosed){
    ComboState state;
    state.select(4u);
    const u64 inputGeneration = state.inputGeneration();
    const u64 previewGeneration = state.listState().inputGeneration();
    state.close();
    EXPECT_NE(state.inputGeneration(), inputGeneration);
    EXPECT_NE(state.listState().inputGeneration(), previewGeneration);
    EXPECT_EQ(state.selectedKey(), 4u);
    EXPECT_FALSE(state.isOpen());
}

TEST(UiComboBehaviorTests, ReopenRestoresCommittedCursorAndRetiresPreviousPreviewAction){
    ComboSource source;
    ComboState state;
    state.select(4u);
    ASSERT_TRUE(ComboBehavior::Reconcile(state, source));
    ComboBehavior::Open(state);
    ASSERT_TRUE(Navigate(state, source, ControlActionKind::Down));
    const u64 inputGeneration = state.inputGeneration();
    const u64 previewGeneration = state.listState().inputGeneration();
    ComboBehavior::Open(state);
    EXPECT_EQ(state.listState().cursorKey(), 4u);
    EXPECT_NE(state.listState().inputGeneration(), previewGeneration);
    EXPECT_EQ(state.inputGeneration(), inputGeneration);
    EXPECT_TRUE(state.isOpen());
}

TEST(UiComboBehaviorTests, RemovedOrDisabledCommittedKeyClearsOnRevisionWithoutCommittingPreview){
    ComboSource source;
    ComboState state;
    state.select(4u);
    ASSERT_TRUE(ComboBehavior::Reconcile(state, source));
    ComboBehavior::Open(state);
    ASSERT_TRUE(Navigate(state, source, ControlActionKind::Down));
    source.disabledKey = 4u;
    ++source.revisionValue;
    ASSERT_TRUE(ComboBehavior::Reconcile(state, source));
    ASSERT_TRUE(ListBehavior::Reconcile(ComboBehavior::Preview(state), source));
    EXPECT_EQ(state.selectedKey(), 0u);
    EXPECT_EQ(state.listState().cursorKey(), 5u);
    EXPECT_TRUE(state.isOpen());
    ComboBehavior::Close(state);
    EXPECT_EQ(state.listState().cursorKey(), 0u);
    state.select(5u);
    source.count = 4u;
    ++source.revisionValue;
    ASSERT_TRUE(ComboBehavior::Reconcile(state, source));
    EXPECT_EQ(state.selectedKey(), 0u);
}

TEST(UiComboBehaviorTests, ReplacingEstablishedSourceClosesAndClearsSelectionPreviewAndScroll){
    ComboSource source;
    ComboState state;
    state.select(10u);
    ASSERT_TRUE(ComboBehavior::Reconcile(state, source));
    ComboBehavior::Open(state);
    ASSERT_TRUE(ListBehavior::Reconcile(ComboBehavior::Preview(state), source));
    ASSERT_TRUE(ListBehavior::EnsureCursor(ComboBehavior::Preview(state), source, 32.0f, 64.0));
    ASSERT_GT(state.listState().scrollOffset(), 0.0);
    const u64 inputGeneration = state.inputGeneration();
    ++source.generation;
    ASSERT_TRUE(ComboBehavior::Reconcile(state, source));
    EXPECT_EQ(state.selectedKey(), 0u);
    EXPECT_EQ(state.listState().selectedKey(), 0u);
    EXPECT_EQ(state.listState().cursorKey(), 0u);
    EXPECT_DOUBLE_EQ(state.listState().scrollOffset(), 0.0);
    EXPECT_EQ(state.inputGeneration(), inputGeneration);
    EXPECT_FALSE(state.isOpen());
}

TEST(UiComboBehaviorTests, EmptyDatasetClearsSelectionAndDoesNotInventACommit){
    ComboSource source;
    ComboState state;
    state.select(4u);
    ASSERT_TRUE(ComboBehavior::Reconcile(state, source));
    ComboBehavior::Open(state);
    source.count = 0u;
    ++source.revisionValue;
    ASSERT_TRUE(ComboBehavior::Reconcile(state, source));
    ASSERT_TRUE(Navigate(state, source, ControlActionKind::Down));
    EXPECT_EQ(state.selectedKey(), 0u);
    EXPECT_EQ(state.listState().cursorKey(), 0u);
    EXPECT_FALSE(ComboBehavior::Commit(state, source, 0u));
    EXPECT_TRUE(state.isOpen());
}

TEST(UiComboBehaviorTests, InvalidIdentitiesAndMalformedLookupLeaveExistingStateUnchanged){
    ComboSource source;
    ComboState state;
    state.select(4u);
    ASSERT_TRUE(ComboBehavior::Reconcile(state, source));
    ComboBehavior::Open(state);
    source.generation = 0u;
    EXPECT_FALSE(ComboBehavior::Reconcile(state, source));
    source.generation = 51u;
    source.revisionValue = 0u;
    EXPECT_FALSE(ComboBehavior::Reconcile(state, source));
    source.revisionValue = 1u;
    source.badLookup = true;
    EXPECT_FALSE(ComboBehavior::Reconcile(state, source));
    EXPECT_FALSE(ComboBehavior::Commit(state, source, 5u));
    source.badLookup = false;
    source.wrongLookup = true;
    EXPECT_FALSE(ComboBehavior::Reconcile(state, source));
    EXPECT_FALSE(ComboBehavior::Commit(state, source, 5u));
    EXPECT_EQ(state.selectedKey(), 4u);
    EXPECT_EQ(state.listState().cursorKey(), 4u);
    EXPECT_TRUE(state.isOpen());
}

TEST(UiComboBehaviorTests, CommitRequiresReconciledRevisionAndRejectsSourceReplacement){
    ComboSource source;
    ComboState state;
    state.select(4u);
    ASSERT_TRUE(ComboBehavior::Reconcile(state, source));
    ComboBehavior::Open(state);
    ++source.revisionValue;
    EXPECT_FALSE(ComboBehavior::Commit(state, source, 5u));
    ASSERT_TRUE(ComboBehavior::Reconcile(state, source));
    ++source.generation;
    EXPECT_FALSE(ComboBehavior::Commit(state, source, 5u));
    EXPECT_EQ(state.selectedKey(), 4u);
    EXPECT_TRUE(state.isOpen());
}

TEST(UiComboBehaviorTests, RevisionAndGenerationChangingDuringReconcileFailWithoutPartialMutation){
    for(const CallbackMutation::Enum mutation : { CallbackMutation::Revision, CallbackMutation::Generation }){
        ComboSource source;
        ComboState state;
        state.select(4u);
        ASSERT_TRUE(ComboBehavior::Reconcile(state, source));
        ComboBehavior::Open(state);
        source.arm(CallbackStage::Lookup, mutation);
        EXPECT_FALSE(ComboBehavior::Reconcile(state, source));
        EXPECT_EQ(state.selectedKey(), 4u);
        EXPECT_TRUE(state.isOpen());
    }
}

TEST(UiComboBehaviorTests, RowCountChangingWithoutRevisionDuringReconcileFailsWithoutPartialMutation){
    ComboSource source;
    ComboState state;
    state.select(4u);
    ASSERT_TRUE(ComboBehavior::Reconcile(state, source));
    ComboBehavior::Open(state);
    source.arm(CallbackStage::Lookup, CallbackMutation::Count);
    EXPECT_FALSE(ComboBehavior::Reconcile(state, source));
    EXPECT_EQ(state.selectedKey(), 4u);
    EXPECT_TRUE(state.isOpen());
}

TEST(UiComboBehaviorTests, SourceChangesDuringCommitLeaveThePopupAndCommittedKeyUnchanged){
    for(const CallbackMutation::Enum mutation : {
        CallbackMutation::Revision, CallbackMutation::Generation, CallbackMutation::Count
    }){
        ComboSource source;
        ComboState state;
        state.select(4u);
        ASSERT_TRUE(ComboBehavior::Reconcile(state, source));
        ComboBehavior::Open(state);
        source.arm(CallbackStage::Lookup, mutation);
        EXPECT_FALSE(ComboBehavior::Commit(state, source, 5u));
        EXPECT_EQ(state.selectedKey(), 4u);
        EXPECT_EQ(state.listState().cursorKey(), 4u);
        EXPECT_TRUE(state.isOpen());
    }
}

TEST(UiComboBehaviorTests, ReconcileCannotOverwriteAnExplicitSelectionFromAnySourceLoan){
    for(const CallbackStage::Enum stage : {
        CallbackStage::Generation, CallbackStage::Revision, CallbackStage::Count,
        CallbackStage::Lookup, CallbackStage::Key, CallbackStage::Enabled
    }){
        ComboState state;
        ComboSource source(&state);
        state.select(4u);
        ASSERT_TRUE(ComboBehavior::Reconcile(state, source));
        ComboBehavior::Open(state);
        source.arm(stage, CallbackMutation::Select, 8u);
        EXPECT_FALSE(ComboBehavior::Reconcile(state, source));
        EXPECT_EQ(state.selectedKey(), 8u);
        EXPECT_EQ(state.listState().cursorKey(), 8u);
        EXPECT_FALSE(state.isOpen());
    }
}

TEST(UiComboBehaviorTests, CommitCannotOverwriteAnExplicitSelectionFromAnySourceLoan){
    for(const CallbackStage::Enum stage : {
        CallbackStage::Generation, CallbackStage::Revision, CallbackStage::Count,
        CallbackStage::Lookup, CallbackStage::Key, CallbackStage::Enabled
    }){
        ComboState state;
        ComboSource source(&state);
        state.select(4u);
        ASSERT_TRUE(ComboBehavior::Reconcile(state, source));
        ComboBehavior::Open(state);
        source.arm(stage, CallbackMutation::Select, 8u);
        EXPECT_FALSE(ComboBehavior::Commit(state, source, 5u));
        EXPECT_EQ(state.selectedKey(), 8u);
        EXPECT_EQ(state.listState().cursorKey(), 8u);
        EXPECT_FALSE(state.isOpen());
    }
}

TEST(UiComboBehaviorTests, CallbackCloseAndReopenPreserveTheExplicitPopupLifetime){
    for(const CallbackMutation::Enum mutation : { CallbackMutation::Open, CallbackMutation::Close }){
        ComboState state;
        ComboSource source(&state);
        state.select(4u);
        ASSERT_TRUE(ComboBehavior::Reconcile(state, source));
        ComboBehavior::Open(state);
        const u64 inputGeneration = state.inputGeneration();
        source.arm(CallbackStage::Enabled, mutation);
        EXPECT_FALSE(ComboBehavior::Commit(state, source, 5u));
        EXPECT_NE(state.inputGeneration(), inputGeneration);
        EXPECT_EQ(state.selectedKey(), 4u);
        EXPECT_EQ(state.listState().cursorKey(), 4u);
        EXPECT_EQ(state.isOpen(), mutation == CallbackMutation::Open);
    }
}

TEST(UiComboBehaviorTests, SameValueCallbackSelectionStillRejectsTheOutstandingCommit){
    ComboState state;
    ComboSource source(&state);
    state.select(4u);
    ASSERT_TRUE(ComboBehavior::Reconcile(state, source));
    ComboBehavior::Open(state);
    const u64 inputGeneration = state.inputGeneration();
    source.arm(CallbackStage::Key, CallbackMutation::Select, 4u);
    EXPECT_FALSE(ComboBehavior::Commit(state, source, 5u));
    EXPECT_NE(state.inputGeneration(), inputGeneration);
    EXPECT_EQ(state.selectedKey(), 4u);
    EXPECT_FALSE(state.isOpen());
}

TEST(UiComboBehaviorTests, HundredThousandRowsResolveCommittedKeyWithBoundedSourceCalls){
    ComboSource source;
    source.count = 100000u;
    ComboState state;
    state.select(99999u);
    ASSERT_TRUE(ComboBehavior::Reconcile(state, source));
    ComboBehavior::Open(state);
    source.lookupCalls = 0u;
    source.keyCalls = 0u;
    source.enabledCalls = 0u;
    ASSERT_TRUE(ComboBehavior::Commit(state, source, 100000u));
    EXPECT_EQ(state.selectedKey(), 100000u);
    EXPECT_EQ(source.lookupCalls, 1u);
    EXPECT_EQ(source.keyCalls, 1u);
    EXPECT_EQ(source.enabledCalls, 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

