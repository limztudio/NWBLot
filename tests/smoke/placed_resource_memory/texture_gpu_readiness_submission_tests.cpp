// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Texture close/submission revocation, queue-boundary defense, and framebuffer-ledger coverage.


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


namespace __hidden_texture_gpu_readiness_submission{


struct SubmissionHookObserver{
    u32 invocationCount = 0u;
};


[[nodiscard]] static bool RejectObservedSubmissionHook(
    void* const rawContext,
    const GpuPhysicalQueueId& executionQueue,
    QueueSubmissionNativeSignal& outSignal
){
    SubmissionHookObserver* const observer = static_cast<SubmissionHookObserver*>(rawContext);
    if(!observer || !executionQueue.valid())
        return false;
    ++observer->invocationCount;
    outSignal = {};
    return false;
}


#if !defined(NWB_FINAL)
struct TextureRevocationHookContext{
    GraphicsBackend::Device* device = nullptr;
    Texture* texture = nullptr;
    Object nativeImage;
    QueueSubmissionNativeSignal signal;
    u32 invocationCount = 0u;
};


[[nodiscard]] static bool RevokeTextureDuringSubmissionHook(
    void* const rawContext,
    const GpuPhysicalQueueId& executionQueue,
    QueueSubmissionNativeSignal& outSignal
){
    TextureRevocationHookContext* const context = static_cast<TextureRevocationHookContext*>(rawContext);
    if(
        !context
        || !context->device
        || !context->texture
        || !executionQueue.valid()
        || !context->signal.valid()
    )
        return false;

    ++context->invocationCount;
    if(!context->device->revokeUnmanagedNativeTextureForTesting(context->texture, context->nativeImage))
        return false;
    outSignal = context->signal;
    return true;
}
#endif


};


class TextureGpuReadinessSubmissionTest : public ::testing::Test{
protected:
    static void SetUpTestSuite(){
        s_logger.emplace();
        s_loggerGuard.emplace(*s_logger);

        s_scope = MakeUnique<HeadlessGraphicsScope>();
        if(!s_scope->initialize()){
            GTEST_SKIP() << "Texture GPU-readiness submission tests: no validation-enabled headless Vulkan device.";
            return;
        }
        s_validationBackedDeviceInitialized = true;
    }

    static void TearDownTestSuite(){
        s_scope.reset();
        if(s_validationBackedDeviceInitialized && s_logger.has_value()){
            EXPECT_FALSE(s_logger->sawMessageContaining(NWB_TEXT("Vulkan debug: [severity=error")))
                << "Texture GPU-readiness submission tests emitted a Vulkan severity=error message";
        }
        s_loggerGuard.reset();
        s_logger.reset();
        s_validationBackedDeviceInitialized = false;
    }

    [[nodiscard]] static GraphicsBackend::Device& device(){
        return s_scope->graphics().getDevice();
    }

    [[nodiscard]] static TextureDesc baseTextureDesc(){
        return TextureDesc()
            .setWidth(16u)
            .setHeight(16u)
            .setFormat(Format::RGBA8_UNORM)
            .setInitialState(ResourceStates::Common)
        ;
    }

    [[nodiscard]] static TextureDesc unmanagedStateTextureDesc(){
        return baseTextureDesc().setKeepInitialState(true);
    }

    [[nodiscard]] static QueueSubmissionToken submit(
        CommandList& commandList,
        const QueueSubmissionDesc& submitDesc = {}
    ){
        CommandList* const commandLists[]{ &commandList };
        return device().executeCommandLists(
            commandLists,
            LengthOf(commandLists),
            commandList.getDescription().physicalQueue,
            submitDesc
        );
    }

protected:
    static bool s_validationBackedDeviceInitialized;
    static UniquePtr<HeadlessGraphicsScope> s_scope;
    static Optional<CapturingLogger> s_logger;
    static Optional<Common::LoggerRegistrationGuard> s_loggerGuard;
};

