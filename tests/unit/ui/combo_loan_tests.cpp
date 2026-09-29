// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "combo_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_combo_loan_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;
using namespace NWB::UiComboTests;

namespace MutationKind{
    enum Enum : u8{ None, RevisionSelect, KeySelect, TextSelect, EnabledOpen, TextRevision };
};


// Source callbacks can reenter host code; failed loans must retain that explicit mutation.
class CallbackSource final : public ComboSource{
public:
    explicit CallbackSource(ComboState& state)
        : m_state(state)
    {}


public:
    [[nodiscard]] virtual u64 revision()const override{
        mutate(MutationKind::RevisionSelect);
        return ComboSource::revision() + m_revisionDelta;
    }

    [[nodiscard]] virtual u64 key(const u64 index)const override{
        mutate(MutationKind::KeySelect);
        return ComboSource::key(index);
    }

    [[nodiscard]] virtual StringView text(const u64 index)const override{
        mutate(MutationKind::TextSelect);
        if(m_kind == MutationKind::TextRevision){
            m_kind = MutationKind::None;
            ++m_mutations;
            ++m_revisionDelta;
        }
        return ComboSource::text(index);
    }

    [[nodiscard]] virtual bool enabled(const u64 index)const override{
        mutate(MutationKind::EnabledOpen);
        return ComboSource::enabled(index);
    }

    void arm(const MutationKind::Enum kind, const u64 key = 8u){
        m_kind = kind;
        m_key = key;
    }

    [[nodiscard]] u64 mutations()const{ return m_mutations; }


private:
    void mutate(const MutationKind::Enum kind)const{
        if(m_kind != kind)
            return;
        m_kind = MutationKind::None;
        ++m_mutations;
        if(kind == MutationKind::EnabledOpen)
            m_state.open();
        else
            m_state.select(m_key);
    }


private:
    ComboState& m_state;
    mutable MutationKind::Enum m_kind = MutationKind::None;
    u64 m_key = 8u;
    mutable u64 m_mutations = 0u;
    mutable u64 m_revisionDelta = 0u;
};

struct AcceptedState{
    Array<HitTarget, 32u> targets{};
    usize count = 0u;
    u64 layoutGeneration = 0u;
    WidgetId capture;
};

class ForeignRevisionSource final : public ComboSource{
public:
    explicit ForeignRevisionSource(ComboSource& source)
        : m_foreign(source)
    {}


public:
    [[nodiscard]] virtual StringView text(const u64 index)const override{
        if(m_armed){
            m_armed = false;
            ++m_foreign.contentRevision;
            ++m_mutations;
        }
        return ComboSource::text(index);
    }

    void arm(){ m_armed = true; }
    [[nodiscard]] u64 mutations()const{ return m_mutations; }


private:
    ComboSource& m_foreign;
    mutable bool m_armed = false;
    mutable u64 m_mutations = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiComboLoanTests : public ComboFixture{
public:
    UiComboLoanTests()
        : m_callbacks(m_state)
    {}


protected:
    [[nodiscard]] bool declareCallbacks(const u64 generation){
        return declareSource(generation, m_callbacks, m_state);
    }

    [[nodiscard]] bool acceptCallbacks(const u64 generation){
        return declareCallbacks(generation) && finishPanel() && m_context.commitFrame(generation);
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
            ExpectRect(after.rectangle, before.rectangle);
            ExpectRect(after.clip, before.clip);
        }
    }


protected:
    CallbackSource m_callbacks;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiComboLoanTests, SameValueApplicationSelectionAfterDeclarationRejectsDeferredPaint){
    m_state.select(1u);
    ASSERT_TRUE(accept(1u));
    const AcceptedState displayed = accepted();
    ASSERT_TRUE(declareCombo(2u));
    const u64 inputGeneration = m_state.inputGeneration();
    m_state.select(1u);
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.ready());
    EXPECT_FALSE(m_context.commitFrame(2u));
    EXPECT_NE(m_state.inputGeneration(), inputGeneration);
    EXPECT_EQ(m_state.selectedKey(), 1u);
    expectAccepted(displayed);
}

TEST_F(UiComboLoanTests, ClosingBetweenDeclarationAndEndKeepsTheApplicationClose){
    m_state.select(1u);
    ASSERT_TRUE(accept(1u));
    ASSERT_TRUE(openByPointer(2u));
    ASSERT_TRUE(declareCombo(3u));
    const AcceptedState displayed = accepted();
    m_state.close();
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.commitFrame(3u));
    EXPECT_FALSE(m_state.isOpen());
    EXPECT_EQ(m_state.selectedKey(), 1u);
    expectAccepted(displayed);
}

