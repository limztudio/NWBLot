// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/edit_box_host.h>

#include <core/os/clipboard_service.h>
#include <core/os/text_input_service.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_edit_box_host_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Core;
using namespace NWB::Impl;

class FakeTextInput final : public QueuedTextInputService{
public:
    explicit FakeTextInput(Alloc::GlobalArena& arena)
        : QueuedTextInputService(arena)
    {}
    virtual ~FakeTextInput()override = default;


public:
    [[nodiscard]] virtual TextInputCapabilities capabilities()const noexcept override{ return { true, true, true, true }; }
    [[nodiscard]] TextInputAdmission::Enum commit(AStringView text){ return emitCommit(activeSession(), text); }
    [[nodiscard]] TextInputAdmission::Enum commitFor(TextInputSessionToken token, AStringView text){ return emitCommit(token, text); }
    [[nodiscard]] TextInputAdmission::Enum preedit(AStringView text, usize anchor, usize caret, bool visible = true){
        return emitPreedit(activeSession(), text, anchor, caret, visible);
    }
    [[nodiscard]] TextInputAdmission::Enum erase(usize before, usize after){
        return emitDeleteSurrounding(activeSession(), before, after);
    }
    [[nodiscard]] TextInputRect nativeCaret()const{ return caretRect(); }
};

class FakeClipboard final : public QueuedClipboardService{
public:
    explicit FakeClipboard(Alloc::GlobalArena& arena)
        : QueuedClipboardService(arena)
        , document(arena)
        , startedText(arena)
    {}
    virtual ~FakeClipboard()override = default;


public:
    [[nodiscard]] virtual ClipboardCapabilities capabilities(ClipboardChannel::Enum channel)const noexcept override{
        return channel == ClipboardChannel::Clipboard ? ClipboardCapabilities{ true, true } : ClipboardCapabilities{};
    }

    [[nodiscard]] bool deliver(const ClipboardRequestToken token, const ClipboardStatus::Enum status, const AStringView text = {}){
        if(!completeNativeRequest(token, status, text))
            return false;
        if(status == ClipboardStatus::Success && startedOperation == ClipboardOperation::WriteText && token == startedToken)
            document = startedText;
        return true;
    }


protected:
    [[nodiscard]] virtual ClipboardStatus::Enum readNativeText(ClipboardChannel::Enum, AString<Alloc::GlobalArena>& text)override{
        text = document;
        return ClipboardStatus::Success;
    }

    [[nodiscard]] virtual ClipboardStatus::Enum writeNativeText(ClipboardChannel::Enum, const AStringView text)override{
        document.assign(text.data(), text.size());
        return ClipboardStatus::Success;
    }

    virtual void startNativeRequest(
        const ClipboardRequestToken token, const ClipboardOperation::Enum operation,
        const ClipboardChannel::Enum channel, const AStringView text)override{
        startedToken = token;
        startedOperation = operation;
        startedText.assign(text.data(), text.size());
        if(!delayed)
            QueuedClipboardService::startNativeRequest(token, operation, channel, text);
    }


public:
    AString<Alloc::GlobalArena> document;
    AString<Alloc::GlobalArena> startedText;
    ClipboardRequestToken startedToken;
    ClipboardOperation::Enum startedOperation = ClipboardOperation::ReadText;
    bool delayed = false;
};

class FixtureShaper final : public Ui::ITextShaper{
public:
    explicit FixtureShaper(Alloc::GlobalArena& arena)
        : m_arena(arena)
    {}
    virtual ~FixtureShaper()override = default;


public:
    [[nodiscard]] virtual Ui::TextLayoutStatus::Enum shape(const Ui::ShapeRequest& request, Ui::ShapedRun& output)override{
        Ui::ShapedRun run(m_arena);
        run.metrics = { 8.0f, 2.0f, 2.0f };
        usize begin = 0u;
        while(begin < request.text.size()){
            usize end = begin + 1u;
            while(end < request.text.size() && IsUtf8Continuation(static_cast<u8>(request.text[end])))
                ++end;
            run.glyphs.push_back({ {}, 1u, static_cast<u32>(begin), static_cast<u32>(end), {}, { 10.0f, 0.0f }, {} });
            begin = end;
        }
        output = Move(run);
        return Ui::TextLayoutStatus::Success;
    }


private:
    Alloc::GlobalArena& m_arena;
};

