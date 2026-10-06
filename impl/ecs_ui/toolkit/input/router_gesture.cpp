// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "router.h"

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool InputRouter::consumePointerGesture(const WidgetId id, const u64 declarationGeneration, PointerGesture& gesture){
    const HitTarget* target = findTarget(id, declarationGeneration);
    if(declarationGeneration == 0u || target == nullptr || !isInteractive(*target) || !target->pointerGesture)
        return false;
    for(usize index = 0u; index < m_pointerGestures.size(); ++index){
        auto& record = m_pointerGestures[index];
        if(
            record.gesture.id.target != id || record.gesture.id.declarationGeneration != declarationGeneration
            || !record.pendingUpdate || record.gesture.popup != target->popup || record.gesture.control != target->control
        )
            continue;
        gesture = record.gesture;
        if(record.gesture.state == PointerGestureState::Completed)
            m_pointerGestures.erase(m_pointerGestures.begin() + static_cast<isize>(index));
        else
            record.pendingUpdate = false;
        return true;
    }
    return false;
}

void InputRouter::appendPointerGesture(const HitTarget& target, InputRoutingResult& result){
    m_activeGestureSequence = 0u;
    if(m_pointerGestures.size() == s_InputMaxPointerGestures || m_nextActionSequence == 0u){
        result.gestureOverflow = true;
        return;
    }
    PointerGesture gesture;
    gesture.id = { target.id, target.declarationGeneration, m_layoutGeneration, m_nextActionSequence };
    gesture.origin = m_pointer;
    gesture.position = m_pointer;
    gesture.targetRectangle = target.rectangle;
    gesture.referenceRectangle = target.gestureReference.width > 0.0f ? target.gestureReference : target.rectangle;
    gesture.popup = target.popup;
    gesture.control = target.control;
    gesture.maximum = target.gestureMaximum;
    gesture.updateSequence = m_nextActionSequence;
    gesture.value = target.value;
    m_pointerGestures.push_back({ gesture, true });
    m_activeGestureSequence = m_nextActionSequence;
    ++m_nextActionSequence;
}

void InputRouter::updatePointerGesture(const Point& position, const bool completed)noexcept{
    if(m_activeGestureSequence == 0u)
        return;
    for(auto& record : m_pointerGestures){
        if(record.gesture.id.sequence != m_activeGestureSequence)
            continue;
        if(completed || record.gesture.position.x != position.x || record.gesture.position.y != position.y){
            if(m_nextActionSequence == 0u)
                TerminateInvariant();
            record.gesture.position = position;
            record.gesture.state = completed ? PointerGestureState::Completed : PointerGestureState::Active;
            record.gesture.updateSequence = m_nextActionSequence;
            ++m_nextActionSequence;
            record.pendingUpdate = true;
        }
        break;
    }
    if(completed)
        m_activeGestureSequence = 0u;
}

void InputRouter::reconcilePointerGestures(){
    for(usize index = 0u; index < m_pointerGestures.size();){
        const auto& gesture = m_pointerGestures[index].gesture;
        const HitTarget* target = findTarget(gesture.id.target, gesture.id.declarationGeneration);
        if(
            target == nullptr || !isInteractive(*target) || !target->pointerGesture
            || gesture.popup != target->popup || gesture.control != target->control
        ){
            if(gesture.id.sequence == m_activeGestureSequence)
                m_activeGestureSequence = 0u;
            m_pointerGestures.erase(m_pointerGestures.begin() + static_cast<isize>(index));
        }
        else
            ++index;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

