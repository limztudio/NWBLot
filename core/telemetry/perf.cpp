// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "perf.h"

#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_perf{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ByteView{
    using value_type = u8;

    const u8* bytes = nullptr;
    usize byteCount = 0u;

    [[nodiscard]] usize size()const{ return byteCount; }
    [[nodiscard]] const u8* data()const{ return bytes; }
    [[nodiscard]] u8 operator[](const usize index)const{ return bytes[index]; }
};

[[nodiscard]] static bool ValidateHeader(const EncodedPerfTimingPayloadHeader& header)noexcept{
    return header.magic == s_PerfTimingPayloadMagic
        && header.version == s_PerfTimingPayloadVersion
        && header.reserved == 0u
        && IsValidPerfTimingSource(static_cast<PerfTimingSource::Enum>(header.source))
        && header.sampleCount != 0u
        && !NameDetail::IsZeroHash(header.scopeHash)
    ;
}

[[nodiscard]] static bool ValidateTimingInput(
    const PerfTimingSource::Enum source,
    const Name& scopeName,
    const AStringView scopeText,
    const Perf::TimingStats& stats
)noexcept{
    return IsValidPerfTimingSource(source)
        && static_cast<bool>(scopeName)
        && !scopeText.empty()
        && scopeText.size() <= Limit<u32>::s_Max
        && stats.valid()
    ;
}

template<typename Container>
static void AppendScopeText(Container& outPayload, const AStringView text){
    if(!text.empty())
        BinaryDetail::AppendBytesNoReserveUnchecked(outPayload, text.data(), text.size());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool IsValidPerfTimingSource(const PerfTimingSource::Enum source)noexcept{
    switch(source){
    case PerfTimingSource::Cpu:
    case PerfTimingSource::Gpu:
        return true;
    default:
        return false;
    }
}

bool BuildPerfTimingPayload(
    TelemetryArena&,
    const PerfTimingSource::Enum source,
    const Name& scopeName,
    const AStringView scopeText,
    const Perf::TimingStats& stats,
    TelemetryBytes& outPayload
){
    outPayload.clear();

    if(!__hidden_telemetry_perf::ValidateTimingInput(source, scopeName, scopeText, stats))
        return false;

    usize payloadBytes = sizeof(EncodedPerfTimingPayloadHeader);
    if(!AddBinaryReserveBytes(payloadBytes, scopeText.size()))
        return false;

    EncodedPerfTimingPayloadHeader header;
    header.source = source;
    header.scopeHash = scopeName.hash();
    header.seconds = stats.seconds;
    header.publishFrameIndex = stats.publishFrameIndex;
    header.firstSampleFrameIndex = stats.firstSampleFrameIndex;
    header.lastSampleFrameIndex = stats.lastSampleFrameIndex;
    header.sampleCount = stats.sampleCount;
    header.scopeNameBytes = static_cast<u32>(scopeText.size());

    outPayload.reserve(payloadBytes);
    AppendPOD(outPayload, header);
    __hidden_telemetry_perf::AppendScopeText(outPayload, scopeText);
    return outPayload.size() == payloadBytes;
}

bool BuildPerfTimingPayload(
    TelemetryArena& arena,
    const PerfTimingSource::Enum source,
    const Name& scopeName,
    const Perf::TimingStats& stats,
    TelemetryBytes& outPayload
){
    return BuildPerfTimingPayload(arena, source, scopeName, AStringView(scopeName.c_str()), stats, outPayload);
}

bool ParsePerfTimingPayload(
    TelemetryArena& arena,
    const void* const payload,
    const usize payloadBytes,
    PerfTimingPayload& outPayload
){
    outPayload = PerfTimingPayload(arena);

    if(payloadBytes < sizeof(EncodedPerfTimingPayloadHeader) || !payload)
        return false;

    const __hidden_telemetry_perf::ByteView encoded{ static_cast<const u8*>(payload), payloadBytes };
    usize cursor = 0u;

    EncodedPerfTimingPayloadHeader header;
    if(!ReadPOD(encoded, cursor, header))
        return false;
    if(!__hidden_telemetry_perf::ValidateHeader(header))
        return false;
    if(!BinaryDetail::CanReadBytes(encoded, cursor, header.scopeNameBytes))
        return false;
    if(payloadBytes - cursor != header.scopeNameBytes)
        return false;

    outPayload.source = static_cast<PerfTimingSource::Enum>(header.source);
    outPayload.scopeName = Name(header.scopeHash);
    outPayload.scopeText.assign(reinterpret_cast<const char*>(encoded.data() + cursor), header.scopeNameBytes);
    outPayload.stats.seconds = header.seconds;
    outPayload.stats.sampleCount = header.sampleCount;
    outPayload.stats.publishFrameIndex = header.publishFrameIndex;
    outPayload.stats.firstSampleFrameIndex = header.firstSampleFrameIndex;
    outPayload.stats.lastSampleFrameIndex = header.lastSampleFrameIndex;
    return true;
}

bool RecordPerfTiming(
    Recorder& recorder,
    const PerfTimingSource::Enum source,
    const Name& scopeName,
    const Perf::TimingStats& stats,
    const u32 streamId
){
    if(!recorder.enabled(EventKind::PerfFrame))
        return false;

    TelemetryBytes payload(recorder.arena());
    if(!BuildPerfTimingPayload(recorder.arena(), source, scopeName, stats, payload))
        return false;

    return recorder.record(EventKind::PerfFrame, PayloadFormat::Binary, stats.publishFrameIndex, payload.data(), payload.size(), streamId);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
