// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/perf/cpu_task_profile.h>
#include <core/perf/session.h>
#include <core/task/cpu/scheduler.h>
#include <core/telemetry/session.h>

#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cpu_task_profile_integration_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;
using TestArena = NWB::Tests::TestArena<struct CpuTaskProfileIntegrationTag>;


class OwnerThreadTimingSink final : public Perf::TimingSink{
public:
    OwnerThreadTimingSink(CpuTaskScheduler& scheduler, Perf::TimingRecorder& recorder, const bool injectTasks = false)
        : m_scheduler(scheduler)
        , m_recorder(recorder)
        , m_owner(QueryCurrentThreadId())
        , m_injectTasks(injectTasks)
    {}


public:
    [[nodiscard]] virtual bool enabled()const noexcept override{ return m_recorder.enabled(); }

    [[nodiscard]] virtual Perf::TimingScopeId registerScope(const Name& name)override{
        EXPECT_EQ(QueryCurrentThreadId(), m_owner);
        return m_recorder.registerScope(name);
    }

    virtual void recordSample(const Perf::TimingScopeId scope, const f64 seconds, const u64 frameIndex)override{
        EXPECT_EQ(QueryCurrentThreadId(), m_owner);
        ++m_recordedSamples;
        m_recorder.recordSample(scope, seconds, frameIndex);
        if(m_injectTasks && m_injectionBudget != 0u){
            --m_injectionBudget;
            const CpuTaskHandle task = m_scheduler.submit([](){});
            ASSERT_TRUE(task.valid());
            m_scheduler.wait(task);
        }
    }

