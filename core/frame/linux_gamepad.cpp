// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "linux_gamepad.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(FOUNDATION_PLATFORM_LINUX)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/simd/vector.h>

#include <cerrno>
#include <fcntl.h>
#include <linux/joystick.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <unistd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FrameDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_linux_gamepad{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f32 s_ReconnectScanIntervalSeconds = 2.0f;
static constexpr f32 s_AxisChangeEpsilon = 1e-4f;
static constexpr f32 s_NegAxisScale = 1.0f / 32768.0f;
static constexpr f32 s_PosAxisScale = 1.0f / 32767.0f;
static constexpr Array<const char*, s_MaxGamepadCount> s_JoystickPaths = {{
    "/dev/input/js0",
    "/dev/input/js1",
    "/dev/input/js2",
    "/dev/input/js3",
}};


static i32 MapJoystickButton(const u8 number)noexcept{
    switch(number){
    case 0u: return GamepadButton::A;
    case 1u: return GamepadButton::B;
    case 2u: return GamepadButton::X;
    case 3u: return GamepadButton::Y;
    case 4u: return GamepadButton::LeftBumper;
    case 5u: return GamepadButton::RightBumper;
    case 6u: return GamepadButton::Back;
    case 7u: return GamepadButton::Start;
    case 8u: return GamepadButton::Guide;
    case 9u: return GamepadButton::LeftThumb;
    case 10u: return GamepadButton::RightThumb;
    default: return -1;
    }
}

static f32 NormalizeSignedAxis(const i16 rawValue, const b8 invert)noexcept{
    const f32 raw = invert ? -static_cast<f32>(rawValue) : static_cast<f32>(rawValue);
    const SIMDVectorSingle vec = VectorReplicate(raw);
    const SIMDVectorSingle scale = VectorSelect(VectorReplicate(s_PosAxisScale), VectorReplicate(s_NegAxisScale), VectorLess(vec, VectorZero()));
    return VectorGetX(VectorClamp(VectorMultiply(vec, scale), VectorReplicate(-1.0f), VectorSplatOne()));
}

static f32 NormalizeTriggerAxis(const i16 rawValue)noexcept{
    const SIMDVectorSingle raw = VectorReplicate(static_cast<f32>(rawValue));
    const SIMDVectorSingle scale = VectorSelect(VectorReplicate(s_PosAxisScale), VectorReplicate(s_NegAxisScale), VectorLess(raw, VectorZero()));
    const SIMDVectorSingle signedNorm = VectorMultiply(raw, scale);
    const SIMDVectorSingle unit = VectorMultiply(VectorAdd(signedNorm, VectorSplatOne()), VectorReplicate(0.5f));
    return VectorGetX(VectorClamp(unit, VectorZero(), VectorSplatOne()));
}

static void SetButtonState(InputDispatcher& input, const u32 slotIndex, const i32 button, const b8 pressed, u32& buttons){
    if(button < 0 || button >= GamepadButton::kCount)
        return;
    const u32 bit = static_cast<u32>(1u) << static_cast<u32>(button);
    const b8 wasDown = (buttons & bit) != 0u;
    if(pressed == wasDown)
        return;
    if(pressed){
        buttons |= bit;
        input.gamepadButtonUpdate(slotIndex, button, InputAction::Press);
    }
    else{
        buttons &= ~bit;
        input.gamepadButtonUpdate(slotIndex, button, InputAction::Release);
    }
}

static void EmitAxisIfChanged(
    InputDispatcher& input,
    const u32 slotIndex,
    const i32 axisIndex,
    const f32 nextValue,
    Array<f32, static_cast<usize>(GamepadAxis::kCount)>& cachedAxes
){
    const usize idx = static_cast<usize>(axisIndex);
    const f32 delta = VectorGetX(VectorAbs(VectorSubtract(VectorReplicate(nextValue), VectorReplicate(cachedAxes[idx]))));
    if(delta > s_AxisChangeEpsilon){
        cachedAxes[idx] = nextValue;
        input.gamepadAxisUpdate(slotIndex, axisIndex, nextValue);
    }
}

static b8 IsGamepadButtonCode(const u16 code)noexcept{
    return (code >= BTN_JOYSTICK && code < BTN_DIGI)
        || (code >= BTN_WHEEL && code < KEY_OK)
        || (code >= BTN_DPAD_UP && code <= BTN_GRIPR2)
        || (code >= BTN_TRIGGER_HAPPY1 && code <= BTN_TRIGGER_HAPPY40);
}

