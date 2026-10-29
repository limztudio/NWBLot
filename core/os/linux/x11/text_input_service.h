// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "text_input.h"
#include "text_input_preedit.h"
#include "text_input_dispatch.h"

#include <core/os/text_input_service.h>
#include <core/os/text_input_text.h>

#include <X11/Xlib.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class X11TextInputService final : public QueuedTextInputService{
private:
    static Bool OnPreeditStart(XIC context, XPointer data, XPointer callData);
    static Bool OnPreeditDone(XIC context, XPointer data, XPointer callData);
    static Bool OnPreeditDraw(XIC context, XPointer data, XPointer callData);
    static Bool OnPreeditCaret(XIC context, XPointer data, XPointer callData);
    static void OnInputMethodDestroyed(XIM method, XPointer data, XPointer callData);


public:
    X11TextInputService(Alloc::GlobalArena& arena, Display& display, Window window);
    virtual ~X11TextInputService()noexcept override;


public:
    [[nodiscard]] b8 initialize();
    [[nodiscard]] b8 filterEvent(XEvent& event);
    [[nodiscard]] b8 dispatchKey(XKeyEvent& event);
    [[nodiscard]] virtual TextInputCapabilities capabilities()const noexcept override;


protected:
    [[nodiscard]] virtual TextInputAdmission::Enum startNativeSession(TextInputSessionToken token, const TextInputSessionDesc& desc)override;
    virtual void endNativeSession(TextInputSessionToken token)noexcept override;
    [[nodiscard]] virtual TextInputAdmission::Enum updateNativeCaret(TextInputRect caret)override;


private:
    void openMethod();
    [[nodiscard]] b8 createContext();
    [[nodiscard]] b8 convertPreeditText(const XIMText& text);
    void publishPreedit();
    void nativeFailure();
    void releaseContext()noexcept;


private:
    Display& m_display;
    const Window m_window;
    X11PreeditBuffer m_preedit;
    AString<Alloc::GlobalArena> m_insertion;
    AString<Alloc::GlobalArena> m_lookup;
    XIM m_method = nullptr;
    XIC m_context = nullptr;
    XIMStyle m_style = 0u;
    XVaNestedList m_caretAttributes = nullptr;
    TextInputSessionToken m_nativeToken;
    XPoint m_caretSpot{};
    b8 m_caretVisible = true;
    b8 m_resetting = false;
    b8 m_caretHintSupported = true;
    b8 m_reopenPending = false;
    b8 m_shuttingDown = false;
    X11TextInputDispatchFence m_dispatch;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

