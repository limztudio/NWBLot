// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <logger/server/crash_report_composer.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_crash_report_composer_tests{


using namespace NWB;


TEST(CrashReportComposerTests, TombstoneStackMovesBeforeDetailsWithoutBorrowingClearedText){
    Tests::TestArena<> arena;
    Core::Alloc::ScratchArena scratch(Name("tests/crash_report_composer/scratch"));
    Log::CrashPackageSummary summary(arena.arena);
    summary.triggerMessage = "fault";
    Log::CrashReportText details(arena.arena);
    details += "package=one\n[tombstone_callstack]\nframe-one\n[metadata]\nkey=value\n";

    const auto report = Log::LoggerCrashSymbolicateDetail::CrashReportComposer::BuildOrderedCrashReport(summary, scratch, Move(details));
    EXPECT_EQ(AStringView(report.data(), report.size()), AStringView("fault\n\ncallstack:\nframe-one\n\ndetails:\npackage=one\n[metadata]\nkey=value\n"));
}

TEST(CrashReportComposerTests, EmptyPrimaryStackTakesPrecedenceOverTombstone){
    Tests::TestArena<> arena;
    Core::Alloc::ScratchArena scratch(Name("tests/crash_report_composer/scratch"));
    Log::CrashPackageSummary summary(arena.arena);
    Log::CrashReportText details(arena.arena);
    details += "[callstack]\n\n[tombstone_callstack]\nfallback\n";

    const auto report = Log::LoggerCrashSymbolicateDetail::CrashReportComposer::BuildOrderedCrashReport(summary, scratch, Move(details));
    EXPECT_EQ(AStringView(report.data(), report.size()), AStringView("callstack:\n<unavailable>\n\ndetails:\n[tombstone_callstack]\nfallback\n"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
