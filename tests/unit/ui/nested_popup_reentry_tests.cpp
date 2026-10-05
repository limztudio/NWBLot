// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "combo_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_nested_popup_reentry_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;
using namespace NWB::UiComboTests;

namespace ReentryKind{
    enum Enum : u8{ Reset, Panel };
};

class ReentrySource final : public ComboSource{
public:
    explicit ReentrySource(const u64 instance){
        generation = instance;
        count = 5u;
    }


public:
    [[nodiscard]] virtual u64 instanceGeneration()const override{
        observeCallback();
        ++m_metadataCalls;
        return generation;
    }

    [[nodiscard]] virtual u64 revision()const override{
        observeCallback();
        ++m_metadataCalls;
        if(m_revisionBuilder){
            Builder* builder = m_revisionBuilder;
            m_revisionBuilder = nullptr;
            attempt(*builder, ReentryKind::Panel);
        }
        return contentRevision;
    }

    [[nodiscard]] virtual u64 rowCount()const override{
        observeCallback();
        ++m_metadataCalls;
        return count;
    }

    [[nodiscard]] virtual u64 key(const u64 index)const override{
        observeCallback();
        return ComboSource::key(index);
    }

    [[nodiscard]] virtual bool indexOf(const u64 value, u64& index)const override{
        observeCallback();
        return ComboSource::indexOf(value, index);
    }

    [[nodiscard]] virtual bool findEnabled(const u64 start, const bool backwards, u64& index)const override{
        observeCallback();
        return ComboSource::findEnabled(start, backwards, index);
    }

    [[nodiscard]] virtual StringView text(const u64 index)const override{
        observeCallback();
        const StringView value = ComboSource::text(index);
        if(m_textBuilder){
            Builder* builder = m_textBuilder;
            m_textBuilder = nullptr;
            attempt(*builder, m_kind);
        }
        if(m_armSource){
            ReentrySource* source = m_armSource;
            Builder* builder = m_armBuilder;
            m_armSource = nullptr;
            m_armBuilder = nullptr;
            ++m_arms;
            source->m_revisionBuilder = builder;
        }
        return value;
    }

    [[nodiscard]] virtual bool enabled(const u64 index)const override{
        observeCallback();
        return ComboSource::enabled(index);
    }

    void armText(Builder& builder, const ReentryKind::Enum kind){
        m_textBuilder = &builder;
        m_kind = kind;
    }

    void armRevisionAfterText(ReentrySource& source, Builder& builder){
        m_armSource = &source;
        m_armBuilder = &builder;
    }

    void observeAttemptsOf(const ReentrySource& source){ m_observer = &source; }

    void resetObservations()const{
        resetCounters();
        m_metadataCalls = 0u;
        m_callbacksAfterAttempt = 0u;
        m_arms = 0u;
        m_attempts = 0u;
        m_resetAttempts = 0u;
        m_beginAttempts = 0u;
        m_endAttempts = 0u;
        m_beginAccepted = false;
        m_endAccepted = false;
        m_failedAtAttempt = false;
    }

    [[nodiscard]] u64 metadataCalls()const{ return m_metadataCalls; }
    [[nodiscard]] u64 callbacksAfterAttempt()const{ return m_callbacksAfterAttempt; }
    [[nodiscard]] u64 arms()const{ return m_arms; }
    [[nodiscard]] u64 attempts()const{ return m_attempts; }
    [[nodiscard]] u64 resetAttempts()const{ return m_resetAttempts; }
    [[nodiscard]] u64 beginAttempts()const{ return m_beginAttempts; }
    [[nodiscard]] u64 endAttempts()const{ return m_endAttempts; }
    [[nodiscard]] bool beginAccepted()const{ return m_beginAccepted; }
    [[nodiscard]] bool endAccepted()const{ return m_endAccepted; }
    [[nodiscard]] bool failedAtAttempt()const{ return m_failedAtAttempt; }


private:
    void observeCallback()const{
        if(m_attempts != 0u || (m_observer && m_observer->attempts() != 0u))
            ++m_callbacksAfterAttempt;
    }

