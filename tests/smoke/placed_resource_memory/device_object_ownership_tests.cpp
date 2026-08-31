// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Direct Vulkan object-owner preflight and command-state atomicity coverage.


#include <gtest/gtest.h>

#include <global/global.h>
#include <global/unique_ptr.h>
#include <core/graphics/vulkan/backend.h>
#include <tests/common/capturing_logger.h>
#include <tests/common/headless_graphics_scope.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core;


static constexpr u32 s_OwnershipComputeSpirv[] = {
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


static constexpr u32 s_OwnershipVertexSpirv[] = {
    0x07230203u, 0x00010600u, 0x000d000bu, 0x00000015u, 0x00000000u, 0x00020011u,
    0x00000001u, 0x0006000bu, 0x00000001u, 0x4c534c47u, 0x6474732eu, 0x3035342eu,
    0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u, 0x0006000fu, 0x00000000u,
    0x00000004u, 0x6e69616du, 0x00000000u, 0x0000000du, 0x00030047u, 0x0000000bu,
    0x00000002u, 0x00050048u, 0x0000000bu, 0x00000000u, 0x0000000bu, 0x00000000u,
    0x00050048u, 0x0000000bu, 0x00000001u, 0x0000000bu, 0x00000001u, 0x00050048u,
    0x0000000bu, 0x00000002u, 0x0000000bu, 0x00000003u, 0x00050048u, 0x0000000bu,
    0x00000003u, 0x0000000bu, 0x00000004u, 0x00020013u, 0x00000002u, 0x00030021u,
    0x00000003u, 0x00000002u, 0x00030016u, 0x00000006u, 0x00000020u, 0x00040017u,
    0x00000007u, 0x00000006u, 0x00000004u, 0x00040015u, 0x00000008u, 0x00000020u,
    0x00000000u, 0x0004002bu, 0x00000008u, 0x00000009u, 0x00000001u, 0x0004001cu,
    0x0000000au, 0x00000006u, 0x00000009u, 0x0006001eu, 0x0000000bu, 0x00000007u,
    0x00000006u, 0x0000000au, 0x0000000au, 0x00040020u, 0x0000000cu, 0x00000003u,
    0x0000000bu, 0x0004003bu, 0x0000000cu, 0x0000000du, 0x00000003u, 0x00040015u,
    0x0000000eu, 0x00000020u, 0x00000001u, 0x0004002bu, 0x0000000eu, 0x0000000fu,
    0x00000000u, 0x0004002bu, 0x00000006u, 0x00000010u, 0x00000000u, 0x0004002bu,
    0x00000006u, 0x00000011u, 0x3f800000u, 0x0007002cu, 0x00000007u, 0x00000012u,
    0x00000010u, 0x00000010u, 0x00000010u, 0x00000011u, 0x00040020u, 0x00000013u,
    0x00000003u, 0x00000007u, 0x00050036u, 0x00000002u, 0x00000004u, 0x00000000u,
    0x00000003u, 0x000200f8u, 0x00000005u, 0x00050041u, 0x00000013u, 0x00000014u,
    0x0000000du, 0x0000000fu, 0x0003003eu, 0x00000014u, 0x00000012u, 0x000100fdu,
    0x00010038u,
};


static constexpr u32 s_OwnershipFragmentSpirv[] = {
    0x07230203u, 0x00010600u, 0x000d000bu, 0x0000000cu, 0x00000000u, 0x00020011u,
    0x00000001u, 0x0006000bu, 0x00000001u, 0x4c534c47u, 0x6474732eu, 0x3035342eu,
    0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u, 0x0006000fu, 0x00000004u,
    0x00000004u, 0x6e69616du, 0x00000000u, 0x00000009u, 0x00030010u, 0x00000004u,
    0x00000007u, 0x00040047u, 0x00000009u, 0x0000001eu, 0x00000000u, 0x00020013u,
    0x00000002u, 0x00030021u, 0x00000003u, 0x00000002u, 0x00030016u, 0x00000006u,
    0x00000020u, 0x00040017u, 0x00000007u, 0x00000006u, 0x00000004u, 0x00040020u,
    0x00000008u, 0x00000003u, 0x00000007u, 0x0004003bu, 0x00000008u, 0x00000009u,
    0x00000003u, 0x0004002bu, 0x00000006u, 0x0000000au, 0x00000000u, 0x0007002cu,
    0x00000007u, 0x0000000bu, 0x0000000au, 0x0000000au, 0x0000000au, 0x0000000au,
    0x00050036u, 0x00000002u, 0x00000004u, 0x00000000u, 0x00000003u, 0x000200f8u,
    0x00000005u, 0x0003003eu, 0x00000009u, 0x0000000bu, 0x000100fdu, 0x00010038u,
};


static ShaderHandle CreateOwnershipShader(
    GraphicsBackend::Device& device,
    Alloc::GlobalArena& arena,
    const ShaderType::Mask shaderType,
    const u32* const spirv,
    const usize byteSize,
    const Name& debugName
){
    ShaderDesc desc(arena);
    desc
        .setShaderType(shaderType)
        .setDebugName(debugName)
    ;
    return device.createShader(desc, spirv, byteSize);
}


static TextureHandle CreateOwnershipRenderTarget(GraphicsBackend::Device& device, const Name& name){
    return device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::RGBA8_UNORM)
            .setName(name)
            .setInRenderTarget(true)
            .setInitialState(ResourceStates::Common)
    );
}


