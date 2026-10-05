// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_input_service.h"

#include <core/common/log.h>

#include <cerrno>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


WaylandTextInputService::WaylandTextInputService(
    Alloc::GlobalArena& arena,
    wl_display& display,
    wl_surface& surface)
    : QueuedTextInputService(arena)
    , m_display(display)
#if defined(NWB_OS_WITH_TEXT_INPUT_V3)
    , m_surface(surface)
    , m_wireSurrounding(arena)
    , m_pendingPreedit(arena)
    , m_pendingCommit(arena)
#endif
{
#if !defined(NWB_OS_WITH_TEXT_INPUT_V3)
    static_cast<void>(surface);
#endif
}

WaylandTextInputService::~WaylandTextInputService(){
    releaseDevice();
#if defined(NWB_OS_WITH_TEXT_INPUT_V3)
    if(m_manager)
        zwp_text_input_manager_v3_destroy(m_manager);
    if(m_registry)
        wl_registry_destroy(m_registry);
#endif
}

bool WaylandTextInputService::initialize(){
#if defined(NWB_OS_WITH_TEXT_INPUT_V3)
    m_registry = wl_display_get_registry(&m_display);
    if(!m_registry)
        return false;
    static const wl_registry_listener listener{ &onRegistryGlobal, &onRegistryRemove };
    if(wl_registry_add_listener(m_registry, &listener, this) != 0)
        return false;
    if(wl_display_roundtrip(&m_display) < 0)
        return false;
#endif
    return true;
}

void WaylandTextInputService::attachSeat(wl_seat* const seat, const u32 seatGlobalName){
    GLB_ASSERT(isOwnerThread());
    if(seat == m_seat && seatGlobalName == m_seatName)
        return;
    const TextInputSessionToken token = activeSession();
    if(token.valid() && !cancelSession(token, TextInputCancelReason::NativeCancelled))
        NWB_LOGGER_WARNING(GLB_TEXT("Wayland text input: seat removal could not cancel current session"));
    releaseDevice();
    m_seat = seat;
    m_seatName = seat ? seatGlobalName : 0u;
#if defined(NWB_OS_WITH_TEXT_INPUT_V3)
    if(!m_manager || !seat)
        return;
    m_input = zwp_text_input_manager_v3_get_text_input(m_manager, seat);
    if(!m_input)
        return;
    static const zwp_text_input_v3_listener listener{
        &onEnter, &onLeave, &onPreedit, &onCommit, &onDelete, &onDone,
#if defined(ZWP_TEXT_INPUT_V3_ACTION_SINCE_VERSION)
        &onAction, &onLanguage, &onPreeditHint,
#endif
    };
    if(zwp_text_input_v3_add_listener(m_input, &listener, this) != 0)
        GLB_FATAL_ASSERT(false);
#endif
}

bool WaylandTextInputService::setKeyboardFocused(const bool focused){
    m_keyboardFocused = focused;
    return setFocused(focused);
}

void WaylandTextInputService::setBufferScale(const i32 scale){
    GLB_ASSERT(isOwnerThread());
    const i32 newScale = Max(scale, 1);
    if(newScale == m_bufferScale)
        return;
    m_bufferScale = newScale;
    if(m_enabled && updateNativeCaret(caretRect()) != TextInputAdmission::Accepted)
        nativeFailure();
}

bool WaylandTextInputService::dispatchDirectCodePoint(const u32 codePoint){
    GLB_ASSERT(isOwnerThread());
    const TextInputSessionToken token = activeSession();
    if(!token.valid())
        return false;
#if defined(NWB_OS_WITH_TEXT_INPUT_V3)
    if(m_manager)
        return true;
#endif
    if(codePoint < 32u || codePoint == 127u)
        return true;
    char bytes[4]{};
    usize length = 0u;
    if(EncodeTextInputCodePoint(codePoint, bytes, length) != TextInputAdmission::Accepted){
        nativeFailure();
        return true;
    }
    const TextInputAdmission::Enum status = emitCommit(token, AStringView(bytes, length));
    if(status != TextInputAdmission::Accepted && status != TextInputAdmission::InvalidSession)
        nativeFailure();
    return true;
}

TextInputCapabilities WaylandTextInputService::capabilities()const noexcept{
#if defined(NWB_OS_WITH_TEXT_INPUT_V3)
    const bool protocol = m_manager != nullptr;
    return { true, protocol, protocol, protocol, TextInputBackend::Wayland };
#else
    return { true, false, false, false, TextInputBackend::Wayland };
#endif
}

TextInputAdmission::Enum WaylandTextInputService::startNativeSession(
    const TextInputSessionToken token,
    const TextInputSessionDesc& desc){
    if(!m_keyboardFocused || !m_seat)
        return TextInputAdmission::Unavailable;
#if defined(NWB_OS_WITH_TEXT_INPUT_V3)
    if(m_manager){
        if(!m_input || !m_entered)
            return TextInputAdmission::Unavailable;
        m_nativeToken = token;
        clearPending();
        m_waitingForCurrentSerial = false;
        m_deferredState.clear();
        m_wireState.reset();
        zwp_text_input_v3_enable(m_input);
        zwp_text_input_v3_set_content_type(m_input, ZWP_TEXT_INPUT_V3_CONTENT_HINT_NONE, ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL);
        sendCaret(desc.caret);
        sendSurrounding(desc.surrounding, desc.anchorByte, desc.caretByte, surroundingRevision());
        m_enabled = true;
        commitState(token, m_wireState.revision());
        if(!flush()){
            endNativeSession(token);
            return TextInputAdmission::NativeFailure;
        }
    }
    else
#endif
    {
        static_cast<void>(desc);
        m_nativeToken = token;
    }
    return TextInputAdmission::Accepted;
}

