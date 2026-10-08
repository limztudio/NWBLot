// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_area_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_area_reentry_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;
using namespace NWB::UiTextAreaTests;


class AreaReentrySource final : public ComboSource{
public:
    AreaReentrySource(){
        generation = 4401u;
        count = 5u;
    }


public:
    [[nodiscard]] virtual u64 instanceGeneration()const override{ observeCallback(); return generation; }
    [[nodiscard]] virtual u64 revision()const override{ observeCallback(); return contentRevision; }
    [[nodiscard]] virtual u64 rowCount()const override{ observeCallback(); return count; }

    [[nodiscard]] virtual u64 key(const u64 index)const override{
        observeCallback();
        return ComboSource::key(index);
    }

    [[nodiscard]] virtual Expected<u64> indexOf(const u64 value)const override{
        observeCallback();
        return ComboSource::indexOf(value);
    }

    [[nodiscard]] virtual Expected<u64> findEnabled(const u64 start, const bool reverseSearch)const override{
        observeCallback();
        return ComboSource::findEnabled(start, reverseSearch);
    }

    [[nodiscard]] virtual StringView text(const u64 index)const override{
        observeCallback();
        const StringView value = ComboSource::text(index);
        if(rowHook){
            Function<void()> hook = Move(rowHook);
            rowHook = {};
            ++attempts;
            hook();
        }
        return value;
    }

    [[nodiscard]] virtual bool enabled(const u64 index)const override{
        observeCallback();
        return ComboSource::enabled(index);
    }


private:
    void observeCallback()const{
        if(attempts != 0u)
            ++callbacksAfterAttempt;
    }


public:
    mutable Function<void()> rowHook;
    mutable usize attempts = 0u;
    mutable usize callbacksAfterAttempt = 0u;
};

struct AcceptedAreaFrame{
    Array<HitTarget, 16u> targets{};
    usize count = 0u;
    usize popupCount = 0u;
    u64 generation = 0u;
};

class UiTextAreaReentryTests : public TextAreaFixture{
protected:
    [[nodiscard]] static TextAreaOptions AreaOptions(){
        TextAreaOptions options;
        options.height = { LayoutSizePolicy::Fixed, 96.0f };
        return options;
    }

    [[nodiscard]] static PopupOptions ParentOptions(){
        PopupOptions options;
        options.anchor = { 20.0f, 20.0f, 80.0f, 20.0f };
        options.size = { 340.0f, 420.0f };
        return options;
    }

    [[nodiscard]] static PopupOptions ChildOptions(){
        PopupOptions options;
        options.anchor = { 420.0f, 20.0f, 80.0f, 20.0f };
        options.size = { 340.0f, 420.0f };
        return options;
    }

    [[nodiscard]] static ListOptions ListOptions(){
        NWB::Impl::Ui::ListOptions options;
        options.height = { LayoutSizePolicy::Fixed, 140.0f };
        options.rowHeight = 24.0f;
        return options;
    }


protected:
    [[nodiscard]] bool acceptBaseline(){
        useHost();
        if(!m_model.setText("alpha\nbeta") || !frameArea(1u, AreaOptions()))
            return false;
        m_host.publications.clear();
        m_host.publishes = 0u;
        m_host.loans = 0u;
        return true;
    }

    [[nodiscard]] bool beginParent(const u64 generation){
        return begin(generation) && m_builder.beginPopup("parent", m_parent, ParentOptions());
    }

    [[nodiscard]] AcceptedAreaFrame accepted()const{
        AcceptedAreaFrame frame;
        frame.count = m_context.input().targets().size();
        frame.popupCount = m_context.input().popupCount();
        frame.generation = m_context.input().layoutGeneration();
        for(usize index = 0u; index < Min(frame.count, frame.targets.size()); ++index)
            frame.targets[index] = m_context.input().targets()[index];
        return frame;
    }

    void expectRejected(const AcceptedAreaFrame& frame){
        EXPECT_TRUE(m_builder.failed());
        EXPECT_TRUE(m_context.failed());
        EXPECT_FALSE(m_context.ready());
        EXPECT_FALSE(m_context.commitFrame(2u));
        EXPECT_EQ(m_context.input().layoutGeneration(), frame.generation);
        EXPECT_EQ(m_context.input().popupCount(), frame.popupCount);
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
            UiComboTests::ExpectRect(after.rectangle, before.rectangle);
            UiComboTests::ExpectRect(after.clip, before.clip);
        }
    }


protected:
    PopupState m_parent;
    PopupState m_child;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiTextAreaReentryTests, HostLoanResetRejectsWithoutClearingTheLentModel){
    ASSERT_TRUE(acceptBaseline());
    const AcceptedAreaFrame before = accepted();
    usize attempts = 0u;
    bool failedAtAttempt = false;
    m_host.loanHook = [&](){
        ++attempts;
        EXPECT_TRUE(m_model.setText("owned\nmutation"));
        m_builder.reset();
        failedAtAttempt = m_builder.failed();
    };
    ASSERT_TRUE(beginArea(2u));
    EXPECT_FALSE(m_builder.textArea("area", m_model, m_state, AreaOptions()).valid);
    EXPECT_EQ(attempts, 1u);
    EXPECT_TRUE(failedAtAttempt);
    EXPECT_EQ(m_host.loans, 1u);
    EXPECT_EQ(m_host.ordinaryLoans, 0u);
    EXPECT_EQ(m_host.actionLoans, 0u);
    EXPECT_EQ(m_host.publishes, 0u);
    EXPECT_EQ(m_model.text(), AStringView("owned\nmutation"));
    expectRejected(before);
}

