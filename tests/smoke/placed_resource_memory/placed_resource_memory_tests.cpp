// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Placed host-visible resource-memory coverage.
//
// Stand up a validation-backed headless Vulkan device and exercise virtual Upload and Readback buffer slices,
// persistent heap mapping, concurrent Readback invalidation, binding retry atomicity, and heap retention/reuse.


#include <gtest/gtest.h>

#include <global/global.h>
#include <global/thread.h>
#include <global/unique_ptr.h>
#include <core/graphics/vulkan/backend.h>
#include <core/graphics/vulkan/host_readback_sync.h>
#include <tests/common/capturing_logger.h>
#include <tests/common/headless_graphics_scope.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core;
namespace HostSync = Core::GraphicsBackend::VulkanDetail;


class PlacedResourceMemoryTest : public ::testing::Test{
protected:
    static void SetUpTestSuite(){
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
        // Re-exec death tests instead of forking the live multi-threaded Vulkan fixture.
        GTEST_FLAG_SET(death_test_style, "threadsafe");
#endif

        // Install logging before device creation because backend diagnostics require a live logger.
        s_logger.emplace();
        s_loggerGuard.emplace(*s_logger);

        s_scope = MakeUnique<HeadlessGraphicsScope>();
        if(!s_scope->initialize()){
            GTEST_SKIP() << "Placed resource memory: no usable validation-enabled headless Vulkan device on this host; "
                            "skipping suite.";
            return;
        }
        s_validationBackedDeviceInitialized = true;
    }