class UiEditBoxHostTests : public testing::Test{
public:
    UiEditBoxHostTests()
        : m_arena(Name("tests/ecs_ui/edit_box_host"))
        , m_context(m_arena)
        , m_textInput(m_arena)
        , m_clipboard(m_arena)
        , m_host(m_arena, m_context, m_textInput, m_clipboard)
        , m_model(m_arena)
        , m_shaper(m_arena)
        , m_layoutBuilder(m_arena, m_shaper)
    {}


protected:
    virtual void SetUp()override{ ASSERT_TRUE(m_textInput.setFocused(true)); }

    [[nodiscard]] bool prepare(Ui::EditModel& model, const Ui::EditBoxOptions& options = {},
        const Ui::Rect& bounds = { 10.0f, 20.0f, 180.0f, 30.0f }){
        ++m_generation;
        if(!m_context.beginFrame(m_generation))
            return false;
        m_host.beginFrame(m_generation, { 500.0f, 200.0f, 1.0f, 1.0f });
        if(!m_context.beginRoot({ 17u, 1u }))
            return false;
        const Ui::WidgetState* widget = m_context.declare("edit", Ui::WidgetKind::EditBox);
        if(!widget)
            return false;
        m_widget = *widget;
        m_result = m_host.edit(m_widget, model, options);
        if(!m_result.valid)
            return false;
        Ui::EditBoxView view(m_arena);
        Ui::TextLayout layout(m_arena);
        if(!view.snapshot(model) || m_layoutBuilder.layout({ view.displayText() }, layout) != Ui::TextLayoutStatus::Success)
            return false;
        if(!view.adoptLayout(Move(layout)) || !view.arrange(bounds, {}, { 0.0f, 0.0f, 500.0f, 200.0f }, 0.0f, m_placement))
            return false;
        if(!m_host.publish(m_widget, view, m_placement, options))
            return false;
        Ui::HitTarget target;
        target.rectangle = bounds;
        target.clip = { 0.0f, 0.0f, 500.0f, 200.0f };
        target.enabled = options.enabled;
        target.focusable = options.enabled;
        target.pointerGesture = options.enabled;
        target.textEditable = true;
        if(!m_context.addTarget(m_widget, target) || !m_context.endRoot() || !m_context.finishFrame())
            return false;
        m_host.finishFrame();
        return true;
    }

    [[nodiscard]] bool commit(){
        if(!m_context.commitFrame(m_generation))
            return false;
        m_host.commitFrame(m_generation);
        return true;
    }

    [[nodiscard]] bool frame(Ui::EditModel& model, const Ui::EditBoxOptions& options = {}){
        return prepare(model, options) && commit();
    }

    [[nodiscard]] bool dispatch(const Ui::InputEvent& event){
        m_host.collectNative();
        const Ui::WidgetId previousCapture = m_context.input().capture();
        if(!m_context.input().queue(event))
            return false;
        const Ui::InputRoutingResult result = m_context.input().process();
        m_host.input(event, previousCapture);
        m_host.synchronizeFocus();
        return !result.activationOverflow && !result.gestureOverflow;
    }

    [[nodiscard]] bool key(Ui::InputKey::Enum value, bool shift = false, bool control = false){
        return dispatch({ .type = Ui::InputEventType::KeyDown, .key = value, .shift = shift, .control = control })
            && dispatch({ .type = Ui::InputEventType::KeyUp, .key = value, .shift = shift, .control = control });
    }