bool TextureGpuReadinessSubmissionTest::s_validationBackedDeviceInitialized = false;
UniquePtr<HeadlessGraphicsScope> TextureGpuReadinessSubmissionTest::s_scope;
Optional<CapturingLogger> TextureGpuReadinessSubmissionTest::s_logger;
Optional<Common::LoggerRegistrationGuard> TextureGpuReadinessSubmissionTest::s_loggerGuard;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(TextureGpuReadinessSubmissionTest, VirtualFramebufferCreationIsReadinessNeutralButRenderingRejects){
    TextureDesc desc = baseTextureDesc().setInRenderTarget(true);
    desc.isVirtual = true;
    TextureHandle texture = device().createTexture(desc);
    ASSERT_TRUE(texture);
    ASSERT_FALSE(device().isTextureReadyForGpuUse(texture.get()));

    FramebufferHandle framebuffer = device().createFramebuffer(
        FramebufferDesc().addColorAttachment(texture.get())
    );
    ASSERT_TRUE(framebuffer);

    CommandListHandle commandList = device().createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    commandList->setGraphicsState(GraphicsState{}.setFramebuffer(framebuffer.get()));
    EXPECT_TRUE(commandList->commandRecordingFailed());
    commandList->close();
    EXPECT_FALSE(commandList->hasCommandBuffer());
}


TEST_F(TextureGpuReadinessSubmissionTest, FramebufferRenderPassRetainsAttachmentThroughClosedRecording){
    TextureDesc desc = baseTextureDesc().setInRenderTarget(true);
    TextureHandle texture = device().createTexture(desc);
    ASSERT_TRUE(texture);
    Texture* const rawTexture = texture.get();
    FramebufferHandle framebuffer = device().createFramebuffer(
        FramebufferDesc().addColorAttachment(rawTexture)
    );
    ASSERT_TRUE(framebuffer);

    const u32 baselineReferences = rawTexture->getReferenceCount();
    CommandListHandle commandList = device().createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    commandList->setGraphicsState(GraphicsState{}.setFramebuffer(framebuffer.get()));
    ASSERT_FALSE(commandList->commandRecordingFailed());
    EXPECT_GT(rawTexture->getReferenceCount(), baselineReferences);

    texture.reset();
    framebuffer.reset();
    EXPECT_GT(rawTexture->getReferenceCount(), 0u);
    commandList->close();
    EXPECT_FALSE(commandList->commandRecordingFailed());
    EXPECT_TRUE(commandList->hasCommandBuffer());
    ASSERT_TRUE(submit(*commandList).valid());
    EXPECT_TRUE(device().waitForIdle());
}


TEST_F(TextureGpuReadinessSubmissionTest, ClosedTrackedTextureReadinessRejectsBeforeHookAndRetriesSameLease){
    TextureHandle texture = device().createTexture(baseTextureDesc());
    ASSERT_TRUE(texture);
    CommandListHandle commandList = device().createCommandList();
    ASSERT_TRUE(commandList);

    const u32 baselineReferences = texture->getReferenceCount();
    commandList->open();
    commandList->beginTrackingTextureState(texture.get(), s_AllSubresources, ResourceStates::Common);
    ASSERT_FALSE(commandList->commandRecordingFailed());
    EXPECT_EQ(texture->getReferenceCount(), baselineReferences + 1u);
    commandList->close();
    ASSERT_FALSE(commandList->commandRecordingFailed());
    ASSERT_TRUE(commandList->hasCommandBuffer());
    const u64 recordingLease = commandList->recordingLeaseSerial();

    TextureDesc& mutableDesc = const_cast<TextureDesc&>(texture->getDescription());
    mutableDesc.isVirtual = true;
    ASSERT_FALSE(device().isTextureReadyForGpuUse(texture.get()));
    __hidden_texture_gpu_readiness_submission::SubmissionHookObserver hookObserver;
    const QueueSubmissionDesc hookedSubmission{
        .preSubmitHook = QueueSubmissionPreSubmitHook{
            .context = &hookObserver,
            .invoke = __hidden_texture_gpu_readiness_submission::RejectObservedSubmissionHook,
        },
    };
    EXPECT_FALSE(submit(*commandList, hookedSubmission).valid());
    EXPECT_EQ(hookObserver.invocationCount, 0u);
    EXPECT_TRUE(commandList->hasCommandBuffer());
    EXPECT_EQ(commandList->recordingLeaseSerial(), recordingLease);

    mutableDesc.isVirtual = false;
    ASSERT_TRUE(device().isTextureReadyForGpuUse(texture.get()));
    ASSERT_TRUE(submit(*commandList).valid());
    EXPECT_TRUE(device().waitForIdle());
}