    static void TearDownTestSuite(){
        s_scope.reset();
        if(s_validationBackedDeviceInitialized && s_logger.has_value()){
            EXPECT_FALSE(s_logger->sawMessageContaining(NWB_TEXT("Vulkan debug: [severity=error")))
                << "validation-enabled placed resource-memory smoke emitted a Vulkan severity=error message";
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

bool PlacedResourceMemoryTest::s_validationBackedDeviceInitialized = false;
UniquePtr<HeadlessGraphicsScope> PlacedResourceMemoryTest::s_scope;
Optional<CapturingLogger> PlacedResourceMemoryTest::s_logger;
Optional<Common::LoggerRegistrationGuard> PlacedResourceMemoryTest::s_loggerGuard;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(PlacedResourceMemoryTest, PlacedHostVisibleBuffersMapPaddedSlicesRetainAndReuseHeap){
    auto& device = PlacedResourceMemoryTest::device();
    static constexpr u32 s_FirstWords[] = {
        0x10293847u,
        0x55667788u,
        0xa5a5c3c3u,
        0xdeadbeefu,
    };
    static constexpr u32 s_SecondWords[] = {
        0x89abcdefu,
        0x76543210u,
        0xcafef00du,
        0x31415926u,
    };

    const BufferDesc uploadDesc = BufferDesc()
        .setByteSize(sizeof(s_FirstWords))
        .setIsVirtual(true)
        .setInitialState(ResourceStates::Common)
        .setCpuAccess(CpuAccessMode::Write)
    ;
    BufferHandle firstUpload = device.createBuffer(uploadDesc);
    BufferHandle secondUpload = device.createBuffer(uploadDesc);
    ASSERT_TRUE(firstUpload);
    ASSERT_TRUE(secondUpload);
    const MemoryRequirements firstUploadRequirements = device.getBufferMemoryRequirements(firstUpload.get());
    const MemoryRequirements secondUploadRequirements = device.getBufferMemoryRequirements(secondUpload.get());
    ASSERT_GT(firstUploadRequirements.size, 0u);
    ASSERT_GT(secondUploadRequirements.size, 0u);

    u64 secondUploadOffset = 0u;
    ASSERT_TRUE(AlignUpChecked(
        firstUploadRequirements.size,
        secondUploadRequirements.alignment,
        secondUploadOffset
    ));
    ASSERT_LE(secondUploadRequirements.size, Limit<u64>::s_Max - secondUploadOffset);
    const HeapDesc uploadHeapDesc{
        .capacity = secondUploadOffset + secondUploadRequirements.size,
        .type = HeapType::Upload,
        .debugName = Name("tests/placed_upload_heap"),
    };
    HeapHandle uploadHeap = device.createHeap(uploadHeapDesc);
    ASSERT_TRUE(uploadHeap);
    if(!device.bindBufferMemory(firstUpload.get(), uploadHeap.get(), 0u))
        GTEST_SKIP() << "Upload heap memory type is incompatible with virtual buffers on this device.";
    ASSERT_TRUE(device.bindBufferMemory(secondUpload.get(), uploadHeap.get(), secondUploadOffset));

    Heap* const retainedUploadHeap = uploadHeap.get();
    EXPECT_EQ(retainedUploadHeap->getReferenceCount(), 3u);
    u32* const firstUploadWords = static_cast<u32*>(device.mapBuffer(firstUpload.get(), CpuAccessMode::Write));
    u32* const secondUploadWords = static_cast<u32*>(device.mapBuffer(secondUpload.get(), CpuAccessMode::Write));
    ASSERT_NE(firstUploadWords, nullptr);
    ASSERT_NE(secondUploadWords, nullptr);
    EXPECT_EQ(device.mapBuffer(firstUpload.get(), CpuAccessMode::Write), firstUploadWords);
    const usize firstUploadAddress = reinterpret_cast<usize>(firstUploadWords);
    const usize secondUploadAddress = reinterpret_cast<usize>(secondUploadWords);
    ASSERT_GE(secondUploadAddress, firstUploadAddress);
    EXPECT_EQ(secondUploadAddress - firstUploadAddress, static_cast<usize>(secondUploadOffset));
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_FirstWords); ++wordIndex){
        firstUploadWords[wordIndex] = s_FirstWords[wordIndex];
        secondUploadWords[wordIndex] = s_SecondWords[wordIndex];
    }
    device.unmapBuffer(secondUpload.get());
    device.unmapBuffer(firstUpload.get());

    u32* const liveSecondUploadWords = static_cast<u32*>(
        device.mapBuffer(secondUpload.get(), CpuAccessMode::Write)
    );
    ASSERT_NE(liveSecondUploadWords, nullptr);
    ASSERT_NE(device.mapBuffer(firstUpload.get(), CpuAccessMode::Write), nullptr);
    firstUpload.reset();
    EXPECT_EQ(retainedUploadHeap->getReferenceCount(), 2u);
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_SecondWords); ++wordIndex)
        EXPECT_EQ(liveSecondUploadWords[wordIndex], s_SecondWords[wordIndex]);

    BufferHandle replacementUpload = device.createBuffer(uploadDesc);
    ASSERT_TRUE(replacementUpload);
    ASSERT_TRUE(device.bindBufferMemory(replacementUpload.get(), uploadHeap.get(), 0u));
    EXPECT_EQ(retainedUploadHeap->getReferenceCount(), 3u);
    u32* const replacementUploadWords = static_cast<u32*>(
        device.mapBuffer(replacementUpload.get(), CpuAccessMode::Write)
    );
    ASSERT_NE(replacementUploadWords, nullptr);
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_FirstWords); ++wordIndex)
        replacementUploadWords[wordIndex] = s_FirstWords[wordIndex];
    device.unmapBuffer(replacementUpload.get());
    device.unmapBuffer(secondUpload.get());
    uploadHeap.reset();
    EXPECT_EQ(retainedUploadHeap->getReferenceCount(), 2u);

    const BufferDesc readbackDesc = BufferDesc()
        .setByteSize(sizeof(s_FirstWords))
        .setIsVirtual(true)
        .enableAutomaticStateTracking(ResourceStates::Common)
        .setCpuAccess(CpuAccessMode::Read)
    ;
    BufferHandle firstReadback = device.createBuffer(readbackDesc);
    BufferHandle secondReadback = device.createBuffer(readbackDesc);
    ASSERT_TRUE(firstReadback);
    ASSERT_TRUE(secondReadback);
    const MemoryRequirements firstReadbackRequirements = device.getBufferMemoryRequirements(firstReadback.get());
    const MemoryRequirements secondReadbackRequirements = device.getBufferMemoryRequirements(secondReadback.get());
    ASSERT_GT(firstReadbackRequirements.size, 0u);
    ASSERT_GT(secondReadbackRequirements.size, 0u);

    u64 secondReadbackOffset = 0u;
    ASSERT_TRUE(AlignUpChecked(
        firstReadbackRequirements.size,
        secondReadbackRequirements.alignment,
        secondReadbackOffset
    ));
    ASSERT_LE(secondReadbackRequirements.size, Limit<u64>::s_Max - secondReadbackOffset);
    const HeapDesc readbackHeapDesc{
        .capacity = secondReadbackOffset + secondReadbackRequirements.size,
        .type = HeapType::Readback,
        .debugName = Name("tests/placed_readback_heap"),
    };
    HeapHandle readbackHeap = device.createHeap(readbackHeapDesc);
    ASSERT_TRUE(readbackHeap);
    if(!device.bindBufferMemory(firstReadback.get(), readbackHeap.get(), 0u))
        GTEST_SKIP() << "Readback heap memory type is incompatible with virtual buffers on this device.";
    ASSERT_TRUE(device.bindBufferMemory(secondReadback.get(), readbackHeap.get(), secondReadbackOffset));

    Heap* const retainedReadbackHeap = readbackHeap.get();
    EXPECT_EQ(retainedReadbackHeap->getReferenceCount(), 3u);
    readbackHeap.reset();
    EXPECT_EQ(retainedReadbackHeap->getReferenceCount(), 2u);

    const BufferDesc directReadbackDesc = BufferDesc()
        .setByteSize(sizeof(s_FirstWords))
        .setInitialState(ResourceStates::CopyDest)
        .setCpuAccess(CpuAccessMode::Read)
    ;
    BufferHandle directReadback = device.createBuffer(directReadbackDesc);
    ASSERT_TRUE(directReadback);

    auto copyCommandList = device.createCommandList();
    ASSERT_TRUE(copyCommandList);
    copyCommandList->open();
    copyCommandList->copyBuffer(
        firstReadback.get(),
        0u,
        replacementUpload.get(),
        0u,
        sizeof(s_FirstWords)
    );
    copyCommandList->copyBuffer(
        secondReadback.get(),
        0u,
        secondUpload.get(),
        0u,
        sizeof(s_SecondWords)
    );