TEST_F(UiComboLoanTests, ReopeningBetweenDeclarationAndEndCannotPublishTheOldPopupLifetime){
    m_state.select(1u);
    ASSERT_TRUE(accept(1u));
    ASSERT_TRUE(openByPointer(2u));
    ASSERT_TRUE(declareCombo(3u));
    const AcceptedState displayed = accepted();
    const u64 inputGeneration = m_state.inputGeneration();
    m_state.open();
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.commitFrame(3u));
    EXPECT_TRUE(m_state.isOpen());
    EXPECT_NE(m_state.inputGeneration(), inputGeneration);
    expectAccepted(displayed);
}

TEST_F(UiComboLoanTests, SourceRevisionBetweenDeclarationAndEndRejectsTheWholeCandidate){
    m_state.select(1u);
    ASSERT_TRUE(accept(1u));
    ASSERT_TRUE(openByPointer(2u));
    ASSERT_TRUE(declareCombo(3u));
    const AcceptedState displayed = accepted();
    ++m_source.contentRevision;
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.commitFrame(3u));
    EXPECT_EQ(m_state.selectedKey(), 1u);
    expectAccepted(displayed);
}

TEST_F(UiComboLoanTests, SourceCountBetweenDeclarationAndEndRejectsEvenWithoutARevisionChange){
    m_state.select(1u);
    ASSERT_TRUE(accept(1u));
    ASSERT_TRUE(declareCombo(2u));
    const AcceptedState displayed = accepted();
    --m_source.count;
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.commitFrame(2u));
    EXPECT_EQ(m_state.selectedKey(), 1u);
    expectAccepted(displayed);
}

TEST_F(UiComboLoanTests, RevisionCallbackAtDeferredValidationKeepsExplicitApplicationSelection){
    m_state.select(1u);
    ASSERT_TRUE(acceptCallbacks(1u));
    ASSERT_TRUE(declareCallbacks(2u));
    const AcceptedState displayed = accepted();
    const u64 inputGeneration = m_state.inputGeneration();
    m_callbacks.arm(MutationKind::RevisionSelect, 8u);
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.commitFrame(2u));
    EXPECT_EQ(m_callbacks.mutations(), 1u);
    EXPECT_NE(m_state.inputGeneration(), inputGeneration);
    EXPECT_EQ(m_state.selectedKey(), 8u);
    expectAccepted(displayed);
}

TEST_F(UiComboLoanTests, PopupTextCallbackCannotOverwriteApplicationSelectionOrAcceptedTargets){
    m_state.select(1u);
    m_state.open();
    ASSERT_TRUE(acceptCallbacks(1u));
    ASSERT_TRUE(declareCallbacks(2u));
    const AcceptedState displayed = accepted();
    const u64 inputGeneration = m_state.inputGeneration();
    m_callbacks.arm(MutationKind::TextSelect, 8u);
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.commitFrame(2u));
    EXPECT_EQ(m_callbacks.mutations(), 1u);
    EXPECT_NE(m_state.inputGeneration(), inputGeneration);
    EXPECT_EQ(m_state.selectedKey(), 8u);
    EXPECT_FALSE(m_state.isOpen());
    expectAccepted(displayed);
}

TEST_F(UiComboLoanTests, EnabledCallbackOpenDuringPopupRowsRejectsTheBorrowedLifetime){
    m_state.select(1u);
    m_state.open();
    ASSERT_TRUE(acceptCallbacks(1u));
    ASSERT_TRUE(declareCallbacks(2u));
    const AcceptedState displayed = accepted();
    const u64 inputGeneration = m_state.inputGeneration();
    m_callbacks.arm(MutationKind::EnabledOpen);
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.commitFrame(2u));
    EXPECT_TRUE(m_state.isOpen());
    EXPECT_EQ(m_state.selectedKey(), 1u);
    EXPECT_NE(m_state.inputGeneration(), inputGeneration);
    expectAccepted(displayed);
}

TEST_F(UiComboLoanTests, TextCallbackSourceRevisionRejectsThePopupAndKeepsAcceptedGeometry){
    m_state.select(1u);
    m_state.open();
    ASSERT_TRUE(acceptCallbacks(1u));
    ASSERT_TRUE(declareCallbacks(2u));
    const AcceptedState displayed = accepted();
    m_callbacks.arm(MutationKind::TextRevision);
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.commitFrame(2u));
    EXPECT_EQ(m_callbacks.mutations(), 1u);
    EXPECT_EQ(m_state.selectedKey(), 1u);
    expectAccepted(displayed);
}

TEST_F(UiComboLoanTests, ReconcileCallbackSelectionRejectsBeforeAnyQueuedCommitIsApplied){
    m_state.select(1u);
    m_state.open();
    ASSERT_TRUE(acceptCallbacks(1u));
    click(Center(target(row(2u))->rectangle));
    m_callbacks.arm(MutationKind::KeySelect, 8u);
    EXPECT_FALSE(declareCallbacks(2u));
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_result.valid);
    EXPECT_FALSE(m_result.committed);
    EXPECT_EQ(m_state.selectedKey(), 8u);
    EXPECT_FALSE(m_state.isOpen());
    EXPECT_FALSE(m_context.commitFrame(2u));
}

