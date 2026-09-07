// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "round_trip_fixture.h"
#include "shaders_test_support.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(DescriptorBufferRoundTripTest, StickyNativeRecordingFailureCannotSubmitAndResetsOnlyOnOpen){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto commandList = device.createCommandList();
    ASSERT_NE(commandList.get(), nullptr);

    commandList->open();
    ASSERT_TRUE(commandList->isRecording());
    constexpr u32 s_PushConstant = 0x6f4d2b19u;
    commandList->setPushConstants(&s_PushConstant, sizeof(s_PushConstant));
    EXPECT_TRUE(commandList->commandRecordingFailed());
    EXPECT_TRUE(commandList->hasCommandBuffer());

    CommandList* const invalidLists[] = { commandList.get() };
    const QueueSubmissionToken rejectedToken = device.executeCommandLists(
        invalidLists,
        LengthOf(invalidLists),
        commandList->getDescription().physicalQueue,
        QueueSubmissionDesc{}
    );
    EXPECT_FALSE(rejectedToken.valid());
    EXPECT_TRUE(commandList->hasCommandBuffer());

    commandList->close();
    EXPECT_FALSE(commandList->isRecording());
    EXPECT_FALSE(commandList->hasCommandBuffer());
    EXPECT_TRUE(commandList->commandRecordingFailed());

    commandList->open();
    ASSERT_TRUE(commandList->isRecording());
    EXPECT_FALSE(commandList->commandRecordingFailed());
    commandList->close();
    ASSERT_TRUE(commandList->hasCommandBuffer());

    commandList->setComputeState(ComputeState{});
    EXPECT_TRUE(commandList->commandRecordingFailed());
    EXPECT_TRUE(commandList->hasCommandBuffer());
    commandList->close();
    EXPECT_FALSE(commandList->hasCommandBuffer());
    EXPECT_TRUE(commandList->commandRecordingFailed());

    commandList->open();
    ASSERT_TRUE(commandList->isRecording());
    EXPECT_FALSE(commandList->commandRecordingFailed());
    commandList->close();
    ASSERT_TRUE(commandList->hasCommandBuffer());

    CommandList* const validLists[] = { commandList.get() };
    const QueueSubmissionToken acceptedToken = device.executeCommandLists(
        validLists,
        LengthOf(validLists),
        commandList->getDescription().physicalQueue,
        QueueSubmissionDesc{}
    );
    EXPECT_TRUE(acceptedToken.valid());
    EXPECT_TRUE(device.waitForIdle());
}


