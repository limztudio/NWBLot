// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Staging-texture creation and cross-family queue-sharing contract coverage.


#include <gtest/gtest.h>

#include <global/global.h>
#include <global/unique_ptr.h>
#include <core/graphics/vulkan/backend.h>
#include <core/graphics/vulkan/texture_resource_detail.h>
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

    struct NonArraySizeCase{
        TextureDimension::Enum nonArrayDimension;
        TextureDimension::Enum arrayDimension;
        u32 height;
    };
    static constexpr NonArraySizeCase s_NonArraySizeCases[] = {
        { TextureDimension::Texture1D, TextureDimension::Texture1DArray, 1u },
        { TextureDimension::Texture2D, TextureDimension::Texture2DArray, 4u },
        { TextureDimension::Texture2DMS, TextureDimension::Texture2DMSArray, 4u },
    };
    for(const NonArraySizeCase& sizeCase : s_NonArraySizeCases){
        SCOPED_TRACE(static_cast<u32>(sizeCase.nonArrayDimension));
        TextureDesc invalidArraySizeDesc = baseDesc;
        invalidArraySizeDesc.dimension = sizeCase.nonArrayDimension;
        invalidArraySizeDesc.height = sizeCase.height;
        invalidArraySizeDesc.arraySize = 2u;
        EXPECT_FALSE(GraphicsBackend::VulkanTextureDetail::IsTextureDescShapeValid(invalidArraySizeDesc));
        expectDiagnosticRejection([&](){
            return device.createStagingTexture(invalidArraySizeDesc, CpuAccessMode::Write).get() != nullptr;
        });
        expectDiagnosticRejection([&](){
            return device.createTexture(invalidArraySizeDesc).get() != nullptr;
        });

        TextureDesc validSingleLayerArrayDesc = invalidArraySizeDesc;
        validSingleLayerArrayDesc.dimension = sizeCase.arrayDimension;
        validSingleLayerArrayDesc.arraySize = 1u;
        EXPECT_TRUE(GraphicsBackend::VulkanTextureDetail::IsTextureDescShapeValid(validSingleLayerArrayDesc));
        const StagingTextureHandle validStaging = device.createStagingTexture(
            validSingleLayerArrayDesc,
            CpuAccessMode::Write
        );
        const TextureHandle validTextureArray = device.createTexture(validSingleLayerArrayDesc);
        ASSERT_TRUE(validStaging);
        ASSERT_TRUE(validTextureArray);
    }

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


