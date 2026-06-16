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

[[nodiscard]] static bool ValidateHeader(const EncodedPerfMemoryPayloadHeader& header)noexcept{
    constexpr u16 s_KnownFlags = PerfMemoryPayloadFlag::HasDelta;
    return header.magic == s_PerfMemoryPayloadMagic
        && header.version == s_PerfMemoryPayloadVersion
        && (header.flags & ~s_KnownFlags) == 0u
        && header.reserved == 0u
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

[[nodiscard]] static bool ValidateMemoryInput(
    const Name& scopeName,
    const AStringView scopeText,
    const Perf::MemorySnapshot& snapshot,
    const Perf::MemoryDelta& delta
)noexcept{
    if(
        !scopeName
        || snapshot.scopeName != scopeName
        || !snapshot.valid()
        || scopeText.empty()
        || scopeText.size() > Limit<u32>::s_Max
    )
        return false;

    return !delta.hasSamples || delta.currentFrameIndex == snapshot.frameIndex;
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

bool BuildPerfMemoryPayload(
    TelemetryArena&,
    const Name& scopeName,
    const AStringView scopeText,
    const Perf::MemorySnapshot& snapshot,
    const Perf::MemoryDelta& delta,
    TelemetryBytes& outPayload
){
    outPayload.clear();

    if(!__hidden_telemetry_perf::ValidateMemoryInput(scopeName, scopeText, snapshot, delta))
        return false;

    usize payloadBytes = sizeof(EncodedPerfMemoryPayloadHeader);
    if(!AddBinaryReserveBytes(payloadBytes, scopeText.size()))
        return false;

    EncodedPerfMemoryPayloadHeader header;
    header.flags = delta.hasSamples ? PerfMemoryPayloadFlag::HasDelta : PerfMemoryPayloadFlag::None;
    header.scopeHash = scopeName.hash();
    header.frameIndex = snapshot.frameIndex;
    header.reservedBytes = snapshot.reservedBytes;
    header.usedBytes = snapshot.usedBytes;
    header.peakUsedBytes = snapshot.peakUsedBytes;
    header.allocationCount = snapshot.allocationCount;
    header.reallocationCount = snapshot.reallocationCount;
    header.deallocationCount = snapshot.deallocationCount;
    header.scopeNameBytes = static_cast<u32>(scopeText.size());

    if(delta.hasSamples){
        header.previousFrameIndex = delta.previousFrameIndex;
        header.deltaReservedBytes = delta.reservedBytes;
        header.deltaUsedBytes = delta.usedBytes;
        header.deltaPeakUsedBytes = delta.peakUsedBytes;
        header.deltaAllocationCount = delta.allocationCount;
        header.deltaReallocationCount = delta.reallocationCount;
        header.deltaDeallocationCount = delta.deallocationCount;
    }

    outPayload.reserve(payloadBytes);
    AppendPOD(outPayload, header);
    __hidden_telemetry_perf::AppendScopeText(outPayload, scopeText);
    return outPayload.size() == payloadBytes;
}

bool BuildPerfMemoryPayload(
    TelemetryArena& arena,
    const Name& scopeName,
    const Perf::MemorySnapshot& snapshot,
    const Perf::MemoryDelta& delta,
    TelemetryBytes& outPayload
){
    return BuildPerfMemoryPayload(arena, scopeName, AStringView(scopeName.c_str()), snapshot, delta, outPayload);
}

bool ParsePerfMemoryPayload(
    TelemetryArena& arena,
    const void* const payload,
    const usize payloadBytes,
    PerfMemoryPayload& outPayload
){
    outPayload = PerfMemoryPayload(arena);

    if(payloadBytes < sizeof(EncodedPerfMemoryPayloadHeader) || !payload)
        return false;

    const __hidden_telemetry_perf::ByteView encoded{ static_cast<const u8*>(payload), payloadBytes };
    usize cursor = 0u;

    EncodedPerfMemoryPayloadHeader header;
    if(!ReadPOD(encoded, cursor, header))
        return false;
    if(!__hidden_telemetry_perf::ValidateHeader(header))
        return false;
    if(!BinaryDetail::CanReadBytes(encoded, cursor, header.scopeNameBytes))
        return false;
    if(payloadBytes - cursor != header.scopeNameBytes)
        return false;

    outPayload.scopeName = Name(header.scopeHash);
    outPayload.scopeText.assign(reinterpret_cast<const char*>(encoded.data() + cursor), header.scopeNameBytes);
    outPayload.snapshot.scopeName = outPayload.scopeName;
    outPayload.snapshot.frameIndex = header.frameIndex;
    outPayload.snapshot.reservedBytes = header.reservedBytes;
    outPayload.snapshot.usedBytes = header.usedBytes;
    outPayload.snapshot.peakUsedBytes = header.peakUsedBytes;
    outPayload.snapshot.allocationCount = header.allocationCount;
    outPayload.snapshot.reallocationCount = header.reallocationCount;
    outPayload.snapshot.deallocationCount = header.deallocationCount;

    if((header.flags & PerfMemoryPayloadFlag::HasDelta) != 0u){
        outPayload.delta.previousFrameIndex = header.previousFrameIndex;
        outPayload.delta.currentFrameIndex = header.frameIndex;
        outPayload.delta.reservedBytes = header.deltaReservedBytes;
        outPayload.delta.usedBytes = header.deltaUsedBytes;
        outPayload.delta.peakUsedBytes = header.deltaPeakUsedBytes;
        outPayload.delta.allocationCount = header.deltaAllocationCount;
        outPayload.delta.reallocationCount = header.deltaReallocationCount;
        outPayload.delta.deallocationCount = header.deltaDeallocationCount;
        outPayload.delta.hasSamples = true;
    }

    return true;
}

bool RecordPerfMemory(
    Recorder& recorder,
    const Name& scopeName,
    const Perf::MemorySnapshot& snapshot,
    const Perf::MemoryDelta& delta,
    const u32 streamId
){
    if(!recorder.enabled(EventKind::MemoryFrame))
        return false;

    TelemetryBytes payload(recorder.arena());
    if(!BuildPerfMemoryPayload(recorder.arena(), scopeName, snapshot, delta, payload))
        return false;

    return recorder.record(EventKind::MemoryFrame, PayloadFormat::Binary, snapshot.frameIndex, payload.data(), payload.size(), streamId);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
