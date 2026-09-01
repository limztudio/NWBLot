// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Buffer GPU-readiness, state-ingress atomicity, retention, close defense, and handoff defense coverage.


#include <gtest/gtest.h>

#include <global/global.h>
#include <global/unique_ptr.h>
#include <core/graphics/rhi/queue_sharing.h>
#include <core/graphics/task_graph/task_graph.h>
#include <core/graphics/vulkan/backend.h>
#include <tests/common/capturing_logger.h>
#include <tests/common/headless_graphics_scope.h>
#include <tests/common/vulkan_test_sync.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core;


namespace __hidden_buffer_gpu_readiness{


struct NativeQueueFamilySet{
    Vector<u32, Alloc::ScratchArena> familyIndices;


    explicit NativeQueueFamilySet(Alloc::ScratchArena& scratchArena)
        : familyIndices(scratchArena)
    {}


    void append(const u32 familyIndex){
        for(const u32 existingFamilyIndex : familyIndices){
            if(existingFamilyIndex == familyIndex)
                return;
        }
        familyIndices.push_back(familyIndex);
    }
    [[nodiscard]] bool contains(const u32 familyIndex)const noexcept{
        for(const u32 existingFamilyIndex : familyIndices){
            if(existingFamilyIndex == familyIndex)
                return true;
        }
        return false;
    }
    [[nodiscard]] VkSharingMode sharingMode()const noexcept{
        return familyIndices.size() >= 2u ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE;
    }
    [[nodiscard]] u32 size()const noexcept{
        NWB_ASSERT(familyIndices.size() <= Limit<u32>::s_Max);
        return static_cast<u32>(familyIndices.size());
    }
    [[nodiscard]] const u32* data()const noexcept{
        return familyIndices.empty() ? nullptr : familyIndices.data();
    }
};


static void GatherQueueFamilies(
    GraphicsBackend::Device& device,
    const ResourceQueueSharing::Mask sharing,
    NativeQueueFamilySet& result
){
    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    result.familyIndices.reserve(topology.queueCount);
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& queue = topology.queues[queueIndex];
        if(ResourceQueueSharing::IncludesQueueClass(sharing, queue.queueClass))
            result.append(queue.familyIndex);
    }
}