    [[nodiscard]] bool activate(){
        return frame(m_model) && key(Ui::InputKey::Tab) && frame(m_model) && m_textInput.activeSession().valid();
    }

    [[nodiscard]] bool emptyFrame(){
        ++m_generation;
        if(!m_context.beginFrame(m_generation))
            return false;
        m_host.beginFrame(m_generation, { 500.0f, 200.0f, 1.0f, 1.0f });
        if(!m_context.beginRoot({ 17u, 1u }) || !m_context.endRoot() || !m_context.finishFrame())
            return false;
        m_host.finishFrame();
        return commit();
    }


protected:
    Alloc::GlobalArena m_arena;
    Ui::Context m_context;
    FakeTextInput m_textInput;
    FakeClipboard m_clipboard;
    UiEditBoxHost m_host;
    Ui::EditModel m_model;
    FixtureShaper m_shaper;
    Ui::TextLayoutBuilder m_layoutBuilder;
    Ui::WidgetState m_widget;
    Ui::EditBoxResult m_result;
    Ui::EditBoxPlacement m_placement;
    u64 m_generation = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiEditBoxHostTests, NativeCommitLocalLeftAndNativeCommitRetainTheirEventOrder){
    ASSERT_TRUE(activate());
    ASSERT_EQ(m_textInput.commit("x"), TextInputAdmission::Accepted);
    ASSERT_TRUE(key(Ui::InputKey::Left));
    ASSERT_EQ(m_textInput.commit("y"), TextInputAdmission::Accepted);
    m_host.collectNative();
    EXPECT_EQ(m_model.text(), "");
    ASSERT_TRUE(frame(m_model));
    EXPECT_TRUE(m_result.textChanged);
    EXPECT_TRUE(m_result.selectionChanged);
    EXPECT_EQ(m_model.text(), "yx");
    EXPECT_EQ(m_model.caret(), 1u);
    ASSERT_TRUE(frame(m_model));
    EXPECT_FALSE(m_result.textChanged);
    EXPECT_EQ(m_model.text(), "yx");
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "x");
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "");
}

TEST_F(UiEditBoxHostTests, QueuedCommitCannotReachAReplacementModelAtTheSameWidget){
    ASSERT_TRUE(m_model.setText("old"));
    ASSERT_TRUE(activate());
    const TextInputSessionToken oldSession = m_textInput.activeSession();
    ASSERT_EQ(m_textInput.commit("late"), TextInputAdmission::Accepted);
    m_host.collectNative();
    Ui::EditModel replacement(m_arena);
    ASSERT_TRUE(replacement.setText("replacement"));
    ASSERT_TRUE(frame(replacement));
    EXPECT_EQ(m_model.text(), "old");
    EXPECT_EQ(replacement.text(), "replacement");
    EXPECT_FALSE(replacement.canUndo());
    EXPECT_EQ(m_textInput.commitFor(oldSession, "later"), TextInputAdmission::InvalidSession);
}

TEST_F(UiEditBoxHostTests, RemovingThenRedeclaringTheWidgetFencesItsOldNativeQueue){
    ASSERT_TRUE(m_model.setText("base"));
    ASSERT_TRUE(activate());
    const Ui::WidgetState oldWidget = m_widget;
    const TextInputSessionToken oldSession = m_textInput.activeSession();
    ASSERT_EQ(m_textInput.commit("late"), TextInputAdmission::Accepted);
    m_host.collectNative();
    ASSERT_TRUE(emptyFrame());
    EXPECT_FALSE(m_host.hasTextFocus());
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_widget.id, oldWidget.id);
    EXPECT_NE(m_widget.declarationGeneration, oldWidget.declarationGeneration);
    EXPECT_EQ(m_model.text(), "base");
    EXPECT_FALSE(m_model.canUndo());
    EXPECT_EQ(m_textInput.commitFor(oldSession, "later"), TextInputAdmission::InvalidSession);
}

