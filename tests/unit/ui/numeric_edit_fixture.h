// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "combo_fixture.h"

#include <impl/ecs_ui/toolkit/edit/commands.h>
#include <impl/ecs_ui/toolkit/input/bindings.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiNumericEditTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace UiComboTests;

namespace NumericEventKind{
    enum Enum : u8{ Text, Key, Preedit, Action };
};

struct NumericEvent{
    AString<Core::Alloc::GlobalArena> text;
    InputCommandIntent intent;
    EditAction::Enum action = EditAction::Submit;
    NumericEventKind::Enum kind = NumericEventKind::Text;

    explicit NumericEvent(Core::Alloc::GlobalArena& arena) : text(arena){}
};

class NumericHost final : public IEditBoxHost{
public:
    explicit NumericHost(Core::Alloc::GlobalArena& arena)
        : m_arena(arena)
        , m_events(arena)
        , m_bindings(arena)
        , displayed(arena)
    {}
    virtual ~NumericHost()override = default;


public:
    void text(const AStringView value){
        NumericEvent event(m_arena);
        event.text.assign(value.data(), value.size());
        m_events.push_back(Move(event));
    }

    void key(const i32 key, const bool control = false){
        NumericEvent event(m_arena);
        event.kind = NumericEventKind::Key;
        event.intent = m_bindings.resolve(key, control ? Core::InputModifier::Control : 0);
        m_events.push_back(Move(event));
    }

    void replace(const AStringView value){ key(Core::Key::A, true); text(value); }

    void preedit(const AStringView value){
        NumericEvent event(m_arena);
        event.kind = NumericEventKind::Preedit;
        event.text.assign(value.data(), value.size());
        m_events.push_back(Move(event));
    }

    void action(const EditAction::Enum action){
        NumericEvent event(m_arena);
        event.kind = NumericEventKind::Action;
        event.action = action;
        m_events.push_back(Move(event));
    }

    [[nodiscard]] virtual EditBoxResult edit(const WidgetState&, EditModel&, const EditBoxOptions&)override{
        ++ordinaryLoans;
        EditBoxResult result;
        result.valid = true;
        return result;
    }

    [[nodiscard]] virtual EditBoxResult editInPopup(const WidgetState&, EditModel&,
        const EditBoxOptions&, const PopupToken&)override{ return {}; }

    [[nodiscard]] virtual EditBoxResult editActions(const WidgetState&, EditModel& draft,
        const EditBoxOptions& options, const PopupToken& popup, IEditActionSink& sink)override{
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
                const auto command = ApplyEditCommand(draft, TranslateEditCommand(event.intent, false), options.readOnly);
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

    [[nodiscard]] virtual EditBoxResult editNavigated(const WidgetState&, EditModel&, const EditBoxOptions&,
        const PopupToken&, EditNavigationState&, IEditNavigationResolver&, IEditActionSink&)override{ return {}; }

    [[nodiscard]] virtual bool publish(const WidgetState&, const EditBoxView& view,
        const EditBoxPlacement&, const EditBoxOptions&)override{
        ++publishes;
        displayed.assign(view.displayText().data(), view.displayText().size());
        if(onPublishInteger)
            mutationApplied = onPublishInteger->setValue(mutationValue);
        if(onPublishFloat)
            mutationApplied = onPublishFloat->setValue(static_cast<f64>(mutationValue));
        return true;
    }


private:
    Core::Alloc::GlobalArena& m_arena;
    PaintVector<NumericEvent> m_events;
    InputBindings m_bindings;


public:
    AString<Core::Alloc::GlobalArena> displayed;
    IntegerEditModel* onLoanInteger = nullptr;
    IntegerEditModel* onPublishInteger = nullptr;
    FloatEditModel* onPublishFloat = nullptr;
    Builder* resetBuilder = nullptr;
    PopupToken lastPopup;
    i64 mutationValue = 99;
    usize loans = 0u;
    usize ordinaryLoans = 0u;
    usize publishes = 0u;
    bool focused = true;
    bool mutationApplied = false;
};

class NumericFixture : public WidgetFixture{
public:
    NumericFixture()
        : m_integer(m_arena)
        , m_float(m_arena)
        , m_host(m_arena)
    {}


protected:
    [[nodiscard]] bool beginNumeric(const u64 generation){
        return begin(generation) && m_builder.beginPanel("panel", { 20.0f, 20.0f, 740.0f, 520.0f });
    }

    [[nodiscard]] bool acceptNumeric(){
        return finishPanel() && m_context.commitFrame(m_context.readyGeneration());
    }

    void useHost(){ m_builder.setEditHost(&m_host); }


protected:
    IntegerEditModel m_integer;
    FloatEditModel m_float;
    EditBoxState m_integerState;
    EditBoxState m_floatState;
    NumericHost m_host;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

