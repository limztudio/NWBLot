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

inline constexpr u32 kWindowsExceptionBreakpointCode = 0x80000003u;
inline constexpr u32 kWindowsExceptionAccessViolationCode = 0xC0000005u;
inline constexpr u32 kWindowsExceptionIllegalInstructionCode = 0xC000001Du;
inline constexpr u32 kWindowsExceptionArrayBoundsExceededCode = 0xC000008Cu;
inline constexpr u32 kWindowsExceptionFloatDenormalOperandCode = 0xC000008Du;
inline constexpr u32 kWindowsExceptionFloatDivideByZeroCode = 0xC000008Eu;
inline constexpr u32 kWindowsExceptionFloatInexactResultCode = 0xC000008Fu;
inline constexpr u32 kWindowsExceptionFloatInvalidOperationCode = 0xC0000090u;
inline constexpr u32 kWindowsExceptionFloatOverflowCode = 0xC0000091u;
inline constexpr u32 kWindowsExceptionFloatStackCheckCode = 0xC0000092u;
inline constexpr u32 kWindowsExceptionFloatUnderflowCode = 0xC0000093u;
inline constexpr u32 kWindowsExceptionIntegerDivideByZeroCode = 0xC0000094u;
inline constexpr u32 kWindowsExceptionIntegerOverflowCode = 0xC0000095u;
inline constexpr u32 kWindowsExceptionStackOverflowCode = 0xC00000FDu;

[[nodiscard]] inline const char* WindowsExceptionName(const u64 exceptionCode)noexcept{
    switch(exceptionCode){
    case kWindowsExceptionBreakpointCode:
        return "breakpoint";
    case kWindowsExceptionAccessViolationCode:
        return "access_violation";
    case kWindowsExceptionIllegalInstructionCode:
        return "illegal_instruction";
    case kWindowsExceptionArrayBoundsExceededCode:
        return "array_bounds_exceeded";
    case kWindowsExceptionFloatDenormalOperandCode:
        return "float_denormal_operand";
    case kWindowsExceptionFloatDivideByZeroCode:
        return "float_divide_by_zero";
    case kWindowsExceptionFloatInexactResultCode:
        return "float_inexact_result";
    case kWindowsExceptionFloatInvalidOperationCode:
        return "float_invalid_operation";
    case kWindowsExceptionFloatOverflowCode:
        return "float_overflow";
    case kWindowsExceptionFloatStackCheckCode:
        return "float_stack_check";
    case kWindowsExceptionFloatUnderflowCode:
        return "float_underflow";
    case kWindowsExceptionIntegerDivideByZeroCode:
        return "integer_divide_by_zero";
    case kWindowsExceptionIntegerOverflowCode:
        return "integer_overflow";
    case kWindowsExceptionStackOverflowCode:
        return "stack_overflow";
    default:
        return "windows_exception";
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

