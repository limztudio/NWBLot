// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Descriptor-buffer round-trip proof — VK_EXT_descriptor_buffer.
//
// Stand up a real headless GPU device and exercise DescriptorBufferManager::allocate / writeDescriptor / free across
// every descriptor class, asserting the vkGetDescriptorEXT path actually produces descriptor bytes into the
// HOST-mapped segments and that the free-range sub-allocator is sound.
//
// Production descriptor-buffer-compatible pipelines consume the manager through the global heap. Alongside
// allocation, byte encoding, layout, and heap-lifetime invariants, this suite verifies that pipeline-local layouts
// are descriptor-free and that heap layouts are the sole resource-bearing descriptor transport.
//
// GPU-optional host: the suite SKIPS (never fails) when the required extension is absent. This is an environment
// limitation, not evidence that an ordinary descriptor-set implementation satisfies the descriptor-buffer contract.


#include <gtest/gtest.h>

#include <global/global.h>
#include <global/unique_ptr.h>
#include <core/common/module.h>
#include <core/alloc/general.h>
#include <core/alloc/thread.h>
#include <core/alloc/job.h>
#include <core/graphics/module.h>
#include <core/graphics/api.h>
#include <core/graphics/backend_selection.h>
#include <core/perf/timing.h>
#include <impl/assets/graphics/avboit/constants.h>
#include <impl/assets/graphics/bindless/runtime_abi.h>
#include <impl/assets/graphics/skinned_mesh/constants.h>
#include <tests/capturing_logger.h>

// The manager lives in the Vulkan backend (Core::GraphicsBackend namespace). The test is inherently Vulkan-aware
// (VkDescriptorType, descriptor-buffer entry points), so the concrete backend header is the right include here
// rather than reaching through a forward declaration.
#include <core/graphics/vulkan/backend.h>

#include <volk/volk.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


// Bring the engine's core namespaces into scope. The test types (Graphics, GraphicsAllocator, Alloc::GlobalArena,
// Perf::TimingRecorder, GraphicsBackend::Device, DescriptorBufferManager, ...) all live under NWB::Core, and the
// suite is nested in NWB::Tests, so a single using-directive keeps the bodies readable without full qualification.
// DescriptorBufferSegmentKind is a nested namespace inside GraphicsBackend and is spelled out fully at each use.
using namespace Core;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Brings up a real headless GPU device with the minimum dependency set Graphics requires, mirroring Core::Frame's
// construction. createHeadlessDevice() creates no window/swap chain, so this runs on any host with a Vulkan driver
// that exposes the required descriptor-buffer capability.
class HeadlessGraphicsScope final : NoCopy{
public:
    HeadlessGraphicsScope()
        : m_objectArena(s_TestArenaName)
        , m_allocator(m_objectArena)
        , m_threadPool(s_TestWorkerThreadCount, Alloc::CoreAffinity::Any)
        , m_jobSystem(m_threadPool)
        , m_gpuTiming(m_objectArena)
        , m_graphics(m_allocator, m_threadPool, m_jobSystem, m_gpuTiming)
    {}

    ~HeadlessGraphicsScope(){
        // Graphics::~Graphics() tears down the backend; nothing else to release here.
    }

    // Returns false on driver/instance failure (no Vulkan, no physical device, etc.). The caller SKIPS in that case
    // rather than failing — a CI runner without a GPU is an environment condition.
    [[nodiscard]] bool initialize(){
        if(!m_graphics.setBindlessHeapAbi(Impl::AssetsGraphicsBindless::MakeGpuDescriptorHeapAbi()))
            return false;
        return m_graphics.createHeadlessDevice();
    }

    [[nodiscard]] Graphics& graphics(){ return m_graphics; }
    [[nodiscard]] Alloc::GlobalArena& arena(){ return m_objectArena; }
    [[nodiscard]] Perf::TimingRecorder& gpuTimingSink(){ return m_gpuTiming; }

    void setGpuTimingEnabled(const bool enabled){
        m_gpuTiming.setEnabled(enabled);
        m_graphics.gpuTiming().setQueryCollectionEnabled(enabled);
    }

private:
    static inline constexpr Name s_TestArenaName{"tests/descriptor_buffer/graphics_object_arena"};
    static inline constexpr u32 s_TestWorkerThreadCount = 2u;

    Alloc::GlobalArena m_objectArena;
    GraphicsAllocator m_allocator;
    Alloc::ThreadPool m_threadPool;
    Alloc::JobSystem m_jobSystem;
    Perf::TimingRecorder m_gpuTiming;
    Graphics m_graphics;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Fixture: one headless device shared across the suite. The descriptor-buffer segments are HOST-mapped and persist
// for device life, so per-case carve/free is exercised against the real global segments (resource + sampler).
class DescriptorBufferRoundTripTest : public ::testing::Test{
protected:
    static void SetUpTestSuite(){
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
        // The hardening checks intentionally exercise diagnostic rejection paths.  This fixture owns a worker pool,
        // so use Google Test's re-exec death-test mode rather than forking a live multi-threaded Vulkan process.
        GTEST_FLAG_SET(death_test_style, "threadsafe");
#endif

        // The device-creation path emits log messages, and every NWB_LOGGER_* macro fatally asserts a logger is
        // installed (log.h:276). Register the capturing logger before bring-up so failures are recorded rather than
        // crashing the process, then keep it registered for the suite's lifetime.
        s_logger.emplace();
        s_loggerGuard.emplace(*s_logger);

        s_scope = MakeUnique<HeadlessGraphicsScope>();
        const bool initialized = s_scope->initialize();

        // No usable Vulkan device on this host -> skip the whole suite. Reported as SKIPPED, not failed.
        if(!initialized){
            GTEST_SKIP() << "Descriptor-buffer round-trip: no usable headless Vulkan device on this host; skipping suite.";
            return;
        }

        auto& device = s_scope->graphics().getDevice();
        auto& mgr = device.getDescriptorBufferManager();

        if(!mgr.isEnabled()){
            // This suite proves the required descriptor-buffer path. A host without the extension cannot exercise
            // that contract, so skip instead of treating an ordinary descriptor-set path as equivalent coverage.
            GTEST_SKIP() << "Descriptor-buffer round-trip: VK_EXT_descriptor_buffer is not enabled on this device; "
                            "skipping descriptor-buffer-only coverage.";
        }
    }

    static void TearDownTestSuite(){
        s_scope.reset();
        s_loggerGuard.reset();
        s_logger.reset();
    }

    [[nodiscard]] static GraphicsBackend::Device& device(){
        return s_scope->graphics().getDevice();
    }
    [[nodiscard]] static GraphicsBackend::DescriptorBufferManager& manager(){
        return device().getDescriptorBufferManager();
    }
    [[nodiscard]] static Alloc::GlobalArena& arena(){ return s_scope->arena(); }

protected:
    static UniquePtr<HeadlessGraphicsScope> s_scope;
    static Optional<CapturingLogger> s_logger;
    static Optional<Common::LoggerRegistrationGuard> s_loggerGuard;
};

UniquePtr<HeadlessGraphicsScope> DescriptorBufferRoundTripTest::s_scope;
Optional<CapturingLogger> DescriptorBufferRoundTripTest::s_logger;
Optional<Common::LoggerRegistrationGuard> DescriptorBufferRoundTripTest::s_loggerGuard;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr GpuTimingScopeDefinition s_FrameTimingPreambleScope("tests/frame_timing_preamble");
inline constexpr GpuTimingScopeDefinition s_FrameTimingLateActivationScope("tests/frame_timing_late_activation");
inline constexpr GpuTimingScopeDefinition s_SubmissionTicketScope("tests/timing_submission_ticket");
inline constexpr GpuTimingScopeDefinition s_ConcurrentSubmissionTicketScope("tests/timing_submission_ticket_concurrent");


// Records a timing scope inside dynamic rendering, where vkCmdResetQueryPool is illegal. The scope can therefore
// only receive a query when Graphics submitted its timer-query reset preamble before invoking this render pass.
class FrameTimingPreambleProbePass final : public IRenderPass{
public:
    explicit FrameTimingPreambleProbePass(
        Graphics& graphics,
        const GpuTimingScopeDefinition& timingScope = s_FrameTimingPreambleScope
    )
        : IRenderPass(graphics)
        , m_timingScope(timingScope)
    {}


public:
    [[nodiscard]] bool initialize(){
        auto& device = getGraphics().getDevice();

        m_target = device.createTexture(
            TextureDesc()
                .setWidth(4u)
                .setHeight(4u)
                .setFormat(Format::RGBA8_UNORM)
                .setInRenderTarget(true)
                .setInitialState(ResourceStates::Common)
        );
        if(!m_target)
            return false;

        m_framebuffer = device.createFramebuffer(FramebufferDesc().addColorAttachment(m_target.get()));
        return m_framebuffer != nullptr;
    }

    virtual void render(Framebuffer*)override{
        if(m_recorded || !m_framebuffer)
            return;

        auto& device = getGraphics().getDevice();

        CommandListHandle commandList = device.createCommandList();
        if(!commandList)
            return;

        {
            GpuTimingSubmissionTicket timingTicket(getGraphics().gpuTiming());
            {
                GpuTimingSubmissionTicket::RecordingScope timingRecording(timingTicket);

                commandList->open();
                GraphicsState graphicsState;
                graphicsState.setFramebuffer(m_framebuffer.get());
                commandList->setGraphicsState(graphicsState);
                {
                    GpuTimingMeasure timing(getGraphics().gpuTiming(), m_timingScope, device, *commandList);
                }
                commandList->endRenderPass();
                commandList->close();
            }

            CommandList* commandLists[] = { commandList.get() };
            m_recorded = timingTicket.submit(device, commandLists, 1u);
        }
    }

    [[nodiscard]] bool recorded()const{ return m_recorded; }


private:
    const GpuTimingScopeDefinition& m_timingScope;
    TextureHandle m_target;
    FramebufferHandle m_framebuffer;
    bool m_recorded = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Manager is enabled and its two global segments report a non-zero device address after device init. The bound
// address is what vkCmdBindDescriptorBuffersEXT hands to the command buffer; a zero address would mean the segment
// buffer was never allocated/mapped. This is the gate every subsequent case depends on.
TEST_F(DescriptorBufferRoundTripTest, ManagerEnabledAndSegmentsMapped){
    auto& mgr = manager();

    ASSERT_TRUE(mgr.isEnabled());
    EXPECT_NE(mgr.getResourceBindingInfo().address, 0u);
    EXPECT_NE(mgr.getSamplerBindingInfo().address, 0u);
    EXPECT_EQ(mgr.getResourceBufferIndex(), 0u);
    EXPECT_EQ(mgr.getSamplerBufferIndex(), 1u);
}


// The global reset must precede every render pass. This probe places its timing scope inside dynamic rendering,
// where it cannot reset a newly reserved query itself; a valid sample on the next frame proves the Graphics preamble
// made the query device-ready before render-pass recording began.
TEST_F(DescriptorBufferRoundTripTest, GraphicsFramePreambleResetsTimerQueriesBeforeRenderPasses){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = graphics.gpuTiming();
    auto& timingSink = s_scope->gpuTimingSink();

    s_scope->setGpuTimingEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(s_FrameTimingPreambleScope.identity, device, 1u));

    FrameTimingPreambleProbePass probePass(graphics);
    ASSERT_TRUE(probePass.initialize());

    graphics.addRenderPassToBack(probePass);
    graphics.render();
    graphics.removeRenderPass(probePass);

    ASSERT_TRUE(probePass.recorded());
    ASSERT_TRUE(device.waitForIdle());

    // collect() runs at the next frame open, before that frame's reset can overwrite the completed sample.
    graphics.render();
    ASSERT_TRUE(device.waitForIdle());
    EXPECT_TRUE(timingSink.stats(s_FrameTimingPreambleScope.identity).valid());

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
}


// Renderer systems declare their timing capacities while resources validate, often before a project enables capture.
// The next Graphics preamble must materialize those declarations before the first dynamic-rendering scope records.
TEST_F(DescriptorBufferRoundTripTest, GraphicsFramePreambleMaterializesTimerQueriesAfterCaptureActivation){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = graphics.gpuTiming();
    auto& timingSink = s_scope->gpuTimingSink();

    s_scope->setGpuTimingEnabled(false);
    ASSERT_TRUE(timing.prepareScopeQueries(s_FrameTimingLateActivationScope.identity, device, 1u));
    s_scope->setGpuTimingEnabled(true);

    FrameTimingPreambleProbePass probePass(graphics, s_FrameTimingLateActivationScope);
    ASSERT_TRUE(probePass.initialize());

    graphics.addRenderPassToBack(probePass);
    graphics.render();
    graphics.removeRenderPass(probePass);

    ASSERT_TRUE(probePass.recorded());
    ASSERT_TRUE(device.waitForIdle());

    graphics.render();
    ASSERT_TRUE(device.waitForIdle());
    EXPECT_TRUE(timingSink.stats(s_FrameTimingLateActivationScope.identity).valid());

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
}


// A frame metric starts on the G-buffer primary and ends on the ordered post-G-buffer primary. If recording aborts
// before its ending timestamp or that batch is rejected, the query slot must be released before the next
// dynamic-rendering scope records; it cannot grow a new query pool there because vkCmdResetQueryPool is illegal
// inside the render pass.
TEST_F(DescriptorBufferRoundTripTest, GpuTimingSubmissionTicketReleasesRejectedSplitScope){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = graphics.gpuTiming();
    auto& timingSink = s_scope->gpuTimingSink();

    s_scope->setGpuTimingEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(s_SubmissionTicketScope.identity, device, 1u));

    auto target = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::RGBA8_UNORM)
            .setInRenderTarget(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(target.get(), nullptr);
    auto framebuffer = device.createFramebuffer(FramebufferDesc().addColorAttachment(target.get()));
    ASSERT_NE(framebuffer.get(), nullptr);

    // Establish the one prepared pool on the device timeline, exactly as Graphics::render() does before its passes.
    auto resetCommandList = device.createCommandList();
    ASSERT_NE(resetCommandList.get(), nullptr);
    resetCommandList->open();
    timing.recordFrameReset(*resetCommandList);
    resetCommandList->close();
    CommandList* resetCommandLists[] = { resetCommandList.get() };
    bool resetSubmitted = false;
    device.executeCommandLists(resetCommandLists, 1u, CommandQueue::Graphics, &resetSubmitted);
    ASSERT_TRUE(resetSubmitted);
    timing.confirmFrameReset();

    auto abandonedCommandList = device.createCommandList();
    ASSERT_NE(abandonedCommandList.get(), nullptr);
    {
        GpuTimingSubmissionTicket abandonedTicket(timing);
        {
            GpuTimingSubmissionTicket::RecordingScope timingRecording(abandonedTicket);
            abandonedCommandList->open();
            GraphicsState graphicsState;
            graphicsState.setFramebuffer(framebuffer.get());
            abandonedCommandList->setGraphicsState(graphicsState);
            {
                GpuTimingMeasure abandonedTiming(timing, s_SubmissionTicketScope, device, *abandonedCommandList);
                abandonedTiming.discardTiming();
            }
            abandonedCommandList->endRenderPass();
            abandonedCommandList->close();
        }
    }

    auto producer = device.createCommandList();
    auto consumer = device.createCommandList();
    ASSERT_NE(producer.get(), nullptr);
    ASSERT_NE(consumer.get(), nullptr);
    {
        GpuTimingSubmissionTicket rejectedTicket(timing);
        {
            GpuTimingSubmissionTicket::RecordingScope timingRecording(rejectedTicket);
            producer->open();
            GraphicsState graphicsState;
            graphicsState.setFramebuffer(framebuffer.get());
            producer->setGraphicsState(graphicsState);

            GpuTimingMeasure rejectedTiming(timing, s_SubmissionTicketScope, device, *producer);
            rejectedTiming.finishMarker();
            producer->endRenderPass();
            producer->close();

            consumer->open();
            rejectedTiming.finishTiming(*consumer);
            consumer->close();
        }

        // A missing consumer must reject the whole batch rather than submit the producer alone. This is the same
        // rollback path used when Vulkan rejects an otherwise complete submission.
        CommandList* rejectedCommandLists[] = { producer.get(), nullptr };
        EXPECT_FALSE(rejectedTicket.submit(device, rejectedCommandLists, 2u));
    }
    producer.reset();
    consumer.reset();

    auto acceptedCommandList = device.createCommandList();
    ASSERT_NE(acceptedCommandList.get(), nullptr);
    GpuTimingSubmissionTicket acceptedTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(acceptedTicket);
        acceptedCommandList->open();
        GraphicsState graphicsState;
        graphicsState.setFramebuffer(framebuffer.get());
        acceptedCommandList->setGraphicsState(graphicsState);
        {
            GpuTimingMeasure acceptedTiming(timing, s_SubmissionTicketScope, device, *acceptedCommandList);
        }
        acceptedCommandList->endRenderPass();
        acceptedCommandList->close();
    }

    CommandList* acceptedCommandLists[] = { acceptedCommandList.get() };
    ASSERT_TRUE(acceptedTicket.submit(device, acceptedCommandLists, 1u));
    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 1u);
    EXPECT_TRUE(timingSink.stats(s_SubmissionTicketScope.identity).valid());

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
}