TEST_F(UiComboLoanTests, ComboInsideUserPopupRejectsBeforeSourceOrModelCallbacks){
    PopupState popupState;
    popupState.open();
    PopupOptions options;
    options.anchor = { 30.0f, 40.0f, 100.0f, 30.0f };
    options.size = { 280.0f, 200.0f };
    m_state.select(1u);
    const u64 inputGeneration = m_state.inputGeneration();
    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginPopup("owner", popupState, options));
    m_callbacks.arm(MutationKind::RevisionSelect, 8u);
    const ComboResult result = m_builder.comboBox("combo", m_callbacks, m_state, Options());
    EXPECT_FALSE(result.valid);
    EXPECT_TRUE(m_context.failed());
    EXPECT_EQ(m_callbacks.mutations(), 0u);
    EXPECT_EQ(m_state.inputGeneration(), inputGeneration);
    EXPECT_EQ(m_state.selectedKey(), 1u);
    EXPECT_FALSE(m_context.commitFrame(1u));
}

TEST_F(UiComboLoanTests, MatchingPanelEndReleasesItsSourceAndStateBeforeTheNextPanel){
    m_state.select(1u);
    m_state.open();
    ASSERT_TRUE(declareCallbacks(1u));
    ASSERT_TRUE(m_builder.endPanel());
    m_state.select(8u);
    m_callbacks.arm(MutationKind::RevisionSelect, 9u);
    m_callbacks.count = 0u;
    ++m_callbacks.contentRevision;
    ASSERT_TRUE(m_builder.beginPanel("later", { 400.0f, 10.0f, 200.0f, 120.0f }));
    ASSERT_TRUE(m_builder.label("label", "Later panel"));
    ASSERT_TRUE(m_builder.endPanel());
    ASSERT_TRUE(m_context.endRoot());
    ASSERT_TRUE(m_context.finishFrame());
    ASSERT_TRUE(m_context.commitFrame(1u));
    EXPECT_EQ(m_callbacks.mutations(), 0u);
    EXPECT_EQ(m_state.selectedKey(), 8u);
    EXPECT_NE(target(popup()), nullptr);
}

TEST_F(UiComboLoanTests, LaterPopupTextCannotChangeAnEarlierComboAfterItsPaint){
    m_state.select(1u);
    m_state.open();
    ComboState other;
    other.select(2u);
    other.open();
    const auto declarePair = [this, &other](const u64 generation){
        if(!begin(generation) || !m_builder.beginPanel("panel", { 10.0f, 10.0f, 320.0f, 240.0f }))
            return false;
        return
            m_builder.comboBox("combo", m_source, m_state, Options()).valid
            && m_builder.comboBox("other", m_callbacks, other, Options()).valid
        ;
    };
    ASSERT_TRUE(declarePair(1u));
    ASSERT_TRUE(finishPanel());
    ASSERT_TRUE(m_context.commitFrame(1u));
    const AcceptedState displayed = accepted();
    ASSERT_TRUE(declarePair(2u));
    m_callbacks.arm(MutationKind::TextSelect, 8u);
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.commitFrame(2u));
    EXPECT_EQ(m_callbacks.mutations(), 1u);
    EXPECT_EQ(m_state.selectedKey(), 8u);
    EXPECT_FALSE(m_state.isOpen());
    EXPECT_EQ(other.selectedKey(), 2u);
    EXPECT_TRUE(other.isOpen());
    expectAccepted(displayed);
}

TEST_F(UiComboLoanTests, LaterPopupTextCannotChangeTheEarlierSourceAfterItsLoanWasChecked){
    m_state.select(1u);
    m_state.open();
    ComboState other;
    other.select(2u);
    other.open();
    ForeignRevisionSource source(m_source);
    const auto declarePair = [this, &other, &source](const u64 generation){
        if(!begin(generation) || !m_builder.beginPanel("panel", { 10.0f, 10.0f, 320.0f, 240.0f }))
            return false;
        return
            m_builder.comboBox("combo", m_source, m_state, Options()).valid
            && m_builder.comboBox("other", source, other, Options()).valid
        ;
    };
    ASSERT_TRUE(declarePair(1u));
    ASSERT_TRUE(finishPanel());
    ASSERT_TRUE(m_context.commitFrame(1u));
    const AcceptedState displayed = accepted();
    const u64 revision = m_source.contentRevision;
    ASSERT_TRUE(declarePair(2u));
    source.arm();
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.commitFrame(2u));
    EXPECT_EQ(source.mutations(), 1u);
    EXPECT_EQ(m_source.contentRevision, revision + 1u);
    EXPECT_EQ(m_state.selectedKey(), 1u);
    EXPECT_EQ(other.selectedKey(), 2u);
    expectAccepted(displayed);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

