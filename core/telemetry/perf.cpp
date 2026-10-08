// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "perf.h"

#include <global/binary.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_telemetry_perf{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ValidateHeader(const EncodedPerfTimingPayloadHeader& header)noexcept{
    return header.magic == s_PerfTimingPayloadMagic
        && header.version == s_PerfTimingPayloadVersion
        && header.reserved == 0u
        && IsValidPerfTimingSource(static_cast<PerfTimingSource::Enum>(header.source))
        && header.sampleCount != 0u
        && !NameDetail::IsZeroHash(header.scopeHash)
    ;
}

[[nodiscard]] static bool IsValidMemorySource(const u32 source)noexcept{
    return source == Perf::MemorySource::ExplicitScope
        || source == Perf::MemorySource::Arena
        || source == Perf::MemorySource::HeapBacking
    ;
}

[[nodiscard]] static bool ValidateHeader(const EncodedPerfMemoryPayloadHeader& header)noexcept{
    constexpr u16 s_KnownFlags = PerfMemoryPayloadFlag::HasDelta;
    return header.magic == s_PerfMemoryPayloadMagic
        && header.version == s_PerfMemoryPayloadVersion
        && (header.flags & ~s_KnownFlags) == 0u
        && IsValidMemorySource(header.source)
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
        || !IsValidMemorySource(snapshot.source)
        || snapshot.scopeName != scopeName
        || !snapshot.valid()
        || scopeText.empty()
        || scopeText.size() > Limit<u32>::s_Max
    )
        return false;

    return !delta.hasSamples || delta.currentFrameIndex == snapshot.frameIndex;
}

template<typename HeaderT>
[[nodiscard]] static bool AppendPerfPayloadWithScopeText(
    const HeaderT& header,
    const AStringView scopeText,
    TelemetryBytes& outPayload
){
    usize payloadBytes = sizeof(HeaderT);
    if(!AddBinaryReserveBytes(payloadBytes, scopeText.size()))
        return false;

    outPayload.reserve(payloadBytes);
    AppendPOD(outPayload, header);
    AppendTextBytesNoReserveUnchecked(outPayload, scopeText);
    return outPayload.size() == payloadBytes;
}

template<typename HeaderT>
struct ParsedPerfPayload{
    HeaderT header;
    AStringView scopeText;
};

template<typename HeaderT>
[[nodiscard]] static Expected<ParsedPerfPayload<HeaderT>> ParsePerfPayloadWithScopeText(
    const void* const payload,
    const usize payloadBytes
){
    if(payloadBytes < sizeof(HeaderT) || !payload)
        return MakeUnexpected(Failure{});

    const BinaryByteView encoded{ static_cast<const u8*>(payload), payloadBytes };
    usize cursor = 0u;
    const auto header = ReadPOD<HeaderT>(encoded, cursor);
    if(!header || !ValidateHeader(*header))
        return MakeUnexpected(Failure{});
    if(!BinaryDetail::CanReadBytes(encoded, cursor, header->scopeNameBytes))
        return MakeUnexpected(Failure{});
    if(payloadBytes - cursor != header->scopeNameBytes)
        return MakeUnexpected(Failure{});

    return ParsedPerfPayload<HeaderT>{
        .header = *header,
        .scopeText = AStringView(reinterpret_cast<const char*>(encoded.data() + cursor), header->scopeNameBytes),
    };
}

static void RecordTimingView(
    Recorder& recorder,
    const PerfTimingSource::Enum source,
    const Perf::TimingView& timing,
    const u32 streamId,
    u32& outRecordedEvents,
    bool& inOutSucceeded
){
    if(!recorder.enabled(EventKind::PerfFrame)){
        inOutSucceeded = false;
        return;
    }

    for(usize i = 0u; i < timing.scopeCount(); ++i){
        const Name scopeName = timing.scopeNameAt(i);
        const Perf::TimingStats& stats = timing.statsAt(i);
        if(!stats.valid())
            continue;
        if(!RecordPerfTiming(recorder, source, scopeName, stats, streamId)){
            inOutSucceeded = false;
            continue;
        }
        ++outRecordedEvents;
    }
}

