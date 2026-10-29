// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "commands.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_edit_commands{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_NewlineText = "\n";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static b8 IsMutation(const EditCommand::Enum command)noexcept{
    return
        command == EditCommand::Backspace || command == EditCommand::Delete || command == EditCommand::WordBackspace
        || command == EditCommand::WordDelete || command == EditCommand::Cut || command == EditCommand::Paste
        || command == EditCommand::Undo || command == EditCommand::Redo || command == EditCommand::Newline
    ;
}

static b8 SuppressRepeat(const EditCommand::Enum command)noexcept{
    return command == EditCommand::SelectAll || command == EditCommand::Copy || command == EditCommand::Cut || command == EditCommand::Paste || command == EditCommand::Submit || command == EditCommand::Cancel;
}

static b8 EraseWord(EditModel& model, const b8 forward){
    if(model.hasSelection())
        return model.replaceSelection({});
    const usize caret = model.caret();
    if(!model.move(forward ? EditMove::WordRight : EditMove::WordLeft))
        return false;
    const usize boundary = model.caret();
    if(!model.setSelection(caret, caret))
        return false;
    return model.eraseSurrounding(forward ? 0u : caret - boundary, forward ? boundary - caret : 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


EditCommandResult ApplyEditCommand(EditModel& model, const EditCommandRequest& request, const b8 readOnly){
    EditCommandResult result;
    if(request.command == EditCommand::None || request.command > EditCommand::Newline)
        return result;
    result.handled = true;
    if(request.repeat && __hidden_ui_edit_commands::SuppressRepeat(request.command))
        return result;
    if(model.composition().active){
        if(request.command == EditCommand::Cancel){
            model.cancelComposition();
            result.compositionChanged = true;
        }
        return result;
    }
    if(readOnly && __hidden_ui_edit_commands::IsMutation(request.command))
        return result;
    const u64 revision = model.revision();
    const usize anchor = model.anchor();
    const usize caret = model.caret();
    b8 applied = true;
    switch(request.command){
    case EditCommand::Left: applied = model.move(EditMove::Left, request.extend); break;
    case EditCommand::Right: applied = model.move(EditMove::Right, request.extend); break;
    case EditCommand::Home: applied = model.move(EditMove::Home, request.extend); break;
    case EditCommand::End: applied = model.move(EditMove::End, request.extend); break;
    case EditCommand::WordLeft: applied = model.move(EditMove::WordLeft, request.extend); break;
    case EditCommand::WordRight: applied = model.move(EditMove::WordRight, request.extend); break;
    case EditCommand::DocumentHome: applied = model.move(EditMove::DocumentHome, request.extend); break;
    case EditCommand::DocumentEnd: applied = model.move(EditMove::DocumentEnd, request.extend); break;
    case EditCommand::Newline: applied = model.replaceSelection(__hidden_ui_edit_commands::s_NewlineText); break;
    case EditCommand::Backspace: applied = model.backspace(); break;
    case EditCommand::Delete: applied = model.eraseForward(); break;
    case EditCommand::WordBackspace: applied = __hidden_ui_edit_commands::EraseWord(model, false); break;
    case EditCommand::WordDelete: applied = __hidden_ui_edit_commands::EraseWord(model, true); break;
    case EditCommand::SelectAll: applied = model.selectAll(); break;
    case EditCommand::Undo: applied = model.undo(); break;
    case EditCommand::Redo: applied = model.redo(); break;
    case EditCommand::Copy: result.clipboard = EditClipboardAction::Copy; break;
    case EditCommand::Cut: result.clipboard = EditClipboardAction::Cut; break;
    case EditCommand::Paste: result.clipboard = EditClipboardAction::Paste; break;
    case EditCommand::Submit: result.submitted = true; break;
    case EditCommand::Cancel: result.cancelled = true; break;
    default: break;
    }
    if(applied){
        result.textChanged = model.revision() != revision;
        result.selectionChanged = model.anchor() != anchor || model.caret() != caret;
    }
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

