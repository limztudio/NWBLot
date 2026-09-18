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

inline constexpr char s_HexAddressPrefix[] = "0x";
inline constexpr char s_SymbolStorePresentText[] = "present";
inline constexpr char s_SymbolStoreMissingText[] = "missing";
inline constexpr char s_SymbolStoreErrorText[] = "error";
inline constexpr char s_SignalReasonKind[] = "signal";
inline constexpr char s_WindowsExceptionReasonKind[] = "windows_exception";
inline constexpr char s_TerminateReasonKind[] = "terminate";
inline constexpr char s_ManualDumpReasonKind[] = "manual_dump";
inline constexpr char s_GpuCrashReasonKind[] = "gpu_crash";
inline constexpr char s_WindowsPlatformName[] = "windows";
inline constexpr char s_LinuxPlatformName[] = "linux";
inline constexpr char s_AndroidPlatformName[] = "android";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AppendHexAddress(LogArena& arena, CrashReportText& outReport, u64 address);
[[nodiscard]] Path EffectiveSymbolStoreDirectory(LogArena& arena, const CrashSymbolicationConfig& config);

void AppendLinuxArtifactSummary(LogArena& arena, const Path& packageDirectory, const CrashSymbolicationConfig& config, CrashReportText& outReport);
void AppendAndroidTombstoneSummary(LogArena& arena, const Path& packageDirectory, CrashReportText& outReport);
// Cross-platform: rgd ships for Windows + Linux, and a .rgd may be decoded by whichever server ingests the package.
void AppendRadeonGpuDetectiveSummary(LogArena& arena, const Path& packageDirectory, const CrashSymbolicationConfig& config, CrashReportText& outReport);
// Cross-platform: the Aftermath runtime ships for Windows + Linux, and a .nv-gpudmp may be decoded by whichever server ingests the package.
void AppendAftermathGpuDumpSummary(LogArena& arena, const Path& packageDirectory, const CrashSymbolicationConfig& config, CrashReportText& outReport);

#if defined(NWB_PLATFORM_WINDOWS)
[[nodiscard]] bool AppendWindowsMinidumpStack(LogArena& arena, const Path& packageDirectory, const CrashPackageSummary& summary, const CrashSymbolicationConfig& config, CrashReportText& outReport);
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

