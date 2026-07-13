
#pragma once


#include "recorder.h"

#include <global/diagnostics.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u16 s_DiagnosticPayloadVersion = 1u;
inline constexpr u32 s_DiagnosticPayloadMagic = 0x4E574447u; // NWDG

namespace DiagnosticPayloadFlag{
    enum Mask : u16{
        None = 0u,
        TerminatesProcess = BitMask<u16>(0u),
    };
};

#pragma pack(push, 1)
struct EncodedDiagnosticPayloadHeader{
    u32 magic = s_DiagnosticPayloadMagic;
    u16 version = s_DiagnosticPayloadVersion;
    u16 flags = DiagnosticPayloadFlag::None;
    u64 instructionPointer = 0u;
    u32 line = 0u;
    u32 eventBytes = 0u;
    u32 categoryBytes = 0u;
    u32 expressionBytes = 0u;
    u32 messageBytes = 0u;
    u32 fileBytes = 0u;
};
#pragma pack(pop)
static_assert(sizeof(EncodedDiagnosticPayloadHeader) == 40u, "EncodedDiagnosticPayloadHeader wire layout drifted");
static_assert(alignof(EncodedDiagnosticPayloadHeader) == 1u, "EncodedDiagnosticPayloadHeader must stay packed");
static_assert(IsStandardLayout_V<EncodedDiagnosticPayloadHeader>, "EncodedDiagnosticPayloadHeader must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedDiagnosticPayloadHeader>, "EncodedDiagnosticPayloadHeader must stay binary-serializable");

struct DiagnosticPayload{
    AString<TelemetryArena> event;
    AString<TelemetryArena> category;
    AString<TelemetryArena> expression;
    AString<TelemetryArena> message;
    AString<TelemetryArena> file;
    u64 instructionPointer = 0u;
    u32 line = 0u;
    bool terminatesProcess = false;

    explicit DiagnosticPayload(TelemetryArena& arena)
        : event(arena)
        , category(arena)
        , expression(arena)
        , message(arena)
        , file(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool BuildDiagnosticPayload(TelemetryArena& arena, const DiagnosticEventRecord& record, TelemetryBytes& outPayload);
[[nodiscard]] bool ParseDiagnosticPayload(TelemetryArena& arena, const void* payload, usize payloadBytes, DiagnosticPayload& outPayload);
[[nodiscard]] bool RecordDiagnostic(
    Recorder& recorder,
    const DiagnosticEventRecord& record,
    u64 frameIndex = 0u,
    u32 streamId = 0u
);

class DiagnosticCaptureGuard final : NoCopy{
public:
    explicit DiagnosticCaptureGuard(Recorder& recorder);
    DiagnosticCaptureGuard(DiagnosticCaptureGuard&&) = delete;
    ~DiagnosticCaptureGuard();


public:
    void setFrameIndex(u64 frameIndex){ m_frameIndex = frameIndex; }
    void setStreamId(u32 streamId){ m_streamId = streamId; }

    [[nodiscard]] u64 frameIndex()const{ return m_frameIndex; }
    [[nodiscard]] u32 streamId()const{ return m_streamId; }
    [[nodiscard]] bool installed()const{ return m_installed; }

    [[nodiscard]] bool capture(const DiagnosticEventRecord& record);


private:
    Recorder& m_recorder;
    u64 m_frameIndex = 0u;
    u32 m_streamId = 0u;
    bool m_installed = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

