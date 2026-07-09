// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_symbolicate_internal.h"

#include <global/core/crash/package_names.h>

#if defined(NWB_WITH_AFTERMATH)
#include <GFSDK_Aftermath_GpuCrashDump.h>
#include <GFSDK_Aftermath_GpuCrashDumpDecoding.h>

#include <global/core/common/aftermath_runtime.h>

#include <global/shared_library.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_logger_crash_symbolicate{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CrashNames = ::NWB::Core::Crash::PackageNames;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// NVIDIA Nsight Aftermath ships only as a runtime shared library exposing a C decoder API; there is no
// rgd-style CLI tool. The engine emits a '.nv-gpudmp' into the package on device-lost, so this decode is
// best-effort: it runs only when a dump is present in the package and the runtime + entry points resolve,
// and it never fails the surrounding ingest (ProcessCrashUpload rejects on a thrown exception). The runtime
// is loaded dynamically so the logserver still runs (and decodes everything else) on hosts without the
// NVIDIA runtime present next to it.
#if defined(NWB_WITH_AFTERMATH)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_AftermathMaxJsonBytes = 8u * 1024u * 1024u; // decoded JSON (shader + warp state) can be large


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Resolves the Aftermath decoder entry points from the dynamically loaded runtime. The logserver never links
// the import lib, so a missing runtime is not a hard dependency: load() simply fails and the dump is left
// undecoded. The library is freed when this object goes out of scope.
struct AftermathDecoder{
    SharedLibrary library;
    PFN_GFSDK_Aftermath_GpuCrashDump_CreateDecoder createDecoder = nullptr;
    PFN_GFSDK_Aftermath_GpuCrashDump_GenerateJSON generateJson = nullptr;
    PFN_GFSDK_Aftermath_GpuCrashDump_GetJSON getJson = nullptr;
    PFN_GFSDK_Aftermath_GpuCrashDump_DestroyDecoder destroyDecoder = nullptr;

    [[nodiscard]] bool load(){
        if(!library.open(Core::Common::s_AftermathRuntimeName))
            return false;

        return library.resolve("GFSDK_Aftermath_GpuCrashDump_CreateDecoder", createDecoder)
            && library.resolve("GFSDK_Aftermath_GpuCrashDump_GenerateJSON", generateJson)
            && library.resolve("GFSDK_Aftermath_GpuCrashDump_GetJSON", getJson)
            && library.resolve("GFSDK_Aftermath_GpuCrashDump_DestroyDecoder", destroyDecoder)
        ;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void AppendAftermathGpuDumpSummary(LogArena& arena, const Path& packageDirectory, const CrashSymbolicationConfig& config, CrashReportText& outReport){
    static_cast<void>(config);

    const Path dumpPath = packageDirectory / CrashNames::s_AftermathGpuDumpFileName;
    if(!PathIsRegularFile(dumpPath))
        return; // no Aftermath crash dump in this package: nothing to decode (silent skip).

#if !defined(NWB_WITH_AFTERMATH)
    static_cast<void>(arena);
    outReport += "\n[aftermath]\nstatus=skipped\ndetail=logserver built without the Aftermath SDK\n";
#else
    Vector<u8, LogArena> dumpBytes(arena);
    ErrorCode readError;
    if(!ReadBinaryFile(dumpPath, dumpBytes, readError) || dumpBytes.empty()){
        outReport += "\n[aftermath]\nstatus=read_failed\n";
        return;
    }

    AftermathDecoder decoderLib;
    if(!decoderLib.load()){
        outReport += "\n[aftermath]\nstatus=skipped\ndetail=Aftermath runtime not found next to logserver\n";
        return;
    }

    GFSDK_Aftermath_GpuCrashDump_Decoder decoder = nullptr;
    if(
        !GFSDK_Aftermath_SUCCEED(decoderLib.createDecoder(GFSDK_Aftermath_Version_API, dumpBytes.data(), static_cast<uint32_t>(dumpBytes.size()), &decoder))
        || !decoder
    ){
        outReport += "\n[aftermath]\nstatus=decode_failed\ndetail=create_decoder\n";
        return;
    }

    uint32_t jsonSize = 0u;
    const GFSDK_Aftermath_Result generateResult = decoderLib.generateJson(
        decoder,
        static_cast<uint32_t>(GFSDK_Aftermath_GpuCrashDumpDecoderFlags_ALL_INFO),
        static_cast<uint32_t>(GFSDK_Aftermath_GpuCrashDumpFormatterFlags_UTF8_OUTPUT),
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        &jsonSize
    );
    if(!GFSDK_Aftermath_SUCCEED(generateResult) || jsonSize == 0u){
        decoderLib.destroyDecoder(decoder);
        outReport += "\n[aftermath]\nstatus=decode_failed\ndetail=generate_json\n";
        return;
    }

    Vector<char, LogArena> json(arena);
    json.resize(jsonSize, '\0');
    const GFSDK_Aftermath_Result getResult = decoderLib.getJson(decoder, jsonSize, json.data());
    decoderLib.destroyDecoder(decoder);
    if(!GFSDK_Aftermath_SUCCEED(getResult)){
        outReport += "\n[aftermath]\nstatus=decode_failed\ndetail=get_json\n";
        return;
    }

    // GetJSON writes a null-terminated string occupying 'jsonSize' bytes (text + terminator); take the text
    // only, bounded so a large shader/warp dump cannot blow the report budget.
    usize jsonLength = jsonSize > 0u ? static_cast<usize>(jsonSize) - 1u : 0u;
    if(jsonLength > s_AftermathMaxJsonBytes)
        jsonLength = s_AftermathMaxJsonBytes;

    outReport += "\n[aftermath]\n";
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

