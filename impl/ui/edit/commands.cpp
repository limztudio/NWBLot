// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "commands.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_edit_commands{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool IsMutation(const EditCommand::Enum command){
    return
        command == EditCommand::Backspace || command == EditCommand::Delete || command == EditCommand::WordBackspace
        || command == EditCommand::WordDelete || command == EditCommand::Cut || command == EditCommand::Paste
        || command == EditCommand::Undo || command == EditCommand::Redo
    ;
}

static bool SuppressRepeat(const EditCommand::Enum command){
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


EditCommandRequest TranslateEditCommand(const EditKeyStroke& stroke){
    EditCommandRequest request{ EditCommand::None, stroke.shift, stroke.repeat };
    if(stroke.alt || stroke.key == EditKey::None || stroke.key > EditKey::Escape)
        return request;
    switch(stroke.key){
    case EditKey::Left: request.command = stroke.control ? EditCommand::WordLeft : EditCommand::Left; break;
    case EditKey::Right: request.command = stroke.control ? EditCommand::WordRight : EditCommand::Right; break;
    case EditKey::Home: request.command = EditCommand::Home; break;
    case EditKey::End: request.command = EditCommand::End; break;
    case EditKey::Backspace: request.command = stroke.control ? EditCommand::WordBackspace : EditCommand::Backspace; break;
    case EditKey::Delete: request.command = stroke.control ? EditCommand::WordDelete : EditCommand::Delete; break;
    case EditKey::Enter: request.command = EditCommand::Submit; break;
    case EditKey::Escape: request.command = EditCommand::Cancel; break;
    default:
        if(!stroke.control)
            break;
        switch(stroke.key){
        case EditKey::A: request.command = EditCommand::SelectAll; break;
        case EditKey::C: request.command = EditCommand::Copy; break;
        case EditKey::X: request.command = EditCommand::Cut; break;
        case EditKey::V: request.command = EditCommand::Paste; break;
        case EditKey::Z: request.command = stroke.shift ? EditCommand::Redo : EditCommand::Undo; break;
        case EditKey::Y: request.command = EditCommand::Redo; break;
        default: break;
        }
        break;
    }
    return request;
}

EditCommandResult ApplyEditCommand(EditModel& model, const EditCommandRequest& request, const bool readOnly){
    EditCommandResult result;
    if(request.command == EditCommand::None || request.command > EditCommand::Cancel)
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

