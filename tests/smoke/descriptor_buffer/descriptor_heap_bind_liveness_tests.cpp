// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <global/global.h>
#include <global/unique_ptr.h>
#include <core/common/module.h>
#include <core/graphics/api.h>
#include <core/graphics/rhi/queue_sharing.h>
#include <core/graphics/vulkan/backend.h>
#include <impl/assets/graphics/bindless/runtime_abi.h>
#include <tests/common/capturing_logger.h>
#include <tests/common/headless_graphics_scope.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core;

// Minimal Vulkan 1.3 shader: `void main(){}`.
static constexpr u32 s_DescriptorHeapLivenessComputeSpirv[] = {
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static GpuDescriptorHeapDesc MakeLivenessHeapDesc(){
    GpuDescriptorHeapDesc desc;
    desc.setBindlessHeapAbi(Impl::AssetsGraphicsBindless::MakeGpuDescriptorHeapAbi());
    return desc;
}

[[nodiscard]] static ComputePipelineHandle CreateLivenessComputePipeline(
    GraphicsBackend::Device& device,
    Alloc::GlobalArena& arena,
    const BindingLayoutHandle* const layouts,
    const usize layoutCount
){
    ShaderDesc shaderDesc(arena);
    shaderDesc
        .setShaderType(ShaderType::Compute)
        .setDebugName(Name("tests/descriptor_buffer/heap_bind_liveness_compute"))
    ;
    ShaderHandle shader = device.createShader(
        shaderDesc,
        s_DescriptorHeapLivenessComputeSpirv,
        sizeof(s_DescriptorHeapLivenessComputeSpirv)
    );
    if(!shader)
        return nullptr;

    ComputePipelineDesc pipelineDesc;
    pipelineDesc.setComputeShader(shader);
    for(usize layoutIndex = 0u; layoutIndex < layoutCount; ++layoutIndex)
        pipelineDesc.addBindingLayout(layouts[layoutIndex]);
    return device.createComputePipeline(pipelineDesc);
}

static void ExpectLivenessHeapStatisticsEqual(
    const GpuDescriptorHeapLifecycleStatistics& expected,
    const GpuDescriptorHeapLifecycleStatistics& actual
){
    EXPECT_EQ(actual.initialized, expected.initialized);
    EXPECT_EQ(actual.resourceCapacity, expected.resourceCapacity);
    EXPECT_EQ(actual.samplerCapacity, expected.samplerCapacity);
    EXPECT_EQ(actual.accelStructCapacity, expected.accelStructCapacity);
    EXPECT_EQ(actual.resourceLiveSlotCount, expected.resourceLiveSlotCount);
    EXPECT_EQ(actual.samplerLiveSlotCount, expected.samplerLiveSlotCount);
    EXPECT_EQ(actual.accelStructLiveSlotCount, expected.accelStructLiveSlotCount);
    EXPECT_EQ(actual.pendingRetiredSlotCount, expected.pendingRetiredSlotCount);
    EXPECT_EQ(actual.acceptedHeapUseCount, expected.acceptedHeapUseCount);
    EXPECT_EQ(actual.unsubmittedHeapUseCount, expected.unsubmittedHeapUseCount);
    EXPECT_EQ(actual.abandonedHeapUseCount, expected.abandonedHeapUseCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class DescriptorHeapBindLivenessTest : public ::testing::Test{
protected:
    static void SetUpTestSuite(){
        s_logger.emplace();
        s_loggerGuard.emplace(*s_logger);
        s_scope = MakeUnique<HeadlessGraphicsScope>();
        if(!s_scope->initialize())
            return;

        s_runtimeInitialized = true;
        auto& localDevice = device();
        s_ready = localDevice.getDescriptorBufferManager().isEnabled()
            && localDevice.getDescriptorHeap().isInitialized()
        ;
    }

    static void TearDownTestSuite(){
        s_scope.reset();
        if(s_runtimeInitialized && s_logger.has_value()){
            EXPECT_FALSE(s_logger->sawMessageContaining(NWB_TEXT("Vulkan debug: [severity=error")))
                << "validation-enabled descriptor-heap liveness tests emitted a Vulkan error";
        }
        s_loggerGuard.reset();
        s_logger.reset();
        s_runtimeInitialized = false;
        s_ready = false;
    }

    [[nodiscard]] static GraphicsBackend::Device& device(){ return s_scope->graphics().getDevice(); }
    [[nodiscard]] static Alloc::GlobalArena& arena(){ return s_scope->arena(); }

    virtual void SetUp()override{
        if(!s_ready)
            GTEST_SKIP() << "Descriptor-heap bind liveness: no usable descriptor-buffer headless device.";
    }

    virtual void TearDown()override{
        if(!s_ready)
            return;

        auto& localDevice = device();
        auto& heap = localDevice.getDescriptorHeap();
        auto& manager = localDevice.getDescriptorBufferManager();
        EXPECT_TRUE(localDevice.waitForIdle());
        heap.collectRetired();
        heap.shutdown();
        EXPECT_FALSE(heap.isInitialized());
        if(!manager.isEnabled())
            EXPECT_TRUE(manager.initialize());
        EXPECT_TRUE(heap.initialize(MakeLivenessHeapDesc()));
    }


protected:
    static bool s_runtimeInitialized;
    static bool s_ready;
    static UniquePtr<HeadlessGraphicsScope> s_scope;
    static Optional<CapturingLogger> s_logger;
    static Optional<Common::LoggerRegistrationGuard> s_loggerGuard;
};

bool DescriptorHeapBindLivenessTest::s_runtimeInitialized = false;
bool DescriptorHeapBindLivenessTest::s_ready = false;
UniquePtr<HeadlessGraphicsScope> DescriptorHeapBindLivenessTest::s_scope;
Optional<CapturingLogger> DescriptorHeapBindLivenessTest::s_logger;
Optional<Common::LoggerRegistrationGuard> DescriptorHeapBindLivenessTest::s_loggerGuard;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(DescriptorHeapBindLivenessTest, PersistentSegmentTupleForgeryRejectsWithoutHeapUse){
    auto& localDevice = device();
    auto& heap = localDevice.getDescriptorHeap();
    const BindingLayoutHandle layouts[] = { heap.getResourceLayout(), heap.getSamplerLayout() };
    ComputePipelineHandle pipeline = CreateLivenessComputePipeline(localDevice, arena(), layouts, LengthOf(layouts));
    ASSERT_TRUE(pipeline);
    const GpuDescriptorHeapLifecycleStatistics baseline = heap.lifecycleStatistics();

    {
        CommandListHandle commandList = localDevice.createCommandList();
        ASSERT_TRUE(commandList);
        commandList->open();
        commandList->setComputeState(ComputeState().setPipeline(pipeline.get()));
        const GraphicsBackend::DescriptorBufferSegment savedBlock = heap.getResourceBufferBlock();
        GraphicsBackend::DescriptorBufferSegment& mutableBlock =
            const_cast<GraphicsBackend::DescriptorBufferSegment&>(heap.getResourceBufferBlock());
        mutableBlock.allocationSerial = savedBlock.allocationSerial == s_MaxU64
            ? savedBlock.allocationSerial - 1u
            : savedBlock.allocationSerial + 1u
        ;
        heap.bindCompute(*commandList, *pipeline);
        mutableBlock = savedBlock;
        EXPECT_TRUE(commandList->commandRecordingFailed());
        commandList->close();
    }
    ExpectLivenessHeapStatisticsEqual(baseline, heap.lifecycleStatistics());

    {
        CommandListHandle commandList = localDevice.createCommandList();
        ASSERT_TRUE(commandList);
        commandList->open();
        commandList->setComputeState(ComputeState().setPipeline(pipeline.get()));
        const GraphicsBackend::DescriptorBufferSegment savedBlock = heap.getSamplerBufferBlock();
        GraphicsBackend::DescriptorBufferSegment& mutableBlock =
            const_cast<GraphicsBackend::DescriptorBufferSegment&>(heap.getSamplerBufferBlock());
        mutableBlock.storageIdentity = savedBlock.storageIdentity == s_MaxU64
            ? savedBlock.storageIdentity - 1u
            : savedBlock.storageIdentity + 1u
        ;
        heap.bindCompute(*commandList, *pipeline);
        mutableBlock = savedBlock;
        EXPECT_TRUE(commandList->commandRecordingFailed());
        commandList->close();
    }
    ExpectLivenessHeapStatisticsEqual(baseline, heap.lifecycleStatistics());

    CommandListHandle commandList = localDevice.createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    commandList->setComputeState(ComputeState().setPipeline(pipeline.get()));
    heap.bindCompute(*commandList, *pipeline);
    ASSERT_FALSE(commandList->commandRecordingFailed());
    commandList->close();
    CommandList* commandLists[] = { commandList.get() };
    const QueueSubmissionToken token = localDevice.executeCommandLists(
        commandLists,
        LengthOf(commandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(token.valid());
    ASSERT_TRUE(localDevice.waitForIdle());
    heap.collectRetired();
    ExpectLivenessHeapStatisticsEqual(baseline, heap.lifecycleStatistics());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(DescriptorHeapBindLivenessTest, RetainedBufferAndTextureMustRemainReadyAtBind){
    auto& localDevice = device();
    auto& heap = localDevice.getDescriptorHeap();
    const BindingLayoutHandle layouts[] = { heap.getResourceLayout(), heap.getSamplerLayout() };
    ComputePipelineHandle pipeline = CreateLivenessComputePipeline(localDevice, arena(), layouts, LengthOf(layouts));
    ASSERT_TRUE(pipeline);
    const GpuDescriptorHeapLifecycleStatistics baseline = heap.lifecycleStatistics();

    BufferHandle buffer = localDevice.createBuffer(
        BufferDesc()
            .setByteSize(4096u)
            .setStructStride(16u)
            .setCanHaveUAVs(true)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    TextureHandle texture = localDevice.createTexture(
        TextureDesc()
            .setWidth(8u)
            .setHeight(8u)
            .setFormat(Format::RGBA8_UNORM)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    ASSERT_TRUE(buffer);
    ASSERT_TRUE(texture);
    const GpuDescriptorHandle bufferHandle = heap.allocate(GpuDescriptorClass::StorageBuffer);
    const GpuDescriptorHandle textureHandle = heap.allocate(GpuDescriptorClass::SampledImage);
    ASSERT_TRUE(bufferHandle.valid());
    ASSERT_TRUE(textureHandle.valid());
    ASSERT_TRUE(heap.write(bufferHandle, DescriptorWriteItem::StructuredBuffer_UAV(0u, buffer.get())));
    ASSERT_TRUE(heap.write(textureHandle, DescriptorWriteItem::Texture_SRV(0u, texture.get())));
    ASSERT_EQ(buffer->getReferenceCount(), 2u);
    ASSERT_EQ(texture->getReferenceCount(), 2u);
    const GpuDescriptorHeapLifecycleStatistics populated = heap.lifecycleStatistics();

    {
        CommandListHandle commandList = localDevice.createCommandList();
        ASSERT_TRUE(commandList);
        commandList->open();
        commandList->setComputeState(ComputeState().setPipeline(pipeline.get()));
        BufferDesc& mutableDesc = const_cast<BufferDesc&>(buffer->getDescription());
        mutableDesc.isVirtual = true;
        heap.bindCompute(*commandList, *pipeline);
        mutableDesc.isVirtual = false;
        EXPECT_TRUE(commandList->commandRecordingFailed());
        commandList->close();
    }
    ExpectLivenessHeapStatisticsEqual(populated, heap.lifecycleStatistics());

    {
        CommandListHandle commandList = localDevice.createCommandList();
        ASSERT_TRUE(commandList);
        commandList->open();
        commandList->setComputeState(ComputeState().setPipeline(pipeline.get()));
        TextureDesc& mutableDesc = const_cast<TextureDesc&>(texture->getDescription());
        mutableDesc.isVirtual = true;
        heap.bindCompute(*commandList, *pipeline);
        mutableDesc.isVirtual = false;
        EXPECT_TRUE(commandList->commandRecordingFailed());
        commandList->close();
    }
    ExpectLivenessHeapStatisticsEqual(populated, heap.lifecycleStatistics());

    {
        CommandListHandle commandList = localDevice.createCommandList();
        ASSERT_TRUE(commandList);
        commandList->open();
        commandList->setComputeState(ComputeState().setPipeline(pipeline.get()));
        heap.bindCompute(*commandList, *pipeline);
        ASSERT_FALSE(commandList->commandRecordingFailed());
        TextureDesc& mutableDesc = const_cast<TextureDesc&>(texture->getDescription());
        mutableDesc.isVirtual = true;
        commandList->close();
        mutableDesc.isVirtual = false;
        EXPECT_TRUE(commandList->commandRecordingFailed());
    }
    heap.collectRetired();
    ExpectLivenessHeapStatisticsEqual(populated, heap.lifecycleStatistics());

    {
        CommandListHandle commandList = localDevice.createCommandList();
        ASSERT_TRUE(commandList);
        commandList->open();
        commandList->setComputeState(ComputeState().setPipeline(pipeline.get()));
        heap.bindCompute(*commandList, *pipeline);
        ASSERT_FALSE(commandList->commandRecordingFailed());
        commandList->close();
        TextureDesc& mutableDesc = const_cast<TextureDesc&>(texture->getDescription());
        mutableDesc.isVirtual = true;
        CommandList* commandLists[] = { commandList.get() };
        const QueueSubmissionToken token = localDevice.executeCommandLists(
            commandLists,
            LengthOf(commandLists),
            CommandQueue::Graphics,
            QueueSubmissionDesc{}
        );
        mutableDesc.isVirtual = false;
        EXPECT_FALSE(token.valid());
    }
    heap.collectRetired();
    ExpectLivenessHeapStatisticsEqual(populated, heap.lifecycleStatistics());

    CommandListHandle commandList = localDevice.createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    commandList->setComputeState(ComputeState().setPipeline(pipeline.get()));
    heap.bindCompute(*commandList, *pipeline);
    ASSERT_FALSE(commandList->commandRecordingFailed());
    commandList->close();
    CommandList* commandLists[] = { commandList.get() };
    const QueueSubmissionToken token = localDevice.executeCommandLists(
        commandLists,
        LengthOf(commandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(token.valid());
    ASSERT_TRUE(localDevice.waitForIdle());
    heap.collectRetired();
    heap.free(bufferHandle);
    heap.free(textureHandle);
    heap.collectRetired();
    EXPECT_EQ(buffer->getReferenceCount(), 1u);
    EXPECT_EQ(texture->getReferenceCount(), 1u);
    ExpectLivenessHeapStatisticsEqual(baseline, heap.lifecycleStatistics());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(DescriptorHeapBindLivenessTest, PostBindTextureWritesJoinCloseAndSubmissionRevalidation){
    auto& localDevice = device();
    auto& heap = localDevice.getDescriptorHeap();
    const BindingLayoutHandle layouts[] = { heap.getResourceLayout(), heap.getSamplerLayout() };
    ComputePipelineHandle pipeline = CreateLivenessComputePipeline(localDevice, arena(), layouts, LengthOf(layouts));
    ASSERT_TRUE(pipeline);
    const GpuDescriptorHeapLifecycleStatistics baseline = heap.lifecycleStatistics();

    TextureHandle closeTexture = localDevice.createTexture(
        TextureDesc()
            .setWidth(8u)
            .setHeight(8u)
            .setFormat(Format::RGBA8_UNORM)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    ASSERT_TRUE(closeTexture);
    const GpuDescriptorHandle closeHandle = heap.allocate(GpuDescriptorClass::SampledImage);
    ASSERT_TRUE(closeHandle.valid());
    {
        CommandListHandle commandList = localDevice.createCommandList();
        ASSERT_TRUE(commandList);
        commandList->open();
        commandList->setComputeState(ComputeState().setPipeline(pipeline.get()));
        heap.bindCompute(*commandList, *pipeline);
        ASSERT_FALSE(commandList->commandRecordingFailed());
        ASSERT_TRUE(heap.write(closeHandle, DescriptorWriteItem::Texture_SRV(0u, closeTexture.get())));

        TextureDesc& mutableDesc = const_cast<TextureDesc&>(closeTexture->getDescription());
        mutableDesc.isVirtual = true;
        commandList->close();
        mutableDesc.isVirtual = false;
        EXPECT_TRUE(commandList->commandRecordingFailed());
    }
    heap.collectRetired();
    heap.free(closeHandle);
    heap.collectRetired();
    EXPECT_EQ(closeTexture->getReferenceCount(), 1u);

    TextureHandle submissionTexture = localDevice.createTexture(
        TextureDesc()
            .setWidth(8u)
            .setHeight(8u)
            .setFormat(Format::RGBA8_UNORM)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    ASSERT_TRUE(submissionTexture);
    const GpuDescriptorHandle submissionHandle = heap.allocate(GpuDescriptorClass::SampledImage);
    ASSERT_TRUE(submissionHandle.valid());
    {
        CommandListHandle commandList = localDevice.createCommandList();
        ASSERT_TRUE(commandList);
        commandList->open();
        commandList->setComputeState(ComputeState().setPipeline(pipeline.get()));
        heap.bindCompute(*commandList, *pipeline);
        ASSERT_FALSE(commandList->commandRecordingFailed());
        ASSERT_TRUE(heap.write(
            submissionHandle,
            DescriptorWriteItem::Texture_SRV(0u, submissionTexture.get())
        ));
        commandList->close();
        ASSERT_FALSE(commandList->commandRecordingFailed());

        TextureDesc& mutableDesc = const_cast<TextureDesc&>(submissionTexture->getDescription());
        mutableDesc.isVirtual = true;
        CommandList* commandLists[] = { commandList.get() };
        const QueueSubmissionToken token = localDevice.executeCommandLists(
            commandLists,
            LengthOf(commandLists),
            CommandQueue::Graphics,
            QueueSubmissionDesc{}
        );
        mutableDesc.isVirtual = false;
        EXPECT_FALSE(token.valid());
    }
    heap.collectRetired();
    heap.free(submissionHandle);
    heap.collectRetired();
    EXPECT_EQ(submissionTexture->getReferenceCount(), 1u);
    ExpectLivenessHeapStatisticsEqual(baseline, heap.lifecycleStatistics());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(DescriptorHeapBindLivenessTest, PostBindWritesRejectResourcesOutsideEveryActiveExactQueue){
    auto& localDevice = device();
    auto& heap = localDevice.getDescriptorHeap();
    const BindingLayoutHandle layouts[] = { heap.getResourceLayout(), heap.getSamplerLayout() };
    ComputePipelineHandle pipeline = CreateLivenessComputePipeline(localDevice, arena(), layouts, LengthOf(layouts));
    ASSERT_TRUE(pipeline);
    const GpuDescriptorHeapLifecycleStatistics baseline = heap.lifecycleStatistics();

    TextureHandle texture = localDevice.createTexture(
        TextureDesc()
            .setWidth(8u)
            .setHeight(8u)
            .setFormat(Format::RGBA8_UNORM)
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::AsyncComputeAndTransfer)
    );
    BufferHandle buffer = localDevice.createBuffer(
        BufferDesc()
            .setByteSize(4096u)
            .setStructStride(16u)
            .setCanHaveUAVs(true)
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::AsyncComputeAndTransfer)
    );
    ASSERT_TRUE(texture);
    ASSERT_TRUE(buffer);
    const ResourceQueueAdmissionSnapshot textureAdmission = texture->getQueueAdmissionSnapshot();
    const ResourceQueueAdmissionSnapshot bufferAdmission = buffer->getQueueAdmissionSnapshot();
    ASSERT_TRUE(textureAdmission.valid());
    ASSERT_TRUE(bufferAdmission.valid());
    if(!textureAdmission.usesConcurrentSharing || !bufferAdmission.usesConcurrentSharing)
        GTEST_SKIP() << "Post-bind exact queue: Compute and Transfer do not resolve to distinct families.";

    const GpuPhysicalQueueId graphicsQueue = localDevice.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    const GpuPhysicalQueueInfo* const graphicsQueueInfo = localDevice.getPhysicalQueueInfo(graphicsQueue);
    ASSERT_NE(graphicsQueueInfo, nullptr);
    ASSERT_FALSE(ResourceQueueAdmissionAdmitsQueue(textureAdmission, *graphicsQueueInfo));
    ASSERT_FALSE(ResourceQueueAdmissionAdmitsQueue(bufferAdmission, *graphicsQueueInfo));

    const GpuDescriptorHandle textureHandle = heap.allocate(GpuDescriptorClass::SampledImage);
    const GpuDescriptorHandle bufferHandle = heap.allocate(GpuDescriptorClass::StorageBuffer);
    ASSERT_TRUE(textureHandle.valid());
    ASSERT_TRUE(bufferHandle.valid());
    {
        CommandListHandle commandList = localDevice.createCommandList();
        ASSERT_TRUE(commandList);
        commandList->open();
        commandList->setComputeState(ComputeState().setPipeline(pipeline.get()));
        heap.bindCompute(*commandList, *pipeline);
        ASSERT_FALSE(commandList->commandRecordingFailed());
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
        EXPECT_DEATH_IF_SUPPORTED({
            EXPECT_FALSE(heap.write(textureHandle, DescriptorWriteItem::Texture_SRV(0u, texture.get())));
        }, "");
#else
        EXPECT_FALSE(heap.write(textureHandle, DescriptorWriteItem::Texture_SRV(0u, texture.get())));
#endif
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
        EXPECT_DEATH_IF_SUPPORTED({
            EXPECT_FALSE(heap.write(bufferHandle, DescriptorWriteItem::StructuredBuffer_UAV(0u, buffer.get())));
        }, "");
#else
        EXPECT_FALSE(heap.write(bufferHandle, DescriptorWriteItem::StructuredBuffer_UAV(0u, buffer.get())));
#endif
        EXPECT_FALSE(commandList->commandRecordingFailed());
        commandList->close();
        EXPECT_FALSE(commandList->commandRecordingFailed());

        CommandList* const commandLists[] = { commandList.get() };
        const QueueSubmissionToken token = localDevice.executeCommandLists(
            commandLists,
            LengthOf(commandLists),
            graphicsQueue,
            QueueSubmissionDesc{}
        );
        ASSERT_TRUE(token.valid());
        ASSERT_TRUE(localDevice.waitForIdle());
    }
    heap.collectRetired();
    heap.free(textureHandle);
    heap.free(bufferHandle);
    heap.collectRetired();
    EXPECT_EQ(texture->getReferenceCount(), 1u);
    EXPECT_EQ(buffer->getReferenceCount(), 1u);
    ExpectLivenessHeapStatisticsEqual(baseline, heap.lifecycleStatistics());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(DescriptorHeapBindLivenessTest, TlasLivenessAndStaleHandleAreValidatedAtBind){
    auto& localDevice = device();
    auto& heap = localDevice.getDescriptorHeap();
    if(!localDevice.queryFeatureSupport(Feature::RayTracingAccelStruct) || !heap.hasAccelStructLayout())
        GTEST_SKIP() << "Descriptor-heap TLAS bind liveness requires acceleration-structure support.";

    const BindingLayoutHandle layouts[] = {
        heap.getResourceLayout(),
        heap.getSamplerLayout(),
        heap.getAccelStructLayout(),
    };
    ComputePipelineHandle pipeline = CreateLivenessComputePipeline(localDevice, arena(), layouts, LengthOf(layouts));
    ASSERT_TRUE(pipeline);
    const BindingLayoutHandle persistentLayouts[] = { heap.getResourceLayout(), heap.getSamplerLayout() };
    ComputePipelineHandle persistentPipeline = CreateLivenessComputePipeline(
        localDevice,
        arena(),
        persistentLayouts,
        LengthOf(persistentLayouts)
    );
    ASSERT_TRUE(persistentPipeline);
    const GpuDescriptorHeapLifecycleStatistics baseline = heap.lifecycleStatistics();

    RayTracingAccelStructDesc tlasDesc(arena());
    tlasDesc.setTopLevelMaxInstances(1u);
    RayTracingAccelStructHandle tlas = localDevice.createAccelStruct(tlasDesc);
    ASSERT_TRUE(tlas);
    ASSERT_NE(tlas->getBackingBuffer(), nullptr);
    const GpuDescriptorHandle tlasHandle = heap.allocate(GpuDescriptorClass::AccelStruct);
    ASSERT_TRUE(tlasHandle.valid());
    ASSERT_TRUE(heap.write(tlasHandle, DescriptorWriteItem::RayTracingAccelStruct(0u, tlas.get())));
    const GpuDescriptorHeapLifecycleStatistics populated = heap.lifecycleStatistics();

    {
        CommandListHandle commandList = localDevice.createCommandList();
        ASSERT_TRUE(commandList);
        commandList->open();
        commandList->setComputeState(ComputeState().setPipeline(pipeline.get()));
        heap.bindCompute(*commandList, *pipeline);
        EXPECT_TRUE(commandList->commandRecordingFailed());
        commandList->close();
    }
    ExpectLivenessHeapStatisticsEqual(populated, heap.lifecycleStatistics());

    {
        CommandListHandle commandList = localDevice.createCommandList();
        ASSERT_TRUE(commandList);
        commandList->open();
        commandList->setComputeState(ComputeState().setPipeline(persistentPipeline.get()));
        heap.bindCompute(*commandList, *persistentPipeline, tlasHandle);
        EXPECT_TRUE(commandList->commandRecordingFailed());
        commandList->close();
    }
    ExpectLivenessHeapStatisticsEqual(populated, heap.lifecycleStatistics());

    {
        CommandListHandle commandList = localDevice.createCommandList();
        ASSERT_TRUE(commandList);
        commandList->open();
        commandList->setComputeState(ComputeState().setPipeline(pipeline.get()));
        BufferDesc& backingDesc = const_cast<BufferDesc&>(tlas->getBackingBuffer()->getDescription());
        backingDesc.isVirtual = true;
        heap.bindCompute(*commandList, *pipeline, tlasHandle);
        backingDesc.isVirtual = false;
        EXPECT_TRUE(commandList->commandRecordingFailed());
        commandList->close();
    }
    ExpectLivenessHeapStatisticsEqual(populated, heap.lifecycleStatistics());

    {
        CommandListHandle commandList = localDevice.createCommandList();
        ASSERT_TRUE(commandList);
        commandList->open();
        commandList->setComputeState(ComputeState().setPipeline(pipeline.get()));
        heap.bindCompute(*commandList, *pipeline, tlasHandle);
        ASSERT_FALSE(commandList->commandRecordingFailed());
        commandList->close();
        CommandList* commandLists[] = { commandList.get() };
        const QueueSubmissionToken token = localDevice.executeCommandLists(
            commandLists,
            LengthOf(commandLists),
            CommandQueue::Graphics,
            QueueSubmissionDesc{}
        );
        ASSERT_TRUE(token.valid());
        ASSERT_TRUE(localDevice.waitForIdle());
        heap.collectRetired();
    }

    heap.free(tlasHandle);
    heap.collectRetired();
    const GpuDescriptorHeapLifecycleStatistics afterFree = heap.lifecycleStatistics();
    {
        CommandListHandle commandList = localDevice.createCommandList();
        ASSERT_TRUE(commandList);
        commandList->open();
        commandList->setComputeState(ComputeState().setPipeline(pipeline.get()));
        heap.bindCompute(*commandList, *pipeline, tlasHandle);
        EXPECT_TRUE(commandList->commandRecordingFailed());
        commandList->close();
    }
    ExpectLivenessHeapStatisticsEqual(afterFree, heap.lifecycleStatistics());

    RayTracingAccelStructHandle freshTlas = localDevice.createAccelStruct(tlasDesc);
    ASSERT_TRUE(freshTlas);
    const GpuDescriptorHandle freshHandle = heap.allocate(GpuDescriptorClass::AccelStruct);
    ASSERT_TRUE(freshHandle.valid());
    ASSERT_TRUE(heap.write(freshHandle, DescriptorWriteItem::RayTracingAccelStruct(0u, freshTlas.get())));
    CommandListHandle commandList = localDevice.createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    commandList->setComputeState(ComputeState().setPipeline(pipeline.get()));
    heap.bindCompute(*commandList, *pipeline, freshHandle);
    ASSERT_FALSE(commandList->commandRecordingFailed());
    commandList->close();
    CommandList* commandLists[] = { commandList.get() };
    const QueueSubmissionToken token = localDevice.executeCommandLists(
        commandLists,
        LengthOf(commandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(token.valid());
    ASSERT_TRUE(localDevice.waitForIdle());
    heap.collectRetired();
    heap.free(freshHandle);
    heap.collectRetired();
    ExpectLivenessHeapStatisticsEqual(baseline, heap.lifecycleStatistics());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

