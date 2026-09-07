// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The global heap is the only resource-bearing descriptor transport. A pipeline-local BindingLayout may carry push
// constants, but creating a local CBV/SRV leaf must fail instead of recreating a local resource descriptor path.
TEST_F(DescriptorBufferRoundTripTest, PipelineLocalResourceLayoutsAreRejected){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/local_resource_layout_rejected_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    BindingLayoutDesc leafDesc(descArena);
    leafDesc.setVisibility(ShaderType::Compute);
    leafDesc.addItem(BindingLayoutItem::ConstantBuffer(0u, 1u));
    leafDesc.addItem(BindingLayoutItem::StructuredBuffer_SRV(1u, 1u));
    leafDesc.addItem(BindingLayoutItem::StructuredBuffer_SRV(2u, 1u));
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        EXPECT_FALSE(device.createBindingLayout(leafDesc));
    }, "");
#else
    auto leafLayout = device.createBindingLayout(leafDesc);
    EXPECT_FALSE(leafLayout);
#endif
}


TEST_F(DescriptorBufferRoundTripTest, PlacedResourceBindingInputsAreRejectedWithoutMutation){
    auto& device = DescriptorBufferRoundTripTest::device();

    const auto expectDiagnosticRejection = [](auto&& operation){
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
        EXPECT_DEATH_IF_SUPPORTED({
            EXPECT_FALSE(operation());
        }, "");
#else
        EXPECT_FALSE(operation());
#endif
    };

    BufferDesc virtualBufferDesc;
    virtualBufferDesc
        .setByteSize(4096u)
        .setIsVirtual(true)
        .setInitialState(ResourceStates::Common)
    ;
    TextureDesc virtualTextureDesc;
    virtualTextureDesc
        .setWidth(16u)
        .setHeight(16u)
        .setFormat(Format::RGBA8_UNORM)
        .setInitialState(ResourceStates::Common)
    ;
    virtualTextureDesc.isVirtual = true;

    BufferHandle localBuffer = device.createBuffer(virtualBufferDesc);
    TextureHandle localTexture = device.createTexture(virtualTextureDesc);
    ASSERT_TRUE(localBuffer);
    ASSERT_TRUE(localTexture);
    EXPECT_EQ(localBuffer->getDeviceGeneration(), device.getDeviceGeneration());
    EXPECT_EQ(localTexture->getDeviceGeneration(), device.getDeviceGeneration());
    const MemoryRequirements bufferRequirements = device.getBufferMemoryRequirements(localBuffer.get());
    const MemoryRequirements textureRequirements = device.getTextureMemoryRequirements(localTexture.get());
    ASSERT_GT(bufferRequirements.size, 0u);
    ASSERT_GT(textureRequirements.size, 0u);

    const HeapDesc heapDesc{
        .capacity = Max(bufferRequirements.size, textureRequirements.size),
        .type = HeapType::DeviceLocal,
        .debugName = Name("tests/placed_resource_heap"),
    };
    HeapHandle heap = device.createHeap(heapDesc);
    ASSERT_TRUE(heap);

    expectDiagnosticRejection([&](){
        return device.getBufferMemoryRequirements(nullptr).size != 0u;
    });
    expectDiagnosticRejection([&](){
        return device.getTextureMemoryRequirements(nullptr).size != 0u;
    });
    expectDiagnosticRejection([&](){
        return device.getAccelStructMemoryRequirements(nullptr).size != 0u;
    });
    expectDiagnosticRejection([&](){ return device.bindBufferMemory(nullptr, heap.get(), 0u); });
    expectDiagnosticRejection([&](){ return device.bindTextureMemory(nullptr, heap.get(), 0u); });
    expectDiagnosticRejection([&](){ return device.bindBufferMemory(localBuffer.get(), nullptr, 0u); });
    expectDiagnosticRejection([&](){ return device.bindTextureMemory(localTexture.get(), nullptr, 0u); });

    BufferHandle ordinaryBuffer = device.createBuffer(BufferDesc().setByteSize(4096u));
    ASSERT_TRUE(ordinaryBuffer);
    expectDiagnosticRejection([&](){
        return device.getBufferMemoryRequirements(ordinaryBuffer.get()).size != 0u;
    });
    expectDiagnosticRejection([&](){ return device.bindBufferMemory(ordinaryBuffer.get(), heap.get(), 0u); });

    TextureHandle ordinaryTexture = device.createTexture(
        TextureDesc()
            .setWidth(16u)
            .setHeight(16u)
            .setFormat(Format::RGBA8_UNORM)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_TRUE(ordinaryTexture);
    expectDiagnosticRejection([&](){
        return device.getTextureMemoryRequirements(ordinaryTexture.get()).size != 0u;
    });
    expectDiagnosticRejection([&](){ return device.bindTextureMemory(ordinaryTexture.get(), heap.get(), 0u); });

    const TextureDesc nativeTextureDesc = ordinaryTexture->getDescription();
    TextureDesc virtualNativeTextureDesc = nativeTextureDesc;
    virtualNativeTextureDesc.isVirtual = true;
    const Object unmanagedNativeImage(static_cast<u64>(0x22d00001u));
    TextureHandle nativeTexture = device.createHandleForNativeTexture(
        GraphicsBackend::ObjectTypes::VK_Image,
        unmanagedNativeImage,
        virtualNativeTextureDesc,
        GraphicsBackend::NativeTextureProvenance{ .usage = VK_IMAGE_USAGE_SAMPLED_BIT }
    );
    ASSERT_TRUE(nativeTexture);
    expectDiagnosticRejection([&](){
        return device.getTextureMemoryRequirements(nativeTexture.get()).size != 0u;
    });
    expectDiagnosticRejection([&](){ return device.bindTextureMemory(nativeTexture.get(), heap.get(), 0u); });

    HeadlessGraphicsScope foreignScope;
    ASSERT_TRUE(foreignScope.initialize());
    auto& foreignDevice = foreignScope.graphics().getDevice();
    BufferHandle foreignBuffer = foreignDevice.createBuffer(virtualBufferDesc);
    TextureHandle foreignTexture = foreignDevice.createTexture(virtualTextureDesc);
    HeapHandle foreignHeap = foreignDevice.createHeap(heapDesc);
    ASSERT_TRUE(foreignBuffer);
    ASSERT_TRUE(foreignTexture);
    ASSERT_TRUE(foreignHeap);
    expectDiagnosticRejection([&](){
        return device.getBufferMemoryRequirements(foreignBuffer.get()).size != 0u;
    });
    expectDiagnosticRejection([&](){
        return device.getTextureMemoryRequirements(foreignTexture.get()).size != 0u;
    });
    expectDiagnosticRejection([&](){ return device.bindBufferMemory(foreignBuffer.get(), heap.get(), 0u); });
    expectDiagnosticRejection([&](){ return device.bindTextureMemory(foreignTexture.get(), heap.get(), 0u); });
    expectDiagnosticRejection([&](){ return device.bindBufferMemory(localBuffer.get(), foreignHeap.get(), 0u); });
    expectDiagnosticRejection([&](){ return device.bindTextureMemory(localTexture.get(), foreignHeap.get(), 0u); });

    const HeapDesc localBufferHeapDesc{
        .capacity = bufferRequirements.size,
        .type = HeapType::DeviceLocal,
        .debugName = Name("tests/placed_local_buffer_retry_heap"),
    };
    const HeapDesc localTextureHeapDesc{
        .capacity = textureRequirements.size,
        .type = HeapType::DeviceLocal,
        .debugName = Name("tests/placed_local_texture_retry_heap"),
    };
    HeapHandle localBufferHeap = device.createHeap(localBufferHeapDesc);
    HeapHandle localTextureHeap = device.createHeap(localTextureHeapDesc);
    ASSERT_TRUE(localBufferHeap);
    ASSERT_TRUE(localTextureHeap);
    const bool localBufferBound = device.bindBufferMemory(localBuffer.get(), localBufferHeap.get(), 0u);
    const bool localTextureBound = device.bindTextureMemory(localTexture.get(), localTextureHeap.get(), 0u);
    if(!localBufferBound || !localTextureBound){
        GTEST_SKIP() << "DeviceLocal heap selection is incompatible with a local virtual resource on this device.";
    }

    const MemoryRequirements foreignBufferRequirements =
        foreignDevice.getBufferMemoryRequirements(foreignBuffer.get());
    const MemoryRequirements foreignTextureRequirements =
        foreignDevice.getTextureMemoryRequirements(foreignTexture.get());
    ASSERT_GT(foreignBufferRequirements.size, 0u);
    ASSERT_GT(foreignTextureRequirements.size, 0u);
    const HeapDesc foreignBufferHeapDesc{
        .capacity = foreignBufferRequirements.size,
        .type = HeapType::DeviceLocal,
        .debugName = Name("tests/placed_foreign_buffer_retry_heap"),
    };
    const HeapDesc foreignTextureHeapDesc{
        .capacity = foreignTextureRequirements.size,
        .type = HeapType::DeviceLocal,
        .debugName = Name("tests/placed_foreign_texture_retry_heap"),
    };
    HeapHandle foreignBufferHeap = foreignDevice.createHeap(foreignBufferHeapDesc);
    HeapHandle foreignTextureHeap = foreignDevice.createHeap(foreignTextureHeapDesc);
    ASSERT_TRUE(foreignBufferHeap);
    ASSERT_TRUE(foreignTextureHeap);
    const bool foreignBufferBound =
        foreignDevice.bindBufferMemory(foreignBuffer.get(), foreignBufferHeap.get(), 0u);
    const bool foreignTextureBound =
        foreignDevice.bindTextureMemory(foreignTexture.get(), foreignTextureHeap.get(), 0u);
    if(!foreignBufferBound || !foreignTextureBound){
        GTEST_SKIP() << "DeviceLocal heap selection is incompatible with a foreign virtual resource on this device.";
    }
}


TEST_F(DescriptorBufferRoundTripTest, PlacedBufferBindingsReserveRetrySerializeAndRetainHeap){
    auto& device = DescriptorBufferRoundTripTest::device();
    const auto expectDiagnosticRejection = [](auto&& operation){
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
        EXPECT_DEATH_IF_SUPPORTED({
            EXPECT_FALSE(operation());
        }, "");
#else
        EXPECT_FALSE(operation());
#endif
    };

    const BufferDesc virtualBufferDesc = BufferDesc()
        .setByteSize(4096u)
        .setIsVirtual(true)
        .setInitialState(ResourceStates::Common)
    ;
    BufferHandle firstBuffer = device.createBuffer(virtualBufferDesc);
    BufferHandle secondBuffer = device.createBuffer(virtualBufferDesc);
    ASSERT_TRUE(firstBuffer);
    ASSERT_TRUE(secondBuffer);
    const MemoryRequirements bufferRequirements = device.getBufferMemoryRequirements(firstBuffer.get());
    ASSERT_GT(bufferRequirements.size, 0u);
    ASSERT_GT(bufferRequirements.alignment, 0u);

    u64 secondBufferOffset = 0u;
    ASSERT_TRUE(AlignUpChecked(bufferRequirements.size, bufferRequirements.alignment, secondBufferOffset));
    ASSERT_LE(bufferRequirements.size, Limit<u64>::s_Max - secondBufferOffset);
    const HeapDesc heapDesc{
        .capacity = secondBufferOffset + bufferRequirements.size,
        .type = HeapType::DeviceLocal,
        .debugName = Name("tests/placed_buffer_heap"),
    };
    HeapHandle heap = device.createHeap(heapDesc);
    ASSERT_TRUE(heap);

    if(!device.bindBufferMemory(firstBuffer.get(), heap.get(), 0u))
        GTEST_SKIP() << "Heap memory type is incompatible with the virtual buffers on this device.";
    expectDiagnosticRejection([&](){
        return device.bindBufferMemory(firstBuffer.get(), heap.get(), bufferRequirements.alignment);
    });
    expectDiagnosticRejection([&](){ return device.bindBufferMemory(secondBuffer.get(), heap.get(), 0u); });
    ASSERT_TRUE(device.bindBufferMemory(secondBuffer.get(), heap.get(), secondBufferOffset));

    firstBuffer.reset();
    BufferHandle reusedBuffer = device.createBuffer(virtualBufferDesc);
    ASSERT_TRUE(reusedBuffer);
    ASSERT_TRUE(device.bindBufferMemory(reusedBuffer.get(), heap.get(), 0u));

#if defined(NWB_FINAL)
    BufferHandle concurrentBuffer = device.createBuffer(virtualBufferDesc);
    HeapHandle concurrentHeap = device.createHeap(heapDesc);
    ASSERT_TRUE(concurrentBuffer);
    ASSERT_TRUE(concurrentHeap);
    Latch bindersReady(2u);
    bool bindingResults[2u] = {};
    Thread binders[2u];
    for(u32 binderIndex = 0u; binderIndex < LengthOf(binders); ++binderIndex){
        binders[binderIndex] = Thread([&, binderIndex](){
            bindersReady.count_down();
            bindersReady.wait();
            bindingResults[binderIndex] = device.bindBufferMemory(concurrentBuffer.get(), concurrentHeap.get(), 0u);
        });
    }
    for(Thread& binder : binders)
        binder.join();
    EXPECT_EQ(static_cast<u32>(bindingResults[0u]) + static_cast<u32>(bindingResults[1u]), 1u);
#endif

    Heap* const retainedHeap = heap.get();
    EXPECT_EQ(retainedHeap->getReferenceCount(), 3u);
    heap.reset();
    EXPECT_EQ(retainedHeap->getReferenceCount(), 2u);
    secondBuffer.reset();
    EXPECT_EQ(retainedHeap->getReferenceCount(), 1u);
    reusedBuffer.reset();
}


TEST_F(DescriptorBufferRoundTripTest, PlacedTextureAndCrossClassBindingsReserveRetryAndRetainHeap){
    auto& device = DescriptorBufferRoundTripTest::device();
    const auto expectDiagnosticRejection = [](auto&& operation){
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
        EXPECT_DEATH_IF_SUPPORTED({
            EXPECT_FALSE(operation());
        }, "");
#else
        EXPECT_FALSE(operation());
#endif
    };

    const BufferDesc virtualBufferDesc = BufferDesc()
        .setByteSize(4096u)
        .setIsVirtual(true)
        .setInitialState(ResourceStates::Common)
    ;
    TextureDesc virtualTextureDesc;
    virtualTextureDesc
        .setWidth(16u)
        .setHeight(16u)
        .setFormat(Format::RGBA8_UNORM)
        .setInitialState(ResourceStates::Common)
    ;
    virtualTextureDesc.isVirtual = true;

    BufferHandle mixedBuffer = device.createBuffer(virtualBufferDesc);
    TextureHandle firstTexture = device.createTexture(virtualTextureDesc);
    TextureHandle secondTexture = device.createTexture(virtualTextureDesc);
    ASSERT_TRUE(mixedBuffer);
    ASSERT_TRUE(firstTexture);
    ASSERT_TRUE(secondTexture);
    const MemoryRequirements bufferRequirements = device.getBufferMemoryRequirements(mixedBuffer.get());
    const MemoryRequirements textureRequirements = device.getTextureMemoryRequirements(firstTexture.get());
    ASSERT_GT(bufferRequirements.size, 0u);
    ASSERT_GT(bufferRequirements.alignment, 0u);
    ASSERT_GT(textureRequirements.size, 0u);
    ASSERT_GT(textureRequirements.alignment, 0u);

    constexpr u64 s_MaxCoreBufferImageGranularity = 128u * 1024u;
    const u64 crossClassAlignment = Max<u64>(
        Max<u64>(bufferRequirements.alignment, textureRequirements.alignment),
        s_MaxCoreBufferImageGranularity
    );
    u64 textureOffset = 0u;
    ASSERT_TRUE(AlignUpChecked(bufferRequirements.size, crossClassAlignment, textureOffset));
    ASSERT_LE(textureRequirements.size, Limit<u64>::s_Max - textureOffset);
    const HeapDesc mixedHeapDesc{
        .capacity = textureOffset + textureRequirements.size,
        .type = HeapType::DeviceLocal,
        .debugName = Name("tests/placed_buffer_texture_heap"),
    };
    HeapHandle mixedHeap = device.createHeap(mixedHeapDesc);
    ASSERT_TRUE(mixedHeap);
    if(!device.bindBufferMemory(mixedBuffer.get(), mixedHeap.get(), 0u))
        GTEST_SKIP() << "Heap memory type is incompatible with the virtual buffer on this device.";
    if(!device.bindTextureMemory(firstTexture.get(), mixedHeap.get(), textureOffset))
        GTEST_SKIP() << "No common buffer/optimal-image heap memory type is available on this device.";

    expectDiagnosticRejection([&](){
        return device.bindTextureMemory(firstTexture.get(), mixedHeap.get(), textureOffset);
    });
    expectDiagnosticRejection([&](){
        return device.bindTextureMemory(secondTexture.get(), mixedHeap.get(), textureOffset);
    });

    firstTexture.reset();
    ASSERT_TRUE(device.bindTextureMemory(secondTexture.get(), mixedHeap.get(), textureOffset));
    Heap* const retainedMixedHeap = mixedHeap.get();
    EXPECT_EQ(retainedMixedHeap->getReferenceCount(), 3u);
    mixedHeap.reset();
    EXPECT_EQ(retainedMixedHeap->getReferenceCount(), 2u);
    const Object retainedView = secondTexture->getNativeView(
        GraphicsBackend::ObjectTypes::VK_ImageView,
        Format::RGBA8_UNORM,
        TextureSubresourceSet(0u, 1u, 0u, 1u),
        TextureDimension::Texture2D,
        false
    );
    EXPECT_NE(retainedView, Object(nullptr));
    mixedBuffer.reset();
    EXPECT_EQ(retainedMixedHeap->getReferenceCount(), 1u);
    secondTexture.reset();
}


TEST_F(DescriptorBufferRoundTripTest, VirtualAccelStructRequirementsCoverBackingBufferBinding){
    auto& device = DescriptorBufferRoundTripTest::device();
    if(!device.queryFeatureSupport(Feature::RayTracingAccelStruct))
        GTEST_SKIP() << "Ray tracing acceleration structures are not enabled on this device.";

    RayTracingAccelStructDesc desc(DescriptorBufferRoundTripTest::arena());
    desc
        .setTopLevelMaxInstances(1u)
        .setIsVirtual(true)
        .setDebugName(Name("tests/virtual_accel_struct_heap_requirements"))
    ;
    RayTracingAccelStructHandle accelStruct = device.createAccelStruct(desc);
    ASSERT_TRUE(accelStruct);
    ASSERT_TRUE(accelStruct->getBackingBuffer());
    EXPECT_EQ(accelStruct->getDeviceGeneration(), accelStruct->getBackingBuffer()->getDeviceGeneration());

    const MemoryRequirements accelRequirements = device.getAccelStructMemoryRequirements(accelStruct.get());
    const MemoryRequirements bufferRequirements =
        device.getBufferMemoryRequirements(accelStruct->getBackingBuffer());
    ASSERT_GT(accelRequirements.size, 0u);
    ASSERT_GT(accelRequirements.alignment, 0u);
    EXPECT_EQ(accelRequirements.size, bufferRequirements.size);
    EXPECT_EQ(
        accelRequirements.alignment,
        Max<u64>(bufferRequirements.alignment, GraphicsBackend::s_AccelerationStructureAlignment)
    );

    const HeapDesc heapDesc{
        .capacity = accelRequirements.size,
        .type = HeapType::DeviceLocal,
        .debugName = Name("tests/virtual_accel_struct_heap"),
    };
    HeapHandle heap = device.createHeap(heapDesc);
    ASSERT_TRUE(heap);
    if(!device.bindAccelStructMemory(accelStruct.get(), heap.get(), 0u))
        GTEST_SKIP() << "Heap memory type is incompatible with acceleration-structure storage on this device.";

    Heap* const retainedHeap = heap.get();
    EXPECT_EQ(retainedHeap->getReferenceCount(), 2u);
    heap.reset();
    EXPECT_EQ(retainedHeap->getReferenceCount(), 1u);
    EXPECT_NE(
        accelStruct->getNativeHandle(GraphicsBackend::ObjectTypes::VK_AccelerationStructureKHR),
        Object(nullptr)
    );
    accelStruct.reset();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

