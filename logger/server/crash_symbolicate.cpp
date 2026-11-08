// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_symbolicate.h"
#include "crash_report_composer.h"
#include "crash_symbolicate_internal.h"

#include <logger/server/crash_values.adhd.h>

#include <core/crash/package_names.adhd.h>
#include <core/crash/reason_names.h>

#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace LoggerCrashSymbolicateDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_ReportSectionOpen = ::NWB::Log::CrashValues::s_CrashSymbolicateReportSectionOpen;
inline constexpr AStringView s_ExceptionKey = ::NWB::Log::CrashValues::s_CrashSymbolicateExceptionKey;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void AppendOptionalTextFile(const CrashScratchPath& packageDirectory, const AStringView fileName, const AStringView label, Core::Alloc::ScratchArena& scratchArena, CrashReportText& outReport){
    AString<Core::Alloc::ScratchArena> text{scratchArena};
    const auto read = ReadTextFile(packageDirectory / fileName, text);
    if(!read){
        if(read.error() != ErrorCondition::no_such_file_or_directory){
            outReport += s_ReportSectionOpen;
            outReport += label;
            outReport += "]\nstatus=read_failed\n";
            AppendNativeFailure(CrashSymbolicationFailure{ .operation = fileName, .code = read.error(), }, outReport);
        }
        return;
    }
    if(text.empty())
        return;

    outReport += s_ReportSectionOpen;
    outReport += label;
    outReport += "]\n";
    outReport += text;
    if(outReport.back() != '\n')
        outReport += '\n';
}

void AppendNativeFailure(const CrashSymbolicationFailure& failure, CrashReportText& outReport){
    const DiagnosticEventText detail = MakeDiagnosticEventText("operation={}\nnative_category={}\nnative_code={}\n", failure.operation, failure.code.category().name(), failure.code.value());
    outReport += detail.view();
}

void AppendHexAddress(const u64 address, CrashReportText& outReport){
    outReport += s_HexAddressPrefix;
    AppendHexU64(address, outReport);
}

Expected<u64> ParseCallstackFrameAddress(const AStringView line)noexcept{
    const usize prefix = line.find(s_HexAddressPrefix);
    if(prefix == AStringView::npos)
        return MakeUnexpected(Failure{});

    usize end = prefix + s_HexAddressPrefix.size();
    while(end < line.size() && line[end] != ' ' && line[end] != '\t' && line[end] != '\r' && line[end] != '\n')
        ++end;
    if(end == prefix + s_HexAddressPrefix.size())
        return MakeUnexpected(Failure{});

    return ::ParseVariableHexU64(AStringView(line.data() + prefix + s_HexAddressPrefix.size(), end - prefix - s_HexAddressPrefix.size()));
}

CrashScratchPath EffectiveSymbolStoreDirectory(const CrashSymbolicationConfig& config, Core::Alloc::ScratchArena& arena){
    if(!config.symbolStoreDirectory.empty())
        return CrashScratchPath(arena, config.symbolStoreDirectory);

    return CrashDefaultRootDirectory(arena) / s_CrashSymbolStoreDirectoryName;
}

static void AppendSymbolStoreStatus(const CrashSymbolicationConfig& config, Core::Alloc::ScratchArena& arena, CrashReportText& outReport){
    const CrashScratchPath symbolStoreDirectory = EffectiveSymbolStoreDirectory(config, arena);
    outReport += "symbol_store=";
    outReport += PathToString<char>(arena, symbolStoreDirectory);
    outReport += "\nsymbol_store_status=";

    const auto exists = IsDirectory(symbolStoreDirectory);
    if(!exists)
        outReport += s_SymbolStoreErrorText;
    else
        outReport += *exists ? s_SymbolStorePresentText : s_SymbolStoreMissingText;
    outReport += s_ReportLineEnd;
}

static void AppendExceptionSummary(const CrashPackageSummary& summary, CrashReportText& outReport){
    if(summary.reasonKind == Core::Crash::s_SignalReasonKind){
        outReport += s_ExceptionKey;
        outReport += Core::Crash::PosixSignalName(summary.reasonCode);
        outReport += " (";
        Array<char, s_DecimalTextBufferCapacity> buffer{};
        outReport += FormatDecimal(static_cast<usize>(summary.reasonCode), buffer);
        outReport += ")\n";
        return;
    }
    if(summary.reasonKind == Core::Crash::s_WindowsExceptionReasonKind){
        outReport += s_ExceptionKey;
        outReport += Core::Crash::WindowsExceptionName(summary.reasonCode);
        outReport += " ";
        AppendHexAddress(summary.reasonCode, outReport);
        outReport += s_ReportLineEnd;
        return;
    }
    if(summary.reasonKind == Core::Crash::s_TerminateReasonKind){
        outReport += "exception=std::terminate\n";
        return;
    }
    if(summary.reasonKind == Core::Crash::s_ManualDumpReasonKind){
        outReport += "exception=manual diagnostic dump\n";
        return;
    }
    if(summary.reasonKind == Core::Crash::s_GpuCrashReasonKind){
        outReport += "exception=GPU device removed / crash\n";
        return;
    }

    outReport += s_ExceptionKey;
    outReport += summary.reasonKind;
    outReport += s_ReportLineEnd;
}

