// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Staging-texture creation and cross-family queue-sharing contract coverage.


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


class StagingTextureContractTest : public ::testing::Test{
protected:
    static void SetUpTestSuite(){
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
        // Re-exec death tests instead of forking the live multi-threaded Vulkan fixture.
        GTEST_FLAG_SET(death_test_style, "threadsafe");
#endif

        s_logger.emplace();
        s_loggerGuard.emplace(*s_logger);

        s_scope = MakeUnique<HeadlessGraphicsScope>();
        if(!s_scope->setTransferQueueEnabled(true)){
            GTEST_SKIP() << "Staging texture contract: transfer-queue configuration is unavailable.";
            return;
        }
        if(!s_scope->initialize()){
            GTEST_SKIP() << "Staging texture contract: no usable validation-enabled headless Vulkan device.";
            return;
        }
        s_validationBackedDeviceInitialized = true;
    }

    static void TearDownTestSuite(){
        s_scope.reset();
        if(s_validationBackedDeviceInitialized && s_logger.has_value()){
            EXPECT_FALSE(s_logger->sawMessageContaining(NWB_TEXT("Vulkan debug: [severity=error")))
                << "validation-enabled staging-texture smoke emitted a Vulkan severity=error message";
        }
        s_loggerGuard.reset();
        s_logger.reset();
        s_validationBackedDeviceInitialized = false;
    }

    [[nodiscard]] static GraphicsBackend::Device& device(){
        return s_scope->graphics().getDevice();
    }

protected:
    static bool s_validationBackedDeviceInitialized;
    static UniquePtr<HeadlessGraphicsScope> s_scope;
    static Optional<CapturingLogger> s_logger;
    static Optional<Common::LoggerRegistrationGuard> s_loggerGuard;
};