static void RecordMemoryView(
    Recorder& recorder,
    const Perf::MemoryView& memory,
    const u32 streamId,
    u32& outRecordedEvents,
    bool& inOutSucceeded
){
    if(!recorder.enabled(EventKind::MemoryFrame)){
        inOutSucceeded = false;
        return;
    }

    for(usize i = 0u; i < memory.scopeCount(); ++i){
        const Name scopeName = memory.scopeNameAt(i);
        const Perf::MemorySnapshot& snapshot = memory.snapshotAt(i);
        if(!snapshot.valid())
            continue;
        if(!RecordPerfMemory(recorder, scopeName, snapshot, memory.deltaAt(i), streamId)){
            inOutSucceeded = false;
            continue;
        }
        ++outRecordedEvents;
    }
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

    EncodedPerfTimingPayloadHeader header;
    header.source = source;
    header.scopeHash = scopeName.hash();
    header.seconds = stats.seconds;
    header.minSeconds = stats.minSeconds;
    header.maxSeconds = stats.maxSeconds;
    header.lastSeconds = stats.lastSeconds;
    header.publishFrameIndex = stats.publishFrameIndex;
    header.firstSampleFrameIndex = stats.firstSampleFrameIndex;
    header.lastSampleFrameIndex = stats.lastSampleFrameIndex;
    header.sampleCount = stats.sampleCount;
    header.scopeNameBytes = static_cast<u32>(scopeText.size());

    return __hidden_telemetry_perf::AppendPerfPayloadWithScopeText(header, scopeText, outPayload);
}

bool BuildPerfTimingPayload(
    TelemetryArena& arena,
    const PerfTimingSource::Enum source,
    const Name& scopeName,
    const Perf::TimingStats& stats,
    TelemetryBytes& outPayload
){
    return BuildPerfTimingPayload(arena, source, scopeName, scopeName.resolvedText(), stats, outPayload);
}

Expected<PerfTimingPayload> ParsePerfTimingPayload(
    TelemetryArena& arena,
    const void* const payload,
    const usize payloadBytes
){
    PerfTimingPayload parsedPayload(arena);

    const auto decoded = __hidden_telemetry_perf::ParsePerfPayloadWithScopeText<EncodedPerfTimingPayloadHeader>(payload, payloadBytes);
    if(!decoded)
        return MakeUnexpected(Failure{});
    const EncodedPerfTimingPayloadHeader& header = decoded->header;
    const AStringView scopeText = decoded->scopeText;

    parsedPayload.source = static_cast<PerfTimingSource::Enum>(header.source);
    parsedPayload.scopeName = Name(header.scopeHash);
    parsedPayload.scopeText.assign(scopeText.data(), scopeText.size());
    parsedPayload.stats.seconds = header.seconds;
    parsedPayload.stats.minSeconds = header.minSeconds;
    parsedPayload.stats.maxSeconds = header.maxSeconds;
    parsedPayload.stats.lastSeconds = header.lastSeconds;
    parsedPayload.stats.sampleCount = header.sampleCount;
    parsedPayload.stats.publishFrameIndex = header.publishFrameIndex;
    parsedPayload.stats.firstSampleFrameIndex = header.firstSampleFrameIndex;
    parsedPayload.stats.lastSampleFrameIndex = header.lastSampleFrameIndex;
    return parsedPayload;
}

bool RecordPerfTiming(
    Recorder& recorder,
    const PerfTimingSource::Enum source,
    const Name& scopeName,
    const AStringView scopeText,
    const Perf::TimingStats& stats,
    const u32 streamId
){
    return recorder.recordBuiltPayload(
        EventKind::PerfFrame,
        stats.publishFrameIndex,
        streamId,
        [source, &scopeName, scopeText, &stats](TelemetryArena& arena, TelemetryBytes& payload){
            return BuildPerfTimingPayload(arena, source, scopeName, scopeText, stats, payload);
        }
    );
}

bool RecordPerfTiming(
    Recorder& recorder,
    const PerfTimingSource::Enum source,
    const Name& scopeName,
    const Perf::TimingStats& stats,
    const u32 streamId
){
    return RecordPerfTiming(recorder, source, scopeName, scopeName.resolvedText(), stats, streamId);
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

    EncodedPerfMemoryPayloadHeader header;
    header.source = snapshot.source;
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

    return __hidden_telemetry_perf::AppendPerfPayloadWithScopeText(header, scopeText, outPayload);
}

