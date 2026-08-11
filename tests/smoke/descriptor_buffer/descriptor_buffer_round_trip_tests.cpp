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
#include <core/graphics/capture/command_ir.h>
#include <core/graphics/task_graph/compiler.h>
#include <core/graphics/task_graph/packet_runtime.h>
#include <core/perf/timing.h>
#include <impl/assets/graphics/avboit/constants.h>
#include <impl/assets/graphics/bindless/runtime_abi.h>
#include <impl/assets/graphics/skinned_mesh/constants.h>
#include <impl/ecs_ui/texture_submission.h>
#include <tests/common/capturing_logger.h>

// The manager lives in the Vulkan backend (Core::GraphicsBackend namespace). The test is inherently Vulkan-aware
// (VkDescriptorType, descriptor-buffer entry points), so the concrete backend header is the right include here
// rather than reaching through a forward declaration.
#include <core/graphics/vulkan/backend.h>

#include <volk/volk.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


using namespace Core;

inline constexpr GpuTimingScopeDefinition s_FrameTransactionScope("tests/timing_frame_transaction");


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
        , m_threadPool(s_TestWorkerThreadCount, CpuAffinity::Any)
        , m_jobSystem(m_threadPool)
        , m_gpuTiming(m_objectArena)
        , m_graphics(m_allocator, m_threadPool, m_jobSystem, m_gpuTiming)
    {}

    ~HeadlessGraphicsScope(){}

    // Returns false on driver/instance failure (no Vulkan, no physical device, etc.). The caller SKIPS in that case
    // rather than failing — a CI runner without a GPU is an environment condition.
    [[nodiscard]] bool initialize(){
        if(!m_graphics.setBindlessHeapAbi(Impl::AssetsGraphicsBindless::MakeGpuDescriptorHeapAbi()))
            return false;
        return m_graphics.createHeadlessDevice();
    }

    [[nodiscard]] bool setAsyncComputeLaneEnabled(const bool enabled){
        return m_graphics.setAsyncComputeLaneEnabled(enabled);
    }

    [[nodiscard]] bool setTransferQueueEnabled(const bool enabled){
        return m_graphics.setTransferQueueEnabled(enabled);
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
        // installed. Register the capturing logger before bring-up so failures are recorded rather than
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


[[nodiscard]] static TextureHandle CreateConcurrentTestTexture(
    GraphicsBackend::Device& device,
    const bool keepInitialState = false
){
    TextureDesc desc;
    desc
        .setWidth(4u)
        .setHeight(4u)
        .setFormat(Format::RGBA8_UNORM)
        .setInUAV(true)
        .setInitialState(ResourceStates::Common)
        .setQueueSharing(ResourceQueueSharing::GraphicsAndAsyncCompute)
    ;
    if(keepInitialState)
        desc.setKeepInitialState(true);
    return device.createTexture(desc);
}

[[nodiscard]] static TextureHandle CreateExclusiveRayOutputTestTexture(GraphicsBackend::Device& device){
    return device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::RGBA8_UNORM)
            .setInUAV(true)
            .setInitialState(ResourceStates::Common)
    );
}

// Graph topology IDs must be the identities emitted by the concrete Device, not local array positions. This keeps
// the native packet smoke paths honest across device recreation and distinct Compute/Transfer transports.
[[nodiscard]] static GpuPhysicalQueueId BackendQueueId(
    GraphicsBackend::Device& device,
    const CommandQueue::Enum queue
){
    return device.getPrimaryPhysicalQueue(queue);
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


// The native registry is authoritative: graph packets and direct physical submissions must name a concrete VkQueue
// rather than infer its identity from a CommandQueue ordinal. Keep this on a real device so the registry, command
// pool selection, and submission token all agree even when a device exposes several queues of one broad class.
TEST_F(DescriptorBufferRoundTripTest, NativePhysicalQueueRegistryDrivesExactSubmission){
    auto& nativeDevice = device();
    const GpuPhysicalQueueTopology topology = nativeDevice.getPhysicalQueueTopology();
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);

    const GpuPhysicalQueueId graphicsQueue = nativeDevice.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_TRUE(graphicsQueue.valid());
    ASSERT_TRUE(nativeDevice.matchesPhysicalQueueIdentity(graphicsQueue));
    EXPECT_EQ(BackendQueueId(nativeDevice, CommandQueue::Graphics), graphicsQueue);

    bool foundGraphicsQueue = false;
    for(usize index = 0u; index < topology.queueCount; ++index){
        const GpuPhysicalQueueInfo& queue = topology.queues[index];
        EXPECT_TRUE(queue.id.valid());
        EXPECT_EQ(queue.id.deviceGeneration, nativeDevice.getDeviceGeneration());
        EXPECT_EQ(nativeDevice.getPhysicalQueueInfo(queue.id), &queue);
        EXPECT_TRUE(nativeDevice.matchesPhysicalQueueIdentity(queue.id));
        EXPECT_TRUE(nativeDevice.matchesPhysicalQueueIdentity(
            queue.queueClass,
            queue.id.index,
            queue.id.deviceGeneration
        ));
        EXPECT_NE(nativeDevice.getNativeQueue(GraphicsBackend::ObjectTypes::VK_Queue, queue.id), Object(nullptr));
        foundGraphicsQueue = foundGraphicsQueue || queue.id == graphicsQueue;
    }
    EXPECT_TRUE(foundGraphicsQueue);

    CommandListParameters parameters;
    parameters.setPhysicalQueue(graphicsQueue);
    auto commandList = nativeDevice.createCommandList(parameters);
    ASSERT_NE(commandList.get(), nullptr);
    EXPECT_EQ(commandList->getDescription().physicalQueue, graphicsQueue);
    commandList->open();
    commandList->close();

    CommandList* const commandLists[] = { commandList.get() };
    const QueueSubmissionToken token = nativeDevice.executeCommandLists(
        commandLists,
        LengthOf(commandLists),
        graphicsQueue,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(token.valid());
    EXPECT_TRUE(token.matchesPhysicalQueue(graphicsQueue.index, graphicsQueue.deviceGeneration));
    EXPECT_EQ(token.queue, CommandQueue::Graphics);
}


// A timeline value is only meaningful for the Device that issued it.  Make the rejection observable at the native
// submission boundary so an imported graph completion cannot accidentally wait on a recycled queue timeline after
// device recreation.
TEST_F(DescriptorBufferRoundTripTest, QueueSubmissionRejectsRetiredDeviceGeneration){
    QueueSubmissionToken retiredToken;
    {
        HeadlessGraphicsScope producerScope;
        ASSERT_TRUE(producerScope.initialize());
        auto& producer = producerScope.graphics().getDevice();

        auto producerCommandList = producer.createCommandList();
        ASSERT_NE(producerCommandList.get(), nullptr);
        producerCommandList->open();
        producerCommandList->close();
        CommandList* const producerCommandLists[] = { producerCommandList.get() };

        retiredToken = producer.executeCommandLists(
            producerCommandLists,
            LengthOf(producerCommandLists),
            CommandQueue::Graphics,
            QueueSubmissionDesc{}
        );
        ASSERT_TRUE(retiredToken.valid());
        ASSERT_TRUE(retiredToken.hasPhysicalQueueIdentity());
        EXPECT_TRUE(producer.matchesPhysicalQueueIdentity(
            retiredToken.queue,
            retiredToken.physicalQueueIndex,
            retiredToken.deviceGeneration
        ));
    }

    HeadlessGraphicsScope consumerScope;
    ASSERT_TRUE(consumerScope.initialize());
    auto& consumer = consumerScope.graphics().getDevice();
    EXPECT_NE(retiredToken.deviceGeneration, consumer.getDeviceGeneration());
    EXPECT_FALSE(consumer.matchesPhysicalQueueIdentity(
        retiredToken.queue,
        retiredToken.physicalQueueIndex,
        retiredToken.deviceGeneration
    ));

    const QueueSubmissionDesc staleWait = QueueSubmissionDesc().setWaitTokens(&retiredToken, 1u);
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        EXPECT_FALSE(consumer.executeCommandLists(nullptr, 0u, CommandQueue::Graphics, staleWait).valid());
    }, "");
#else
    EXPECT_FALSE(consumer.executeCommandLists(nullptr, 0u, CommandQueue::Graphics, staleWait).valid());
#endif
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


struct NativePacketPrefixTask{
    struct Payload{
        Buffer* buffer = nullptr;
        ResourceStates::Mask expectedState = ResourceStates::Unknown;
        Texture* texture = nullptr;
        ResourceStates::Mask expectedTextureState = ResourceStates::Unknown;
        Texture* additionalTexture = nullptr;
        ResourceStates::Mask expectedAdditionalTextureState = ResourceStates::Unknown;
        bool* recorded = nullptr;
        QueueSubmissionToken* acceptedToken = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.buffer)
            return false;
        bool ready = commandList.getBufferState(payload.buffer) == payload.expectedState;
        if(payload.texture)
            ready = ready && commandList.getTextureSubresourceState(payload.texture, 0u, 0u) == payload.expectedTextureState;
        if(payload.additionalTexture)
            ready = ready && commandList.getTextureSubresourceState(payload.additionalTexture, 0u, 0u) == payload.expectedAdditionalTextureState;
        if(payload.recorded)
            *payload.recorded = ready;
        return ready;
    }

    static void accepted(Payload& payload, const QueueSubmissionToken& token){
        if(payload.acceptedToken)
            *payload.acceptedToken = token;
    }
};


// A mutable success gate lets the packet recorder prove that an optional capture rolls back an incomplete packet
// before a retry. It intentionally records no native work beyond the task marker.
struct NativePacketCaptureRetryTask{
    struct Payload{
        const bool* shouldRecord = nullptr;
        bool* attempted = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(commandList);
        static_cast<void>(context);
        if(payload.attempted)
            *payload.attempted = true;
        return payload.shouldRecord && *payload.shouldRecord;
    }
};


struct NativePacketRangeAcceptanceObserver{
    u32 acceptedCount = 0u;
    GpuSubmissionPacketId lastPacket;
    QueueSubmissionToken lastToken;
    bool continueSubmission = true;
};


[[nodiscard]] static bool ObserveNativePacketRangeAcceptance(
    void* const rawContext,
    const GpuSubmissionPacketId& packet,
    const QueueSubmissionToken& token
){
    NativePacketRangeAcceptanceObserver* const context =
        static_cast<NativePacketRangeAcceptanceObserver*>(rawContext)
    ;
    if(!context || !packet.valid() || !token.valid())
        return false;
    ++context->acceptedCount;
    context->lastPacket = packet;
    context->lastToken = token;
    return context->continueSubmission;
}


// The production presentation hook supplies a real swap-chain binary semaphore. The headless fixture cannot
// manufacture one through BackendContext, so this probe verifies range validation rejects a hook before native
// submission or callback invocation.
struct NativePacketSubmissionHookObserver{
    u32 invocationCount = 0u;
};

[[nodiscard]] static bool RejectNativePacketSubmissionHook(
    void* const rawContext,
    const GpuPhysicalQueueId& executionQueue,
    QueueSubmissionNativeSignal& outSignal
){
    NativePacketSubmissionHookObserver* const context =
        static_cast<NativePacketSubmissionHookObserver*>(rawContext)
    ;
    if(!context || !executionQueue.valid())
        return false;
    ++context->invocationCount;
    outSignal = {};
    return false;
}


// Minimal graph-native timing endpoints used to exercise a late recovery packet in the same submission transaction.
struct NativeFrameTimingPacketTask{
    enum class Endpoint : u8{
        Begin,
        End,
    };

    struct Payload{
        Device* device = nullptr;
        GpuTimingFrameTransaction* transaction = nullptr;
        GpuTimingSubmissionTicket* timingTicket = nullptr;
        Endpoint endpoint = Endpoint::Begin;
        bool* recorded = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.device || !payload.transaction || !payload.timingTicket)
            return false;
        GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        const bool ready = payload.endpoint == Endpoint::Begin
            ? payload.transaction->begin(s_FrameTransactionScope, *payload.device, commandList)
            : payload.transaction->recordEnd(commandList)
        ;
        if(payload.recorded)
            *payload.recorded = ready;
        return ready;
    }
};


struct NativeFrameRecoveryPacketTask{
    struct Payload{
        GpuTimingFrameTransaction* transaction = nullptr;
        bool* armed = nullptr;
        bool* retiresTiming = nullptr;
        bool* recorded = nullptr;
        bool* accepted = nullptr;
        bool* discarded = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        const bool ready = payload.transaction
            && payload.armed
            && payload.retiresTiming
            && *payload.armed
            && (!*payload.retiresTiming || payload.transaction->recordEnd(commandList))
        ;
        if(payload.recorded)
            *payload.recorded = ready;
        return ready;
    }

    static void accepted(Payload& payload, const QueueSubmissionToken& token){
        static_cast<void>(token);
        if(
            payload.transaction
            && payload.armed
            && payload.retiresTiming
            && *payload.armed
            && *payload.retiresTiming
        ){
            if(!payload.transaction->confirmEndSubmission(false))
                payload.transaction->discard();
        }
        if(payload.armed)
            *payload.armed = false;
        if(payload.retiresTiming)
            *payload.retiresTiming = false;
        if(payload.accepted)
            *payload.accepted = true;
    }

    static void discarded(Payload& payload){
        if(payload.transaction && payload.armed && *payload.armed)
            payload.transaction->discard();
        if(payload.armed)
            *payload.armed = false;
        if(payload.retiresTiming)
            *payload.retiresTiming = false;
        if(payload.discarded)
            *payload.discarded = true;
    }
};


struct NativeShadowPrepareTask{
    struct Payload{
        Buffer* bindlessSlots = nullptr;
        bool* recorded = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.bindlessSlots)
            return false;
        // The synthetic chain models an already-uploaded selector. Record its known final state so a later packet
        // can import the native snapshot even though the compiler correctly plans no initial Common transition.
        commandList.setBufferState(payload.bindlessSlots, ResourceStates::ConstantBuffer);
        commandList.commitBarriers();
        const bool ready = commandList.getBufferState(payload.bindlessSlots) == ResourceStates::ConstantBuffer;
        if(payload.recorded)
            *payload.recorded = ready;
        return ready;
    }
};


