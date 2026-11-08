// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "crash_ingest.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr usize s_MaxPendingCrashUploadPathText = 1024u;

struct PendingCrashUpload{
    Array<char, s_MaxPendingCrashUploadPathText> path = {};
};

using CrashUploadQueue = ParallelQueue<PendingCrashUpload, LogArena>;


interface ICrashIngestSink{
    virtual ~ICrashIngestSink()noexcept = default;
    virtual void deliverCrashIngestMessage(LogString&& message, Core::Common::LogType::Enum type) = 0;
};


class CrashIngestWorker final : NoCopy{
public:
    CrashIngestWorker(LogArena& arena, ICrashIngestSink& sink);
    ~CrashIngestWorker()noexcept;


public:
    void configure(AStringView symbolStoreDirectory, CrashRetentionConfig retention);
    void start();
    void stop();
    [[nodiscard]] b8 enqueue(const Path& path, Core::Alloc::ScratchArena& scratchArena);


private:
    void run();
    [[nodiscard]] Expected<PendingCrashUpload> tryDequeue();


private:
    ICrashIngestSink& m_sink;
    CrashIngestConfig m_config;
    CrashUploadQueue m_uploads;
    Semaphore<> m_semaphore;
    Atomic<b8> m_exit;
    Thread m_thread;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