static BindingLayoutHandle CreateOwnershipBindingLayout(
    GraphicsBackend::Device& device,
    Alloc::GlobalArena& arena
){
    BindingLayoutDesc desc(arena);
    desc
        .setVisibility(ShaderType::All)
        .addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32)))
    ;
    return device.createBindingLayout(desc);
}


static RenderState CreateOwnershipRenderState(){
    DepthStencilState depthStencilState;
    depthStencilState.disableDepthTest().disableDepthWrite();
    RenderState renderState;
    renderState.setDepthStencilState(depthStencilState);
    return renderState;
}


template<typename Operation>
static void ExpectOwnershipDiagnosticRejection(Operation&& operation){
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        EXPECT_FALSE(operation());
    }, "");
#else
    EXPECT_FALSE(operation());
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class DeviceObjectOwnershipTest : public ::testing::Test{
protected:
    static void SetUpTestSuite(){
        s_logger.emplace();
        s_loggerGuard.emplace(*s_logger);

        s_scope = MakeUnique<HeadlessGraphicsScope>();
        if(!s_scope->initialize()){
            GTEST_SKIP() << "Device ownership: no usable validation-enabled headless Vulkan device.";
            return;
        }
        s_foreignScope = MakeUnique<HeadlessGraphicsScope>();
        if(!s_foreignScope->initialize()){
            GTEST_SKIP() << "Device ownership: second validation-enabled headless Vulkan device is unavailable.";
            return;
        }
        s_validationBackedDevicesInitialized = true;
    }

    static void TearDownTestSuite(){
        s_foreignScope.reset();
        s_scope.reset();
        if(s_validationBackedDevicesInitialized && s_logger.has_value()){
            EXPECT_FALSE(s_logger->sawMessageContaining(NWB_TEXT("Vulkan debug: [severity=error")))
                << "validation-enabled direct object-ownership smoke emitted a Vulkan severity=error message";
        }
        s_loggerGuard.reset();
        s_logger.reset();
        s_validationBackedDevicesInitialized = false;
    }

    [[nodiscard]] static GraphicsBackend::Device& device(){
        return s_scope->graphics().getDevice();
    }

    [[nodiscard]] static GraphicsBackend::Device& foreignDevice(){
        return s_foreignScope->graphics().getDevice();
    }

    [[nodiscard]] static Alloc::GlobalArena& arena(){
        return s_scope->arena();
    }

    [[nodiscard]] static Alloc::GlobalArena& foreignArena(){
        return s_foreignScope->arena();
    }

protected:
    static bool s_validationBackedDevicesInitialized;
    static UniquePtr<HeadlessGraphicsScope> s_scope;
    static UniquePtr<HeadlessGraphicsScope> s_foreignScope;
    static Optional<CapturingLogger> s_logger;
    static Optional<Common::LoggerRegistrationGuard> s_loggerGuard;
};


