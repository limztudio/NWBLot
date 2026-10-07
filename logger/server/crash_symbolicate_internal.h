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

