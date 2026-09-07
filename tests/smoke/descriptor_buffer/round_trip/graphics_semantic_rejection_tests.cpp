// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "round_trip_fixture.h"
#include "shaders_test_support.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(DescriptorBufferRoundTripTest, GraphicsSemanticRejectionsDiscardPriorWorkAndReopenInAllBuilds){
    auto& device = DescriptorBufferRoundTripTest::device();

    ShaderDesc vertexShaderDesc(DescriptorBufferRoundTripTest::arena());
    vertexShaderDesc
        .setShaderType(ShaderType::Vertex)
        .setDebugName(Name("tests/descriptor_buffer/semantic_validation_vertex"))
    ;
    ShaderHandle vertexShader = device.createShader(
        vertexShaderDesc,
        s_CommandBufferLifetimeVertexSpirv,
        sizeof(s_CommandBufferLifetimeVertexSpirv)
    );
    ASSERT_TRUE(vertexShader);

    ShaderDesc fragmentShaderDesc(DescriptorBufferRoundTripTest::arena());
    fragmentShaderDesc
        .setShaderType(ShaderType::Pixel)
        .setDebugName(Name("tests/descriptor_buffer/semantic_validation_fragment"))
    ;
    ShaderHandle fragmentShader = device.createShader(
        fragmentShaderDesc,
        s_CommandBufferLifetimeFragmentSpirv,
        sizeof(s_CommandBufferLifetimeFragmentSpirv)
    );
    ASSERT_TRUE(fragmentShader);

    TextureHandle renderTarget = device.createTexture(
        TextureDesc()
            .setWidth(8u)
            .setHeight(8u)
            .setFormat(Format::RGBA8_UNORM)
            .setInRenderTarget(true)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    TextureHandle secondRenderTarget = device.createTexture(
        TextureDesc()
            .setWidth(8u)
            .setHeight(8u)
            .setFormat(Format::RGBA8_UNORM)
            .setInRenderTarget(true)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    TextureHandle volumeRenderTarget = device.createTexture(
        TextureDesc()
            .setWidth(8u)
            .setHeight(8u)
            .setDepth(2u)
            .setFormat(Format::RGBA8_UNORM)
            .setDimension(TextureDimension::Texture3D)
            .setInRenderTarget(true)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    ASSERT_TRUE(renderTarget);
    ASSERT_TRUE(secondRenderTarget);
    ASSERT_TRUE(volumeRenderTarget);

    const FramebufferDesc framebufferDesc = FramebufferDesc().addColorAttachment(renderTarget.get());
    const FramebufferDesc incompatibleFramebufferDesc = FramebufferDesc()
        .addColorAttachment(renderTarget.get())
        .addColorAttachment(secondRenderTarget.get())
    ;
    const FramebufferDesc volumeFramebufferDesc = FramebufferDesc().addColorAttachment(
        volumeRenderTarget.get()
    );
    FramebufferHandle framebuffer = device.createFramebuffer(framebufferDesc);
    FramebufferHandle incompatibleFramebuffer = device.createFramebuffer(incompatibleFramebufferDesc);
    FramebufferHandle emptyFramebuffer = device.createFramebuffer(FramebufferDesc{});
    FramebufferHandle volumeFramebuffer = device.createFramebuffer(volumeFramebufferDesc);
    ASSERT_TRUE(framebuffer);
    ASSERT_TRUE(incompatibleFramebuffer);
    ASSERT_TRUE(emptyFramebuffer);
    ASSERT_TRUE(volumeFramebuffer);

    DepthStencilState depthStencilState;
    depthStencilState.disableDepthTest().disableDepthWrite();
    RenderState renderState;
    renderState.setDepthStencilState(depthStencilState);
    GraphicsPipelineDesc pipelineDesc;
    pipelineDesc
        .setVertexShader(vertexShader)
        .setPixelShader(fragmentShader)
        .setRenderState(renderState)
    ;
    GraphicsPipelineHandle pipeline = device.createGraphicsPipeline(
        pipelineDesc,
        FramebufferInfo(framebufferDesc)
    );
    ASSERT_TRUE(pipeline);

    VertexAttributeDesc vertexAttribute;
    vertexAttribute
        .setFormat(Format::R32_FLOAT)
        .setBufferIndex(3u)
        .setElementStride(sizeof(f32))
    ;
    InputLayoutHandle inputLayout = device.createInputLayout(&vertexAttribute, 1u, vertexShader.get());
    ASSERT_TRUE(inputLayout);
    GraphicsPipelineDesc inputPipelineDesc;
    inputPipelineDesc
        .setVertexShader(vertexShader)
        .setPixelShader(fragmentShader)
        .setInputLayout(inputLayout)
        .setRenderState(renderState)
    ;
    GraphicsPipelineHandle inputPipeline = device.createGraphicsPipeline(
        inputPipelineDesc,
        FramebufferInfo(framebufferDesc)
    );
    ASSERT_TRUE(inputPipeline);

    BufferHandle oracle = device.createBuffer(
        BufferDesc()
            .setByteSize(sizeof(u32) * 4u)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    BufferHandle vertexBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(16u)
            .setIsVertexBuffer(true)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    BufferHandle indexBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(sizeof(u32))
            .setIsIndexBuffer(true)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    BufferHandle shortIndexBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(3u)
            .setIsIndexBuffer(true)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    BufferHandle indirectBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(128u)
            .setIsDrawIndirectArgs(true)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    BufferHandle wrongUsageBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(128u)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    BufferHandle allRolesBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(128u)
            .setIsVertexBuffer(true)
            .setIsIndexBuffer(true)
            .setIsDrawIndirectArgs(true)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    BufferHandle permanentIndirectBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(sizeof(DrawIndirectArguments))
            .setIsDrawIndirectArgs(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(oracle);
    ASSERT_TRUE(vertexBuffer);
    ASSERT_TRUE(indexBuffer);
    ASSERT_TRUE(shortIndexBuffer);
    ASSERT_TRUE(indirectBuffer);
    ASSERT_TRUE(wrongUsageBuffer);
    ASSERT_TRUE(allRolesBuffer);
    ASSERT_TRUE(permanentIndirectBuffer);

    const ViewportState viewportState = ViewportState().addViewport(Viewport(8.0f, 8.0f));
    const ViewportState explicitNegativeViewport = ViewportState()
        .addViewport(Viewport(-0.5f, 7.5f, -0.25f, 7.75f, 0.0f, 1.0f))
        .addScissorRect(Rect(0, 8, 0, 8))
    ;

    auto commandList = device.createCommandList();
    ASSERT_TRUE(commandList);
    const auto submit = [&](){
        CommandList* const commandLists[] = { commandList.get() };
        return device.executeCommandLists(
            commandLists,
            LengthOf(commandLists),
            commandList->getDescription().physicalQueue,
            QueueSubmissionDesc{}
        );
    };

    commandList->open();
    commandList->close();
    QueueSubmissionToken lastAcceptedToken = submit();
    ASSERT_TRUE(lastAcceptedToken.valid());

    enum class Operation : u8{
        PipelineWithoutFramebuffer,
        EmptyFramebuffer,
        ZeroWidthViewport,
        NegativeImplicitScissor,
        DuplicateVertexSlot,
        WrongVertexUsage,
        VertexOffsetOutOfBounds,
        WrongIndexUsage,
        UnknownIndexFormat,
        UnalignedIndexOffset,
        WrongIndirectUsage,
        EnabledShadingRate,
        IncompatibleFramebuffer,
        DrawWithoutPipeline,
        EmptyStateClearsPipeline,
        DrawWithoutViewport,
        MissingRequiredVertexSlot,
        IndexedDrawWithoutIndexBuffer,
        IndexedDrawOutOfBounds,
        IndexedBaseVertexOutOfRange,
        IndirectOffsetUnaligned,
        IndirectRangeOutOfBounds,
        IndexedIndirectWithoutIndexBuffer,
        MeshDispatchWithoutPipeline,
    };
    struct Case{
        Operation operation;
        const char* label;
    };
    const Case cases[] = {
        { Operation::PipelineWithoutFramebuffer, "pipeline without framebuffer" },
        { Operation::EmptyFramebuffer, "empty framebuffer" },
        { Operation::ZeroWidthViewport, "zero-width viewport" },
        { Operation::NegativeImplicitScissor, "negative implicit scissor" },
        { Operation::DuplicateVertexSlot, "duplicate vertex slot" },
        { Operation::WrongVertexUsage, "wrong vertex usage" },
        { Operation::VertexOffsetOutOfBounds, "vertex offset out of bounds" },
        { Operation::WrongIndexUsage, "wrong index usage" },
        { Operation::UnknownIndexFormat, "unknown index format" },
        { Operation::UnalignedIndexOffset, "unaligned index offset" },
        { Operation::WrongIndirectUsage, "wrong indirect usage" },
        { Operation::EnabledShadingRate, "enabled shading rate" },
        { Operation::IncompatibleFramebuffer, "incompatible framebuffer" },
        { Operation::DrawWithoutPipeline, "draw without pipeline" },
        { Operation::EmptyStateClearsPipeline, "empty state clears pipeline" },
        { Operation::DrawWithoutViewport, "draw without viewport" },
        { Operation::MissingRequiredVertexSlot, "missing required vertex slot" },
        { Operation::IndexedDrawWithoutIndexBuffer, "indexed draw without index buffer" },
        { Operation::IndexedDrawOutOfBounds, "indexed draw out of bounds" },
        { Operation::IndexedBaseVertexOutOfRange, "indexed base vertex out of range" },
        { Operation::IndirectOffsetUnaligned, "indirect offset unaligned" },
        { Operation::IndirectRangeOutOfBounds, "indirect range out of bounds" },
        { Operation::IndexedIndirectWithoutIndexBuffer, "indexed indirect without index buffer" },
        { Operation::MeshDispatchWithoutPipeline, "mesh dispatch without pipeline" },
    };

    for(const Case& testCase : cases){
        SCOPED_TRACE(testCase.label);
        commandList->open();
        commandList->clearBufferUInt(oracle.get(), 0x6e57424cu);
        commandList->setGraphicsState(GraphicsState().setFramebuffer(framebuffer.get()));
        ASSERT_FALSE(commandList->commandRecordingFailed());
        ASSERT_TRUE(commandList->isRenderPassActive());

        switch(testCase.operation){
        case Operation::PipelineWithoutFramebuffer:
            commandList->setGraphicsState(
                GraphicsState().setPipeline(pipeline.get()).setViewport(viewportState)
            );
            break;
        case Operation::EmptyFramebuffer:
            commandList->setGraphicsState(GraphicsState().setFramebuffer(emptyFramebuffer.get()));
            break;
        case Operation::ZeroWidthViewport:
            commandList->setGraphicsState(
                GraphicsState()
                    .setFramebuffer(framebuffer.get())
                    .setViewport(ViewportState().addViewport(Viewport(0.0f, 8.0f)))
            );
            break;
        case Operation::NegativeImplicitScissor:
            commandList->setGraphicsState(
                GraphicsState()
                    .setFramebuffer(framebuffer.get())
                    .setViewport(ViewportState().addViewport(Viewport(-1.0f, 7.0f, 0.0f, 8.0f, 0.0f, 1.0f)))
            );
            break;
        case Operation::DuplicateVertexSlot:
            commandList->setGraphicsState(
                GraphicsState()
                    .setFramebuffer(framebuffer.get())
                    .addVertexBuffer(VertexBufferBinding().setBuffer(vertexBuffer.get()).setSlot(1u))
                    .addVertexBuffer(VertexBufferBinding().setBuffer(vertexBuffer.get()).setSlot(1u))
            );
            break;
        case Operation::WrongVertexUsage:
            commandList->setGraphicsState(
                GraphicsState()
                    .setFramebuffer(framebuffer.get())
                    .addVertexBuffer(VertexBufferBinding().setBuffer(wrongUsageBuffer.get()))
            );
            break;
        case Operation::VertexOffsetOutOfBounds:
            commandList->setGraphicsState(
                GraphicsState()
                    .setFramebuffer(framebuffer.get())
                    .addVertexBuffer(
                        VertexBufferBinding().setBuffer(vertexBuffer.get()).setOffset(16u)
                    )
            );
            break;
        case Operation::WrongIndexUsage:
            commandList->setGraphicsState(
                GraphicsState()
                    .setFramebuffer(framebuffer.get())
                    .setIndexBuffer(
                        IndexBufferBinding().setBuffer(wrongUsageBuffer.get()).setFormat(Format::R32_UINT)
                    )
            );
            break;
        case Operation::UnknownIndexFormat:
            commandList->setGraphicsState(
                GraphicsState()
                    .setFramebuffer(framebuffer.get())
                    .setIndexBuffer(IndexBufferBinding().setBuffer(indexBuffer.get()))
            );
            break;
        case Operation::UnalignedIndexOffset:
            commandList->setGraphicsState(
                GraphicsState()
                    .setFramebuffer(framebuffer.get())
                    .setIndexBuffer(
                        IndexBufferBinding()
                            .setBuffer(indexBuffer.get())
                            .setFormat(Format::R32_UINT)
                            .setOffset(2u)
                    )
            );
            break;
        case Operation::WrongIndirectUsage:
            commandList->setGraphicsState(
                GraphicsState().setFramebuffer(framebuffer.get()).setIndirectParams(wrongUsageBuffer.get())
            );
            break;
        case Operation::EnabledShadingRate:
            commandList->setGraphicsState(
                GraphicsState()
                    .setFramebuffer(framebuffer.get())
                    .setShadingRateState(VariableRateShadingState().setEnabled(true))
            );
            break;
        case Operation::IncompatibleFramebuffer:
            commandList->setGraphicsState(
                GraphicsState()
                    .setPipeline(pipeline.get())
                    .setFramebuffer(incompatibleFramebuffer.get())
                    .setViewport(viewportState)
            );
            break;
        case Operation::DrawWithoutPipeline:
            commandList->draw(DrawArguments().setVertexCount(3u));
            break;
        case Operation::EmptyStateClearsPipeline:
            commandList->setGraphicsState(
                GraphicsState()
                    .setPipeline(pipeline.get())
                    .setFramebuffer(framebuffer.get())
                    .setViewport(viewportState)
            );
            ASSERT_FALSE(commandList->commandRecordingFailed());
            commandList->setGraphicsState(GraphicsState{});
            ASSERT_TRUE(commandList->isRenderPassActive());
            commandList->draw(DrawArguments().setVertexCount(3u));
            break;
        case Operation::DrawWithoutViewport:
            commandList->setGraphicsState(
                GraphicsState().setPipeline(pipeline.get()).setFramebuffer(framebuffer.get())
            );
            ASSERT_FALSE(commandList->commandRecordingFailed());
            commandList->draw(DrawArguments().setVertexCount(3u));
            break;
        case Operation::MissingRequiredVertexSlot:
            commandList->setGraphicsState(
                GraphicsState()
                    .setPipeline(inputPipeline.get())
                    .setFramebuffer(framebuffer.get())
                    .setViewport(viewportState)
            );
            ASSERT_FALSE(commandList->commandRecordingFailed());
            commandList->draw(DrawArguments().setVertexCount(1u));
            break;
        case Operation::IndexedDrawWithoutIndexBuffer:
            commandList->setGraphicsState(
                GraphicsState()
                    .setPipeline(pipeline.get())
                    .setFramebuffer(framebuffer.get())
                    .setViewport(viewportState)
            );
            ASSERT_FALSE(commandList->commandRecordingFailed());
            commandList->drawIndexed(DrawArguments().setVertexCount(1u));
            break;
        case Operation::IndexedDrawOutOfBounds:
            commandList->setGraphicsState(
                GraphicsState()
                    .setPipeline(pipeline.get())
                    .setFramebuffer(framebuffer.get())
                    .setViewport(viewportState)
                    .setIndexBuffer(
                        IndexBufferBinding().setBuffer(indexBuffer.get()).setFormat(Format::R32_UINT)
                    )
            );
            ASSERT_FALSE(commandList->commandRecordingFailed());
            commandList->drawIndexed(
                DrawArguments().setVertexCount(1u).setStartIndexLocation(1u)
            );
            break;
        case Operation::IndexedBaseVertexOutOfRange:
            commandList->setGraphicsState(
                GraphicsState()
                    .setPipeline(pipeline.get())
                    .setFramebuffer(framebuffer.get())
                    .setViewport(viewportState)
                    .setIndexBuffer(
                        IndexBufferBinding().setBuffer(indexBuffer.get()).setFormat(Format::R32_UINT)
                    )
            );
            ASSERT_FALSE(commandList->commandRecordingFailed());
            commandList->drawIndexed(
                DrawArguments().setVertexCount(1u).setStartVertexLocation(Limit<u32>::s_Max)
            );
            break;
        case Operation::IndirectOffsetUnaligned:
            commandList->setGraphicsState(
                GraphicsState()
                    .setPipeline(pipeline.get())
                    .setFramebuffer(framebuffer.get())
                    .setViewport(viewportState)
                    .setIndirectParams(indirectBuffer.get())
            );
            ASSERT_FALSE(commandList->commandRecordingFailed());
            commandList->drawIndirect(2u, 1u);
            break;
        case Operation::IndirectRangeOutOfBounds:
            commandList->setGraphicsState(
                GraphicsState()
                    .setPipeline(pipeline.get())
                    .setFramebuffer(framebuffer.get())
                    .setViewport(viewportState)
                    .setIndirectParams(indirectBuffer.get())
            );
            ASSERT_FALSE(commandList->commandRecordingFailed());
            commandList->drawIndirect(128u, 1u);
            break;
        case Operation::IndexedIndirectWithoutIndexBuffer:
            commandList->setGraphicsState(
                GraphicsState()
                    .setPipeline(pipeline.get())
                    .setFramebuffer(framebuffer.get())
                    .setViewport(viewportState)
                    .setIndirectParams(indirectBuffer.get())
            );
            ASSERT_FALSE(commandList->commandRecordingFailed());
            commandList->drawIndexedIndirect(0u, 1u);
            break;
        case Operation::MeshDispatchWithoutPipeline:
            commandList->setMeshletState(
                MeshletState().setFramebuffer(framebuffer.get()).setViewport(viewportState)
            );
            ASSERT_FALSE(commandList->commandRecordingFailed());
            commandList->dispatchMesh(1u, 1u, 1u);
            break;
        }

        EXPECT_TRUE(commandList->commandRecordingFailed());
        EXPECT_TRUE(commandList->isRenderPassActive());
        commandList->endRenderPass();
        EXPECT_TRUE(commandList->isRenderPassActive());
        commandList->close();
        EXPECT_FALSE(commandList->hasCommandBuffer());
        EXPECT_FALSE(submit().valid());

        commandList->open();
        ASSERT_FALSE(commandList->commandRecordingFailed());
        commandList->setGraphicsState(
            GraphicsState()
                .setPipeline(pipeline.get())
                .setFramebuffer(framebuffer.get())
                .setViewport(viewportState)
        );
        commandList->draw(DrawArguments().setVertexCount(3u));
        ASSERT_FALSE(commandList->commandRecordingFailed());
        commandList->endRenderPass();
        commandList->close();
        const QueueSubmissionToken recoveredToken = submit();
        ASSERT_TRUE(recoveredToken.valid());
        EXPECT_EQ(recoveredToken.value, lastAcceptedToken.value + 1u);
        lastAcceptedToken = recoveredToken;
    }

    commandList->open();
    commandList->draw(DrawArguments{});
    commandList->drawIndexed(DrawArguments().setVertexCount(1u).setInstanceCount(0u));
    commandList->drawIndirect(0u, 0u);
    commandList->drawIndexedIndirect(0u, 0u);
    commandList->dispatchMesh(0u, 1u, 1u);
    EXPECT_FALSE(commandList->commandRecordingFailed());
    commandList->close();
    const QueueSubmissionToken zeroWorkToken = submit();
    ASSERT_TRUE(zeroWorkToken.valid());
    EXPECT_EQ(zeroWorkToken.value, lastAcceptedToken.value + 1u);
    lastAcceptedToken = zeroWorkToken;

    constexpr u32 s_Index = 0u;
    DrawIndirectArguments indirectArguments[2u];
    indirectArguments[0u].setVertexCount(1u);
    indirectArguments[1u].setVertexCount(1u);
    DrawIndexedIndirectArguments indexedIndirectArguments;
    indexedIndirectArguments.setIndexCount(1u);

    commandList->open();
    commandList->writeBuffer(allRolesBuffer.get(), &s_Index, sizeof(s_Index), 0u);
    commandList->writeBuffer(
        allRolesBuffer.get(),
        indirectArguments,
        sizeof(indirectArguments),
        16u
    );
    commandList->writeBuffer(
        allRolesBuffer.get(),
        &indexedIndirectArguments,
        sizeof(indexedIndirectArguments),
        64u
    );
    commandList->setGraphicsState(
        GraphicsState()
            .setPipeline(inputPipeline.get())
            .setFramebuffer(framebuffer.get())
            .setViewport(ViewportState().addViewport(Viewport(0.5f, 7.5f, 0.5f, 7.5f, 0.0f, 1.0f)))
            .addVertexBuffer(
                VertexBufferBinding().setBuffer(allRolesBuffer.get()).setSlot(3u)
            )
            .setIndexBuffer(
                IndexBufferBinding().setBuffer(allRolesBuffer.get()).setFormat(Format::R32_UINT)
            )
            .setIndirectParams(allRolesBuffer.get())
    );
    EXPECT_EQ(
        commandList->getBufferState(allRolesBuffer.get()),
        ResourceStates::VertexBuffer | ResourceStates::IndexBuffer | ResourceStates::IndirectArgument
    );
    commandList->draw(DrawArguments().setVertexCount(1u));
    commandList->drawIndexed(DrawArguments().setVertexCount(1u));
    commandList->drawIndirect(16u, 2u);
    commandList->drawIndexedIndirect(64u, 1u);
    commandList->setGraphicsState(
        GraphicsState()
            .setPipeline(inputPipeline.get())
            .setFramebuffer(framebuffer.get())
            .setViewport(explicitNegativeViewport)
            .addVertexBuffer(VertexBufferBinding().setBuffer(vertexBuffer.get()).setSlot(3u))
    );
    commandList->draw(DrawArguments().setVertexCount(1u));
    commandList->endRenderPass();
    commandList->setGraphicsState(GraphicsState().setFramebuffer(volumeFramebuffer.get()));
    ASSERT_TRUE(commandList->isRenderPassActive());
    commandList->endRenderPass();
    ASSERT_FALSE(commandList->commandRecordingFailed());
    commandList->close();
    const QueueSubmissionToken positiveToken = submit();
    ASSERT_TRUE(positiveToken.valid());
    EXPECT_EQ(positiveToken.value, lastAcceptedToken.value + 1u);
    lastAcceptedToken = positiveToken;

    commandList->open();
    commandList->setGraphicsState(
        GraphicsState()
            .setFramebuffer(framebuffer.get())
            .setIndexBuffer(
                IndexBufferBinding()
                    .setBuffer(shortIndexBuffer.get())
                    .setFormat(Format::R16_UINT)
                    .setOffset(2u)
            )
    );
    ASSERT_FALSE(commandList->commandRecordingFailed());
    commandList->endRenderPass();
    commandList->close();
    const QueueSubmissionToken bindOnlyIndexToken = submit();
    ASSERT_TRUE(bindOnlyIndexToken.valid());
    EXPECT_EQ(bindOnlyIndexToken.value, lastAcceptedToken.value + 1u);
    lastAcceptedToken = bindOnlyIndexToken;

    const DrawIndirectArguments retainedIndirectArguments = DrawIndirectArguments().setVertexCount(1u);
    commandList->open();
    commandList->writeBuffer(
        permanentIndirectBuffer.get(),
        &retainedIndirectArguments,
        sizeof(retainedIndirectArguments)
    );
    commandList->setPermanentBufferState(
        permanentIndirectBuffer.get(),
        ResourceStates::IndirectArgument
    );
    commandList->close();
    const QueueSubmissionToken permanentStateToken = submit();
    ASSERT_TRUE(permanentStateToken.valid());
    ASSERT_TRUE(device.waitForIdle());
    lastAcceptedToken = permanentStateToken;

    BufferHandle retainedPermanentIndirectBuffer = permanentIndirectBuffer;
    const usize referencesBeforeRecord = permanentIndirectBuffer->getReferenceCount();
    commandList->open();
    commandList->setGraphicsState(
        GraphicsState()
            .setPipeline(pipeline.get())
            .setFramebuffer(framebuffer.get())
            .setViewport(viewportState)
            .setIndirectParams(permanentIndirectBuffer.get())
    );
    EXPECT_EQ(retainedPermanentIndirectBuffer->getReferenceCount(), referencesBeforeRecord + 1u);
    permanentIndirectBuffer.reset();
    commandList->drawIndirect(0u, 1u);
    commandList->endRenderPass();
    ASSERT_FALSE(commandList->commandRecordingFailed());
    commandList->close();
    const QueueSubmissionToken retainedIndirectToken = submit();
    ASSERT_TRUE(retainedIndirectToken.valid());
    EXPECT_EQ(retainedIndirectToken.value, lastAcceptedToken.value + 1u);
    ASSERT_TRUE(device.waitForIdle());

    for(
        u32 retry = 0u;
        retry < 5000u && retainedPermanentIndirectBuffer->getReferenceCount() != referencesBeforeRecord - 1u;
        ++retry
    ){
        device.runGarbageCollection();
        SleepMS(1u);
    }
    EXPECT_EQ(retainedPermanentIndirectBuffer->getReferenceCount(), referencesBeforeRecord - 1u);
}


TEST_F(DescriptorBufferRoundTripTest, ResolveTextureRejectionsDiscardRenderingAndPositiveResolveClosesItInAllBuilds){
    auto& device = DescriptorBufferRoundTripTest::device();
    constexpr u32 s_Width = 8u;
    constexpr u32 s_Height = 8u;
    constexpr u32 s_SampleCount = 4u;
    constexpr FormatSupport::Mask s_RequiredFormatSupport = FormatSupport::Texture | FormatSupport::RenderTarget;
    if((device.queryFormatSupport(Format::RGBA8_UNORM) & s_RequiredFormatSupport) != s_RequiredFormatSupport)
        GTEST_SKIP() << "Resolve texture smoke: RGBA8_UNORM lacks sampled/color-attachment support.";

    TextureDesc sourceDesc;
    sourceDesc
        .setWidth(s_Width)
        .setHeight(s_Height)
        .setSampleCount(s_SampleCount)
        .setFormat(Format::RGBA8_UNORM)
        .setInRenderTarget(true)
        .setInitialState(ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    TextureDesc destinationDesc;
    destinationDesc
        .setWidth(s_Width)
        .setHeight(s_Height)
        .setFormat(Format::RGBA8_UNORM)
        .setInitialState(ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    TextureDesc seededSourceDesc = sourceDesc;
    seededSourceDesc.setInitialState(ResourceStates::ResolveSource);
    TextureDesc seededDestinationDesc = destinationDesc;
    seededDestinationDesc.setInitialState(ResourceStates::ResolveDest);

    TextureDesc mismatchedExtentDesc = destinationDesc;
    mismatchedExtentDesc.setWidth(s_Width / 2u).setHeight(s_Height / 2u);

    TextureDesc layerCountSourceDesc = sourceDesc;
    layerCountSourceDesc.setArraySize(2u).setDimension(TextureDimension::Texture2DMSArray);

    const TextureHandle source = device.createTexture(sourceDesc);
    const TextureHandle destination = device.createTexture(destinationDesc);
    const TextureHandle singleSampleSource = device.createTexture(destinationDesc);
    const TextureHandle multisampledDestination = device.createTexture(sourceDesc);
    const TextureHandle seededSource = device.createTexture(seededSourceDesc);
    const TextureHandle seededDestination = device.createTexture(seededDestinationDesc);
    const TextureHandle mismatchedExtentDestination = device.createTexture(mismatchedExtentDesc);

    const TextureHandle layerCountSource = device.createTexture(layerCountSourceDesc);

    TextureHandle mismatchedFormatDestination;
    TextureDesc mismatchedFormatDesc = destinationDesc;
    mismatchedFormatDesc.setFormat(Format::BGRA8_UNORM);
    if(
        (device.queryFormatSupport(Format::BGRA8_UNORM) & s_RequiredFormatSupport)
        == s_RequiredFormatSupport
    )
        mismatchedFormatDestination = device.createTexture(mismatchedFormatDesc);

    if(
        !source
        || !destination
        || !singleSampleSource
        || !multisampledDestination
        || !mismatchedExtentDestination
        || !layerCountSource
        || !seededSource
        || !seededDestination
    )
        GTEST_SKIP() << "Resolve texture smoke: the device does not support the required RGBA8 multisample shape.";

    const FramebufferHandle framebuffer = device.createFramebuffer(
        FramebufferDesc().addColorAttachment(source.get())
    );
    const FramebufferHandle seededFramebuffer = device.createFramebuffer(
        FramebufferDesc().addColorAttachment(seededSource.get())
    );
    const StagingTextureHandle readback = device.createStagingTexture(destinationDesc, CpuAccessMode::Read);
    ASSERT_TRUE(framebuffer);
    ASSERT_TRUE(seededFramebuffer);
    ASSERT_TRUE(readback);

    auto commandList = device.createCommandList();
    ASSERT_TRUE(commandList);
    const auto submit = [&](){
        CommandList* const commandLists[] = { commandList.get() };
        return device.executeCommandLists(
            commandLists,
            LengthOf(commandLists),
            commandList->getDescription().physicalQueue,
            QueueSubmissionDesc{}
        );
    };

    commandList->open();
    commandList->close();
    QueueSubmissionToken lastAcceptedToken = submit();
    ASSERT_TRUE(lastAcceptedToken.valid());

    enum class Operation : u8{
        NullDestination,
        NullSource,
        EmptyDestinationRange,
        EmptySourceRange,
        SingleSampleSource,
        MultisampledDestination,
        MismatchedFormat,
        MismatchedExtent,
        MismatchedLayerCount,
        SameNativeImage,
        DestinationPermanentStateConflict,
    };
    struct Case{
        Operation operation;
        const char* label;
    };
    const Case cases[]{
        { Operation::NullDestination, "null destination" },
        { Operation::NullSource, "null source" },
        { Operation::EmptyDestinationRange, "empty destination range" },
        { Operation::EmptySourceRange, "empty source range" },
        { Operation::SingleSampleSource, "single-sample source" },
        { Operation::MultisampledDestination, "multisampled destination" },
        { Operation::MismatchedFormat, "mismatched native format" },
        { Operation::MismatchedExtent, "mismatched extent" },
        { Operation::MismatchedLayerCount, "mismatched layer count" },
        { Operation::SameNativeImage, "same native image" },
        { Operation::DestinationPermanentStateConflict, "destination permanent-state conflict" },
    };

    for(const Case& testCase : cases){
        if(testCase.operation == Operation::MismatchedFormat && !mismatchedFormatDestination)
            continue;
        if(testCase.operation == Operation::MismatchedLayerCount && !layerCountSource)
            continue;

        SCOPED_TRACE(testCase.label);
        Texture* caseDestination = destination.get();
        Texture* caseSource = source.get();
        TextureSubresourceSet destinationSubresources = s_AllSubresources;
        TextureSubresourceSet sourceSubresources = s_AllSubresources;
        bool installDestinationPermanentConflict = false;
        switch(testCase.operation){
        case Operation::NullDestination:
            caseDestination = nullptr;
            break;
        case Operation::NullSource:
            caseSource = nullptr;
            break;
        case Operation::EmptyDestinationRange:
            destinationSubresources.setMipLevels(1u, 1u);
            break;
        case Operation::EmptySourceRange:
            sourceSubresources.setMipLevels(1u, 1u);
            break;
        case Operation::SingleSampleSource:
            caseSource = singleSampleSource.get();
            break;
        case Operation::MultisampledDestination:
            caseDestination = multisampledDestination.get();
            break;
        case Operation::MismatchedFormat:
            caseDestination = mismatchedFormatDestination.get();
            break;
        case Operation::MismatchedExtent:
            caseDestination = mismatchedExtentDestination.get();
            break;
        case Operation::MismatchedLayerCount:
            caseSource = layerCountSource.get();
            break;
        case Operation::SameNativeImage:
            caseDestination = source.get();
            break;
        case Operation::DestinationPermanentStateConflict:
            installDestinationPermanentConflict = true;
            break;
        }

        commandList->open();
        ASSERT_TRUE(commandList->isRecording());
        ASSERT_FALSE(commandList->commandRecordingFailed());
        if(installDestinationPermanentConflict)
            commandList->setPermanentTextureState(destination.get(), ResourceStates::Common);
        ASSERT_FALSE(commandList->commandRecordingFailed());
        commandList->setGraphicsState(GraphicsState().setFramebuffer(framebuffer.get()));
        ASSERT_TRUE(commandList->isRenderPassActive());
        ASSERT_FALSE(commandList->commandRecordingFailed());

        const usize sourceReferencesBeforeResolve = caseSource ? caseSource->getReferenceCount() : 0u;
        const usize destinationReferencesBeforeResolve = caseDestination ? caseDestination->getReferenceCount() : 0u;
        commandList->resolveTexture(
            caseDestination,
            destinationSubresources,
            caseSource,
            sourceSubresources
        );
        EXPECT_TRUE(commandList->commandRecordingFailed());
        EXPECT_TRUE(commandList->isRenderPassActive());
        EXPECT_EQ(
            commandList->getTextureSubresourceState(source.get(), 0u, 0u),
            ResourceStates::RenderTarget
        );
        if(caseSource)
            EXPECT_EQ(caseSource->getReferenceCount(), sourceReferencesBeforeResolve);
        if(caseDestination)
            EXPECT_EQ(caseDestination->getReferenceCount(), destinationReferencesBeforeResolve);

        CommandListResourceStateHandoff invalidHandoff(DescriptorBufferRoundTripTest::arena());
        commandList->close(&invalidHandoff);
        EXPECT_FALSE(invalidHandoff.valid());
        EXPECT_FALSE(commandList->hasCommandBuffer());
        EXPECT_FALSE(submit().valid());

        commandList->open();
        ASSERT_TRUE(commandList->isRecording());
        EXPECT_FALSE(commandList->commandRecordingFailed());
        commandList->close();
        const QueueSubmissionToken recoveredToken = submit();
        ASSERT_TRUE(recoveredToken.valid());
        EXPECT_EQ(recoveredToken.value, lastAcceptedToken.value + 1u);
        lastAcceptedToken = recoveredToken;
    }

    commandList->open();
    commandList->setGraphicsState(GraphicsState().setFramebuffer(framebuffer.get()));
    ASSERT_TRUE(commandList->isRenderPassActive());
    commandList->clearTextureFloat(source.get(), s_AllSubresources, Color(1.f, 0.f, 0.f, 1.f));
    ASSERT_TRUE(commandList->isRenderPassActive());
    ASSERT_FALSE(commandList->commandRecordingFailed());

    commandList->resolveTexture(destination.get(), s_AllSubresources, source.get(), s_AllSubresources);
    EXPECT_FALSE(commandList->commandRecordingFailed());
    EXPECT_FALSE(commandList->isRenderPassActive());
    EXPECT_EQ(
        commandList->getTextureSubresourceState(source.get(), 0u, 0u),
        ResourceStates::ResolveSource
    );
    EXPECT_EQ(
        commandList->getTextureSubresourceState(destination.get(), 0u, 0u),
        ResourceStates::ResolveDest
    );
    commandList->copyTexture(readback.get(), TextureSlice{}, destination.get(), TextureSlice{});
    ASSERT_FALSE(commandList->commandRecordingFailed());
    commandList->close();
    const QueueSubmissionToken resolveToken = submit();
    ASSERT_TRUE(resolveToken.valid());
    EXPECT_EQ(resolveToken.value, lastAcceptedToken.value + 1u);
    ASSERT_TRUE(device.waitForIdle());

    usize rowPitch = 0u;
    const auto* const readbackBytes = static_cast<const u8*>(device.mapStagingTexture(
        readback.get(),
        TextureSlice{},
        CpuAccessMode::Read,
        &rowPitch
    ));
    ASSERT_NE(readbackBytes, nullptr);
    ASSERT_GE(rowPitch, static_cast<usize>(s_Width) * 4u);
    for(u32 row = 0u; row < s_Height; ++row){
        for(u32 column = 0u; column < s_Width; ++column){
            const usize pixelOffset = static_cast<usize>(row) * rowPitch + static_cast<usize>(column) * 4u;
            EXPECT_EQ(readbackBytes[pixelOffset + 0u], 255u);
            EXPECT_EQ(readbackBytes[pixelOffset + 1u], 0u);
            EXPECT_EQ(readbackBytes[pixelOffset + 2u], 0u);
            EXPECT_EQ(readbackBytes[pixelOffset + 3u], 255u);
        }
    }
    device.unmapStagingTexture(readback.get());

    lastAcceptedToken = resolveToken;
    commandList->open();
    commandList->setGraphicsState(GraphicsState().setFramebuffer(seededFramebuffer.get()));
    ASSERT_TRUE(commandList->isRenderPassActive());
    commandList->clearTextureFloat(seededSource.get(), s_AllSubresources, Color(0.f, 1.f, 0.f, 1.f));
    commandList->endRenderPass();
    commandList->setTextureState(seededSource.get(), s_AllSubresources, ResourceStates::ResolveSource);
    commandList->setTextureState(seededDestination.get(), s_AllSubresources, ResourceStates::ResolveDest);
    ASSERT_FALSE(commandList->commandRecordingFailed());
    commandList->close();
    const QueueSubmissionToken primeToken = submit();
    ASSERT_TRUE(primeToken.valid());
    EXPECT_EQ(primeToken.value, lastAcceptedToken.value + 1u);
    ASSERT_TRUE(device.waitForIdle());

    commandList->open();
    ASSERT_EQ(
        commandList->getTextureSubresourceState(seededSource.get(), 0u, 0u),
        ResourceStates::ResolveSource
    );
    ASSERT_EQ(
        commandList->getTextureSubresourceState(seededDestination.get(), 0u, 0u),
        ResourceStates::ResolveDest
    );
    const usize seededSourceReferencesBeforeResolve = seededSource->getReferenceCount();
    const usize seededDestinationReferencesBeforeResolve = seededDestination->getReferenceCount();
    commandList->resolveTexture(
        seededDestination.get(),
        s_AllSubresources,
        seededSource.get(),
        s_AllSubresources
    );
    EXPECT_FALSE(commandList->commandRecordingFailed());
    EXPECT_EQ(seededSource->getReferenceCount(), seededSourceReferencesBeforeResolve + 1u);
    EXPECT_EQ(seededDestination->getReferenceCount(), seededDestinationReferencesBeforeResolve + 1u);
    commandList->close();
    const QueueSubmissionToken seededResolveToken = submit();
    ASSERT_TRUE(seededResolveToken.valid());
    EXPECT_EQ(seededResolveToken.value, primeToken.value + 1u);
    ASSERT_TRUE(device.waitForIdle());

    for(
        u32 retry = 0u;
        retry < 5000u
            && (
                seededSource->getReferenceCount() != seededSourceReferencesBeforeResolve
                || seededDestination->getReferenceCount() != seededDestinationReferencesBeforeResolve
            );
        ++retry
    ){
        device.runGarbageCollection();
        SleepMS(1u);
    }
    EXPECT_EQ(seededSource->getReferenceCount(), seededSourceReferencesBeforeResolve);
    EXPECT_EQ(seededDestination->getReferenceCount(), seededDestinationReferencesBeforeResolve);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

