// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "router.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_input_router{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ValidRectangle(const Rect& rectangle){
    return
        IsFinite(rectangle.x) && IsFinite(rectangle.y) && IsFinite(rectangle.width) && IsFinite(rectangle.height)
        && rectangle.width >= 0.0f && rectangle.height >= 0.0f
        && IsFinite(rectangle.x + rectangle.width) && IsFinite(rectangle.y + rectangle.height)
    ;
}

bool Contains(const Rect& rectangle, const Point& position){
    return
        position.x >= rectangle.x && position.y >= rectangle.y
        && position.x < rectangle.x + rectangle.width && position.y < rectangle.y + rectangle.height
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


InputRouter::InputRouter(Core::Alloc::GlobalArena& arena)
    : m_targets(arena)
    , m_stagedTargets(arena)
    , m_lookup(arena)
    , m_stagedLookup(arena)
    , m_events(arena)
    , m_actions(arena)
    , m_pointerGestures(arena)
{
    m_targets.reserve(s_InputMaxTargets);
    m_stagedTargets.reserve(s_InputMaxTargets);
    m_lookup.reserve(s_InputMaxTargets);
    m_stagedLookup.reserve(s_InputMaxTargets);
    m_events.reserve(s_InputMaxEvents);
    m_actions.reserve(s_InputMaxActions);
    m_pointerGestures.reserve(s_InputMaxPointerGestures);
}

bool InputRouter::queue(const InputEvent& event){
    if(m_events.size() == s_InputMaxEvents || event.type > InputEventType::PointerCaptureLost)
        return false;
    if(event.type <= InputEventType::PrimaryUp && (!IsFinite(event.position.x) || !IsFinite(event.position.y)))
        return false;
    if((event.type == InputEventType::KeyDown || event.type == InputEventType::KeyUp) && (event.key == InputKey::None || event.key > InputKey::Y))
        return false;
    m_events.push_back(event);
    return true;
}

InputRoutingResult InputRouter::process(){
    InputRoutingResult result;
    for(const InputEvent& event : m_events){
        if(event.type <= InputEventType::PrimaryUp)
            routePointer(event, result);
        else if(event.type == InputEventType::KeyDown || event.type == InputEventType::KeyUp)
            routeKeyboard(event, result);
        else if(event.type == InputEventType::FocusLost){
            result.pointerConsumed |= m_capture.valid() || m_pointerSequenceConsumed;
            result.keyboardConsumed |= m_focus.valid() || m_consumedKeys != 0u;
            cancelInteraction();
        }
        else if(event.type == InputEventType::PointerCaptureLost){
            result.pointerConsumed |= m_capture.valid() || m_pointerSequenceConsumed;
            cancelPointerCapture();
        }
        else{
            result.pointerConsumed |= m_capture.valid() || m_pointerSequenceConsumed;
            m_pointerKnown = false;
            m_hover = {};
        }
    }
    m_events.clear();
    result.wantsPointer = m_primaryDown ? m_pointerSequenceConsumed : m_hover.valid();
    result.wantsKeyboard = m_focus.valid() || m_consumedKeys != 0u;
    result.hover = m_hover;
    result.focus = m_focus;
    result.capture = m_capture;
    return result;
}

bool InputRouter::commitTargets(const HitTarget* targets, const usize count, const u64 layoutGeneration){
    if(count > s_InputMaxTargets || (count != 0u && targets == nullptr) || layoutGeneration == 0u || layoutGeneration <= m_layoutGeneration)
        return false;
    m_stagedTargets.clear();
    m_stagedLookup.clear();
    for(usize index = 0u; index < count; ++index){
        const HitTarget& target = targets[index];
        if(
            !target.id.valid() || target.declarationGeneration == 0u
            || !__hidden_ui_input_router::ValidRectangle(target.rectangle)
            || !__hidden_ui_input_router::ValidRectangle(target.clip)
            || !__hidden_ui_input_router::ValidRectangle(target.gestureReference)
            || ((target.gestureReference.width == 0.0f) != (target.gestureReference.height == 0.0f))
        )
            return false;
        m_stagedTargets.push_back(target);
        m_stagedLookup.push_back({ target.id.value, static_cast<u32>(index) });
    }
    Sort(m_stagedLookup.begin(), m_stagedLookup.end(), [](const TargetLookup& lhs, const TargetLookup& rhs){
        return lhs.value < rhs.value;
    });
    for(usize index = 1u; index < m_stagedLookup.size(); ++index){
        if(m_stagedLookup[index - 1u].value == m_stagedLookup[index].value)
            return false;
    }
    m_targets.swap(m_stagedTargets);
    m_lookup.swap(m_stagedLookup);
    m_layoutGeneration = layoutGeneration;
    reconcileTargets();
    return true;
}

void InputRouter::invalidateTarget(const WidgetId id){
    const HitTarget* target = findTarget(id);
    if(target == nullptr)
        return;
    const usize targetIndex = static_cast<usize>(target - m_targets.data());
    m_targets.erase(m_targets.begin() + static_cast<isize>(targetIndex));
    for(usize index = 0u; index < m_lookup.size();){
        if(m_lookup[index].value == id.value)
            m_lookup.erase(m_lookup.begin() + static_cast<isize>(index));
        else{
            if(m_lookup[index].index > targetIndex)
                --m_lookup[index].index;
            ++index;
        }
    }
    reconcileTargets();
}

void InputRouter::clearFocus(){
    m_focus = {};
    m_focusDeclaration = 0u;
}

void InputRouter::reset(){
    m_events.clear();
    m_targets.clear();
    m_stagedTargets.clear();
    m_lookup.clear();
    m_stagedLookup.clear();
    m_layoutGeneration = 0u;
    cancelInteraction();
}

bool InputRouter::consumeActivation(const WidgetId id){
    const HitTarget* target = findTarget(id);
    if(target == nullptr || !isInteractive(*target) || !target->activatable)
        return false;
    for(usize index = 0u; index < m_actions.size(); ++index){
        const InputActionId& action = m_actions[index].id;
        if(action.target == id && action.declarationGeneration == target->declarationGeneration){
            m_actions.erase(m_actions.begin() + static_cast<isize>(index));
            return true;
        }
    }
    return false;
}

WidgetId InputRouter::hitTest(const Point& position)const{
    const HitTarget* target = findHitTarget(position);
    return target == nullptr ? WidgetId{} : target->id;
}

bool InputRouter::wouldConsumePointer(const Point& position)const{
    return m_capture.valid() || (m_primaryDown && m_pointerSequenceConsumed) || hitTest(position).valid();
}

const HitTarget* InputRouter::findTarget(const WidgetId id, const u64 declarationGeneration)const{
    usize begin = 0u;
    usize end = m_lookup.size();
    while(begin < end){
        const usize middle = begin + (end - begin) / 2u;
        if(m_lookup[middle].value < id.value)
            begin = middle + 1u;
        else
            end = middle;
    }
    if(begin == m_lookup.size() || m_lookup[begin].value != id.value)
        return nullptr;
    const HitTarget& target = m_targets[m_lookup[begin].index];
    return declarationGeneration == 0u || target.declarationGeneration == declarationGeneration ? &target : nullptr;
}

const HitTarget* InputRouter::findHitTarget(const Point& position)const{
    if(!IsFinite(position.x) || !IsFinite(position.y))
        return nullptr;
    const HitTarget* found = nullptr;
    for(const HitTarget& target : m_targets){
        if(
            !isInteractive(target)
            || !__hidden_ui_input_router::Contains(target.rectangle, position)
            || !__hidden_ui_input_router::Contains(target.clip, position)
        )
            continue;
        if(found == nullptr || target.paintOrder >= found->paintOrder)
            found = &target;
    }
    return found;
}

bool InputRouter::isInteractive(const HitTarget& target)const{
    const f32 left = Max(target.rectangle.x, target.clip.x);
    const f32 top = Max(target.rectangle.y, target.clip.y);
    const f32 right = Min(target.rectangle.x + target.rectangle.width, target.clip.x + target.clip.width);
    const f32 bottom = Min(target.rectangle.y + target.rectangle.height, target.clip.y + target.clip.height);
    return target.enabled && right > left && bottom > top;
}

void InputRouter::reconcileTargets(){
    const HitTarget* focused = findTarget(m_focus, m_focusDeclaration);
    if(focused == nullptr || !isInteractive(*focused) || !focused->focusable){
        m_focus = {};
        m_focusDeclaration = 0u;
    }
    const HitTarget* captured = findTarget(m_capture, m_captureDeclaration);
    if(captured == nullptr || !isInteractive(*captured)){
        m_capture = {};
        m_captureDeclaration = 0u;
    }
    for(usize index = 0u; index < m_actions.size();){
        const InputActionId& action = m_actions[index].id;
        const HitTarget* target = findTarget(action.target, action.declarationGeneration);
        if(target == nullptr || !isInteractive(*target) || !target->activatable)
            m_actions.erase(m_actions.begin() + static_cast<isize>(index));
        else
            ++index;
    }
    reconcilePointerGestures();
    updateHover();
}

void InputRouter::updateHover(){
    m_hover = m_pointerKnown ? hitTest(m_pointer) : WidgetId{};
}

void InputRouter::cancelPointerCapture(){
    if(!m_primaryDown)
        return;
    for(usize index = 0u; index < m_pointerGestures.size(); ++index){
        if(m_pointerGestures[index].gesture.id.sequence == m_activeGestureSequence){
            m_pointerGestures.erase(m_pointerGestures.begin() + static_cast<isize>(index));
            break;
        }
    }
    m_activeGestureSequence = 0u;
    m_hover = {};
    m_capture = {};
    m_captureDeclaration = 0u;
    m_pointerKnown = false;
    m_primaryDown = false;
    m_pointerSequenceConsumed = false;
}

void InputRouter::cancelInteraction(){
    m_actions.clear();
    m_pointerGestures.clear();
    m_activeGestureSequence = 0u;
    m_hover = {};
    m_focus = {};
    m_capture = {};
    m_focusDeclaration = 0u;
    m_captureDeclaration = 0u;
    m_pointerKnown = false;
    m_primaryDown = false;
    m_pointerSequenceConsumed = false;
    m_pressedKeys = 0u;
    m_consumedKeys = 0u;
}

void InputRouter::appendActivation(const HitTarget& target, const InputActionSource::Enum source, InputRoutingResult& result){
    if(m_actions.size() == s_InputMaxActions || m_nextActionSequence == 0u){
        result.activationOverflow = true;
        return;
    }
    m_actions.push_back({ { target.id, target.declarationGeneration, m_layoutGeneration, m_nextActionSequence }, source });
    ++m_nextActionSequence;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

