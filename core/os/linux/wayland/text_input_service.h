// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "text_input.h"
#include "text_input_state.h"

#include <core/os/text_input_service.h>
#include <core/os/text_input_text.h>

#pragma push_macro("interface")
#undef interface
#include <wayland-client.h>
#if defined(NWB_OS_WITH_TEXT_INPUT_V3)
#include <text-input-v3-client-protocol.h>
#endif
#pragma pop_macro("interface")


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class WaylandTextInputService final : public QueuedTextInputService{
private:
#if defined(NWB_OS_WITH_TEXT_INPUT_V3)
    static void onRegistryGlobal(void* data, wl_registry* registry, u32 name, const char* interfaceName, u32 version);
    static void onRegistryRemove(void* data, wl_registry* registry, u32 name);
    static void onEnter(void* data, zwp_text_input_v3* input, wl_surface* surface);
    static void onLeave(void* data, zwp_text_input_v3* input, wl_surface* surface);
    static void onPreedit(void* data, zwp_text_input_v3* input, const char* text, i32 begin, i32 end);
    static void onCommit(void* data, zwp_text_input_v3* input, const char* text);
    static void onDelete(void* data, zwp_text_input_v3* input, u32 before, u32 after);
    static void onDone(void* data, zwp_text_input_v3* input, u32 serial);
#if defined(ZWP_TEXT_INPUT_V3_ACTION_SINCE_VERSION)
    static void onAction(void* data, zwp_text_input_v3* input, u32 action, u32 serial);
    static void onLanguage(void* data, zwp_text_input_v3* input, const char* language);
    static void onPreeditHint(void* data, zwp_text_input_v3* input, u32 start, u32 end, u32 hint);
#endif
#endif


public:
    WaylandTextInputService(Alloc::GlobalArena& arena, wl_display& display, wl_surface& surface);
    virtual ~WaylandTextInputService()override;


public:
    [[nodiscard]] bool initialize();
    void attachSeat(wl_seat* seat, u32 seatGlobalName);
    [[nodiscard]] bool setKeyboardFocused(bool focused);
    void setBufferScale(i32 scale);
    [[nodiscard]] bool dispatchDirectCodePoint(u32 codePoint);
    [[nodiscard]] virtual TextInputCapabilities capabilities()const noexcept override;


protected:
    [[nodiscard]] virtual TextInputAdmission::Enum startNativeSession(
        TextInputSessionToken token, const TextInputSessionDesc& desc
    )override;
    virtual void endNativeSession(TextInputSessionToken token)override;
    [[nodiscard]] virtual TextInputAdmission::Enum updateNativeCaret(TextInputRect caret)override;
    virtual void updateNativeSurrounding(
        AStringView text, usize anchorByte, usize caretByte, u64 revision, TextInputChangeCause::Enum cause
    )override;


private:
    void releaseDevice();
    void clearPending();
    void nativeFailure();
    [[nodiscard]] bool flush();
#if defined(NWB_OS_WITH_TEXT_INPUT_V3)
    void commitState(TextInputSessionToken token, u64 revision);
    void sendCaret(TextInputRect caret);
    void sendSurrounding(AStringView text, usize anchorByte, usize caretByte, u64 revision);
    void receiveDone(u32 serial);
#endif


private:
    wl_display& m_display;
#if defined(NWB_OS_WITH_TEXT_INPUT_V3)
    wl_surface& m_surface;
#endif
    wl_seat* m_seat = nullptr;
    u32 m_seatName = 0u;
    i32 m_bufferScale = 1;
    bool m_keyboardFocused = false;
    bool m_entered = false;
    bool m_enabled = false;
    TextInputSessionToken m_nativeToken;
#if defined(NWB_OS_WITH_TEXT_INPUT_V3)
    wl_registry* m_registry = nullptr;
    zwp_text_input_manager_v3* m_manager = nullptr;
    zwp_text_input_v3* m_input = nullptr;
    u32 m_managerName = 0u;
    u32 m_commitSerial = 0u;
    WaylandTextInputSerialTracker m_serials;
    WaylandTextInputSurroundingState m_wireState;
    AString<Alloc::GlobalArena> m_wireSurrounding;
    AString<Alloc::GlobalArena> m_pendingPreedit;
    AString<Alloc::GlobalArena> m_pendingCommit;
    i32 m_pendingBegin = 0;
    i32 m_pendingEnd = 0;
    u32 m_pendingBefore = 0u;
    u32 m_pendingAfter = 0u;
    bool m_hasPreedit = false;
    bool m_hasCommit = false;
    bool m_hasDelete = false;
    bool m_pendingInvalid = false;
    bool m_waitingForCurrentSerial = false;
    WaylandTextInputDeferredState m_deferredState;
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