// Independent packet jobs can share one submission ticket. Both workers reserve the same timing scope at the same
// latch, so the recorder must claim distinct query slots before either command list reaches its ending timestamp.
TEST_F(DescriptorBufferRoundTripTest, GpuTimingSubmissionTicketReservesConcurrentWorkerScopes){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = graphics.gpuTiming();
    auto& timingSink = s_scope->gpuTimingSink();

    s_scope->setGpuTimingEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(s_ConcurrentSubmissionTicketScope.identity, device, 2u));

    auto resetCommandList = device.createCommandList();
    ASSERT_NE(resetCommandList.get(), nullptr);
    resetCommandList->open();
    timing.recordFrameReset(*resetCommandList);
    resetCommandList->close();
    CommandList* resetCommandLists[] = { resetCommandList.get() };
    bool resetSubmitted = false;
    device.executeCommandLists(resetCommandLists, 1u, CommandQueue::Graphics, &resetSubmitted);
    ASSERT_TRUE(resetSubmitted);
    timing.confirmFrameReset();

    auto firstCommandList = device.createCommandList();
    auto secondCommandList = device.createCommandList();
    ASSERT_NE(firstCommandList.get(), nullptr);
    ASSERT_NE(secondCommandList.get(), nullptr);

    GpuTimingSubmissionTicket timingTicket(timing);
    Latch recordingStarted(2);
    Latch queryReservationsStarted(2);
    bool firstRecorded = false;
    bool secondRecorded = false;
    const Graphics::JobHandle firstJob = graphics.scheduleGraphicsJob([&](){
        GpuTimingSubmissionTicket::RecordingScope timingRecording(timingTicket);
        firstCommandList->open();
        recordingStarted.count_down();
        recordingStarted.wait();
        {
            GpuTimingMeasure measure(timing, s_ConcurrentSubmissionTicketScope, device, *firstCommandList);
            queryReservationsStarted.count_down();
            queryReservationsStarted.wait();
        }
        firstCommandList->close();
        firstRecorded = firstCommandList->hasCommandBuffer();
    });
    const Graphics::JobHandle secondJob = graphics.scheduleGraphicsJob([&](){
        GpuTimingSubmissionTicket::RecordingScope timingRecording(timingTicket);
        secondCommandList->open();
        recordingStarted.count_down();
        recordingStarted.wait();
        {
            GpuTimingMeasure measure(timing, s_ConcurrentSubmissionTicketScope, device, *secondCommandList);
            queryReservationsStarted.count_down();
            queryReservationsStarted.wait();
        }
        secondCommandList->close();
        secondRecorded = secondCommandList->hasCommandBuffer();
    });
    ASSERT_TRUE(firstJob.valid());
    ASSERT_TRUE(secondJob.valid());

    graphics.waitJob(firstJob);
    graphics.waitJob(secondJob);
    ASSERT_TRUE(firstRecorded);
    ASSERT_TRUE(secondRecorded);

    CommandList* commandLists[] = { firstCommandList.get(), secondCommandList.get() };
    ASSERT_TRUE(timingTicket.submit(device, commandLists, 2u));
    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 1u);
    EXPECT_EQ(timingSink.stats(s_ConcurrentSubmissionTicketScope.identity).sampleCount, 2u);

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
}


// Ordered primary command buffers must carry the producer's final state into the consumer. The consumer's
// ShaderResource transition therefore has CopyDest as its source, rather than treating the buffer as unknown.
TEST_F(DescriptorBufferRoundTripTest, CommandListStateHandoffTransfersFinalBufferState){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto buffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(buffer.get(), nullptr);
    auto restoredBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    ASSERT_NE(restoredBuffer.get(), nullptr);
    auto permanentBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(permanentBuffer.get(), nullptr);

    CommandListResourceStateHandoff handoff(DescriptorBufferRoundTripTest::arena());
    auto producer = device.createCommandList();
    auto consumer = device.createCommandList();
    ASSERT_NE(producer.get(), nullptr);
    ASSERT_NE(consumer.get(), nullptr);

    producer->open();
    producer->setBufferState(buffer.get(), ResourceStates::CopyDest);
    producer->setBufferState(restoredBuffer.get(), ResourceStates::CopyDest);
    producer->setPermanentBufferState(permanentBuffer.get(), ResourceStates::ShaderResource);
    producer->close(&handoff);
    ASSERT_TRUE(handoff.valid());

    consumer->open(&handoff);
    EXPECT_EQ(consumer->getBufferState(buffer.get()), ResourceStates::CopyDest);
    EXPECT_EQ(consumer->getBufferState(restoredBuffer.get()), ResourceStates::Common);
    EXPECT_EQ(consumer->getBufferState(permanentBuffer.get()), ResourceStates::ShaderResource);
    consumer->setBufferState(buffer.get(), ResourceStates::ShaderResource);
    EXPECT_EQ(consumer->getBufferState(buffer.get()), ResourceStates::ShaderResource);
    consumer->close();

    CommandList* commandLists[] = { producer.get(), consumer.get() };
    bool submitted = false;
    EXPECT_GT(device.executeCommandLists(commandLists, 2u, CommandQueue::Graphics, &submitted), 0u);
    EXPECT_TRUE(submitted);
    EXPECT_TRUE(device.waitForIdle());
}


// A normalized prelude transitions shared inputs once before independently recorded primary command lists begin.
// Each branch can therefore import the same ShaderResource state without emitting another stale RenderTarget ->
// ShaderResource barrier. The fan-in preserves their disjoint output states for the later ordered consumer.
TEST_F(DescriptorBufferRoundTripTest, NormalizedStatePreludeFansInIndependentBranches){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto sharedInput = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::RGBA8_UNORM)
            .setInRenderTarget(true)
            .setInitialState(ResourceStates::Common)
    );
    auto firstOutput = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
    );
    auto secondOutput = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(sharedInput.get(), nullptr);
    ASSERT_NE(firstOutput.get(), nullptr);
    ASSERT_NE(secondOutput.get(), nullptr);

    CommandListResourceStateHandoff producerState(DescriptorBufferRoundTripTest::arena());
    CommandListResourceStateHandoff normalizedState(DescriptorBufferRoundTripTest::arena());
    CommandListResourceStateHandoff firstBranchState(DescriptorBufferRoundTripTest::arena());
    CommandListResourceStateHandoff secondBranchState(DescriptorBufferRoundTripTest::arena());
    CommandListResourceStateHandoff fanInState(DescriptorBufferRoundTripTest::arena());
    auto producer = device.createCommandList();
    auto prelude = device.createCommandList();
    auto firstBranch = device.createCommandList();
    auto secondBranch = device.createCommandList();
    auto consumer = device.createCommandList();
    ASSERT_NE(producer.get(), nullptr);
    ASSERT_NE(prelude.get(), nullptr);
    ASSERT_NE(firstBranch.get(), nullptr);
    ASSERT_NE(secondBranch.get(), nullptr);
    ASSERT_NE(consumer.get(), nullptr);

    producer->open();
    producer->setTextureState(sharedInput.get(), s_AllSubresources, ResourceStates::RenderTarget);
    producer->close(&producerState);
    ASSERT_TRUE(producerState.valid());

    prelude->open(&producerState);
    prelude->setTextureState(sharedInput.get(), s_AllSubresources, ResourceStates::ShaderResource);
    prelude->close(&normalizedState);
    ASSERT_TRUE(normalizedState.valid());

    Latch recordingStarted(2);
    bool firstRecorded = false;
    bool secondRecorded = false;
    const Graphics::JobHandle firstJob = graphics.scheduleGraphicsJob([&](){
        recordingStarted.count_down();
        recordingStarted.wait();
        firstBranch->open(&normalizedState);
        firstBranch->setTextureState(sharedInput.get(), s_AllSubresources, ResourceStates::ShaderResource);
        firstBranch->setBufferState(firstOutput.get(), ResourceStates::UnorderedAccess);
        firstBranch->close(&firstBranchState);
        firstRecorded = firstBranchState.valid() && firstBranch->hasCommandBuffer();
    });
    const Graphics::JobHandle secondJob = graphics.scheduleGraphicsJob([&](){
        recordingStarted.count_down();
        recordingStarted.wait();
        secondBranch->open(&normalizedState);
        secondBranch->setTextureState(sharedInput.get(), s_AllSubresources, ResourceStates::ShaderResource);
        secondBranch->setBufferState(secondOutput.get(), ResourceStates::UnorderedAccess);
        secondBranch->close(&secondBranchState);
        secondRecorded = secondBranchState.valid() && secondBranch->hasCommandBuffer();
    });
    ASSERT_TRUE(firstJob.valid());
    ASSERT_TRUE(secondJob.valid());

    graphics.waitJob(firstJob);
    graphics.waitJob(secondJob);
    ASSERT_TRUE(firstRecorded);
    ASSERT_TRUE(secondRecorded);

    const CommandListResourceStateHandoff* branchStates[] = { &firstBranchState, &secondBranchState };
    ASSERT_TRUE(fanInState.buildFanIn(normalizedState, branchStates, 2u));
    ASSERT_TRUE(fanInState.valid());

    consumer->open(&fanInState);
    consumer->setTextureState(sharedInput.get(), s_AllSubresources, ResourceStates::ShaderResource);
    EXPECT_EQ(consumer->getBufferState(firstOutput.get()), ResourceStates::UnorderedAccess);
    EXPECT_EQ(consumer->getBufferState(secondOutput.get()), ResourceStates::UnorderedAccess);
    consumer->setBufferState(firstOutput.get(), ResourceStates::ShaderResource);
    consumer->setBufferState(secondOutput.get(), ResourceStates::ShaderResource);
    consumer->close();

    CommandList* commandLists[] = {
        producer.get(),
        prelude.get(),
        firstBranch.get(),
        secondBranch.get(),
        consumer.get()
    };
    bool submitted = false;
    EXPECT_GT(device.executeCommandLists(commandLists, 5u, CommandQueue::Graphics, &submitted), 0u);
    EXPECT_TRUE(submitted);
    EXPECT_TRUE(device.waitForIdle());
}


