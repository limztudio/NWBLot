// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "router.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<InputEvent> InputRouter::queuePointer(const PointerInput& input){
    InputEvent event;
    if(!TryMakePointerInputEvent(input, event))
        return MakeUnexpected(Failure{});
    return queue(event);
}

Expected<InputEvent> InputRouter::resolvePointerEvent(const InputEvent& event){
    if(!IsPointerInputEventType(event.type))
        return event;
    if(!event.source.valid() || !IsFinite(event.position.x) || !IsFinite(event.position.y))
        return MakeUnexpected(Failure{});
    if(event.type == InputEventType::PointerWheel && (!IsFinite(event.scrollX) || !IsFinite(event.scrollY)))
        return MakeUnexpected(Failure{});
    InputEvent resolved = event;
    const b8 press = event.type == InputEventType::PrimaryDown || event.type == InputEventType::SecondaryDown;
    const b8 release = event.type == InputEventType::PrimaryUp || event.type == InputEventType::SecondaryUp;
    const PointerIntent::Enum intent = event.type == InputEventType::SecondaryDown || event.type == InputEventType::SecondaryUp ? PointerIntent::ContextMenu : PointerIntent::Activate;
    const auto held = FindIf(m_pointerBindings.begin(), m_pointerBindings.end(), [&event, intent](const PointerBinding& binding)noexcept{
        return binding.source == event.source && binding.intent == intent;
    });
    const auto contact = FindIf(m_pointerBindings.begin(), m_pointerBindings.end(), [&event](const PointerBinding& binding)noexcept{
        return binding.source == event.source;
    });
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

