// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_box_host.h"

#include <global/simplemath.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool UiEditBoxHost::rejectBorrowedMutation(){
    if(!m_borrowed)
        return false;
    m_borrowRejected = true;
    return true;
}

u64 UiEditBoxHost::nextFocusGeneration(){
    if(m_focusGeneration == Limit<u64>::s_Max)
        TerminateInvariant();
    return ++m_focusGeneration;
}

bool UiEditBoxHost::applyAction(Entry& entry, Ui::EditModel& model, const Ui::EditBoxOptions& options,
    const Ui::EditAction::Enum action, Ui::EditBoxResult& result, Ui::IEditActionSink& actions){
    if(m_clipboard.pending() && m_clipboardOwner == entry.owner && !m_clipboard.cancel())
        TerminateInvariant();
    if(m_primary.pending() && m_primaryOwner == entry.owner && !m_primary.cancel())
        TerminateInvariant();
    if(action == Ui::EditAction::Blur || action == Ui::EditAction::Abandon)
        model.cancelComposition();
    const u64 externalRevision = model.externalRevision();
    if(m_borrowRejected || !actions.apply(model, action, options.readOnly) || m_borrowRejected)
        return false;
    result.submitted |= action == Ui::EditAction::Submit;
    result.cancelled |= action == Ui::EditAction::Cancel;
    result.blurred |= action == Ui::EditAction::Blur;
    result.abandoned |= action == Ui::EditAction::Abandon;
    if(action != Ui::EditAction::Submit || model.externalRevision() != externalRevision){
        // Copied commits still apply at their ordered position; surrounding deletion cannot address this retired session.
        if(m_session.owner() == entry.owner && !m_session.cancel())
            TerminateInvariant();
    }
    return !m_borrowRejected;
}

bool UiEditBoxHost::apply(Entry& entry, Ui::EditModel& model, const Ui::EditBoxOptions& options,
    Event& event, Ui::EditBoxResult& result, Ui::IEditActionSink* actions){
    const bool clipboard = m_clipboard.cancel();
    const bool primary = m_primary.cancel();
    if(!clipboard || !primary)
        TerminateInvariant();
    if(m_borrowRejected)
        return false;
    const bool composing = model.composition().active;
    if(event.kind == UiEditBoxEventKind::Key){
        const auto command = Ui::ApplyEditCommand(model, Ui::TranslateEditCommand(event.key), options.readOnly);
        result.submitted |= command.submitted;
        result.cancelled |= command.cancelled;
        if(actions && (command.submitted || command.cancelled)){
            if(!applyAction(entry, model, options, command.submitted ? Ui::EditAction::Submit : Ui::EditAction::Cancel, result, *actions))
                return false;
        }
        if(command.cancelled){
            const bool cancelFocus = !actions || (
                entry.focused && entry.focusGeneration == event.focusGeneration && m_context.input().focus() == entry.widget.id
            );
            if(actions){
                entry.retiredFocusGeneration = Max(entry.retiredFocusGeneration, event.focusGeneration);
                if(entry.focusGeneration == event.focusGeneration)
                    entry.focused = false;
            }
            if(cancelFocus){
                if(!m_context.input().dismissPopup(Ui::PopupDismissReason::Escape))
                    m_context.input().clearFocus();
            }
        }
        if(command.clipboard != Ui::EditClipboardAction::None){
            if(command.clipboard == Ui::EditClipboardAction::Copy){
                if(!model.hasSelection())
                    return true;
                drainPublications();
                const auto status = m_publications.request(model.selectedText());
                m_clipboardFailure |= status != UiClipboardPublicationStatus::Pending;
            }
            else{
                const auto request = m_clipboard.request(entry.owner, model, command.clipboard, Core::ClipboardChannel::Clipboard, options.readOnly);
                if(request.status == UiEditClipboardStatus::Pending)
                    m_clipboardOwner = entry.owner;
            }
        }
    }
    else if(event.kind == UiEditBoxEventKind::Character){
        if(!options.readOnly && !model.replaceSelection(event.native.text))
            return true;
    }
    else if(event.kind == UiEditBoxEventKind::Native){
        if(options.readOnly || event.native.token == entry.rejectedNative)
            return true;
        if(
            event.native.kind == Core::TextInputEventKind::Commit && event.native.text.size() == 1u
            && (static_cast<u8>(event.native.text.front()) < 32u || event.native.text.front() == 127)
        )
            return true;
        const bool matching = model.revision() == event.surroundingModelRevision
            && model.externalRevision() == event.geometryExternalRevision
            && model.selectionGeneration() == event.surroundingSelectionGeneration
            && model.anchor() == event.surroundingAnchor && model.caret() == event.surroundingCaret
            && (!actions || (m_session.owner() == entry.owner && m_session.token() == event.native.token));
        const auto status = ApplyUiTextEditEvent(model, event.native, event.surroundingRevision, matching);
        if(status != UiTextEditStatus::Applied){
            model.cancelComposition();
            entry.rejectedNative = event.native.token;
            if(m_session.owner() == entry.owner && !m_session.cancel())
                TerminateInvariant();
        }
        entry.preeditCaretVisible = event.native.kind != Core::TextInputEventKind::Preedit || event.native.caretVisible;
    }
    else if(event.kind == UiEditBoxEventKind::Selection){
        if(model.revision() != event.geometryRevision || model.externalRevision() != event.geometryExternalRevision)
            return true;
        const usize anchor = event.dragging && entry.dragging ? entry.dragAnchor : event.extend ? model.anchor() : event.position;
        if(!model.setSelection(anchor, event.position))
            return true;
        entry.dragAnchor = anchor;
        entry.dragging = !event.completed;
        if(model.hasSelection() && m_clipboardService.capabilities(Core::ClipboardChannel::PrimarySelection).writeText){
            const auto request = m_primary.request(entry.owner, model, Ui::EditClipboardAction::PublishSelection,
                Core::ClipboardChannel::PrimarySelection, options.readOnly
            );
            if(request.status == UiEditClipboardStatus::Pending)
                m_primaryOwner = entry.owner;
        }
    }
    else if(event.kind == UiEditBoxEventKind::PastePrimary){
        const auto request = m_clipboard.request(entry.owner, model, Ui::EditClipboardAction::Paste,
            Core::ClipboardChannel::PrimarySelection, options.readOnly
        );
        if(request.status == UiEditClipboardStatus::Pending)
            m_clipboardOwner = entry.owner;
    }
    else if(event.kind == UiEditBoxEventKind::Blur && actions){
        entry.retiredFocusGeneration = event.focusGeneration;
        return applyAction(entry, model, options, Ui::EditAction::Blur, result, *actions);
    }
    if(composing && !model.composition().active && event.kind != UiEditBoxEventKind::Native){
        entry.rejectedNative = m_session.token();
        if(m_session.owner() == entry.owner && !m_session.cancel())
            TerminateInvariant();
    }
    return !m_borrowRejected;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

