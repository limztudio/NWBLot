// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/task/cpu_task.h>

#include <global/termination.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cpu_task_mpmc_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;

inline constexpr u32 s_WorkerCount = 4u;
inline constexpr u32 s_ExternalProducerCount = 4u;
inline constexpr u32 s_ProducerCount = 1u + s_ExternalProducerCount + s_WorkerCount;
inline constexpr u32 s_TasksPerProducer = 128u;
inline constexpr u32 s_GateTimeoutMS = 4000u;


class DeadlineGuard final{
public:
    DeadlineGuard()
        : m_watchdog([](const StopToken& stop){
            const Timer begin = TimerNow();
            while(!stop.stop_requested()){
                if(DurationInMS<u64>(TimerNow(), begin) >= 12000u)
                    TerminateInvariant();
                SleepMS(1u);
            }
        })
    {}


private:
    JoiningThread m_watchdog;
};


template<typename Predicate>
[[nodiscard]] bool WaitUntil(const Predicate& predicate){
    const Timer begin = TimerNow();
    while(!predicate()){
        if(DurationInMS<u64>(TimerNow(), begin) >= s_GateTimeoutMS)
            return false;
        SleepMS(1u);
    }
    return true;
}


struct ProducerState{
    Atomic<bool> start{ false };
    Atomic<bool> expired{ false };
    Atomic<bool> rejected{ false };
    Atomic<bool> invalidOrder{ false };
    Atomic<u32> ready{ 0u };
    Atomic<u32> workerMask{ 0u };
    Atomic<u32> consumedPrefixMask{ 0u };
    Atomic<bool> published[s_ProducerCount]{};
    CpuTaskHandle handles[s_ProducerCount][s_TasksPerProducer]{};
    Atomic<u32> visits[s_ProducerCount * s_TasksPerProducer * 4u]{};
};