TEST_F(UiEditBoxHostTests, ExternalSameTextResetFencesAnAlreadyCollectedCommit){
    ASSERT_TRUE(m_model.setText("base"));
    ASSERT_TRUE(activate());
    const u64 revision = m_model.revision();
    const u64 externalRevision = m_model.externalRevision();
    const TextInputSessionToken oldSession = m_textInput.activeSession();
    ASSERT_EQ(m_textInput.commit("late"), TextInputAdmission::Accepted);
    m_host.collectNative();
    ASSERT_TRUE(m_model.setText("base"));
    EXPECT_EQ(m_model.revision(), revision);
    EXPECT_GT(m_model.externalRevision(), externalRevision);
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.text(), "base");
    EXPECT_FALSE(m_model.canUndo());
    EXPECT_EQ(m_textInput.commitFor(oldSession, "later"), TextInputAdmission::InvalidSession);
}

TEST_F(UiEditBoxHostTests, FocusLossAndImmediateRefocusCannotReviveAnOwnedOldCommit){
    ASSERT_TRUE(m_model.setText("base"));
    ASSERT_TRUE(activate());
    const TextInputSessionToken oldSession = m_textInput.activeSession();
    ASSERT_EQ(m_textInput.commit("late"), TextInputAdmission::Accepted);
    m_host.collectNative();
    m_context.input().clearFocus();
    m_host.synchronizeFocus();
    ASSERT_TRUE(key(Ui::InputKey::Tab));
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.text(), "base");
    EXPECT_FALSE(m_model.canUndo());
    EXPECT_NE(m_textInput.activeSession(), oldSession);
    ASSERT_EQ(m_textInput.commit("fresh"), TextInputAdmission::Accepted);
    m_host.collectNative();
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.text(), "basefresh");
}

TEST_F(UiEditBoxHostTests, LosingFocusCancelsPreeditAtTheNextModelLoanAndRefocusStartsFresh){
    ASSERT_TRUE(m_model.setText("base"));
    ASSERT_TRUE(activate());
    const TextInputSessionToken oldSession = m_textInput.activeSession();
    ASSERT_EQ(m_textInput.preedit("IME", 0u, 3u, false), TextInputAdmission::Accepted);
    m_host.collectNative();
    ASSERT_TRUE(frame(m_model));
    EXPECT_TRUE(m_model.composition().active);
    EXPECT_EQ(m_model.composition().text, "IME");
    EXPECT_FALSE(m_result.preeditCaretVisible);
    m_context.input().clearFocus();
    m_host.synchronizeFocus();
    ASSERT_TRUE(frame(m_model));
    EXPECT_FALSE(m_model.composition().active);
    EXPECT_EQ(m_model.text(), "base");
    EXPECT_FALSE(m_model.canUndo());
    EXPECT_FALSE(m_host.hasTextFocus());
    ASSERT_TRUE(key(Ui::InputKey::Tab));
    ASSERT_TRUE(frame(m_model));
    EXPECT_NE(m_textInput.activeSession(), oldSession);
    ASSERT_EQ(m_textInput.commit("x"), TextInputAdmission::Accepted);
    m_host.collectNative();
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.text(), "basex");
}

TEST_F(UiEditBoxHostTests, ReadOnlyPolicyChangeDiscardsQueuedMutationAndNativeSession){
    ASSERT_TRUE(m_model.setText("base"));
    ASSERT_TRUE(activate());
    ASSERT_EQ(m_textInput.commit("late"), TextInputAdmission::Accepted);
    m_host.collectNative();
    Ui::EditBoxOptions options;
    options.readOnly = true;
    ASSERT_TRUE(frame(m_model, options));
    EXPECT_EQ(m_model.text(), "base");
    EXPECT_FALSE(m_model.canUndo());
    EXPECT_FALSE(m_textInput.activeSession().valid());
    EXPECT_TRUE(m_host.character('x'));
    ASSERT_TRUE(frame(m_model, options));
    EXPECT_EQ(m_model.text(), "base");
    options.readOnly = false;
    ASSERT_TRUE(frame(m_model, options));
    ASSERT_TRUE(m_textInput.activeSession().valid());
    ASSERT_EQ(m_textInput.commit("fresh"), TextInputAdmission::Accepted);
    m_host.collectNative();
    ASSERT_TRUE(frame(m_model, options));
    EXPECT_EQ(m_model.text(), "basefresh");
}

