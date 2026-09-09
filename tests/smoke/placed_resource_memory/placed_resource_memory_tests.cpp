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
#include <tests/common/headless_gtest_fixture.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core;


struct PlacedResourceMemoryTestConfig : HeadlessGraphicsTestConfig{
    static constexpr bool s_DeathTestThreadsafe = true;
    static constexpr const char* s_SkipMessage = "Placed resource memory: no usable validation-enabled headless Vulkan device on this host; skipping suite.";
    static constexpr const tchar* s_VulkanErrorMessage = NWB_TEXT("validation-enabled placed resource-memory smoke emitted a Vulkan severity=error message");
};

class PlacedResourceMemoryTest : public HeadlessGraphicsTest<PlacedResourceMemoryTestConfig>{
};


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
    const MemoryRequirements firstUploadRequirements = device.getBufferMemoryRequirements(*firstUpload);
    const MemoryRequirements secondUploadRequirements = device.getBufferMemoryRequirements(*secondUpload);
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
    if(!device.bindBufferMemory(*firstUpload, *uploadHeap, 0u))
        GTEST_SKIP() << "Upload heap memory type is incompatible with virtual buffers on this device.";
    ASSERT_TRUE(device.bindBufferMemory(*secondUpload, *uploadHeap, secondUploadOffset));

    Heap* const retainedUploadHeap = uploadHeap.get();
    EXPECT_EQ(retainedUploadHeap->getReferenceCount(), 3u);
    u32* const firstUploadWords = static_cast<u32*>(device.mapBuffer(*firstUpload, CpuAccessMode::Write));
    u32* const secondUploadWords = static_cast<u32*>(device.mapBuffer(*secondUpload, CpuAccessMode::Write));
    ASSERT_NE(firstUploadWords, nullptr);
    ASSERT_NE(secondUploadWords, nullptr);
    EXPECT_EQ(device.mapBuffer(*firstUpload, CpuAccessMode::Write), firstUploadWords);
    const usize firstUploadAddress = reinterpret_cast<usize>(firstUploadWords);
    const usize secondUploadAddress = reinterpret_cast<usize>(secondUploadWords);
    ASSERT_GE(secondUploadAddress, firstUploadAddress);
    EXPECT_EQ(secondUploadAddress - firstUploadAddress, static_cast<usize>(secondUploadOffset));
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_FirstWords); ++wordIndex){
        firstUploadWords[wordIndex] = s_FirstWords[wordIndex];
        secondUploadWords[wordIndex] = s_SecondWords[wordIndex];
    }
    device.unmapBuffer(*secondUpload);
    device.unmapBuffer(*firstUpload);

    u32* const liveSecondUploadWords = static_cast<u32*>(
        device.mapBuffer(*secondUpload, CpuAccessMode::Write)
    );
    ASSERT_NE(liveSecondUploadWords, nullptr);
    ASSERT_NE(device.mapBuffer(*firstUpload, CpuAccessMode::Write), nullptr);
    firstUpload.reset();
    EXPECT_EQ(retainedUploadHeap->getReferenceCount(), 2u);
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_SecondWords); ++wordIndex)
        EXPECT_EQ(liveSecondUploadWords[wordIndex], s_SecondWords[wordIndex]);

    BufferHandle replacementUpload = device.createBuffer(uploadDesc);
    ASSERT_TRUE(replacementUpload);
    ASSERT_TRUE(device.bindBufferMemory(*replacementUpload, *uploadHeap, 0u));
    EXPECT_EQ(retainedUploadHeap->getReferenceCount(), 3u);
    u32* const replacementUploadWords = static_cast<u32*>(
        device.mapBuffer(*replacementUpload, CpuAccessMode::Write)
    );
    ASSERT_NE(replacementUploadWords, nullptr);
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_FirstWords); ++wordIndex)
        replacementUploadWords[wordIndex] = s_FirstWords[wordIndex];
    device.unmapBuffer(*replacementUpload);
    device.unmapBuffer(*secondUpload);
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
    const MemoryRequirements firstReadbackRequirements = device.getBufferMemoryRequirements(*firstReadback);
    const MemoryRequirements secondReadbackRequirements = device.getBufferMemoryRequirements(*secondReadback);
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
    if(!device.bindBufferMemory(*firstReadback, *readbackHeap, 0u))
        GTEST_SKIP() << "Readback heap memory type is incompatible with virtual buffers on this device.";
    ASSERT_TRUE(device.bindBufferMemory(*secondReadback, *readbackHeap, secondReadbackOffset));

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
        *firstReadback,
        0u,
        *replacementUpload,
        0u,
        sizeof(s_FirstWords)
    );
    copyCommandList->copyBuffer(
        *secondReadback,
        0u,
        *secondUpload,
        0u,
        sizeof(s_SecondWords)
    );
    copyCommandList->close();

    auto directCopyCommandList = device.createCommandList();
    ASSERT_TRUE(directCopyCommandList);
    directCopyCommandList->open();
    ASSERT_TRUE(directCopyCommandList->recordPreflightedCopyBufferDirectVulkan(
        *directReadback,
        0u,
        *replacementUpload,
        0u,
        sizeof(s_FirstWords)
    ));
    directCopyCommandList->close();

    auto closeScanCommandList = device.createCommandList();
    ASSERT_TRUE(closeScanCommandList);
    closeScanCommandList->open();
    closeScanCommandList->beginTrackingBufferState(firstReadback.get(), ResourceStates::CopyDest);
    closeScanCommandList->clearState();
    closeScanCommandList->close();
    EXPECT_FALSE(closeScanCommandList->commandRecordingFailed());
    EXPECT_TRUE(closeScanCommandList->hasCommandBuffer());

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
                device.mapBuffer(*readbackBuffers[mapperIndex], CpuAccessMode::Read)
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
    EXPECT_EQ(device.mapBuffer(*firstReadback, CpuAccessMode::Read), firstReadbackWords);
    const usize firstReadbackAddress = reinterpret_cast<usize>(firstReadbackWords);
    const usize secondReadbackAddress = reinterpret_cast<usize>(secondReadbackWords);
    ASSERT_GE(secondReadbackAddress, firstReadbackAddress);
    EXPECT_EQ(secondReadbackAddress - firstReadbackAddress, static_cast<usize>(secondReadbackOffset));
    for(usize wordIndex = 0u; wordIndex < LengthOf(s_FirstWords); ++wordIndex){
        EXPECT_EQ(firstReadbackWords[wordIndex], s_FirstWords[wordIndex]);
        EXPECT_EQ(secondReadbackWords[wordIndex], s_SecondWords[wordIndex]);
        EXPECT_EQ(directReadbackWords[wordIndex], s_FirstWords[wordIndex]);
    }
    device.unmapBuffer(*firstReadback);
    device.unmapBuffer(*secondReadback);
    device.unmapBuffer(*directReadback);
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
            device.mapStagingTexture(*upload, slice, CpuAccessMode::Write, &rowPitch)
        );
        ASSERT_NE(mappedBytes, nullptr);
        ASSERT_GE(rowPitch, static_cast<usize>(s_Width) * sizeof(u32));
        for(u32 row = 0u; row < s_Height; ++row){
            u32* const mappedRow = reinterpret_cast<u32*>(mappedBytes + static_cast<usize>(row) * rowPitch);
            for(u32 column = 0u; column < s_Width; ++column)
                mappedRow[column] = s_SlicePixels[arraySlice];
        }
        device.unmapStagingTexture(*upload);
    }

    const TextureSlice firstSlice = TextureSlice().setArraySlice(0u);
    const auto exerciseWrongDirection = [&](const bool writeStagingDestination){
        CommandListHandle invalidCommandList = device.createCommandList();
        ASSERT_TRUE(invalidCommandList);
        invalidCommandList->open();
        if(writeStagingDestination)
            invalidCommandList->copyTexture(*upload, firstSlice, *texture, firstSlice);
        else
            invalidCommandList->copyTexture(*texture, firstSlice, *readback, firstSlice);
        EXPECT_TRUE(invalidCommandList->commandRecordingFailed());
        invalidCommandList->close();
        EXPECT_FALSE(invalidCommandList->hasCommandBuffer());
        invalidCommandList->open();
        EXPECT_FALSE(invalidCommandList->commandRecordingFailed());
        invalidCommandList->close();
        EXPECT_TRUE(invalidCommandList->hasCommandBuffer());
    };

    exerciseWrongDirection(true);
    exerciseWrongDirection(false);

    CommandListHandle uploadCommandList = device.createCommandList();
    ASSERT_TRUE(uploadCommandList);
    uploadCommandList->open();
    for(u32 arraySlice = 0u; arraySlice < s_ArraySize; ++arraySlice){
        TextureSlice slice;
        slice.setArraySlice(arraySlice);
        uploadCommandList->copyTexture(*texture, slice, *upload, slice);
    }
    uploadCommandList->close();
    ASSERT_FALSE(uploadCommandList->commandRecordingFailed());

    CommandListHandle readbackCommandList = device.createCommandList();
    ASSERT_TRUE(readbackCommandList);
    readbackCommandList->open();
    for(u32 arraySlice = 0u; arraySlice < s_ArraySize; ++arraySlice){
        TextureSlice slice;
        slice.setArraySlice(arraySlice);
        readbackCommandList->copyTexture(*readback, slice, *texture, slice);
    }
    readbackCommandList->close();
    ASSERT_FALSE(readbackCommandList->commandRecordingFailed());

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
            device.mapStagingTexture(*readback, slice, CpuAccessMode::Read, &rowPitch)
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
        device.unmapStagingTexture(*readback);
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
    BufferHandle noCpuBuffer = device.createBuffer(BufferDesc().setByteSize(256u));
    ASSERT_TRUE(noCpuBuffer);
    expectDiagnosticRejection([&](){
        return device.mapBuffer(*noCpuBuffer, CpuAccessMode::Read) != nullptr;
    });
    expectDiagnosticVoidRejection([&](){ device.unmapBuffer(*noCpuBuffer); });

    const BufferDesc managedOwnerDesc = BufferDesc()
        .setByteSize(256u)
        .setCpuAccess(CpuAccessMode::Write)
    ;
    BufferHandle managedOwner = device.createBuffer(managedOwnerDesc);
    ASSERT_TRUE(managedOwner);
    const Object managedNativeBuffer = managedOwner->getNativeHandle(GraphicsBackend::ObjectTypes::VK_Buffer);
    ASSERT_NE(managedNativeBuffer, nullptr);
    const u32 managedOwnerReferences = managedOwner->getReferenceCount();
    VkBufferUsageFlags managedNativeUsage =
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT
        | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT
        | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT
    ;
    if(device.isBufferReadyForGpuUse(managedOwner.get(), VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT))
        managedNativeUsage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    BufferHandle duplicateWrapper = device.createHandleForNativeBuffer(
        GraphicsBackend::ObjectTypes::VK_Buffer,
        managedNativeBuffer,
        managedOwnerDesc,
        GraphicsBackend::NativeBufferProvenance{ .usage = managedNativeUsage }
    );
    EXPECT_FALSE(duplicateWrapper);
    EXPECT_EQ(managedOwner->getReferenceCount(), managedOwnerReferences);
    EXPECT_TRUE(device.isBufferReadyForGpuUse(managedOwner.get()));
    EXPECT_EQ(managedOwner->getNativeHandle(GraphicsBackend::ObjectTypes::VK_Buffer), managedNativeBuffer);
    u32* const managedOwnerWords = static_cast<u32*>(
        device.mapBuffer(*managedOwner, CpuAccessMode::Write)
    );
    ASSERT_NE(managedOwnerWords, nullptr);
    managedOwnerWords[0u] = 0x1234abcdu;
    device.unmapBuffer(*managedOwner);

    const BufferDesc writeDesc = BufferDesc()
        .setByteSize(256u)
        .setIsVirtual(true)
        .setCpuAccess(CpuAccessMode::Write)
    ;
    BufferHandle writeBuffer = device.createBuffer(writeDesc);
    ASSERT_TRUE(writeBuffer);
    expectDiagnosticRejection([&](){
        return device.mapBuffer(*writeBuffer, CpuAccessMode::Write) != nullptr;
    });
    expectDiagnosticVoidRejection([&](){ device.unmapBuffer(*writeBuffer); });
    const MemoryRequirements writeRequirements = device.getBufferMemoryRequirements(*writeBuffer);
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
        return device.bindBufferMemory(*writeBuffer, *wrongWriteHeap, 0u);
    });
    if(!device.bindBufferMemory(*writeBuffer, *writeHeap, 0u))
        GTEST_SKIP() << "Upload heap memory type is incompatible with virtual buffers on this device.";

    expectDiagnosticRejection([&](){
        return device.mapBuffer(*writeBuffer, CpuAccessMode::None) != nullptr;
    });
    expectDiagnosticRejection([&](){
        return device.mapBuffer(*writeBuffer, CpuAccessMode::Read) != nullptr;
    });
    expectDiagnosticRejection([&](){
        return device.mapBuffer(
            *writeBuffer,
            static_cast<CpuAccessMode::Enum>(UINT8_MAX)
        ) != nullptr;
    });
    u32* const writeWords = static_cast<u32*>(device.mapBuffer(*writeBuffer, CpuAccessMode::Write));
    ASSERT_NE(writeWords, nullptr);
    writeWords[0u] = 0xabcdef01u;
    device.unmapBuffer(*writeBuffer);

    const BufferDesc readDesc = BufferDesc()
        .setByteSize(256u)
        .setIsVirtual(true)
        .setCpuAccess(CpuAccessMode::Read)
    ;
    BufferHandle readBuffer = device.createBuffer(readDesc);
    ASSERT_TRUE(readBuffer);
    const MemoryRequirements readRequirements = device.getBufferMemoryRequirements(*readBuffer);
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
        return device.bindBufferMemory(*readBuffer, *wrongReadHeap, 0u);
    });
    if(!device.bindBufferMemory(*readBuffer, *readHeap, 0u))
        GTEST_SKIP() << "Readback heap memory type is incompatible with virtual buffers on this device.";
    expectDiagnosticRejection([&](){
        return device.mapBuffer(*readBuffer, CpuAccessMode::Write) != nullptr;
    });
    ASSERT_NE(device.mapBuffer(*readBuffer, CpuAccessMode::Read), nullptr);
    device.unmapBuffer(*readBuffer);

    HeadlessGraphicsScope foreignScope;
    ASSERT_TRUE(foreignScope.initialize());
    auto& foreignDevice = foreignScope.graphics().getDevice();
    BufferHandle foreignBuffer = foreignDevice.createBuffer(
        BufferDesc().setByteSize(256u).setCpuAccess(CpuAccessMode::Write)
    );
    ASSERT_TRUE(foreignBuffer);
    expectDiagnosticRejection([&](){
        return device.mapBuffer(*foreignBuffer, CpuAccessMode::Write) != nullptr;
    });
    expectDiagnosticVoidRejection([&](){ device.unmapBuffer(*foreignBuffer); });
    ASSERT_NE(foreignDevice.mapBuffer(*foreignBuffer, CpuAccessMode::Write), nullptr);
    foreignDevice.unmapBuffer(*foreignBuffer);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

