// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Buffer GPU-readiness, state-ingress atomicity, retention, close defense, and handoff defense coverage.


#include <gtest/gtest.h>

#include <global/global.h>
#include <global/unique_ptr.h>
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


#if !defined(NWB_FINAL)
namespace __hidden_buffer_gpu_readiness{


struct BufferRevocationHookContext{
    GraphicsBackend::Device* device = nullptr;
    Buffer* buffer = nullptr;
    Object nativeBuffer;
    QueueSubmissionNativeSignal signal;
    u32 invocationCount = 0u;
};


[[nodiscard]] static bool RevokeBufferDuringSubmissionHook(
    void* const rawContext,
    const GpuPhysicalQueueId& executionQueue,
    QueueSubmissionNativeSignal& outSignal
){
    BufferRevocationHookContext* const context = static_cast<BufferRevocationHookContext*>(rawContext);
    if(
        !context
        || !context->device
        || !context->buffer
        || !executionQueue.valid()
        || !context->signal.valid()
    )
        return false;

    ++context->invocationCount;
    if(!context->device->revokeBufferNativeIdentityForTesting(context->buffer, context->nativeBuffer))
        return false;
    outSignal = context->signal;
    return true;
}


};
#endif


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
        nativeUsage
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
            s_NativeUsage
        ).get() != nullptr;
    });

    const BufferDesc validNativeDesc = BufferDesc()
        .setByteSize(256u)
        .setInitialState(ResourceStates::Common)
        .setQueueSharing(ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
    ;
    BufferHandle retry = device.createHandleForNativeBuffer(
        GraphicsBackend::ObjectTypes::VK_Buffer,
        nativeBuffer,
        validNativeDesc,
        s_NativeUsage
    );
    ASSERT_TRUE(retry);
    EXPECT_EQ(
        retry->getCreationDescription().queueSharing,
        ResourceQueueSharing::GraphicsAsyncComputeAndTransfer
    );
    EXPECT_TRUE(retry->descriptionMatchesCreation());
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
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT
        ).get() != nullptr;
    });
    BufferHandle logicalRetry = device.createHandleForNativeBuffer(
        GraphicsBackend::ObjectTypes::VK_Buffer,
        logicalMismatchNativeBuffer,
        vertexDesc,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
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
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT
        ).get() != nullptr;
    });
    BufferHandle stateRetry = device.createHandleForNativeBuffer(
        GraphicsBackend::ObjectTypes::VK_Buffer,
        stateMismatchNativeBuffer,
        copyDestDesc,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT
    );
    ASSERT_TRUE(stateRetry);

    if(!device.queryFeatureSupport(Feature::RayTracingOpacityMicromap)){
        const Object featureMismatchNativeBuffer(static_cast<u64>(0x22d0000du));
        expectDiagnosticRejection([&](){
            return device.createHandleForNativeBuffer(
                GraphicsBackend::ObjectTypes::VK_Buffer,
                featureMismatchNativeBuffer,
                BufferDesc().setByteSize(256u).setInitialState(ResourceStates::Common),
                VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
            ).get() != nullptr;
        });
        BufferHandle featureRetry = device.createHandleForNativeBuffer(
            GraphicsBackend::ObjectTypes::VK_Buffer,
            featureMismatchNativeBuffer,
            BufferDesc().setByteSize(256u).setInitialState(ResourceStates::Common),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT
        );
        ASSERT_TRUE(featureRetry);
    }
}