TEST_F(DescriptorBufferRoundTripTest, NativePacketRecordsPrefixSequenceAndExportsFinalState){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto buffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(buffer.get(), nullptr);
    auto texture = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::RGBA8_UNORM)
            .setInRenderTarget(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(texture.get(), nullptr);
    auto additionalTexture = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::RGBA8_UNORM)
            .setInUAV(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(additionalTexture.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId resource = graph.importBuffer(
        buffer,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/merged_packet_buffer"))
            .setMarkerLabel("Merged Packet Buffer")
            .setType(GpuGraphResourceType::Buffer)
    );
    ASSERT_TRUE(resource.valid());
    const GpuGraphResourceId textureResource = graph.importTexture(
        texture,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/merged_packet_texture"))
            .setMarkerLabel("Merged Packet Texture")
            .setType(GpuGraphResourceType::Texture)
    );
    ASSERT_TRUE(textureResource.valid());
    const GpuGraphResourceId additionalTextureResource = graph.importTexture(
        additionalTexture,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/merged_packet_additional_texture"))
            .setMarkerLabel("Merged Packet Additional Texture")
            .setType(GpuGraphResourceType::Texture)
    );
    ASSERT_TRUE(additionalTextureResource.valid());

    const GpuTaskResourceUse meshViewSetupUses[] = {
        GpuTaskResourceUse{
            .resource = resource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    GpuTaskSchedulingHint meshViewSetupScheduling;
    meshViewSetupScheduling.allowPacketMerge = true;
    GpuTaskDesc meshViewSetupDesc;
    meshViewSetupDesc
        .setIdentity(Name("tests/descriptor_buffer/native_packet_mesh_view_setup"))
        .setMarkerLabel("Native Packet Mesh View Setup")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Graphics,
            GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setScheduling(meshViewSetupScheduling)
        .setResourceUses(meshViewSetupUses, LengthOf(meshViewSetupUses))
    ;
    bool nativeMeshViewSetupRecorded = false;
    const GpuTaskId meshViewSetupTask = graph.addTask<NativePacketPrefixTask>(
        meshViewSetupDesc,
        NativePacketPrefixTask::Payload{
            .buffer = buffer.get(),
            .expectedState = ResourceStates::ConstantBuffer,
            .recorded = &nativeMeshViewSetupRecorded,
        }
    );
    ASSERT_TRUE(meshViewSetupTask.valid());

    const GpuTaskResourceUse sceneSetupUses[] = {
        GpuTaskResourceUse{
            .resource = resource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    GpuTaskSchedulingHint sceneSetupScheduling;
    sceneSetupScheduling.allowPacketMerge = true;
    sceneSetupScheduling.mergeWithPrevious = true;
    GpuTaskDesc sceneSetupDesc;
    sceneSetupDesc
        .setIdentity(Name("tests/descriptor_buffer/merged_packet_scene_setup"))
        .setMarkerLabel("Merged Packet Scene Setup")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Graphics,
            GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setScheduling(sceneSetupScheduling)
        .setDependencies(&meshViewSetupTask, 1u)
        .setResourceUses(sceneSetupUses, LengthOf(sceneSetupUses))
    ;
    bool nativeSceneSetupRecorded = false;
    const GpuTaskId sceneSetupTask = graph.addTask<NativePacketPrefixTask>(
        sceneSetupDesc,
        NativePacketPrefixTask::Payload{
            .buffer = buffer.get(),
            .expectedState = ResourceStates::ConstantBuffer,
            .recorded = &nativeSceneSetupRecorded,
        }
    );
    ASSERT_TRUE(sceneSetupTask.valid());

    const GpuTaskResourceUse clearUses[] = {
        GpuTaskResourceUse{
            .resource = resource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = textureResource,
            .range = {},
            .requiredState = ResourceStates::CopyDest,
            .access = GpuTaskResourceAccess::Write,
        },
        GpuTaskResourceUse{
            .resource = additionalTextureResource,
            .range = {},
            .requiredState = ResourceStates::CopyDest,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    GpuTaskSchedulingHint clearScheduling;
    clearScheduling.allowPacketMerge = true;
    clearScheduling.mergeWithPrevious = true;
    GpuTaskDesc clearDesc;
    clearDesc
        .setIdentity(Name("tests/descriptor_buffer/merged_packet_clear"))
        .setMarkerLabel("Merged Packet Clear")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Graphics,
            GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setScheduling(clearScheduling)
        .setDependencies(&sceneSetupTask, 1u)
        .setResourceUses(clearUses, LengthOf(clearUses))
    ;
    bool nativeClearRecorded = false;
    const GpuTaskId clearTask = graph.addTask<NativePacketPrefixTask>(
        clearDesc,
        NativePacketPrefixTask::Payload{
            .buffer = buffer.get(),
            .expectedState = ResourceStates::ConstantBuffer,
            .texture = texture.get(),
            .expectedTextureState = ResourceStates::CopyDest,
            .additionalTexture = additionalTexture.get(),
            .expectedAdditionalTextureState = ResourceStates::CopyDest,
            .recorded = &nativeClearRecorded,
        }
    );
    ASSERT_TRUE(clearTask.valid());

    const GpuTaskResourceUse gbufferUses[] = {
        GpuTaskResourceUse{
            .resource = resource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = textureResource,
            .range = {},
            .requiredState = ResourceStates::RenderTarget,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    GpuTaskSchedulingHint gbufferScheduling;
    gbufferScheduling.allowPacketMerge = true;
    gbufferScheduling.mergeWithPrevious = true;
    GpuTaskDesc gbufferDesc;
    gbufferDesc
        .setIdentity(Name("tests/descriptor_buffer/merged_packet_gbuffer"))
        .setMarkerLabel("Merged Packet G-Buffer")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Graphics,
            GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setScheduling(gbufferScheduling)
        .setDependencies(&clearTask, 1u)
        .setResourceUses(gbufferUses, LengthOf(gbufferUses))
    ;
    bool nativeGbufferRecorded = false;
    const GpuTaskId gbufferTask = graph.addTask<NativePacketPrefixTask>(
        gbufferDesc,
        NativePacketPrefixTask::Payload{
            .buffer = buffer.get(),
            .expectedState = ResourceStates::ConstantBuffer,
            .texture = texture.get(),
            .expectedTextureState = ResourceStates::RenderTarget,
            .additionalTexture = additionalTexture.get(),
            .expectedAdditionalTextureState = ResourceStates::CopyDest,
            .recorded = &nativeGbufferRecorded,
        }
    );
    ASSERT_TRUE(gbufferTask.valid());

    const GpuTaskResourceUse normalizeUses[] = {
        GpuTaskResourceUse{
            .resource = resource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = textureResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    GpuTaskSchedulingHint normalizeScheduling;
    normalizeScheduling.allowPacketMerge = true;
    normalizeScheduling.mergeWithPrevious = true;
    GpuTaskDesc normalizeDesc;
    normalizeDesc
        .setIdentity(Name("tests/descriptor_buffer/merged_packet_normalize"))
        .setMarkerLabel("Merged Packet Normalize")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Graphics,
            GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setScheduling(normalizeScheduling)
        .setDependencies(&gbufferTask, 1u)
        .setResourceUses(normalizeUses, LengthOf(normalizeUses))
    ;
    bool nativeNormalizeRecorded = false;
    const GpuTaskId normalizeTask = graph.addTask<NativePacketPrefixTask>(
        normalizeDesc,
        NativePacketPrefixTask::Payload{
            .buffer = buffer.get(),
            .expectedState = ResourceStates::ConstantBuffer,
            .texture = texture.get(),
            .expectedTextureState = ResourceStates::ShaderResource,
            .additionalTexture = additionalTexture.get(),
            .expectedAdditionalTextureState = ResourceStates::CopyDest,
            .recorded = &nativeNormalizeRecorded,
        }
    );
    ASSERT_TRUE(normalizeTask.valid());

    const GpuPhysicalQueueInfo queue{
        .id = BackendQueueId(device, CommandQueue::Graphics),
        .queueClass = CommandQueue::Graphics,
        .capabilities = static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
            | static_cast<u8>(GpuQueueCapability::Transfer)
        ),
        .familyIndex = 0u,
        .queueIndex = 0u,
        .dedicated = false,
    };
    const GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/merged_packet_scratch"));
    const GpuTaskGraphCompiler compiler;
    ASSERT_TRUE(compiler.compile(graph, analysis, topology, assignments, compiledGraph, scratchArena));
    ASSERT_EQ(compiledGraph.packetCount(), 1u);
    const GpuSubmissionPacketId packet = compiledGraph.packetForTask(normalizeTask);
    const GpuSubmissionPacketRange packetRange = compiledGraph.allPacketRange();
    ASSERT_TRUE(packet.valid());
    ASSERT_TRUE(packetRange.valid());
    ASSERT_EQ(packetRange.packetCount, 1u);
    EXPECT_EQ(packet, compiledGraph.packetForTask(meshViewSetupTask));
    EXPECT_EQ(packet, compiledGraph.packetForTask(sceneSetupTask));
    EXPECT_EQ(packet, compiledGraph.packetForTask(clearTask));
    EXPECT_EQ(packet, compiledGraph.packetForTask(gbufferTask));
    const GpuSubmissionPacket& packetPlan = compiledGraph.packet(packet);
    ASSERT_EQ(packetPlan.taskCount, 5u);
    ASSERT_NE(compiledGraph.packetTasks(packet), nullptr);
    EXPECT_EQ(compiledGraph.packetTasks(packet)[0u], meshViewSetupTask);
    EXPECT_EQ(compiledGraph.packetTasks(packet)[1u], sceneSetupTask);
    EXPECT_EQ(compiledGraph.packetTasks(packet)[2u], clearTask);
    EXPECT_EQ(compiledGraph.packetTasks(packet)[3u], gbufferTask);
    EXPECT_EQ(compiledGraph.packetTasks(packet)[4u], normalizeTask);
    EXPECT_TRUE(graph.taskAt(meshViewSetupTask.index).hasPayload);
    EXPECT_TRUE(graph.taskAt(sceneSetupTask.index).hasPayload);
    EXPECT_TRUE(graph.taskAt(clearTask.index).hasPayload);
    EXPECT_TRUE(graph.taskAt(gbufferTask.index).hasPayload);
    EXPECT_TRUE(graph.taskAt(normalizeTask.index).hasPayload);
    ASSERT_NE(compiledGraph.findTask(sceneSetupTask), nullptr);
    ASSERT_NE(compiledGraph.findTask(clearTask), nullptr);
    ASSERT_NE(compiledGraph.findTask(gbufferTask), nullptr);
    ASSERT_NE(compiledGraph.findTask(normalizeTask), nullptr);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        packetRange,
        nullptr,
        0u,
        recordedGraph
    ));
    ASSERT_TRUE(nativeMeshViewSetupRecorded);
    ASSERT_TRUE(nativeSceneSetupRecorded);
    ASSERT_TRUE(nativeClearRecorded);
    ASSERT_TRUE(nativeGbufferRecorded);
    ASSERT_TRUE(nativeNormalizeRecorded);
    const GpuRecordedPacket* const recordedPacket = recordedGraph.find(packet);
    ASSERT_NE(recordedPacket, nullptr);
    EXPECT_EQ(recordedPacket->commandListCount, 1u);
    const CommandListResourceStateHandoff* const finalState = recordedGraph.packetFinalStateSeed(packet);
    ASSERT_NE(finalState, nullptr);

    auto stateProbe = device.createCommandList();
    ASSERT_NE(stateProbe.get(), nullptr);
    stateProbe->open(finalState);
    EXPECT_EQ(stateProbe->getBufferState(buffer.get()), ResourceStates::ConstantBuffer);
    EXPECT_EQ(stateProbe->getTextureSubresourceState(texture.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(stateProbe->getTextureSubresourceState(additionalTexture.get(), 0u, 0u), ResourceStates::CopyDest);
    stateProbe->close();

    const GpuTaskGraphSubmitter submitter(device);
    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        packetRange,
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    EXPECT_TRUE(transaction.packetToken(packet).valid());
    EXPECT_TRUE(device.waitForIdle());
}


// Primitive copies belong to the graph as typed task payloads: it derives their resource-state declarations,
// records direct native copies on the resolved physical queue, and publishes the accepted packet token only after
// submission. On this host the same proof automatically exercises either a dedicated Transfer family or Graphics
// fallback without a renderer-specific command thunk.
TEST_F(DescriptorBufferRoundTripTest, BuiltInCopyTextureTaskRecordsAndPublishesAcceptedToken){
    auto& device = DescriptorBufferRoundTripTest::device();
    const TextureDesc copyTextureDesc = TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setFormat(Format::RGBA8_UNORM)
        .setInUAV(true)
        .setInitialState(ResourceStates::Common)
        .setQueueSharing(ResourceQueueSharing::GraphicsAndTransfer)
    ;
    auto source = device.createTexture(copyTextureDesc);
    auto destination = device.createTexture(copyTextureDesc);
    ASSERT_NE(source.get(), nullptr);
    ASSERT_NE(destination.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId sourceResource = graph.importTexture(
        source,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/built_in_copy_source"))
            .setMarkerLabel("Built-In Copy Source")
            .setType(GpuGraphResourceType::Texture)
    );
    const GpuGraphResourceId destinationResource = graph.importTexture(
        destination,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/built_in_copy_destination"))
            .setMarkerLabel("Built-In Copy Destination")
            .setType(GpuGraphResourceType::Texture)
    );
    ASSERT_TRUE(sourceResource.valid());
    ASSERT_TRUE(destinationResource.valid());

    GpuTaskSchedulingHint copyScheduling;
    copyScheduling.cost = GpuTaskCostHint::Medium;
    copyScheduling.forceSubmissionBoundary = true;
    copyScheduling.allowPacketMerge = false;
    GpuTaskDesc copyTaskDesc;
    copyTaskDesc
        .setIdentity(Name("tests/descriptor_buffer/built_in_copy_texture"))
        .setMarkerLabel("Built-In Copy Texture")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Transfer,
            GpuQueuePreference::Transfer,
            true,
            true,
        })
        .setScheduling(copyScheduling)
    ;
    const GpuCopyTextureTaskRegion copyRegions[] = {
        GpuCopyTextureTaskRegion{
            .source = sourceResource,
            .destination = destinationResource,
        },
    };
    QueueSubmissionToken acceptedToken;
    const GpuTaskId copyTask = graph.addCopyTextureTask(
        copyTaskDesc,
        GpuCopyTextureTaskDesc{
            .regions = copyRegions,
            .regionCount = LengthOf(copyRegions),
            .acceptedToken = &acceptedToken,
        }
    );
    ASSERT_TRUE(copyTask.valid());
    ASSERT_TRUE(graph.taskAt(copyTask.index).hasPayload);
    ASSERT_EQ(graph.taskAt(copyTask.index).resourceUseCount, 2u);

    const u32 graphicsFamily = device.getQueueFamilyIndex(CommandQueue::Graphics);
    const u32 transferFamily = device.getQueueFamilyIndex(CommandQueue::Transfer);
    const bool dedicatedTransfer = device.getQueue(CommandQueue::Transfer)
        && transferFamily != Limit<u32>::s_Max
        && transferFamily != graphicsFamily
    ;
    GpuPhysicalQueueInfo queues[2u] = {
        GpuPhysicalQueueInfo{
            .id = BackendQueueId(device, CommandQueue::Graphics),
            .queueClass = CommandQueue::Graphics,
            .capabilities = static_cast<GpuQueueCapability::Mask>(
                static_cast<u8>(GpuQueueCapability::Graphics)
                | static_cast<u8>(GpuQueueCapability::Compute)
                | static_cast<u8>(GpuQueueCapability::Transfer)
            ),
            .familyIndex = graphicsFamily,
            .queueIndex = 0u,
            .dedicated = false,
        },
    };
    usize queueCount = 1u;
    if(dedicatedTransfer){
        queues[queueCount] = GpuPhysicalQueueInfo{
            .id = BackendQueueId(device, CommandQueue::Transfer),
            .queueClass = CommandQueue::Transfer,
            .capabilities = GpuQueueCapability::Transfer,
            .familyIndex = transferFamily,
            .queueIndex = 0u,
            .dedicated = true,
        };
        ++queueCount;
    }
    const GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = queueCount,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/built_in_copy_texture_scratch"));
    const GpuTaskGraphCompiler compiler;
    ASSERT_TRUE(compiler.compile(graph, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskQueueAssignment* const assignment = assignments.find(copyTask);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(
        assignment->queueClass,
        dedicatedTransfer ? CommandQueue::Transfer : CommandQueue::Graphics
    );
    const GpuSubmissionPacketId packet = compiledGraph.packetForTask(copyTask);
    ASSERT_TRUE(packet.valid());

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    GpuCommandIrCapture commandIrCapture(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        compiledGraph.allPacketRange(),
        nullptr,
        0u,
        recordedGraph,
        nullptr,
        &commandIrCapture
    ));
    ASSERT_EQ(commandIrCapture.recordCount(), 1u);
    const GpuCommandIrBuiltinTaskRecord* const copyCapture = commandIrCapture.recordAt(0u);
    ASSERT_NE(copyCapture, nullptr);
    EXPECT_EQ(copyCapture->opcode, GpuCommandIrOpcode::CopyTexture);
    EXPECT_EQ(copyCapture->task, copyTask);
    EXPECT_EQ(copyCapture->packet, packet);
    EXPECT_EQ(copyCapture->queue, compiledGraph.packet(packet).queue);
    EXPECT_EQ(copyCapture->source, sourceResource);
    EXPECT_EQ(copyCapture->destination, destinationResource);
    EXPECT_EQ(copyCapture->sourceSlice.mipLevel, copyRegions[0u].sourceSlice.mipLevel);
    EXPECT_EQ(copyCapture->sourceSlice.arraySlice, copyRegions[0u].sourceSlice.arraySlice);
    EXPECT_EQ(copyCapture->destinationSlice.mipLevel, copyRegions[0u].destinationSlice.mipLevel);
    EXPECT_EQ(copyCapture->destinationSlice.arraySlice, copyRegions[0u].destinationSlice.arraySlice);
    const CommandListResourceStateHandoff* const finalState = recordedGraph.packetFinalStateSeed(packet);
    ASSERT_NE(finalState, nullptr);

    const GpuTaskGraphSubmitter submitter(device);
    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        compiledGraph.allPacketRange(),
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    const QueueSubmissionToken packetToken = transaction.packetToken(packet);
    ASSERT_TRUE(packetToken.valid());
    EXPECT_TRUE(acceptedToken.valid());
    EXPECT_EQ(acceptedToken.queue, packetToken.queue);
    EXPECT_EQ(acceptedToken.value, packetToken.value);
    EXPECT_TRUE(device.waitForIdle());
}


// Upload blobs copy caller bytes at declaration, then record through the ordinary CommandList staging allocator.
// Mutating the original stack storage before late packet recording must therefore not affect the submitted upload.
TEST_F(DescriptorBufferRoundTripTest, BuiltInUploadBufferTaskCopiesGraphOwnedBlobAndPublishesAcceptedToken){
    auto& device = DescriptorBufferRoundTripTest::device();
    u32 sourceWords[] = {
        0x13c0ffeeu,
        0x4a7b12d3u,
        0x9e3779b9u,
        0xfeedfaceu,
    };
    static constexpr u32 s_ExpectedWords[] = {
        0x13c0ffeeu,
        0x4a7b12d3u,
        0x9e3779b9u,
        0xfeedfaceu,
    };
    auto destination = device.createBuffer(
        BufferDesc()
            .setByteSize(sizeof(sourceWords))
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::Exclusive)
            .setCpuAccess(CpuAccessMode::Read)
    );
    ASSERT_NE(destination.get(), nullptr);
    auto keepInitialDestination = device.createBuffer(
        BufferDesc()
            .setByteSize(sizeof(sourceWords))
            .enableAutomaticStateTracking(ResourceStates::Common)
    );
    ASSERT_NE(keepInitialDestination.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId destinationResource = graph.importBuffer(
        destination,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/built_in_upload_buffer_destination"))
            .setMarkerLabel("Built-In Upload Buffer Destination")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(destinationResource.valid());
    const GpuGraphResourceId keepInitialDestinationResource = graph.importBuffer(
        keepInitialDestination,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/built_in_upload_buffer_keep_initial_destination"))
            .setMarkerLabel("Built-In Upload Buffer Keep Initial Destination")
            .setType(GpuGraphResourceType::Buffer)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(keepInitialDestinationResource.valid());
    const GpuUploadBlobId source = graph.copyUploadData(sourceWords, sizeof(sourceWords), alignof(u32));
    ASSERT_TRUE(source.valid());
    ASSERT_TRUE(graph.validUploadBlob(source));
    ASSERT_EQ(graph.uploadBlobCount(), 1u);
    for(u32& word : sourceWords)
        word = 0u;

    GpuTaskSchedulingHint scheduling;
    scheduling.cost = GpuTaskCostHint::Medium;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    GpuTaskDesc uploadTaskDesc;
    uploadTaskDesc
        .setIdentity(Name("tests/descriptor_buffer/built_in_upload_buffer"))
        .setMarkerLabel("Built-In Buffer Upload")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Transfer,
            GpuQueuePreference::Transfer,
            true,
            true,
        })
        .setScheduling(scheduling)
    ;
    const QueueSubmissionToken staleToken{
        .queue = CommandQueue::Graphics,
        .value = 7u,
    };
    ASSERT_TRUE(staleToken.valid());
    QueueSubmissionToken acceptedToken = staleToken;
    const GpuTaskId uploadTask = graph.addUploadBufferTask(
        uploadTaskDesc,
        GpuUploadBufferTaskDesc{
            .source = source,
            .destination = destinationResource,
            .finalState = ResourceStates::ShaderResource,
            .acceptedToken = &acceptedToken,
        }
    );
    ASSERT_TRUE(uploadTask.valid());
    // Declaration clears a stale caller token. It is populated only once the packet reaches queue submission.
    EXPECT_FALSE(acceptedToken.valid());
    ASSERT_TRUE(graph.taskAt(uploadTask.index).hasPayload);
    ASSERT_EQ(graph.taskAt(uploadTask.index).resourceUseCount, 1u);
    EXPECT_FALSE(graph.addUploadBufferTask(
        uploadTaskDesc,
        GpuUploadBufferTaskDesc{
            .source = source,
            .destination = destinationResource,
            .destinationOffsetBytes = sizeof(sourceWords),
        }
    ).valid());
    EXPECT_FALSE(graph.addUploadBufferTask(
        uploadTaskDesc,
        GpuUploadBufferTaskDesc{
            .source = source,
            .destination = destinationResource,
            .destinationOffsetBytes = 1u,
        }
    ).valid());
    const u8 unalignedByte = 0xabu;
    const GpuUploadBlobId unalignedSource = graph.copyUploadData(&unalignedByte, sizeof(unalignedByte));
    ASSERT_TRUE(unalignedSource.valid());
    EXPECT_FALSE(graph.addUploadBufferTask(
        uploadTaskDesc,
        GpuUploadBufferTaskDesc{
            .source = unalignedSource,
            .destination = destinationResource,
        }
    ).valid());
    // Automatic state tracking restores the backend descriptor state when a command list closes. A different
    // graph-visible final state would leave the next packet with an incorrect compiler seed, so reject it early.
    EXPECT_FALSE(graph.addUploadBufferTask(
        uploadTaskDesc,
        GpuUploadBufferTaskDesc{
            .source = source,
            .destination = keepInitialDestinationResource,
            .finalState = ResourceStates::ShaderResource,
        }
    ).valid());

    const u32 graphicsFamily = device.getQueueFamilyIndex(CommandQueue::Graphics);
    const u32 transferFamily = device.getQueueFamilyIndex(CommandQueue::Transfer);
    const bool dedicatedTransfer = device.getQueue(CommandQueue::Transfer)
        && transferFamily != Limit<u32>::s_Max
        && transferFamily != graphicsFamily
    ;
    GpuPhysicalQueueInfo queues[2u] = {
        GpuPhysicalQueueInfo{
            .id = BackendQueueId(device, CommandQueue::Graphics),
            .queueClass = CommandQueue::Graphics,
            .capabilities = static_cast<GpuQueueCapability::Mask>(
                static_cast<u8>(GpuQueueCapability::Graphics)
                | static_cast<u8>(GpuQueueCapability::Compute)
                | static_cast<u8>(GpuQueueCapability::Transfer)
            ),
            .familyIndex = graphicsFamily,
            .queueIndex = 0u,
            .dedicated = false,
        },
    };
    usize queueCount = 1u;
    if(dedicatedTransfer){
        queues[queueCount] = GpuPhysicalQueueInfo{
            .id = BackendQueueId(device, CommandQueue::Transfer),
            .queueClass = CommandQueue::Transfer,
            .capabilities = GpuQueueCapability::Transfer,
            .familyIndex = transferFamily,
            .queueIndex = 0u,
            .dedicated = true,
        };
        ++queueCount;
    }
    const GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = queueCount,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/built_in_upload_buffer_scratch"));
    const GpuTaskGraphCompiler compiler;
    ASSERT_TRUE(compiler.compile(graph, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskQueueAssignment* const assignment = assignments.find(uploadTask);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->queueClass, dedicatedTransfer ? CommandQueue::Transfer : CommandQueue::Graphics);
    const GpuSubmissionPacketId packet = compiledGraph.packetForTask(uploadTask);
    ASSERT_TRUE(packet.valid());

    const GpuNativePacketRecorder recorder(device);
    // Upload blobs deliberately have no command-IR encoding. A capture attempt exercises record failure followed by
    // transaction discard, which must clear an output token rather than leaving an unrelated accepted value alive.
    GpuRecordedGraph rejectedRecordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction rejectedTransaction(DescriptorBufferRoundTripTest::arena());
    rejectedTransaction.reset(compiledGraph);
    GpuCommandIrCapture rejectedCapture(DescriptorBufferRoundTripTest::arena());
    acceptedToken = staleToken;
    EXPECT_FALSE(recorder.recordPacket(
        graph,
        compiledGraph,
        GpuNativePacketRecordDesc{ .packet = packet },
        rejectedRecordedGraph,
        &rejectedCapture
    ));
    rejectedTransaction.discardUnaccepted(graph, compiledGraph);
    EXPECT_FALSE(acceptedToken.valid());

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        compiledGraph.allPacketRange(),
        nullptr,
        0u,
        recordedGraph
    ));
    ASSERT_NE(recordedGraph.packetFinalStateSeed(packet), nullptr);

    const GpuTaskGraphSubmitter submitter(device);
    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        compiledGraph.allPacketRange(),
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    const QueueSubmissionToken packetToken = transaction.packetToken(packet);
    ASSERT_TRUE(packetToken.valid());
    EXPECT_TRUE(acceptedToken.valid());
    EXPECT_EQ(acceptedToken.queue, packetToken.queue);
    EXPECT_EQ(acceptedToken.value, packetToken.value);
    ASSERT_TRUE(device.waitForIdle());

    const u32* const uploadedWords = static_cast<const u32*>(device.mapBuffer(destination.get(), CpuAccessMode::Read));
    ASSERT_NE(uploadedWords, nullptr);
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_ExpectedWords); ++wordIndex)
        EXPECT_EQ(uploadedWords[wordIndex], s_ExpectedWords[wordIndex]);
    device.unmapBuffer(destination.get());
}


// Independent immutable uploads are the first production opt-in packets for worker recording.  Use the real Vulkan
// Device to prove the packet recorder keeps per-packet state scratch and command pools isolated before serial graph
// submission consumes the recorded command lists in compiler order.
TEST_F(DescriptorBufferRoundTripTest, ReadyFrontierRecorderRecordsIndependentGraphOwnedUploadsOnWorkers){
    auto& device = DescriptorBufferRoundTripTest::device();
    static constexpr u32 s_FirstWords[] = {
        0x4ef8a219u,
        0x13c0ffeeu,
        0x7f4a7c15u,
        0x9e3779b9u,
    };
    static constexpr u32 s_SecondWords[] = {
        0xfeedfaceu,
        0x0badf00du,
        0x2c1b3a49u,
        0xd1cebeefu,
    };
    const auto createDestination = [&device]{
        return device.createBuffer(
            BufferDesc()
                .setByteSize(sizeof(s_FirstWords))
                .setInitialState(ResourceStates::Common)
                .setQueueSharing(ResourceQueueSharing::Exclusive)
                .setCpuAccess(CpuAccessMode::Read)
        );
    };
    auto firstDestination = createDestination();
    auto secondDestination = createDestination();
    ASSERT_NE(firstDestination.get(), nullptr);
    ASSERT_NE(secondDestination.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const auto importDestination = [&graph](const BufferHandle& buffer, const Name& identity, const AStringView label){
        return graph.importBuffer(
            buffer,
            GpuGraphResourceDesc{}
                .setIdentity(identity)
                .setMarkerLabel(label)
                .setType(GpuGraphResourceType::Buffer)
                .setInitialState(ResourceStates::Common)
        );
    };
    const GpuGraphResourceId firstDestinationResource = importDestination(
        firstDestination,
        Name("tests/descriptor_buffer/parallel_upload_first_destination"),
        "Parallel Upload First Destination"
    );
    const GpuGraphResourceId secondDestinationResource = importDestination(
        secondDestination,
        Name("tests/descriptor_buffer/parallel_upload_second_destination"),
        "Parallel Upload Second Destination"
    );
    const GpuUploadBlobId firstSource = graph.copyUploadData(s_FirstWords, sizeof(s_FirstWords), alignof(u32));
    const GpuUploadBlobId secondSource = graph.copyUploadData(s_SecondWords, sizeof(s_SecondWords), alignof(u32));
    ASSERT_TRUE(firstDestinationResource.valid());
    ASSERT_TRUE(secondDestinationResource.valid());
    ASSERT_TRUE(firstSource.valid());
    ASSERT_TRUE(secondSource.valid());

    GpuTaskSchedulingHint scheduling;
    scheduling.cost = GpuTaskCostHint::Medium;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    scheduling.allowParallelRecording = true;
    const auto addUpload = [&](const Name& identity,
        const AStringView label,
        const GpuUploadBlobId source,
        const GpuGraphResourceId destination,
        QueueSubmissionToken* const acceptedToken
    ){
        GpuTaskDesc desc;
        desc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setQueue(GpuQueueRequest{
                GpuQueueCapability::Transfer,
                GpuQueuePreference::Transfer,
                true,
                true,
            })
            .setScheduling(scheduling)
        ;
        return graph.addUploadBufferTask(
            desc,
            GpuUploadBufferTaskDesc{
                .source = source,
                .destination = destination,
                .finalState = ResourceStates::Common,
                .acceptedToken = acceptedToken,
            }
        );
    };
    QueueSubmissionToken firstAcceptedToken;
    QueueSubmissionToken secondAcceptedToken;
    const GpuTaskId firstUpload = addUpload(
        Name("tests/descriptor_buffer/parallel_upload_first"),
        "Parallel Upload First",
        firstSource,
        firstDestinationResource,
        &firstAcceptedToken
    );
    const GpuTaskId secondUpload = addUpload(
        Name("tests/descriptor_buffer/parallel_upload_second"),
        "Parallel Upload Second",
        secondSource,
        secondDestinationResource,
        &secondAcceptedToken
    );
    ASSERT_TRUE(firstUpload.valid());
    ASSERT_TRUE(secondUpload.valid());

    const GpuPhysicalQueueInfo graphicsQueue{
        .id = BackendQueueId(device, CommandQueue::Graphics),
        .queueClass = CommandQueue::Graphics,
        .capabilities = static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
            | static_cast<u8>(GpuQueueCapability::Transfer)
        ),
        .familyIndex = device.getQueueFamilyIndex(CommandQueue::Graphics),
        .queueIndex = 0u,
        .dedicated = false,
    };
    const GpuTaskGraphQueueTopology topology{
        .queues = &graphicsQueue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/parallel_upload_recording_scratch"));
    const GpuTaskGraphCompiler compiler;
    ASSERT_TRUE(compiler.compile(graph, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuSubmissionPacketId firstPacket = compiledGraph.packetForTask(firstUpload);
    const GpuSubmissionPacketId secondPacket = compiledGraph.packetForTask(secondUpload);
    ASSERT_TRUE(firstPacket.valid());
    ASSERT_TRUE(secondPacket.valid());
    EXPECT_EQ(compiledGraph.packet(firstPacket).recordingFrontier, 0u);
    EXPECT_EQ(compiledGraph.packet(secondPacket).recordingFrontier, 0u);

    Alloc::ThreadPool recordingWorkers(2u, CpuAffinity::Any);
    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacketRangeInReadyFrontiers(
        graph,
        compiledGraph,
        compiledGraph.allPacketRange(),
        nullptr,
        0u,
        recordedGraph,
        recordingWorkers
    ));
    ASSERT_NE(recordedGraph.find(firstPacket), nullptr);
    ASSERT_NE(recordedGraph.find(secondPacket), nullptr);
    ASSERT_NE(recordedGraph.packetFinalStateSeed(firstPacket), nullptr);
    ASSERT_NE(recordedGraph.packetFinalStateSeed(secondPacket), nullptr);

    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuTaskGraphSubmitter submitter(device);
    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        compiledGraph.allPacketRange(),
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    EXPECT_TRUE(firstAcceptedToken.valid());
    EXPECT_TRUE(secondAcceptedToken.valid());
    ASSERT_TRUE(device.waitForIdle());

    const u32* const firstWords = static_cast<const u32*>(device.mapBuffer(firstDestination.get(), CpuAccessMode::Read));
    const u32* const secondWords = static_cast<const u32*>(device.mapBuffer(secondDestination.get(), CpuAccessMode::Read));
    ASSERT_NE(firstWords, nullptr);
    ASSERT_NE(secondWords, nullptr);
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_FirstWords); ++wordIndex){
        EXPECT_EQ(firstWords[wordIndex], s_FirstWords[wordIndex]);
        EXPECT_EQ(secondWords[wordIndex], s_SecondWords[wordIndex]);
    }
    device.unmapBuffer(firstDestination.get());
    device.unmapBuffer(secondDestination.get());
}


TEST_F(DescriptorBufferRoundTripTest, BuiltInUploadTextureTaskRecordsGraphOwnedBlobAndFinalState){
    auto& device = DescriptorBufferRoundTripTest::device();
    u8 sourceBytes[4u * 4u * 4u] = {
        0x1u, 0x2u, 0x3u, 0xffu,  0x4u, 0x5u, 0x6u, 0xffu,
        0x7u, 0x8u, 0x9u, 0xffu,  0xau, 0xbu, 0xcu, 0xffu,
        0xdu, 0xeu, 0xfu, 0xffu,  0x10u, 0x11u, 0x12u, 0xffu,
        0x13u, 0x14u, 0x15u, 0xffu, 0x16u, 0x17u, 0x18u, 0xffu,
        0x19u, 0x1au, 0x1bu, 0xffu, 0x1cu, 0x1du, 0x1eu, 0xffu,
        0x1fu, 0x20u, 0x21u, 0xffu, 0x22u, 0x23u, 0x24u, 0xffu,
        0x25u, 0x26u, 0x27u, 0xffu, 0x28u, 0x29u, 0x2au, 0xffu,
        0x2bu, 0x2cu, 0x2du, 0xffu, 0x2eu, 0x2fu, 0x30u, 0xffu,
    };
    u8 expectedBytes[sizeof(sourceBytes)];
    NWB_MEMCPY(expectedBytes, sizeof(expectedBytes), sourceBytes, sizeof(sourceBytes));
    auto destination = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::RGBA8_UNORM)
            .setInUAV(true)
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::GraphicsAndTransfer)
    );
    ASSERT_NE(destination.get(), nullptr);
    auto keepInitialDestination = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::RGBA8_UNORM)
            .setInUAV(true)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    ASSERT_NE(keepInitialDestination.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId destinationResource = graph.importTexture(
        destination,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/built_in_upload_texture_destination"))
            .setMarkerLabel("Built-In Upload Texture Destination")
            .setType(GpuGraphResourceType::Texture)
    );
    ASSERT_TRUE(destinationResource.valid());
    const GpuGraphResourceId keepInitialDestinationResource = graph.importTexture(
        keepInitialDestination,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/built_in_upload_texture_keep_initial_destination"))
            .setMarkerLabel("Built-In Upload Texture Keep Initial Destination")
            .setType(GpuGraphResourceType::Texture)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(keepInitialDestinationResource.valid());
    const GpuUploadBlobId source = graph.copyUploadData(sourceBytes, sizeof(sourceBytes), alignof(u32));
    ASSERT_TRUE(source.valid());
    NWB_MEMSET(sourceBytes, 0, sizeof(sourceBytes));

    GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    GpuTaskDesc uploadTaskDesc;
    uploadTaskDesc
        .setIdentity(Name("tests/descriptor_buffer/built_in_upload_texture"))
        .setMarkerLabel("Built-In Texture Upload")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Transfer,
            GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setScheduling(scheduling)
    ;
    QueueSubmissionToken acceptedToken;
    const GpuTaskId uploadTask = graph.addUploadTextureTask(
        uploadTaskDesc,
        GpuUploadTextureTaskDesc{
            .source = source,
            .destination = destinationResource,
            .finalState = ResourceStates::ShaderResource,
            .acceptedToken = &acceptedToken,
        }
    );
    ASSERT_TRUE(uploadTask.valid());
    ASSERT_EQ(graph.taskAt(uploadTask.index).resourceUseCount, 1u);
    EXPECT_FALSE(graph.addUploadTextureTask(
        uploadTaskDesc,
        GpuUploadTextureTaskDesc{
            .source = source,
            .destination = destinationResource,
            .mipLevel = 1u,
        }
    ).valid());
    // `writeTexture` lowers row/depth pitches into VkBufferImageCopy's 32-bit texel fields. The helper must reject
    // an otherwise small 2D upload whose explicit depth pitch exceeds that native limit, rather than accepting a
    // packet whose recorder later emits no copy.
    EXPECT_FALSE(graph.addUploadTextureTask(
        uploadTaskDesc,
        GpuUploadTextureTaskDesc{
            .source = source,
            .destination = destinationResource,
            .rowPitch = 16u,
            .depthPitch = static_cast<usize>(Limit<u32>::s_Max + 1ull) * 16u,
        }
    ).valid());
    EXPECT_FALSE(graph.addUploadTextureTask(
        uploadTaskDesc,
        GpuUploadTextureTaskDesc{
            .source = source,
            .destination = keepInitialDestinationResource,
            .finalState = ResourceStates::ShaderResource,
        }
    ).valid());

    const GpuPhysicalQueueInfo queue{
        .id = BackendQueueId(device, CommandQueue::Graphics),
        .queueClass = CommandQueue::Graphics,
        .capabilities = static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
            | static_cast<u8>(GpuQueueCapability::Transfer)
        ),
        .familyIndex = device.getQueueFamilyIndex(CommandQueue::Graphics),
        .queueIndex = 0u,
        .dedicated = false,
    };
    const GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/built_in_upload_texture_scratch"));
    const GpuTaskGraphCompiler compiler;
    ASSERT_TRUE(compiler.compile(graph, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuSubmissionPacketId packet = compiledGraph.packetForTask(uploadTask);
    ASSERT_TRUE(packet.valid());

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacket(
        graph,
        compiledGraph,
        GpuNativePacketRecordDesc{ .packet = packet },
        recordedGraph
    ));
    const CommandListResourceStateHandoff* const finalState = recordedGraph.packetFinalStateSeed(packet);
    ASSERT_NE(finalState, nullptr);
    auto stateProbe = device.createCommandList();
    ASSERT_NE(stateProbe.get(), nullptr);
    stateProbe->open(finalState);
    EXPECT_EQ(stateProbe->getTextureSubresourceState(destination.get(), 0u, 0u), ResourceStates::ShaderResource);
    stateProbe->close();

    const GpuTaskGraphSubmitter submitter(device);
    ASSERT_TRUE(submitter.submitPacket(
        graph,
        compiledGraph,
        recordedGraph,
        packet,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    EXPECT_TRUE(acceptedToken.valid());
    EXPECT_EQ(acceptedToken.value, transaction.packetToken(packet).value);
    EXPECT_TRUE(device.waitForIdle());

    // The copied caller array was overwritten before late recording. Read the image back to prove native recording
    // resolved the graph-owned blob, rather than retaining a caller pointer or merely publishing a state handoff.
    StagingTextureHandle readback = device.createStagingTexture(destination->getDescription(), CpuAccessMode::Read);
    ASSERT_NE(readback.get(), nullptr);
    CommandListHandle readbackCommandList = device.createCommandList();
    ASSERT_NE(readbackCommandList.get(), nullptr);
    readbackCommandList->open(finalState);
    ASSERT_TRUE(readbackCommandList->hasCommandBuffer());
    readbackCommandList->copyTexture(readback.get(), TextureSlice{}, destination.get(), TextureSlice{});
    readbackCommandList->close();
    CommandList* const readbackLists[] = { readbackCommandList.get() };
    ASSERT_TRUE(device.executeCommandLists(
        readbackLists,
        LengthOf(readbackLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    ).valid());
    ASSERT_TRUE(device.waitForIdle());
    usize readbackRowPitch = 0u;
    const auto* const readbackBytes = static_cast<const u8*>(device.mapStagingTexture(
        readback.get(),
        TextureSlice{},
        CpuAccessMode::Read,
        &readbackRowPitch
    ));
    ASSERT_NE(readbackBytes, nullptr);
    ASSERT_GE(readbackRowPitch, 4u * sizeof(u32));
    for(u32 row = 0u; row < 4u; ++row){
        for(usize byte = 0u; byte < 4u * sizeof(u32); ++byte){
            EXPECT_EQ(
                readbackBytes[static_cast<usize>(row) * readbackRowPitch + byte],
                expectedBytes[static_cast<usize>(row) * 4u * sizeof(u32) + byte]
            );
        }
    }
    device.unmapStagingTexture(readback.get());
}


// The buffer primitive follows the same late-recording/lifecycle contract as texture copies, while its two exact
// regions are retained by the payload. The Graphics producer and consumer make a dedicated Transfer route prove
// an exclusive Graphics -> Transfer -> Graphics ownership handoff; single-queue hosts exercise fallback.
TEST_F(DescriptorBufferRoundTripTest, BuiltInCopyBufferTaskRecordsAndPublishesAcceptedToken){
    auto& device = DescriptorBufferRoundTripTest::device();
    static constexpr u32 s_SourceWords[] = {
        0x0347a2d1u,
        0x89abcdefu,
        0x5162f093u,
        0xc0ffee42u,
    };
    static constexpr u32 s_SecondSourceWords[] = {
        0x41f0a7c3u,
        0xdeadc0deu,
        0x0badf00du,
        0x76543210u,
    };
    const BufferDesc sourceDesc = BufferDesc()
        .setByteSize(sizeof(s_SourceWords))
        .setInitialState(ResourceStates::Common)
        .setQueueSharing(ResourceQueueSharing::Exclusive)
        .setCpuAccess(CpuAccessMode::Write)
    ;
    const BufferDesc destinationDesc = BufferDesc()
        .setByteSize(sizeof(s_SourceWords))
        .setInitialState(ResourceStates::Common)
        .setQueueSharing(ResourceQueueSharing::Exclusive)
        .setCpuAccess(CpuAccessMode::Read)
    ;
    auto source = device.createBuffer(sourceDesc);
    auto secondSource = device.createBuffer(sourceDesc);
    auto destination = device.createBuffer(destinationDesc);
    auto secondDestination = device.createBuffer(destinationDesc);
    ASSERT_NE(source.get(), nullptr);
    ASSERT_NE(secondSource.get(), nullptr);
    ASSERT_NE(destination.get(), nullptr);
    ASSERT_NE(secondDestination.get(), nullptr);

    u32* const sourceWords = static_cast<u32*>(device.mapBuffer(source.get(), CpuAccessMode::Write));
    ASSERT_NE(sourceWords, nullptr);
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_SourceWords); ++wordIndex)
        sourceWords[wordIndex] = s_SourceWords[wordIndex];
    device.unmapBuffer(source.get());
    u32* const secondSourceWords = static_cast<u32*>(device.mapBuffer(secondSource.get(), CpuAccessMode::Write));
    ASSERT_NE(secondSourceWords, nullptr);
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_SecondSourceWords); ++wordIndex)
        secondSourceWords[wordIndex] = s_SecondSourceWords[wordIndex];
    device.unmapBuffer(secondSource.get());

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId sourceResource = graph.importBuffer(
        source,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/built_in_copy_buffer_source"))
            .setMarkerLabel("Built-In Buffer Copy Source")
            .setType(GpuGraphResourceType::Buffer)
    );
    const GpuGraphResourceId destinationResource = graph.importBuffer(
        destination,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/built_in_copy_buffer_destination"))
            .setMarkerLabel("Built-In Buffer Copy Destination")
            .setType(GpuGraphResourceType::Buffer)
    );
    const GpuGraphResourceId secondSourceResource = graph.importBuffer(
        secondSource,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/built_in_copy_buffer_second_source"))
            .setMarkerLabel("Built-In Buffer Copy Second Source")
            .setType(GpuGraphResourceType::Buffer)
    );
    const GpuGraphResourceId secondDestinationResource = graph.importBuffer(
        secondDestination,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/built_in_copy_buffer_second_destination"))
            .setMarkerLabel("Built-In Buffer Copy Second Destination")
            .setType(GpuGraphResourceType::Buffer)
    );
    ASSERT_TRUE(sourceResource.valid());
    ASSERT_TRUE(destinationResource.valid());
    ASSERT_TRUE(secondSourceResource.valid());
    ASSERT_TRUE(secondDestinationResource.valid());

    GpuTaskSchedulingHint copyScheduling;
    copyScheduling.cost = GpuTaskCostHint::Medium;
    copyScheduling.forceSubmissionBoundary = true;
    copyScheduling.allowPacketMerge = false;
    const GpuTaskResourceUse producerUses[] = {
        GpuTaskResourceUse{
            .resource = sourceResource,
            .range = {},
            .requiredState = ResourceStates::CopySource,
            .access = GpuTaskResourceAccess::Write,
        },
        GpuTaskResourceUse{
            .resource = secondSourceResource,
            .range = {},
            .requiredState = ResourceStates::CopySource,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    GpuTaskDesc producerTaskDesc;
    producerTaskDesc
        .setIdentity(Name("tests/descriptor_buffer/built_in_copy_buffer_producer"))
        .setMarkerLabel("Built-In Buffer Producer")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Graphics,
            GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setScheduling(copyScheduling)
        .setResourceUses(producerUses, LengthOf(producerUses))
    ;
    bool producerRecorded = false;
    const GpuTaskId producerTask = graph.addTask<NativePacketPrefixTask>(
        producerTaskDesc,
        NativePacketPrefixTask::Payload{
            .buffer = source.get(),
            .expectedState = ResourceStates::CopySource,
            .recorded = &producerRecorded,
        }
    );
    ASSERT_TRUE(producerTask.valid());

    GpuTaskDesc copyTaskDesc;
    copyTaskDesc
        .setIdentity(Name("tests/descriptor_buffer/built_in_copy_buffer"))
        .setMarkerLabel("Built-In Buffer Copy")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Transfer,
            GpuQueuePreference::Transfer,
            true,
            true,
        })
        .setScheduling(copyScheduling)
        .setDependencies(&producerTask, 1u)
    ;
    const GpuCopyBufferTaskRegion copyRegions[] = {
        GpuCopyBufferTaskRegion{
            .source = sourceResource,
            .destination = destinationResource,
            .dataSizeBytes = sizeof(s_SourceWords),
        },
        GpuCopyBufferTaskRegion{
            .source = secondSourceResource,
            .destination = secondDestinationResource,
            .dataSizeBytes = sizeof(s_SecondSourceWords),
        },
    };
    QueueSubmissionToken acceptedToken;
    const GpuTaskId copyTask = graph.addCopyBufferTask(
        copyTaskDesc,
        GpuCopyBufferTaskDesc{
            .regions = copyRegions,
            .regionCount = LengthOf(copyRegions),
            .acceptedToken = &acceptedToken,
        }
    );
    ASSERT_TRUE(copyTask.valid());
    ASSERT_TRUE(graph.taskAt(copyTask.index).hasPayload);
    ASSERT_EQ(graph.taskAt(copyTask.index).resourceUseCount, 4u);

    const GpuTaskResourceUse consumerUses[] = {
        GpuTaskResourceUse{
            .resource = destinationResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = secondDestinationResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    GpuTaskDesc consumerTaskDesc;
    consumerTaskDesc
        .setIdentity(Name("tests/descriptor_buffer/built_in_copy_buffer_consumer"))
        .setMarkerLabel("Built-In Buffer Consumer")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Graphics,
            GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setScheduling(copyScheduling)
        .setDependencies(&copyTask, 1u)
        .setResourceUses(consumerUses, LengthOf(consumerUses))
    ;
    bool consumerRecorded = false;
    const GpuTaskId consumerTask = graph.addTask<NativePacketPrefixTask>(
        consumerTaskDesc,
        NativePacketPrefixTask::Payload{
            .buffer = destination.get(),
            .expectedState = ResourceStates::ShaderResource,
            .recorded = &consumerRecorded,
        }
    );
    ASSERT_TRUE(consumerTask.valid());

    const u32 graphicsFamily = device.getQueueFamilyIndex(CommandQueue::Graphics);
    const u32 transferFamily = device.getQueueFamilyIndex(CommandQueue::Transfer);
    const bool dedicatedTransfer = device.getQueue(CommandQueue::Transfer)
        && transferFamily != Limit<u32>::s_Max
        && transferFamily != graphicsFamily
    ;
    GpuPhysicalQueueInfo queues[2u] = {
        GpuPhysicalQueueInfo{
            .id = BackendQueueId(device, CommandQueue::Graphics),
            .queueClass = CommandQueue::Graphics,
            .capabilities = static_cast<GpuQueueCapability::Mask>(
                static_cast<u8>(GpuQueueCapability::Graphics)
                | static_cast<u8>(GpuQueueCapability::Compute)
                | static_cast<u8>(GpuQueueCapability::Transfer)
            ),
            .familyIndex = graphicsFamily,
            .queueIndex = 0u,
            .dedicated = false,
        },
    };
    usize queueCount = 1u;
    if(dedicatedTransfer){
        queues[queueCount] = GpuPhysicalQueueInfo{
            .id = BackendQueueId(device, CommandQueue::Transfer),
            .queueClass = CommandQueue::Transfer,
            .capabilities = GpuQueueCapability::Transfer,
            .familyIndex = transferFamily,
            .queueIndex = 0u,
            .dedicated = true,
        };
        ++queueCount;
    }
    const GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = queueCount,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/built_in_copy_buffer_scratch"));
    const GpuTaskGraphCompiler compiler;
    ASSERT_TRUE(compiler.compile(graph, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskQueueAssignment* const producerAssignment = assignments.find(producerTask);
    const GpuTaskQueueAssignment* const assignment = assignments.find(copyTask);
    const GpuTaskQueueAssignment* const consumerAssignment = assignments.find(consumerTask);
    ASSERT_NE(producerAssignment, nullptr);
    ASSERT_NE(assignment, nullptr);
    ASSERT_NE(consumerAssignment, nullptr);
    EXPECT_EQ(producerAssignment->queueClass, CommandQueue::Graphics);
    EXPECT_EQ(
        assignment->queueClass,
        dedicatedTransfer ? CommandQueue::Transfer : CommandQueue::Graphics
    );
    EXPECT_EQ(consumerAssignment->queueClass, CommandQueue::Graphics);
    const GpuSubmissionPacketId producerPacket = compiledGraph.packetForTask(producerTask);
    const GpuSubmissionPacketId packet = compiledGraph.packetForTask(copyTask);
    const GpuSubmissionPacketId consumerPacket = compiledGraph.packetForTask(consumerTask);
    ASSERT_TRUE(producerPacket.valid());
    ASSERT_TRUE(packet.valid());
    ASSERT_TRUE(consumerPacket.valid());
    EXPECT_EQ(compiledGraph.packetCount(), 3u);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    GpuCommandIrCapture commandIrCapture(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        compiledGraph.allPacketRange(),
        nullptr,
        0u,
        recordedGraph,
        nullptr,
        &commandIrCapture
    ));
    ASSERT_EQ(commandIrCapture.recordCount(), LengthOf(copyRegions));
    for(usize regionIndex = 0u; regionIndex < LengthOf(copyRegions); ++regionIndex){
        const GpuCommandIrBuiltinTaskRecord* const copyCapture = commandIrCapture.recordAt(regionIndex);
        ASSERT_NE(copyCapture, nullptr);
        EXPECT_EQ(copyCapture->opcode, GpuCommandIrOpcode::CopyBuffer);
        EXPECT_EQ(copyCapture->task, copyTask);
        EXPECT_EQ(copyCapture->packet, packet);
        EXPECT_EQ(copyCapture->queue, compiledGraph.packet(packet).queue);
        EXPECT_EQ(copyCapture->source, copyRegions[regionIndex].source);
        EXPECT_EQ(copyCapture->destination, copyRegions[regionIndex].destination);
        EXPECT_EQ(copyCapture->sourceOffsetBytes, copyRegions[regionIndex].sourceOffsetBytes);
        EXPECT_EQ(copyCapture->destinationOffsetBytes, copyRegions[regionIndex].destinationOffsetBytes);
        EXPECT_EQ(copyCapture->dataSizeBytes, copyRegions[regionIndex].dataSizeBytes);
    }
    const CommandListResourceStateHandoff* const finalState = recordedGraph.packetFinalStateSeed(packet);
    ASSERT_NE(finalState, nullptr);
    EXPECT_TRUE(producerRecorded);
    EXPECT_TRUE(consumerRecorded);

    const GpuTaskGraphSubmitter submitter(device);
    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        compiledGraph.allPacketRange(),
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    const QueueSubmissionToken packetToken = transaction.packetToken(packet);
    ASSERT_TRUE(packetToken.valid());
    EXPECT_TRUE(acceptedToken.valid());
    EXPECT_EQ(acceptedToken.queue, packetToken.queue);
    EXPECT_EQ(acceptedToken.value, packetToken.value);
    ASSERT_TRUE(device.waitForIdle());

    const u32* const copiedWords = static_cast<const u32*>(device.mapBuffer(destination.get(), CpuAccessMode::Read));
    ASSERT_NE(copiedWords, nullptr);
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_SourceWords); ++wordIndex)
        EXPECT_EQ(copiedWords[wordIndex], s_SourceWords[wordIndex]);
    device.unmapBuffer(destination.get());
    const u32* const secondCopiedWords = static_cast<const u32*>(
        device.mapBuffer(secondDestination.get(), CpuAccessMode::Read)
    );
    ASSERT_NE(secondCopiedWords, nullptr);
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_SecondSourceWords); ++wordIndex)
        EXPECT_EQ(secondCopiedWords[wordIndex], s_SecondSourceWords[wordIndex]);
    device.unmapBuffer(secondDestination.get());
}


// The optional IR lowerer selects one packet from a full primitive capture only after graph-aware preflight. A
// malformed later command in that packet must leave the earlier valid copy unrecorded; the success paths prove both
// the ordinary Core::CommandList lowerer and the explicitly pre-stated direct-Vulkan CopyBuffer prototype.
TEST_F(DescriptorBufferRoundTripTest, CommandIrPacketReplayPreflightsThenLowersCopyBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();
    static constexpr u32 s_SourceWords[] = {
        0x7143a9d2u,
        0xcafebabeu,
        0x0badf00du,
        0xdecafbadU,
    };
    static constexpr u32 s_Sentinel = 0xa5a55a5au;
    const BufferDesc sourceDesc = BufferDesc()
        .setByteSize(sizeof(s_SourceWords))
        .setInitialState(ResourceStates::Common)
        .setCpuAccess(CpuAccessMode::Write)
    ;
    const BufferDesc destinationDesc = BufferDesc()
        .setByteSize(sizeof(s_SourceWords))
        .setInitialState(ResourceStates::Common)
        .setCpuAccess(CpuAccessMode::Read)
    ;
    auto source = device.createBuffer(sourceDesc);
    auto destination = device.createBuffer(destinationDesc);
    auto secondDestination = device.createBuffer(destinationDesc);
    ASSERT_NE(source.get(), nullptr);
    ASSERT_NE(destination.get(), nullptr);
    ASSERT_NE(secondDestination.get(), nullptr);
    u32* const sourceWords = static_cast<u32*>(device.mapBuffer(source.get(), CpuAccessMode::Write));
    ASSERT_NE(sourceWords, nullptr);
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_SourceWords); ++wordIndex)
        sourceWords[wordIndex] = s_SourceWords[wordIndex];
    device.unmapBuffer(source.get());

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId sourceResource = graph.importBuffer(
        source,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/command_ir_replay_source"))
            .setMarkerLabel("Command IR Replay Source")
            .setType(GpuGraphResourceType::Buffer)
    );
    const GpuGraphResourceId destinationResource = graph.importBuffer(
        destination,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/command_ir_replay_destination"))
            .setMarkerLabel("Command IR Replay Destination")
            .setType(GpuGraphResourceType::Buffer)
    );
    const GpuGraphResourceId secondDestinationResource = graph.importBuffer(
        secondDestination,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/command_ir_replay_second_destination"))
            .setMarkerLabel("Command IR Replay Second Destination")
            .setType(GpuGraphResourceType::Buffer)
    );
    ASSERT_TRUE(sourceResource.valid());
    ASSERT_TRUE(destinationResource.valid());
    ASSERT_TRUE(secondDestinationResource.valid());

    GpuTaskDesc copyDesc;
    copyDesc
        .setIdentity(Name("tests/descriptor_buffer/command_ir_replay_copy"))
        .setMarkerLabel("Command IR Replay Copy")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Transfer,
            GpuQueuePreference::Transfer,
            true,
            true,
        })
    ;
    const GpuCopyBufferTaskRegion copyRegion{
        .source = sourceResource,
        .destination = destinationResource,
        .dataSizeBytes = sizeof(s_SourceWords),
    };
    const GpuTaskId copyTask = graph.addCopyBufferTask(
        copyDesc,
        GpuCopyBufferTaskDesc{
            .regions = &copyRegion,
            .regionCount = 1u,
        }
    );
    ASSERT_TRUE(copyTask.valid());
    const GpuTaskId secondDependencies[] = { copyTask };
    GpuTaskDesc secondCopyDesc = copyDesc;
    secondCopyDesc
        .setIdentity(Name("tests/descriptor_buffer/command_ir_replay_copy_second"))
        .setMarkerLabel("Command IR Replay Copy Second")
        .setDependencies(secondDependencies, LengthOf(secondDependencies))
    ;
    const GpuCopyBufferTaskRegion secondCopyRegion{
        .source = sourceResource,
        .destination = secondDestinationResource,
        .dataSizeBytes = sizeof(s_SourceWords),
    };
    const GpuTaskId secondCopyTask = graph.addCopyBufferTask(
        secondCopyDesc,
        GpuCopyBufferTaskDesc{
            .regions = &secondCopyRegion,
            .regionCount = 1u,
        }
    );
    ASSERT_TRUE(secondCopyTask.valid());

    const GpuPhysicalQueueInfo queue{
        .id = BackendQueueId(device, CommandQueue::Graphics),
        .queueClass = CommandQueue::Graphics,
        .capabilities = static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
            | static_cast<u8>(GpuQueueCapability::Transfer)
        ),
        .familyIndex = device.getQueueFamilyIndex(CommandQueue::Graphics),
        .queueIndex = 0u,
        .dedicated = false,
    };
    const GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/command_ir_replay_scratch"));
    const GpuTaskGraphCompiler compiler;
    ASSERT_TRUE(compiler.compile(graph, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuSubmissionPacketId packet = compiledGraph.packetForTask(copyTask);
    const GpuSubmissionPacketId secondPacket = compiledGraph.packetForTask(secondCopyTask);
    ASSERT_TRUE(packet.valid());
    ASSERT_TRUE(secondPacket.valid());
    ASSERT_NE(secondPacket, packet);
    ASSERT_EQ(compiledGraph.packet(packet).queue, queue.id);
    ASSERT_EQ(compiledGraph.packet(secondPacket).queue, queue.id);

    auto clearDestination = device.createCommandList();
    ASSERT_NE(clearDestination.get(), nullptr);
    clearDestination->open();
    clearDestination->clearBufferUInt(destination.get(), s_Sentinel);
    clearDestination->clearBufferUInt(secondDestination.get(), s_Sentinel);
    clearDestination->close();
    ASSERT_TRUE(clearDestination->hasCommandBuffer());
    CommandList* const clearCommandLists[] = { clearDestination.get() };
    bool clearSubmitted = false;
    EXPECT_GT(device.executeCommandLists(
        clearCommandLists,
        LengthOf(clearCommandLists),
        CommandQueue::Graphics,
        &clearSubmitted
    ), 0u);
    ASSERT_TRUE(clearSubmitted);
    ASSERT_TRUE(device.waitForIdle());

    GpuCommandIrCapture malformedCapture(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(malformedCapture.captureCopyBuffer(
        copyTask,
        packet,
        queue.id,
        sourceResource,
        0u,
        destinationResource,
        0u,
        sizeof(s_SourceWords)
    ));
    ASSERT_TRUE(malformedCapture.captureCopyBuffer(
        copyTask,
        packet,
        queue.id,
        sourceResource,
        0u,
        destinationResource,
        sizeof(s_SourceWords) - sizeof(u32),
        sizeof(s_SourceWords)
    ));
    auto rejectedReplay = device.createCommandList();
    ASSERT_NE(rejectedReplay.get(), nullptr);
    rejectedReplay->open();
    ASSERT_TRUE(rejectedReplay->isRecording());
    const GpuCommandIrReplayResult rejectedResult = ReplayGpuCommandIrPacket(
        malformedCapture.commandBytes(),
        graph,
        compiledGraph,
        packet,
        *rejectedReplay
    );
    EXPECT_EQ(rejectedResult.error, GpuCommandIrReplayError::InvalidBufferCopy);
    EXPECT_EQ(rejectedResult.recordIndex, 1u);
    EXPECT_TRUE(rejectedResult.streamValidation.valid());
    rejectedReplay->close();
    EXPECT_FALSE(rejectedReplay->isRecording());
    CommandList* const rejectedCommandLists[] = { rejectedReplay.get() };
    bool rejectedSubmitted = false;
    EXPECT_GT(device.executeCommandLists(
        rejectedCommandLists,
        LengthOf(rejectedCommandLists),
        CommandQueue::Graphics,
        &rejectedSubmitted
    ), 0u);
    ASSERT_TRUE(rejectedSubmitted);
    ASSERT_TRUE(device.waitForIdle());
    const u32* const untouchedWords = static_cast<const u32*>(device.mapBuffer(destination.get(), CpuAccessMode::Read));
    ASSERT_NE(untouchedWords, nullptr);
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_SourceWords); ++wordIndex)
        EXPECT_EQ(untouchedWords[wordIndex], s_Sentinel);
    device.unmapBuffer(destination.get());

    // The normal recorder produces one capture artifact for its complete packet range. Replay must select this
    // first packet from the two-packet stream, never emit the second packet's body, and retain all ordinary packet
    // state/barrier ownership in the graph recorder.
    GpuRecordedGraph capturedGraph(DescriptorBufferRoundTripTest::arena());
    GpuCommandIrCapture capture(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        compiledGraph.allPacketRange(),
        nullptr,
        0u,
        capturedGraph,
        nullptr,
        &capture
    ));
    ASSERT_EQ(capture.recordCount(), 2u);
    const GpuCommandIrBuiltinTaskRecord* const firstCapture = capture.recordAt(0u);
    const GpuCommandIrBuiltinTaskRecord* const secondCapture = capture.recordAt(1u);
    ASSERT_NE(firstCapture, nullptr);
    ASSERT_NE(secondCapture, nullptr);
    EXPECT_EQ(firstCapture->task, copyTask);
    EXPECT_EQ(firstCapture->packet, packet);
    EXPECT_EQ(secondCapture->task, secondCopyTask);
    EXPECT_EQ(secondCapture->packet, secondPacket);
    auto unopenedReplay = device.createCommandList();
    ASSERT_NE(unopenedReplay.get(), nullptr);
    const GpuCommandIrReplayResult unopenedResult = ReplayGpuCommandIrPacket(
        capture.commandBytes(),
        graph,
        compiledGraph,
        packet,
        *unopenedReplay
    );
    EXPECT_EQ(unopenedResult.error, GpuCommandIrReplayError::CommandListNotRecording);
    EXPECT_TRUE(unopenedResult.streamValidation.valid());

    auto replay = device.createCommandList();
    ASSERT_NE(replay.get(), nullptr);
    replay->open();
    ASSERT_TRUE(replay->isRecording());
    const GpuCommandIrReplayResult replayResult = ReplayGpuCommandIrPacket(
        capture.commandBytes(),
        graph,
        compiledGraph,
        packet,
        *replay
    );
    EXPECT_TRUE(replayResult.valid());
    EXPECT_TRUE(replayResult.streamValidation.valid());
    replay->close();
    EXPECT_FALSE(replay->isRecording());
    CommandList* const replayCommandLists[] = { replay.get() };
    bool replaySubmitted = false;
    EXPECT_GT(device.executeCommandLists(
        replayCommandLists,
        LengthOf(replayCommandLists),
        CommandQueue::Graphics,
        &replaySubmitted
    ), 0u);
    ASSERT_TRUE(replaySubmitted);
    ASSERT_TRUE(device.waitForIdle());

    const u32* const replayedWords = static_cast<const u32*>(device.mapBuffer(destination.get(), CpuAccessMode::Read));
    ASSERT_NE(replayedWords, nullptr);
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_SourceWords); ++wordIndex)
        EXPECT_EQ(replayedWords[wordIndex], s_SourceWords[wordIndex]);
    device.unmapBuffer(destination.get());

    auto resetDirectDestination = device.createCommandList();
    ASSERT_NE(resetDirectDestination.get(), nullptr);
    resetDirectDestination->open();
    resetDirectDestination->clearBufferUInt(destination.get(), s_Sentinel);
    resetDirectDestination->close();
    CommandList* const resetDirectCommandLists[] = { resetDirectDestination.get() };
    bool resetDirectSubmitted = false;
    EXPECT_GT(device.executeCommandLists(
        resetDirectCommandLists,
        LengthOf(resetDirectCommandLists),
        CommandQueue::Graphics,
        &resetDirectSubmitted
    ), 0u);
    ASSERT_TRUE(resetDirectSubmitted);
    ASSERT_TRUE(device.waitForIdle());

    // The direct Vulkan prototype must reject an unsupported later opcode before it records the first valid copy.
    // This synthetic trace passes current graph/resource preflight (a clear can use the copy task's whole-buffer
    // CopyDest declaration), but it is intentionally outside the current CopyBuffer-only direct-lowering contract.
    GpuCommandIrCapture unsupportedDirectCapture(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(unsupportedDirectCapture.captureCopyBuffer(
        copyTask,
        packet,
        queue.id,
        sourceResource,
        0u,
        destinationResource,
        0u,
        sizeof(s_SourceWords)
    ));
    ASSERT_TRUE(unsupportedDirectCapture.captureClearBuffer(
        copyTask,
        packet,
        queue.id,
        destinationResource,
        s_Sentinel
    ));
    auto unsupportedDirectReplay = device.createCommandList();
    ASSERT_NE(unsupportedDirectReplay.get(), nullptr);
    unsupportedDirectReplay->open();
    ASSERT_TRUE(unsupportedDirectReplay->isRecording());
    unsupportedDirectReplay->setBufferState(source.get(), ResourceStates::CopySource);
    unsupportedDirectReplay->setBufferState(destination.get(), ResourceStates::CopyDest);
    unsupportedDirectReplay->commitBarriers();
    const GpuCommandIrReplayResult unsupportedDirectResult = ReplayGpuCommandIrPacketDirectVulkan(
        unsupportedDirectCapture.commandBytes(),
        graph,
        compiledGraph,
        packet,
        *unsupportedDirectReplay
    );
    EXPECT_EQ(unsupportedDirectResult.error, GpuCommandIrReplayError::UnsupportedDirectVulkanOpcode);
    EXPECT_EQ(unsupportedDirectResult.recordIndex, 1u);
    EXPECT_TRUE(unsupportedDirectResult.streamValidation.valid());
    unsupportedDirectReplay->close();
    CommandList* const unsupportedDirectCommandLists[] = { unsupportedDirectReplay.get() };
    bool unsupportedDirectSubmitted = false;
    EXPECT_GT(device.executeCommandLists(
        unsupportedDirectCommandLists,
        LengthOf(unsupportedDirectCommandLists),
        CommandQueue::Graphics,
        &unsupportedDirectSubmitted
    ), 0u);
    ASSERT_TRUE(unsupportedDirectSubmitted);
    ASSERT_TRUE(device.waitForIdle());
    const u32* const stillSentinelWords = static_cast<const u32*>(device.mapBuffer(destination.get(), CpuAccessMode::Read));
    ASSERT_NE(stillSentinelWords, nullptr);
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_SourceWords); ++wordIndex)
        EXPECT_EQ(stillSentinelWords[wordIndex], s_Sentinel);
    device.unmapBuffer(destination.get());

    // Model the graph recorder's already-established packet state, then lower only the selected CopyBuffer body
    // directly to Vulkan. The direct lowerer must not create implicit state transitions of its own.
    auto directVulkanReplay = device.createCommandList();
    ASSERT_NE(directVulkanReplay.get(), nullptr);
    directVulkanReplay->open();
    ASSERT_TRUE(directVulkanReplay->isRecording());
    directVulkanReplay->setBufferState(source.get(), ResourceStates::CopySource);
    directVulkanReplay->setBufferState(destination.get(), ResourceStates::CopyDest);
    directVulkanReplay->commitBarriers();
    const GpuCommandIrReplayResult directVulkanResult = ReplayGpuCommandIrPacketDirectVulkan(
        capture.commandBytes(),
        graph,
        compiledGraph,
        packet,
        *directVulkanReplay
    );
    EXPECT_TRUE(directVulkanResult.valid());
    EXPECT_TRUE(directVulkanResult.streamValidation.valid());
    directVulkanReplay->close();
    CommandList* const directVulkanCommandLists[] = { directVulkanReplay.get() };
    bool directVulkanSubmitted = false;
    EXPECT_GT(device.executeCommandLists(
        directVulkanCommandLists,
        LengthOf(directVulkanCommandLists),
        CommandQueue::Graphics,
        &directVulkanSubmitted
    ), 0u);
    ASSERT_TRUE(directVulkanSubmitted);
    ASSERT_TRUE(device.waitForIdle());
    const u32* const directVulkanWords = static_cast<const u32*>(device.mapBuffer(destination.get(), CpuAccessMode::Read));
    ASSERT_NE(directVulkanWords, nullptr);
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_SourceWords); ++wordIndex)
        EXPECT_EQ(directVulkanWords[wordIndex], s_SourceWords[wordIndex]);
    device.unmapBuffer(destination.get());

    const u32* const untouchedSecondWords = static_cast<const u32*>(
        device.mapBuffer(secondDestination.get(), CpuAccessMode::Read)
    );
    ASSERT_NE(untouchedSecondWords, nullptr);
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_SourceWords); ++wordIndex)
        EXPECT_EQ(untouchedSecondWords[wordIndex], s_Sentinel);
    device.unmapBuffer(secondDestination.get());
}