static b8 IsGamepadDevice(const i32 fd)noexcept{
    u8 buttonCount = 0u;
    if(::ioctl(fd, JSIOCGBUTTONS, &buttonCount) < 0 || buttonCount == 0u)
        return false;

    Array<u16, KEY_MAX - BTN_MISC + 1> buttonMap{};
    if(::ioctl(fd, JSIOCGBTNMAP, buttonMap.data()) < 0)
        return false;

    const u32 clampedCount = static_cast<u32>(buttonCount) < static_cast<u32>(buttonMap.size())
        ? static_cast<u32>(buttonCount)
        : static_cast<u32>(buttonMap.size());
    for(u32 idx = 0u; idx < clampedCount; ++idx){
        if(IsGamepadButtonCode(buttonMap[idx]))
            return true;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


LinuxGamepadPoller::LinuxGamepadPoller()noexcept{
    m_inotifyFd = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if(m_inotifyFd >= 0){
        m_inotifyWatch = ::inotify_add_watch(m_inotifyFd, "/dev/input", IN_CREATE | IN_ATTRIB | IN_MOVED_TO);
        if(m_inotifyWatch < 0){
            ::close(m_inotifyFd);
            m_inotifyFd = -1;
        }
    }
}

LinuxGamepadPoller::~LinuxGamepadPoller()noexcept{
    for(u32 i = 0u; i < s_MaxGamepadCount; ++i)
        closeSlot(i);
    if(m_inotifyFd >= 0){
        if(m_inotifyWatch >= 0)
            ::inotify_rm_watch(m_inotifyFd, m_inotifyWatch);
        ::close(m_inotifyFd);
        m_inotifyWatch = -1;
        m_inotifyFd = -1;
    }
}

void LinuxGamepadPoller::closeSlot(const u32 index)noexcept{
    SlotState& slot = m_slots[index];
    if(slot.fd >= 0){
        ::close(slot.fd);
        slot.fd = -1;
    }
}

void LinuxGamepadPoller::releaseSlot(InputDispatcher& input, const u32 index){
    using namespace __hidden_linux_gamepad;

    SlotState& slot = m_slots[index];
    for(i32 button = 0; button < GamepadButton::kCount; ++button){
        const u32 bit = static_cast<u32>(1u) << static_cast<u32>(button);
        if((slot.buttons & bit) != 0u){
            slot.buttons &= ~bit;
            input.gamepadButtonUpdate(index, button, InputAction::Release);
        }
    }
    slot.buttons = 0u;
    slot.hatX = 0;
    slot.hatY = 0;
    for(usize axisIdx = 0u; axisIdx < slot.axes.size(); ++axisIdx){
        const f32 magnitude = VectorGetX(VectorAbs(VectorReplicate(slot.axes[axisIdx])));
        if(magnitude > s_AxisChangeEpsilon){
            slot.axes[axisIdx] = 0.0f;
            input.gamepadAxisUpdate(index, static_cast<i32>(axisIdx), 0.0f);
        }
        else
            slot.axes[axisIdx] = 0.0f;
    }
}

void LinuxGamepadPoller::poll(InputDispatcher& input, const f32 delta, const b8 allowInput){
    using namespace __hidden_linux_gamepad;

    if(!allowInput){
        releaseAll(input);
        return;
    }

    if(m_inotifyFd >= 0){
        char notifyBuf[512];
        for(;;){
            const ssize_t bytesRead = ::read(m_inotifyFd, notifyBuf, sizeof(notifyBuf));
            if(bytesRead > 0){
                m_topologyDirty = true;
                if(bytesRead < static_cast<ssize_t>(sizeof(notifyBuf)))
                    break;
                continue;
            }
            break;
        }
    }

    const f32 safeDelta = delta > 0.0f ? delta : 0.0f;
    const b8 scanAllDisconnected = m_topologyDirty;
    b8 scanOneFallback = false;
    if(scanAllDisconnected){
        m_topologyDirty = false;
        m_fallbackScanCooldown = s_ReconnectScanIntervalSeconds;
    }
    else if(m_inotifyFd < 0){
        m_fallbackScanCooldown = VectorGetX(VectorMax(VectorZero(), VectorSubtract(VectorReplicate(m_fallbackScanCooldown), VectorReplicate(safeDelta))));
        if(m_fallbackScanCooldown <= 0.0f){
            scanOneFallback = true;
            m_fallbackScanCooldown = s_ReconnectScanIntervalSeconds;
        }
    }
    const u32 fallbackSlot = m_fallbackScanSlot;
    if(scanOneFallback)
        m_fallbackScanSlot = (m_fallbackScanSlot + 1u) % s_MaxGamepadCount;

    for(u32 i = 0u; i < s_MaxGamepadCount; ++i){
        SlotState& slot = m_slots[i];
        if(!slot.connected){
            if(!scanAllDisconnected && (!scanOneFallback || i != fallbackSlot))
                continue;

            const i32 fd = ::open(s_JoystickPaths[i], O_RDONLY | O_NONBLOCK);
            if(fd < 0)
                continue;
            if(!IsGamepadDevice(fd)){
                ::close(fd);
                continue;
            }
            slot.fd = fd;
            slot.connected = true;
            slot.buttons = 0u;
            slot.hatX = 0;
            slot.hatY = 0;
            slot.axes.fill(0.0f);
            input.gamepadConnectionUpdate(i, true);
        }

        for(;;){
            struct js_event events[32] = {};
            const ssize_t readBytes = ::read(slot.fd, events, sizeof(events));
            if(readBytes > 0){
                const usize eventCount = static_cast<usize>(readBytes) / sizeof(struct js_event);
                for(usize eventIdx = 0u; eventIdx < eventCount; ++eventIdx){
                    const struct js_event& ev = events[eventIdx];
                    const u8 type = static_cast<u8>(ev.type & ~JS_EVENT_INIT);
                    if(type == JS_EVENT_BUTTON){
                        const i32 mapped = MapJoystickButton(ev.number);
                        SetButtonState(input, i, mapped, ev.value != 0, slot.buttons);
                    }
                    else if(type == JS_EVENT_AXIS){
                        switch(ev.number){
                        case 0u:
                            EmitAxisIfChanged(input, i, GamepadAxis::LeftX, NormalizeSignedAxis(ev.value, false), slot.axes);
                            break;
                        case 1u:
                            EmitAxisIfChanged(input, i, GamepadAxis::LeftY, NormalizeSignedAxis(ev.value, true), slot.axes);
                            break;
                        case 2u:
                            EmitAxisIfChanged(input, i, GamepadAxis::LeftTrigger, NormalizeTriggerAxis(ev.value), slot.axes);
                            break;
                        case 3u:
                            EmitAxisIfChanged(input, i, GamepadAxis::RightX, NormalizeSignedAxis(ev.value, false), slot.axes);
                            break;
                        case 4u:
                            EmitAxisIfChanged(input, i, GamepadAxis::RightY, NormalizeSignedAxis(ev.value, true), slot.axes);
                            break;
                        case 5u:
                            EmitAxisIfChanged(input, i, GamepadAxis::RightTrigger, NormalizeTriggerAxis(ev.value), slot.axes);
                            break;
                        case 6u:
                            slot.hatX = ev.value < 0 ? -1 : (ev.value > 0 ? 1 : 0);
                            SetButtonState(input, i, GamepadButton::DPadLeft, slot.hatX < 0, slot.buttons);
                            SetButtonState(input, i, GamepadButton::DPadRight, slot.hatX > 0, slot.buttons);
                            break;
                        case 7u:
                            slot.hatY = ev.value < 0 ? -1 : (ev.value > 0 ? 1 : 0);
                            SetButtonState(input, i, GamepadButton::DPadUp, slot.hatY < 0, slot.buttons);
                            SetButtonState(input, i, GamepadButton::DPadDown, slot.hatY > 0, slot.buttons);
                            break;
                        default:
                            break;
                        }
                    }
                }
                if(readBytes < static_cast<ssize_t>(sizeof(events)))
                    break;
                continue;
            }
            if(readBytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                break;

            closeSlot(i);
            releaseSlot(input, i);
            slot.connected = false;
            input.gamepadConnectionUpdate(i, false);
            break;
        }
    }
}

void LinuxGamepadPoller::releaseAll(InputDispatcher& input){
    for(u32 i = 0u; i < s_MaxGamepadCount; ++i){
        if(m_slots[i].connected)
            releaseSlot(input, i);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
