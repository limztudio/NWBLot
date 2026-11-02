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
inline constexpr AStringView s_CallstackSectionHeader = "\n[callstack]\n";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CrashSymbolicationFailure{
    AStringView operation;
    ErrorCode code;
};

void AppendNativeFailure(const CrashSymbolicationFailure& failure, CrashReportText& outReport);
void AppendHexAddress(u64 address, CrashReportText& outReport);
[[nodiscard]] Expected<u64> ParseCallstackFrameAddress(AStringView line)noexcept;
[[nodiscard]] CrashScratchPath EffectiveSymbolStoreDirectory(const CrashSymbolicationConfig& config, Core::Alloc::ScratchArena& arena);

void AppendLinuxArtifactSummary(const CrashScratchPath& packageDirectory, const CrashSymbolicationConfig& config, Core::Alloc::ScratchArena& scratchArena, CrashReportText& outReport);
void AppendAndroidTombstoneSummary(const CrashScratchPath& packageDirectory, Core::Alloc::ScratchArena& arena, CrashReportText& outReport);
// Cross-platform: rgd ships for Windows + Linux, and a .rgd may be decoded by whichever server ingests the package.
void AppendRadeonGpuDetectiveSummary(const CrashScratchPath& packageDirectory, Core::Alloc::ScratchArena& arena, CrashReportText& outReport);
// Cross-platform: the Aftermath runtime ships for Windows + Linux, and a .nv-gpudmp may be decoded by whichever server ingests the package.
void AppendAftermathGpuDumpSummary(const CrashScratchPath& packageDirectory, Core::Alloc::ScratchArena& scratchArena, CrashReportText& outReport);

#if defined(NWB_PLATFORM_WINDOWS)
[[nodiscard]] Expected<b8, CrashSymbolicationFailure> AppendWindowsMinidumpStack(
    const CrashScratchPath& packageDirectory,
    const CrashPackageSummary& summary,
    const CrashSymbolicationConfig& config,
    Core::Alloc::ScratchArena& scratchArena,
    CrashReportText& outReport
);
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

