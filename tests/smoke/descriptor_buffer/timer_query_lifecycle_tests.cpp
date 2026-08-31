// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <global/global.h>
#include <global/unique_ptr.h>
#include <core/common/module.h>
#include <core/graphics/api.h>
#include <core/graphics/vulkan/backend.h>
#include <tests/common/capturing_logger.h>
#include <tests/common/headless_graphics_scope.h>
#include <tests/common/vulkan_test_sync.h>

#include <volk/volk.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core;


namespace __hidden_timer_query_lifecycle_tests{

struct TimerQueryRenderTarget{
    TextureHandle texture;
    FramebufferHandle framebuffer;


    explicit TimerQueryRenderTarget(GraphicsBackend::Device& device)
        : texture(
            device.createTexture(
                TextureDesc()
                    .setWidth(4u)
                    .setHeight(4u)
                    .setFormat(Format::RGBA8_UNORM)
                    .setInRenderTarget(true)
                    .setInitialState(ResourceStates::Common)
            )
        ){
        if(texture)
            framebuffer = device.createFramebuffer(FramebufferDesc().addColorAttachment(texture.get()));
    }


    [[nodiscard]] bool valid()const noexcept{ return texture && framebuffer; }
};


class TimerQueryLifecycleTest : public ::testing::Test{
protected:
    static void SetUpTestSuite(){
        s_logger.emplace();
        s_loggerGuard.emplace(*s_logger);
        s_scope = MakeUnique<HeadlessGraphicsScope>();
        if(!s_scope->initialize())
            return;

        s_runtimeInitialized = true;
    }

    static void TearDownTestSuite(){
        s_scope.reset();
        if(s_runtimeInitialized && s_logger.has_value()){
            EXPECT_FALSE(s_logger->sawMessageContaining(NWB_TEXT("Vulkan debug: [severity=error")))
                << "validation-enabled timer-query lifecycle tests emitted a Vulkan error";
        }
        s_loggerGuard.reset();
        s_logger.reset();
        s_runtimeInitialized = false;
    }

    [[nodiscard]] static GraphicsBackend::Device& device(){ return s_scope->graphics().getDevice(); }


protected:
    virtual void SetUp()override{
        if(!s_runtimeInitialized)
            GTEST_SKIP() << "Timer-query lifecycle: no usable validation-enabled headless Vulkan device.";

        m_graphicsQueue = device().getPrimaryPhysicalQueue(CommandQueue::Graphics);
        ASSERT_TRUE(m_graphicsQueue.valid());
        const GpuPhysicalQueueInfo* const queueInfo = device().getPhysicalQueueInfo(m_graphicsQueue);
        ASSERT_TRUE(queueInfo);
        if(queueInfo->timestampValidBits == 0u)
            GTEST_SKIP() << "Timer-query lifecycle: primary Graphics queue does not support timestamps.";
        m_parameters.setPhysicalQueue(m_graphicsQueue);
    }

    [[nodiscard]] bool submitResetOnly(GraphicsBackend::TimerQuery& query){
        auto commandList = device().createCommandList(m_parameters);
        if(!commandList)
            return false;

        commandList->open();
        if(!commandList->resetTimerQuery(&query))
            return false;
        commandList->close();
        CommandList* const commandLists[] = { commandList.get() };
        const QueueSubmissionToken submissionToken = device().executeCommandLists(
            commandLists,
            LengthOf(commandLists),
            m_graphicsQueue,
            QueueSubmissionDesc{}
        );
        return submissionToken.valid() && device().waitForIdle();
    }

    [[nodiscard]] bool recordAbandonedAuthorizedRenderPassCycle(
        GraphicsBackend::TimerQuery& query,
        TimerQueryRecordingToken& outRecording
    ){
        TimerQueryRenderTarget renderTarget(device());
        auto commandList = device().createCommandList(m_parameters);
        if(!renderTarget.valid() || !commandList)
            return false;

        commandList->open();
        commandList->setGraphicsState(GraphicsState().setFramebuffer(renderTarget.framebuffer.get()));
        if(!commandList->isRenderPassActive() || !commandList->beginTimerQuery(&query, outRecording))
            return false;
        if(outRecording.resetAuthorizationGeneration == 0u || !commandList->endTimerQuery(&query, outRecording))
            return false;
        commandList->endRenderPass();
        commandList->close();
        commandList.reset();
        return true;
    }

