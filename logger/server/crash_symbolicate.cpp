// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_symbolicate.h"
#include "crash_symbolicate_internal.h"
#include "crash_paths.h"

#include <core/crash/package_names.h>
#include <core/crash/reason_names.h>

#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace LoggerCrashSymbolicateDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void AppendOptionalTextFile(Core::Alloc::ScratchArena& scratchArena, CrashReportText& outReport, const CrashScratchPath& packageDirectory, const AStringView fileName, const AStringView label){
    AString<Core::Alloc::ScratchArena> text{scratchArena};
    const auto read = ReadTextFile(packageDirectory / fileName, text);
    if(!read){
        if(read.error() != ErrorCondition::no_such_file_or_directory){
            outReport += "\n[";
            outReport += label;
            outReport += "]\nstatus=read_failed\n";
            AppendNativeFailure(outReport, CrashSymbolicationFailure{ .operation = fileName, .code = read.error(), });
        }
        return;
    }
    if(text.empty())
        return;

    outReport += "\n[";
    outReport += label;
    outReport += "]\n";
    outReport += text;
    if(outReport.back() != '\n')
        outReport += '\n';
}

void AppendNativeFailure(CrashReportText& outReport, const CrashSymbolicationFailure& failure){
    const DiagnosticEventText detail = MakeDiagnosticEventText("operation={}\nnative_category={}\nnative_code={}\n", failure.operation, failure.code.category().name(), failure.code.value());
    outReport += detail.view();
}

void AppendHexAddress(CrashReportText& outReport, const u64 address){
    outReport += s_HexAddressPrefix;
    AppendHexU64(address, outReport);
}

CrashScratchPath EffectiveSymbolStoreDirectory(Core::Alloc::ScratchArena& arena, const CrashSymbolicationConfig& config){
    if(!config.symbolStoreDirectory.empty())
        return CrashScratchPath(arena, config.symbolStoreDirectory);

    return CrashDefaultRootDirectory(arena) / s_CrashSymbolStoreDirectoryName;
}

static void AppendSymbolStoreStatus(Core::Alloc::ScratchArena& arena, CrashReportText& outReport, const CrashSymbolicationConfig& config){
    const CrashScratchPath symbolStoreDirectory = EffectiveSymbolStoreDirectory(arena, config);
    outReport += "symbol_store=";
    outReport += PathToString<char>(arena, symbolStoreDirectory);
    outReport += "\nsymbol_store_status=";

    const auto exists = IsDirectory(symbolStoreDirectory);
    if(!exists)
        outReport += s_SymbolStoreErrorText;
    else
        outReport += *exists ? s_SymbolStorePresentText : s_SymbolStoreMissingText;
    outReport += "\n";
}

static void AppendExceptionSummary(CrashReportText& outReport, const CrashPackageSummary& summary){
    if(summary.reasonKind == s_SignalReasonKind){
        outReport += "exception=";
        outReport += Core::Crash::PosixSignalName(summary.reasonCode);
        outReport += " (";
        char buffer[s_DecimalTextBufferCapacity];
        outReport += FormatDecimal(static_cast<usize>(summary.reasonCode), buffer);
        outReport += ")\n";
        return;
    }
    if(summary.reasonKind == s_WindowsExceptionReasonKind){
        outReport += "exception=";
        outReport += Core::Crash::WindowsExceptionName(summary.reasonCode);
        outReport += " ";
        AppendHexAddress(outReport, summary.reasonCode);
        outReport += "\n";
        return;
    }
    if(summary.reasonKind == s_TerminateReasonKind){
        outReport += "exception=std::terminate\n";
        return;
    }
    if(summary.reasonKind == s_ManualDumpReasonKind){
        outReport += "exception=manual diagnostic dump\n";
        return;
    }
    if(summary.reasonKind == s_GpuCrashReasonKind){
        outReport += "exception=GPU device removed / crash\n";
        return;
    }

    outReport += "exception=";
    outReport += summary.reasonKind;
    outReport += "\n";
}

static void AppendEventSummary(const CrashPackageSummary& summary, CrashReportText& outReport){
    outReport += "\n[event]\n";
    outReport += "event=";
    outReport += summary.event;
    outReport += "\n";

    if(summary.triggerExpression.empty() && summary.triggerMessage.empty() && summary.triggerFile.empty() && summary.triggerLine == 0u)
        AppendExceptionSummary(outReport, summary);
}

