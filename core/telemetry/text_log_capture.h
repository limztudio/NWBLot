// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "recorder.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TextLogCaptureLogger final : public Common::ILogger{
public:
    explicit TextLogCaptureLogger(Recorder& recorder, Common::ILogger* const forwardLogger = nullptr)noexcept
        : m_recorder(recorder)
        , m_forwardLogger(forwardLogger)
    {}
    virtual ~TextLogCaptureLogger()noexcept override = default;


public:
    void setFrameIndex(const u64 frameIndex)noexcept{ m_frameIndex.store(frameIndex, MemoryOrder::release); }

    void setStreamId(const u32 streamId)noexcept{ m_streamId.store(streamId, MemoryOrder::release); }

    void setForwardLogger(Common::ILogger* const forwardLogger)noexcept{ m_forwardLogger.store(forwardLogger, MemoryOrder::release); }

    [[nodiscard]] u64 droppedTextLogCount()const noexcept{ return m_droppedTextLogCount.load(MemoryOrder::relaxed); }

    [[nodiscard]] u64 frameIndex()const noexcept{ return m_frameIndex.load(MemoryOrder::acquire); }

    [[nodiscard]] u32 streamId()const noexcept{ return m_streamId.load(MemoryOrder::acquire); }

    [[nodiscard]] Common::ILogger* forwardLogger()const noexcept{ return m_forwardLogger.load(MemoryOrder::acquire); }

    virtual Common::LogArena& arena()override{
        Common::ILogger* const forwardLogger = m_forwardLogger.load(MemoryOrder::acquire);
        return forwardLogger ? forwardLogger->arena() : m_recorder.arena();
    }
    virtual void enqueue(Common::LogString&& str, Common::LogType::Enum type = Common::LogType::Info)override;
    virtual void enqueue(const Common::LogString& str, Common::LogType::Enum type = Common::LogType::Info)override;


private:
    void capture(TStringView message, Common::LogType::Enum type);


private:
    Recorder& m_recorder;
    Atomic<Common::ILogger*> m_forwardLogger{ nullptr };
    Atomic<u64> m_frameIndex{ 0u };
    Atomic<u32> m_streamId{ 0u };
    Atomic<u64> m_droppedTextLogCount{ 0u };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_TELEMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

