// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_symbolicate_internal.h"

#include <core/crash/package_names.h>

#if defined(NWB_WITH_AFTERMATH)
#include <GFSDK_Aftermath_GpuCrashDump.h>
#include <GFSDK_Aftermath_GpuCrashDumpDecoding.h>

#include <core/common/aftermath_runtime.h>

#include <global/shared_library.h>
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Decode available dumps through the optional Aftermath runtime; failure never aborts ingest.
#if defined(NWB_WITH_AFTERMATH)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_AftermathMaxJsonBytes = 8u * 1024u * 1024u; // decoded JSON (shader + warp state) can be large


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AftermathDecoder{
    SharedLibrary library;
    PFN_GFSDK_Aftermath_GpuCrashDump_CreateDecoder createDecoder = nullptr;
    PFN_GFSDK_Aftermath_GpuCrashDump_GenerateJSON generateJson = nullptr;
    PFN_GFSDK_Aftermath_GpuCrashDump_GetJSON getJson = nullptr;
    PFN_GFSDK_Aftermath_GpuCrashDump_DestroyDecoder destroyDecoder = nullptr;

    [[nodiscard]] bool load(LogArena& arena){
        if(!library.open(arena, Core::Common::s_AftermathRuntimeName))
            return false;

        const auto create = library.resolve<PFN_GFSDK_Aftermath_GpuCrashDump_CreateDecoder>(arena, "GFSDK_Aftermath_GpuCrashDump_CreateDecoder");
        const auto generate = library.resolve<PFN_GFSDK_Aftermath_GpuCrashDump_GenerateJSON>(arena, "GFSDK_Aftermath_GpuCrashDump_GenerateJSON");
        const auto get = library.resolve<PFN_GFSDK_Aftermath_GpuCrashDump_GetJSON>(arena, "GFSDK_Aftermath_GpuCrashDump_GetJSON");
        const auto destroy = library.resolve<PFN_GFSDK_Aftermath_GpuCrashDump_DestroyDecoder>(arena, "GFSDK_Aftermath_GpuCrashDump_DestroyDecoder");
        if(!create || !generate || !get || !destroy)
            return false;
        createDecoder = *create;
        generateJson = *generate;
        getJson = *get;
        destroyDecoder = *destroy;
        return true;
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
    outReport += s_AftermathSectionHeader;
    outReport += s_AftermathSkippedReport;
#else
    Vector<u8, LogArena> dumpBytes(arena);
    if(!ReadBinaryFile(dumpPath, dumpBytes) || dumpBytes.empty()){
        outReport += "\n[aftermath]\nstatus=read_failed\n";
        return;
    }

    AftermathDecoder decoderLib;
    if(!decoderLib.load(arena)){
        outReport += "\n[aftermath]\nstatus=skipped\ndetail=Aftermath runtime not found next to logserver\n";
        return;
    }

    GFSDK_Aftermath_GpuCrashDump_Decoder decoder = nullptr;
    if(
        !GFSDK_Aftermath_SUCCEED(decoderLib.createDecoder(GFSDK_Aftermath_Version_API, dumpBytes.data(), static_cast<uint32_t>(dumpBytes.size()), &decoder))
        || !decoder
    ){
        outReport += s_AftermathSectionHeader;
        outReport += s_AftermathCreateDecoderFailedReport;
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
        outReport += s_AftermathSectionHeader;
        outReport += s_AftermathGenerateJsonFailedReport;
        return;
    }

    Vector<char, LogArena> json(arena);
    json.resize(jsonSize, '\0');
    const GFSDK_Aftermath_Result getResult = decoderLib.getJson(decoder, jsonSize, json.data());
    decoderLib.destroyDecoder(decoder);
    if(!GFSDK_Aftermath_SUCCEED(getResult)){
        outReport += s_AftermathSectionHeader;
        outReport += s_AftermathGetJsonFailedReport;
        return;
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