bool DeviceObjectOwnershipTest::s_validationBackedDevicesInitialized = false;
UniquePtr<HeadlessGraphicsScope> DeviceObjectOwnershipTest::s_scope;
UniquePtr<HeadlessGraphicsScope> DeviceObjectOwnershipTest::s_foreignScope;
Optional<CapturingLogger> DeviceObjectOwnershipTest::s_logger;
Optional<Common::LoggerRegistrationGuard> DeviceObjectOwnershipTest::s_loggerGuard;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(DeviceObjectOwnershipTest, FramebufferFactoryPreflightsEveryAttachmentBeforeRetention){
    auto& device = DeviceObjectOwnershipTest::device();
    auto& foreignDevice = DeviceObjectOwnershipTest::foreignDevice();
    const TextureHandle localTexture = CreateOwnershipRenderTarget(
        device,
        Name("tests/device_ownership/local_framebuffer_texture")
    );
    const TextureHandle foreignTexture = CreateOwnershipRenderTarget(
        foreignDevice,
        Name("tests/device_ownership/foreign_framebuffer_texture")
    );
    ASSERT_TRUE(localTexture);
    ASSERT_TRUE(foreignTexture);
    const u32 localReferences = localTexture->getReferenceCount();
    const u32 foreignReferences = foreignTexture->getReferenceCount();

    FramebufferDesc colorDesc;
    colorDesc.addColorAttachment(localTexture.get()).addColorAttachment(foreignTexture.get());
    ExpectOwnershipDiagnosticRejection([&](){ return device.createFramebuffer(colorDesc); });
    EXPECT_EQ(localTexture->getReferenceCount(), localReferences);
    EXPECT_EQ(foreignTexture->getReferenceCount(), foreignReferences);

    FramebufferDesc depthDesc;
    depthDesc.addColorAttachment(localTexture.get()).setDepthAttachment(foreignTexture.get());
    ExpectOwnershipDiagnosticRejection([&](){ return device.createFramebuffer(depthDesc); });
    EXPECT_EQ(localTexture->getReferenceCount(), localReferences);
    EXPECT_EQ(foreignTexture->getReferenceCount(), foreignReferences);

    FramebufferDesc shadingRateDesc;
    shadingRateDesc.addColorAttachment(localTexture.get()).setShadingRateAttachment(foreignTexture.get());
    ExpectOwnershipDiagnosticRejection([&](){ return device.createFramebuffer(shadingRateDesc); });
    EXPECT_EQ(localTexture->getReferenceCount(), localReferences);
    EXPECT_EQ(foreignTexture->getReferenceCount(), foreignReferences);

    FramebufferHandle localFramebuffer = device.createFramebuffer(
        FramebufferDesc().addColorAttachment(localTexture.get())
    );
    ASSERT_TRUE(localFramebuffer);
    EXPECT_EQ(localTexture->getReferenceCount(), localReferences + 1u);
    localFramebuffer.reset();
    EXPECT_EQ(localTexture->getReferenceCount(), localReferences);
}


