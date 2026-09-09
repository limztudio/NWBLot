// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "round_trip_fixture.h"
#include "shaders_test_support.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Test-only embedded Vulkan 1.3 compute module. Generated in a temporary file with glslc
// --target-env=vulkan1.3 -fshader-stage=compute -O and validated by spirv-val --target-env vulkan1.3.
// It deliberately resolves the production runtime StorageBuffer table at set 0 / binding 3.
static constexpr u32 s_DescriptorHeapStorageBufferDispatchSpirv[] = {
    0x07230203u, 0x00010600u, 0x000d000bu, 0x00000026u, 0x00000000u,
    0x00020011u, 0x00000001u, 0x00020011u, 0x000014b5u, 0x00020011u, 0x000014b6u, 0x00020011u, 0x000014bcu,
    0x0006000bu, 0x00000001u, 0x4c534c47u, 0x6474732eu, 0x3035342eu, 0x00000000u,
    0x0003000eu, 0x00000000u, 0x00000001u,
    0x0008000fu, 0x00000005u, 0x00000004u, 0x6e69616du, 0x00000000u, 0x0000000bu, 0x00000014u, 0x00000017u,
    0x00060010u, 0x00000004u, 0x00000011u, 0x00000001u, 0x00000001u, 0x00000001u,
    0x00040047u, 0x0000000bu, 0x0000000bu, 0x0000001cu,
    0x00040047u, 0x00000010u, 0x00000006u, 0x00000004u,
    0x00030047u, 0x00000011u, 0x00000002u,
    0x00050048u, 0x00000011u, 0x00000000u, 0x00000023u, 0x00000000u,
    0x00040047u, 0x00000014u, 0x00000021u, 0x00000003u,
    0x00040047u, 0x00000014u, 0x00000022u, 0x00000000u,
    0x00030047u, 0x00000015u, 0x00000002u,
    0x00050048u, 0x00000015u, 0x00000000u, 0x00000023u, 0x00000000u,
    0x00050048u, 0x00000015u, 0x00000001u, 0x00000023u, 0x00000004u,
    0x00050048u, 0x00000015u, 0x00000002u, 0x00000023u, 0x00000008u,
    0x00050048u, 0x00000015u, 0x00000003u, 0x00000023u, 0x0000000cu,
    0x00030047u, 0x0000001du, 0x000014b4u,
    0x00030047u, 0x00000025u, 0x000014b4u,
    0x00020013u, 0x00000002u,
    0x00030021u, 0x00000003u, 0x00000002u,
    0x00040015u, 0x00000006u, 0x00000020u, 0x00000000u,
    0x00040017u, 0x00000009u, 0x00000006u, 0x00000003u,
    0x00040020u, 0x0000000au, 0x00000001u, 0x00000009u,
    0x0004003bu, 0x0000000au, 0x0000000bu, 0x00000001u,
    0x0004002bu, 0x00000006u, 0x0000000cu, 0x00000000u,
    0x00040020u, 0x0000000du, 0x00000001u, 0x00000006u,
    0x0003001du, 0x00000010u, 0x00000006u,
    0x0003001eu, 0x00000011u, 0x00000010u,
    0x0003001du, 0x00000012u, 0x00000011u,
    0x00040020u, 0x00000013u, 0x0000000cu, 0x00000012u,
    0x0004003bu, 0x00000013u, 0x00000014u, 0x0000000cu,
    0x0006001eu, 0x00000015u, 0x00000006u, 0x00000006u, 0x00000006u, 0x00000006u,
    0x00040020u, 0x00000016u, 0x00000009u, 0x00000015u,
    0x0004003bu, 0x00000016u, 0x00000017u, 0x00000009u,
    0x00040015u, 0x00000018u, 0x00000020u, 0x00000001u,
    0x0004002bu, 0x00000018u, 0x00000019u, 0x00000000u,
    0x00040020u, 0x0000001au, 0x00000009u, 0x00000006u,
    0x0004002bu, 0x00000018u, 0x0000001fu, 0x00000001u,
    0x00040020u, 0x00000024u, 0x0000000cu, 0x00000006u,
    0x00050036u, 0x00000002u, 0x00000004u, 0x00000000u, 0x00000003u,
    0x000200f8u, 0x00000005u,
    0x00050041u, 0x0000000du, 0x0000000eu, 0x0000000bu, 0x0000000cu,
    0x0004003du, 0x00000006u, 0x0000000fu, 0x0000000eu,
    0x00050041u, 0x0000001au, 0x0000001bu, 0x00000017u, 0x00000019u,
    0x0004003du, 0x00000006u, 0x0000001cu, 0x0000001bu,
    0x00040053u, 0x00000006u, 0x0000001du, 0x0000001cu,
    0x00050041u, 0x0000001au, 0x00000020u, 0x00000017u, 0x0000001fu,
    0x0004003du, 0x00000006u, 0x00000021u, 0x00000020u,
    0x00050080u, 0x00000006u, 0x00000023u, 0x00000021u, 0x0000000fu,
    0x00070041u, 0x00000024u, 0x00000025u, 0x00000014u, 0x0000001du, 0x00000019u, 0x0000000fu,
    0x0003003eu, 0x00000025u, 0x00000023u,
    0x000100fdu,
    0x00010038u,
};


