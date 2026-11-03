// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "recorder.h"

#include <global/diagnostics.h>
#include <global/expected.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u16 s_DiagnosticPayloadVersion = 1u;
inline constexpr u32 s_DiagnosticPayloadMagic = 0x4E574447u; // NWDG

namespace DiagnosticPayloadFlag{
    static constexpr auto s_DiagnosticPayloadFlagNoneBase = 0u;
    enum Mask : u16{
        None = s_DiagnosticPayloadFlagNoneBase,
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
static constexpr usize s_EncodedDiagnosticPayloadHeaderByteSize = 40u;
static_assert(sizeof(EncodedDiagnosticPayloadHeader) == s_EncodedDiagnosticPayloadHeaderByteSize, "EncodedDiagnosticPayloadHeader wire layout drifted");
static constexpr usize s_DiagnosticPackedAlignBytes = 1u;
static_assert(alignof(EncodedDiagnosticPayloadHeader) == s_DiagnosticPackedAlignBytes, "EncodedDiagnosticPayloadHeader must stay packed");
static_assert(IsStandardLayout_V<EncodedDiagnosticPayloadHeader>, "EncodedDiagnosticPayloadHeader must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedDiagnosticPayloadHeader>, "EncodedDiagnosticPayloadHeader must stay binary-serializable");

// Text fields borrow the parsed payload bytes; keep them alive and unchanged while reading.
struct DiagnosticPayload{
    AStringView event;
    AStringView category;
    AStringView expression;
    AStringView message;
    AStringView file;
    u64 instructionPointer = 0u;
    u32 line = 0u;
    b8 terminatesProcess = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 BuildDiagnosticPayload(const DiagnosticEventRecord& record, TelemetryArena& arena, TelemetryBytes& outPayload);
[[nodiscard]] Expected<DiagnosticPayload> ParseDiagnosticPayload(const void* payload, usize payloadBytes)noexcept;
[[nodiscard]] b8 RecordDiagnostic(Recorder& recorder, const DiagnosticEventRecord& record, u64 frameIndex = 0u, u32 streamId = 0u);

class DiagnosticCaptureGuard final : NoCopy{
public:
    explicit DiagnosticCaptureGuard(
        Recorder& recorder,
        u64 frameIndex = 0u,
        u32 streamId = 0u,
        const Atomic<u64>* sharedFrameIndex = nullptr,
        const Atomic<u32>* sharedStreamId = nullptr
    );
    DiagnosticCaptureGuard(DiagnosticCaptureGuard&&) = delete;
    ~DiagnosticCaptureGuard()noexcept;


public:
    void setFrameIndex(const u64 frameIndex)noexcept{ m_frameIndex.store(frameIndex, MemoryOrder::release); }

    void setStreamId(const u32 streamId)noexcept{ m_streamId.store(streamId, MemoryOrder::release); }

    [[nodiscard]] u64 frameIndex()const noexcept{
        return m_sharedFrameIndex ? m_sharedFrameIndex->load(MemoryOrder::acquire) : m_frameIndex.load(MemoryOrder::acquire);
    }

    [[nodiscard]] u32 streamId()const noexcept{
        return m_sharedStreamId ? m_sharedStreamId->load(MemoryOrder::acquire) : m_streamId.load(MemoryOrder::acquire);
    }

    [[nodiscard]] b8 installed()const noexcept{ return m_installed; }

    void capture(const DiagnosticEventRecord& record)noexcept;


private:
    Recorder& m_recorder;
    Atomic<u64> m_frameIndex{ 0u };
    Atomic<u32> m_streamId{ 0u };
    const Atomic<u64>* m_sharedFrameIndex = nullptr;
    const Atomic<u32>* m_sharedStreamId = nullptr;
    b8 m_installed = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

