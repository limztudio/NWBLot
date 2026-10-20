// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_symbolicate_internal.h"

#include <core/crash/package_names.h>

#if defined(NWB_WITH_AFTERMATH)
#include <core/common/aftermath_runtime.h>

#include <global/diagnostics.h>
#include <global/limit.h>
#include <global/scope_exit.h>
#include <global/shared_library.h>

#include <GFSDK_Aftermath_GpuCrashDump.h>
#include <GFSDK_Aftermath_GpuCrashDumpDecoding.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace LoggerCrashSymbolicateDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CrashNames = ::NWB::Core::Crash::PackageNames;

inline constexpr AStringView s_AftermathSectionHeader = "\n[aftermath]\n";
inline constexpr AStringView s_AftermathSkippedReport = "status=skipped\ndetail=logserver built without the Aftermath SDK\n";
inline constexpr AStringView s_AftermathCreateDecoderFailedReport = "status=decode_failed\ndetail=create_decoder\n";
inline constexpr AStringView s_AftermathGenerateJsonFailedReport = "status=decode_failed\ndetail=generate_json\n";
inline constexpr AStringView s_AftermathGetJsonFailedReport = "status=decode_failed\ndetail=get_json\n";
inline constexpr AStringView s_AftermathLoadRuntimeOperation = "load_runtime";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Expected SDK failures retain the ingest report; unexpected exceptions remain terminal.
#if defined(NWB_WITH_AFTERMATH)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_AftermathMaxJsonBytes = 8u * 1024u * 1024u; // decoded JSON (shader + warp state) can be large


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AftermathLoadFailure{
    AStringView operation;
    SharedLibraryFailure native;
};

