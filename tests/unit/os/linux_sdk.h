// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>

#include <core/os/linux/pipe_io.h>
#include <core/os/linux/x11/clipboard.h>
#include <core/os/linux/x11/x11_checked.h>

#include <X11/Xatom.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::OsSdk{

class DisplayConnection final : NoCopy{
public:
    ~DisplayConnection()noexcept{ if(m_display) XCloseDisplay(m_display); }
    b8 open(){ m_display = XOpenDisplay(nullptr); return m_display != nullptr; }
    int close(){ auto* value = m_display; m_display = nullptr; return XCloseDisplay(value); }
    b8 valid()const noexcept{ return m_display != nullptr; }
    Display& display()const noexcept{ return *m_display; }
    int pending()const{ return XPending(m_display); }
    int next(XEvent& event){ return XNextEvent(m_display, &event); }
    b8 checkTyped(const int type, XEvent& event){ return XCheckTypedEvent(m_display, type, &event) != False; }
    Atom atom(const char* const name)const{ return XInternAtom(m_display, name, False); }
    Window rootWindow()const{ return DefaultRootWindow(m_display); }
    Window createWindow(const Window parent, const unsigned width, const unsigned height){ return XCreateSimpleWindow(m_display, parent, 0, 0, width, height, 0, 0, 0); }
    void destroyWindow(const Window window){ XDestroyWindow(m_display, window); }
    void sync()const{ XSync(m_display, False); }
    void setOwner(const Atom atom, const Window window, const Time time){ XSetSelectionOwner(m_display, atom, window, time); }
    Window owner(const Atom atom)const{ return XGetSelectionOwner(m_display, atom); }

private:
    Display* m_display = nullptr;
};

[[nodiscard]] inline Atom SelectionClearAtom(const XEvent& event)noexcept{ return event.xselectionclear.selection; }
[[nodiscard]] inline isize WriteFd(const i32 fd, const AStringView bytes)noexcept{ return write(fd, bytes.data(), bytes.size()); }
[[nodiscard]] inline i32 PollFd(pollfd& descriptor, const i32 timeout)noexcept{ return poll(&descriptor, 1u, timeout); }
[[nodiscard]] inline i32 CurrentSignalMask(sigset_t& mask)noexcept{ return pthread_sigmask(SIG_BLOCK, nullptr, &mask); }
[[nodiscard]] inline i32 SignalMember(const sigset_t& mask, const i32 signal)noexcept{ return sigismember(&mask, signal); }

};
NWB_END

#undef None
#undef Status
#undef Bool
#undef Success


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

