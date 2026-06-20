//=============================================================================
// NWB-authored shim (NOT vendored AMD source). See nwb_rgd_decode.h.
//=============================================================================
#include "nwb_rgd_decode.h"

#include <cstdint>
#include <exception>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

// RGD backend.
#include "rgd_data_types.h"
#include "rgd_parsing_utils.h"
#include "rgd_serializer.h"
#include "rgd_marker_data_serializer.h"
#include "rgd_resource_info_serializer.h"

// Dev driver: KmdEventId / VmPageFaultEvent.
#include "dev_driver/include/rgdevents.h"

namespace nwb_rgd{

namespace{

// Mirrors the RGD CLI frontend's SerializeTextOutput, summary-only: input info, system info, the
// "markers in progress" list, the execution-marker tree (+ legend), and — when the crash is a page fault —
// the offending-VA resource history. The shader/ISA disassembly sections are intentionally omitted (the
// disassembler path is not compiled into the vendored backend).
void AssembleTextSummary(const Config& config, RgdCrashDumpContents& contents, std::string& out_text){
    std::stringstream txt;

    // System info: CPU / GPU / driver / OS, plus any active driver experiments.
    {
        std::string system_info_str;
        RgdSerializer::ToString(config, contents.system_info, contents.driver_experiments_json, system_info_str);
        txt << system_info_str << std::endl;
    }

    // Execution markers: the "in progress" summary first, then the full annotated tree with its legend.
    // Both are built from the UMD crash data and the command-buffer mapping (no in-flight shader info, so
    // the '[#] shader in flight' state never appears and is left out of the legend).
    {
        ExecMarkerDataSerializer exec_marker_serializer(std::unordered_map<uint64_t, RgdCrashingShaderInfo>{});

        std::string exec_marker_summary;
        exec_marker_serializer.GenerateExecutionMarkerSummaryList(config, contents.umd_crash_data, contents.cmd_buffer_mapping, exec_marker_summary);

        txt << std::endl << std::endl;
        txt << "===================" << std::endl;
        txt << "MARKERS IN PROGRESS" << std::endl;
        txt << "===================" << std::endl << std::endl;
        txt << exec_marker_summary;

        std::string exec_marker_tree;
        exec_marker_serializer.GenerateExecutionMarkerTree(config, contents.umd_crash_data, contents.cmd_buffer_mapping, exec_marker_tree);

        txt << std::endl << std::endl;
        txt << "=====================" << std::endl;
        txt << "EXECUTION MARKER TREE" << std::endl;
        txt << "=====================" << std::endl << std::endl;
        txt << "Legend" << std::endl;
        txt << "======" << std::endl;
        txt << "[X] finished" << std::endl;
        txt << "[>] in progress" << std::endl;
        txt << "[ ] not started" << std::endl << std::endl;
        txt << exec_marker_tree;
    }

    // Page fault summary: scan the KMD events for VmPageFault events; for each, report the offending VA and
    // its resource history. A hang (no page-fault event) skips this section entirely.
    {
        std::vector<size_t> page_fault_events;
        for(size_t i = 0, count = contents.kmd_crash_data.events.size(); i < count; ++i){
            const RgdEventOccurrence& current_event = contents.kmd_crash_data.events[i];
            if(current_event.rgd_event != nullptr
               && current_event.rgd_event->header.eventId == static_cast<uint8_t>(KmdEventId::RgdEventVmPageFault))
                page_fault_events.push_back(i);
        }

        if(!page_fault_events.empty()){
            txt << std::endl << std::endl;
            txt << "==================" << std::endl;
            txt << "PAGE FAULT SUMMARY" << std::endl;
            txt << "==================" << std::endl << std::endl;

            RgdResourceInfoSerializer resource_serializer;
            resource_serializer.InitializeWithTraceFile(config.crash_dump_file);

            for(const size_t event_index : page_fault_events){
                const RgdEventOccurrence& page_fault_event = contents.kmd_crash_data.events[event_index];
                const VmPageFaultEvent& vm_page_fault = static_cast<const VmPageFaultEvent&>(*page_fault_event.rgd_event);

                txt << "Offending VA: 0x" << std::hex << vm_page_fault.faultVmAddress << std::dec << std::endl << std::endl;

                std::string resource_info_string;
                resource_serializer.GetVirtualAddressHistoryInfo(config, vm_page_fault.faultVmAddress, resource_info_string);
                txt << resource_info_string << std::endl;
            }
        }
    }

    // Input info (input file, RGD version, API type) is prepended ahead of the analysis body, matching the
    // frontend's final layout.
    std::string input_info_str;
    RgdSerializer::InputInfoToString(config, contents, std::vector<std::string>{}, input_info_str);

    std::stringstream rgd_summary_txt;
    rgd_summary_txt << input_info_str;
    rgd_summary_txt << txt.str();

    out_text = rgd_summary_txt.str();
}

}

bool DecodeCrashDumpToText(const std::string& rgd_file_path, std::string& out_text){
    out_text.clear();
    try{
        Config config;
        config.crash_dump_file = rgd_file_path;
        // output_file_txt / output_file_json stay empty: the report is assembled in memory. Every feature
        // flag keeps its false default, so no shader/ISA disassembly code path is exercised.

        RgdCrashDumpContents contents;
        if(!RgdParsingUtils::ParseCrashDump(config, contents)){
            out_text = "parse_failed";
            return false;
        }

        AssembleTextSummary(config, contents, out_text);
        return true;
    }
    catch(const std::exception& e){
        out_text = std::string("exception: ") + e.what();
        return false;
    }
    catch(...){
        out_text = "exception: unknown";
        return false;
    }
}

}
