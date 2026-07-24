// Backend C round-trip proof — VK_EXT_descriptor_buffer (Phase 3, step 1c).
//
// This is the dark-manager hardware proof the GpuDescriptorHeap (Backend A) self-test was for Phase 1: stand up a
// real headless GPU device and exercise DescriptorBufferManager::allocate / writeDescriptor / free across every
// descriptor class, asserting the vkGetDescriptorEXT path actually produces descriptor bytes into the HOST-mapped
// segments and that the free-range sub-allocator (mirrored from Backend B) is sound.
//
// No pipeline consumes the manager yet; this test is its sole consumer until the Phase-3 binding-layer conversion
// (steps 2-4) lights it up. The GPU bind step (vkCmdBindDescriptorBuffersEXT / vkCmdSetDescriptorBufferOffsetsEXT)
// is deliberately not exercised here: CommandList exposes no VkCommandBuffer accessor and the conversion will add
// that path as production code, not a test-only hook (see .helper policy on production test hooks).
//
// GPU-optional host: the suite SKIPS (never fails) when the extension is absent — Backend A remains the
// portability floor on such runners, so its absence is an environment condition, not a regression.


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


// Brings up a real headless GPU device with the minimum dependency set Graphics requires, mirroring Core::Frame's
// construction. The device carries both the classic Backend-A descriptor heap and the Backend-C descriptor-buffer
// manager; createHeadlessDevice() creates no window/swap chain, so this runs on any host with a Vulkan driver.
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
        return m_graphics.createHeadlessDevice();
    }

    [[nodiscard]] Graphics& graphics(){ return m_graphics; }

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
        // The device-creation path emits log messages, and every NWB_LOGGER_* macro fatally asserts a logger is
        // installed (log.h:276). Register the capturing logger before bring-up so failures are recorded rather than
        // crashing the process, then keep it registered for the suite's lifetime.
        s_logger.emplace();
        s_loggerGuard.emplace(*s_logger);

        s_scope = MakeUnique<HeadlessGraphicsScope>();
        const bool initialized = s_scope->initialize();

        // No usable Vulkan device on this host -> skip the whole suite. Reported as SKIPPED, not failed.
        if(!initialized){
            GTEST_SKIP() << "Backend C round-trip: no usable headless Vulkan device on this host; skipping suite.";
            return;
        }

        auto* const device = s_scope->graphics().getDevice();
        ASSERT_NE(device, nullptr);
        auto& mgr = device->getDescriptorBufferManager();

        if(!mgr.isEnabled()){
            // The extension is genuinely absent (e.g. a non-BC-250 runner). Backend A is the portability floor there;
            // absence is not a regression. Skip the suite rather than report a failure.
            GTEST_SKIP() << "Backend C round-trip: VK_EXT_descriptor_buffer not enabled on this device; "
                            "Backend A remains the portability floor. Skipping suite.";
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

    const u32 stride = mgr.getDescriptorStride(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    ASSERT_GT(stride, 0u);

    const auto segment = mgr.allocate(GraphicsBackend::DescriptorBufferSegmentKind::Resource, stride, stride);
    ASSERT_TRUE(segment.valid());

    // The authoritative round-trip signal is writeDescriptor's return: a failed vkGetDescriptorEXT returns false and
    // logs at ERROR (the capturing logger would surface it). Byte-level inspection of the mapped segment is private
    // to the manager; the conversion trusts the return value plus the non-zero-stride gate below.
    const BindingSetItem item = BindingSetItem::RawBuffer_UAV(0u, storageBuffer.get());
    const bool wrote = mgr.writeDescriptor(item, segment.offsetBytes, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
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

    const u32 stride = mgr.getDescriptorStride(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    ASSERT_GT(stride, 0u);

    const auto segment = mgr.allocate(GraphicsBackend::DescriptorBufferSegmentKind::Resource, stride, stride);
    ASSERT_TRUE(segment.valid());

    const BindingSetItem item = BindingSetItem::ConstantBuffer(0u, constantBuffer.get());
    const bool wrote = mgr.writeDescriptor(item, segment.offsetBytes, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
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

    const u32 stride = mgr.getDescriptorStride(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    ASSERT_GT(stride, 0u);

    const auto segment = mgr.allocate(GraphicsBackend::DescriptorBufferSegmentKind::Resource, stride, stride);
    ASSERT_TRUE(segment.valid());

    const BindingSetItem item = BindingSetItem::Texture_SRV(0u, texture.get());
    const bool wrote = mgr.writeDescriptor(item, segment.offsetBytes, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
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

    const u32 stride = mgr.getDescriptorStride(VK_DESCRIPTOR_TYPE_SAMPLER);
    ASSERT_GT(stride, 0u);

    const auto segment = mgr.allocate(GraphicsBackend::DescriptorBufferSegmentKind::Sampler, stride, stride);
    ASSERT_TRUE(segment.valid());
    EXPECT_EQ(segment.kind, GraphicsBackend::DescriptorBufferSegmentKind::Sampler);

    const BindingSetItem item = BindingSetItem::Sampler(0u, sampler.get());
    const bool wrote = mgr.writeDescriptor(item, segment.offsetBytes, VK_DESCRIPTOR_TYPE_SAMPLER);
    EXPECT_TRUE(wrote);

    mgr.free(segment);
}


// Free-list reuse: allocate a range, free it, allocate the same size again — the second carve must be satisfied from
// the free list (mirroring Backend B's policy) and succeed. This is the sub-allocator correctness check the dark
// manager needs before live wiring can trust allocate/free churn across frames.
TEST_F(DescriptorBufferRoundTripTest, FreeListReusesFreedRange){
    auto& mgr = manager();

    const u32 stride = mgr.getDescriptorStride(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    ASSERT_GT(stride, 0u);

    const auto first = mgr.allocate(GraphicsBackend::DescriptorBufferSegmentKind::Resource, stride, stride);
    ASSERT_TRUE(first.valid());

    mgr.free(first);

    const auto second = mgr.allocate(GraphicsBackend::DescriptorBufferSegmentKind::Resource, stride, stride);
    ASSERT_TRUE(second.valid());
    // Same offset: the freed range was the head of the free list and exactly matched the request.
    EXPECT_EQ(second.offsetBytes, first.offsetBytes);

    mgr.free(second);
}


// getDescriptorStride is the per-type footprint strided up to descriptorBufferOffsetAlignment. Every type the
// conversion routes through writeDescriptor must report a non-zero stride; a zero stride would make allocate() carve
// zero bytes and writeDescriptor() reject the write. Cover the full set the binding layer will exercise.
TEST_F(DescriptorBufferRoundTripTest, EveryDescriptorTypeReportsNonZeroStride){
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
        EXPECT_GT(mgr.getDescriptorStride(type), 0u)
            << "descriptor type " << static_cast<u32>(type) << " reported a zero stride";
        EXPECT_GT(mgr.getDescriptorSize(type), 0u)
            << "descriptor type " << static_cast<u32>(type) << " reported a zero size";
    }
}


// Alignment: carve two adjacent ranges and confirm each offset is stride-aligned, and the second does not overlap the
// first. This is the invariant vkCmdSetDescriptorBufferOffsetsEXT's offset argument must honor, so the allocator must
// never hand back a misaligned or overlapping offset.
TEST_F(DescriptorBufferRoundTripTest, AllocationsAreStrideAligned){
    auto& mgr = manager();

    const u32 stride = mgr.getDescriptorStride(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    ASSERT_GT(stride, 0u);

    const auto a = mgr.allocate(GraphicsBackend::DescriptorBufferSegmentKind::Resource, stride, stride);
    const auto b = mgr.allocate(GraphicsBackend::DescriptorBufferSegmentKind::Resource, stride, stride);
    ASSERT_TRUE(a.valid());
    ASSERT_TRUE(b.valid());

    EXPECT_EQ(a.offsetBytes % stride, 0u);
    EXPECT_EQ(b.offsetBytes % stride, 0u);
    EXPECT_GE(b.offsetBytes, a.offsetBytes + a.sizeBytes);

    mgr.free(a);
    mgr.free(b);
}


// Step 2 (Phase 3) production-binding-layer proof: exercise createBindingLayout/createBindingSet through the live
// device API with the EXACT shape of the caustic resolve pass — 5 Texture_SRVs + 1 Texture_UAV + push constants,
// useDescriptorBuffer = true. This is the first end-to-end exercise of the Backend-C binding layer (the prior cases
// only round-trip the manager directly): it verifies (1) the layout is marked descriptor-buffer-compatible (the
// segment-coherence + driver size/offset queries succeed), (2) its set block reports a non-zero driver-queried size,
// (3) createBindingSet carves the block from the live resource segment and writes all 6 texture descriptors through
// the production writeDescriptor path, and (4) the resulting set/layout are wired for the command bind path. The
// caustic pass is segment-coherent pure-resource with push constants, which the Backend-C path serves wholesale; a
// mixed or unsupported shape would downgrade the layout and fail the compatibility assertion.
TEST_F(DescriptorBufferRoundTripTest, CausticResolveShapeBuildsAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();

    // A short-lived arena for the layout/set descs (they copy their bindings into object-arena storage on creation).
    static constexpr Name kDescArenaName{"tests/descriptor_buffer/caustic_shape_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    // Six textures matching caustic resolve's 5 SRV + 1 UAV (+ the geometry + input-color ping-pong siblings). All
    // RGBA16F-ish 2D textures so their image views are valid descriptor targets.
    auto makeTexture = [&](const u32 w, const u32 h) {
        return device.createTexture(
            TextureDesc()
                .setWidth(w)
                .setHeight(h)
                .setFormat(Format::RGBA16_FLOAT)
                .setInitialState(ResourceStates::ShaderResource)
                .setKeepInitialState(true)
        );
    };
    auto makeUavTexture = [&](const u32 w, const u32 h) {
        return device.createTexture(
            TextureDesc()
                .setWidth(w)
                .setHeight(h)
                .setFormat(Format::RGBA16_FLOAT)
                .setInitialState(ResourceStates::UnorderedAccess)
                .setKeepInitialState(true)
        );
    };

    auto accumulator = makeTexture(32u, 32u);
    auto worldPosition = makeTexture(32u, 32u);
    auto depth = makeTexture(32u, 32u);
    auto output = makeUavTexture(32u, 32u);   // UAV (wavelet output)
    auto inputColor = makeTexture(32u, 32u);
    auto geometry = makeTexture(32u, 32u);
    ASSERT_TRUE(accumulator && worldPosition && depth && output && inputColor && geometry);

    // The caustic resolve binding layout: 5 SRV + 1 UAV + push constants, opting into the descriptor-buffer path.
    // Slots mirror resolve_binding_slots.h (0..5); push constants ride the pipeline layout, allowed alongside.
    BindingLayoutDesc layoutDesc(descArena);
    layoutDesc.setVisibility(ShaderType::Compute);
    layoutDesc.setUseDescriptorBuffer(true);
    layoutDesc.addItem(BindingLayoutItem::Texture_SRV(0u, 1u));
    layoutDesc.addItem(BindingLayoutItem::Texture_SRV(1u, 1u));
    layoutDesc.addItem(BindingLayoutItem::Texture_SRV(2u, 1u));
    layoutDesc.addItem(BindingLayoutItem::Texture_UAV(3u, 1u));
    layoutDesc.addItem(BindingLayoutItem::Texture_SRV(4u, 1u));
    layoutDesc.addItem(BindingLayoutItem::Texture_SRV(5u, 1u));
    layoutDesc.addItem(BindingLayoutItem::PushConstants(0u, 32u));

    auto layout = device.createBindingLayout(layoutDesc);
    ASSERT_NE(layout.get(), nullptr);

    // The authoritative step-2 signal: the layout is descriptor-buffer-compatible. On a host where the extension is
    // absent the suite is already skipped (SetUpTestSuite); here the extension is present, so this shape MUST route
    // to Backend C. A downgrade here would mean the live caustic pass silently fell back to Backend A.
    ASSERT_TRUE(layout->isDescriptorBufferCompatible())
        << "caustic resolve shape did not route to the descriptor-buffer path";
    EXPECT_GT(layout->getDescriptorBufferSetSizeBytes(), 0u);
    EXPECT_EQ(layout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::Resource);
    // Every non-push-constant binding must have a driver-queried layout offset within the set block.
    const auto& offsets = layout->getDescriptorBufferBindingOffsets();
    EXPECT_EQ(offsets.size(), 6u);

    // A binding set carved against this layout exercises the production carve + 6 writeDescriptor calls.
    BindingSetDesc setDesc(descArena);
    setDesc.addItem(BindingSetItem::Texture_SRV(0u, accumulator.get(), Format::RGBA16_FLOAT));
    setDesc.addItem(BindingSetItem::Texture_SRV(1u, worldPosition.get(), Format::RGBA16_FLOAT));
    setDesc.addItem(BindingSetItem::Texture_SRV(2u, depth.get(), Format::RGBA16_FLOAT));
    setDesc.addItem(BindingSetItem::Texture_UAV(3u, output.get(), Format::RGBA16_FLOAT));
    setDesc.addItem(BindingSetItem::Texture_SRV(4u, inputColor.get(), Format::RGBA16_FLOAT));
    setDesc.addItem(BindingSetItem::Texture_SRV(5u, geometry.get(), Format::RGBA16_FLOAT));

    auto bindingSet = device.createBindingSet(setDesc, layout);
    // createBindingSet carved the set block and wrote all 6 descriptors; a failure here would have logged at ERROR.
    ASSERT_NE(bindingSet.get(), nullptr);
    EXPECT_EQ(bindingSet->getLayout(), layout.get());
}


// Caustic geometry downsample is the second pass flipped onto Backend C. Its shape (2 texture SRVs + 1 texture UAV +
// push constants, no samplers) is a distinct resource-only segment -- two SRVs into one view is a worthwhile addition
// to the resolve case, which interleaves the single UAV mid-set. Mirrors the resolve parity proof.
TEST_F(DescriptorBufferRoundTripTest, CausticGeometryDownsampleShapeBuildsAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/caustic_geom_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    auto makeTexture = [&](const u32 w, const u32 h) {
        return device.createTexture(
            TextureDesc()
                .setWidth(w).setHeight(h)
                .setFormat(Format::RGBA16_FLOAT)
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

    auto worldPosition = makeTexture(32u, 32u);
    auto depth = makeTexture(32u, 32u);
    auto geometryOutput = makeUavTexture(32u, 32u);
    ASSERT_TRUE(worldPosition && depth && geometryOutput);

    // Slots mirror resolve_binding_slots.h geometry-downsample block (0..2); push constants ride the pipeline layout.
    BindingLayoutDesc layoutDesc(descArena);
    layoutDesc.setVisibility(ShaderType::Compute);
    layoutDesc.setUseDescriptorBuffer(true);
    layoutDesc.addItem(BindingLayoutItem::Texture_SRV(0u, 1u));
    layoutDesc.addItem(BindingLayoutItem::Texture_SRV(1u, 1u));
    layoutDesc.addItem(BindingLayoutItem::Texture_UAV(2u, 1u));
    layoutDesc.addItem(BindingLayoutItem::PushConstants(0u, 32u));

    auto layout = device.createBindingLayout(layoutDesc);
    ASSERT_NE(layout.get(), nullptr);

    ASSERT_TRUE(layout->isDescriptorBufferCompatible())
        << "caustic geometry downsample shape did not route to the descriptor-buffer path";
    EXPECT_GT(layout->getDescriptorBufferSetSizeBytes(), 0u);
    EXPECT_EQ(layout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::Resource);
    const auto& offsets = layout->getDescriptorBufferBindingOffsets();
    EXPECT_EQ(offsets.size(), 3u);

    BindingSetDesc setDesc(descArena);
    setDesc.addItem(BindingSetItem::Texture_SRV(0u, worldPosition.get(), Format::RGBA16_FLOAT));
    setDesc.addItem(BindingSetItem::Texture_SRV(1u, depth.get(), Format::RGBA16_FLOAT));
    setDesc.addItem(BindingSetItem::Texture_UAV(2u, geometryOutput.get(), Format::RGBA16_FLOAT));

    auto bindingSet = device.createBindingSet(setDesc, layout);
    ASSERT_NE(bindingSet.get(), nullptr);
    EXPECT_EQ(bindingSet->getLayout(), layout.get());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Caustic accumulator decay is the third pass flipped onto Backend C. Its shape (1 texture UAV + push constants, no
// samplers) is the minimal single-UAV case -- distinct from resolve (UAV interleaved mid-set) and geometry downsample
// (2 SRVs + 1 UAV). The UAV is an R32_UINT Texture2DArray (3 flux-channel slices), exercised faithfully here.
// Mirrors the resolve/geometry-downsample parity proofs.
TEST_F(DescriptorBufferRoundTripTest, CausticAccumulatorDecayShapeBuildsAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/caustic_decay_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    auto makeUavTextureArray = [&](const u32 w, const u32 h) {
        return device.createTexture(
            TextureDesc()
                .setWidth(w).setHeight(h)
                .setArraySize(3u)
                .setDimension(TextureDimension::Texture2DArray)
                .setFormat(Format::R32_UINT)
                .setInitialState(ResourceStates::UnorderedAccess)
                .setKeepInitialState(true)
        );
    };

    auto accumulator = makeUavTextureArray(32u, 32u);
    ASSERT_TRUE(accumulator);

    // Slots mirror resolve_binding_slots.h accumulator-decay block (0); push constants ride the pipeline layout.
    // sizeof(CausticAccumulatorDecayPushConstants) == 4 * u32 == 16 (matches the shader push-constant layout).
    BindingLayoutDesc layoutDesc(descArena);
    layoutDesc.setVisibility(ShaderType::Compute);
    layoutDesc.setUseDescriptorBuffer(true);
    layoutDesc.addItem(BindingLayoutItem::Texture_UAV(0u, 1u));
    layoutDesc.addItem(BindingLayoutItem::PushConstants(0u, 16u));

    auto layout = device.createBindingLayout(layoutDesc);
    ASSERT_NE(layout.get(), nullptr);

    ASSERT_TRUE(layout->isDescriptorBufferCompatible())
        << "caustic accumulator decay shape did not route to the descriptor-buffer path";
    EXPECT_GT(layout->getDescriptorBufferSetSizeBytes(), 0u);
    EXPECT_EQ(layout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::Resource);
    const auto& offsets = layout->getDescriptorBufferBindingOffsets();
    EXPECT_EQ(offsets.size(), 1u);

    BindingSetDesc setDesc(descArena);
    setDesc.addItem(BindingSetItem::Texture_UAV(0u, accumulator.get(), Format::R32_UINT));

    auto bindingSet = device.createBindingSet(setDesc, layout);
    ASSERT_NE(bindingSet.get(), nullptr);
    EXPECT_EQ(bindingSet->getLayout(), layout.get());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Surfel upsample is the fourth pass flipped onto Backend C and the first surfel-GI pass migrated. Its shape (3 texture
// SRVs + 1 texture UAV, no samplers) is the no-push-constant case -- distinct from the caustic passes, which all carry
// push constants. The half/full-res irradiance and G-buffer SRVs are RGBA16_FLOAT (HDR surfel irradiance + view-space
// attributes); the full-res surfelIrradiance UAV is RGBA16_FLOAT too. Mirrors the resolve/geometry-downsample/accumulator
// parity proofs.
TEST_F(DescriptorBufferRoundTripTest, SurfelUpsampleShapeBuildsAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/surfel_upsample_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    auto makeTexture = [&](const u32 w, const u32 h) {
        return device.createTexture(
            TextureDesc()
                .setWidth(w).setHeight(h)
                .setFormat(Format::RGBA16_FLOAT)
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

    auto halfIrradiance = makeTexture(32u, 32u);
    auto normal = makeTexture(32u, 32u);
    auto worldPosition = makeTexture(32u, 32u);
    auto output = makeUavTexture(32u, 32u);
    ASSERT_TRUE(halfIrradiance && normal && worldPosition && output);

    // Slots mirror surfel_binding_slots.h upsample block (0..3); NO push constants ride the pipeline layout (the
    // joint-bilinear filter is driven by the G-buffer alone), so the set is the pure 4-resource case.
    BindingLayoutDesc layoutDesc(descArena);
    layoutDesc.setVisibility(ShaderType::Compute);
    layoutDesc.setUseDescriptorBuffer(true);
    layoutDesc.addItem(BindingLayoutItem::Texture_SRV(0u, 1u));
    layoutDesc.addItem(BindingLayoutItem::Texture_SRV(1u, 1u));
    layoutDesc.addItem(BindingLayoutItem::Texture_SRV(2u, 1u));
    layoutDesc.addItem(BindingLayoutItem::Texture_UAV(3u, 1u));

    auto layout = device.createBindingLayout(layoutDesc);
    ASSERT_NE(layout.get(), nullptr);

    ASSERT_TRUE(layout->isDescriptorBufferCompatible())
        << "surfel upsample shape did not route to the descriptor-buffer path";
    EXPECT_GT(layout->getDescriptorBufferSetSizeBytes(), 0u);
    EXPECT_EQ(layout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::Resource);
    const auto& offsets = layout->getDescriptorBufferBindingOffsets();
    EXPECT_EQ(offsets.size(), 4u);

    BindingSetDesc setDesc(descArena);
    setDesc.addItem(BindingSetItem::Texture_SRV(0u, halfIrradiance.get(), Format::RGBA16_FLOAT));
    setDesc.addItem(BindingSetItem::Texture_SRV(1u, normal.get(), Format::RGBA16_FLOAT));
    setDesc.addItem(BindingSetItem::Texture_SRV(2u, worldPosition.get(), Format::RGBA16_FLOAT));
    setDesc.addItem(BindingSetItem::Texture_UAV(3u, output.get(), Format::RGBA16_FLOAT));

    auto bindingSet = device.createBindingSet(setDesc, layout);
    ASSERT_NE(bindingSet.get(), nullptr);
    EXPECT_EQ(bindingSet->getLayout(), layout.get());
}


// Surfel hash-build parity: the first surfel-GI pass migrated that carries a uniform buffer alongside storage buffers
// (1 ConstantBuffer + 2 StructuredBuffer_UAV), extending the texture-only shapes of the prior five migrations. Slots
// mirror surfel_binding_slots.h hash-build block (12..14). The layout is segment-coherent pure-resource with no
// samplers, so it routes to Backend C intact; the set is built once on persistent buffers in production. This proof
// exercises the live device API with hash-build's exact binding shape, asserting the layout routes to Backend C,
// reports a non-zero driver-queried set size, gives the binding 3 layout offsets, and createBindingSet carves the
// block and writes the uniform/storage descriptors through the production writeDescriptor path.
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

    // Slots mirror surfel_binding_slots.h hash-build block (12..14); the segment-coherent pure-resource layout (a
    // uniform buffer plus two storage buffers, no samplers) is the first Backend-C migration to mix CB + UAV.
    BindingLayoutDesc layoutDesc(descArena);
    layoutDesc.setVisibility(ShaderType::Compute);
    layoutDesc.setUseDescriptorBuffer(true);
    layoutDesc.addItem(BindingLayoutItem::ConstantBuffer(12u, 1u));
    layoutDesc.addItem(BindingLayoutItem::StructuredBuffer_UAV(13u, 1u));
    layoutDesc.addItem(BindingLayoutItem::StructuredBuffer_UAV(14u, 1u));

    auto layout = device.createBindingLayout(layoutDesc);
    ASSERT_NE(layout.get(), nullptr);

    ASSERT_TRUE(layout->isDescriptorBufferCompatible())
        << "surfel hash-build shape did not route to the descriptor-buffer path";
    EXPECT_GT(layout->getDescriptorBufferSetSizeBytes(), 0u);
    EXPECT_EQ(layout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::Resource);
    const auto& offsets = layout->getDescriptorBufferBindingOffsets();
    EXPECT_EQ(offsets.size(), 3u);

    BindingSetDesc setDesc(descArena);
    setDesc.addItem(BindingSetItem::ConstantBuffer(12u, constants.get()));
    setDesc.addItem(BindingSetItem::StructuredBuffer_UAV(13u, pool.get()));
    setDesc.addItem(BindingSetItem::StructuredBuffer_UAV(14u, cellHead.get()));

    auto bindingSet = device.createBindingSet(setDesc, layout);
    ASSERT_NE(bindingSet.get(), nullptr);
    EXPECT_EQ(bindingSet->getLayout(), layout.get());
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

    // Slots mirror surfel_binding_slots.h age-free block (12, 13, 15, 19); the segment-coherent pure-resource layout
    // (a uniform buffer plus three storage buffers, no samplers) is the CB + UAV shape of hash-build with one
    // additional storage buffer (the free-list).
    BindingLayoutDesc layoutDesc(descArena);
    layoutDesc.setVisibility(ShaderType::Compute);
    layoutDesc.setUseDescriptorBuffer(true);
    layoutDesc.addItem(BindingLayoutItem::ConstantBuffer(12u, 1u));
    layoutDesc.addItem(BindingLayoutItem::StructuredBuffer_UAV(13u, 1u));
    layoutDesc.addItem(BindingLayoutItem::StructuredBuffer_UAV(15u, 1u));
    layoutDesc.addItem(BindingLayoutItem::StructuredBuffer_UAV(19u, 1u));

    auto layout = device.createBindingLayout(layoutDesc);
    ASSERT_NE(layout.get(), nullptr);

    ASSERT_TRUE(layout->isDescriptorBufferCompatible())
        << "surfel age-free shape did not route to the descriptor-buffer path";
    EXPECT_GT(layout->getDescriptorBufferSetSizeBytes(), 0u);
    EXPECT_EQ(layout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::Resource);
    const auto& offsets = layout->getDescriptorBufferBindingOffsets();
    EXPECT_EQ(offsets.size(), 4u);

    BindingSetDesc setDesc(descArena);
    setDesc.addItem(BindingSetItem::ConstantBuffer(12u, constants.get()));
    setDesc.addItem(BindingSetItem::StructuredBuffer_UAV(13u, pool.get()));
    setDesc.addItem(BindingSetItem::StructuredBuffer_UAV(15u, counter.get()));
    setDesc.addItem(BindingSetItem::StructuredBuffer_UAV(19u, freeList.get()));

    auto bindingSet = device.createBindingSet(setDesc, layout);
    ASSERT_NE(bindingSet.get(), nullptr);
    EXPECT_EQ(bindingSet->getLayout(), layout.get());
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

    // Slots mirror surfel_binding_slots.h trace-build-args block (0, 1, 2); the segment-coherent pure-resource layout
    // (a uniform buffer plus two storage buffers, no samplers) is the minimal CB + UAV subset of the age-free shape --
    // its two storage buffers are the BUMP_TOP counter read and the DispatchIndirectArguments write.
    BindingLayoutDesc layoutDesc(descArena);
    layoutDesc.setVisibility(ShaderType::Compute);
    layoutDesc.setUseDescriptorBuffer(true);
    layoutDesc.addItem(BindingLayoutItem::ConstantBuffer(0u, 1u));
    layoutDesc.addItem(BindingLayoutItem::StructuredBuffer_UAV(1u, 1u));
    layoutDesc.addItem(BindingLayoutItem::StructuredBuffer_UAV(2u, 1u));

    auto layout = device.createBindingLayout(layoutDesc);
    ASSERT_NE(layout.get(), nullptr);

    ASSERT_TRUE(layout->isDescriptorBufferCompatible())
        << "surfel trace build-args shape did not route to the descriptor-buffer path";
    EXPECT_GT(layout->getDescriptorBufferSetSizeBytes(), 0u);
    EXPECT_EQ(layout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::Resource);
    const auto& offsets = layout->getDescriptorBufferBindingOffsets();
    EXPECT_EQ(offsets.size(), 3u);

    BindingSetDesc setDesc(descArena);
    setDesc.addItem(BindingSetItem::ConstantBuffer(0u, constants.get()));
    setDesc.addItem(BindingSetItem::StructuredBuffer_UAV(1u, counter.get()));
    setDesc.addItem(BindingSetItem::StructuredBuffer_UAV(2u, args.get()));

    auto bindingSet = device.createBindingSet(setDesc, layout);
    ASSERT_NE(bindingSet.get(), nullptr);
    EXPECT_EQ(bindingSet->getLayout(), layout.get());
}


// Surfel spawn parity: the surfel-GI pass that allocates new surfels into the pool. Its shape is segment-coherent
// pure-resource (1 ConstantBuffer + 4 StructuredBuffer_UAV + 2 Texture_SRV, no samplers) -- the age-free CB + UAV mix
// extended with the two G-buffer SRVs (world position + normal) the pass samples. Slots mirror surfel_binding_slots.h
// spawn block (12, 13, 14, 15, 19, 16, 17). The layout routes to Backend C intact; in production the set is rebuilt on
// G-buffer (resize) change against persistent surfel buffers. This proof exercises the live device API with spawn's
// exact binding shape, asserting the layout routes to Backend C, reports a non-zero driver-queried set size, gives the
// binding 7 layout offsets, and createBindingSet carves the block and writes the uniform/storage/texture descriptors
// through the production writeDescriptor path.
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
    auto makeTexture = [&](const u32 w, const u32 h) {
        return device.createTexture(
            TextureDesc()
                .setWidth(w).setHeight(h)
                .setFormat(Format::RGBA16_FLOAT)
                .setInitialState(ResourceStates::ShaderResource)
                .setKeepInitialState(true)
        );
    };

    auto constants = makeConstantBuffer();
    auto pool = makeStructuredUav(16u);
    auto cellHead = makeStructuredUav(4u);
    auto counter = makeStructuredUav(4u);
    auto freeList = makeStructuredUav(4u);
    auto worldPosition = makeTexture(32u, 32u);
    auto normal = makeTexture(32u, 32u);
    ASSERT_TRUE(constants && pool && cellHead && counter && freeList && worldPosition && normal);

    // Slots mirror surfel_binding_slots.h spawn block (12, 13, 14, 15, 19, 16, 17); the segment-coherent pure-resource
    // layout (a uniform buffer, four storage buffers, and two textures, no samplers) is the age-free CB + UAV mix
    // extended with the two G-buffer SRVs the pass samples.
    BindingLayoutDesc layoutDesc(descArena);
    layoutDesc.setVisibility(ShaderType::Compute);
    layoutDesc.setUseDescriptorBuffer(true);
    layoutDesc.addItem(BindingLayoutItem::ConstantBuffer(12u, 1u));
    layoutDesc.addItem(BindingLayoutItem::StructuredBuffer_UAV(13u, 1u));
    layoutDesc.addItem(BindingLayoutItem::StructuredBuffer_UAV(14u, 1u));
    layoutDesc.addItem(BindingLayoutItem::StructuredBuffer_UAV(15u, 1u));
    layoutDesc.addItem(BindingLayoutItem::StructuredBuffer_UAV(19u, 1u));
    layoutDesc.addItem(BindingLayoutItem::Texture_SRV(16u, 1u));
    layoutDesc.addItem(BindingLayoutItem::Texture_SRV(17u, 1u));

    auto layout = device.createBindingLayout(layoutDesc);
    ASSERT_NE(layout.get(), nullptr);

    ASSERT_TRUE(layout->isDescriptorBufferCompatible())
        << "surfel spawn shape did not route to the descriptor-buffer path";
    EXPECT_GT(layout->getDescriptorBufferSetSizeBytes(), 0u);
    EXPECT_EQ(layout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::Resource);
    const auto& offsets = layout->getDescriptorBufferBindingOffsets();
    EXPECT_EQ(offsets.size(), 7u);

    BindingSetDesc setDesc(descArena);
    setDesc.addItem(BindingSetItem::ConstantBuffer(12u, constants.get()));
    setDesc.addItem(BindingSetItem::StructuredBuffer_UAV(13u, pool.get()));
    setDesc.addItem(BindingSetItem::StructuredBuffer_UAV(14u, cellHead.get()));
    setDesc.addItem(BindingSetItem::StructuredBuffer_UAV(15u, counter.get()));
    setDesc.addItem(BindingSetItem::StructuredBuffer_UAV(19u, freeList.get()));
    setDesc.addItem(BindingSetItem::Texture_SRV(16u, worldPosition.get(), Format::RGBA16_FLOAT));
    setDesc.addItem(BindingSetItem::Texture_SRV(17u, normal.get(), Format::RGBA16_FLOAT));

    auto bindingSet = device.createBindingSet(setDesc, layout);
    ASSERT_NE(bindingSet.get(), nullptr);
    EXPECT_EQ(bindingSet->getLayout(), layout.get());
}


// Surfel resolve parity: the pass that gathers irradiance from the surfel pool into the half-res irradiance target.
// Its shape is segment-coherent pure-resource (1 ConstantBuffer + 2 StructuredBuffer_SRV + 2 Texture_SRV + 1
// Texture_UAV, no samplers): it reads the pool and cell-head built by the surfel passes, the G-buffer world position +
// normal, and writes the half-res irradiance. Slots mirror surfel_binding_slots.h resolve block (0..5). The layout
// routes to Backend C intact; in production the set is rebuilt on G-buffer / output (resize) change. This proof
// exercises the live device API with resolve's exact binding shape, asserting the layout routes to Backend C, reports
// a non-zero driver-queried set size, gives the binding 6 layout offsets, and createBindingSet carves the block and
// writes the uniform/storage/texture descriptors through the production writeDescriptor path.
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
    auto makeTexture = [&](const u32 w, const u32 h) {
        return device.createTexture(
            TextureDesc()
                .setWidth(w).setHeight(h)
                .setFormat(Format::RGBA16_FLOAT)
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
    auto worldPosition = makeTexture(32u, 32u);
    auto normal = makeTexture(32u, 32u);
    auto output = makeUavTexture(32u, 32u);
    ASSERT_TRUE(constants && pool && cellHead && worldPosition && normal && output);

    // Slots mirror surfel_binding_slots.h resolve block (0..5); the segment-coherent pure-resource layout (a uniform
    // buffer, two read-only storage buffers, two textures, and one storage texture, no samplers) is the first
    // migrated shape to mix StructuredBuffer_SRV with Texture_UAV in one segment.
    BindingLayoutDesc layoutDesc(descArena);
    layoutDesc.setVisibility(ShaderType::Compute);
    layoutDesc.setUseDescriptorBuffer(true);
    layoutDesc.addItem(BindingLayoutItem::ConstantBuffer(0u, 1u));
    layoutDesc.addItem(BindingLayoutItem::StructuredBuffer_SRV(1u, 1u));
    layoutDesc.addItem(BindingLayoutItem::StructuredBuffer_SRV(2u, 1u));
    layoutDesc.addItem(BindingLayoutItem::Texture_SRV(3u, 1u));
    layoutDesc.addItem(BindingLayoutItem::Texture_SRV(4u, 1u));
    layoutDesc.addItem(BindingLayoutItem::Texture_UAV(5u, 1u));

    auto layout = device.createBindingLayout(layoutDesc);
    ASSERT_NE(layout.get(), nullptr);

    ASSERT_TRUE(layout->isDescriptorBufferCompatible())
        << "surfel resolve shape did not route to the descriptor-buffer path";
    EXPECT_GT(layout->getDescriptorBufferSetSizeBytes(), 0u);
    EXPECT_EQ(layout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::Resource);
    const auto& offsets = layout->getDescriptorBufferBindingOffsets();
    EXPECT_EQ(offsets.size(), 6u);

    BindingSetDesc setDesc(descArena);
    setDesc.addItem(BindingSetItem::ConstantBuffer(0u, constants.get()));
    setDesc.addItem(BindingSetItem::StructuredBuffer_SRV(1u, pool.get()));
    setDesc.addItem(BindingSetItem::StructuredBuffer_SRV(2u, cellHead.get()));
    setDesc.addItem(BindingSetItem::Texture_SRV(3u, worldPosition.get(), Format::RGBA16_FLOAT));
    setDesc.addItem(BindingSetItem::Texture_SRV(4u, normal.get(), Format::RGBA16_FLOAT));
    setDesc.addItem(BindingSetItem::Texture_UAV(5u, output.get(), Format::RGBA16_FLOAT));

    auto bindingSet = device.createBindingSet(setDesc, layout);
    ASSERT_NE(bindingSet.get(), nullptr);
    EXPECT_EQ(bindingSet->getLayout(), layout.get());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


}; // namespace Tests


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END
