// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <core/crash/reason_names.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr StringView s_SignalReasonKind = NWB::Core::Crash::CrashReasonNames::s_SignalReasonKind;
inline constexpr StringView s_WindowsExceptionReasonKind = NWB::Core::Crash::CrashReasonNames::s_WindowsExceptionReasonKind;
inline constexpr StringView s_TerminateReasonKind = NWB::Core::Crash::CrashReasonNames::s_TerminateReasonKind;
inline constexpr StringView s_ManualDumpReasonKind = NWB::Core::Crash::CrashReasonNames::s_ManualDumpReasonKind;
inline constexpr StringView s_GpuCrashReasonKind = NWB::Core::Crash::CrashReasonNames::s_GpuCrashReasonKind;
inline constexpr StringView s_UnknownReasonKind = NWB::Core::Crash::CrashReasonNames::s_UnknownReasonKind;

inline constexpr StringView s_WindowsPlatformName = NWB::Core::Crash::CrashReasonNames::s_WindowsPlatformName;
inline constexpr StringView s_LinuxPlatformName = NWB::Core::Crash::CrashReasonNames::s_LinuxPlatformName;
inline constexpr StringView s_AndroidPlatformName = NWB::Core::Crash::CrashReasonNames::s_AndroidPlatformName;
inline constexpr StringView s_UnknownPlatformName = NWB::Core::Crash::CrashReasonNames::s_UnknownPlatformName;

[[nodiscard]] inline StringView PosixSignalName(const u64 signalNumber)noexcept{
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
    return s_SignalReasonKind;
}

inline constexpr u32 s_WindowsExceptionBreakpointCode = NWB::Core::Crash::CrashReasonNames::s_WindowsExceptionBreakpointCode;
inline constexpr u32 s_WindowsExceptionAccessViolationCode = NWB::Core::Crash::CrashReasonNames::s_WindowsExceptionAccessViolationCode;
inline constexpr u32 s_WindowsExceptionIllegalInstructionCode = NWB::Core::Crash::CrashReasonNames::s_WindowsExceptionIllegalInstructionCode;
inline constexpr u32 s_WindowsExceptionArrayBoundsExceededCode = NWB::Core::Crash::CrashReasonNames::s_WindowsExceptionArrayBoundsExceededCode;
inline constexpr u32 s_WindowsExceptionFloatDenormalOperandCode = NWB::Core::Crash::CrashReasonNames::s_WindowsExceptionFloatDenormalOperandCode;
inline constexpr u32 s_WindowsExceptionFloatDivideByZeroCode = NWB::Core::Crash::CrashReasonNames::s_WindowsExceptionFloatDivideByZeroCode;
inline constexpr u32 s_WindowsExceptionFloatInexactResultCode = NWB::Core::Crash::CrashReasonNames::s_WindowsExceptionFloatInexactResultCode;
inline constexpr u32 s_WindowsExceptionFloatInvalidOperationCode = NWB::Core::Crash::CrashReasonNames::s_WindowsExceptionFloatInvalidOperationCode;
inline constexpr u32 s_WindowsExceptionFloatOverflowCode = NWB::Core::Crash::CrashReasonNames::s_WindowsExceptionFloatOverflowCode;
inline constexpr u32 s_WindowsExceptionFloatStackCheckCode = NWB::Core::Crash::CrashReasonNames::s_WindowsExceptionFloatStackCheckCode;
inline constexpr u32 s_WindowsExceptionFloatUnderflowCode = NWB::Core::Crash::CrashReasonNames::s_WindowsExceptionFloatUnderflowCode;
inline constexpr u32 s_WindowsExceptionIntegerDivideByZeroCode = NWB::Core::Crash::CrashReasonNames::s_WindowsExceptionIntegerDivideByZeroCode;
inline constexpr u32 s_WindowsExceptionIntegerOverflowCode = NWB::Core::Crash::CrashReasonNames::s_WindowsExceptionIntegerOverflowCode;
inline constexpr u32 s_WindowsExceptionStackOverflowCode = NWB::Core::Crash::CrashReasonNames::s_WindowsExceptionStackOverflowCode;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline StringView WindowsExceptionName(const u64 exceptionCode)noexcept{
    return NWB::Core::Crash::CrashReasonNames::WindowsExceptionName(exceptionCode);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CRASH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

