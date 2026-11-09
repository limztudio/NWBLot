// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_symbolicate_internal.h"

#include <logger/server/crash_values.adhd.h>

#include <core/crash/package_names.h>

#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace LoggerCrashSymbolicateDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CrashNames = ::NWB::Core::Crash::PackageNames;
inline constexpr usize s_AndroidTombstoneFrameMinimumTextLength = ::NWB::Log::CrashValues::s_CrashSymbolicateAndroidAndroidTombstoneFrameMinimumTextLength;
inline constexpr char s_TombstoneFrameMarker = '#';
inline constexpr AStringView s_TombstoneProgramCounterToken = ::NWB::Log::CrashValues::s_CrashSymbolicateAndroidTombstoneProgramCounterToken;
inline constexpr AStringView s_AndroidTombstoneMissingReport = ::NWB::Log::CrashValues::s_CrashSymbolicateAndroidAndroidTombstoneMissingReport;
inline constexpr AStringView s_AndroidTombstoneParsedStatus = ::NWB::Log::CrashValues::s_CrashSymbolicateAndroidAndroidTombstoneParsedStatus;
inline constexpr AStringView s_AndroidTombstoneUndecodedStatus = ::NWB::Log::CrashValues::s_CrashSymbolicateAndroidAndroidTombstoneUndecodedStatus;
inline constexpr AStringView s_AndroidTombstoneResolverLine = ::NWB::Log::CrashValues::s_CrashSymbolicateAndroidAndroidTombstoneResolverLine;
inline constexpr AStringView s_AndroidTombstonePresentLine = ::NWB::Log::CrashValues::s_CrashSymbolicateAndroidAndroidTombstonePresentLine;
inline constexpr AStringView s_AndroidTombstoneNoFramesDetail = ::NWB::Log::CrashValues::s_CrashSymbolicateAndroidAndroidTombstoneNoFramesDetail;
inline constexpr AStringView s_AndroidTombstoneFramesDetail = ::NWB::Log::CrashValues::s_CrashSymbolicateAndroidAndroidTombstoneFramesDetail;
inline constexpr AStringView s_TombstoneCallstackSectionHeader = ::NWB::Log::CrashValues::s_CrashSymbolicateAndroidTombstoneCallstackSectionHeader;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AppendAndroidTombstoneSummary(const CrashScratchPath& packageDirectory, Core::Alloc::ScratchArena& arena, CrashReportText& outReport){
    AString<Core::Alloc::ScratchArena> tombstone{arena};
    const b8 tombstonePresent = ReadTextFile(packageDirectory / CrashNames::s_AndroidTombstoneFileName, tombstone) && !tombstone.empty();
    if(!tombstonePresent){
        outReport += s_AndroidTombstoneMissingReport;
        return;
    }

    AString<Core::Alloc::ScratchArena> frames{arena};
    frames.reserve(AddSize(tombstone.size(), 1u));
    const AStringView tombstoneText(tombstone.data(), tombstone.size());
    usize cursor = 0u;
    while(const auto line = NextTextLine(tombstoneText, cursor)){
        const AStringView trimmed = TrimLeftView(*line);
        if(trimmed.size() < s_AndroidTombstoneFrameMinimumTextLength || trimmed.front() != s_TombstoneFrameMarker || trimmed.find(s_TombstoneProgramCounterToken) == AStringView::npos)
            continue;

        frames.append(trimmed.data(), trimmed.size());
        frames += '\n';
    }

    outReport += frames.empty() ? s_AndroidTombstoneUndecodedStatus : s_AndroidTombstoneParsedStatus;
    outReport += s_AndroidTombstoneResolverLine;
    outReport += s_AndroidTombstonePresentLine;
    outReport += frames.empty() ? s_AndroidTombstoneNoFramesDetail : s_AndroidTombstoneFramesDetail;

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

