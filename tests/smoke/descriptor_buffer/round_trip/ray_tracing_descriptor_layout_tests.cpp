// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "round_trip_fixture.h"
#include "shaders_test_support.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// A ray-tracing-capable device must expose the global TLAS through the required immutable descriptor-heap layout.
// The renderer no longer has a supported local TLAS path, so a device that exposes RT but fails to create set 2
// is a contract failure rather than a reason to skip the HW trace paths.
TEST_F(DescriptorBufferRoundTripTest, RayTracingHeapRequiresDescriptorBufferTlasLayout){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& heap = device.getDescriptorHeap();
    ASSERT_TRUE(heap.isInitialized());

    if(!device.queryFeatureSupport(Feature::RayTracingAccelStruct))
        GTEST_SKIP() << "Ray tracing acceleration structures are not enabled on this device.";

    ASSERT_TRUE(heap.hasAccelStructLayout())
        << "ray-tracing-capable device has no required descriptor-buffer TLAS layout";
    const auto* const tlasLayout = heap.getAccelStructLayout().get();
    ASSERT_NE(tlasLayout, nullptr);
    EXPECT_EQ(heap.getAccelStructSetIndex(), NWB_BINDLESS_HEAP_ACCEL_STRUCT_SET);
    EXPECT_TRUE(tlasLayout->isDescriptorBufferCompatible());
    ASSERT_NE(tlasLayout->getBindlessDesc(), nullptr);
    EXPECT_EQ(tlasLayout->getBindlessDesc()->layoutType, BindlessLayoutType::Immutable);
    EXPECT_EQ(tlasLayout->getBindlessDesc()->maxCapacity, 1u);
    EXPECT_EQ(tlasLayout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::Resource);
    EXPECT_GT(tlasLayout->getDescriptorBufferSetSizeBytes(), 0u);
    EXPECT_NE(
        tlasLayout->getDescriptorBufferBindingOffsets().find(NWB_BINDLESS_HEAP_BINDING_ACCEL_STRUCT),
        tlasLayout->getDescriptorBufferBindingOffsets().end()
    );
}


// The descriptor-buffer TLAS surface is deliberately an immutable one-descriptor global-heap layout rather than an
// mutable descriptor array: an AccelStruct handle selects its own carved block, letting a replacement TLAS coexist
// with the block referenced by an in-flight frame. Exercise the actual heap write path rather than a standalone descriptor object on
// RT-capable devices.
TEST_F(DescriptorBufferRoundTripTest, GlobalDescriptorHeapWritesImmutableTlasBlock){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& heap = device.getDescriptorHeap();
    ASSERT_TRUE(heap.isInitialized());
    if(!device.queryFeatureSupport(Feature::RayTracingAccelStruct))
        GTEST_SKIP() << "Ray tracing acceleration structures are not enabled on this device.";
    ASSERT_TRUE(heap.hasAccelStructLayout())
        << "ray-tracing-capable device has no required descriptor-buffer TLAS layout";

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/heap_tlas_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    RayTracingAccelStructDesc tlasDesc(descArena);
    tlasDesc.setTopLevelMaxInstances(8u);
    tlasDesc.setDebugName(Name{"tests/descriptor_buffer/heap_tlas"});
    auto tlas = device.createAccelStruct(tlasDesc);
    ASSERT_NE(tlas.get(), nullptr);

    const GpuDescriptorHandle handle = heap.allocate(GpuDescriptorClass::AccelStruct);
    ASSERT_TRUE(handle.valid());
    ASSERT_TRUE(heap.write(handle, DescriptorWriteItem::RayTracingAccelStruct(0u, tlas.get())));

    const auto block = heap.getAccelStructBufferBlock(handle);
    EXPECT_TRUE(block.valid());
    EXPECT_EQ(block.kind, GraphicsBackend::DescriptorBufferSegmentKind::Resource);
    EXPECT_GT(block.sizeBytes, 0u);

    ShaderDesc shaderDesc(descArena);
    shaderDesc
        .setShaderType(ShaderType::Compute)
        .setDebugName(Name{"tests/descriptor_buffer/heap_pending_tlas"})
    ;
    auto shader = device.createShader(
        shaderDesc,
        s_DescriptorHeapRetirementComputeSpirv,
        sizeof(s_DescriptorHeapRetirementComputeSpirv)
    );
    ASSERT_TRUE(shader);

    ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(shader)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
        .addBindingLayout(heap.getAccelStructLayout())
    ;
    auto pipeline = device.createComputePipeline(pipelineDesc);
    ASSERT_TRUE(pipeline);

    auto commandList = device.createCommandList();
    ASSERT_TRUE(commandList);
    {
        auto pendingRecordingLease = heap.acquirePendingRecordingLease();
        ASSERT_TRUE(pendingRecordingLease.valid());
        heap.free(handle);
        EXPECT_FALSE(heap.getAccelStructBufferBlock(handle).valid())
            << "the public TLAS block query exposed a freed pending-recording handle";

        commandList->open();
        ComputeState computeState;
        computeState.setPipeline(pipeline.get());
        commandList->setComputeState(computeState);
        heap.bindCompute(*commandList, *pipeline, handle);
        ASSERT_FALSE(commandList->commandRecordingFailed())
            << "native TLAS binding rejected the exact pending-recording generation";
        commandList->close();
        ASSERT_FALSE(commandList->commandRecordingFailed())
            << "native TLAS binding became invalid while closing its pending-recording heap use";
        ASSERT_TRUE(commandList->hasCommandBuffer())
            << "native TLAS binding rejected the exact pending-recording generation";
    }

    const GpuDescriptorHeapLifecycleStatistics recordedStatistics = heap.lifecycleStatistics();
    EXPECT_EQ(recordedStatistics.pendingRetiredSlotCount, 1u);
    EXPECT_EQ(recordedStatistics.unsubmittedHeapUseCount, 1u);

    CommandList* commandLists[] = { commandList.get() };
    const QueueSubmissionToken submissionToken = device.executeCommandLists(
        commandLists,
        LengthOf(commandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(submissionToken.valid());
    ASSERT_TRUE(device.waitForIdle());
    heap.collectRetired();
    EXPECT_EQ(heap.lifecycleStatistics().pendingRetiredSlotCount, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

