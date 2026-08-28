// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Texture close/submission readiness, queue-boundary defense, and framebuffer-ledger coverage.


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

struct TextureOwnerReleaseHookContext{
    TextureHandle* owner = nullptr;
    Texture* retainedTexture = nullptr;
    QueueSubmissionNativeSignal signal;
    u32 invocationCount = 0u;
};

struct TextureDescriptionDriftHookContext{
    Texture* texture = nullptr;
    QueueSubmissionNativeSignal signal;
    TextureDesc savedDescription;
    u32 invocationCount = 0u;
    bool descriptionMutated = false;
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


[[nodiscard]] static bool ReleaseTextureOwnerDuringSubmissionHook(
    void* const rawContext,
    const GpuPhysicalQueueId& executionQueue,
    QueueSubmissionNativeSignal& outSignal
){
    TextureOwnerReleaseHookContext* const context = static_cast<TextureOwnerReleaseHookContext*>(rawContext);
    if(
        !context
        || !context->owner
        || !*context->owner
        || context->owner->get() != context->retainedTexture
        || !executionQueue.valid()
        || !context->signal.valid()
    )
        return false;

    ++context->invocationCount;
    context->owner->reset();
    outSignal = context->signal;
    return true;
}

[[nodiscard]] static bool DriftTextureDescriptionDuringSubmissionHook(
    void* const rawContext,
    const GpuPhysicalQueueId& executionQueue,
    QueueSubmissionNativeSignal& outSignal
){
    TextureDescriptionDriftHookContext* const context = static_cast<TextureDescriptionDriftHookContext*>(rawContext);
    if(
        !context
        || !context->texture
        || !executionQueue.valid()
        || !context->signal.valid()
    )
        return false;

    context->savedDescription = context->texture->getDescription();
    TextureDesc& publishedDescription = const_cast<TextureDesc&>(context->texture->getDescription());
    publishedDescription.isVirtual = true;
    context->descriptionMutated = true;
    ++context->invocationCount;
    outSignal = context->signal;
    return true;
}


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

    [[nodiscard]] static TextureDesc retainedStateTextureDesc(){
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


TEST_F(TextureGpuReadinessSubmissionTest, DescriptionDriftBeforeCloseRollsBackProvisionalPermanentState){
    TextureHandle texture = device().createTexture(retainedStateTextureDesc());
    ASSERT_TRUE(texture);
    CommandListHandle commandList = device().createCommandList();
    ASSERT_TRUE(commandList);

    commandList->open();
    commandList->setPermanentTextureState(texture.get(), ResourceStates::Common);
    ASSERT_FALSE(commandList->commandRecordingFailed());
    ASSERT_EQ(commandList->getPermanentTextureState(texture.get()), ResourceStates::Common);

    TextureDesc& publishedDescription = const_cast<TextureDesc&>(texture->getDescription());
    publishedDescription.isVirtual = true;

    commandList->close();
    publishedDescription = texture->getCreationDescription();
    EXPECT_TRUE(commandList->commandRecordingFailed());
    EXPECT_FALSE(commandList->hasCommandBuffer());
    EXPECT_EQ(commandList->getPermanentTextureState(texture.get()), ResourceStates::Unknown);
}


TEST_F(TextureGpuReadinessSubmissionTest, SubmissionRetainsTextureWhenCallerReleasesOwnerInPreSubmitHook){
    TextureHandle owner = device().createTexture(baseTextureDesc());
    ASSERT_TRUE(owner);
    CommandListHandle commandList = device().createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    commandList->beginTrackingTextureState(owner.get(), s_AllSubresources, ResourceStates::Common);
    commandList->close();
    ASSERT_FALSE(commandList->commandRecordingFailed());
    ASSERT_TRUE(commandList->hasCommandBuffer());
    Texture* const retainedTexture = owner.get();
    ASSERT_GT(retainedTexture->getReferenceCount(), 1u);

    VulkanTestBinarySemaphore signal(device());
    ASSERT_TRUE(signal.valid());
    __hidden_texture_gpu_readiness_submission::TextureOwnerReleaseHookContext hookContext{
        .owner = &owner,
        .retainedTexture = retainedTexture,
        .signal = signal.nativeSignal(),
    };
    const QueueSubmissionDesc hookedSubmission{
        .preSubmitHook = QueueSubmissionPreSubmitHook{
            .context = &hookContext,
            .invoke = __hidden_texture_gpu_readiness_submission::ReleaseTextureOwnerDuringSubmissionHook,
        },
    };
    const QueueSubmissionToken token = submit(*commandList, hookedSubmission);
    ASSERT_TRUE(token.valid());
    EXPECT_EQ(hookContext.invocationCount, 1u);
    EXPECT_FALSE(owner);
    EXPECT_FALSE(commandList->hasCommandBuffer());
    EXPECT_TRUE(device().isTextureReadyForGpuUse(retainedTexture));
    ASSERT_TRUE(device().waitForIdle());
}


TEST_F(TextureGpuReadinessSubmissionTest, SubmissionRevalidatesRetainedTextureDescriptionAfterPreSubmitHook){
    TextureHandle texture = device().createTexture(baseTextureDesc());
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
    __hidden_texture_gpu_readiness_submission::TextureDescriptionDriftHookContext hookContext{
        .texture = texture.get(),
        .signal = signal.nativeSignal(),
        .savedDescription = {},
        .invocationCount = 0u,
        .descriptionMutated = false,
    };
    const QueueSubmissionDesc hookedSubmission{
        .preSubmitHook = QueueSubmissionPreSubmitHook{
            .context = &hookContext,
            .invoke = __hidden_texture_gpu_readiness_submission::DriftTextureDescriptionDuringSubmissionHook,
        },
    };
    const QueueSubmissionToken rejectedToken = submit(*commandList, hookedSubmission);
    if(hookContext.descriptionMutated){
        TextureDesc& publishedDescription = const_cast<TextureDesc&>(texture->getDescription());
        publishedDescription = hookContext.savedDescription;
    }
    if(rejectedToken.valid())
        ASSERT_TRUE(device().waitForIdle());
    ASSERT_TRUE(hookContext.descriptionMutated);
    ASSERT_FALSE(rejectedToken.valid());
    EXPECT_EQ(hookContext.invocationCount, 1u);
    EXPECT_TRUE(commandList->hasCommandBuffer());
    EXPECT_EQ(commandList->recordingLeaseSerial(), recordingLease);

    const QueueSubmissionToken retryToken = submit(*commandList);
    ASSERT_TRUE(retryToken.valid());
    ASSERT_TRUE(device().waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

