// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "numeric_edit_fixture.h"

#include <impl/ecs_ui/toolkit/edit/text_normalization.adhd.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiNumericEditTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NumericHost::NumericHost(Core::Alloc::GlobalArena& arena)
    : displayed(arena)
    , m_arena(arena)
    , m_events(arena)
    , m_bindings(arena)
{}

void NumericHost::text(const AStringView value){
    NumericEvent event(m_arena);
    event.text.assign(value.data(), value.size());
    m_events.push_back(Move(event));
}

void NumericHost::key(const i32 key, const b8 control){
    NumericEvent event(m_arena);
    event.kind = NumericEventKind::Key;
    event.intent = m_bindings.resolve(key, control ? Core::InputModifier::Control : 0);
    m_events.push_back(Move(event));
}

void NumericHost::replace(const AStringView value){
    key(Core::Key::A, true);
    text(value);
}

void NumericHost::preedit(const AStringView value){
    NumericEvent event(m_arena);
    event.kind = NumericEventKind::Preedit;
    event.text.assign(value.data(), value.size());
    m_events.push_back(Move(event));
}

void NumericHost::action(const EditAction::Enum action){
    NumericEvent event(m_arena);
    event.kind = NumericEventKind::Action;
    event.action = action;
    m_events.push_back(Move(event));
}

EditBoxResult NumericHost::edit([[maybe_unused]] const WidgetState& widget, [[maybe_unused]] const EditBoxOptions& options, [[maybe_unused]] EditModel& draft)noexcept{
    ++ordinaryLoans;
    EditBoxResult result;
    result.valid = true;
    return result;
}

EditBoxResult NumericHost::editActions([[maybe_unused]] const WidgetState& widget, const EditBoxOptions& options, const PopupToken& popup, EditModel& draft, IEditActionSink& sink){
    ++loans;
    lastPopup = popup;
    EditBoxResult result;
    result.valid = true;
    result.focused = options.enabled && focused;
    const u64 revision = draft.revision();
    const usize anchor = draft.anchor();
    const usize caret = draft.caret();
    for(const auto& event : m_events){
        if(event.kind == NumericEventKind::Action){
            if(!sink.apply(draft, event.action, options.readOnly)){
                result.valid = false;
                break;
            }
            result.blurred |= event.action == EditAction::Blur;
            result.abandoned |= event.action == EditAction::Abandon;
            if(event.action == EditAction::Blur)
                result.focused = false;
        }
        else if(options.enabled && event.kind == NumericEventKind::Text && !options.readOnly){
            if(!draft.replaceSelection(event.text)){
                result.valid = false;
                break;
            }
        }
        else if(options.enabled && event.kind == NumericEventKind::Preedit && !options.readOnly){
            if(!draft.beginComposition() || !draft.updateComposition(event.text, event.text.size(), event.text.size())){
                result.valid = false;
                break;
            }
        }
        else if(options.enabled && event.kind == NumericEventKind::Key){
            const auto command = ApplyEditCommand(draft, TranslateEditCommand(event.intent, false, EditTextMode::SingleLine), options.readOnly);
            result.submitted |= command.submitted;
            result.cancelled |= command.cancelled;
            if(command.submitted || command.cancelled){
                if(!sink.apply(draft, command.submitted ? EditAction::Submit : EditAction::Cancel, options.readOnly)){
                    result.valid = false;
                    break;
                }
                if(command.cancelled){
                    result.focused = false;
                    break;
                }
            }
        }
    }
    m_events.clear();
    if(onLoanInteger)
        mutationApplied = onLoanInteger->setValue(mutationValue);
    if(resetBuilder)
        resetBuilder->reset();
    result.textChanged = revision != draft.revision();
    result.selectionChanged = anchor != draft.anchor() || caret != draft.caret();
    return result;
}

b8 NumericHost::publish([[maybe_unused]] const WidgetState& widget, const EditBoxView& view, [[maybe_unused]] const EditBoxPlacement& placement, [[maybe_unused]] const EditBoxOptions& options){
    ++publishes;
    displayed.assign(view.displayText().data(), view.displayText().size());
    if(onPublishInteger)
        mutationApplied = onPublishInteger->setValue(mutationValue);
    if(onPublishFloat)
        mutationApplied = onPublishFloat->setValue(static_cast<f64>(mutationValue));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NumericFixture::NumericFixture()
    : m_integer(m_arena)
    , m_float(m_arena)
    , m_host(m_arena)
{}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

