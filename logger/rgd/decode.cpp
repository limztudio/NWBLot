// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "decode.h"

#include <global/sync.h>

#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

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

Futex& RgdConsoleMutex(){
    static Futex s_Mutex;
    return s_Mutex;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The vendored RGD parser writes progress diagnostics to the process console. Capture that chatter in a caller-owned sink instead of leaking it into the logger stream, and drop it with the guard.
class ScopedRgdConsoleCapture final{
public:
    ScopedRgdConsoleCapture()
        : m_lock(RgdConsoleMutex())
        , m_coutBuffer(std::cout.rdbuf(m_sink.rdbuf()))
        , m_cerrBuffer(std::cerr.rdbuf(m_sink.rdbuf()))
    {
    }

    ~ScopedRgdConsoleCapture()noexcept{
        std::cout.rdbuf(m_coutBuffer);
        std::cerr.rdbuf(m_cerrBuffer);
    }

    ScopedRgdConsoleCapture(const ScopedRgdConsoleCapture&) = delete;
    ScopedRgdConsoleCapture& operator=(const ScopedRgdConsoleCapture&) = delete;

private:
    std::ostringstream m_sink;
    ScopedLock<Futex> m_lock;
    std::streambuf* m_coutBuffer;
    std::streambuf* m_cerrBuffer;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The upstream serializer APIs keep native C++ strings and streams at this integration boundary; decode failures retain any report sections already produced.
std::string AssembleTextSummary(const Config& config, RgdCrashDumpContents& contents){
    std::stringstream txt;

    {
        std::string systemInfo;
        if(!RgdSerializer::ToString(config, contents.system_info, contents.driver_experiments_json, systemInfo))
            txt << "ERROR: failed to serialize system information." << std::endl;
        txt << systemInfo << std::endl;
    }

    {
        ExecMarkerDataSerializer markerSerializer(std::unordered_map<uint64_t, RgdCrashingShaderInfo>{});

        std::string markerSummary;
        if(!markerSerializer.GenerateExecutionMarkerSummaryList(config, contents.umd_crash_data, contents.cmd_buffer_mapping, markerSummary))
            txt << "ERROR: failed to serialize the execution marker summary." << std::endl;

        txt << std::endl << std::endl;
        txt << "===================" << std::endl;
        txt << "MARKERS IN PROGRESS" << std::endl;
        txt << "===================" << std::endl << std::endl;
        txt << markerSummary;

        std::string markerTree;
        if(!markerSerializer.GenerateExecutionMarkerTree(config, contents.umd_crash_data, contents.cmd_buffer_mapping, markerTree))
            txt << "ERROR: failed to serialize the execution marker tree." << std::endl;

        txt << std::endl << std::endl;
        txt << "=====================" << std::endl;
        txt << "EXECUTION MARKER TREE" << std::endl;
        txt << "=====================" << std::endl << std::endl;
        txt << "Legend" << std::endl;
        txt << "======" << std::endl;
        txt << "[X] finished" << std::endl;
        txt << "[>] in progress" << std::endl;
        txt << "[ ] not started" << std::endl << std::endl;
        txt << markerTree;
    }

    {
        std::vector<size_t> pageFaultEvents;
        for(size_t i = 0, count = contents.kmd_crash_data.events.size(); i < count; ++i){
            const RgdEventOccurrence& currentEvent = contents.kmd_crash_data.events[i];
            if(currentEvent.rgd_event != nullptr && currentEvent.rgd_event->header.eventId == static_cast<uint8_t>(KmdEventId::RgdEventVmPageFault))
                pageFaultEvents.push_back(i);
        }

        if(!pageFaultEvents.empty()){
            txt << std::endl << std::endl;
            txt << "==================" << std::endl;
            txt << "PAGE FAULT SUMMARY" << std::endl;
            txt << "==================" << std::endl << std::endl;

            RgdResourceInfoSerializer resourceSerializer;
            const bool resourceHistoryReady = resourceSerializer.InitializeWithTraceFile(config.crash_dump_file);
            if(!resourceHistoryReady)
                txt << "ERROR: failed to initialize page-fault resource history." << std::endl;

            for(const size_t eventIndex : pageFaultEvents){
                const RgdEventOccurrence& pageFaultEvent = contents.kmd_crash_data.events[eventIndex];
                const VmPageFaultEvent& pageFault = static_cast<const VmPageFaultEvent&>(*pageFaultEvent.rgd_event);

                txt << "Offending VA: 0x" << std::hex << pageFault.faultVmAddress << std::dec << std::endl << std::endl;

                std::string resourceInfo;
                if(resourceHistoryReady && !resourceSerializer.GetVirtualAddressHistoryInfo(config, pageFault.faultVmAddress, resourceInfo))
                    txt << "ERROR: failed to serialize page-fault resource history." << std::endl;
                txt << resourceInfo << std::endl;
            }
        }
    }

    std::string inputInfo;
    RgdSerializer::InputInfoToString(config, contents, std::vector<std::string>{}, inputInfo);

    std::stringstream summary;
    summary << inputInfo;
    summary << txt.str();

    return summary.str();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// RDF's C API returns explicit file/open/format status instead of using its throwing C++ file wrappers for expected input failures.
[[nodiscard]] Expected<void, StringView> AdmitRdfInput(const std::string& filePath)noexcept{
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


Expected<std::string, StringView> DecodeCrashDumpToText(const std::string& filePath){
    const auto admitted = __hidden_rgd_decode::AdmitRdfInput(filePath);
    if(!admitted)
        return MakeUnexpected(admitted.error());

    Config config;
    config.crash_dump_file = filePath;
    RgdCrashDumpContents contents;
    {
        __hidden_rgd_decode::ScopedRgdConsoleCapture captureRgdConsole;

        if(!RgdParsingUtils::ParseCrashDump(config, contents))
            return MakeUnexpected(StringView("parse_failed"));
    }

    return __hidden_rgd_decode::AssembleTextSummary(config, contents);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

