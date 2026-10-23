// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "widget_fixture.h"
#include "widget_id_assertion.h"

#include <global/simdmath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_list_loan_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;
using namespace NWB::UiWidgetTests;

constexpr AStringView s_PanelKey = "panel";
constexpr AStringView s_ListKey = "list";

namespace MutationKind{
    enum Enum : u8{ None, TextSelect, KeySelect, EnabledScroll, RevisionScroll };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Deliberate callback reentrancy verifies that a borrowed const source cannot overwrite an explicit host change.
class CallbackSource final : public IListDataSource{
public:
    explicit CallbackSource(ListState& state)noexcept
        : m_state(state)
    {}


public:
    [[nodiscard]] virtual u64 instanceGeneration()const noexcept override{ return 91u; }

    [[nodiscard]] virtual u64 revision()const noexcept override{ mutate(MutationKind::RevisionScroll); return 1u; }

    [[nodiscard]] virtual u64 rowCount()const noexcept override{ return 50u; }

    [[nodiscard]] virtual u64 key(const u64 index)const noexcept override{
        mutate(MutationKind::KeySelect);
        return index < 50u ? index + 1u : 0u;
    }

    [[nodiscard]] virtual Expected<u64> indexOf(const u64 keyValue)const noexcept override{
        u64 index = 0u;
        if(keyValue == 0u || keyValue > 50u)
            return MakeUnexpected(Failure{});
        index = keyValue - 1u;
        return index;
    }

    [[nodiscard]] virtual Expected<u64> findEnabled(const u64 start, bool)const noexcept override{
        u64 index = 0u;
        if(start >= 50u)
            return MakeUnexpected(Failure{});
        index = start;
        return index;
    }

    [[nodiscard]] virtual StringView text(u64)const noexcept override{
        mutate(MutationKind::TextSelect);
        return "row";
    }

    [[nodiscard]] virtual bool enabled(const u64 index)const noexcept override{
        mutate(MutationKind::EnabledScroll);
        return index < 50u;
    }

    void arm(const MutationKind::Enum kind, const u64 keyValue = 8u)noexcept{
        m_kind = kind;
        m_key = keyValue;
    }

    [[nodiscard]] u64 mutations()const noexcept{ return m_mutations; }


private:
    void mutate(const MutationKind::Enum kind)const noexcept{
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

using AcceptedState = UiWidgetTests::AcceptedInputState<16u>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiListLoanTests : public WidgetFixture{
public:
    UiListLoanTests()
        : m_source(m_state)
    {}


protected:
    [[nodiscard]] bool declareList(const u64 generation){
        if(!begin(generation) || !m_builder.beginPanel(s_PanelKey, { 10.0f, 10.0f, 320.0f, 240.0f }))
            return false;
        ListOptions options;
        options.width = { LayoutSizePolicy::Fixed, 280.0f };
        options.height = { LayoutSizePolicy::Fixed, 120.0f };
        return m_builder.virtualList(s_ListKey, m_source, m_state, options).valid;
    }

    [[nodiscard]] bool acceptFirst(){
        return declareList(1u) && finishPanel() && m_context.commitFrame(1u);
    }

    [[nodiscard]] WidgetId firstRow()const{
        return UiTestSupport::RequireWidgetId(MakeWidgetPartId(UiTestSupport::RequireWidgetId(MakeWidgetId(id(s_ListKey, s_PanelKey), "rows")), 1u));
    }

    [[nodiscard]] AcceptedState accepted()const noexcept{ return UiWidgetTests::CaptureAcceptedInput<16u>(m_context.input()); }

    void expectAccepted(const AcceptedState& saved)const{ UiWidgetTests::ExpectAcceptedInput(m_context.input(), saved); }


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
    const SIMDVectorSingle pointValue = VectorAdd(VectorSet(row->rectangle.x, row->rectangle.y, 0.0f, 0.0f), VectorSet(10.0f, 12.0f, 0.0f, 0.0f));
    const Point point{ VectorGetX(pointValue), VectorGetY(pointValue) };
    EXPECT_TRUE(send({ .type = InputEventType::PrimaryDown, .position = point }).pointerConsumed);
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
    EXPECT_FALSE(ListBehavior::Apply(state, result, source, {}, action));
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
    EXPECT_FALSE(ListBehavior::Apply(state, result, source, {}, action));
    EXPECT_DOUBLE_EQ(state.scrollOffset(), 64.0);
    EXPECT_FALSE(result.valid);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