TEST_F(UiEditBoxHostTests, SurroundingDeletionCannotFollowALocalMoveAgainstOldNativeCaret){
    ASSERT_TRUE(m_model.setText("abcd"));
    ASSERT_TRUE(activate());
    ASSERT_TRUE(key(Ui::InputKey::Left));
    ASSERT_EQ(m_textInput.erase(1u, 0u), TextInputAdmission::Accepted);
    m_host.collectNative();
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.text(), "abcd");
    EXPECT_EQ(m_model.caret(), 3u);
    EXPECT_FALSE(m_model.canUndo());
}

TEST_F(UiEditBoxHostTests, CandidateGeometryCannotMoveTheNativeCaretBeforeContextAcceptance){
    ASSERT_TRUE(m_model.setText("abc"));
    ASSERT_TRUE(activate());
    const TextInputRect displayed = m_textInput.nativeCaret();
    ASSERT_TRUE(prepare(m_model, {}, { 200.0f, 20.0f, 180.0f, 30.0f }));
    EXPECT_EQ(m_context.input().layoutGeneration(), m_generation - 1u);
    m_host.commitFrame(m_generation);
    EXPECT_EQ(m_textInput.nativeCaret().x, displayed.x);
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::PrimaryDown, .position = { 20.0f, 25.0f } }));
    ASSERT_TRUE(dispatch({ .type = Ui::InputEventType::PrimaryUp, .position = { 20.0f, 25.0f } }));
    ASSERT_TRUE(commit());
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.caret(), 1u);
    EXPECT_EQ(m_model.anchor(), 1u);
}

TEST_F(UiEditBoxHostTests, HostEventOverflowDiscardsTheBoundedBatchBeforeAnyModelMutation){
    ASSERT_TRUE(m_model.setText("base"));
    ASSERT_TRUE(activate());
    usize accepted = 0u;
    for(usize index = 0u; index < Ui::s_InputMaxEvents + 2u; ++index){
        if(!m_textInput.activeSession().valid())
            break;
        ASSERT_EQ(m_textInput.commit("x"), TextInputAdmission::Accepted);
        ++accepted;
        m_host.collectNative();
    }
    EXPECT_LE(accepted, Ui::s_InputMaxEvents + 1u);
    EXPECT_FALSE(m_textInput.activeSession().valid());
    EXPECT_EQ(m_model.text(), "base");
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.text(), "base");
    EXPECT_FALSE(m_model.canUndo());
}

TEST_F(UiEditBoxHostTests, NativeEventOverflowCancelsTheBatchAndCanRecoverOnANewDeclaration){
    ASSERT_TRUE(m_model.setText("base"));
    ASSERT_TRUE(activate());
    for(usize index = 0u; index < s_TextInputMaxEvents; ++index)
        ASSERT_EQ(m_textInput.commit("x"), TextInputAdmission::Accepted);
    EXPECT_EQ(m_textInput.commit("overflow"), TextInputAdmission::QueueFull);
    m_host.collectNative();
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.text(), "base");
    EXPECT_FALSE(m_model.canUndo());
    ASSERT_TRUE(m_textInput.activeSession().valid());
    ASSERT_EQ(m_textInput.commit("ok"), TextInputAdmission::Accepted);
    m_host.collectNative();
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.text(), "baseok");
}

