// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_box_host.h"

#include <global/scope_exit.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Ui::EditBoxResult UiEditBoxHost::edit(const Ui::WidgetState& widget, Ui::EditModel& model, const Ui::EditBoxOptions& options){
    return editBorrowed(widget, model, options, m_context.popupToken(), nullptr);
}

Ui::EditBoxResult UiEditBoxHost::editInPopup(const Ui::WidgetState& widget, Ui::EditModel& model,
    const Ui::EditBoxOptions& options, const Ui::PopupToken& popup){
    return editBorrowed(widget, model, options, popup, nullptr);
}

Ui::EditBoxResult UiEditBoxHost::editActions(const Ui::WidgetState& widget, Ui::EditModel& model,
    const Ui::EditBoxOptions& options, const Ui::PopupToken& popup, Ui::IEditActionSink& actions){
    return editBorrowed(widget, model, options, popup, &actions);
}

Ui::EditBoxResult UiEditBoxHost::editBorrowed(const Ui::WidgetState& widget, Ui::EditModel& model,
    const Ui::EditBoxOptions& options, const Ui::PopupToken& popup, Ui::IEditActionSink* actions){
    if(rejectBorrowedMutation())
        return {};
    if(actions)
        synchronizeFocus();
    m_borrowed = true;
    m_borrowRejected = false;
    ScopeExit release([this]()noexcept{ m_borrowed = false; });

    Ui::EditBoxResult result;
    result.valid = true;
    result.focused = options.enabled && m_context.input().focus() == widget.id;
    const UiTextEditOwner owner{ widget.id, widget.declarationGeneration, model.instanceGeneration() };
    Entry* entry = find(widget.id);
    bool abandon = false;
    if(!entry){
        if(m_entries.size() == 256u){
            result.valid = false;
            return result;
        }
        m_entries.emplace_back(m_arena);
        entry = &m_entries.back();
        entry->owner = owner;
        entry->expected.capture(model);
        abandon = actions != nullptr;
    }
    else{
        const bool rebound = entry->owner != owner || entry->popup != popup || entry->actionCapable != (actions != nullptr);
        const bool policyChanged = actions && (entry->enabled != options.enabled || entry->readOnly != options.readOnly);
        abandon = actions && (rebound || policyChanged);
        if(rebound || policyChanged || !entry->expected.matches(model)){
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
            if(rebound || policyChanged){
                entry->focused = false;
                entry->focusGeneration = 0u;
                entry->retiredFocusGeneration = 0u;
            }
        }
    }
    entry->widget = widget;
    entry->popup = popup;
    entry->seen = m_generation;
    entry->actionCapable = actions != nullptr;
    entry->enabled = options.enabled;
    entry->readOnly = options.readOnly;
    if(actions && result.focused && !entry->focused){
        entry->focused = true;
        entry->focusGeneration = nextFocusGeneration();
    }
    const u64 beforeRevision = model.revision();
    const usize beforeAnchor = model.anchor();
    const usize beforeCaret = model.caret();
    if(m_borrowRejected){
        result.valid = false;
        return result;
    }
    if(abandon && !applyAction(*entry, model, options, Ui::EditAction::Abandon, result, *actions)){
        result.valid = false;
        discard(owner);
        return result;
    }
    if((!result.focused && !actions) || options.readOnly || !options.enabled){
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
        if(actions && event.focusGeneration <= entry->retiredFocusGeneration)
            continue;
        if(options.enabled){
            if(!apply(*entry, model, options, event, result, actions)){
                result.valid = false;
                discard(owner);
                if(
                    m_session.owner() == owner || (m_clipboard.pending() && m_clipboardOwner == owner)
                    || (m_primary.pending() && m_primaryOwner == owner)
                )
                    cancelTransfers();
                return result;
            }
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
    result.valid = !m_borrowRejected;
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

