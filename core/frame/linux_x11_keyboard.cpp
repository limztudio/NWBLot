// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "linux_x11_keyboard.h"
#include "frame_detail.h"
#include "input_helpers.h"

#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>

#include <core/common/log.h>
#include <core/frame/frame_platform_values.adhd.h>
#include <core/os/linux/x11/text_input.h>
#include <global/thread.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_LINUX)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FrameDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr usize s_TextInputBufferSize = FrameDetail::s_FramePlatformTextInputBufferSize;


void X11KeyboardInput::reset()noexcept{
    m_detectableAutoRepeat = false;
    m_keyStates.fill(false);
    m_filteredKeyHistory.reset();
}

void X11KeyboardInput::configure(::Display& display)noexcept{
    Bool detectableAutoRepeat = False;
    m_detectableAutoRepeat = XkbSetDetectableAutoRepeat(&display, True, &detectableAutoRepeat) != False && detectableAutoRepeat != False;
}

void X11KeyboardInput::focusLost()noexcept{
    m_keyStates.fill(false);
}

void X11KeyboardInput::synchronizeSession(const TextInputSessionToken session)noexcept{
    m_filteredKeyHistory.synchronizeSession(session);
}

b8 X11KeyboardInput::isRetiredDuplicate(const XKeyEvent& event)const noexcept{
    return m_filteredKeyHistory.isRetiredDuplicate(
        static_cast<u32>(event.keycode), event.type == KeyRelease, static_cast<u32>(event.time),
        static_cast<u32>(event.serial), event.send_event != False, DurationInMS<u64>(TimerNow())
    );
}

void X11KeyboardInput::synchronize(::Display* const display){
    Array<char, s_KeyStateCount / s_BitsPerByte> keymap;
    if(XQueryKeymap(display, keymap.data()) == 0){
        NWB_MEMSET(m_keyStates.data(), 0, sizeof(m_keyStates));
        NWB_LOGGER_ERROR(NWB_TEXT("Frame X11 physical key-state query failed"));
        return;
    }
    // Keys held while focus returns must remain repeats until their physical release.
    for(usize keycode = 0u; keycode < s_KeyStateCount; ++keycode)
        m_keyStates[keycode] = (static_cast<u8>(keymap[keycode / s_BitsPerByte]) & (1u << (keycode % s_BitsPerByte))) != 0u;
}

i32 X11KeyboardInput::TranslateModifiers(u32 state)noexcept{
    i32 mods = 0;

    if(state & ShiftMask)
        mods |= InputModifier::Shift;
    if(state & ControlMask)
        mods |= InputModifier::Control;
    if(state & Mod1Mask)
        mods |= InputModifier::Alt;
    if(state & Mod4Mask)
        mods |= InputModifier::Super;
    if(state & LockMask)
        mods |= InputModifier::CapsLock;
    if(state & Mod2Mask)
        mods |= InputModifier::NumLock;

    return mods;
}


NWB_DECLARE_LINUX_KEY_SYMBOLS(X11KeySymbols, KeySym, XK);

void X11KeyboardInput::DispatchText(InputDispatcher& input, XKeyEvent keyEvent, i32 mods){
    Array<char, s_TextInputBufferSize> buffer;
    KeySym ignored = NoSymbol;
    const i32 byteCount = XLookupString(&keyEvent, buffer.data(), static_cast<i32>(buffer.size()), &ignored, nullptr);
    if(byteCount <= 0)
        return;

    for(i32 i = 0; i < byteCount;){
        const auto decoded = DecodeUtf8CodePoint(AStringView(buffer.data() + i, static_cast<usize>(byteCount - i)));
        const u32 unicode = decoded ? decoded->codePoint : static_cast<u8>(buffer[i]);
        const i32 consumed = decoded ? decoded->byteCount : 1;

        i += consumed;

        if(unicode < s_UnicodeSpace || unicode == s_UnicodeDelete)
            continue;

        input.keyboardCharInput(unicode, mods);
    }
}

void X11KeyboardInput::Dispatch(Frame& frame, const XKeyEvent& keyEvent, i32 action, const b8 dispatchText, const b8 dispatchPhysical){
    XKeyEvent translatedEvent = keyEvent;
    const KeySym keySym = keyEvent.keycode ? XLookupKeysym(&translatedEvent, 0) : NoSymbol;
    const i32 key = TranslateLinuxKeySymbol<X11KeySymbols>(keySym);
    const i32 mods = AdjustModifiersForKey(key, action, TranslateModifiers(keyEvent.state));

    if(dispatchPhysical && keyEvent.keycode)
        frame.input().keyboardUpdate(key, static_cast<i32>(keyEvent.keycode), action, mods);
    if(dispatchText && action != InputAction::Release){
        // Forwarded XIM commits bypass the ordinary physical key policy.
        if(dispatchPhysical && frame.input().keyboardTextBlocked(static_cast<i32>(keyEvent.keycode)))
            return;
        ITextInputService* const textInput = frame.tryTextInput();
        if(!textInput || !DispatchX11TextInputKey(*textInput, translatedEvent))
            X11KeyboardInput::DispatchText(frame.input(), translatedEvent, mods);
    }
}

void X11KeyboardInput::process(Frame& frame, const XKeyEvent& event, const b8 dispatchText){
    const u32 keycode = static_cast<u32>(event.keycode);
    const u32 timestamp = static_cast<u32>(event.time);
    const u32 serial = static_cast<u32>(event.serial);
    const u64 receivedAtMs = DurationInMS<u64>(TimerNow());
    const b8 released = event.type == KeyRelease;
    if(m_filteredKeyHistory.isForwardedDuplicate(keycode, released, timestamp, serial, event.send_event != False, receivedAtMs)){
        // The physical event already ran, but an unfiltered press must still reach XIM text lookup.
        if(dispatchText && !released)
            Dispatch(frame, event, InputAction::Press, true, false);
        return;
    }
    if(event.type == KeyPress){
        if(!dispatchText)
            m_filteredKeyHistory.recordFiltered(keycode, false, timestamp, serial, receivedAtMs);
        i32 action = InputAction::Press;
        if(keycode > 0u && keycode < s_KeyStateCount){
            action = m_keyStates[keycode] ? InputAction::Repeat : InputAction::Press;
            m_keyStates[keycode] = true;
        }
        Dispatch(frame, event, action, dispatchText);
        return;
    }
    // A coalesced autorepeat release must not return later as a real XIM-forwarded release.
    if(!dispatchText)
        m_filteredKeyHistory.recordFiltered(keycode, true, timestamp, serial, receivedAtMs);
    const auto& frameData = frame.data<Common::LinuxFrame>();
    ::Display* const display = static_cast<::Display*>(frameData.nativeDisplay());
    if(!m_detectableAutoRepeat && XEventsQueued(display, QueuedAfterReading) > 0){
        XEvent nextEvent{};
        XPeekEvent(display, &nextEvent);
        if(nextEvent.type == KeyPress && nextEvent.xkey.keycode == event.keycode && nextEvent.xkey.time == event.time)
            return;
    }
    if(keycode < s_KeyStateCount)
        m_keyStates[keycode] = false;
    Dispatch(frame, event, InputAction::Release, false);
}



};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

