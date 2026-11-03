// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "router.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<InputEvent> InputRouter::queuePointer(const PointerInput& input){
    const auto event = TryMakePointerInputEvent(input);
    if(!event)
        return MakeUnexpected(Failure{});
    return queue(*event);
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
    const b8 press = event.type == InputEventType::PrimaryDown || event.type == InputEventType::SecondaryDown;
    const b8 release = event.type == InputEventType::PrimaryUp || event.type == InputEventType::SecondaryUp;
    // Reuse only within this event; popup dismissal and layout reconciliation still resolve hover against their new state.
    const HitTarget* hit = event.hoverCapable ? findHitTarget(event.position) : nullptr;
    b8 hitResolved = event.hoverCapable;
    const auto resolveHit = [this, &event, &hit, &hitResolved]()noexcept{
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
        const b8 accepted = !event.repeat && acceptsPointerSource(event.source);
        const b8 consumed = !event.repeat && (m_activePointerSource == event.source ? m_primaryDown ? m_pointerSequenceConsumed : m_secondarySequenceConsumed : hasPopup() || resolveHit() != nullptr);
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
        const b8 blocked = (activation != nullptr && activation->inert) || (context != nullptr && context->inert) || !acceptsPointerSource(event.source);
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
        routeSecondary(event, hit, result);
    else if(event.type == InputEventType::PointerWheel)
        routeWheel(event, hit, result);
    else
        routePointer(event, hit, result);
    if(release){
        const auto index = static_cast<isize>(held - m_pointerSources.data());
        m_pointerSources.erase(m_pointerSources.begin() + index);
        if(!m_primaryDown && !m_secondaryDown)
            m_activePointerSource = {};
    }
}

void InputRouter::observePointer(const InputEvent& event, const HitTarget* hit)noexcept{
    if(!event.hoverCapable)
        return;
    m_hoverPointerSource = event.source;
    m_pointer = event.position;
    m_pointerKnown = true;
    setHoverTarget(hit);
}

void InputRouter::terminatePointerSource(const InputSource source)noexcept{
    const b8 active = m_activePointerSource == source;
    const b8 unfinished = FindIf(m_pointerSources.begin(), m_pointerSources.end(), [&source](const PointerSource& held)noexcept{
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

void InputRouter::routePointer(const InputEvent& event, const HitTarget* hit, InputRoutingResult& result){
    if(event.type == InputEventType::PointerMove){
        if(m_primaryDown)
            updatePointerGesture(event.position, false);
        result.pointerConsumed |= m_primaryDown ? m_pointerSequenceConsumed : m_secondaryDown ? m_secondarySequenceConsumed : hasPopup() || hit != nullptr;
        return;
    }
    if(event.type == InputEventType::PrimaryDown){
        if(m_primaryDown){
            result.pointerConsumed |= m_pointerSequenceConsumed;
            return;
        }
        advanceHoverActivity();
        m_primaryDown = true;
        m_pointerSubmitArmed = false;
        m_pointerSequenceConsumed = m_secondaryDown ? m_secondarySequenceConsumed : hasPopup() || hit != nullptr;
        result.pointerConsumed |= m_pointerSequenceConsumed;
        if(!m_pointerSequenceConsumed){
            clearFocus();
            return;
        }
        if(hasPopup() && hit == nullptr){
            const b8 dismissed = dismissPopup(PopupDismissReason::OutsideClick);
            if(dismissed)
                updateHover();
            m_capture = {};
            m_capturePopup = {};
            m_captureDeclaration = 0u;
            m_captureControl = {};
            return;
        }
        m_capture = hit == nullptr ? WidgetId{} : hit->id;
        m_captureDeclaration = hit == nullptr ? 0u : hit->declarationGeneration;
        m_capturePopup = hit == nullptr ? PopupToken{} : hit->popup;
        m_captureControl = hit == nullptr ? ControlToken{} : hit->control;
        m_pointerSubmitArmed = hit != nullptr && hit->activatable;
        const HitTarget* focused = hit != nullptr && hit->owner.valid() ? controlHost(*hit) : hit;
        if(focused != nullptr && focused->focusable)
            assignFocus(*focused);
        else
            clearFocus();
        if(hit != nullptr && hit->pointerGesture)
            appendPointerGesture(*hit, result);
        return;
    }
    result.pointerConsumed |= m_primaryDown ? m_pointerSequenceConsumed : m_secondaryDown ? m_secondarySequenceConsumed : hit != nullptr;
    if(m_primaryDown && m_pointerSequenceConsumed && m_pointerSubmitArmed && hit != nullptr && hit->id == m_capture && hit->declarationGeneration == m_captureDeclaration && hit->activatable && hit->popup == m_capturePopup && hit->control == m_captureControl){
        const HitTarget* host = hit->owner.valid() ? controlHost(*hit) : nullptr;
        if(host != nullptr)
            appendControlAction(*host, *hit, ControlActionKind::Activate, 0.0, result);
        else
            appendActivation(*hit, InputActionSource::Pointer, event.source, result);
    }
    if(m_primaryDown)
        updatePointerGesture(event.position, true);
    m_primaryDown = false;
    m_pointerSubmitArmed = false;
    m_pointerSequenceConsumed = false;
    m_capture = {};
    m_capturePopup = {};
    m_captureDeclaration = 0u;
    m_captureControl = {};
}

void InputRouter::routeSecondary(const InputEvent& event, const HitTarget* hit, InputRoutingResult& result){
    if(event.type == InputEventType::SecondaryUp){
        result.pointerConsumed |= m_secondaryDown ? m_secondarySequenceConsumed : m_primaryDown ? m_pointerSequenceConsumed : hasPopup() || hit != nullptr;
        const HitTarget* anchor = hit != nullptr && !hit->contextMenu && hit->owner.valid() ? controlHost(*hit) : hit;
        if(m_secondaryDown && m_secondarySequenceConsumed && anchor != nullptr && anchor->id == m_secondaryOwner.target && currentContextMenuOwner(m_secondaryOwner))
            appendContextMenu(*anchor, m_secondaryPosition, InputActionSource::Pointer, event.source, result);
        m_secondaryDown = false;
        m_secondarySequenceConsumed = false;
        m_secondaryOwner = {};
        return;
    }
    if(m_secondaryDown){
        result.pointerConsumed |= m_secondarySequenceConsumed;
        return;
    }
    advanceHoverActivity();
    m_secondaryDown = true;
    m_secondarySequenceConsumed = m_primaryDown ? m_pointerSequenceConsumed : hasPopup() || hit != nullptr;
    result.pointerConsumed |= m_secondarySequenceConsumed;
    if(!m_secondarySequenceConsumed)
        return;
    if(hasPopup() && hit == nullptr){
        const b8 dismissed = dismissPopup(PopupDismissReason::OutsideClick);
        if(dismissed)
            updateHover();
        return;
    }
    const HitTarget* anchor = hit != nullptr && !hit->contextMenu && hit->owner.valid() ? controlHost(*hit) : hit;
    if(anchor == nullptr || !anchor->contextMenu || !m_windowFocused)
        return;
    m_secondaryOwner = { anchor->id, anchor->declarationGeneration, anchor->popup, anchor->control };
    m_secondaryPosition = event.position;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