struct ReportSectionRange{
    usize removeBegin = AStringView::npos;
    usize removeEnd = AStringView::npos;
    usize contentBegin = AStringView::npos;
    usize contentEnd = AStringView::npos;


public:
    [[nodiscard]] bool found()const noexcept{ return removeBegin != AStringView::npos; }
};

[[nodiscard]] static Expected<ReportSectionRange> FindReportSection(const AStringView report, const AStringView header, const AStringView headerWithLeadingNewline)noexcept{
    ReportSectionRange outRange;

    if(StartsWith(report, header)){
        outRange.removeBegin = 0u;
        outRange.contentBegin = header.size();
    }
    else{
        const usize headerBegin = report.find(headerWithLeadingNewline);
        if(headerBegin == AStringView::npos)
            return MakeUnexpected(Failure{});

        outRange.removeBegin = headerBegin;
        outRange.contentBegin = headerBegin + headerWithLeadingNewline.size();
    }

    const usize nextSection = report.find(AStringView("\n["), outRange.contentBegin);
    outRange.contentEnd = nextSection == AStringView::npos ? report.size() : nextSection;
    outRange.removeEnd = outRange.contentEnd;
    return outRange;
}

[[nodiscard]] static ReportSectionRange FindCallstackSection(const AStringView report)noexcept{
    const auto callstack = FindReportSection(report, AStringView("[callstack]\n"), AStringView("\n[callstack]\n"));
    if(callstack)
        return *callstack;
    const auto tombstone = FindReportSection(report, AStringView("[tombstone_callstack]\n"), AStringView("\n[tombstone_callstack]\n"));
    return tombstone ? *tombstone : ReportSectionRange{};
}

[[nodiscard]] static AStringView ReportSectionContent(const AStringView report, const ReportSectionRange& range)noexcept{
    if(!range.found())
        return AStringView();

    return TrimView(AStringView(report.data() + range.contentBegin, range.contentEnd - range.contentBegin));
}

static void AppendReportWithoutSection(AString<Core::Alloc::ScratchArena>& outReport, const AStringView report, const ReportSectionRange& range){
    if(!range.found()){
        outReport.append(report.data(), report.size());
        return;
    }

    if(range.removeBegin > 0u)
        outReport.append(report.data(), range.removeBegin);
    if(range.removeEnd < report.size())
        outReport.append(report.data() + range.removeEnd, report.size() - range.removeEnd);
}

[[nodiscard]] static bool AppendCrashHeadline(CrashReportText& outReport, const CrashPackageSummary& summary){
    bool wrote = false;
    if(!summary.triggerExpression.empty()){
        outReport += summary.triggerExpression;
        outReport += "\n";
        wrote = true;
    }
    if(!summary.triggerMessage.empty()){
        outReport += summary.triggerMessage;
        outReport += "\n";
        wrote = true;
    }
    if(!summary.triggerFile.empty() || summary.triggerLine != 0u){
        outReport += "at ";
        if(!summary.triggerFile.empty())
            outReport += summary.triggerFile;
        else
            outReport += "line ";
        if(summary.triggerLine != 0u){
            if(!summary.triggerFile.empty())
                outReport += ":";
            char buffer[s_DecimalTextBufferCapacity];
            outReport += FormatDecimal(static_cast<usize>(summary.triggerLine), buffer);
        }
        outReport += "\n";
        wrote = true;
    }
    return wrote;
}

static void AppendVisibleCallstack(CrashReportText& outReport, const AStringView callstack){
    outReport += "callstack:\n";
    if(callstack.empty()){
        outReport += "<unavailable>\n";
        return;
    }

    outReport.append(callstack.data(), callstack.size());
    if(outReport.back() != '\n')
        outReport += "\n";
}