void WaylandTextInputService::endNativeSession(const TextInputSessionToken token){
    if(m_nativeToken != token)
        return;
    m_nativeToken = {};
    clearPending();
#if defined(NWB_OS_WITH_TEXT_INPUT_V3)
    if(m_input && m_enabled && m_entered){
        zwp_text_input_v3_disable(m_input);
        commitState({}, 0u);
        if(!flush())
            NWB_LOGGER_WARNING(GLB_TEXT("Wayland text input: disabling native session failed"));
    }
    m_serials.reset();
    m_wireState.reset();
    m_waitingForCurrentSerial = false;
    m_deferredState.clear();
#endif
    m_enabled = false;
}

TextInputAdmission::Enum WaylandTextInputService::updateNativeCaret(const TextInputRect caret){
#if defined(NWB_OS_WITH_TEXT_INPUT_V3)
    if(m_enabled && m_input && m_entered){
        if(m_waitingForCurrentSerial){
            m_deferredState.caretChanged();
            return TextInputAdmission::Accepted;
        }
        sendCaret(caret);
        commitState(m_nativeToken, m_wireState.revision());
        return flush() ? TextInputAdmission::Accepted : TextInputAdmission::NativeFailure;
    }
#else
    static_cast<void>(caret);
#endif
    return TextInputAdmission::Accepted;
}

void WaylandTextInputService::updateNativeSurrounding(
    const AStringView text,
    const usize anchorByte,
    const usize caretByte,
    const u64 revision,
    const TextInputChangeCause::Enum cause){
#if defined(NWB_OS_WITH_TEXT_INPUT_V3)
    if(m_enabled && m_input && m_entered){
        if(m_waitingForCurrentSerial){
            m_deferredState.surroundingChanged(cause);
            return;
        }
        sendSurrounding(text, anchorByte, caretByte, revision);
        const u32 nativeCause = cause == TextInputChangeCause::InputMethod
            ? ZWP_TEXT_INPUT_V3_CHANGE_CAUSE_INPUT_METHOD : ZWP_TEXT_INPUT_V3_CHANGE_CAUSE_OTHER;
        zwp_text_input_v3_set_text_change_cause(m_input, nativeCause);
        commitState(m_nativeToken, m_wireState.revision());
        if(!flush())
            nativeFailure();
    }
#else
    static_cast<void>(text);
    static_cast<void>(anchorByte);
    static_cast<void>(caretByte);
    static_cast<void>(revision);
    static_cast<void>(cause);
#endif
}

void WaylandTextInputService::releaseDevice(){
#if defined(NWB_OS_WITH_TEXT_INPUT_V3)
    if(m_input){
        zwp_text_input_v3_destroy(m_input);
        m_input = nullptr;
    }
    m_commitSerial = 0u;
    m_serials.reset();
    m_wireState.reset();
#endif
    m_entered = false;
    m_enabled = false;
    m_nativeToken = {};
    clearPending();
}

void WaylandTextInputService::clearPending(){
#if defined(NWB_OS_WITH_TEXT_INPUT_V3)
    m_pendingPreedit.clear();
    m_pendingCommit.clear();
    m_pendingBegin = 0;
    m_pendingEnd = 0;
    m_pendingBefore = 0u;
    m_pendingAfter = 0u;
    m_hasPreedit = false;
    m_hasCommit = false;
    m_hasDelete = false;
    m_pendingInvalid = false;
#endif
}

void WaylandTextInputService::nativeFailure(){
    const TextInputSessionToken token = activeSession();
    if(token.valid() && !cancelSession(token, TextInputCancelReason::NativeFailure))
        NWB_LOGGER_WARNING(GLB_TEXT("Wayland text input: native failure could not cancel current session"));
}

bool WaylandTextInputService::flush(){
    return wl_display_flush(&m_display) >= 0 || errno == EAGAIN;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GlobalUniquePtr<ITextInputService> CreateWaylandTextInputService(
    Alloc::GlobalArena& arena,
    wl_display& display,
    wl_surface& surface){
    auto service = MakeGlobalUnique<WaylandTextInputService>(arena, arena, display, surface);
    if(!service->initialize())
        return nullptr;
    return service;
}

void AttachWaylandTextInputSeat(ITextInputService& service, wl_seat* const seat, const u32 seatGlobalName){
    if(service.isOwnerThread() && service.capabilities().backend == TextInputBackend::Wayland)
        CheckedCast<WaylandTextInputService*>(&service)->attachSeat(seat, seatGlobalName);
}

bool SetWaylandTextInputKeyboardFocus(ITextInputService& service, const bool focused){
    if(!service.isOwnerThread() || service.capabilities().backend != TextInputBackend::Wayland)
        return false;
    return CheckedCast<WaylandTextInputService*>(&service)->setKeyboardFocused(focused);
}

void SetWaylandTextInputBufferScale(ITextInputService& service, const i32 scale){
    if(service.isOwnerThread() && service.capabilities().backend == TextInputBackend::Wayland)
        CheckedCast<WaylandTextInputService*>(&service)->setBufferScale(scale);
}

bool DispatchWaylandDirectTextInput(ITextInputService& service, const u32 codePoint){
    if(!service.isOwnerThread() || service.capabilities().backend != TextInputBackend::Wayland)
        return false;
    return CheckedCast<WaylandTextInputService*>(&service)->dispatchDirectCodePoint(codePoint);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