#if !defined(NWB_FINAL)
    HostSync::ResetHostReadbackBarrierAppendCountForTesting();
#endif
    copyCommandList->close();
#if !defined(NWB_FINAL)
    EXPECT_EQ(HostSync::GetHostReadbackBarrierAppendCountForTesting(), 2u);
#endif

    auto directCopyCommandList = device.createCommandList();
    ASSERT_TRUE(directCopyCommandList);
    directCopyCommandList->open();
    ASSERT_TRUE(directCopyCommandList->recordPreflightedCopyBufferDirectVulkan(
        directReadback.get(),
        0u,
        replacementUpload.get(),
        0u,
        sizeof(s_FirstWords)
    ));
#if !defined(NWB_FINAL)
    HostSync::ResetHostReadbackBarrierAppendCountForTesting();
#endif
    directCopyCommandList->close();
#if !defined(NWB_FINAL)
    EXPECT_EQ(HostSync::GetHostReadbackBarrierAppendCountForTesting(), 1u);
#endif

    auto closeScanCommandList = device.createCommandList();
    ASSERT_TRUE(closeScanCommandList);
    closeScanCommandList->open();
    closeScanCommandList->beginTrackingBufferState(firstReadback.get(), ResourceStates::CopyDest);
    closeScanCommandList->clearState();
#if !defined(NWB_FINAL)
    HostSync::ResetHostReadbackBarrierAppendCountForTesting();
#endif
    closeScanCommandList->close();
    EXPECT_FALSE(closeScanCommandList->commandRecordingFailed());
    EXPECT_TRUE(closeScanCommandList->hasCommandBuffer());
#if !defined(NWB_FINAL)
    EXPECT_EQ(HostSync::GetHostReadbackBarrierAppendCountForTesting(), 1u);