class CallerOwnedVulkanBuffer final : NoCopy{
private:
    [[nodiscard]] static Object encode(const VkBuffer buffer)noexcept{
#if VK_USE_64_BIT_PTR_DEFINES
        return Object(static_cast<void*>(buffer));
#else
        return Object(static_cast<u64>(buffer));
#endif
    }


public:
    CallerOwnedVulkanBuffer(
        GraphicsBackend::Device& device,
        const u64 byteSize,
        const VkBufferUsageFlags usage
    )
        : m_context(VulkanTestDeviceProbe::capture(device))
        , m_usage(usage)
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
            || (
                (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0u
                && !m_context.deviceDispatch->vkGetBufferDeviceAddress
            )
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
        if(m_context.deviceDispatch->vkCreateBuffer(
            m_context.device,
            &bufferInfo,
            m_context.allocationCallbacks,
            &m_buffer
        ) != VK_SUCCESS){
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

        const VkMemoryAllocateFlagsInfo allocationFlags{
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
            .pNext = nullptr,
            .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT,
            .deviceMask = 0u,
        };
        const VkMemoryAllocateInfo allocationInfo{
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0u ? &allocationFlags : nullptr,
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
    ~CallerOwnedVulkanBuffer(){
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
    [[nodiscard]] GpuVirtualAddress deviceAddress()const noexcept{
        if(!valid() || (m_usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) == 0u)
            return 0u;

        const VkBufferDeviceAddressInfo addressInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
            .pNext = nullptr,
            .buffer = m_buffer,
        };
        return m_context.deviceDispatch->vkGetBufferDeviceAddress(m_context.device, &addressInfo);
    }


private:
    VulkanTestDeviceContext m_context;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    VkBufferUsageFlags m_usage = 0u;
    bool m_bound = false;
};


struct BufferOwnerReleaseHookContext{
    BufferHandle* owner = nullptr;
    Buffer* retainedBuffer = nullptr;
    QueueSubmissionNativeSignal signal;
    u32 invocationCount = 0u;
};

struct BufferDescriptionDriftHookContext{
    Buffer* buffer = nullptr;
    QueueSubmissionNativeSignal signal;
    BufferDesc savedDescription;
    u32 invocationCount = 0u;
    bool descriptionMutated = false;
};


[[nodiscard]] static bool ReleaseBufferOwnerDuringSubmissionHook(
    void* const rawContext,
    const u64,
    const GpuPhysicalQueueId& executionQueue,
    QueueSubmissionNativeSignal& outSignal
){
    BufferOwnerReleaseHookContext* const context = static_cast<BufferOwnerReleaseHookContext*>(rawContext);
    if(
        !context
        || !context->owner
        || !*context->owner
        || context->owner->get() != context->retainedBuffer
        || !executionQueue.valid()
        || !context->signal.valid()
    )
        return false;

    ++context->invocationCount;
    context->owner->reset();
    outSignal = context->signal;
    return true;
}

[[nodiscard]] static bool DriftBufferDescriptionDuringSubmissionHook(
    void* const rawContext,
    const u64,
    const GpuPhysicalQueueId& executionQueue,
    QueueSubmissionNativeSignal& outSignal
){
    BufferDescriptionDriftHookContext* const context = static_cast<BufferDescriptionDriftHookContext*>(rawContext);
    if(
        !context
        || !context->buffer
        || !executionQueue.valid()
        || !context->signal.valid()
    )
        return false;

    context->savedDescription = context->buffer->getDescription();
    BufferDesc& publishedDescription = const_cast<BufferDesc&>(context->buffer->getDescription());
    publishedDescription.isVirtual = true;
    context->descriptionMutated = true;
    ++context->invocationCount;
    outSignal = context->signal;
    return true;
}


};


static constexpr u32 s_BufferReadinessComputeSpirv[] = {
    0x07230203u, 0x00010600u, 0x00070000u, 0x00000005u, 0x00000000u,
    0x00020011u, 0x00000001u,
    0x0003000eu, 0x00000000u, 0x00000001u,
    0x0005000fu, 0x00000005u, 0x00000001u, 0x6e69616du, 0x00000000u,
    0x00060010u, 0x00000001u, 0x00000011u, 0x00000001u, 0x00000001u, 0x00000001u,
    0x00020013u, 0x00000002u,
    0x00030021u, 0x00000003u, 0x00000002u,
    0x00050036u, 0x00000002u, 0x00000001u, 0x00000000u, 0x00000003u,
    0x000200f8u, 0x00000004u,
    0x000100fdu,
    0x00010038u,
};


class BufferGpuReadinessTest : public ::testing::Test{
protected:
    static void SetUpTestSuite(){
        s_logger.emplace();
        s_loggerGuard.emplace(*s_logger);

        s_scope = MakeUnique<HeadlessGraphicsScope>();
        if(!s_scope->setTransferQueueEnabled(true)){
            GTEST_SKIP() << "Buffer GPU readiness: transfer-queue configuration is unavailable.";
            return;
        }
        if(!s_scope->initialize()){
            GTEST_SKIP() << "Buffer GPU readiness: no usable validation-enabled headless Vulkan device.";
            return;
        }
        s_validationBackedDeviceInitialized = true;
    }

    static void TearDownTestSuite(){
        s_scope.reset();
        if(s_validationBackedDeviceInitialized && s_logger.has_value()){
            EXPECT_FALSE(s_logger->sawMessageContaining(NWB_TEXT("Vulkan debug: [severity=error")))
                << "validation-enabled buffer GPU-readiness smoke emitted a Vulkan severity=error message";
        }
        s_loggerGuard.reset();
        s_logger.reset();
        s_validationBackedDeviceInitialized = false;
    }

    [[nodiscard]] static GraphicsBackend::Device& device(){
        return s_scope->graphics().getDevice();
    }

    [[nodiscard]] static Core::Alloc::GlobalArena& arena(){
        return s_scope->arena();
    }

protected:
    static bool s_validationBackedDeviceInitialized;
    static UniquePtr<HeadlessGraphicsScope> s_scope;
    static Optional<CapturingLogger> s_logger;
    static Optional<Common::LoggerRegistrationGuard> s_loggerGuard;
};

bool BufferGpuReadinessTest::s_validationBackedDeviceInitialized = false;
UniquePtr<HeadlessGraphicsScope> BufferGpuReadinessTest::s_scope;
Optional<CapturingLogger> BufferGpuReadinessTest::s_logger;
Optional<Common::LoggerRegistrationGuard> BufferGpuReadinessTest::s_loggerGuard;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(BufferGpuReadinessTest, PredicateAcceptsOrdinaryAndReadyHandoff){
    auto& device = BufferGpuReadinessTest::device();
    EXPECT_FALSE(device.isBufferReadyForGpuUse(nullptr));

    const BufferDesc desc = BufferDesc()
        .setByteSize(256u)
        .setInitialState(ResourceStates::Common)
    ;
    BufferHandle ordinary = device.createBuffer(desc);
    ASSERT_TRUE(ordinary);
    ASSERT_TRUE(device.isBufferReadyForGpuUse(ordinary.get()));

    CommandListResourceStateHandoff producerState(BufferGpuReadinessTest::arena());
    CommandListHandle producer = device.createCommandList();
    ASSERT_TRUE(producer);
    producer->open();
    producer->beginTrackingBufferState(ordinary.get(), ResourceStates::Common);
    producer->close(&producerState);
    ASSERT_FALSE(producer->commandRecordingFailed());
    ASSERT_TRUE(producer->hasCommandBuffer());
    ASSERT_TRUE(producerState.valid());

    CommandListHandle consumer = device.createCommandList();
    ASSERT_TRUE(consumer);
    consumer->open(&producerState);
    consumer->setBufferState(ordinary.get(), ResourceStates::CopySource);
    consumer->close();
    EXPECT_FALSE(consumer->commandRecordingFailed());
    EXPECT_TRUE(consumer->hasCommandBuffer());
}


TEST_F(BufferGpuReadinessTest, ImmutableCreationDescriptionAndUsageAwareReadinessAreEnforced){
    auto& device = BufferGpuReadinessTest::device();
    const BufferDesc desc = BufferDesc()
        .setByteSize(256u)
        .setInitialState(ResourceStates::Common)
        .setIsVertexBuffer(true)
    ;
    BufferHandle buffer = device.createBuffer(desc);
    ASSERT_TRUE(buffer);

    const BufferDesc& creationDesc = buffer->getCreationDescription();
    EXPECT_EQ(creationDesc.byteSize, desc.byteSize);
    EXPECT_EQ(creationDesc.initialState, desc.initialState);
    EXPECT_TRUE(creationDesc.isVertexBuffer);
    EXPECT_TRUE(buffer->descriptionMatchesCreation());
    EXPECT_TRUE(device.isBufferReadyForGpuUse(buffer.get(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));
    EXPECT_TRUE(device.isBufferReadyForGpuUse(
        buffer.get(),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT
    ));
    EXPECT_FALSE(device.isBufferReadyForGpuUse(buffer.get(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT));

    BufferDesc& publishedDesc = const_cast<BufferDesc&>(buffer->getDescription());
    publishedDesc.isIndexBuffer = true;
    EXPECT_FALSE(buffer->descriptionMatchesCreation());
    EXPECT_FALSE(device.isBufferReadyForGpuUse(buffer.get()));

    const u32 referencesBefore = buffer->getReferenceCount();
    CommandListHandle driftedList = device.createCommandList();
    ASSERT_TRUE(driftedList);
    driftedList->open();
    driftedList->beginTrackingBufferState(buffer.get(), ResourceStates::IndexBuffer);
    EXPECT_TRUE(driftedList->commandRecordingFailed());
    EXPECT_FALSE(driftedList->hasExplicitBufferState(buffer.get()));
    EXPECT_EQ(buffer->getReferenceCount(), referencesBefore);
    driftedList->close();
    EXPECT_FALSE(driftedList->hasCommandBuffer());

    publishedDesc = creationDesc;
    EXPECT_TRUE(buffer->descriptionMatchesCreation());
    EXPECT_TRUE(device.isBufferReadyForGpuUse(buffer.get(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));

    CommandListHandle capabilityList = device.createCommandList();
    ASSERT_TRUE(capabilityList);
    capabilityList->open();
    capabilityList->setBufferState(buffer.get(), ResourceStates::IndexBuffer);
    EXPECT_TRUE(capabilityList->commandRecordingFailed());
    EXPECT_FALSE(capabilityList->hasExplicitBufferState(buffer.get()));
    EXPECT_EQ(buffer->getReferenceCount(), referencesBefore);
    capabilityList->close();
    EXPECT_FALSE(capabilityList->hasCommandBuffer());
}


TEST_F(BufferGpuReadinessTest, ExplicitTransferOnlyNativeUsageDoesNotInferOtherCapabilities){
    auto& device = BufferGpuReadinessTest::device();
    const BufferDesc desc = BufferDesc()
        .setByteSize(256u)
        .setInitialState(ResourceStates::Common)
    ;
    const VkBufferUsageFlags nativeUsage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    BufferHandle unmanaged = device.createHandleForNativeBuffer(
        GraphicsBackend::ObjectTypes::VK_Buffer,
        Object(static_cast<u64>(0x22d0000au)),
        desc,
        GraphicsBackend::NativeBufferProvenance{ .usage = nativeUsage }
    );
    ASSERT_TRUE(unmanaged);
    EXPECT_TRUE(device.isBufferReadyForGpuUse(unmanaged.get()));
    EXPECT_TRUE(device.isBufferReadyForGpuUse(unmanaged.get(), nativeUsage));
    EXPECT_FALSE(device.isBufferReadyForGpuUse(unmanaged.get(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));
    EXPECT_FALSE(device.isBufferReadyForGpuUse(unmanaged.get(), VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT));
    EXPECT_FALSE(device.isBufferReadyForGpuUse(unmanaged.get(), VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT));
    EXPECT_EQ(unmanaged->getGpuVirtualAddress(), 0u);
}


TEST_F(BufferGpuReadinessTest, CreationRejectsUnknownQueueSharingBeforeAllocationOrNativeIdentity){
    auto& device = BufferGpuReadinessTest::device();
    const auto expectDiagnosticRejection = [](const auto& operation){
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
        EXPECT_DEATH_IF_SUPPORTED({ EXPECT_FALSE(operation()); }, "");
#else
        EXPECT_FALSE(operation());
#endif
    };
    constexpr u8 s_UnknownQueueSharingBit = 1u << 7u;
    constexpr ResourceQueueSharing::Mask s_UnknownQueueSharing =
        static_cast<ResourceQueueSharing::Mask>(s_UnknownQueueSharingBit);
    constexpr ResourceQueueSharing::Mask s_MixedQueueSharing = static_cast<ResourceQueueSharing::Mask>(
        static_cast<u8>(ResourceQueueSharing::GraphicsAndTransfer) | s_UnknownQueueSharingBit
    );

    const BufferDesc managedDesc = BufferDesc()
        .setByteSize(256u)
        .setInitialState(ResourceStates::Common)
        .setQueueSharing(s_UnknownQueueSharing)
    ;
    expectDiagnosticRejection([&](){ return device.createBuffer(managedDesc).get() != nullptr; });

    const BufferDesc virtualDesc = BufferDesc()
        .setByteSize(256u)
        .setInitialState(ResourceStates::Common)
        .setQueueSharing(s_MixedQueueSharing)
        .setIsVirtual(true)
    ;
    expectDiagnosticRejection([&](){ return device.createBuffer(virtualDesc).get() != nullptr; });

    const Object nativeBuffer(static_cast<u64>(0x22d0000eu));
    const BufferDesc invalidNativeDesc = BufferDesc()
        .setByteSize(256u)
        .setInitialState(ResourceStates::Common)
        .setQueueSharing(s_MixedQueueSharing)
    ;
    constexpr VkBufferUsageFlags s_NativeUsage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    expectDiagnosticRejection([&](){
        return device.createHandleForNativeBuffer(
            GraphicsBackend::ObjectTypes::VK_Buffer,
            nativeBuffer,
            invalidNativeDesc,
            GraphicsBackend::NativeBufferProvenance{ .usage = s_NativeUsage }
        ).get() != nullptr;
    });

    const BufferDesc validNativeDesc = BufferDesc()
        .setByteSize(256u)
        .setInitialState(ResourceStates::Common)
        .setQueueSharing(ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
    ;
    Alloc::ScratchArena scratchArena(Name("tests/buffer_gpu_readiness/native_queue_sharing_retry"));
    __hidden_buffer_gpu_readiness::NativeQueueFamilySet nativeQueueFamilies(scratchArena);
    __hidden_buffer_gpu_readiness::GatherQueueFamilies(
        device,
        validNativeDesc.queueSharing,
        nativeQueueFamilies
    );
    const bool usesConcurrentSharing = nativeQueueFamilies.sharingMode() == VK_SHARING_MODE_CONCURRENT;
    BufferHandle retry = device.createHandleForNativeBuffer(
        GraphicsBackend::ObjectTypes::VK_Buffer,
        nativeBuffer,
        validNativeDesc,
        GraphicsBackend::NativeBufferProvenance{
            .usage = s_NativeUsage,
            .sharingMode = nativeQueueFamilies.sharingMode(),
            .queueFamilyIndexCount = usesConcurrentSharing ? nativeQueueFamilies.size() : 0u,
            .queueFamilyIndices = usesConcurrentSharing ? nativeQueueFamilies.data() : nullptr,
        }
    );
    ASSERT_TRUE(retry);
    EXPECT_EQ(
        retry->getCreationDescription().queueSharing,
        ResourceQueueSharing::GraphicsAsyncComputeAndTransfer
    );
    EXPECT_TRUE(retry->descriptionMatchesCreation());
}


TEST_F(BufferGpuReadinessTest, NativeProvenanceRejectsMalformedSharingAndProtectedFlagsBeforeIdentityPublication){
    auto& device = BufferGpuReadinessTest::device();
    constexpr VkBufferUsageFlags s_NativeUsage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    const BufferDesc exclusiveDesc = BufferDesc()
        .setByteSize(256u)
        .setInitialState(ResourceStates::Common)
    ;
    BufferDesc graphicsDesc = exclusiveDesc;
    graphicsDesc.setQueueSharing(ResourceQueueSharing::Graphics);
    const Object nativeBuffer(static_cast<u64>(0x22d00110u));
    const u32 queueFamilyIndex = 0u;
    const Array<u32, 2u> queueFamilies = { 0u, 1u };
    const Array<u32, 2u> duplicateQueueFamilies = { 0u, 0u };
    const Array<u32, 2u> outOfRangeQueueFamilies = { 0u, VK_QUEUE_FAMILY_IGNORED };
    const auto expectDiagnosticRejection = [](const auto& operation){
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
        EXPECT_DEATH_IF_SUPPORTED({ EXPECT_FALSE(operation()); }, "");
#else
        EXPECT_FALSE(operation());
#endif
    };

    expectDiagnosticRejection([&](){
        return device.createHandleForNativeBuffer(
            GraphicsBackend::ObjectTypes::VK_Buffer,
            nativeBuffer,
            exclusiveDesc,
            GraphicsBackend::NativeBufferProvenance{}
        ).get() != nullptr;
    });
    expectDiagnosticRejection([&](){
        return device.createHandleForNativeBuffer(
            GraphicsBackend::ObjectTypes::VK_Buffer,
            nativeBuffer,
            exclusiveDesc,
            GraphicsBackend::NativeBufferProvenance{
                .usage = s_NativeUsage,
                .flags = VK_BUFFER_CREATE_PROTECTED_BIT,
            }
        ).get() != nullptr;
    });
    expectDiagnosticRejection([&](){
        return device.createHandleForNativeBuffer(
            GraphicsBackend::ObjectTypes::VK_Buffer,
            nativeBuffer,
            graphicsDesc,
            GraphicsBackend::NativeBufferProvenance{
                .usage = s_NativeUsage,
                .sharingMode = static_cast<VkSharingMode>(VK_SHARING_MODE_MAX_ENUM),
            }
        ).get() != nullptr;
    });
    expectDiagnosticRejection([&](){
        return device.createHandleForNativeBuffer(
            GraphicsBackend::ObjectTypes::VK_Buffer,
            nativeBuffer,
            exclusiveDesc,
            GraphicsBackend::NativeBufferProvenance{
                .usage = s_NativeUsage,
                .queueFamilyIndexCount = 1u,
                .queueFamilyIndices = &queueFamilyIndex,
            }
        ).get() != nullptr;
    });
    expectDiagnosticRejection([&](){
        return device.createHandleForNativeBuffer(
            GraphicsBackend::ObjectTypes::VK_Buffer,
            nativeBuffer,
            graphicsDesc,
            GraphicsBackend::NativeBufferProvenance{
                .usage = s_NativeUsage,
                .sharingMode = VK_SHARING_MODE_CONCURRENT,
                .queueFamilyIndexCount = 1u,
                .queueFamilyIndices = &queueFamilyIndex,
            }
        ).get() != nullptr;
    });
    expectDiagnosticRejection([&](){
        return device.createHandleForNativeBuffer(
            GraphicsBackend::ObjectTypes::VK_Buffer,
            nativeBuffer,
            graphicsDesc,
            GraphicsBackend::NativeBufferProvenance{
                .usage = s_NativeUsage,
                .sharingMode = VK_SHARING_MODE_CONCURRENT,
                .queueFamilyIndexCount = 2u,
                .queueFamilyIndices = nullptr,
            }
        ).get() != nullptr;
    });
    expectDiagnosticRejection([&](){
        return device.createHandleForNativeBuffer(
            GraphicsBackend::ObjectTypes::VK_Buffer,
            nativeBuffer,
            exclusiveDesc,
            GraphicsBackend::NativeBufferProvenance{
                .usage = s_NativeUsage,
                .sharingMode = VK_SHARING_MODE_CONCURRENT,
                .queueFamilyIndexCount = static_cast<u32>(queueFamilies.size()),
                .queueFamilyIndices = queueFamilies.data(),
            }
        ).get() != nullptr;
    });
    expectDiagnosticRejection([&](){
        return device.createHandleForNativeBuffer(
            GraphicsBackend::ObjectTypes::VK_Buffer,
            nativeBuffer,
            graphicsDesc,
            GraphicsBackend::NativeBufferProvenance{
                .usage = s_NativeUsage,
                .sharingMode = VK_SHARING_MODE_CONCURRENT,
                .queueFamilyIndexCount = static_cast<u32>(duplicateQueueFamilies.size()),
                .queueFamilyIndices = duplicateQueueFamilies.data(),
            }
        ).get() != nullptr;
    });
    expectDiagnosticRejection([&](){
        return device.createHandleForNativeBuffer(
            GraphicsBackend::ObjectTypes::VK_Buffer,
            nativeBuffer,
            graphicsDesc,
            GraphicsBackend::NativeBufferProvenance{
                .usage = s_NativeUsage,
                .sharingMode = VK_SHARING_MODE_CONCURRENT,
                .queueFamilyIndexCount = static_cast<u32>(outOfRangeQueueFamilies.size()),
                .queueFamilyIndices = outOfRangeQueueFamilies.data(),
            }
        ).get() != nullptr;
    });

    BufferHandle retry = device.createHandleForNativeBuffer(
        GraphicsBackend::ObjectTypes::VK_Buffer,
        nativeBuffer,
        exclusiveDesc,
        GraphicsBackend::NativeBufferProvenance{ .usage = s_NativeUsage }
    );
    ASSERT_TRUE(retry);
    EXPECT_TRUE(device.isBufferReadyForGpuUse(retry.get(), s_NativeUsage));
}


TEST_F(BufferGpuReadinessTest, NativeInitialStateKnowledgeSurvivesTypedTaskGraphImport){
    auto& device = BufferGpuReadinessTest::device();
    constexpr VkBufferUsageFlags s_NativeUsage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    const BufferDesc desc = BufferDesc()
        .setByteSize(256u)
        .setInitialState(ResourceStates::CopySource)
        .setKeepInitialState(true)
    ;
    BufferHandle unknownState = device.createHandleForNativeBuffer(
        GraphicsBackend::ObjectTypes::VK_Buffer,
        Object(static_cast<u64>(0x22d00111u)),
        desc,
        GraphicsBackend::NativeBufferProvenance{
            .usage = s_NativeUsage,
            .initialStateKnown = false,
        }
    );
    BufferHandle knownState = device.createHandleForNativeBuffer(
        GraphicsBackend::ObjectTypes::VK_Buffer,
        Object(static_cast<u64>(0x22d00112u)),
        desc,
        GraphicsBackend::NativeBufferProvenance{
            .usage = s_NativeUsage,
            .initialStateKnown = true,
        }
    );
    ASSERT_TRUE(unknownState);
    ASSERT_TRUE(knownState);

    GpuTaskGraph graph(BufferGpuReadinessTest::arena());
    const GpuGraphResourceId unknownResource = graph.importBuffer(
        unknownState,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/buffer_native_provenance/unknown_initial_state"))
            .setMarkerLabel("Unknown Native Buffer Initial State")
            .setType(GpuGraphResourceType::Buffer)
    );
    const GpuGraphResourceId knownResource = graph.importBuffer(
        knownState,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/buffer_native_provenance/known_initial_state"))
            .setMarkerLabel("Known Native Buffer Initial State")
            .setType(GpuGraphResourceType::Buffer)
    );
    ASSERT_TRUE(unknownResource.valid());
    ASSERT_TRUE(knownResource.valid());
    EXPECT_EQ(graph.resourceAt(unknownResource.index).initialState, ResourceStates::Unknown);
    EXPECT_EQ(graph.resourceAt(knownResource.index).initialState, ResourceStates::CopySource);
}


TEST_F(BufferGpuReadinessTest, UnknownNativeStateOwnershipReleaseDoesNotUseDescriptorFallback){
    auto& device = BufferGpuReadinessTest::device();
    constexpr VkBufferUsageFlags s_NativeUsage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    const BufferDesc desc = BufferDesc()
        .setByteSize(256u)
        .setInitialState(ResourceStates::CopySource)
        .setKeepInitialState(true)
    ;
    __hidden_buffer_gpu_readiness::CallerOwnedVulkanBuffer nativeOwner(device, desc.byteSize, s_NativeUsage);
    ASSERT_TRUE(nativeOwner.valid());
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
    ASSERT_EQ(buffer->resolveTaskGraphImportInitialState(), ResourceStates::Unknown);

    const u32 referencesBefore = buffer->getReferenceCount();
    CommandListHandle commandList = device.createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    commandList->releaseBufferOwnership(
        buffer.get(),
        device.getPrimaryPhysicalQueue(CommandQueue::Graphics)
    );
    EXPECT_TRUE(commandList->commandRecordingFailed());
    EXPECT_FALSE(commandList->hasExplicitBufferState(buffer.get()));
    EXPECT_EQ(buffer->getReferenceCount(), referencesBefore);
    commandList->close();
    EXPECT_FALSE(commandList->hasCommandBuffer());
}


TEST_F(BufferGpuReadinessTest, ConcurrentNativeSharingCopiesFamiliesAndEnforcesExactLogicalAdmission){
    auto& device = BufferGpuReadinessTest::device();
    Alloc::ScratchArena scratchArena(Name("tests/buffer_native_provenance/queue_families"));
    __hidden_buffer_gpu_readiness::NativeQueueFamilySet nativeQueueFamilies(scratchArena);
    __hidden_buffer_gpu_readiness::GatherQueueFamilies(
        device,
        ResourceQueueSharing::Graphics,
        nativeQueueFamilies
    );
    ASSERT_GT(nativeQueueFamilies.size(), 0u);
    const BufferDesc desc = BufferDesc()
        .setByteSize(256u)
        .setInitialState(ResourceStates::Common)
        .setQueueSharing(ResourceQueueSharing::Graphics)
    ;
    const Object nativeBuffer(static_cast<u64>(0x22d00113u));
    const auto expectDiagnosticRejection = [](const auto& operation){
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
        EXPECT_DEATH_IF_SUPPORTED({ EXPECT_FALSE(operation()); }, "");
#else
        EXPECT_FALSE(operation());
#endif
    };

    __hidden_buffer_gpu_readiness::NativeQueueFamilySet mixedQueueFamilies(scratchArena);
    __hidden_buffer_gpu_readiness::GatherQueueFamilies(
        device,
        ResourceQueueSharing::GraphicsAndTransfer,
        mixedQueueFamilies
    );
    if(mixedQueueFamilies.sharingMode() == VK_SHARING_MODE_CONCURRENT){
        const BufferDesc mixedDesc = BufferDesc()
            .setByteSize(256u)
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::GraphicsAndTransfer)
        ;
        expectDiagnosticRejection([&](){
            return device.createHandleForNativeBuffer(
                GraphicsBackend::ObjectTypes::VK_Buffer,
                nativeBuffer,
                mixedDesc,
                GraphicsBackend::NativeBufferProvenance{ .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT }
            ).get() != nullptr;
        });
    }

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    __hidden_buffer_gpu_readiness::NativeQueueFamilySet allQueueFamilies(scratchArena);
    allQueueFamilies.familyIndices.reserve(topology.queueCount);
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex)
        allQueueFamilies.append(topology.queues[queueIndex].familyIndex);
    if(allQueueFamilies.size() >= 3u){
        __hidden_buffer_gpu_readiness::NativeQueueFamilySet omittedQueueFamilies(scratchArena);
        omittedQueueFamilies.familyIndices.reserve(2u);
        const u32 omittedLogicalFamily = nativeQueueFamilies.familyIndices[0u];
        for(const u32 familyIndex : allQueueFamilies.familyIndices){
            if(familyIndex == omittedLogicalFamily)
                continue;
            omittedQueueFamilies.append(familyIndex);
            if(omittedQueueFamilies.size() == 2u)
                break;
        }
        ASSERT_EQ(omittedQueueFamilies.size(), 2u);
        expectDiagnosticRejection([&](){
            return device.createHandleForNativeBuffer(
                GraphicsBackend::ObjectTypes::VK_Buffer,
                nativeBuffer,
                desc,
                GraphicsBackend::NativeBufferProvenance{
                    .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    .sharingMode = VK_SHARING_MODE_CONCURRENT,
                    .queueFamilyIndexCount = omittedQueueFamilies.size(),
                    .queueFamilyIndices = omittedQueueFamilies.data(),
                }
            ).get() != nullptr;
        });
    }

    const GpuPhysicalQueueInfo* unadmittedQueue = nullptr;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
        if(candidate.queueClass == CommandQueue::Graphics)
            continue;
        if(!unadmittedQueue)
            unadmittedQueue = &candidate;
        if(nativeQueueFamilies.size() >= 2u || !nativeQueueFamilies.contains(candidate.familyIndex)){
            unadmittedQueue = &candidate;
            nativeQueueFamilies.append(candidate.familyIndex);
            break;
        }
    }
    if(nativeQueueFamilies.sharingMode() != VK_SHARING_MODE_CONCURRENT)
        GTEST_SKIP() << "Buffer native provenance: no second queue family is available for concurrent import.";

    BufferHandle buffer = device.createHandleForNativeBuffer(
        GraphicsBackend::ObjectTypes::VK_Buffer,
        nativeBuffer,
        desc,
        GraphicsBackend::NativeBufferProvenance{
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .sharingMode = VK_SHARING_MODE_CONCURRENT,
            .queueFamilyIndexCount = nativeQueueFamilies.size(),
            .queueFamilyIndices = nativeQueueFamilies.data(),
        }
    );
    ASSERT_TRUE(buffer);

    for(u32& familyIndex : nativeQueueFamilies.familyIndices)
        familyIndex = VK_QUEUE_FAMILY_IGNORED;
    CommandListHandle admittedList = device.createCommandList();
    ASSERT_TRUE(admittedList);
    admittedList->open();
    admittedList->setEnableUavBarriersForBuffer(buffer.get(), false);
    EXPECT_FALSE(admittedList->commandRecordingFailed());
    admittedList->close();

    if(!unadmittedQueue)
        return;

    CommandListParameters policyParameters;
    policyParameters.setPhysicalQueue(unadmittedQueue->id);
    CommandListHandle policyList = device.createCommandList(policyParameters);
    ASSERT_TRUE(policyList);
    const u32 bufferReferences = buffer->getReferenceCount();
    policyList->open();
    policyList->setEnableUavBarriersForBuffer(buffer.get(), false);
    EXPECT_TRUE(policyList->commandRecordingFailed());
    EXPECT_EQ(buffer->getReferenceCount(), bufferReferences);
    policyList->close();
    EXPECT_FALSE(policyList->hasCommandBuffer());
}


TEST_F(BufferGpuReadinessTest, NativeUsageMismatchRejectionDoesNotRetainIdentity){
    auto& device = BufferGpuReadinessTest::device();
    const auto expectDiagnosticRejection = [](const auto& operation){
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
        EXPECT_DEATH_IF_SUPPORTED({ EXPECT_FALSE(operation()); }, "");
#else
        EXPECT_FALSE(operation());
#endif
    };
    const Object logicalMismatchNativeBuffer(static_cast<u64>(0x22d0000bu));
    const BufferDesc vertexDesc = BufferDesc()
        .setByteSize(256u)
        .setInitialState(ResourceStates::Common)
        .setIsVertexBuffer(true)
    ;
    expectDiagnosticRejection([&](){
        return device.createHandleForNativeBuffer(
            GraphicsBackend::ObjectTypes::VK_Buffer,
            logicalMismatchNativeBuffer,
            vertexDesc,
            GraphicsBackend::NativeBufferProvenance{ .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT }
        ).get() != nullptr;
    });
    BufferHandle logicalRetry = device.createHandleForNativeBuffer(
        GraphicsBackend::ObjectTypes::VK_Buffer,
        logicalMismatchNativeBuffer,
        vertexDesc,
        GraphicsBackend::NativeBufferProvenance{ .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT }
    );
    ASSERT_TRUE(logicalRetry);

    const Object stateMismatchNativeBuffer(static_cast<u64>(0x22d0000cu));
    const BufferDesc copyDestDesc = BufferDesc()
        .setByteSize(256u)
        .setInitialState(ResourceStates::CopyDest)
    ;
    expectDiagnosticRejection([&](){
        return device.createHandleForNativeBuffer(
            GraphicsBackend::ObjectTypes::VK_Buffer,
            stateMismatchNativeBuffer,
            copyDestDesc,
            GraphicsBackend::NativeBufferProvenance{ .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT }
        ).get() != nullptr;
    });
    BufferHandle stateRetry = device.createHandleForNativeBuffer(
        GraphicsBackend::ObjectTypes::VK_Buffer,
        stateMismatchNativeBuffer,
        copyDestDesc,
        GraphicsBackend::NativeBufferProvenance{ .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT }
    );
    ASSERT_TRUE(stateRetry);

    if(!device.queryFeatureSupport(Feature::RayTracingOpacityMicromap)){
        const Object featureMismatchNativeBuffer(static_cast<u64>(0x22d0000du));
        expectDiagnosticRejection([&](){
            return device.createHandleForNativeBuffer(
                GraphicsBackend::ObjectTypes::VK_Buffer,
                featureMismatchNativeBuffer,
                BufferDesc().setByteSize(256u).setInitialState(ResourceStates::Common),
                GraphicsBackend::NativeBufferProvenance{
                    .usage = VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                }
            ).get() != nullptr;
        });
        BufferHandle featureRetry = device.createHandleForNativeBuffer(
            GraphicsBackend::ObjectTypes::VK_Buffer,
            featureMismatchNativeBuffer,
            BufferDesc().setByteSize(256u).setInitialState(ResourceStates::Common),
            GraphicsBackend::NativeBufferProvenance{ .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT }
        );
        ASSERT_TRUE(featureRetry);
    }
}


TEST_F(BufferGpuReadinessTest, CallerOwnedNativeBufferRetainsExactUsageAddressAndReimport){
    auto& device = BufferGpuReadinessTest::device();
    const BufferDesc desc = BufferDesc()
        .setByteSize(256u)
        .setInitialState(ResourceStates::Common)
        .setIsVertexBuffer(true)
    ;
    BufferHandle owner = device.createBuffer(desc);
    ASSERT_TRUE(owner);

    VkBufferUsageFlags nativeUsage =
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT
        | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT
        | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT
        | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
    ;
    const bool deviceAddressSupported = device.isBufferReadyForGpuUse(
        owner.get(),
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
    );
    owner.reset();
    if(deviceAddressSupported)
        nativeUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    __hidden_buffer_gpu_readiness::CallerOwnedVulkanBuffer nativeOwner(device, desc.byteSize, nativeUsage);
    ASSERT_TRUE(nativeOwner.valid());
    const Object nativeBuffer = nativeOwner.nativeHandle();
    ASSERT_NE(nativeBuffer, nullptr);
    const GpuVirtualAddress nativeDeviceAddress = nativeOwner.deviceAddress();
    if(deviceAddressSupported)
        ASSERT_NE(nativeDeviceAddress, 0u);
    else
        ASSERT_EQ(nativeDeviceAddress, 0u);

    BufferHandle imported = device.createHandleForNativeBuffer(
        GraphicsBackend::ObjectTypes::VK_Buffer,
        nativeBuffer,
        desc,
        GraphicsBackend::NativeBufferProvenance{ .usage = nativeUsage }
    );
    ASSERT_TRUE(imported);
    EXPECT_TRUE(device.isBufferReadyForGpuUse(imported.get(), nativeUsage));
    EXPECT_FALSE(device.isBufferReadyForGpuUse(imported.get(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT));
    EXPECT_FALSE(device.isBufferReadyForGpuUse(imported.get(), VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT));
    EXPECT_EQ(imported->getGpuVirtualAddress(), nativeDeviceAddress);
    EXPECT_FALSE(device.createHandleForNativeBuffer(
        GraphicsBackend::ObjectTypes::VK_Buffer,
        nativeBuffer,
        desc,
        GraphicsBackend::NativeBufferProvenance{ .usage = nativeUsage }
    ));

    imported.reset();
    BufferHandle reimported = device.createHandleForNativeBuffer(
        GraphicsBackend::ObjectTypes::VK_Buffer,
        nativeBuffer,
        desc,
        GraphicsBackend::NativeBufferProvenance{ .usage = nativeUsage }
    );
    ASSERT_TRUE(reimported);
    EXPECT_TRUE(device.isBufferReadyForGpuUse(reimported.get(), nativeUsage));
    EXPECT_EQ(reimported->getGpuVirtualAddress(), nativeDeviceAddress);
    reimported.reset();
}

TEST_F(BufferGpuReadinessTest, SubmissionRetainsBufferWhenCallerReleasesOwnerInPreSubmitHook){
    auto& device = BufferGpuReadinessTest::device();
    BufferHandle owner = device.createBuffer(
        BufferDesc().setByteSize(256u).setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(owner);
    CommandListHandle commandList = device.createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    commandList->beginTrackingBufferState(owner.get(), ResourceStates::Common);
    commandList->close();
    ASSERT_FALSE(commandList->commandRecordingFailed());
    ASSERT_TRUE(commandList->hasCommandBuffer());
    Buffer* const retainedBuffer = owner.get();
    ASSERT_GT(retainedBuffer->getReferenceCount(), 1u);

    VulkanTestBinarySemaphore signal(device);
    ASSERT_TRUE(signal.valid());
    __hidden_buffer_gpu_readiness::BufferOwnerReleaseHookContext hookContext{
        .owner = &owner,
        .retainedBuffer = retainedBuffer,
        .signal = signal.nativeSignal(),
    };
    const QueueSubmissionDesc hookedSubmission{
        .preSubmitHook = QueueSubmissionPreSubmitHook{
            .context = &hookContext,
            .invoke = __hidden_buffer_gpu_readiness::ReleaseBufferOwnerDuringSubmissionHook,
        },
    };
    CommandList* const commandLists[] = { commandList.get() };
    const QueueSubmissionToken token = device.executeCommandLists(
        commandLists,
        LengthOf(commandLists),
        CommandQueue::Graphics,
        hookedSubmission
    );
    ASSERT_TRUE(token.valid());
    EXPECT_EQ(hookContext.invocationCount, 1u);
    EXPECT_FALSE(owner);
    EXPECT_FALSE(commandList->hasCommandBuffer());
    EXPECT_TRUE(device.isBufferReadyForGpuUse(retainedBuffer));
    ASSERT_TRUE(device.waitForIdle());
}


TEST_F(BufferGpuReadinessTest, SubmissionRevalidatesRetainedBufferDescriptionAfterPreSubmitHook){
    auto& device = BufferGpuReadinessTest::device();
    BufferHandle buffer = device.createBuffer(
        BufferDesc().setByteSize(256u).setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(buffer);
    CommandListHandle commandList = device.createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    commandList->beginTrackingBufferState(buffer.get(), ResourceStates::Common);
    commandList->close();
    ASSERT_FALSE(commandList->commandRecordingFailed());
    ASSERT_TRUE(commandList->hasCommandBuffer());

    VulkanTestBinarySemaphore signal(device);
    ASSERT_TRUE(signal.valid());
    __hidden_buffer_gpu_readiness::BufferDescriptionDriftHookContext hookContext{
        .buffer = buffer.get(),
        .signal = signal.nativeSignal(),
        .savedDescription = {},
        .invocationCount = 0u,
        .descriptionMutated = false,
    };
    const QueueSubmissionDesc hookedSubmission{
        .preSubmitHook = QueueSubmissionPreSubmitHook{
            .context = &hookContext,
            .invoke = __hidden_buffer_gpu_readiness::DriftBufferDescriptionDuringSubmissionHook,
        },
    };
    CommandList* const commandLists[] = { commandList.get() };
    const QueueSubmissionToken rejectedToken = device.executeCommandLists(
        commandLists,
        LengthOf(commandLists),
        CommandQueue::Graphics,
        hookedSubmission
    );
    if(hookContext.descriptionMutated){
        BufferDesc& publishedDescription = const_cast<BufferDesc&>(buffer->getDescription());
        publishedDescription = hookContext.savedDescription;
    }
    if(rejectedToken.valid())
        ASSERT_TRUE(device.waitForIdle());
    ASSERT_TRUE(hookContext.descriptionMutated);
    ASSERT_FALSE(rejectedToken.valid());
    EXPECT_EQ(hookContext.invocationCount, 1u);
    EXPECT_TRUE(commandList->hasCommandBuffer());

    const QueueSubmissionToken retryToken = device.executeCommandLists(
        commandLists,
        LengthOf(commandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(retryToken.valid());
    ASSERT_TRUE(device.waitForIdle());
}


TEST_F(BufferGpuReadinessTest, AmbiguousSynchronizationStatesDoNotInventDescriptorUsage){
    auto& device = BufferGpuReadinessTest::device();
    BufferHandle buffer = device.createBuffer(
        BufferDesc().setByteSize(256u).setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(buffer);
    ASSERT_TRUE(device.isBufferReadyForGpuUse(buffer.get()));
    EXPECT_FALSE(device.isBufferReadyForGpuUse(buffer.get(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
    EXPECT_FALSE(device.isBufferReadyForGpuUse(
        buffer.get(),
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR
    ));

    static constexpr ResourceStates::Mask s_AmbiguousStates[] = {
        ResourceStates::ShaderResource,
        ResourceStates::UnorderedAccess,
        ResourceStates::AccelStructWrite,
    };
    const u32 referencesBefore = buffer->getReferenceCount();
    for(const ResourceStates::Mask state : s_AmbiguousStates){
        CommandListHandle commandList = device.createCommandList();
        ASSERT_TRUE(commandList);
        commandList->open();
        commandList->beginTrackingBufferState(buffer.get(), state);
        EXPECT_FALSE(commandList->commandRecordingFailed());
        EXPECT_TRUE(commandList->hasExplicitBufferState(buffer.get()));
        commandList->close();
        EXPECT_FALSE(commandList->commandRecordingFailed());
        EXPECT_TRUE(commandList->hasCommandBuffer());
    }
    EXPECT_EQ(buffer->getReferenceCount(), referencesBefore);
}


TEST_F(BufferGpuReadinessTest, NullUnknownScopeConflictAndRetentionContractsAreExact){
    auto& device = BufferGpuReadinessTest::device();
    CommandListHandle commandList = device.createCommandList();
    ASSERT_TRUE(commandList);

    commandList->beginTrackingBufferState(nullptr, ResourceStates::Unknown);
    commandList->setBufferState(nullptr, ResourceStates::CopyDest);
    commandList->setPermanentBufferState(nullptr, ResourceStates::CopyDest);
    commandList->releaseBufferOwnership(nullptr, GpuPhysicalQueueId{});
    commandList->releaseBufferOwnership(nullptr, static_cast<CommandQueue::Enum>(UINT8_MAX));
    EXPECT_FALSE(commandList->commandRecordingFailed());
    EXPECT_FALSE(commandList->hasCommandBuffer());

    BufferHandle tracked = device.createBuffer(
        BufferDesc().setByteSize(256u).setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(tracked);
    commandList->beginTrackingBufferState(tracked.get(), ResourceStates::Common);
    EXPECT_TRUE(commandList->commandRecordingFailed());

    commandList->open();
    commandList->beginTrackingBufferState(tracked.get(), ResourceStates::Unknown);
    EXPECT_TRUE(commandList->commandRecordingFailed());
    EXPECT_FALSE(commandList->hasExplicitBufferState(tracked.get()));
    commandList->close();
    EXPECT_FALSE(commandList->hasCommandBuffer());

    commandList->open();
    const u32 trackedReferences = tracked->getReferenceCount();
    commandList->beginTrackingBufferState(tracked.get(), ResourceStates::Common);
    commandList->beginTrackingBufferState(tracked.get(), ResourceStates::Common);
    EXPECT_FALSE(commandList->commandRecordingFailed());
    EXPECT_TRUE(commandList->hasExplicitBufferState(tracked.get()));
    EXPECT_EQ(tracked->getReferenceCount(), trackedReferences + 1u);
    commandList->close();
    EXPECT_TRUE(commandList->hasCommandBuffer());

    BufferHandle permanent = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    ASSERT_TRUE(permanent);
    CommandListHandle permanentList = device.createCommandList();
    ASSERT_TRUE(permanentList);
    permanentList->open();
    const u32 permanentReferences = permanent->getReferenceCount();
    permanentList->setPermanentBufferState(permanent.get(), ResourceStates::Common);
    EXPECT_FALSE(permanentList->commandRecordingFailed());
    EXPECT_EQ(permanentList->getPermanentBufferState(permanent.get()), ResourceStates::Common);
    EXPECT_EQ(permanent->getReferenceCount(), permanentReferences + 2u);
    permanentList->close();
    ASSERT_TRUE(permanentList->hasCommandBuffer());

    CommandListHandle conflict = device.createCommandList();
    ASSERT_TRUE(conflict);
    conflict->open();
    conflict->setPermanentBufferState(permanent.get(), ResourceStates::Common);
    ASSERT_FALSE(conflict->commandRecordingFailed());
    const ResourceStates::Mask stateBeforeConflict = conflict->getBufferState(permanent.get());
    const u32 referencesBeforeConflict = permanent->getReferenceCount();
    ASSERT_TRUE(conflict->hasExplicitBufferState(permanent.get()));
    conflict->beginTrackingBufferState(permanent.get(), ResourceStates::CopyDest);
    EXPECT_TRUE(conflict->commandRecordingFailed());
    EXPECT_EQ(conflict->getPermanentBufferState(permanent.get()), ResourceStates::Common);
    EXPECT_EQ(conflict->getBufferState(permanent.get()), stateBeforeConflict);
    EXPECT_TRUE(conflict->hasExplicitBufferState(permanent.get()));
    EXPECT_EQ(permanent->getReferenceCount(), referencesBeforeConflict);
    conflict->close();
    EXPECT_FALSE(conflict->hasCommandBuffer());
    EXPECT_EQ(conflict->getPermanentBufferState(permanent.get()), ResourceStates::Unknown);
}


TEST_F(BufferGpuReadinessTest, UnboundVirtualStateIngressRejectsWithoutPublishing){
    auto& device = BufferGpuReadinessTest::device();
    BufferHandle unbound = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setIsVirtual(true)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    ASSERT_TRUE(unbound);
    ASSERT_FALSE(device.isBufferReadyForGpuUse(unbound.get()));

    const auto expectRejectedWithoutState = [&](auto&& operation){
        const u32 referencesBefore = unbound->getReferenceCount();
        CommandListResourceStateHandoff rejectedState(BufferGpuReadinessTest::arena());
        CommandListHandle commandList = device.createCommandList();
        ASSERT_TRUE(commandList);
        commandList->open();
        operation(*commandList);
        EXPECT_TRUE(commandList->commandRecordingFailed());
        EXPECT_FALSE(commandList->hasExplicitBufferState(unbound.get()));
        EXPECT_EQ(commandList->getPermanentBufferState(unbound.get()), ResourceStates::Unknown);
        EXPECT_EQ(unbound->getReferenceCount(), referencesBefore);
        commandList->close(&rejectedState);
        EXPECT_FALSE(commandList->hasCommandBuffer());
        EXPECT_FALSE(rejectedState.valid());
    };

    expectRejectedWithoutState([&](CommandList& commandList){
        commandList.beginTrackingBufferState(unbound.get(), ResourceStates::Common);
    });
    expectRejectedWithoutState([&](CommandList& commandList){
        commandList.setBufferState(unbound.get(), ResourceStates::Common);
    });
    expectRejectedWithoutState([&](CommandList& commandList){
        commandList.setPermanentBufferState(unbound.get(), ResourceStates::Common);
    });
    expectRejectedWithoutState([&](CommandList& commandList){
        commandList.setEnableUavBarriersForBuffer(unbound.get(), true);
    });
    expectRejectedWithoutState([&](CommandList& commandList){
        commandList.releaseBufferOwnership(
            unbound.get(),
            device.getPrimaryPhysicalQueue(CommandQueue::Graphics)
        );
    });
}


TEST_F(BufferGpuReadinessTest, BufferUavBarrierPolicyOwnsItsBufferAndRewritesAreReferenceIdempotent){
    auto& device = BufferGpuReadinessTest::device();
    BufferHandle buffer = device.createBuffer(
        BufferDesc().setByteSize(256u).setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(buffer);
    Buffer* const rawBuffer = buffer.get();

    CommandListHandle commandList = device.createCommandList();
    ASSERT_TRUE(commandList);
    const u32 callerReferences = rawBuffer->getReferenceCount();
    commandList->setEnableUavBarriersForBuffer(rawBuffer, true);
    EXPECT_TRUE(commandList->commandRecordingFailed());
    EXPECT_EQ(rawBuffer->getReferenceCount(), callerReferences);

    commandList->open();
    commandList->setEnableUavBarriersForBuffer(rawBuffer, true);
    ASSERT_FALSE(commandList->commandRecordingFailed());
    ASSERT_EQ(rawBuffer->getReferenceCount(), callerReferences + 1u);
    commandList->setEnableUavBarriersForBuffer(rawBuffer, false);
    EXPECT_EQ(rawBuffer->getReferenceCount(), callerReferences + 1u);
    commandList->setEnableUavBarriersForBuffer(rawBuffer, true);
    EXPECT_EQ(rawBuffer->getReferenceCount(), callerReferences + 1u);
    commandList->close();
    ASSERT_TRUE(commandList->hasCommandBuffer());

    buffer.reset();
    ASSERT_EQ(rawBuffer->getReferenceCount(), 1u);
    commandList->open();
    commandList->close();
    ASSERT_EQ(rawBuffer->getReferenceCount(), 1u);

    BufferHandle observer(rawBuffer, BufferHandle::deleter_type(&BufferGpuReadinessTest::arena()));
    ASSERT_EQ(rawBuffer->getReferenceCount(), 2u);
    commandList.reset();
    EXPECT_EQ(observer->getReferenceCount(), 1u);
}


TEST_F(BufferGpuReadinessTest, VirtualBufferBindsAfterRejectionAndFreshRecordingSucceeds){
    auto& device = BufferGpuReadinessTest::device();
    BufferHandle placed = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setIsVirtual(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(placed);

    CommandListHandle rejected = device.createCommandList();
    ASSERT_TRUE(rejected);
    rejected->open();
    rejected->beginTrackingBufferState(placed.get(), ResourceStates::Common);
    ASSERT_TRUE(rejected->commandRecordingFailed());
    rejected->close();
    ASSERT_FALSE(rejected->hasCommandBuffer());

    const MemoryRequirements requirements = device.getBufferMemoryRequirements(placed.get());
    ASSERT_GT(requirements.size, 0u);
    HeapHandle heap = device.createHeap(HeapDesc{
        .capacity = requirements.size,
        .type = HeapType::DeviceLocal,
        .debugName = Name("tests/buffer_gpu_readiness/placed_heap"),
    });
    ASSERT_TRUE(heap);
    if(!device.bindBufferMemory(placed.get(), heap.get(), 0u))
        GTEST_SKIP() << "Buffer GPU readiness: DeviceLocal heap is incompatible with virtual buffers.";
    ASSERT_TRUE(device.isBufferReadyForGpuUse(placed.get()));

    CommandListResourceStateHandoff finalState(BufferGpuReadinessTest::arena());
    CommandListHandle retry = device.createCommandList();
    ASSERT_TRUE(retry);
    retry->open();
    retry->beginTrackingBufferState(placed.get(), ResourceStates::Common);
    retry->clearBufferUInt(placed.get(), 0u);
    retry->close(&finalState);
    ASSERT_FALSE(retry->commandRecordingFailed());
    ASSERT_TRUE(retry->hasCommandBuffer());
    ASSERT_TRUE(finalState.valid());

    CommandListHandle consumer = device.createCommandList();
    ASSERT_TRUE(consumer);
    consumer->open(&finalState);
    consumer->close();
    EXPECT_FALSE(consumer->commandRecordingFailed());
    EXPECT_TRUE(consumer->hasCommandBuffer());
}


TEST_F(BufferGpuReadinessTest, DirectBufferOperationsRejectUnboundOperandsWithoutPublishingEarlierState){
    auto& device = BufferGpuReadinessTest::device();
    BufferHandle readySource = device.createBuffer(
        BufferDesc().setByteSize(256u).setInitialState(ResourceStates::Common)
    );
    BufferHandle unbound = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setIsVirtual(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(readySource);
    ASSERT_TRUE(unbound);
    ASSERT_TRUE(device.isBufferReadyForGpuUse(readySource.get()));
    ASSERT_FALSE(device.isBufferReadyForGpuUse(unbound.get()));

    constexpr u32 s_WriteValue = 0x6e57424cu;
    const auto expectRejectedWithoutPublication = [&](auto&& operation){
        const u32 sourceReferences = readySource->getReferenceCount();
        const u32 unboundReferences = unbound->getReferenceCount();
        CommandListHandle commandList = device.createCommandList();
        ASSERT_TRUE(commandList);
        commandList->open();
        operation(*commandList);
        EXPECT_TRUE(commandList->commandRecordingFailed());
        EXPECT_FALSE(commandList->hasExplicitBufferState(readySource.get()));
        EXPECT_FALSE(commandList->hasExplicitBufferState(unbound.get()));
        EXPECT_EQ(readySource->getReferenceCount(), sourceReferences);
        EXPECT_EQ(unbound->getReferenceCount(), unboundReferences);
        commandList->close();
        EXPECT_FALSE(commandList->hasCommandBuffer());
    };

    expectRejectedWithoutPublication([&](CommandList& commandList){
        EXPECT_FALSE(commandList.tryWriteBuffer(unbound.get(), &s_WriteValue, sizeof(s_WriteValue)));
    });
    expectRejectedWithoutPublication([&](CommandList& commandList){
        commandList.clearBufferUInt(unbound.get(), s_WriteValue);
    });
    expectRejectedWithoutPublication([&](CommandList& commandList){
        commandList.copyBuffer(unbound.get(), 0u, readySource.get(), 0u, 64u);
    });
    expectRejectedWithoutPublication([&](CommandList& commandList){
        EXPECT_FALSE(commandList.recordPreflightedCopyBufferDirectVulkan(
            unbound.get(),
            0u,
            readySource.get(),
            0u,
            64u
        ));
    });
}


TEST_F(BufferGpuReadinessTest, DuplicateNativeBufferWrapperIsRejectedWithoutDisturbingOriginal){
    auto& device = BufferGpuReadinessTest::device();
    const BufferDesc desc = BufferDesc()
        .setByteSize(256u)
        .setInitialState(ResourceStates::Common)
        .setIsVertexBuffer(true)
        .setIsIndexBuffer(true)
    ;
    BufferHandle original = device.createBuffer(desc);
    ASSERT_TRUE(original);
    ASSERT_TRUE(device.isBufferReadyForGpuUse(original.get()));
    const Object nativeBuffer = original->getNativeHandle(GraphicsBackend::ObjectTypes::VK_Buffer);
    ASSERT_NE(nativeBuffer, nullptr);
    const u32 originalReferences = original->getReferenceCount();
    VkBufferUsageFlags nativeUsage =
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT
        | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT
        | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT
        | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
        | VK_BUFFER_USAGE_INDEX_BUFFER_BIT
    ;
    if(device.isBufferReadyForGpuUse(original.get(), VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT))
        nativeUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    BufferHandle duplicate = device.createHandleForNativeBuffer(
        GraphicsBackend::ObjectTypes::VK_Buffer,
        nativeBuffer,
        desc,
        GraphicsBackend::NativeBufferProvenance{ .usage = nativeUsage }
    );
    EXPECT_FALSE(duplicate);
    EXPECT_EQ(original->getReferenceCount(), originalReferences);
    ASSERT_TRUE(device.isBufferReadyForGpuUse(original.get()));

    BufferHandle retryDuplicate = device.createHandleForNativeBuffer(
        GraphicsBackend::ObjectTypes::VK_Buffer,
        nativeBuffer,
        desc,
        GraphicsBackend::NativeBufferProvenance{ .usage = nativeUsage }
    );
    EXPECT_FALSE(retryDuplicate);
    EXPECT_EQ(original->getReferenceCount(), originalReferences);
    ASSERT_TRUE(device.isBufferReadyForGpuUse(original.get()));

    {
        CommandListHandle sameObjectList = device.createCommandList();
        ASSERT_TRUE(sameObjectList);
        sameObjectList->open();
        sameObjectList->setGraphicsState(
            GraphicsState()
                .addVertexBuffer(VertexBufferBinding().setBuffer(original.get()))
                .setIndexBuffer(
                    IndexBufferBinding()
                        .setBuffer(original.get())
                        .setFormat(Format::R16_UINT)
                )
        );
        sameObjectList->copyBuffer(original.get(), 128u, original.get(), 0u, 64u);
        sameObjectList->close();
        EXPECT_FALSE(sameObjectList->commandRecordingFailed());
        EXPECT_TRUE(sameObjectList->hasCommandBuffer());
    }
    EXPECT_EQ(original->getReferenceCount(), originalReferences);
}


TEST_F(BufferGpuReadinessTest, SameBufferCopyAggregatesPermanentStateAndFreshRecordingRetries){
    auto& device = BufferGpuReadinessTest::device();
    BufferHandle buffer = device.createBuffer(
        BufferDesc().setByteSize(256u).setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(buffer);

    CommandListHandle commandList = device.createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    commandList->setPermanentBufferState(buffer.get(), ResourceStates::CopySource);
    ASSERT_FALSE(commandList->commandRecordingFailed());
    const ResourceStates::Mask stateBeforeConflict = commandList->getBufferState(buffer.get());
    const u32 referencesBeforeConflict = buffer->getReferenceCount();
    commandList->copyBuffer(buffer.get(), 128u, buffer.get(), 0u, 64u);
    EXPECT_TRUE(commandList->commandRecordingFailed());
    EXPECT_EQ(commandList->getPermanentBufferState(buffer.get()), ResourceStates::CopySource);
    EXPECT_EQ(commandList->getBufferState(buffer.get()), stateBeforeConflict);
    EXPECT_EQ(buffer->getReferenceCount(), referencesBeforeConflict);
    commandList->close();
    EXPECT_FALSE(commandList->hasCommandBuffer());
    EXPECT_EQ(commandList->getPermanentBufferState(buffer.get()), ResourceStates::Unknown);

    commandList->open();
    commandList->copyBuffer(buffer.get(), 128u, buffer.get(), 0u, 64u);
    commandList->close();
    EXPECT_FALSE(commandList->commandRecordingFailed());
    EXPECT_TRUE(commandList->hasCommandBuffer());
}


TEST_F(BufferGpuReadinessTest, GraphicsComputeAndMeshPreflightForeignBuffersBeforePublishingStateOrReferences){
    auto& device = BufferGpuReadinessTest::device();
    HeadlessGraphicsScope foreignScope;
    if(!foreignScope.initialize())
        GTEST_SKIP() << "Buffer GPU readiness: second validation-backed headless device is unavailable.";
    auto& foreignDevice = foreignScope.graphics().getDevice();

    BufferHandle localGraphicsBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setInitialState(ResourceStates::Common)
            .setIsVertexBuffer(true)
            .setIsIndexBuffer(true)
    );
    BufferHandle foreignIndirectBuffer = foreignDevice.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setInitialState(ResourceStates::Common)
            .setIsDrawIndirectArgs(true)
    );
    ASSERT_TRUE(localGraphicsBuffer);
    ASSERT_TRUE(foreignIndirectBuffer);
    ASSERT_TRUE(device.isBufferReadyForGpuUse(localGraphicsBuffer.get()));
    ASSERT_FALSE(device.isBufferReadyForGpuUse(foreignIndirectBuffer.get()));

    ShaderDesc shaderDesc(BufferGpuReadinessTest::arena());
    shaderDesc
        .setShaderType(ShaderType::Compute)
        .setDebugName(Name{"tests/buffer_gpu_readiness/foreign_indirect"})
    ;
    ShaderHandle shader = device.createShader(
        shaderDesc,
        s_BufferReadinessComputeSpirv,
        sizeof(s_BufferReadinessComputeSpirv)
    );
    ASSERT_TRUE(shader);
    ComputePipelineDesc pipelineDesc;
    pipelineDesc.setComputeShader(shader);
    ComputePipelineHandle pipeline = device.createComputePipeline(pipelineDesc);
    ASSERT_TRUE(pipeline);

    GraphicsState graphicsState;
    graphicsState
        .addVertexBuffer(VertexBufferBinding().setBuffer(localGraphicsBuffer.get()))
        .setIndexBuffer(
            IndexBufferBinding()
                .setBuffer(localGraphicsBuffer.get())
                .setFormat(Format::R16_UINT)
        )
        .setIndirectParams(foreignIndirectBuffer.get())
    ;
    const u32 localGraphicsReferences = localGraphicsBuffer->getReferenceCount();
    const u32 foreignIndirectReferences = foreignIndirectBuffer->getReferenceCount();
    CommandListHandle graphicsList = device.createCommandList();
    ASSERT_TRUE(graphicsList);
    graphicsList->open();
    graphicsList->setGraphicsState(graphicsState);
    EXPECT_TRUE(graphicsList->commandRecordingFailed());
    EXPECT_FALSE(graphicsList->hasExplicitBufferState(localGraphicsBuffer.get()));
    EXPECT_FALSE(graphicsList->hasExplicitBufferState(foreignIndirectBuffer.get()));
    EXPECT_EQ(localGraphicsBuffer->getReferenceCount(), localGraphicsReferences);
    EXPECT_EQ(foreignIndirectBuffer->getReferenceCount(), foreignIndirectReferences);
    graphicsList->close();
    EXPECT_FALSE(graphicsList->hasCommandBuffer());

    const u32 pipelineReferences = pipeline->getReferenceCount();
    CommandListHandle computeList = device.createCommandList();
    ASSERT_TRUE(computeList);
    computeList->open();
    computeList->setComputeState(
        ComputeState()
            .setPipeline(pipeline.get())
            .setIndirectParams(foreignIndirectBuffer.get())
    );
    EXPECT_TRUE(computeList->commandRecordingFailed());
    EXPECT_FALSE(computeList->hasExplicitBufferState(foreignIndirectBuffer.get()));
    EXPECT_EQ(pipeline->getReferenceCount(), pipelineReferences);
    EXPECT_EQ(foreignIndirectBuffer->getReferenceCount(), foreignIndirectReferences);
    computeList->close();
    EXPECT_FALSE(computeList->hasCommandBuffer());

    CommandListHandle meshList = device.createCommandList();
    ASSERT_TRUE(meshList);
    meshList->open();
    meshList->setMeshletState(MeshletState().setIndirectParams(foreignIndirectBuffer.get()));
    EXPECT_TRUE(meshList->commandRecordingFailed());
    EXPECT_FALSE(meshList->hasExplicitBufferState(foreignIndirectBuffer.get()));
    EXPECT_EQ(foreignIndirectBuffer->getReferenceCount(), foreignIndirectReferences);
    meshList->close();
    EXPECT_FALSE(meshList->hasCommandBuffer());
}


TEST_F(BufferGpuReadinessTest, ForeignBufferIsRejectedAndOwnerHandoffRecovers){
    auto& device = BufferGpuReadinessTest::device();
    HeadlessGraphicsScope foreignScope;
    if(!foreignScope.initialize())
        GTEST_SKIP() << "Buffer GPU readiness: second validation-backed headless device is unavailable.";
    auto& foreignDevice = foreignScope.graphics().getDevice();

    BufferHandle foreignBuffer = foreignDevice.createBuffer(
        BufferDesc().setByteSize(256u).setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(foreignBuffer);
    ASSERT_FALSE(device.isBufferReadyForGpuUse(foreignBuffer.get()));
    ASSERT_TRUE(foreignDevice.isBufferReadyForGpuUse(foreignBuffer.get()));

    const u32 foreignReferences = foreignBuffer->getReferenceCount();
    CommandListHandle policyList = device.createCommandList();
    ASSERT_TRUE(policyList);
    policyList->open();
    policyList->setEnableUavBarriersForBuffer(foreignBuffer.get(), true);
    EXPECT_TRUE(policyList->commandRecordingFailed());
    EXPECT_FALSE(policyList->hasExplicitBufferState(foreignBuffer.get()));
    EXPECT_EQ(foreignBuffer->getReferenceCount(), foreignReferences);
    policyList->close();
    EXPECT_FALSE(policyList->hasCommandBuffer());

    CommandListHandle local = device.createCommandList();
    ASSERT_TRUE(local);
    local->open();
    local->beginTrackingBufferState(foreignBuffer.get(), ResourceStates::Common);
    EXPECT_TRUE(local->commandRecordingFailed());
    EXPECT_FALSE(local->hasExplicitBufferState(foreignBuffer.get()));
    local->close();
    EXPECT_FALSE(local->hasCommandBuffer());

    CommandListResourceStateHandoff foreignState(foreignScope.arena());
    CommandListHandle ownerProducer = foreignDevice.createCommandList();
    ASSERT_TRUE(ownerProducer);
    ownerProducer->open();
    ownerProducer->beginTrackingBufferState(foreignBuffer.get(), ResourceStates::Common);
    ownerProducer->close(&foreignState);
    ASSERT_TRUE(foreignState.valid());

    CommandListHandle ownerConsumer = foreignDevice.createCommandList();
    ASSERT_TRUE(ownerConsumer);
    ownerConsumer->open(&foreignState);
    ownerConsumer->close();
    EXPECT_FALSE(ownerConsumer->commandRecordingFailed());
    EXPECT_TRUE(ownerConsumer->hasCommandBuffer());
}


TEST_F(BufferGpuReadinessTest, ConflictingReleaseRejectsAndSameDestinationRetryIsIdempotent){
    auto& device = BufferGpuReadinessTest::device();
    if(!device.getQueue(CommandQueue::Transfer))
        GTEST_SKIP() << "Buffer GPU readiness: adapter exposes no Transfer queue.";

    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    const GpuPhysicalQueueId transferQueue = device.getPrimaryPhysicalQueue(CommandQueue::Transfer);
    if(graphicsQueue == transferQueue)
        GTEST_SKIP() << "Buffer GPU readiness: Graphics and Transfer use the same exact queue.";

    BufferHandle buffer = device.createBuffer(
        BufferDesc().setByteSize(256u).setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(buffer);

    CommandListHandle conflicting = device.createCommandList();
    ASSERT_TRUE(conflicting);
    conflicting->open();
    conflicting->releaseBufferOwnership(buffer.get(), graphicsQueue);
    ASSERT_FALSE(conflicting->commandRecordingFailed());
    conflicting->releaseBufferOwnership(buffer.get(), transferQueue);
    EXPECT_TRUE(conflicting->commandRecordingFailed());
    conflicting->close();
    EXPECT_FALSE(conflicting->hasCommandBuffer());

    CommandListResourceStateHandoff recoveredState(BufferGpuReadinessTest::arena());
    CommandListHandle recovered = device.createCommandList();
    ASSERT_TRUE(recovered);
    recovered->open();
    recovered->releaseBufferOwnership(buffer.get(), transferQueue);
    recovered->releaseBufferOwnership(buffer.get(), transferQueue);
    recovered->close(&recoveredState);
    EXPECT_FALSE(recovered->commandRecordingFailed());
    EXPECT_TRUE(recovered->hasCommandBuffer());
    EXPECT_TRUE(recoveredState.valid());
}


TEST_F(BufferGpuReadinessTest, CloseAndHandoffDefensesRejectBackingMutationAtomically){
    auto& device = BufferGpuReadinessTest::device();
    BufferHandle first = device.createBuffer(
        BufferDesc().setByteSize(256u).setInitialState(ResourceStates::Common)
    );
    BufferHandle second = device.createBuffer(
        BufferDesc().setByteSize(256u).setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);

    CommandListHandle closeProbe = device.createCommandList();
    ASSERT_TRUE(closeProbe);
    closeProbe->open();
    closeProbe->beginTrackingBufferState(first.get(), ResourceStates::Common);
    BufferDesc& firstDesc = const_cast<BufferDesc&>(first->getDescription());
    firstDesc.isVirtual = true;
    closeProbe->close();
    firstDesc.isVirtual = false;
    EXPECT_TRUE(closeProbe->commandRecordingFailed());
    EXPECT_FALSE(closeProbe->hasCommandBuffer());

    closeProbe->open();
    closeProbe->beginTrackingBufferState(first.get(), ResourceStates::Common);
    closeProbe->close();
    ASSERT_FALSE(closeProbe->commandRecordingFailed());
    ASSERT_TRUE(closeProbe->hasCommandBuffer());

    CommandListResourceStateHandoff producerState(BufferGpuReadinessTest::arena());
    CommandListHandle producer = device.createCommandList();
    ASSERT_TRUE(producer);
    producer->open();
    producer->beginTrackingBufferState(first.get(), ResourceStates::Common);
    producer->beginTrackingBufferState(second.get(), ResourceStates::Common);
    producer->close(&producerState);
    ASSERT_TRUE(producerState.valid());

    CommandListHandle consumer = device.createCommandList();
    ASSERT_TRUE(consumer);
    const u32 firstReferences = first->getReferenceCount();
    const u32 secondReferences = second->getReferenceCount();
    BufferDesc& secondDesc = const_cast<BufferDesc&>(second->getDescription());
    secondDesc.isVirtual = true;
    consumer->open(&producerState);
    secondDesc.isVirtual = false;
    EXPECT_TRUE(consumer->commandRecordingFailed());
    EXPECT_FALSE(consumer->hasCommandBuffer());
    EXPECT_FALSE(consumer->hasExplicitBufferState(first.get()));
    EXPECT_FALSE(consumer->hasExplicitBufferState(second.get()));
    EXPECT_EQ(first->getReferenceCount(), firstReferences);
    EXPECT_EQ(second->getReferenceCount(), secondReferences);
    consumer->close();

    consumer->open(&producerState);
    consumer->close();
    EXPECT_FALSE(consumer->commandRecordingFailed());
    EXPECT_TRUE(consumer->hasCommandBuffer());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

