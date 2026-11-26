// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/input/module.h>

#include <global/simd/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace FrameDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr f32 s_ReconnectScanIntervalSeconds = 2.0f;
inline constexpr f32 s_AxisChangeEpsilon = 1e-4f;
inline constexpr f32 s_NegThumbScale = 1.0f / 32768.0f;
inline constexpr f32 s_PosThumbScale = 1.0f / 32767.0f;


struct GamepadScanSchedule{
    u32 fallbackSlot = 0u;
    b8 scanAllDisconnected = false;
    b8 scanOneFallback = false;
};


[[nodiscard]] inline GamepadScanSchedule AdvanceGamepadScanSchedule(
    const f32 delta,
    const b8 allowFallbackCountdown,
    b8& inOutTopologyDirty,
    f32& inOutFallbackScanCooldown,
    u32& inOutFallbackScanSlot
)noexcept{
    GamepadScanSchedule schedule;
    schedule.scanAllDisconnected = inOutTopologyDirty;
    if(schedule.scanAllDisconnected){
        inOutTopologyDirty = false;
        inOutFallbackScanCooldown = s_ReconnectScanIntervalSeconds;
    }
    else if(allowFallbackCountdown){
        const f32 safeDelta = delta > 0.0f ? delta : 0.0f;
        inOutFallbackScanCooldown = VectorGetX(VectorMax(VectorZero(), VectorSubtract(VectorReplicate(inOutFallbackScanCooldown), VectorReplicate(safeDelta))));
        if(inOutFallbackScanCooldown <= 0.0f){
            schedule.scanOneFallback = true;
            inOutFallbackScanCooldown = s_ReconnectScanIntervalSeconds;
        }
    }
    schedule.fallbackSlot = inOutFallbackScanSlot;
    if(schedule.scanOneFallback)
        inOutFallbackScanSlot = (inOutFallbackScanSlot + 1u) % s_MaxGamepadCount;
    return schedule;
}

inline void UpdateGamepadButtonState(InputDispatcher& input, const u32 slotIndex, const i32 button, const b8 pressed, u32& inOutButtons){
    if(button < 0 || button >= GamepadButton::kCount)
        return;
    const u32 bit = static_cast<u32>(1u) << static_cast<u32>(button);
    const b8 wasDown = (inOutButtons & bit) != 0u;
    if(pressed == wasDown)
        return;
    if(pressed){
        inOutButtons |= bit;
        input.gamepadButtonUpdate(slotIndex, button, InputAction::Press);
    }
    else{
        inOutButtons &= ~bit;
        input.gamepadButtonUpdate(slotIndex, button, InputAction::Release);
    }
}

inline void ReleaseGamepadButtonsAndAxes(InputDispatcher& input, const u32 slotIndex, u32& inOutButtons, Array<f32, static_cast<usize>(GamepadAxis::kCount)>& inOutAxes){
    for(i32 button = 0; button < GamepadButton::kCount; ++button){
        const u32 bit = static_cast<u32>(1u) << static_cast<u32>(button);
        if((inOutButtons & bit) != 0u){
            inOutButtons &= ~bit;
            input.gamepadButtonUpdate(slotIndex, button, InputAction::Release);
        }
    }
    inOutButtons = 0u;
    for(usize axisIdx = 0u; axisIdx < inOutAxes.size(); ++axisIdx){
        const f32 magnitude = VectorGetX(VectorAbs(VectorReplicate(inOutAxes[axisIdx])));
        if(magnitude > s_AxisChangeEpsilon){
            inOutAxes[axisIdx] = 0.0f;
            input.gamepadAxisUpdate(slotIndex, static_cast<i32>(axisIdx), 0.0f);
        }
        else
            inOutAxes[axisIdx] = 0.0f;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
