// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_symbolicate_internal.h"
#include "crash_paths.h"

#include <core/crash/package_names.h>
#include <core/crash/reason_names.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logger_crash_symbolicate{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void AppendOptionalTextFile(LogArena& arena, CrashReportText& outReport, const Path& packageDirectory, const char* fileName, const char* label){
    CrashReportText text{arena};
    if(!ReadTextFile(packageDirectory / fileName, text) || text.empty())
        return;

    outReport += "\n[";
    outReport += label;
    outReport += "]\n";
    outReport += text;
    if(outReport.back() != '\n')
        outReport += '\n';
}

void AppendHexAddress(LogArena& arena, CrashReportText& outReport, const u64 address){
    outReport += "0x";
    outReport += FormatHex64A(arena, address);
}

Path EffectiveSymbolStoreDirectory(LogArena& arena, const CrashSymbolicationConfig& config){
    if(!config.symbolStoreDirectory.empty())
        return Path(arena, config.symbolStoreDirectory);

    return CrashDefaultRootDirectory(arena) / s_CrashSymbolStoreDirectoryName;
}

static void AppendSymbolStoreStatus(LogArena& arena, CrashReportText& outReport, const CrashSymbolicationConfig& config){
    const Path symbolStoreDirectory = EffectiveSymbolStoreDirectory(arena, config);
    outReport += "symbol_store=";
    outReport += PathToString<char>(arena, symbolStoreDirectory);
    outReport += "\nsymbol_store_status=";

    ErrorCode error;
    const bool exists = IsDirectory(symbolStoreDirectory, error);
    if(error)
        outReport += "error";
    else
        outReport += exists ? "present" : "missing";
    outReport += "\n";
}

static void AppendExceptionSummary(LogArena& arena, CrashReportText& outReport, const CrashPackageSummary& summary){
    if(summary.reasonKind == "signal"){
        outReport += "exception=";
        outReport += Core::Crash::PosixSignalName(summary.reasonCode);
        outReport += " (";
        char buffer[s_DecimalTextBufferCapacity] = {};
        outReport += FormatDecimal(static_cast<usize>(summary.reasonCode), buffer);
        outReport += ")\n";
        return;
    }
    if(summary.reasonKind == "windows_exception"){
        outReport += "exception=";
        outReport += Core::Crash::WindowsExceptionName(summary.reasonCode);
        outReport += " ";
        AppendHexAddress(arena, outReport, summary.reasonCode);
        outReport += "\n";
        return;
    }
    if(summary.reasonKind == "terminate"){
        outReport += "exception=std::terminate\n";
        return;
    }
    if(summary.reasonKind == "manual_dump"){
        outReport += "exception=manual diagnostic dump\n";
        return;
    }
    if(summary.reasonKind == "gpu_crash"){
        outReport += "exception=GPU device removed / crash\n";
        return;
    }

    outReport += "exception=";
    outReport += summary.reasonKind;
    outReport += "\n";
}

static void AppendEventSummary(LogArena& arena, const CrashPackageSummary& summary, CrashReportText& outReport){
    outReport += "\n[event]\n";
    outReport += "event=";
    outReport += summary.event;
    outReport += "\n";

    if(
        summary.triggerExpression.empty()
        && summary.triggerMessage.empty()
        && summary.triggerFile.empty()
        && summary.triggerLine == 0u
    )
        AppendExceptionSummary(arena, outReport, summary);
}

struct ReportSectionRange{
    usize removeBegin = AStringView::npos;
    usize removeEnd = AStringView::npos;
    usize contentBegin = AStringView::npos;
    usize contentEnd = AStringView::npos;

    [[nodiscard]] bool found()const{ return removeBegin != AStringView::npos; }
};

[[nodiscard]] static bool FindReportSection(
    const AStringView report,
    const AStringView header,
    const AStringView headerWithLeadingNewline,
    ReportSectionRange& outRange
){
    outRange = ReportSectionRange{};

    if(StartsWith(report, header)){
        outRange.removeBegin = 0u;
        outRange.contentBegin = header.size();
    }
    else{
        const usize headerBegin = report.find(headerWithLeadingNewline);
        if(headerBegin == AStringView::npos)
            return false;

        outRange.removeBegin = headerBegin;
        outRange.contentBegin = headerBegin + headerWithLeadingNewline.size();
    }

    const usize nextSection = report.find(AStringView("\n["), outRange.contentBegin);
    outRange.contentEnd = nextSection == AStringView::npos ? report.size() : nextSection;
    outRange.removeEnd = outRange.contentEnd;
    return true;
}

