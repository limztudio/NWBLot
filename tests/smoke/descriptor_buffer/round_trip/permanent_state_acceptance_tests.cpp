// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DescriptorBufferRoundTripDetail{};
namespace __hidden_descriptor_buffer_round_trip_tests = DescriptorBufferRoundTripDetail;


namespace DescriptorBufferRoundTripDetail{

class CallerOwnedRetainedStateBuffer final : NoCopy{
private:
    [[nodiscard]] static Object encode(const VkBuffer buffer)noexcept{
#if VK_USE_64_BIT_PTR_DEFINES
        return Object(static_cast<void*>(buffer));
#else
        return Object(static_cast<u64>(buffer));
#endif
    }


public:
    CallerOwnedRetainedStateBuffer(
        GraphicsBackend::Device& device,
        const u64 byteSize,
        const VkBufferUsageFlags usage
    )
        : m_context(VulkanTestDeviceProbe::capture(device))
    {
        if(
            !m_context.valid()
            || byteSize == 0u
            || usage == 0u
            || !m_context.deviceDispatch->vkCreateBuffer
            || !m_context.deviceDispatch->vkDestroyBuffer
            || !m_context.deviceDispatch->vkGetBufferMemoryRequirements
            || !m_context.deviceDispatch->vkAllocateMemory
            || !m_context.deviceDispatch->vkFreeMemory
            || !m_context.deviceDispatch->vkBindBufferMemory
        )
            return;

        const VkBufferCreateInfo bufferInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0u,
            .size = byteSize,
            .usage = usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0u,
            .pQueueFamilyIndices = nullptr,
        };
        if(m_context.deviceDispatch->vkCreateBuffer(m_context.device, &bufferInfo, m_context.allocationCallbacks, &m_buffer) != VK_SUCCESS){
            m_buffer = VK_NULL_HANDLE;
            return;
        }

        VkMemoryRequirements memoryRequirements{};
        m_context.deviceDispatch->vkGetBufferMemoryRequirements(m_context.device, m_buffer, &memoryRequirements);
        u32 memoryTypeIndex = VK_MAX_MEMORY_TYPES;
        for(u32 candidateIndex = 0u; candidateIndex < VK_MAX_MEMORY_TYPES; ++candidateIndex){
            if((memoryRequirements.memoryTypeBits & (1u << candidateIndex)) != 0u){
                memoryTypeIndex = candidateIndex;
                break;
            }
        }
        if(memoryTypeIndex == VK_MAX_MEMORY_TYPES)
            return;

        const VkMemoryAllocateInfo allocationInfo{
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = nullptr,
            .allocationSize = memoryRequirements.size,
            .memoryTypeIndex = memoryTypeIndex,
        };
        if(m_context.deviceDispatch->vkAllocateMemory(
            m_context.device,
            &allocationInfo,
            m_context.allocationCallbacks,
            &m_memory
        ) != VK_SUCCESS){
            m_memory = VK_NULL_HANDLE;
            return;
        }
        if(m_context.deviceDispatch->vkBindBufferMemory(m_context.device, m_buffer, m_memory, 0u) != VK_SUCCESS)
            return;
        m_bound = true;
    }
    ~CallerOwnedRetainedStateBuffer(){
        if(m_buffer != VK_NULL_HANDLE){
            m_context.deviceDispatch->vkDestroyBuffer(m_context.device, m_buffer, m_context.allocationCallbacks);
            m_buffer = VK_NULL_HANDLE;
        }
        if(m_memory != VK_NULL_HANDLE){
            m_context.deviceDispatch->vkFreeMemory(m_context.device, m_memory, m_context.allocationCallbacks);
            m_memory = VK_NULL_HANDLE;
        }
    }


public:
    [[nodiscard]] bool valid()const noexcept{ return m_bound; }
    [[nodiscard]] Object nativeHandle()const noexcept{
        return valid() ? encode(m_buffer) : Object(u64{0u});
    }


private:
    VulkanTestDeviceContext m_context;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    bool m_bound = false;
};

};