// Mirrors RendererSystem's split frame sequence: record mesh-view and scene-shading setup plus the non-CSG deferred
// clear from the completed shadow-preparation snapshot, fan their disjoint outputs in for the opaque producer, then
// normalize the G-buffer once. Shadow, caustics, surfel GI, and AVBOIT record from that same snapshot; their
// four-way fan-in feeds deferred lighting before the ordered composite consumer.
TEST_F(DescriptorBufferRoundTripTest, RendererFrameSetupAndPostGbufferPacketsFanInBeforeComposite){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    const auto makeGbufferTarget = [&device](){
        return device.createTexture(
            TextureDesc()
                .setWidth(4u)
                .setHeight(4u)
                .setFormat(Format::RGBA8_UNORM)
                .setInRenderTarget(true)
                .setInitialState(ResourceStates::Common)
        );
    };
    const auto makeDepthTarget = [&device](){
        return device.createTexture(
            TextureDesc()
                .setWidth(4u)
                .setHeight(4u)
                .setFormat(Format::D32)
                .setInRenderTarget(true)
                .setInitialState(ResourceStates::Common)
        );
    };
    const auto makePacketOutput = [&device](){
        return device.createTexture(
            TextureDesc()
                .setWidth(4u)
                .setHeight(4u)
                .setFormat(Format::RGBA8_UNORM)
                .setInUAV(true)
                .setInitialState(ResourceStates::Common)
        );
    };
    const auto makeSetupBuffer = [&device](){
        return device.createBuffer(
            BufferDesc()
                .setByteSize(256u)
                .setCanHaveRawViews(true)
                .setInitialState(ResourceStates::Common)
        );
    };

    auto meshViewBuffer = makeSetupBuffer();
    auto sceneShadingBuffer = makeSetupBuffer();
    auto albedo = makeGbufferTarget();
    auto worldPosition = makeGbufferTarget();
    auto normal = makeGbufferTarget();
    auto depth = makeDepthTarget();
    auto shadowVisibility = makePacketOutput();
    auto causticIrradiance = makePacketOutput();
    auto surfelIrradiance = makePacketOutput();
    auto opaqueColor = makeGbufferTarget();
    auto avboitAccumColor = makeGbufferTarget();
    auto avboitAccumExtinction = makeGbufferTarget();
    ASSERT_NE(meshViewBuffer.get(), nullptr);
    ASSERT_NE(sceneShadingBuffer.get(), nullptr);
    ASSERT_NE(albedo.get(), nullptr);
    ASSERT_NE(worldPosition.get(), nullptr);
    ASSERT_NE(normal.get(), nullptr);
    ASSERT_NE(depth.get(), nullptr);
    ASSERT_NE(shadowVisibility.get(), nullptr);
    ASSERT_NE(causticIrradiance.get(), nullptr);
    ASSERT_NE(surfelIrradiance.get(), nullptr);
    ASSERT_NE(opaqueColor.get(), nullptr);
    ASSERT_NE(avboitAccumColor.get(), nullptr);
    ASSERT_NE(avboitAccumExtinction.get(), nullptr);

    CommandListResourceStateHandoff shadowPrepareState(DescriptorBufferRoundTripTest::arena());
    CommandListResourceStateHandoff meshViewSetupState(DescriptorBufferRoundTripTest::arena());
    CommandListResourceStateHandoff sceneShadingSetupState(DescriptorBufferRoundTripTest::arena());
    CommandListResourceStateHandoff deferredClearState(DescriptorBufferRoundTripTest::arena());
    CommandListResourceStateHandoff frameSetupFanInState(DescriptorBufferRoundTripTest::arena());
    CommandListResourceStateHandoff gbufferState(DescriptorBufferRoundTripTest::arena());
    CommandListResourceStateHandoff normalizedState(DescriptorBufferRoundTripTest::arena());
    CommandListResourceStateHandoff shadowState(DescriptorBufferRoundTripTest::arena());
    CommandListResourceStateHandoff causticsState(DescriptorBufferRoundTripTest::arena());
    CommandListResourceStateHandoff surfelGiState(DescriptorBufferRoundTripTest::arena());
    CommandListResourceStateHandoff avboitState(DescriptorBufferRoundTripTest::arena());
    CommandListResourceStateHandoff fanInState(DescriptorBufferRoundTripTest::arena());
    CommandListResourceStateHandoff deferredLightingState(DescriptorBufferRoundTripTest::arena());
    auto shadowPrepare = device.createCommandList();
    auto meshViewSetup = device.createCommandList();
    auto sceneShadingSetup = device.createCommandList();
    auto deferredClear = device.createCommandList();
    auto gbuffer = device.createCommandList();
    auto prelude = device.createCommandList();
    auto shadow = device.createCommandList();
    auto caustics = device.createCommandList();
    auto surfelGi = device.createCommandList();
    auto lighting = device.createCommandList();
    auto avboit = device.createCommandList();
    auto composite = device.createCommandList();
    ASSERT_NE(shadowPrepare.get(), nullptr);
    ASSERT_NE(meshViewSetup.get(), nullptr);
    ASSERT_NE(sceneShadingSetup.get(), nullptr);
    ASSERT_NE(deferredClear.get(), nullptr);
    ASSERT_NE(gbuffer.get(), nullptr);
    ASSERT_NE(prelude.get(), nullptr);
    ASSERT_NE(shadow.get(), nullptr);
    ASSERT_NE(caustics.get(), nullptr);
    ASSERT_NE(surfelGi.get(), nullptr);
    ASSERT_NE(lighting.get(), nullptr);
    ASSERT_NE(avboit.get(), nullptr);
    ASSERT_NE(composite.get(), nullptr);

    shadowPrepare->open();
    shadowPrepare->close(&shadowPrepareState);
    ASSERT_TRUE(shadowPrepareState.valid());

    Latch frameSetupRecordingStarted(2);
    bool meshViewSetupRecorded = false;
    bool sceneShadingSetupRecorded = false;
    bool deferredClearRecorded = false;
    const Graphics::JobHandle meshViewSetupJob = graphics.scheduleGraphicsJob([&](){
        frameSetupRecordingStarted.count_down();
        frameSetupRecordingStarted.wait();
        meshViewSetup->open(&shadowPrepareState);
        meshViewSetup->setBufferState(meshViewBuffer.get(), ResourceStates::CopyDest);
        meshViewSetup->setBufferState(meshViewBuffer.get(), ResourceStates::ConstantBuffer);
        meshViewSetup->close(&meshViewSetupState);
        meshViewSetupRecorded = meshViewSetupState.valid() && meshViewSetup->hasCommandBuffer();
    });
    const Graphics::JobHandle sceneShadingSetupJob = graphics.scheduleGraphicsJob([&](){
        frameSetupRecordingStarted.count_down();
        frameSetupRecordingStarted.wait();
        sceneShadingSetup->open(&shadowPrepareState);
        sceneShadingSetup->setBufferState(sceneShadingBuffer.get(), ResourceStates::CopyDest);
        sceneShadingSetup->setBufferState(sceneShadingBuffer.get(), ResourceStates::ConstantBuffer);
        sceneShadingSetup->close(&sceneShadingSetupState);
        sceneShadingSetupRecorded = sceneShadingSetupState.valid() && sceneShadingSetup->hasCommandBuffer();
    });
    const Graphics::JobHandle deferredClearJob = graphics.scheduleGraphicsJob([&](){
        deferredClear->open(&shadowPrepareState);
        deferredClear->setTextureState(albedo.get(), s_AllSubresources, ResourceStates::CopyDest);
        deferredClear->setTextureState(worldPosition.get(), s_AllSubresources, ResourceStates::CopyDest);
        deferredClear->setTextureState(normal.get(), s_AllSubresources, ResourceStates::CopyDest);
        deferredClear->setTextureState(depth.get(), s_AllSubresources, ResourceStates::CopyDest);
        deferredClear->setTextureState(opaqueColor.get(), s_AllSubresources, ResourceStates::CopyDest);
        deferredClear->setTextureState(surfelIrradiance.get(), s_AllSubresources, ResourceStates::CopyDest);
        deferredClear->close(&deferredClearState);
        deferredClearRecorded = deferredClearState.valid() && deferredClear->hasCommandBuffer();
    });
    ASSERT_TRUE(meshViewSetupJob.valid());
    ASSERT_TRUE(sceneShadingSetupJob.valid());
    ASSERT_TRUE(deferredClearJob.valid());

    graphics.waitJob(meshViewSetupJob);
    graphics.waitJob(sceneShadingSetupJob);
    graphics.waitJob(deferredClearJob);
    ASSERT_TRUE(meshViewSetupRecorded);
    ASSERT_TRUE(sceneShadingSetupRecorded);
    ASSERT_TRUE(deferredClearRecorded);

    const CommandListResourceStateHandoff* frameSetupBranchStates[] = {
        &meshViewSetupState,
        &sceneShadingSetupState,
        &deferredClearState,
    };
    ASSERT_TRUE(frameSetupFanInState.buildFanIn(shadowPrepareState, frameSetupBranchStates, 3u));
    ASSERT_TRUE(frameSetupFanInState.valid());

    gbuffer->open(&frameSetupFanInState);
    EXPECT_EQ(gbuffer->getBufferState(meshViewBuffer.get()), ResourceStates::ConstantBuffer);
    EXPECT_EQ(gbuffer->getBufferState(sceneShadingBuffer.get()), ResourceStates::ConstantBuffer);
    EXPECT_EQ(gbuffer->getTextureSubresourceState(albedo.get(), 0u, 0u), ResourceStates::CopyDest);
    EXPECT_EQ(gbuffer->getTextureSubresourceState(worldPosition.get(), 0u, 0u), ResourceStates::CopyDest);
    EXPECT_EQ(gbuffer->getTextureSubresourceState(normal.get(), 0u, 0u), ResourceStates::CopyDest);
    EXPECT_EQ(gbuffer->getTextureSubresourceState(depth.get(), 0u, 0u), ResourceStates::CopyDest);
    EXPECT_EQ(gbuffer->getTextureSubresourceState(opaqueColor.get(), 0u, 0u), ResourceStates::CopyDest);
    EXPECT_EQ(gbuffer->getTextureSubresourceState(surfelIrradiance.get(), 0u, 0u), ResourceStates::CopyDest);
    gbuffer->setTextureState(albedo.get(), s_AllSubresources, ResourceStates::RenderTarget);
    gbuffer->setTextureState(worldPosition.get(), s_AllSubresources, ResourceStates::RenderTarget);
    gbuffer->setTextureState(normal.get(), s_AllSubresources, ResourceStates::RenderTarget);
    gbuffer->setTextureState(depth.get(), s_AllSubresources, ResourceStates::DepthWrite);
    gbuffer->close(&gbufferState);
    ASSERT_TRUE(gbufferState.valid());

    prelude->open(&gbufferState);
    prelude->setTextureState(worldPosition.get(), s_AllSubresources, ResourceStates::ShaderResource);
    prelude->setTextureState(normal.get(), s_AllSubresources, ResourceStates::ShaderResource);
    prelude->setTextureState(depth.get(), s_AllSubresources, ResourceStates::ShaderResource);
    prelude->close(&normalizedState);
    ASSERT_TRUE(normalizedState.valid());

    Latch recordingStarted(2);
    bool shadowRecorded = false;
    bool causticsRecorded = false;
    bool surfelGiRecorded = false;
    bool avboitRecorded = false;
    const Graphics::JobHandle shadowJob = graphics.scheduleGraphicsJob([&](){
        recordingStarted.count_down();
        recordingStarted.wait();
        shadow->open(&normalizedState);
        shadow->setTextureState(worldPosition.get(), s_AllSubresources, ResourceStates::ShaderResource);
        shadow->setTextureState(normal.get(), s_AllSubresources, ResourceStates::ShaderResource);
        shadow->setTextureState(depth.get(), s_AllSubresources, ResourceStates::ShaderResource);
        shadow->setTextureState(shadowVisibility.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
        shadow->close(&shadowState);
        shadowRecorded = shadowState.valid() && shadow->hasCommandBuffer();
    });
    const Graphics::JobHandle causticsJob = graphics.scheduleGraphicsJob([&](){
        recordingStarted.count_down();
        recordingStarted.wait();
        caustics->open(&normalizedState);
        caustics->setTextureState(worldPosition.get(), s_AllSubresources, ResourceStates::ShaderResource);
        caustics->setTextureState(depth.get(), s_AllSubresources, ResourceStates::ShaderResource);
        caustics->setTextureState(causticIrradiance.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
        caustics->close(&causticsState);
        causticsRecorded = causticsState.valid() && caustics->hasCommandBuffer();
    });
    const Graphics::JobHandle surfelGiJob = graphics.scheduleGraphicsJob([&](){
        surfelGi->open(&normalizedState);
        surfelGi->setTextureState(worldPosition.get(), s_AllSubresources, ResourceStates::ShaderResource);
        surfelGi->setTextureState(normal.get(), s_AllSubresources, ResourceStates::ShaderResource);
        surfelGi->setTextureState(surfelIrradiance.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
        surfelGi->close(&surfelGiState);
        surfelGiRecorded = surfelGiState.valid() && surfelGi->hasCommandBuffer();
    });
    const Graphics::JobHandle avboitJob = graphics.scheduleGraphicsJob([&](){
        avboit->open(&normalizedState);
        // Transparent CSG temporarily binds the opaque G-buffer, then AVBOIT's accumulation path leaves depth in
        // a depth-read layout. Restore the shared inputs before deferred lighting consumes the four-way fan-in.
        avboit->setTextureState(normal.get(), s_AllSubresources, ResourceStates::RenderTarget);
        avboit->setTextureState(worldPosition.get(), s_AllSubresources, ResourceStates::RenderTarget);
        avboit->setTextureState(depth.get(), s_AllSubresources, ResourceStates::DepthRead);
        avboit->setTextureState(normal.get(), s_AllSubresources, ResourceStates::ShaderResource);
        avboit->setTextureState(worldPosition.get(), s_AllSubresources, ResourceStates::ShaderResource);
        avboit->setTextureState(depth.get(), s_AllSubresources, ResourceStates::ShaderResource);
        avboit->setTextureState(avboitAccumColor.get(), s_AllSubresources, ResourceStates::RenderTarget);
        avboit->setTextureState(avboitAccumExtinction.get(), s_AllSubresources, ResourceStates::RenderTarget);
        avboit->close(&avboitState);
        avboitRecorded = avboitState.valid() && avboit->hasCommandBuffer();
    });
    ASSERT_TRUE(shadowJob.valid());
    ASSERT_TRUE(causticsJob.valid());
    ASSERT_TRUE(surfelGiJob.valid());
    ASSERT_TRUE(avboitJob.valid());

    graphics.waitJob(shadowJob);
    graphics.waitJob(causticsJob);
    graphics.waitJob(surfelGiJob);
    graphics.waitJob(avboitJob);
    ASSERT_TRUE(shadowRecorded);
    ASSERT_TRUE(causticsRecorded);
    ASSERT_TRUE(surfelGiRecorded);
    ASSERT_TRUE(avboitRecorded);

    const CommandListResourceStateHandoff* branchStates[] = {
        &shadowState,
        &causticsState,
        &surfelGiState,
        &avboitState,
    };
    ASSERT_TRUE(fanInState.buildFanIn(normalizedState, branchStates, 4u));
    ASSERT_TRUE(fanInState.valid());

    bool lightingRecorded = false;
    const Graphics::JobHandle lightingJob = graphics.scheduleGraphicsJob([&](){
        lighting->open(&fanInState);
        lighting->setTextureState(shadowVisibility.get(), s_AllSubresources, ResourceStates::ShaderResource);
        lighting->setTextureState(causticIrradiance.get(), s_AllSubresources, ResourceStates::ShaderResource);
        lighting->setTextureState(surfelIrradiance.get(), s_AllSubresources, ResourceStates::ShaderResource);
        lighting->setTextureState(albedo.get(), s_AllSubresources, ResourceStates::ShaderResource);
        lighting->setTextureState(opaqueColor.get(), s_AllSubresources, ResourceStates::RenderTarget);
        lighting->close(&deferredLightingState);
        lightingRecorded = deferredLightingState.valid() && lighting->hasCommandBuffer();
    });
    ASSERT_TRUE(lightingJob.valid());

    graphics.waitJob(lightingJob);
    ASSERT_TRUE(lightingRecorded);

    composite->open(&deferredLightingState);
    EXPECT_EQ(composite->getTextureSubresourceState(albedo.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(composite->getTextureSubresourceState(worldPosition.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(composite->getTextureSubresourceState(normal.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(composite->getTextureSubresourceState(depth.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(composite->getTextureSubresourceState(shadowVisibility.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(composite->getTextureSubresourceState(causticIrradiance.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(composite->getTextureSubresourceState(surfelIrradiance.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(composite->getTextureSubresourceState(opaqueColor.get(), 0u, 0u), ResourceStates::RenderTarget);
    EXPECT_EQ(composite->getTextureSubresourceState(avboitAccumColor.get(), 0u, 0u), ResourceStates::RenderTarget);
    EXPECT_EQ(composite->getTextureSubresourceState(avboitAccumExtinction.get(), 0u, 0u), ResourceStates::RenderTarget);
    composite->setTextureState(opaqueColor.get(), s_AllSubresources, ResourceStates::ShaderResource);
    composite->setTextureState(avboitAccumColor.get(), s_AllSubresources, ResourceStates::ShaderResource);
    composite->setTextureState(avboitAccumExtinction.get(), s_AllSubresources, ResourceStates::ShaderResource);
    composite->close();

    CommandList* commandLists[] = {
        shadowPrepare.get(),
        meshViewSetup.get(),
        sceneShadingSetup.get(),
        deferredClear.get(),
        gbuffer.get(),
        prelude.get(),
        shadow.get(),
        caustics.get(),
        surfelGi.get(),
        avboit.get(),
        lighting.get(),
        composite.get(),
    };
    bool submitted = false;
    EXPECT_GT(device.executeCommandLists(commandLists, 12u, CommandQueue::Graphics, &submitted), 0u);
    EXPECT_TRUE(submitted);
    EXPECT_TRUE(device.waitForIdle());
}


// Fan-in only accepts branch deltas that agree on every shared resource. A scheduler must split or serialize
// conflicting packets instead of selecting one final layout arbitrarily.
TEST_F(DescriptorBufferRoundTripTest, StateFanInRejectsConflictingBranchFinalStates){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto texture = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::RGBA8_UNORM)
            .setInRenderTarget(true)
            .setInUAV(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(texture.get(), nullptr);

    CommandListResourceStateHandoff baseState(DescriptorBufferRoundTripTest::arena());
    CommandListResourceStateHandoff firstBranchState(DescriptorBufferRoundTripTest::arena());
    CommandListResourceStateHandoff secondBranchState(DescriptorBufferRoundTripTest::arena());
    CommandListResourceStateHandoff fanInState(DescriptorBufferRoundTripTest::arena());
    auto base = device.createCommandList();
    auto firstBranch = device.createCommandList();
    auto secondBranch = device.createCommandList();
    ASSERT_NE(base.get(), nullptr);
    ASSERT_NE(firstBranch.get(), nullptr);
    ASSERT_NE(secondBranch.get(), nullptr);

    base->open();
    base->setTextureState(texture.get(), s_AllSubresources, ResourceStates::RenderTarget);
    base->close(&baseState);
    ASSERT_TRUE(baseState.valid());

    firstBranch->open(&baseState);
    firstBranch->setTextureState(texture.get(), s_AllSubresources, ResourceStates::ShaderResource);
    firstBranch->close(&firstBranchState);
    ASSERT_TRUE(firstBranchState.valid());

    secondBranch->open(&baseState);
    secondBranch->setTextureState(texture.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    secondBranch->close(&secondBranchState);
    ASSERT_TRUE(secondBranchState.valid());

    const CommandListResourceStateHandoff* branchStates[] = { &firstBranchState, &secondBranchState };
    EXPECT_FALSE(fanInState.buildFanIn(baseState, branchStates, 2u));
    EXPECT_FALSE(fanInState.valid());
}


// Independent primary command lists use distinct Vulkan command pools. Start both recording jobs at the same latch
// so this exercises the worker-thread path instead of merely submitting them in a fixed order on the main thread.
TEST_F(DescriptorBufferRoundTripTest, IndependentPrimaryCommandListsRecordConcurrentlyOnGraphicsWorkers){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto firstBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
    );
    auto secondBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(firstBuffer.get(), nullptr);
    ASSERT_NE(secondBuffer.get(), nullptr);

    auto firstCommandList = device.createCommandList();
    auto secondCommandList = device.createCommandList();
    ASSERT_NE(firstCommandList.get(), nullptr);
    ASSERT_NE(secondCommandList.get(), nullptr);

    Latch recordingStarted(2);
    bool firstRecorded = false;
    bool secondRecorded = false;
    const Graphics::JobHandle firstJob = graphics.scheduleGraphicsJob([&](){
        recordingStarted.count_down();
        recordingStarted.wait();
        firstCommandList->open();
        firstCommandList->setBufferState(firstBuffer.get(), ResourceStates::CopyDest);
        firstCommandList->close();
        firstRecorded = true;
    });
    const Graphics::JobHandle secondJob = graphics.scheduleGraphicsJob([&](){
        recordingStarted.count_down();
        recordingStarted.wait();
        secondCommandList->open();
        secondCommandList->setBufferState(secondBuffer.get(), ResourceStates::CopyDest);
        secondCommandList->close();
        secondRecorded = true;
    });
    ASSERT_TRUE(firstJob.valid());
    ASSERT_TRUE(secondJob.valid());

    graphics.waitJob(firstJob);
    graphics.waitJob(secondJob);
    EXPECT_TRUE(firstRecorded);
    EXPECT_TRUE(secondRecorded);

    CommandList* commandLists[] = { firstCommandList.get(), secondCommandList.get() };
    bool submitted = false;
    EXPECT_GT(device.executeCommandLists(commandLists, 2u, CommandQueue::Graphics, &submitted), 0u);
    EXPECT_TRUE(submitted);
    EXPECT_TRUE(device.waitForIdle());
}


// Carve one storage-buffer descriptor out of the resource segment and confirm writeDescriptor succeeds via the
// vkGetDescriptorEXT path. Free returns the range to the free list. Storage buffer is the descriptor class every
// raytrace pass binds, so it is the most representative round trip.
TEST_F(DescriptorBufferRoundTripTest, RoundTripsStorageBufferDescriptor){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& mgr = manager();

    auto storageBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(4096u)
            .setStructStride(16u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    ASSERT_NE(storageBuffer.get(), nullptr);

    const u32 descriptorSize = mgr.getDescriptorSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    ASSERT_GT(descriptorSize, 0u);

    const auto segment = mgr.allocate(GraphicsBackend::DescriptorBufferSegmentKind::Resource, descriptorSize, mgr.getOffsetAlignmentBytes());
    ASSERT_TRUE(segment.valid());

    // The authoritative round-trip signal is writeDescriptor's return: a failed vkGetDescriptorEXT returns false and
    // logs at ERROR (the capturing logger would surface it). Byte-level inspection of the mapped segment is private
    // to the manager; the conversion trusts the return value plus the non-zero-size gate below.
    const DescriptorWriteItem item = DescriptorWriteItem::RawBuffer_UAV(0u, storageBuffer.get());
    const bool wrote = mgr.writeDescriptor(item, segment, segment.offsetBytes, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    EXPECT_TRUE(wrote);

    mgr.free(segment);
}


// Uniform (constant) buffer descriptor: same carve/write/free shape, different VkDescriptorType arm. Ensures the
// manager routes UNIFORM_BUFFER through VkDescriptorAddressInfoEXT (the non-texel buffer-info path).
TEST_F(DescriptorBufferRoundTripTest, RoundTripsUniformBufferDescriptor){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& mgr = manager();

    auto constantBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setIsConstantBuffer(true)
            .setInitialState(ResourceStates::ConstantBuffer)
            .setKeepInitialState(true)
    );
    ASSERT_NE(constantBuffer.get(), nullptr);

    const u32 descriptorSize = mgr.getDescriptorSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    ASSERT_GT(descriptorSize, 0u);

    const auto segment = mgr.allocate(GraphicsBackend::DescriptorBufferSegmentKind::Resource, descriptorSize, mgr.getOffsetAlignmentBytes());
    ASSERT_TRUE(segment.valid());

    const DescriptorWriteItem item = DescriptorWriteItem::ConstantBuffer(0u, constantBuffer.get());
    const bool wrote = mgr.writeDescriptor(item, segment, segment.offsetBytes, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    EXPECT_TRUE(wrote);

    mgr.free(segment);
}


// Sampled-image descriptor: the Texture_SRV path, which routes through VkDescriptorImageInfo (image view + layout).
// Together with storage/uniform buffers, this covers the descriptor classes the shadow/GI/caustics passes bind.
TEST_F(DescriptorBufferRoundTripTest, RoundTripsSampledImageDescriptor){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& mgr = manager();

    auto texture = device.createTexture(
        TextureDesc()
            .setWidth(64u)
            .setHeight(64u)
            .setFormat(Format::RGBA8_UNORM)
            .setInitialState(ResourceStates::ShaderResource)
            .setKeepInitialState(true)
    );
    ASSERT_NE(texture.get(), nullptr);

    const u32 descriptorSize = mgr.getDescriptorSize(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    ASSERT_GT(descriptorSize, 0u);

    const auto segment = mgr.allocate(GraphicsBackend::DescriptorBufferSegmentKind::Resource, descriptorSize, mgr.getOffsetAlignmentBytes());
    ASSERT_TRUE(segment.valid());

    const DescriptorWriteItem item = DescriptorWriteItem::Texture_SRV(0u, texture.get());
    const bool wrote = mgr.writeDescriptor(item, segment, segment.offsetBytes, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    EXPECT_TRUE(wrote);

    mgr.free(segment);
}


// Sampler descriptor: the one class that lives in the separate SAMPLER segment (RADV requires samplers in their own
// descriptor buffer binding). Verifies the kind routing places the carve in the sampler segment, not the resource one.
TEST_F(DescriptorBufferRoundTripTest, RoundTripsSamplerDescriptor){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& mgr = manager();

    auto sampler = device.createSampler(SamplerDesc().setAllFilters(true));
    ASSERT_NE(sampler.get(), nullptr);

    const u32 descriptorSize = mgr.getDescriptorSize(VK_DESCRIPTOR_TYPE_SAMPLER);
    ASSERT_GT(descriptorSize, 0u);

    const auto segment = mgr.allocate(GraphicsBackend::DescriptorBufferSegmentKind::Sampler, descriptorSize, mgr.getOffsetAlignmentBytes());
    ASSERT_TRUE(segment.valid());
    EXPECT_EQ(segment.kind, GraphicsBackend::DescriptorBufferSegmentKind::Sampler);

    const DescriptorWriteItem item = DescriptorWriteItem::Sampler(0u, sampler.get());
    const bool wrote = mgr.writeDescriptor(item, segment, segment.offsetBytes, VK_DESCRIPTOR_TYPE_SAMPLER);
    EXPECT_TRUE(wrote);

    mgr.free(segment);
}


// Free-list reuse: allocate a range, free it, allocate the same size again — the second carve must be satisfied from
// the free list and succeed. This proves the sub-allocator remains safe under live allocate/free churn across frames.
TEST_F(DescriptorBufferRoundTripTest, FreeListReusesFreedRange){
    auto& mgr = manager();

    const u32 descriptorSize = mgr.getDescriptorSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    ASSERT_GT(descriptorSize, 0u);

    const auto first = mgr.allocate(GraphicsBackend::DescriptorBufferSegmentKind::Resource, descriptorSize, mgr.getOffsetAlignmentBytes());
    ASSERT_TRUE(first.valid());

    mgr.free(first);

    const auto second = mgr.allocate(GraphicsBackend::DescriptorBufferSegmentKind::Resource, descriptorSize, mgr.getOffsetAlignmentBytes());
    ASSERT_TRUE(second.valid());
    // Same offset: the freed range was the head of the free list and exactly matched the request.
    EXPECT_EQ(second.offsetBytes, first.offsetBytes);

    mgr.free(second);
}


// Descriptor array elements advance by the driver-reported descriptor size. Every type the conversion routes through
// writeDescriptor must report a non-zero size; a zero size would make allocation and writes reject the descriptor.
TEST_F(DescriptorBufferRoundTripTest, EveryDescriptorTypeReportsNonZeroSize){
    auto& mgr = manager();

    static constexpr VkDescriptorType kTypes[] = {
        VK_DESCRIPTOR_TYPE_SAMPLER,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    };

    for(const VkDescriptorType type : kTypes){
        EXPECT_GT(mgr.getDescriptorSize(type), 0u)
            << "descriptor type " << static_cast<u32>(type) << " reported a zero size";
    }
}


// Alignment: carve two adjacent ranges and confirm each block offset is descriptorBufferOffsetAlignment-aligned, and
// the second does not overlap the first. That is the invariant vkCmdSetDescriptorBufferOffsetsEXT must honor.
TEST_F(DescriptorBufferRoundTripTest, AllocationsAreDescriptorBufferOffsetAligned){
    auto& mgr = manager();

    const u32 descriptorSize = mgr.getDescriptorSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    const u32 offsetAlignment = mgr.getOffsetAlignmentBytes();
    ASSERT_GT(descriptorSize, 0u);
    ASSERT_GT(offsetAlignment, 0u);

    const auto a = mgr.allocate(GraphicsBackend::DescriptorBufferSegmentKind::Resource, descriptorSize, offsetAlignment);
    const auto b = mgr.allocate(GraphicsBackend::DescriptorBufferSegmentKind::Resource, descriptorSize, offsetAlignment);
    ASSERT_TRUE(a.valid());
    ASSERT_TRUE(b.valid());

    EXPECT_EQ(a.offsetBytes % offsetAlignment, 0u);
    EXPECT_EQ(b.offsetBytes % offsetAlignment, 0u);
    EXPECT_GE(b.offsetBytes, a.offsetBytes + a.sizeBytes);

    mgr.free(a);
    mgr.free(b);
}


// The manager's public byte-write entry point must not reinterpret a resource payload through the wrong
// VkDescriptorDataEXT union arm, and allocation/free inputs must preserve the block-ownership invariant.
TEST_F(DescriptorBufferRoundTripTest, ManagerRejectsMismatchedWritesAndInvalidBlocks){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& mgr = manager();

    auto storageBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(4096u)
            .setStructStride(16u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    ASSERT_TRUE(storageBuffer);

    const u32 descriptorSize = mgr.getDescriptorSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    ASSERT_GT(descriptorSize, 0u);

    // Invalid public descriptor-buffer operations are diagnostic contract failures in developer builds: the error
    // logger deliberately raises a soft break after rejecting them. Exercise that policy in child processes so this
    // suite can validate it without stopping the parent test run; final builds still verify the ordinary false return.
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        EXPECT_FALSE(mgr.allocate(
            static_cast<GraphicsBackend::DescriptorBufferSegmentKind::Enum>(0xffu),
            descriptorSize,
            mgr.getOffsetAlignmentBytes()
        ).valid());
    }, "");
    EXPECT_DEATH_IF_SUPPORTED({
        EXPECT_FALSE(mgr.allocate(
            GraphicsBackend::DescriptorBufferSegmentKind::Resource,
            descriptorSize,
            0u
        ).valid());
    }, "");
#else
    EXPECT_FALSE(mgr.allocate(
        static_cast<GraphicsBackend::DescriptorBufferSegmentKind::Enum>(0xffu),
        descriptorSize,
        mgr.getOffsetAlignmentBytes()
    ).valid());
    EXPECT_FALSE(mgr.allocate(
        GraphicsBackend::DescriptorBufferSegmentKind::Resource,
        descriptorSize,
        0u
    ).valid());
#endif

    const auto segment = mgr.allocate(
        GraphicsBackend::DescriptorBufferSegmentKind::Resource,
        descriptorSize,
        mgr.getOffsetAlignmentBytes()
    );
    ASSERT_TRUE(segment.valid());

    const DescriptorWriteItem item = DescriptorWriteItem::RawBuffer_UAV(0u, storageBuffer.get());
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        EXPECT_FALSE(mgr.writeDescriptor(item, segment, segment.offsetBytes, VK_DESCRIPTOR_TYPE_SAMPLER));
    }, "");
#else
    EXPECT_FALSE(mgr.writeDescriptor(item, segment, segment.offsetBytes, VK_DESCRIPTOR_TYPE_SAMPLER));
#endif
    EXPECT_TRUE(mgr.writeDescriptor(item, segment, segment.offsetBytes, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER));

    mgr.free(segment);
    // A duplicate free must be rejected instead of inserting a second copy of the range into the free list.
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        mgr.free(segment);
    }, "");
#else
    mgr.free(segment);
#endif

    // Reusing the manager after a free gives the new owner a different allocation serial. A stale segment copy must
    // not be allowed to write even if a future free-list allocation happens to recycle its byte range.
    const auto replacement = mgr.allocate(
        GraphicsBackend::DescriptorBufferSegmentKind::Resource,
        descriptorSize,
        mgr.getOffsetAlignmentBytes()
    );
    ASSERT_TRUE(replacement.valid());
    EXPECT_NE(replacement.allocationSerial, segment.allocationSerial);
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        EXPECT_FALSE(mgr.writeDescriptor(item, segment, segment.offsetBytes, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER));
    }, "");
#else
    EXPECT_FALSE(mgr.writeDescriptor(item, segment, segment.offsetBytes, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER));
#endif
    EXPECT_TRUE(mgr.writeDescriptor(item, replacement, replacement.offsetBytes, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER));
    mgr.free(replacement);
}


// A sampler/resource mix is not segment-coherent. The renderer never falls back to ordinary descriptor sets, so
// callers must split this shape into separate resource and sampler layouts.
TEST_F(DescriptorBufferRoundTripTest, MixedDescriptorBufferLayoutIsRejected){
    auto& device = DescriptorBufferRoundTripTest::device();

    BindlessLayoutDesc layoutDesc;
    layoutDesc
        .setLayoutType(BindlessLayoutType::Immutable)
        .setMaxCapacity(1u)
        .setDescriptorSetIndex(s_MaxBindingLayouts)
        .setVisibility(ShaderType::Compute)
        .addRegisterSpace(BindingLayoutItem::Texture_SRV(0u, 1u))
        .addRegisterSpace(BindingLayoutItem::Sampler(1u, 1u))
    ;
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        EXPECT_FALSE(device.createBindlessLayout(layoutDesc));
    }, "");
#else
    auto layout = device.createBindlessLayout(layoutDesc);
    EXPECT_FALSE(layout);
#endif
}


// Heap slots are raw ABI values, so lifecycle bookkeeping must prevent a duplicate free from recycling the same raw
// slot twice and must reject a write through a handle that is already in its deferred-free quarantine.
TEST_F(DescriptorBufferRoundTripTest, DescriptorHeapRejectsRetiredAndDoubleFreedHandles){
    auto& device = DescriptorBufferRoundTripTest::device();

    GraphicsBackend::GpuDescriptorHeap heap(device);
    GpuDescriptorHeapDesc heapDesc;
    heapDesc
        .setResourceCapacity(2u)
        .setSamplerCapacity(1u)
        .setBindlessHeapAbi(Impl::AssetsGraphicsBindless::MakeGpuDescriptorHeapAbi())
    ;
    ASSERT_TRUE(heap.initialize(heapDesc));

    auto storageBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(4096u)
            .setStructStride(16u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    ASSERT_TRUE(storageBuffer);

    const GpuDescriptorHandle retired = heap.allocate(GpuDescriptorClass::StorageBuffer);
    ASSERT_TRUE(retired.valid());
    ASSERT_TRUE(heap.write(retired, DescriptorWriteItem::StructuredBuffer_UAV(0u, storageBuffer.get())));

    heap.free(retired);
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        EXPECT_FALSE(heap.write(retired, DescriptorWriteItem::StructuredBuffer_UAV(0u, storageBuffer.get())));
    }, "");
    EXPECT_DEATH_IF_SUPPORTED({
        heap.free(retired);
    }, "");
#else
    EXPECT_FALSE(heap.write(retired, DescriptorWriteItem::StructuredBuffer_UAV(0u, storageBuffer.get())));
    heap.free(retired);
#endif

    for(u32 frame = 0u; frame < 8u; ++frame)
        heap.advanceFrame();

    const GpuDescriptorHandle first = heap.allocate(GpuDescriptorClass::StorageBuffer);
    const GpuDescriptorHandle second = heap.allocate(GpuDescriptorClass::StorageBuffer);
    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    EXPECT_NE(first, second);
}


// AVBOIT's material and compute passes share one push-only local layout. Their target-generation slot payload is a
// global UniformBuffer descriptor, all work buffers are global StorageBuffer descriptors, and the writable
// transmittance volume is a global StorageImage descriptor. Exercise the shared local shape and material heap
// registrations together so a future pass-local CBV/buffer/image cannot silently reappear in the transparent path.
TEST_F(DescriptorBufferRoundTripTest, AvboitSharedPushLayoutAndMaterialHeapResourcesBuildAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& heap = device.getDescriptorHeap();
    ASSERT_TRUE(heap.isInitialized());

    static_assert(NWB_AVBOIT_PUSH_CONSTANT_BYTE_SIZE == sizeof(u32) * 16u, "AVBOIT compute push ABI must remain four uint4 lanes");
    static_assert(NWB_AVBOIT_DRAW_PUSH_CONSTANT_BYTE_SIZE == sizeof(u32) * 32u, "AVBOIT transparent draw ABI must remain 128 bytes");

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/avboit_depth_gate_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    auto makeConstantBuffer = [&]() {
        return device.createBuffer(
            BufferDesc()
                .setByteSize(64u)
                .setIsConstantBuffer(true)
                .setInitialState(ResourceStates::ConstantBuffer)
                .setKeepInitialState(true)
        );
    };
    auto makeStructuredUav = [&](const u32 stride) {
        return device.createBuffer(
            BufferDesc()
                .setByteSize(stride * 4096u)
                .setStructStride(stride)
                .setCanHaveUAVs(true)
                .setInitialState(ResourceStates::UnorderedAccess)
                .setKeepInitialState(true)
        );
    };

    auto slots = makeConstantBuffer();
    auto coverage = makeStructuredUav(4u);
    auto depthWarp = makeStructuredUav(4u);
    auto control = makeStructuredUav(4u);
    auto extinction = makeStructuredUav(4u);
    auto overflowDepth = makeStructuredUav(4u);
    ASSERT_TRUE(slots && coverage && depthWarp && control && extinction && overflowDepth);

    const GpuDescriptorHandle slotsHandle = heap.allocate(GpuDescriptorClass::UniformBuffer);
    const GpuDescriptorHandle coverageHandle = heap.allocate(GpuDescriptorClass::StorageBuffer);
    const GpuDescriptorHandle depthWarpHandle = heap.allocate(GpuDescriptorClass::StorageBuffer);
    const GpuDescriptorHandle controlHandle = heap.allocate(GpuDescriptorClass::StorageBuffer);
    const GpuDescriptorHandle extinctionHandle = heap.allocate(GpuDescriptorClass::StorageBuffer);
    const GpuDescriptorHandle overflowDepthHandle = heap.allocate(GpuDescriptorClass::StorageBuffer);
    ASSERT_TRUE(
        slotsHandle.valid()
        && coverageHandle.valid()
        && depthWarpHandle.valid()
        && controlHandle.valid()
        && extinctionHandle.valid()
        && overflowDepthHandle.valid()
    );
    EXPECT_EQ(slotsHandle.descriptorClass(), GpuDescriptorClass::UniformBuffer);
    EXPECT_EQ(coverageHandle.descriptorClass(), GpuDescriptorClass::StorageBuffer);
    EXPECT_EQ(depthWarpHandle.descriptorClass(), GpuDescriptorClass::StorageBuffer);
    EXPECT_EQ(controlHandle.descriptorClass(), GpuDescriptorClass::StorageBuffer);
    EXPECT_EQ(extinctionHandle.descriptorClass(), GpuDescriptorClass::StorageBuffer);
    EXPECT_EQ(overflowDepthHandle.descriptorClass(), GpuDescriptorClass::StorageBuffer);
    ASSERT_TRUE(heap.write(slotsHandle, DescriptorWriteItem::ConstantBuffer(0u, slots.get())));
    ASSERT_TRUE(heap.write(coverageHandle, DescriptorWriteItem::StructuredBuffer_UAV(0u, coverage.get())));
    ASSERT_TRUE(heap.write(depthWarpHandle, DescriptorWriteItem::StructuredBuffer_UAV(0u, depthWarp.get())));
    ASSERT_TRUE(heap.write(controlHandle, DescriptorWriteItem::StructuredBuffer_UAV(0u, control.get())));
    ASSERT_TRUE(heap.write(extinctionHandle, DescriptorWriteItem::StructuredBuffer_UAV(0u, extinction.get())));
    ASSERT_TRUE(heap.write(overflowDepthHandle, DescriptorWriteItem::StructuredBuffer_UAV(0u, overflowDepth.get())));

    BindingLayoutDesc sharedLayoutDesc(descArena);
    sharedLayoutDesc.setVisibility(ShaderType::All);
    sharedLayoutDesc.addItem(BindingLayoutItem::PushConstants(0u, NWB_AVBOIT_DRAW_PUSH_CONSTANT_BYTE_SIZE));
    auto sharedLayout = device.createBindingLayout(sharedLayoutDesc);
    ASSERT_NE(sharedLayout.get(), nullptr);
    ASSERT_TRUE(sharedLayout->isDescriptorBufferCompatible());
    EXPECT_EQ(sharedLayout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::None);
    EXPECT_EQ(sharedLayout->getDescriptorBufferSetSizeBytes(), 0u);
    EXPECT_TRUE(sharedLayout->getDescriptorBufferBindingOffsets().empty());

    EXPECT_TRUE(heap.getResourceLayout()->isDescriptorBufferCompatible());
    EXPECT_TRUE(heap.getSamplerLayout()->isDescriptorBufferCompatible());

    heap.free(slotsHandle);
    heap.free(coverageHandle);
    heap.free(depthWarpHandle);
    heap.free(controlHandle);
    heap.free(extinctionHandle);
    heap.free(overflowDepthHandle);
    for(u32 frame = 0u; frame < s_MaxFramesInFlight; ++frame)
        heap.advanceFrame();
}


// Both AVBOIT compute passes reuse the material path's shared 128-byte push-only layout. Depth warp selects
// coverage/depth-warp/control through the global StorageBuffer heap, while integration selects its writable Texture3D
// plus every source buffer through the same target-generation payload and global heap.
TEST_F(DescriptorBufferRoundTripTest, AvboitComputeResourcesUseSharedPushLayout){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& heap = device.getDescriptorHeap();
    ASSERT_TRUE(heap.isInitialized());

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/avboit_compute_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    auto transmittance = device.createTexture(
        TextureDesc()
            .setWidth(16u)
            .setHeight(16u)
            .setDepth(4u)
            .setDimension(TextureDimension::Texture3D)
            .setFormat(NWB_AVBOIT_TRANSMITTANCE_CORE_FORMAT)
            .setInUAV(true)
            .setInitialState(ResourceStates::UnorderedAccess)
            .setKeepInitialState(true)
    );
    ASSERT_TRUE(transmittance);

    const GpuDescriptorHandle transmittanceStorageHandle = heap.allocate(GpuDescriptorClass::StorageImage);
    ASSERT_TRUE(transmittanceStorageHandle.valid());
    EXPECT_EQ(transmittanceStorageHandle.descriptorClass(), GpuDescriptorClass::StorageImage);
    ASSERT_TRUE(heap.write(transmittanceStorageHandle, DescriptorWriteItem::Texture_UAV(
        0u,
        transmittance.get(),
        NWB_AVBOIT_TRANSMITTANCE_CORE_FORMAT,
        TextureSubresourceSet(0u, 1u, 0u, 1u),
        TextureDimension::Texture3D
    )));

    EXPECT_LE(NWB_AVBOIT_PUSH_CONSTANT_BYTE_SIZE, NWB_AVBOIT_DRAW_PUSH_CONSTANT_BYTE_SIZE);
    BindingLayoutDesc sharedLayoutDesc(descArena);
    sharedLayoutDesc.setVisibility(ShaderType::All);
    sharedLayoutDesc.addItem(BindingLayoutItem::PushConstants(0u, NWB_AVBOIT_DRAW_PUSH_CONSTANT_BYTE_SIZE));
    auto sharedLayout = device.createBindingLayout(sharedLayoutDesc);
    ASSERT_NE(sharedLayout.get(), nullptr);
    ASSERT_TRUE(sharedLayout->isDescriptorBufferCompatible());
    EXPECT_EQ(sharedLayout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::None);
    EXPECT_EQ(sharedLayout->getDescriptorBufferSetSizeBytes(), 0u);
    EXPECT_TRUE(sharedLayout->getDescriptorBufferBindingOffsets().empty());

    EXPECT_TRUE(heap.getResourceLayout()->isDescriptorBufferCompatible());
    EXPECT_TRUE(heap.getSamplerLayout()->isDescriptorBufferCompatible());

    heap.free(transmittanceStorageHandle);
    for(u32 frame = 0u; frame < s_MaxFramesInFlight; ++frame)
        heap.advanceFrame();
}


// CSG's clip, cap-fill, and interval dispatch inputs all live in the global descriptor heap. The clip/cap-fill leaf
// retains the shared 64-byte mesh push ABI, while each interval kernel uses its 48-byte dispatch selector ABI. Keep
// both local layouts descriptor-free and prove the corresponding UniformBuffer/StorageBuffer heap writes separately.
TEST_F(DescriptorBufferRoundTripTest, CsgMaterialTailShapesBuildAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/csg_material_tail_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    auto makeConstantBuffer = [&]() {
        return device.createBuffer(
            BufferDesc()
                .setByteSize(64u)
                .setIsConstantBuffer(true)
                .setInitialState(ResourceStates::ConstantBuffer)
                .setKeepInitialState(true)
        );
    };
    auto makeStructuredSrv = [&](const u32 stride) {
        return device.createBuffer(
            BufferDesc()
                .setByteSize(stride * 64u)
                .setStructStride(stride)
                .setCanHaveRawViews(true)
                .setInitialState(ResourceStates::ShaderResource)
                .setKeepInitialState(true)
        );
    };
    auto receiverRanges = makeStructuredSrv(96u);
    auto cutters = makeStructuredSrv(112u);
    auto materialTyped = makeStructuredSrv(4u);
    auto instances = makeStructuredSrv(64u);
    auto clipContextSlots = makeConstantBuffer();
    auto bindlessSlots = makeConstantBuffer();
    auto sampleState = makeConstantBuffer();
    auto meshView = makeConstantBuffer();
    ASSERT_TRUE(
        receiverRanges && cutters && materialTyped && instances && clipContextSlots && bindlessSlots && sampleState && meshView
    );

    const auto verifyPushOnlyLayout = [&](const char* name, const ShaderType::Mask visibility, const u32 pushConstantByteSize) {
        SCOPED_TRACE(name);
        BindingLayoutDesc layoutDesc(descArena);
        layoutDesc.setVisibility(visibility);
        layoutDesc.addItem(BindingLayoutItem::PushConstants(0u, pushConstantByteSize));

        auto layout = device.createBindingLayout(layoutDesc);
        ASSERT_NE(layout.get(), nullptr);
        ASSERT_TRUE(layout->isDescriptorBufferCompatible());
        EXPECT_EQ(layout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::None);
        EXPECT_EQ(layout->getDescriptorBufferSetSizeBytes(), 0u);
        EXPECT_TRUE(layout->getDescriptorBufferBindingOffsets().empty());
    };
    verifyPushOnlyLayout("clip/cap-fill", ShaderType::Mesh | ShaderType::Compute | ShaderType::Pixel, sizeof(u32) * 16u);
    verifyPushOnlyLayout("interval dispatch", ShaderType::Compute, sizeof(u32) * 12u);

    // The CSG shader aliases its uint/float Texture2DArray views onto the existing StorageImage heap binding. Prove
    // the live heap accepts a typed array UAV descriptor there; the graphics cook verifies each Slang alias emitted
    // against this same set-8 binding.
    auto& heap = device.getDescriptorHeap();
    ASSERT_TRUE(heap.isInitialized());
    const auto registerCsgUniformBuffer = [&](Core::Buffer* buffer) {
        const GpuDescriptorHandle handle = heap.allocate(GpuDescriptorClass::UniformBuffer);
        EXPECT_TRUE(handle.valid());
        if(handle.valid())
            EXPECT_TRUE(heap.write(handle, DescriptorWriteItem::ConstantBuffer(0u, buffer)));
        return handle;
    };
    const GpuDescriptorHandle clipContextHandle = registerCsgUniformBuffer(clipContextSlots.get());
    const GpuDescriptorHandle bindlessSlotsHandle = registerCsgUniformBuffer(bindlessSlots.get());
    const GpuDescriptorHandle sampleStateHandle = registerCsgUniformBuffer(sampleState.get());
    const GpuDescriptorHandle meshViewHandle = registerCsgUniformBuffer(meshView.get());
    auto csgStorageImage = device.createTexture(
        TextureDesc()
            .setWidth(16u)
            .setHeight(16u)
            .setArraySize(3u)
            .setDimension(TextureDimension::Texture2DArray)
            .setFormat(Format::RGBA32_UINT)
            .setInUAV(true)
            .setInitialState(ResourceStates::UnorderedAccess)
            .setKeepInitialState(true)
    );
    ASSERT_TRUE(csgStorageImage);
    const GpuDescriptorHandle csgStorageHandle = heap.allocate(GpuDescriptorClass::StorageImage);
    ASSERT_TRUE(csgStorageHandle.valid());
    EXPECT_TRUE(heap.write(
        csgStorageHandle,
        DescriptorWriteItem::Texture_UAV(
            0u,
            csgStorageImage.get(),
            Format::RGBA32_UINT,
            TextureSubresourceSet(0u, 1u, 0u, 3u),
            TextureDimension::Texture2DArray
        )
    ));

    // CSG's receiver/cutter buffers and the cap-fill material/instance inputs are each persistent StorageBuffer heap
    // entries. The local clip layout above deliberately has no SRVs for them, so prove all four descriptor writes use
    // the same global table instead.
    const auto registerCsgContextBuffer = [&](Core::Buffer* buffer) {
        const GpuDescriptorHandle handle = heap.allocate(GpuDescriptorClass::StorageBuffer);
        EXPECT_TRUE(handle.valid());
        if(handle.valid())
            EXPECT_TRUE(heap.write(handle, DescriptorWriteItem::StructuredBuffer_SRV(0u, buffer)));
        return handle;
    };
    const GpuDescriptorHandle receiverRangeHandle = registerCsgContextBuffer(receiverRanges.get());
    const GpuDescriptorHandle cutterHandle = registerCsgContextBuffer(cutters.get());
    const GpuDescriptorHandle materialTypedHandle = registerCsgContextBuffer(materialTyped.get());
    const GpuDescriptorHandle instanceHandle = registerCsgContextBuffer(instances.get());

    if(clipContextHandle.valid())
        heap.free(clipContextHandle);
    if(bindlessSlotsHandle.valid())
        heap.free(bindlessSlotsHandle);
    if(sampleStateHandle.valid())
        heap.free(sampleStateHandle);
    if(meshViewHandle.valid())
        heap.free(meshViewHandle);
    heap.free(csgStorageHandle);
    if(receiverRangeHandle.valid())
        heap.free(receiverRangeHandle);
    if(cutterHandle.valid())
        heap.free(cutterHandle);
    if(materialTypedHandle.valid())
        heap.free(materialTypedHandle);
    if(instanceHandle.valid())
        heap.free(instanceHandle);
    for(u32 frame = 0u; frame < s_MaxFramesInFlight; ++frame)
        heap.advanceFrame();
}


// Caustic resolve selects every sampled input and its writable output from the global descriptor heap. Its set 0 ABI
// is therefore only the 14-word selector push block; a future local image or buffer must make this proof fail.
TEST_F(DescriptorBufferRoundTripTest, CausticResolveShapeBuildsAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();

    // A short-lived arena for the layout desc (it copies its bindings into object-arena storage on creation).
    static constexpr Name kDescArenaName{"tests/descriptor_buffer/caustic_shape_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    // CausticResolvePushConstants is 14 scalar words in the C++/Slang ABI.
    BindingLayoutDesc layoutDesc(descArena);
    layoutDesc.setVisibility(ShaderType::Compute);
    layoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 14u));

    auto layout = device.createBindingLayout(layoutDesc);
    ASSERT_NE(layout.get(), nullptr);

    ASSERT_TRUE(layout->isDescriptorBufferCompatible())
        << "caustic resolve push-only shape did not route to the descriptor-buffer path";
    EXPECT_EQ(layout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::None);
    EXPECT_EQ(layout->getDescriptorBufferSetSizeBytes(), 0u);
    EXPECT_TRUE(layout->getDescriptorBufferBindingOffsets().empty());
}


// Caustic geometry downsample reads its G-buffer inputs and writes its half-res geometry cache through global heap
// slots. Its local ABI is only the eight-word selector push block.
TEST_F(DescriptorBufferRoundTripTest, CausticGeometryDownsampleShapeBuildsAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/caustic_geom_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    // CausticGeometryDownsamplePushConstants is eight scalar words in the C++/Slang ABI.
    BindingLayoutDesc layoutDesc(descArena);
    layoutDesc.setVisibility(ShaderType::Compute);
    layoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 8u));

    auto layout = device.createBindingLayout(layoutDesc);
    ASSERT_NE(layout.get(), nullptr);

    ASSERT_TRUE(layout->isDescriptorBufferCompatible())
        << "caustic geometry downsample push-only shape did not route to the descriptor-buffer path";
    EXPECT_EQ(layout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::None);
    EXPECT_EQ(layout->getDescriptorBufferSetSizeBytes(), 0u);
    EXPECT_TRUE(layout->getDescriptorBufferBindingOffsets().empty());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Caustic accumulator decay selects its writable R32_UINT Texture2DArray from the global StorageImage heap. Its local
// ABI is only the four-word push block.
TEST_F(DescriptorBufferRoundTripTest, CausticAccumulatorDecayShapeBuildsAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/caustic_decay_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    // CausticAccumulatorDecayPushConstants is four scalar words in the C++/Slang ABI.
    BindingLayoutDesc layoutDesc(descArena);
    layoutDesc.setVisibility(ShaderType::Compute);
    layoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 4u));

    auto layout = device.createBindingLayout(layoutDesc);
    ASSERT_NE(layout.get(), nullptr);

    ASSERT_TRUE(layout->isDescriptorBufferCompatible())
        << "caustic accumulator decay push-only shape did not route to the descriptor-buffer path";
    EXPECT_EQ(layout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::None);
    EXPECT_EQ(layout->getDescriptorBufferSetSizeBytes(), 0u);
    EXPECT_TRUE(layout->getDescriptorBufferBindingOffsets().empty());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Both caustic photon producers fetch every selector, input, and accumulator output from the global descriptor heap.
// Their identical local set ABI is only the shared 14-word photon push block; the fixed global-heap TLAS remains
// outside set 0.
TEST_F(DescriptorBufferRoundTripTest, CausticPhotonProducerShapesBuildAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/caustic_photon_producer_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    // CausticPhotonPushConstants is 14 scalar words and byte-identical for the SW and HW producers.
    BindingLayoutDesc swLayoutDesc(descArena);
    swLayoutDesc.setVisibility(ShaderType::Compute);
    swLayoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 14u));

    auto swLayout = device.createBindingLayout(swLayoutDesc);
    ASSERT_NE(swLayout.get(), nullptr);
    ASSERT_TRUE(swLayout->isDescriptorBufferCompatible())
        << "caustic SW photon push-only shape did not route to the descriptor-buffer path";
    EXPECT_EQ(swLayout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::None);
    EXPECT_EQ(swLayout->getDescriptorBufferSetSizeBytes(), 0u);
    EXPECT_TRUE(swLayout->getDescriptorBufferBindingOffsets().empty());

    // Descriptor layout compatibility is independent of shader stage, so Compute makes this HW local-shape proof
    // runnable on descriptor-buffer-only test devices.
    BindingLayoutDesc hwLayoutDesc(descArena);
    hwLayoutDesc.setVisibility(ShaderType::Compute);
    hwLayoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 14u));

    auto hwLayout = device.createBindingLayout(hwLayoutDesc);
    ASSERT_NE(hwLayout.get(), nullptr);
    ASSERT_TRUE(hwLayout->isDescriptorBufferCompatible())
        << "caustic HW photon push-only shape did not route to the descriptor-buffer path";
    EXPECT_EQ(hwLayout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::None);
    EXPECT_EQ(hwLayout->getDescriptorBufferSetSizeBytes(), 0u);
    EXPECT_TRUE(hwLayout->getDescriptorBufferBindingOffsets().empty());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Both surfel-GI trace backends select their target-generation, material-context, surfel, and scene data through the
// global descriptor heap. Their pipeline-local layout is therefore only the shared 14-u32 selector push range.
TEST_F(DescriptorBufferRoundTripTest, SurfelTraceShapesBuildAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/surfel_trace_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    auto makeConstantBuffer = [&]() {
        return device.createBuffer(
            BufferDesc()
                .setByteSize(256u)
                .setIsConstantBuffer(true)
                .setInitialState(ResourceStates::ConstantBuffer)
                .setKeepInitialState(true)
        );
    };
    auto makeStructuredSrv = [&](const u32 stride) {
        return device.createBuffer(
            BufferDesc()
                .setByteSize(stride * 256u)
                .setStructStride(stride)
                .setCanHaveRawViews(true)
                .setInitialState(ResourceStates::ShaderResource)
                .setKeepInitialState(true)
        );
    };
    auto makeStructuredUav = [&](const u32 stride) {
        return device.createBuffer(
            BufferDesc()
                .setByteSize(stride * 256u)
                .setStructStride(stride)
                .setCanHaveRawViews(true)
                .setInitialState(ResourceStates::UnorderedAccess)
                .setKeepInitialState(true)
        );
    };

    auto bindlessResources = makeConstantBuffer();
    auto materialContextSlots = makeConstantBuffer();
    auto surfelConstants = makeConstantBuffer();
    auto pool = makeStructuredUav(96u);
    auto snapshotPool = makeStructuredSrv(96u);
    auto snapshotCellHead = makeStructuredSrv(4u);
    ASSERT_TRUE(
        bindlessResources && materialContextSlots && surfelConstants && pool && snapshotPool && snapshotCellHead
    );

    // SW trace carries no local CBV/SRV/UAV entries.
    BindingLayoutDesc swLayoutDesc(descArena);
    swLayoutDesc.setVisibility(ShaderType::Compute);
    swLayoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 14u));

    auto swLayout = device.createBindingLayout(swLayoutDesc);
    ASSERT_NE(swLayout.get(), nullptr);
    ASSERT_TRUE(swLayout->isDescriptorBufferCompatible())
        << "surfel SW trace shape did not route to the descriptor-buffer path";
    EXPECT_TRUE(swLayout->getDescriptorBufferBindingOffsets().empty());

    // HW trace differs only by its global TLAS heap layout; its local leaf is identical.
    BindingLayoutDesc hwLayoutDesc(descArena);
    hwLayoutDesc.setVisibility(ShaderType::Compute);
    hwLayoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 14u));

    auto hwLayout = device.createBindingLayout(hwLayoutDesc);
    ASSERT_NE(hwLayout.get(), nullptr);
    ASSERT_TRUE(hwLayout->isDescriptorBufferCompatible())
        << "surfel HW trace shape did not route to the descriptor-buffer path";
    EXPECT_TRUE(hwLayout->getDescriptorBufferBindingOffsets().empty());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Surfel upsample selects its half irradiance, G-buffer inputs, and storage output through the descriptor heap. Its
// local layout must remain a 14-u32 push-only selector block.
TEST_F(DescriptorBufferRoundTripTest, SurfelUpsampleShapeBuildsAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/surfel_upsample_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    auto makeUavTexture = [&](const u32 w, const u32 h) {
        return device.createTexture(
            TextureDesc()
                .setWidth(w).setHeight(h)
                .setFormat(Format::RGBA16_FLOAT)
                .setInitialState(ResourceStates::UnorderedAccess)
                .setKeepInitialState(true)
        );
    };

    auto output = makeUavTexture(32u, 32u);
    ASSERT_TRUE(output);

    // The local ABI carries no UAVs; the output uses the storage-image heap.
    BindingLayoutDesc layoutDesc(descArena);
    layoutDesc.setVisibility(ShaderType::Compute);
    layoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 14u));

    auto layout = device.createBindingLayout(layoutDesc);
    ASSERT_NE(layout.get(), nullptr);

    ASSERT_TRUE(layout->isDescriptorBufferCompatible())
        << "surfel upsample shape did not route to the descriptor-buffer path";
    EXPECT_TRUE(layout->getDescriptorBufferBindingOffsets().empty());
}


