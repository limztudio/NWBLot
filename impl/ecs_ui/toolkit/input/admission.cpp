// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "admission.h"



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


InputAdmission::InputAdmission(Core::Alloc::GlobalArena& arena)
    : m_bindings(arena)
    , m_boundSources(arena)
    , m_pointerBindings(arena)
    , m_events(arena)
{
    m_boundSources.reserve(s_InputSourceReserve);
    m_pointerBindings.reserve(s_InputSourceReserve);
    m_events.reserve(s_InputMaxEvents);
}


Expected<InputEvent> InputAdmission::queue(const InputEvent& event){
    if(m_events.size() == s_InputMaxEvents || event.type > InputEventType::CommandUp)
        return MakeUnexpected(Failure{});
    const auto pointer = TryResolveRouterPointerEvent(event, m_pointerBindings);
    if(!pointer)
        return MakeUnexpected(Failure{});
    const auto candidate = TryResolveRouterSourceEvent(m_bindings.bindings(), *pointer, m_boundSources);
    if(!candidate)
        return MakeUnexpected(Failure{});
    m_events.push_back(*candidate);
    return *candidate;
}

Expected<InputEvent> InputAdmission::queuePointer(const PointerInput& input){
    const auto event = TryMakePointerInputEvent(input);
    if(!event)
        return MakeUnexpected(Failure{});
    return queue(*event);
}

b8 InputAdmission::setBindings(const Span<const InputKeyBinding> bindings){
    return m_bindings.set(bindings);
}

void InputAdmission::restoreDefaultBindings(){
    m_bindings.restoreDefaults();
}

void InputAdmission::cancelDevice(const u64 device, Vector<PointerSource, Core::Alloc::GlobalArena>& pointerSources, Vector<CommandSource, Core::Alloc::GlobalArena>& commandSources)noexcept{
    EraseRouterDeviceInputs(device, pointerSources, m_pointerBindings, commandSources, m_boundSources, m_events);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

