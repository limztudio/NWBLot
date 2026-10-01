// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <global/type.h>

#include <csignal>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline const char* PosixSignalName(const u64 signalNumber)noexcept{
#if defined(SIGILL)
    if(signalNumber == static_cast<u64>(SIGILL))
        return "SIGILL";
#endif
#if defined(SIGTRAP)
    if(signalNumber == static_cast<u64>(SIGTRAP))
        return "SIGTRAP";
#endif
#if defined(SIGABRT)
    if(signalNumber == static_cast<u64>(SIGABRT))
        return "SIGABRT";
#endif
#if defined(SIGBUS)
    if(signalNumber == static_cast<u64>(SIGBUS))
        return "SIGBUS";
#endif
#if defined(SIGFPE)
    if(signalNumber == static_cast<u64>(SIGFPE))
        return "SIGFPE";
#endif
#if defined(SIGSEGV)
    if(signalNumber == static_cast<u64>(SIGSEGV))
        return "SIGSEGV";
#endif
    return "signal";
}

inline constexpr u32 s_WindowsExceptionBreakpointCode = 0x80000003u;
inline constexpr u32 s_WindowsExceptionAccessViolationCode = 0xC0000005u;
inline constexpr u32 s_WindowsExceptionIllegalInstructionCode = 0xC000001Du;
inline constexpr u32 s_WindowsExceptionArrayBoundsExceededCode = 0xC000008Cu;
inline constexpr u32 s_WindowsExceptionFloatDenormalOperandCode = 0xC000008Du;
inline constexpr u32 s_WindowsExceptionFloatDivideByZeroCode = 0xC000008Eu;
inline constexpr u32 s_WindowsExceptionFloatInexactResultCode = 0xC000008Fu;
inline constexpr u32 s_WindowsExceptionFloatInvalidOperationCode = 0xC0000090u;
inline constexpr u32 s_WindowsExceptionFloatOverflowCode = 0xC0000091u;
inline constexpr u32 s_WindowsExceptionFloatStackCheckCode = 0xC0000092u;
inline constexpr u32 s_WindowsExceptionFloatUnderflowCode = 0xC0000093u;
inline constexpr u32 s_WindowsExceptionIntegerDivideByZeroCode = 0xC0000094u;
inline constexpr u32 s_WindowsExceptionIntegerOverflowCode = 0xC0000095u;
inline constexpr u32 s_WindowsExceptionStackOverflowCode = 0xC00000FDu;

[[nodiscard]] inline const char* WindowsExceptionName(const u64 exceptionCode)noexcept{
    switch(exceptionCode){
    case s_WindowsExceptionBreakpointCode:
        return "breakpoint";
    case s_WindowsExceptionAccessViolationCode:
        return "access_violation";
    case s_WindowsExceptionIllegalInstructionCode:
        return "illegal_instruction";
    case s_WindowsExceptionArrayBoundsExceededCode:
        return "array_bounds_exceeded";
    case s_WindowsExceptionFloatDenormalOperandCode:
        return "float_denormal_operand";
    case s_WindowsExceptionFloatDivideByZeroCode:
        return "float_divide_by_zero";
    case s_WindowsExceptionFloatInexactResultCode:
        return "float_inexact_result";
    case s_WindowsExceptionFloatInvalidOperationCode:
        return "float_invalid_operation";
    case s_WindowsExceptionFloatOverflowCode:
        return "float_overflow";
    case s_WindowsExceptionFloatStackCheckCode:
        return "float_stack_check";
    case s_WindowsExceptionFloatUnderflowCode:
        return "float_underflow";
    case s_WindowsExceptionIntegerDivideByZeroCode:
        return "integer_divide_by_zero";
    case s_WindowsExceptionIntegerOverflowCode:
        return "integer_overflow";
    case s_WindowsExceptionStackOverflowCode:
        return "stack_overflow";
    default:
        return "windows_exception";
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