    [[nodiscard]] bool submitAuthorizedRenderPassCycle(
        GraphicsBackend::TimerQuery& query,
        const u64 disallowedResetAuthorizationGeneration,
        TimerQueryRecordingToken& outRecording
    ){
        TimerQueryRenderTarget renderTarget(device());
        auto commandList = device().createCommandList(m_parameters);
        if(!renderTarget.valid() || !commandList)
            return false;

        commandList->open();
        commandList->setGraphicsState(GraphicsState().setFramebuffer(renderTarget.framebuffer.get()));
        if(!commandList->isRenderPassActive() || !commandList->beginTimerQuery(&query, outRecording))
            return false;
        if(
            outRecording.resetAuthorizationGeneration == 0u
            || outRecording.resetAuthorizationGeneration == disallowedResetAuthorizationGeneration
            || !commandList->endTimerQuery(&query, outRecording)
        )
            return false;
        commandList->endRenderPass();
        commandList->close();

        CommandList* const commandLists[] = { commandList.get() };
        const QueueSubmissionToken submissionToken = device().executeCommandLists(
            commandLists,
            LengthOf(commandLists),
            m_graphicsQueue,
            QueueSubmissionDesc{}
        );
        if(!submissionToken.valid() || !device().waitForIdle())
            return false;

        TimerQueryResult result;
        return device().getTimerQueryResult(&query, result);
    }


protected:
    static bool s_runtimeInitialized;
    static UniquePtr<HeadlessGraphicsScope> s_scope;
    static Optional<CapturingLogger> s_logger;
    static Optional<Common::LoggerRegistrationGuard> s_loggerGuard;
    GpuPhysicalQueueId m_graphicsQueue;
    CommandListParameters m_parameters;
};

bool TimerQueryLifecycleTest::s_runtimeInitialized = false;
UniquePtr<HeadlessGraphicsScope> TimerQueryLifecycleTest::s_scope;
Optional<CapturingLogger> TimerQueryLifecycleTest::s_logger;
Optional<Common::LoggerRegistrationGuard> TimerQueryLifecycleTest::s_loggerGuard;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// An outside-render-pass begin records its own reset and must not consume or later revoke an unrelated host reset.
TEST_F(TimerQueryLifecycleTest, OutsideRenderPassBeginPreservesHostResetAuthorization){
    auto query = device().createTimerQuery();
    ASSERT_TRUE(query);
    if(!device().resetTimerQuery(query.get()))
        GTEST_SKIP() << "Timer-query lifecycle: host query reset is unavailable.";

    auto abandoned = device().createCommandList(m_parameters);
    ASSERT_TRUE(abandoned);
    abandoned->open();
    TimerQueryRecordingToken abandonedRecording;
    ASSERT_TRUE(abandoned->beginTimerQuery(query.get(), abandonedRecording));
    EXPECT_EQ(abandonedRecording.resetAuthorizationGeneration, 0u);
    ASSERT_TRUE(abandoned->endTimerQuery(query.get(), abandonedRecording));
    abandoned->close();
    abandoned.reset();
    ASSERT_TRUE(query->discardUnacceptedRecording(abandonedRecording));

    TimerQueryRecordingToken acceptedRecording;
    EXPECT_TRUE(submitAuthorizedRenderPassCycle(*query, 0u, acceptedRecording));
}


// A stale high-level scope may outlive its rolled-back native command buffer. A newer host reset owns a different
// authorization generation and must survive delayed retirement of the stale scope.
TEST_F(TimerQueryLifecycleTest, StaleDiscardPreservesNewerHostResetAuthorization){
    auto query = device().createTimerQuery();
    ASSERT_TRUE(query);
    if(!device().resetTimerQuery(query.get()))
        GTEST_SKIP() << "Timer-query lifecycle: host query reset is unavailable.";

    TimerQueryRecordingToken staleRecording;
    ASSERT_TRUE(recordAbandonedAuthorizedRenderPassCycle(*query, staleRecording));
    if(!device().resetTimerQuery(query.get()))
        GTEST_SKIP() << "Timer-query lifecycle: second host query reset is unavailable.";
    ASSERT_TRUE(query->discardUnacceptedRecording(staleRecording));

    TimerQueryRecordingToken acceptedRecording;
    EXPECT_TRUE(submitAuthorizedRenderPassCycle(
        *query,
        staleRecording.resetAuthorizationGeneration,
        acceptedRecording
    ));
}


// Reset-only submissions allocate reset-authorization identity without advancing the begin-cycle generation. The
// accepted reset therefore remains available after a delayed stale begin-cycle retirement.
TEST_F(TimerQueryLifecycleTest, StaleDiscardPreservesAcceptedCommandResetAuthorization){
    auto query = device().createTimerQuery();
    ASSERT_TRUE(query);
    ASSERT_TRUE(submitResetOnly(*query));

    TimerQueryRecordingToken staleRecording;
    ASSERT_TRUE(recordAbandonedAuthorizedRenderPassCycle(*query, staleRecording));

    auto resetOnly = device().createCommandList(m_parameters);
    ASSERT_TRUE(resetOnly);
    resetOnly->open();
    ASSERT_TRUE(resetOnly->resetTimerQuery(query.get()));
    resetOnly->close();
    CommandList* const commandLists[] = { resetOnly.get() };
    const QueueSubmissionToken resetSubmission = device().executeCommandLists(
        commandLists,
        LengthOf(commandLists),
        m_graphicsQueue,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(resetSubmission.valid());
    ASSERT_TRUE(device().waitForIdle());
    ASSERT_TRUE(query->discardUnacceptedRecording(staleRecording));

    TimerQueryRecordingToken acceptedRecording;
    EXPECT_TRUE(submitAuthorizedRenderPassCycle(
        *query,
        staleRecording.resetAuthorizationGeneration,
        acceptedRecording
    ));
}


// A failed reset-only submission must release only its reset reservation. It cannot invalidate the begin-cycle
// identity needed to retire an already rolled-back high-level timing scope.
TEST_F(TimerQueryLifecycleTest, StaleDiscardSurvivesRejectedCommandResetReservation){
    auto query = device().createTimerQuery();
    ASSERT_TRUE(query);
    ASSERT_TRUE(submitResetOnly(*query));

    TimerQueryRecordingToken staleRecording;
    ASSERT_TRUE(recordAbandonedAuthorizedRenderPassCycle(*query, staleRecording));

    auto resetOnly = device().createCommandList(m_parameters);
    ASSERT_TRUE(resetOnly);
    resetOnly->open();
    ASSERT_TRUE(resetOnly->resetTimerQuery(query.get()));
    resetOnly->close();

    const VkQueue nativeGraphicsQueue = static_cast<VkQueue>(
        device().getNativeQueue(GraphicsBackend::ObjectTypes::VK_Queue, m_graphicsQueue).pointer
    );
    ASSERT_NE(nativeGraphicsQueue, VK_NULL_HANDLE);
    CommandList* const commandLists[] = { resetOnly.get() };
    QueueSubmissionToken rejectedSubmission;
    {
        VulkanTestQueueSubmit2Observer submissionObserver(device());
        ASSERT_TRUE(submissionObserver.valid());
        ASSERT_TRUE(submissionObserver.armSubmissionFailures(nativeGraphicsQueue));
        rejectedSubmission = device().executeCommandLists(
            commandLists,
            LengthOf(commandLists),
            m_graphicsQueue,
            QueueSubmissionDesc{}
        );
        EXPECT_EQ(submissionObserver.injectedSubmissionFailureCount(), 1u);
        EXPECT_EQ(submissionObserver.pendingSubmissionFailureCount(), 0u);
    }
    EXPECT_FALSE(rejectedSubmission.valid());
    EXPECT_FALSE(resetOnly->hasCommandBuffer());
    ASSERT_TRUE(query->discardUnacceptedRecording(staleRecording));

    ASSERT_TRUE(device().resetTimerQuery(query.get()));
    TimerQueryRecordingToken acceptedRecording;
    EXPECT_TRUE(submitAuthorizedRenderPassCycle(
        *query,
        staleRecording.resetAuthorizationGeneration,
        acceptedRecording
    ));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

