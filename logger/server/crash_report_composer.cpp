// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_report_composer.h"
#include "crash_symbolicate_internal.h"

#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


namespace LoggerCrashSymbolicateDetail{


inline constexpr AStringView s_CrashReportSectionOpen = "\n[";


[[nodiscard]] Expected<CrashReportComposer::ReportSectionRange> CrashReportComposer::FindReportSection(const AStringView report, const AStringView header, const AStringView headerWithLeadingNewline)noexcept{
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

    const usize nextSection = report.find(s_CrashReportSectionOpen, outRange.contentBegin);
    outRange.contentEnd = nextSection == AStringView::npos ? report.size() : nextSection;
    outRange.removeEnd = outRange.contentEnd;
    return outRange;
}

[[nodiscard]] CrashReportComposer::ReportSectionRange CrashReportComposer::FindCallstackSection(const AStringView report)noexcept{
    const auto callstack = FindReportSection(report, AStringView("[callstack]\n"), s_CallstackSectionHeader);
    if(callstack)
        return *callstack;
    const auto tombstone = FindReportSection(report, AStringView("[tombstone_callstack]\n"), AStringView("\n[tombstone_callstack]\n"));
    return tombstone ? *tombstone : ReportSectionRange{};
}

[[nodiscard]] AStringView CrashReportComposer::ReportSectionContent(const AStringView report, const ReportSectionRange& range)noexcept{
    if(!range.found())
        return AStringView();

    return TrimView(AStringView(report.data() + range.contentBegin, range.contentEnd - range.contentBegin));
}

void CrashReportComposer::AppendReportWithoutSection(const AStringView report, const ReportSectionRange& range, AString<Core::Alloc::ScratchArena>& outReport){
    if(!range.found()){
        outReport.append(report.data(), report.size());
        return;
    }

    if(range.removeBegin > 0u)
        outReport.append(report.data(), range.removeBegin);
    if(range.removeEnd < report.size())
        outReport.append(report.data() + range.removeEnd, report.size() - range.removeEnd);
}

[[nodiscard]] b8 CrashReportComposer::AppendCrashHeadline(const CrashPackageSummary& summary, CrashReportText& outReport){
    b8 wrote = false;
    if(!summary.triggerExpression.empty()){
        outReport += summary.triggerExpression;
        outReport += s_ReportLineEnd;
        wrote = true;
    }
    if(!summary.triggerMessage.empty()){
        outReport += summary.triggerMessage;
        outReport += s_ReportLineEnd;
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
            Array<char, s_DecimalTextBufferCapacity> buffer{};
            outReport += FormatDecimal(static_cast<usize>(summary.triggerLine), buffer);
        }
        outReport += s_ReportLineEnd;
        wrote = true;
    }
    return wrote;
}

void CrashReportComposer::AppendVisibleCallstack(const AStringView callstack, CrashReportText& outReport){
    outReport += "callstack:\n";
    if(callstack.empty()){
        outReport += "<unavailable>\n";
        return;
    }

    outReport.append(callstack.data(), callstack.size());
    if(outReport.back() != '\n')
        outReport += s_ReportLineEnd;
}

[[nodiscard]] CrashReportText CrashReportComposer::BuildOrderedCrashReport(const CrashPackageSummary& summary, Core::Alloc::ScratchArena& scratchArena, CrashReportText&& detailReport){
    CrashReportText report(Move(detailReport));
    const AStringView detailReportView(report.data(), report.size());
    const ReportSectionRange callstackSection = FindCallstackSection(detailReportView);
    const AStringView callstack = ReportSectionContent(detailReportView, callstackSection);

    AString<Core::Alloc::ScratchArena> details(scratchArena);
    details.reserve(report.size());
    AppendReportWithoutSection(detailReportView, callstackSection, details);
    const AStringView detailsView = TrimView(AStringView(details.data(), details.size()));

    const AString<Core::Alloc::ScratchArena> callstackCopy(callstack.data(), callstack.size(), scratchArena);
    report.clear();
    report.reserve(AddSize(AddSize(detailsView.size(), callstackCopy.size()), s_CrashReportReserveBytes));
    if(AppendCrashHeadline(summary, report))
        report += s_ReportLineEnd;
    AppendVisibleCallstack(AStringView(callstackCopy.data(), callstackCopy.size()), report);
    report += "\ndetails:\n";
    if(!detailsView.empty()){
        report.append(detailsView.data(), detailsView.size());
        if(report.back() != '\n')
            report += s_ReportLineEnd;
    }
    return report;
}


};


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
