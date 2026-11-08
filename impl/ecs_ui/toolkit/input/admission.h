// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "bindings.h"

#include <impl/ecs_ui/toolkit/input/input.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class InputAdmission final : NoCopy{
public:
    explicit InputAdmission(Core::Alloc::GlobalArena& arena);


public:
    [[nodiscard]] Expected<InputEvent> queue(const InputEvent& event);
    [[nodiscard]] Expected<InputEvent> queuePointer(const PointerInput& input);
    [[nodiscard]] b8 setBindings(Span<const InputKeyBinding> bindings);
    void restoreDefaultBindings();
    [[nodiscard]] const InputBindings& bindings()const noexcept{ return m_bindings; }
    [[nodiscard]] Span<const InputEvent> events()const noexcept{ return m_events; }
    void clear()noexcept{ m_events.clear(); }
    void cancelDevice(u64 device, Vector<PointerSource, Core::Alloc::GlobalArena>& pointerSources, Vector<CommandSource, Core::Alloc::GlobalArena>& commandSources)noexcept;


private:
    InputBindings m_bindings;
    Vector<BoundSource, Core::Alloc::GlobalArena> m_boundSources;
    Vector<PointerBinding, Core::Alloc::GlobalArena> m_pointerBindings;
    Vector<InputEvent, Core::Alloc::GlobalArena> m_events;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