[[nodiscard]] static CrashReportText BuildOrderedCrashReport(Core::Alloc::ScratchArena& scratchArena, const CrashPackageSummary& summary, CrashReportText&& detailReport){
    CrashReportText report(Move(detailReport));
    const AStringView detailReportView(report.data(), report.size());
    const ReportSectionRange callstackSection = FindCallstackSection(detailReportView);
    const AStringView callstack = ReportSectionContent(detailReportView, callstackSection);

    AString<Core::Alloc::ScratchArena> details(scratchArena);
    details.reserve(report.size());
    AppendReportWithoutSection(details, detailReportView, callstackSection);
    const AStringView detailsView = TrimView(AStringView(details.data(), details.size()));

    const AString<Core::Alloc::ScratchArena> callstackCopy(callstack.data(), callstack.size(), scratchArena);
    report.clear();
    report.reserve(AddSize(AddSize(detailsView.size(), callstackCopy.size()), s_CrashReportReserveBytes));
    if(AppendCrashHeadline(report, summary))
        report += "\n";
    AppendVisibleCallstack(report, AStringView(callstackCopy.data(), callstackCopy.size()));
    report += "\ndetails:\n";
    if(!detailsView.empty()){
        report.append(detailsView.data(), detailsView.size());
        if(report.back() != '\n')
            report += "\n";
    }
    return report;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CrashReportText BuildCrashSymbolicationReport(LogArena& arena, const CrashScratchPath& packageDirectory, const CrashPackageSummary& summary, const CrashSymbolicationConfig& config, Core::Alloc::ScratchArena& scratchArena){
    namespace Symbolicate = LoggerCrashSymbolicateDetail;

    CrashReportText detailReport{arena};
    detailReport.reserve(Symbolicate::s_CrashReportReserveBytes);

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
    detailReport += "\n";
    Symbolicate::AppendSymbolStoreStatus(scratchArena, detailReport, config);
    Symbolicate::AppendEventSummary(summary, detailReport);

    if(summary.platform == LoggerCrashSymbolicateDetail::s_WindowsPlatformName){
#if defined(NWB_PLATFORM_WINDOWS)
        const auto decoded = Symbolicate::AppendWindowsMinidumpStack(scratchArena, packageDirectory, summary, config, detailReport);
        if(!decoded)
            Symbolicate::AppendNativeFailure(detailReport, decoded.error());
        else if(!*decoded)
            detailReport += "stack_decode=unavailable\n";
#else
        detailReport += "status=not_decoded\nresolver=windows_pdb_minidump\ndetail=Windows minidump resolver is only available on Windows logserver builds\n";
#endif
    }
    else if(summary.platform == LoggerCrashSymbolicateDetail::s_LinuxPlatformName){
        Symbolicate::AppendLinuxArtifactSummary(scratchArena, packageDirectory, config, detailReport);
    }
    else if(summary.platform == LoggerCrashSymbolicateDetail::s_AndroidPlatformName){
        Symbolicate::AppendAndroidTombstoneSummary(scratchArena, packageDirectory, detailReport);
    }
    else{
        detailReport += "status=not_decoded\nresolver=unknown\ndetail=unknown crash platform\n";
    }

    Symbolicate::AppendOptionalTextFile(scratchArena, detailReport, packageDirectory, Core::Crash::PackageNames::s_GpuCrashReportFileName, "gpu_crash");
    Symbolicate::AppendRadeonGpuDetectiveSummary(scratchArena, packageDirectory, config, detailReport);
    Symbolicate::AppendAftermathGpuDumpSummary(scratchArena, packageDirectory, config, detailReport);
    Symbolicate::AppendOptionalTextFile(scratchArena, detailReport, packageDirectory, Core::Crash::PackageNames::s_CpuContextFileName, "cpu_context");
    Symbolicate::AppendOptionalTextFile(scratchArena, detailReport, packageDirectory, Core::Crash::PackageNames::s_MetadataFileName, "metadata");
    Symbolicate::AppendOptionalTextFile(scratchArena, detailReport, packageDirectory, Core::Crash::PackageNames::s_BreadcrumbsFileName, "breadcrumbs");
    Symbolicate::AppendOptionalTextFile(scratchArena, detailReport, packageDirectory, Core::Crash::PackageNames::s_SymbolicationFileName, "client_symbolication_note");
    Symbolicate::AppendOptionalTextFile(scratchArena, detailReport, packageDirectory, Core::Crash::PackageNames::s_AndroidCollectionFileName, "android_collection");
    return Symbolicate::BuildOrderedCrashReport(scratchArena, summary, Move(detailReport));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

