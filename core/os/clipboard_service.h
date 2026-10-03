// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "clipboard.h"

#include <global/thread.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Shared event-thread request lifecycle for native backends and deterministic platform fakes.
class QueuedClipboardService : public IClipboardService{
private:
    struct Request{
        AString<Alloc::GlobalArena> text;
        ClipboardRequestToken token;
        ClipboardOperation::Enum operation = ClipboardOperation::ReadText;
        ClipboardChannel::Enum channel = ClipboardChannel::Clipboard;
        ClipboardStatus::Enum status = ClipboardStatus::Unavailable;
        bool completed = false;
        bool executing = false;
        bool started = false;

        explicit Request(Alloc::GlobalArena& arena)
            : text(arena)
        {}
    };


public:
    explicit QueuedClipboardService(Alloc::GlobalArena& arena);
    virtual ~QueuedClipboardService()override;


public:
    [[nodiscard]] virtual bool isOwnerThread()const noexcept override final{ return QueryCurrentThreadId() == m_ownerThread; }
    [[nodiscard]] virtual ClipboardRequestResult requestReadText(ClipboardChannel::Enum channel)override final;
    [[nodiscard]] virtual ClipboardRequestResult requestWriteText(ClipboardChannel::Enum channel, AStringView text)override final;
    [[nodiscard]] virtual ClipboardPollResult::Enum poll(ClipboardRequestToken token, ClipboardCompletion& completion)override final;
    [[nodiscard]] virtual bool cancel(ClipboardRequestToken token)override final;
    [[nodiscard]] virtual bool pump()override final;


protected:
    // Native events may complete a started token on a later event-thread iteration. No client storage is borrowed.
    virtual void startNativeRequest(ClipboardRequestToken token, ClipboardOperation::Enum operation, ClipboardChannel::Enum channel, AStringView text) = 0;
    virtual void cancelNativeRequest(ClipboardRequestToken token);
    virtual void pumpNativeRequests();
    [[nodiscard]] bool completeNativeRequest(ClipboardRequestToken token, ClipboardStatus::Enum status, AStringView text = {});


private:
    [[nodiscard]] ClipboardRequestResult enqueue(ClipboardChannel::Enum channel, ClipboardOperation::Enum operation, AStringView text);
    [[nodiscard]] Request* findRequest(ClipboardRequestToken token);


private:
    Alloc::GlobalArena& m_arena;
    Vector<Request, Alloc::GlobalArena> m_requests;
    const ThreadId m_ownerThread;
    const u64 m_serviceIdentity;
    u64 m_nextGeneration = 1u;
    bool m_pumping = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

