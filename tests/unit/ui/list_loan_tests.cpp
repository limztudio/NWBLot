// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "widget_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_list_loan_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;
using namespace NWB::UiWidgetTests;

namespace MutationKind{
    enum Enum : u8{ None, TextSelect, KeySelect, EnabledScroll, RevisionScroll };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Deliberate callback reentrancy verifies that a borrowed const source cannot overwrite an explicit host change.
class CallbackSource final : public IListDataSource{
public:
    explicit CallbackSource(ListState& state)
        : m_state(state)
    {}


public:
    [[nodiscard]] virtual u64 instanceGeneration()const override{ return 91u; }
    [[nodiscard]] virtual u64 revision()const override{ mutate(MutationKind::RevisionScroll); return 1u; }
    [[nodiscard]] virtual u64 rowCount()const override{ return 50u; }

    [[nodiscard]] virtual u64 key(const u64 index)const override{
        mutate(MutationKind::KeySelect);
        return index < 50u ? index + 1u : 0u;
    }

    [[nodiscard]] virtual bool indexOf(const u64 keyValue, u64& index)const override{
        if(keyValue == 0u || keyValue > 50u)
            return false;
        index = keyValue - 1u;
        return true;
    }

    [[nodiscard]] virtual bool findEnabled(const u64 start, bool, u64& index)const override{
        if(start >= 50u)
            return false;
        index = start;
        return true;
    }

    [[nodiscard]] virtual StringView text(u64)const override{
        mutate(MutationKind::TextSelect);
        return "row";
    }

    [[nodiscard]] virtual bool enabled(const u64 index)const override{
        mutate(MutationKind::EnabledScroll);
        return index < 50u;
    }

    void arm(const MutationKind::Enum kind, const u64 keyValue = 8u){
        m_kind = kind;
        m_key = keyValue;
    }

    [[nodiscard]] u64 mutations()const{ return m_mutations; }


private:
    void mutate(const MutationKind::Enum kind)const{
        if(m_kind != kind)
            return;
        m_kind = MutationKind::None;
        ++m_mutations;
        if(kind == MutationKind::EnabledScroll || kind == MutationKind::RevisionScroll){
            if(!m_state.scrollTo(64.0))
                ADD_FAILURE() << "callback scroll mutation failed";
        }
        else
            m_state.select(m_key);
    }


private:
    ListState& m_state;
    mutable MutationKind::Enum m_kind = MutationKind::None;
    u64 m_key = 8u;
    mutable u64 m_mutations = 0u;
};

struct AcceptedState{
    Array<HitTarget, 16u> targets{};
    usize count = 0u;
    u64 layoutGeneration = 0u;
    WidgetId capture;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiListLoanTests : public WidgetFixture{
public:
    UiListLoanTests()
        : m_source(m_state)
    {}


protected:
    [[nodiscard]] bool declareList(const u64 generation){
        if(!begin(generation) || !m_builder.beginPanel("panel", { 10.0f, 10.0f, 320.0f, 240.0f }))
            return false;
        ListOptions options;
        options.width = { LayoutSizePolicy::Fixed, 280.0f };
        options.height = { LayoutSizePolicy::Fixed, 120.0f };
        options.rowHeight = 24.0f;
        return m_builder.virtualList("list", m_source, m_state, options).valid;
    }

    [[nodiscard]] bool acceptFirst(){
        return declareList(1u) && finishPanel() && m_context.commitFrame(1u);
    }

    [[nodiscard]] WidgetId firstRow()const{
        return MakeWidgetPartId(MakeWidgetId(id("list", "panel"), "rows"), 1u);
    }

    [[nodiscard]] AcceptedState accepted()const{
        AcceptedState saved;
        saved.count = m_context.input().targets().size();
        saved.layoutGeneration = m_context.input().layoutGeneration();
        saved.capture = m_context.input().capture();
        for(usize index = 0u; index < Min(saved.count, saved.targets.size()); ++index)
            saved.targets[index] = m_context.input().targets()[index];
        return saved;
    }

    void expectAccepted(const AcceptedState& saved)const{
        EXPECT_EQ(m_context.input().layoutGeneration(), saved.layoutGeneration);
        EXPECT_EQ(m_context.input().capture(), saved.capture);
        ASSERT_EQ(m_context.input().targets().size(), saved.count);
        ASSERT_LE(saved.count, saved.targets.size());
        for(usize index = 0u; index < saved.count; ++index){
            const HitTarget& before = saved.targets[index];
            const HitTarget& after = m_context.input().targets()[index];
            EXPECT_EQ(after.id, before.id);
            EXPECT_EQ(after.declarationGeneration, before.declarationGeneration);
            EXPECT_EQ(after.control, before.control);
            EXPECT_EQ(after.owner, before.owner);
            EXPECT_EQ(after.ownerDeclarationGeneration, before.ownerDeclarationGeneration);
            EXPECT_EQ(after.enabled, before.enabled);
            NWB::UiWidgetTests::ExpectRect(after.rectangle, before.rectangle);
            NWB::UiWidgetTests::ExpectRect(after.clip, before.clip);
        }
    }


protected:
    ListState m_state;
    CallbackSource m_source;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiListLoanTests, TextCallbackSelectionRejectsDeferredPaintWithoutReplacingAcceptedTargets){
    ASSERT_TRUE(acceptFirst());
    const AcceptedState displayed = accepted();
    ASSERT_TRUE(declareList(2u));
    const u64 inputGeneration = m_state.inputGeneration();
    m_source.arm(MutationKind::TextSelect, 8u);
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.ready());
    EXPECT_FALSE(m_context.commitFrame(2u));
    EXPECT_EQ(m_source.mutations(), 1u);
    EXPECT_NE(m_state.inputGeneration(), inputGeneration);
    EXPECT_EQ(m_state.selectedKey(), 8u);
    expectAccepted(displayed);
}

TEST_F(UiListLoanTests, SameValueKeyCallbackRejectsTheLoanWhileDisplayedCaptureStaysUnchanged){
    ASSERT_TRUE(acceptFirst());
    const HitTarget* row = target(firstRow());
    ASSERT_NE(row, nullptr);
    const Point point{ row->rectangle.x + 10.0f, row->rectangle.y + 12.0f };
    EXPECT_TRUE(send({ InputEventType::PrimaryDown, point }).pointerConsumed);
    ASSERT_EQ(m_context.input().capture(), firstRow());
    const AcceptedState displayed = accepted();
    ASSERT_TRUE(declareList(2u));
    const u64 inputGeneration = m_state.inputGeneration();
    m_source.arm(MutationKind::KeySelect, 0u);
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.commitFrame(2u));
    EXPECT_EQ(m_state.selectedKey(), 0u);
    EXPECT_NE(m_state.inputGeneration(), inputGeneration);
    expectAccepted(displayed);
}