#endif

    CommandList* copyCommandLists[] = { copyCommandList.get(), directCopyCommandList.get() };
    const QueueSubmissionToken copyToken = device.executeCommandLists(
        copyCommandLists,
        LengthOf(copyCommandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(copyToken.valid());
    ASSERT_TRUE(device.waitForIdle());

    Buffer* const readbackBuffers[] = { firstReadback.get(), secondReadback.get(), directReadback.get() };
    const u32* mappedReadbackWords[LengthOf(readbackBuffers)] = {};
    Latch readbackMappersReady(LengthOf(readbackBuffers));
    Thread readbackMappers[LengthOf(readbackBuffers)];
    for(u32 mapperIndex = 0u; mapperIndex < LengthOf(readbackMappers); ++mapperIndex){
        readbackMappers[mapperIndex] = Thread([&, mapperIndex](){
            readbackMappersReady.count_down();
            readbackMappersReady.wait();
            mappedReadbackWords[mapperIndex] = static_cast<const u32*>(
                device.mapBuffer(readbackBuffers[mapperIndex], CpuAccessMode::Read)
            );
        });
    }
    for(Thread& readbackMapper : readbackMappers)
        readbackMapper.join();

    const u32* const firstReadbackWords = mappedReadbackWords[0u];
    const u32* const secondReadbackWords = mappedReadbackWords[1u];
    const u32* const directReadbackWords = mappedReadbackWords[2u];
    ASSERT_NE(firstReadbackWords, nullptr);
    ASSERT_NE(secondReadbackWords, nullptr);
    ASSERT_NE(directReadbackWords, nullptr);
    EXPECT_EQ(device.mapBuffer(firstReadback.get(), CpuAccessMode::Read), firstReadbackWords);
    const usize firstReadbackAddress = reinterpret_cast<usize>(firstReadbackWords);
    const usize secondReadbackAddress = reinterpret_cast<usize>(secondReadbackWords);
    ASSERT_GE(secondReadbackAddress, firstReadbackAddress);
    EXPECT_EQ(secondReadbackAddress - firstReadbackAddress, static_cast<usize>(secondReadbackOffset));
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_FirstWords); ++wordIndex){
        EXPECT_EQ(firstReadbackWords[wordIndex], s_FirstWords[wordIndex]);
        EXPECT_EQ(secondReadbackWords[wordIndex], s_SecondWords[wordIndex]);
        EXPECT_EQ(directReadbackWords[wordIndex], s_FirstWords[wordIndex]);
    }
    device.unmapBuffer(firstReadback.get());
    device.unmapBuffer(secondReadback.get());
    device.unmapBuffer(directReadback.get());
}