TEST_F(StagingTextureContractTest, MappingRejectsInvalidInputsWithoutMutatingPitchAndRecovers){
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
    const auto expectDiagnosticVoidRejection = [](auto&& operation){
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
        EXPECT_DEATH_IF_SUPPORTED({
            operation();
        }, "");
#else
        operation();
#endif
    };

    TextureDesc desc = TextureDesc()
        .setWidth(8u)
        .setHeight(8u)
        .setFormat(Format::RGBA8_UNORM)
        .setDimension(TextureDimension::Texture2DArray)
        .setInitialState(ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    desc.mipLevels = 2u;
    desc.arraySize = 2u;
    const StagingTextureHandle readback = device.createStagingTexture(desc, CpuAccessMode::Read);
    const StagingTextureHandle upload = device.createStagingTexture(desc, CpuAccessMode::Write);
    ASSERT_TRUE(readback);
    ASSERT_TRUE(upload);

    static constexpr usize s_UnchangedPitch = 0x5a5a5a5au;
    const auto mapWasAcceptedOrChangedPitch = [&](
        GraphicsBackend::Device& mapDevice,
        StagingTexture* staging,
        const TextureSlice& slice,
        const CpuAccessMode::Enum access
    ){
        usize rowPitch = s_UnchangedPitch;
        void* const mapped = mapDevice.mapStagingTexture(staging, slice, access, &rowPitch);
        return mapped != nullptr || rowPitch != s_UnchangedPitch;
    };

    expectDiagnosticRejection([&](){
        return mapWasAcceptedOrChangedPitch(device, nullptr, TextureSlice{}, CpuAccessMode::Read);
    });
    expectDiagnosticVoidRejection([&](){ device.unmapStagingTexture(nullptr); });
    expectDiagnosticRejection([&](){
        return mapWasAcceptedOrChangedPitch(device, readback.get(), TextureSlice{}, CpuAccessMode::None);
    });
    expectDiagnosticRejection([&](){
        return mapWasAcceptedOrChangedPitch(
            device,
            readback.get(),
            TextureSlice{},
            static_cast<CpuAccessMode::Enum>(UINT8_MAX)
        );
    });
    expectDiagnosticRejection([&](){
        return mapWasAcceptedOrChangedPitch(device, readback.get(), TextureSlice{}, CpuAccessMode::Write);
    });
    expectDiagnosticRejection([&](){
        return mapWasAcceptedOrChangedPitch(device, upload.get(), TextureSlice{}, CpuAccessMode::Read);
    });

    const TextureSlice invalidSlices[] = {
        TextureSlice{}.setMipLevel(2u),
        TextureSlice{}.setArraySlice(2u),
        TextureSlice{}.setWidth(0u),
        TextureSlice{}.setHeight(0u),
        TextureSlice{}.setDepth(0u),
        TextureSlice{}.setOrigin(8u, 0u, 0u),
        TextureSlice{}.setOrigin(7u, 0u, 0u).setWidth(2u),
        TextureSlice{}.setOrigin(UINT32_MAX, 0u, 0u).setWidth(UINT32_MAX),
        TextureSlice{}.setOrigin(0u, 8u, 0u),
        TextureSlice{}.setOrigin(0u, 7u, 0u).setHeight(2u),
        TextureSlice{}.setOrigin(0u, UINT32_MAX, 0u).setHeight(UINT32_MAX),
        TextureSlice{}.setOrigin(0u, 0u, 1u),
        TextureSlice{}.setDepth(2u),
        TextureSlice{}.setOrigin(0u, 0u, UINT32_MAX).setDepth(UINT32_MAX),
    };
    for(const TextureSlice& invalidSlice : invalidSlices){
        expectDiagnosticRejection([&](){
            return mapWasAcceptedOrChangedPitch(device, readback.get(), invalidSlice, CpuAccessMode::Read);
        });
    }

    usize readbackPitch = s_UnchangedPitch;
    void* const readbackMemory = device.mapStagingTexture(
        readback.get(),
        TextureSlice{}.setOrigin(2u, 2u, 0u).setSize(3u, 3u, 1u).setArraySlice(1u),
        CpuAccessMode::Read,
        &readbackPitch
    );
    ASSERT_NE(readbackMemory, nullptr);
    EXPECT_NE(readbackPitch, s_UnchangedPitch);
    device.unmapStagingTexture(readback.get());

    const TextureDesc compressedDesc = TextureDesc()
        .setWidth(8u)
        .setHeight(8u)
        .setFormat(Format::BC1_UNORM)
        .setDimension(TextureDimension::Texture2D)
        .setInitialState(ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    const StagingTextureHandle compressed = device.createStagingTexture(compressedDesc, CpuAccessMode::Read);
    ASSERT_TRUE(compressed);
    expectDiagnosticRejection([&](){
        return mapWasAcceptedOrChangedPitch(
            device,
            compressed.get(),
            TextureSlice{}.setOrigin(1u, 0u, 0u).setSize(4u, 4u, 1u),
            CpuAccessMode::Read
        );
    });
    expectDiagnosticRejection([&](){
        return mapWasAcceptedOrChangedPitch(
            device,
            compressed.get(),
            TextureSlice{}.setOrigin(0u, 1u, 0u).setSize(4u, 4u, 1u),
            CpuAccessMode::Read
        );
    });
    expectDiagnosticRejection([&](){
        return mapWasAcceptedOrChangedPitch(
            device,
            compressed.get(),
            TextureSlice{}.setSize(2u, 4u, 1u),
            CpuAccessMode::Read
        );
    });
    usize compressedPitch = s_UnchangedPitch;
    ASSERT_NE(
        device.mapStagingTexture(compressed.get(), TextureSlice{}, CpuAccessMode::Read, &compressedPitch),
        nullptr
    );
    EXPECT_NE(compressedPitch, s_UnchangedPitch);
    device.unmapStagingTexture(compressed.get());
}


TEST_F(StagingTextureContractTest, MappingRejectsForeignDeviceAndOwnerRecovers){
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
    const auto expectDiagnosticVoidRejection = [](auto&& operation){
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
        EXPECT_DEATH_IF_SUPPORTED({
            operation();
        }, "");
#else
        operation();
#endif
    };

    const TextureDesc desc = TextureDesc()
        .setWidth(8u)
        .setHeight(8u)
        .setFormat(Format::RGBA8_UNORM)
        .setDimension(TextureDimension::Texture2D)
        .setInitialState(ResourceStates::Common)
        .setKeepInitialState(true)
    ;

    HeadlessGraphicsScope foreignScope;
    if(!foreignScope.initialize())
        GTEST_SKIP() << "Staging texture mapping ownership: a second Vulkan device is unavailable.";
    auto& foreignDevice = foreignScope.graphics().getDevice();
    const StagingTextureHandle foreignReadback = foreignDevice.createStagingTexture(desc, CpuAccessMode::Read);
    ASSERT_TRUE(foreignReadback);
    expectDiagnosticRejection([&](){
        usize rowPitch = 0x5a5a5a5au;
        void* const mapped = device.mapStagingTexture(
            foreignReadback.get(),
            TextureSlice{},
            CpuAccessMode::Read,
            &rowPitch
        );
        return mapped != nullptr || rowPitch != 0x5a5a5a5au;
    });
    expectDiagnosticVoidRejection([&](){ device.unmapStagingTexture(foreignReadback.get()); });

    usize foreignPitch = 0x5a5a5a5au;
    ASSERT_NE(
        foreignDevice.mapStagingTexture(
            foreignReadback.get(),
            TextureSlice{},
            CpuAccessMode::Read,
            &foreignPitch
        ),
        nullptr
    );
    EXPECT_NE(foreignPitch, 0x5a5a5a5au);
    foreignDevice.unmapStagingTexture(foreignReadback.get());
}


TEST_F(StagingTextureContractTest, MappingLifecycleIsPersistentAndInvalidateFailureIsTransactional){
    auto& device = StagingTextureContractTest::device();
    const TextureDesc desc = TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setFormat(Format::RGBA8_UNORM)
        .setDimension(TextureDimension::Texture2D)
        .setInitialState(ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    const StagingTextureHandle upload = device.createStagingTexture(desc, CpuAccessMode::Write);
    const StagingTextureHandle readback = device.createStagingTexture(desc, CpuAccessMode::Read);
    ASSERT_TRUE(upload);
    ASSERT_TRUE(readback);

    usize firstPitch = 0u;
    usize secondPitch = 0u;
    void* const firstMap = device.mapStagingTexture(upload.get(), TextureSlice{}, CpuAccessMode::Write, &firstPitch);
    void* const secondMap = device.mapStagingTexture(upload.get(), TextureSlice{}, CpuAccessMode::Write, &secondPitch);
    ASSERT_NE(firstMap, nullptr);
    ASSERT_NE(secondMap, nullptr);
    EXPECT_EQ(firstMap, secondMap);
    EXPECT_EQ(firstPitch, secondPitch);
    device.unmapStagingTexture(upload.get());
    device.unmapStagingTexture(upload.get());

    usize remapPitch = 0u;
    void* const remap = device.mapStagingTexture(upload.get(), TextureSlice{}, CpuAccessMode::Write, &remapPitch);
    ASSERT_EQ(remap, firstMap);
    EXPECT_EQ(remapPitch, firstPitch);
    device.unmapStagingTexture(upload.get());

#if !defined(NWB_FINAL)
    const auto expectDiagnosticVoidRejection = [](auto&& operation){
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
        EXPECT_DEATH_IF_SUPPORTED({
            operation();
        }, "");
#else
        operation();
#endif
    };
    static constexpr usize s_UnchangedPitch = 0x5a5a5a5au;
    EXPECT_TRUE(readback->hasMappedMemoryForTesting());
    EXPECT_TRUE(readback->isPersistentlyMappedForTesting());
    readback->rejectNextInvalidateForTesting();
    EXPECT_FALSE(readback->hasMappedMemoryForTesting());
    EXPECT_FALSE(readback->isPersistentlyMappedForTesting());
    usize rejectedPitch = s_UnchangedPitch;
    EXPECT_EQ(
        device.mapStagingTexture(readback.get(), TextureSlice{}, CpuAccessMode::Read, &rejectedPitch),
        nullptr
    );
    EXPECT_EQ(rejectedPitch, s_UnchangedPitch);
    EXPECT_FALSE(readback->hasMappedMemoryForTesting());
    EXPECT_FALSE(readback->isPersistentlyMappedForTesting());

    usize retryPitch = s_UnchangedPitch;
    void* const retryMap = device.mapStagingTexture(
        readback.get(),
        TextureSlice{},
        CpuAccessMode::Read,
        &retryPitch
    );
    ASSERT_NE(retryMap, nullptr);
    EXPECT_NE(retryPitch, s_UnchangedPitch);
    EXPECT_TRUE(readback->hasMappedMemoryForTesting());
    EXPECT_FALSE(readback->isPersistentlyMappedForTesting());
    device.unmapStagingTexture(readback.get());
    EXPECT_FALSE(readback->hasMappedMemoryForTesting());
    EXPECT_FALSE(readback->isPersistentlyMappedForTesting());
    expectDiagnosticVoidRejection([&](){ device.unmapStagingTexture(readback.get()); });
#endif
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

