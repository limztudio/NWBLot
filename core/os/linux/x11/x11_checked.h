// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "x11_lib.h"

#include <core/os/clipboard.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr int s_X11PropertyByteFormat = 8;
inline constexpr int s_X11PropertyLongFormat = 32;
inline constexpr usize s_X11WireUnitBytes = 4u;


struct X11Property final : NoCopy{
    Atom type = 0u;
    int format = 0;
    unsigned long count = 0u;
    unsigned long remaining = 0u;
    unsigned char* bytes = nullptr;


public:
    X11Property()noexcept = default;
    X11Property(X11Property&& other)noexcept;
    ~X11Property()noexcept;
};


struct X11PropertyFailure{
    i32 nativeStatus = 0;
    u8 errorCode = 0u;
};

class X11CheckedOperation final : NoCopy{
    friend Expected<X11Property, X11PropertyFailure> ReadX11Property(Display& display, Window window, Atom property, b8 remove, usize maxBytes);


private:
    static int OnError(Display* display, XErrorEvent* event);


public:
    explicit X11CheckedOperation(Display& display);
    ~X11CheckedOperation()noexcept;


public:
    [[nodiscard]] b8 succeeded();


private:
    Display& m_display;
    XErrorHandler m_previous;
    u8 m_error = 0u;
};


[[nodiscard]] Expected<X11Property, X11PropertyFailure> ReadX11Property(Display& display, Window window, Atom property, b8 remove, usize maxBytes);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

