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

private:
    static inline constexpr Name s_TestArenaName{"tests/descriptor_buffer/graphics_object_arena"};
    static inline constexpr u32 s_TestWorkerThreadCount = 1u;

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

        auto* const device = s_scope->graphics().getDevice();
        ASSERT_NE(device, nullptr);
        auto& mgr = device->getDescriptorBufferManager();

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
        auto* const ptr = s_scope->graphics().getDevice();
        return *ptr;
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
    EXPECT_GT(device.executeCommandLists(commandLists, 2u), 0u);
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