void Produce(CpuTaskScheduler& scheduler, CpuTaskScope& scope, ProducerState& state, u32 producer){
    if(!WaitUntil([&](){ return state.start.load(MemoryOrder::acquire); })){
        state.expired.store(true, MemoryOrder::release);
        return;
    }
    for(u32 task = 0u; task < s_TasksPerProducer; ++task){
        const u32 slot = (producer * s_TasksPerProducer + task) * 4u;
        auto callback = [&scheduler, &state, slot](){
            state.visits[slot].fetch_add(1u, MemoryOrder::relaxed);
            if(!scheduler.submit([&scheduler, &state, slot](){
                state.visits[slot + 1u].fetch_add(1u, MemoryOrder::relaxed);
                if(!scheduler.submit([&state, slot](){
                    state.visits[slot + 2u].fetch_add(1u, MemoryOrder::relaxed);
                }).valid())
                    state.rejected.store(true, MemoryOrder::release);
            }).valid())
                state.rejected.store(true, MemoryOrder::release);
        };
        CpuTaskOptions options;
        options.cost = static_cast<CpuTaskCost::Enum>(task % 3u);
        options.priority = static_cast<CpuTaskPriority::Enum>((task / 3u) % 3u);
        const CpuTaskHandle handle = task % 2u == 0u
            ? scope.submit(Move(callback), options)
            : scheduler.submit(Move(callback), options)
        ;
        state.handles[producer][task] = handle;
        if(!handle.valid())
            state.rejected.store(true, MemoryOrder::release);
        if(task == 31u && scheduler.currentWorkerIndex() != 0u && handle.valid()){
            scheduler.wait(handle);
            for(u32 descendant = 0u; descendant < 3u; ++descendant){
                if(state.visits[slot + descendant].load(MemoryOrder::acquire) != 1u)
                    state.invalidOrder.store(true, MemoryOrder::release);
            }
            if(state.published[producer].load(MemoryOrder::acquire))
                state.invalidOrder.store(true, MemoryOrder::release);
            state.consumedPrefixMask.fetch_or(1u << (producer - 1u - s_ExternalProducerCount), MemoryOrder::release);
        }
    }
    state.published[producer].store(true, MemoryOrder::release);
    const u32 predecessor = (producer + 1u) % s_ProducerCount;
    if(!WaitUntil([&](){ return state.published[predecessor].load(MemoryOrder::acquire); })){
        state.expired.store(true, MemoryOrder::release);
        return;
    }
    for(u32 task = 0u; task < s_TasksPerProducer; ++task){
        const u32 slot = (producer * s_TasksPerProducer + task) * 4u;
        const u32 precedingSlot = (predecessor * s_TasksPerProducer + task) * 4u;
        if(!scope.submit([&state, slot, precedingSlot](){
            for(u32 descendant = 0u; descendant < 3u; ++descendant){
                if(state.visits[precedingSlot + descendant].load(MemoryOrder::acquire) != 1u)
                    state.invalidOrder.store(true, MemoryOrder::release);
            }
            state.visits[slot + 3u].fetch_add(1u, MemoryOrder::relaxed);
        }, state.handles[predecessor][task]).valid())
            state.rejected.store(true, MemoryOrder::release);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(CpuTaskMpmcTests, OwnerExternalAndWorkerProducersShareQueuesWithoutLosingOrRepeatingTasks){
    using namespace __hidden_cpu_task_mpmc_tests;
    DeadlineGuard deadline;
    ProducerState state;
    CpuTaskSchedulerConfig config;
    config.workerCount = s_WorkerCount;
    config.heterogeneous = false;
    CpuTaskScheduler scheduler(config);
    CpuTaskScope scope(scheduler);
    JoiningThread producers[s_ExternalProducerCount];
    ScopeExit stopProducers([&]()noexcept{
        state.start.store(true, MemoryOrder::release);
        for(auto& producer : producers){
            if(producer.joinable())
                producer.join();
        }
        scheduler.drain();
    });

    for(u32 producer = 0u; producer < s_WorkerCount; ++producer){
        EXPECT_TRUE(scope.submit([&, producer](){
            const usize worker = scheduler.currentWorkerIndex();
            if(worker >= 1u && worker <= s_WorkerCount)
                state.workerMask.fetch_or(1u << (worker - 1u), MemoryOrder::release);
            state.ready.fetch_add(1u, MemoryOrder::release);
            Produce(scheduler, scope, state, 1u + s_ExternalProducerCount + producer);
        }, { .cost = CpuTaskCost::Any }).valid());
    }
    for(u32 producer = 0u; producer < s_ExternalProducerCount; ++producer){
        producers[producer] = JoiningThread([&, producer](){
            state.ready.fetch_add(1u, MemoryOrder::release);
            Produce(scheduler, scope, state, 1u + producer);
        });
    }
    EXPECT_TRUE(WaitUntil([&](){ return state.ready.load(MemoryOrder::acquire) == s_ProducerCount - 1u; }));
    EXPECT_EQ(state.workerMask.load(MemoryOrder::acquire), (1u << s_WorkerCount) - 1u);
    state.start.store(true, MemoryOrder::release);
    Produce(scheduler, scope, state, 0u);
    for(auto& producer : producers)
        producer.join();
    scope.wait();
    scheduler.wait();
    stopProducers.release();

    EXPECT_FALSE(state.expired.load(MemoryOrder::acquire));
    EXPECT_FALSE(state.rejected.load(MemoryOrder::acquire));
    EXPECT_FALSE(state.invalidOrder.load(MemoryOrder::acquire));
    EXPECT_EQ(state.consumedPrefixMask.load(MemoryOrder::acquire), (1u << s_WorkerCount) - 1u);
    for(const auto& visits : state.visits)
        EXPECT_EQ(visits.load(MemoryOrder::acquire), 1u);
    EXPECT_EQ(scheduler.statistics().completedTasks, s_WorkerCount + s_ProducerCount * s_TasksPerProducer * 4u);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}


TEST(CpuTaskMpmcTests, ExternalProducersCanWaitForMainThreadTasksWhileTheOwnerPumps){
    using namespace __hidden_cpu_task_mpmc_tests;
    DeadlineGuard deadline;
    const ThreadId owner = QueryCurrentThreadId();
    Atomic<u32> ready{ 0u };
    Atomic<u32> firstPublished{ 0u };
    Atomic<u32> finished{ 0u };
    Atomic<u32> invoked{ 0u };
    Atomic<bool> start{ false };
    Atomic<bool> expired{ false };
    Atomic<bool> rejected{ false };
    Atomic<bool> wrongThread{ false };
    Atomic<u32> visits[s_ExternalProducerCount * s_TasksPerProducer]{};
    CpuTaskSchedulerConfig config;
    config.workerCount = s_WorkerCount;
    config.heterogeneous = false;
    CpuTaskScheduler scheduler(config);
    CpuTaskScope scope(scheduler);
    JoiningThread producers[s_ExternalProducerCount];
    ScopeExit stopProducers([&]()noexcept{
        start.store(true, MemoryOrder::release);
        scheduler.drain();
        for(auto& producer : producers){
            if(producer.joinable())
                producer.join();
        }
    });

    for(u32 producer = 0u; producer < s_ExternalProducerCount; ++producer){
        producers[producer] = JoiningThread([&, producer](){
            ScopeExit finish([&]()noexcept{ finished.fetch_add(1u, MemoryOrder::release); });
            ready.fetch_add(1u, MemoryOrder::release);
            if(!WaitUntil([&](){ return start.load(MemoryOrder::acquire); })){
                expired.store(true, MemoryOrder::release);
                return;
            }
            for(u32 task = 0u; task < s_TasksPerProducer; ++task){
                const u32 slot = producer * s_TasksPerProducer + task;
                auto callback = [&, slot](){
                    if(QueryCurrentThreadId() != owner || scheduler.currentWorkerIndex() != 0u)
                        wrongThread.store(true, MemoryOrder::release);
                    visits[slot].fetch_add(1u, MemoryOrder::relaxed);
                    invoked.fetch_add(1u, MemoryOrder::release);
                };
                const CpuTaskOptions options{ .cost = CpuTaskCost::Light, .target = CpuTaskTarget::MainThread };
                const CpuTaskHandle handle = task % 2u == 0u
                    ? scope.submit(Move(callback), options)
                    : scheduler.submit(Move(callback), options)
                ;
                if(!handle.valid()){
                    rejected.store(true, MemoryOrder::release);
                    return;
                }
                if(task == 0u)
                    firstPublished.fetch_add(1u, MemoryOrder::release);
                scheduler.wait(handle);
            }
        });
    }
    EXPECT_TRUE(WaitUntil([&](){ return ready.load(MemoryOrder::acquire) == s_ExternalProducerCount; }));
    start.store(true, MemoryOrder::release);
    EXPECT_TRUE(WaitUntil([&](){ return firstPublished.load(MemoryOrder::acquire) == s_ExternalProducerCount; }));
    EXPECT_EQ(invoked.load(MemoryOrder::acquire), 0u);
    const Timer begin = TimerNow();
    while(finished.load(MemoryOrder::acquire) != s_ExternalProducerCount){
        if(DurationInMS<u64>(TimerNow(), begin) >= s_GateTimeoutMS){
            expired.store(true, MemoryOrder::release);
            scheduler.drain();
            break;
        }
        scheduler.pumpMainThread();
        SleepMS(1u);
    }
    for(auto& producer : producers)
        producer.join();
    scope.wait();
    scheduler.wait();
    stopProducers.release();

    EXPECT_FALSE(expired.load(MemoryOrder::acquire));
    EXPECT_FALSE(rejected.load(MemoryOrder::acquire));
    EXPECT_FALSE(wrongThread.load(MemoryOrder::acquire));
    EXPECT_EQ(invoked.load(MemoryOrder::acquire), s_ExternalProducerCount * s_TasksPerProducer);
    for(const auto& count : visits)
        EXPECT_EQ(count.load(MemoryOrder::acquire), 1u);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