// Clear helpers are deliberately graph-native primitives: their CopyDest declarations remain authoritative while an
// explicitly supplied Phase 11 capture receives compact resource-ID records. The normal recorder call sites pass
// no capture object and continue directly to native command lists.
TEST_F(DescriptorBufferRoundTripTest, BuiltInClearTasksRecordAndCapture){
    auto& device = DescriptorBufferRoundTripTest::device();
    const BufferDesc clearBufferDesc = BufferDesc()
        .setByteSize(sizeof(u32) * 4u)
        .setInitialState(ResourceStates::Common)
        .setQueueSharing(ResourceQueueSharing::GraphicsAndTransfer)
        .setCpuAccess(CpuAccessMode::Read)
    ;
    auto buffer = device.createBuffer(clearBufferDesc);
    ASSERT_NE(buffer.get(), nullptr);

    const TextureDesc clearTextureDesc = TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setFormat(Format::RGBA8_UINT)
        .setInitialState(ResourceStates::Common)
        .setQueueSharing(ResourceQueueSharing::GraphicsAndTransfer)
    ;
    auto texture = device.createTexture(clearTextureDesc);
    ASSERT_NE(texture.get(), nullptr);

    const TextureDesc clearDepthTextureDesc = TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setFormat(Format::D24S8)
        .setInRenderTarget(true)
        .setInitialState(ResourceStates::Common)
        .setQueueSharing(ResourceQueueSharing::GraphicsAndTransfer)
    ;
    auto depthTexture = device.createTexture(clearDepthTextureDesc);
    ASSERT_NE(depthTexture.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId bufferResource = graph.importBuffer(
        buffer,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/built_in_clear_buffer"))
            .setMarkerLabel("Built-In Clear Buffer")
            .setType(GpuGraphResourceType::Buffer)
    );
    const GpuGraphResourceId textureResource = graph.importTexture(
        texture,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/built_in_clear_texture"))
            .setMarkerLabel("Built-In Clear Texture")
            .setType(GpuGraphResourceType::Texture)
    );
    const GpuGraphResourceId depthTextureResource = graph.importTexture(
        depthTexture,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/built_in_clear_depth_texture"))
            .setMarkerLabel("Built-In Clear Depth Texture")
            .setType(GpuGraphResourceType::Texture)
    );
    ASSERT_TRUE(bufferResource.valid());
    ASSERT_TRUE(textureResource.valid());
    ASSERT_TRUE(depthTextureResource.valid());

    GpuTaskSchedulingHint clearScheduling;
    clearScheduling.cost = GpuTaskCostHint::Small;
    clearScheduling.forceSubmissionBoundary = true;
    clearScheduling.allowPacketMerge = false;
    const GpuQueueRequest transferQueue{
        GpuQueueCapability::Transfer,
        GpuQueuePreference::Transfer,
        true,
        true,
    };
    GpuTaskDesc clearBufferTaskDesc;
    clearBufferTaskDesc
        .setIdentity(Name("tests/descriptor_buffer/built_in_clear_buffer_task"))
        .setMarkerLabel("Built-In Clear Buffer Task")
        .setQueue(transferQueue)
        .setScheduling(clearScheduling)
    ;
    GpuTaskDesc clearTextureTaskDesc;
    clearTextureTaskDesc
        .setIdentity(Name("tests/descriptor_buffer/built_in_clear_texture_task"))
        .setMarkerLabel("Built-In Clear Texture Task")
        .setQueue(transferQueue)
        .setScheduling(clearScheduling)
    ;
    GpuTaskDesc clearDepthTextureTaskDesc;
    clearDepthTextureTaskDesc
        .setIdentity(Name("tests/descriptor_buffer/built_in_clear_depth_texture_task"))
        .setMarkerLabel("Built-In Clear Depth Texture Task")
        .setQueue(transferQueue)
        .setScheduling(clearScheduling)
    ;
    QueueSubmissionToken clearBufferAcceptedToken;
    QueueSubmissionToken clearTextureAcceptedToken;
    QueueSubmissionToken clearDepthTextureAcceptedToken;
    const GpuTaskId clearBufferTask = graph.addClearBufferTask(
        clearBufferTaskDesc,
        GpuClearBufferTaskDesc{
            .destination = bufferResource,
            .clearValue = 0xdecafbadU,
            .acceptedToken = &clearBufferAcceptedToken,
        }
    );
    const GpuTaskId clearTextureTask = graph.addClearTextureTask(
        clearTextureTaskDesc,
        GpuClearTextureTaskDesc{
            .destination = textureResource,
            .subresources = TextureSubresourceSet(0u, 1u, 0u, 1u),
            .valueType = GpuClearTextureTaskValueType::UInt,
            .uintValue = UIntColor(0x10203040u, 0x50607080u, 0x90a0b0c0u, 0xd0e0f000u),
            .acceptedToken = &clearTextureAcceptedToken,
        }
    );
    const GpuTaskId clearDepthTextureTask = graph.addClearTextureTask(
        clearDepthTextureTaskDesc,
        GpuClearTextureTaskDesc{
            .destination = depthTextureResource,
            .subresources = TextureSubresourceSet(0u, 1u, 0u, 1u),
            .valueType = GpuClearTextureTaskValueType::DepthStencil,
            .depthValue = 0.25f,
            .stencilValue = 0x7fu,
            .clearDepth = true,
            .clearStencil = true,
            .acceptedToken = &clearDepthTextureAcceptedToken,
        }
    );
    ASSERT_TRUE(clearBufferTask.valid());
    ASSERT_TRUE(clearTextureTask.valid());
    ASSERT_TRUE(clearDepthTextureTask.valid());
    ASSERT_EQ(graph.taskAt(clearBufferTask.index).resourceUseCount, 1u);
    ASSERT_EQ(graph.taskAt(clearTextureTask.index).resourceUseCount, 1u);
    ASSERT_EQ(graph.taskAt(clearDepthTextureTask.index).resourceUseCount, 1u);
    EXPECT_EQ(
        graph.taskAt(clearBufferTask.index).resourceUses[0u].requiredState,
        ResourceStates::CopyDest
    );
    EXPECT_EQ(
        graph.taskAt(clearTextureTask.index).resourceUses[0u].requiredState,
        ResourceStates::CopyDest
    );
    EXPECT_EQ(
        graph.taskAt(clearDepthTextureTask.index).resourceUses[0u].requiredState,
        ResourceStates::CopyDest
    );
    // Typed task creation rejects value types that native Vulkan clear commands cannot lower for the target image.
    EXPECT_FALSE(graph.addClearTextureTask(
        clearTextureTaskDesc,
        GpuClearTextureTaskDesc{
            .destination = textureResource,
            .valueType = GpuClearTextureTaskValueType::Float,
        }
    ).valid());
    EXPECT_FALSE(graph.addClearTextureTask(
        clearTextureTaskDesc,
        GpuClearTextureTaskDesc{
            .destination = depthTextureResource,
            .valueType = GpuClearTextureTaskValueType::UInt,
        }
    ).valid());

    const u32 graphicsFamily = device.getQueueFamilyIndex(CommandQueue::Graphics);
    const u32 transferFamily = device.getQueueFamilyIndex(CommandQueue::Transfer);
    const bool dedicatedTransfer = device.getQueue(CommandQueue::Transfer)
        && transferFamily != Limit<u32>::s_Max
        && transferFamily != graphicsFamily
    ;
    GpuPhysicalQueueInfo queues[2u] = {
        GpuPhysicalQueueInfo{
            .id = BackendQueueId(device, CommandQueue::Graphics),
            .queueClass = CommandQueue::Graphics,
            .capabilities = static_cast<GpuQueueCapability::Mask>(
                static_cast<u8>(GpuQueueCapability::Graphics)
                | static_cast<u8>(GpuQueueCapability::Compute)
                | static_cast<u8>(GpuQueueCapability::Transfer)
            ),
            .familyIndex = graphicsFamily,
            .queueIndex = 0u,
            .dedicated = false,
        },
    };
    usize queueCount = 1u;
    if(dedicatedTransfer){
        queues[queueCount] = GpuPhysicalQueueInfo{
            .id = BackendQueueId(device, CommandQueue::Transfer),
            .queueClass = CommandQueue::Transfer,
            .capabilities = GpuQueueCapability::Transfer,
            .familyIndex = transferFamily,
            .queueIndex = 0u,
            .dedicated = true,
        };
        ++queueCount;
    }
    const GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = queueCount,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/built_in_clear_scratch"));
    const GpuTaskGraphCompiler compiler;
    ASSERT_TRUE(compiler.compile(graph, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuSubmissionPacketId clearBufferPacket = compiledGraph.packetForTask(clearBufferTask);
    const GpuSubmissionPacketId clearTexturePacket = compiledGraph.packetForTask(clearTextureTask);
    const GpuSubmissionPacketId clearDepthTexturePacket = compiledGraph.packetForTask(clearDepthTextureTask);
    ASSERT_TRUE(clearBufferPacket.valid());
    ASSERT_TRUE(clearTexturePacket.valid());
    ASSERT_TRUE(clearDepthTexturePacket.valid());

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    GpuCommandIrCapture commandIrCapture(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        compiledGraph.allPacketRange(),
        nullptr,
        0u,
        recordedGraph,
        nullptr,
        &commandIrCapture
    ));
    ASSERT_EQ(commandIrCapture.recordCount(), 3u);
    const GpuCommandIrBuiltinTaskRecord* const bufferCapture = commandIrCapture.recordAt(0u);
    const GpuCommandIrBuiltinTaskRecord* const textureCapture = commandIrCapture.recordAt(1u);
    const GpuCommandIrBuiltinTaskRecord* const depthTextureCapture = commandIrCapture.recordAt(2u);
    ASSERT_NE(bufferCapture, nullptr);
    ASSERT_NE(textureCapture, nullptr);
    ASSERT_NE(depthTextureCapture, nullptr);
    EXPECT_EQ(bufferCapture->opcode, GpuCommandIrOpcode::ClearBuffer);
    EXPECT_EQ(bufferCapture->task, clearBufferTask);
    EXPECT_EQ(bufferCapture->packet, clearBufferPacket);
    EXPECT_EQ(bufferCapture->destination, bufferResource);
    EXPECT_EQ(bufferCapture->uintClearValue, UIntColor(0xdecafbadU));
    EXPECT_EQ(textureCapture->opcode, GpuCommandIrOpcode::ClearTexture);
    EXPECT_EQ(textureCapture->task, clearTextureTask);
    EXPECT_EQ(textureCapture->packet, clearTexturePacket);
    EXPECT_EQ(textureCapture->destination, textureResource);
    EXPECT_EQ(textureCapture->clearTextureValueType, GpuClearTextureTaskValueType::UInt);
    EXPECT_EQ(textureCapture->uintClearValue, UIntColor(0x10203040u, 0x50607080u, 0x90a0b0c0u, 0xd0e0f000u));
    EXPECT_EQ(depthTextureCapture->opcode, GpuCommandIrOpcode::ClearTexture);
    EXPECT_EQ(depthTextureCapture->task, clearDepthTextureTask);
    EXPECT_EQ(depthTextureCapture->packet, clearDepthTexturePacket);
    EXPECT_EQ(depthTextureCapture->destination, depthTextureResource);
    EXPECT_EQ(depthTextureCapture->clearTextureValueType, GpuClearTextureTaskValueType::DepthStencil);
    EXPECT_EQ(depthTextureCapture->depthClearValue, 0.25f);
    EXPECT_EQ(depthTextureCapture->stencilClearValue, 0x7fu);
    EXPECT_TRUE(depthTextureCapture->clearDepth);
    EXPECT_TRUE(depthTextureCapture->clearStencil);

    const GpuTaskGraphSubmitter submitter(device);
    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        compiledGraph.allPacketRange(),
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    EXPECT_TRUE(clearBufferAcceptedToken.valid());
    EXPECT_TRUE(clearTextureAcceptedToken.valid());
    EXPECT_TRUE(clearDepthTextureAcceptedToken.valid());
    ASSERT_TRUE(device.waitForIdle());

    const u32* const clearedWords = static_cast<const u32*>(device.mapBuffer(buffer.get(), CpuAccessMode::Read));
    ASSERT_NE(clearedWords, nullptr);
    for(usize wordIndex = 0u; wordIndex < 4u; ++wordIndex)
        EXPECT_EQ(clearedWords[wordIndex], 0xdecafbadU);
    device.unmapBuffer(buffer.get());
}


TEST_F(DescriptorBufferRoundTripTest, CommandIrCaptureRollsBackARejectedPacketBeforeRetry){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto buffer = device.createBuffer(
        BufferDesc()
            .setByteSize(sizeof(u32))
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::GraphicsAndTransfer)
    );
    ASSERT_NE(buffer.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId bufferResource = graph.importBuffer(
        buffer,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/command_ir_rollback_buffer"))
            .setMarkerLabel("Command IR Rollback Buffer")
            .setType(GpuGraphResourceType::Buffer)
    );
    ASSERT_TRUE(bufferResource.valid());

    const GpuQueueRequest transferQueue{
        GpuQueueCapability::Transfer,
        GpuQueuePreference::Transfer,
        true,
        true,
    };
    GpuTaskDesc clearDesc;
    clearDesc
        .setIdentity(Name("tests/descriptor_buffer/command_ir_rollback_clear"))
        .setMarkerLabel("Command IR Rollback Clear")
        .setQueue(transferQueue)
    ;
    const GpuTaskId clearTask = graph.addClearBufferTask(
        clearDesc,
        GpuClearBufferTaskDesc{
            .destination = bufferResource,
            .clearValue = 0x8badf00dU,
        }
    );
    ASSERT_TRUE(clearTask.valid());

    bool shouldRecord = false;
    bool retryTaskAttempted = false;
    GpuTaskSchedulingHint retryScheduling;
    retryScheduling.mergeWithPrevious = true;
    GpuTaskDesc retryDesc;
    retryDesc
        .setIdentity(Name("tests/descriptor_buffer/command_ir_rollback_retry"))
        .setMarkerLabel("Command IR Rollback Retry")
        .setQueue(transferQueue)
        .setScheduling(retryScheduling)
        .setDependencies(&clearTask, 1u)
    ;
    const GpuTaskId retryTask = graph.addTask<NativePacketCaptureRetryTask>(
        retryDesc,
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &shouldRecord,
            .attempted = &retryTaskAttempted,
        }
    );
    ASSERT_TRUE(retryTask.valid());

    const GpuPhysicalQueueInfo queue{
        .id = BackendQueueId(device, CommandQueue::Graphics),
        .queueClass = CommandQueue::Graphics,
        .capabilities = static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
            | static_cast<u8>(GpuQueueCapability::Transfer)
        ),
        .familyIndex = device.getQueueFamilyIndex(CommandQueue::Graphics),
        .queueIndex = 0u,
        .dedicated = false,
    };
    const GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/command_ir_rollback_scratch"));
    const GpuTaskGraphCompiler compiler;
    ASSERT_TRUE(compiler.compile(graph, analysis, topology, assignments, compiledGraph, scratchArena));
    ASSERT_EQ(compiledGraph.packetCount(), 1u);
    const GpuSubmissionPacketId packet = compiledGraph.packetForTask(clearTask);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(compiledGraph.packetForTask(retryTask), packet);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuCommandIrCapture commandIrCapture(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    EXPECT_FALSE(recorder.recordPacket(
        graph,
        compiledGraph,
        GpuNativePacketRecordDesc{ .packet = packet },
        recordedGraph,
        &commandIrCapture
    ));
    EXPECT_TRUE(retryTaskAttempted);
    EXPECT_EQ(commandIrCapture.recordCount(), 0u);
    EXPECT_EQ(recordedGraph.find(packet), nullptr);

    shouldRecord = true;
    retryTaskAttempted = false;
    ASSERT_TRUE(recorder.recordPacket(
        graph,
        compiledGraph,
        GpuNativePacketRecordDesc{ .packet = packet },
        recordedGraph,
        &commandIrCapture
    ));
    EXPECT_TRUE(retryTaskAttempted);
    ASSERT_EQ(commandIrCapture.recordCount(), 1u);
    const GpuCommandIrBuiltinTaskRecord* const captureRecord = commandIrCapture.recordAt(0u);
    ASSERT_NE(captureRecord, nullptr);
    EXPECT_EQ(captureRecord->opcode, GpuCommandIrOpcode::ClearBuffer);
    EXPECT_EQ(captureRecord->task, clearTask);
    EXPECT_EQ(captureRecord->packet, packet);
    EXPECT_EQ(captureRecord->destination, bufferResource);

    // A non-empty capture belongs to this graph generation. Reject it before recording an unrelated packet that
    // contains no primitive command, rather than making old records appear to be a trace for the new graph.
    GpuTaskGraph foreignGraph(DescriptorBufferRoundTripTest::arena());
    bool foreignTaskAttempted = false;
    GpuTaskDesc foreignTaskDesc;
    foreignTaskDesc
        .setIdentity(Name("tests/descriptor_buffer/command_ir_foreign_graph"))
        .setMarkerLabel("Command IR Foreign Graph")
        .setQueue(transferQueue)
    ;
    const GpuTaskId foreignTask = foreignGraph.addTask<NativePacketCaptureRetryTask>(
        foreignTaskDesc,
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &shouldRecord,
            .attempted = &foreignTaskAttempted,
        }
    );
    ASSERT_TRUE(foreignTask.valid());
    GpuTaskGraphAnalysis foreignAnalysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments foreignAssignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph foreignCompiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena foreignScratchArena(Name("tests/descriptor_buffer/command_ir_foreign_scratch"));
    ASSERT_TRUE(compiler.compile(
        foreignGraph,
        foreignAnalysis,
        topology,
        foreignAssignments,
        foreignCompiledGraph,
        foreignScratchArena
    ));
    const GpuSubmissionPacketId foreignPacket = foreignCompiledGraph.packetForTask(foreignTask);
    ASSERT_TRUE(foreignPacket.valid());
    GpuRecordedGraph foreignRecordedGraph(DescriptorBufferRoundTripTest::arena());
    EXPECT_FALSE(recorder.recordPacket(
        foreignGraph,
        foreignCompiledGraph,
        GpuNativePacketRecordDesc{ .packet = foreignPacket },
        foreignRecordedGraph,
        &commandIrCapture
    ));
    EXPECT_FALSE(foreignTaskAttempted);
    EXPECT_EQ(commandIrCapture.recordCount(), 1u);
}