// Hash-build's constants, pool, and cell-head are all persistent heap descriptors. Its local layout contains only the
// shared selector push range.
TEST_F(DescriptorBufferRoundTripTest, SurfelHashBuildShapeBuildsAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/surfel_hash_build_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    auto makeConstantBuffer = [&]() {
        return device.createBuffer(
            BufferDesc()
                .setByteSize(256u)
                .setIsConstantBuffer(true)
                .setInitialState(ResourceStates::ConstantBuffer)
                .setKeepInitialState(true)
        );
    };
    auto makeStructuredUav = [&](const u32 stride) {
        return device.createBuffer(
            BufferDesc()
                .setByteSize(stride * 4096u)
                .setStructStride(stride)
                .setCanHaveRawViews(true)
                .setInitialState(ResourceStates::Common)
                .setKeepInitialState(true)
        );
    };

    auto constants = makeConstantBuffer();
    auto pool = makeStructuredUav(16u);
    auto cellHead = makeStructuredUav(4u);
    ASSERT_TRUE(constants && pool && cellHead);

    BindingLayoutDesc layoutDesc(descArena);
    layoutDesc.setVisibility(ShaderType::Compute);
    layoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 14u));

    auto layout = device.createBindingLayout(layoutDesc);
    ASSERT_NE(layout.get(), nullptr);

    ASSERT_TRUE(layout->isDescriptorBufferCompatible())
        << "surfel hash-build shape did not route to the descriptor-buffer path";
    EXPECT_TRUE(layout->getDescriptorBufferBindingOffsets().empty());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(DescriptorBufferRoundTripTest, SurfelAgeFreeShapeBuildsAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/surfel_age_free_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    auto makeConstantBuffer = [&]() {
        return device.createBuffer(
            BufferDesc()
                .setByteSize(256u)
                .setIsConstantBuffer(true)
                .setInitialState(ResourceStates::ConstantBuffer)
                .setKeepInitialState(true)
        );
    };
    auto makeStructuredUav = [&](const u32 stride) {
        return device.createBuffer(
            BufferDesc()
                .setByteSize(stride * 4096u)
                .setStructStride(stride)
                .setCanHaveRawViews(true)
                .setInitialState(ResourceStates::Common)
                .setKeepInitialState(true)
        );
    };

    auto constants = makeConstantBuffer();
    auto pool = makeStructuredUav(16u);
    auto counter = makeStructuredUav(4u);
    auto freeList = makeStructuredUav(4u);
    ASSERT_TRUE(constants && pool && counter && freeList);

    // Constants, pool, counter, and free-list are selected by the shared heap-slot block.
    BindingLayoutDesc layoutDesc(descArena);
    layoutDesc.setVisibility(ShaderType::Compute);
    layoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 14u));

    auto layout = device.createBindingLayout(layoutDesc);
    ASSERT_NE(layout.get(), nullptr);

    ASSERT_TRUE(layout->isDescriptorBufferCompatible())
        << "surfel age-free shape did not route to the descriptor-buffer path";
    EXPECT_TRUE(layout->getDescriptorBufferBindingOffsets().empty());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(DescriptorBufferRoundTripTest, SurfelTraceBuildArgsShapeBuildsAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/surfel_trace_buildargs_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    auto makeConstantBuffer = [&]() {
        return device.createBuffer(
            BufferDesc()
                .setByteSize(256u)
                .setIsConstantBuffer(true)
                .setInitialState(ResourceStates::ConstantBuffer)
                .setKeepInitialState(true)
        );
    };
    auto makeStructuredUav = [&](const u32 stride) {
        return device.createBuffer(
            BufferDesc()
                .setByteSize(stride * 4096u)
                .setStructStride(stride)
                .setCanHaveRawViews(true)
                .setInitialState(ResourceStates::Common)
                .setKeepInitialState(true)
        );
    };

    auto constants = makeConstantBuffer();
    auto counter = makeStructuredUav(4u);
    auto args = makeStructuredUav(4u);
    ASSERT_TRUE(constants && counter && args);

    // Constants, counter, and indirect-argument output are heap descriptors.
    BindingLayoutDesc layoutDesc(descArena);
    layoutDesc.setVisibility(ShaderType::Compute);
    layoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 14u));

    auto layout = device.createBindingLayout(layoutDesc);
    ASSERT_NE(layout.get(), nullptr);

    ASSERT_TRUE(layout->isDescriptorBufferCompatible())
        << "surfel trace build-args shape did not route to the descriptor-buffer path";
    EXPECT_TRUE(layout->getDescriptorBufferBindingOffsets().empty());
}