TEST_F(TextureGpuReadinessSubmissionTest, ClosedFramebufferAttachmentReadinessRejectsAndRetriesSameLease){
    TextureHandle texture = device().createTexture(baseTextureDesc().setInRenderTarget(true));
    ASSERT_TRUE(texture);
    FramebufferHandle framebuffer = device().createFramebuffer(
        FramebufferDesc().addColorAttachment(texture.get())
    );
    ASSERT_TRUE(framebuffer);
    CommandListHandle commandList = device().createCommandList();
    ASSERT_TRUE(commandList);

    commandList->open();
    commandList->setGraphicsState(GraphicsState{}.setFramebuffer(framebuffer.get()));
    commandList->close();
    ASSERT_FALSE(commandList->commandRecordingFailed());
    ASSERT_TRUE(commandList->hasCommandBuffer());
    const u64 recordingLease = commandList->recordingLeaseSerial();

    TextureDesc& mutableDesc = const_cast<TextureDesc&>(texture->getDescription());
    mutableDesc.isVirtual = true;
    ASSERT_FALSE(device().isTextureReadyForGpuUse(texture.get()));
    __hidden_texture_gpu_readiness_submission::SubmissionHookObserver hookObserver;
    const QueueSubmissionDesc hookedSubmission{
        .preSubmitHook = QueueSubmissionPreSubmitHook{
            .context = &hookObserver,
            .invoke = __hidden_texture_gpu_readiness_submission::RejectObservedSubmissionHook,
        },
    };
    EXPECT_FALSE(submit(*commandList, hookedSubmission).valid());
    EXPECT_EQ(hookObserver.invocationCount, 0u);
    EXPECT_TRUE(commandList->hasCommandBuffer());
    EXPECT_EQ(commandList->recordingLeaseSerial(), recordingLease);

    mutableDesc.isVirtual = false;
    ASSERT_TRUE(device().isTextureReadyForGpuUse(texture.get()));
    ASSERT_TRUE(submit(*commandList).valid());
    EXPECT_TRUE(device().waitForIdle());
}


#if !defined(NWB_FINAL)
TEST_F(TextureGpuReadinessSubmissionTest, RevocationBeforeCloseRollsBackProvisionalPermanentState){
    const Object nativeImage(static_cast<u64>(0x51a00001u));
    TextureHandle texture = device().createHandleForNativeTexture(
        GraphicsBackend::ObjectTypes::VK_Image,
        nativeImage,
        unmanagedStateTextureDesc()
    );
    ASSERT_TRUE(texture);
    CommandListHandle commandList = device().createCommandList();
    ASSERT_TRUE(commandList);

    commandList->open();
    commandList->setPermanentTextureState(texture.get(), ResourceStates::Common);
    ASSERT_FALSE(commandList->commandRecordingFailed());
    ASSERT_EQ(commandList->getPermanentTextureState(texture.get()), ResourceStates::Common);
    ASSERT_TRUE(device().revokeUnmanagedNativeTextureForTesting(texture.get(), nativeImage));

    commandList->close();
    EXPECT_TRUE(commandList->commandRecordingFailed());
    EXPECT_FALSE(commandList->hasCommandBuffer());
    EXPECT_EQ(commandList->getPermanentTextureState(texture.get()), ResourceStates::Unknown);
    device().releaseRevokedNativeTextureIdentityForTesting(texture.get(), nativeImage);
}