TEST_F(DescriptorBufferRoundTripTest, NativePacketTraversesCompilerPacketRanges){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto buffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(buffer.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId resource = graph.importBuffer(
        buffer,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/compile_order_buffer"))
            .setMarkerLabel("Compile Order Buffer")
            .setType(GpuGraphResourceType::Buffer)
    );
    ASSERT_TRUE(resource.valid());

    // This mirrors the graph-owned software effects sequence: Shadow Visibility -> Software Caustics.
    // A single Graphics family remains a valid fallback for each compute-designated packet.
    const GpuTaskResourceUse writerUses[] = {
        GpuTaskResourceUse{
            .resource = resource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    GpuTaskSchedulingHint writerScheduling;
    writerScheduling.forceSubmissionBoundary = true;
    writerScheduling.allowPacketMerge = false;
    GpuTaskDesc writerDesc;
    writerDesc
        .setIdentity(Name("tests/descriptor_buffer/software_effects_shadow_visibility"))
        .setMarkerLabel("Shadow Visibility")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Compute,
            GpuQueuePreference::Compute,
            true,
            true,
        })
        .setScheduling(writerScheduling)
        .setResourceUses(writerUses, LengthOf(writerUses))
    ;
    bool writerRecorded = false;
    const GpuTaskId writer = graph.addTask<NativePacketPrefixTask>(
        writerDesc,
        NativePacketPrefixTask::Payload{
            .buffer = buffer.get(),
            .expectedState = ResourceStates::UnorderedAccess,
            .recorded = &writerRecorded,
        }
    );
    ASSERT_TRUE(writer.valid());

    const GpuTaskResourceUse readerUses[] = {
        GpuTaskResourceUse{
            .resource = resource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
    };
    GpuTaskSchedulingHint readerScheduling;
    readerScheduling.forceSubmissionBoundary = true;
    readerScheduling.allowPacketMerge = false;
    GpuTaskDesc readerDesc;
    readerDesc
        .setIdentity(Name("tests/descriptor_buffer/software_effects_caustics"))
        .setMarkerLabel("Software Caustics")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Compute,
            GpuQueuePreference::Compute,
            true,
            true,
        })
        .setScheduling(readerScheduling)
        .setDependencies(&writer, 1u)
        .setResourceUses(readerUses, LengthOf(readerUses))
    ;
    bool readerRecorded = false;
    const GpuTaskId reader = graph.addTask<NativePacketPrefixTask>(
        readerDesc,
        NativePacketPrefixTask::Payload{
            .buffer = buffer.get(),
            .expectedState = ResourceStates::UnorderedAccess,
            .recorded = &readerRecorded,
        }
    );
    ASSERT_TRUE(reader.valid());

    const GpuPhysicalQueueInfo queue{
        .id = BackendQueueId(device, CommandQueue::Graphics),
        .queueClass = CommandQueue::Graphics,
        .capabilities = static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
            | static_cast<u8>(GpuQueueCapability::Transfer)
        ),
        .familyIndex = device.getQueueFamilyIndex(CommandQueue::Graphics),
        .queueIndex = 0u,
        .dedicated = false,
    };
    const GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/software_effects_scratch"));
    const GpuTaskGraphCompiler compiler;
    ASSERT_TRUE(compiler.compile(graph, analysis, topology, assignments, compiledGraph, scratchArena));
    ASSERT_EQ(compiledGraph.packetCount(), 2u);
    const GpuSubmissionPacketId writerPacket = compiledGraph.packetForTask(writer);
    const GpuSubmissionPacketId readerPacket = compiledGraph.packetForTask(reader);
    ASSERT_TRUE(writerPacket.valid());
    ASSERT_TRUE(readerPacket.valid());
    EXPECT_EQ(compiledGraph.packetIdAt(0u), writerPacket);
    EXPECT_EQ(compiledGraph.packetIdAt(1u), readerPacket);
    ASSERT_EQ(compiledGraph.packet(readerPacket).dependencyCount, 1u);
    EXPECT_EQ(compiledGraph.packetDependencies(readerPacket)[0].producer, writerPacket);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    // The writer override proves that range recording may seed only selected packets; the reader receives the
    // compiler-derived default descriptor.
    const GpuNativePacketRecordDesc recordOverrides[] = {
        GpuNativePacketRecordDesc{ .packet = writerPacket },
    };
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        compiledGraph.allPacketRange(),
        recordOverrides,
        1u,
        recordedGraph
    ));
    EXPECT_TRUE(writerRecorded);
    EXPECT_TRUE(readerRecorded);

    GpuRecordedGraph rejectedOverrideGraph(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecordDesc outsideRangeOverride[] = {
        GpuNativePacketRecordDesc{ .packet = readerPacket },
    };
    EXPECT_FALSE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        compiledGraph.packetRange(writerPacket, writerPacket),
        outsideRangeOverride,
        LengthOf(outsideRangeOverride),
        rejectedOverrideGraph
    ));
    const GpuNativePacketRecordDesc duplicateOverrides[] = {
        GpuNativePacketRecordDesc{ .packet = writerPacket },
        GpuNativePacketRecordDesc{ .packet = writerPacket },
    };
    EXPECT_FALSE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        compiledGraph.allPacketRange(),
        duplicateOverrides,
        LengthOf(duplicateOverrides),
        rejectedOverrideGraph
    ));

    const GpuTaskGraphSubmitter submitter(device);
    NativePacketRangeAcceptanceObserver acceptanceObserver;
    acceptanceObserver.continueSubmission = false;
    const GpuTaskGraphPacketAcceptedCallback acceptedCallback{
        .context = &acceptanceObserver,
        .invoke = ObserveNativePacketRangeAcceptance,
    };
    GpuSubmissionPacketId stoppedPacket;
    EXPECT_FALSE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        compiledGraph.packetRange(writerPacket, writerPacket),
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena,
        &stoppedPacket,
        &acceptedCallback
    ));
    EXPECT_EQ(stoppedPacket, writerPacket);
    EXPECT_EQ(acceptanceObserver.acceptedCount, 1u);
    EXPECT_EQ(acceptanceObserver.lastPacket, writerPacket);
    EXPECT_TRUE(transaction.packetToken(writerPacket).valid());
    EXPECT_FALSE(transaction.packetToken(readerPacket).valid());

    NativePacketSubmissionHookObserver hookObserver;
    const QueueSubmissionPreSubmitHook rejectedHook{
        .context = &hookObserver,
        .invoke = RejectNativePacketSubmissionHook,
    };
    const GpuTaskGraphPacketSubmissionHook outsideRangeSubmissionHook[] = {
        GpuTaskGraphPacketSubmissionHook{
            .packet = writerPacket,
            .hook = rejectedHook,
        },
    };
    EXPECT_FALSE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        compiledGraph.packetRange(readerPacket, readerPacket),
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena,
        nullptr,
        &acceptedCallback,
        outsideRangeSubmissionHook,
        LengthOf(outsideRangeSubmissionHook)
    ));
    EXPECT_EQ(hookObserver.invocationCount, 0u);

    acceptanceObserver.continueSubmission = true;
    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        compiledGraph.packetRange(readerPacket, readerPacket),
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena,
        nullptr,
        &acceptedCallback
    ));
    EXPECT_EQ(acceptanceObserver.acceptedCount, 2u);
    EXPECT_EQ(acceptanceObserver.lastPacket, readerPacket);
    EXPECT_TRUE(transaction.packetToken(readerPacket).valid());
    EXPECT_TRUE(device.waitForIdle());
}


