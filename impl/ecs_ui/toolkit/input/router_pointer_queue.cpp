// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "router.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool InputRouter::IsPointerEvent(const InputEventType::Enum type)noexcept{
    switch(type){
    case InputEventType::PointerMove:
    case InputEventType::PrimaryDown:
    case InputEventType::PrimaryUp:
    case InputEventType::SecondaryDown:
    case InputEventType::SecondaryUp:
    case InputEventType::PointerWheel:
    case InputEventType::PointerLeave:
    case InputEventType::PointerCaptureLost:
        return true;
    default: return false;
    }
}

Expected<InputEvent> InputRouter::queuePointer(const PointerInput& input){
    if(input.phase > PointerPhase::Scroll || input.intent > PointerIntent::ContextMenu)
        return MakeUnexpected(Failure{});
    InputEvent event;
    switch(input.phase){
    case PointerPhase::Move: event.type = InputEventType::PointerMove; break;
    case PointerPhase::Press: event.type = input.intent == PointerIntent::Activate ? InputEventType::PrimaryDown : InputEventType::SecondaryDown; break;
    case PointerPhase::Release: event.type = input.intent == PointerIntent::Activate ? InputEventType::PrimaryUp : InputEventType::SecondaryUp; break;
    case PointerPhase::Cancel: event.type = InputEventType::PointerCaptureLost; break;
    case PointerPhase::Leave: event.type = InputEventType::PointerLeave; break;
    case PointerPhase::Scroll: event.type = InputEventType::PointerWheel; break;
    }
    event.source = input.source;
    event.position = input.position;
    event.scrollX = input.scrollX;
    event.scrollY = input.scrollY;
    event.timestampMs = input.timestampMs;
    event.extend = input.extend;
    event.hoverCapable = input.hoverCapable;
    return queue(event);
}

Expected<InputEvent> InputRouter::resolvePointerEvent(const InputEvent& event){
    if(!IsPointerEvent(event.type))
        return event;
    if(!event.source.valid() || !IsFinite(event.position.x) || !IsFinite(event.position.y))
        return MakeUnexpected(Failure{});
    if(event.type == InputEventType::PointerWheel && (!IsFinite(event.scrollX) || !IsFinite(event.scrollY)))
        return MakeUnexpected(Failure{});
    InputEvent resolved = event;
    const bool press = event.type == InputEventType::PrimaryDown || event.type == InputEventType::SecondaryDown;
    const bool release = event.type == InputEventType::PrimaryUp || event.type == InputEventType::SecondaryUp;
    const PointerIntent::Enum intent = event.type == InputEventType::SecondaryDown || event.type == InputEventType::SecondaryUp ? PointerIntent::ContextMenu : PointerIntent::Activate;
    const auto held = FindIf(m_pointerBindings.begin(), m_pointerBindings.end(), [&event, intent](const PointerBinding& binding)noexcept{
        return binding.source == event.source && binding.intent == intent;
    }
    );
    const auto contact = FindIf(m_pointerBindings.begin(), m_pointerBindings.end(), [&event](const PointerBinding& binding)noexcept{
        return binding.source == event.source;
    }
    );
    if(contact != m_pointerBindings.end())
        resolved.hoverCapable = contact->hoverCapable;
    if(press){
        if(held == m_pointerBindings.end()){
            if(m_pointerBindings.size() == s_InputMaxSources)
                return MakeUnexpected(Failure{});
            m_pointerBindings.push_back({ event.source, intent, resolved.hoverCapable });
        }
        else
            resolved.repeat = true;
    }
    else if(release && held != m_pointerBindings.end())
        m_pointerBindings.erase(held);
    else if(event.type == InputEventType::PointerCaptureLost){
        for(usize index = m_pointerBindings.size(); index > 0u; --index){
            if(m_pointerBindings[index - 1u].source == event.source)
                m_pointerBindings.erase(m_pointerBindings.begin() + static_cast<isize>(index - 1u));
        }
    }
    return resolved;
}

void InputRouter::retireQueuedReleases()noexcept{
    // Admission already observed these releases; dropping delivery must also retire their processed sequence ownership.
    for(const InputEvent& event : m_events){
        if(event.type == InputEventType::CommandUp){
            CommandSource* source = findCommandSource(event.source);
            if(source != nullptr)
                m_commandSources.erase(m_commandSources.begin() + (source - m_commandSources.data()));
        }
        else if(event.type == InputEventType::PointerCaptureLost)
            terminatePointerSource(event.source);
        else if(event.type == InputEventType::PrimaryUp || event.type == InputEventType::SecondaryUp){
            const PointerIntent::Enum intent = event.type == InputEventType::PrimaryUp ? PointerIntent::Activate : PointerIntent::ContextMenu;
            PointerSource* source = findPointerSource(event.source, intent);
            if(source == nullptr)
                continue;
            m_pointerSources.erase(m_pointerSources.begin() + (source - m_pointerSources.data()));
            if(m_activePointerSource != event.source)
                continue;
            if(intent == PointerIntent::Activate){
                m_primaryDown = false;
                m_pointerSequenceConsumed = false;
            }
            else{
                m_secondaryDown = false;
                m_secondarySequenceConsumed = false;
            }
            if(!m_primaryDown && !m_secondaryDown)
                m_activePointerSource = {};
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

