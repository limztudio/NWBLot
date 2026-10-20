// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "crash_symbolicate.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace LoggerCrashSymbolicateDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_DecimalTextBufferCapacity = 32u;
inline constexpr usize s_CrashReportReserveBytes = 4096u;

inline constexpr AStringView s_HexAddressPrefix = "0x";
inline constexpr AStringView s_SymbolStorePresentText = "present";
inline constexpr AStringView s_SymbolStoreMissingText = "missing";
inline constexpr AStringView s_SymbolStoreErrorText = "error";
inline constexpr AStringView s_SignalReasonKind = "signal";
inline constexpr AStringView s_WindowsExceptionReasonKind = "windows_exception";
inline constexpr AStringView s_TerminateReasonKind = "terminate";
inline constexpr AStringView s_ManualDumpReasonKind = "manual_dump";
inline constexpr AStringView s_GpuCrashReasonKind = "gpu_crash";
inline constexpr AStringView s_WindowsPlatformName = "windows";
inline constexpr AStringView s_LinuxPlatformName = "linux";
inline constexpr AStringView s_AndroidPlatformName = "android";
inline constexpr AStringView s_CallstackSectionHeader = "\n[callstack]\n";
inline constexpr AStringView s_ReportLineEnd = "\n";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CrashSymbolicationFailure{
    AStringView operation;
    ErrorCode code;
};

void AppendNativeFailure(CrashReportText& outReport, const CrashSymbolicationFailure& failure);
void AppendHexAddress(CrashReportText& outReport, u64 address);
[[nodiscard]] CrashScratchPath EffectiveSymbolStoreDirectory(Core::Alloc::ScratchArena& arena, const CrashSymbolicationConfig& config);

void AppendLinuxArtifactSummary(Core::Alloc::ScratchArena& scratchArena, const CrashScratchPath& packageDirectory, const CrashSymbolicationConfig& config, CrashReportText& outReport);
void AppendAndroidTombstoneSummary(Core::Alloc::ScratchArena& arena, const CrashScratchPath& packageDirectory, CrashReportText& outReport);
// Cross-platform: rgd ships for Windows + Linux, and a .rgd may be decoded by whichever server ingests the package.
void AppendRadeonGpuDetectiveSummary(Core::Alloc::ScratchArena& arena, const CrashScratchPath& packageDirectory, const CrashSymbolicationConfig& config, CrashReportText& outReport);
// Cross-platform: the Aftermath runtime ships for Windows + Linux, and a .nv-gpudmp may be decoded by whichever server ingests the package.
void AppendAftermathGpuDumpSummary(Core::Alloc::ScratchArena& scratchArena, const CrashScratchPath& packageDirectory, const CrashSymbolicationConfig& config, CrashReportText& outReport);

#if defined(NWB_PLATFORM_WINDOWS)
[[nodiscard]] Expected<bool, CrashSymbolicationFailure> AppendWindowsMinidumpStack(
    Core::Alloc::ScratchArena& scratchArena,
    const CrashScratchPath& packageDirectory,
    const CrashPackageSummary& summary,
    const CrashSymbolicationConfig& config,
    CrashReportText& outReport
);
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