TEST_F(DeviceObjectOwnershipTest, PipelineFactoriesRejectForeignInputsAndWrongComputeStage){
    auto& device = DeviceObjectOwnershipTest::device();
    auto& foreignDevice = DeviceObjectOwnershipTest::foreignDevice();
    const ShaderHandle localVertex = CreateOwnershipShader(
        device,
        DeviceObjectOwnershipTest::arena(),
        ShaderType::Vertex,
        s_OwnershipVertexSpirv,
        sizeof(s_OwnershipVertexSpirv),
        Name("tests/device_ownership/local_vertex")
    );
    const ShaderHandle foreignVertex = CreateOwnershipShader(
        foreignDevice,
        DeviceObjectOwnershipTest::foreignArena(),
        ShaderType::Vertex,
        s_OwnershipVertexSpirv,
        sizeof(s_OwnershipVertexSpirv),
        Name("tests/device_ownership/foreign_vertex")
    );
    const ShaderHandle localPixel = CreateOwnershipShader(
        device,
        DeviceObjectOwnershipTest::arena(),
        ShaderType::Pixel,
        s_OwnershipFragmentSpirv,
        sizeof(s_OwnershipFragmentSpirv),
        Name("tests/device_ownership/local_pixel")
    );
    const ShaderHandle foreignPixel = CreateOwnershipShader(
        foreignDevice,
        DeviceObjectOwnershipTest::foreignArena(),
        ShaderType::Pixel,
        s_OwnershipFragmentSpirv,
        sizeof(s_OwnershipFragmentSpirv),
        Name("tests/device_ownership/foreign_pixel")
    );
    const ShaderHandle localCompute = CreateOwnershipShader(
        device,
        DeviceObjectOwnershipTest::arena(),
        ShaderType::Compute,
        s_OwnershipComputeSpirv,
        sizeof(s_OwnershipComputeSpirv),
        Name("tests/device_ownership/local_compute")
    );
    const ShaderHandle foreignCompute = CreateOwnershipShader(
        foreignDevice,
        DeviceObjectOwnershipTest::foreignArena(),
        ShaderType::Compute,
        s_OwnershipComputeSpirv,
        sizeof(s_OwnershipComputeSpirv),
        Name("tests/device_ownership/foreign_compute")
    );
    ASSERT_TRUE(localVertex);
    ASSERT_TRUE(foreignVertex);
    ASSERT_TRUE(localPixel);
    ASSERT_TRUE(foreignPixel);
    ASSERT_TRUE(localCompute);
    ASSERT_TRUE(foreignCompute);

    VertexAttributeDesc vertexAttribute;
    vertexAttribute
        .setFormat(Format::R32_FLOAT)
        .setBufferIndex(0u)
        .setElementStride(sizeof(f32))
    ;
    const InputLayoutHandle localInput = device.createInputLayout(&vertexAttribute, 1u, localVertex.get());
    const InputLayoutHandle foreignInput = foreignDevice.createInputLayout(
        &vertexAttribute,
        1u,
        foreignVertex.get()
    );
    const BindingLayoutHandle localLayout = CreateOwnershipBindingLayout(
        device,
        DeviceObjectOwnershipTest::arena()
    );
    const BindingLayoutHandle foreignLayout = CreateOwnershipBindingLayout(
        foreignDevice,
        DeviceObjectOwnershipTest::foreignArena()
    );
    ASSERT_TRUE(localInput);
    ASSERT_TRUE(foreignInput);
    ASSERT_TRUE(localLayout);
    ASSERT_TRUE(foreignLayout);

    const RenderState renderState = CreateOwnershipRenderState();
    const FramebufferInfo framebufferInfo = FramebufferInfo().addColorFormat(Format::RGBA8_UNORM);
    {
        GraphicsPipelineDesc desc;
        desc.setVertexShader(foreignVertex).setPixelShader(localPixel).setRenderState(renderState);
        const u32 references = foreignVertex->getReferenceCount();
        ExpectOwnershipDiagnosticRejection([&](){ return device.createGraphicsPipeline(desc, framebufferInfo); });
        EXPECT_EQ(foreignVertex->getReferenceCount(), references);
    }
    {
        GraphicsPipelineDesc desc;
        desc.setVertexShader(localVertex).setPixelShader(foreignPixel).setRenderState(renderState);
        const u32 references = foreignPixel->getReferenceCount();
        ExpectOwnershipDiagnosticRejection([&](){ return device.createGraphicsPipeline(desc, framebufferInfo); });
        EXPECT_EQ(foreignPixel->getReferenceCount(), references);
    }
    {
        GraphicsPipelineDesc desc;
        desc
            .setVertexShader(localVertex)
            .setPixelShader(localPixel)
            .setInputLayout(foreignInput)
            .setRenderState(renderState)
        ;
        const u32 references = foreignInput->getReferenceCount();
        ExpectOwnershipDiagnosticRejection([&](){ return device.createGraphicsPipeline(desc, framebufferInfo); });
        EXPECT_EQ(foreignInput->getReferenceCount(), references);
    }
    {
        GraphicsPipelineDesc desc;
        desc
            .setVertexShader(localVertex)
            .setPixelShader(localPixel)
            .setRenderState(renderState)
            .addBindingLayout(foreignLayout)
        ;
        const u32 references = foreignLayout->getReferenceCount();
        ExpectOwnershipDiagnosticRejection([&](){ return device.createGraphicsPipeline(desc, framebufferInfo); });
        EXPECT_EQ(foreignLayout->getReferenceCount(), references);
    }
    {
        ComputePipelineDesc desc;
        desc.setComputeShader(foreignCompute);
        const u32 references = foreignCompute->getReferenceCount();
        ExpectOwnershipDiagnosticRejection([&](){ return device.createComputePipeline(desc); });
        EXPECT_EQ(foreignCompute->getReferenceCount(), references);
    }
    {
        ComputePipelineDesc desc;
        desc.setComputeShader(localVertex);
        const u32 references = localVertex->getReferenceCount();
        ExpectOwnershipDiagnosticRejection([&](){ return device.createComputePipeline(desc); });
        EXPECT_EQ(localVertex->getReferenceCount(), references);
    }
    {
        ComputePipelineDesc desc;
        desc.setComputeShader(localCompute).addBindingLayout(foreignLayout);
        const u32 references = foreignLayout->getReferenceCount();
        ExpectOwnershipDiagnosticRejection([&](){ return device.createComputePipeline(desc); });
        EXPECT_EQ(foreignLayout->getReferenceCount(), references);
    }

    GraphicsPipelineDesc localGraphicsDesc;
    localGraphicsDesc
        .setVertexShader(localVertex)
        .setPixelShader(localPixel)
        .setInputLayout(localInput)
        .setRenderState(renderState)
        .addBindingLayout(localLayout)
    ;
    EXPECT_TRUE(device.createGraphicsPipeline(localGraphicsDesc, framebufferInfo));
    ComputePipelineDesc localComputeDesc;
    localComputeDesc.setComputeShader(localCompute).addBindingLayout(localLayout);
    EXPECT_TRUE(device.createComputePipeline(localComputeDesc));
}