#if !defined(NWB_FINAL)
TEST_F(BufferGpuReadinessTest, RealNativeBufferRetainsExactUsageAndAddressProvenance){
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
    if(device.isBufferReadyForGpuUse(owner.get(), VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT))
        nativeUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    const GpuVirtualAddress ownerDeviceAddress = owner->getGpuVirtualAddress();
    const Object nativeBuffer = owner->getNativeHandle(GraphicsBackend::ObjectTypes::VK_Buffer);
    ASSERT_NE(nativeBuffer, nullptr);
    ASSERT_TRUE(device.revokeBufferNativeIdentityForTesting(owner.get(), nativeBuffer));
    ASSERT_FALSE(device.isBufferReadyForGpuUse(owner.get()));

    BufferHandle imported = device.createHandleForNativeBuffer(
        GraphicsBackend::ObjectTypes::VK_Buffer,
        nativeBuffer,
        desc,
        nativeUsage
    );
    ASSERT_TRUE(imported);
    EXPECT_TRUE(device.isBufferReadyForGpuUse(imported.get(), nativeUsage));
    EXPECT_FALSE(device.isBufferReadyForGpuUse(imported.get(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT));
    EXPECT_FALSE(device.isBufferReadyForGpuUse(imported.get(), VK_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT));
    EXPECT_EQ(imported->getGpuVirtualAddress(), ownerDeviceAddress);

    imported.reset();
    ASSERT_TRUE(device.restoreBufferNativeIdentityForTesting(owner.get(), nativeBuffer));
    EXPECT_TRUE(device.isBufferReadyForGpuUse(owner.get(), nativeUsage));
}

TEST_F(BufferGpuReadinessTest, SubmissionRevalidatesRetainedBufferIdentityAndDescription){
    auto& device = BufferGpuReadinessTest::device();
    const auto recordBufferUse = [&device](Buffer& buffer){
        CommandListHandle commandList = device.createCommandList();
        if(!commandList)
            return commandList;

        commandList->open();
        commandList->beginTrackingBufferState(&buffer, ResourceStates::Common);
        commandList->close();
        return commandList;
    };
    const auto submit = [&device](CommandList& commandList){
        CommandList* const commandLists[] = { &commandList };
        return device.executeCommandLists(
            commandLists,
            LengthOf(commandLists),
            CommandQueue::Graphics,
            QueueSubmissionDesc{}
        );
    };

    BufferHandle identityBuffer = device.createBuffer(
        BufferDesc().setByteSize(256u).setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(identityBuffer);
    CommandListHandle identityList = recordBufferUse(*identityBuffer);
    ASSERT_TRUE(identityList);
    ASSERT_FALSE(identityList->commandRecordingFailed());
    ASSERT_TRUE(identityList->hasCommandBuffer());

    const Object nativeBuffer = identityBuffer->getNativeHandle(GraphicsBackend::ObjectTypes::VK_Buffer);
    ASSERT_NE(nativeBuffer, nullptr);
    VulkanTestBinarySemaphore signal(device);
    ASSERT_TRUE(signal.valid());
    __hidden_buffer_gpu_readiness::BufferRevocationHookContext hookContext{
        .device = &device,
        .buffer = identityBuffer.get(),
        .nativeBuffer = nativeBuffer,
        .signal = signal.nativeSignal(),
    };
    const QueueSubmissionDesc hookedSubmission{
        .preSubmitHook = QueueSubmissionPreSubmitHook{
            .context = &hookContext,
            .invoke = __hidden_buffer_gpu_readiness::RevokeBufferDuringSubmissionHook,
        },
    };
    CommandList* const identityLists[] = { identityList.get() };
    const QueueSubmissionToken rejectedToken = device.executeCommandLists(
        identityLists,
        LengthOf(identityLists),
        CommandQueue::Graphics,
        hookedSubmission
    );
    if(rejectedToken.valid()){
        const bool idle = device.waitForIdle();
        if(idle){
            ASSERT_TRUE(device.restoreBufferNativeIdentityForTesting(identityBuffer.get(), nativeBuffer));
        }
        ASSERT_TRUE(idle);
        FAIL() << "Buffer revocation hook unexpectedly reached the native queue";
    }
    ASSERT_FALSE(rejectedToken.valid());
    EXPECT_EQ(hookContext.invocationCount, 1u);
    EXPECT_TRUE(identityList->hasCommandBuffer());
    ASSERT_TRUE(device.restoreBufferNativeIdentityForTesting(identityBuffer.get(), nativeBuffer));
    ASSERT_TRUE(submit(*identityList).valid());
    ASSERT_TRUE(device.waitForIdle());

    BufferHandle descriptionBuffer = device.createBuffer(
        BufferDesc().setByteSize(256u).setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(descriptionBuffer);
    CommandListHandle descriptionList = recordBufferUse(*descriptionBuffer);
    ASSERT_TRUE(descriptionList);
    ASSERT_FALSE(descriptionList->commandRecordingFailed());
    ASSERT_TRUE(descriptionList->hasCommandBuffer());

    BufferDesc& publishedDesc = const_cast<BufferDesc&>(descriptionBuffer->getDescription());
    publishedDesc.isVirtual = true;
    EXPECT_FALSE(submit(*descriptionList).valid());
    publishedDesc = descriptionBuffer->getCreationDescription();
    ASSERT_TRUE(submit(*descriptionList).valid());
    EXPECT_TRUE(device.waitForIdle());
}
#endif


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
        nativeUsage
    );
    EXPECT_FALSE(duplicate);
    EXPECT_EQ(original->getReferenceCount(), originalReferences);
    ASSERT_TRUE(device.isBufferReadyForGpuUse(original.get()));

    BufferHandle retryDuplicate = device.createHandleForNativeBuffer(
        GraphicsBackend::ObjectTypes::VK_Buffer,
        nativeBuffer,
        desc,
        nativeUsage
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