bool StagingTextureContractTest::s_validationBackedDeviceInitialized = false;
UniquePtr<HeadlessGraphicsScope> StagingTextureContractTest::s_scope;
Optional<CapturingLogger> StagingTextureContractTest::s_logger;
Optional<Common::LoggerRegistrationGuard> StagingTextureContractTest::s_loggerGuard;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(StagingTextureContractTest, CreationRejectsInvalidInputsAndPreservesAllValidShapes){
    auto& device = StagingTextureContractTest::device();
    const auto expectDiagnosticRejection = [](auto&& operation){
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
        EXPECT_DEATH_IF_SUPPORTED({
            EXPECT_FALSE(operation());
        }, "");
#else
        EXPECT_FALSE(operation());
#endif
    };

    const TextureDesc baseDesc = TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setFormat(Format::RGBA8_UNORM)
        .setDimension(TextureDimension::Texture2D)
        .setInitialState(ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    expectDiagnosticRejection([&](){
        return device.createStagingTexture(baseDesc, CpuAccessMode::None).get() != nullptr;
    });
    expectDiagnosticRejection([&](){
        return device.createStagingTexture(
            baseDesc,
            static_cast<CpuAccessMode::Enum>(UINT8_MAX)
        ).get() != nullptr;
    });

    TextureDesc invalidDimensionDesc = baseDesc;
    invalidDimensionDesc.dimension = static_cast<TextureDimension::Enum>(UINT8_MAX);
    expectDiagnosticRejection([&](){
        return device.createStagingTexture(invalidDimensionDesc, CpuAccessMode::Write).get() != nullptr;
    });
    expectDiagnosticRejection([&](){
        return device.createTexture(invalidDimensionDesc).get() != nullptr;
    });

    struct ValidShapeCase{
        TextureDimension::Enum dimension;
        u32 height;
        u32 depth;
        u32 arraySize;
    };
    static constexpr ValidShapeCase s_ValidShapes[] = {
        { TextureDimension::Texture1D, 1u, 1u, 1u },
        { TextureDimension::Texture1DArray, 1u, 1u, 2u },
        { TextureDimension::Texture2D, 4u, 1u, 1u },
        { TextureDimension::Texture2DArray, 4u, 1u, 2u },
        { TextureDimension::TextureCube, 4u, 1u, 6u },
        { TextureDimension::TextureCubeArray, 4u, 1u, 12u },
        { TextureDimension::Texture2DMS, 4u, 1u, 1u },
        { TextureDimension::Texture2DMSArray, 4u, 1u, 2u },
        { TextureDimension::Texture3D, 4u, 2u, 1u },
    };
    for(const ValidShapeCase& shapeCase : s_ValidShapes){
        SCOPED_TRACE(static_cast<u32>(shapeCase.dimension));
        TextureDesc shapeDesc = baseDesc;
        shapeDesc.dimension = shapeCase.dimension;
        shapeDesc.height = shapeCase.height;
        shapeDesc.depth = shapeCase.depth;
        shapeDesc.arraySize = shapeCase.arraySize;
        shapeDesc.sampleCount = 1u;
        const StagingTextureHandle staging = device.createStagingTexture(shapeDesc, CpuAccessMode::Write);
        ASSERT_TRUE(staging);
    }

    const StagingTextureHandle validRead = device.createStagingTexture(baseDesc, CpuAccessMode::Read);
    const StagingTextureHandle validWrite = device.createStagingTexture(baseDesc, CpuAccessMode::Write);
    const TextureHandle validTexture = device.createTexture(baseDesc);
    ASSERT_TRUE(validRead);
    ASSERT_TRUE(validWrite);
    ASSERT_TRUE(validTexture);
}


TEST_F(StagingTextureContractTest, GraphicsAndTransferStagingBufferIsUsedAcrossDistinctFamilies){
    auto& device = StagingTextureContractTest::device();
    if(!device.getQueue(CommandQueue::Transfer))
        GTEST_SKIP() << "Staging texture queue sharing: adapter exposes no Transfer queue.";

    const u32 graphicsFamily = device.getQueueFamilyIndex(CommandQueue::Graphics);
    const u32 transferFamily = device.getQueueFamilyIndex(CommandQueue::Transfer);
    if(graphicsFamily == transferFamily)
        GTEST_SKIP() << "Staging texture queue sharing: Graphics and Transfer collapse to one queue family.";
    ASSERT_TRUE(device.usesConcurrentQueueSharing(ResourceQueueSharing::GraphicsAndTransfer));

    static constexpr u32 s_Width = 4u;
    static constexpr u32 s_Height = 4u;
    static constexpr u32 s_Pixels[s_Height][s_Width] = {
        { 0x10293847u, 0x55667788u, 0xa5a5c3c3u, 0xdeadbeefu },
        { 0x89abcdefu, 0x76543210u, 0xcafef00du, 0x31415926u },
        { 0x00112233u, 0x44556677u, 0x8899aabbu, 0xccddeeffu },
        { 0xfedcba98u, 0x13579bdfu, 0x2468ace0u, 0x0badf00du },
    };
    const TextureDesc sharedDesc = TextureDesc()
        .setWidth(s_Width)
        .setHeight(s_Height)
        .setFormat(Format::RGBA8_UNORM)
        .setDimension(TextureDimension::Texture2D)
        .setInitialState(ResourceStates::Common)
        .setKeepInitialState(true)
        .setQueueSharing(ResourceQueueSharing::GraphicsAndTransfer)
    ;
    const TextureHandle texture = device.createTexture(sharedDesc);
    const StagingTextureHandle upload = device.createStagingTexture(sharedDesc, CpuAccessMode::Write);
    const StagingTextureHandle readback = device.createStagingTexture(sharedDesc, CpuAccessMode::Read);
    ASSERT_TRUE(texture);
    ASSERT_TRUE(upload);
    ASSERT_TRUE(readback);
#if !defined(NWB_FINAL)
    const auto& nativeSharing = upload->getNativeQueueFamilySharingForTesting();
    EXPECT_EQ(nativeSharing.mode, VK_SHARING_MODE_CONCURRENT);
    bool includesGraphicsFamily = false;
    bool includesTransferFamily = false;
    for(u32 familyIndex = 0u; familyIndex < nativeSharing.familyIndexCount; ++familyIndex){
        includesGraphicsFamily = includesGraphicsFamily || nativeSharing.familyIndices[familyIndex] == graphicsFamily;
        includesTransferFamily = includesTransferFamily || nativeSharing.familyIndices[familyIndex] == transferFamily;
    }
    EXPECT_TRUE(includesGraphicsFamily);
    EXPECT_TRUE(includesTransferFamily);
#endif

    usize uploadRowPitch = 0u;
    u8* const uploadBytes = static_cast<u8*>(
        device.mapStagingTexture(upload.get(), TextureSlice{}, CpuAccessMode::Write, &uploadRowPitch)
    );
    ASSERT_NE(uploadBytes, nullptr);
    ASSERT_GE(uploadRowPitch, static_cast<usize>(s_Width) * sizeof(u32));
    for(u32 row = 0u; row < s_Height; ++row){
        u32* const uploadRow = reinterpret_cast<u32*>(uploadBytes + static_cast<usize>(row) * uploadRowPitch);
        for(u32 column = 0u; column < s_Width; ++column)
            uploadRow[column] = s_Pixels[row][column];
    }
    device.unmapStagingTexture(upload.get());

    CommandListParameters transferParameters;
    transferParameters.setQueueType(CommandQueue::Transfer);
    const CommandListHandle transferList = device.createCommandList(transferParameters);
    ASSERT_TRUE(transferList);
    transferList->open();
    transferList->copyTexture(texture.get(), TextureSlice{}, upload.get(), TextureSlice{});
    transferList->close();
    ASSERT_FALSE(transferList->commandRecordingFailed());
    ASSERT_TRUE(transferList->hasCommandBuffer());
    CommandList* const transferLists[] = { transferList.get() };
    const QueueSubmissionToken transferToken = device.executeCommandLists(
        transferLists,
        LengthOf(transferLists),
        CommandQueue::Transfer,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(transferToken.valid());
    ASSERT_EQ(transferToken.queue, CommandQueue::Transfer);
    ASSERT_TRUE(device.waitForIdle());

    const CommandListHandle graphicsList = device.createCommandList();
    ASSERT_TRUE(graphicsList);
    graphicsList->open();
    graphicsList->copyTexture(texture.get(), TextureSlice{}, upload.get(), TextureSlice{});
    graphicsList->copyTexture(readback.get(), TextureSlice{}, texture.get(), TextureSlice{});
    graphicsList->close();
    ASSERT_FALSE(graphicsList->commandRecordingFailed());
    ASSERT_TRUE(graphicsList->hasCommandBuffer());
    CommandList* const graphicsLists[] = { graphicsList.get() };
    const QueueSubmissionToken graphicsToken = device.executeCommandLists(
        graphicsLists,
        LengthOf(graphicsLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(graphicsToken.valid());
    ASSERT_EQ(graphicsToken.queue, CommandQueue::Graphics);
    ASSERT_TRUE(device.waitForIdle());

    usize readbackRowPitch = 0u;
    const u8* const readbackBytes = static_cast<const u8*>(
        device.mapStagingTexture(readback.get(), TextureSlice{}, CpuAccessMode::Read, &readbackRowPitch)
    );
    ASSERT_NE(readbackBytes, nullptr);
    ASSERT_GE(readbackRowPitch, static_cast<usize>(s_Width) * sizeof(u32));
    for(u32 row = 0u; row < s_Height; ++row){
        const u32* const readbackRow = reinterpret_cast<const u32*>(
            readbackBytes + static_cast<usize>(row) * readbackRowPitch
        );
        for(u32 column = 0u; column < s_Width; ++column)
            EXPECT_EQ(readbackRow[column], s_Pixels[row][column]);
    }
    device.unmapStagingTexture(readback.get());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

