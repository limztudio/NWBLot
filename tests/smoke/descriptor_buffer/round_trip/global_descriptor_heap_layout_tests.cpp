// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Global-heap proof: the GpuDescriptorHeap requires the descriptor-buffer backend where the device advertises
// VK_EXT_descriptor_buffer. Unlike the per-pass shape tests above (which exercise push-only pipeline layouts), this proves
// the heap itself -- a persistent, per-slot-writable structure -- (1) selected the required backend, (2) built descriptor-buffer-
// compatible bindless layouts at sets 0/1, (3) carved two persistent blocks from its segments, and (4)
// routes write() through the descriptor-buffer path. This is the prerequisite the five heap-coupled tail pipelines
// (surfel SW/HW trace, caustic SW/HW, SW shadow) embed, so their opt-in is only valid when these hold.
TEST_F(DescriptorBufferRoundTripTest, GlobalDescriptorHeapRequiresDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& heap = device.getDescriptorHeap();
    ASSERT_TRUE(heap.isInitialized()) << "device-owned GpuDescriptorHeap was not initialized";

    // Initialization proves the required descriptor-buffer path: no ordinary descriptor-set heap implementation exists.

    // The heap's two bindless layouts must be descriptor-buffer-compatible so a pipeline that embeds them at sets 0/1
    // passes the all-compatible wholesale-conversion gate. Each is pure-class by construction (resource / sampler).
    const auto* resourceLayout = heap.getResourceLayout().get();
    const auto* samplerLayout = heap.getSamplerLayout().get();
    ASSERT_NE(resourceLayout, nullptr);
    ASSERT_NE(samplerLayout, nullptr);
    EXPECT_TRUE(resourceLayout->isDescriptorBufferCompatible())
        << "heap resource bindless layout is not descriptor-buffer-compatible";
    EXPECT_TRUE(samplerLayout->isDescriptorBufferCompatible())
        << "heap sampler bindless layout is not descriptor-buffer-compatible";

    // Each layout reports a non-zero driver-queried set size and resolves to its expected segment (resource set ->
    // Resource segment, sampler set -> Sampler segment), the carve input for the persistent heap blocks.
    EXPECT_GT(resourceLayout->getDescriptorBufferSetSizeBytes(), 0u);
    EXPECT_EQ(resourceLayout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::Resource);
    EXPECT_GT(samplerLayout->getDescriptorBufferSetSizeBytes(), 0u);
    EXPECT_EQ(samplerLayout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::Sampler);

    // The two persistent heap blocks were carved once at init and live for the heap's lifetime; their offsets are what
    // vkCmdSetDescriptorBufferOffsetsEXT binds at sets 0/1. A zero/invalid block would mean no carve happened.
    const auto& resourceBlock = heap.getResourceBufferBlock();
    const auto& samplerBlock = heap.getSamplerBufferBlock();
    EXPECT_TRUE(resourceBlock.valid())
        << "heap resource descriptor-buffer block was not carved";
    EXPECT_TRUE(samplerBlock.valid())
        << "heap sampler descriptor-buffer block was not carved";

    // write() must route through the descriptor-buffer path: allocate a slot in the StorageBuffer class
    // and write a structured buffer into it. The write lands in the resource block at
    // block.offsetBytes + classBindingOffset + slot*descriptorSize; success proves the carve + class-offset cache +
    // write path.
    static constexpr Name kDescArenaName{"tests/descriptor_buffer/heap_write_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    auto structuredBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(16u * 4096u)
            .setStructStride(16u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    ASSERT_TRUE(structuredBuffer);

    const GpuDescriptorHandle handle = heap.allocate(GpuDescriptorClass::StorageBuffer);
    ASSERT_TRUE(handle.valid());
    EXPECT_TRUE(heap.write(handle, DescriptorWriteItem::StructuredBuffer_SRV(0u, structuredBuffer.get())))
        << "heap write() did not route through the descriptor-buffer path";

    heap.free(handle);

    // Deferred lighting consumes its per-light shadow visibility as Texture2DArray while ordinary G-buffer and
    // compositor inputs remain Texture2D. Both use VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, but the shader types differ,
    // so this distinct heap class/binding must have a valid descriptor-buffer write path as well.
    auto sampledImageArray = device.createTexture(
        TextureDesc()
            .setWidth(32u)
            .setHeight(32u)
            .setArraySize(3u)
            .setDimension(TextureDimension::Texture2DArray)
            .setFormat(Format::RGBA16_FLOAT)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    ASSERT_TRUE(sampledImageArray);
    EXPECT_EQ(sampledImageArray->getReferenceCount(), 1u);

    EXPECT_EQ(
        heap.getRegisterSlot(GpuDescriptorClass::SampledImage2DArray),
        NWB_BINDLESS_HEAP_BINDING_SAMPLED_IMAGE_2D_ARRAY
    );
    const GpuDescriptorHandle sampledImageArrayHandle = heap.allocate(GpuDescriptorClass::SampledImage2DArray);
    ASSERT_TRUE(sampledImageArrayHandle.valid());
    EXPECT_TRUE(heap.write(
        sampledImageArrayHandle,
        DescriptorWriteItem::Texture_SRV(
            0u,
            sampledImageArray.get(),
            Format::RGBA16_FLOAT,
            TextureSubresourceSet(0u, 1u, 0u, 3u),
            TextureDimension::Texture2DArray
        )
    )) << "heap Texture2DArray write() did not route through the descriptor-buffer path";
    EXPECT_EQ(sampledImageArray->getReferenceCount(), 2u)
        << "heap write() did not retain the persistent Texture2DArray resource";

    heap.free(sampledImageArrayHandle);
    EXPECT_EQ(sampledImageArray->getReferenceCount(), 1u)
        << "heap did not release an unused descriptor resource immediately";

    // The caustic resolve reads its R32_UINT fixed-point accumulator through a dedicated typed uint Texture2DArray
    // descriptor table. Its image-view format differs from the floating-point array above, so verify the distinct
    // descriptor-buffer class/binding's write and resource-retention path too.
    auto sampledImageArrayUint = device.createTexture(
        TextureDesc()
            .setWidth(32u)
            .setHeight(32u)
            .setArraySize(3u)
            .setDimension(TextureDimension::Texture2DArray)
            .setFormat(Format::R32_UINT)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    ASSERT_TRUE(sampledImageArrayUint);
    EXPECT_EQ(sampledImageArrayUint->getReferenceCount(), 1u);

    EXPECT_EQ(
        heap.getRegisterSlot(GpuDescriptorClass::SampledImage2DArrayUint),
        NWB_BINDLESS_HEAP_BINDING_SAMPLED_IMAGE_2D_ARRAY_UINT
    );
    const GpuDescriptorHandle sampledImageArrayUintHandle = heap.allocate(GpuDescriptorClass::SampledImage2DArrayUint);
    ASSERT_TRUE(sampledImageArrayUintHandle.valid());
    EXPECT_TRUE(heap.write(
        sampledImageArrayUintHandle,
        DescriptorWriteItem::Texture_SRV(
            0u,
            sampledImageArrayUint.get(),
            Format::R32_UINT,
            TextureSubresourceSet(0u, 1u, 0u, 3u),
            TextureDimension::Texture2DArray
        )
    )) << "heap R32_UINT Texture2DArray write() did not route through the descriptor-buffer path";
    EXPECT_EQ(sampledImageArrayUint->getReferenceCount(), 2u)
        << "heap write() did not retain the typed Texture2DArray resource";

    heap.free(sampledImageArrayUintHandle);
    EXPECT_EQ(sampledImageArrayUint->getReferenceCount(), 1u)
        << "heap did not release an unused typed Texture2DArray resource immediately";

    // AVBOIT accumulation consumes its integrated transmittance as Texture3D. This needs a separate shader-side
    // descriptor array from Texture2D/Texture2DArray even though all three encode VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE.
    // Verify the new class routes through the descriptor-buffer path and keeps the volume alive while its handle is live.
    auto sampledImage3D = device.createTexture(
        TextureDesc()
            .setWidth(32u)
            .setHeight(32u)
            .setDepth(8u)
            .setDimension(TextureDimension::Texture3D)
            .setFormat(Format::RGBA16_FLOAT)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    ASSERT_TRUE(sampledImage3D);
    EXPECT_EQ(sampledImage3D->getReferenceCount(), 1u);

    EXPECT_EQ(
        heap.getRegisterSlot(GpuDescriptorClass::SampledImage3D),
        NWB_BINDLESS_HEAP_BINDING_SAMPLED_IMAGE_3D
    );
    const GpuDescriptorHandle sampledImage3DHandle = heap.allocate(GpuDescriptorClass::SampledImage3D);
    ASSERT_TRUE(sampledImage3DHandle.valid());
    EXPECT_TRUE(heap.write(
        sampledImage3DHandle,
        DescriptorWriteItem::Texture_SRV(
            0u,
            sampledImage3D.get(),
            Format::RGBA16_FLOAT,
            TextureSubresourceSet(0u, 1u, 0u, 1u),
            TextureDimension::Texture3D
        )
    )) << "heap Texture3D write() did not route through the descriptor-buffer path";
    EXPECT_EQ(sampledImage3D->getReferenceCount(), 2u)
        << "heap write() did not retain the persistent Texture3D resource";

    heap.free(sampledImage3DHandle);
    EXPECT_EQ(sampledImage3D->getReferenceCount(), 1u)
        << "heap did not release an unused Texture3D resource immediately";

    // TextureCube has a distinct image-view type from the other sampled-image arrays. The heap therefore gives it
    // its own appended ABI binding/class instead of exposing a cube through the Texture2D table.
    auto sampledImageCube = device.createTexture(
        TextureDesc()
            .setWidth(32u)
            .setHeight(32u)
            .setArraySize(6u)
            .setDimension(TextureDimension::TextureCube)
            .setFormat(Format::RGBA16_FLOAT)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    ASSERT_TRUE(sampledImageCube);
    EXPECT_EQ(sampledImageCube->getReferenceCount(), 1u);

    EXPECT_EQ(
        heap.getRegisterSlot(GpuDescriptorClass::SampledImageCube),
        NWB_BINDLESS_HEAP_BINDING_SAMPLED_IMAGE_CUBE
    );
    const GpuDescriptorHandle sampledImageCubeHandle = heap.allocate(GpuDescriptorClass::SampledImageCube);
    ASSERT_TRUE(sampledImageCubeHandle.valid());
    EXPECT_TRUE(heap.write(
        sampledImageCubeHandle,
        DescriptorWriteItem::Texture_SRV(
            0u,
            sampledImageCube.get(),
            Format::RGBA16_FLOAT,
            TextureSubresourceSet(0u, 1u, 0u, 6u),
            TextureDimension::TextureCube
        )
    )) << "heap TextureCube write() did not route through the descriptor-buffer path";
    EXPECT_EQ(sampledImageCube->getReferenceCount(), 2u)
        << "heap write() did not retain the persistent TextureCube resource";

    heap.free(sampledImageCubeHandle);
    EXPECT_EQ(sampledImageCube->getReferenceCount(), 1u)
        << "heap did not release an unused TextureCube resource immediately";
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

