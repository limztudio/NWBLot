// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>

#include <global/floating_point_environment.h>
#include <global/simdmath.h>
#if defined(FOUNDATION_PLATFORM_WINDOWS)
#include <windows.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::FloatingPointControl{

[[nodiscard]] inline i32 ReadPowerError()noexcept{ return errno; }
inline void WritePowerError(const i32 value)noexcept{ errno = value; }
inline constexpr i32 s_SeedPowerError = E2BIG;

#if defined(FOUNDATION_PLATFORM_WINDOWS) && defined(FOUNDATION_HAS_NEON64) && !defined(_M_ARM64EC)
inline constexpr b8 s_HasPowerStatus = true;
inline constexpr u32 s_DenormalSave = _DN_SAVE;
inline constexpr u32 s_DenormalFlush = _DN_FLUSH;
inline void WriteRawStatus(const u64 value)noexcept{ __asm__ __volatile__("msr fpsr, %0" : : "r"(value)); }
[[nodiscard]] inline u64 ReadRawStatus()noexcept{
    u64 value = 0;
    __asm__ __volatile__("mrs %0, fpsr" : "=r"(value));
    return value;
}
#else
inline constexpr b8 s_HasPowerStatus = false;
inline constexpr u32 s_DenormalSave = 0u;
inline constexpr u32 s_DenormalFlush = 0u;
inline void WriteRawStatus(u64)noexcept{}
[[nodiscard]] inline u64 ReadRawStatus()noexcept{ return 0u; }
#endif

#if defined(FOUNDATION_PLATFORM_WINDOWS)
inline void ClearNativeStatus()noexcept{ _clearfp(); }
[[nodiscard]] inline u32 ReadNativeStatus()noexcept{ return _statusfp(); }
#else
inline void ClearNativeStatus()noexcept{}
[[nodiscard]] inline u32 ReadNativeStatus()noexcept{ return 0u; }
#endif

#if defined(FOUNDATION_HAS_SSE2) || defined(FOUNDATION_HAS_AVX)
inline constexpr b8 s_HasMxcsr = true;
[[nodiscard]] inline u32 ReadMxcsr()noexcept{ return _mm_getcsr(); }
inline void WriteMxcsr(const u32 value)noexcept{ _mm_setcsr(value); }
#else
inline constexpr b8 s_HasMxcsr = false;
[[nodiscard]] inline u32 ReadMxcsr()noexcept{ return 0u; }
inline void WriteMxcsr(u32)noexcept{}
#endif


#if defined(FOUNDATION_PLATFORM_WINDOWS)
inline constexpr b8 s_HasWindowsTraps = true;
inline constexpr u32 s_InvalidMask = _EM_INVALID, s_OverflowMask = _EM_OVERFLOW, s_ZeroDivideMask = _EM_ZERODIVIDE;
inline constexpr u32 s_UnderflowMask = _EM_UNDERFLOW, s_InexactMask = _EM_INEXACT;
inline constexpr u32 s_ControlExceptionMask = _MCW_EM, s_ControlDenormalMask = _MCW_DN;
inline constexpr u32 s_InvalidException = EXCEPTION_FLT_INVALID_OPERATION, s_OverflowException = EXCEPTION_FLT_OVERFLOW;
inline constexpr u32 s_ZeroDivideException = EXCEPTION_FLT_DIVIDE_BY_ZERO, s_UnderflowException = EXCEPTION_FLT_UNDERFLOW;
[[nodiscard]] inline b8 UpdateControl(u32& current, u32 value, u32 mask)noexcept{ return _controlfp_s(&current, value, mask) == 0; }
[[nodiscard]] inline u32 ClearMappedStatus()noexcept{ return _clearfp(); }

template<typename Function>
[[nodiscard]] u32 CatchHardwareException(const Function& action){
    __try{ action(); }
    __except(EXCEPTION_EXECUTE_HANDLER){ return _exception_code(); }
    return 0u;
}
#else
inline constexpr b8 s_HasWindowsTraps = false;
inline constexpr u32 s_InvalidMask = 0, s_OverflowMask = 0, s_ZeroDivideMask = 0, s_UnderflowMask = 0, s_InexactMask = 0;
inline constexpr u32 s_ControlExceptionMask = 0, s_ControlDenormalMask = 0;
inline constexpr u32 s_InvalidException = 0, s_OverflowException = 0, s_ZeroDivideException = 0, s_UnderflowException = 0;
[[nodiscard]] inline b8 UpdateControl(u32&, u32, u32)noexcept{ return false; }
[[nodiscard]] inline u32 ClearMappedStatus()noexcept{ return 0; }
template<typename Function>
[[nodiscard]] u32 CatchHardwareException(const Function& action){ action(); return 0u; }
#endif

#if defined(FOUNDATION_PLATFORM_WINDOWS) && defined(FOUNDATION_HAS_NEON64) && !defined(_M_ARM64EC)
[[nodiscard]] inline u64 ReadRawControl()noexcept{
    u64 value = 0u;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(value));
    return value;
}
inline void WriteRawControl(const u64 value)noexcept{ __asm__ __volatile__("msr fpcr, %0" : : "r"(value)); }
#else
[[nodiscard]] inline u64 ReadRawControl()noexcept{ return 0u; }
inline void WriteRawControl(u64)noexcept{}
#endif

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