TEST_F(DeviceObjectOwnershipTest, CommandStatesRejectForeignObjectsAtomicallyAndRecover){
    auto& device = DeviceObjectOwnershipTest::device();
    auto& foreignDevice = DeviceObjectOwnershipTest::foreignDevice();
    const TextureHandle localTexture = CreateOwnershipRenderTarget(
        device,
        Name("tests/device_ownership/local_command_texture")
    );
    const TextureHandle foreignTexture = CreateOwnershipRenderTarget(
        foreignDevice,
        Name("tests/device_ownership/foreign_command_texture")
    );
    ASSERT_TRUE(localTexture);
    ASSERT_TRUE(foreignTexture);
    const FramebufferHandle localFramebuffer = device.createFramebuffer(
        FramebufferDesc().addColorAttachment(localTexture.get())
    );
    const FramebufferHandle foreignFramebuffer = foreignDevice.createFramebuffer(
        FramebufferDesc().addColorAttachment(foreignTexture.get())
    );
    ASSERT_TRUE(localFramebuffer);
    ASSERT_TRUE(foreignFramebuffer);

    const ShaderHandle localComputeShader = CreateOwnershipShader(
        device,
        DeviceObjectOwnershipTest::arena(),
        ShaderType::Compute,
        s_OwnershipComputeSpirv,
        sizeof(s_OwnershipComputeSpirv),
        Name("tests/device_ownership/local_command_compute")
    );
    const ShaderHandle foreignComputeShader = CreateOwnershipShader(
        foreignDevice,
        DeviceObjectOwnershipTest::foreignArena(),
        ShaderType::Compute,
        s_OwnershipComputeSpirv,
        sizeof(s_OwnershipComputeSpirv),
        Name("tests/device_ownership/foreign_command_compute")
    );
    const ShaderHandle foreignVertex = CreateOwnershipShader(
        foreignDevice,
        DeviceObjectOwnershipTest::foreignArena(),
        ShaderType::Vertex,
        s_OwnershipVertexSpirv,
        sizeof(s_OwnershipVertexSpirv),
        Name("tests/device_ownership/foreign_command_vertex")
    );
    const ShaderHandle foreignPixel = CreateOwnershipShader(
        foreignDevice,
        DeviceObjectOwnershipTest::foreignArena(),
        ShaderType::Pixel,
        s_OwnershipFragmentSpirv,
        sizeof(s_OwnershipFragmentSpirv),
        Name("tests/device_ownership/foreign_command_pixel")
    );
    ASSERT_TRUE(localComputeShader);
    ASSERT_TRUE(foreignComputeShader);
    ASSERT_TRUE(foreignVertex);
    ASSERT_TRUE(foreignPixel);

    ComputePipelineDesc localComputeDesc;
    localComputeDesc.setComputeShader(localComputeShader);
    const ComputePipelineHandle localCompute = device.createComputePipeline(localComputeDesc);
    ComputePipelineDesc foreignComputeDesc;
    foreignComputeDesc.setComputeShader(foreignComputeShader);
    const ComputePipelineHandle foreignCompute = foreignDevice.createComputePipeline(foreignComputeDesc);
    GraphicsPipelineDesc foreignGraphicsDesc;
    foreignGraphicsDesc
        .setVertexShader(foreignVertex)
        .setPixelShader(foreignPixel)
        .setRenderState(CreateOwnershipRenderState())
    ;
    const GraphicsPipelineHandle foreignGraphics = foreignDevice.createGraphicsPipeline(
        foreignGraphicsDesc,
        FramebufferInfo().addColorFormat(Format::RGBA8_UNORM)
    );
    ASSERT_TRUE(localCompute);
    ASSERT_TRUE(foreignCompute);
    ASSERT_TRUE(foreignGraphics);

    CommandListHandle commandList = device.createCommandList();
    ASSERT_TRUE(commandList);
    const u32 foreignFramebufferReferences = foreignFramebuffer->getReferenceCount();
    commandList->open();
    commandList->setGraphicsState(GraphicsState().setFramebuffer(foreignFramebuffer.get()));
    EXPECT_TRUE(commandList->commandRecordingFailed());
    EXPECT_FALSE(commandList->isRenderPassActive());
    EXPECT_EQ(foreignFramebuffer->getReferenceCount(), foreignFramebufferReferences);
    commandList->close();
    EXPECT_FALSE(commandList->hasCommandBuffer());

    commandList->open();
    commandList->setMeshletState(MeshletState().setFramebuffer(foreignFramebuffer.get()));
    EXPECT_TRUE(commandList->commandRecordingFailed());
    EXPECT_FALSE(commandList->isRenderPassActive());
    EXPECT_EQ(foreignFramebuffer->getReferenceCount(), foreignFramebufferReferences);
    commandList->close();
    EXPECT_FALSE(commandList->hasCommandBuffer());

    const u32 localFramebufferReferences = localFramebuffer->getReferenceCount();
    const u32 foreignGraphicsReferences = foreignGraphics->getReferenceCount();
    commandList->open();
    commandList->setGraphicsState(GraphicsState().setFramebuffer(localFramebuffer.get()));
    ASSERT_TRUE(commandList->isRenderPassActive());
    ASSERT_EQ(localFramebuffer->getReferenceCount(), localFramebufferReferences + 1u);
    commandList->setGraphicsState(
        GraphicsState().setPipeline(foreignGraphics.get()).setFramebuffer(localFramebuffer.get())
    );
    EXPECT_TRUE(commandList->commandRecordingFailed());
    EXPECT_TRUE(commandList->isRenderPassActive());
    EXPECT_EQ(localFramebuffer->getReferenceCount(), localFramebufferReferences + 1u);
    EXPECT_EQ(foreignGraphics->getReferenceCount(), foreignGraphicsReferences);
    commandList->close();
    EXPECT_FALSE(commandList->hasCommandBuffer());
    EXPECT_EQ(localFramebuffer->getReferenceCount(), localFramebufferReferences);

    const u32 foreignComputeReferences = foreignCompute->getReferenceCount();
    commandList->open();
    commandList->setGraphicsState(GraphicsState().setFramebuffer(localFramebuffer.get()));
    ASSERT_TRUE(commandList->isRenderPassActive());
    commandList->setComputeState(ComputeState().setPipeline(foreignCompute.get()));
    EXPECT_TRUE(commandList->commandRecordingFailed());
    EXPECT_TRUE(commandList->isRenderPassActive());
    EXPECT_EQ(localFramebuffer->getReferenceCount(), localFramebufferReferences + 1u);
    EXPECT_EQ(foreignCompute->getReferenceCount(), foreignComputeReferences);
    commandList->close();
    EXPECT_FALSE(commandList->hasCommandBuffer());
    EXPECT_EQ(localFramebuffer->getReferenceCount(), localFramebufferReferences);

    commandList->open();
    ASSERT_FALSE(commandList->commandRecordingFailed());
    commandList->setComputeState(ComputeState().setPipeline(localCompute.get()));
    commandList->dispatch(1u);
    commandList->close();
    ASSERT_TRUE(commandList->hasCommandBuffer());
    CommandList* const commandLists[] = { commandList.get() };
    const QueueSubmissionToken token = device.executeCommandLists(
        commandLists,
        LengthOf(commandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(token.valid());
    ASSERT_TRUE(device.waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