TEST_F(UiEditBoxHostTests, NativeCommitPathConsumesCharacterFallbackWithoutDuplicatingIt){
    ASSERT_TRUE(activate());
    EXPECT_TRUE(m_host.character('x'));
    ASSERT_EQ(m_textInput.commit("x"), TextInputAdmission::Accepted);
    m_host.collectNative();
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.text(), "x");
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "");
    EXPECT_FALSE(m_model.canUndo());
}

TEST_F(UiEditBoxHostTests, DelayedPasteAppliesOnlyAfterSuccessfulCompletion){
    ASSERT_TRUE(m_model.setText("base"));
    ASSERT_TRUE(activate());
    m_clipboard.delayed = true;
    ASSERT_TRUE(key(Ui::InputKey::V, false, true));
    ASSERT_TRUE(frame(m_model));
    ASSERT_TRUE(m_clipboard.pump());
    EXPECT_EQ(m_clipboard.startedOperation, ClipboardOperation::ReadText);
    EXPECT_EQ(m_model.text(), "base");
    ASSERT_TRUE(m_clipboard.deliver(m_clipboard.startedToken, ClipboardStatus::Success, "paste"));
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.text(), "basepaste");
    EXPECT_TRUE(m_result.textChanged);
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "base");
    EXPECT_FALSE(m_model.canUndo());
}

TEST_F(UiEditBoxHostTests, QueuedNativeEditFencesACompletedPasteBeforeModelMutation){
    ASSERT_TRUE(m_model.setText("base"));
    ASSERT_TRUE(activate());
    m_clipboard.delayed = true;
    ASSERT_TRUE(key(Ui::InputKey::V, false, true));
    ASSERT_TRUE(frame(m_model));
    ASSERT_TRUE(m_clipboard.pump());
    const auto old = m_clipboard.startedToken;
    ASSERT_EQ(m_textInput.commit("x"), TextInputAdmission::Accepted);
    m_host.collectNative();
    ASSERT_TRUE(m_clipboard.deliver(old, ClipboardStatus::Success, "old"));
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.text(), "basex");
    ASSERT_TRUE(m_model.undo());
    EXPECT_EQ(m_model.text(), "base");
    EXPECT_FALSE(m_model.canUndo());
}

TEST_F(UiEditBoxHostTests, QueuedLocalSelectionFencesCompletedCutWithoutDeletingOldSelection){
    ASSERT_TRUE(m_model.setText("base"));
    ASSERT_TRUE(activate());
    ASSERT_TRUE(key(Ui::InputKey::A, false, true));
    ASSERT_TRUE(frame(m_model));
    ASSERT_EQ(m_model.selectedText(), "base");
    m_clipboard.delayed = true;
    ASSERT_TRUE(key(Ui::InputKey::X, false, true));
    ASSERT_TRUE(frame(m_model));
    ASSERT_TRUE(m_clipboard.pump());
    const auto old = m_clipboard.startedToken;
    EXPECT_EQ(m_clipboard.startedOperation, ClipboardOperation::WriteText);
    ASSERT_TRUE(key(Ui::InputKey::Left));
    ASSERT_TRUE(m_clipboard.deliver(old, ClipboardStatus::Success));
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.text(), "base");
    EXPECT_EQ(m_model.caret(), 0u);
    EXPECT_FALSE(m_model.hasSelection());
    EXPECT_FALSE(m_model.canUndo());
    EXPECT_EQ(m_clipboard.document, "base");
}