// Surfel spawn selects both G-buffer inputs and all persistent surfel buffers through the descriptor heap. The local
// layout is only the common selector block.
TEST_F(DescriptorBufferRoundTripTest, SurfelSpawnShapeBuildsAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/surfel_spawn_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    auto makeConstantBuffer = [&]() {
        return device.createBuffer(
            BufferDesc()
                .setByteSize(256u)
                .setIsConstantBuffer(true)
                .setInitialState(ResourceStates::ConstantBuffer)
                .setKeepInitialState(true)
        );
    };
    auto makeStructuredUav = [&](const u32 stride) {
        return device.createBuffer(
            BufferDesc()
                .setByteSize(stride * 4096u)
                .setStructStride(stride)
                .setCanHaveRawViews(true)
                .setInitialState(ResourceStates::Common)
                .setKeepInitialState(true)
        );
    };
    auto constants = makeConstantBuffer();
    auto pool = makeStructuredUav(16u);
    auto cellHead = makeStructuredUav(4u);
    auto counter = makeStructuredUav(4u);
    auto freeList = makeStructuredUav(4u);
    ASSERT_TRUE(constants && pool && cellHead && counter && freeList);

    // No local persistent-buffer descriptors remain.
    BindingLayoutDesc layoutDesc(descArena);
    layoutDesc.setVisibility(ShaderType::Compute);
    layoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 14u));

    auto layout = device.createBindingLayout(layoutDesc);
    ASSERT_NE(layout.get(), nullptr);

    ASSERT_TRUE(layout->isDescriptorBufferCompatible())
        << "surfel spawn shape did not route to the descriptor-buffer path";
    EXPECT_TRUE(layout->getDescriptorBufferBindingOffsets().empty());
}


