// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_box_host.h"

#include <global/scope_exit.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void UiEditBoxHost::collectNative(){
    if(rejectBorrowedMutation())
        return;
    for(usize index = 0u; m_session.token().valid() && index <= Core::s_TextInputMaxEvents; ++index){
        Event event(m_arena);
        const auto poll = m_session.pollOwned(event.native);
        if(poll == Core::TextInputPollResult::Pending)
            return;
        if(poll != Core::TextInputPollResult::Event){
            discard(m_session.owner());
            if(!m_session.cancel())
                TerminateInvariant();
            return;
        }
        if(index == Core::s_TextInputMaxEvents || event.native.token != m_session.token() || event.native.sequence <= m_lastNativeSequence){
            reset();
            return;
        }
        m_lastNativeSequence = event.native.sequence;
        m_clickTracker.cancel();
        event.owner = m_session.owner();
        const Entry* entry = find(event.owner.widget);
        event.focusGeneration = entry && entry->owner == event.owner ? entry->focusGeneration : 0u;
        event.kind = UiEditBoxEventKind::Native;
        event.surroundingRevision = m_session.surroundingRevision();
        event.surroundingModelRevision = m_nativePublished.revision;
        event.surroundingSelectionGeneration = m_nativePublished.selectionGeneration;
        event.geometryExternalRevision = m_nativePublished.externalRevision;
        event.surroundingAnchor = m_nativePublished.anchor;
        event.surroundingCaret = m_nativePublished.caret;
        if(event.native.kind == Core::TextInputEventKind::Preedit && !event.native.text.empty())
            m_context.input().cancelSubmitCommands(event.owner.widget);
        if(!append(Move(event)))
            return;
    }
}

void UiEditBoxHost::input(const Ui::InputEvent& input, const Ui::WidgetId previousCapture, const b8 submit){
    if(rejectBorrowedMutation())
        return;
    const b8 selectionRelease = input.type == Ui::InputEventType::PrimaryUp && input.source == m_selectionSource;
    ScopeExit finishSelection([this, selectionRelease]()noexcept{
        if(selectionRelease){
            m_activeSelectionSequence = 0u;
            m_selectionSource = {};
        }
    });

    if(input.type == Ui::InputEventType::FocusLost){
        reset();
        return;
    }
    synchronizeFocus();
    if(input.type == Ui::InputEventType::PointerMove && m_activeSelectionSequence != 0u && input.source == m_selectionSource)
        m_clickTracker.move(input.position);
    if(input.type == Ui::InputEventType::PointerCaptureLost){
        if(m_activeSelectionSequence == 0u || input.source != m_selectionSource)
            return;
        m_clickTracker.cancel();
        for(auto& entry : m_entries){
            entry.dragging = false;
            entry.wordDragging = false;
        }
        for(usize index = m_events.size(); index > 0u; --index){
            const Event& event = m_events[index - 1u];
            if(event.kind == UiEditBoxEventKind::Selection && event.selectionSequence == m_activeSelectionSequence && event.selectionSource == input.source)
                m_events.erase(m_events.begin() + static_cast<isize>(index - 1u));
        }
        m_activeSelectionSequence = 0u;
        m_selectionSource = {};
        return;
    }
    if(input.type == Ui::InputEventType::CommandDown || input.type == Ui::InputEventType::CommandUp){
        m_clickTracker.cancel();
        Entry* entry = find(m_context.input().focus());
        const b8 released = input.type == Ui::InputEventType::CommandUp;
        if(!input.edit || !entry || !hasTextFocus() || (released ? !submit : !m_context.input().canEditCommand(input)))
            return;
        Event event(m_arena);
        event.owner = entry->owner;
        event.focusGeneration = entry->focusGeneration;
        if(released)
            event.command.command = Ui::EditCommand::Submit;
        else{
            const Ui::InputCommandIntent intent{ input.command, input.extend, input.edit };
            event.command = Ui::TranslateEditCommand(intent, input.repeat, entry->displayed.textMode);
            if(event.command.command == Ui::EditCommand::Submit){
                if(compositionActive(*entry))
                    m_context.input().cancelSubmitCommands(entry->widget.id);
                return;
            }
            const auto navigationDirection = Ui::TryTranslateEditNavigation(intent);
            event.navigation = navigationDirection.has_value();
            if(navigationDirection){
                event.navigationDirection = *navigationDirection;
                if(entry->navigationInstanceGeneration == 0u || entry->displayed.textMode != Ui::EditTextMode::Multiline)
                    return;
                event.navigationViewportHeight = entry->displayed.placement.content.height;
            }
            else if(event.command.command == Ui::EditCommand::None)
                return;
        }
        if(!append(Move(event)))
            return;
    }
    else if((input.type == Ui::InputEventType::PrimaryDown || input.type == Ui::InputEventType::PrimaryUp || input.type == Ui::InputEventType::PointerMove) && (input.type != Ui::InputEventType::PointerMove || previousCapture.valid())){
        if(input.type == Ui::InputEventType::PrimaryDown){
            if(m_selectionSequence == Limit<u64>::s_Max)
                TerminateInvariant();
            m_activeSelectionSequence = ++m_selectionSequence;
            m_selectionSource = input.source;
        }
        else if(m_activeSelectionSequence == 0u || input.source != m_selectionSource)
            return;
        const Ui::WidgetId target = input.type == Ui::InputEventType::PrimaryDown ? m_context.input().focus() : previousCapture;
        if(input.type == Ui::InputEventType::PrimaryDown && m_context.input().capture() != target){
            m_clickTracker.cancel();
            return;
        }
        Entry* entry = find(target);
        if(!entry){
            m_clickTracker.cancel();
            return;
        }
        const auto byte = hit(*entry, input.position);
        if(!byte){
            m_clickTracker.cancel();
            return;
        }
        const auto wordByte = hitWord(*entry, input.position);
        if(!wordByte){
            m_clickTracker.cancel();
            return;
        }
        Event event(m_arena);
        event.owner = entry->owner;
        event.focusGeneration = entry->focusGeneration;
        event.kind = UiEditBoxEventKind::Selection;
        event.selectionSequence = m_activeSelectionSequence;
        event.selectionSource = m_selectionSource;
        event.position = *byte;
        event.wordPosition = *wordByte;
        event.geometryRevision = entry->displayed.revision;
        event.geometryExternalRevision = entry->displayed.externalRevision;
        event.extend = input.extend;
        event.dragging = input.type != Ui::InputEventType::PrimaryDown;
        event.completed = input.type == Ui::InputEventType::PrimaryUp;
        if(input.type == Ui::InputEventType::PrimaryDown){
            const u64 clickTimestampMs = input.hoverCapable ? input.timestampMs : 0u;
            event.wordSelect = m_clickTracker.press(entry->owner, entry->popup, entry->displayed.revision, entry->displayed.externalRevision, input.position, clickTimestampMs, input.extend) == UiEditClickKind::Word;
        }
        else if(input.type == Ui::InputEventType::PrimaryUp)
            m_clickTracker.release(entry->owner);
        if(!append(Move(event)))
            return;
    }
    else if(input.type == Ui::InputEventType::PointerLeave || input.type == Ui::InputEventType::SecondaryDown)
        m_clickTracker.cancel();
    synchronizeFocus();
}

