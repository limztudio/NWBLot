// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "x11_checked.h"

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_x11_checked{
thread_local X11CheckedOperation* g_Operation = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


int X11CheckedOperation::onError(Display* const display, XErrorEvent* const event){
    X11CheckedOperation* const operation = __hidden_x11_checked::g_Operation;
    if(operation && display == &operation->m_display){
        operation->m_error = event->error_code;
        return 0;
    }
    return operation && operation->m_previous ? operation->m_previous(display, event) : 0;
}

X11CheckedOperation::X11CheckedOperation(Display& display)
    : m_display(display)
    , m_previous(nullptr)
{
    GLB_FATAL_ASSERT(!__hidden_x11_checked::g_Operation);
    XSync(&m_display, False);
    __hidden_x11_checked::g_Operation = this;
    m_previous = XSetErrorHandler(&X11CheckedOperation::onError);
}

X11CheckedOperation::~X11CheckedOperation(){
    XSync(&m_display, False);
    XSetErrorHandler(m_previous);
    __hidden_x11_checked::g_Operation = nullptr;
}

bool X11CheckedOperation::succeeded(){
    XSync(&m_display, False);
    return m_error == 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


X11Property::~X11Property(){
    if(bytes && XFree(bytes) == 0)
        TerminateInvariant();
}

bool ReadX11Property(
    Display& display,
    const Window window,
    const Atom property,
    const bool remove,
    const usize maxBytes,
    X11Property& result){
    X11CheckedOperation operation(display);
    const int status = XGetWindowProperty(
        &display, window, property, 0, static_cast<long>((maxBytes + 3u) / 4u), remove ? True : False,
        AnyPropertyType, &result.type, &result.format, &result.count, &result.remaining, &result.bytes
    );
    return operation.succeeded() && status == 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