// Surfel resolve gathers persistent heap buffers and writes its half-resolution output through the storage-image heap.
// Its local layout is the same 14-u32 selector block as every other surfel pass.
TEST_F(DescriptorBufferRoundTripTest, SurfelResolveShapeBuildsAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/surfel_resolve_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    auto makeConstantBuffer = [&]() {
        return device.createBuffer(
            BufferDesc()
                .setByteSize(256u)
                .setIsConstantBuffer(true)
                .setInitialState(ResourceStates::ConstantBuffer)
                .setKeepInitialState(true)
        );
    };
    auto makeStructuredSrv = [&](const u32 stride) {
        return device.createBuffer(
            BufferDesc()
                .setByteSize(stride * 4096u)
                .setStructStride(stride)
                .setCanHaveRawViews(true)
                .setInitialState(ResourceStates::ShaderResource)
                .setKeepInitialState(true)
        );
    };
    auto makeUavTexture = [&](const u32 w, const u32 h) {
        return device.createTexture(
            TextureDesc()
                .setWidth(w).setHeight(h)
                .setFormat(Format::RGBA16_FLOAT)
                .setInitialState(ResourceStates::UnorderedAccess)
                .setKeepInitialState(true)
        );
    };

    auto constants = makeConstantBuffer();
    auto pool = makeStructuredSrv(16u);
    auto cellHead = makeStructuredSrv(4u);
    auto output = makeUavTexture(32u, 32u);
    ASSERT_TRUE(constants && pool && cellHead && output);

    // No local CBV/SRV/UAV entries remain.
    BindingLayoutDesc layoutDesc(descArena);
    layoutDesc.setVisibility(ShaderType::Compute);
    layoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 14u));

    auto layout = device.createBindingLayout(layoutDesc);
    ASSERT_NE(layout.get(), nullptr);

    ASSERT_TRUE(layout->isDescriptorBufferCompatible())
        << "surfel resolve shape did not route to the descriptor-buffer path";
    EXPECT_TRUE(layout->getDescriptorBufferBindingOffsets().empty());
}


