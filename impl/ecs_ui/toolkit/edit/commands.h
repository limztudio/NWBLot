// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "model.h"

#include <impl/ecs_ui/toolkit/edit/edit_command_mapping.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline b8 EraseEditWord(EditModel& model, const b8 forward){
    if(model.hasSelection())
        return model.replaceSelection({});
    const usize caret = model.caret();
    if(!model.move(forward ? EditMove::WordRight : EditMove::WordLeft))
        return false;
    const usize boundary = model.caret();
    if(!model.setSelection(caret, caret))
        return false;
    usize beforeBytes = 0u;
    usize afterBytes = 0u;
    ComputeEditWordEraseDistances(forward, caret, boundary, beforeBytes, afterBytes);
    return model.eraseSurrounding(beforeBytes, afterBytes);
}

// Active preedit owns editing keys and Enter.
// Escape first cancels preedit without cancelling the widget.
// Clipboard actions describe requests; the host borrows its OS service to perform the actual exchange.
[[nodiscard]] inline EditCommandResult ApplyEditCommand(EditModel& model, const EditCommandRequest& request, const b8 readOnly = false){
    EditCommandResult result;
    if(request.command == EditCommand::None || request.command > EditCommand::Newline)
        return result;
    result.handled = true;
    if(request.repeat && ShouldSuppressEditCommandRepeat(request.command))
        return result;
    if(model.composition().active){
        if(request.command == EditCommand::Cancel){
            model.cancelComposition();
            result.compositionChanged = true;
        }
        return result;
    }
    if(readOnly && IsMutatingEditCommand(request.command))
        return result;
    EditCommandMapping mapping;
    if(const auto passiveResult = mapping.passiveResult(request.command))
        return *passiveResult;
    const u64 revision = model.revision();
    const usize anchor = model.anchor();
    const usize caret = model.caret();
    b8 applied = true;
    if(const auto movement = mapping.movement(request.command)){
        applied = model.move(*movement, request.extend);
    }
    else{
        switch(request.command){
        case EditCommand::Newline: applied = model.replaceSelection(s_EditNewlineText); break;
        case EditCommand::Backspace: applied = model.backspace(); break;
        case EditCommand::Delete: applied = model.eraseForward(); break;
        case EditCommand::WordBackspace: applied = EraseEditWord(model, false); break;
        case EditCommand::WordDelete: applied = EraseEditWord(model, true); break;
        case EditCommand::SelectAll: applied = model.selectAll(); break;
        case EditCommand::Undo: applied = model.undo(); break;
        case EditCommand::Redo: applied = model.redo(); break;
        default: break;
        }
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

