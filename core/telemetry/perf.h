// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "recorder.h"

#include <core/perf/timing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u16 s_PerfTimingPayloadVersion = 1u;
inline constexpr u32 s_PerfTimingPayloadMagic = 0x4E575046u; // NWPF

namespace PerfTimingSource{
    enum Enum : u8{
        Unknown,
        Cpu,
        Gpu,
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
#pragma pack(pop)
static_assert(sizeof(EncodedPerfTimingPayloadHeader) == 112u, "EncodedPerfTimingPayloadHeader wire layout drifted");
static_assert(alignof(EncodedPerfTimingPayloadHeader) == 1u, "EncodedPerfTimingPayloadHeader must stay packed");
static_assert(IsStandardLayout_V<EncodedPerfTimingPayloadHeader>, "EncodedPerfTimingPayloadHeader must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedPerfTimingPayloadHeader>, "EncodedPerfTimingPayloadHeader must stay binary-serializable");

struct PerfTimingPayload{
    PerfTimingSource::Enum source = PerfTimingSource::Unknown;
    Name scopeName = NAME_NONE;
    AString<TelemetryArena> scopeText;
    Perf::TimingStats stats;

    explicit PerfTimingPayload(TelemetryArena& arena)
        : scopeText(arena)
    {}
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
