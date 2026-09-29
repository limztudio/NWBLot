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
    static Bool onPreeditStart(XIC context, XPointer data, XPointer callData);
    static Bool onPreeditDone(XIC context, XPointer data, XPointer callData);
    static Bool onPreeditDraw(XIC context, XPointer data, XPointer callData);
    static Bool onPreeditCaret(XIC context, XPointer data, XPointer callData);
    static void onInputMethodDestroyed(XIM method, XPointer data, XPointer callData);


public:
    X11TextInputService(Alloc::GlobalArena& arena, Display& display, Window window);
    virtual ~X11TextInputService()override;


public:
    [[nodiscard]] bool initialize();
    [[nodiscard]] bool filterEvent(XEvent& event);
    [[nodiscard]] bool dispatchKey(XKeyEvent& event);
    [[nodiscard]] virtual TextInputCapabilities capabilities()const noexcept override;


protected:
    [[nodiscard]] virtual TextInputAdmission::Enum startNativeSession(
        TextInputSessionToken token, const TextInputSessionDesc& desc
    )override;
    virtual void endNativeSession(TextInputSessionToken token)override;
    [[nodiscard]] virtual TextInputAdmission::Enum updateNativeCaret(TextInputRect caret)override;


private:
    [[nodiscard]] bool createContext();
    [[nodiscard]] bool convertPreeditText(const XIMText& text);
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
    TextInputSessionToken m_nativeToken;
    bool m_caretVisible = true;
    bool m_resetting = false;
    bool m_caretHintSupported = true;
    X11TextInputDispatchFence m_dispatch;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