[[nodiscard]] static ReportSectionRange FindCallstackSection(const AStringView report){
    ReportSectionRange range;
    if(FindReportSection(report, AStringView("[callstack]\n"), AStringView("\n[callstack]\n"), range))
        return range;
    if(FindReportSection(report, AStringView("[tombstone_callstack]\n"), AStringView("\n[tombstone_callstack]\n"), range))
        return range;

    return ReportSectionRange{};
}

[[nodiscard]] static AStringView ReportSectionContent(const AStringView report, const ReportSectionRange& range){
    if(!range.found())
        return AStringView();

    return TrimView(AStringView(report.data() + range.contentBegin, range.contentEnd - range.contentBegin));
}

static void AppendReportWithoutSection(CrashReportText& outReport, const AStringView report, const ReportSectionRange& range){
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
            char buffer[s_DecimalTextBufferCapacity] = {};
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

[[nodiscard]] static CrashReportText BuildOrderedCrashReport(LogArena& arena, const CrashPackageSummary& summary, const CrashReportText& detailReport){
    const AStringView detailReportView(detailReport.data(), detailReport.size());
    const ReportSectionRange callstackSection = FindCallstackSection(detailReportView);
    const AStringView callstack = ReportSectionContent(detailReportView, callstackSection);

    CrashReportText details(arena);
    details.reserve(detailReport.size());
    AppendReportWithoutSection(details, detailReportView, callstackSection);
    const AStringView detailsView = TrimView(AStringView(details.data(), details.size()));

    CrashReportText report(arena);
    report.reserve(detailsView.size() + callstack.size() + s_CrashReportReserveBytes);
    if(AppendCrashHeadline(report, summary))
        report += "\n";
    AppendVisibleCallstack(report, callstack);
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


CrashReportText BuildCrashSymbolicationReport(LogArena& arena, const Path& packageDirectory, const CrashPackageSummary& summary, const CrashSymbolicationConfig& config){
    namespace Symbolicate = __hidden_logger_crash_symbolicate;

    CrashReportText detailReport{arena};
    detailReport.reserve(Symbolicate::s_CrashReportReserveBytes);

    detailReport += "package=";
    detailReport += PathToString<char>(arena, packageDirectory);
    detailReport += "\ncrash_id=";
    detailReport += summary.crashId;
    detailReport += "\nplatform=";
    detailReport += summary.platform;
    detailReport += "\nreason=";
    detailReport += summary.reasonKind;
    detailReport += "\nartifact_strategy=";
    detailReport += summary.artifactStrategy;
    detailReport += "\n";
    Symbolicate::AppendSymbolStoreStatus(arena, detailReport, config);
    Symbolicate::AppendEventSummary(arena, summary, detailReport);

    if(summary.platform == "windows"){
#if defined(NWB_PLATFORM_WINDOWS)
        if(!Symbolicate::AppendWindowsMinidumpStack(arena, packageDirectory, summary, config, detailReport))
            NWB_LOGGER_WARNING(NWB_TEXT("Windows minidump stack could not be fully decoded"));
#else
        detailReport += "status=not_decoded\nresolver=windows_pdb_minidump\ndetail=Windows minidump resolver is only available on Windows logserver builds\n";
#endif
    }
    else if(summary.platform == "linux"){
        Symbolicate::AppendLinuxArtifactSummary(arena, packageDirectory, config, detailReport);
    }
    else if(summary.platform == "android"){
        Symbolicate::AppendAndroidTombstoneSummary(arena, packageDirectory, detailReport);
    }
    else{
        detailReport += "status=not_decoded\nresolver=unknown\ndetail=unknown crash platform\n";
    }

    Symbolicate::AppendOptionalTextFile(arena, detailReport, packageDirectory, Core::Crash::PackageNames::s_GpuCrashReportFileName, "gpu_crash");
    Symbolicate::AppendRadeonGpuDetectiveSummary(arena, packageDirectory, config, detailReport);
    Symbolicate::AppendAftermathGpuDumpSummary(arena, packageDirectory, config, detailReport);
    Symbolicate::AppendOptionalTextFile(arena, detailReport, packageDirectory, Core::Crash::PackageNames::s_CpuContextFileName, "cpu_context");
    Symbolicate::AppendOptionalTextFile(arena, detailReport, packageDirectory, Core::Crash::PackageNames::s_MetadataFileName, "metadata");
    Symbolicate::AppendOptionalTextFile(arena, detailReport, packageDirectory, Core::Crash::PackageNames::s_BreadcrumbsFileName, "breadcrumbs");
    Symbolicate::AppendOptionalTextFile(arena, detailReport, packageDirectory, Core::Crash::PackageNames::s_SymbolicationFileName, "client_symbolication_note");
    Symbolicate::AppendOptionalTextFile(arena, detailReport, packageDirectory, Core::Crash::PackageNames::s_AndroidCollectionFileName, "android_collection");
    return Symbolicate::BuildOrderedCrashReport(arena, summary, detailReport);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

