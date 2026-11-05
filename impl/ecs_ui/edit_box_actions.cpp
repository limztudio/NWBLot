// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_box_host.h"

#include <global/simplemath.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 UiEditBoxHost::apply(const Ui::EditBoxOptions& options, Event& event, Entry& entry, Ui::EditModel& model, Ui::EditBoxResult& result, Ui::IEditActionSink* actions, NavigationBorrow* navigation){
    const u64 revision = model.revision();
    const u64 externalRevision = model.externalRevision();
    const u64 selectionGeneration = model.selectionGeneration();
    const u64 compositionGeneration = model.compositionGeneration();
    const b8 clipboard = m_clipboard.cancel();
    const b8 primary = m_primary.cancel();
    if(!clipboard || !primary)
        TerminateInvariant();
    if(m_borrowRejected)
        return false;
    const b8 composing = model.composition().active;
    if(event.kind == UiEditBoxEventKind::Command){
        if(event.navigation)
            return navigation ? applyNavigation(event, event.navigationDirection, model, *navigation) : true;
        const auto command = Ui::ApplyEditCommand(model, event.command, options.readOnly);
        result.submitted |= command.submitted;
        result.cancelled |= command.cancelled;
        if(actions && (command.submitted || command.cancelled)){
            if(!applyAction(options, command.submitted ? Ui::EditAction::Submit : Ui::EditAction::Cancel, entry, model, result, *actions, navigation))
                return false;
        }
        if(command.cancelled){
            const b8 cancelFocus = !actions || (entry.focused && entry.focusGeneration == event.focusGeneration && m_context.input().focus() == entry.widget.id);
            if(actions){
                entry.retiredFocusGeneration = Max(entry.retiredFocusGeneration, event.focusGeneration);
                if(entry.focusGeneration == event.focusGeneration)
                    entry.focused = false;
            }
            if(cancelFocus){
                if(!m_context.input().dismissPopup(Ui::PopupDismissReason::Cancel))
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
        if(event.native.kind == Core::TextInputEventKind::Commit && event.native.text.size() == 1u && !IsTextInputCodePoint(static_cast<u8>(event.native.text.front())))
            return true;
        const b8 matching = model.revision() == event.surroundingModelRevision
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
        if(model.revision() != event.geometryRevision || model.externalRevision() != event.geometryExternalRevision){
            if(event.completed){
                entry.dragging = false;
                entry.wordDragging = false;
            }
            return true;
        }
        if(event.wordSelect){
            const auto range = model.wordRangeAt(event.wordPosition);
            if(!range || !model.setSelection(range->begin, range->end))
                return true;
            entry.wordDragStart = range->begin;
            entry.wordDragEnd = range->end;
            entry.wordDragging = true;
        }
        else if(event.dragging && entry.dragging && entry.wordDragging){
            const auto range = model.wordRangeAt(event.wordPosition);
            if(!range)
                return true;
            const Ui::EditWordRange selection = ResolveUiEditWordDragSelection(event.wordPosition, entry.wordDragStart, entry.wordDragEnd, *range);
            if(!model.setSelection(selection.begin, selection.end))
                return true;
        }
        else{
            const usize anchor = ResolveUiEditSelectionAnchor(event.dragging, entry.dragging, entry.dragAnchor, event.extend, model.anchor(), event.position);
            if(!model.setSelection(anchor, event.position))
                return true;
            entry.dragAnchor = anchor;
            entry.wordDragging = false;
        }
        entry.dragging = !event.completed;
        if(event.completed)
            entry.wordDragging = false;
        if(model.hasSelection() && m_clipboardService.capabilities(Core::ClipboardChannel::PrimarySelection).writeText){
            const auto request = m_primary.request(entry.owner, model, Ui::EditClipboardAction::PublishSelection, Core::ClipboardChannel::PrimarySelection, options.readOnly);
            if(request.status == UiEditClipboardStatus::Pending)
                m_primaryOwner = entry.owner;
        }
    }
    else if(event.kind == UiEditBoxEventKind::PastePrimary){
        const auto request = m_clipboard.request(entry.owner, model, Ui::EditClipboardAction::Paste, Core::ClipboardChannel::PrimarySelection, options.readOnly);
        if(request.status == UiEditClipboardStatus::Pending)
            m_clipboardOwner = entry.owner;
    }
    else if(event.kind == UiEditBoxEventKind::Blur && actions){
        entry.retiredFocusGeneration = event.focusGeneration;
        return applyAction(options, Ui::EditAction::Blur, entry, model, result, *actions, navigation);
    }
    else if(event.kind == UiEditBoxEventKind::Focus && navigation){
        navigation->state.reset();
    }
    if(composing && !model.composition().active && event.kind != UiEditBoxEventKind::Native){
        entry.rejectedNative = m_session.token();
        if(m_session.owner() == entry.owner && !m_session.cancel())
            TerminateInvariant();
    }
    if(m_borrowRejected)
        return false;
    if(navigation && (model.revision() != revision || model.externalRevision() != externalRevision || model.selectionGeneration() != selectionGeneration || model.compositionGeneration() != compositionGeneration))
        navigation->state.reset();
    return true;
}

b8 UiEditBoxHost::applyAction(const Ui::EditBoxOptions& options, const Ui::EditAction::Enum action, Entry& entry, Ui::EditModel& model, Ui::EditBoxResult& result, Ui::IEditActionSink& actions, NavigationBorrow* navigation){
    const u64 revision = model.revision();
    const u64 selectionGeneration = model.selectionGeneration();
    const u64 compositionGeneration = model.compositionGeneration();
    if(m_clipboard.pending() && m_clipboardOwner == entry.owner && !m_clipboard.cancel())
        TerminateInvariant();
    if(m_primary.pending() && m_primaryOwner == entry.owner && !m_primary.cancel())
        TerminateInvariant();
    if(action == Ui::EditAction::Blur || action == Ui::EditAction::Abandon)
        model.cancelComposition();
    const u64 externalRevision = model.externalRevision();
    if(m_borrowRejected || !actions.apply(model, action, options.readOnly) || m_borrowRejected)
        return false;
    if(
        navigation
        && ShouldResetUiEditNavigationAfterAction(
            action,
            revision,
            externalRevision,
            selectionGeneration,
            compositionGeneration,
            model.revision(),
            model.externalRevision(),
            model.selectionGeneration(),
            model.compositionGeneration()
        )
    )
        navigation->state.reset();
    MergeUiEditActionToResult(action, result);
    if(action != Ui::EditAction::Submit || model.externalRevision() != externalRevision){
        // Copied commits still apply at their ordered position; surrounding deletion cannot address this retired session.
        if(m_session.owner() == entry.owner && !m_session.cancel())
            TerminateInvariant();
    }
    return !m_borrowRejected;
}

b8 UiEditBoxHost::applyNavigation(const Event& event, const Ui::EditNavigationDirection::Enum direction, Ui::EditModel& model, NavigationBorrow& navigation){
    if(m_borrowRejected || m_context.failed())
        return false;
    if(model.textMode() != Ui::EditTextMode::Multiline || model.composition().active)
        return true;
    if(!IsValidUiEditNavigationViewportHeight(direction, event.navigationViewportHeight))
        return false;
    const Ui::TextAreaModelStamp expected = model.stamp();
    const Ui::EditNavigationSnapshot preferred = navigation.state.snapshot();
    const auto target = navigation.resolver.resolve(model, direction, preferred, event.navigationViewportHeight);
    if(m_borrowRejected || m_context.failed() || !model.matchesStamp(expected) || model.composition().active || !navigation.state.matches(preferred) || !target || !IsFinite(target->preferredX))
        return false;
    if(!IsCommittedByteOnGraphemeBoundary(model.graphemeBoundaries(), target->committedByte))
        return false;
    if(!model.setSelection(event.command.extend ? expected.anchor : target->committedByte, target->committedByte))
        return false;
    if(!navigation.state.setPreferredX(target->preferredX))
        TerminateInvariant();
    return true;
}

b8 UiEditBoxHost::rejectBorrowedMutation()noexcept{
    if(!m_borrowed)
        return false;
    m_borrowRejected = true;
    return true;
}

u64 UiEditBoxHost::nextFocusGeneration()noexcept{
    if(m_focusGeneration == Limit<u64>::s_Max)
        TerminateInvariant();
    return ++m_focusGeneration;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

