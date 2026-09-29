// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_box_host.h"

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Ui::EditBoxResult UiEditBoxHost::edit(const Ui::WidgetState& widget, Ui::EditModel& model, const Ui::EditBoxOptions& options){
    return editInPopup(widget, model, options, m_context.popupToken());
}

Ui::EditBoxResult UiEditBoxHost::editInPopup(const Ui::WidgetState& widget, Ui::EditModel& model,
    const Ui::EditBoxOptions& options, const Ui::PopupToken& popup){
    Ui::EditBoxResult result;
    result.valid = true;
    result.focused = options.enabled && m_context.input().focus() == widget.id;
    const UiTextEditOwner owner{ widget.id, widget.declarationGeneration, model.instanceGeneration() };
    Entry* entry = find(widget.id);
    if(!entry){
        if(m_entries.size() == 256u){
            result.valid = false;
            return result;
        }
        m_entries.emplace_back(m_arena);
        entry = &m_entries.back();
        entry->owner = owner;
        entry->expected.capture(model);
    }
    else if(entry->owner != owner || entry->popup != popup || !entry->expected.matches(model)){
        if(
            m_session.owner() == entry->owner || (m_clipboard.pending() && m_clipboardOwner == entry->owner)
            || (m_primary.pending() && m_primaryOwner == entry->owner)
        )
            cancelTransfers();
        discard(entry->owner);
        entry->owner = owner;
        entry->dragging = false;
        model.cancelComposition();
        entry->expected.capture(model);
    }
    entry->widget = widget;
    entry->popup = popup;
    entry->seen = m_generation;
    const u64 beforeRevision = model.revision();
    const usize beforeAnchor = model.anchor();
    const usize beforeCaret = model.caret();
    if(!result.focused || options.readOnly){
        if(m_session.owner() == owner)
            cancelTransfers();
        model.cancelComposition();
    }
    bool inputMethod = false;
    for(usize index = 0u; index < m_events.size();){
        if(m_events[index].owner != owner){
            ++index;
            continue;
        }
        Event event = Move(m_events[index]);
        m_queuedTextBytes -= event.native.text.size();
        m_events.erase(m_events.begin() + static_cast<isize>(index));
        if(options.enabled){
            apply(*entry, model, options, event, result);
            inputMethod = event.kind == UiEditBoxEventKind::Native;
        }
    }
    // Earlier edit intentions cancel transfers before a completion can mutate their original selection.
    if(m_clipboard.pending() && m_clipboardOwner == owner){
        if(!result.focused){
            if(!m_clipboard.cancel())
                TerminateInvariant();
        }
        else{
            if(m_clipboard.action() == Ui::EditClipboardAction::Cut || m_clipboard.action() == Ui::EditClipboardAction::Paste){
                if(m_session.owner() == owner && !m_session.cancel())
                    TerminateInvariant();
            }
            const auto completion = m_clipboard.drain(owner, model, options.readOnly);
            result.textChanged |= completion.textChanged;
            result.selectionChanged |= completion.selectionChanged;
        }
    }
    if(m_primary.pending() && m_primaryOwner == owner){
        const auto completion = m_primary.drain(owner, model, options.readOnly);
        result.textChanged |= completion.textChanged;
        result.selectionChanged |= completion.selectionChanged;
    }
    result.textChanged |= beforeRevision != model.revision();
    result.selectionChanged |= beforeAnchor != model.anchor() || beforeCaret != model.caret();
    result.focused = options.enabled && m_context.input().focus() == widget.id;
    result.preeditCaretVisible = entry->preeditCaretVisible;
    synchronizeSession(*entry, model, options, inputMethod);
    entry->expected.capture(model);
    return result;
}

void UiEditBoxHost::synchronizeSession(Entry& entry, Ui::EditModel& model, const Ui::EditBoxOptions& options, const bool inputMethod){
    if(!options.enabled || options.readOnly || m_context.input().focus() != entry.widget.id){
        if(m_session.owner() == entry.owner && !m_session.cancel())
            TerminateInvariant();
        model.cancelComposition();
        return;
    }
    if(
        entry.displayed.generation == 0u || entry.displayed.modelGeneration != entry.owner.modelGeneration
        || entry.displayed.popup != entry.popup
    )
        return;
    if(m_session.token().valid() && m_session.owner() != entry.owner && !m_session.cancel())
        TerminateInvariant();
    if(!m_session.token().valid()){
        model.cancelComposition();
        const auto admission = m_session.begin(entry.owner, model, nativeCaret(entry.displayed));
        if(admission != Core::TextInputAdmission::Accepted)
            return;
        m_lastNativeSequence = 0u;
        entry.rejectedNative = {};
    }
    else{
        const auto admission = m_session.adoptLocal(entry.owner, model, nativeCaret(entry.displayed), inputMethod
            ? Core::TextInputChangeCause::InputMethod : Core::TextInputChangeCause::Other
        );
        if(admission != Core::TextInputAdmission::Accepted){
            model.cancelComposition();
            if(!m_session.cancel())
                TerminateInvariant();
            return;
        }
    }
    m_nativePublished.capture(model);
}

void UiEditBoxHost::apply(Entry& entry, Ui::EditModel& model, const Ui::EditBoxOptions& options,
    Event& event, Ui::EditBoxResult& result){
    const bool clipboard = m_clipboard.cancel();
    const bool primary = m_primary.cancel();
    if(!clipboard || !primary)
        TerminateInvariant();
    const bool composing = model.composition().active;
    if(event.kind == UiEditBoxEventKind::Key){
        const auto command = Ui::ApplyEditCommand(model, Ui::TranslateEditCommand(event.key), options.readOnly);
        result.submitted |= command.submitted;
        result.cancelled |= command.cancelled;
        if(command.cancelled){
            if(!m_context.input().dismissPopup(Ui::PopupDismissReason::Escape))
                m_context.input().clearFocus();
        }
        if(command.clipboard != Ui::EditClipboardAction::None){
            if(command.clipboard == Ui::EditClipboardAction::Copy){
                if(!model.hasSelection())
                    return;
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
            return;
    }
    else if(event.kind == UiEditBoxEventKind::Native){
        if(options.readOnly || event.native.token == entry.rejectedNative)
            return;
        if(
            event.native.kind == Core::TextInputEventKind::Commit && event.native.text.size() == 1u
            && (static_cast<u8>(event.native.text.front()) < 32u || event.native.text.front() == 127)
        )
            return;
        const bool matching = model.revision() == event.surroundingModelRevision
            && model.externalRevision() == event.geometryExternalRevision
            && model.anchor() == event.surroundingAnchor && model.caret() == event.surroundingCaret;
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
            return;
        const usize anchor = event.dragging && entry.dragging ? entry.dragAnchor : event.extend ? model.anchor() : event.position;
        if(!model.setSelection(anchor, event.position))
            return;
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
    if(composing && !model.composition().active && event.kind != UiEditBoxEventKind::Native){
        entry.rejectedNative = m_session.token();
        if(m_session.owner() == entry.owner && !m_session.cancel())
            TerminateInvariant();
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

