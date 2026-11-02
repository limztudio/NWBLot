// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_area_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiTextAreaTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TextAreaHost::TextAreaHost(Core::Alloc::GlobalArena& arena, const Context& context)
    : publications(arena)
    , resolutions(arena)
    , actionTexts(arena)
    , m_arena(arena)
    , m_context(context)
    , m_events(arena)
    , m_bindings(arena)
{}

void TextAreaHost::text(const AStringView value){
    AreaEvent event(m_arena);
    event.text.assign(value.data(), value.size());
    m_events.push_back(Move(event));
}

void TextAreaHost::key(const i32 value, const b8 control, const b8 shift){
    AreaEvent event(m_arena);
    event.kind = AreaEventKind::Key;
    const i32 modifiers = (control ? Core::InputModifier::Control : 0) | (shift ? Core::InputModifier::Shift : 0);
    event.intent = m_bindings.resolve(value, modifiers);
    m_events.push_back(Move(event));
}

void TextAreaHost::selection(const usize anchor, const usize caret){
    AreaEvent event(m_arena);
    event.kind = AreaEventKind::Selection;
    event.anchor = anchor;
    event.caret = caret;
    m_events.push_back(Move(event));
}

void TextAreaHost::action(const EditAction::Enum value){
    AreaEvent event(m_arena);
    event.kind = AreaEventKind::Action;
    event.action = value;
    m_events.push_back(Move(event));
}

EditBoxResult TextAreaHost::edit([[maybe_unused]] const WidgetState& widget, [[maybe_unused]] const EditBoxOptions& options, [[maybe_unused]] EditModel& model)noexcept{
    ++ordinaryLoans;
    return {};
}

EditBoxResult TextAreaHost::editActions([[maybe_unused]] const WidgetState& widget, [[maybe_unused]] const EditBoxOptions& options, [[maybe_unused]] const PopupToken& popup, [[maybe_unused]] EditModel& model, [[maybe_unused]] IEditActionSink& sink)noexcept{
    ++actionLoans;
    return {};
}

EditBoxResult TextAreaHost::editNavigated(
    [[maybe_unused]] const WidgetState& widget,
    const EditBoxOptions& options,
    const PopupToken& popup,
    EditModel& model,
    EditNavigationState& navigation,
    IEditNavigationResolver& resolver,
    IEditActionSink& sink)
{
    ++loans;
    lastPopup = popup;
    EditBoxResult result;
    result.valid = true;
    result.focused = options.enabled && focused;
    const u64 revision = model.revision();
    const usize anchor = model.anchor();
    const usize caret = model.caret();
    if(seedColumn)
        result.valid = navigation.setPreferredX(seededColumn);
    seedColumn = false;
    for(const auto& event : m_events){
        if(!result.valid)
            break;
        const u64 beforeRevision = model.revision();
        const u64 beforeSelection = model.selectionGeneration();
        const u64 beforeComposition = model.compositionGeneration();
        b8 vertical = false;
        if(event.kind == AreaEventKind::Action)
            result.valid = applyAction(event.action, options.readOnly, model, navigation, sink, result);
        else if(options.enabled && event.kind == AreaEventKind::Text && !options.readOnly)
            result.valid = model.replaceSelection(event.text);
        else if(options.enabled && event.kind == AreaEventKind::Selection)
            result.valid = model.setSelection(event.anchor, event.caret);
        else if(options.enabled && event.kind == AreaEventKind::Key){
            EditNavigationDirection::Enum direction = EditNavigationDirection::Up;
            vertical = TryTranslateEditNavigation(event.intent, direction);
            if(vertical && !model.composition().active){
                resolutions.emplace_back(m_arena);
                AreaResolution& call = resolutions.back();
                call.text.assign(model.text().data(), model.text().size());
                call.caret = model.caret();
                call.preferred = navigation.snapshot();
                call.result = resolver.resolve(model, direction, call.preferred, viewportHeight);
                if(call.result){
                    const usize targetAnchor = event.intent.extend ? model.anchor() : call.result->committedByte;
                    result.valid = model.setSelection(targetAnchor, call.result->committedByte) && navigation.setPreferredX(call.result->preferredX);
                }
                else
                    result.valid = false;
            }
            else if(!vertical){
                const EditCommandRequest request = TranslateEditCommand(event.intent, false, model.textMode());
                const auto command = ApplyEditCommand(model, request, options.readOnly);
                if(command.submitted || command.cancelled){
                    const auto action = command.submitted ? EditAction::Submit : EditAction::Cancel;
                    result.valid = applyAction(action, options.readOnly, model, navigation, sink, result);
                }
            }
        }
        if(!vertical && (model.revision() != beforeRevision || model.selectionGeneration() != beforeSelection || model.compositionGeneration() != beforeComposition))
            navigation.reset();
    }
    m_events.clear();
    if(loanHook && (!loanHookWidget.valid() || loanHookWidget == widget.id)){
        Function<void()> hook = Move(loanHook);
        loanHook = {};
        hook();
    }
    result.textChanged = model.revision() != revision;
    result.selectionChanged = model.anchor() != anchor || model.caret() != caret;
    return result;
}

