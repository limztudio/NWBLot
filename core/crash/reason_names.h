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

[[nodiscard]] inline const char* WindowsExceptionName(const u64 exceptionCode)noexcept{
    switch(exceptionCode){
    case 0x80000003u:
        return "breakpoint";
    case 0xC0000005u:
        return "access_violation";
    case 0xC000001Du:
        return "illegal_instruction";
    case 0xC000008Cu:
        return "array_bounds_exceeded";
    case 0xC000008Du:
        return "float_denormal_operand";
    case 0xC000008Eu:
        return "float_divide_by_zero";
    case 0xC000008Fu:
        return "float_inexact_result";
    case 0xC0000090u:
        return "float_invalid_operation";
    case 0xC0000091u:
        return "float_overflow";
    case 0xC0000092u:
        return "float_stack_check";
    case 0xC0000093u:
        return "float_underflow";
    case 0xC0000094u:
        return "integer_divide_by_zero";
    case 0xC0000095u:
        return "integer_overflow";
    case 0xC00000FDu:
        return "stack_overflow";
    default:
        return "windows_exception";
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