TEST_F(DescriptorBufferRoundTripTest, BufferSemanticRejectionsDiscardPriorWorkAndReopenInAllBuilds){
    auto& device = DescriptorBufferRoundTripTest::device();
    static constexpr u32 s_InitialWords[] = { 1u, 2u, 3u, 4u };
    constexpr u32 s_DiscardedClearValue = 0xa5a55a5au;

    const BufferHandle buffer = device.createBuffer(
        BufferDesc()
            .setByteSize(sizeof(s_InitialWords))
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
            .setCpuAccess(CpuAccessMode::Read)
    );
    const BufferHandle otherBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(sizeof(s_InitialWords))
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    const BufferHandle unalignedSizeBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(6u)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(buffer);
    ASSERT_TRUE(otherBuffer);
    ASSERT_TRUE(unalignedSizeBuffer);

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
    commandList->writeBuffer(buffer.get(), s_InitialWords, sizeof(s_InitialWords));
    commandList->close();
    QueueSubmissionToken lastAcceptedToken = submit();
    ASSERT_TRUE(lastAcceptedToken.valid());

    enum class Operation : u8{
        NullWriteBuffer,
        NullWriteData,
        WriteDestinationOutOfBounds,
        WriteOffsetUnaligned,
        WriteSizeUnaligned,
        NullClearBuffer,
        ClearSizeUnaligned,
        NullCopyDestination,
        NullCopySource,
        CopyDestinationOutOfBounds,
        CopySourceOutOfBounds,
        CopyOffsetOverflow,
        OverlappingSelfCopy,
    };
    struct Case{
        Operation operation;
        const char* label;
    };
    const Case cases[] = {
        { Operation::NullWriteBuffer, "null write buffer" },
        { Operation::NullWriteData, "null write data" },
        { Operation::WriteDestinationOutOfBounds, "write destination out of bounds" },
        { Operation::WriteOffsetUnaligned, "write offset unaligned" },
        { Operation::WriteSizeUnaligned, "write size unaligned" },
        { Operation::NullClearBuffer, "null clear buffer" },
        { Operation::ClearSizeUnaligned, "clear size unaligned" },
        { Operation::NullCopyDestination, "null copy destination" },
        { Operation::NullCopySource, "null copy source" },
        { Operation::CopyDestinationOutOfBounds, "copy destination out of bounds" },
        { Operation::CopySourceOutOfBounds, "copy source out of bounds" },
        { Operation::CopyOffsetOverflow, "copy offset overflow" },
        { Operation::OverlappingSelfCopy, "overlapping self copy" },
    };

    for(const Case& testCase : cases){
        SCOPED_TRACE(testCase.label);
        commandList->open();
        ASSERT_TRUE(commandList->isRecording());
        ASSERT_FALSE(commandList->commandRecordingFailed());
        commandList->clearBufferUInt(buffer.get(), s_DiscardedClearValue);
        ASSERT_FALSE(commandList->commandRecordingFailed());

        switch(testCase.operation){
        case Operation::NullWriteBuffer:
            commandList->writeBuffer(nullptr, s_InitialWords, sizeof(s_InitialWords));
            break;
        case Operation::NullWriteData:
            commandList->writeBuffer(buffer.get(), nullptr, sizeof(u32));
            break;
        case Operation::WriteDestinationOutOfBounds:
            commandList->writeBuffer(buffer.get(), s_InitialWords, sizeof(s_InitialWords), sizeof(u32));
            break;
        case Operation::WriteOffsetUnaligned:
            commandList->writeBuffer(buffer.get(), s_InitialWords, sizeof(u32), 2u);
            break;
        case Operation::WriteSizeUnaligned:
            commandList->writeBuffer(buffer.get(), s_InitialWords, 2u);
            break;
        case Operation::NullClearBuffer:
            commandList->clearBufferUInt(nullptr, s_DiscardedClearValue);
            break;
        case Operation::ClearSizeUnaligned:
            commandList->clearBufferUInt(unalignedSizeBuffer.get(), s_DiscardedClearValue);
            break;
        case Operation::NullCopyDestination:
            commandList->copyBuffer(nullptr, 0u, buffer.get(), 0u, sizeof(u32));
            break;
        case Operation::NullCopySource:
            commandList->copyBuffer(buffer.get(), 0u, nullptr, 0u, sizeof(u32));
            break;
        case Operation::CopyDestinationOutOfBounds:
            commandList->copyBuffer(buffer.get(), sizeof(s_InitialWords), otherBuffer.get(), 0u, sizeof(u32));
            break;
        case Operation::CopySourceOutOfBounds:
            commandList->copyBuffer(buffer.get(), 0u, otherBuffer.get(), sizeof(s_InitialWords), sizeof(u32));
            break;
        case Operation::CopyOffsetOverflow:
            commandList->copyBuffer(buffer.get(), Limit<u64>::s_Max, otherBuffer.get(), 0u, sizeof(u32));
            break;
        case Operation::OverlappingSelfCopy:
            commandList->copyBuffer(buffer.get(), sizeof(u32), buffer.get(), 0u, sizeof(u32) * 2u);
            break;
        }

        EXPECT_TRUE(commandList->commandRecordingFailed());
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
    commandList->writeBuffer(nullptr, nullptr, 0u);
    commandList->copyBuffer(nullptr, 0u, nullptr, 0u, 0u);
    EXPECT_FALSE(commandList->commandRecordingFailed());
    commandList->close();
    const QueueSubmissionToken zeroWorkToken = submit();
    ASSERT_TRUE(zeroWorkToken.valid());
    EXPECT_EQ(zeroWorkToken.value, lastAcceptedToken.value + 1u);
    lastAcceptedToken = zeroWorkToken;

    commandList->open();
    commandList->copyBuffer(buffer.get(), sizeof(u32) * 2u, buffer.get(), 0u, sizeof(u32) * 2u);
    EXPECT_FALSE(commandList->commandRecordingFailed());
    EXPECT_EQ(commandList->getBufferState(buffer.get()), ResourceStates::CopySource | ResourceStates::CopyDest);
    commandList->close();
    const QueueSubmissionToken selfCopyToken = submit();
    ASSERT_TRUE(selfCopyToken.valid());
    EXPECT_EQ(selfCopyToken.value, lastAcceptedToken.value + 1u);
    ASSERT_TRUE(device.waitForIdle());

    const u32* const copiedWords = static_cast<const u32*>(device.mapBuffer(buffer.get(), CpuAccessMode::Read));
    ASSERT_NE(copiedWords, nullptr);
    EXPECT_EQ(copiedWords[0u], s_InitialWords[0u]);
    EXPECT_EQ(copiedWords[1u], s_InitialWords[1u]);
    EXPECT_EQ(copiedWords[2u], s_InitialWords[0u]);
    EXPECT_EQ(copiedWords[3u], s_InitialWords[1u]);
    device.unmapBuffer(buffer.get());
}


TEST_F(DescriptorBufferRoundTripTest, ComputeAndPushSemanticRejectionsDiscardPriorWorkAndReopenInAllBuilds){
    auto& device = DescriptorBufferRoundTripTest::device();

    ShaderDesc shaderDesc(DescriptorBufferRoundTripTest::arena());
    shaderDesc
        .setShaderType(ShaderType::Compute)
        .setDebugName(Name("tests/descriptor_buffer/semantic_validation_compute"))
    ;
    const ShaderHandle shader = device.createShader(
        shaderDesc,
        s_DescriptorHeapRetirementComputeSpirv,
        sizeof(s_DescriptorHeapRetirementComputeSpirv)
    );
    ASSERT_TRUE(shader);

    ComputePipelineDesc zeroRangeDesc;
    zeroRangeDesc.setComputeShader(shader);
    const ComputePipelineHandle zeroRangePipeline = device.createComputePipeline(zeroRangeDesc);
    ASSERT_TRUE(zeroRangePipeline);

    BindingLayoutDesc pushLayoutDesc(DescriptorBufferRoundTripTest::arena());
    pushLayoutDesc
        .setVisibility(ShaderType::Compute)
        .addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32)))
    ;
    const BindingLayoutHandle pushLayout = device.createBindingLayout(pushLayoutDesc);
    ASSERT_TRUE(pushLayout);
    ComputePipelineDesc pushRangeDesc;
    pushRangeDesc
        .setComputeShader(shader)
        .addBindingLayout(pushLayout)
    ;
    const ComputePipelineHandle pushRangePipeline = device.createComputePipeline(pushRangeDesc);
    ASSERT_TRUE(pushRangePipeline);

    const BufferHandle oracle = device.createBuffer(
        BufferDesc()
            .setByteSize(sizeof(u32) * 4u)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    const BufferHandle indirectBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(sizeof(DispatchIndirectArguments))
            .setIsDrawIndirectArgs(true)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    const BufferHandle wrongUsageBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(sizeof(DispatchIndirectArguments))
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(oracle);
    ASSERT_TRUE(indirectBuffer);
    ASSERT_TRUE(wrongUsageBuffer);

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
        NullPipeline,
        DispatchWithoutPipeline,
        WrongIndirectUsage,
        IndirectWithoutBuffer,
        IndirectOffsetUnaligned,
        IndirectRangeOutOfBounds,
        PushNullData,
        PushSizeUnaligned,
        PushWithoutPipeline,
        PushWithoutRange,
        PushBeyondPipelineRange,
        PushBeyondUint32,
    };
    struct Case{
        Operation operation;
        const char* label;
    };
    const Case cases[] = {
        { Operation::NullPipeline, "null compute pipeline" },
        { Operation::DispatchWithoutPipeline, "dispatch without pipeline" },
        { Operation::WrongIndirectUsage, "wrong indirect usage" },
        { Operation::IndirectWithoutBuffer, "indirect dispatch without buffer" },
        { Operation::IndirectOffsetUnaligned, "indirect dispatch unaligned offset" },
        { Operation::IndirectRangeOutOfBounds, "indirect dispatch out of bounds" },
        { Operation::PushNullData, "push constants null data" },
        { Operation::PushSizeUnaligned, "push constants unaligned size" },
        { Operation::PushWithoutPipeline, "push constants without pipeline" },
        { Operation::PushWithoutRange, "push constants without range" },
        { Operation::PushBeyondPipelineRange, "push constants beyond pipeline range" },
        { Operation::PushBeyondUint32, "push constants beyond uint32" },
    };
    constexpr u32 s_PushValue = 0x1f2e3d4cu;
    constexpr u32 s_TwoPushValues[] = { s_PushValue, 0x55667788u };

    for(const Case& testCase : cases){
        if(testCase.operation == Operation::PushBeyondUint32 && sizeof(usize) <= sizeof(u32))
            continue;

        SCOPED_TRACE(testCase.label);
        commandList->open();
        commandList->clearBufferUInt(oracle.get(), 0x6e57424cu);
        ASSERT_FALSE(commandList->commandRecordingFailed());

        switch(testCase.operation){
        case Operation::NullPipeline:
            commandList->setComputeState(ComputeState{});
            break;
        case Operation::DispatchWithoutPipeline:
            commandList->dispatch(1u, 1u, 1u);
            break;
        case Operation::WrongIndirectUsage:
            commandList->setComputeState(
                ComputeState().setPipeline(zeroRangePipeline.get()).setIndirectParams(wrongUsageBuffer.get())
            );
            break;
        case Operation::IndirectWithoutBuffer:
            commandList->setComputeState(ComputeState().setPipeline(zeroRangePipeline.get()));
            commandList->dispatchIndirect(0u);
            break;
        case Operation::IndirectOffsetUnaligned:
            commandList->setComputeState(
                ComputeState().setPipeline(zeroRangePipeline.get()).setIndirectParams(indirectBuffer.get())
            );
            commandList->dispatchIndirect(2u);
            break;
        case Operation::IndirectRangeOutOfBounds:
            commandList->setComputeState(
                ComputeState().setPipeline(zeroRangePipeline.get()).setIndirectParams(indirectBuffer.get())
            );
            commandList->dispatchIndirect(sizeof(DispatchIndirectArguments));
            break;
        case Operation::PushNullData:
            commandList->setPushConstants(nullptr, sizeof(s_PushValue));
            break;
        case Operation::PushSizeUnaligned:
            commandList->setPushConstants(&s_PushValue, 2u);
            break;
        case Operation::PushWithoutPipeline:
            commandList->setPushConstants(&s_PushValue, sizeof(s_PushValue));
            break;
        case Operation::PushWithoutRange:
            commandList->setComputeState(ComputeState().setPipeline(zeroRangePipeline.get()));
            commandList->setPushConstants(&s_PushValue, sizeof(s_PushValue));
            break;
        case Operation::PushBeyondPipelineRange:
            commandList->setComputeState(ComputeState().setPipeline(pushRangePipeline.get()));
            commandList->setPushConstants(s_TwoPushValues, sizeof(s_TwoPushValues));
            break;
        case Operation::PushBeyondUint32:
            commandList->setPushConstants(&s_PushValue, static_cast<usize>(UINT32_MAX) + 1u);
            break;
        }

        EXPECT_TRUE(commandList->commandRecordingFailed());
        commandList->close();
        EXPECT_FALSE(commandList->hasCommandBuffer());
        EXPECT_FALSE(submit().valid());

        commandList->open();
        ASSERT_FALSE(commandList->commandRecordingFailed());
        commandList->setComputeState(ComputeState().setPipeline(zeroRangePipeline.get()));
        commandList->dispatch(1u, 1u, 1u);
        ASSERT_FALSE(commandList->commandRecordingFailed());
        commandList->close();
        const QueueSubmissionToken recoveredToken = submit();
        ASSERT_TRUE(recoveredToken.valid());
        EXPECT_EQ(recoveredToken.value, lastAcceptedToken.value + 1u);
        lastAcceptedToken = recoveredToken;
    }

    commandList->open();
    commandList->dispatch(0u, 1u, 1u);
    commandList->setPushConstants(nullptr, 0u);
    EXPECT_FALSE(commandList->commandRecordingFailed());
    commandList->close();
    const QueueSubmissionToken zeroWorkToken = submit();
    ASSERT_TRUE(zeroWorkToken.valid());
    EXPECT_EQ(zeroWorkToken.value, lastAcceptedToken.value + 1u);
    lastAcceptedToken = zeroWorkToken;

    const DispatchIndirectArguments indirectArguments;
    commandList->open();
    commandList->writeBuffer(indirectBuffer.get(), &indirectArguments, sizeof(indirectArguments));
    commandList->setComputeState(
        ComputeState().setPipeline(zeroRangePipeline.get()).setIndirectParams(indirectBuffer.get())
    );
    commandList->dispatchIndirect(0u);
    commandList->setComputeState(ComputeState().setPipeline(pushRangePipeline.get()));
    commandList->setPushConstants(&s_PushValue, sizeof(s_PushValue));
    commandList->dispatch(1u, 1u, 1u);
    EXPECT_FALSE(commandList->commandRecordingFailed());
    commandList->close();
    const QueueSubmissionToken positiveToken = submit();
    ASSERT_TRUE(positiveToken.valid());
    EXPECT_EQ(positiveToken.value, lastAcceptedToken.value + 1u);
    EXPECT_TRUE(device.waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

