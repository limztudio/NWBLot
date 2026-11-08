// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "crash_symbolicate.h"

#include <logger/server/crash_values.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace LoggerCrashSymbolicateDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_DecimalTextBufferCapacity = ::NWB::Log::CrashValues::s_CrashSymbolicateInternalDecimalTextBufferCapacity;
inline constexpr usize s_CrashReportReserveBytes = ::NWB::Log::CrashValues::s_CrashSymbolicateInternalCrashReportReserveBytes;

inline constexpr AStringView s_HexAddressPrefix = ::NWB::Log::CrashValues::s_CrashSymbolicateInternalHexAddressPrefix;
inline constexpr AStringView s_SymbolStorePresentText = ::NWB::Log::CrashValues::s_CrashSymbolicateInternalSymbolStorePresentText;
inline constexpr AStringView s_SymbolStoreMissingText = ::NWB::Log::CrashValues::s_CrashSymbolicateInternalSymbolStoreMissingText;
inline constexpr AStringView s_SymbolStoreErrorText = ::NWB::Log::CrashValues::s_CrashSymbolicateInternalSymbolStoreErrorText;
inline constexpr AStringView s_CallstackSectionHeader = ::NWB::Log::CrashValues::s_CrashSymbolicateInternalCallstackSectionHeader;
inline constexpr AStringView s_AnonymousModulePath = ::NWB::Log::CrashValues::s_CrashSymbolicateLinuxAnonymousModulePath;


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

