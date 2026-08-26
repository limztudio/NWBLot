// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <global/global.h>
#include <global/unique_ptr.h>
#include <core/graphics/capture/command_ir.h>
#include <core/graphics/task_graph/compiler.h>
#include <core/graphics/task_graph/packet_runtime.h>
#include <core/graphics/vulkan/texture_clear_detail.h>
#include <tests/common/capturing_logger.h>
#include <tests/common/headless_graphics_scope.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_texture_clear_staging_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace PoisonedClearKind{
    enum Enum : u8{
        WholeTexture = 0u,
        TextureRect,
    };
};

struct PoisonedClearHookResult{
    GpuTaskGraphRecordingStatistics recordingStatistics;
    usize commandIrRecordCount = 0u;
    u32 beforeCount = 0u;
    u32 afterCount = 0u;
    u32 discardedCount = 0u;
    bool recordPacketAccepted = false;
    bool acceptedTokenCleared = false;
    bool recordedPacketPresent = false;
    bool discardUnacceptedSucceeded = false;
};

[[nodiscard]] static bool PoisonClearBeforeRecord(
    void* const rawResult,
    CommandList& commandList,
    const GpuTaskRecordContext& recordContext
){
    static_cast<void>(recordContext);
    PoisonedClearHookResult* const result = static_cast<PoisonedClearHookResult*>(rawResult);
    if(!result)
        return false;

    ++result->beforeCount;
    commandList.clearTextureUInt(nullptr, TextureSubresourceSet{}, 0u);
    return true;
}

[[nodiscard]] static bool CountClearAfterRecord(
    void* const rawResult,
    CommandList& commandList,
    const GpuTaskRecordContext& recordContext
){
    static_cast<void>(commandList);
    static_cast<void>(recordContext);
    PoisonedClearHookResult* const result = static_cast<PoisonedClearHookResult*>(rawResult);
    if(!result)
        return false;

    ++result->afterCount;
    return true;
}

static void CountDiscardedClear(void* const rawResult){
    PoisonedClearHookResult* const result = static_cast<PoisonedClearHookResult*>(rawResult);
    if(result)
        ++result->discardedCount;
}