    void attempt(Builder& builder, const ReentryKind::Enum kind)const{
        ++m_attempts;
        if(kind == ReentryKind::Reset){
            ++m_resetAttempts;
            builder.reset();
        }
        else{
            ++m_beginAttempts;
            m_beginAccepted = builder.beginPanel("reentered", { 0.0f, 0.0f, 100.0f, 80.0f });
            ++m_endAttempts;
            m_endAccepted = builder.endPanel();
        }
        m_failedAtAttempt = builder.failed();
    }


private:
    ReentryKind::Enum m_kind = ReentryKind::Reset;
    const ReentrySource* m_observer = nullptr;
    mutable Builder* m_textBuilder = nullptr;
    mutable Builder* m_revisionBuilder = nullptr;
    mutable Builder* m_armBuilder = nullptr;
    mutable ReentrySource* m_armSource = nullptr;
    mutable u64 m_metadataCalls = 0u;
    mutable u64 m_callbacksAfterAttempt = 0u;
    mutable u64 m_arms = 0u;
    mutable u64 m_attempts = 0u;
    mutable u64 m_resetAttempts = 0u;
    mutable u64 m_beginAttempts = 0u;
    mutable u64 m_endAttempts = 0u;
    mutable bool m_beginAccepted = false;
    mutable bool m_endAccepted = false;
    mutable bool m_failedAtAttempt = false;
};

