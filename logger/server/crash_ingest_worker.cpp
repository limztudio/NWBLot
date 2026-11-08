// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "crash_ingest_worker.h"

#include <global/fixed_buffer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_crash_ingest_worker{
inline constexpr Name s_CrashIngestArena("logger/server/crash_ingest");
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CrashIngestWorker::CrashIngestWorker(LogArena& arena, ICrashIngestSink& sink)
    : m_sink(sink)
    , m_config(arena)
    , m_uploads(arena)
    , m_semaphore(0)
    , m_exit(false)
{}

CrashIngestWorker::~CrashIngestWorker()noexcept{
    stop();
}


void CrashIngestWorker::configure(const AStringView symbolStoreDirectory, const CrashRetentionConfig retention){
    m_config.symbolication.symbolStoreDirectory.clear();
    if(!symbolStoreDirectory.empty())
        m_config.symbolication.symbolStoreDirectory = symbolStoreDirectory;
    m_config.retention = retention;
}

void CrashIngestWorker::start(){
    m_exit.store(false, MemoryOrder::release);
    m_thread = Thread([this](){ run(); });
}

void CrashIngestWorker::stop(){
    const b8 alreadyStopping = m_exit.exchange(true, MemoryOrder::acq_rel);
    if(!alreadyStopping)
        m_semaphore.release();
    if(m_thread.joinable())
        m_thread.join();
}

b8 CrashIngestWorker::enqueue(const Path& path, Core::Alloc::ScratchArena& scratchArena){
    PendingCrashUpload upload;
    const AString<Core::Alloc::ScratchArena> pathText = PathToString<char>(scratchArena, path);
    if(pathText.size() >= sizeof(upload.path))
        return false;

    CopyFixedBuffer(upload.path, AStringView(pathText.data(), pathText.size()));
    m_uploads.emplace(upload);
    m_semaphore.release();
    return true;
}


void CrashIngestWorker::run(){
    for(;;){
        m_semaphore.acquire();

        while(const auto crashUpload = tryDequeue()){
            LogArena ingestArena(__hidden_crash_ingest_worker::s_CrashIngestArena);
            CrashIngestConfig ingestConfig(ingestArena);
            ingestConfig.storageDirectory = m_config.storageDirectory;
            ingestConfig.symbolication.symbolStoreDirectory = m_config.symbolication.symbolStoreDirectory;
            ingestConfig.retention = m_config.retention;

            const Path archivePath(ingestArena, AStringView(crashUpload->path.data()));
            CrashIngestResult ingestResult = ProcessCrashUpload(ingestArena, archivePath, ingestConfig);
            m_sink.deliverCrashIngestMessage(Move(ingestResult.message), ingestResult.type);
        }

        if(m_exit.load(MemoryOrder::acquire))
            break;
    }
}

Expected<PendingCrashUpload> CrashIngestWorker::tryDequeue(){
    PendingCrashUpload upload;
    if(!m_uploads.try_pop(upload))
        return MakeUnexpected(Failure{});
    return upload;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

