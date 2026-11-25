// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cursor_service.h"

#if defined(FOUNDATION_PLATFORM_LINUX) && defined(NWB_OS_WITH_WAYLAND)

#pragma push_macro("interface")
#undef interface
#include <wayland-client.h>
#pragma pop_macro("interface")


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


WaylandCursorService::WaylandCursorService(Alloc::GlobalArena& arena)noexcept
    : TrackedCursorService(arena)
{}

CursorCapabilities WaylandCursorService::capabilities()const noexcept{
    return {
        .setShape = false,
        .hide = true,
        .backend = CursorBackend::Wayland
    };
}

void WaylandCursorService::observePointerEnter(wl_pointer* const pointer, const u32 serial)noexcept{
    if(!isOwnerThread())
        return;
    m_pointer = pointer;
    m_enterSerial = serial;
    m_hasEnterSerial = pointer != nullptr;
    if(m_hasEnterSerial && currentCursor() == CursorShape::Hidden)
        wl_pointer_set_cursor(m_pointer, m_enterSerial, nullptr, 0, 0);
}

b8 WaylandCursorService::applyNativeCursor(const CursorShape::Enum shape)noexcept{
    if(shape == CursorShape::Hidden && m_pointer && m_hasEnterSerial)
        wl_pointer_set_cursor(m_pointer, m_enterSerial, nullptr, 0, 0);
    return true;
}

Expected<GlobalUniquePtr<ICursorService>> CreateWaylandCursorService(Alloc::GlobalArena& arena){
    auto service = MakeGlobalUnique<WaylandCursorService>(arena, arena);
    if(!service)
        return MakeUnexpected(Failure{});
    return service;
}

void ObserveWaylandCursorPointerEnter(ICursorService& service, wl_pointer* const pointer, const u32 serial)noexcept{
    if(!service.isOwnerThread() || service.capabilities().backend != CursorBackend::Wayland)
        return;
    MakeNotNull(checked_cast<WaylandCursorService*>(&service))->observePointerEnter(pointer, serial);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