TEST_F(PlacedResourceMemoryTest, StagingTextureReadbackDirectionsAreAtomicAndHostSynchronized){
    auto& device = PlacedResourceMemoryTest::device();
    static constexpr u32 s_Width = 4u;
    static constexpr u32 s_Height = 2u;
    static constexpr u32 s_ArraySize = 2u;
    static constexpr u32 s_SlicePixels[] = {
        0x10293847u,
        0xa5c3e17bu,
    };

    const TextureDesc textureDesc = TextureDesc()
        .setWidth(s_Width)
        .setHeight(s_Height)
        .setArraySize(s_ArraySize)
        .setDimension(TextureDimension::Texture2DArray)
        .setFormat(Format::RGBA8_UNORM)
        .setInitialState(ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    TextureHandle texture = device.createTexture(textureDesc);
    StagingTextureHandle upload = device.createStagingTexture(textureDesc, CpuAccessMode::Write);
    StagingTextureHandle readback = device.createStagingTexture(textureDesc, CpuAccessMode::Read);
    if(!texture || !upload || !readback)
        GTEST_SKIP() << "RGBA8 array texture staging is unavailable on this device.";

    for(u32 arraySlice = 0u; arraySlice < s_ArraySize; ++arraySlice){
        TextureSlice slice;
        slice.setArraySlice(arraySlice);
        usize rowPitch = 0u;
        u8* const mappedBytes = static_cast<u8*>(
            device.mapStagingTexture(upload.get(), slice, CpuAccessMode::Write, &rowPitch)
        );
        ASSERT_NE(mappedBytes, nullptr);
        ASSERT_GE(rowPitch, static_cast<usize>(s_Width) * sizeof(u32));
        for(u32 row = 0u; row < s_Height; ++row){
            u32* const mappedRow = reinterpret_cast<u32*>(mappedBytes + static_cast<usize>(row) * rowPitch);
            for(u32 column = 0u; column < s_Width; ++column)
                mappedRow[column] = s_SlicePixels[arraySlice];
        }
        device.unmapStagingTexture(upload.get());
    }

    const TextureSlice firstSlice = TextureSlice().setArraySlice(0u);
    const auto exerciseWrongDirection = [&](const bool writeStagingDestination){
        CommandListHandle invalidCommandList = device.createCommandList();
        ASSERT_TRUE(invalidCommandList);
        invalidCommandList->open();
        if(writeStagingDestination)
            invalidCommandList->copyTexture(upload.get(), firstSlice, texture.get(), firstSlice);
        else
            invalidCommandList->copyTexture(texture.get(), firstSlice, readback.get(), firstSlice);
        EXPECT_TRUE(invalidCommandList->commandRecordingFailed());
        invalidCommandList->close();
        EXPECT_FALSE(invalidCommandList->hasCommandBuffer());
        invalidCommandList->open();
        EXPECT_FALSE(invalidCommandList->commandRecordingFailed());
        invalidCommandList->close();
        EXPECT_TRUE(invalidCommandList->hasCommandBuffer());
    };

#if !defined(NWB_FINAL)
    HostSync::ResetHostReadbackBarrierAppendCountForTesting();
#endif
    exerciseWrongDirection(true);
#if !defined(NWB_FINAL)
    EXPECT_EQ(HostSync::GetHostReadbackBarrierAppendCountForTesting(), 0u);
    HostSync::ResetHostReadbackBarrierAppendCountForTesting();
#endif
    exerciseWrongDirection(false);
#if !defined(NWB_FINAL)
    EXPECT_EQ(HostSync::GetHostReadbackBarrierAppendCountForTesting(), 0u);
#endif

    CommandListHandle uploadCommandList = device.createCommandList();
    ASSERT_TRUE(uploadCommandList);
    uploadCommandList->open();
    for(u32 arraySlice = 0u; arraySlice < s_ArraySize; ++arraySlice){
        TextureSlice slice;
        slice.setArraySlice(arraySlice);
        uploadCommandList->copyTexture(texture.get(), slice, upload.get(), slice);
    }
#if !defined(NWB_FINAL)
    HostSync::ResetHostReadbackBarrierAppendCountForTesting();
#endif
    uploadCommandList->close();
    ASSERT_FALSE(uploadCommandList->commandRecordingFailed());
#if !defined(NWB_FINAL)
    EXPECT_EQ(HostSync::GetHostReadbackBarrierAppendCountForTesting(), 0u);
#endif

    CommandListHandle readbackCommandList = device.createCommandList();
    ASSERT_TRUE(readbackCommandList);
    readbackCommandList->open();
    for(u32 arraySlice = 0u; arraySlice < s_ArraySize; ++arraySlice){
        TextureSlice slice;
        slice.setArraySlice(arraySlice);
        readbackCommandList->copyTexture(readback.get(), slice, texture.get(), slice);
    }
#if !defined(NWB_FINAL)
    HostSync::ResetHostReadbackBarrierAppendCountForTesting();
#endif
    readbackCommandList->close();
    ASSERT_FALSE(readbackCommandList->commandRecordingFailed());
#if !defined(NWB_FINAL)
    EXPECT_EQ(HostSync::GetHostReadbackBarrierAppendCountForTesting(), 1u);
#endif

    CommandList* commandLists[] = { uploadCommandList.get(), readbackCommandList.get() };
    const QueueSubmissionToken token = device.executeCommandLists(
        commandLists,
        LengthOf(commandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(token.valid());
    ASSERT_TRUE(device.waitForIdle());

    for(u32 arraySlice = 0u; arraySlice < s_ArraySize; ++arraySlice){
        TextureSlice slice;
        slice.setArraySlice(arraySlice);
        usize rowPitch = 0u;
        const u8* const mappedBytes = static_cast<const u8*>(
            device.mapStagingTexture(readback.get(), slice, CpuAccessMode::Read, &rowPitch)
        );
        ASSERT_NE(mappedBytes, nullptr);
        ASSERT_GE(rowPitch, static_cast<usize>(s_Width) * sizeof(u32));
        for(u32 row = 0u; row < s_Height; ++row){
            const u32* const mappedRow = reinterpret_cast<const u32*>(
                mappedBytes + static_cast<usize>(row) * rowPitch
            );
            for(u32 column = 0u; column < s_Width; ++column)
                EXPECT_EQ(mappedRow[column], s_SlicePixels[arraySlice]);
        }
        device.unmapStagingTexture(readback.get());
    }
}

TEST_F(PlacedResourceMemoryTest, PlacedHostVisibleBufferRejectionsAreAtomicAndRetryable){
    auto& device = PlacedResourceMemoryTest::device();
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

    BufferDesc invalidAccessDesc = BufferDesc().setByteSize(256u);
    invalidAccessDesc.cpuAccess = static_cast<CpuAccessMode::Enum>(UINT8_MAX);
    expectDiagnosticRejection([&](){ return device.createBuffer(invalidAccessDesc).get() != nullptr; });
    const BufferDesc volatileReadDesc = BufferDesc()
        .setByteSize(256u)
        .setIsVolatile(true)
        .setMaxVersions(2u)
        .setCpuAccess(CpuAccessMode::Read)
    ;
    expectDiagnosticRejection([&](){ return device.createBuffer(volatileReadDesc).get() != nullptr; });
    expectDiagnosticRejection([&](){ return device.mapBuffer(nullptr, CpuAccessMode::Write) != nullptr; });
    expectDiagnosticVoidRejection([&](){ device.unmapBuffer(nullptr); });

    BufferHandle noCpuBuffer = device.createBuffer(BufferDesc().setByteSize(256u));
    ASSERT_TRUE(noCpuBuffer);
    expectDiagnosticRejection([&](){
        return device.mapBuffer(noCpuBuffer.get(), CpuAccessMode::Read) != nullptr;
    });
    expectDiagnosticVoidRejection([&](){ device.unmapBuffer(noCpuBuffer.get()); });

    const BufferDesc managedOwnerDesc = BufferDesc()
        .setByteSize(256u)
        .setCpuAccess(CpuAccessMode::Write)
    ;
    BufferHandle managedOwner = device.createBuffer(managedOwnerDesc);
    ASSERT_TRUE(managedOwner);
    const Object managedNativeBuffer = managedOwner->getNativeHandle(GraphicsBackend::ObjectTypes::VK_Buffer);
    ASSERT_NE(managedNativeBuffer, nullptr);
    const u32 managedOwnerReferences = managedOwner->getReferenceCount();
    BufferHandle duplicateWrapper = device.createHandleForNativeBuffer(
        GraphicsBackend::ObjectTypes::VK_Buffer,
        managedNativeBuffer,
        managedOwnerDesc
    );
    EXPECT_FALSE(duplicateWrapper);
    EXPECT_EQ(managedOwner->getReferenceCount(), managedOwnerReferences);
    EXPECT_TRUE(device.isBufferReadyForGpuUse(managedOwner.get()));
    EXPECT_EQ(managedOwner->getNativeHandle(GraphicsBackend::ObjectTypes::VK_Buffer), managedNativeBuffer);
    u32* const managedOwnerWords = static_cast<u32*>(
        device.mapBuffer(managedOwner.get(), CpuAccessMode::Write)
    );
    ASSERT_NE(managedOwnerWords, nullptr);
    managedOwnerWords[0u] = 0x1234abcdu;
    device.unmapBuffer(managedOwner.get());

    const BufferDesc writeDesc = BufferDesc()
        .setByteSize(256u)
        .setIsVirtual(true)
        .setCpuAccess(CpuAccessMode::Write)
    ;
    BufferHandle writeBuffer = device.createBuffer(writeDesc);
    ASSERT_TRUE(writeBuffer);
    expectDiagnosticRejection([&](){
        return device.mapBuffer(writeBuffer.get(), CpuAccessMode::Write) != nullptr;
    });
    expectDiagnosticVoidRejection([&](){ device.unmapBuffer(writeBuffer.get()); });
    const MemoryRequirements writeRequirements = device.getBufferMemoryRequirements(writeBuffer.get());
    ASSERT_GT(writeRequirements.size, 0u);
    const HeapDesc wrongWriteHeapDesc{
        .capacity = writeRequirements.size,
        .type = HeapType::DeviceLocal,
        .debugName = Name("tests/placed_write_wrong_heap"),
    };
    const HeapDesc writeHeapDesc{
        .capacity = writeRequirements.size,
        .type = HeapType::Upload,
        .debugName = Name("tests/placed_write_retry_heap"),
    };
    HeapHandle wrongWriteHeap = device.createHeap(wrongWriteHeapDesc);
    HeapHandle writeHeap = device.createHeap(writeHeapDesc);
    ASSERT_TRUE(wrongWriteHeap);
    ASSERT_TRUE(writeHeap);
    expectDiagnosticRejection([&](){
        return device.bindBufferMemory(writeBuffer.get(), wrongWriteHeap.get(), 0u);
    });
    if(!device.bindBufferMemory(writeBuffer.get(), writeHeap.get(), 0u))
        GTEST_SKIP() << "Upload heap memory type is incompatible with virtual buffers on this device.";

    expectDiagnosticRejection([&](){
        return device.mapBuffer(writeBuffer.get(), CpuAccessMode::None) != nullptr;
    });
    expectDiagnosticRejection([&](){
        return device.mapBuffer(writeBuffer.get(), CpuAccessMode::Read) != nullptr;
    });
    expectDiagnosticRejection([&](){
        return device.mapBuffer(
            writeBuffer.get(),
            static_cast<CpuAccessMode::Enum>(UINT8_MAX)
        ) != nullptr;
    });
    u32* const writeWords = static_cast<u32*>(device.mapBuffer(writeBuffer.get(), CpuAccessMode::Write));
    ASSERT_NE(writeWords, nullptr);
    writeWords[0u] = 0xabcdef01u;
    device.unmapBuffer(writeBuffer.get());

    const BufferDesc readDesc = BufferDesc()
        .setByteSize(256u)
        .setIsVirtual(true)
        .setCpuAccess(CpuAccessMode::Read)
    ;
    BufferHandle readBuffer = device.createBuffer(readDesc);
    ASSERT_TRUE(readBuffer);
    const MemoryRequirements readRequirements = device.getBufferMemoryRequirements(readBuffer.get());
    ASSERT_GT(readRequirements.size, 0u);
    const HeapDesc wrongReadHeapDesc{
        .capacity = readRequirements.size,
        .type = HeapType::Upload,
        .debugName = Name("tests/placed_read_wrong_heap"),
    };
    const HeapDesc readHeapDesc{
        .capacity = readRequirements.size,
        .type = HeapType::Readback,
        .debugName = Name("tests/placed_read_retry_heap"),
    };
    HeapHandle wrongReadHeap = device.createHeap(wrongReadHeapDesc);
    HeapHandle readHeap = device.createHeap(readHeapDesc);
    ASSERT_TRUE(wrongReadHeap);
    ASSERT_TRUE(readHeap);
    expectDiagnosticRejection([&](){
        return device.bindBufferMemory(readBuffer.get(), wrongReadHeap.get(), 0u);
    });
    if(!device.bindBufferMemory(readBuffer.get(), readHeap.get(), 0u))
        GTEST_SKIP() << "Readback heap memory type is incompatible with virtual buffers on this device.";
    expectDiagnosticRejection([&](){
        return device.mapBuffer(readBuffer.get(), CpuAccessMode::Write) != nullptr;
    });
    ASSERT_NE(device.mapBuffer(readBuffer.get(), CpuAccessMode::Read), nullptr);
    device.unmapBuffer(readBuffer.get());

    HeadlessGraphicsScope foreignScope;
    ASSERT_TRUE(foreignScope.initialize());
    auto& foreignDevice = foreignScope.graphics().getDevice();
    BufferHandle foreignBuffer = foreignDevice.createBuffer(
        BufferDesc().setByteSize(256u).setCpuAccess(CpuAccessMode::Write)
    );
    ASSERT_TRUE(foreignBuffer);
    expectDiagnosticRejection([&](){
        return device.mapBuffer(foreignBuffer.get(), CpuAccessMode::Write) != nullptr;
    });
    expectDiagnosticVoidRejection([&](){ device.unmapBuffer(foreignBuffer.get()); });
    ASSERT_NE(foreignDevice.mapBuffer(foreignBuffer.get(), CpuAccessMode::Write), nullptr);
    foreignDevice.unmapBuffer(foreignBuffer.get());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