[[nodiscard]] static bool ExercisePoisonedClearHooks(
    GraphicsBackend::Device& device,
    GraphicsArena& arena,
    const PoisonedClearKind::Enum clearKind,
    PoisonedClearHookResult& result
){
    result = {};
    TextureHandle texture = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setDimension(TextureDimension::Texture2D)
            .setFormat(Format::R8_UINT)
            .setInitialState(ResourceStates::Common)
            .setQueueSharing(ResourceQueueSharing::GraphicsAndTransfer)
    );
    if(!texture)
        return false;

    GpuTaskGraph graph(arena);
    const GpuGraphResourceId destination = graph.importTexture(
        texture,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/texture_clear_staging/poisoned_hook_destination"))
            .setMarkerLabel("Poisoned Clear Hook Destination")
            .setType(GpuGraphResourceType::Texture)
    );
    if(!destination.valid())
        return false;

    const GpuClearTextureTaskRecordHooks recordHooks{
        .context = &result,
        .beforeClear = &PoisonClearBeforeRecord,
        .afterClear = &CountClearAfterRecord,
        .discarded = &CountDiscardedClear,
    };
    const GpuQueueRequest queueRequest{
        static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Transfer)
            | static_cast<u8>(GpuQueueCapability::Graphics)
        ),
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskDesc taskDesc;
    taskDesc
        .setIdentity(Name("tests/texture_clear_staging/poisoned_clear_hook"))
        .setMarkerLabel("Poisoned Clear Hook")
        .setQueue(queueRequest)
    ;

    QueueSubmissionToken acceptedToken;
    GpuTaskId task;
    if(clearKind == PoisonedClearKind::WholeTexture){
        GpuClearTextureTaskDesc clearDesc;
        clearDesc.acceptedToken = &acceptedToken;
        clearDesc.destination = destination;
        clearDesc.subresources = TextureSubresourceSet(0u, 1u, 0u, 1u);
        clearDesc.recordHooks = recordHooks;
        clearDesc.valueType = GpuClearTextureTaskValueType::UInt;
        clearDesc.uintValue = UIntColor(0x5au);
        task = graph.addClearTextureTask(taskDesc, clearDesc);
    }
    else if(clearKind == PoisonedClearKind::TextureRect){
        GpuClearTextureRectUIntTaskDesc clearDesc;
        clearDesc.acceptedToken = &acceptedToken;
        clearDesc.destination = destination;
        clearDesc.subresources = TextureSubresourceSet(0u, 1u, 0u, 1u);
        clearDesc.rect = Rect(2, 2);
        clearDesc.uintValue = UIntColor(0x5au);
        clearDesc.recordHooks = recordHooks;
        task = graph.addClearTextureRectUIntTask(taskDesc, clearDesc);
    }
    else
        return false;
    if(!task.valid())
        return false;

    acceptedToken = QueueSubmissionToken{
        .queue = CommandQueue::Graphics,
        .value = 1u,
    };
    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    const GpuPhysicalQueueInfo* const queueInfo = device.getPhysicalQueueInfo(graphicsQueue);
    if(!queueInfo)
        return false;
    const GpuTaskGraphQueueTopology topology{
        .queues = queueInfo,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(arena);
    GpuTaskGraphQueueAssignments assignments(arena);
    GpuCompiledGraph compiledGraph(arena);
    Alloc::ScratchArena scratchArena(Name("tests/texture_clear_staging/poisoned_hook_scratch"));
    const GpuTaskGraphCompiler compiler;
    if(!compiler.compile(graph, analysis, topology, assignments, compiledGraph, scratchArena))
        return false;

    const GpuSubmissionPacketId packet = compiledGraph.packetForTask(task);
    if(!packet.valid())
        return false;
    GpuRecordedGraph recordedGraph(arena);
    GpuCommandIrCapture commandIrCapture(arena);
    const GpuNativePacketRecorder recorder(device);
    result.recordPacketAccepted = recorder.recordPacket(
        graph,
        compiledGraph,
        GpuNativePacketRecordDesc{ .packet = packet },
        recordedGraph,
        &commandIrCapture
    );
    result.acceptedTokenCleared = !acceptedToken.valid();
    result.recordedPacketPresent = recordedGraph.find(packet) != nullptr;
    result.commandIrRecordCount = commandIrCapture.recordCount();
    result.recordingStatistics = recordedGraph.recordingStatistics(compiledGraph);

    {
        GpuGraphSubmissionTransaction transaction(arena);
        transaction.reset(compiledGraph);
        result.discardUnacceptedSucceeded = transaction.discardUnaccepted(
            graph,
            compiledGraph,
            recordedGraph.recordingAttemptGeneration()
        );
    }
    graph.reset();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(TextureClearUploadLayoutTest, UsesCommonTexelBlockAndCopyOffsetAlignment){
    using TextureClearUploadLayout = GraphicsBackend::VulkanTextureDetail::TextureClearUploadLayout;

    TextureClearUploadLayout r8ArrayLayout;
    ASSERT_TRUE(GraphicsBackend::VulkanTextureDetail::BuildTextureClearUploadLayout(
        9ull,
        1u,
        2ull,
        r8ArrayLayout
    ));
    EXPECT_EQ(r8ArrayLayout.uploadSize, 9ull);
    EXPECT_EQ(r8ArrayLayout.layerPitch, 12ull);
    EXPECT_EQ(r8ArrayLayout.clearByteCount, 21u);
    EXPECT_EQ(r8ArrayLayout.copyOffsetAlignment, 4u);
    EXPECT_EQ(r8ArrayLayout.stagingAlignment, 256u);
    EXPECT_TRUE(r8ArrayLayout.mergeArrayLayerCopies);

    TextureClearUploadLayout sixByteLayout;
    ASSERT_TRUE(GraphicsBackend::VulkanTextureDetail::BuildTextureClearUploadLayout(
        1ull,
        6u,
        2ull,
        sixByteLayout
    ));
    EXPECT_EQ(sixByteLayout.uploadSize, 6ull);
    EXPECT_EQ(sixByteLayout.layerPitch, 12ull);
    EXPECT_EQ(sixByteLayout.clearByteCount, 18u);
    EXPECT_EQ(sixByteLayout.copyOffsetAlignment, 12u);
    EXPECT_EQ(sixByteLayout.stagingAlignment, 768u);
    EXPECT_TRUE(sixByteLayout.mergeArrayLayerCopies);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TextureClearStagingTest : public ::testing::Test{
protected:
    static void SetUpTestSuite(){
        s_logger.emplace();
        s_loggerGuard.emplace(*s_logger);

        s_scope = MakeUnique<HeadlessGraphicsScope>();
        if(!s_scope->setTransferQueueEnabled(true) || !s_scope->initialize()){
            GTEST_SKIP() << "Texture-clear staging: no validation-enabled headless Vulkan device.";
            return;
        }
        s_validationBackedDeviceInitialized = true;
    }

    static void TearDownTestSuite(){
        s_scope.reset();
        if(s_validationBackedDeviceInitialized && s_logger.has_value()){
            EXPECT_FALSE(s_logger->sawMessageContaining(NWB_TEXT("Vulkan debug: [severity=error")))
                << "texture-clear staging tests emitted a Vulkan severity=error message";
        }
        s_loggerGuard.reset();
        s_logger.reset();
        s_validationBackedDeviceInitialized = false;
    }

    [[nodiscard]] static GraphicsBackend::Device& device(){
        return s_scope->graphics().getDevice();
    }

    [[nodiscard]] static Core::Alloc::GlobalArena& arena(){
        return s_scope->arena();
    }

    [[nodiscard]] static bool submitAndWait(CommandList& commandList){
        commandList.close();
        if(commandList.commandRecordingFailed() || !commandList.hasCommandBuffer())
            return false;

        CommandList* const commandLists[] = { &commandList };
        const QueueSubmissionToken token = device().executeCommandLists(
            commandLists,
            LengthOf(commandLists),
            commandList.getDescription().physicalQueue,
            QueueSubmissionDesc{}
        );
        return token.valid() && device().waitForIdle();
    }


protected:
    static bool s_validationBackedDeviceInitialized;
    static UniquePtr<HeadlessGraphicsScope> s_scope;
    static Optional<CapturingLogger> s_logger;
    static Optional<Common::LoggerRegistrationGuard> s_loggerGuard;
};

bool TextureClearStagingTest::s_validationBackedDeviceInitialized = false;
UniquePtr<HeadlessGraphicsScope> TextureClearStagingTest::s_scope;
Optional<CapturingLogger> TextureClearStagingTest::s_logger;
Optional<Common::LoggerRegistrationGuard> TextureClearStagingTest::s_loggerGuard;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(TextureClearStagingTest, PoisonedBeforeClearSuppressesAfterClearAndRollsBackWholeAndRectTasks){
    if((device().queryFormatSupport(Format::R8_UINT) & FormatSupport::Texture) != FormatSupport::Texture)
        GTEST_SKIP() << "Texture-clear hooks: R8_UINT textures are unsupported.";

    const __hidden_texture_clear_staging_tests::PoisonedClearKind::Enum clearKinds[] = {
        __hidden_texture_clear_staging_tests::PoisonedClearKind::WholeTexture,
        __hidden_texture_clear_staging_tests::PoisonedClearKind::TextureRect,
    };
    for(const auto clearKind : clearKinds){
        SCOPED_TRACE(
            clearKind == __hidden_texture_clear_staging_tests::PoisonedClearKind::WholeTexture
            ? "whole texture clear"
            : "texture rect clear"
        );
        __hidden_texture_clear_staging_tests::PoisonedClearHookResult result;
        ASSERT_TRUE(__hidden_texture_clear_staging_tests::ExercisePoisonedClearHooks(
            device(),
            arena(),
            clearKind,
            result
        ));

        EXPECT_FALSE(result.recordPacketAccepted);
        EXPECT_EQ(result.beforeCount, 1u);
        EXPECT_EQ(result.afterCount, 0u);
        EXPECT_EQ(result.discardedCount, 1u);
        EXPECT_TRUE(result.acceptedTokenCleared);
        EXPECT_FALSE(result.recordedPacketPresent);
        EXPECT_EQ(result.commandIrRecordCount, 0u);
        EXPECT_TRUE(result.discardUnacceptedSucceeded);
        ASSERT_TRUE(result.recordingStatistics.valid());
        EXPECT_EQ(result.recordingStatistics.packetCount, 0u);
        EXPECT_EQ(result.recordingStatistics.taskCount, 0u);
        EXPECT_EQ(result.recordingStatistics.commandListCount, 0u);
        EXPECT_EQ(result.recordingStatistics.barrierCount, 0u);
        EXPECT_EQ(result.recordingStatistics.parallelPacketCount, 0u);
        EXPECT_EQ(result.recordingStatistics.commandListAcquisitionSeconds, 0.0);
        EXPECT_EQ(result.recordingStatistics.graphBarrierRecordingSeconds, 0.0);
        EXPECT_EQ(result.recordingStatistics.taskRecordSeconds, 0.0);
        EXPECT_EQ(result.recordingStatistics.recordingSeconds, 0.0);
    }
}

TEST_F(TextureClearStagingTest, R8ArrayClearUsesAlignedMergedLayerOffsets){
    static constexpr u32 s_TextureWidth = 4u;
    static constexpr u32 s_TextureHeight = 3u;
    static constexpr u32 s_ClearWidth = 3u;
    static constexpr u32 s_ArraySize = 2u;
    static constexpr u32 s_ClearValue = 0x5au;

    if((device().queryFormatSupport(Format::R8_UINT) & FormatSupport::Texture) != FormatSupport::Texture)
        GTEST_SKIP() << "Texture-clear staging: R8_UINT textures are unsupported.";

    const TextureDesc desc = TextureDesc()
        .setWidth(s_TextureWidth)
        .setHeight(s_TextureHeight)
        .setArraySize(s_ArraySize)
        .setDimension(TextureDimension::Texture2DArray)
        .setFormat(Format::R8_UINT)
        .setInitialState(ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    TextureHandle texture = device().createTexture(desc);
    StagingTextureHandle readback = device().createStagingTexture(desc, CpuAccessMode::Read);
    if(!texture || !readback)
        GTEST_SKIP() << "Texture-clear staging: R8_UINT array readback is unavailable.";

    const u32 errorCountBeforeRecording = s_logger->errorCount();
    const TextureSubresourceSet subresources = TextureSubresourceSet{}.setArraySlices(0u, s_ArraySize);
    const Rect clearRect(static_cast<i32>(s_ClearWidth), static_cast<i32>(s_TextureHeight));

    CommandListHandle commandList = device().createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    commandList->clearTextureRectUInt(texture.get(), subresources, clearRect, s_ClearValue);
    for(u32 arraySlice = 0u; arraySlice < s_ArraySize; ++arraySlice){
        const TextureSlice slice = TextureSlice{}.setArraySlice(arraySlice);
        commandList->copyTexture(readback.get(), slice, texture.get(), slice);
    }
    EXPECT_FALSE(commandList->commandRecordingFailed());
    ASSERT_TRUE(submitAndWait(*commandList));

    for(u32 arraySlice = 0u; arraySlice < s_ArraySize; ++arraySlice){
        const TextureSlice slice = TextureSlice{}.setArraySlice(arraySlice);
        usize rowPitch = 0u;
        const u8* const mappedBytes = static_cast<const u8*>(
            device().mapStagingTexture(readback.get(), slice, CpuAccessMode::Read, &rowPitch)
        );
        ASSERT_NE(mappedBytes, nullptr);
        ASSERT_GE(rowPitch, static_cast<usize>(s_TextureWidth));
        for(u32 row = 0u; row < s_TextureHeight; ++row){
            const u8* const mappedRow = mappedBytes + static_cast<usize>(row) * rowPitch;
            for(u32 column = 0u; column < s_ClearWidth; ++column)
                EXPECT_EQ(mappedRow[column], static_cast<u8>(s_ClearValue));
        }
        device().unmapStagingTexture(readback.get());
    }

    EXPECT_EQ(s_logger->errorCount(), errorCountBeforeRecording);
}

TEST_F(TextureClearStagingTest, SequentialRgb32ClearsUseTexelBlockAlignedSuballocations){
    static constexpr u32 s_TextureWidth = 2u;
    static constexpr u32 s_TextureHeight = 1u;
    static constexpr usize s_ComponentCount = 3u;
    static constexpr Color s_FirstClear(0.25f, 0.5f, 0.75f, 0.0f);
    static constexpr Color s_SecondClear(1.25f, -2.5f, 3.75f, 0.0f);

    if((device().queryFormatSupport(Format::RGB32_FLOAT) & FormatSupport::Texture) != FormatSupport::Texture)
        GTEST_SKIP() << "Texture-clear staging: RGB32_FLOAT textures are unsupported.";

    const TextureDesc desc = TextureDesc()
        .setWidth(s_TextureWidth)
        .setHeight(s_TextureHeight)
        .setDimension(TextureDimension::Texture2D)
        .setFormat(Format::RGB32_FLOAT)
        .setInitialState(ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    TextureHandle firstTexture = device().createTexture(desc);
    TextureHandle secondTexture = device().createTexture(desc);
    StagingTextureHandle readback = device().createStagingTexture(desc, CpuAccessMode::Read);
    if(!firstTexture || !secondTexture || !readback)
        GTEST_SKIP() << "Texture-clear staging: RGB32_FLOAT readback is unavailable.";

    const u32 errorCountBeforeRecording = s_logger->errorCount();
    const Rect clearRect(1, 1);

    CommandListHandle commandList = device().createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    commandList->clearTextureRectFloat(firstTexture.get(), TextureSubresourceSet{}, clearRect, s_FirstClear);
    commandList->clearTextureRectFloat(secondTexture.get(), TextureSubresourceSet{}, clearRect, s_SecondClear);
    commandList->copyTexture(readback.get(), TextureSlice{}, secondTexture.get(), TextureSlice{});
    EXPECT_FALSE(commandList->commandRecordingFailed());
    ASSERT_TRUE(submitAndWait(*commandList));

    usize rowPitch = 0u;
    const u8* const mappedBytes = static_cast<const u8*>(
        device().mapStagingTexture(readback.get(), TextureSlice{}, CpuAccessMode::Read, &rowPitch)
    );
    ASSERT_NE(mappedBytes, nullptr);
    ASSERT_GE(rowPitch, static_cast<usize>(s_TextureWidth) * s_ComponentCount * sizeof(f32));
    f32 actualClear[s_ComponentCount] = {};
    NWB_MEMCPY(actualClear, sizeof(actualClear), mappedBytes, sizeof(actualClear));
    device().unmapStagingTexture(readback.get());

    EXPECT_FLOAT_EQ(actualClear[0], s_SecondClear.r);
    EXPECT_FLOAT_EQ(actualClear[1], s_SecondClear.g);
    EXPECT_FLOAT_EQ(actualClear[2], s_SecondClear.b);
    EXPECT_EQ(s_logger->errorCount(), errorCountBeforeRecording);
}

TEST_F(TextureClearStagingTest, TransferOnlyQueueRejectsPartialColorClear){
    if((device().queryFormatSupport(Format::R8_UINT) & FormatSupport::Texture) != FormatSupport::Texture)
        GTEST_SKIP() << "Texture-clear staging: R8_UINT textures are unsupported.";

    const GpuPhysicalQueueTopology topology = device().getPhysicalQueueTopology();
    const GpuPhysicalQueueInfo* transferOnlyQueue = nullptr;
    for(usize queueIndex = 0u; queueIndex < topology.queueCount; ++queueIndex){
        const GpuPhysicalQueueInfo& candidate = topology.queues[queueIndex];
        const u8 capabilities = static_cast<u8>(candidate.capabilities);
        if(
            (capabilities & static_cast<u8>(GpuQueueCapability::Transfer)) != 0u
            && (capabilities & static_cast<u8>(GpuQueueCapability::Graphics)) == 0u
            && (capabilities & static_cast<u8>(GpuQueueCapability::Compute)) == 0u
        ){
            transferOnlyQueue = &candidate;
            break;
        }
    }
    if(!transferOnlyQueue)
        GTEST_SKIP() << "Texture-clear staging: no transfer-only physical queue.";

    const TextureDesc desc = TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setDimension(TextureDimension::Texture2D)
        .setFormat(Format::R8_UINT)
        .setInitialState(ResourceStates::Common)
        .setQueueSharing(ResourceQueueSharing::GraphicsAndTransfer)
    ;
    TextureHandle texture = device().createTexture(desc);
    ASSERT_TRUE(texture);

    CommandListParameters parameters;
    parameters.setPhysicalQueue(transferOnlyQueue->id);
    CommandListHandle commandList = device().createCommandList(parameters);
    ASSERT_TRUE(commandList);

    const u32 textureReferences = texture->getReferenceCount();
    commandList->open();
    commandList->clearTextureRectUInt(texture.get(), TextureSubresourceSet{}, Rect(2, 2), 0x5au);
    EXPECT_TRUE(commandList->commandRecordingFailed());
    EXPECT_FALSE(commandList->hasExplicitTextureSubresourceState(texture.get(), 0u, 0u));
    EXPECT_EQ(texture->getReferenceCount(), textureReferences);
    commandList->close();
    EXPECT_FALSE(commandList->hasCommandBuffer());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