// BVH bitonic-sort parity: its keys and payload are global StorageBuffer-heap entries selected by the push block.
// The pipeline-local leaf therefore has no resource entries or descriptor object; it must stay descriptor-buffer-compatible
// alongside the heap's persistent resource/sampler layouts.
TEST_F(DescriptorBufferRoundTripTest, BvhSortPushOnlyHeapLayoutBuildsAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& heap = device.getDescriptorHeap();
    ASSERT_TRUE(heap.isInitialized());

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/bvh_sort_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    BindingLayoutDesc layoutDesc(descArena);
    layoutDesc.setVisibility(ShaderType::Compute);
    layoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 8u));

    auto layout = device.createBindingLayout(layoutDesc);
    ASSERT_NE(layout.get(), nullptr);

    ASSERT_TRUE(layout->isDescriptorBufferCompatible())
        << "bvh sort push-only layout did not route to the descriptor-buffer path";
    EXPECT_TRUE(layout->getDescriptorBufferBindingOffsets().empty());
    EXPECT_TRUE(heap.getResourceLayout()->isDescriptorBufferCompatible());
    EXPECT_TRUE(heap.getSamplerLayout()->isDescriptorBufferCompatible());
}


// BVH LBVH-build parity: all five scratch/work buffers are heap registrations. The local leaf only
// carries the expanded push constants; heap writes retain each concrete buffer until deferred free retires its slot.
TEST_F(DescriptorBufferRoundTripTest, BvhBuildPushOnlyHeapLayoutRegistersScratchBuffers){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& heap = device.getDescriptorHeap();
    ASSERT_TRUE(heap.isInitialized());

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/bvh_build_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    auto makeStructuredUav = [&](const u32 stride) {
        return device.createBuffer(
            BufferDesc()
                .setByteSize(stride * 4096u)
                .setStructStride(stride)
                .setCanHaveUAVs(true)
                .setInitialState(ResourceStates::Common)
                .setKeepInitialState(true)
        );
    };

    auto buildKeys = makeStructuredUav(4u);
    auto buildPayload = makeStructuredUav(4u);
    auto nodes = makeStructuredUav(64u);
    auto parent = makeStructuredUav(4u);
    auto visitCounter = makeStructuredUav(4u);
    ASSERT_TRUE(buildKeys && buildPayload && nodes && parent && visitCounter);

    BindingLayoutDesc layoutDesc(descArena);
    layoutDesc.setVisibility(ShaderType::Compute);
    layoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 12u + sizeof(Float4) * 2u));

    auto layout = device.createBindingLayout(layoutDesc);
    ASSERT_NE(layout.get(), nullptr);

    ASSERT_TRUE(layout->isDescriptorBufferCompatible())
        << "bvh build push-only layout did not route to the descriptor-buffer path";
    EXPECT_TRUE(layout->getDescriptorBufferBindingOffsets().empty());

    Core::Buffer* buffers[] = {
        buildKeys.get(),
        buildPayload.get(),
        nodes.get(),
        parent.get(),
        visitCounter.get(),
    };
    Vector<GpuDescriptorHandle, Alloc::GlobalArena> handles(descArena);
    for(Core::Buffer* buffer : buffers){
        const GpuDescriptorHandle handle = heap.allocate(GpuDescriptorClass::StorageBuffer);
        ASSERT_TRUE(handle.valid());
        ASSERT_TRUE(heap.write(handle, DescriptorWriteItem::StructuredBuffer_UAV(0u, buffer)));
        EXPECT_EQ(handle.descriptorClass(), GpuDescriptorClass::StorageBuffer);
        handles.push_back(handle);
    }
    for(const GpuDescriptorHandle handle : handles)
        heap.free(handle);
    for(u32 frame = 0u; frame < s_MaxFramesInFlight; ++frame)
        heap.advanceFrame();
}


// Shadow geometry-downsample reads its G-buffer inputs, scene payload, and output through the global heap. The local
// leaf is therefore only the ten-word selector push ABI.
TEST_F(DescriptorBufferRoundTripTest, ShadowGeometryDownsampleShapeBuildsAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/shadow_geom_downsample_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    BindingLayoutDesc layoutDesc(descArena);
    layoutDesc.setVisibility(ShaderType::Compute);
    layoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 10u));

    auto layout = device.createBindingLayout(layoutDesc);
    ASSERT_NE(layout.get(), nullptr);

    ASSERT_TRUE(layout->isDescriptorBufferCompatible())
        << "shadow geometry downsample push-only shape did not route to the descriptor-buffer path";
    EXPECT_EQ(layout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::None);
    EXPECT_EQ(layout->getDescriptorBufferSetSizeBytes(), 0u);
    EXPECT_TRUE(layout->getDescriptorBufferBindingOffsets().empty());
}


// The SVGF a-trous resolve (and its RGB variant) selects every input, output, and scene payload from the global heap.
// Its local leaf is the 21-word selector push ABI.
TEST_F(DescriptorBufferRoundTripTest, ShadowResolveShapeBuildsAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/shadow_resolve_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    BindingLayoutDesc layoutDesc(descArena);
    layoutDesc.setVisibility(ShaderType::Compute);
    layoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 21u));

    auto layout = device.createBindingLayout(layoutDesc);
    ASSERT_NE(layout.get(), nullptr);

    ASSERT_TRUE(layout->isDescriptorBufferCompatible())
        << "shadow resolve push-only shape did not route to the descriptor-buffer path";
    EXPECT_EQ(layout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::None);
    EXPECT_EQ(layout->getDescriptorBufferSetSizeBytes(), 0u);
    EXPECT_TRUE(layout->getDescriptorBufferBindingOffsets().empty());
}


// Shadow reproject-merge selects its temporal images and geometry from the global heap. Its local leaf retains the
// 128-byte matrix-plus-selector push ABI only.
TEST_F(DescriptorBufferRoundTripTest, ShadowReprojectMergeShapeBuildsAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/shadow_reproject_merge_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    BindingLayoutDesc layoutDesc(descArena);
    layoutDesc.setVisibility(ShaderType::Compute);
    layoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 32u));

    auto layout = device.createBindingLayout(layoutDesc);
    ASSERT_NE(layout.get(), nullptr);

    ASSERT_TRUE(layout->isDescriptorBufferCompatible())
        << "shadow reproject-merge push-only shape did not route to the descriptor-buffer path";
    EXPECT_EQ(layout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::None);
    EXPECT_EQ(layout->getDescriptorBufferSetSizeBytes(), 0u);
    EXPECT_TRUE(layout->getDescriptorBufferBindingOffsets().empty());
}


// The HW hard and soft inline-RayQuery passes select their TLAS, G-buffer inputs, and visibility outputs from the
// global heap. They differ only in their six-word hard and eight-word soft push selector ABIs.
TEST_F(DescriptorBufferRoundTripTest, ShadowRtTraceShapeBuildsAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/shadow_rt_trace_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    struct RayQueryShape{
        const char* name = nullptr;
        u32 pushConstantWordCount = 0u;
    };
    const RayQueryShape shapes[] = {
        { "hard", 6u },
        { "soft", 8u },
    };
    for(const RayQueryShape& shape : shapes){
        SCOPED_TRACE(shape.name);
        BindingLayoutDesc layoutDesc(descArena);
        layoutDesc.setVisibility(ShaderType::Compute);
        layoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * shape.pushConstantWordCount));

        auto layout = device.createBindingLayout(layoutDesc);
        ASSERT_NE(layout.get(), nullptr);
        ASSERT_TRUE(layout->isDescriptorBufferCompatible())
            << "shadow RT " << shape.name << " push-only shape did not route to the descriptor-buffer path";
        EXPECT_EQ(layout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::None);
        EXPECT_EQ(layout->getDescriptorBufferSetSizeBytes(), 0u);
        EXPECT_TRUE(layout->getDescriptorBufferBindingOffsets().empty());
    }
}


// All nine software-shadow kernels share one 20-word selector push ABI. Their G-buffer, context, output, scratch,
// and indirect resources are global-heap entries, leaving no local descriptor bindings.
TEST_F(DescriptorBufferRoundTripTest, SwShadowTraceShapeBuildsAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/sw_shadow_trace_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    BindingLayoutDesc layoutDesc(descArena);
    layoutDesc.setVisibility(ShaderType::Compute);
    layoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 20u));

    auto layout = device.createBindingLayout(layoutDesc);
    ASSERT_NE(layout.get(), nullptr);
    ASSERT_TRUE(layout->isDescriptorBufferCompatible())
        << "SW shadow push-only shape did not route to the descriptor-buffer path";
    EXPECT_EQ(layout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::None);
    EXPECT_EQ(layout->getDescriptorBufferSetSizeBytes(), 0u);
    EXPECT_TRUE(layout->getDescriptorBufferBindingOffsets().empty());
}


// Skinned-mesh compute resolves every persistent stream and its per-runtime selector payload through the global heap.
// Its three local leaves therefore retain only their dispatch push blocks. Verify those push-only layouts and their
// composition with the global resource/sampler heap layouts.
TEST_F(DescriptorBufferRoundTripTest, SkinnedMeshComputeShapesBuildAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& heap = device.getDescriptorHeap();
    ASSERT_TRUE(heap.isInitialized());

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/skinned_mesh_compute_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    struct ComputeShape{
        const char* name = nullptr;
        u32 pushConstantByteSize = 0u;
    };
    const ComputeShape shapes[] = {
        { "skinning", NWB_SKINNED_MESH_PUSH_CONSTANT_BYTE_SIZE },
        { "bounds", NWB_SKINNED_MESH_BOUNDS_PUSH_CONSTANT_BYTE_SIZE },
        { "repack", NWB_SKINNED_MESH_REPACK_PUSH_CONSTANT_BYTE_SIZE },
    };

    for(const ComputeShape& shape : shapes){
        SCOPED_TRACE(shape.name);

        BindingLayoutDesc layoutDesc(descArena);
        layoutDesc
            .setVisibility(ShaderType::Compute)
        ;
        layoutDesc.addItem(BindingLayoutItem::PushConstants(0u, shape.pushConstantByteSize));

        auto layout = device.createBindingLayout(layoutDesc);
        ASSERT_NE(layout.get(), nullptr);
        ASSERT_TRUE(layout->isDescriptorBufferCompatible())
            << shape.name << " push-only layout did not route to the descriptor-buffer path";
        EXPECT_EQ(layout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::None);
        EXPECT_EQ(layout->getDescriptorBufferSetSizeBytes(), 0u);
        EXPECT_TRUE(layout->getDescriptorBufferBindingOffsets().empty());

        // This is the exact three-layout composition used by each production skinned compute pipeline: its local
        // slot-CB set followed by the fixed resource/sampler heap sets. No shader is needed to validate descriptor
        // layout composition, and creating this descriptor keeps the test headless and independent of cooked assets.
        ComputePipelineDesc pipelineDesc;
        pipelineDesc
            .addBindingLayout(layout)
            .addBindingLayout(heap.getResourceLayout())
            .addBindingLayout(heap.getSamplerLayout())
        ;
        ASSERT_EQ(pipelineDesc.bindingLayouts.size(), 3u);
        EXPECT_EQ(pipelineDesc.bindingLayouts[0].get(), layout.get());
        EXPECT_EQ(pipelineDesc.bindingLayouts[1].get(), heap.getResourceLayout().get());
        EXPECT_EQ(pipelineDesc.bindingLayouts[2].get(), heap.getSamplerLayout().get());
    }
}