b8 TextAreaHost::publish(const WidgetState& widget, const EditBoxView& view, const EditBoxPlacement& placement, [[maybe_unused]] const EditBoxOptions& options){
    ++publishes;
    publications.emplace_back(m_arena);
    AreaPublication& record = publications.back();
    record.text.assign(view.displayText().data(), view.displayText().size());
    record.placement = placement;
    record.popup = m_context.popupToken();
    viewportHeight = placement.content.height;
    if(publishHook && (!publishHookWidget.valid() || publishHookWidget == widget.id)){
        Function<void()> hook = Move(publishHook);
        publishHook = {};
        hook();
    }
    return true;
}

b8 TextAreaHost::applyAction(const EditAction::Enum action, const b8 readOnly, EditModel& model, EditNavigationState& navigation, IEditActionSink& sink, EditBoxResult& result){
    actionTexts.emplace_back(m_arena);
    actionTexts.back().assign(model.text().data(), model.text().size());
    if(!sink.apply(model, action, readOnly))
        return false;
    result.submitted |= action == EditAction::Submit;
    result.cancelled |= action == EditAction::Cancel;
    result.blurred |= action == EditAction::Blur;
    result.abandoned |= action == EditAction::Abandon;
    if(action == EditAction::Cancel || action == EditAction::Blur || action == EditAction::Abandon){
        navigation.reset();
        if(action == EditAction::Cancel || action == EditAction::Blur)
            result.focused = false;
    }
    return !m_context.failed();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TextAreaFixture::TextAreaFixture()
    : m_model(m_arena, {}, EditTextMode::Multiline)
    , m_host(m_arena, m_context)
{}

b8 TextAreaFixture::frameArea(const u64 generation, const TextAreaOptions& options, const Rect& bounds){
    if(!beginArea(generation, bounds))
        return false;
    m_result = m_builder.textArea(s_TextAreaKey, m_model, m_state, options);
    return m_result.valid && acceptArea();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TextAreaOptions TextAreaLoanFixture::AreaOptions()noexcept{
    TextAreaOptions options;
    options.height = { LayoutSizePolicy::Fixed, 96.0f };
    return options;
}

b8 TextAreaLoanFixture::acceptBaseline(){
    useHost();
    if(!m_model.setText("alpha\nbeta") || !frameArea(1u, AreaOptions()))
        return false;
    m_host.publications.clear();
    m_host.publishes = 0u;
    m_host.loans = 0u;
    return true;
}

void TextAreaLoanFixture::expectRejected(const UiWidgetTests::AcceptedInputState<16u>& frame){
    EXPECT_TRUE(m_builder.failed());
    EXPECT_TRUE(m_context.failed());
    EXPECT_FALSE(m_context.ready());
    EXPECT_FALSE(m_context.commitFrame(2u));
    UiWidgetTests::ExpectAcceptedInput(m_context.input(), frame, true, false, true, false);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