struct AcceptedFrame{
    Array<HitTarget, 32u> targets{};
    usize count = 0u;
    usize popupCount = 0u;
    u64 generation = 0u;
    WidgetId focus;
    WidgetId capture;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiNestedPopupReentryTests : public WidgetFixture{
protected:
    virtual void SetUp()override{
        WidgetFixture::SetUp();
        m_parent.open();
        m_child.open();
        m_sibling.open();
        m_parentList.select(2u);
        m_childList.select(2u);
        m_siblingList.select(2u);
    }

    [[nodiscard]] static PopupOptions ParentOptions(){
        PopupOptions options;
        options.anchor = { 20.0f, 20.0f, 80.0f, 20.0f };
        options.size = { 300.0f, 380.0f };
        return options;
    }

    [[nodiscard]] static PopupOptions ChildOptions(){
        PopupOptions options;
        options.anchor = { 420.0f, 60.0f, 80.0f, 24.0f };
        options.size = { 300.0f, 300.0f };
        return options;
    }

    [[nodiscard]] static ListOptions ListOptions(){
        NWB::Impl::Ui::ListOptions options;
        options.height = { LayoutSizePolicy::Fixed, 150.0f };
        options.rowHeight = 24.0f;
        return options;
    }

    [[nodiscard]] bool acceptBase(){
        if(!begin(1u) || !m_builder.beginPanel("base", { 0.0f, 0.0f, 180.0f, 100.0f }))
            return false;
        if(m_builder.button("button", "Accepted baseline"))
            return false;
        return finishPanel() && m_context.commitFrame(1u);
    }

    [[nodiscard]] bool beginParent(){ return begin(2u) && m_builder.beginPopup("parent", m_parent, ParentOptions()); }

    [[nodiscard]] bool declareChild(){
        return
            m_builder.beginPopup("child", m_child, ChildOptions())
            && m_builder.virtualList("list", m_childSource, m_childList, ListOptions()).valid
            && m_builder.endPopup()
        ;
    }

    [[nodiscard]] bool declareSibling(){
        return
            m_builder.beginPopup("sibling", m_sibling, ChildOptions())
            && m_builder.virtualList("list", m_siblingSource, m_siblingList, ListOptions()).valid
            && m_builder.endPopup()
        ;
    }

    [[nodiscard]] AcceptedFrame accepted()const{
        AcceptedFrame frame;
        frame.count = m_context.input().targets().size();
        frame.popupCount = m_context.input().popupCount();
        frame.generation = m_context.input().layoutGeneration();
        frame.focus = m_context.input().focus();
        frame.capture = m_context.input().capture();
        for(usize index = 0u; index < Min(frame.count, frame.targets.size()); ++index)
            frame.targets[index] = m_context.input().targets()[index];
        return frame;
    }

    void expectRejected(const AcceptedFrame& frame)const{
        EXPECT_TRUE(m_builder.failed());
        EXPECT_TRUE(m_context.failed());
        EXPECT_FALSE(m_context.ready());
        EXPECT_EQ(m_context.input().layoutGeneration(), frame.generation);
        EXPECT_EQ(m_context.input().popupCount(), frame.popupCount);
        EXPECT_EQ(m_context.input().focus(), frame.focus);
        EXPECT_EQ(m_context.input().capture(), frame.capture);
        ASSERT_EQ(m_context.input().targets().size(), frame.count);
        ASSERT_LE(frame.count, frame.targets.size());
        for(usize index = 0u; index < frame.count; ++index){
            const HitTarget& before = frame.targets[index];
            const HitTarget& after = m_context.input().targets()[index];
            EXPECT_EQ(after.id, before.id);
            EXPECT_EQ(after.declarationGeneration, before.declarationGeneration);
            EXPECT_EQ(after.control, before.control);
            EXPECT_EQ(after.owner, before.owner);
            EXPECT_EQ(after.ownerDeclarationGeneration, before.ownerDeclarationGeneration);
            EXPECT_EQ(after.popup, before.popup);
            EXPECT_EQ(after.enabled, before.enabled);
            ExpectRect(after.rectangle, before.rectangle);
            ExpectRect(after.clip, before.clip);
        }
    }

    void expectSourcePreserved(const ReentrySource& source, const u64 generation)const{
        EXPECT_EQ(source.generation, generation);
        EXPECT_EQ(source.contentRevision, 1u);
        EXPECT_EQ(source.count, 5u);
        EXPECT_EQ(source.callbacksAfterAttempt(), 0u);
    }

    void expectListPreserved(const ListState& state, const u64 generation)const{
        EXPECT_EQ(state.inputGeneration(), generation);
        EXPECT_EQ(state.selectedKey(), 2u);
        EXPECT_EQ(state.cursorKey(), 2u);
        EXPECT_DOUBLE_EQ(state.scrollOffset(), 0.0);
    }

    void expectPanelRejected(const ReentrySource& source)const{
        EXPECT_EQ(source.attempts(), 1u);
        EXPECT_EQ(source.beginAttempts(), 1u);
        EXPECT_EQ(source.endAttempts(), 1u);
        EXPECT_FALSE(source.beginAccepted());
        EXPECT_FALSE(source.endAccepted());
        EXPECT_TRUE(source.failedAtAttempt());
    }


protected:
    PopupState m_parent;
    PopupState m_child;
    PopupState m_sibling;
    ReentrySource m_parentSource{ 2401u };
    ReentrySource m_childSource{ 2402u };
    ReentrySource m_siblingSource{ 2403u };
    ReentrySource m_comboSource{ 2404u };
    ListState m_parentList;
    ListState m_childList;
    ListState m_siblingList;
    ComboState m_combo;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiNestedPopupReentryTests, ChildFirstRowResetRejectsWithoutClearingBorrowedFrames){
    ASSERT_TRUE(acceptBase());
    const AcceptedFrame frame = accepted();
    ASSERT_GT(frame.count, 0u);
    ASSERT_TRUE(beginParent());
    ASSERT_TRUE(declareChild());
    ASSERT_TRUE(declareSibling());
    const u64 generation = m_childList.inputGeneration();
    const u64 parentOpening = m_parent.openGeneration();
    const u64 childOpening = m_child.openGeneration();
    m_childSource.resetObservations();
    m_siblingSource.resetObservations();
    m_siblingSource.observeAttemptsOf(m_childSource);
    m_childSource.armText(m_builder, ReentryKind::Reset);
    EXPECT_FALSE(m_builder.endPopup());
    EXPECT_EQ(m_childSource.attempts(), 1u);
    EXPECT_EQ(m_childSource.resetAttempts(), 1u);
    EXPECT_TRUE(m_childSource.failedAtAttempt());
    EXPECT_EQ(m_childSource.textCalls, 1u);
    EXPECT_EQ(m_siblingSource.textCalls, 0u);
    EXPECT_EQ(m_siblingSource.metadataCalls(), 0u);
    expectSourcePreserved(m_childSource, 2402u);
    expectSourcePreserved(m_siblingSource, 2403u);
    expectListPreserved(m_childList, generation);
    EXPECT_TRUE(m_parent.isOpen());
    EXPECT_TRUE(m_child.isOpen());
    EXPECT_EQ(m_parent.openGeneration(), parentOpening);
    EXPECT_EQ(m_child.openGeneration(), childOpening);
    EXPECT_FALSE(m_context.commitFrame(2u));
    expectRejected(frame);
}

TEST_F(UiNestedPopupReentryTests, ChildFirstRowBalancedPanelReentryRejectsBeforeLaterSiblingCallbacks){
    ASSERT_TRUE(acceptBase());
    const AcceptedFrame frame = accepted();
    ASSERT_TRUE(beginParent());
    ASSERT_TRUE(declareChild());
    ASSERT_TRUE(declareSibling());
    const u64 generation = m_childList.inputGeneration();
    m_childSource.resetObservations();
    m_siblingSource.resetObservations();
    m_siblingSource.observeAttemptsOf(m_childSource);
    m_childSource.armText(m_builder, ReentryKind::Panel);
    EXPECT_FALSE(m_builder.endPopup());
    expectPanelRejected(m_childSource);
    EXPECT_EQ(m_childSource.textCalls, 1u);
    EXPECT_EQ(m_siblingSource.textCalls, 0u);
    EXPECT_EQ(m_siblingSource.metadataCalls(), 0u);
    expectSourcePreserved(m_childSource, 2402u);
    expectSourcePreserved(m_siblingSource, 2403u);
    expectListPreserved(m_childList, generation);
    EXPECT_TRUE(m_parent.isOpen());
    EXPECT_TRUE(m_child.isOpen());
    EXPECT_FALSE(m_context.commitFrame(2u));
    expectRejected(frame);
}

TEST_F(UiNestedPopupReentryTests, ParentComboAutomaticPopupRowResetRejectsBeforeChildSourceCallbacks){
    ASSERT_TRUE(acceptBase());
    const AcceptedFrame frame = accepted();
    m_combo.open();
    ASSERT_TRUE(beginParent());
    ASSERT_TRUE(m_builder.comboBox("combo", m_comboSource, m_combo, Options()).valid);
    ASSERT_TRUE(declareChild());
    const u64 comboGeneration = m_combo.inputGeneration();
    const u64 listGeneration = m_combo.listState().inputGeneration();
    const u64 preview = m_combo.listState().cursorKey();
    const u64 childGeneration = m_childList.inputGeneration();
    m_comboSource.resetObservations();
    m_childSource.resetObservations();
    m_childSource.observeAttemptsOf(m_comboSource);
    m_comboSource.armText(m_builder, ReentryKind::Reset);
    EXPECT_FALSE(m_builder.endPopup());
    EXPECT_EQ(m_comboSource.attempts(), 1u);
    EXPECT_EQ(m_comboSource.resetAttempts(), 1u);
    EXPECT_TRUE(m_comboSource.failedAtAttempt());
    EXPECT_EQ(m_comboSource.textCalls, 1u);
    EXPECT_EQ(m_childSource.textCalls, 0u);
    EXPECT_EQ(m_childSource.metadataCalls(), 0u);
    expectSourcePreserved(m_comboSource, 2404u);
    expectSourcePreserved(m_childSource, 2402u);
    EXPECT_TRUE(m_combo.isOpen());
    EXPECT_EQ(m_combo.selectedKey(), 0u);
    EXPECT_EQ(m_combo.inputGeneration(), comboGeneration);
    EXPECT_EQ(m_combo.listState().inputGeneration(), listGeneration);
    EXPECT_EQ(m_combo.listState().cursorKey(), preview);
    expectListPreserved(m_childList, childGeneration);
    EXPECT_FALSE(m_context.commitFrame(2u));
    expectRejected(frame);
}

TEST_F(UiNestedPopupReentryTests, LateParentRevisionScopeReentryRejectsAfterAllRowsWerePainted){
    ASSERT_TRUE(acceptBase());
    const AcceptedFrame frame = accepted();
    ASSERT_TRUE(beginParent());
    ASSERT_TRUE(m_builder.virtualList("list", m_parentSource, m_parentList, ListOptions()).valid);
    ASSERT_TRUE(declareChild());
    ASSERT_TRUE(declareSibling());
    const u64 parentGeneration = m_parentList.inputGeneration();
    const u64 childGeneration = m_childList.inputGeneration();
    m_parentSource.resetObservations();
    m_childSource.resetObservations();
    m_siblingSource.resetObservations();
    m_childSource.observeAttemptsOf(m_parentSource);
    m_siblingSource.observeAttemptsOf(m_parentSource);
    // The parent has finished painting before child text arms its next revision call in final validation.
    m_childSource.armRevisionAfterText(m_parentSource, m_builder);
    EXPECT_FALSE(m_builder.endPopup());
    EXPECT_EQ(m_childSource.arms(), 1u);
    expectPanelRejected(m_parentSource);
    EXPECT_EQ(m_parentSource.textCalls, 5u);
    EXPECT_EQ(m_childSource.textCalls, 5u);
    EXPECT_EQ(m_siblingSource.textCalls, 5u);
    expectSourcePreserved(m_parentSource, 2401u);
    expectSourcePreserved(m_childSource, 2402u);
    expectSourcePreserved(m_siblingSource, 2403u);
    expectListPreserved(m_parentList, parentGeneration);
    expectListPreserved(m_childList, childGeneration);
    EXPECT_TRUE(m_parent.isOpen());
    EXPECT_TRUE(m_child.isOpen());
    EXPECT_FALSE(m_context.commitFrame(2u));
    expectRejected(frame);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

