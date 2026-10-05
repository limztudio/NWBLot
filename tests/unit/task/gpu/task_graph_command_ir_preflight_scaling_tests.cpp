// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_command_ir_preflight_scaling_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


static void CheckMergedPacketPreflight(const usize taskCount, const bool benchmark){
    ASSERT_GE(taskCount, 4u);
    TestArena testArena;
    Graphics::GraphicsAllocator graphicsAllocator(testArena.arena);
    Core::CpuTaskScheduler cpuScheduler(0u);
    Graphics::GraphicsBackend::VulkanContext context(graphicsAllocator, cpuScheduler, 1u);
    Graphics::GraphicsBackend::VulkanAllocator allocator(context);
    Graphics::Texture* const textureObject = NewMetadataOnlyTexture(
        testArena.arena,
        context,
        allocator,
        Graphics::TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setDimension(Graphics::TextureDimension::Texture2DMS)
            .setFormat(Graphics::Format::RGBA8_UINT)
            .setSampleCount(4u)
            .setInitialState(Graphics::ResourceStates::CopyDest)
    );
    ASSERT_NE(textureObject, nullptr);
    Graphics::TextureHandle texture(textureObject, Graphics::TextureHandle::deleter_type(&testArena.arena), s_AdoptRef);
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId resource = graph.importTexture(
        texture,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/command_ir_replay/merged_preflight_texture"))
            .setMarkerLabel("Merged Preflight Texture")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::CopyDest)
    );
    ASSERT_TRUE(resource.valid());
    Graphics::GpuClearTextureTaskDesc clearDesc;
    clearDesc.destination = resource;
    clearDesc.subresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u);
    clearDesc.valueType = Graphics::GpuClearTextureTaskValueType::UInt;
    Graphics::GraphicsVector<Graphics::GpuTaskId> tasks(testArena.arena);
    tasks.reserve(taskCount);
    for(usize taskIndex = 0u; taskIndex < taskCount; ++taskIndex){
        char identityText[32u] = {};
        Graphics::GpuTaskSchedulingHint scheduling;
        scheduling.mergeWithPrevious = taskIndex != 0u;
        Graphics::GpuTaskDesc desc;
        desc
            .setIdentity(DeriveName(Name("tests/command_ir_replay/merged_preflight_task/"), FormatDecimal(taskIndex, identityText)))
            .setMarkerLabel("Merged Preflight Task")
            .setScheduling(scheduling)
            .setDependencies(tasks.empty() ? nullptr : &tasks.back(), tasks.empty() ? 0u : 1u)
        ;
        const Graphics::GpuTaskId task = graph.addClearTextureTask(desc, clearDesc);
        ASSERT_TRUE(task.valid());
        tasks.push_back(task);
    }

    SingleQueueCompile singleQueueCompile(testArena);
    ASSERT_TRUE(singleQueueCompile.compile(graph));
    const GpuTaskGraphReadViews reads(graph, singleQueueCompile.compiledGraph);
    ASSERT_TRUE(reads.valid());
    const Graphics::GpuSubmissionPacketId packet = reads.compiled.packetForTask(tasks.front());
    const Graphics::GpuCompiledPacketView packetView = reads.compiled.packet(packet);
    ASSERT_TRUE(packetView.valid());
    ASSERT_EQ(packetView.plan->taskCount, taskCount);
    ASSERT_EQ(reads.compiled.packetForTask(tasks.back()), packet);
    const Graphics::GpuPhysicalQueueId queue = packetView.plan->queue;
    Graphics::GpuCommandIrCapture capture(testArena.arena);
    for(const Graphics::GpuTaskId task : tasks){
        ASSERT_TRUE(capture.captureClearTexture(task, packet, queue, resource, clearDesc));
        ASSERT_TRUE(capture.captureClearTexture(task, packet, queue, resource, clearDesc));
    }

    const ArenaMemoryStats before = testArena.arena.memoryStats();
    u64 minimumNanoseconds = Limit<u64>::s_Max;
    for(usize iteration = 0u; iteration < (benchmark ? 4u : 1u); ++iteration){
        const Timer begin = TimerNow();
        const Graphics::GpuCommandIrReplayResult result = Graphics::PreflightGpuCommandIrPacket(
            capture.commandBytes(), reads.declarations, reads.compiled, packet
        );
        const u64 nanoseconds = DurationInNS<u64>(TimerNow(), begin);
        ASSERT_EQ(result.error, Graphics::GpuCommandIrReplayError::None);
        EXPECT_TRUE(result.streamValidation.valid());
        EXPECT_EQ(result.recordIndex, taskCount * 2u);
        if(iteration != 0u || !benchmark)
            minimumNanoseconds = Min(minimumNanoseconds, nanoseconds);
    }
    const ArenaMemoryStats after = testArena.arena.memoryStats();
    EXPECT_EQ(after.allocationCount, before.allocationCount);
    EXPECT_EQ(after.usedBytes, before.usedBytes);
    if(benchmark){
        char durationText[32u] = {};
        const AStringView formatted = FormatDecimal(minimumNanoseconds, durationText);
        durationText[formatted.size()] = '\0';
        testing::Test::RecordProperty("preflight_ns", durationText);
    }

    Graphics::GpuCommandIrCapture skipped(testArena.arena);
    ASSERT_TRUE(skipped.captureClearTexture(tasks[1u], packet, queue, resource, clearDesc));
    ASSERT_TRUE(skipped.captureClearTexture(tasks.back(), packet, queue, resource, clearDesc));
    ASSERT_TRUE(skipped.captureClearTexture(tasks.back(), packet, queue, resource, clearDesc));
    const Graphics::GpuCommandIrReplayResult skippedResult = Graphics::PreflightGpuCommandIrPacket(
        skipped.commandBytes(), reads.declarations, reads.compiled, packet
    );
    EXPECT_EQ(skippedResult.error, Graphics::GpuCommandIrReplayError::None);
    EXPECT_EQ(skippedResult.recordIndex, 3u);
    ASSERT_TRUE(skipped.captureClearTexture(tasks.front(), packet, queue, resource, clearDesc));
    const Graphics::GpuCommandIrReplayResult backwardsResult = Graphics::PreflightGpuCommandIrPacket(
        skipped.commandBytes(), reads.declarations, reads.compiled, packet
    );
    EXPECT_EQ(backwardsResult.error, Graphics::GpuCommandIrReplayError::TaskOrderMismatch);
    EXPECT_TRUE(backwardsResult.streamValidation.valid());
    EXPECT_EQ(backwardsResult.recordIndex, 3u);
}


TEST(GpuCommandIrReplay, MergedPacketPreflightPreservesRepeatedSkippedAndBackwardTaskOrder){
    CheckMergedPacketPreflight(64u, false);
}

TEST(GpuCommandIrReplay, DISABLED_PreflightBenchmark1024MergedTasks){
    CheckMergedPacketPreflight(1024u, true);
}

TEST(GpuCommandIrReplay, DISABLED_PreflightBenchmark4096MergedTasks){
    CheckMergedPacketPreflight(4096u, true);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