// The production heap is the only descriptor transport: an indexed write through its runtime StorageBuffer table
// must reach a CPU-readable UAV without falling back to a classic descriptor set.
TEST_F(DescriptorBufferRoundTripTest, GlobalDescriptorHeapStorageBufferDispatchesAndReadsBack){
    constexpr u32 dispatchWordCount = 4u;
    constexpr u32 dispatchSeed = 0x4e57424cu;

    auto& device = DescriptorBufferRoundTripTest::device();
    auto& heap = device.getDescriptorHeap();
    ASSERT_TRUE(heap.isInitialized());
    ASSERT_TRUE(heap.getResourceLayout());
    ASSERT_TRUE(heap.getSamplerLayout());

    EXPECT_EQ(heap.getResourceSetIndex(), static_cast<u32>(NWB_BINDLESS_HEAP_RESOURCE_SET));
    EXPECT_EQ(heap.getSamplerSetIndex(), static_cast<u32>(NWB_BINDLESS_HEAP_SAMPLER_SET));
    EXPECT_EQ(
        heap.getRegisterSlot(GpuDescriptorClass::StorageBuffer),
        static_cast<u32>(NWB_BINDLESS_HEAP_BINDING_STORAGE_BUFFER)
    );

    BindingLayoutDesc pushLayoutDesc(DescriptorBufferRoundTripTest::arena());
    pushLayoutDesc
        .setVisibility(ShaderType::Compute)
        .addItem(BindingLayoutItem::PushConstants(0u, sizeof(DescriptorHeapStorageBufferDispatchPushConstants)))
    ;
    auto pushLayout = device.createBindingLayout(pushLayoutDesc);
    ASSERT_TRUE(pushLayout);
    EXPECT_TRUE(pushLayout->isDescriptorBufferCompatible());
    EXPECT_EQ(pushLayout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::None);
    EXPECT_TRUE(pushLayout->getDescriptorBufferBindingOffsets().empty());

    ShaderDesc shaderDesc(DescriptorBufferRoundTripTest::arena());
    shaderDesc
        .setShaderType(ShaderType::Compute)
        .setDebugName(Name{"tests/descriptor_buffer/global_heap_storage_dispatch"})
    ;
    auto shader = device.createShader(
        shaderDesc,
        s_DescriptorHeapStorageBufferDispatchSpirv,
        sizeof(s_DescriptorHeapStorageBufferDispatchSpirv)
    );
    ASSERT_TRUE(shader);

    ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(shader)
        .addBindingLayout(pushLayout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    auto pipeline = device.createComputePipeline(pipelineDesc);
    ASSERT_TRUE(pipeline);

    auto storageBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setCanHaveUAVs(true)
            .setCpuAccess(CpuAccessMode::Read)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(storageBuffer);

    const GpuDescriptorHandle storageBufferHandle = heap.allocate(GpuDescriptorClass::StorageBuffer);
    ASSERT_TRUE(storageBufferHandle.valid());
    EXPECT_EQ(storageBufferHandle.descriptorClass(), GpuDescriptorClass::StorageBuffer);
    ASSERT_TRUE(heap.write(storageBufferHandle, DescriptorWriteItem::RawBuffer_UAV(0u, storageBuffer.get())));

    const DescriptorHeapStorageBufferDispatchPushConstants pushConstants{
        storageBufferHandle.slot(),
        dispatchSeed,
        0u,
        0u
    };
    auto commandList = device.createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    commandList->setBufferState(storageBuffer.get(), ResourceStates::UnorderedAccess);

    ComputeState computeState;
    computeState.setPipeline(pipeline.get());
    commandList->setComputeState(computeState);
    heap.bindCompute(*commandList, *pipeline);
    commandList->setPushConstants(&pushConstants, sizeof(pushConstants));
    commandList->dispatch(dispatchWordCount, 1u, 1u);
    commandList->close();
    ASSERT_TRUE(commandList->hasCommandBuffer());

    CommandList* commandLists[] = { commandList.get() };
    const QueueSubmissionToken submissionToken = device.executeCommandLists(
        commandLists,
        LengthOf(commandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(submissionToken.valid());
    const GpuPhysicalQueueId primaryGraphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_TRUE(primaryGraphicsQueue.valid());
    EXPECT_EQ(submissionToken.queue, CommandQueue::Graphics);
    EXPECT_TRUE(submissionToken.matchesPhysicalQueue(
        primaryGraphicsQueue.index,
        primaryGraphicsQueue.deviceGeneration
    ));
    ASSERT_TRUE(device.waitForIdle());

    const u32* const outputWords = static_cast<const u32*>(device.mapBuffer(*storageBuffer, CpuAccessMode::Read));
    ASSERT_NE(outputWords, nullptr);
    for(u32 wordIndex = 0u; wordIndex < dispatchWordCount; ++wordIndex)
        EXPECT_EQ(outputWords[wordIndex], dispatchSeed + wordIndex);
    device.unmapBuffer(*storageBuffer);

    heap.free(storageBufferHandle);
    heap.collectRetired();
}


// A heap binding belongs to its recording command buffer, not to a CPU frame. A free therefore stays pinned while
// that command buffer is unsubmitted, becomes reclaimable when an injected native-submit failure abandons it, and
// follows the accepted queue token through completion on the normal path.
TEST_F(DescriptorBufferRoundTripTest, DescriptorHeapRetirementTracksCommandBufferSubmissionAndAbandonment){
    auto& device = DescriptorBufferRoundTripTest::device();

    auto& heap = device.getDescriptorHeap();
    ASSERT_TRUE(heap.isInitialized());

    const GpuDescriptorHeapLifecycleStatistics initialStatistics = heap.lifecycleStatistics();
    EXPECT_TRUE(initialStatistics.initialized);
    EXPECT_GT(initialStatistics.resourceCapacity, 1u);
    EXPECT_GT(initialStatistics.samplerCapacity, 0u);
    if(heap.hasAccelStructLayout())
        EXPECT_GT(initialStatistics.accelStructCapacity, 0u);
    else
        EXPECT_EQ(initialStatistics.accelStructCapacity, 0u);
    EXPECT_EQ(initialStatistics.resourceLiveSlotCount, 0u);
    EXPECT_EQ(initialStatistics.samplerLiveSlotCount, 0u);
    EXPECT_EQ(initialStatistics.accelStructLiveSlotCount, 0u);
    EXPECT_EQ(initialStatistics.pendingRetiredSlotCount, 0u);
    EXPECT_EQ(initialStatistics.acceptedHeapUseCount, 0u);
    EXPECT_EQ(initialStatistics.unsubmittedHeapUseCount, 0u);
    EXPECT_EQ(initialStatistics.abandonedHeapUseCount, 0u);

    ShaderDesc shaderDesc(DescriptorBufferRoundTripTest::arena());
    shaderDesc
        .setShaderType(ShaderType::Compute)
        .setDebugName(Name{"tests/descriptor_buffer/heap_retirement"})
    ;
    auto shader = device.createShader(shaderDesc, s_DescriptorHeapRetirementComputeSpirv, sizeof(s_DescriptorHeapRetirementComputeSpirv));
    ASSERT_TRUE(shader);

    ComputePipelineDesc pipelineDesc;
    pipelineDesc
        .setComputeShader(shader)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    auto pipeline = device.createComputePipeline(pipelineDesc);
    ASSERT_TRUE(pipeline);

    auto storageBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(4096u)
            .setStructStride(16u)
            .setCanHaveRawViews(true)
            .setCanHaveUAVs(true)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    ASSERT_TRUE(storageBuffer);

    const GpuDescriptorHandle unsubmittedHandle = heap.allocate(GpuDescriptorClass::StorageBuffer);
    ASSERT_TRUE(unsubmittedHandle.valid());
    ASSERT_TRUE(heap.write(unsubmittedHandle, DescriptorWriteItem::StructuredBuffer_UAV(0u, storageBuffer.get())));
    const GpuDescriptorHeapLifecycleStatistics allocatedStatistics = heap.lifecycleStatistics();
    EXPECT_EQ(allocatedStatistics.resourceLiveSlotCount, 1u);
    EXPECT_EQ(allocatedStatistics.samplerLiveSlotCount, 0u);
    EXPECT_EQ(allocatedStatistics.accelStructLiveSlotCount, 0u);
    EXPECT_EQ(allocatedStatistics.pendingRetiredSlotCount, 0u);
    EXPECT_EQ(allocatedStatistics.acceptedHeapUseCount, 0u);
    EXPECT_EQ(allocatedStatistics.unsubmittedHeapUseCount, 0u);
    EXPECT_EQ(storageBuffer->getReferenceCount(), 2u);

    auto unsubmittedCommandList = device.createCommandList();
    ASSERT_TRUE(unsubmittedCommandList);
    unsubmittedCommandList->open();
    ComputeState unsubmittedComputeState;
    unsubmittedComputeState.setPipeline(pipeline.get());
    unsubmittedCommandList->setComputeState(unsubmittedComputeState);
    heap.bindCompute(*unsubmittedCommandList, *pipeline);
    unsubmittedCommandList->close();
    ASSERT_TRUE(unsubmittedCommandList->hasCommandBuffer());
    const GpuDescriptorHeapLifecycleStatistics unsubmittedStatistics = heap.lifecycleStatistics();
    EXPECT_EQ(unsubmittedStatistics.resourceLiveSlotCount, 1u);
    EXPECT_EQ(unsubmittedStatistics.pendingRetiredSlotCount, 0u);
    EXPECT_EQ(unsubmittedStatistics.acceptedHeapUseCount, 0u);
    EXPECT_EQ(unsubmittedStatistics.unsubmittedHeapUseCount, 1u);
    EXPECT_EQ(unsubmittedStatistics.abandonedHeapUseCount, 0u);

    heap.free(unsubmittedHandle);
    heap.collectRetired();
    const GpuDescriptorHeapLifecycleStatistics retiredUnsubmittedStatistics = heap.lifecycleStatistics();
    EXPECT_EQ(retiredUnsubmittedStatistics.resourceLiveSlotCount, 0u);
    EXPECT_EQ(retiredUnsubmittedStatistics.pendingRetiredSlotCount, 1u);
    EXPECT_EQ(retiredUnsubmittedStatistics.acceptedHeapUseCount, 0u);
    EXPECT_EQ(retiredUnsubmittedStatistics.unsubmittedHeapUseCount, 1u);
    EXPECT_EQ(retiredUnsubmittedStatistics.abandonedHeapUseCount, 0u);
    EXPECT_EQ(storageBuffer->getReferenceCount(), 2u)
        << "an unsubmitted command buffer lost its descriptor resource";

    const GpuDescriptorHandle heldSlot = heap.allocate(GpuDescriptorClass::StorageBuffer);
    ASSERT_TRUE(heldSlot.valid());
    EXPECT_NE(heldSlot.slot(), unsubmittedHandle.slot())
        << "an unsubmitted command buffer allowed its descriptor slot to be recycled";
    const GpuDescriptorHeapLifecycleStatistics heldSlotStatistics = heap.lifecycleStatistics();
    EXPECT_EQ(heldSlotStatistics.resourceLiveSlotCount, 1u);
    EXPECT_EQ(heldSlotStatistics.pendingRetiredSlotCount, 1u);
    EXPECT_EQ(heldSlotStatistics.acceptedHeapUseCount, 0u);
    EXPECT_EQ(heldSlotStatistics.unsubmittedHeapUseCount, 1u);
    heap.free(heldSlot);

    device.runGarbageCollection();
    heap.collectRetired();
    const GpuDescriptorHeapLifecycleStatistics garbageCollectedUnsubmittedStatistics = heap.lifecycleStatistics();
    EXPECT_EQ(garbageCollectedUnsubmittedStatistics.resourceLiveSlotCount, 0u);
    EXPECT_EQ(garbageCollectedUnsubmittedStatistics.pendingRetiredSlotCount, 2u);
    EXPECT_EQ(garbageCollectedUnsubmittedStatistics.acceptedHeapUseCount, 0u);
    EXPECT_EQ(garbageCollectedUnsubmittedStatistics.unsubmittedHeapUseCount, 1u);
    EXPECT_EQ(storageBuffer->getReferenceCount(), 2u)
        << "CPU garbage collection released an unsubmitted descriptor heap use";

    CommandList* unsubmittedCommandLists[] = { unsubmittedCommandList.get() };
    bool unsubmittedAccepted = true;
    {
        const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
        ASSERT_TRUE(graphicsQueue.valid());
        const VkQueue nativeGraphicsQueue = static_cast<VkQueue>(
            device.getNativeQueue(GraphicsBackend::ObjectTypes::VK_Queue, graphicsQueue).pointer()
        );
        ASSERT_NE(nativeGraphicsQueue, VK_NULL_HANDLE);
        VulkanTestQueueSubmit2Observer submissionObserver(device);
        ASSERT_TRUE(submissionObserver.valid());
        ASSERT_TRUE(submissionObserver.armSubmissionFailures(nativeGraphicsQueue));

        device.executeCommandLists(unsubmittedCommandLists, 1u, CommandQueue::Graphics, &unsubmittedAccepted);
        EXPECT_FALSE(unsubmittedAccepted);
        EXPECT_EQ(submissionObserver.injectedSubmissionFailureCount(), 1u);
        EXPECT_EQ(submissionObserver.pendingSubmissionFailureCount(), 0u);
    }

    const GpuDescriptorHeapLifecycleStatistics rejectedStatistics = heap.lifecycleStatistics();
    EXPECT_EQ(rejectedStatistics.resourceLiveSlotCount, 0u);
    EXPECT_EQ(rejectedStatistics.pendingRetiredSlotCount, 2u);
    EXPECT_EQ(rejectedStatistics.acceptedHeapUseCount, 0u);
    EXPECT_EQ(rejectedStatistics.unsubmittedHeapUseCount, 0u);
    EXPECT_EQ(rejectedStatistics.abandonedHeapUseCount, 1u);

    heap.collectRetired();
    const GpuDescriptorHeapLifecycleStatistics abandonedStatistics = heap.lifecycleStatistics();
    EXPECT_EQ(abandonedStatistics.resourceLiveSlotCount, 0u);
    EXPECT_EQ(abandonedStatistics.pendingRetiredSlotCount, 0u);
    EXPECT_EQ(abandonedStatistics.acceptedHeapUseCount, 0u);
    EXPECT_EQ(abandonedStatistics.unsubmittedHeapUseCount, 0u);
    EXPECT_EQ(abandonedStatistics.abandonedHeapUseCount, 0u);
    EXPECT_EQ(storageBuffer->getReferenceCount(), 1u)
        << "an injected native-submit failure did not release the abandoned descriptor heap use";

    const GpuDescriptorHandle recycledFirst = heap.allocate(GpuDescriptorClass::StorageBuffer);
    const GpuDescriptorHandle recycledSecond = heap.allocate(GpuDescriptorClass::StorageBuffer);
    ASSERT_TRUE(recycledFirst.valid());
    ASSERT_TRUE(recycledSecond.valid());
    EXPECT_TRUE(recycledFirst.slot() == unsubmittedHandle.slot() || recycledSecond.slot() == unsubmittedHandle.slot())
        << "an injected native-submit failure did not return the abandoned descriptor slot";
    const GpuDescriptorHeapLifecycleStatistics recycledStatistics = heap.lifecycleStatistics();
    EXPECT_EQ(recycledStatistics.resourceLiveSlotCount, 2u);
    EXPECT_EQ(recycledStatistics.pendingRetiredSlotCount, 0u);
    EXPECT_EQ(recycledStatistics.acceptedHeapUseCount, 0u);
    EXPECT_EQ(recycledStatistics.unsubmittedHeapUseCount, 0u);

    heap.free(recycledFirst);
    heap.free(recycledSecond);
    heap.collectRetired();
    const GpuDescriptorHeapLifecycleStatistics clearedStatistics = heap.lifecycleStatistics();
    EXPECT_EQ(clearedStatistics.resourceLiveSlotCount, 0u);
    EXPECT_EQ(clearedStatistics.pendingRetiredSlotCount, 0u);
    EXPECT_EQ(clearedStatistics.acceptedHeapUseCount, 0u);
    EXPECT_EQ(clearedStatistics.unsubmittedHeapUseCount, 0u);

    const GpuDescriptorHandle acceptedHandle = heap.allocate(GpuDescriptorClass::StorageBuffer);
    ASSERT_TRUE(acceptedHandle.valid());
    ASSERT_TRUE(heap.write(acceptedHandle, DescriptorWriteItem::StructuredBuffer_UAV(0u, storageBuffer.get())));

    auto acceptedCommandList = device.createCommandList();
    ASSERT_TRUE(acceptedCommandList);
    acceptedCommandList->open();
    ComputeState acceptedComputeState;
    acceptedComputeState.setPipeline(pipeline.get());
    acceptedCommandList->setComputeState(acceptedComputeState);
    heap.bindCompute(*acceptedCommandList, *pipeline);
    acceptedCommandList->close();
    ASSERT_TRUE(acceptedCommandList->hasCommandBuffer());
    const GpuDescriptorHeapLifecycleStatistics acceptedUnsubmittedStatistics = heap.lifecycleStatistics();
    EXPECT_EQ(acceptedUnsubmittedStatistics.resourceLiveSlotCount, 1u);
    EXPECT_EQ(acceptedUnsubmittedStatistics.pendingRetiredSlotCount, 0u);
    EXPECT_EQ(acceptedUnsubmittedStatistics.acceptedHeapUseCount, 0u);
    EXPECT_EQ(acceptedUnsubmittedStatistics.unsubmittedHeapUseCount, 1u);

    heap.free(acceptedHandle);
    heap.collectRetired();
    const GpuDescriptorHeapLifecycleStatistics acceptedRetiredStatistics = heap.lifecycleStatistics();
    EXPECT_EQ(acceptedRetiredStatistics.resourceLiveSlotCount, 0u);
    EXPECT_EQ(acceptedRetiredStatistics.pendingRetiredSlotCount, 1u);
    EXPECT_EQ(acceptedRetiredStatistics.acceptedHeapUseCount, 0u);
    EXPECT_EQ(acceptedRetiredStatistics.unsubmittedHeapUseCount, 1u);
    EXPECT_EQ(storageBuffer->getReferenceCount(), 2u)
        << "an unsubmitted accepted-path command buffer lost its descriptor resource";

    CommandList* acceptedCommandLists[] = { acceptedCommandList.get() };
    const QueueSubmissionToken acceptedToken = device.executeCommandLists(
        acceptedCommandLists,
        1u,
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(acceptedToken.valid());

    // Accepted tokens remain visible until collection, independent of whether this no-op submission has already
    // completed on a fast adapter. This avoids conflating submission acceptance with physical queue timing.
    const GpuDescriptorHeapLifecycleStatistics acceptedStatistics = heap.lifecycleStatistics();
    EXPECT_EQ(acceptedStatistics.resourceLiveSlotCount, 0u);
    EXPECT_EQ(acceptedStatistics.pendingRetiredSlotCount, 1u);
    EXPECT_EQ(acceptedStatistics.acceptedHeapUseCount, 1u);
    EXPECT_EQ(acceptedStatistics.unsubmittedHeapUseCount, 0u);
    EXPECT_EQ(acceptedStatistics.abandonedHeapUseCount, 0u);

    ASSERT_TRUE(device.waitForIdle());

    heap.collectRetired();
    const GpuDescriptorHeapLifecycleStatistics completedStatistics = heap.lifecycleStatistics();
    EXPECT_TRUE(completedStatistics.initialized);
    EXPECT_EQ(completedStatistics.resourceLiveSlotCount, 0u);
    EXPECT_EQ(completedStatistics.pendingRetiredSlotCount, 0u);
    EXPECT_EQ(completedStatistics.acceptedHeapUseCount, 0u);
    EXPECT_EQ(completedStatistics.unsubmittedHeapUseCount, 0u);
    EXPECT_EQ(completedStatistics.abandonedHeapUseCount, 0u);
    EXPECT_EQ(storageBuffer->getReferenceCount(), 1u)
        << "a completed queue submission did not release its descriptor heap use";

}


// Descriptor retirement follows the exact physical queue that bound the heap. A same-family auxiliary Graphics
// queue has its own timeline, so querying the primary Graphics timeline must not retain its completed descriptor.
TEST_F(DescriptorBufferRoundTripTest, DescriptorHeapRetirementTracksAuxiliaryGraphicsQueueCompletion){
    HeadlessGraphicsScope multiQueueScope;
    ASSERT_TRUE(multiQueueScope.setSameClassMultiQueueEnabled(true));
    if(!multiQueueScope.initialize())
        GTEST_SKIP() << "Descriptor-heap auxiliary Graphics retirement: no usable headless Vulkan device on this host.";

    auto& device = multiQueueScope.graphics().getDevice();
    if(!device.getDescriptorBufferManager().isEnabled())
        GTEST_SKIP() << "Descriptor-heap auxiliary Graphics retirement: VK_EXT_descriptor_buffer is unavailable on this device.";

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    const GpuPhysicalQueueId primaryGraphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    const u32 primaryGraphicsFamily = device.getQueueFamilyIndex(primaryGraphicsQueue);
    const GpuPhysicalQueueInfo* auxiliaryGraphicsQueue = nullptr;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
        if(
            candidate.queueClass == CommandQueue::Graphics
            && candidate.id != primaryGraphicsQueue
            && candidate.familyIndex == primaryGraphicsFamily
            && (static_cast<u8>(candidate.capabilities) & static_cast<u8>(GpuQueueCapability::Compute)) != 0u
        ){
            auxiliaryGraphicsQueue = &candidate;
            break;
        }
    }
    if(!auxiliaryGraphicsQueue)
        GTEST_SKIP() << "Descriptor-heap auxiliary Graphics retirement: adapter exposes no same-family Graphics queue.";

    auto& heap = device.getDescriptorHeap();
    ASSERT_TRUE(heap.isInitialized());

    const GpuDescriptorHeapLifecycleStatistics initialStatistics = heap.lifecycleStatistics();
    EXPECT_TRUE(initialStatistics.initialized);
    EXPECT_GT(initialStatistics.resourceCapacity, 1u);
    EXPECT_GT(initialStatistics.samplerCapacity, 0u);
    if(heap.hasAccelStructLayout())
        EXPECT_GT(initialStatistics.accelStructCapacity, 0u);
    else
        EXPECT_EQ(initialStatistics.accelStructCapacity, 0u);
    EXPECT_EQ(initialStatistics.resourceLiveSlotCount, 0u);
    EXPECT_EQ(initialStatistics.samplerLiveSlotCount, 0u);
    EXPECT_EQ(initialStatistics.accelStructLiveSlotCount, 0u);
    EXPECT_EQ(initialStatistics.pendingRetiredSlotCount, 0u);
    EXPECT_EQ(initialStatistics.acceptedHeapUseCount, 0u);
    EXPECT_EQ(initialStatistics.unsubmittedHeapUseCount, 0u);
    EXPECT_EQ(initialStatistics.abandonedHeapUseCount, 0u);

    ShaderDesc shaderDesc(multiQueueScope.arena());
    shaderDesc
        .setShaderType(ShaderType::Compute)
        .setDebugName(Name{"tests/descriptor_buffer/auxiliary_graphics_heap_retirement"})
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
    ;
    auto pipeline = device.createComputePipeline(pipelineDesc);
    ASSERT_TRUE(pipeline);

    auto storageBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(4096u)
            .setStructStride(16u)
            .setCanHaveRawViews(true)
            .setCanHaveUAVs(true)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    ASSERT_TRUE(storageBuffer);

    const GpuDescriptorHandle handle = heap.allocate(GpuDescriptorClass::StorageBuffer);
    ASSERT_TRUE(handle.valid());
    ASSERT_TRUE(heap.write(handle, DescriptorWriteItem::StructuredBuffer_UAV(0u, storageBuffer.get())));
    EXPECT_EQ(storageBuffer->getReferenceCount(), 2u);

    const GpuDescriptorHeapLifecycleStatistics allocatedStatistics = heap.lifecycleStatistics();
    EXPECT_EQ(allocatedStatistics.resourceLiveSlotCount, 1u);
    EXPECT_EQ(allocatedStatistics.samplerLiveSlotCount, 0u);
    EXPECT_EQ(allocatedStatistics.accelStructLiveSlotCount, 0u);
    EXPECT_EQ(allocatedStatistics.pendingRetiredSlotCount, 0u);
    EXPECT_EQ(allocatedStatistics.acceptedHeapUseCount, 0u);
    EXPECT_EQ(allocatedStatistics.unsubmittedHeapUseCount, 0u);
    EXPECT_EQ(allocatedStatistics.abandonedHeapUseCount, 0u);

    CommandListParameters parameters;
    parameters.setPhysicalQueue(auxiliaryGraphicsQueue->id);
    auto commandList = device.createCommandList(parameters);
    ASSERT_TRUE(commandList);
    commandList->open();
    ComputeState computeState;
    computeState.setPipeline(pipeline.get());
    commandList->setComputeState(computeState);
    heap.bindCompute(*commandList, *pipeline);
    commandList->close();
    ASSERT_TRUE(commandList->hasCommandBuffer());

    const GpuDescriptorHeapLifecycleStatistics recordedStatistics = heap.lifecycleStatistics();
    EXPECT_EQ(recordedStatistics.resourceLiveSlotCount, 1u);
    EXPECT_EQ(recordedStatistics.samplerLiveSlotCount, 0u);
    EXPECT_EQ(recordedStatistics.accelStructLiveSlotCount, 0u);
    EXPECT_EQ(recordedStatistics.pendingRetiredSlotCount, 0u);
    EXPECT_EQ(recordedStatistics.acceptedHeapUseCount, 0u);
    EXPECT_EQ(recordedStatistics.unsubmittedHeapUseCount, 1u);
    EXPECT_EQ(recordedStatistics.abandonedHeapUseCount, 0u);

    heap.free(handle);
    heap.collectRetired();
    const GpuDescriptorHeapLifecycleStatistics retiredStatistics = heap.lifecycleStatistics();
    EXPECT_EQ(retiredStatistics.resourceLiveSlotCount, 0u);
    EXPECT_EQ(retiredStatistics.samplerLiveSlotCount, 0u);
    EXPECT_EQ(retiredStatistics.accelStructLiveSlotCount, 0u);
    EXPECT_EQ(retiredStatistics.pendingRetiredSlotCount, 1u);
    EXPECT_EQ(retiredStatistics.acceptedHeapUseCount, 0u);
    EXPECT_EQ(retiredStatistics.unsubmittedHeapUseCount, 1u);
    EXPECT_EQ(retiredStatistics.abandonedHeapUseCount, 0u);
    EXPECT_EQ(storageBuffer->getReferenceCount(), 2u)
        << "an unsubmitted auxiliary command buffer lost its descriptor resource";

    CommandList* commandLists[] = { commandList.get() };
    const QueueSubmissionToken submissionToken = device.executeCommandLists(
        commandLists,
        1u,
        auxiliaryGraphicsQueue->id,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(submissionToken.valid());
    ASSERT_TRUE(submissionToken.matchesPhysicalQueue(
        auxiliaryGraphicsQueue->id.index,
        auxiliaryGraphicsQueue->id.deviceGeneration
    ));

    // Lifecycle telemetry stays aggregate: acceptance is visible without exposing an individual queue identity.
    const GpuDescriptorHeapLifecycleStatistics acceptedStatistics = heap.lifecycleStatistics();
    EXPECT_EQ(acceptedStatistics.resourceLiveSlotCount, 0u);
    EXPECT_EQ(acceptedStatistics.samplerLiveSlotCount, 0u);
    EXPECT_EQ(acceptedStatistics.accelStructLiveSlotCount, 0u);
    EXPECT_EQ(acceptedStatistics.pendingRetiredSlotCount, 1u);
    EXPECT_EQ(acceptedStatistics.acceptedHeapUseCount, 1u);
    EXPECT_EQ(acceptedStatistics.unsubmittedHeapUseCount, 0u);
    EXPECT_EQ(acceptedStatistics.abandonedHeapUseCount, 0u);
    ASSERT_TRUE(device.waitForIdle());

    heap.collectRetired();
    const GpuDescriptorHeapLifecycleStatistics completedStatistics = heap.lifecycleStatistics();
    EXPECT_TRUE(completedStatistics.initialized);
    EXPECT_EQ(completedStatistics.resourceCapacity, initialStatistics.resourceCapacity);
    EXPECT_EQ(completedStatistics.samplerCapacity, initialStatistics.samplerCapacity);
    EXPECT_EQ(completedStatistics.accelStructCapacity, initialStatistics.accelStructCapacity);
    EXPECT_EQ(completedStatistics.resourceLiveSlotCount, 0u);
    EXPECT_EQ(completedStatistics.samplerLiveSlotCount, 0u);
    EXPECT_EQ(completedStatistics.accelStructLiveSlotCount, 0u);
    EXPECT_EQ(completedStatistics.pendingRetiredSlotCount, 0u);
    EXPECT_EQ(completedStatistics.acceptedHeapUseCount, 0u);
    EXPECT_EQ(completedStatistics.unsubmittedHeapUseCount, 0u);
    EXPECT_EQ(completedStatistics.abandonedHeapUseCount, 0u);
    EXPECT_EQ(storageBuffer->getReferenceCount(), 1u)
        << "descriptor retirement queried the primary Graphics timeline instead of the auxiliary physical queue";

    const GpuDescriptorHandle recycled = heap.allocate(GpuDescriptorClass::StorageBuffer);
    ASSERT_TRUE(recycled.valid());
    EXPECT_EQ(recycled.slot(), handle.slot());
    heap.free(recycled);
    heap.collectRetired();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

