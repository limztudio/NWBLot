// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_symbolicate_internal.h"

#include <core/crash/package_names.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logger_crash_symbolicate{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CrashNames = ::NWB::Core::Crash::PackageNames;
inline constexpr usize s_AndroidTombstoneFrameMinimumTextLength = 4u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AppendAndroidTombstoneSummary(LogArena& arena, const Path& packageDirectory, CrashReportText& outReport){
    CrashReportText tombstone{arena};
    const bool tombstonePresent = ReadTextFile(packageDirectory / CrashNames::s_AndroidTombstoneFileName, tombstone) && !tombstone.empty();
    if(!tombstonePresent){
        outReport += "status=not_decoded\nresolver=android_tombstone_native_symbols\n";
        outReport += "android_tombstone=missing\ndetail=Android resolver requires Java/ApplicationExitInfo tombstone attachment and native symbol store\n";
        return;
    }

    CrashReportText frames{arena};
    usize cursor = 0u;
    while(cursor < tombstone.size()){
        const usize begin = cursor;
        while(cursor < tombstone.size() && tombstone[cursor] != '\n' && tombstone[cursor] != '\r')
            ++cursor;

        const AStringView line(tombstone.data() + begin, cursor - begin);
        while(cursor < tombstone.size() && (tombstone[cursor] == '\n' || tombstone[cursor] == '\r'))
            ++cursor;

        const AStringView trimmed = TrimLeftView(line);
        if(trimmed.size() < s_AndroidTombstoneFrameMinimumTextLength || trimmed.front() != '#' || trimmed.find(" pc ") == AStringView::npos)
            continue;

        frames.append(trimmed.data(), trimmed.size());
        frames += '\n';
    }

    outReport += frames.empty()
        ? "status=not_decoded\n"
        : "status=tombstone_parsed\n"
    ;
    outReport += "resolver=android_tombstone_native_symbols\n";
    outReport += "android_tombstone=present\n";
    outReport += frames.empty()
        ? "detail=tombstone attached, but no native frame lines were recognized; native symbols are required for full decoding\n"
        : "detail=tombstone native frame lines copied; native symbol store is required for offline address resolution\n"
    ;

    if(!frames.empty()){
        outReport += "\n[tombstone_callstack]\n";
        outReport += frames;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

