// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_box_host.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool UiEditBoxHost::prepareSubmit(const Ui::InputEvent& event){
    if(rejectBorrowedMutation() || !m_context.input().canSubmitCommand(event))
        return false;
    const Entry* entry = find(m_context.input().focus());
    if(!entry || !hasTextFocus())
        return false;
    const Ui::InputCommandIntent intent{ event.command, event.extend, event.edit };
    if(Ui::TranslateEditCommand(intent, false, entry->displayed.textMode).command != Ui::EditCommand::Submit)
        return false;
    if(compositionActive(*entry)){
        m_context.input().cancelSubmitCommands(entry->widget.id);
        return false;
    }
    return true;
}

bool UiEditBoxHost::compositionActive(const Entry& entry)const{
    bool composing = entry.expected.m_expectedComposition.active;
    for(const Event& event : m_events){
        if(event.owner != entry.owner || event.focusGeneration != entry.focusGeneration)
            continue;
        if(event.kind == UiEditBoxEventKind::Native){
            if(event.native.kind == Core::TextInputEventKind::Preedit)
                composing = !event.native.text.empty();
            else if(event.native.kind == Core::TextInputEventKind::Cancelled || (event.native.kind == Core::TextInputEventKind::Commit && !event.native.text.empty()))
                composing = false;
        }
        else if(event.kind == UiEditBoxEventKind::Command && event.command.command == Ui::EditCommand::Cancel)
            composing = false;
    }
    return composing;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

