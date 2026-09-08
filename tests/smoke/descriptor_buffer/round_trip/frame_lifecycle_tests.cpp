// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class OrderedCpuRenderPass final : public IRenderPass{
public:
    OrderedCpuRenderPass(GraphicsRuntime& graphics, Vector<u32, Alloc::GlobalArena>& events, Atomic<u32>& completed,
        const ThreadId mainThread, const u32 index)
        : IRenderPass(graphics)
        , m_events(events)
        , m_completed(completed)
        , m_mainThread(mainThread)
        , m_index(index)
    {}

    virtual bool prepareResources(Framebuffer*)override{
        EXPECT_EQ(QueryCurrentThreadId(), m_mainThread);
        EXPECT_EQ(m_completed.load(MemoryOrder::acquire), m_index);
        m_events.push_back(m_index * 2u);
        getGraphics().waitTasks();
        return true;
    }

    virtual void render(Framebuffer*)override{
        EXPECT_EQ(QueryCurrentThreadId(), m_mainThread);
        m_events.push_back(m_index * 2u + 1u);
        const auto child = getGraphics().scheduleGraphicsTask([this](){
            m_completed.fetch_add(1u, MemoryOrder::release);
        });
        EXPECT_TRUE(child.valid());
    }


private:
    Vector<u32, Alloc::GlobalArena>& m_events;
    Atomic<u32>& m_completed;
    ThreadId m_mainThread;
    u32 m_index;
};


TEST_F(DescriptorBufferRoundTripTest, RenderPassTasksPreserveMainThreadInterleavingAndJoinDescendants){
    auto& graphics = s_scope->graphics();
    Vector<u32, Alloc::GlobalArena> events(DescriptorBufferRoundTripTest::arena());
    Atomic<u32> completed{ 0u };
    OrderedCpuRenderPass first(graphics, events, completed, QueryCurrentThreadId(), 0u);
    OrderedCpuRenderPass second(graphics, events, completed, QueryCurrentThreadId(), 1u);
    graphics.addRenderPassToBack(first);
    graphics.addRenderPassToBack(second);
    struct RenderPassCleanup{
        GraphicsRuntime& graphics;
        IRenderPass& first;
        IRenderPass& second;

        ~RenderPassCleanup()noexcept(false){
            graphics.removeRenderPass(second);
            graphics.removeRenderPass(first);
        }
    } removePasses{ graphics, first, second };

    graphics.render();

    ASSERT_EQ(events.size(), 4u);
    for(u32 index = 0u; index < 4u; ++index)
        EXPECT_EQ(events[index], index);
    EXPECT_EQ(completed.load(MemoryOrder::acquire), 2u);
}

struct FrameCpuTimingCallbackState{
    u32 invocationCount = 0u;
};


[[nodiscard]] static bool RunFrameCpuTimingCallback(void* const userData, const f32 delta){
    static_cast<void>(delta);

    auto* const state = static_cast<FrameCpuTimingCallbackState*>(userData);
    if(!state)
        return false;

    ++state->invocationCount;
    return true;
}


