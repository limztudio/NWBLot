// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/common/application_entry.h>
#include <core/common/module.h>
#include <core/telemetry/module.h>

#include <global/filesystem/operations.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_dump{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using Arena = NWB::Core::Alloc::GlobalArena;
using Path = ::Path<Arena>;
namespace Telemetry = NWB::Core::Telemetry;

struct Options{
    explicit Options(Arena& arena)
        : inputPath(arena)
        , jsonPath(arena)
        , perfCsvPath(arena)
    {}

    Path inputPath;
    Path jsonPath;
    Path perfCsvPath;
    bool quiet = false;
    bool help = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void PrintUsage(){
    NWB_COUT
        << "Usage: nwb_telemetry_dump <archive.nwbs> [--json <path>] [--perf-csv <path>] [--quiet]\n"
        << "\n"
        << "Exports a telemetry archive summary for benchmark monitoring.\n";
}

[[nodiscard]] const char* ArchiveStatusText(const Telemetry::ArchiveStatus::Enum status)noexcept{
    switch(status){
    case Telemetry::ArchiveStatus::Ok:
        return "ok";
    case Telemetry::ArchiveStatus::Disabled:
        return "disabled";
    case Telemetry::ArchiveStatus::NotConfigured:
        return "not_configured";
    case Telemetry::ArchiveStatus::EncodeFailed:
        return "encode_failed";
    case Telemetry::ArchiveStatus::WriteFailed:
        return "write_failed";
    case Telemetry::ArchiveStatus::ReadFailed:
        return "read_failed";
    case Telemetry::ArchiveStatus::DecodeFailed:
        return "decode_failed";
    default:
        return "unknown";
    }
}

[[nodiscard]] bool ParseOptions(const int argc, char** argv, Options& outOptions){
    for(int i = 1; i < argc; ++i){
        const AStringView arg(argv[i] ? argv[i] : "");
        if(arg == "--help" || arg == "-h"){
            outOptions.help = true;
            return true;
        }
        if(arg == "--quiet"){
            outOptions.quiet = true;
            continue;
        }
        if(arg == "--json"){
            if(i + 1 >= argc){
                NWB_CERR << "Missing path after --json.\n";
                return false;
            }
            outOptions.jsonPath = argv[++i];
            continue;
        }
        if(arg == "--perf-csv"){
            if(i + 1 >= argc){
                NWB_CERR << "Missing path after --perf-csv.\n";
                return false;
            }
            outOptions.perfCsvPath = argv[++i];
            continue;
        }
        if(!outOptions.inputPath.empty()){
            NWB_CERR << "Unexpected argument: " << arg << "\n";
            return false;
        }
        outOptions.inputPath = arg;
    }

    if(outOptions.inputPath.empty()){
        NWB_CERR << "Telemetry archive path is required.\n";
        return false;
    }
    return true;
}

void PrintSummary(const Telemetry::ArchiveReportSummary& summary){
    NWB_COUT << "telemetry archive\n";
    NWB_COUT << "  events: " << summary.eventCount << "\n";
    if(summary.hasFrameRange)
        NWB_COUT << "  frames: " << summary.minFrameIndex << ".." << summary.maxFrameIndex << "\n";
    else
        NWB_COUT << "  frames: none\n";
    NWB_COUT << "  text logs: " << summary.eventKindCounts[static_cast<usize>(Telemetry::EventKind::TextLog)] << "\n";
    NWB_COUT << "  diagnostics: " << summary.eventKindCounts[static_cast<usize>(Telemetry::EventKind::Diagnostic)] << "\n";
    NWB_COUT
        << "  perf timing: cpu=" << summary.cpuTimingEventCount
        << " gpu=" << summary.gpuTimingEventCount
        << " memory=" << summary.memoryEventCount
        << "\n";
    NWB_COUT
        << "  frame graph: frames=" << summary.frameGraphFrameCount
        << " max_nodes=" << summary.maxFrameGraphNodeCount
        << " max_edges=" << summary.maxFrameGraphEdgeCount
        << "\n";
    if(summary.parseFailureCount != 0u)
        NWB_COUT << "  parse failures: " << summary.parseFailureCount << "\n";
}

[[nodiscard]] bool WriteTextOutput(const Path& path, const AString<Telemetry::TelemetryArena>& text, const char* label){
    if(path.empty())
        return true;
    if(WriteTextFile(path, AStringView(text.data(), text.size())))
        return true;

    NWB_CERR << "Failed to write " << label << ": " << path.generic_string() << "\n";
    return false;
}

int Run(const int argc, char** argv){
    Arena arena("NWB::Utilities::TelemetryDump");
    Options options(arena);
    if(!ParseOptions(argc, argv, options)){
        PrintUsage();
        return -1;
    }
    if(options.help){
        PrintUsage();
        return 0;
    }

    Telemetry::Recorder recorder(arena);
    const Telemetry::ArchiveResult readResult = Telemetry::ReadEventStreamArchive(arena, options.inputPath, recorder);
    if(!readResult.ok()){
        NWB_CERR << "Failed to read telemetry archive (" << ArchiveStatusText(readResult.status) << "): " << options.inputPath.generic_string() << "\n";
        return -1;
    }

    Telemetry::ArchiveReport report(arena);
    if(!Telemetry::BuildArchiveReport(arena, recorder.view(), report)){
        NWB_CERR << "Failed to build telemetry report.\n";
        return -1;
    }

    if(!WriteTextOutput(options.jsonPath, report.json, "JSON summary"))
        return -1;
    if(!WriteTextOutput(options.perfCsvPath, report.perfCsv, "perf CSV"))
        return -1;

    if(!options.quiet)
        PrintSummary(report.summary);
    return 0;
}

int EntryPoint(const isize argc, char** argv, void*){
    return Run(static_cast<int>(argc), argv);
}

#if defined(NWB_PLATFORM_WINDOWS) && defined(NWB_UNICODE)
int EntryPoint(const isize argc, wchar** argv, void*){
    return NWB::Core::Common::ApplicationEntryDetail::InvokeWithUtf8Args(argc, argv, Run);
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_APPLICATION_ENTRY_POINT(__hidden_telemetry_dump::EntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