TEST_F(UiListLoanTests, EnabledCallbackScrollIsPreservedWhenEnsureCursorRejectsTheChangedEpoch){
    m_state.select(5u);
    ASSERT_TRUE(acceptFirst());
    // Same-value application selection requests cursor visibility and fences the previous input token before painting.
    m_state.select(5u);
    ASSERT_TRUE(declareList(2u));
    const AcceptedState displayed = accepted();
    const u64 inputGeneration = m_state.inputGeneration();
    m_source.arm(MutationKind::EnabledScroll);
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.commitFrame(2u));
    EXPECT_NE(m_state.inputGeneration(), inputGeneration);
    EXPECT_DOUBLE_EQ(m_state.scrollOffset(), 64.0);
    expectAccepted(displayed);
}

TEST(UiListLoanBehaviorTests, ReconcileAndApplyPreserveExplicitChangesMadeBySourceCallbacks){
    ListState state;
    CallbackSource source(state);
    state.select(2u);
    source.arm(MutationKind::KeySelect, 8u);
    EXPECT_FALSE(ListBehavior::Reconcile(state, source));
    EXPECT_EQ(state.selectedKey(), 8u);
    EXPECT_EQ(state.cursorKey(), 8u);
    ASSERT_TRUE(ListBehavior::Reconcile(state, source));
    ControlAction action;
    action.control = { state.inputGeneration(), source.instanceGeneration(), source.revision() };
    action.kind = ControlActionKind::Down;
    source.arm(MutationKind::KeySelect, 3u);
    ListResult result;
    EXPECT_FALSE(ListBehavior::Apply(state, source, {}, action, result));
    EXPECT_EQ(state.selectedKey(), 3u);
    EXPECT_EQ(state.cursorKey(), 3u);
    EXPECT_FALSE(result.valid);
    EXPECT_FALSE(result.selectionChanged);
    action.control.instanceGeneration = state.inputGeneration();
    action.kind = ControlActionKind::Wheel;
    action.delta = -1.0;
    action.step = 24.0;
    action.maximum = 300.0;
    source.arm(MutationKind::RevisionScroll);
    EXPECT_FALSE(ListBehavior::Apply(state, source, {}, action, result));
    EXPECT_DOUBLE_EQ(state.scrollOffset(), 64.0);
    EXPECT_FALSE(result.valid);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

