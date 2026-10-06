// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "commands.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_edit_commands{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool IsMutation(const EditCommand::Enum command)noexcept{
    return
        command == EditCommand::Backspace || command == EditCommand::Delete || command == EditCommand::WordBackspace
        || command == EditCommand::WordDelete || command == EditCommand::Cut || command == EditCommand::Paste
        || command == EditCommand::Undo || command == EditCommand::Redo || command == EditCommand::Newline
    ;
}

static bool SuppressRepeat(const EditCommand::Enum command)noexcept{
    return
        command == EditCommand::SelectAll || command == EditCommand::Copy || command == EditCommand::Cut
        || command == EditCommand::Paste || command == EditCommand::Submit || command == EditCommand::Cancel
    ;
}

static bool EraseWord(EditModel& model, const bool forward){
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


EditCommandRequest TranslateEditCommand(const InputCommandIntent& intent, const bool repeat, const EditTextMode::Enum mode)noexcept{
    EditCommandRequest request{ EditCommand::None, intent.extend, repeat };
    if(!intent.edit || mode > EditTextMode::Multiline)
        return request;
    switch(intent.command){
    case InputCommand::Left: request.command = EditCommand::Left; break;
    case InputCommand::Right: request.command = EditCommand::Right; break;
    case InputCommand::Home: request.command = EditCommand::Home; break;
    case InputCommand::End: request.command = EditCommand::End; break;
    case InputCommand::WordLeft: request.command = EditCommand::WordLeft; break;
    case InputCommand::WordRight: request.command = EditCommand::WordRight; break;
    case InputCommand::DocumentHome:
        request.command = mode == EditTextMode::Multiline ? EditCommand::DocumentHome : EditCommand::Home;
        break;
    case InputCommand::DocumentEnd:
        request.command = mode == EditTextMode::Multiline ? EditCommand::DocumentEnd : EditCommand::End;
        break;
    case InputCommand::Backspace: request.command = EditCommand::Backspace; break;
    case InputCommand::Delete: request.command = EditCommand::Delete; break;
    case InputCommand::WordBackspace: request.command = EditCommand::WordBackspace; break;
    case InputCommand::WordDelete: request.command = EditCommand::WordDelete; break;
    case InputCommand::SelectAll: request.command = EditCommand::SelectAll; break;
    case InputCommand::Copy: request.command = EditCommand::Copy; break;
    case InputCommand::Cut: request.command = EditCommand::Cut; break;
    case InputCommand::Paste: request.command = EditCommand::Paste; break;
    case InputCommand::Undo: request.command = EditCommand::Undo; break;
    case InputCommand::Redo: request.command = EditCommand::Redo; break;
    case InputCommand::Accept:
        request.command = mode == EditTextMode::Multiline ? EditCommand::Newline : EditCommand::Submit;
        break;
    case InputCommand::Submit: request.command = EditCommand::Submit; break;
    case InputCommand::Cancel: request.command = EditCommand::Cancel; break;
    case InputCommand::Newline: request.command = EditCommand::Newline; break;
    default: break;
    }
    return request;
}

EditCommandResult ApplyEditCommand(EditModel& model, const EditCommandRequest& request, const bool readOnly){
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
    bool applied = true;
    switch(request.command){
    case EditCommand::Left: applied = model.move(EditMove::Left, request.extend); break;
    case EditCommand::Right: applied = model.move(EditMove::Right, request.extend); break;
    case EditCommand::Home: applied = model.move(EditMove::Home, request.extend); break;
    case EditCommand::End: applied = model.move(EditMove::End, request.extend); break;
    case EditCommand::WordLeft: applied = model.move(EditMove::WordLeft, request.extend); break;
    case EditCommand::WordRight: applied = model.move(EditMove::WordRight, request.extend); break;
    case EditCommand::DocumentHome: applied = model.move(EditMove::DocumentHome, request.extend); break;
    case EditCommand::DocumentEnd: applied = model.move(EditMove::DocumentEnd, request.extend); break;
    case EditCommand::Newline: applied = model.replaceSelection("\n"); break;
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

