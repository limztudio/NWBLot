// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <global/global.h>
#include <global/unique_ptr.h>
#include <core/common/module.h>
#include <core/graphics/api.h>
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
static constexpr u32 s_DescriptorHeapBindComputeSpirv[] = {
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

static constexpr u32 s_DescriptorHeapBindVertexSpirv[] = {
    0x07230203u, 0x00010600u, 0x000d000bu, 0x00000015u, 0x00000000u, 0x00020011u, 0x00000001u,
    0x0006000bu, 0x00000001u, 0x4c534c47u, 0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu,
    0x00000000u, 0x00000001u, 0x0006000fu, 0x00000000u, 0x00000004u, 0x6e69616du, 0x00000000u,
    0x0000000du, 0x00030047u, 0x0000000bu, 0x00000002u, 0x00050048u, 0x0000000bu, 0x00000000u,
    0x0000000bu, 0x00000000u, 0x00050048u, 0x0000000bu, 0x00000001u, 0x0000000bu, 0x00000001u,
    0x00050048u, 0x0000000bu, 0x00000002u, 0x0000000bu, 0x00000003u, 0x00050048u, 0x0000000bu,
    0x00000003u, 0x0000000bu, 0x00000004u, 0x00020013u, 0x00000002u, 0x00030021u, 0x00000003u,
    0x00000002u, 0x00030016u, 0x00000006u, 0x00000020u, 0x00040017u, 0x00000007u, 0x00000006u,
    0x00000004u, 0x00040015u, 0x00000008u, 0x00000020u, 0x00000000u, 0x0004002bu, 0x00000008u,
    0x00000009u, 0x00000001u, 0x0004001cu, 0x0000000au, 0x00000006u, 0x00000009u, 0x0006001eu,
    0x0000000bu, 0x00000007u, 0x00000006u, 0x0000000au, 0x0000000au, 0x00040020u, 0x0000000cu,
    0x00000003u, 0x0000000bu, 0x0004003bu, 0x0000000cu, 0x0000000du, 0x00000003u, 0x00040015u,
    0x0000000eu, 0x00000020u, 0x00000001u, 0x0004002bu, 0x0000000eu, 0x0000000fu, 0x00000000u,
    0x0004002bu, 0x00000006u, 0x00000010u, 0x00000000u, 0x0004002bu, 0x00000006u, 0x00000011u,
    0x3f800000u, 0x0007002cu, 0x00000007u, 0x00000012u, 0x00000010u, 0x00000010u, 0x00000010u,
    0x00000011u, 0x00040020u, 0x00000013u, 0x00000003u, 0x00000007u, 0x00050036u, 0x00000002u,
    0x00000004u, 0x00000000u, 0x00000003u, 0x000200f8u, 0x00000005u, 0x00050041u, 0x00000013u,
    0x00000014u, 0x0000000du, 0x0000000fu, 0x0003003eu, 0x00000014u, 0x00000012u, 0x000100fdu,
    0x00010038u,
};

static constexpr u32 s_DescriptorHeapBindFragmentSpirv[] = {
    0x07230203u, 0x00010600u, 0x000d000bu, 0x0000000cu, 0x00000000u, 0x00020011u, 0x00000001u,
    0x0006000bu, 0x00000001u, 0x4c534c47u, 0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu,
    0x00000000u, 0x00000001u, 0x0006000fu, 0x00000004u, 0x00000004u, 0x6e69616du, 0x00000000u,
    0x00000009u, 0x00030010u, 0x00000004u, 0x00000007u, 0x00040047u, 0x00000009u, 0x0000001eu,
    0x00000000u, 0x00020013u, 0x00000002u, 0x00030021u, 0x00000003u, 0x00000002u, 0x00030016u,
    0x00000006u, 0x00000020u, 0x00040017u, 0x00000007u, 0x00000006u, 0x00000004u, 0x00040020u,
    0x00000008u, 0x00000003u, 0x00000007u, 0x0004003bu, 0x00000008u, 0x00000009u, 0x00000003u,
    0x0004002bu, 0x00000006u, 0x0000000au, 0x00000000u, 0x0007002cu, 0x00000007u, 0x0000000bu,
    0x0000000au, 0x0000000au, 0x0000000au, 0x0000000au, 0x00050036u, 0x00000002u, 0x00000004u,
    0x00000000u, 0x00000003u, 0x000200f8u, 0x00000005u, 0x0003003eu, 0x00000009u, 0x0000000bu,
    0x000100fdu, 0x00010038u,
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Operation>
void ExpectBindRejection(Operation&& operation){
    operation();
}

template<typename Operation>
void ExpectSubmissionRejection(Operation&& operation){
    EXPECT_FALSE(operation().valid());
}

template<typename Operation>
void ExpectHeapAllocateRejection(Operation&& operation){
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({ EXPECT_FALSE(operation().valid()); }, "");
#else
    EXPECT_FALSE(operation().valid());
#endif
}

template<typename Operation>
void ExpectHeapWriteRejection(Operation&& operation){
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({ EXPECT_FALSE(operation()); }, "");
#else
    EXPECT_FALSE(operation());
#endif
}

void ExpectHeapStatisticsEqual(
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

[[nodiscard]] GpuDescriptorHeapDesc MakeDefaultHeapDesc(){
    GpuDescriptorHeapDesc desc;
    desc.setBindlessHeapAbi(Impl::AssetsGraphicsBindless::MakeGpuDescriptorHeapAbi());
    return desc;
}

[[nodiscard]] GpuDescriptorHeapDesc MakeSmallHeapDesc(){
    GpuDescriptorHeapDesc desc;
    desc
        .setResourceCapacity(8u)
        .setSamplerCapacity(4u)
        .setBindlessHeapAbi(Impl::AssetsGraphicsBindless::MakeGpuDescriptorHeapAbi())
    ;
    return desc;
}

[[nodiscard]] ComputePipelineHandle CreateComputePipeline(
    GraphicsBackend::Device& device,
    Alloc::GlobalArena& arena,
    const BindingLayoutHandle* const layouts,
    const usize layoutCount
){
    ShaderDesc shaderDesc(arena);
    shaderDesc
        .setShaderType(ShaderType::Compute)
        .setDebugName(Name("tests/descriptor_buffer/heap_bind_ingress_compute"))
    ;
    ShaderHandle shader = device.createShader(
        shaderDesc,
        s_DescriptorHeapBindComputeSpirv,
        sizeof(s_DescriptorHeapBindComputeSpirv)
    );
    if(!shader)
        return nullptr;

    ComputePipelineDesc pipelineDesc;
    pipelineDesc.setComputeShader(shader);
    for(usize layoutIndex = 0u; layoutIndex < layoutCount; ++layoutIndex)
        pipelineDesc.addBindingLayout(layouts[layoutIndex]);
    return device.createComputePipeline(pipelineDesc);
}

class DescriptorHeapBindIngressTest : public ::testing::Test{
protected:
    static void SetUpTestSuite(){
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
        GTEST_FLAG_SET(death_test_style, "threadsafe");
#endif
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
            const bool sawVulkanError = s_logger->sawMessageContaining(
                NWB_TEXT("Vulkan debug: [severity=error")
            );
            EXPECT_FALSE(sawVulkanError)
                << "validation-enabled descriptor-heap bind tests emitted a Vulkan error";
        }
        s_loggerGuard.reset();
        s_logger.reset();
        s_runtimeInitialized = false;
        s_ready = false;
    }

    virtual void SetUp()override{
        if(!s_ready)
            GTEST_SKIP() << "Descriptor-heap bind ingress: no usable descriptor-buffer headless device.";
    }

    virtual void TearDown()override{
        if(!s_ready)
            return;

        auto& localDevice = device();
        auto& heap = localDevice.getDescriptorHeap();
        auto& localManager = localDevice.getDescriptorBufferManager();
        EXPECT_TRUE(localDevice.waitForIdle());
        heap.collectRetired();
        heap.shutdown();
        EXPECT_FALSE(heap.isInitialized());
        if(!localManager.isEnabled())
            EXPECT_TRUE(localManager.initialize());
        EXPECT_TRUE(heap.initialize(MakeDefaultHeapDesc()));
    }

    [[nodiscard]] static GraphicsBackend::Device& device(){ return s_scope->graphics().getDevice(); }
    [[nodiscard]] static Alloc::GlobalArena& arena(){ return s_scope->arena(); }


protected:
    static bool s_runtimeInitialized;
    static bool s_ready;
    static UniquePtr<HeadlessGraphicsScope> s_scope;
    static Optional<CapturingLogger> s_logger;
    static Optional<Common::LoggerRegistrationGuard> s_loggerGuard;
};

bool DescriptorHeapBindIngressTest::s_runtimeInitialized = false;
bool DescriptorHeapBindIngressTest::s_ready = false;
UniquePtr<HeadlessGraphicsScope> DescriptorHeapBindIngressTest::s_scope;
Optional<CapturingLogger> DescriptorHeapBindIngressTest::s_logger;
Optional<Common::LoggerRegistrationGuard> DescriptorHeapBindIngressTest::s_loggerGuard;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(DescriptorHeapBindIngressTest, ImmutablePipelineSnapshotBindsAndPublicShutdownPinsUnsubmittedUse){
    auto& localDevice = device();
    auto& heap = localDevice.getDescriptorHeap();
    const BindingLayoutHandle layouts[] = { heap.getResourceLayout(), heap.getSamplerLayout() };
    ComputePipelineHandle pipeline = CreateComputePipeline(localDevice, arena(), layouts, LengthOf(layouts));
    ASSERT_TRUE(pipeline);

    const_cast<ComputePipelineDesc&>(pipeline->getDescription()).bindingLayouts.clear();
    const GpuDescriptorHeapLifecycleStatistics baseline = heap.lifecycleStatistics();
    ASSERT_EQ(baseline.unsubmittedHeapUseCount, 0u);

    CommandListHandle commandList = localDevice.createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    commandList->setComputeState(ComputeState().setPipeline(pipeline.get()));
    heap.bindCompute(*commandList, *pipeline);
    heap.bindCompute(*commandList, *pipeline);
    ASSERT_FALSE(commandList->commandRecordingFailed());

    const GpuDescriptorHeapLifecycleStatistics recorded = heap.lifecycleStatistics();
    EXPECT_EQ(recorded.unsubmittedHeapUseCount, baseline.unsubmittedHeapUseCount + 1u);
    heap.shutdown();
    ExpectHeapStatisticsEqual(recorded, heap.lifecycleStatistics());

    commandList->open();
    commandList->close();
    heap.collectRetired();
    const GpuDescriptorHeapLifecycleStatistics abandoned = heap.lifecycleStatistics();
    EXPECT_EQ(abandoned.unsubmittedHeapUseCount, 0u);
    EXPECT_EQ(abandoned.abandonedHeapUseCount, 0u);

    heap.shutdown();
    EXPECT_FALSE(heap.isInitialized());
    EXPECT_TRUE(heap.initialize(MakeDefaultHeapDesc()));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(DescriptorHeapBindIngressTest, RejectsCustomHybridAndWrongCurrentPipelineWithoutHeapMutation){
    auto& localDevice = device();
    auto& heap = localDevice.getDescriptorHeap();
    GraphicsBackend::GpuDescriptorHeap customHeap(localDevice);
    ASSERT_TRUE(customHeap.initialize(MakeSmallHeapDesc()));

    const BindingLayoutHandle globalLayouts[] = { heap.getResourceLayout(), heap.getSamplerLayout() };
    const BindingLayoutHandle customLayouts[] = {
        customHeap.getResourceLayout(),
        customHeap.getSamplerLayout(),
    };
    const BindingLayoutHandle hybridLayouts[] = { heap.getResourceLayout(), customHeap.getSamplerLayout() };
    ComputePipelineHandle globalPipeline = CreateComputePipeline(
        localDevice,
        arena(),
        globalLayouts,
        LengthOf(globalLayouts)
    );
    ComputePipelineHandle secondGlobalPipeline = CreateComputePipeline(
        localDevice,
        arena(),
        globalLayouts,
        LengthOf(globalLayouts)
    );
    ComputePipelineHandle customPipeline = CreateComputePipeline(
        localDevice,
        arena(),
        customLayouts,
        LengthOf(customLayouts)
    );
    ComputePipelineHandle hybridPipeline = CreateComputePipeline(
        localDevice,
        arena(),
        hybridLayouts,
        LengthOf(hybridLayouts)
    );
    ASSERT_TRUE(globalPipeline);
    ASSERT_TRUE(secondGlobalPipeline);
    ASSERT_TRUE(customPipeline);
    ASSERT_TRUE(hybridPipeline);

    const GpuDescriptorHeapLifecycleStatistics globalBaseline = heap.lifecycleStatistics();
    const GpuDescriptorHeapLifecycleStatistics customBaseline = customHeap.lifecycleStatistics();
    {
        CommandListHandle commandList = localDevice.createCommandList();
        ASSERT_TRUE(commandList);
        commandList->open();
        commandList->setComputeState(ComputeState().setPipeline(customPipeline.get()));
        ExpectBindRejection([&](){ customHeap.bindCompute(*commandList, *customPipeline); });
        EXPECT_TRUE(commandList->commandRecordingFailed());
        commandList->close();
    }
    ExpectHeapStatisticsEqual(globalBaseline, heap.lifecycleStatistics());
    ExpectHeapStatisticsEqual(customBaseline, customHeap.lifecycleStatistics());

    {
        CommandListHandle commandList = localDevice.createCommandList();
        ASSERT_TRUE(commandList);
        commandList->open();
        commandList->setComputeState(ComputeState().setPipeline(hybridPipeline.get()));
        ExpectBindRejection([&](){ heap.bindCompute(*commandList, *hybridPipeline); });
        EXPECT_TRUE(commandList->commandRecordingFailed());
        commandList->close();
    }
    ExpectHeapStatisticsEqual(globalBaseline, heap.lifecycleStatistics());

    {
        CommandListHandle commandList = localDevice.createCommandList();
        ASSERT_TRUE(commandList);
        commandList->open();
        commandList->setComputeState(ComputeState().setPipeline(globalPipeline.get()));
        ExpectBindRejection([&](){ heap.bindCompute(*commandList, *secondGlobalPipeline); });
        EXPECT_TRUE(commandList->commandRecordingFailed());
        commandList->close();
    }
    ExpectHeapStatisticsEqual(globalBaseline, heap.lifecycleStatistics());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(DescriptorHeapBindIngressTest, ManagerRolloverRejectsStaleHeapAndRecordingThenRecovers){
    auto& localDevice = device();
    auto& manager = localDevice.getDescriptorBufferManager();
    auto& heap = localDevice.getDescriptorHeap();
    const BindingLayoutHandle layouts[] = { heap.getResourceLayout(), heap.getSamplerLayout() };
    ComputePipelineHandle pipeline = CreateComputePipeline(localDevice, arena(), layouts, LengthOf(layouts));
    ASSERT_TRUE(pipeline);

    BufferHandle buffer = localDevice.createBuffer(
        BufferDesc()
            .setByteSize(4096u)
            .setStructStride(16u)
            .setCanHaveUAVs(true)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    ASSERT_TRUE(buffer);
    const GpuDescriptorHandle handle = heap.allocate(GpuDescriptorClass::StorageBuffer);
    ASSERT_TRUE(handle.valid());
    const DescriptorWriteItem writeItem = DescriptorWriteItem::StructuredBuffer_UAV(0u, buffer.get());
    ASSERT_TRUE(heap.write(handle, writeItem));
    ASSERT_EQ(buffer->getReferenceCount(), 2u);

    CommandListHandle commandList = localDevice.createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    commandList->setComputeState(ComputeState().setPipeline(pipeline.get()));
    heap.bindCompute(*commandList, *pipeline);
    ASSERT_FALSE(commandList->commandRecordingFailed());
    const GpuDescriptorHeapLifecycleStatistics beforeRollover = heap.lifecycleStatistics();

    manager.shutdown();
    EXPECT_FALSE(manager.isEnabled());
    if(!manager.initialize()){
        ADD_FAILURE() << "descriptor-buffer manager did not recover after deliberate rollover";
        return;
    }

    ExpectHeapAllocateRejection([&](){ return heap.allocate(GpuDescriptorClass::StorageBuffer); });
    ExpectHeapWriteRejection([&](){ return heap.write(handle, writeItem); });
    EXPECT_EQ(buffer->getReferenceCount(), 2u);
    ExpectHeapStatisticsEqual(beforeRollover, heap.lifecycleStatistics());

    commandList->close();
    CommandList* commandLists[] = { commandList.get() };
    ExpectSubmissionRejection([&](){
        return localDevice.executeCommandLists(
            commandLists,
            LengthOf(commandLists),
            CommandQueue::Graphics,
            QueueSubmissionDesc{}
        );
    });

    if(!heap.initialize(MakeDefaultHeapDesc())){
        ADD_FAILURE() << "global descriptor heap did not recover after stable manager rollover";
        return;
    }
    EXPECT_EQ(buffer->getReferenceCount(), 1u);
    const GpuDescriptorHeapLifecycleStatistics recovered = heap.lifecycleStatistics();
    EXPECT_EQ(recovered.resourceLiveSlotCount, 0u);
    EXPECT_EQ(recovered.acceptedHeapUseCount, 0u);
    EXPECT_EQ(recovered.unsubmittedHeapUseCount, 0u);
    EXPECT_EQ(recovered.abandonedHeapUseCount, 0u);

    commandList->open();
    commandList->close();

    const BindingLayoutHandle recoveredLayouts[] = { heap.getResourceLayout(), heap.getSamplerLayout() };
    ComputePipelineHandle recoveredPipeline = CreateComputePipeline(
        localDevice,
        arena(),
        recoveredLayouts,
        LengthOf(recoveredLayouts)
    );
    ASSERT_TRUE(recoveredPipeline);
    CommandListHandle recoveredCommandList = localDevice.createCommandList();
    ASSERT_TRUE(recoveredCommandList);
    recoveredCommandList->open();
    recoveredCommandList->setComputeState(ComputeState().setPipeline(recoveredPipeline.get()));
    heap.bindCompute(*recoveredCommandList, *recoveredPipeline);
    recoveredCommandList->close();
    CommandList* recoveredCommandLists[] = { recoveredCommandList.get() };
    const QueueSubmissionToken recoveredToken = localDevice.executeCommandLists(
        recoveredCommandLists,
        LengthOf(recoveredCommandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(recoveredToken.valid());
    ASSERT_TRUE(localDevice.waitForIdle());
    heap.collectRetired();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(DescriptorHeapBindIngressTest, EmptySetOnlyRecordingRejectsManagerRollover){
    auto& localDevice = device();
    auto& manager = localDevice.getDescriptorBufferManager();
    auto& heap = localDevice.getDescriptorHeap();
    ComputePipelineHandle pipeline = CreateComputePipeline(localDevice, arena(), nullptr, 0u);
    ASSERT_TRUE(pipeline);

    CommandListHandle commandList = localDevice.createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    commandList->setComputeState(ComputeState().setPipeline(pipeline.get()));
    ASSERT_FALSE(commandList->commandRecordingFailed());
    commandList->close();

    manager.shutdown();
    if(!manager.initialize()){
        ADD_FAILURE() << "descriptor-buffer manager did not recover after empty-set rollover";
        return;
    }
    CommandList* commandLists[] = { commandList.get() };
    ExpectSubmissionRejection([&](){
        return localDevice.executeCommandLists(
            commandLists,
            LengthOf(commandLists),
            CommandQueue::Graphics,
            QueueSubmissionDesc{}
        );
    });
    commandList->open();
    commandList->close();
    EXPECT_TRUE(heap.initialize(MakeDefaultHeapDesc()));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(DescriptorHeapBindIngressTest, GraphicsBindingRequiresTheMatchingActiveRenderScope){
    auto& localDevice = device();
    auto& heap = localDevice.getDescriptorHeap();
    ShaderDesc vertexShaderDesc(arena());
    vertexShaderDesc
        .setShaderType(ShaderType::Vertex)
        .setDebugName(Name("tests/descriptor_buffer/heap_bind_ingress_vertex"))
    ;
    ShaderHandle vertexShader = localDevice.createShader(
        vertexShaderDesc,
        s_DescriptorHeapBindVertexSpirv,
        sizeof(s_DescriptorHeapBindVertexSpirv)
    );
    ShaderDesc fragmentShaderDesc(arena());
    fragmentShaderDesc
        .setShaderType(ShaderType::Pixel)
        .setDebugName(Name("tests/descriptor_buffer/heap_bind_ingress_fragment"))
    ;
    ShaderHandle fragmentShader = localDevice.createShader(
        fragmentShaderDesc,
        s_DescriptorHeapBindFragmentSpirv,
        sizeof(s_DescriptorHeapBindFragmentSpirv)
    );
    ASSERT_TRUE(vertexShader);
    ASSERT_TRUE(fragmentShader);

    TextureHandle renderTarget = localDevice.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::RGBA8_UNORM)
            .setInRenderTarget(true)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    ASSERT_TRUE(renderTarget);
    const FramebufferDesc framebufferDesc = FramebufferDesc().addColorAttachment(renderTarget.get());
    FramebufferHandle framebuffer = localDevice.createFramebuffer(framebufferDesc);
    ASSERT_TRUE(framebuffer);

    DepthStencilState depthStencilState;
    depthStencilState.disableDepthTest().disableDepthWrite();
    RenderState renderState;
    renderState.setDepthStencilState(depthStencilState);
    GraphicsPipelineDesc pipelineDesc;
    pipelineDesc
        .setVertexShader(vertexShader)
        .setPixelShader(fragmentShader)
        .setRenderState(renderState)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    GraphicsPipelineHandle pipeline = localDevice.createGraphicsPipeline(
        pipelineDesc,
        FramebufferInfo(framebufferDesc)
    );
    ASSERT_TRUE(pipeline);

    CommandListHandle validCommandList = localDevice.createCommandList();
    ASSERT_TRUE(validCommandList);
    validCommandList->open();
    validCommandList->setGraphicsState(
        GraphicsState().setPipeline(pipeline.get()).setFramebuffer(framebuffer.get())
    );
    ASSERT_TRUE(validCommandList->isRenderPassActive());
    heap.bindGraphics(*validCommandList, *pipeline);
    ASSERT_FALSE(validCommandList->commandRecordingFailed());
    validCommandList->close();
    CommandList* validCommandLists[] = { validCommandList.get() };
    const QueueSubmissionToken token = localDevice.executeCommandLists(
        validCommandLists,
        LengthOf(validCommandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(token.valid());
    ASSERT_TRUE(localDevice.waitForIdle());
    heap.collectRetired();
    const GpuDescriptorHeapLifecycleStatistics baseline = heap.lifecycleStatistics();

    CommandListHandle invalidCommandList = localDevice.createCommandList();
    ASSERT_TRUE(invalidCommandList);
    invalidCommandList->open();
    invalidCommandList->setGraphicsState(
        GraphicsState().setPipeline(pipeline.get()).setFramebuffer(framebuffer.get())
    );
    ASSERT_TRUE(invalidCommandList->isRenderPassActive());
    invalidCommandList->endRenderPass();
    ASSERT_FALSE(invalidCommandList->isRenderPassActive());
    ExpectBindRejection([&](){ heap.bindGraphics(*invalidCommandList, *pipeline); });
    EXPECT_TRUE(invalidCommandList->commandRecordingFailed());
    invalidCommandList->close();
    ExpectHeapStatisticsEqual(baseline, heap.lifecycleStatistics());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_FINAL)
TEST_F(DescriptorHeapBindIngressTest, AcceptedInFlightUsePinsPublicShutdown){
    GTEST_SKIP() << "accepted in-flight shutdown gating uses the non-Final native submission semaphore seam";
}
#else
TEST_F(DescriptorHeapBindIngressTest, AcceptedInFlightUsePinsPublicShutdown){
    auto& localDevice = device();
    auto& heap = localDevice.getDescriptorHeap();
    const GpuPhysicalQueueId waitQueueID = localDevice.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    GraphicsBackend::Queue* const waitQueue = localDevice.getQueue(waitQueueID);
    ASSERT_NE(waitQueue, nullptr);
    const BindingLayoutHandle layouts[] = { heap.getResourceLayout(), heap.getSamplerLayout() };
    ComputePipelineHandle pipeline = CreateComputePipeline(localDevice, arena(), layouts, LengthOf(layouts));
    ASSERT_TRUE(pipeline);
    CommandListHandle commandList = localDevice.createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    commandList->setComputeState(ComputeState().setPipeline(pipeline.get()));
    heap.bindCompute(*commandList, *pipeline);
    commandList->close();

    GraphicsBackend::Queue::SubmissionWait blocker;
    if(!localDevice.createSubmissionTimelineForTesting(blocker)){
        ADD_FAILURE() << "could not create the accepted-in-flight test timeline";
        return;
    }

    waitQueue->addWaitSemaphore(blocker.semaphore, blocker.value);
    CommandList* commandLists[] = { commandList.get() };
    const QueueSubmissionToken blockedToken = localDevice.executeCommandLists(
        commandLists,
        LengthOf(commandLists),
        waitQueueID,
        QueueSubmissionDesc{}
    );
    const bool commandAccepted = blockedToken.valid();
    EXPECT_TRUE(commandAccepted);
    GpuDescriptorHeapLifecycleStatistics blockedStatistics;
    if(commandAccepted){
        blockedStatistics = heap.lifecycleStatistics();
        EXPECT_EQ(blockedStatistics.acceptedHeapUseCount, 1u);
        heap.shutdown();
        ExpectHeapStatisticsEqual(blockedStatistics, heap.lifecycleStatistics());
    }

    EXPECT_TRUE(localDevice.signalSubmissionTimelineForTesting(blocker));
    if(!commandAccepted){
        const QueueSubmissionToken drainToken = localDevice.executeCommandLists(
            nullptr,
            0u,
            waitQueueID,
            QueueSubmissionDesc{}
        );
        EXPECT_TRUE(drainToken.valid());
    }
    const bool idle = localDevice.waitForIdle();
    EXPECT_TRUE(idle);
    localDevice.destroySubmissionTimelineForTesting(blocker);
    if(!idle)
        return;

    heap.collectRetired();
    EXPECT_EQ(heap.lifecycleStatistics().acceptedHeapUseCount, 0u);
    heap.shutdown();
    EXPECT_FALSE(heap.isInitialized());
    EXPECT_TRUE(heap.initialize(MakeDefaultHeapDesc()));
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

