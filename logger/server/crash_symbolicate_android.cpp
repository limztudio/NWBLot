// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_symbolicate_internal.h"

#include <core/crash/package_names.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace LoggerCrashSymbolicateDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CrashNames = ::NWB::Core::Crash::PackageNames;
inline constexpr usize s_AndroidTombstoneFrameMinimumTextLength = 4u;
inline constexpr char s_TombstoneFrameMarker = '#';
inline constexpr AStringView s_TombstoneProgramCounterToken = " pc ";
inline constexpr AStringView s_AndroidTombstoneMissingReport =
    "status=not_decoded\nresolver=android_tombstone_native_symbols\n"
    "android_tombstone=missing\ndetail=Android resolver requires Java/ApplicationExitInfo tombstone attachment and native symbol store\n"
;
inline constexpr AStringView s_AndroidTombstoneParsedStatus = "status=tombstone_parsed\n";
inline constexpr AStringView s_AndroidTombstoneUndecodedStatus = "status=not_decoded\n";
inline constexpr AStringView s_AndroidTombstoneResolverLine = "resolver=android_tombstone_native_symbols\n";
inline constexpr AStringView s_AndroidTombstonePresentLine = "android_tombstone=present\n";
inline constexpr AStringView s_AndroidTombstoneNoFramesDetail =
    "detail=tombstone attached, but no native frame lines were recognized; native symbols are required for full decoding\n"
;
inline constexpr AStringView s_AndroidTombstoneFramesDetail =
    "detail=tombstone native frame lines copied; native symbol store is required for offline address resolution\n"
;
inline constexpr AStringView s_TombstoneCallstackSectionHeader = "\n[tombstone_callstack]\n";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AppendAndroidTombstoneSummary(LogArena& arena, const Path& packageDirectory, CrashReportText& outReport){
    CrashReportText tombstone{arena};
    const bool tombstonePresent = ReadTextFile(packageDirectory / CrashNames::s_AndroidTombstoneFileName, tombstone) && !tombstone.empty();
    if(!tombstonePresent){
        outReport += s_AndroidTombstoneMissingReport;
        return;
    }

    CrashReportText frames{arena};
    const AStringView tombstoneText(tombstone.data(), tombstone.size());
    usize cursor = 0u;
    while(const auto line = NextTextLine(tombstoneText, cursor)){
        const AStringView trimmed = TrimLeftView(*line);
        if(trimmed.size() < s_AndroidTombstoneFrameMinimumTextLength || trimmed.front() != s_TombstoneFrameMarker || trimmed.find(s_TombstoneProgramCounterToken) == AStringView::npos)
            continue;

        frames.append(trimmed.data(), trimmed.size());
        frames += '\n';
    }

    outReport += frames.empty()
        ? s_AndroidTombstoneUndecodedStatus
        : s_AndroidTombstoneParsedStatus
    ;
    outReport += s_AndroidTombstoneResolverLine;
    outReport += s_AndroidTombstonePresentLine;
    outReport += frames.empty()
        ? s_AndroidTombstoneNoFramesDetail
        : s_AndroidTombstoneFramesDetail
    ;

    if(!frames.empty()){
        outReport += s_TombstoneCallstackSectionHeader;
        outReport += frames;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

