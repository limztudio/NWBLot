// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Operation>
void ExpectDescriptorHeapWriteRejection(Operation&& operation){
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        EXPECT_FALSE(operation());
    }, "");
#else
    EXPECT_FALSE(operation());
#endif
}


TEST_F(DescriptorBufferAllocationTest, DescriptorHeapRejectsRetaggedAndIncompatibleWritesBeforeMutation){
    auto& device = DescriptorBufferRoundTripTest::device();

    GraphicsBackend::GpuDescriptorHeap heap(device);
    GpuDescriptorHeapDesc heapDesc;
    heapDesc
        .setResourceCapacity(8u)
        .setSamplerCapacity(2u)
        .setBindlessHeapAbi(Impl::AssetsGraphicsBindless::MakeGpuDescriptorHeapAbi())
    ;
    ASSERT_TRUE(heap.initialize(heapDesc));

    BufferHandle buffer = device.createBuffer(
        BufferDesc()
            .setByteSize(4096u)
            .setStructStride(16u)
            .setFormat(Format::R32_UINT)
            .setCanHaveTypedViews(true)
            .setCanHaveRawViews(true)
            .setCanHaveUAVs(true)
            .setIsConstantBuffer(true)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    TextureHandle texture = device.createTexture(
        TextureDesc()
            .setWidth(16u)
            .setHeight(16u)
            .setFormat(Format::RGBA16_FLOAT)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    ASSERT_TRUE(buffer);
    ASSERT_TRUE(texture);

    const usize bufferReferences = buffer->getReferenceCount();
    const usize textureReferences = texture->getReferenceCount();
    const GpuDescriptorHandle storageHandle = heap.allocate(GpuDescriptorClass::StorageBuffer);
    ASSERT_TRUE(storageHandle.valid());

    ExpectDescriptorHeapWriteRejection([&](){
        return heap.write(storageHandle, DescriptorWriteItem::Texture_SRV(0u, texture.get()));
    });
    EXPECT_EQ(buffer->getReferenceCount(), bufferReferences);
    EXPECT_EQ(texture->getReferenceCount(), textureReferences);

    const GpuDescriptorHandle retagged = GpuDescriptorHandle::make(
        GpuDescriptorClass::SampledImage,
        storageHandle.slot()
    );
    ExpectDescriptorHeapWriteRejection([&](){
        return heap.write(retagged, DescriptorWriteItem::Texture_SRV(0u, texture.get()));
    });
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        heap.free(retagged);
    }, "");
#else
    heap.free(retagged);
#endif
    EXPECT_EQ(heap.lifecycleStatistics().resourceLiveSlotCount, 1u);
    EXPECT_EQ(buffer->getReferenceCount(), bufferReferences);
    EXPECT_EQ(texture->getReferenceCount(), textureReferences);

    ASSERT_TRUE(heap.write(storageHandle, DescriptorWriteItem::StructuredBuffer_SRV(0u, buffer.get())));
    EXPECT_EQ(buffer->getReferenceCount(), bufferReferences + 1u);
    heap.free(storageHandle);
    EXPECT_EQ(buffer->getReferenceCount(), bufferReferences);

    const DescriptorWriteItem storageAliases[] = {
        DescriptorWriteItem::StructuredBuffer_SRV(0u, buffer.get()),
        DescriptorWriteItem::StructuredBuffer_UAV(0u, buffer.get()),
        DescriptorWriteItem::RawBuffer_SRV(0u, buffer.get()),
        DescriptorWriteItem::RawBuffer_UAV(0u, buffer.get()),
    };
    for(const DescriptorWriteItem& alias : storageAliases){
        const GpuDescriptorHandle aliasHandle = heap.allocate(GpuDescriptorClass::StorageBuffer);
        ASSERT_TRUE(aliasHandle.valid());
        EXPECT_TRUE(heap.write(aliasHandle, alias));
        EXPECT_EQ(buffer->getReferenceCount(), bufferReferences + 1u);
        heap.free(aliasHandle);
        EXPECT_EQ(buffer->getReferenceCount(), bufferReferences);
    }

    const GpuDescriptorHandle uniformHandle = heap.allocate(GpuDescriptorClass::UniformBuffer);
    ASSERT_TRUE(uniformHandle.valid());
    DescriptorWriteItem volatileItem = DescriptorWriteItem::ConstantBuffer(0u, buffer.get());
    volatileItem.type = ResourceType::VolatileConstantBuffer;
    ExpectDescriptorHeapWriteRejection([&](){ return heap.write(uniformHandle, volatileItem); });
    EXPECT_EQ(buffer->getReferenceCount(), bufferReferences);
    ASSERT_TRUE(heap.write(uniformHandle, DescriptorWriteItem::ConstantBuffer(0u, buffer.get())));
    heap.free(uniformHandle);
    EXPECT_EQ(buffer->getReferenceCount(), bufferReferences);

    const GpuDescriptorHandle sampledBufferHandle = heap.allocate(GpuDescriptorClass::SampledBuffer);
    ASSERT_TRUE(sampledBufferHandle.valid());
    ExpectDescriptorHeapWriteRejection([&](){
        return heap.write(
            sampledBufferHandle,
            DescriptorWriteItem::TypedBuffer_SRV(0u, buffer.get(), Format::R32_SINT)
        );
    });
    EXPECT_EQ(buffer->getReferenceCount(), bufferReferences);
    ASSERT_TRUE(heap.write(
        sampledBufferHandle,
        DescriptorWriteItem::TypedBuffer_SRV(0u, buffer.get(), Format::R32_UINT)
    ));
    heap.free(sampledBufferHandle);
    EXPECT_EQ(buffer->getReferenceCount(), bufferReferences);
}


