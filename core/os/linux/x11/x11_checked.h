// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/os/clipboard.h>

#include <X11/Xlib.h>

#ifdef Success
#undef Success
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Foreign selection requestors can disappear between Xlib calls. Check those errors without a fatal Xlib handler.
class X11CheckedOperation final : NoCopy{
private:
    static int OnError(Display* display, XErrorEvent* event);


public:
    explicit X11CheckedOperation(Display& display);
    ~X11CheckedOperation();


public:
    [[nodiscard]] bool succeeded();


private:
    Display& m_display;
    XErrorHandler m_previous;
    u8 m_error = 0u;
};


struct X11Property final : NoCopy{
    Atom type = 0u;
    int format = 0;
    unsigned long count = 0u;
    unsigned long remaining = 0u;
    unsigned char* bytes = nullptr;

    X11Property()noexcept = default;
    X11Property(X11Property&& other)noexcept;
    ~X11Property()noexcept;
};


[[nodiscard]] Expected<X11Property> ReadX11Property(Display& display, Window window, Atom property, bool remove, usize maxBytes);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

