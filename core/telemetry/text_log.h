
#pragma once


#include "recorder.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u16 s_TextLogPayloadVersion = 1u;
inline constexpr u32 s_TextLogPayloadMagic = 0x4E574C47u; // NWLG

#pragma pack(push, 1)
struct EncodedTextLogPayloadHeader{
    u32 magic = s_TextLogPayloadMagic;
    u16 version = s_TextLogPayloadVersion;
    u8 type = Common::LogType::Info;
    u8 reserved = 0u;
    u64 messageBytes = 0u;
};
#pragma pack(pop)
static_assert(sizeof(EncodedTextLogPayloadHeader) == 16u, "EncodedTextLogPayloadHeader wire layout drifted");
static_assert(alignof(EncodedTextLogPayloadHeader) == 1u, "EncodedTextLogPayloadHeader must stay packed");
static_assert(IsStandardLayout_V<EncodedTextLogPayloadHeader>, "EncodedTextLogPayloadHeader must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EncodedTextLogPayloadHeader>, "EncodedTextLogPayloadHeader must stay binary-serializable");

struct TextLogPayload{
    Common::LogType::Enum type = Common::LogType::Info;
    AString<TelemetryArena> messageUtf8;

    explicit TextLogPayload(TelemetryArena& arena)
        : messageUtf8(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool IsValidTextLogType(Common::LogType::Enum type)noexcept;
[[nodiscard]] bool BuildTextLogPayload(TelemetryArena& arena, Common::LogType::Enum type, TStringView message, TelemetryBytes& outPayload);
[[nodiscard]] bool ParseTextLogPayload(TelemetryArena& arena, const void* payload, usize payloadBytes, TextLogPayload& outPayload);
[[nodiscard]] bool RecordTextLog(
    Recorder& recorder,
    Common::LogType::Enum type,
    TStringView message,
    u64 frameIndex = 0u,
    u32 streamId = 0u
);

class TextLogCaptureLogger final : public Common::ILogger{
public:
    explicit TextLogCaptureLogger(Recorder& recorder, Common::ILogger* forwardLogger = nullptr);
    virtual ~TextLogCaptureLogger()override = default;


public:
    void setFrameIndex(u64 frameIndex){ m_frameIndex = frameIndex; }
    void setStreamId(u32 streamId){ m_streamId = streamId; }
    void setForwardLogger(Common::ILogger* forwardLogger){ m_forwardLogger = forwardLogger; }

    [[nodiscard]] u64 frameIndex()const{ return m_frameIndex; }
    [[nodiscard]] u32 streamId()const{ return m_streamId; }
    [[nodiscard]] Common::ILogger* forwardLogger()const{ return m_forwardLogger; }

    virtual Common::LogArena& arena()override;
    virtual void enqueue(Common::LogString&& str, Common::LogType::Enum type = Common::LogType::Info)override;
    virtual void enqueue(const Common::LogString& str, Common::LogType::Enum type = Common::LogType::Info)override;


private:
    void capture(TStringView message, Common::LogType::Enum type);


private:
    Recorder& m_recorder;
    Common::ILogger* m_forwardLogger = nullptr;
    u64 m_frameIndex = 0u;
    u32 m_streamId = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