bool BuildPerfMemoryPayload(
    TelemetryArena& arena,
    const Name& scopeName,
    const Perf::MemorySnapshot& snapshot,
    const Perf::MemoryDelta& delta,
    TelemetryBytes& outPayload
){
    return BuildPerfMemoryPayload(arena, scopeName, scopeName.resolvedText(), snapshot, delta, outPayload);
}

Expected<PerfMemoryPayload> ParsePerfMemoryPayload(
    TelemetryArena& arena,
    const void* const payload,
    const usize payloadBytes
){
    PerfMemoryPayload parsedPayload(arena);

    const auto decoded = __hidden_telemetry_perf::ParsePerfPayloadWithScopeText<EncodedPerfMemoryPayloadHeader>(payload, payloadBytes);
    if(!decoded)
        return MakeUnexpected(Failure{});
    const EncodedPerfMemoryPayloadHeader& header = decoded->header;
    const AStringView scopeText = decoded->scopeText;

    parsedPayload.scopeName = Name(header.scopeHash);
    parsedPayload.scopeText.assign(scopeText.data(), scopeText.size());
    parsedPayload.snapshot.scopeName = parsedPayload.scopeName;
    parsedPayload.snapshot.source = static_cast<Perf::MemorySource::Enum>(header.source);
    parsedPayload.snapshot.frameIndex = header.frameIndex;
    parsedPayload.snapshot.reservedBytes = header.reservedBytes;
    parsedPayload.snapshot.usedBytes = header.usedBytes;
    parsedPayload.snapshot.peakUsedBytes = header.peakUsedBytes;
    parsedPayload.snapshot.allocationCount = header.allocationCount;
    parsedPayload.snapshot.reallocationCount = header.reallocationCount;
    parsedPayload.snapshot.deallocationCount = header.deallocationCount;

    if((header.flags & PerfMemoryPayloadFlag::HasDelta) != 0u){
        parsedPayload.delta.previousFrameIndex = header.previousFrameIndex;
        parsedPayload.delta.currentFrameIndex = header.frameIndex;
        parsedPayload.delta.reservedBytes = header.deltaReservedBytes;
        parsedPayload.delta.usedBytes = header.deltaUsedBytes;
        parsedPayload.delta.peakUsedBytes = header.deltaPeakUsedBytes;
        parsedPayload.delta.allocationCount = header.deltaAllocationCount;
        parsedPayload.delta.reallocationCount = header.deltaReallocationCount;
        parsedPayload.delta.deallocationCount = header.deltaDeallocationCount;
        parsedPayload.delta.hasSamples = true;
    }

    return parsedPayload;
}

bool RecordPerfMemory(
    Recorder& recorder,
    const Name& scopeName,
    const AStringView scopeText,
    const Perf::MemorySnapshot& snapshot,
    const Perf::MemoryDelta& delta,
    const u32 streamId
){
    return recorder.recordBuiltPayload(
        EventKind::MemoryFrame,
        snapshot.frameIndex,
        streamId,
        [&scopeName, scopeText, &snapshot, &delta](TelemetryArena& arena, TelemetryBytes& payload){
            return BuildPerfMemoryPayload(arena, scopeName, scopeText, snapshot, delta, payload);
        }
    );
}

bool RecordPerfMemory(
    Recorder& recorder,
    const Name& scopeName,
    const Perf::MemorySnapshot& snapshot,
    const Perf::MemoryDelta& delta,
    const u32 streamId
){
    return RecordPerfMemory(recorder, scopeName, scopeName.resolvedText(), snapshot, delta, streamId);
}

PerfSessionRecordResult RecordPerfSessionReport(
    Recorder& recorder,
    const Perf::SessionReport& report,
    const u32 streamId
){
    PerfSessionRecordResult result;
    if(report.capture.cpuTimingActive())
        __hidden_telemetry_perf::RecordTimingView(
            recorder,
            PerfTimingSource::Cpu,
            report.cpuTiming,
            streamId,
            result.cpuTimingEvents,
            result.succeeded
        );
    if(report.capture.gpuTimingActive())
        __hidden_telemetry_perf::RecordTimingView(
            recorder,
            PerfTimingSource::Gpu,
            report.gpuTiming,
            streamId,
            result.gpuTimingEvents,
            result.succeeded
        );
    if(report.capture.memoryActive())
        __hidden_telemetry_perf::RecordMemoryView(
            recorder,
            report.memory,
            streamId,
            result.memoryEvents,
            result.succeeded
        );
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

