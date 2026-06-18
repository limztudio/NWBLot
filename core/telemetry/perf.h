// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "recorder.h"

#include <core/perf/memory.h>
#include <core/perf/report.h>
#include <core/perf/timing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u16 s_PerfTimingPayloadVersion = 1u;
inline constexpr u32 s_PerfTimingPayloadMagic = 0x4E575046u; // NWPF
inline constexpr u16 s_PerfMemoryPayloadVersion = 1u;
inline constexpr u32 s_PerfMemoryPayloadMagic = 0x4E57504Du; // NWPM

namespace PerfTimingSource{
    enum Enum : u8{
        Unknown,
        Cpu,
        Gpu,
    };
};

namespace PerfMemoryPayloadFlag{
    enum Mask : u16{
        None = 0u,
        HasDelta = BitMask<u16>(0u),
    };
};

#pragma pack(push, 1)
struct EncodedPerfTimingPayloadHeader{
    u32 magic = s_PerfTimingPayloadMagic;
    u16 version = s_PerfTimingPayloadVersion;
    u8 source = PerfTimingSource::Unknown;
    u8 reserved = 0u;
    NameHash scopeHash = {};
    f64 seconds = 0.0;
    u64 publishFrameIndex = 0u;
    u64 firstSampleFrameIndex = 0u;
    u64 lastSampleFrameIndex = 0u;
    u32 sampleCount = 0u;
    u32 scopeNameBytes = 0u;
};

struct EncodedPerfMemoryPayloadHeader{
    u32 magic = s_PerfMemoryPayloadMagic;
    u16 version = s_PerfMemoryPayloadVersion;
    u16 flags = PerfMemoryPayloadFlag::None;
    NameHash scopeHash = {};
    u64 frameIndex = 0u;
    u64 reservedBytes = 0u;
    u64 usedBytes = 0u;
    u64 peakUsedBytes = 0u;
    u64 allocationCount = 0u;
    u64 reallocationCount = 0u;
    u64 deallocationCount = 0u;
    u64 previousFrameIndex = 0u;
    i64 deltaReservedBytes = 0;
    i64 deltaUsedBytes = 0;
    i64 deltaPeakUsedBytes = 0;
    i64 deltaAllocationCount = 0;
    i64 deltaReallocationCount = 0;
    i64 deltaDeallocationCount = 0;
    u32 scopeNameBytes = 0u;
    u32 reserved = 0u;
};
#pragma pack(pop)
static_assert(sizeof(EncodedPerfTimingPayloadHeader) == 112u, "EncodedPerfTimingPayloadHeader wire layout drifted");
static_assert(alignof(EncodedPerfTimingPayloadHeader) == 1u, "EncodedPerfTimingPayloadHeader must stay packed");
static_assert(IsStandardLayout_V<EncodedPerfTimingPayloadHeader>, "EncodedPerfTimingPayloadHeader must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedPerfTimingPayloadHeader>, "EncodedPerfTimingPayloadHeader must stay binary-serializable");
static_assert(sizeof(EncodedPerfMemoryPayloadHeader) == 192u, "EncodedPerfMemoryPayloadHeader wire layout drifted");
static_assert(alignof(EncodedPerfMemoryPayloadHeader) == 1u, "EncodedPerfMemoryPayloadHeader must stay packed");
static_assert(IsStandardLayout_V<EncodedPerfMemoryPayloadHeader>, "EncodedPerfMemoryPayloadHeader must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedPerfMemoryPayloadHeader>, "EncodedPerfMemoryPayloadHeader must stay binary-serializable");

struct PerfTimingPayload{
    PerfTimingSource::Enum source = PerfTimingSource::Unknown;
    Name scopeName = NAME_NONE;
    AString<TelemetryArena> scopeText;
    Perf::TimingStats stats;

    explicit PerfTimingPayload(TelemetryArena& arena)
        : scopeText(arena)
    {}
};

struct PerfMemoryPayload{
    Name scopeName = NAME_NONE;
    AString<TelemetryArena> scopeText;
    Perf::MemorySnapshot snapshot;
    Perf::MemoryDelta delta;

    explicit PerfMemoryPayload(TelemetryArena& arena)
        : scopeText(arena)
    {}
};

struct PerfSessionRecordResult{
    u32 cpuTimingEvents = 0u;
    u32 gpuTimingEvents = 0u;
    u32 memoryEvents = 0u;

    [[nodiscard]] u32 eventCount()const{ return cpuTimingEvents + gpuTimingEvents + memoryEvents; }
    [[nodiscard]] bool recordedAny()const{ return eventCount() != 0u; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool IsValidPerfTimingSource(PerfTimingSource::Enum source)noexcept;
[[nodiscard]] bool BuildPerfTimingPayload(
    TelemetryArena& arena,
    PerfTimingSource::Enum source,
    const Name& scopeName,
    AStringView scopeText,
    const Perf::TimingStats& stats,
    TelemetryBytes& outPayload
);
[[nodiscard]] bool BuildPerfTimingPayload(
    TelemetryArena& arena,
    PerfTimingSource::Enum source,
    const Name& scopeName,
    const Perf::TimingStats& stats,
    TelemetryBytes& outPayload
);
[[nodiscard]] bool ParsePerfTimingPayload(TelemetryArena& arena, const void* payload, usize payloadBytes, PerfTimingPayload& outPayload);
[[nodiscard]] bool RecordPerfTiming(
    Recorder& recorder,
    PerfTimingSource::Enum source,
    const Name& scopeName,
    const Perf::TimingStats& stats,
    u32 streamId = 0u
);
[[nodiscard]] bool BuildPerfMemoryPayload(
    TelemetryArena& arena,
    const Name& scopeName,
    AStringView scopeText,
    const Perf::MemorySnapshot& snapshot,
    const Perf::MemoryDelta& delta,
    TelemetryBytes& outPayload
);
[[nodiscard]] bool BuildPerfMemoryPayload(
    TelemetryArena& arena,
    const Name& scopeName,
    const Perf::MemorySnapshot& snapshot,
    const Perf::MemoryDelta& delta,
    TelemetryBytes& outPayload
);
[[nodiscard]] bool ParsePerfMemoryPayload(TelemetryArena& arena, const void* payload, usize payloadBytes, PerfMemoryPayload& outPayload);
[[nodiscard]] bool RecordPerfMemory(
    Recorder& recorder,
    const Name& scopeName,
    const Perf::MemorySnapshot& snapshot,
    const Perf::MemoryDelta& delta,
    u32 streamId = 0u
);
[[nodiscard]] PerfSessionRecordResult RecordPerfSessionReport(
    Recorder& recorder,
    const Perf::SessionReport& report,
    u32 streamId = 0u
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

