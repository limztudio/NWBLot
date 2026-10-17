// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "decode.h"

#include <core/alloc/scratch.h>

#include <global/algorithm.h>
#include <global/basic_string.h>
#include <global/containers.h>
#include <global/numeric_text.h>
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Futex& RgdDecodeMutex()noexcept{
    static Futex s_Mutex;
    return s_Mutex;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The upstream serializer APIs require native strings; decode failures retain any report sections already produced while project streams use scratch storage.
AInteropString AssembleTextSummary(const Config& config, RgdCrashDumpContents& contents, Core::Alloc::ScratchArena& scratchArena){
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
        txt << "===================" << '\n';
        txt << "MARKERS IN PROGRESS" << '\n';
        txt << "===================" << '\n' << '\n';
        txt << markerSummary;

        AInteropString markerTree;
        if(!markerSerializer.GenerateExecutionMarkerTree(config, contents.umd_crash_data, contents.cmd_buffer_mapping, markerTree))
            txt << "ERROR: failed to serialize the execution marker tree." << '\n';

        txt << '\n' << '\n';
        txt << "=====================" << '\n';
        txt << "EXECUTION MARKER TREE" << '\n';
        txt << "=====================" << '\n' << '\n';
        txt << "Legend" << '\n';
        txt << "======" << '\n';
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
            txt << "==================" << '\n';
            txt << "PAGE FAULT SUMMARY" << '\n';
            txt << "==================" << '\n' << '\n';

            RgdResourceInfoSerializer resourceSerializer;
            const bool resourceHistoryReady = resourceSerializer.InitializeWithTraceFile(config.crash_dump_file);
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
[[nodiscard]] Expected<void, StringView> AdmitRdfInput(const AInteropString& filePath)noexcept{
    rdfStream* stream = nullptr;
    if(rdfStreamOpenFile(filePath.c_str(), &stream) != rdfResultOk)
        return MakeUnexpected(StringView("rdf_open_failed"));

    rdfChunkFile* chunks = nullptr;
    const int formatStatus = rdfChunkFileOpenStream(stream, &chunks);
    const int chunksCloseStatus = chunks ? rdfChunkFileClose(&chunks) : rdfResultOk;
    const int streamCloseStatus = rdfStreamClose(&stream);
    if(formatStatus != rdfResultOk)
        return MakeUnexpected(StringView("parse_failed"));
    if(chunksCloseStatus != rdfResultOk || streamCloseStatus != rdfResultOk)
        return MakeUnexpected(StringView("rdf_close_failed"));
    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<AInteropString, StringView> DecodeCrashDumpToText(const AInteropString& filePath){
    const auto admitted = __hidden_rgd_decode::AdmitRdfInput(filePath);
    if(!admitted)
        return MakeUnexpected(admitted.error());

    // The vendor trace loader owns process-global state; retain this lock through report assembly and local teardown.
    ScopedLock decodeLock(__hidden_rgd_decode::RgdDecodeMutex());
    Core::Alloc::ScratchArena scratchArena(__hidden_rgd_decode::s_RgdDecodeScratch);

    Config config;
    config.crash_dump_file = filePath;
    RgdCrashDumpContents contents;
    // Keep vendor console diagnostics on their existing streams; replacing shared buffers races with the server output worker.
    if(!RgdParsingUtils::ParseCrashDump(config, contents))
        return MakeUnexpected(StringView("parse_failed"));

    return __hidden_rgd_decode::AssembleTextSummary(config, contents, scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

