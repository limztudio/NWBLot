// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_input_service.h"

#include <core/common/log.h>

#include <global/scope_exit.h>

#include <X11/Xutil.h>
#include <locale.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_x11_text_input{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Array<XIMStyle, 3u> s_PreferredStyles{
    XIMPreeditCallbacks | XIMStatusNothing,
    XIMPreeditNothing | XIMStatusNothing,
    XIMPreeditNone | XIMStatusNone,
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ContainsStyle(const XIMStyles& styles, const XIMStyle requested)noexcept{
    for(unsigned short index = 0u; index < styles.count_styles; ++index){
        if(styles.supported_styles[index] == requested)
            return true;
    }
    return false;
}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void X11TextInputService::OnInputMethodDestroyed(const XIM method, const XPointer data, XPointer){
    auto& service = *reinterpret_cast<X11TextInputService*>(data);
    if(service.m_method != method)
        return;
    service.m_method = nullptr;
    service.m_context = nullptr;
    service.m_style = 0u;
    service.nativeFailure();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


X11TextInputService::X11TextInputService(Alloc::GlobalArena& arena, Display& display, const Window window)
    : QueuedTextInputService(arena)
    , m_display(display)
    , m_window(window)
    , m_preedit(arena)
    , m_insertion(arena)
    , m_lookup(arena)
{}

X11TextInputService::~X11TextInputService(){
    m_shuttingDown = true;
    if(m_context)
        endNativeSession(m_nativeToken);
    else
        releaseContext();
}

bool X11TextInputService::initialize(){
    // Xlib binds an input method to LC_CTYPE; select the user's native text locale during Frame startup.
    if(!::setlocale(LC_CTYPE, "") || !XSupportsLocale() || !XSetLocaleModifiers("")){
        NWB_LOGGER_WARNING(NWB_TEXT("X11 text input: native locale initialization unavailable"));
        return true;
    }
    openMethod();
    return true;
}

bool X11TextInputService::filterEvent(XEvent& event){
    NWB_ASSERT(isOwnerThread());
    if(!m_context || !activeSession().valid())
        return false;
    m_dispatch.enter();
    ScopeExit finishDispatch([this]()noexcept{
        if(m_dispatch.leave())
            releaseContext();
    });

    // XIM transport replies target its hidden connection window, not the application window.
    return XFilterEvent(&event, None) != False;
}

bool X11TextInputService::dispatchKey(XKeyEvent& event){
    NWB_ASSERT(isOwnerThread());
    const TextInputSessionToken token = activeSession();
    if(!token.valid())
        return false;
    if(!m_context || m_nativeToken != token || event.type != KeyPress || event.window != m_window)
        return true;
    m_dispatch.enter();
    ScopeExit finishDispatch([this]()noexcept{
        if(m_dispatch.leave())
            releaseContext();
    });

    m_lookup.resize(128u);
    KeySym symbol = NoSymbol;
    Status status = XLookupNone;
    i32 bytes = Xutf8LookupString(m_context, &event, m_lookup.data(), static_cast<i32>(m_lookup.size()), &symbol, &status);
    if(token != activeSession())
        return true;
    if(status == XBufferOverflow){
        if(bytes <= 0 || static_cast<usize>(bytes) > s_TextInputMaxEventTextBytes){
            nativeFailure();
            return true;
        }
        m_lookup.resize(static_cast<usize>(bytes));
        bytes = Xutf8LookupString(m_context, &event, m_lookup.data(), bytes, &symbol, &status);
        if(token != activeSession())
            return true;
    }
    if(status != XLookupChars && status != XLookupBoth)
        return true;
    if(bytes <= 0)
        return true;
    const AStringView text(m_lookup.data(), static_cast<usize>(bytes));
    // Editing and shortcut controls remain physical key events, rather than text commits.
    if(text.size() == 1u && (static_cast<u8>(text[0]) < 32u || text[0] == 127))
        return true;
    const TextInputAdmission::Enum admission = emitCommit(token, text);
    if(admission != TextInputAdmission::Accepted && admission != TextInputAdmission::InvalidSession)
        nativeFailure();
    return true;
}

TextInputCapabilities X11TextInputService::capabilities()const noexcept{
    const bool available = !m_shuttingDown && (m_method || m_reopenPending) && m_style;
    return { available, available && (m_style & XIMPreeditCallbacks) != 0u, false, false, TextInputBackend::X11Xim };
}

TextInputAdmission::Enum X11TextInputService::startNativeSession(
    const TextInputSessionToken token,
    const TextInputSessionDesc& desc
){
    if(m_shuttingDown)
        return TextInputAdmission::Unavailable;
    if(m_reopenPending){
        m_reopenPending = false;
        openMethod();
        if(!m_method || !m_style)
            return TextInputAdmission::Unavailable;
    }
    if(!m_method || !m_style)
        return TextInputAdmission::Unsupported;
    m_dispatch.enter();
    ScopeExit finishDispatch([this]()noexcept{
        if(m_dispatch.leave())
            releaseContext();
    });

    m_nativeToken = token;
    m_preedit.clear();
    m_caretVisible = true;
    if(!createContext()){
        m_nativeToken = {};
        return TextInputAdmission::NativeFailure;
    }
    const TextInputAdmission::Enum caretStatus = updateNativeCaret(desc.caret);
    if(caretStatus != TextInputAdmission::Accepted){
        endNativeSession(token);
        return caretStatus;
    }
    if(m_nativeToken != token || activeSession() != token)
        return TextInputAdmission::Unavailable;
    XSetICFocus(m_context);
    return m_nativeToken == token && activeSession() == token ? TextInputAdmission::Accepted : TextInputAdmission::Unavailable;
}

void X11TextInputService::endNativeSession(const TextInputSessionToken token){
    if(m_nativeToken != token)
        return;
    m_nativeToken = {};
    if(m_dispatch.retire())
        releaseContext();
}

TextInputAdmission::Enum X11TextInputService::updateNativeCaret(const TextInputRect caret){
    if(!m_context)
        return TextInputAdmission::Unavailable;
    if(!m_caretHintSupported)
        return TextInputAdmission::Accepted;
    m_dispatch.enter();
    ScopeExit finishDispatch([this]()noexcept{
        if(m_dispatch.leave())
            releaseContext();
    });

    XPoint spot{
        static_cast<short>(Clamp<i32>(caret.x, -32768, 32767)),
        static_cast<short>(Clamp<i64>(static_cast<i64>(caret.y) + caret.height, -32768, 32767)),
    };
    XVaNestedList attributes = XVaCreateNestedList(0, XNSpotLocation, &spot, nullptr);
    if(!attributes)
        return TextInputAdmission::NativeFailure;
    // Callback/no-preedit styles may ignore this optional candidate-window hint.
    const char* const unsupported = XSetICValues(m_context, XNPreeditAttributes, attributes, nullptr);
    if(unsupported){
        m_caretHintSupported = false;
        NWB_LOGGER_WARNING(NWB_TEXT("X11 text input: active input style does not support native caret positioning"));
    }
    XFree(attributes);
    return TextInputAdmission::Accepted;
}

void X11TextInputService::openMethod(){
    m_style = 0u;
    m_caretHintSupported = true;
    m_method = XOpenIM(&m_display, nullptr, nullptr, nullptr);
    if(!m_method){
        NWB_LOGGER_WARNING(NWB_TEXT("X11 text input: input method unavailable; session capability disabled"));
        return;
    }
    XIMStyles* styles = nullptr;
    const char* const error = XGetIMValues(m_method, XNQueryInputStyle, &styles, nullptr);
    if(!error && styles){
        for(const XIMStyle style : __hidden_x11_text_input::s_PreferredStyles){
            if(__hidden_x11_text_input::ContainsStyle(*styles, style)){
                m_style = style;
                break;
            }
        }
    }
    if(styles)
        XFree(styles);
    XIMCallback destroyed{ reinterpret_cast<XPointer>(this), &OnInputMethodDestroyed };
    if(XSetIMValues(m_method, XNDestroyCallback, &destroyed, nullptr))
        NWB_LOGGER_WARNING(NWB_TEXT("X11 text input: input method destruction notification unavailable"));
}

bool X11TextInputService::createContext(){
    if((m_style & XIMPreeditCallbacks) == 0u){
        m_context = XCreateIC(m_method, XNInputStyle, m_style, XNClientWindow, m_window, XNFocusWindow, m_window, nullptr);
    }
    else{
        XICCallback start{ reinterpret_cast<XPointer>(this), &OnPreeditStart };
        XICCallback done{ reinterpret_cast<XPointer>(this), &OnPreeditDone };
        XICCallback draw{ reinterpret_cast<XPointer>(this), &OnPreeditDraw };
        XICCallback caret{ reinterpret_cast<XPointer>(this), &OnPreeditCaret };
        XVaNestedList attributes = XVaCreateNestedList(
            0, XNPreeditStartCallback, &start, XNPreeditDoneCallback, &done,
            XNPreeditDrawCallback, &draw, XNPreeditCaretCallback, &caret, nullptr
        );
        if(!attributes)
            return false;
        m_context = XCreateIC(
            m_method, XNInputStyle, m_style, XNClientWindow, m_window,
            XNFocusWindow, m_window, XNPreeditAttributes, attributes, nullptr
        );
        XFree(attributes);
    }
    if(!m_context)
        return false;
    long filterMask = 0;
    XWindowAttributes attributes{};
    if(!XGetICValues(m_context, XNFilterEvents, &filterMask, nullptr) && XGetWindowAttributes(&m_display, m_window, &attributes))
        XSelectInput(&m_display, m_window, attributes.your_event_mask | filterMask);
    return true;
}

void X11TextInputService::nativeFailure(){
    const TextInputSessionToken token = activeSession();
    if(token.valid() && !cancelSession(token, TextInputCancelReason::NativeFailure))
        NWB_LOGGER_WARNING(NWB_TEXT("X11 text input: native failure could not cancel current session"));
}

void X11TextInputService::releaseContext()noexcept{
    m_resetting = true;
    if(m_context){
        XUnsetICFocus(m_context);
        if(m_context){
            char* const discarded = Xutf8ResetIC(m_context);
            if(discarded)
                XFree(discarded);
        }
        if(m_context){
            XDestroyIC(m_context);
            m_context = nullptr;
        }
    }
    // Forwarded keys can leave connection-wide XIM_SYNC_REPLY work behind after their IC is destroyed.
    // A replacement session gets a fresh connection; its input cannot inherit that retired protocol queue.
    XIM method = m_method;
    m_method = nullptr;
    m_reopenPending = method && XCloseIM(method) != False && m_style != 0u && !m_shuttingDown;
    if(!m_reopenPending)
        m_style = 0u;
    m_resetting = false;
    m_dispatch.released();
    m_preedit.clear();
    m_insertion.clear();
    m_lookup.clear();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GlobalUniquePtr<ITextInputService> CreateX11TextInputService(
    Alloc::GlobalArena& arena,
    Display& display,
    const u64 window
){
    auto service = MakeGlobalUnique<X11TextInputService>(arena, arena, display, static_cast<Window>(window));
    if(!service->initialize())
        return nullptr;
    return service;
}

bool FilterX11TextInputEvent(ITextInputService& service, XEvent& event){
    if(!service.isOwnerThread() || service.capabilities().backend != TextInputBackend::X11Xim)
        return false;
    return checked_cast<X11TextInputService*>(&service)->filterEvent(event);
}

bool DispatchX11TextInputKey(ITextInputService& service, XKeyEvent& event){
    if(!service.isOwnerThread() || service.capabilities().backend != TextInputBackend::X11Xim)
        return false;
    return checked_cast<X11TextInputService*>(&service)->dispatchKey(event);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