TEST_F(TextureGpuReadinessSubmissionTest, ClosedRevokedTextureRejectsBeforeHookAndFreshWrapperDoesNotRetagOldList){
    const Object nativeImage(static_cast<u64>(0x51a00002u));
    const TextureDesc desc = unmanagedStateTextureDesc();
    TextureHandle oldWrapper = device().createHandleForNativeTexture(
        GraphicsBackend::ObjectTypes::VK_Image,
        nativeImage,
        desc
    );
    ASSERT_TRUE(oldWrapper);
    CommandListHandle commandList = device().createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    commandList->setPermanentTextureState(oldWrapper.get(), ResourceStates::Common);
    commandList->close();
    ASSERT_FALSE(commandList->commandRecordingFailed());
    ASSERT_TRUE(commandList->hasCommandBuffer());
    const u64 recordingLease = commandList->recordingLeaseSerial();

    ASSERT_TRUE(device().revokeUnmanagedNativeTextureForTesting(oldWrapper.get(), nativeImage));
    __hidden_texture_gpu_readiness_submission::SubmissionHookObserver hookObserver;
    const QueueSubmissionDesc hookedSubmission{
        .preSubmitHook = QueueSubmissionPreSubmitHook{
            .context = &hookObserver,
            .invoke = __hidden_texture_gpu_readiness_submission::RejectObservedSubmissionHook,
        },
    };
    EXPECT_FALSE(submit(*commandList, hookedSubmission).valid());
    EXPECT_EQ(hookObserver.invocationCount, 0u);
    EXPECT_TRUE(commandList->hasCommandBuffer());
    EXPECT_EQ(commandList->recordingLeaseSerial(), recordingLease);

    device().releaseRevokedNativeTextureIdentityForTesting(oldWrapper.get(), nativeImage);
    TextureHandle replacement = device().createHandleForNativeTexture(
        GraphicsBackend::ObjectTypes::VK_Image,
        nativeImage,
        desc
    );
    ASSERT_TRUE(replacement);
    ASSERT_TRUE(device().isTextureReadyForGpuUse(replacement.get()));
    EXPECT_FALSE(device().isTextureReadyForGpuUse(oldWrapper.get()));
    EXPECT_FALSE(submit(*commandList, hookedSubmission).valid());
    EXPECT_EQ(hookObserver.invocationCount, 0u);
    EXPECT_TRUE(commandList->hasCommandBuffer());

    commandList->open();
    commandList->beginTrackingTextureState(replacement.get(), s_AllSubresources, ResourceStates::Common);
    commandList->close();
    ASSERT_FALSE(commandList->commandRecordingFailed());
    ASSERT_TRUE(submit(*commandList).valid());
    EXPECT_TRUE(device().waitForIdle());
}


TEST_F(TextureGpuReadinessSubmissionTest, HookRevocationIsRejectedByQueueWithoutDetachingClosedLease){
    const Object nativeImage(static_cast<u64>(0x51a00003u));
    TextureHandle texture = device().createHandleForNativeTexture(
        GraphicsBackend::ObjectTypes::VK_Image,
        nativeImage,
        baseTextureDesc()
    );
    ASSERT_TRUE(texture);
    CommandListHandle commandList = device().createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    commandList->beginTrackingTextureState(texture.get(), s_AllSubresources, ResourceStates::Common);
    commandList->close();
    ASSERT_FALSE(commandList->commandRecordingFailed());
    ASSERT_TRUE(commandList->hasCommandBuffer());
    const u64 recordingLease = commandList->recordingLeaseSerial();

    VulkanTestBinarySemaphore signal(device());
    ASSERT_TRUE(signal.valid());
    __hidden_texture_gpu_readiness_submission::TextureRevocationHookContext hookContext{
        .device = &device(),
        .texture = texture.get(),
        .nativeImage = nativeImage,
        .signal = signal.nativeSignal(),
    };
    const QueueSubmissionDesc hookedSubmission{
        .preSubmitHook = QueueSubmissionPreSubmitHook{
            .context = &hookContext,
            .invoke = __hidden_texture_gpu_readiness_submission::RevokeTextureDuringSubmissionHook,
        },
    };
    const QueueSubmissionToken rejectedToken = submit(*commandList, hookedSubmission);
    if(rejectedToken.valid()){
        const bool idle = device().waitForIdle();
        if(idle){
            ASSERT_FALSE(device().isTextureReadyForGpuUse(texture.get()));
            device().releaseRevokedNativeTextureIdentityForTesting(texture.get(), nativeImage);
        }
        ASSERT_TRUE(idle);
        FAIL() << "Texture revocation hook unexpectedly reached the native queue";
    }
    ASSERT_FALSE(rejectedToken.valid());
    EXPECT_EQ(hookContext.invocationCount, 1u);
    EXPECT_TRUE(commandList->hasCommandBuffer());
    EXPECT_EQ(commandList->recordingLeaseSerial(), recordingLease);

    ASSERT_FALSE(device().isTextureReadyForGpuUse(texture.get()));
    device().releaseRevokedNativeTextureIdentityForTesting(texture.get(), nativeImage);
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