TEST_F(UiTextAreaReentryTests, HostLoanNestedDeclarationRejectsBeforeASecondLoan){
    ASSERT_TRUE(acceptBaseline());
    const AcceptedAreaFrame before = accepted();
    EditModel otherModel(m_arena, {}, EditTextMode::Multiline);
    TextAreaState otherState;
    ASSERT_TRUE(otherModel.setText("other\narea"));
    usize attempts = 0u;
    EditBoxResult nested;
    m_host.loanHook = [&](){
        ++attempts;
        nested = m_builder.textArea("reentered", otherModel, otherState, AreaOptions());
    };
    ASSERT_TRUE(beginArea(2u));
    EXPECT_FALSE(m_builder.textArea("area", m_model, m_state, AreaOptions()).valid);
    EXPECT_EQ(attempts, 1u);
    EXPECT_FALSE(nested.valid);
    EXPECT_EQ(m_host.loans, 1u);
    EXPECT_EQ(m_host.publishes, 0u);
    EXPECT_EQ(m_model.text(), AStringView("alpha\nbeta"));
    EXPECT_EQ(otherModel.text(), AStringView("other\narea"));
    expectRejected(before);
}

TEST_F(UiTextAreaReentryTests, PublicationResetRejectsBeforeLaterAreaPublication){
    ASSERT_TRUE(acceptBaseline());
    const AcceptedAreaFrame before = accepted();
    EditModel otherModel(m_arena, {}, EditTextMode::Multiline);
    TextAreaState otherState;
    ASSERT_TRUE(otherModel.setText("other\narea"));
    ASSERT_TRUE(beginArea(2u));
    ASSERT_TRUE(m_builder.textArea("area", m_model, m_state, AreaOptions()).valid);
    ASSERT_TRUE(m_builder.textArea("other", otherModel, otherState, AreaOptions()).valid);
    usize attempts = 0u;
    bool failedAtAttempt = false;
    m_host.publishHook = [&](){
        ++attempts;
        EXPECT_TRUE(otherModel.setText("application\nmutation"));
        m_builder.reset();
        failedAtAttempt = m_builder.failed();
    };
    EXPECT_FALSE(m_builder.endPanel());
    EXPECT_EQ(attempts, 1u);
    EXPECT_TRUE(failedAtAttempt);
    EXPECT_EQ(m_host.publishes, 1u);
    ASSERT_EQ(m_host.publications.size(), 1u);
    EXPECT_EQ(AStringView(m_host.publications[0u].text), AStringView("alpha\nbeta"));
    EXPECT_EQ(m_model.text(), AStringView("alpha\nbeta"));
    EXPECT_EQ(otherModel.text(), AStringView("application\nmutation"));
    expectRejected(before);
}

TEST_F(UiTextAreaReentryTests, ChildRowScopeReentryRejectsBeforeSiblingAreaPublication){
    ASSERT_TRUE(acceptBaseline());
    const AcceptedAreaFrame before = accepted();
    AreaReentrySource source;
    ListState list;
    EditModel siblingModel(m_arena, {}, EditTextMode::Multiline);
    TextAreaState siblingState;
    PopupState sibling;
    ASSERT_TRUE(siblingModel.setText("sibling\narea"));
    m_parent.open();
    m_child.open();
    sibling.open();
    ASSERT_TRUE(beginParent(2u));
    ASSERT_TRUE(m_builder.textArea("parentArea", m_model, m_state, AreaOptions()).valid);
    ASSERT_TRUE(m_builder.beginPopup("child", m_child, ChildOptions()));
    ASSERT_TRUE(m_builder.virtualList("list", source, list, ListOptions()).valid);
    ASSERT_TRUE(m_builder.endPopup());
    ASSERT_TRUE(m_builder.beginPopup("sibling", sibling, ChildOptions()));
    ASSERT_TRUE(m_builder.textArea("siblingArea", siblingModel, siblingState, AreaOptions()).valid);
    ASSERT_TRUE(m_builder.endPopup());
    bool beginAccepted = true;
    bool endAccepted = true;
    bool failedAtAttempt = false;
    source.rowHook = [&](){
        beginAccepted = m_builder.beginPanel("reentered", { 0.0f, 0.0f, 100.0f, 80.0f });
        endAccepted = m_builder.endPanel();
        failedAtAttempt = m_builder.failed();
    };
    EXPECT_FALSE(m_builder.endPopup());
    EXPECT_EQ(source.attempts, 1u);
    EXPECT_EQ(source.callbacksAfterAttempt, 0u);
    EXPECT_FALSE(beginAccepted);
    EXPECT_FALSE(endAccepted);
    EXPECT_TRUE(failedAtAttempt);
    EXPECT_EQ(m_host.loans, 2u);
    EXPECT_EQ(m_host.publishes, 1u);
    ASSERT_EQ(m_host.publications.size(), 1u);
    EXPECT_EQ(AStringView(m_host.publications[0u].text), AStringView("alpha\nbeta"));
    EXPECT_EQ(siblingModel.text(), AStringView("sibling\narea"));
    expectRejected(before);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