TEST_F(DescriptorBufferRoundTripTest, PermanentStateRecordingAttemptsCommitOnlyOnAcceptedSubmission){
    auto& device = DescriptorBufferRoundTripTest::device();
    const BufferHandle baseline = device.createBuffer(
        BufferDesc()
            .setByteSize(16u)
            .setCanHaveUAVs(true)
            .setInitialState(ResourceStates::Common)
    );
    const BufferHandle provisional = device.createBuffer(
        BufferDesc()
            .setByteSize(16u)
            .setCanHaveUAVs(true)
            .setInitialState(ResourceStates::Common)
    );
    const BufferHandle abandoned = device.createBuffer(
        BufferDesc()
            .setByteSize(16u)
            .setCanHaveUAVs(true)
            .setInitialState(ResourceStates::Common)
    );
    const BufferHandle rejected = device.createBuffer(
        BufferDesc()
            .setByteSize(16u)
            .setCanHaveUAVs(true)
            .setInitialState(ResourceStates::Common)
    );
    const BufferHandle retainedInitial = device.createBuffer(
        BufferDesc()
            .setByteSize(16u)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    const BufferHandle releaseCandidate = device.createBuffer(
        BufferDesc().setByteSize(16u).setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(baseline);
    ASSERT_TRUE(provisional);
    ASSERT_TRUE(abandoned);
    ASSERT_TRUE(rejected);
    ASSERT_TRUE(retainedInitial);
    ASSERT_TRUE(releaseCandidate);

    auto commandList = device.createCommandList();
    ASSERT_TRUE(commandList);
    const auto submit = [&](){
        CommandList* const commandLists[] = { commandList.get() };
        return device.executeCommandLists(
            commandLists,
            LengthOf(commandLists),
            commandList->getDescription().physicalQueue,
            QueueSubmissionDesc{}
        );
    };

    CommandListResourceStateHandoff attemptHandoff(DescriptorBufferRoundTripTest::arena());
    commandList->open();
    commandList->setPermanentBufferState(baseline.get(), ResourceStates::UnorderedAccess);
    ASSERT_FALSE(commandList->commandRecordingFailed());
    commandList->close(&attemptHandoff);
    ASSERT_TRUE(attemptHandoff.valid());
    CommandList* const duplicateLists[] = { commandList.get(), commandList.get() };
    EXPECT_FALSE(device.executeCommandLists(
        duplicateLists,
        LengthOf(duplicateLists),
        commandList->getDescription().physicalQueue,
        QueueSubmissionDesc{}
    ).valid());
    EXPECT_TRUE(commandList->hasCommandBuffer());
    EXPECT_EQ(commandList->getPermanentBufferState(baseline.get()), ResourceStates::UnorderedAccess);
    ASSERT_TRUE(submit().valid());
    ASSERT_TRUE(device.waitForIdle());
    EXPECT_EQ(commandList->getPermanentBufferState(baseline.get()), ResourceStates::UnorderedAccess);

    commandList->open();
    commandList->setPermanentBufferState(provisional.get(), ResourceStates::UnorderedAccess);
    ASSERT_EQ(commandList->getPermanentBufferState(provisional.get()), ResourceStates::UnorderedAccess);
    commandList->setBufferState(baseline.get(), ResourceStates::ShaderResource);
    EXPECT_TRUE(commandList->commandRecordingFailed());
    commandList->close(&attemptHandoff);
    EXPECT_FALSE(attemptHandoff.valid());
    EXPECT_FALSE(commandList->hasCommandBuffer());
    EXPECT_EQ(commandList->getPermanentBufferState(baseline.get()), ResourceStates::UnorderedAccess);
    EXPECT_EQ(commandList->getPermanentBufferState(provisional.get()), ResourceStates::Unknown);

    commandList->open();
    commandList->setPermanentBufferState(abandoned.get(), ResourceStates::UnorderedAccess);
    ASSERT_EQ(commandList->getPermanentBufferState(abandoned.get()), ResourceStates::UnorderedAccess);
    commandList->close();
    ASSERT_TRUE(commandList->hasCommandBuffer());
    commandList->open();
    EXPECT_EQ(commandList->getPermanentBufferState(abandoned.get()), ResourceStates::Unknown);
    EXPECT_EQ(commandList->getPermanentBufferState(baseline.get()), ResourceStates::UnorderedAccess);
    commandList->close();
    ASSERT_TRUE(submit().valid());

    commandList->open();
    commandList->setPermanentBufferState(rejected.get(), ResourceStates::UnorderedAccess);
    ASSERT_EQ(commandList->getPermanentBufferState(rejected.get()), ResourceStates::UnorderedAccess);
    commandList->close();
    ASSERT_TRUE(commandList->hasCommandBuffer());
    const GpuPhysicalQueueId rejectedQueue = commandList->getDescription().physicalQueue;
    ASSERT_TRUE(rejectedQueue.valid());
    const VkQueue nativeRejectedQueue = static_cast<VkQueue>(
        device.getNativeQueue(GraphicsBackend::ObjectTypes::VK_Queue, rejectedQueue).pointer()
    );
    ASSERT_NE(nativeRejectedQueue, VK_NULL_HANDLE);
    VulkanTestQueueSubmit2Observer submissionObserver(device);
    ASSERT_TRUE(submissionObserver.valid());
    ASSERT_TRUE(submissionObserver.armSubmissionFailures(nativeRejectedQueue));
    EXPECT_FALSE(submit().valid());
    EXPECT_EQ(submissionObserver.injectedSubmissionFailureCount(), 1u);
    EXPECT_EQ(submissionObserver.pendingSubmissionFailureCount(), 0u);
    EXPECT_FALSE(commandList->hasCommandBuffer());
    EXPECT_EQ(commandList->getPermanentBufferState(rejected.get()), ResourceStates::Unknown);
    EXPECT_EQ(commandList->getPermanentBufferState(baseline.get()), ResourceStates::UnorderedAccess);

    commandList->open();
    commandList->setPermanentBufferState(provisional.get(), ResourceStates::Unknown);
    EXPECT_TRUE(commandList->commandRecordingFailed());
    commandList->close();
    EXPECT_EQ(commandList->getPermanentBufferState(provisional.get()), ResourceStates::Unknown);

    commandList->open();
    commandList->setPermanentBufferState(retainedInitial.get(), ResourceStates::CopyDest);
    EXPECT_TRUE(commandList->commandRecordingFailed());
    commandList->close();
    EXPECT_EQ(commandList->getPermanentBufferState(retainedInitial.get()), ResourceStates::Unknown);

    commandList->open();
    commandList->setBufferState(nullptr, ResourceStates::CopyDest);
    commandList->setPermanentBufferState(nullptr, ResourceStates::CopyDest);
    commandList->releaseBufferOwnership(nullptr, commandList->getDescription().physicalQueue);
    EXPECT_FALSE(commandList->commandRecordingFailed());
    commandList->close();
    ASSERT_TRUE(submit().valid());

    commandList->open();
    commandList->releaseBufferOwnership(releaseCandidate.get(), commandList->getDescription().physicalQueue);
    EXPECT_FALSE(commandList->commandRecordingFailed());
    commandList->close();
    EXPECT_TRUE(commandList->commandRecordingFailed());
    EXPECT_FALSE(commandList->hasCommandBuffer());
    EXPECT_EQ(commandList->getPermanentBufferState(baseline.get()), ResourceStates::UnorderedAccess);

    commandList->open();
    EXPECT_FALSE(commandList->commandRecordingFailed());
    commandList->close();
    EXPECT_TRUE(submit().valid());
    EXPECT_TRUE(device.waitForIdle());
}


TEST_F(DescriptorBufferRoundTripTest, NativeBufferRetainedInitialStatePublishesOnlyAfterAcceptedRestoration){
    auto& device = DescriptorBufferRoundTripTest::device();
    constexpr u64 s_ByteSize = 256u;
    constexpr VkBufferUsageFlags s_NativeUsage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    __hidden_descriptor_buffer_round_trip_tests::CallerOwnedRetainedStateBuffer nativeOwner(
        device,
        s_ByteSize,
        s_NativeUsage
    );
    ASSERT_TRUE(nativeOwner.valid());

    const BufferDesc desc = BufferDesc()
        .setByteSize(s_ByteSize)
        .setInitialState(ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    BufferHandle buffer = device.createHandleForNativeBuffer(
        GraphicsBackend::ObjectTypes::VK_Buffer,
        nativeOwner.nativeHandle(),
        desc,
        GraphicsBackend::NativeBufferProvenance{
            .usage = s_NativeUsage,
            .initialStateKnown = false,
        }
    );
    ASSERT_TRUE(buffer);
    EXPECT_EQ(buffer->resolveTaskGraphImportInitialState(), ResourceStates::Unknown);

    CommandListHandle commandList = device.createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    commandList->setBufferState(buffer.get(), ResourceStates::CopyDest);
    ASSERT_FALSE(commandList->commandRecordingFailed());
    commandList->close();
    ASSERT_FALSE(commandList->commandRecordingFailed());
    ASSERT_TRUE(commandList->hasCommandBuffer());
    EXPECT_EQ(buffer->resolveTaskGraphImportInitialState(), ResourceStates::Unknown);

    commandList->open();
    EXPECT_EQ(buffer->resolveTaskGraphImportInitialState(), ResourceStates::Unknown);
    commandList->setBufferState(buffer.get(), ResourceStates::CopyDest);
    ASSERT_FALSE(commandList->commandRecordingFailed());
    commandList->close();
    ASSERT_FALSE(commandList->commandRecordingFailed());
    ASSERT_TRUE(commandList->hasCommandBuffer());

    const GpuPhysicalQueueId rejectedQueue = commandList->getDescription().physicalQueue;
    ASSERT_TRUE(rejectedQueue.valid());
    const VkQueue nativeRejectedQueue = static_cast<VkQueue>(
        device.getNativeQueue(GraphicsBackend::ObjectTypes::VK_Queue, rejectedQueue).pointer()
    );
    ASSERT_NE(nativeRejectedQueue, VK_NULL_HANDLE);
    {
        VulkanTestQueueSubmit2Observer submissionObserver(device);
        ASSERT_TRUE(submissionObserver.valid());
        ASSERT_TRUE(submissionObserver.armSubmissionFailures(nativeRejectedQueue));
        CommandList* const rejectedLists[] = { commandList.get() };
        EXPECT_FALSE(device.executeCommandLists(
            rejectedLists,
            LengthOf(rejectedLists),
            rejectedQueue,
            QueueSubmissionDesc{}
        ).valid());
        EXPECT_EQ(submissionObserver.injectedSubmissionFailureCount(), 1u);
        EXPECT_EQ(submissionObserver.pendingSubmissionFailureCount(), 0u);
    }
    EXPECT_FALSE(commandList->hasCommandBuffer());
    EXPECT_EQ(buffer->resolveTaskGraphImportInitialState(), ResourceStates::Unknown);

    commandList->open();
    commandList->setBufferState(buffer.get(), ResourceStates::CopyDest);
    ASSERT_FALSE(commandList->commandRecordingFailed());
    commandList->close();
    ASSERT_FALSE(commandList->commandRecordingFailed());
    ASSERT_TRUE(commandList->hasCommandBuffer());
    EXPECT_EQ(buffer->resolveTaskGraphImportInitialState(), ResourceStates::Unknown);

    CommandList* const acceptedLists[] = { commandList.get() };
    const QueueSubmissionToken acceptedToken = device.executeCommandLists(
        acceptedLists,
        LengthOf(acceptedLists),
        commandList->getDescription().physicalQueue,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(acceptedToken.valid());
    EXPECT_EQ(buffer->resolveTaskGraphImportInitialState(), ResourceStates::Common);
    ASSERT_TRUE(device.waitForIdle());

    commandList.reset();
    buffer.reset();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