struct AftermathDecoder{
    SharedLibrary library;
    PFN_GFSDK_Aftermath_GpuCrashDump_CreateDecoder createDecoder = nullptr;
    PFN_GFSDK_Aftermath_GpuCrashDump_GenerateJSON generateJson = nullptr;
    PFN_GFSDK_Aftermath_GpuCrashDump_GetJSON getJson = nullptr;
    PFN_GFSDK_Aftermath_GpuCrashDump_DestroyDecoder destroyDecoder = nullptr;


public:
    [[nodiscard]] Expected<void, AftermathLoadFailure> load(Core::Alloc::ScratchArena& scratchArena){
        const auto opened = library.open(scratchArena, Core::Common::s_AftermathRuntimeName);
        if(!opened)
            return MakeUnexpected(AftermathLoadFailure{ .operation = s_AftermathLoadRuntimeOperation, .native = opened.error(), });

        const auto create = library.resolve<PFN_GFSDK_Aftermath_GpuCrashDump_CreateDecoder>(scratchArena, "GFSDK_Aftermath_GpuCrashDump_CreateDecoder");
        const auto generate = library.resolve<PFN_GFSDK_Aftermath_GpuCrashDump_GenerateJSON>(scratchArena, "GFSDK_Aftermath_GpuCrashDump_GenerateJSON");
        const auto get = library.resolve<PFN_GFSDK_Aftermath_GpuCrashDump_GetJSON>(scratchArena, "GFSDK_Aftermath_GpuCrashDump_GetJSON");
        const auto destroy = library.resolve<PFN_GFSDK_Aftermath_GpuCrashDump_DestroyDecoder>(scratchArena, "GFSDK_Aftermath_GpuCrashDump_DestroyDecoder");
        if(!create || !generate || !get || !destroy){
            const SharedLibraryFailure& failure = !create ? create.error() : !generate ? generate.error() : !get ? get.error() : destroy.error();
            return MakeUnexpected(AftermathLoadFailure{ .operation = "resolve_symbols", .native = failure, });
        }
        createDecoder = *create;
        generateJson = *generate;
        getJson = *get;
        destroyDecoder = *destroy;
        return {};
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AppendAftermathGpuDumpSummary(Core::Alloc::ScratchArena& scratchArena, const CrashScratchPath& packageDirectory, const CrashSymbolicationConfig& config, CrashReportText& outReport){
    static_cast<void>(config);

    const CrashScratchPath dumpPath = packageDirectory / CrashNames::s_AftermathGpuDumpFileName;
    const auto regularDump = IsRegularFile(dumpPath);
    if(!regularDump){
        outReport += "\n[aftermath]\nstatus=inspect_failed\n";
        AppendNativeFailure(outReport, CrashSymbolicationFailure{ .operation = "inspect_aftermath_dump", .code = regularDump.error(), });
        return;
    }
    if(!*regularDump)
        return;

#if !defined(NWB_WITH_AFTERMATH)
    static_cast<void>(scratchArena);
    outReport += s_AftermathSectionHeader;
    outReport += s_AftermathSkippedReport;
#else
    Vector<u8, Core::Alloc::ScratchArena> dumpBytes(scratchArena);
    const auto read = ReadBinaryFile(dumpPath, dumpBytes);
    if(!read || dumpBytes.empty()){
        outReport += "\n[aftermath]\nstatus=read_failed\n";
        if(!read)
            AppendNativeFailure(outReport, CrashSymbolicationFailure{ .operation = "read_aftermath_dump", .code = read.error(), });
        return;
    }

    AftermathDecoder decoderLib;
    const auto loaded = decoderLib.load(scratchArena);
    if(!loaded){
        const AftermathLoadFailure& failure = loaded.error();
        outReport += failure.operation == s_AftermathLoadRuntimeOperation ? "\n[aftermath]\nstatus=skipped\n" : "\n[aftermath]\nstatus=decode_failed\n";
        outReport += "detail=Aftermath runtime admission failed\n";
        AppendNativeFailure(outReport, CrashSymbolicationFailure{ .operation = failure.operation, .code = failure.native.code, });
        outReport += "native_detail=";
        outReport += failure.native.detail.view();
        outReport += failure.native.detailTruncated ? "\nnative_detail_truncated=true\n" : "\nnative_detail_truncated=false\n";
        return;
    }

    if(dumpBytes.size() > static_cast<usize>(Limit<u32>::s_Max)){
        outReport += s_AftermathSectionHeader;
        outReport += s_AftermathCreateDecoderFailedReport;
        return;
    }

    Vector<char, Core::Alloc::ScratchArena> json(scratchArena);
    u32 jsonSize = 0u;
    {
        GFSDK_Aftermath_GpuCrashDump_Decoder decoder = nullptr;
        const GFSDK_Aftermath_Result createResult = decoderLib.createDecoder(GFSDK_Aftermath_Version_API, dumpBytes.data(), static_cast<u32>(dumpBytes.size()), &decoder);
        if(!GFSDK_Aftermath_SUCCEED(createResult) || !decoder){
            outReport += s_AftermathSectionHeader;
            outReport += s_AftermathCreateDecoderFailedReport;
            outReport += MakeDiagnosticEventText("native_category=GFSDK_Aftermath\nnative_code={}\ndecoder_present={}\n", createResult, decoder != nullptr).view();
            return;
        }

        ScopeExit cleanupDecoder([&]()noexcept{
            const GFSDK_Aftermath_Result destroyed = decoderLib.destroyDecoder(decoder);
            if(!GFSDK_Aftermath_SUCCEED(destroyed)){
                const DiagnosticEventText detail = MakeDiagnosticEventText("Failed to destroy Aftermath crash dump decoder: native_category=GFSDK_Aftermath native_code={}", destroyed);
                CaptureDiagnosticEvent("logger_server_aftermath", detail.view(), __FILE__, __LINE__);
            }
            decoder = nullptr;
        }
        );

        const GFSDK_Aftermath_Result generateResult = decoderLib.generateJson(
            decoder,
            static_cast<u32>(GFSDK_Aftermath_GpuCrashDumpDecoderFlags_ALL_INFO),
            static_cast<u32>(GFSDK_Aftermath_GpuCrashDumpFormatterFlags_UTF8_OUTPUT),
            nullptr,
            nullptr,
            nullptr,
            nullptr,
            &jsonSize
        );
        if(!GFSDK_Aftermath_SUCCEED(generateResult) || jsonSize == 0u){
            outReport += s_AftermathSectionHeader;
            outReport += s_AftermathGenerateJsonFailedReport;
            outReport += MakeDiagnosticEventText("native_category=GFSDK_Aftermath\nnative_code={}\njson_bytes={}\n", generateResult, jsonSize).view();
            return;
        }

        json.resize(jsonSize, '\0');
        const GFSDK_Aftermath_Result getResult = decoderLib.getJson(decoder, jsonSize, json.data());
        if(!GFSDK_Aftermath_SUCCEED(getResult)){
            outReport += s_AftermathSectionHeader;
            outReport += s_AftermathGetJsonFailedReport;
            outReport += MakeDiagnosticEventText("native_category=GFSDK_Aftermath\nnative_code={}\n", getResult).view();
            return;
        }
    }

    // jsonSize includes the terminator; omit it and cap shader/warp text to the report budget.
    usize jsonLength = jsonSize > 0u ? static_cast<usize>(jsonSize) - 1u : 0u;
    if(jsonLength > s_AftermathMaxJsonBytes)
        jsonLength = s_AftermathMaxJsonBytes;

    outReport += s_AftermathSectionHeader;
    outReport.append(json.data(), jsonLength);
    if(jsonLength == 0u || json[jsonLength - 1u] != '\n')
        outReport.push_back('\n');
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