    virtual void publishFrame(const u64 frameIndex)override{
        EXPECT_EQ(QueryCurrentThreadId(), m_owner);
        m_recorder.publishFrame(frameIndex);
    }


public:
    usize m_recordedSamples = 0u;

private:
    CpuTaskScheduler& m_scheduler;
    Perf::TimingRecorder& m_recorder;
    ThreadId m_owner;
    bool m_injectTasks;
    usize m_injectionBudget = 64u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(CpuTaskProfileIntegration, NamedTasksAndWaitsReachPublishedPerfAndTelemetry){
    using namespace __hidden_cpu_task_profile_integration_tests;
    TestArena arena;
    Perf::Session perf(arena.arena);
    perf.setCaptureOptions({ .enabled = true, .cpuTiming = true });
    perf.beginFrame(42u);
    CpuTaskScheduler scheduler(0u);
    const Name taskName("cpu.task.tests.explicit");
    const Name scopeName("cpu.task.tests.scope");
    const CpuTaskProfileLabel taskLabel = scheduler.registerProfileLabel(taskName);
    const CpuTaskProfileLabel scopeLabel = scheduler.registerProfileLabel(scopeName);
    ASSERT_TRUE(taskLabel.valid());
    ASSERT_TRUE(scopeLabel.valid());
    CpuTaskScope scope(scheduler, scopeLabel);
    scheduler.setProfiling(true, 41u);

    const CpuTaskHandle task = scope.submit([](){}, { .profileLabel = taskLabel });
    ASSERT_TRUE(task.valid());
    scheduler.wait(task);
    ASSERT_TRUE(scope.submit([](){}).valid());
    scope.wait();
    ASSERT_TRUE(scheduler.submit([](){}).valid());
    scheduler.wait();
    ASSERT_GT(scheduler.statistics().profilePendingEvents, 0u);
    Perf::CollectCpuTaskProfile(scheduler, perf.cpuTimingSink());
    EXPECT_EQ(scheduler.statistics().profilePendingEvents, 0u);
    perf.publishFrame();

    const Perf::TimingView timing = perf.cpuTimingView();
    EXPECT_EQ(timing.stats(taskName).sampleCount, 1u);
    EXPECT_EQ(timing.stats(scopeName).sampleCount, 1u);
    EXPECT_EQ(timing.stats(Name("cpu.task.execution")).sampleCount, 1u);
    EXPECT_EQ(timing.stats(Name("cpu.task.queue_delay")).sampleCount, 3u);
    EXPECT_TRUE(timing.stats(Name("cpu.task.handle_join")).valid());
    EXPECT_TRUE(timing.stats(Name("cpu.task.scope_join")).valid());
    EXPECT_TRUE(timing.stats(Name("cpu.task.scheduler_join")).valid());
    EXPECT_EQ(timing.stats(taskName).firstSampleFrameIndex, 41u);
    EXPECT_EQ(timing.stats(taskName).lastSampleFrameIndex, 41u);
    EXPECT_EQ(timing.stats(taskName).publishFrameIndex, 42u);

    Telemetry::CaptureSession telemetry(arena.arena);
    telemetry.setCaptureOptions(Telemetry::CaptureOptions::All());
    const Telemetry::PerfSessionRecordResult result = telemetry.recordPerfReport(perf.report());
    ASSERT_TRUE(result.ok());
    EXPECT_GT(result.cpuTimingEvents, 0u);
    bool foundNamedTask = false;
    const Telemetry::EventView events = telemetry.view();
    for(usize index = 0u; index < events.eventCount(); ++index){
        const Telemetry::EventRecord* event = events.eventAt(index);
        ASSERT_NE(event, nullptr);
        Telemetry::PerfTimingPayload payload(arena.arena);
        ASSERT_TRUE(Telemetry::ParsePerfTimingPayload(arena.arena, event->payload.data(), event->payload.size(), payload));
        if(payload.scopeName == taskName){
            foundNamedTask = true;
            EXPECT_EQ(payload.source, Telemetry::PerfTimingSource::Cpu);
            EXPECT_EQ(payload.stats.sampleCount, 1u);
            EXPECT_EQ(payload.stats.firstSampleFrameIndex, 41u);
            EXPECT_EQ(payload.stats.publishFrameIndex, 42u);
            EXPECT_EQ(payload.stats.seconds, timing.stats(taskName).seconds);
        }
    }
    EXPECT_TRUE(foundNamedTask);
}

TEST(CpuTaskProfileIntegration, DisabledTimingSinkDiscardsBufferedSamples){
    using namespace __hidden_cpu_task_profile_integration_tests;
    TestArena arena;
    Perf::TimingRecorder timing(arena.arena);
    CpuTaskScheduler scheduler(0u);
    scheduler.setProfiling(true, 7u);
    ASSERT_TRUE(scheduler.submit([](){}).valid());
    scheduler.wait();
    ASSERT_GT(scheduler.statistics().profilePendingEvents, 0u);
    Perf::CollectCpuTaskProfile(scheduler, timing);
    EXPECT_EQ(scheduler.statistics().profilePendingEvents, 0u);
    EXPECT_EQ(timing.scopeCount(), 0u);
    timing.setEnabled(true);
    Perf::CollectCpuTaskProfile(scheduler, timing);
    timing.publishFrame(8u);
    EXPECT_EQ(timing.scopeCount(), 0u);
}

TEST(CpuTaskProfileIntegration, CollectorConsumesOnlyItsInitialPendingSnapshot){
    using namespace __hidden_cpu_task_profile_integration_tests;
    TestArena arena;
    Perf::TimingRecorder timing(arena.arena);
    timing.setEnabled(true);
    CpuTaskScheduler scheduler(0u);
    scheduler.setProfiling(true, 11u);
    ASSERT_TRUE(scheduler.submit([](){}).valid());
    scheduler.wait();
    const usize pendingBefore = scheduler.statistics().profilePendingEvents;
    ASSERT_GT(pendingBefore, 0u);
    OwnerThreadTimingSink sink(scheduler, timing, true);
    Perf::CollectCpuTaskProfile(scheduler, sink);
    EXPECT_EQ(sink.m_recordedSamples, pendingBefore);
    EXPECT_GT(scheduler.statistics().profilePendingEvents, 0u);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}

TEST(CpuTaskProfileIntegration, NativeWorkerSamplesAreRecordedOnTheCollectorThread){
    using namespace __hidden_cpu_task_profile_integration_tests;
    TestArena arena;
    Perf::TimingRecorder timing(arena.arena);
    timing.setEnabled(true);
    const ThreadId owner = QueryCurrentThreadId();
    ThreadId executionThread;
    CpuTaskSchedulerConfig config;
    config.workerCount = 1u;
    config.heterogeneous = false;
    CpuTaskScheduler scheduler(config);
    const Name taskName("cpu.task.tests.worker");
    const CpuTaskProfileLabel label = scheduler.registerProfileLabel(taskName);
    ASSERT_TRUE(label.valid());
    scheduler.setProfiling(true, 19u);
    const CpuTaskHandle task = scheduler.submit([&](){ executionThread = QueryCurrentThreadId(); }, { .profileLabel = label });
    ASSERT_TRUE(task.valid());
    scheduler.wait(task);
    EXPECT_NE(executionThread, owner);
    OwnerThreadTimingSink sink(scheduler, timing);
    Perf::CollectCpuTaskProfile(scheduler, sink);
    timing.publishFrame(20u);
    EXPECT_GT(sink.m_recordedSamples, 0u);
    EXPECT_EQ(timing.stats(taskName).sampleCount, 1u);
    EXPECT_EQ(timing.stats(taskName).firstSampleFrameIndex, 19u);
    EXPECT_EQ(timing.stats(taskName).publishFrameIndex, 20u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

