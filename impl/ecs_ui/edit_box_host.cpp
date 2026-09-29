// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_box_host.h"

#include <global/simplemath.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void UiEditModelSnapshot::capture(const Ui::EditModel& model){
    text.assign(model.text().data(), model.text().size());
    anchor = model.anchor();
    caret = model.caret();
    revision = model.revision();
    externalRevision = model.externalRevision();
    compositionGeneration = model.compositionGeneration();
    selectionGeneration = model.selectionGeneration();
    composition = model.composition();
    preedit.assign(composition.text.data(), composition.text.size());
    composition.text = {};
}

bool UiEditModelSnapshot::matches(const Ui::EditModel& model)const{
    const auto current = model.composition();
    return
        text == model.text() && revision == model.revision() && externalRevision == model.externalRevision()
        && compositionGeneration == model.compositionGeneration() && selectionGeneration == model.selectionGeneration()
        && anchor == model.anchor() && caret == model.caret() && preedit == current.text
        && composition.active == current.active && composition.anchor == current.anchor && composition.caret == current.caret
        && composition.replacementStart == current.replacementStart && composition.replacementEnd == current.replacementEnd
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiEditBoxHost::UiEditBoxHost(Core::Alloc::GlobalArena& arena, Ui::Context& context,
    Core::ITextInputService& textInput, Core::IClipboardService& clipboard)
    : m_arena(arena)
    , m_context(context)
    , m_textInput(textInput)
    , m_clipboardService(clipboard)
    , m_session(arena, textInput)
    , m_clipboard(arena, clipboard)
    , m_primary(arena, clipboard)
    , m_publications(arena, clipboard)
    , m_entries(arena)
    , m_events(arena)
    , m_nativePublished(arena)
{
    m_entries.reserve(256u);
    m_events.reserve(Ui::s_InputMaxEvents);
}

UiEditBoxHost::~UiEditBoxHost(){ cancelTransfers(); }

bool UiEditBoxHost::publish(const Ui::WidgetState& widget, const Ui::EditBoxView& view,
    const Ui::EditBoxPlacement& placement, const Ui::EditBoxOptions& options){
    if(rejectBorrowedMutation())
        return false;
    Entry* entry = find(widget.id);
    if(!entry || entry->owner.declarationGeneration != widget.declarationGeneration || entry->popup != m_context.popupToken() || !view.ready())
        return false;
    UiEditBoxGeometry candidate(m_arena);
    candidate.popup = entry->popup;
    candidate.placement = placement;
    candidate.stops.assign(view.caretStops().begin(), view.caretStops().end());
    candidate.lines.assign(view.caretGeometry().lines().begin(), view.caretGeometry().lines().end());
    candidate.textMode = view.textMode();
    candidate.options = options;
    candidate.generation = m_generation;
    candidate.revision = entry->expected.revision;
    candidate.externalRevision = entry->expected.externalRevision;
    candidate.modelGeneration = entry->owner.modelGeneration;
    entry->candidate = Move(candidate);
    return true;
}

void UiEditBoxHost::beginFrame(const u64 generation, const Ui::DisplayMetrics& display){
    if(rejectBorrowedMutation())
        return;
    drainPublications();
    collectNative();
    m_generation = generation;
    m_display = display;
}

void UiEditBoxHost::finishFrame(){
    if(rejectBorrowedMutation())
        return;
    for(usize index = m_entries.size(); index > 0u; --index){
        const auto& entry = m_entries[index - 1u];
        if(entry.seen == m_generation)
            continue;
        if(
            m_session.owner() == entry.owner || (m_clipboard.pending() && m_clipboardOwner == entry.owner)
            || (m_primary.pending() && m_primaryOwner == entry.owner)
        )
            cancelTransfers();
        discard(entry.owner);
        m_entries.erase(m_entries.begin() + static_cast<isize>(index - 1u));
    }
    synchronizeFocus();
}

void UiEditBoxHost::commitFrame(const u64 generation){
    if(rejectBorrowedMutation())
        return;
    if(m_context.input().layoutGeneration() != generation)
        return;
    for(auto& entry : m_entries){
        if(entry.candidate.generation == generation){
            entry.displayed = Move(entry.candidate);
            entry.candidate.generation = 0u;
        }
    }
    if(m_session.token().valid()){
        const Entry* entry = find(m_session.owner().widget);
        if(entry && entry->owner == m_session.owner() && entry->displayed.generation == generation){
            const auto status = m_textInput.updateCaret(m_session.token(), nativeCaret(entry->displayed));
            if(status != Core::TextInputAdmission::Accepted && !m_session.cancel())
                TerminateInvariant();
        }
    }
    synchronizeFocus();
}

bool UiEditBoxHost::hasTextFocus()const{
    const Entry* entry = find(m_context.input().focus());
    if(
        !entry || entry->displayed.generation == 0u || !entry->displayed.options.enabled
        || entry->displayed.modelGeneration != entry->owner.modelGeneration
        || entry->displayed.popup != entry->popup
    )
        return false;
    for(const auto& target : m_context.input().targets()){
        if(target.id == entry->widget.id && target.declarationGeneration == entry->owner.declarationGeneration)
            return target.enabled && target.textEditable && target.popup == entry->popup;
    }
    return false;
}

bool UiEditBoxHost::wantsTextInput()const{
    if(!m_context.input().windowFocused() || !hasTextFocus())
        return false;
    const Entry* entry = find(m_context.input().focus());
    return entry && !entry->displayed.options.readOnly;
}

bool UiEditBoxHost::takeClipboardFailure(){
    if(rejectBorrowedMutation())
        return false;
    const bool failed = m_clipboardFailure;
    m_clipboardFailure = false;
    return failed;
}

void UiEditBoxHost::synchronizeFocus(){
    if(rejectBorrowedMutation())
        return;
    const Entry* entry = find(m_context.input().focus());
    const bool focused = hasTextFocus() && entry;
    for(auto& candidate : m_entries){
        const bool active = focused && candidate.owner == entry->owner;
        if(candidate.actionCapable){
            if(active && !candidate.focused){
                candidate.focused = true;
                candidate.focusGeneration = nextFocusGeneration();
                if(candidate.navigationInstanceGeneration != 0u){
                    Event event(m_arena);
                    event.owner = candidate.owner;
                    event.focusGeneration = candidate.focusGeneration;
                    event.kind = UiEditBoxEventKind::Focus;
                    if(!append(Move(event)))
                        return;
                }
            }
            else if(!active && candidate.focused){
                Event event(m_arena);
                event.owner = candidate.owner;
                event.focusGeneration = candidate.focusGeneration;
                event.kind = UiEditBoxEventKind::Blur;
                candidate.focused = false;
                if(!append(Move(event)))
                    return;
            }
        }
        if(active)
            continue;
        if(!candidate.actionCapable)
            discard(candidate.owner);
        candidate.dragging = false;
    }
    if(
        (m_session.token().valid() && (!focused || entry->owner != m_session.owner()))
        || (m_clipboard.pending() && (!focused || entry->owner != m_clipboardOwner))
        || (m_primary.pending() && (!focused || entry->owner != m_primaryOwner))
    )
        cancelTransfers();
}

void UiEditBoxHost::reset(){
    if(rejectBorrowedMutation())
        return;
    cancelTransfers();
    m_entries.clear();
    m_events.clear();
    m_queuedTextBytes = 0u;
    m_context.input().clearFocus();
}

UiEditBoxHost::Entry* UiEditBoxHost::find(const Ui::WidgetId widget){
    for(auto& entry : m_entries){
        if(entry.widget.id == widget)
            return &entry;
    }
    return nullptr;
}

const UiEditBoxHost::Entry* UiEditBoxHost::find(const Ui::WidgetId widget)const{
    for(const auto& entry : m_entries){
        if(entry.widget.id == widget)
            return &entry;
    }
    return nullptr;
}

void UiEditBoxHost::discard(const UiTextEditOwner& owner){
    for(usize index = m_events.size(); index > 0u; --index){
        if(m_events[index - 1u].owner == owner){
            m_queuedTextBytes -= m_events[index - 1u].native.text.size();
            m_events.erase(m_events.begin() + static_cast<isize>(index - 1u));
        }
    }
}

void UiEditBoxHost::cancelTransfers(){
    const bool text = m_session.cancel();
    const bool clipboard = m_clipboard.cancel();
    const bool primary = m_primary.cancel();
    if(!text || !clipboard || !primary)
        TerminateInvariant();
}

void UiEditBoxHost::drainPublications(){
    const auto completion = m_publications.drain();
    m_clipboardFailure |= completion.failed != 0u || completion.status == UiClipboardPublicationStatus::WrongThread;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

