// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cursor_service.h"

#if defined(FOUNDATION_PLATFORM_LINUX)

#include "x11_lib.h"
#include <X11/cursorfont.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_x11_cursor_service{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u32 X11CursorFontShape(const CursorShape::Enum shape)noexcept{
    switch(shape){
    case CursorShape::Default: return XC_left_ptr;
    case CursorShape::TextInput: return XC_xterm;
    case CursorShape::Hand: return XC_hand2;
    case CursorShape::Crosshair: return XC_crosshair;
    case CursorShape::ResizeEW: return XC_sb_h_double_arrow;
    case CursorShape::ResizeNS: return XC_sb_v_double_arrow;
    case CursorShape::ResizeNWSE: return XC_top_left_corner;
    case CursorShape::ResizeNESW: return XC_top_right_corner;
    case CursorShape::Move: return XC_fleur;
    case CursorShape::NotAllowed: return XC_X_cursor;
    case CursorShape::Wait: return XC_watch;
    case CursorShape::Progress: return XC_watch;
    case CursorShape::Grab: return XC_hand1;
    case CursorShape::Grabbing: return XC_fleur;
    case CursorShape::Hidden:
    case CursorShape::kCount:
    default:
        return XC_left_ptr;
    }
}

static Cursor CreateBlankCursor(Display& display, const Window window)noexcept{
    static constexpr char s_BlankBits[1] = { 0 };
    const Pixmap blank = XCreateBitmapFromData(&display, window, s_BlankBits, 1u, 1u);
    if(blank == None)
        return None;
    XColor dummy = {};
    const Cursor cursor = XCreatePixmapCursor(&display, blank, blank, &dummy, &dummy, 0u, 0u);
    XFreePixmap(&display, blank);
    return cursor;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


X11CursorService::X11CursorService(Alloc::GlobalArena& arena, _XDisplay& display, const u64 nativeWindowHandle)noexcept
    : TrackedCursorService(arena)
    , m_display(display)
    , m_window(nativeWindowHandle)
{
    if(m_window == 0u)
        return;
    for(usize i = 0u; i < static_cast<usize>(CursorShape::kCount); ++i){
        const CursorShape::Enum shape = static_cast<CursorShape::Enum>(i);
        if(shape == CursorShape::Hidden)
            m_cursors[i] = static_cast<u64>(__hidden_x11_cursor_service::CreateBlankCursor(m_display, static_cast<Window>(m_window)));
        else
            m_cursors[i] = static_cast<u64>(XCreateFontCursor(&m_display, __hidden_x11_cursor_service::X11CursorFontShape(shape)));
    }
}

X11CursorService::~X11CursorService()noexcept{
    if(isOwnerThread()){
        for(const u64 handle : m_cursors){
            if(handle != None)
                XFreeCursor(&m_display, static_cast<Cursor>(handle));
        }
    }
}

CursorCapabilities X11CursorService::capabilities()const noexcept{
    return {
        .setShape = m_window != 0u,
        .hide = m_window != 0u && m_cursors[static_cast<usize>(CursorShape::Hidden)] != None,
        .backend = CursorBackend::X11
    };
}

b8 X11CursorService::applyNativeCursor(const CursorShape::Enum shape)noexcept{
    if(m_window == 0u)
        return false;
    const u64 cursor = m_cursors[static_cast<usize>(shape)];
    if(cursor == None)
        XUndefineCursor(&m_display, static_cast<Window>(m_window));
    else
        XDefineCursor(&m_display, static_cast<Window>(m_window), static_cast<Cursor>(cursor));
    XFlush(&m_display);
    return true;
}

Expected<GlobalUniquePtr<ICursorService>> CreateX11CursorService(Alloc::GlobalArena& arena, _XDisplay& display, const u64 nativeWindowHandle){
    auto service = MakeGlobalUnique<X11CursorService>(arena, arena, display, nativeWindowHandle);
    if(!service)
        return MakeUnexpected(Failure{});
    return service;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