b8 UiEditBoxHost::character(const u32 unicode){
    if(rejectBorrowedMutation())
        return false;
    synchronizeFocus();
    if(!hasTextFocus())
        return false;
    if(m_session.token().valid())
        return true;
    if(!IsTextInputCodePoint(unicode))
        return true;
    const Entry& entry = *MakeNotNull(find(m_context.input().focus()));
    Event event(m_arena);
    event.kind = UiEditBoxEventKind::Character;
    event.owner = entry.owner;
    event.focusGeneration = entry.focusGeneration;
    Array<char, s_Utf8MaxSequenceBytes> bytes{};
    const auto count = EncodeUtf8CodePoint(unicode, bytes);
    if(!count)
        return true;
    event.native.text.assign(bytes.data(), *count);
    m_clickTracker.cancel();
    if(!append(Move(event)))
        return true;
    return true;
}

b8 UiEditBoxHost::pastePrimary(const Ui::Point position){
    if(rejectBorrowedMutation())
        return false;
    synchronizeFocus();
    const Ui::WidgetId target = m_context.input().hitTest(position);
    Entry* entry = find(target);
    if(!entry || !hasTextFocus() || m_context.input().focus() != target)
        return false;
    m_clickTracker.cancel();
    Event event(m_arena);
    event.owner = entry->owner;
    event.focusGeneration = entry->focusGeneration;
    event.kind = UiEditBoxEventKind::PastePrimary;
    return append(Move(event));
}

b8 UiEditBoxHost::prepareSubmit(const Ui::InputEvent& event)noexcept{
    if(rejectBorrowedMutation() || !m_context.input().canSubmitCommand(event))
        return false;
    const Entry* entry = find(m_context.input().focus());
    if(!entry || !hasTextFocus())
        return false;
    const Ui::InputCommandIntent intent{ event.command, event.extend, event.edit };
    if(Ui::TranslateEditCommand(intent, false, entry->displayed.textMode).command != Ui::EditCommand::Submit)
        return false;
    if(compositionActive(*entry)){
        m_context.input().cancelSubmitCommands(entry->widget.id);
        return false;
    }
    return true;
}

b8 UiEditBoxHost::compositionActive(const Entry& entry)const noexcept{
    b8 composing = entry.expected.m_expectedComposition.active;
    for(const Event& event : m_events){
        if(event.owner != entry.owner || event.focusGeneration != entry.focusGeneration)
            continue;
        if(event.kind == UiEditBoxEventKind::Native){
            if(event.native.kind == Core::TextInputEventKind::Preedit)
                composing = !event.native.text.empty();
            else if(event.native.kind == Core::TextInputEventKind::Cancelled || (event.native.kind == Core::TextInputEventKind::Commit && !event.native.text.empty()))
                composing = false;
        }
        else if(event.kind == UiEditBoxEventKind::Command && event.command.command == Ui::EditCommand::Cancel)
            composing = false;
    }
    return composing;
}

b8 UiEditBoxHost::append(Event&& event){
    if(!CanAppendUiEditBoxEvent(m_events.size(), Ui::s_InputMaxEvents, event.native.text.size(), m_queuedTextBytes, Core::s_TextInputMaxQueuedTextBytes)){
        reset();
        return false;
    }
    m_queuedTextBytes += event.native.text.size();
    m_events.push_back(Move(event));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

