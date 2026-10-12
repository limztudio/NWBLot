// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "router.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


InputRouter::PointerSource* InputRouter::findPointerSource(const InputSource& source, const PointerIntent::Enum intent)noexcept{
    const auto found = FindIf(m_pointerSources.begin(), m_pointerSources.end(), [&source, intent](const PointerSource& current)noexcept{
        return current.source == source && current.intent == intent;
    });
    return found == m_pointerSources.end() ? nullptr : &*found;
}

const InputRouter::PointerSource* InputRouter::findPointerSource(const InputSource& source, const PointerIntent::Enum intent)const noexcept{
    const auto found = FindIf(m_pointerSources.begin(), m_pointerSources.end(), [&source, intent](const PointerSource& current)noexcept{
        return current.source == source && current.intent == intent;
    });
    return found == m_pointerSources.end() ? nullptr : &*found;
}

bool InputRouter::ownsPointerSource(const InputSource& source, const PointerIntent::Enum intent)const noexcept{
    const PointerSource* held = findPointerSource(source, intent);
    return held != nullptr && held->consumed;
}

bool InputRouter::acceptsPointerSource(const InputSource& source)const noexcept{
    if(!source.valid() || (m_activePointerSource.valid() && m_activePointerSource != source))
        return false;
    return FindIf(m_pointerSources.begin(), m_pointerSources.end(), [&source](const PointerSource& held)noexcept{
        return held.source == source && held.inert;
    }) == m_pointerSources.end();
}

bool InputRouter::wantsPointer()const noexcept{
    if(!m_pointerSources.empty()){
        return FindIf(m_pointerSources.begin(), m_pointerSources.end(), [](const PointerSource& held)noexcept{
            return held.consumed;
        }) != m_pointerSources.end();
    }
    return hasPopup() || m_hover.valid();
}

void InputRouter::observePointer(const InputEvent& event, const HitTarget* hit){
    if(!event.hoverCapable)
        return;
    m_hoverPointerSource = event.source;
    m_pointer = event.position;
    m_pointerKnown = true;
    setHoverTarget(hit);
}

void InputRouter::terminatePointerSource(const InputSource source){
    const bool active = m_activePointerSource == source;
    const bool unfinished = FindIf(m_pointerSources.begin(), m_pointerSources.end(), [&source](const PointerSource& held)noexcept{
        return held.source == source;
    }) != m_pointerSources.end();
    if(active){
        cancelPointerPress();
        m_primaryDown = false;
        m_pointerSequenceConsumed = false;
        m_secondaryDown = false;
        m_secondarySequenceConsumed = false;
        m_activePointerSource = {};
    }
    for(usize index = m_pointerSources.size(); index > 0u; --index){
        if(m_pointerSources[index - 1u].source == source)
            m_pointerSources.erase(m_pointerSources.begin() + static_cast<isize>(index - 1u));
    }
    // Ordinary mouse release may immediately drop native capture; its completed events and hover remain usable.
    if(unfinished && m_hoverPointerSource == source){
        advanceHoverActivity();
        clearHover();
        m_pointerKnown = false;
        m_hoverPointerSource = {};
    }
}

void InputRouter::routePointerSource(const InputEvent& event, InputRoutingResult& result){
    if(event.type == InputEventType::PointerCaptureLost){
        result.pointerConsumed |= ownsPointerSource(event.source, PointerIntent::Activate) || ownsPointerSource(event.source, PointerIntent::ContextMenu);
        result.pointerRouted |= m_activePointerSource == event.source;
        terminatePointerSource(event.source);
        return;
    }
    if(event.type == InputEventType::PointerLeave){
        result.pointerConsumed |= ownsPointerSource(event.source, PointerIntent::Activate) || ownsPointerSource(event.source, PointerIntent::ContextMenu);
        result.pointerRouted |= m_activePointerSource == event.source;
        if(m_hoverPointerSource == event.source){
            advanceHoverActivity();
            m_pointerKnown = false;
            m_hoverPointerSource = {};
            clearHover();
        }
        return;
    }
    const bool press = event.type == InputEventType::PrimaryDown || event.type == InputEventType::SecondaryDown;
    const bool release = event.type == InputEventType::PrimaryUp || event.type == InputEventType::SecondaryUp;
    // Reuse only within this event; popup dismissal and layout reconciliation still resolve hover against their new state.
    const HitTarget* hit = event.hoverCapable ? findHitTarget(event.position) : nullptr;
    bool hitResolved = event.hoverCapable;
    const auto resolveHit = [this, &event, &hit, &hitResolved](){
        if(!hitResolved){
            hit = findHitTarget(event.position);
            hitResolved = true;
        }
        return hit;
    };
    observePointer(event, hit);
    const PointerIntent::Enum intent = event.type == InputEventType::SecondaryDown || event.type == InputEventType::SecondaryUp ? PointerIntent::ContextMenu : PointerIntent::Activate;
    PointerSource* held = findPointerSource(event.source, intent);
    if(press){
        if(held != nullptr){
            result.pointerConsumed |= held->consumed;
            return;
        }
        if(m_pointerSources.size() == s_InputMaxSources)
            return;
        const bool accepted = !event.repeat && acceptsPointerSource(event.source);
        const bool consumed = !event.repeat && (m_activePointerSource == event.source ? m_primaryDown ? m_pointerSequenceConsumed : m_secondarySequenceConsumed : hasPopup() || resolveHit() != nullptr);
        m_pointerSources.push_back({ event.source, intent, consumed, !accepted });
        result.pointerConsumed |= consumed;
        if(!accepted)
            return;
        m_activePointerSource = event.source;
    }
    else if(release){
        if(held == nullptr){
            result.pointerConsumed |= hasPopup() || resolveHit() != nullptr;
            return;
        }
        result.pointerConsumed |= held->consumed;
        if(held->inert || m_activePointerSource != event.source){
            const auto index = static_cast<isize>(held - m_pointerSources.data());
            m_pointerSources.erase(m_pointerSources.begin() + index);
            return;
        }
    }
    else{
        const PointerSource* activation = findPointerSource(event.source, PointerIntent::Activate);
        const PointerSource* context = findPointerSource(event.source, PointerIntent::ContextMenu);
        const bool blocked = (activation != nullptr && activation->inert) || (context != nullptr && context->inert) || !acceptsPointerSource(event.source);
        if(blocked){
            result.pointerConsumed |= (activation != nullptr && activation->consumed) || (context != nullptr && context->consumed);
            return;
        }
        if(!m_activePointerSource.valid() && !event.hoverCapable && event.type == InputEventType::PointerMove)
            return;
    }
    if(press || release || event.type == InputEventType::PointerWheel)
        resolveHit();
    result.pointerRouted = true;
    m_interactionPosition = event.position;
    if(event.type == InputEventType::SecondaryDown || event.type == InputEventType::SecondaryUp)
        routeSecondary(event, result, hit);
    else if(event.type == InputEventType::PointerWheel)
        routeWheel(event, result, hit);
    else
        routePointer(event, result, hit);
    if(release){
        const auto index = static_cast<isize>(held - m_pointerSources.data());
        m_pointerSources.erase(m_pointerSources.begin() + index);
        if(!m_primaryDown && !m_secondaryDown)
            m_activePointerSource = {};
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

