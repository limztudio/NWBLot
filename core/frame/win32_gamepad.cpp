// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "win32_gamepad.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(FOUNDATION_PLATFORM_WINDOWS)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <windows.h>
#include <xinput.h>

#include <global/simd/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FrameDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_win32_gamepad{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using XInputGetStateFn = DWORD (WINAPI*)(DWORD, XINPUT_STATE*);

struct ButtonMapping{
    u16 xinputMask;
    i32 button;
};

static constexpr Array<ButtonMapping, 14u> s_ButtonMappings = {{
    { XINPUT_GAMEPAD_A, GamepadButton::A },
    { XINPUT_GAMEPAD_B, GamepadButton::B },
    { XINPUT_GAMEPAD_X, GamepadButton::X },
    { XINPUT_GAMEPAD_Y, GamepadButton::Y },
    { XINPUT_GAMEPAD_LEFT_SHOULDER, GamepadButton::LeftBumper },
    { XINPUT_GAMEPAD_RIGHT_SHOULDER, GamepadButton::RightBumper },
    { XINPUT_GAMEPAD_BACK, GamepadButton::Back },
    { XINPUT_GAMEPAD_START, GamepadButton::Start },
    { XINPUT_GAMEPAD_LEFT_THUMB, GamepadButton::LeftThumb },
    { XINPUT_GAMEPAD_RIGHT_THUMB, GamepadButton::RightThumb },
    { XINPUT_GAMEPAD_DPAD_UP, GamepadButton::DPadUp },
    { XINPUT_GAMEPAD_DPAD_RIGHT, GamepadButton::DPadRight },
    { XINPUT_GAMEPAD_DPAD_DOWN, GamepadButton::DPadDown },
    { XINPUT_GAMEPAD_DPAD_LEFT, GamepadButton::DPadLeft },
}};

static constexpr f32 s_ReconnectScanIntervalSeconds = 2.0f;
static constexpr f32 s_AxisChangeEpsilon = 1e-4f;
static constexpr f32 s_NegThumbScale = 1.0f / 32768.0f;
static constexpr f32 s_PosThumbScale = 1.0f / 32767.0f;
static constexpr f32 s_TriggerScale = 1.0f / 255.0f;


static void EmitAxisMask(
    InputDispatcher& input,
    const u32 slotIndex,
    const u32 mask,
    const i32 baseAxis,
    const u32 laneCount,
    const SIMDVectorSingle values,
    Array<f32, static_cast<usize>(GamepadAxis::kCount)>& cachedAxes
){
    const Array<f32, 4u> laneValues = {{ VectorGetX(values), VectorGetY(values), VectorGetZ(values), VectorGetW(values) }};
    for(u32 lane = 0u; lane < laneCount; ++lane){
        if((mask & (1u << lane)) == 0u)
            continue;
        const i32 axisIndex = baseAxis + static_cast<i32>(lane);
        const f32 nextValue = laneValues[lane];
        cachedAxes[static_cast<usize>(axisIndex)] = nextValue;
        input.gamepadAxisUpdate(slotIndex, axisIndex, nextValue);
    }
}

