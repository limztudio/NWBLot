// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_box_host.h"

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
        if(
            index == Core::s_TextInputMaxEvents || event.native.token != m_session.token()
            || event.native.sequence <= m_lastNativeSequence
        ){
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
        event.surroundingModelRevision = m_nativePublished.m_expectedRevision;
        event.surroundingSelectionGeneration = m_nativePublished.m_expectedSelectionGeneration;
        event.geometryExternalRevision = m_nativePublished.m_expectedExternalRevision;
        event.surroundingAnchor = m_nativePublished.m_expectedAnchor;
        event.surroundingCaret = m_nativePublished.m_expectedCaret;
        if(!append(Move(event)))
            return;
    }
}

void UiEditBoxHost::input(const Ui::InputEvent& input, const Ui::WidgetId previousCapture){
    if(rejectBorrowedMutation())
        return;
    if(input.type == Ui::InputEventType::FocusLost){
        reset();
        return;
    }
    synchronizeFocus();
    if(input.type == Ui::InputEventType::PointerMove)
        m_clickTracker.move(input.position);
    if(input.type == Ui::InputEventType::PointerCaptureLost){
        m_clickTracker.cancel();
        for(auto& entry : m_entries)
            entry.dragging = false;
        for(auto& entry : m_entries)
            entry.wordDragging = false;
        for(usize index = m_events.size(); index > 0u; --index){
            if(m_events[index - 1u].kind == UiEditBoxEventKind::Selection)
                m_events.erase(m_events.begin() + static_cast<isize>(index - 1u));
        }
        return;
    }
    if(input.type == Ui::InputEventType::CommandDown){
        m_clickTracker.cancel();
        Entry* entry = find(m_context.input().focus());
        if(!input.edit || !entry || !hasTextFocus() || !m_context.input().canEditCommand(input))
            return;
        Event event(m_arena);
        event.owner = entry->owner;
        event.focusGeneration = entry->focusGeneration;
        const Ui::InputCommandIntent intent{ input.command, input.extend, input.edit };
        event.command = Ui::TranslateEditCommand(intent, input.repeat, entry->displayed.textMode);
        const auto navigation = Ui::TranslateEditNavigation(intent);
        event.navigation = navigation.has_value();
        if(navigation){
            event.navigationDirection = *navigation;
            if(entry->navigationInstanceGeneration == 0u || entry->displayed.textMode != Ui::EditTextMode::Multiline)
                return;
            event.navigationViewportHeight = entry->displayed.placement.content.height;
        }
        else if(event.command.command == Ui::EditCommand::None)
            return;
        if(!append(Move(event)))
            return;
    }
    else if(
        input.type <= Ui::InputEventType::PrimaryUp
        && (input.type != Ui::InputEventType::PointerMove || previousCapture.valid())
    ){
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
        event.position = *byte;
        event.wordPosition = *wordByte;
        event.geometryRevision = entry->displayed.revision;
        event.geometryExternalRevision = entry->displayed.externalRevision;
        event.extend = input.shift;
        event.dragging = input.type != Ui::InputEventType::PrimaryDown;
        event.completed = input.type == Ui::InputEventType::PrimaryUp;
        if(input.type == Ui::InputEventType::PrimaryDown){
            event.wordSelect = m_clickTracker.press(entry->owner, entry->popup, entry->displayed.revision,
                entry->displayed.externalRevision, input.position, input.timestampMs, input.shift) == UiEditClickKind::Word;
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

bool UiEditBoxHost::character(const u32 unicode){
    if(rejectBorrowedMutation())
        return false;
    synchronizeFocus();
    if(!hasTextFocus())
        return false;
    if(m_session.token().valid())
        return true;
    if(unicode < 32u || unicode == 127u || unicode > 0x10ffffu || (unicode >= 0xd800u && unicode <= 0xdfffu))
        return true;
    Entry* entry = find(m_context.input().focus());
    Event event(m_arena);
    event.kind = UiEditBoxEventKind::Character;
    event.owner = entry->owner;
    event.focusGeneration = entry->focusGeneration;
    char bytes[4u] = {};
    usize count = 0u;
    if(unicode < 0x80u){
        bytes[0u] = static_cast<char>(unicode);
        count = 1u;
    }
    else if(unicode < 0x800u){
        bytes[0u] = static_cast<char>(0xc0u | (unicode >> 6u));
        bytes[1u] = static_cast<char>(0x80u | (unicode & 63u));
        count = 2u;
    }
    else if(unicode < 0x10000u){
        bytes[0u] = static_cast<char>(0xe0u | (unicode >> 12u));
        bytes[1u] = static_cast<char>(0x80u | ((unicode >> 6u) & 63u));
        bytes[2u] = static_cast<char>(0x80u | (unicode & 63u));
        count = 3u;
    }
    else{
        bytes[0u] = static_cast<char>(0xf0u | (unicode >> 18u));
        bytes[1u] = static_cast<char>(0x80u | ((unicode >> 12u) & 63u));
        bytes[2u] = static_cast<char>(0x80u | ((unicode >> 6u) & 63u));
        bytes[3u] = static_cast<char>(0x80u | (unicode & 63u));
        count = 4u;
    }
    event.native.text.assign(bytes, count);
    m_clickTracker.cancel();
    if(!append(Move(event)))
        return true;
    return true;
}

bool UiEditBoxHost::pastePrimary(const Ui::Point position){
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

bool UiEditBoxHost::append(Event&& event){
    if(m_events.size() == Ui::s_InputMaxEvents || event.native.text.size() > Core::s_TextInputMaxQueuedTextBytes - m_queuedTextBytes){
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