TEST_F(DescriptorBufferRoundTripTest, FramePublishesMainThreadCpuTimingScopes){
    Frame frame(nullptr, 1u, 1u);
#if !defined(NWB_FINAL)
    ASSERT_TRUE(frame.graphics().setDebugRuntimeEnabled(true));
#endif
    ASSERT_TRUE(frame.graphics().setBindlessHeapAbi(Impl::AssetsGraphicsBindless::MakeGpuDescriptorHeapAbi()));
    if(!frame.graphics().createHeadlessDevice())
        GTEST_SKIP() << "Frame CPU timing: no usable headless Vulkan device on this host.";

    FrameCpuTimingCallbackState callbackState;
    frame.setProjectUpdateCallback(&RunFrameCpuTimingCallback, &callbackState);

    ASSERT_TRUE(frame.update(0.f));
    EXPECT_EQ(callbackState.invocationCount, 1u);
    const Perf::TimingView disabledTiming = frame.perfSession().cpuTimingView();
    EXPECT_EQ(disabledTiming.scopeCount(), 0u);
    EXPECT_FALSE(disabledTiming.stats(Name("frame.project_update")).valid());
    EXPECT_FALSE(disabledTiming.stats(Name("graphics.frame")).valid());
    EXPECT_FALSE(disabledTiming.stats(Name("graphics.animate")).valid());
    EXPECT_FALSE(disabledTiming.stats(Name("graphics.begin_frame")).valid());
    EXPECT_FALSE(disabledTiming.stats(Name("graphics.frame_preamble")).valid());
    EXPECT_FALSE(disabledTiming.stats(Name("graphics.render")).valid());
    EXPECT_FALSE(disabledTiming.stats(Name("graphics.present")).valid());
    EXPECT_FALSE(disabledTiming.stats(Name("graphics.garbage_collect")).valid());

    Perf::CaptureOptions cpuCapture;
    cpuCapture.enabled = true;
    cpuCapture.cpuTiming = true;
    frame.setPerfCapture(cpuCapture);

    const u64 sampleFrameIndex = frame.graphics().getFrameIndex();
    ASSERT_TRUE(frame.update(0.f));
    EXPECT_EQ(callbackState.invocationCount, 2u);

    const Perf::TimingView cpuTiming = frame.perfSession().cpuTimingView();
    const Perf::TimingStats& projectUpdateStats = cpuTiming.stats(Name("frame.project_update"));
    const Perf::TimingStats& graphicsFrameStats = cpuTiming.stats(Name("graphics.frame"));
    const Perf::TimingStats& garbageCollectStats = cpuTiming.stats(Name("graphics.garbage_collect"));
    EXPECT_EQ(cpuTiming.scopeCount(), 3u);
    EXPECT_EQ(projectUpdateStats.sampleCount, 1u);
    EXPECT_EQ(projectUpdateStats.firstSampleFrameIndex, sampleFrameIndex);
    EXPECT_EQ(projectUpdateStats.lastSampleFrameIndex, sampleFrameIndex);
    EXPECT_EQ(projectUpdateStats.publishFrameIndex, sampleFrameIndex);
    EXPECT_EQ(graphicsFrameStats.sampleCount, 1u);
    EXPECT_EQ(graphicsFrameStats.firstSampleFrameIndex, sampleFrameIndex);
    EXPECT_EQ(graphicsFrameStats.lastSampleFrameIndex, sampleFrameIndex);
    EXPECT_EQ(graphicsFrameStats.publishFrameIndex, sampleFrameIndex);
    EXPECT_EQ(garbageCollectStats.sampleCount, 1u);
    EXPECT_EQ(garbageCollectStats.firstSampleFrameIndex, graphicsFrameStats.firstSampleFrameIndex);
    EXPECT_EQ(garbageCollectStats.lastSampleFrameIndex, graphicsFrameStats.lastSampleFrameIndex);
    EXPECT_EQ(garbageCollectStats.publishFrameIndex, graphicsFrameStats.publishFrameIndex);
    EXPECT_FALSE(cpuTiming.stats(Name("graphics.animate")).valid());
    EXPECT_FALSE(cpuTiming.stats(Name("graphics.begin_frame")).valid());
    EXPECT_FALSE(cpuTiming.stats(Name("graphics.frame_preamble")).valid());
    EXPECT_FALSE(cpuTiming.stats(Name("graphics.render")).valid());
    EXPECT_FALSE(cpuTiming.stats(Name("graphics.present")).valid());

    frame.graphics().setFrameSubmissionSuspended(true);
    const u64 suspendedFrameIndex = frame.graphics().getFrameIndex();
    ASSERT_TRUE(frame.update(0.f));

    const Perf::TimingView suspendedCpuTiming = frame.perfSession().cpuTimingView();
    const Perf::TimingStats& suspendedGraphicsFrameStats = suspendedCpuTiming.stats(Name("graphics.frame"));
    EXPECT_EQ(suspendedCpuTiming.scopeCount(), 3u);
    EXPECT_EQ(suspendedGraphicsFrameStats.sampleCount, 1u);
    EXPECT_EQ(suspendedGraphicsFrameStats.firstSampleFrameIndex, suspendedFrameIndex);
    EXPECT_EQ(suspendedGraphicsFrameStats.lastSampleFrameIndex, suspendedFrameIndex);
    EXPECT_EQ(suspendedGraphicsFrameStats.publishFrameIndex, suspendedFrameIndex);
    EXPECT_FALSE(suspendedCpuTiming.stats(Name("graphics.garbage_collect")).valid());
    EXPECT_FALSE(suspendedCpuTiming.stats(Name("graphics.animate")).valid());
    EXPECT_FALSE(suspendedCpuTiming.stats(Name("graphics.begin_frame")).valid());
    EXPECT_FALSE(suspendedCpuTiming.stats(Name("graphics.frame_preamble")).valid());
    EXPECT_FALSE(suspendedCpuTiming.stats(Name("graphics.render")).valid());
    EXPECT_FALSE(suspendedCpuTiming.stats(Name("graphics.present")).valid());
}


// RendererSystem requests this terminal policy only when accepted cross-queue ownership cannot be recovered. The
// Graphics owner must stop the current generation before it records another frame, leaving orderly teardown and
// recreation to the caller that owns the device lifetime.
TEST_F(DescriptorBufferRoundTripTest, DeviceRecreationRequestStopsTheCurrentGraphicsGeneration){
    HeadlessGraphicsScope recoveryScope;
    ASSERT_TRUE(recoveryScope.initialize());

    auto& graphics = recoveryScope.graphics();
    EXPECT_FALSE(graphics.isDeviceRecreationRequested());

    graphics.requestDeviceRecreation();

    EXPECT_TRUE(graphics.isDeviceRecreationRequested());
    EXPECT_FALSE(graphics.runFrame());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// An external pixel capture must be able to hold the last completed image without accidentally opening another frame.
// The suspension still observes terminal device state so it cannot conceal a recovery/recreation request.
TEST_F(DescriptorBufferRoundTripTest, FrameSubmissionSuspensionFreezesTheFrameClockWithoutMaskingDeviceRecreation){
    HeadlessGraphicsScope captureScope;
    ASSERT_TRUE(captureScope.initialize());

    auto& graphics = captureScope.graphics();
    const u64 frameIndex = graphics.getFrameIndex();
    graphics.setFrameSubmissionSuspended(true);

    EXPECT_TRUE(graphics.isFrameSubmissionSuspended());
    EXPECT_TRUE(graphics.runFrame());
    EXPECT_EQ(frameIndex, graphics.getFrameIndex());

    graphics.requestDeviceRecreation();
    EXPECT_FALSE(graphics.runFrame());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