static void ZeroSlotInputs(InputDispatcher& input, const u32 slotIndex, u32& buttons, Array<f32, static_cast<usize>(GamepadAxis::kCount)>& axes){
    for(const ButtonMapping& mapping : s_ButtonMappings){
        const u32 bit = static_cast<u32>(1u) << static_cast<u32>(mapping.button);
        if((buttons & bit) != 0u){
            buttons &= ~bit;
            input.gamepadButtonUpdate(slotIndex, mapping.button, InputAction::Release);
        }
    }
    buttons = 0u;
    for(usize axisIdx = 0u; axisIdx < axes.size(); ++axisIdx){
        const f32 magnitude = VectorGetX(VectorAbs(VectorReplicate(axes[axisIdx])));
        if(magnitude > s_AxisChangeEpsilon){
            axes[axisIdx] = 0.0f;
            input.gamepadAxisUpdate(slotIndex, static_cast<i32>(axisIdx), 0.0f);
        }
        else
            axes[axisIdx] = 0.0f;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Win32GamepadPoller::~Win32GamepadPoller()noexcept{
    if(m_xinputLibrary){
        FreeLibrary(static_cast<HMODULE>(m_xinputLibrary));
        m_xinputLibrary = nullptr;
        m_getStateProc = nullptr;
    }
}

b8 Win32GamepadPoller::ensureLoaded()noexcept{
    if(m_getStateProc)
        return true;
    if(m_loadAttempted)
        return false;
    m_loadAttempted = true;

    HMODULE lib = LoadLibraryW(L"xinput1_4.dll");
    if(!lib)
        lib = LoadLibraryW(L"xinput9_1_0.dll");
    if(!lib)
        return false;

    void* const proc = reinterpret_cast<void*>(GetProcAddress(lib, "XInputGetState"));
    if(!proc){
        FreeLibrary(lib);
        return false;
    }

    m_xinputLibrary = static_cast<void*>(lib);
    m_getStateProc = proc;
    return true;
}

void Win32GamepadPoller::poll(InputDispatcher& input, const f32 delta, const b8 allowInput){
    using namespace __hidden_win32_gamepad;

    if(!allowInput){
        releaseAll(input);
        return;
    }
    if(!ensureLoaded())
        return;

    const XInputGetStateFn getState = reinterpret_cast<XInputGetStateFn>(m_getStateProc);
    const f32 safeDelta = delta > 0.0f ? delta : 0.0f;
    const b8 scanAllDisconnected = m_topologyDirty;
    b8 scanOneFallback = false;
    if(scanAllDisconnected){
        m_topologyDirty = false;
        m_fallbackScanCooldown = s_ReconnectScanIntervalSeconds;
    }
    else{
        m_fallbackScanCooldown = VectorGetX(VectorMax(VectorZero(), VectorSubtract(VectorReplicate(m_fallbackScanCooldown), VectorReplicate(safeDelta))));
        if(m_fallbackScanCooldown <= 0.0f){
            scanOneFallback = true;
            m_fallbackScanCooldown = s_ReconnectScanIntervalSeconds;
        }
    }
    const u32 fallbackSlot = m_fallbackScanSlot;
    if(scanOneFallback)
        m_fallbackScanSlot = (m_fallbackScanSlot + 1u) % s_MaxGamepadCount;

    const SIMDVectorSingle epsVec = VectorReplicate(s_AxisChangeEpsilon);

    for(u32 i = 0u; i < s_MaxGamepadCount; ++i){
        SlotState& slot = m_slots[i];
        XINPUT_STATE state = {};
        if(!slot.connected){
            if(!scanAllDisconnected && (!scanOneFallback || i != fallbackSlot))
                continue;
            if(getState(i, &state) != ERROR_SUCCESS)
                continue;
            slot.connected = true;
            slot.buttons = 0u;
            slot.lastPacketNumber = 0u;
            slot.hasPacketNumber = false;
            slot.axes.fill(0.0f);
            input.gamepadConnectionUpdate(i, true);
        }
        else if(getState(i, &state) != ERROR_SUCCESS){
            ZeroSlotInputs(input, i, slot.buttons, slot.axes);
            slot.connected = false;
            slot.lastPacketNumber = 0u;
            slot.hasPacketNumber = false;
            input.gamepadConnectionUpdate(i, false);
            continue;
        }

        if(slot.hasPacketNumber && state.dwPacketNumber == slot.lastPacketNumber)
            continue;
        slot.lastPacketNumber = state.dwPacketNumber;
        slot.hasPacketNumber = true;

        const XINPUT_GAMEPAD& gp = state.Gamepad;
        for(const ButtonMapping& mapping : s_ButtonMappings){
            const b8 isDown = (gp.wButtons & mapping.xinputMask) != 0u;
            const u32 bit = static_cast<u32>(1u) << static_cast<u32>(mapping.button);
            const b8 wasDown = (slot.buttons & bit) != 0u;
            if(isDown != wasDown){
                if(isDown){
                    slot.buttons |= bit;
                    input.gamepadButtonUpdate(i, mapping.button, InputAction::Press);
                }
                else{
                    slot.buttons &= ~bit;
                    input.gamepadButtonUpdate(i, mapping.button, InputAction::Release);
                }
            }
        }

        const SIMDVectorSingle rawSticks = VectorSet(
            static_cast<f32>(gp.sThumbLX),
            static_cast<f32>(gp.sThumbLY),
            static_cast<f32>(gp.sThumbRX),
            static_cast<f32>(gp.sThumbRY)
        );
        const SIMDVectorSingle stickScales = VectorSelect(VectorReplicate(s_PosThumbScale), VectorReplicate(s_NegThumbScale), VectorLess(rawSticks, VectorZero()));
        const SIMDVectorSingle sticks = VectorClamp(VectorMultiply(rawSticks, stickScales), VectorReplicate(-1.0f), VectorSplatOne());
        const SIMDVectorSingle prevSticks = VectorSet(
            slot.axes[GamepadAxis::LeftX],
            slot.axes[GamepadAxis::LeftY],
            slot.axes[GamepadAxis::RightX],
            slot.axes[GamepadAxis::RightY]
        );
        const u32 stickMask = VectorMoveMask(VectorGreater(VectorAbs(VectorSubtract(sticks, prevSticks)), epsVec));
        if(stickMask != 0u)
            EmitAxisMask(input, i, stickMask, GamepadAxis::LeftX, 4u, sticks, slot.axes);

        const SIMDVectorSingle rawTriggers = VectorSet(static_cast<f32>(gp.bLeftTrigger), static_cast<f32>(gp.bRightTrigger), 0.0f, 0.0f);
        const SIMDVectorSingle triggers = VectorClamp(VectorScale(rawTriggers, s_TriggerScale), VectorZero(), VectorSplatOne());
        const SIMDVectorSingle prevTriggers = VectorSet(slot.axes[GamepadAxis::LeftTrigger], slot.axes[GamepadAxis::RightTrigger], 0.0f, 0.0f);
        const u32 triggerMask = VectorMoveMask(VectorGreater(VectorAbs(VectorSubtract(triggers, prevTriggers)), epsVec)) & 0x3u;
        if(triggerMask != 0u)
            EmitAxisMask(input, i, triggerMask, GamepadAxis::LeftTrigger, 2u, triggers, slot.axes);
    }
}

void Win32GamepadPoller::releaseAll(InputDispatcher& input){
    using namespace __hidden_win32_gamepad;

    for(u32 i = 0u; i < s_MaxGamepadCount; ++i){
        SlotState& slot = m_slots[i];
        if(slot.connected){
            ZeroSlotInputs(input, i, slot.buttons, slot.axes);
            slot.hasPacketNumber = false;
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