TEST_F(DescriptorBufferAllocationTest, DescriptorHeapValidatesSampledAndStorageImageAbiBeforeRetention){
    auto& device = DescriptorBufferRoundTripTest::device();

    GraphicsBackend::GpuDescriptorHeap heap(device);
    GpuDescriptorHeapDesc heapDesc;
    heapDesc
        .setResourceCapacity(8u)
        .setSamplerCapacity(1u)
        .setBindlessHeapAbi(Impl::AssetsGraphicsBindless::MakeGpuDescriptorHeapAbi())
    ;
    ASSERT_TRUE(heap.initialize(heapDesc));

    const auto createTexture = [&](const TextureDimension::Enum dimension, const Format::Enum format){
        TextureDesc desc;
        desc
            .setWidth(16u)
            .setHeight(16u)
            .setDimension(dimension)
            .setFormat(format)
            .setInUAV(true)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
        ;
        if(dimension == TextureDimension::Texture2DArray)
            desc.setArraySize(3u);
        else if(dimension == TextureDimension::Texture3D)
            desc.setDepth(4u);
        else if(dimension == TextureDimension::TextureCube)
            desc.setArraySize(6u);
        return device.createTexture(desc);
    };

    TextureHandle sampled2D = createTexture(TextureDimension::Texture2D, Format::RGBA16_FLOAT);
    TextureHandle sampledArray = createTexture(TextureDimension::Texture2DArray, Format::RGBA16_FLOAT);
    TextureHandle sampledArrayUint = createTexture(TextureDimension::Texture2DArray, Format::R32_UINT);
    TextureHandle sampled3D = createTexture(TextureDimension::Texture3D, Format::RGBA16_FLOAT);
    TextureHandle sampledCube = createTexture(TextureDimension::TextureCube, Format::RGBA16_FLOAT);
    ASSERT_TRUE(sampled2D);
    ASSERT_TRUE(sampledArray);
    ASSERT_TRUE(sampledArrayUint);
    ASSERT_TRUE(sampled3D);
    ASSERT_TRUE(sampledCube);

    TextureDesc multisampledDesc;
    multisampledDesc
        .setWidth(16u)
        .setHeight(16u)
        .setSampleCount(4u)
        .setDimension(TextureDimension::Texture2D)
        .setFormat(Format::RGBA8_UNORM)
        .setInRenderTarget(true)
        .setInitialState(ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    TextureHandle multisampled = device.createTexture(multisampledDesc);
    ASSERT_TRUE(multisampled);
    const usize multisampledReferences = multisampled->getReferenceCount();
    const GpuDescriptorHandle multisampledHandle = heap.allocate(GpuDescriptorClass::SampledImage);
    ASSERT_TRUE(multisampledHandle.valid());
    ExpectDescriptorHeapWriteRejection([&](){
        return heap.write(
            multisampledHandle,
            DescriptorWriteItem::Texture_SRV(
                0u,
                multisampled.get(),
                Format::RGBA8_UNORM,
                s_AllSubresources,
                TextureDimension::Texture2D
            )
        );
    });
    EXPECT_EQ(multisampled->getReferenceCount(), multisampledReferences);
    heap.free(multisampledHandle);

    struct SampledCase{
        GpuDescriptorClass::Enum descriptorClass;
        TextureHandle* texture;
        TextureDimension::Enum dimension;
        Format::Enum format;
    };
    SampledCase sampledCases[] = {
        { GpuDescriptorClass::SampledImage, &sampled2D, TextureDimension::Texture2D, Format::RGBA16_FLOAT },
        {
            GpuDescriptorClass::SampledImage2DArray,
            &sampledArray,
            TextureDimension::Texture2DArray,
            Format::RGBA16_FLOAT,
        },
        {
            GpuDescriptorClass::SampledImage2DArrayUint,
            &sampledArrayUint,
            TextureDimension::Texture2DArray,
            Format::R32_UINT,
        },
        { GpuDescriptorClass::SampledImage3D, &sampled3D, TextureDimension::Texture3D, Format::RGBA16_FLOAT },
        { GpuDescriptorClass::SampledImageCube, &sampledCube, TextureDimension::TextureCube, Format::RGBA16_FLOAT },
    };

    for(const SampledCase& sampledCase : sampledCases){
        Texture* const texture = sampledCase.texture->get();
        const usize references = texture->getReferenceCount();
        const GpuDescriptorHandle handle = heap.allocate(sampledCase.descriptorClass);
        ASSERT_TRUE(handle.valid());

        const TextureDimension::Enum wrongDimension = sampledCase.dimension == TextureDimension::Texture2D
            ? TextureDimension::Texture2DArray
            : TextureDimension::Texture2D
        ;
        ExpectDescriptorHeapWriteRejection([&](){
            return heap.write(
                handle,
                DescriptorWriteItem::Texture_SRV(
                    0u,
                    texture,
                    sampledCase.format,
                    s_AllSubresources,
                    wrongDimension
                )
            );
        });
        EXPECT_EQ(texture->getReferenceCount(), references);

        const Format::Enum wrongFormat = sampledCase.descriptorClass
            == GpuDescriptorClass::SampledImage2DArrayUint
            ? Format::RGBA16_FLOAT
            : Format::R32_UINT
        ;
        ExpectDescriptorHeapWriteRejection([&](){
            return heap.write(
                handle,
                DescriptorWriteItem::Texture_SRV(
                    0u,
                    texture,
                    wrongFormat,
                    s_AllSubresources,
                    sampledCase.dimension
                )
            );
        });
        EXPECT_EQ(texture->getReferenceCount(), references);

        if(sampledCase.descriptorClass == GpuDescriptorClass::SampledImage2DArrayUint){
            ExpectDescriptorHeapWriteRejection([&](){
                return heap.write(
                    handle,
                    DescriptorWriteItem::Texture_SRV(
                        0u,
                        texture,
                        Format::R32_SINT,
                        s_AllSubresources,
                        sampledCase.dimension
                    )
                );
            });
            EXPECT_EQ(texture->getReferenceCount(), references);
        }

        ASSERT_TRUE(heap.write(handle, DescriptorWriteItem::Texture_SRV(0u, texture)));
        EXPECT_EQ(texture->getReferenceCount(), references + 1u);
        ASSERT_TRUE(heap.write(handle, DescriptorWriteItem::Texture_SRV(0u, texture)));
        EXPECT_EQ(texture->getReferenceCount(), references + 1u);
        heap.free(handle);
        EXPECT_EQ(texture->getReferenceCount(), references);
    }

    const usize sampled3DReferences = sampled3D->getReferenceCount();
    const GpuDescriptorHandle storage3D = heap.allocate(GpuDescriptorClass::StorageImage);
    ASSERT_TRUE(storage3D.valid());
    ExpectDescriptorHeapWriteRejection([&](){
        return heap.write(
            storage3D,
            DescriptorWriteItem::Texture_UAV(
                0u,
                sampled3D.get(),
                Format::RGBA16_FLOAT,
                s_AllSubresources,
                TextureDimension::TextureCube
            )
        );
    });
    ExpectDescriptorHeapWriteRejection([&](){
        return heap.write(
            storage3D,
            DescriptorWriteItem::Texture_UAV(
                0u,
                sampled3D.get(),
                Format::R32_UINT,
                s_AllSubresources,
                TextureDimension::Texture3D
            )
        );
    });
    EXPECT_EQ(sampled3D->getReferenceCount(), sampled3DReferences);
    ASSERT_TRUE(heap.write(storage3D, DescriptorWriteItem::Texture_UAV(0u, sampled3D.get())));
    heap.free(storage3D);
    EXPECT_EQ(sampled3D->getReferenceCount(), sampled3DReferences);

    TextureHandle storageUint = createTexture(TextureDimension::Texture2D, Format::R32_UINT);
    ASSERT_TRUE(storageUint);
    const usize storageUintReferences = storageUint->getReferenceCount();
    const GpuDescriptorHandle storageUintHandle = heap.allocate(GpuDescriptorClass::StorageImage);
    ASSERT_TRUE(storageUintHandle.valid());
    for(const Format::Enum rejectedFormat : { Format::R32_SINT, Format::D32, Format::BC1_UNORM }){
        ExpectDescriptorHeapWriteRejection([&](){
            return heap.write(
                storageUintHandle,
                DescriptorWriteItem::Texture_UAV(
                    0u,
                    storageUint.get(),
                    rejectedFormat,
                    s_AllSubresources,
                    TextureDimension::Texture2D
                )
            );
        });
        EXPECT_EQ(storageUint->getReferenceCount(), storageUintReferences);
    }
    ASSERT_TRUE(heap.write(storageUintHandle, DescriptorWriteItem::Texture_UAV(0u, storageUint.get())));
    heap.free(storageUintHandle);
    EXPECT_EQ(storageUint->getReferenceCount(), storageUintReferences);

    const GpuDescriptorHandle storageArrayHandle = heap.allocate(GpuDescriptorClass::StorageImage);
    ASSERT_TRUE(storageArrayHandle.valid());
    ASSERT_TRUE(heap.write(storageArrayHandle, DescriptorWriteItem::Texture_UAV(0u, sampledArray.get())));
    heap.free(storageArrayHandle);
}


TEST_F(DescriptorBufferAllocationTest, DescriptorHeapTypedRetentionOutlivesExternalHandles){
    auto& device = DescriptorBufferRoundTripTest::device();

    GraphicsBackend::GpuDescriptorHeap heap(device);
    GpuDescriptorHeapDesc heapDesc;
    heapDesc
        .setResourceCapacity(4u)
        .setSamplerCapacity(2u)
        .setBindlessHeapAbi(Impl::AssetsGraphicsBindless::MakeGpuDescriptorHeapAbi())
    ;
    ASSERT_TRUE(heap.initialize(heapDesc));

    BufferHandle buffer = device.createBuffer(
        BufferDesc()
            .setByteSize(4096u)
            .setStructStride(16u)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    TextureHandle texture = device.createTexture(
        TextureDesc()
            .setWidth(16u)
            .setHeight(16u)
            .setFormat(Format::RGBA16_FLOAT)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    SamplerDesc samplerDesc;
    SamplerHandle sampler = device.createSampler(samplerDesc);
    ASSERT_TRUE(buffer);
    ASSERT_TRUE(texture);
    ASSERT_TRUE(sampler);

    BufferHandle observedBuffer = buffer;
    TextureHandle observedTexture = texture;
    SamplerHandle observedSampler = sampler;

    const GpuDescriptorHandle bufferHandle = heap.allocate(GpuDescriptorClass::StorageBuffer);
    ASSERT_TRUE(bufferHandle.valid());
    ASSERT_TRUE(heap.write(bufferHandle, DescriptorWriteItem::StructuredBuffer_SRV(0u, buffer.get())));
    EXPECT_EQ(observedBuffer->getReferenceCount(), 3u);
    buffer.reset();
    EXPECT_EQ(observedBuffer->getReferenceCount(), 2u);
    heap.free(bufferHandle);
    EXPECT_EQ(observedBuffer->getReferenceCount(), 1u);

    const GpuDescriptorHandle textureHandle = heap.allocate(GpuDescriptorClass::SampledImage);
    ASSERT_TRUE(textureHandle.valid());
    ASSERT_TRUE(heap.write(textureHandle, DescriptorWriteItem::Texture_SRV(0u, texture.get())));
    EXPECT_EQ(observedTexture->getReferenceCount(), 3u);
    texture.reset();
    EXPECT_EQ(observedTexture->getReferenceCount(), 2u);
    heap.free(textureHandle);
    EXPECT_EQ(observedTexture->getReferenceCount(), 1u);

    const GpuDescriptorHandle samplerHandle = heap.allocate(GpuDescriptorClass::Sampler);
    ASSERT_TRUE(samplerHandle.valid());
    ASSERT_TRUE(heap.write(samplerHandle, DescriptorWriteItem::Sampler(0u, sampler.get())));
    EXPECT_EQ(observedSampler->getReferenceCount(), 3u);
    sampler.reset();
    EXPECT_EQ(observedSampler->getReferenceCount(), 2u);
    heap.free(samplerHandle);
    EXPECT_EQ(observedSampler->getReferenceCount(), 1u);
}


TEST_F(DescriptorBufferAllocationTest, DescriptorHeapRejectsForeignResourcesAndDoesNotExposeFailedTlasBlocks){
    auto& device = DescriptorBufferRoundTripTest::device();
    HeadlessGraphicsScope foreignScope;
    ASSERT_TRUE(foreignScope.initialize());
    auto& foreignDevice = foreignScope.graphics().getDevice();

    GraphicsBackend::GpuDescriptorHeap heap(device);
    GpuDescriptorHeapDesc heapDesc;
    heapDesc
        .setResourceCapacity(8u)
        .setSamplerCapacity(2u)
        .setBindlessHeapAbi(Impl::AssetsGraphicsBindless::MakeGpuDescriptorHeapAbi())
    ;
    ASSERT_TRUE(heap.initialize(heapDesc));

    const BufferDesc bufferDesc = BufferDesc()
        .setByteSize(4096u)
        .setStructStride(16u)
        .setInitialState(ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    BufferHandle localBuffer = device.createBuffer(bufferDesc);
    BufferHandle foreignBuffer = foreignDevice.createBuffer(bufferDesc);
    const TextureDesc textureDesc = TextureDesc()
        .setWidth(16u)
        .setHeight(16u)
        .setFormat(Format::RGBA16_FLOAT)
        .setInitialState(ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    TextureHandle localTexture = device.createTexture(textureDesc);
    TextureHandle foreignTexture = foreignDevice.createTexture(textureDesc);
    SamplerDesc samplerDesc;
    SamplerHandle localSampler = device.createSampler(samplerDesc);
    SamplerHandle foreignSampler = foreignDevice.createSampler(samplerDesc);
    ASSERT_TRUE(localBuffer);
    ASSERT_TRUE(foreignBuffer);
    ASSERT_TRUE(localTexture);
    ASSERT_TRUE(foreignTexture);
    ASSERT_TRUE(localSampler);
    ASSERT_TRUE(foreignSampler);

    const usize foreignBufferReferences = foreignBuffer->getReferenceCount();
    const GpuDescriptorHandle bufferHandle = heap.allocate(GpuDescriptorClass::StorageBuffer);
    ASSERT_TRUE(bufferHandle.valid());
    ExpectDescriptorHeapWriteRejection([&](){
        return heap.write(bufferHandle, DescriptorWriteItem::StructuredBuffer_SRV(0u, foreignBuffer.get()));
    });
    EXPECT_EQ(foreignBuffer->getReferenceCount(), foreignBufferReferences);
    ASSERT_TRUE(heap.write(bufferHandle, DescriptorWriteItem::StructuredBuffer_SRV(0u, localBuffer.get())));
    heap.free(bufferHandle);

    const usize foreignTextureReferences = foreignTexture->getReferenceCount();
    const GpuDescriptorHandle textureHandle = heap.allocate(GpuDescriptorClass::SampledImage);
    ASSERT_TRUE(textureHandle.valid());
    ExpectDescriptorHeapWriteRejection([&](){
        return heap.write(textureHandle, DescriptorWriteItem::Texture_SRV(0u, foreignTexture.get()));
    });
    EXPECT_EQ(foreignTexture->getReferenceCount(), foreignTextureReferences);
    ASSERT_TRUE(heap.write(textureHandle, DescriptorWriteItem::Texture_SRV(0u, localTexture.get())));
    heap.free(textureHandle);

    const usize foreignSamplerReferences = foreignSampler->getReferenceCount();
    const GpuDescriptorHandle samplerHandle = heap.allocate(GpuDescriptorClass::Sampler);
    ASSERT_TRUE(samplerHandle.valid());
    ExpectDescriptorHeapWriteRejection([&](){
        return heap.write(samplerHandle, DescriptorWriteItem::Sampler(0u, foreignSampler.get()));
    });
    EXPECT_EQ(foreignSampler->getReferenceCount(), foreignSamplerReferences);
    ASSERT_TRUE(heap.write(samplerHandle, DescriptorWriteItem::Sampler(0u, localSampler.get())));
    heap.free(samplerHandle);

    if(
        device.queryFeatureSupport(Feature::RayTracingAccelStruct)
        && foreignDevice.queryFeatureSupport(Feature::RayTracingAccelStruct)
    ){
        RayTracingAccelStructDesc tlasDesc(DescriptorBufferRoundTripTest::arena());
        tlasDesc.setTopLevelMaxInstances(1u);
        RayTracingAccelStructHandle localTlas = device.createAccelStruct(tlasDesc);
        RayTracingAccelStructHandle foreignTlas = foreignDevice.createAccelStruct(tlasDesc);
        RayTracingAccelStructDesc blasDesc(DescriptorBufferRoundTripTest::arena());
        RayTracingAccelStructHandle localBlas = device.createAccelStruct(blasDesc);
        ASSERT_TRUE(localTlas && foreignTlas && localBlas);

        const GpuDescriptorHandle tlasHandle = heap.allocate(GpuDescriptorClass::AccelStruct);
        ASSERT_TRUE(tlasHandle.valid());
        ExpectDescriptorHeapWriteRejection([&](){
            return heap.write(
                tlasHandle,
                DescriptorWriteItem::RayTracingAccelStruct(0u, nullptr)
            );
        });
        EXPECT_FALSE(heap.getAccelStructBufferBlock(tlasHandle).valid());

        const usize foreignTlasReferences = foreignTlas->getReferenceCount();
        ExpectDescriptorHeapWriteRejection([&](){
            return heap.write(
                tlasHandle,
                DescriptorWriteItem::RayTracingAccelStruct(0u, foreignTlas.get())
            );
        });
        EXPECT_EQ(foreignTlas->getReferenceCount(), foreignTlasReferences);
        EXPECT_FALSE(heap.getAccelStructBufferBlock(tlasHandle).valid());

        const usize localBlasReferences = localBlas->getReferenceCount();
        ExpectDescriptorHeapWriteRejection([&](){
            return heap.write(
                tlasHandle,
                DescriptorWriteItem::RayTracingAccelStruct(0u, localBlas.get())
            );
        });
        EXPECT_EQ(localBlas->getReferenceCount(), localBlasReferences);
        EXPECT_FALSE(heap.getAccelStructBufferBlock(tlasHandle).valid());

        const usize localTlasReferences = localTlas->getReferenceCount();
        ASSERT_TRUE(heap.write(
            tlasHandle,
            DescriptorWriteItem::RayTracingAccelStruct(0u, localTlas.get())
        ));
        EXPECT_EQ(localTlas->getReferenceCount(), localTlasReferences + 1u);
        EXPECT_TRUE(heap.getAccelStructBufferBlock(tlasHandle).valid());
        heap.free(tlasHandle);
        EXPECT_EQ(localTlas->getReferenceCount(), localTlasReferences);
        EXPECT_FALSE(heap.getAccelStructBufferBlock(tlasHandle).valid());
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