TEST_F(UiEditBoxHostTests, ClipboardCompletionCannotSurviveFocusLossAndImmediateRefocus){
    ASSERT_TRUE(m_model.setText("base"));
    ASSERT_TRUE(activate());
    m_clipboard.delayed = true;
    ASSERT_TRUE(key(Ui::InputKey::V, false, true));
    ASSERT_TRUE(frame(m_model));
    ASSERT_TRUE(m_clipboard.pump());
    const auto old = m_clipboard.startedToken;
    m_context.input().clearFocus();
    m_host.synchronizeFocus();
    ASSERT_TRUE(key(Ui::InputKey::Tab));
    ASSERT_TRUE(frame(m_model));
    EXPECT_FALSE(m_clipboard.deliver(old, ClipboardStatus::Success, "old"));
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.text(), "base");
    EXPECT_FALSE(m_model.canUndo());
}

TEST_F(UiEditBoxHostTests, RemovingReadOnlyCopyOwnerPreservesCopiedNativePublication){
    ASSERT_TRUE(m_model.setText("base"));
    ASSERT_TRUE(activate());
    Ui::EditBoxOptions options;
    options.readOnly = true;
    ASSERT_TRUE(frame(m_model, options));
    EXPECT_FALSE(m_textInput.activeSession().valid());
    ASSERT_TRUE(key(Ui::InputKey::A, false, true));
    ASSERT_TRUE(frame(m_model, options));
    m_clipboard.delayed = true;
    ASSERT_TRUE(key(Ui::InputKey::C, false, true));
    ASSERT_TRUE(frame(m_model, options));
    ASSERT_TRUE(m_clipboard.pump());
    const auto old = m_clipboard.startedToken;
    EXPECT_EQ(m_clipboard.startedOperation, ClipboardOperation::WriteText);
    ASSERT_TRUE(emptyFrame());
    EXPECT_TRUE(m_clipboard.deliver(old, ClipboardStatus::Success));
    EXPECT_EQ(m_clipboard.document, "base");
    EXPECT_EQ(m_model.text(), "base");
}

TEST_F(UiEditBoxHostTests, ClipboardCompletionCannotReachIdenticalExternallyResetModel){
    ASSERT_TRUE(m_model.setText("base"));
    ASSERT_TRUE(activate());
    m_clipboard.delayed = true;
    ASSERT_TRUE(key(Ui::InputKey::V, false, true));
    ASSERT_TRUE(frame(m_model));
    ASSERT_TRUE(m_clipboard.pump());
    const auto old = m_clipboard.startedToken;
    ASSERT_TRUE(m_model.setText("base"));
    ASSERT_TRUE(frame(m_model));
    EXPECT_FALSE(m_clipboard.deliver(old, ClipboardStatus::Success, "old"));
    EXPECT_EQ(m_model.text(), "base");
    EXPECT_FALSE(m_model.canUndo());
}

TEST_F(UiEditBoxHostTests, QueuedCopyThenPastePreservesAsynchronousNativePublicationOrder){
    ASSERT_TRUE(m_model.setText("base"));
    ASSERT_TRUE(activate());
    ASSERT_TRUE(key(Ui::InputKey::A, false, true));
    ASSERT_TRUE(frame(m_model));
    m_clipboard.document.assign("old");
    m_clipboard.delayed = true;
    ASSERT_TRUE(key(Ui::InputKey::C, false, true));
    ASSERT_TRUE(key(Ui::InputKey::V, false, true));
    ASSERT_TRUE(frame(m_model));
    ASSERT_TRUE(m_clipboard.pump());
    ASSERT_EQ(m_clipboard.startedOperation, ClipboardOperation::WriteText);
    EXPECT_EQ(m_clipboard.startedText, "base");
    ASSERT_TRUE(m_clipboard.deliver(m_clipboard.startedToken, ClipboardStatus::Success));
    ASSERT_TRUE(m_clipboard.pump());
    ASSERT_EQ(m_clipboard.startedOperation, ClipboardOperation::ReadText);
    ASSERT_TRUE(m_clipboard.deliver(m_clipboard.startedToken, ClipboardStatus::Success, m_clipboard.document));
    ASSERT_TRUE(frame(m_model));
    EXPECT_EQ(m_model.text(), "base");
    EXPECT_FALSE(m_model.canUndo());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

