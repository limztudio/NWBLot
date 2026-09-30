// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_edit_session.h"

#include <core/os/text_input_text.h>
#include <impl/ecs_ui/toolkit/edit/multiline_text.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiTextEditStatus::Enum ApplyUiTextEditEvent(
    Ui::EditModel& model, const Core::TextInputEvent& event, const u64 expectedSurroundingRevision, const bool matchesPublishedModel){
    switch(event.kind){
    case Core::TextInputEventKind::Commit: {
        if(Core::ValidateTextInputUtf8(event.text, Core::s_TextInputMaxEventTextBytes) != Core::TextInputAdmission::Accepted)
            return UiTextEditStatus::InvalidEvent;
        if(event.text.empty())
            return UiTextEditStatus::Applied;
        if(model.textMode() == Ui::EditTextMode::Multiline){
            const auto composition = model.composition();
            const usize replaced = composition.active
                ? composition.replacementEnd - composition.replacementStart
                : model.selectionEnd() - model.selectionStart()
            ;
            const usize retainedBytes = model.text().size() - replaced;
            AString<Core::Alloc::GlobalArena> normalized(event.text.get_allocator());
            if(Ui::NormalizeMultilineText(event.text, normalized, model.limits().maxBytes - retainedBytes) != Ui::EditTextStatus::Accepted)
                return UiTextEditStatus::ModelRejected;
            return (composition.active ? model.commitComposition(normalized) : model.replaceSelection(normalized))
                ? UiTextEditStatus::Applied : UiTextEditStatus::ModelRejected
            ;
        }
        return (model.composition().active ? model.commitComposition(event.text) : model.replaceSelection(event.text))
            ? UiTextEditStatus::Applied : UiTextEditStatus::ModelRejected
        ;
    }
    case Core::TextInputEventKind::Preedit: {
        if(
            Core::ValidateTextInputUtf8(event.text, Core::s_TextInputMaxEventTextBytes) != Core::TextInputAdmission::Accepted
            || !Core::IsTextInputUtf8Boundary(event.text, event.anchorByte)
            || !Core::IsTextInputUtf8Boundary(event.text, event.caretByte)
        )
            return UiTextEditStatus::InvalidEvent;
        if(event.text.empty()){
            model.cancelComposition();
            return UiTextEditStatus::Applied;
        }
        const bool wasActive = model.composition().active;
        if(!wasActive && !model.beginComposition())
            return UiTextEditStatus::ModelRejected;
        if(model.updateComposition(event.text, event.anchorByte, event.caretByte))
            return UiTextEditStatus::Applied;
        if(!wasActive)
            model.cancelComposition();
        return UiTextEditStatus::ModelRejected;
    }
    case Core::TextInputEventKind::DeleteSurrounding: {
        if(expectedSurroundingRevision == 0u || event.surroundingRevision != expectedSurroundingRevision || !matchesPublishedModel)
            return UiTextEditStatus::StaleSurrounding;
        if(event.deletionBasis >= Core::TextInputDeletionBasis::kCount)
            return UiTextEditStatus::InvalidEvent;
        if(event.deletionBasis == Core::TextInputDeletionBasis::Selection){
            if(
                event.deleteBeforeBytes > model.selectionStart()
                || event.deleteAfterBytes > model.text().size() - model.selectionEnd()
            )
                return UiTextEditStatus::InvalidEvent;
            return model.eraseAroundSelection(event.deleteBeforeBytes, event.deleteAfterBytes)
                ? UiTextEditStatus::Applied : UiTextEditStatus::ModelRejected
            ;
        }
        if(model.hasSelection() && (event.deleteBeforeBytes != 0u || event.deleteAfterBytes != 0u))
            return UiTextEditStatus::ModelRejected;
        const usize caret = model.caret();
        if(event.deleteBeforeBytes > caret || event.deleteAfterBytes > model.text().size() - caret)
            return UiTextEditStatus::InvalidEvent;
        return model.eraseSurrounding(event.deleteBeforeBytes, event.deleteAfterBytes)
            ? UiTextEditStatus::Applied : UiTextEditStatus::ModelRejected
        ;
    }
    case Core::TextInputEventKind::Cancelled:
        model.cancelComposition();
        return UiTextEditStatus::Cancelled;
    default:
        return UiTextEditStatus::InvalidEvent;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

