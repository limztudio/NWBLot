// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/graphics/backend_selection.h>
#include <core/task/gpu/compiler.h>
#include <core/task/gpu/scheduler.h>
#include <tests/common/gpu_task_graph_read_views.h>
#include <tests/common/headless_gtest_fixture.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_scheduler_lifecycle_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core;

struct GpuSchedulerLifecycleTestConfig : HeadlessGraphicsTestConfig{};

class GpuSchedulerLifecycleTest : public HeadlessGraphicsTest<GpuSchedulerLifecycleTestConfig>{
};

struct LifecycleProbeTask{
    struct Payload{
        GpuTaskScheduler& scheduler;
        Device& device;
        u32& recordCount;
        bool& detachedDuringRecording;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(commandList);
        static_cast<void>(context);
        ++payload.recordCount;
        // A distinct producer thread exercises admission without relying on same-thread callback detection.
        Thread detachThread([&payload](){
            payload.detachedDuringRecording = payload.scheduler.detachDevice(payload.device);
        });
        detachThread.join();
        return !payload.detachedDuringRecording;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuSchedulerDetachedTest, IdleWaitSucceedsAndTokenWaitRejectsWithoutADevice){
    const GpuTaskScheduler scheduler;
    EXPECT_FALSE(scheduler.isInitialized());
    EXPECT_TRUE(scheduler.wait());
    EXPECT_FALSE(scheduler.wait(QueueSubmissionToken{}));
}

TEST_F(GpuSchedulerLifecycleTest, GraphicsBorrowsTheSameSchedulerAcrossDeviceRecreation){
    HeadlessGraphicsScope ownedScope;
    ASSERT_TRUE(ownedScope.initialize());
    GpuTaskScheduler& scheduler = ownedScope.gpuTasks();
    EXPECT_EQ(&ownedScope.graphics().gpuTasks(), &scheduler);
    ASSERT_TRUE(scheduler.isAttachedTo(ownedScope.graphics().getDevice()));
    const u16 firstDeviceGeneration = ownedScope.graphics().getDevice().getDeviceGeneration();
    ASSERT_TRUE(ownedScope.graphics().destroy());
    EXPECT_EQ(&ownedScope.graphics().gpuTasks(), &scheduler);
    EXPECT_EQ(&ownedScope.gpuTasks(), &scheduler);
    EXPECT_FALSE(scheduler.isInitialized());
    EXPECT_TRUE(scheduler.wait());

    ASSERT_TRUE(ownedScope.initialize());
    EXPECT_EQ(&ownedScope.graphics().gpuTasks(), &scheduler);
    EXPECT_EQ(&ownedScope.gpuTasks(), &scheduler);
    EXPECT_TRUE(scheduler.isAttachedTo(ownedScope.graphics().getDevice()));
    EXPECT_NE(ownedScope.graphics().getDevice().getDeviceGeneration(), firstDeviceGeneration);
    EXPECT_TRUE(scheduler.wait());
}

TEST_F(GpuSchedulerLifecycleTest, BindingRejectsAnotherDeviceUntilTheCurrentOwnerDetaches){
    Device& device = GpuSchedulerLifecycleTest::device();
    HeadlessGraphicsScope foreignScope;
    ASSERT_TRUE(foreignScope.initialize());
    Device& foreignDevice = foreignScope.graphics().getDevice();
    GpuTaskScheduler scheduler;
    ASSERT_TRUE(scheduler.attachDevice(device));
    EXPECT_TRUE(scheduler.attachDevice(device));
    EXPECT_TRUE(scheduler.isInitialized());
    EXPECT_FALSE(scheduler.attachDevice(foreignDevice));
    EXPECT_FALSE(scheduler.detachDevice(foreignDevice));
    EXPECT_TRUE(scheduler.isAttachedTo(device));
    EXPECT_FALSE(scheduler.isAttachedTo(foreignDevice));
    ASSERT_TRUE(scheduler.wait());
    ASSERT_TRUE(scheduler.detachDevice(device));
    EXPECT_FALSE(scheduler.detachDevice(device));
    EXPECT_FALSE(scheduler.isInitialized());
    ASSERT_TRUE(scheduler.attachDevice(foreignDevice));
    EXPECT_TRUE(scheduler.isAttachedTo(foreignDevice));
    EXPECT_FALSE(scheduler.isAttachedTo(device));
    ASSERT_TRUE(scheduler.wait());
    EXPECT_TRUE(scheduler.detachDevice(foreignDevice));
}

TEST_F(GpuSchedulerLifecycleTest, FailedRuntimeInitializationPreservesAnotherDevicesSchedulerBinding){
    Device& ownerDevice = GpuSchedulerLifecycleTest::device();
    HeadlessGraphicsScope rejectedScope;
    GpuTaskScheduler& scheduler = rejectedScope.gpuTasks();
    ASSERT_TRUE(scheduler.attachDevice(ownerDevice));
    EXPECT_FALSE(rejectedScope.initialize());
    EXPECT_TRUE(scheduler.isAttachedTo(ownerDevice));
    ASSERT_TRUE(rejectedScope.graphics().destroy());
    EXPECT_TRUE(scheduler.isAttachedTo(ownerDevice));
    ASSERT_TRUE(scheduler.wait());
    EXPECT_TRUE(scheduler.detachDevice(ownerDevice));
}

TEST_F(GpuSchedulerLifecycleTest, DetachedSubmissionPreservesWorkAndActiveRecordingRejectsDetach){
    Device& device = GpuSchedulerLifecycleTest::device();
    GpuTaskScheduler scheduler;
    u32 recordCount = 0u;
    bool detachedDuringRecording = true;
    GpuTaskGraph graph(GpuSchedulerLifecycleTest::arena());
    const GpuTaskId task = graph.addTask<LifecycleProbeTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/task/gpu/scheduler/lifecycle_probe"))
            .setMarkerLabel("GPU Scheduler Lifecycle Probe")
            .setQueue(GpuQueueRequest{ GpuQueueCapability::Graphics, GpuQueuePreference::Graphics, false, false }),
        LifecycleProbeTask::Payload{ scheduler, device, recordCount, detachedDuringRecording }
    );
    ASSERT_TRUE(task.valid());
    GpuTaskGraphAnalysis analysis(GpuSchedulerLifecycleTest::arena());
    GpuTaskGraphQueueAssignments assignments(GpuSchedulerLifecycleTest::arena());
    GpuCompiledGraph compiled(GpuSchedulerLifecycleTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/task/gpu/scheduler/lifecycle_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView declarations(graph);
    ASSERT_TRUE(compiler.compile(declarations, analysis, device.getPhysicalQueueTopology(), assignments, compiled, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiled);
    ASSERT_TRUE(views.valid());
    GpuRecordedGraph recorded(GpuSchedulerLifecycleTest::arena());
    GpuGraphSubmissionTransaction transaction(GpuSchedulerLifecycleTest::arena());
    transaction.reset(compiled);
    const GpuNativePacketRecorder recorder(device);
    const GpuTaskGraphNormalExecutionDesc execution;
    GpuSubmissionPacketId failedPacket = views.compiled.packetForTask(task);
    ASSERT_TRUE(failedPacket.valid());
    EXPECT_FALSE(scheduler.submit(graph, compiled, recorder, recorded, execution, transaction, scratchArena, &failedPacket));
    EXPECT_FALSE(failedPacket.valid());
    EXPECT_EQ(recordCount, 0u);
    EXPECT_FALSE(transaction.taskToken(views.compiled, task).valid());

    ASSERT_TRUE(scheduler.attachDevice(device));
    ASSERT_TRUE(scheduler.submit(graph, compiled, recorder, recorded, execution, transaction, scratchArena));
    EXPECT_EQ(recordCount, 1u);
    EXPECT_FALSE(detachedDuringRecording);
    EXPECT_TRUE(scheduler.isAttachedTo(device));
    const QueueSubmissionToken token = transaction.taskToken(views.compiled, task);
    ASSERT_TRUE(token.valid());
    ASSERT_TRUE(scheduler.wait(token));
    QueueSubmissionToken futureToken = token;
    ++futureToken.value;
    EXPECT_FALSE(scheduler.wait(futureToken));
    QueueSubmissionToken foreignToken = token;
    ++foreignToken.deviceGeneration;
    EXPECT_FALSE(scheduler.wait(foreignToken));
    EXPECT_FALSE(scheduler.wait(QueueSubmissionToken{}));
    ASSERT_TRUE(scheduler.wait());
    ASSERT_TRUE(scheduler.detachDevice(device));
    EXPECT_FALSE(scheduler.wait(token));
    EXPECT_TRUE(scheduler.wait());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