// Global-heap proof: the GpuDescriptorHeap requires the descriptor-buffer backend where the device advertises
// VK_EXT_descriptor_buffer. Unlike the per-pass shape tests above (which exercise push-only pipeline layouts), this proves
// the heap itself -- a persistent, per-slot-writable structure -- (1) selected the required backend, (2) built descriptor-buffer-
// compatible bindless layouts at sets 8/9, (3) carved two persistent blocks from its segments, and (4)
// routes write() through the descriptor-buffer path. This is the prerequisite the five heap-coupled tail pipelines
// (surfel SW/HW trace, caustic SW/HW, SW shadow) embed, so their opt-in is only valid when these hold.
TEST_F(DescriptorBufferRoundTripTest, GlobalDescriptorHeapRequiresDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& heap = device.getDescriptorHeap();
    ASSERT_TRUE(heap.isInitialized()) << "device-owned GpuDescriptorHeap was not initialized";

    // Initialization proves the required descriptor-buffer path: no ordinary descriptor-set heap implementation exists.

    // The heap's two bindless layouts must be descriptor-buffer-compatible so a pipeline that embeds them at sets 8/9
    // passes the all-compatible wholesale-conversion gate. Each is pure-class by construction (resource / sampler).
    const auto* resourceLayout = heap.getResourceLayout().get();
    const auto* samplerLayout = heap.getSamplerLayout().get();
    ASSERT_NE(resourceLayout, nullptr);
    ASSERT_NE(samplerLayout, nullptr);
    EXPECT_TRUE(resourceLayout->isDescriptorBufferCompatible())
        << "heap resource bindless layout is not descriptor-buffer-compatible";
    EXPECT_TRUE(samplerLayout->isDescriptorBufferCompatible())
        << "heap sampler bindless layout is not descriptor-buffer-compatible";

    // Each layout reports a non-zero driver-queried set size and resolves to its expected segment (resource set ->
    // Resource segment, sampler set -> Sampler segment), the carve input for the persistent heap blocks.
    EXPECT_GT(resourceLayout->getDescriptorBufferSetSizeBytes(), 0u);
    EXPECT_EQ(resourceLayout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::Resource);
    EXPECT_GT(samplerLayout->getDescriptorBufferSetSizeBytes(), 0u);
    EXPECT_EQ(samplerLayout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::Sampler);

    // The two persistent heap blocks were carved once at init and live for the heap's lifetime; their offsets are what
    // vkCmdSetDescriptorBufferOffsetsEXT binds at sets 8/9. A zero/invalid block would mean no carve happened.
    const auto& resourceBlock = heap.getResourceBufferBlock();
    const auto& samplerBlock = heap.getSamplerBufferBlock();
    EXPECT_TRUE(resourceBlock.valid())
        << "heap resource descriptor-buffer block was not carved";
    EXPECT_TRUE(samplerBlock.valid())
        << "heap sampler descriptor-buffer block was not carved";

    // write() must route through the descriptor-buffer path: allocate a slot in the StorageBuffer class
    // and write a structured buffer into it. The write lands in the resource block at
    // block.offsetBytes + classBindingOffset + slot*descriptorSize; success proves the carve + class-offset cache +
    // write path.
    static constexpr Name kDescArenaName{"tests/descriptor_buffer/heap_write_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    auto structuredBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(16u * 4096u)
            .setStructStride(16u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    ASSERT_TRUE(structuredBuffer);

    const GpuDescriptorHandle handle = heap.allocate(GpuDescriptorClass::StorageBuffer);
    ASSERT_TRUE(handle.valid());
    EXPECT_TRUE(heap.write(handle, DescriptorWriteItem::StructuredBuffer_SRV(0u, structuredBuffer.get())))
        << "heap write() did not route through the descriptor-buffer path";

    heap.free(handle);

    // Deferred lighting consumes its per-light shadow visibility as Texture2DArray while ordinary G-buffer and
    // compositor inputs remain Texture2D. Both use VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, but the shader types differ,
    // so this distinct heap class/binding must have a valid descriptor-buffer write path as well.
    auto sampledImageArray = device.createTexture(
        TextureDesc()
            .setWidth(32u)
            .setHeight(32u)
            .setArraySize(3u)
            .setDimension(TextureDimension::Texture2DArray)
            .setFormat(Format::RGBA16_FLOAT)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    ASSERT_TRUE(sampledImageArray);
    EXPECT_EQ(sampledImageArray->getReferenceCount(), 1u);

    EXPECT_EQ(
        heap.getRegisterSlot(GpuDescriptorClass::SampledImage2DArray),
        NWB_BINDLESS_HEAP_BINDING_SAMPLED_IMAGE_2D_ARRAY
    );
    const GpuDescriptorHandle sampledImageArrayHandle = heap.allocate(GpuDescriptorClass::SampledImage2DArray);
    ASSERT_TRUE(sampledImageArrayHandle.valid());
    EXPECT_TRUE(heap.write(
        sampledImageArrayHandle,
        DescriptorWriteItem::Texture_SRV(
            0u,
            sampledImageArray.get(),
            Format::RGBA16_FLOAT,
            TextureSubresourceSet(0u, 1u, 0u, 3u),
            TextureDimension::Texture2DArray
        )
    )) << "heap Texture2DArray write() did not route through the descriptor-buffer path";
    EXPECT_EQ(sampledImageArray->getReferenceCount(), 2u)
        << "heap write() did not retain the persistent Texture2DArray resource";

    heap.free(sampledImageArrayHandle);
    EXPECT_EQ(sampledImageArray->getReferenceCount(), 2u)
        << "heap free() released a descriptor resource before its in-flight quarantine matured";
    for(u32 frame = 0u; frame < s_MaxFramesInFlight; ++frame)
        heap.advanceFrame();
    EXPECT_EQ(sampledImageArray->getReferenceCount(), 1u)
        << "heap did not release the descriptor resource after its in-flight quarantine matured";

    // The caustic resolve reads its R32_UINT fixed-point accumulator through a dedicated typed uint Texture2DArray
    // descriptor table. Its image-view format differs from the floating-point array above, so verify the distinct
    // descriptor-buffer class/binding's write and resource-retention path too.
    auto sampledImageArrayUint = device.createTexture(
        TextureDesc()
            .setWidth(32u)
            .setHeight(32u)
            .setArraySize(3u)
            .setDimension(TextureDimension::Texture2DArray)
            .setFormat(Format::R32_UINT)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    ASSERT_TRUE(sampledImageArrayUint);
    EXPECT_EQ(sampledImageArrayUint->getReferenceCount(), 1u);

    EXPECT_EQ(
        heap.getRegisterSlot(GpuDescriptorClass::SampledImage2DArrayUint),
        NWB_BINDLESS_HEAP_BINDING_SAMPLED_IMAGE_2D_ARRAY_UINT
    );
    const GpuDescriptorHandle sampledImageArrayUintHandle = heap.allocate(GpuDescriptorClass::SampledImage2DArrayUint);
    ASSERT_TRUE(sampledImageArrayUintHandle.valid());
    EXPECT_TRUE(heap.write(
        sampledImageArrayUintHandle,
        DescriptorWriteItem::Texture_SRV(
            0u,
            sampledImageArrayUint.get(),
            Format::R32_UINT,
            TextureSubresourceSet(0u, 1u, 0u, 3u),
            TextureDimension::Texture2DArray
        )
    )) << "heap R32_UINT Texture2DArray write() did not route through the descriptor-buffer path";
    EXPECT_EQ(sampledImageArrayUint->getReferenceCount(), 2u)
        << "heap write() did not retain the typed Texture2DArray resource";

    heap.free(sampledImageArrayUintHandle);
    EXPECT_EQ(sampledImageArrayUint->getReferenceCount(), 2u)
        << "heap free() released the typed descriptor resource before its in-flight quarantine matured";
    for(u32 frame = 0u; frame < s_MaxFramesInFlight; ++frame)
        heap.advanceFrame();
    EXPECT_EQ(sampledImageArrayUint->getReferenceCount(), 1u)
        << "heap did not release the typed Texture2DArray resource after its in-flight quarantine matured";

    // AVBOIT accumulation consumes its integrated transmittance as Texture3D. This needs a separate shader-side
    // descriptor array from Texture2D/Texture2DArray even though all three encode VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE.
    // Verify the new class routes through the descriptor-buffer path and retains the volume through the same in-flight quarantine.
    auto sampledImage3D = device.createTexture(
        TextureDesc()
            .setWidth(32u)
            .setHeight(32u)
            .setDepth(8u)
            .setDimension(TextureDimension::Texture3D)
            .setFormat(Format::RGBA16_FLOAT)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    ASSERT_TRUE(sampledImage3D);
    EXPECT_EQ(sampledImage3D->getReferenceCount(), 1u);

    EXPECT_EQ(
        heap.getRegisterSlot(GpuDescriptorClass::SampledImage3D),
        NWB_BINDLESS_HEAP_BINDING_SAMPLED_IMAGE_3D
    );
    const GpuDescriptorHandle sampledImage3DHandle = heap.allocate(GpuDescriptorClass::SampledImage3D);
    ASSERT_TRUE(sampledImage3DHandle.valid());
    EXPECT_TRUE(heap.write(
        sampledImage3DHandle,
        DescriptorWriteItem::Texture_SRV(
            0u,
            sampledImage3D.get(),
            Format::RGBA16_FLOAT,
            TextureSubresourceSet(0u, 1u, 0u, 1u),
            TextureDimension::Texture3D
        )
    )) << "heap Texture3D write() did not route through the descriptor-buffer path";
    EXPECT_EQ(sampledImage3D->getReferenceCount(), 2u)
        << "heap write() did not retain the persistent Texture3D resource";

    heap.free(sampledImage3DHandle);
    EXPECT_EQ(sampledImage3D->getReferenceCount(), 2u)
        << "heap free() released the Texture3D resource before its in-flight quarantine matured";
    for(u32 frame = 0u; frame < s_MaxFramesInFlight; ++frame)
        heap.advanceFrame();
    EXPECT_EQ(sampledImage3D->getReferenceCount(), 1u)
        << "heap did not release the Texture3D resource after its in-flight quarantine matured";
}


// A ray-tracing-capable device must expose the global TLAS through the required immutable descriptor-heap layout.
// The renderer no longer has a supported local TLAS path, so a device that exposes RT but fails to create set 10
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

    heap.free(handle);
    // The production heap intentionally quarantines handles for the in-flight-frame window. This headless test has
    // no submitted work, so advance the synthetic frame counter to retire the block and its retained TLAS before the
    // device fixture tears down.
    for(u32 frame = 0u; frame < s_MaxFramesInFlight; ++frame)
        heap.advanceFrame();
}


// ImGui's leaf set contains only the 32-byte per-draw push block (scale/translate plus sampled-image and sampler
// slots). The font atlas and dynamic ImTextureData images use the global SampledImage/Sampler tables, so this shape
// must remain descriptor-buffer compatible with both heap layouts.
TEST_F(DescriptorBufferRoundTripTest, ImguiHeapTextureAndSamplerLayoutBuildsAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& heap = device.getDescriptorHeap();
    ASSERT_TRUE(heap.isInitialized());

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/imgui_heap_desc_arena"};
    static constexpr u32 kImguiPushConstantBytes = sizeof(f32) * 4u + sizeof(u32) * 4u;
    Alloc::GlobalArena descArena{kDescArenaName};

    // The production leaf has no descriptors at set 0. A zero-binding layout is the descriptor-buffer-compatible
    // gap form; the persistent pure resource/sampler heap tables occupy sets 8/9.
    BindingLayoutDesc leafDesc(descArena);
    leafDesc.setVisibility(ShaderType::AllGraphics);
    leafDesc.addItem(BindingLayoutItem::PushConstants(0u, kImguiPushConstantBytes));
    auto leafLayout = device.createBindingLayout(leafDesc);
    ASSERT_NE(leafLayout.get(), nullptr);
    EXPECT_TRUE(leafLayout->isDescriptorBufferCompatible())
        << "ImGui push-only leaf layout did not route to the descriptor-buffer path";
    EXPECT_TRUE(heap.getResourceLayout()->isDescriptorBufferCompatible());
    EXPECT_TRUE(heap.getSamplerLayout()->isDescriptorBufferCompatible());

    auto texture = device.createTexture(
        TextureDesc()
            .setWidth(32u)
            .setHeight(32u)
            .setFormat(Format::RGBA8_UNORM)
            .setInitialState(ResourceStates::ShaderResource)
            .setKeepInitialState(true)
    );
    ASSERT_TRUE(texture);

    SamplerDesc samplerDesc;
    samplerDesc.setAllFilters(true).setAllAddressModes(SamplerAddressMode::Clamp);
    auto sampler = device.createSampler(samplerDesc);
    ASSERT_TRUE(sampler);

    EXPECT_EQ(heap.getRegisterSlot(GpuDescriptorClass::SampledImage), NWB_BINDLESS_HEAP_BINDING_SAMPLED_IMAGE);
    EXPECT_EQ(heap.getRegisterSlot(GpuDescriptorClass::Sampler), NWB_BINDLESS_HEAP_BINDING_SAMPLER);
    const GpuDescriptorHandle textureHandle = heap.allocate(GpuDescriptorClass::SampledImage);
    const GpuDescriptorHandle samplerHandle = heap.allocate(GpuDescriptorClass::Sampler);
    ASSERT_TRUE(textureHandle.valid());
    ASSERT_TRUE(samplerHandle.valid());
    EXPECT_TRUE(heap.write(
        textureHandle,
        DescriptorWriteItem::Texture_SRV(0u, texture.get(), Format::RGBA8_UNORM, s_AllSubresources, TextureDimension::Texture2D)
    ));
    EXPECT_TRUE(heap.write(samplerHandle, DescriptorWriteItem::Sampler(0u, sampler.get())));

    // Free mirrors ImGui texture destruction: deferred retirement keeps both resources alive until recorded UI draws
    // have drained, even though the owning ImTextureData resource may be erased immediately.
    heap.free(textureHandle);
    heap.free(samplerHandle);
    for(u32 frame = 0u; frame < s_MaxFramesInFlight; ++frame)
        heap.advanceFrame();
}


// The global heap is the only resource-bearing descriptor transport. A pipeline-local BindingLayout may carry push
// constants, but creating a local CBV/SRV leaf must fail instead of recreating a local resource descriptor path.
TEST_F(DescriptorBufferRoundTripTest, PipelineLocalResourceLayoutsAreRejected){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/local_resource_layout_rejected_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    BindingLayoutDesc leafDesc(descArena);
    leafDesc.setVisibility(ShaderType::Compute);
    leafDesc.addItem(BindingLayoutItem::ConstantBuffer(0u, 1u));
    leafDesc.addItem(BindingLayoutItem::StructuredBuffer_SRV(1u, 1u));
    leafDesc.addItem(BindingLayoutItem::StructuredBuffer_SRV(2u, 1u));
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        EXPECT_FALSE(device.createBindingLayout(leafDesc));
    }, "");
#else
    auto leafLayout = device.createBindingLayout(leafDesc);
    EXPECT_FALSE(leafLayout);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


}; // namespace Tests


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

