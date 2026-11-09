// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "crash_symbolicate.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


namespace LoggerCrashSymbolicateDetail{


class CrashReportComposer final{
private:
    struct ReportSectionRange{
        usize removeBegin = AStringView::npos;
        usize removeEnd = AStringView::npos;
        usize contentBegin = AStringView::npos;
        usize contentEnd = AStringView::npos;


    public:
        [[nodiscard]] b8 found()const noexcept{ return removeBegin != AStringView::npos; }
    };


public:
    [[nodiscard]] static CrashReportText BuildOrderedCrashReport(const CrashPackageSummary& summary, Core::Alloc::ScratchArena& scratchArena, CrashReportText&& detailReport);


private:
    [[nodiscard]] static Expected<ReportSectionRange> FindReportSection(AStringView report, AStringView header, AStringView headerWithLeadingNewline)noexcept;
    [[nodiscard]] static ReportSectionRange FindCallstackSection(AStringView report)noexcept;
    [[nodiscard]] static AStringView ReportSectionContent(AStringView report, const ReportSectionRange& range)noexcept;
    static void AppendReportWithoutSection(AStringView report, const ReportSectionRange& range, AString<Core::Alloc::ScratchArena>& outReport);
    [[nodiscard]] static b8 AppendCrashHeadline(const CrashPackageSummary& summary, CrashReportText& outReport);
    static void AppendVisibleCallstack(AStringView callstack, CrashReportText& outReport);
};


};


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

