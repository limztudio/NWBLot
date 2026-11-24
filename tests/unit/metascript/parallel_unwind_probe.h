// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>
#include <global/atomic.h>
#include <global/scope_exit.h>
#include <global/thread.h>
#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::Metascript{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ParallelUnwindTracker final{
    mutable Atomic<u32> workerRuns{ 0u };
    mutable Atomic<u32> workerValueSum{ 0u };
    mutable Atomic<b8> workerStarted{ false };
    mutable Atomic<b8> workerMayFinish{ false };
    mutable Atomic<b8> workerFinished{ false };
    mutable u32 nextDestroyOrder = 1u;
    mutable u32 postDestroyOrder = 0u;
    mutable u32 preDestroyOrder = 0u;
    mutable b8 postSawFinished = false;
    mutable b8 preSawFinished = false;

private:
    [[nodiscard]] static b8 WaitForSignal(const Atomic<b8>& signal)noexcept{
        const Timer deadline = TimerAddMS(TimerNow(), 2000);
        while(!signal.load(MemoryOrder::acquire)){
            if(TimerNow() >= deadline)
                return false;
            SleepMS(1u);
        }
        return true;
    }


public:
    template<typename Callback>
    void invokeAfterWorkerStarts(Callback& callback)const{
        const ScopeExit release([this]()noexcept{ workerMayFinish.store(true, MemoryOrder::release); });
        if(WaitForSignal(workerStarted))
            callback();
    }

    void completeWork(const u32 seed)const noexcept{
        workerStarted.store(true, MemoryOrder::release);
        if(!WaitForSignal(workerMayFinish))
            return;
        SleepMS(10u);
        workerRuns.fetch_add(1u, MemoryOrder::relaxed);
        workerValueSum.fetch_add(seed, MemoryOrder::relaxed);
        workerFinished.store(true, MemoryOrder::release);
    }

    [[nodiscard]] u32 runs()const noexcept{ return workerRuns.load(MemoryOrder::acquire); }
    [[nodiscard]] u32 valueSum()const noexcept{ return workerValueSum.load(MemoryOrder::acquire); }
    [[nodiscard]] b8 finished()const noexcept{ return workerFinished.load(MemoryOrder::acquire); }
    [[nodiscard]] b8 postObservedFinished()const noexcept{ return postSawFinished; }
    [[nodiscard]] b8 preObservedFinished()const noexcept{ return preSawFinished; }
    [[nodiscard]] u32 postOrder()const noexcept{ return postDestroyOrder; }
    [[nodiscard]] u32 preOrder()const noexcept{ return preDestroyOrder; }
};

class ParallelLifetimeProbe final : NoCopy{
public:
    ParallelLifetimeProbe(const ParallelUnwindTracker& tracker, const b8 isPost)noexcept
        : m_tracker(tracker)
        , m_isPost(isPost)
    {}
    ~ParallelLifetimeProbe()noexcept{
        const b8 done = m_tracker.workerFinished.load(MemoryOrder::acquire);
        const u32 seq = m_tracker.nextDestroyOrder++;
        if(m_isPost){
            m_tracker.postSawFinished = done;
            m_tracker.postDestroyOrder = seq;
        }
        else{
            m_tracker.preSawFinished = done;
            m_tracker.preDestroyOrder = seq;
        }
    }


private:
    const ParallelUnwindTracker& m_tracker;
    b8 m_isPost = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

