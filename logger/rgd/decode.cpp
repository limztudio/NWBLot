// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "decode.h"

#include <core/alloc/scratch.h>

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


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Rgd{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_rgd_decode{

inline constexpr Name s_RgdDecodeScratch("Log/RgdDecodeScratch");
inline constexpr StringView s_ParseFailedError = "parse_failed";
inline constexpr StringView s_RdfOpenFailedError = "rdf_open_failed";
inline constexpr StringView s_RdfCloseFailedError = "rdf_close_failed";
inline constexpr AStringView s_MarkersInProgressDivider = "===================";
inline constexpr AStringView s_ExecutionMarkerTreeDivider = "=====================";
inline constexpr AStringView s_LegendDivider = "======";
inline constexpr AStringView s_PageFaultSummaryDivider = "==================";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Futex& RgdDecodeMutex()noexcept{
    static Futex s_Mutex;
    return s_Mutex;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Serializer APIs need native strings; decode failures keep produced sections while project streams use scratch.
AInteropString AssembleTextSummary(const Config& config, Core::Alloc::ScratchArena& scratchArena, RgdCrashDumpContents& contents){
    AStringStream<Core::Alloc::ScratchArena> txt(s_FileOpenWrite, scratchArena);

    {
        AInteropString systemInfo;
        if(!RgdSerializer::ToString(config, contents.system_info, contents.driver_experiments_json, systemInfo))
            txt << "ERROR: failed to serialize system information." << '\n';
        txt << systemInfo << '\n';
    }

    {
        ExecMarkerDataSerializer markerSerializer({});

        AInteropString markerSummary;
        if(!markerSerializer.GenerateExecutionMarkerSummaryList(config, contents.umd_crash_data, contents.cmd_buffer_mapping, markerSummary))
            txt << "ERROR: failed to serialize the execution marker summary." << '\n';

        txt << '\n' << '\n';
        txt << s_MarkersInProgressDivider << '\n';
        txt << "MARKERS IN PROGRESS" << '\n';
        txt << s_MarkersInProgressDivider << '\n' << '\n';
        txt << markerSummary;

        AInteropString markerTree;
        if(!markerSerializer.GenerateExecutionMarkerTree(config, contents.umd_crash_data, contents.cmd_buffer_mapping, markerTree))
            txt << "ERROR: failed to serialize the execution marker tree." << '\n';

        txt << '\n' << '\n';
        txt << s_ExecutionMarkerTreeDivider << '\n';
        txt << "EXECUTION MARKER TREE" << '\n';
        txt << s_ExecutionMarkerTreeDivider << '\n' << '\n';
        txt << "Legend" << '\n';
        txt << s_LegendDivider << '\n';
        txt << "[X] finished" << '\n';
        txt << "[>] in progress" << '\n';
        txt << "[ ] not started" << '\n' << '\n';
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
            txt << s_PageFaultSummaryDivider << '\n';
            txt << "PAGE FAULT SUMMARY" << '\n';
            txt << s_PageFaultSummaryDivider << '\n' << '\n';

            RgdResourceInfoSerializer resourceSerializer;
            const b8 resourceHistoryReady = resourceSerializer.InitializeWithTraceFile(config.crash_dump_file);
            if(!resourceHistoryReady)
                txt << "ERROR: failed to initialize page-fault resource history." << '\n';

            for(auto eventIt = firstPageFault; eventIt != eventsEnd; ++eventIt){
                const RgdEventOccurrence& pageFaultEvent = *eventIt;
                if(!isPageFault(pageFaultEvent))
                    continue;
                const VmPageFaultEvent& pageFault = static_cast<const VmPageFaultEvent&>(*pageFaultEvent.rgd_event);

                txt << "Offending VA: 0x" << StreamHex << pageFault.faultVmAddress << StreamDec << '\n' << '\n';

                AInteropString resourceInfo;
                if(resourceHistoryReady && !resourceSerializer.GetVirtualAddressHistoryInfo(config, pageFault.faultVmAddress, resourceInfo))
                    txt << "ERROR: failed to serialize page-fault resource history." << '\n';
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


// RDF's C API returns explicit file/open/format status instead of using its throwing C++ file wrappers for expected input failures.
[[nodiscard]] Expected<void, StringView> AdmitRdfInput(Core::Alloc::ScratchArena& scratchArena, const StringView filePath){
    if(filePath.empty() || filePath.find('\0') != StringView::npos)
        return MakeUnexpected(s_RdfOpenFailedError);

    const AString<Core::Alloc::ScratchArena> pathText(filePath, scratchArena);
    rdfStream* stream = nullptr;
    if(rdfStreamOpenFile(pathText.c_str(), &stream) != rdfResultOk)
        return MakeUnexpected(s_RdfOpenFailedError);

    rdfChunkFile* chunks = nullptr;
    const int formatStatus = rdfChunkFileOpenStream(stream, &chunks);
    const int chunksCloseStatus = chunks ? rdfChunkFileClose(&chunks) : rdfResultOk;
    const int streamCloseStatus = rdfStreamClose(&stream);
    if(formatStatus != rdfResultOk)
        return MakeUnexpected(s_ParseFailedError);
    if(chunksCloseStatus != rdfResultOk || streamCloseStatus != rdfResultOk)
        return MakeUnexpected(s_RdfCloseFailedError);
    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<AInteropString, StringView> DecodeCrashDumpToText(const StringView filePath){
    // The vendor trace loader owns process-global state; retain this lock through report assembly and local teardown.
    ScopedLock decodeLock(__hidden_rgd_decode::RgdDecodeMutex());
    Core::Alloc::ScratchArena scratchArena(__hidden_rgd_decode::s_RgdDecodeScratch);

    const auto admitted = __hidden_rgd_decode::AdmitRdfInput(scratchArena, filePath);
    if(!admitted)
        return MakeUnexpected(admitted.error());

    Config config;
    config.crash_dump_file.assign(filePath.data(), filePath.size());
    RgdCrashDumpContents contents;
    // Keep vendor console diagnostics on their existing streams; replacing shared buffers races with the server output worker.
    if(!RgdParsingUtils::ParseCrashDump(config, contents))
        return MakeUnexpected(__hidden_rgd_decode::s_ParseFailedError);

    return __hidden_rgd_decode::AssembleTextSummary(config, scratchArena, contents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

