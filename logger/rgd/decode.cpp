// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "decode.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>

#include <global/algorithm.h>
#include <global/containers.h>
#include <global/sync.h>

#include "rgd_data_types.h"
#include "rgd_parsing_utils.h"
#include "rgd_serializer.h"
#include "rgd_marker_data_serializer.h"
#include "rgd_resource_info_serializer.h"
#include "dev_driver/include/rgdevents.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_rgd_decode_constants{
inline constexpr AStringView s_RgdSystemInfoErrorLine = "ERROR: failed to serialize system information.\n";
inline constexpr AStringView s_RgdMarkerSummaryErrorLine = "ERROR: failed to serialize the execution marker summary.\n";
inline constexpr AStringView s_RgdMarkerTreeErrorLine = "ERROR: failed to serialize the execution marker tree.\n";
inline constexpr AStringView s_RgdResourceHistoryInitErrorLine = "ERROR: failed to initialize page-fault resource history.\n";
inline constexpr AStringView s_RgdResourceHistoryErrorLine = "ERROR: failed to serialize page-fault resource history.\n";
inline constexpr AStringView s_RgdMarkersInProgressTitle = "MARKERS IN PROGRESS";
inline constexpr AStringView s_RgdExecutionMarkerTreeTitle = "EXECUTION MARKER TREE";
inline constexpr AStringView s_RgdLegendTitle = "Legend";
inline constexpr AStringView s_RgdLegendFinishedLine = "[X] finished";
inline constexpr AStringView s_RgdLegendInProgressLine = "[>] in progress";
inline constexpr AStringView s_RgdLegendNotStartedLine = "[ ] not started";
inline constexpr AStringView s_RgdPageFaultSummaryTitle = "PAGE FAULT SUMMARY";
inline constexpr AStringView s_RgdOffendingVaPrefix = "Offending VA: 0x";
[[nodiscard]] b8 IsAdmissibleRgdInputPath(const AStringView filePath)noexcept{ return !filePath.empty() && filePath.find('\0') == AStringView::npos; }
};
using __hidden_rgd_decode_constants::s_RgdSystemInfoErrorLine;
using __hidden_rgd_decode_constants::s_RgdMarkerSummaryErrorLine;
using __hidden_rgd_decode_constants::s_RgdMarkerTreeErrorLine;
using __hidden_rgd_decode_constants::s_RgdResourceHistoryInitErrorLine;
using __hidden_rgd_decode_constants::s_RgdResourceHistoryErrorLine;
using __hidden_rgd_decode_constants::s_RgdMarkersInProgressTitle;
using __hidden_rgd_decode_constants::s_RgdExecutionMarkerTreeTitle;
using __hidden_rgd_decode_constants::s_RgdLegendTitle;
using __hidden_rgd_decode_constants::s_RgdLegendFinishedLine;
using __hidden_rgd_decode_constants::s_RgdLegendInProgressLine;
using __hidden_rgd_decode_constants::s_RgdLegendNotStartedLine;
using __hidden_rgd_decode_constants::s_RgdPageFaultSummaryTitle;
using __hidden_rgd_decode_constants::s_RgdOffendingVaPrefix;
using __hidden_rgd_decode_constants::IsAdmissibleRgdInputPath;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Rgd{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_rgd_decode{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Futex& RgdDecodeMutex()noexcept{
    static Futex s_Mutex;
    return s_Mutex;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AInteropString AssembleTextSummary(const Config& config, Core::Alloc::ScratchArena& scratchArena, RgdCrashDumpContents& contents){
    AStringStream<Core::Alloc::ScratchArena> txt(s_FileOpenWrite, scratchArena);

    {
        AInteropString systemInfo;
        if(!RgdSerializer::ToString(config, contents.system_info, contents.driver_experiments_json, systemInfo))
            txt << s_RgdSystemInfoErrorLine;
        txt << systemInfo << '\n';
    }

    {
        ExecMarkerDataSerializer markerSerializer({});

        AInteropString markerSummary;
        if(!markerSerializer.GenerateExecutionMarkerSummaryList(config, contents.umd_crash_data, contents.cmd_buffer_mapping, markerSummary))
            txt << s_RgdMarkerSummaryErrorLine;

        txt << '\n' << '\n';
        txt << s_RgdDecodeValueMarkersInProgressDivider << '\n';
        txt << s_RgdMarkersInProgressTitle << '\n';
        txt << s_RgdDecodeValueMarkersInProgressDivider << '\n' << '\n';
        txt << markerSummary;

        AInteropString markerTree;
        if(!markerSerializer.GenerateExecutionMarkerTree(config, contents.umd_crash_data, contents.cmd_buffer_mapping, markerTree))
            txt << s_RgdMarkerTreeErrorLine;

        txt << '\n' << '\n';
        txt << s_RgdDecodeValueExecutionMarkerTreeDivider << '\n';
        txt << s_RgdExecutionMarkerTreeTitle << '\n';
        txt << s_RgdDecodeValueExecutionMarkerTreeDivider << '\n' << '\n';
        txt << s_RgdLegendTitle << '\n';
        txt << s_RgdDecodeValueLegendDivider << '\n';
        txt << s_RgdLegendFinishedLine << '\n';
        txt << s_RgdLegendInProgressLine << '\n';
        txt << s_RgdLegendNotStartedLine << '\n' << '\n';
        txt << markerTree;
    }

    {
        const auto isPageFault = [](const RgdEventOccurrence& event)noexcept{
            return event.rgd_event != nullptr && event.rgd_event->header.eventId == static_cast<u8>(KmdEventId::RgdEventVmPageFault);
        };
        const auto eventsEnd = contents.kmd_crash_data.events.end();
        const auto firstPageFault = FindIf(contents.kmd_crash_data.events.begin(), eventsEnd, isPageFault);
        if(firstPageFault != eventsEnd){
            txt << '\n' << '\n';
            txt << s_RgdDecodeValuePageFaultSummaryDivider << '\n';
            txt << s_RgdPageFaultSummaryTitle << '\n';
            txt << s_RgdDecodeValuePageFaultSummaryDivider << '\n' << '\n';

            RgdResourceInfoSerializer resourceSerializer;
            const b8 resourceHistoryReady = resourceSerializer.InitializeWithTraceFile(config.crash_dump_file);
            if(!resourceHistoryReady)
                txt << s_RgdResourceHistoryInitErrorLine;

            for(auto eventIt = firstPageFault; eventIt != eventsEnd; ++eventIt){
                const RgdEventOccurrence& pageFaultEvent = *eventIt;
                if(!isPageFault(pageFaultEvent))
                    continue;
                const VmPageFaultEvent& pageFault = static_cast<const VmPageFaultEvent&>(*pageFaultEvent.rgd_event);

                txt << s_RgdOffendingVaPrefix << StreamHex << pageFault.faultVmAddress << StreamDec << '\n' << '\n';

                AInteropString resourceInfo;
                if(resourceHistoryReady && !resourceSerializer.GetVirtualAddressHistoryInfo(config, pageFault.faultVmAddress, resourceInfo))
                    txt << s_RgdResourceHistoryErrorLine;
                txt << resourceInfo << '\n';
            }
        }
    }

    AInteropString inputInfo;
    RgdSerializer::InputInfoToString(config, contents, InteropVector<AInteropString>{}, inputInfo);

    const AStringView summary = txt.view();
    inputInfo.append(summary.data(), summary.size());
    return inputInfo;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<void, StringView> AdmitRdfInput(const char* pathText){
    rdfStream* stream = nullptr;
    if(rdfStreamOpenFile(pathText, &stream) != rdfResultOk)
        return MakeUnexpected(s_RgdDecodeValueRdfOpenFailedError);

    rdfChunkFile* chunks = nullptr;
    const int formatStatus = rdfChunkFileOpenStream(stream, &chunks);
    const int chunksCloseStatus = chunks ? rdfChunkFileClose(&chunks) : rdfResultOk;
    const int streamCloseStatus = rdfStreamClose(&stream);
    if(formatStatus != rdfResultOk)
        return MakeUnexpected(s_RgdDecodeValueParseFailedError);
    if(chunksCloseStatus != rdfResultOk || streamCloseStatus != rdfResultOk)
        return MakeUnexpected(s_RgdDecodeValueRdfCloseFailedError);
    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<AInteropString, StringView> DecodeCrashDumpToText(const StringView filePath){
    ScopedLock decodeLock(__hidden_rgd_decode::RgdDecodeMutex());
    Core::Alloc::ScratchArena scratchArena(s_RgdArenaNameLogRgdDecodeScratch);

    if(!IsAdmissibleRgdInputPath(filePath))
        return MakeUnexpected(s_RgdDecodeValueRdfOpenFailedError);
    Config config;
    config.crash_dump_file.assign(filePath.data(), filePath.size());
    const auto admitted = __hidden_rgd_decode::AdmitRdfInput(config.crash_dump_file.c_str());
    if(!admitted)
        return MakeUnexpected(admitted.error());
    RgdCrashDumpContents contents;
    if(!RgdParsingUtils::ParseCrashDump(config, contents))
        return MakeUnexpected(s_RgdDecodeValueParseFailedError);

    return __hidden_rgd_decode::AssembleTextSummary(config, scratchArena, contents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

