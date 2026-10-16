// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "decode.h"

#include <core/alloc/scratch.h>

#include <global/basic_string.h>
#include <global/containers.h>
#include <global/sync.h>

#include <iostream>
#include <sstream>

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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_DecodeScratchArena("logger/rgd/decode");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Futex& RgdConsoleMutex(){
    static Futex s_Mutex;
    return s_Mutex;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The vendored RGD parser writes progress diagnostics to the process console. Capture that chatter in a caller-owned sink instead of leaking it into the logger stream, and drop it with the guard.
class ScopedRgdConsoleCapture final{
public:
    explicit ScopedRgdConsoleCapture(Core::Alloc::ScratchArena& scratchArena)
        : m_sink(s_FileOpenWrite, scratchArena)
        , m_lock(RgdConsoleMutex())
        , m_coutBuffer(NWB_COUT.rdbuf(m_sink.rdbuf()))
        , m_cerrBuffer(NWB_CERR.rdbuf(m_sink.rdbuf()))
    {
    }
    ~ScopedRgdConsoleCapture()noexcept{
        NWB_COUT.rdbuf(m_coutBuffer);
        NWB_CERR.rdbuf(m_cerrBuffer);
    }


public:
    ScopedRgdConsoleCapture(const ScopedRgdConsoleCapture&) = delete;
    ScopedRgdConsoleCapture& operator=(const ScopedRgdConsoleCapture&) = delete;


private:
    AStringStream<Core::Alloc::ScratchArena> m_sink;
    ScopedLock<Futex> m_lock;
    std::streambuf* m_coutBuffer;
    std::streambuf* m_cerrBuffer;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The upstream serializer APIs keep native C++ strings and streams at this integration boundary; decode failures retain any report sections already produced.
AInteropString AssembleTextSummary(Core::Alloc::ScratchArena& scratchArena, const Config& config, RgdCrashDumpContents& contents){
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
        Vector<usize, Core::Alloc::ScratchArena> pageFaultEvents(scratchArena);
        pageFaultEvents.reserve(contents.kmd_crash_data.events.size());
        for(usize i = 0, count = contents.kmd_crash_data.events.size(); i < count; ++i){
            const RgdEventOccurrence& currentEvent = contents.kmd_crash_data.events[i];
            if(currentEvent.rgd_event != nullptr && currentEvent.rgd_event->header.eventId == static_cast<u8>(KmdEventId::RgdEventVmPageFault))
                pageFaultEvents.push_back(i);
        }

        if(!pageFaultEvents.empty()){
            txt << '\n' << '\n';
            txt << "==================" << '\n';
            txt << "PAGE FAULT SUMMARY" << '\n';
            txt << "==================" << '\n' << '\n';

            RgdResourceInfoSerializer resourceSerializer;
            const bool resourceHistoryReady = resourceSerializer.InitializeWithTraceFile(config.crash_dump_file);
            if(!resourceHistoryReady)
                txt << "ERROR: failed to initialize page-fault resource history." << '\n';

            for(const usize eventIndex : pageFaultEvents){
                const RgdEventOccurrence& pageFaultEvent = contents.kmd_crash_data.events[eventIndex];
                const VmPageFaultEvent& pageFault = static_cast<const VmPageFaultEvent&>(*pageFaultEvent.rgd_event);

                txt << "Offending VA: 0x" << std::hex << pageFault.faultVmAddress << std::dec << '\n' << '\n';

                AInteropString resourceInfo;
                if(resourceHistoryReady && !resourceSerializer.GetVirtualAddressHistoryInfo(config, pageFault.faultVmAddress, resourceInfo))
                    txt << "ERROR: failed to serialize page-fault resource history." << '\n';
                txt << resourceInfo << '\n';
            }
        }
    }

    AInteropString inputInfo;
    RgdSerializer::InputInfoToString(config, contents, {}, inputInfo);

    AStringStream<Core::Alloc::ScratchArena> summary(s_FileOpenWrite, scratchArena);
    summary << inputInfo;
    summary << txt.view();

    const auto report = summary.view();
    return AInteropString(report.data(), report.size());
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

    Core::Alloc::ScratchArena scratchArena(__hidden_rgd_decode::s_DecodeScratchArena);
    Config config;
    config.crash_dump_file = filePath;
    RgdCrashDumpContents contents;
    {
        __hidden_rgd_decode::ScopedRgdConsoleCapture captureRgdConsole(scratchArena);

        if(!RgdParsingUtils::ParseCrashDump(config, contents))
            return MakeUnexpected(StringView("parse_failed"));
    }

    return __hidden_rgd_decode::AssembleTextSummary(scratchArena, config, contents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