static void AppendEventSummary(const CrashPackageSummary& summary, CrashReportText& outReport){
    outReport += "\n[event]\n";
    outReport += "event=";
    outReport += summary.event;
    outReport += s_ReportLineEnd;

    if(summary.triggerExpression.empty() && summary.triggerMessage.empty() && summary.triggerFile.empty() && summary.triggerLine == 0u)
        AppendExceptionSummary(summary, outReport);
}




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CrashReportText BuildCrashSymbolicationReport(const CrashScratchPath& packageDirectory, const CrashPackageSummary& summary, const CrashSymbolicationConfig& config, LogArena& arena, Core::Alloc::ScratchArena& scratchArena){
    CrashReportText detailReport{arena};
    detailReport.reserve(LoggerCrashSymbolicateDetail::s_CrashReportReserveBytes);

    detailReport += "package=";
    detailReport += PathToString<char>(scratchArena, packageDirectory);
    detailReport += "\ncrash_id=";
    detailReport += summary.crashId;
    detailReport += "\nplatform=";
    detailReport += summary.platform;
    detailReport += "\nreason=";
    detailReport += summary.reasonKind;
    detailReport += "\nartifact_strategy=";
    detailReport += summary.artifactStrategy;
    detailReport += s_ReportLineEnd;
    LoggerCrashSymbolicateDetail::AppendSymbolStoreStatus(config, scratchArena, detailReport);
    LoggerCrashSymbolicateDetail::AppendEventSummary(summary, detailReport);

    if(summary.platform == Core::Crash::s_WindowsPlatformName){
#if defined(NWB_PLATFORM_WINDOWS)
        const auto decoded = LoggerCrashSymbolicateDetail::AppendWindowsMinidumpStack(packageDirectory, summary, config, scratchArena, detailReport);
        if(!decoded)
            LoggerCrashSymbolicateDetail::AppendNativeFailure(decoded.error(), detailReport);
        else if(!*decoded)
            detailReport += "stack_decode=unavailable\n";
#else
        detailReport += "status=not_decoded\nresolver=windows_pdb_minidump\ndetail=Windows minidump resolver is only available on Windows logserver builds\n";
#endif
    }
    else if(summary.platform == Core::Crash::s_LinuxPlatformName){
        LoggerCrashSymbolicateDetail::AppendLinuxArtifactSummary(packageDirectory, config, scratchArena, detailReport);
    }
    else if(summary.platform == Core::Crash::s_AndroidPlatformName){
        LoggerCrashSymbolicateDetail::AppendAndroidTombstoneSummary(packageDirectory, scratchArena, detailReport);
    }
    else{
        detailReport += "status=not_decoded\nresolver=unknown\ndetail=unknown crash platform\n";
    }

    LoggerCrashSymbolicateDetail::AppendOptionalTextFile(packageDirectory, Core::Crash::PackageNames::s_GpuCrashReportFileName, "gpu_crash", scratchArena, detailReport);
    LoggerCrashSymbolicateDetail::AppendRadeonGpuDetectiveSummary(packageDirectory, scratchArena, detailReport);
    LoggerCrashSymbolicateDetail::AppendAftermathGpuDumpSummary(packageDirectory, scratchArena, detailReport);
    LoggerCrashSymbolicateDetail::AppendOptionalTextFile(packageDirectory, Core::Crash::PackageNames::s_CpuContextFileName, "cpu_context", scratchArena, detailReport);
    LoggerCrashSymbolicateDetail::AppendOptionalTextFile(packageDirectory, Core::Crash::PackageNames::s_MetadataFileName, "metadata", scratchArena, detailReport);
    LoggerCrashSymbolicateDetail::AppendOptionalTextFile(packageDirectory, Core::Crash::PackageNames::s_BreadcrumbsFileName, "breadcrumbs", scratchArena, detailReport);
    LoggerCrashSymbolicateDetail::AppendOptionalTextFile(packageDirectory, Core::Crash::PackageNames::s_SymbolicationFileName, "client_symbolication_note", scratchArena, detailReport);
    LoggerCrashSymbolicateDetail::AppendOptionalTextFile(packageDirectory, Core::Crash::PackageNames::s_AndroidCollectionFileName, "android_collection", scratchArena, detailReport);
    return LoggerCrashSymbolicateDetail::CrashReportComposer::BuildOrderedCrashReport(summary, scratchArena, Move(detailReport));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