TEST_F(DescriptorBufferRoundTripTest, NativePacketStagesHardwareAvboitLightingCompositeSharedTransaction){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto buffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(buffer.get(), nullptr);
    auto avboitPrefixBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(avboitPrefixBuffer.get(), nullptr);
    auto avboitOutput = CreateConcurrentTestTexture(device);
    ASSERT_NE(avboitOutput.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId resource = graph.importBuffer(
        buffer,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/staged_deferred_buffer"))
            .setMarkerLabel("Staged Deferred Buffer")
            .setType(GpuGraphResourceType::Buffer)
    );
    const GpuGraphResourceId avboitPrefix = graph.importBuffer(
        avboitPrefixBuffer,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/staged_avboit_prefix"))
            .setMarkerLabel("AVBOIT Prefix")
            .setType(GpuGraphResourceType::Buffer)
    );
    const GpuGraphResourceId avboitAccumulation = graph.importTexture(
        avboitOutput,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/staged_avboit_accumulation"))
            .setMarkerLabel("AVBOIT Accumulation")
            .setType(GpuGraphResourceType::Texture)
    );
    ASSERT_TRUE(resource.valid());
    ASSERT_TRUE(avboitPrefix.valid());
    ASSERT_TRUE(avboitAccumulation.valid());

    GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;

    const GpuTaskResourceUse hardwareUses[] = {
        GpuTaskResourceUse{
            .resource = resource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    GpuTaskDesc hardwareDesc;
    hardwareDesc
        .setIdentity(Name("tests/descriptor_buffer/staged_hardware_caustics"))
        .setMarkerLabel("Hardware Caustics")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Graphics,
            GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setScheduling(scheduling)
        .setResourceUses(hardwareUses, LengthOf(hardwareUses))
    ;
    bool hardwareRecorded = false;
    const GpuTaskId hardwareTask = graph.addTask<NativePacketPrefixTask>(
        hardwareDesc,
        NativePacketPrefixTask::Payload{
            .buffer = buffer.get(),
            .expectedState = ResourceStates::UnorderedAccess,
            .recorded = &hardwareRecorded,
        }
    );
    ASSERT_TRUE(hardwareTask.valid());

    const GpuTaskResourceUse avboitPreUses[] = {
        GpuTaskResourceUse{
            .resource = avboitPrefix,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = avboitAccumulation,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    GpuTaskDesc avboitPreDesc;
    avboitPreDesc
        .setIdentity(Name("tests/descriptor_buffer/staged_avboit_pre"))
        .setMarkerLabel("AVBOIT Pre")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Graphics,
            GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setScheduling(scheduling)
        .setResourceUses(avboitPreUses, LengthOf(avboitPreUses))
    ;
    bool avboitPreRecorded = false;
    const GpuTaskId avboitPreTask = graph.addTask<NativePacketPrefixTask>(
        avboitPreDesc,
        NativePacketPrefixTask::Payload{
            .buffer = avboitPrefixBuffer.get(),
            .expectedState = ResourceStates::ConstantBuffer,
            .texture = avboitOutput.get(),
            .expectedTextureState = ResourceStates::UnorderedAccess,
            .recorded = &avboitPreRecorded,
        }
    );
    ASSERT_TRUE(avboitPreTask.valid());

    const GpuTaskResourceUse lightingUses[] = {
        GpuTaskResourceUse{
            .resource = resource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
    };
    GpuTaskDesc lightingDesc;
    lightingDesc
        .setIdentity(Name("tests/descriptor_buffer/staged_deferred_lighting"))
        .setMarkerLabel("Deferred Lighting")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Compute,
            GpuQueuePreference::Compute,
            true,
            true,
        })
        .setScheduling(scheduling)
        .setDependencies(&hardwareTask, 1u)
        .setResourceUses(lightingUses, LengthOf(lightingUses))
    ;
    bool lightingRecorded = false;
    const GpuTaskId lightingTask = graph.addTask<NativePacketPrefixTask>(
        lightingDesc,
        NativePacketPrefixTask::Payload{
            .buffer = buffer.get(),
            .expectedState = ResourceStates::UnorderedAccess,
            .recorded = &lightingRecorded,
        }
    );
    ASSERT_TRUE(lightingTask.valid());

    const GpuTaskResourceUse compositeUses[] = {
        GpuTaskResourceUse{
            .resource = resource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = avboitAccumulation,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    const GpuTaskId compositeTaskDependencies[] = {
        lightingTask,
        avboitPreTask,
    };
    GpuTaskDesc compositeDesc;
    compositeDesc
        .setIdentity(Name("tests/descriptor_buffer/staged_deferred_composite"))
        .setMarkerLabel("Deferred Composite")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Compute,
            GpuQueuePreference::Compute,
            true,
            true,
        })
        .setScheduling(scheduling)
        .setDependencies(compositeTaskDependencies, LengthOf(compositeTaskDependencies))
        .setResourceUses(compositeUses, LengthOf(compositeUses))
    ;
    bool compositeRecorded = false;
    const GpuTaskId compositeTask = graph.addTask<NativePacketPrefixTask>(
        compositeDesc,
        NativePacketPrefixTask::Payload{
            .buffer = buffer.get(),
            .expectedState = ResourceStates::ShaderResource,
            .texture = avboitOutput.get(),
            .expectedTextureState = ResourceStates::ShaderResource,
            .recorded = &compositeRecorded,
        }
    );
    ASSERT_TRUE(compositeTask.valid());

    const GpuPhysicalQueueInfo queue{
        .id = BackendQueueId(device, CommandQueue::Graphics),
        .queueClass = CommandQueue::Graphics,
        .capabilities = static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
            | static_cast<u8>(GpuQueueCapability::Transfer)
        ),
        .familyIndex = device.getQueueFamilyIndex(CommandQueue::Graphics),
        .queueIndex = 0u,
        .dedicated = false,
    };
    const GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/staged_deferred_scratch"));
    const GpuTaskGraphCompiler compiler;
    ASSERT_TRUE(compiler.compile(graph, analysis, topology, assignments, compiledGraph, scratchArena));

    const GpuTaskQueueAssignment* const hardwareAssignment = assignments.find(hardwareTask);
    const GpuTaskQueueAssignment* const avboitPreAssignment = assignments.find(avboitPreTask);
    const GpuTaskQueueAssignment* const lightingAssignment = assignments.find(lightingTask);
    const GpuTaskQueueAssignment* const compositeAssignment = assignments.find(compositeTask);
    ASSERT_NE(hardwareAssignment, nullptr);
    ASSERT_NE(avboitPreAssignment, nullptr);
    ASSERT_NE(lightingAssignment, nullptr);
    ASSERT_NE(compositeAssignment, nullptr);
    EXPECT_EQ(hardwareAssignment->queueClass, CommandQueue::Graphics);
    EXPECT_EQ(hardwareAssignment->reason, GpuTaskQueueAssignmentReason::RequiredGraphics);
    EXPECT_EQ(avboitPreAssignment->queueClass, CommandQueue::Graphics);
    EXPECT_EQ(avboitPreAssignment->reason, GpuTaskQueueAssignmentReason::RequiredGraphics);
    EXPECT_EQ(lightingAssignment->queueClass, CommandQueue::Graphics);
    EXPECT_EQ(lightingAssignment->reason, GpuTaskQueueAssignmentReason::Fallback);
    EXPECT_EQ(compositeAssignment->queueClass, CommandQueue::Graphics);
    EXPECT_EQ(compositeAssignment->reason, GpuTaskQueueAssignmentReason::Fallback);

    ASSERT_EQ(compiledGraph.packetCount(), 4u);
    const GpuSubmissionPacketRange packetRange = compiledGraph.allPacketRange();
    ASSERT_TRUE(packetRange.valid());
    ASSERT_EQ(packetRange.packetCount, compiledGraph.packetCount());
    const GpuSubmissionPacketId hardwarePacket = compiledGraph.packetForTask(hardwareTask);
    const GpuSubmissionPacketId avboitPrePacket = compiledGraph.packetForTask(avboitPreTask);
    const GpuSubmissionPacketId lightingPacket = compiledGraph.packetForTask(lightingTask);
    const GpuSubmissionPacketId compositePacket = compiledGraph.packetForTask(compositeTask);
    ASSERT_TRUE(hardwarePacket.valid());
    ASSERT_TRUE(avboitPrePacket.valid());
    ASSERT_TRUE(lightingPacket.valid());
    ASSERT_TRUE(compositePacket.valid());
    EXPECT_EQ(compiledGraph.packetIdAt(0u), hardwarePacket);
    EXPECT_EQ(compiledGraph.packetIdAt(1u), avboitPrePacket);
    EXPECT_EQ(compiledGraph.packetIdAt(2u), lightingPacket);
    EXPECT_EQ(compiledGraph.packetIdAt(3u), compositePacket);
    EXPECT_EQ(compiledGraph.packet(hardwarePacket).dependencyCount, 0u);
    EXPECT_EQ(compiledGraph.packet(avboitPrePacket).dependencyCount, 0u);
    ASSERT_EQ(compiledGraph.packet(lightingPacket).dependencyCount, 1u);
    EXPECT_EQ(compiledGraph.packetDependencies(lightingPacket)[0u].producer, hardwarePacket);
    ASSERT_EQ(compiledGraph.packet(compositePacket).dependencyCount, 2u);
    const GpuPacketDependency* const compositePacketDependencies = compiledGraph.packetDependencies(compositePacket);
    ASSERT_NE(compositePacketDependencies, nullptr);
    bool compositeWaitsForLighting = false;
    bool compositeWaitsForAvboit = false;
    for(usize index = 0u; index < compiledGraph.packet(compositePacket).dependencyCount; ++index){
        compositeWaitsForLighting = compositeWaitsForLighting || compositePacketDependencies[index].producer == lightingPacket;
        compositeWaitsForAvboit = compositeWaitsForAvboit || compositePacketDependencies[index].producer == avboitPrePacket;
    }
    EXPECT_TRUE(compositeWaitsForLighting);
    EXPECT_TRUE(compositeWaitsForAvboit);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    GpuSubmissionPacketId failedPacket;
    const bool allPacketsRecorded = recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        packetRange,
        nullptr,
        0u,
        recordedGraph,
        &failedPacket
    );
    ASSERT_TRUE(allPacketsRecorded) << "failed packet index: " << failedPacket.index;
    EXPECT_TRUE(hardwareRecorded);
    EXPECT_TRUE(avboitPreRecorded);
    EXPECT_TRUE(lightingRecorded);
    EXPECT_TRUE(compositeRecorded);

    const GpuTaskGraphSubmitter submitter(device);
    ASSERT_TRUE(submitter.submitPacket(
        graph,
        compiledGraph,
        recordedGraph,
        hardwarePacket,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    const QueueSubmissionToken hardwareToken = transaction.packetToken(hardwarePacket);
    ASSERT_TRUE(hardwareToken.valid());
    EXPECT_FALSE(transaction.packetToken(avboitPrePacket).valid());
    EXPECT_FALSE(transaction.packetToken(lightingPacket).valid());
    EXPECT_FALSE(transaction.packetToken(compositePacket).valid());

    ASSERT_TRUE(submitter.submitPacket(
        graph,
        compiledGraph,
        recordedGraph,
        lightingPacket,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    const QueueSubmissionToken lightingToken = transaction.packetToken(lightingPacket);
    ASSERT_TRUE(lightingToken.valid());
    EXPECT_EQ(transaction.packetToken(hardwarePacket).value, hardwareToken.value);
    EXPECT_FALSE(transaction.packetToken(avboitPrePacket).valid());
    EXPECT_FALSE(transaction.packetToken(compositePacket).valid());

    ASSERT_TRUE(submitter.submitPacket(
        graph,
        compiledGraph,
        recordedGraph,
        avboitPrePacket,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    const QueueSubmissionToken avboitPreToken = transaction.packetToken(avboitPrePacket);
    ASSERT_TRUE(avboitPreToken.valid());
    EXPECT_FALSE(transaction.packetToken(compositePacket).valid());

    ASSERT_TRUE(submitter.submitPacket(
        graph,
        compiledGraph,
        recordedGraph,
        compositePacket,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    ASSERT_TRUE(transaction.packetToken(compositePacket).valid());
    EXPECT_EQ(transaction.packetToken(hardwarePacket).value, hardwareToken.value);
    EXPECT_EQ(transaction.packetToken(avboitPrePacket).value, avboitPreToken.value);
    EXPECT_EQ(transaction.packetToken(lightingPacket).value, lightingToken.value);
    EXPECT_TRUE(device.waitForIdle());
}


TEST_F(DescriptorBufferRoundTripTest, NativePacketLateRecordsHistoryTailInSharedTransaction){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto sourceBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
    );
    auto historyBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
    );
    auto presentationBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(sourceBuffer.get(), nullptr);
    ASSERT_NE(historyBuffer.get(), nullptr);
    ASSERT_NE(presentationBuffer.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId source = graph.importBuffer(
        sourceBuffer,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/late_history_source"))
            .setMarkerLabel("Late History Source")
            .setType(GpuGraphResourceType::Buffer)
    );
    const GpuGraphResourceId history = graph.importBuffer(
        historyBuffer,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/late_history_destination"))
            .setMarkerLabel("Late History Destination")
            .setType(GpuGraphResourceType::Buffer)
    );
    const GpuGraphResourceId presentation = graph.importBuffer(
        presentationBuffer,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/late_history_presentation"))
            .setMarkerLabel("Late History Presentation")
            .setType(GpuGraphResourceType::Buffer)
    );
    ASSERT_TRUE(source.valid());
    ASSERT_TRUE(history.valid());
    ASSERT_TRUE(presentation.valid());

    GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;

    const GpuTaskResourceUse presentUses[] = {
        GpuTaskResourceUse{
            .resource = presentation,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    GpuTaskDesc presentDesc;
    presentDesc
        .setIdentity(Name("tests/descriptor_buffer/late_history_present"))
        .setMarkerLabel("Deferred Present")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Graphics,
            GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setScheduling(scheduling)
        .setResourceUses(presentUses, LengthOf(presentUses))
    ;
    bool presentRecorded = false;
    QueueSubmissionToken presentAcceptedToken;
    const GpuTaskId presentTask = graph.addTask<NativePacketPrefixTask>(
        presentDesc,
        NativePacketPrefixTask::Payload{
            .buffer = presentationBuffer.get(),
            .expectedState = ResourceStates::ShaderResource,
            .recorded = &presentRecorded,
            .acceptedToken = &presentAcceptedToken,
        }
    );
    ASSERT_TRUE(presentTask.valid());

    const GpuTaskResourceUse historyUses[] = {
        GpuTaskResourceUse{
            .resource = source,
            .range = {},
            .requiredState = ResourceStates::CopySource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = history,
            .range = {},
            .requiredState = ResourceStates::CopyDest,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    const GpuTaskId historyDependencies[] = { presentTask };
    GpuTaskDesc historyDesc;
    historyDesc
        .setIdentity(Name("tests/descriptor_buffer/late_history_copy"))
        .setMarkerLabel("Lagged Lighting History Copy")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Transfer,
            GpuQueuePreference::Transfer,
            true,
            true,
        })
        .setScheduling(scheduling)
        .setDependencies(historyDependencies, LengthOf(historyDependencies))
        .setResourceUses(historyUses, LengthOf(historyUses))
    ;
    bool historyRecorded = false;
    QueueSubmissionToken historyAcceptedToken;
    const GpuTaskId historyTask = graph.addTask<NativePacketPrefixTask>(
        historyDesc,
        NativePacketPrefixTask::Payload{
            .buffer = sourceBuffer.get(),
            .expectedState = ResourceStates::CopySource,
            .recorded = &historyRecorded,
            .acceptedToken = &historyAcceptedToken,
        }
    );
    ASSERT_TRUE(historyTask.valid());

    const GpuPhysicalQueueInfo queue{
        .id = BackendQueueId(device, CommandQueue::Graphics),
        .queueClass = CommandQueue::Graphics,
        .capabilities = static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
            | static_cast<u8>(GpuQueueCapability::Transfer)
        ),
        .familyIndex = device.getQueueFamilyIndex(CommandQueue::Graphics),
        .queueIndex = 0u,
        .dedicated = false,
    };
    const GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/late_history_scratch"));
    const GpuTaskGraphCompiler compiler;
    ASSERT_TRUE(compiler.compile(graph, analysis, topology, assignments, compiledGraph, scratchArena));

    const GpuSubmissionPacketId presentPacket = compiledGraph.packetForTask(presentTask);
    const GpuSubmissionPacketId historyPacket = compiledGraph.packetForTask(historyTask);
    ASSERT_TRUE(presentPacket.valid());
    ASSERT_TRUE(historyPacket.valid());
    ASSERT_EQ(compiledGraph.packetCount(), 2u);
    EXPECT_EQ(compiledGraph.packetIdAt(0u), presentPacket);
    EXPECT_EQ(compiledGraph.packetIdAt(1u), historyPacket);
    ASSERT_GE(compiledGraph.packet(historyPacket).dependencyCount, 1u);
    const GpuPacketDependency* const historyPacketDependencies = compiledGraph.packetDependencies(historyPacket);
    ASSERT_NE(historyPacketDependencies, nullptr);
    bool historyWaitsForPresent = false;
    for(usize index = 0u; index < compiledGraph.packet(historyPacket).dependencyCount; ++index)
        historyWaitsForPresent = historyWaitsForPresent || historyPacketDependencies[index].producer == presentPacket;
    EXPECT_TRUE(historyWaitsForPresent);
    EXPECT_EQ(compiledGraph.packet(historyPacket).externalDependencyCount, 0u);

    // Current-frame producer state arrives independently from the terminal Present dependency. This is the same
    // late fan-in shape used by the renderer's shadow/caustic/surfel return snapshots.
    CommandListResourceStateHandoff sourceState(DescriptorBufferRoundTripTest::arena());
    auto sourceProducer = device.createCommandList();
    ASSERT_NE(sourceProducer.get(), nullptr);
    sourceProducer->open();
    sourceProducer->setBufferState(sourceBuffer.get(), ResourceStates::CopySource);
    sourceProducer->close(&sourceState);
    ASSERT_TRUE(sourceState.valid());
    CommandList* const sourceProducerCommandLists[] = { sourceProducer.get() };
    bool sourceProducerSubmitted = false;
    EXPECT_GT(device.executeCommandLists(
        sourceProducerCommandLists,
        LengthOf(sourceProducerCommandLists),
        CommandQueue::Graphics,
        &sourceProducerSubmitted
    ), 0u);
    ASSERT_TRUE(sourceProducerSubmitted);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacket(
        graph,
        compiledGraph,
        GpuNativePacketRecordDesc{ .packet = presentPacket },
        recordedGraph
    ));
    EXPECT_TRUE(presentRecorded);

    const GpuTaskGraphSubmitter submitter(device);
    ASSERT_TRUE(submitter.submitPacket(
        graph,
        compiledGraph,
        recordedGraph,
        presentPacket,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    const QueueSubmissionToken presentSubmissionToken = transaction.packetToken(presentPacket);
    ASSERT_TRUE(presentSubmissionToken.valid());
    EXPECT_EQ(presentAcceptedToken.value, presentSubmissionToken.value);
    EXPECT_FALSE(transaction.packetToken(historyPacket).valid());

    const GpuExternalPacketStateSource historyStateSources[] = {
        GpuExternalPacketStateSource{ .states = &sourceState },
    };
    ASSERT_TRUE(recorder.recordPacket(
        graph,
        compiledGraph,
        GpuNativePacketRecordDesc{
            .packet = historyPacket,
            .externalStateSources = historyStateSources,
            .externalStateSourceCount = LengthOf(historyStateSources),
        },
        recordedGraph
    ));
    EXPECT_TRUE(historyRecorded);
    ASSERT_TRUE(submitter.submitPacket(
        graph,
        compiledGraph,
        recordedGraph,
        historyPacket,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    const QueueSubmissionToken historySubmissionToken = transaction.packetToken(historyPacket);
    ASSERT_TRUE(historySubmissionToken.valid());
    EXPECT_EQ(historyAcceptedToken.value, historySubmissionToken.value);
    EXPECT_TRUE(device.waitForIdle());
}


inline constexpr GpuTimingScopeDefinition s_FrameTimingPreambleScope("tests/frame_timing_preamble");
inline constexpr GpuTimingScopeDefinition s_FrameTimingLateActivationScope("tests/frame_timing_late_activation");
inline constexpr GpuTimingScopeDefinition s_UnpreparedTimingScope("tests/timing_unprepared_scope");
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
        if(!m_framebuffer)
            return false;

        m_commandList = device.createCommandList();
        return m_commandList != nullptr;
    }

    virtual void render(Framebuffer*)override{
        if(m_recorded || !m_framebuffer || !m_commandList)
            return;

        auto& device = getGraphics().getDevice();
        CommandList* const commandList = m_commandList.get();

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

            CommandList* commandLists[] = { commandList };
            m_recorded = timingTicket.submit(device, commandLists, 1u);
        }
    }

    [[nodiscard]] bool recorded()const{ return m_recorded; }


private:
    const GpuTimingScopeDefinition& m_timingScope;
    TextureHandle m_target;
    FramebufferHandle m_framebuffer;
    CommandListHandle m_commandList;
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
    ASSERT_TRUE(graphics.prepareFramePreamble());
    graphics.render();
    graphics.removeRenderPass(probePass);

    ASSERT_TRUE(probePass.recorded());
    ASSERT_TRUE(device.waitForIdle());

    // collect() runs at the next frame open, before that frame's reset can overwrite the completed sample.
    ASSERT_TRUE(graphics.prepareFramePreamble());
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
    ASSERT_TRUE(graphics.prepareFramePreamble());
    graphics.render();
    graphics.removeRenderPass(probePass);

    ASSERT_TRUE(probePass.recorded());
    ASSERT_TRUE(device.waitForIdle());

    ASSERT_TRUE(graphics.prepareFramePreamble());
    graphics.render();
    ASSERT_TRUE(device.waitForIdle());
    EXPECT_TRUE(timingSink.stats(s_FrameTimingLateActivationScope.identity).valid());

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
}


// Recording must not grow persistent timer-query pools. A scope that was not declared during preparation simply
// produces no sample, even when the command list is outside dynamic rendering and could otherwise self-reset a new
// query pool on the device timeline.
TEST_F(DescriptorBufferRoundTripTest, GpuTimingScopesRequirePreparedQueryPools){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = graphics.gpuTiming();
    auto& timingSink = s_scope->gpuTimingSink();

    s_scope->setGpuTimingEnabled(true);
    ASSERT_TRUE(graphics.prepareFramePreamble());

    auto commandList = device.createCommandList();
    ASSERT_NE(commandList.get(), nullptr);
    GpuTimingSubmissionTicket timingTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(timingTicket);
        commandList->open();
        {
            GpuTimingMeasure timingMeasure(timing, s_UnpreparedTimingScope, device, *commandList);
        }
        commandList->close();
    }

    CommandList* commandLists[] = { commandList.get() };
    ASSERT_TRUE(timingTicket.submit(device, commandLists, 1u));
    ASSERT_TRUE(device.waitForIdle());
    ASSERT_TRUE(graphics.prepareFramePreamble());
    EXPECT_FALSE(timingSink.stats(s_UnpreparedTimingScope.identity).valid());

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

    // Establish the one prepared pool on the device timeline, exactly as Graphics::prepareFramePreamble() does
    // before its passes.
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


// Both submit forms must reject an incomplete batch before Vulkan sees any part of it, and that rejection must
// resolve the ticket so a later retry cannot accidentally submit a split timing scope.
TEST_F(DescriptorBufferRoundTripTest, GpuTimingSubmissionTicketMalformedBatchesResolveAcrossSubmitOverloads){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = s_scope->graphics().gpuTiming();

    auto queueCommandList = device.createCommandList();
    auto laneCommandList = device.createCommandList();
    ASSERT_NE(queueCommandList.get(), nullptr);
    ASSERT_NE(laneCommandList.get(), nullptr);
    queueCommandList->open();
    queueCommandList->close();
    laneCommandList->open();
    laneCommandList->close();
    ASSERT_TRUE(queueCommandList->hasCommandBuffer());
    ASSERT_TRUE(laneCommandList->hasCommandBuffer());

    CommandList* queueCommandLists[] = { queueCommandList.get() };
    GpuTimingSubmissionTicket rejectedQueueTicket(timing);
    EXPECT_FALSE(rejectedQueueTicket.submit(device, nullptr, 0u));
    EXPECT_FALSE(rejectedQueueTicket.submit(device, queueCommandLists, 1u));
    EXPECT_TRUE(queueCommandList->hasCommandBuffer());

    CommandList* incompleteLaneCommandLists[] = { nullptr };
    CommandList* laneCommandLists[] = { laneCommandList.get() };
    GpuTimingSubmissionTicket rejectedLaneTicket(timing);
    EXPECT_FALSE(rejectedLaneTicket.submit(
        device,
        incompleteLaneCommandLists,
        1u,
        RenderLane::Graphics,
        QueueSubmissionDesc{}
    ).valid());
    EXPECT_FALSE(rejectedLaneTicket.submit(
        device,
        laneCommandLists,
        1u,
        RenderLane::Graphics,
        QueueSubmissionDesc{}
    ).valid());
    EXPECT_TRUE(laneCommandList->hasCommandBuffer());

    GpuTimingSubmissionTicket acceptedQueueTicket(timing);
    ASSERT_TRUE(acceptedQueueTicket.submit(device, queueCommandLists, 1u));
    GpuTimingSubmissionTicket acceptedLaneTicket(timing);
    ASSERT_TRUE(acceptedLaneTicket.submit(
        device,
        laneCommandLists,
        1u,
        RenderLane::Graphics,
        QueueSubmissionDesc{}
    ).valid());
    ASSERT_TRUE(device.waitForIdle());
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


#if !defined(NWB_FINAL) || defined(NWB_ENABLE_TEST_FEATURE_OVERRIDES)

// Texture uploads must leave ImGui's create/update status pending if the Vulkan submission is rejected. The next
// recording batch then retries the request and commits its status only after the device accepts that retry.
TEST_F(DescriptorBufferRoundTripTest, ImguiTextureUploadBatchCommitsOnlyAfterAcceptedSubmission){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name s_TestArenaName{"tests/descriptor_buffer/imgui_texture_upload_batch_arena"};
    Alloc::GlobalArena arena{s_TestArenaName};
    Impl::UiTextureUploadBatch uploads{arena};
    ImTextureData createTexture;
    ImTextureData updateTexture;
    createTexture.SetStatus(ImTextureStatus_WantCreate);
    updateTexture.SetStatus(ImTextureStatus_WantUpdates);

    auto rejected = device.createCommandList();
    ASSERT_NE(rejected.get(), nullptr);
    rejected->open();
    rejected->close();
    ASSERT_TRUE(rejected->hasCommandBuffer());

    uploads.add(createTexture);
    uploads.add(updateTexture);
    device.rejectNextSubmissionForTesting(CommandQueue::Graphics);
    CommandList* rejectedCommandLists[] = { rejected.get() };
    bool submitted = true;
    device.executeCommandLists(rejectedCommandLists, 1u, CommandQueue::Graphics, &submitted);
    EXPECT_FALSE(submitted);
    uploads.complete(submitted);
    EXPECT_EQ(createTexture.Status, ImTextureStatus_WantCreate);
    EXPECT_EQ(updateTexture.Status, ImTextureStatus_WantUpdates);

    auto accepted = device.createCommandList();
    ASSERT_NE(accepted.get(), nullptr);
    accepted->open();
    accepted->close();
    ASSERT_TRUE(accepted->hasCommandBuffer());

    uploads.add(createTexture);
    uploads.add(updateTexture);
    CommandList* acceptedCommandLists[] = { accepted.get() };
    submitted = false;
    device.executeCommandLists(acceptedCommandLists, 1u, CommandQueue::Graphics, &submitted);
    ASSERT_TRUE(submitted);
    uploads.complete(submitted);
    EXPECT_EQ(createTexture.Status, ImTextureStatus_OK);
    EXPECT_EQ(updateTexture.Status, ImTextureStatus_OK);
}


// The async renderer records its Graphics-prefix timestamp before it knows whether the pre-recorded final packet will
// submit. Reject that final submit after the prefix is accepted, then use a tiny Graphics recovery packet to complete
// the query without publishing a misleading partial render.frame sample. A following valid transaction proves the
// one reserved query slot was released rather than leaked.
TEST_F(DescriptorBufferRoundTripTest, GpuTimingFrameTransactionRetiresAcceptedPrefixAfterRejectedFinal){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = graphics.gpuTiming();
    auto& timingSink = s_scope->gpuTimingSink();

    s_scope->setGpuTimingEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(s_FrameTransactionScope.identity, device, 1u));
    timing.beginFrame(90u);

    auto prefix = device.createCommandList();
    auto rejectedFinal = device.createCommandList();
    auto recovery = device.createCommandList();
    ASSERT_NE(prefix.get(), nullptr);
    ASSERT_NE(rejectedFinal.get(), nullptr);
    ASSERT_NE(recovery.get(), nullptr);

    GpuTimingFrameTransaction rejectedTransaction(timing);
    GpuTimingSubmissionTicket prefixTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(prefixTicket);
        prefix->open();
        ASSERT_TRUE(rejectedTransaction.begin(s_FrameTransactionScope, device, *prefix));
        prefix->close();
    }
    GpuTimingSubmissionTicket finalTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(finalTicket);
        rejectedFinal->open();
        ASSERT_TRUE(rejectedTransaction.recordEnd(*rejectedFinal));
        rejectedFinal->close();
    }

    CommandList* prefixCommandLists[] = { prefix.get() };
    const QueueSubmissionToken prefixToken = prefixTicket.submit(
        device,
        prefixCommandLists,
        1u,
        RenderLane::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(prefixToken.valid());
    rejectedTransaction.confirmBeginSubmission();

    device.rejectNextSubmissionForTesting(CommandQueue::Graphics);
    CommandList* rejectedFinalCommandLists[] = { rejectedFinal.get() };
    EXPECT_FALSE(finalTicket.submit(
        device,
        rejectedFinalCommandLists,
        1u,
        RenderLane::Graphics,
        QueueSubmissionDesc{}
    ).valid());

    rejectedTransaction.prepareForRecovery();
    recovery->open();
    ASSERT_TRUE(rejectedTransaction.recordEnd(*recovery));
    recovery->close();
    CommandList* recoveryCommandLists[] = { recovery.get() };
    const QueueSubmissionToken recoveryToken = device.executeCommandLists(
        recoveryCommandLists,
        1u,
        RenderLane::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(recoveryToken.valid());
    ASSERT_TRUE(rejectedTransaction.confirmEndSubmission(false));
    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 91u);
    EXPECT_FALSE(timingSink.stats(s_FrameTransactionScope.identity).valid());

    timing.beginFrame(91u);
    auto acceptedPrefix = device.createCommandList();
    auto acceptedFinal = device.createCommandList();
    ASSERT_NE(acceptedPrefix.get(), nullptr);
    ASSERT_NE(acceptedFinal.get(), nullptr);

    GpuTimingFrameTransaction acceptedTransaction(timing);
    GpuTimingSubmissionTicket acceptedPrefixTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(acceptedPrefixTicket);
        acceptedPrefix->open();
        ASSERT_TRUE(acceptedTransaction.begin(s_FrameTransactionScope, device, *acceptedPrefix));
        acceptedPrefix->close();
    }
    GpuTimingSubmissionTicket acceptedFinalTicket(timing);
    {
        GpuTimingSubmissionTicket::RecordingScope timingRecording(acceptedFinalTicket);
        acceptedFinal->open();
        ASSERT_TRUE(acceptedTransaction.recordEnd(*acceptedFinal));
        acceptedFinal->close();
    }

    CommandList* acceptedPrefixCommandLists[] = { acceptedPrefix.get() };
    ASSERT_TRUE(acceptedPrefixTicket.submit(
        device,
        acceptedPrefixCommandLists,
        1u,
        RenderLane::Graphics,
        QueueSubmissionDesc{}
    ).valid());
    acceptedTransaction.confirmBeginSubmission();
    CommandList* acceptedFinalCommandLists[] = { acceptedFinal.get() };
    ASSERT_TRUE(acceptedFinalTicket.submit(
        device,
        acceptedFinalCommandLists,
        1u,
        RenderLane::Graphics,
        QueueSubmissionDesc{}
    ).valid());
    ASSERT_TRUE(acceptedTransaction.confirmEndSubmission(true));
    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 92u);
    EXPECT_TRUE(timingSink.stats(s_FrameTransactionScope.identity).valid());

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
}


// A recovery packet is declared with the normal frame graph but recorded only after a later packet rejects. It must
// remain independent of that rejected packet, join the accepted queue frontier, and retire the timing scope before
// the shared transaction rejects any remaining normal work.
TEST_F(DescriptorBufferRoundTripTest, NativePacketLateRecordsFrameRecoveryInSharedTransaction){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = graphics.gpuTiming();
    auto& timingSink = s_scope->gpuTimingSink();

    s_scope->setGpuTimingEnabled(true);
    ASSERT_TRUE(timing.prepareScopeQueries(s_FrameTransactionScope.identity, device, 1u));
    timing.beginFrame(120u);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId frameTimingDomain = graph.importHazardDomain(
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/recovery_frame_timing"))
            .setMarkerLabel("Frame Timing")
            .setType(GpuGraphResourceType::HazardDomain)
    );
    const GpuGraphResourceId recoveryDomain = graph.importHazardDomain(
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/recovery_domain"))
            .setMarkerLabel("Frame Recovery")
            .setType(GpuGraphResourceType::HazardDomain)
    );
    ASSERT_TRUE(frameTimingDomain.valid());
    ASSERT_TRUE(recoveryDomain.valid());

    const GpuTaskResourceUse frameTimingUses[] = {
        GpuTaskResourceUse{
            .resource = frameTimingDomain,
            .range = {},
            .requiredState = ResourceStates::Common,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
    };
    const GpuTaskResourceUse recoveryUses[] = {
        GpuTaskResourceUse{
            .resource = recoveryDomain,
            .range = {},
            .requiredState = ResourceStates::Common,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
    };
    GpuTaskSchedulingHint scheduling;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    const GpuQueueRequest graphicsQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };

    GpuTimingFrameTransaction frameTransaction(timing);
    GpuTimingSubmissionTicket prefixTimingTicket(timing);
    GpuTimingSubmissionTicket finalTimingTicket(timing);
    bool prefixRecorded = false;
    bool finalRecorded = false;
    bool recoveryArmed = false;
    bool recoveryRetiresTiming = false;
    bool recoveryRecorded = false;
    bool recoveryAccepted = false;
    bool recoveryDiscarded = false;

    GpuTaskDesc prefixDesc;
    prefixDesc
        .setIdentity(Name("tests/descriptor_buffer/recovery_prefix"))
        .setMarkerLabel("Prefix")
        .setQueue(graphicsQueue)
        .setScheduling(scheduling)
        .setResourceUses(frameTimingUses, LengthOf(frameTimingUses))
    ;
    const GpuTaskId prefixTask = graph.addTask<NativeFrameTimingPacketTask>(
        prefixDesc,
        NativeFrameTimingPacketTask::Payload{
            .device = &device,
            .transaction = &frameTransaction,
            .timingTicket = &prefixTimingTicket,
            .endpoint = NativeFrameTimingPacketTask::Endpoint::Begin,
            .recorded = &prefixRecorded,
        }
    );
    ASSERT_TRUE(prefixTask.valid());

    const GpuTaskId finalDependencies[] = { prefixTask };
    GpuTaskDesc finalDesc;
    finalDesc
        .setIdentity(Name("tests/descriptor_buffer/recovery_rejected_final"))
        .setMarkerLabel("Rejected Final")
        .setQueue(graphicsQueue)
        .setScheduling(scheduling)
        .setDependencies(finalDependencies, LengthOf(finalDependencies))
        .setResourceUses(frameTimingUses, LengthOf(frameTimingUses))
    ;
    const GpuTaskId finalTask = graph.addTask<NativeFrameTimingPacketTask>(
        finalDesc,
        NativeFrameTimingPacketTask::Payload{
            .device = &device,
            .transaction = &frameTransaction,
            .timingTicket = &finalTimingTicket,
            .endpoint = NativeFrameTimingPacketTask::Endpoint::End,
            .recorded = &finalRecorded,
        }
    );
    ASSERT_TRUE(finalTask.valid());

    GpuTaskSchedulingHint recoveryScheduling = scheduling;
    recoveryScheduling.joinsAcceptedQueueFrontier = true;
    GpuTaskDesc recoveryDesc;
    recoveryDesc
        .setIdentity(Name("tests/descriptor_buffer/recovery_tail"))
        .setMarkerLabel("Frame Recovery")
        .setQueue(graphicsQueue)
        .setScheduling(recoveryScheduling)
        .setResourceUses(recoveryUses, LengthOf(recoveryUses))
    ;
    const GpuTaskId recoveryTask = graph.addTask<NativeFrameRecoveryPacketTask>(
        recoveryDesc,
        NativeFrameRecoveryPacketTask::Payload{
            .transaction = &frameTransaction,
            .armed = &recoveryArmed,
            .retiresTiming = &recoveryRetiresTiming,
            .recorded = &recoveryRecorded,
            .accepted = &recoveryAccepted,
            .discarded = &recoveryDiscarded,
        }
    );
    ASSERT_TRUE(recoveryTask.valid());

    const GpuPhysicalQueueInfo queue{
        .id = BackendQueueId(device, CommandQueue::Graphics),
        .queueClass = CommandQueue::Graphics,
        .capabilities = static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
            | static_cast<u8>(GpuQueueCapability::Transfer)
        ),
        .familyIndex = device.getQueueFamilyIndex(CommandQueue::Graphics),
        .queueIndex = 0u,
        .dedicated = false,
    };
    const GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/late_recovery_scratch"));
    const GpuTaskGraphCompiler compiler;
    ASSERT_TRUE(compiler.compile(graph, analysis, topology, assignments, compiledGraph, scratchArena));

    const GpuSubmissionPacketId prefixPacket = compiledGraph.packetForTask(prefixTask);
    const GpuSubmissionPacketId finalPacket = compiledGraph.packetForTask(finalTask);
    const GpuSubmissionPacketId recoveryPacket = compiledGraph.packetForTask(recoveryTask);
    ASSERT_TRUE(prefixPacket.valid());
    ASSERT_TRUE(finalPacket.valid());
    ASSERT_TRUE(recoveryPacket.valid());
    ASSERT_EQ(compiledGraph.packetCount(), 3u);
    EXPECT_EQ(compiledGraph.packetIdAt(0u), prefixPacket);
    EXPECT_EQ(compiledGraph.packetIdAt(1u), finalPacket);
    EXPECT_EQ(compiledGraph.packetIdAt(2u), recoveryPacket);
    EXPECT_EQ(compiledGraph.packet(recoveryPacket).dependencyCount, 0u);
    EXPECT_EQ(compiledGraph.packet(recoveryPacket).externalDependencyCount, 0u);
    EXPECT_TRUE(compiledGraph.packet(recoveryPacket).joinsAcceptedQueueFrontier);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacket(
        graph,
        compiledGraph,
        GpuNativePacketRecordDesc{ .packet = prefixPacket },
        recordedGraph
    ));
    ASSERT_TRUE(recorder.recordPacket(
        graph,
        compiledGraph,
        GpuNativePacketRecordDesc{ .packet = finalPacket },
        recordedGraph
    ));
    EXPECT_TRUE(prefixRecorded);
    EXPECT_TRUE(finalRecorded);

    const GpuTaskGraphSubmitter submitter(device);
    ASSERT_TRUE(submitter.submitPacket(
        graph,
        compiledGraph,
        recordedGraph,
        prefixPacket,
        nullptr,
        0u,
        transaction,
        scratchArena,
        &prefixTimingTicket
    ));
    const QueueSubmissionToken prefixToken = transaction.packetToken(prefixPacket);
    ASSERT_TRUE(prefixToken.valid());
    frameTransaction.confirmBeginSubmission();

    device.rejectNextSubmissionForTesting(CommandQueue::Graphics);
    EXPECT_FALSE(submitter.submitPacket(
        graph,
        compiledGraph,
        recordedGraph,
        finalPacket,
        nullptr,
        0u,
        transaction,
        scratchArena,
        &finalTimingTicket
    ));
    ASSERT_NE(transaction.packetRuntime(finalPacket), nullptr);
    EXPECT_EQ(transaction.packetRuntime(finalPacket)->state, GpuPacketRuntimeState::Rejected);
    EXPECT_FALSE(recoveryRecorded);
    EXPECT_FALSE(recoveryAccepted);
    EXPECT_FALSE(recoveryDiscarded);
    ASSERT_TRUE(frameTransaction.needsRetirement());

    frameTransaction.prepareForRecovery();
    recoveryArmed = true;
    recoveryRetiresTiming = true;
    ASSERT_TRUE(recorder.recordPacket(
        graph,
        compiledGraph,
        GpuNativePacketRecordDesc{ .packet = recoveryPacket },
        recordedGraph
    ));
    ASSERT_TRUE(recoveryRecorded);
    ASSERT_TRUE(submitter.submitPacket(
        graph,
        compiledGraph,
        recordedGraph,
        recoveryPacket,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    EXPECT_TRUE(recoveryAccepted);
    EXPECT_FALSE(recoveryDiscarded);
    EXPECT_FALSE(recoveryArmed);
    EXPECT_FALSE(recoveryRetiresTiming);
    EXPECT_FALSE(frameTransaction.needsRetirement());

    transaction.discardUnaccepted(graph, compiledGraph);
    ASSERT_NE(transaction.packetRuntime(prefixPacket), nullptr);
    ASSERT_NE(transaction.packetRuntime(recoveryPacket), nullptr);
    EXPECT_EQ(transaction.packetRuntime(prefixPacket)->state, GpuPacketRuntimeState::Accepted);
    EXPECT_EQ(transaction.packetRuntime(recoveryPacket)->state, GpuPacketRuntimeState::Accepted);
    EXPECT_FALSE(recoveryDiscarded);
    ASSERT_TRUE(device.waitForIdle());
    timing.collect(device, 121u);
    EXPECT_FALSE(timingSink.stats(s_FrameTransactionScope.identity).valid());

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
}

#endif


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


// Cross-frame Compute scratch must not carry the preceding frame's state for resources that the current Graphics
// prefix has already prepared. Select the private scratch state before fan-in so the current prefix remains authoritative
// for shared inputs while the Compute-only resource retains its prior layout.
TEST_F(DescriptorBufferRoundTripTest, CommandListStateHandoffSeparatesCurrentInputsFromPersistentScratch){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto sharedInput = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setCanHaveUAVs(true)
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::GraphicsAndAsyncCompute)
    );
    auto computeScratch = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setCanHaveUAVs(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(sharedInput.get(), nullptr);
    ASSERT_NE(computeScratch.get(), nullptr);

    CommandListResourceStateHandoff previousComputeState(DescriptorBufferRoundTripTest::arena());
    CommandListResourceStateHandoff currentPrefixState(DescriptorBufferRoundTripTest::arena());
    CommandListResourceStateHandoff persistentScratchState(DescriptorBufferRoundTripTest::arena());
    CommandListResourceStateHandoff nextComputeState(DescriptorBufferRoundTripTest::arena());
    auto previousCompute = device.createCommandList();
    auto currentPrefix = device.createCommandList();
    auto nextCompute = device.createCommandList();
    ASSERT_NE(previousCompute.get(), nullptr);
    ASSERT_NE(currentPrefix.get(), nullptr);
    ASSERT_NE(nextCompute.get(), nullptr);

    previousCompute->open();
    previousCompute->setBufferState(sharedInput.get(), ResourceStates::UnorderedAccess);
    previousCompute->setBufferState(computeScratch.get(), ResourceStates::UnorderedAccess);
    previousCompute->close(&previousComputeState);
    ASSERT_TRUE(previousComputeState.valid());

    currentPrefix->open();
    currentPrefix->setBufferState(sharedInput.get(), ResourceStates::ShaderResource);
    currentPrefix->close(&currentPrefixState);
    ASSERT_TRUE(currentPrefixState.valid());

    Core::Buffer* const scratchBuffers[] = { computeScratch.get() };
    ASSERT_TRUE(persistentScratchState.buildResourceSubset(
        previousComputeState,
        nullptr,
        0u,
        scratchBuffers,
        1u
    ));

    const CommandListResourceStateHandoff* branches[] = { &persistentScratchState };
    ASSERT_TRUE(nextComputeState.buildFanIn(currentPrefixState, branches, 1u));

    nextCompute->open(&nextComputeState);
    EXPECT_EQ(nextCompute->getBufferState(sharedInput.get()), ResourceStates::ShaderResource);
    EXPECT_EQ(nextCompute->getBufferState(computeScratch.get()), ResourceStates::UnorderedAccess);
    nextCompute->close();
}


// The logical AsyncCompute lane is always usable by packet code: when explicitly disabled, it resolves to Graphics,
// preserves ordered execution, and returns a Graphics timeline token rather than inventing a second queue.
TEST_F(DescriptorBufferRoundTripTest, AsyncComputeLaneRoutesToGraphicsWhenNotEnabled){
    HeadlessGraphicsScope graphicsRouteScope;
    ASSERT_TRUE(graphicsRouteScope.setAsyncComputeLaneEnabled(false));
    ASSERT_TRUE(graphicsRouteScope.initialize());

    auto& device = graphicsRouteScope.graphics().getDevice();
    EXPECT_EQ(device.resolveRenderLane(RenderLane::AsyncCompute), CommandQueue::Graphics);
    EXPECT_FALSE(device.isRenderLaneDedicated(RenderLane::AsyncCompute));

    CommandListParameters asyncParams;
    asyncParams.setRenderLane(RenderLane::AsyncCompute);
    auto commandList = device.createCommandList(asyncParams);
    ASSERT_NE(commandList.get(), nullptr);

    commandList->open();
    commandList->close();

    CommandList* commandLists[] = { commandList.get() };
    const QueueSubmissionToken token = device.executeCommandLists(
        commandLists,
        1u,
        RenderLane::AsyncCompute,
        QueueSubmissionDesc{}
    );
    EXPECT_TRUE(token.valid());
    EXPECT_EQ(token.queue, CommandQueue::Graphics);
    EXPECT_TRUE(device.waitForIdle());
}


// A transfer-only family is optional, but when available it must be a real physical transport: Graphics uploads a
// shared buffer, Transfer copies it, and Graphics imports the result again through timeline waits. The buffers use
// concurrent Graphics/Transfer sharing, so this validates the new family set without manufacturing ownership aliases.
TEST_F(DescriptorBufferRoundTripTest, DedicatedTransferQueueCopiesConcurrentBufferRoundTrip){
    HeadlessGraphicsScope transferScope;
    ASSERT_TRUE(transferScope.setTransferQueueEnabled(true));
    if(!transferScope.initialize())
        GTEST_SKIP() << "Transfer queue: no usable dedicated-transfer headless Vulkan device on this host.";

    auto& device = transferScope.graphics().getDevice();
    if(!device.getQueue(CommandQueue::Transfer))
        GTEST_SKIP() << "Transfer queue: adapter has no dedicated transfer-only queue family.";

    EXPECT_TRUE(device.usesConcurrentQueueSharing(ResourceQueueSharing::GraphicsAndTransfer));

    static constexpr u32 s_CopyWords[] = {
        0x0347a2d1u,
        0x89abcdefu,
        0x5162f093u,
        0xc0ffee42u,
    };
    const BufferDesc sourceDesc = BufferDesc()
        .setByteSize(sizeof(s_CopyWords))
        .setInitialState(ResourceStates::Common)
        .setQueueSharing(ResourceQueueSharing::GraphicsAndTransfer)
    ;
    const BufferDesc destinationDesc = BufferDesc()
        .setByteSize(sizeof(s_CopyWords))
        .setInitialState(ResourceStates::Common)
        .setQueueSharing(ResourceQueueSharing::GraphicsAndTransfer)
        .setCpuAccess(CpuAccessMode::Read)
    ;
    auto source = device.createBuffer(sourceDesc);
    auto destination = device.createBuffer(destinationDesc);
    ASSERT_NE(source.get(), nullptr);
    ASSERT_NE(destination.get(), nullptr);

    CommandListResourceStateHandoff graphicsToTransfer(transferScope.arena());
    auto graphicsProducer = device.createCommandList();
    ASSERT_NE(graphicsProducer.get(), nullptr);
    graphicsProducer->open();
    graphicsProducer->writeBuffer(source.get(), s_CopyWords, sizeof(s_CopyWords));
    graphicsProducer->setBufferState(source.get(), ResourceStates::CopySource);
    graphicsProducer->close(&graphicsToTransfer);
    ASSERT_TRUE(graphicsToTransfer.valid());

    CommandList* graphicsProducerLists[] = { graphicsProducer.get() };
    const QueueSubmissionToken graphicsProducerToken = device.executeCommandLists(
        graphicsProducerLists,
        LengthOf(graphicsProducerLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(graphicsProducerToken.valid());
    ASSERT_EQ(graphicsProducerToken.queue, CommandQueue::Graphics);

    CommandListParameters transferParams;
    transferParams.setQueueType(CommandQueue::Transfer);
    CommandListResourceStateHandoff transferToGraphics(transferScope.arena());
    auto transferCopy = device.createCommandList(transferParams);
    ASSERT_NE(transferCopy.get(), nullptr);
    transferCopy->open(&graphicsToTransfer);
    EXPECT_EQ(transferCopy->getBufferState(source.get()), ResourceStates::CopySource);
    transferCopy->copyBuffer(destination.get(), 0u, source.get(), 0u, sizeof(s_CopyWords));
    transferCopy->close(&transferToGraphics);
    ASSERT_TRUE(transferToGraphics.valid());

    const QueueSubmissionToken transferWaits[] = { graphicsProducerToken };
    CommandList* transferCopyLists[] = { transferCopy.get() };
    const QueueSubmissionToken transferToken = device.executeCommandLists(
        transferCopyLists,
        LengthOf(transferCopyLists),
        CommandQueue::Transfer,
        QueueSubmissionDesc().setWaitTokens(transferWaits, LengthOf(transferWaits))
    );
    ASSERT_TRUE(transferToken.valid());
    ASSERT_EQ(transferToken.queue, CommandQueue::Transfer);

    auto graphicsConsumer = device.createCommandList();
    ASSERT_NE(graphicsConsumer.get(), nullptr);
    graphicsConsumer->open(&transferToGraphics);
    EXPECT_EQ(graphicsConsumer->getBufferState(destination.get()), ResourceStates::CopyDest);
    graphicsConsumer->setBufferState(destination.get(), ResourceStates::ShaderResource);
    graphicsConsumer->close();

    const QueueSubmissionToken graphicsConsumerWaits[] = { transferToken };
    CommandList* graphicsConsumerLists[] = { graphicsConsumer.get() };
    const QueueSubmissionToken graphicsConsumerToken = device.executeCommandLists(
        graphicsConsumerLists,
        LengthOf(graphicsConsumerLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc().setWaitTokens(graphicsConsumerWaits, LengthOf(graphicsConsumerWaits))
    );
    ASSERT_TRUE(graphicsConsumerToken.valid());
    ASSERT_EQ(graphicsConsumerToken.queue, CommandQueue::Graphics);
    ASSERT_TRUE(device.waitForIdle());

    const u32* const copiedWords = static_cast<const u32*>(device.mapBuffer(destination.get(), CpuAccessMode::Read));
    ASSERT_NE(copiedWords, nullptr);
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_CopyWords); ++wordIndex)
        EXPECT_EQ(copiedWords[wordIndex], s_CopyWords[wordIndex]);
    device.unmapBuffer(destination.get());
}


// Setup uploads expose only a returned resource handle, so an automatic Transfer producer must publish readiness
// onto Graphics before it returns. This deliberately submits the Graphics copy without an explicit wait token: queue
// order behind the setup bridge is the compatibility contract that keeps legacy callers and graph imports safe.
TEST_F(DescriptorBufferRoundTripTest, SetupBufferUsesDedicatedTransferAndBridgesGraphicsReadiness){
    HeadlessGraphicsScope transferScope;
    ASSERT_TRUE(transferScope.setTransferQueueEnabled(true));
    if(!transferScope.initialize())
        GTEST_SKIP() << "Setup upload Transfer route: no usable dedicated-transfer headless Vulkan device on this host.";

    auto& graphics = transferScope.graphics();
    auto& device = graphics.getDevice();
    if(!device.getQueue(CommandQueue::Transfer))
        GTEST_SKIP() << "Setup upload Transfer route: adapter has no dedicated transfer-only queue family.";

    static constexpr usize s_UploadByteSize = 1024u * 1024u;
    static constexpr usize s_UploadWordCount = s_UploadByteSize / sizeof(u32);
    Vector<u32, Alloc::GlobalArena> uploadWords(transferScope.arena());
    uploadWords.resize(s_UploadWordCount);
    for(usize wordIndex = 0u; wordIndex < s_UploadWordCount; ++wordIndex)
        uploadWords[wordIndex] = 0x9e3779b9u * static_cast<u32>(wordIndex) + 0x5a17c3e1u;

    QueueSubmissionToken uploadToken;
    Graphics::BufferSetupDesc setupDesc;
    setupDesc.bufferDesc = BufferDesc()
        .setByteSize(s_UploadByteSize)
        .setInitialState(ResourceStates::Common)
    ;
    setupDesc.data = uploadWords.data();
    setupDesc.dataSize = s_UploadByteSize;
    setupDesc.acceptedToken = &uploadToken;
    const BufferHandle source = graphics.setupBuffer(setupDesc);
    ASSERT_NE(source.get(), nullptr);
    ASSERT_TRUE(uploadToken.valid());
    ASSERT_EQ(uploadToken.queue, CommandQueue::Transfer);
    EXPECT_EQ(source->getDescription().queueSharing, ResourceQueueSharing::GraphicsAndTransfer);

    const BufferDesc destinationDesc = BufferDesc()
        .setByteSize(s_UploadByteSize)
        .setInitialState(ResourceStates::Common)
        .setCpuAccess(CpuAccessMode::Read)
    ;
    const BufferHandle destination = device.createBuffer(destinationDesc);
    ASSERT_NE(destination.get(), nullptr);

    auto graphicsCopy = device.createCommandList();
    ASSERT_NE(graphicsCopy.get(), nullptr);
    graphicsCopy->open();
    graphicsCopy->copyBuffer(destination.get(), 0u, source.get(), 0u, s_UploadByteSize);
    graphicsCopy->close();

    CommandList* graphicsCopyLists[] = { graphicsCopy.get() };
    const QueueSubmissionToken graphicsCopyToken = device.executeCommandLists(
        graphicsCopyLists,
        LengthOf(graphicsCopyLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(graphicsCopyToken.valid());
    ASSERT_EQ(graphicsCopyToken.queue, CommandQueue::Graphics);
    ASSERT_TRUE(device.waitForIdle());

    const u32* const copiedWords = static_cast<const u32*>(device.mapBuffer(destination.get(), CpuAccessMode::Read));
    ASSERT_NE(copiedWords, nullptr);
    const usize sampledWords[] = { 0u, s_UploadWordCount / 2u, s_UploadWordCount - 1u };
    for(const usize wordIndex : sampledWords)
        EXPECT_EQ(copiedWords[wordIndex], uploadWords[wordIndex]);
    device.unmapBuffer(destination.get());
}


// If no dedicated Transfer family exists, automatic sizeable setup uploads must still choose a real transport: a
// dedicated Compute queue when one is available, otherwise the established Graphics path. The accepted token makes
// this routing observable without exposing a backend-specific queue object through the public setup API.
TEST_F(DescriptorBufferRoundTripTest, SetupBufferAutomaticallyFallsBackWithoutTransfer){
    HeadlessGraphicsScope fallbackScope;
    ASSERT_TRUE(fallbackScope.setTransferQueueEnabled(false));
    if(!fallbackScope.initialize())
        GTEST_SKIP() << "Setup upload fallback: no usable headless Vulkan device on this host.";

    auto& graphics = fallbackScope.graphics();
    auto& device = graphics.getDevice();
    ASSERT_EQ(device.getQueue(CommandQueue::Transfer), nullptr);

    static constexpr usize s_UploadByteSize = 1024u * 1024u;
    Vector<u8, Alloc::GlobalArena> uploadBytes(fallbackScope.arena());
    uploadBytes.resize(s_UploadByteSize);
    for(usize byteIndex = 0u; byteIndex < s_UploadByteSize; ++byteIndex)
        uploadBytes[byteIndex] = static_cast<u8>(byteIndex);

    QueueSubmissionToken uploadToken;
    Graphics::BufferSetupDesc setupDesc;
    setupDesc.bufferDesc = BufferDesc()
        .setByteSize(s_UploadByteSize)
        .setInitialState(ResourceStates::Common)
    ;
    setupDesc.data = uploadBytes.data();
    setupDesc.dataSize = s_UploadByteSize;
    setupDesc.acceptedToken = &uploadToken;
    const BufferHandle uploaded = graphics.setupBuffer(setupDesc);
    ASSERT_NE(uploaded.get(), nullptr);
    ASSERT_TRUE(uploadToken.valid());

    const CommandQueue::Enum expectedQueue = device.getQueue(CommandQueue::Compute)
        ? CommandQueue::Compute
        : CommandQueue::Graphics
    ;
    EXPECT_EQ(uploadToken.queue, expectedQueue);
    EXPECT_EQ(
        uploaded->getDescription().queueSharing,
        expectedQueue == CommandQueue::Compute
            ? ResourceQueueSharing::GraphicsAndAsyncCompute
            : ResourceQueueSharing::Exclusive
    );
    EXPECT_TRUE(device.waitForIdle());
}


// Exercise the texture path through the same automatic resolver. A one-mip 512x512 RGBA texture is deliberately
// large enough to cross the automatic-transfer threshold; with Transfer disabled it must use Compute when present
// or retain Graphics, while publishing the requested ShaderResource state before the setup call returns.
TEST_F(DescriptorBufferRoundTripTest, SetupTextureAutomaticallyFallsBackWithoutTransfer){
    HeadlessGraphicsScope fallbackScope;
    ASSERT_TRUE(fallbackScope.setTransferQueueEnabled(false));
    if(!fallbackScope.initialize())
        GTEST_SKIP() << "Setup texture fallback: no usable headless Vulkan device on this host.";

    auto& graphics = fallbackScope.graphics();
    auto& device = graphics.getDevice();
    ASSERT_EQ(device.getQueue(CommandQueue::Transfer), nullptr);

    static constexpr u32 s_TextureWidth = 512u;
    static constexpr u32 s_TextureHeight = 512u;
    static constexpr usize s_UploadByteSize = static_cast<usize>(s_TextureWidth) * s_TextureHeight * 4u;
    Vector<u8, Alloc::GlobalArena> uploadBytes(fallbackScope.arena());
    uploadBytes.resize(s_UploadByteSize);
    for(usize byteIndex = 0u; byteIndex < s_UploadByteSize; ++byteIndex)
        uploadBytes[byteIndex] = static_cast<u8>(byteIndex * 17u);

    QueueSubmissionToken uploadToken;
    Graphics::TextureSetupDesc setupDesc;
    setupDesc.textureDesc = TextureDesc()
        .setWidth(s_TextureWidth)
        .setHeight(s_TextureHeight)
        .setFormat(Format::RGBA8_UNORM)
        .setInitialState(ResourceStates::ShaderResource)
    ;
    setupDesc.data = uploadBytes.data();
    setupDesc.uploadDataSize = s_UploadByteSize;
    setupDesc.acceptedToken = &uploadToken;
    const TextureHandle uploaded = graphics.setupTexture(setupDesc);
    ASSERT_NE(uploaded.get(), nullptr);
    ASSERT_TRUE(uploadToken.valid());

    const CommandQueue::Enum expectedQueue = device.getQueue(CommandQueue::Compute)
        ? CommandQueue::Compute
        : CommandQueue::Graphics
    ;
    EXPECT_EQ(uploadToken.queue, expectedQueue);
    EXPECT_EQ(
        uploaded->getDescription().queueSharing,
        expectedQueue == CommandQueue::Compute
            ? ResourceQueueSharing::GraphicsAndAsyncCompute
            : ResourceQueueSharing::Exclusive
    );
    EXPECT_EQ(uploaded->getDescription().initialState, ResourceStates::ShaderResource);
    EXPECT_TRUE(device.waitForIdle());
}


// A dedicated compute family is optional in CI, but when one exists this is the phase-zero ownership proof:
// exclusive storage moves Compute -> Graphics -> Compute with paired release/acquire barriers and submission-local
// timeline tokens. No rendering job has moved yet; this specifically validates the resource-lifecycle round trip
// that a reused shadow-visibility frame slot will need.
TEST_F(DescriptorBufferRoundTripTest, AsyncComputeLaneTransfersExclusiveBufferOwnershipRoundTrip){
    HeadlessGraphicsScope asyncScope;
    ASSERT_TRUE(asyncScope.setAsyncComputeLaneEnabled(true));
    if(!asyncScope.initialize())
        GTEST_SKIP() << "Async-compute lane: no usable dedicated-compute headless Vulkan device on this host.";

    auto& device = asyncScope.graphics().getDevice();
    if(!device.isRenderLaneDedicated(RenderLane::AsyncCompute))
        GTEST_SKIP() << "Async-compute lane: adapter has no dedicated compute-only queue family.";

    auto buffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveUAVs(true)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(buffer.get(), nullptr);
    auto sharedInput = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveUAVs(true)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::GraphicsAndAsyncCompute)
    );
    ASSERT_NE(sharedInput.get(), nullptr);

    CommandListParameters computeParams;
    computeParams.setRenderLane(RenderLane::AsyncCompute);

    CommandListResourceStateHandoff computeToGraphics(asyncScope.arena());
    auto computeProducer = device.createCommandList(computeParams);
    ASSERT_NE(computeProducer.get(), nullptr);
    computeProducer->open();
    computeProducer->setBufferState(buffer.get(), ResourceStates::UnorderedAccess);
    computeProducer->setBufferState(sharedInput.get(), ResourceStates::UnorderedAccess);
    computeProducer->releaseBufferOwnership(buffer.get(), RenderLane::Graphics);
    computeProducer->close(&computeToGraphics);
    ASSERT_TRUE(computeToGraphics.valid());

    CommandList* computeProducerLists[] = { computeProducer.get() };
    const QueueSubmissionToken computeToken = device.executeCommandLists(
        computeProducerLists,
        1u,
        RenderLane::AsyncCompute,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(computeToken.valid());
    ASSERT_EQ(computeToken.queue, CommandQueue::Compute);

    CommandListResourceStateHandoff graphicsToCompute(asyncScope.arena());
    auto graphicsConsumer = device.createCommandList();
    ASSERT_NE(graphicsConsumer.get(), nullptr);
    graphicsConsumer->open(&computeToGraphics);
    EXPECT_EQ(graphicsConsumer->getBufferState(buffer.get()), ResourceStates::UnorderedAccess);
    EXPECT_EQ(graphicsConsumer->getBufferState(sharedInput.get()), ResourceStates::UnorderedAccess);
    graphicsConsumer->setBufferState(buffer.get(), ResourceStates::ShaderResource);
    graphicsConsumer->setBufferState(sharedInput.get(), ResourceStates::ShaderResource);
    graphicsConsumer->releaseBufferOwnership(buffer.get(), RenderLane::AsyncCompute);
    graphicsConsumer->close(&graphicsToCompute);
    ASSERT_TRUE(graphicsToCompute.valid());

    // Passing both producer tokens from one physical queue is legal API use. The submission folds them into the
    // single greatest timeline wait Vulkan permits for that semaphore.
    const QueueSubmissionToken computeWaits[] = { computeToken, computeToken };
    const QueueSubmissionDesc graphicsSubmissionDesc = QueueSubmissionDesc().setWaitTokens(computeWaits, 2u);
    CommandList* graphicsConsumerLists[] = { graphicsConsumer.get() };
    const QueueSubmissionToken graphicsToken = device.executeCommandLists(
        graphicsConsumerLists,
        1u,
        RenderLane::Graphics,
        graphicsSubmissionDesc
    );
    ASSERT_TRUE(graphicsToken.valid());
    ASSERT_EQ(graphicsToken.queue, CommandQueue::Graphics);

    auto computeReuse = device.createCommandList(computeParams);
    ASSERT_NE(computeReuse.get(), nullptr);
    computeReuse->open(&graphicsToCompute);
    EXPECT_EQ(computeReuse->getBufferState(buffer.get()), ResourceStates::ShaderResource);
    computeReuse->setBufferState(buffer.get(), ResourceStates::UnorderedAccess);
    computeReuse->close();

    const QueueSubmissionToken graphicsWaits[] = { graphicsToken };
    const QueueSubmissionDesc computeSubmissionDesc = QueueSubmissionDesc().setWaitTokens(graphicsWaits, 1u);
    CommandList* computeReuseLists[] = { computeReuse.get() };
    const QueueSubmissionToken reuseToken = device.executeCommandLists(
        computeReuseLists,
        1u,
        RenderLane::AsyncCompute,
        computeSubmissionDesc
    );
    EXPECT_TRUE(reuseToken.valid());
    EXPECT_EQ(reuseToken.queue, CommandQueue::Compute);
    EXPECT_TRUE(device.waitForIdle());
}


// AVBOIT alternates raster and compute phases. Its shared work resources use concurrent queue sharing, while timeline
// waits order the exact Graphics pre -> Compute warp -> Graphics extinction -> Compute integration -> Graphics
// accumulation chain. Exercise the state subsets/fan-ins and every lane crossing on a real dedicated family.
TEST_F(DescriptorBufferRoundTripTest, AsyncComputeLaneChainsConcurrentAvboitWorkStates){
    HeadlessGraphicsScope asyncScope;
    ASSERT_TRUE(asyncScope.setAsyncComputeLaneEnabled(true));
    if(!asyncScope.initialize())
        GTEST_SKIP() << "Async-compute lane: no usable dedicated-compute headless Vulkan device on this host.";

    auto& device = asyncScope.graphics().getDevice();
    if(!device.isRenderLaneDedicated(RenderLane::AsyncCompute))
        GTEST_SKIP() << "Async-compute lane: adapter has no dedicated compute-only queue family.";

    const auto makeSharedWorkBuffer = [&device](){
        return device.createBuffer(
            BufferDesc()
                .setByteSize(256u)
                .setCanHaveUAVs(true)
                .setCanHaveRawViews(true)
                .setInitialState(ResourceStates::Common)
                .setQueueSharing(ResourceQueueSharing::GraphicsAndAsyncCompute)
        );
    };
    auto coverage = makeSharedWorkBuffer();
    auto depthWarp = makeSharedWorkBuffer();
    auto control = makeSharedWorkBuffer();
    auto extinction = makeSharedWorkBuffer();
    auto extinctionOverflow = makeSharedWorkBuffer();
    auto transmittance = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setDepth(4u)
            .setDimension(TextureDimension::Texture3D)
            .setFormat(Format::RGBA8_UNORM)
            .setInUAV(true)
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::GraphicsAndAsyncCompute)
    );
    ASSERT_NE(coverage.get(), nullptr);
    ASSERT_NE(depthWarp.get(), nullptr);
    ASSERT_NE(control.get(), nullptr);
    ASSERT_NE(extinction.get(), nullptr);
    ASSERT_NE(extinctionOverflow.get(), nullptr);
    ASSERT_NE(transmittance.get(), nullptr);

    CommandListParameters computeParams;
    computeParams.setRenderLane(RenderLane::AsyncCompute);
    auto graphicsPre = device.createCommandList();
    auto computeWarp = device.createCommandList(computeParams);
    auto graphicsExtinction = device.createCommandList();
    auto computeIntegration = device.createCommandList(computeParams);
    auto graphicsAccumulate = device.createCommandList();
    ASSERT_NE(graphicsPre.get(), nullptr);
    ASSERT_NE(computeWarp.get(), nullptr);
    ASSERT_NE(graphicsExtinction.get(), nullptr);
    ASSERT_NE(computeIntegration.get(), nullptr);
    ASSERT_NE(graphicsAccumulate.get(), nullptr);

    CommandListResourceStateHandoff preState(asyncScope.arena());
    CommandListResourceStateHandoff warpInputState(asyncScope.arena());
    CommandListResourceStateHandoff warpState(asyncScope.arena());
    CommandListResourceStateHandoff extinctionInputState(asyncScope.arena());
    CommandListResourceStateHandoff extinctionState(asyncScope.arena());
    CommandListResourceStateHandoff integrationInputState(asyncScope.arena());
    CommandListResourceStateHandoff integrationState(asyncScope.arena());
    CommandListResourceStateHandoff accumulateInputState(asyncScope.arena());
    CommandListResourceStateHandoff finalState(asyncScope.arena());

    graphicsPre->open();
    graphicsPre->setBufferState(coverage.get(), ResourceStates::UnorderedAccess);
    graphicsPre->setBufferState(depthWarp.get(), ResourceStates::CopyDest);
    graphicsPre->setBufferState(control.get(), ResourceStates::CopyDest);
    graphicsPre->setBufferState(extinction.get(), ResourceStates::CopyDest);
    graphicsPre->setBufferState(extinctionOverflow.get(), ResourceStates::CopyDest);
    graphicsPre->setTextureState(transmittance.get(), s_AllSubresources, ResourceStates::CopyDest);
    graphicsPre->close(&preState);
    ASSERT_TRUE(preState.valid());

    Core::Buffer* const warpBuffers[] = { coverage.get(), depthWarp.get(), control.get() };
    ASSERT_TRUE(warpInputState.buildResourceSubset(preState, nullptr, 0u, warpBuffers, 3u));
    computeWarp->open(&warpInputState);
    EXPECT_EQ(computeWarp->getBufferState(coverage.get()), ResourceStates::UnorderedAccess);
    computeWarp->setBufferState(coverage.get(), ResourceStates::ShaderResource);
    computeWarp->setBufferState(depthWarp.get(), ResourceStates::UnorderedAccess);
    computeWarp->setBufferState(control.get(), ResourceStates::UnorderedAccess);
    computeWarp->close(&warpState);
    ASSERT_TRUE(warpState.valid());

    const CommandListResourceStateHandoff* const extinctionBranches[] = { &warpState };
    ASSERT_TRUE(extinctionInputState.buildFanIn(preState, extinctionBranches, 1u));
    graphicsExtinction->open(&extinctionInputState);
    EXPECT_EQ(graphicsExtinction->getBufferState(depthWarp.get()), ResourceStates::UnorderedAccess);
    EXPECT_EQ(graphicsExtinction->getBufferState(control.get()), ResourceStates::UnorderedAccess);
    graphicsExtinction->setBufferState(depthWarp.get(), ResourceStates::ShaderResource);
    graphicsExtinction->setBufferState(control.get(), ResourceStates::ShaderResource);
    graphicsExtinction->setBufferState(extinction.get(), ResourceStates::UnorderedAccess);
    graphicsExtinction->setBufferState(extinctionOverflow.get(), ResourceStates::UnorderedAccess);
    graphicsExtinction->close(&extinctionState);
    ASSERT_TRUE(extinctionState.valid());

    Core::Texture* const integrationTextures[] = { transmittance.get() };
    Core::Buffer* const integrationBuffers[] = { extinction.get(), control.get(), extinctionOverflow.get() };
    ASSERT_TRUE(integrationInputState.buildResourceSubset(
        extinctionState,
        integrationTextures,
        1u,
        integrationBuffers,
        3u
    ));
    computeIntegration->open(&integrationInputState);
    EXPECT_EQ(computeIntegration->getBufferState(extinction.get()), ResourceStates::UnorderedAccess);
    EXPECT_EQ(computeIntegration->getTextureSubresourceState(transmittance.get(), 0u, 0u), ResourceStates::CopyDest);
    computeIntegration->setBufferState(extinction.get(), ResourceStates::ShaderResource);
    computeIntegration->setBufferState(control.get(), ResourceStates::ShaderResource);
    computeIntegration->setBufferState(extinctionOverflow.get(), ResourceStates::ShaderResource);
    computeIntegration->setTextureState(transmittance.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    computeIntegration->close(&integrationState);
    ASSERT_TRUE(integrationState.valid());

    const CommandListResourceStateHandoff* const accumulateBranches[] = { &integrationState };
    ASSERT_TRUE(accumulateInputState.buildFanIn(extinctionState, accumulateBranches, 1u));
    graphicsAccumulate->open(&accumulateInputState);
    EXPECT_EQ(graphicsAccumulate->getBufferState(depthWarp.get()), ResourceStates::ShaderResource);
    EXPECT_EQ(graphicsAccumulate->getTextureSubresourceState(transmittance.get(), 0u, 0u), ResourceStates::UnorderedAccess);
    graphicsAccumulate->setTextureState(transmittance.get(), s_AllSubresources, ResourceStates::ShaderResource);
    graphicsAccumulate->close(&finalState);
    ASSERT_TRUE(finalState.valid());

    CommandList* graphicsPreLists[] = { graphicsPre.get() };
    const QueueSubmissionToken graphicsPreToken = device.executeCommandLists(
        graphicsPreLists,
        1u,
        RenderLane::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(graphicsPreToken.valid());

    const QueueSubmissionDesc warpSubmitDesc = QueueSubmissionDesc().setWaitTokens(&graphicsPreToken, 1u);
    CommandList* computeWarpLists[] = { computeWarp.get() };
    const QueueSubmissionToken warpToken = device.executeCommandLists(
        computeWarpLists,
        1u,
        RenderLane::AsyncCompute,
        warpSubmitDesc
    );
    ASSERT_TRUE(warpToken.valid());

    const QueueSubmissionDesc extinctionSubmitDesc = QueueSubmissionDesc().setWaitTokens(&warpToken, 1u);
    CommandList* graphicsExtinctionLists[] = { graphicsExtinction.get() };
    const QueueSubmissionToken extinctionToken = device.executeCommandLists(
        graphicsExtinctionLists,
        1u,
        RenderLane::Graphics,
        extinctionSubmitDesc
    );
    ASSERT_TRUE(extinctionToken.valid());

    const QueueSubmissionDesc integrationSubmitDesc = QueueSubmissionDesc().setWaitTokens(&extinctionToken, 1u);
    CommandList* computeIntegrationLists[] = { computeIntegration.get() };
    const QueueSubmissionToken integrationToken = device.executeCommandLists(
        computeIntegrationLists,
        1u,
        RenderLane::AsyncCompute,
        integrationSubmitDesc
    );
    ASSERT_TRUE(integrationToken.valid());

    const QueueSubmissionDesc accumulationSubmitDesc = QueueSubmissionDesc().setWaitTokens(&integrationToken, 1u);
    CommandList* graphicsAccumulateLists[] = { graphicsAccumulate.get() };
    const QueueSubmissionToken accumulationToken = device.executeCommandLists(
        graphicsAccumulateLists,
        1u,
        RenderLane::Graphics,
        accumulationSubmitDesc
    );
    EXPECT_TRUE(accumulationToken.valid());
    EXPECT_TRUE(device.waitForIdle());
}


// Hardware caustics can produce the resolved irradiance on Graphics while deferred lighting and the optional history
// stash run on AsyncCompute. The output is intentionally concurrent, so timeline dependencies are sufficient and no
// exclusive ownership release is required. Exercise both the bootstrap (lighting supplies the stash source) and the
// active lagged path (the current Graphics producer supplies it directly).
TEST_F(DescriptorBufferRoundTripTest, AsyncComputeLaneLetsGraphicsCausticsFeedLightingAndLaggedStashThroughConcurrentIrradiance){
    HeadlessGraphicsScope asyncScope;
    ASSERT_TRUE(asyncScope.setAsyncComputeLaneEnabled(true));
    if(!asyncScope.initialize())
        GTEST_SKIP() << "Async-compute lane: no usable dedicated-compute headless Vulkan device on this host.";

    auto& device = asyncScope.graphics().getDevice();
    if(!device.isRenderLaneDedicated(RenderLane::AsyncCompute))
        GTEST_SKIP() << "Async-compute lane: adapter has no dedicated compute-only queue family.";

    auto causticIrradiance = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::RGBA8_UNORM)
            .setInUAV(true)
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::GraphicsAndAsyncCompute)
    );
    ASSERT_NE(causticIrradiance.get(), nullptr);
    auto causticHistory = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::RGBA8_UNORM)
            .setInUAV(true)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
            .setQueueSharing(ResourceQueueSharing::GraphicsAndAsyncCompute)
    );
    ASSERT_NE(causticHistory.get(), nullptr);

    CommandListParameters computeParams;
    computeParams.setRenderLane(RenderLane::AsyncCompute);
    auto graphicsCaustics = device.createCommandList();
    auto asyncLighting = device.createCommandList(computeParams);
    auto bootstrapFinal = device.createCommandList();
    auto bootstrapStash = device.createCommandList(computeParams);
    auto activeGraphicsCaustics = device.createCommandList();
    auto activeStash = device.createCommandList(computeParams);
    ASSERT_NE(graphicsCaustics.get(), nullptr);
    ASSERT_NE(asyncLighting.get(), nullptr);
    ASSERT_NE(bootstrapFinal.get(), nullptr);
    ASSERT_NE(bootstrapStash.get(), nullptr);
    ASSERT_NE(activeGraphicsCaustics.get(), nullptr);
    ASSERT_NE(activeStash.get(), nullptr);

    CommandListResourceStateHandoff causticsState(asyncScope.arena());
    CommandListResourceStateHandoff lightingState(asyncScope.arena());
    CommandListResourceStateHandoff bootstrapCausticReturnState(asyncScope.arena());
    CommandListResourceStateHandoff bootstrapStashState(asyncScope.arena());
    CommandListResourceStateHandoff activeCausticsState(asyncScope.arena());
    CommandListResourceStateHandoff activeStashState(asyncScope.arena());
    graphicsCaustics->open();
    graphicsCaustics->setTextureState(
        causticIrradiance.get(),
        s_AllSubresources,
        ResourceStates::UnorderedAccess
    );
    graphicsCaustics->setTextureState(
        causticIrradiance.get(),
        s_AllSubresources,
        ResourceStates::ShaderResource
    );
    graphicsCaustics->close(&causticsState);
    ASSERT_TRUE(causticsState.valid());

    asyncLighting->open(&causticsState);
    ASSERT_TRUE(asyncLighting->hasCommandBuffer());
    EXPECT_EQ(
        asyncLighting->getTextureSubresourceState(causticIrradiance.get(), 0u, 0u),
        ResourceStates::ShaderResource
    );
    asyncLighting->close(&lightingState);
    ASSERT_TRUE(lightingState.valid());
    ASSERT_TRUE(bootstrapCausticReturnState.buildTextureSubset(
        lightingState,
        causticIrradiance.get()
    ));

    // Bootstrap consumes the live image in Async lighting, so the stash imports the post-lighting state.
    bootstrapFinal->open();
    bootstrapFinal->close();
    ASSERT_TRUE(bootstrapFinal->hasCommandBuffer());
    bootstrapStash->open(&bootstrapCausticReturnState);
    EXPECT_EQ(
        bootstrapStash->getTextureSubresourceState(causticIrradiance.get(), 0u, 0u),
        ResourceStates::ShaderResource
    );
    bootstrapStash->setTextureState(causticIrradiance.get(), s_AllSubresources, ResourceStates::CopySource);
    bootstrapStash->setTextureState(causticHistory.get(), s_AllSubresources, ResourceStates::CopyDest);
    bootstrapStash->commitBarriers();
    const TextureSlice slice;
    bootstrapStash->copyTexture(causticHistory.get(), slice, causticIrradiance.get(), slice);
    bootstrapStash->close(&bootstrapStashState);
    ASSERT_TRUE(bootstrapStashState.valid());

    // Once history is accepted, Graphics lighting uses the immutable prior-frame image. The live caustic result now
    // comes from the current Graphics producer, so the next stash must import that state rather than the old
    // Async-lighting return state.
    activeGraphicsCaustics->open();
    activeGraphicsCaustics->setTextureState(
        causticIrradiance.get(),
        s_AllSubresources,
        ResourceStates::UnorderedAccess
    );
    activeGraphicsCaustics->setTextureState(
        causticIrradiance.get(),
        s_AllSubresources,
        ResourceStates::ShaderResource
    );
    activeGraphicsCaustics->close(&activeCausticsState);
    ASSERT_TRUE(activeCausticsState.valid());

    activeStash->open(&activeCausticsState);
    EXPECT_EQ(
        activeStash->getTextureSubresourceState(causticIrradiance.get(), 0u, 0u),
        ResourceStates::ShaderResource
    );
    activeStash->setTextureState(causticIrradiance.get(), s_AllSubresources, ResourceStates::CopySource);
    activeStash->setTextureState(causticHistory.get(), s_AllSubresources, ResourceStates::CopyDest);
    activeStash->commitBarriers();
    activeStash->copyTexture(causticHistory.get(), slice, causticIrradiance.get(), slice);
    activeStash->close(&activeStashState);
    ASSERT_TRUE(activeStashState.valid());

    CommandList* graphicsLists[] = { graphicsCaustics.get() };
    const QueueSubmissionToken graphicsToken = device.executeCommandLists(
        graphicsLists,
        1u,
        RenderLane::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(graphicsToken.valid());

    const QueueSubmissionDesc lightingSubmitDesc = QueueSubmissionDesc().setWaitTokens(&graphicsToken, 1u);
    CommandList* lightingLists[] = { asyncLighting.get() };
    const QueueSubmissionToken lightingToken = device.executeCommandLists(
        lightingLists,
        1u,
        RenderLane::AsyncCompute,
        lightingSubmitDesc
    );
    EXPECT_TRUE(lightingToken.valid());

    const QueueSubmissionDesc bootstrapFinalSubmitDesc = QueueSubmissionDesc().setWaitTokens(&lightingToken, 1u);
    CommandList* bootstrapFinalLists[] = { bootstrapFinal.get() };
    const QueueSubmissionToken bootstrapFinalToken = device.executeCommandLists(
        bootstrapFinalLists,
        1u,
        RenderLane::Graphics,
        bootstrapFinalSubmitDesc
    );
    ASSERT_TRUE(bootstrapFinalToken.valid());

    const QueueSubmissionDesc bootstrapStashSubmitDesc = QueueSubmissionDesc().setWaitTokens(&bootstrapFinalToken, 1u);
    CommandList* bootstrapStashLists[] = { bootstrapStash.get() };
    const QueueSubmissionToken bootstrapStashToken = device.executeCommandLists(
        bootstrapStashLists,
        1u,
        RenderLane::AsyncCompute,
        bootstrapStashSubmitDesc
    );
    ASSERT_TRUE(bootstrapStashToken.valid());

    // This wait is the active-lagged plan's explicit cross-frame protection: do not overwrite the live image until
    // the previous Async history copy has stopped reading it.
    const QueueSubmissionDesc activeCausticsSubmitDesc = QueueSubmissionDesc().setWaitTokens(&bootstrapStashToken, 1u);
    CommandList* activeCausticsLists[] = { activeGraphicsCaustics.get() };
    const QueueSubmissionToken activeCausticsToken = device.executeCommandLists(
        activeCausticsLists,
        1u,
        RenderLane::Graphics,
        activeCausticsSubmitDesc
    );
    ASSERT_TRUE(activeCausticsToken.valid());

    const QueueSubmissionDesc activeStashSubmitDesc = QueueSubmissionDesc().setWaitTokens(&activeCausticsToken, 1u);
    CommandList* activeStashLists[] = { activeStash.get() };
    const QueueSubmissionToken activeStashToken = device.executeCommandLists(
        activeStashLists,
        1u,
        RenderLane::AsyncCompute,
        activeStashSubmitDesc
    );
    EXPECT_TRUE(activeStashToken.valid());
    EXPECT_TRUE(device.waitForIdle());
}


// Deferred lighting and the logical composite now join the dedicated Compute lane after AVBOIT. The exclusive
// shadow, caustic, and surfel outputs therefore remain Compute-local through their only consumer; Graphics imports only the linear
// composite image for presentation. This exercises the narrow handoffs and verifies that no Graphics acquire/release
// is needed before the next Compute reuse of shadow/caustic/surfel outputs.
TEST_F(DescriptorBufferRoundTripTest, AsyncComputeLaneKeepsDeferredLightingAndCompositeOnComputeUntilPresent){
    HeadlessGraphicsScope asyncScope;
    ASSERT_TRUE(asyncScope.setAsyncComputeLaneEnabled(true));
    if(!asyncScope.initialize())
        GTEST_SKIP() << "Async-compute lane: no usable dedicated-compute headless Vulkan device on this host.";

    auto& device = asyncScope.graphics().getDevice();
    if(!device.isRenderLaneDedicated(RenderLane::AsyncCompute))
        GTEST_SKIP() << "Async-compute lane: adapter has no dedicated compute-only queue family.";

    auto gbuffer = CreateConcurrentTestTexture(device);
    auto opaqueColor = CreateConcurrentTestTexture(device, true);
    auto compositeColor = CreateConcurrentTestTexture(device, true);
    auto avboitColor = CreateConcurrentTestTexture(device);
    auto avboitExtinction = CreateConcurrentTestTexture(device);
    auto shadowVisibility = CreateExclusiveRayOutputTestTexture(device);
    auto causticIrradiance = CreateExclusiveRayOutputTestTexture(device);
    auto surfelIrradiance = CreateExclusiveRayOutputTestTexture(device);
    auto slotsBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::GraphicsAndAsyncCompute)
    );
    ASSERT_NE(gbuffer.get(), nullptr);
    ASSERT_NE(opaqueColor.get(), nullptr);
    ASSERT_NE(compositeColor.get(), nullptr);
    ASSERT_NE(avboitColor.get(), nullptr);
    ASSERT_NE(avboitExtinction.get(), nullptr);
    ASSERT_NE(shadowVisibility.get(), nullptr);
    ASSERT_NE(causticIrradiance.get(), nullptr);
    ASSERT_NE(surfelIrradiance.get(), nullptr);
    ASSERT_NE(slotsBuffer.get(), nullptr);

    CommandListParameters computeParams;
    computeParams.setRenderLane(RenderLane::AsyncCompute);
    auto prefix = device.createCommandList();
    auto rayEffects = device.createCommandList(computeParams);
    auto avboit = device.createCommandList();
    auto lighting = device.createCommandList(computeParams);
    auto composite = device.createCommandList(computeParams);
    auto present = device.createCommandList();
    auto computeReuse = device.createCommandList(computeParams);
    ASSERT_NE(prefix.get(), nullptr);
    ASSERT_NE(rayEffects.get(), nullptr);
    ASSERT_NE(avboit.get(), nullptr);
    ASSERT_NE(lighting.get(), nullptr);
    ASSERT_NE(composite.get(), nullptr);
    ASSERT_NE(present.get(), nullptr);
    ASSERT_NE(computeReuse.get(), nullptr);

    CommandListResourceStateHandoff prefixState(asyncScope.arena());
    CommandListResourceStateHandoff rayEffectsState(asyncScope.arena());
    CommandListResourceStateHandoff shadowLightingState(asyncScope.arena());
    CommandListResourceStateHandoff causticLightingState(asyncScope.arena());
    CommandListResourceStateHandoff surfelLightingState(asyncScope.arena());
    CommandListResourceStateHandoff avboitState(asyncScope.arena());
    CommandListResourceStateHandoff lightingBaseState(asyncScope.arena());
    CommandListResourceStateHandoff avboitLightingState(asyncScope.arena());
    CommandListResourceStateHandoff lightingInputState(asyncScope.arena());
    CommandListResourceStateHandoff lightingState(asyncScope.arena());
    CommandListResourceStateHandoff opaqueCompositeState(asyncScope.arena());
    CommandListResourceStateHandoff avboitCompositeState(asyncScope.arena());
    CommandListResourceStateHandoff compositeBaseState(asyncScope.arena());
    CommandListResourceStateHandoff compositeInputState(asyncScope.arena());
    CommandListResourceStateHandoff compositeState(asyncScope.arena());
    CommandListResourceStateHandoff compositePresentState(asyncScope.arena());
    CommandListResourceStateHandoff presentBaseState(asyncScope.arena());
    CommandListResourceStateHandoff presentInputState(asyncScope.arena());
    CommandListResourceStateHandoff presentState(asyncScope.arena());
    CommandListResourceStateHandoff shadowReturnState(asyncScope.arena());
    CommandListResourceStateHandoff causticReturnState(asyncScope.arena());
    CommandListResourceStateHandoff surfelReturnState(asyncScope.arena());
    CommandListResourceStateHandoff computeReuseInputState(asyncScope.arena());

    prefix->open();
    prefix->setTextureState(gbuffer.get(), s_AllSubresources, ResourceStates::ShaderResource);
    prefix->setTextureState(opaqueColor.get(), s_AllSubresources, ResourceStates::CopyDest);
    prefix->setBufferState(slotsBuffer.get(), ResourceStates::ConstantBuffer);
    prefix->close(&prefixState);
    ASSERT_TRUE(prefixState.valid());

    rayEffects->open(&prefixState);
    rayEffects->setTextureState(gbuffer.get(), s_AllSubresources, ResourceStates::ShaderResource);
    rayEffects->setBufferState(slotsBuffer.get(), ResourceStates::ConstantBuffer);
    rayEffects->setTextureState(shadowVisibility.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    rayEffects->setTextureState(causticIrradiance.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    rayEffects->setTextureState(surfelIrradiance.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    rayEffects->setTextureState(shadowVisibility.get(), s_AllSubresources, ResourceStates::ShaderResource);
    rayEffects->setTextureState(causticIrradiance.get(), s_AllSubresources, ResourceStates::ShaderResource);
    rayEffects->setTextureState(surfelIrradiance.get(), s_AllSubresources, ResourceStates::ShaderResource);
    rayEffects->close(&rayEffectsState);
    ASSERT_TRUE(rayEffectsState.valid());
    ASSERT_TRUE(shadowLightingState.buildTextureSubset(rayEffectsState, shadowVisibility.get()));
    ASSERT_TRUE(causticLightingState.buildTextureSubset(rayEffectsState, causticIrradiance.get()));
    ASSERT_TRUE(surfelLightingState.buildTextureSubset(rayEffectsState, surfelIrradiance.get()));

    avboit->open(&prefixState);
    avboit->setTextureState(gbuffer.get(), s_AllSubresources, ResourceStates::ShaderResource);
    avboit->setTextureState(avboitColor.get(), s_AllSubresources, ResourceStates::ShaderResource);
    avboit->setTextureState(avboitExtinction.get(), s_AllSubresources, ResourceStates::ShaderResource);
    avboit->close(&avboitState);
    ASSERT_TRUE(avboitState.valid());

    Texture* const lightingBaseTextures[] = { gbuffer.get(), opaqueColor.get() };
    Buffer* const lightingBaseBuffers[] = { slotsBuffer.get() };
    ASSERT_TRUE(lightingBaseState.buildResourceSubset(
        prefixState,
        lightingBaseTextures,
        2u,
        lightingBaseBuffers,
        1u
    ));
    Texture* const avboitLightingTextures[] = { gbuffer.get() };
    ASSERT_TRUE(avboitLightingState.buildResourceSubset(
        avboitState,
        avboitLightingTextures,
        1u,
        nullptr,
        0u
    ));
    const CommandListResourceStateHandoff* const lightingBranches[] = {
        &shadowLightingState,
        &causticLightingState,
        &surfelLightingState,
        &avboitLightingState,
    };
    ASSERT_TRUE(lightingInputState.buildFanIn(lightingBaseState, lightingBranches, 4u));

    lighting->open(&lightingInputState);
    EXPECT_EQ(lighting->getTextureSubresourceState(gbuffer.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(lighting->getTextureSubresourceState(shadowVisibility.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(lighting->getTextureSubresourceState(causticIrradiance.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(lighting->getTextureSubresourceState(surfelIrradiance.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(lighting->getTextureSubresourceState(opaqueColor.get(), 0u, 0u), ResourceStates::CopyDest);
    lighting->setTextureState(opaqueColor.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    lighting->close(&lightingState);
    ASSERT_TRUE(lightingState.valid());
    ASSERT_TRUE(opaqueCompositeState.buildTextureSubset(lightingState, opaqueColor.get()));

    Texture* const compositeBaseTextures[] = { avboitColor.get(), avboitExtinction.get() };
    ASSERT_TRUE(avboitCompositeState.buildResourceSubset(
        avboitState,
        compositeBaseTextures,
        2u,
        nullptr,
        0u
    ));
    Buffer* const compositeBaseBuffers[] = { slotsBuffer.get() };
    ASSERT_TRUE(compositeBaseState.buildResourceSubset(
        lightingBaseState,
        nullptr,
        0u,
        compositeBaseBuffers,
        1u
    ));
    const CommandListResourceStateHandoff* const compositeBranches[] = {
        &avboitCompositeState,
        &opaqueCompositeState,
    };
    ASSERT_TRUE(compositeInputState.buildFanIn(compositeBaseState, compositeBranches, 2u));

    composite->open(&compositeInputState);
    EXPECT_EQ(composite->getTextureSubresourceState(opaqueColor.get(), 0u, 0u), ResourceStates::Common);
    EXPECT_EQ(composite->getTextureSubresourceState(avboitColor.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(composite->getTextureSubresourceState(shadowVisibility.get(), 0u, 0u), ResourceStates::Unknown);
    composite->setTextureState(opaqueColor.get(), s_AllSubresources, ResourceStates::ShaderResource);
    composite->setTextureState(avboitColor.get(), s_AllSubresources, ResourceStates::ShaderResource);
    composite->setTextureState(avboitExtinction.get(), s_AllSubresources, ResourceStates::ShaderResource);
    composite->setTextureState(compositeColor.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    composite->close(&compositeState);
    ASSERT_TRUE(compositeState.valid());

    ASSERT_TRUE(compositePresentState.buildTextureSubset(compositeState, compositeColor.get()));
    Buffer* const presentBaseBuffers[] = { slotsBuffer.get() };
    ASSERT_TRUE(presentBaseState.buildResourceSubset(
        compositeBaseState,
        nullptr,
        0u,
        presentBaseBuffers,
        1u
    ));
    const CommandListResourceStateHandoff* const presentBranches[] = { &compositePresentState };
    ASSERT_TRUE(presentInputState.buildFanIn(presentBaseState, presentBranches, 1u));
    present->open(&presentInputState);
    EXPECT_EQ(present->getTextureSubresourceState(compositeColor.get(), 0u, 0u), ResourceStates::Common);
    EXPECT_EQ(present->getTextureSubresourceState(opaqueColor.get(), 0u, 0u), ResourceStates::Unknown);
    present->setTextureState(compositeColor.get(), s_AllSubresources, ResourceStates::ShaderResource);
    present->close(&presentState);
    ASSERT_TRUE(presentState.valid());

    ASSERT_TRUE(shadowReturnState.buildTextureSubset(lightingState, shadowVisibility.get()));
    ASSERT_TRUE(causticReturnState.buildTextureSubset(lightingState, causticIrradiance.get()));
    ASSERT_TRUE(surfelReturnState.buildTextureSubset(lightingState, surfelIrradiance.get()));
    const CommandListResourceStateHandoff* const computeReuseBranches[] = {
        &shadowReturnState,
        &causticReturnState,
        &surfelReturnState,
    };
    ASSERT_TRUE(computeReuseInputState.buildFanIn(prefixState, computeReuseBranches, 3u));
    computeReuse->open(&computeReuseInputState);
    EXPECT_EQ(computeReuse->getTextureSubresourceState(shadowVisibility.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(computeReuse->getTextureSubresourceState(causticIrradiance.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(computeReuse->getTextureSubresourceState(surfelIrradiance.get(), 0u, 0u), ResourceStates::ShaderResource);
    computeReuse->setTextureState(shadowVisibility.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    computeReuse->setTextureState(causticIrradiance.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    computeReuse->setTextureState(surfelIrradiance.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    computeReuse->close();

    CommandList* prefixLists[] = { prefix.get() };
    const QueueSubmissionToken prefixToken = device.executeCommandLists(
        prefixLists,
        1u,
        RenderLane::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(prefixToken.valid());

    const QueueSubmissionDesc rayEffectsSubmitDesc = QueueSubmissionDesc().setWaitTokens(&prefixToken, 1u);
    CommandList* rayEffectsLists[] = { rayEffects.get() };
    const QueueSubmissionToken rayEffectsToken = device.executeCommandLists(
        rayEffectsLists,
        1u,
        RenderLane::AsyncCompute,
        rayEffectsSubmitDesc
    );
    ASSERT_TRUE(rayEffectsToken.valid());

    const QueueSubmissionDesc avboitSubmitDesc = QueueSubmissionDesc().setWaitTokens(&prefixToken, 1u);
    CommandList* avboitLists[] = { avboit.get() };
    const QueueSubmissionToken avboitToken = device.executeCommandLists(
        avboitLists,
        1u,
        RenderLane::Graphics,
        avboitSubmitDesc
    );
    ASSERT_TRUE(avboitToken.valid());

    const QueueSubmissionDesc lightingSubmitDesc = QueueSubmissionDesc().setWaitTokens(&avboitToken, 1u);
    CommandList* lightingLists[] = { lighting.get() };
    const QueueSubmissionToken lightingToken = device.executeCommandLists(
        lightingLists,
        1u,
        RenderLane::AsyncCompute,
        lightingSubmitDesc
    );
    ASSERT_TRUE(lightingToken.valid());

    CommandList* compositeLists[] = { composite.get() };
    const QueueSubmissionToken compositeToken = device.executeCommandLists(
        compositeLists,
        1u,
        RenderLane::AsyncCompute,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(compositeToken.valid());

    const QueueSubmissionDesc presentSubmitDesc = QueueSubmissionDesc().setWaitTokens(&compositeToken, 1u);
    CommandList* presentLists[] = { present.get() };
    const QueueSubmissionToken presentToken = device.executeCommandLists(
        presentLists,
        1u,
        RenderLane::Graphics,
        presentSubmitDesc
    );
    ASSERT_TRUE(presentToken.valid());

    // This reuse is ordered after composite by the same AsyncCompute queue. It deliberately does not wait for the
    // Graphics present packet because that packet imports only the concurrent composite presentation image.
    CommandList* reuseLists[] = { computeReuse.get() };
    const QueueSubmissionToken reuseToken = device.executeCommandLists(
        reuseLists,
        1u,
        RenderLane::AsyncCompute,
        QueueSubmissionDesc{}
    );
    EXPECT_TRUE(reuseToken.valid());
    EXPECT_TRUE(device.waitForIdle());
}


// The optional latency trade-off keeps the three exclusive live effect outputs on AsyncCompute, but copies an
// accepted snapshot into concurrent, keep-initial-state history images. The next frame's Graphics lighting samples
// only those history images while AsyncCompute writes a new live triple; final still joins the current producer before
// the next snapshot. This verifies the precise queue and state topology without relying on a renderer pipeline.
TEST_F(DescriptorBufferRoundTripTest, AsyncComputeLaneUsesAcceptedLaggedLightingHistory){
    HeadlessGraphicsScope asyncScope;
    ASSERT_TRUE(asyncScope.setAsyncComputeLaneEnabled(true));
    if(!asyncScope.initialize())
        GTEST_SKIP() << "Async-compute lane: no usable dedicated-compute headless Vulkan device on this host.";

    auto& device = asyncScope.graphics().getDevice();
    if(!device.isRenderLaneDedicated(RenderLane::AsyncCompute))
        GTEST_SKIP() << "Async-compute lane: adapter has no dedicated compute-only queue family.";

    auto gbuffer = CreateConcurrentTestTexture(device);
    auto opaqueColor = CreateConcurrentTestTexture(device, true);
    auto compositeColor = CreateConcurrentTestTexture(device, true);
    auto shadowVisibility = CreateExclusiveRayOutputTestTexture(device);
    auto causticIrradiance = CreateExclusiveRayOutputTestTexture(device);
    auto surfelIrradiance = CreateExclusiveRayOutputTestTexture(device);
    auto shadowHistory = CreateConcurrentTestTexture(device, true);
    auto causticHistory = CreateConcurrentTestTexture(device, true);
    auto surfelHistory = CreateConcurrentTestTexture(device, true);
    ASSERT_NE(gbuffer.get(), nullptr);
    ASSERT_NE(opaqueColor.get(), nullptr);
    ASSERT_NE(compositeColor.get(), nullptr);
    ASSERT_NE(shadowVisibility.get(), nullptr);
    ASSERT_NE(causticIrradiance.get(), nullptr);
    ASSERT_NE(surfelIrradiance.get(), nullptr);
    ASSERT_NE(shadowHistory.get(), nullptr);
    ASSERT_NE(causticHistory.get(), nullptr);
    ASSERT_NE(surfelHistory.get(), nullptr);

    CommandListParameters asyncParams;
    asyncParams.setRenderLane(RenderLane::AsyncCompute);
    auto seedPrefix = device.createCommandList();
    auto seedProducer = device.createCommandList(asyncParams);
    auto seedFinal = device.createCommandList();
    auto seedStash = device.createCommandList(asyncParams);
    auto nextPrefix = device.createCommandList();
    auto nextProducer = device.createCommandList(asyncParams);
    auto laggedLighting = device.createCommandList();
    auto laggedComposite = device.createCommandList();
    auto laggedFinal = device.createCommandList();
    auto nextStash = device.createCommandList(asyncParams);
    ASSERT_NE(seedPrefix.get(), nullptr);
    ASSERT_NE(seedProducer.get(), nullptr);
    ASSERT_NE(seedFinal.get(), nullptr);
    ASSERT_NE(seedStash.get(), nullptr);
    ASSERT_NE(nextPrefix.get(), nullptr);
    ASSERT_NE(nextProducer.get(), nullptr);
    ASSERT_NE(laggedLighting.get(), nullptr);
    ASSERT_NE(laggedComposite.get(), nullptr);
    ASSERT_NE(laggedFinal.get(), nullptr);
    ASSERT_NE(nextStash.get(), nullptr);

    CommandListResourceStateHandoff seedPrefixState(asyncScope.arena());
    CommandListResourceStateHandoff seedProducerState(asyncScope.arena());
    CommandListResourceStateHandoff seedShadowSourceState(asyncScope.arena());
    CommandListResourceStateHandoff seedCausticSourceState(asyncScope.arena());
    CommandListResourceStateHandoff seedSurfelSourceState(asyncScope.arena());
    CommandListResourceStateHandoff seedStashInputState(asyncScope.arena());
    CommandListResourceStateHandoff seedStashState(asyncScope.arena());
    CommandListResourceStateHandoff seedShadowReturnState(asyncScope.arena());
    CommandListResourceStateHandoff seedCausticReturnState(asyncScope.arena());
    CommandListResourceStateHandoff seedSurfelReturnState(asyncScope.arena());
    CommandListResourceStateHandoff nextPrefixState(asyncScope.arena());
    CommandListResourceStateHandoff nextProducerInputState(asyncScope.arena());
    CommandListResourceStateHandoff nextProducerState(asyncScope.arena());
    CommandListResourceStateHandoff nextShadowSourceState(asyncScope.arena());
    CommandListResourceStateHandoff nextCausticSourceState(asyncScope.arena());
    CommandListResourceStateHandoff nextSurfelSourceState(asyncScope.arena());
    CommandListResourceStateHandoff laggedLightingBaseState(asyncScope.arena());
    CommandListResourceStateHandoff laggedLightingState(asyncScope.arena());
    CommandListResourceStateHandoff laggedOpaqueCompositeState(asyncScope.arena());
    CommandListResourceStateHandoff laggedCompositeState(asyncScope.arena());
    CommandListResourceStateHandoff laggedCompositeFinalState(asyncScope.arena());
    CommandListResourceStateHandoff laggedFinalState(asyncScope.arena());
    CommandListResourceStateHandoff nextStashInputState(asyncScope.arena());
    CommandListResourceStateHandoff nextStashState(asyncScope.arena());

    seedPrefix->open();
    seedPrefix->setTextureState(gbuffer.get(), s_AllSubresources, ResourceStates::ShaderResource);
    seedPrefix->setTextureState(opaqueColor.get(), s_AllSubresources, ResourceStates::CopyDest);
    seedPrefix->close(&seedPrefixState);
    ASSERT_TRUE(seedPrefixState.valid());

    seedProducer->open(&seedPrefixState);
    seedProducer->setTextureState(gbuffer.get(), s_AllSubresources, ResourceStates::ShaderResource);
    seedProducer->setTextureState(shadowVisibility.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    seedProducer->setTextureState(causticIrradiance.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    seedProducer->setTextureState(surfelIrradiance.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    seedProducer->setTextureState(shadowVisibility.get(), s_AllSubresources, ResourceStates::ShaderResource);
    seedProducer->setTextureState(causticIrradiance.get(), s_AllSubresources, ResourceStates::ShaderResource);
    seedProducer->setTextureState(surfelIrradiance.get(), s_AllSubresources, ResourceStates::ShaderResource);
    seedProducer->close(&seedProducerState);
    ASSERT_TRUE(seedProducerState.valid());
    ASSERT_TRUE(seedShadowSourceState.buildTextureSubset(seedProducerState, shadowVisibility.get()));
    ASSERT_TRUE(seedCausticSourceState.buildTextureSubset(seedProducerState, causticIrradiance.get()));
    ASSERT_TRUE(seedSurfelSourceState.buildTextureSubset(seedProducerState, surfelIrradiance.get()));

    seedFinal->open();
    seedFinal->close();
    ASSERT_TRUE(seedFinal->hasCommandBuffer());

    const CommandListResourceStateHandoff* const seedStashBranches[] = {
        &seedCausticSourceState,
        &seedSurfelSourceState,
    };
    ASSERT_TRUE(seedStashInputState.buildFanIn(
        seedShadowSourceState,
        seedStashBranches,
        LengthOf(seedStashBranches)
    ));
    seedStash->open(&seedStashInputState);
    EXPECT_EQ(seedStash->getTextureSubresourceState(shadowVisibility.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(seedStash->getTextureSubresourceState(causticIrradiance.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(seedStash->getTextureSubresourceState(surfelIrradiance.get(), 0u, 0u), ResourceStates::ShaderResource);
    seedStash->setTextureState(shadowVisibility.get(), s_AllSubresources, ResourceStates::CopySource);
    seedStash->setTextureState(causticIrradiance.get(), s_AllSubresources, ResourceStates::CopySource);
    seedStash->setTextureState(surfelIrradiance.get(), s_AllSubresources, ResourceStates::CopySource);
    seedStash->setTextureState(shadowHistory.get(), s_AllSubresources, ResourceStates::CopyDest);
    seedStash->setTextureState(causticHistory.get(), s_AllSubresources, ResourceStates::CopyDest);
    seedStash->setTextureState(surfelHistory.get(), s_AllSubresources, ResourceStates::CopyDest);
    seedStash->commitBarriers();
    const TextureSlice slice;
    seedStash->copyTexture(shadowHistory.get(), slice, shadowVisibility.get(), slice);
    seedStash->copyTexture(causticHistory.get(), slice, causticIrradiance.get(), slice);
    seedStash->copyTexture(surfelHistory.get(), slice, surfelIrradiance.get(), slice);
    seedStash->close(&seedStashState);
    ASSERT_TRUE(seedStashState.valid());
    ASSERT_TRUE(seedShadowReturnState.buildTextureSubset(seedStashState, shadowVisibility.get()));
    ASSERT_TRUE(seedCausticReturnState.buildTextureSubset(seedStashState, causticIrradiance.get()));
    ASSERT_TRUE(seedSurfelReturnState.buildTextureSubset(seedStashState, surfelIrradiance.get()));

    nextPrefix->open();
    nextPrefix->setTextureState(gbuffer.get(), s_AllSubresources, ResourceStates::ShaderResource);
    nextPrefix->setTextureState(opaqueColor.get(), s_AllSubresources, ResourceStates::CopyDest);
    nextPrefix->close(&nextPrefixState);
    ASSERT_TRUE(nextPrefixState.valid());

    const CommandListResourceStateHandoff* const nextProducerBranches[] = {
        &seedShadowReturnState,
        &seedCausticReturnState,
        &seedSurfelReturnState,
    };
    ASSERT_TRUE(nextProducerInputState.buildFanIn(
        nextPrefixState,
        nextProducerBranches,
        LengthOf(nextProducerBranches)
    ));
    nextProducer->open(&nextProducerInputState);
    EXPECT_EQ(nextProducer->getTextureSubresourceState(shadowVisibility.get(), 0u, 0u), ResourceStates::CopySource);
    EXPECT_EQ(nextProducer->getTextureSubresourceState(causticIrradiance.get(), 0u, 0u), ResourceStates::CopySource);
    EXPECT_EQ(nextProducer->getTextureSubresourceState(surfelIrradiance.get(), 0u, 0u), ResourceStates::CopySource);
    nextProducer->setTextureState(shadowVisibility.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    nextProducer->setTextureState(causticIrradiance.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    nextProducer->setTextureState(surfelIrradiance.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    nextProducer->setTextureState(shadowVisibility.get(), s_AllSubresources, ResourceStates::ShaderResource);
    nextProducer->setTextureState(causticIrradiance.get(), s_AllSubresources, ResourceStates::ShaderResource);
    nextProducer->setTextureState(surfelIrradiance.get(), s_AllSubresources, ResourceStates::ShaderResource);
    nextProducer->close(&nextProducerState);
    ASSERT_TRUE(nextProducerState.valid());
    ASSERT_TRUE(nextShadowSourceState.buildTextureSubset(nextProducerState, shadowVisibility.get()));
    ASSERT_TRUE(nextCausticSourceState.buildTextureSubset(nextProducerState, causticIrradiance.get()));
    ASSERT_TRUE(nextSurfelSourceState.buildTextureSubset(nextProducerState, surfelIrradiance.get()));

    Texture* const laggedLightingBaseTextures[] = {
        gbuffer.get(),
        opaqueColor.get(),
    };
    ASSERT_TRUE(laggedLightingBaseState.buildResourceSubset(
        nextPrefixState,
        laggedLightingBaseTextures,
        LengthOf(laggedLightingBaseTextures),
        nullptr,
        0u
    ));
    laggedLighting->open(&laggedLightingBaseState);
    EXPECT_EQ(laggedLighting->getTextureSubresourceState(gbuffer.get(), 0u, 0u), ResourceStates::ShaderResource);
    // The history is intentionally absent from the producer handoff. Its keep-initial-state close from the accepted
    // stash makes the prior snapshot independently importable on Graphics after the stash token is waited.
    EXPECT_EQ(laggedLighting->getTextureSubresourceState(shadowHistory.get(), 0u, 0u), ResourceStates::Common);
    EXPECT_EQ(laggedLighting->getTextureSubresourceState(causticHistory.get(), 0u, 0u), ResourceStates::Common);
    EXPECT_EQ(laggedLighting->getTextureSubresourceState(surfelHistory.get(), 0u, 0u), ResourceStates::Common);
    EXPECT_EQ(laggedLighting->getTextureSubresourceState(shadowVisibility.get(), 0u, 0u), ResourceStates::Unknown);
    laggedLighting->setTextureState(shadowHistory.get(), s_AllSubresources, ResourceStates::ShaderResource);
    laggedLighting->setTextureState(causticHistory.get(), s_AllSubresources, ResourceStates::ShaderResource);
    laggedLighting->setTextureState(surfelHistory.get(), s_AllSubresources, ResourceStates::ShaderResource);
    laggedLighting->setTextureState(opaqueColor.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    laggedLighting->close(&laggedLightingState);
    ASSERT_TRUE(laggedLightingState.valid());
    ASSERT_TRUE(laggedOpaqueCompositeState.buildTextureSubset(laggedLightingState, opaqueColor.get()));

    laggedComposite->open(&laggedOpaqueCompositeState);
    EXPECT_EQ(laggedComposite->getTextureSubresourceState(opaqueColor.get(), 0u, 0u), ResourceStates::Common);
    laggedComposite->setTextureState(opaqueColor.get(), s_AllSubresources, ResourceStates::ShaderResource);
    laggedComposite->setTextureState(compositeColor.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    laggedComposite->close(&laggedCompositeState);
    ASSERT_TRUE(laggedCompositeState.valid());
    ASSERT_TRUE(laggedCompositeFinalState.buildTextureSubset(laggedCompositeState, compositeColor.get()));

    laggedFinal->open(&laggedCompositeFinalState);
    EXPECT_EQ(laggedFinal->getTextureSubresourceState(compositeColor.get(), 0u, 0u), ResourceStates::Common);
    laggedFinal->setTextureState(compositeColor.get(), s_AllSubresources, ResourceStates::ShaderResource);
    laggedFinal->close(&laggedFinalState);
    ASSERT_TRUE(laggedFinalState.valid());

    const CommandListResourceStateHandoff* const nextStashBranches[] = {
        &nextCausticSourceState,
        &nextSurfelSourceState,
    };
    ASSERT_TRUE(nextStashInputState.buildFanIn(
        nextShadowSourceState,
        nextStashBranches,
        LengthOf(nextStashBranches)
    ));
    nextStash->open(&nextStashInputState);
    nextStash->setTextureState(shadowVisibility.get(), s_AllSubresources, ResourceStates::CopySource);
    nextStash->setTextureState(causticIrradiance.get(), s_AllSubresources, ResourceStates::CopySource);
    nextStash->setTextureState(surfelIrradiance.get(), s_AllSubresources, ResourceStates::CopySource);
    nextStash->setTextureState(shadowHistory.get(), s_AllSubresources, ResourceStates::CopyDest);
    nextStash->setTextureState(causticHistory.get(), s_AllSubresources, ResourceStates::CopyDest);
    nextStash->setTextureState(surfelHistory.get(), s_AllSubresources, ResourceStates::CopyDest);
    nextStash->commitBarriers();
    nextStash->copyTexture(shadowHistory.get(), slice, shadowVisibility.get(), slice);
    nextStash->copyTexture(causticHistory.get(), slice, causticIrradiance.get(), slice);
    nextStash->copyTexture(surfelHistory.get(), slice, surfelIrradiance.get(), slice);
    nextStash->close(&nextStashState);
    ASSERT_TRUE(nextStashState.valid());

    CommandList* seedPrefixLists[] = { seedPrefix.get() };
    const QueueSubmissionToken seedPrefixToken = device.executeCommandLists(
        seedPrefixLists,
        LengthOf(seedPrefixLists),
        RenderLane::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(seedPrefixToken.valid());

    const QueueSubmissionDesc seedProducerSubmitDesc = QueueSubmissionDesc().setWaitTokens(&seedPrefixToken, 1u);
    CommandList* seedProducerLists[] = { seedProducer.get() };
    const QueueSubmissionToken seedProducerToken = device.executeCommandLists(
        seedProducerLists,
        LengthOf(seedProducerLists),
        RenderLane::AsyncCompute,
        seedProducerSubmitDesc
    );
    ASSERT_TRUE(seedProducerToken.valid());

    const QueueSubmissionDesc seedFinalSubmitDesc = QueueSubmissionDesc().setWaitTokens(&seedProducerToken, 1u);
    CommandList* seedFinalLists[] = { seedFinal.get() };
    const QueueSubmissionToken seedFinalToken = device.executeCommandLists(
        seedFinalLists,
        LengthOf(seedFinalLists),
        RenderLane::Graphics,
        seedFinalSubmitDesc
    );
    ASSERT_TRUE(seedFinalToken.valid());

    const QueueSubmissionDesc seedStashSubmitDesc = QueueSubmissionDesc().setWaitTokens(&seedFinalToken, 1u);
    CommandList* seedStashLists[] = { seedStash.get() };
    const QueueSubmissionToken seedStashToken = device.executeCommandLists(
        seedStashLists,
        LengthOf(seedStashLists),
        RenderLane::AsyncCompute,
        seedStashSubmitDesc
    );
    ASSERT_TRUE(seedStashToken.valid());

    CommandList* nextPrefixLists[] = { nextPrefix.get() };
    const QueueSubmissionToken nextPrefixToken = device.executeCommandLists(
        nextPrefixLists,
        LengthOf(nextPrefixLists),
        RenderLane::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(nextPrefixToken.valid());

    const QueueSubmissionDesc nextProducerSubmitDesc = QueueSubmissionDesc().setWaitTokens(&nextPrefixToken, 1u);
    CommandList* nextProducerLists[] = { nextProducer.get() };
    const QueueSubmissionToken nextProducerToken = device.executeCommandLists(
        nextProducerLists,
        LengthOf(nextProducerLists),
        RenderLane::AsyncCompute,
        nextProducerSubmitDesc
    );
    ASSERT_TRUE(nextProducerToken.valid());

    const QueueSubmissionDesc laggedLightingSubmitDesc = QueueSubmissionDesc().setWaitTokens(&seedStashToken, 1u);
    CommandList* laggedLightingLists[] = { laggedLighting.get() };
    const QueueSubmissionToken laggedLightingToken = device.executeCommandLists(
        laggedLightingLists,
        LengthOf(laggedLightingLists),
        RenderLane::Graphics,
        laggedLightingSubmitDesc
    );
    ASSERT_TRUE(laggedLightingToken.valid());

    CommandList* laggedCompositeLists[] = { laggedComposite.get() };
    const QueueSubmissionToken laggedCompositeToken = device.executeCommandLists(
        laggedCompositeLists,
        LengthOf(laggedCompositeLists),
        RenderLane::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(laggedCompositeToken.valid());

    const QueueSubmissionToken laggedFinalWaitTokens[] = { laggedCompositeToken, nextProducerToken };
    const QueueSubmissionDesc laggedFinalSubmitDesc = QueueSubmissionDesc().setWaitTokens(
        laggedFinalWaitTokens,
        LengthOf(laggedFinalWaitTokens)
    );
    CommandList* laggedFinalLists[] = { laggedFinal.get() };
    const QueueSubmissionToken laggedFinalToken = device.executeCommandLists(
        laggedFinalLists,
        LengthOf(laggedFinalLists),
        RenderLane::Graphics,
        laggedFinalSubmitDesc
    );
    ASSERT_TRUE(laggedFinalToken.valid());

    const QueueSubmissionDesc nextStashSubmitDesc = QueueSubmissionDesc().setWaitTokens(&laggedFinalToken, 1u);
    CommandList* nextStashLists[] = { nextStash.get() };
    const QueueSubmissionToken nextStashToken = device.executeCommandLists(
        nextStashLists,
        LengthOf(nextStashLists),
        RenderLane::AsyncCompute,
        nextStashSubmitDesc
    );
    EXPECT_TRUE(nextStashToken.valid());
    EXPECT_TRUE(device.waitForIdle());
}


// Caustics (including the hardware dispatch-rays producer) and surfel GI add two exclusive Compute results beside
// shadowVisibility. If Graphics effects/final cannot consume them, the recovery packet must acquire and return all
// three outputs together; the next Compute packet
// then imports their shared return handoff alongside its ordinary concurrent prefix input.
TEST_F(DescriptorBufferRoundTripTest, AsyncComputeLaneRecoversCausticSurfelAndShadowTextureOwnershipTogether){
    HeadlessGraphicsScope asyncScope;
    ASSERT_TRUE(asyncScope.setAsyncComputeLaneEnabled(true));
    if(!asyncScope.initialize())
        GTEST_SKIP() << "Async-compute lane: no usable dedicated-compute headless Vulkan device on this host.";

    auto& device = asyncScope.graphics().getDevice();
    if(!device.isRenderLaneDedicated(RenderLane::AsyncCompute))
        GTEST_SKIP() << "Async-compute lane: adapter has no dedicated compute-only queue family.";

    const auto makeExclusiveOutput = [&device](){
        return device.createTexture(
            TextureDesc()
                .setWidth(4u)
                .setHeight(4u)
                .setFormat(Format::RGBA8_UNORM)
                .setInUAV(true)
                .setInitialState(ResourceStates::Common)
        );
    };
    auto shadowVisibility = makeExclusiveOutput();
    auto causticIrradiance = makeExclusiveOutput();
    auto surfelIrradiance = makeExclusiveOutput();
    auto sharedInput = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::GraphicsAndAsyncCompute)
    );
    ASSERT_NE(shadowVisibility.get(), nullptr);
    ASSERT_NE(causticIrradiance.get(), nullptr);
    ASSERT_NE(surfelIrradiance.get(), nullptr);
    ASSERT_NE(sharedInput.get(), nullptr);

    CommandListParameters computeParams;
    computeParams.setRenderLane(RenderLane::AsyncCompute);

    CommandListResourceStateHandoff prefixState(asyncScope.arena());
    CommandListResourceStateHandoff computeState(asyncScope.arena());
    CommandListResourceStateHandoff shadowGraphicsState(asyncScope.arena());
    CommandListResourceStateHandoff causticGraphicsState(asyncScope.arena());
    CommandListResourceStateHandoff surfelGraphicsState(asyncScope.arena());
    CommandListResourceStateHandoff recoveryInputState(asyncScope.arena());
    CommandListResourceStateHandoff recoveryState(asyncScope.arena());
    CommandListResourceStateHandoff nextComputeInputState(asyncScope.arena());

    auto prefix = device.createCommandList();
    auto compute = device.createCommandList(computeParams);
    auto recovery = device.createCommandList();
    auto computeReuse = device.createCommandList(computeParams);
    ASSERT_NE(prefix.get(), nullptr);
    ASSERT_NE(compute.get(), nullptr);
    ASSERT_NE(recovery.get(), nullptr);
    ASSERT_NE(computeReuse.get(), nullptr);

    prefix->open();
    prefix->setBufferState(sharedInput.get(), ResourceStates::ShaderResource);
    prefix->close(&prefixState);
    ASSERT_TRUE(prefixState.valid());

    compute->open(&prefixState);
    compute->setBufferState(sharedInput.get(), ResourceStates::ShaderResource);
    compute->setTextureState(shadowVisibility.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    compute->setTextureState(causticIrradiance.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    compute->setTextureState(surfelIrradiance.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    compute->releaseTextureOwnership(shadowVisibility.get(), s_AllSubresources, RenderLane::Graphics);
    compute->releaseTextureOwnership(causticIrradiance.get(), s_AllSubresources, RenderLane::Graphics);
    compute->releaseTextureOwnership(surfelIrradiance.get(), s_AllSubresources, RenderLane::Graphics);
    compute->close(&computeState);
    ASSERT_TRUE(computeState.valid());
    ASSERT_TRUE(shadowGraphicsState.buildTextureSubset(computeState, shadowVisibility.get()));
    ASSERT_TRUE(causticGraphicsState.buildTextureSubset(computeState, causticIrradiance.get()));
    ASSERT_TRUE(surfelGraphicsState.buildTextureSubset(computeState, surfelIrradiance.get()));

    const CommandListResourceStateHandoff* recoveryBranches[] = { &causticGraphicsState, &surfelGraphicsState };
    ASSERT_TRUE(recoveryInputState.buildFanIn(shadowGraphicsState, recoveryBranches, 2u));

    CommandList* prefixLists[] = { prefix.get() };
    const QueueSubmissionToken prefixToken = device.executeCommandLists(
        prefixLists,
        1u,
        RenderLane::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(prefixToken.valid());

    const QueueSubmissionDesc computeSubmitDesc = QueueSubmissionDesc().setWaitTokens(&prefixToken, 1u);
    CommandList* computeLists[] = { compute.get() };
    const QueueSubmissionToken computeToken = device.executeCommandLists(
        computeLists,
        1u,
        RenderLane::AsyncCompute,
        computeSubmitDesc
    );
    ASSERT_TRUE(computeToken.valid());

    recovery->open(&recoveryInputState);
    EXPECT_EQ(recovery->getTextureSubresourceState(shadowVisibility.get(), 0u, 0u), ResourceStates::UnorderedAccess);
    EXPECT_EQ(recovery->getTextureSubresourceState(causticIrradiance.get(), 0u, 0u), ResourceStates::UnorderedAccess);
    EXPECT_EQ(recovery->getTextureSubresourceState(surfelIrradiance.get(), 0u, 0u), ResourceStates::UnorderedAccess);
    recovery->setTextureState(shadowVisibility.get(), s_AllSubresources, ResourceStates::ShaderResource);
    recovery->setTextureState(causticIrradiance.get(), s_AllSubresources, ResourceStates::ShaderResource);
    recovery->setTextureState(surfelIrradiance.get(), s_AllSubresources, ResourceStates::ShaderResource);
    recovery->releaseTextureOwnership(shadowVisibility.get(), s_AllSubresources, RenderLane::AsyncCompute);
    recovery->releaseTextureOwnership(causticIrradiance.get(), s_AllSubresources, RenderLane::AsyncCompute);
    recovery->releaseTextureOwnership(surfelIrradiance.get(), s_AllSubresources, RenderLane::AsyncCompute);
    recovery->close(&recoveryState);
    ASSERT_TRUE(recoveryState.valid());

    const QueueSubmissionDesc recoverySubmitDesc = QueueSubmissionDesc().setWaitTokens(&computeToken, 1u);
    CommandList* recoveryLists[] = { recovery.get() };
    const QueueSubmissionToken recoveryToken = device.executeCommandLists(
        recoveryLists,
        1u,
        RenderLane::Graphics,
        recoverySubmitDesc
    );
    ASSERT_TRUE(recoveryToken.valid());

    const CommandListResourceStateHandoff* nextComputeBranches[] = { &recoveryState };
    ASSERT_TRUE(nextComputeInputState.buildFanIn(prefixState, nextComputeBranches, 1u));
    computeReuse->open(&nextComputeInputState);
    EXPECT_EQ(computeReuse->getBufferState(sharedInput.get()), ResourceStates::ShaderResource);
    EXPECT_EQ(computeReuse->getTextureSubresourceState(shadowVisibility.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(computeReuse->getTextureSubresourceState(causticIrradiance.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(computeReuse->getTextureSubresourceState(surfelIrradiance.get(), 0u, 0u), ResourceStates::ShaderResource);
    computeReuse->setTextureState(shadowVisibility.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    computeReuse->setTextureState(causticIrradiance.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    computeReuse->setTextureState(surfelIrradiance.get(), s_AllSubresources, ResourceStates::UnorderedAccess);
    computeReuse->close();

    const QueueSubmissionDesc reuseSubmitDesc = QueueSubmissionDesc().setWaitTokens(&recoveryToken, 1u);
    CommandList* reuseLists[] = { computeReuse.get() };
    const QueueSubmissionToken reuseToken = device.executeCommandLists(
        reuseLists,
        1u,
        RenderLane::AsyncCompute,
        reuseSubmitDesc
    );
    EXPECT_TRUE(reuseToken.valid());
    EXPECT_TRUE(device.waitForIdle());
}


#if !defined(NWB_FINAL) || defined(NWB_ENABLE_TEST_FEATURE_OVERRIDES)

// Exercise the four-submission shadow topology's rejection boundaries with the same pre-Vulkan injection seam used
// by RendererSystem. Prefix and shadow rejections leave no ownership handoff; effects/final rejections repair the
// accepted Compute release through a Graphics acquire/release; a rejected recovery deliberately stops before reuse,
// matching the renderer's device-recreation/suspension policy.
TEST_F(DescriptorBufferRoundTripTest, AsyncComputePacketFailureInjectionPreservesOrSuspendsExclusiveOwnership){
    HeadlessGraphicsScope asyncScope;
    ASSERT_TRUE(asyncScope.setAsyncComputeLaneEnabled(true));
    if(!asyncScope.initialize())
        GTEST_SKIP() << "Async-compute lane: no usable dedicated-compute headless Vulkan device on this host.";

    auto& device = asyncScope.graphics().getDevice();
    if(!device.isRenderLaneDedicated(RenderLane::AsyncCompute))
        GTEST_SKIP() << "Async-compute lane: adapter has no dedicated compute-only queue family.";

    enum class FailurePoint : u8{
        Prefix,
        Shadow,
        Effects,
        Final,
        Recovery,
    };

    const auto makeExclusiveBuffer = [&device](){
        return device.createBuffer(
            BufferDesc()
                .setByteSize(256u)
                .setCanHaveUAVs(true)
                .setCanHaveRawViews(true)
                .setInitialState(ResourceStates::Common)
        );
    };
    const auto makeSharedBuffer = [&device](){
        return device.createBuffer(
            BufferDesc()
                .setByteSize(256u)
                .setCanHaveUAVs(true)
                .setCanHaveRawViews(true)
                .setInitialState(ResourceStates::Common)
                .setQueueSharing(ResourceQueueSharing::GraphicsAndAsyncCompute)
        );
    };

    CommandListParameters computeParams;
    computeParams.setRenderLane(RenderLane::AsyncCompute);

    // Executes the next valid Compute -> Graphics -> Compute cycle. `initialState` is present only after the
    // ownership-recovery acquire/release; `initialWait` makes that handoff's accepted token explicit.
    const auto executeNextValidCycle = [&](
        Buffer* output,
        Buffer* sharedInput,
        const CommandListResourceStateHandoff* initialState,
        const QueueSubmissionToken initialWait
    ){
        CommandListResourceStateHandoff computeToGraphics(asyncScope.arena());
        CommandListResourceStateHandoff graphicsToCompute(asyncScope.arena());
        auto compute = device.createCommandList(computeParams);
        auto graphics = device.createCommandList();
        auto computeReuse = device.createCommandList(computeParams);
        ASSERT_NE(compute.get(), nullptr);
        ASSERT_NE(graphics.get(), nullptr);
        ASSERT_NE(computeReuse.get(), nullptr);

        compute->open(initialState);
        if(initialState)
            EXPECT_EQ(compute->getBufferState(output), ResourceStates::ShaderResource);
        compute->setBufferState(sharedInput, ResourceStates::ShaderResource);
        compute->setBufferState(output, ResourceStates::UnorderedAccess);
        compute->releaseBufferOwnership(output, RenderLane::Graphics);
        compute->close(&computeToGraphics);
        ASSERT_TRUE(computeToGraphics.valid());

        QueueSubmissionDesc computeSubmitDesc;
        if(initialWait.valid())
            computeSubmitDesc.setWaitTokens(&initialWait, 1u);
        CommandList* computeCommandLists[] = { compute.get() };
        const QueueSubmissionToken computeToken = device.executeCommandLists(
            computeCommandLists,
            1u,
            RenderLane::AsyncCompute,
            computeSubmitDesc
        );
        ASSERT_TRUE(computeToken.valid());

        graphics->open(&computeToGraphics);
        EXPECT_EQ(graphics->getBufferState(output), ResourceStates::UnorderedAccess);
        graphics->setBufferState(output, ResourceStates::ShaderResource);
        graphics->releaseBufferOwnership(output, RenderLane::AsyncCompute);
        graphics->close(&graphicsToCompute);
        ASSERT_TRUE(graphicsToCompute.valid());

        const QueueSubmissionDesc graphicsSubmitDesc = QueueSubmissionDesc().setWaitTokens(&computeToken, 1u);
        CommandList* graphicsCommandLists[] = { graphics.get() };
        const QueueSubmissionToken graphicsToken = device.executeCommandLists(
            graphicsCommandLists,
            1u,
            RenderLane::Graphics,
            graphicsSubmitDesc
        );
        ASSERT_TRUE(graphicsToken.valid());

        computeReuse->open(&graphicsToCompute);
        EXPECT_EQ(computeReuse->getBufferState(output), ResourceStates::ShaderResource);
        computeReuse->setBufferState(output, ResourceStates::UnorderedAccess);
        computeReuse->close();
        const QueueSubmissionDesc reuseSubmitDesc = QueueSubmissionDesc().setWaitTokens(&graphicsToken, 1u);
        CommandList* reuseCommandLists[] = { computeReuse.get() };
        const QueueSubmissionToken reuseToken = device.executeCommandLists(
            reuseCommandLists,
            1u,
            RenderLane::AsyncCompute,
            reuseSubmitDesc
        );
        ASSERT_TRUE(reuseToken.valid());
        ASSERT_TRUE(device.waitForIdle());
    };

    const FailurePoint failurePoints[] = {
        FailurePoint::Prefix,
        FailurePoint::Shadow,
        FailurePoint::Effects,
        FailurePoint::Final,
        FailurePoint::Recovery,
    };
    for(const FailurePoint failurePoint : failurePoints){
        SCOPED_TRACE(static_cast<u32>(failurePoint));
        device.clearSubmissionRejectionsForTesting();

        auto output = makeExclusiveBuffer();
        auto sharedInput = makeSharedBuffer();
        ASSERT_NE(output.get(), nullptr);
        ASSERT_NE(sharedInput.get(), nullptr);

        CommandListResourceStateHandoff computeToGraphics(asyncScope.arena());
        CommandListResourceStateHandoff graphicsToCompute(asyncScope.arena());
        auto prefix = device.createCommandList();
        auto shadow = device.createCommandList(computeParams);
        auto effects = device.createCommandList();
        auto final = device.createCommandList();
        ASSERT_NE(prefix.get(), nullptr);
        ASSERT_NE(shadow.get(), nullptr);
        ASSERT_NE(effects.get(), nullptr);
        ASSERT_NE(final.get(), nullptr);

        prefix->open();
        prefix->setBufferState(sharedInput.get(), ResourceStates::ShaderResource);
        prefix->close();
        if(failurePoint == FailurePoint::Prefix)
            device.rejectNextSubmissionForTesting(CommandQueue::Graphics);
        CommandList* prefixCommandLists[] = { prefix.get() };
        const QueueSubmissionToken prefixToken = device.executeCommandLists(
            prefixCommandLists,
            1u,
            RenderLane::Graphics,
            QueueSubmissionDesc{}
        );
        if(failurePoint == FailurePoint::Prefix){
            EXPECT_FALSE(prefixToken.valid());
            executeNextValidCycle(output.get(), sharedInput.get(), nullptr, {});
            continue;
        }
        ASSERT_TRUE(prefixToken.valid());

        shadow->open();
        shadow->setBufferState(sharedInput.get(), ResourceStates::ShaderResource);
        shadow->setBufferState(output.get(), ResourceStates::UnorderedAccess);
        shadow->releaseBufferOwnership(output.get(), RenderLane::Graphics);
        shadow->close(&computeToGraphics);
        ASSERT_TRUE(computeToGraphics.valid());
        if(failurePoint == FailurePoint::Shadow)
            device.rejectNextSubmissionForTesting(CommandQueue::Compute);
        const QueueSubmissionDesc shadowSubmitDesc = QueueSubmissionDesc().setWaitTokens(&prefixToken, 1u);
        CommandList* shadowCommandLists[] = { shadow.get() };
        const QueueSubmissionToken shadowToken = device.executeCommandLists(
            shadowCommandLists,
            1u,
            RenderLane::AsyncCompute,
            shadowSubmitDesc
        );
        if(failurePoint == FailurePoint::Shadow){
            EXPECT_FALSE(shadowToken.valid());
            executeNextValidCycle(output.get(), sharedInput.get(), nullptr, prefixToken);
            continue;
        }
        ASSERT_TRUE(shadowToken.valid());

        effects->open();
        effects->setBufferState(sharedInput.get(), ResourceStates::ShaderResource);
        effects->close();
        if(failurePoint == FailurePoint::Effects || failurePoint == FailurePoint::Recovery)
            device.rejectNextSubmissionForTesting(CommandQueue::Graphics);
        if(failurePoint == FailurePoint::Recovery)
            device.rejectNextSubmissionForTesting(CommandQueue::Graphics);
        CommandList* effectsCommandLists[] = { effects.get() };
        const QueueSubmissionToken effectsToken = device.executeCommandLists(
            effectsCommandLists,
            1u,
            RenderLane::Graphics,
            QueueSubmissionDesc{}
        );
        if(failurePoint == FailurePoint::Effects || failurePoint == FailurePoint::Recovery){
            EXPECT_FALSE(effectsToken.valid());
        }
        else
            ASSERT_TRUE(effectsToken.valid());

        bool finalRejected = false;
        if(failurePoint != FailurePoint::Effects && failurePoint != FailurePoint::Recovery){
            final->open(&computeToGraphics);
            EXPECT_EQ(final->getBufferState(output.get()), ResourceStates::UnorderedAccess);
            final->setBufferState(output.get(), ResourceStates::ShaderResource);
            final->releaseBufferOwnership(output.get(), RenderLane::AsyncCompute);
            final->close(&graphicsToCompute);
            ASSERT_TRUE(graphicsToCompute.valid());
            if(failurePoint == FailurePoint::Final)
                device.rejectNextSubmissionForTesting(CommandQueue::Graphics);
            const QueueSubmissionToken finalWaitTokens[] = { shadowToken, effectsToken };
            const QueueSubmissionDesc finalSubmitDesc = QueueSubmissionDesc().setWaitTokens(finalWaitTokens, 2u);
            CommandList* finalCommandLists[] = { final.get() };
            const QueueSubmissionToken finalToken = device.executeCommandLists(
                finalCommandLists,
                1u,
                RenderLane::Graphics,
                finalSubmitDesc
            );
            finalRejected = failurePoint == FailurePoint::Final;
            if(finalRejected)
                EXPECT_FALSE(finalToken.valid());
            else
                ASSERT_TRUE(finalToken.valid());

            if(!finalRejected){
                executeNextValidCycle(output.get(), sharedInput.get(), &graphicsToCompute, finalToken);
                continue;
            }
        }

        // Effects/final did not leave an accepted Graphics acquire, so return the accepted Compute release to its
        // documented Compute owner. The second injected rejection covers the renderer's terminal recovery failure.
        auto recovery = device.createCommandList();
        ASSERT_NE(recovery.get(), nullptr);
        recovery->open(&computeToGraphics);
        EXPECT_EQ(recovery->getBufferState(output.get()), ResourceStates::UnorderedAccess);
        recovery->setBufferState(output.get(), ResourceStates::ShaderResource);
        recovery->releaseBufferOwnership(output.get(), RenderLane::AsyncCompute);
        recovery->close(&graphicsToCompute);
        ASSERT_TRUE(graphicsToCompute.valid());
        const QueueSubmissionDesc recoverySubmitDesc = QueueSubmissionDesc().setWaitTokens(&shadowToken, 1u);
        CommandList* recoveryCommandLists[] = { recovery.get() };
        const QueueSubmissionToken recoveryToken = device.executeCommandLists(
            recoveryCommandLists,
            1u,
            RenderLane::Graphics,
            recoverySubmitDesc
        );
        if(failurePoint == FailurePoint::Recovery){
            EXPECT_FALSE(recoveryToken.valid());
            EXPECT_TRUE(device.waitForIdle());
            continue;
        }
        ASSERT_TRUE(recoveryToken.valid());
        executeNextValidCycle(output.get(), sharedInput.get(), &graphicsToCompute, recoveryToken);
    }
}

#endif


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


// One compiled graph owns Shadow Prepare, the Graphics Prefix, every effect, and Present. Slots begin in the
// already-uploaded ConstantBuffer state; graph-owned packet seeds carry that state through the prefix and effects
// without any renderer-owned serial state snapshot.
TEST_F(DescriptorBufferRoundTripTest, RendererGraphShadowPrepareStateChainThroughComputePresent){
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
    const auto makeStorageTarget = [&device](){
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

    auto prefixBuffer = makeSetupBuffer();
    auto slotsBuffer = makeSetupBuffer();
    auto gbuffer = makeGbufferTarget();
    auto shadowVisibility = makeStorageTarget();
    auto causticIrradiance = makeStorageTarget();
    auto surfelIrradiance = makeStorageTarget();
    auto opaqueColor = makeStorageTarget();
    auto compositeColor = makeStorageTarget();
    ASSERT_NE(prefixBuffer.get(), nullptr);
    ASSERT_NE(slotsBuffer.get(), nullptr);
    ASSERT_NE(gbuffer.get(), nullptr);
    ASSERT_NE(shadowVisibility.get(), nullptr);
    ASSERT_NE(causticIrradiance.get(), nullptr);
    ASSERT_NE(surfelIrradiance.get(), nullptr);
    ASSERT_NE(opaqueColor.get(), nullptr);
    ASSERT_NE(compositeColor.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const auto importBuffer = [&graph](const auto& buffer, const Name& identity, const AStringView label){
        return graph.importBuffer(
            buffer,
            GpuGraphResourceDesc{}
                .setIdentity(identity)
                .setMarkerLabel(label)
                .setType(GpuGraphResourceType::Buffer)
        );
    };
    const auto importTexture = [&graph](const auto& texture, const Name& identity, const AStringView label){
        return graph.importTexture(
            texture,
            GpuGraphResourceDesc{}
                .setIdentity(identity)
                .setMarkerLabel(label)
                .setType(GpuGraphResourceType::Texture)
        );
    };
    const GpuGraphResourceId prefixBufferResource = importBuffer(
        prefixBuffer,
        Name("tests/descriptor_buffer/shadow_chain_prefix_buffer"),
        "Graphics Prefix Buffer"
    );
    const GpuGraphResourceId slotsResource = graph.importBuffer(
        slotsBuffer,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/shadow_chain_bindless_slots"))
            .setMarkerLabel("Bindless Slots")
            .setType(GpuGraphResourceType::Buffer)
            // This is the current uploaded layout, not the allocation-time Common state.
            .setInitialState(ResourceStates::ConstantBuffer)
    );
    const GpuGraphResourceId gbufferResource = importTexture(
        gbuffer,
        Name("tests/descriptor_buffer/shadow_chain_gbuffer"),
        "G-Buffer"
    );
    const GpuGraphResourceId shadowVisibilityResource = importTexture(
        shadowVisibility,
        Name("tests/descriptor_buffer/shadow_chain_shadow_visibility"),
        "Shadow Visibility"
    );
    const GpuGraphResourceId causticIrradianceResource = importTexture(
        causticIrradiance,
        Name("tests/descriptor_buffer/shadow_chain_caustic_irradiance"),
        "Caustic Irradiance"
    );
    const GpuGraphResourceId surfelIrradianceResource = importTexture(
        surfelIrradiance,
        Name("tests/descriptor_buffer/shadow_chain_surfel_irradiance"),
        "Surfel Irradiance"
    );
    const GpuGraphResourceId opaqueColorResource = importTexture(
        opaqueColor,
        Name("tests/descriptor_buffer/shadow_chain_opaque_color"),
        "Opaque Color"
    );
    const GpuGraphResourceId compositeColorResource = importTexture(
        compositeColor,
        Name("tests/descriptor_buffer/shadow_chain_composite_color"),
        "Composite Color"
    );
    ASSERT_TRUE(prefixBufferResource.valid());
    ASSERT_TRUE(slotsResource.valid());
    ASSERT_TRUE(gbufferResource.valid());
    ASSERT_TRUE(shadowVisibilityResource.valid());
    ASSERT_TRUE(causticIrradianceResource.valid());
    ASSERT_TRUE(surfelIrradianceResource.valid());
    ASSERT_TRUE(opaqueColorResource.valid());
    ASSERT_TRUE(compositeColorResource.valid());

    const GpuQueueRequest graphicsQueueRequest{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    const GpuQueueRequest computeQueueRequest{
        GpuQueueCapability::Compute,
        GpuQueuePreference::Compute,
        true,
        true,
    };
    GpuTaskSchedulingHint packetScheduling;
    packetScheduling.cost = GpuTaskCostHint::Large;
    packetScheduling.forceSubmissionBoundary = true;
    packetScheduling.allowPacketMerge = false;

    const auto addProbeTask = [
        &graph,
        &packetScheduling
    ](
        const Name& identity,
        const AStringView label,
        const GpuQueueRequest& queue,
        const GpuTaskId* const dependencies,
        const usize dependencyCount,
        const GpuTaskResourceUse* const resourceUses,
        const usize resourceUseCount,
        Buffer* const buffer,
        const ResourceStates::Mask expectedBufferState,
        Texture* const texture,
        const ResourceStates::Mask expectedTextureState,
        bool* const recorded
    ){
        GpuTaskDesc desc;
        desc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setQueue(queue)
            .setScheduling(packetScheduling)
            .setDependencies(dependencies, dependencyCount)
            .setResourceUses(resourceUses, resourceUseCount)
        ;
        return graph.addTask<NativePacketPrefixTask>(
            desc,
            NativePacketPrefixTask::Payload{
                .buffer = buffer,
                .expectedState = expectedBufferState,
                .texture = texture,
                .expectedTextureState = expectedTextureState,
                .recorded = recorded,
            }
        );
    };

    const GpuTaskResourceUse shadowPrepareUses[] = {
        GpuTaskResourceUse{
            .resource = slotsResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
    };
    GpuTaskDesc shadowPrepareDesc;
    shadowPrepareDesc
        .setIdentity(Name("tests/descriptor_buffer/shadow_chain_prepare"))
        .setMarkerLabel("Shadow Prepare")
        .setQueue(graphicsQueueRequest)
        .setScheduling(packetScheduling)
        .setResourceUses(shadowPrepareUses, LengthOf(shadowPrepareUses))
    ;
    bool shadowPrepareRecorded = false;
    const GpuTaskId shadowPrepareTask = graph.addTask<NativeShadowPrepareTask>(
        shadowPrepareDesc,
        NativeShadowPrepareTask::Payload{
            .bindlessSlots = slotsBuffer.get(),
            .recorded = &shadowPrepareRecorded,
        }
    );
    ASSERT_TRUE(shadowPrepareTask.valid());

    // Prefix receives the exact final slot snapshot from Shadow Prepare through its declared read, not through an
    // opaque serial seed. Subsequent effects continue the same compiler-owned chain.
    const GpuTaskResourceUse graphicsPrefixUses[] = {
        GpuTaskResourceUse{
            .resource = slotsResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = prefixBufferResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Write,
        },
        GpuTaskResourceUse{
            .resource = gbufferResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    bool graphicsPrefixRecorded = false;
    const GpuTaskId graphicsPrefixTask = addProbeTask(
        Name("tests/descriptor_buffer/shadow_chain_graphics_prefix"),
        "Graphics Prefix",
        graphicsQueueRequest,
        &shadowPrepareTask,
        1u,
        graphicsPrefixUses,
        LengthOf(graphicsPrefixUses),
        slotsBuffer.get(),
        ResourceStates::ConstantBuffer,
        gbuffer.get(),
        ResourceStates::ShaderResource,
        &graphicsPrefixRecorded
    );
    ASSERT_TRUE(graphicsPrefixTask.valid());

    const GpuTaskResourceUse shadowVisibilityUses[] = {
        GpuTaskResourceUse{
            .resource = gbufferResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = slotsResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = shadowVisibilityResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    bool shadowVisibilityRecorded = false;
    const GpuTaskId shadowVisibilityTask = addProbeTask(
        Name("tests/descriptor_buffer/shadow_chain_shadow_visibility"),
        "Shadow Visibility",
        computeQueueRequest,
        &graphicsPrefixTask,
        1u,
        shadowVisibilityUses,
        LengthOf(shadowVisibilityUses),
        slotsBuffer.get(),
        ResourceStates::ConstantBuffer,
        gbuffer.get(),
        ResourceStates::ShaderResource,
        &shadowVisibilityRecorded
    );
    ASSERT_TRUE(shadowVisibilityTask.valid());

    const GpuTaskResourceUse causticsUses[] = {
        GpuTaskResourceUse{
            .resource = gbufferResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = slotsResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = causticIrradianceResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    bool causticsRecorded = false;
    const GpuTaskId causticsTask = addProbeTask(
        Name("tests/descriptor_buffer/shadow_chain_caustics"),
        "Software Caustics",
        computeQueueRequest,
        &shadowVisibilityTask,
        1u,
        causticsUses,
        LengthOf(causticsUses),
        slotsBuffer.get(),
        ResourceStates::ConstantBuffer,
        gbuffer.get(),
        ResourceStates::ShaderResource,
        &causticsRecorded
    );
    ASSERT_TRUE(causticsTask.valid());

    const GpuTaskResourceUse surfelGiUses[] = {
        GpuTaskResourceUse{
            .resource = gbufferResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = slotsResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = surfelIrradianceResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    bool surfelGiRecorded = false;
    const GpuTaskId surfelGiTask = addProbeTask(
        Name("tests/descriptor_buffer/shadow_chain_surfel_gi"),
        "Surfel GI",
        computeQueueRequest,
        &causticsTask,
        1u,
        surfelGiUses,
        LengthOf(surfelGiUses),
        slotsBuffer.get(),
        ResourceStates::ConstantBuffer,
        gbuffer.get(),
        ResourceStates::ShaderResource,
        &surfelGiRecorded
    );
    ASSERT_TRUE(surfelGiTask.valid());

    const GpuTaskResourceUse lightingUses[] = {
        GpuTaskResourceUse{
            .resource = shadowVisibilityResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = causticIrradianceResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = surfelIrradianceResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = slotsResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = opaqueColorResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    bool lightingRecorded = false;
    const GpuTaskId lightingTask = addProbeTask(
        Name("tests/descriptor_buffer/shadow_chain_lighting"),
        "Deferred Lighting",
        computeQueueRequest,
        &surfelGiTask,
        1u,
        lightingUses,
        LengthOf(lightingUses),
        slotsBuffer.get(),
        ResourceStates::ConstantBuffer,
        shadowVisibility.get(),
        ResourceStates::ShaderResource,
        &lightingRecorded
    );
    ASSERT_TRUE(lightingTask.valid());

    const GpuTaskResourceUse compositeUses[] = {
        GpuTaskResourceUse{
            .resource = opaqueColorResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = slotsResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = compositeColorResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    bool compositeRecorded = false;
    const GpuTaskId compositeTask = addProbeTask(
        Name("tests/descriptor_buffer/shadow_chain_composite"),
        "Deferred Composite",
        computeQueueRequest,
        &lightingTask,
        1u,
        compositeUses,
        LengthOf(compositeUses),
        slotsBuffer.get(),
        ResourceStates::ConstantBuffer,
        opaqueColor.get(),
        ResourceStates::ShaderResource,
        &compositeRecorded
    );
    ASSERT_TRUE(compositeTask.valid());

    const GpuTaskResourceUse presentUses[] = {
        GpuTaskResourceUse{
            .resource = compositeColorResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = slotsResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    bool presentRecorded = false;
    const GpuTaskId presentTask = addProbeTask(
        Name("tests/descriptor_buffer/shadow_chain_present"),
        "Deferred Present",
        graphicsQueueRequest,
        &compositeTask,
        1u,
        presentUses,
        LengthOf(presentUses),
        slotsBuffer.get(),
        ResourceStates::ConstantBuffer,
        compositeColor.get(),
        ResourceStates::ShaderResource,
        &presentRecorded
    );
    ASSERT_TRUE(presentTask.valid());

    const GpuPhysicalQueueInfo queue{
        .id = BackendQueueId(device, CommandQueue::Graphics),
        .queueClass = CommandQueue::Graphics,
        .capabilities = static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
            | static_cast<u8>(GpuQueueCapability::Transfer)
        ),
        .familyIndex = device.getQueueFamilyIndex(CommandQueue::Graphics),
        .queueIndex = 0u,
        .dedicated = false,
    };
    const GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/shadow_chain_scratch"));
    const GpuTaskGraphCompiler compiler;
    ASSERT_TRUE(compiler.compile(graph, analysis, topology, assignments, compiledGraph, scratchArena));

    ASSERT_TRUE(analysis.hasExplicitEdge(shadowPrepareTask, graphicsPrefixTask));
    ASSERT_EQ(compiledGraph.packetCount(), 8u);
    const GpuSubmissionPacketId shadowPreparePacket = compiledGraph.packetForTask(shadowPrepareTask);
    const GpuSubmissionPacketId graphicsPrefixPacket = compiledGraph.packetForTask(graphicsPrefixTask);
    const GpuSubmissionPacketId shadowVisibilityPacket = compiledGraph.packetForTask(shadowVisibilityTask);
    const GpuSubmissionPacketId causticsPacket = compiledGraph.packetForTask(causticsTask);
    const GpuSubmissionPacketId surfelGiPacket = compiledGraph.packetForTask(surfelGiTask);
    const GpuSubmissionPacketId lightingPacket = compiledGraph.packetForTask(lightingTask);
    const GpuSubmissionPacketId compositePacket = compiledGraph.packetForTask(compositeTask);
    const GpuSubmissionPacketId presentPacket = compiledGraph.packetForTask(presentTask);
    ASSERT_TRUE(shadowPreparePacket.valid());
    ASSERT_TRUE(graphicsPrefixPacket.valid());
    ASSERT_TRUE(shadowVisibilityPacket.valid());
    ASSERT_TRUE(causticsPacket.valid());
    ASSERT_TRUE(surfelGiPacket.valid());
    ASSERT_TRUE(lightingPacket.valid());
    ASSERT_TRUE(compositePacket.valid());
    ASSERT_TRUE(presentPacket.valid());
    EXPECT_EQ(compiledGraph.packetIdAt(0u), shadowPreparePacket);
    EXPECT_EQ(compiledGraph.packetIdAt(1u), graphicsPrefixPacket);
    EXPECT_EQ(compiledGraph.packetIdAt(2u), shadowVisibilityPacket);
    EXPECT_EQ(compiledGraph.packetIdAt(3u), causticsPacket);
    EXPECT_EQ(compiledGraph.packetIdAt(4u), surfelGiPacket);
    EXPECT_EQ(compiledGraph.packetIdAt(5u), lightingPacket);
    EXPECT_EQ(compiledGraph.packetIdAt(6u), compositePacket);
    EXPECT_EQ(compiledGraph.packetIdAt(7u), presentPacket);

    const GpuCompiledTask* const compiledShadowPrepare = compiledGraph.findTask(shadowPrepareTask);
    const GpuCompiledTask* const compiledPrefix = compiledGraph.findTask(graphicsPrefixTask);
    ASSERT_NE(compiledShadowPrepare, nullptr);
    ASSERT_NE(compiledPrefix, nullptr);
    // slotsUploaded makes ConstantBuffer the authoritative imported state. Preparation must not add a stale
    // Common -> ConstantBuffer graph barrier, but its actual native final snapshot still seeds Prefix.
    EXPECT_EQ(graph.resourceAt(slotsResource.index).initialState, ResourceStates::ConstantBuffer);
    EXPECT_EQ(compiledShadowPrepare->prologueBarrierCount, 0u);
    ASSERT_GT(compiledPrefix->prologueStateSeedCount, 0u);
    const GpuPacketStateSeed* const graphicsPrefixStateSeeds = compiledGraph.taskPrologueStateSeeds(
        graphicsPrefixTask
    );
    ASSERT_NE(graphicsPrefixStateSeeds, nullptr);
    bool graphicsPrefixImportsPreparedSlots = false;
    for(usize index = 0u; index < compiledPrefix->prologueStateSeedCount; ++index){
        graphicsPrefixImportsPreparedSlots = graphicsPrefixImportsPreparedSlots
            || (
                graphicsPrefixStateSeeds[index].resource == slotsResource
                && graphicsPrefixStateSeeds[index].sourcePacket == shadowPreparePacket
            )
        ;
    }
    EXPECT_TRUE(graphicsPrefixImportsPreparedSlots);
    ASSERT_EQ(compiledGraph.packet(graphicsPrefixPacket).dependencyCount, 1u);
    const GpuPacketDependency* const graphicsPrefixDependencies = compiledGraph.packetDependencies(
        graphicsPrefixPacket
    );
    ASSERT_NE(graphicsPrefixDependencies, nullptr);
    EXPECT_EQ(graphicsPrefixDependencies[0u].producer, shadowPreparePacket);

    const GpuCompiledTask* const compiledShadowVisibility = compiledGraph.findTask(shadowVisibilityTask);
    ASSERT_NE(compiledShadowVisibility, nullptr);
    ASSERT_GT(compiledShadowVisibility->prologueStateSeedCount, 0u);
    const GpuPacketStateSeed* const shadowVisibilityStateSeeds = compiledGraph.taskPrologueStateSeeds(
        shadowVisibilityTask
    );
    ASSERT_NE(shadowVisibilityStateSeeds, nullptr);
    bool shadowVisibilityImportsPrefixSlots = false;
    for(usize index = 0u; index < compiledShadowVisibility->prologueStateSeedCount; ++index){
        shadowVisibilityImportsPrefixSlots = shadowVisibilityImportsPrefixSlots
            || (
                shadowVisibilityStateSeeds[index].resource == slotsResource
                && shadowVisibilityStateSeeds[index].sourcePacket == graphicsPrefixPacket
            )
        ;
    }
    EXPECT_TRUE(shadowVisibilityImportsPrefixSlots);
    ASSERT_EQ(compiledGraph.packet(shadowVisibilityPacket).dependencyCount, 2u);
    const GpuPacketDependency* const shadowVisibilityDependencies = compiledGraph.packetDependencies(
        shadowVisibilityPacket
    );
    ASSERT_NE(shadowVisibilityDependencies, nullptr);
    bool shadowVisibilityWaitsForPrepare = false;
    bool shadowVisibilityWaitsForPrefix = false;
    for(usize index = 0u; index < compiledGraph.packet(shadowVisibilityPacket).dependencyCount; ++index){
        shadowVisibilityWaitsForPrepare = shadowVisibilityWaitsForPrepare
            || shadowVisibilityDependencies[index].producer == shadowPreparePacket
        ;
        shadowVisibilityWaitsForPrefix = shadowVisibilityWaitsForPrefix
            || shadowVisibilityDependencies[index].producer == graphicsPrefixPacket
        ;
    }
    EXPECT_TRUE(shadowVisibilityWaitsForPrepare);
    EXPECT_TRUE(shadowVisibilityWaitsForPrefix);

    const GpuCompiledTask* const compiledPresent = compiledGraph.findTask(presentTask);
    ASSERT_NE(compiledPresent, nullptr);
    ASSERT_GT(compiledPresent->prologueStateSeedCount, 0u);
    const GpuPacketStateSeed* const presentStateSeeds = compiledGraph.taskPrologueStateSeeds(presentTask);
    ASSERT_NE(presentStateSeeds, nullptr);
    bool presentImportsCompositeState = false;
    for(usize index = 0u; index < compiledPresent->prologueStateSeedCount; ++index){
        presentImportsCompositeState = presentImportsCompositeState
            || (
                presentStateSeeds[index].resource == compositeColorResource
                && presentStateSeeds[index].sourcePacket == compositePacket
            )
        ;
    }
    EXPECT_TRUE(presentImportsCompositeState);
    ASSERT_EQ(compiledGraph.packet(presentPacket).dependencyCount, 2u);
    const GpuPacketDependency* const presentDependencies = compiledGraph.packetDependencies(presentPacket);
    ASSERT_NE(presentDependencies, nullptr);
    bool presentWaitsForPrepare = false;
    bool presentWaitsForComposite = false;
    for(usize index = 0u; index < compiledGraph.packet(presentPacket).dependencyCount; ++index){
        presentWaitsForPrepare = presentWaitsForPrepare
            || presentDependencies[index].producer == shadowPreparePacket
        ;
        presentWaitsForComposite = presentWaitsForComposite
            || presentDependencies[index].producer == compositePacket
        ;
    }
    EXPECT_TRUE(presentWaitsForPrepare);
    EXPECT_TRUE(presentWaitsForComposite);

    const GpuSubmissionPacketRange packetRange = compiledGraph.allPacketRange();
    ASSERT_TRUE(packetRange.valid());
    ASSERT_EQ(packetRange.packetCount, compiledGraph.packetCount());
    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        packetRange,
        nullptr,
        0u,
        recordedGraph
    ));
    EXPECT_TRUE(shadowPrepareRecorded);
    EXPECT_TRUE(graphicsPrefixRecorded);
    EXPECT_TRUE(shadowVisibilityRecorded);
    EXPECT_TRUE(causticsRecorded);
    EXPECT_TRUE(surfelGiRecorded);
    EXPECT_TRUE(lightingRecorded);
    EXPECT_TRUE(compositeRecorded);
    EXPECT_TRUE(presentRecorded);

    const CommandListResourceStateHandoff* const presentFinalState = recordedGraph.packetFinalStateSeed(presentPacket);
    ASSERT_NE(presentFinalState, nullptr);
    auto stateProbe = device.createCommandList();
    ASSERT_NE(stateProbe.get(), nullptr);
    stateProbe->open(presentFinalState);
    EXPECT_EQ(stateProbe->getBufferState(slotsBuffer.get()), ResourceStates::ConstantBuffer);
    EXPECT_EQ(
        stateProbe->getTextureSubresourceState(compositeColor.get(), 0u, 0u),
        ResourceStates::ShaderResource
    );
    stateProbe->close();

    const GpuTaskGraphSubmitter submitter(device);
    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        packetRange,
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    EXPECT_TRUE(transaction.packetToken(shadowPreparePacket).valid());
    EXPECT_TRUE(transaction.packetToken(graphicsPrefixPacket).valid());
    EXPECT_TRUE(transaction.packetToken(presentPacket).valid());
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
// Their identical local set ABI is only the shared 15-word photon push block; the fixed global-heap TLAS remains
// outside set 0.
TEST_F(DescriptorBufferRoundTripTest, CausticPhotonProducerShapesBuildAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/caustic_photon_producer_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    // CausticPhotonPushConstants is 15 scalar words and byte-identical for the SW and HW producers.
    BindingLayoutDesc swLayoutDesc(descArena);
    swLayoutDesc.setVisibility(ShaderType::Compute);
    swLayoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 15u));

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
    hwLayoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 15u));

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
// global heap. They differ only in their six-word hard and nine-word soft push selector ABIs.
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
        { "soft", 9u },
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


// All nine software-shadow kernels share one 21-word selector push ABI. Their G-buffer, context, output, scratch,
// and indirect resources are global-heap entries, leaving no local descriptor bindings.
TEST_F(DescriptorBufferRoundTripTest, SwShadowTraceShapeBuildsAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/sw_shadow_trace_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    BindingLayoutDesc layoutDesc(descArena);
    layoutDesc.setVisibility(ShaderType::Compute);
    layoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 21u));

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

    // TextureCube has a distinct image-view type from the other sampled-image arrays. The heap therefore gives it
    // its own appended ABI binding/class instead of exposing a cube through the Texture2D table.
    auto sampledImageCube = device.createTexture(
        TextureDesc()
            .setWidth(32u)
            .setHeight(32u)
            .setArraySize(6u)
            .setDimension(TextureDimension::TextureCube)
            .setFormat(Format::RGBA16_FLOAT)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    ASSERT_TRUE(sampledImageCube);
    EXPECT_EQ(sampledImageCube->getReferenceCount(), 1u);

    EXPECT_EQ(
        heap.getRegisterSlot(GpuDescriptorClass::SampledImageCube),
        NWB_BINDLESS_HEAP_BINDING_SAMPLED_IMAGE_CUBE
    );
    const GpuDescriptorHandle sampledImageCubeHandle = heap.allocate(GpuDescriptorClass::SampledImageCube);
    ASSERT_TRUE(sampledImageCubeHandle.valid());
    EXPECT_TRUE(heap.write(
        sampledImageCubeHandle,
        DescriptorWriteItem::Texture_SRV(
            0u,
            sampledImageCube.get(),
            Format::RGBA16_FLOAT,
            TextureSubresourceSet(0u, 1u, 0u, 6u),
            TextureDimension::TextureCube
        )
    )) << "heap TextureCube write() did not route through the descriptor-buffer path";
    EXPECT_EQ(sampledImageCube->getReferenceCount(), 2u)
        << "heap write() did not retain the persistent TextureCube resource";

    heap.free(sampledImageCubeHandle);
    EXPECT_EQ(sampledImageCube->getReferenceCount(), 2u)
        << "heap free() released the TextureCube resource before its in-flight quarantine matured";
    for(u32 frame = 0u; frame < s_MaxFramesInFlight; ++frame)
        heap.advanceFrame();
    EXPECT_EQ(sampledImageCube->getReferenceCount(), 1u)
        << "heap did not release the TextureCube resource after its in-flight quarantine matured";
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

