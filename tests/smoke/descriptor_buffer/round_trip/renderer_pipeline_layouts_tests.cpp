// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "round_trip_fixture.h"
#include "shaders_test_support.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// AVBOIT's material and compute passes share one push-only local layout. Their target-generation slot payload is a
// global UniformBuffer descriptor, all work buffers are global StorageBuffer descriptors, and the writable
// transmittance volume is a global StorageImage descriptor. Exercise the shared local shape and material heap
// registrations together so a future pass-local CBV/buffer/image cannot silently reappear in the transparent path.
TEST_F(DescriptorBufferRoundTripTest, AvboitSharedPushLayoutAndMaterialHeapResourcesBuildAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& heap = device.getDescriptorHeap();
    ASSERT_TRUE(heap.isInitialized());

    static_assert(NWB_AVBOIT_PUSH_CONSTANT_BYTE_SIZE == sizeof(u32) * 16u, "AVBOIT compute push ABI must remain four uint4 lanes");
    static_assert(NWB_AVBOIT_DRAW_PUSH_CONSTANT_BYTE_SIZE == sizeof(u32) * 32u, "AVBOIT transparent draw ABI must remain 128 bytes");

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/avboit_depth_gate_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    auto makeConstantBuffer = [&]() {
        return device.createBuffer(
            BufferDesc()
                .setByteSize(64u)
                .setIsConstantBuffer(true)
                .setInitialState(ResourceStates::ConstantBuffer)
                .setKeepInitialState(true)
        );
    };
    auto makeStructuredUav = [&](const u32 stride) {
        return device.createBuffer(
            BufferDesc()
                .setByteSize(stride * 4096u)
                .setStructStride(stride)
                .setCanHaveUAVs(true)
                .setInitialState(ResourceStates::UnorderedAccess)
                .setKeepInitialState(true)
        );
    };

    auto slots = makeConstantBuffer();
    auto coverage = makeStructuredUav(4u);
    auto depthWarp = makeStructuredUav(4u);
    auto control = makeStructuredUav(4u);
    auto extinction = makeStructuredUav(4u);
    auto overflowDepth = makeStructuredUav(4u);
    ASSERT_TRUE(slots && coverage && depthWarp && control && extinction && overflowDepth);

    const GpuDescriptorHandle slotsHandle = heap.allocate(GpuDescriptorClass::UniformBuffer);
    const GpuDescriptorHandle coverageHandle = heap.allocate(GpuDescriptorClass::StorageBuffer);
    const GpuDescriptorHandle depthWarpHandle = heap.allocate(GpuDescriptorClass::StorageBuffer);
    const GpuDescriptorHandle controlHandle = heap.allocate(GpuDescriptorClass::StorageBuffer);
    const GpuDescriptorHandle extinctionHandle = heap.allocate(GpuDescriptorClass::StorageBuffer);
    const GpuDescriptorHandle overflowDepthHandle = heap.allocate(GpuDescriptorClass::StorageBuffer);
    ASSERT_TRUE(
        slotsHandle.valid()
        && coverageHandle.valid()
        && depthWarpHandle.valid()
        && controlHandle.valid()
        && extinctionHandle.valid()
        && overflowDepthHandle.valid()
    );
    EXPECT_EQ(slotsHandle.descriptorClass(), GpuDescriptorClass::UniformBuffer);
    EXPECT_EQ(coverageHandle.descriptorClass(), GpuDescriptorClass::StorageBuffer);
    EXPECT_EQ(depthWarpHandle.descriptorClass(), GpuDescriptorClass::StorageBuffer);
    EXPECT_EQ(controlHandle.descriptorClass(), GpuDescriptorClass::StorageBuffer);
    EXPECT_EQ(extinctionHandle.descriptorClass(), GpuDescriptorClass::StorageBuffer);
    EXPECT_EQ(overflowDepthHandle.descriptorClass(), GpuDescriptorClass::StorageBuffer);
    ASSERT_TRUE(heap.write(slotsHandle, DescriptorWriteItem::ConstantBuffer(0u, slots.get())));
    ASSERT_TRUE(heap.write(coverageHandle, DescriptorWriteItem::StructuredBuffer_UAV(0u, coverage.get())));
    ASSERT_TRUE(heap.write(depthWarpHandle, DescriptorWriteItem::StructuredBuffer_UAV(0u, depthWarp.get())));
    ASSERT_TRUE(heap.write(controlHandle, DescriptorWriteItem::StructuredBuffer_UAV(0u, control.get())));
    ASSERT_TRUE(heap.write(extinctionHandle, DescriptorWriteItem::StructuredBuffer_UAV(0u, extinction.get())));
    ASSERT_TRUE(heap.write(overflowDepthHandle, DescriptorWriteItem::StructuredBuffer_UAV(0u, overflowDepth.get())));

    BindingLayoutDesc sharedLayoutDesc(descArena);
    sharedLayoutDesc.setVisibility(ShaderType::All);
    sharedLayoutDesc.addItem(BindingLayoutItem::PushConstants(0u, NWB_AVBOIT_DRAW_PUSH_CONSTANT_BYTE_SIZE));
    auto sharedLayout = device.createBindingLayout(sharedLayoutDesc);
    ASSERT_NE(sharedLayout.get(), nullptr);
    ASSERT_TRUE(sharedLayout->isDescriptorBufferCompatible());
    EXPECT_EQ(sharedLayout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::None);
    EXPECT_EQ(sharedLayout->getDescriptorBufferSetSizeBytes(), 0u);
    EXPECT_TRUE(sharedLayout->getDescriptorBufferBindingOffsets().empty());

    EXPECT_TRUE(heap.getResourceLayout()->isDescriptorBufferCompatible());
    EXPECT_TRUE(heap.getSamplerLayout()->isDescriptorBufferCompatible());

    heap.free(slotsHandle);
    heap.free(coverageHandle);
    heap.free(depthWarpHandle);
    heap.free(controlHandle);
    heap.free(extinctionHandle);
    heap.free(overflowDepthHandle);
}


// Both AVBOIT compute passes reuse the material path's shared 128-byte push-only layout. Depth warp selects
// coverage/depth-warp/control through the global StorageBuffer heap, while integration selects its writable Texture3D
// plus every source buffer through the same target-generation payload and global heap.
TEST_F(DescriptorBufferRoundTripTest, AvboitComputeResourcesUseSharedPushLayout){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& heap = device.getDescriptorHeap();
    ASSERT_TRUE(heap.isInitialized());

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/avboit_compute_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    auto transmittance = device.createTexture(
        TextureDesc()
            .setWidth(16u)
            .setHeight(16u)
            .setDepth(4u)
            .setDimension(TextureDimension::Texture3D)
            .setFormat(NWB_AVBOIT_TRANSMITTANCE_CORE_FORMAT)
            .setInUAV(true)
            .setInitialState(ResourceStates::UnorderedAccess)
            .setKeepInitialState(true)
    );
    ASSERT_TRUE(transmittance);

    const GpuDescriptorHandle transmittanceStorageHandle = heap.allocate(GpuDescriptorClass::StorageImage);
    ASSERT_TRUE(transmittanceStorageHandle.valid());
    EXPECT_EQ(transmittanceStorageHandle.descriptorClass(), GpuDescriptorClass::StorageImage);
    ASSERT_TRUE(heap.write(transmittanceStorageHandle, DescriptorWriteItem::Texture_UAV(
        0u,
        transmittance.get(),
        NWB_AVBOIT_TRANSMITTANCE_CORE_FORMAT,
        TextureSubresourceSet(0u, 1u, 0u, 1u),
        TextureDimension::Texture3D
    )));

    EXPECT_LE(NWB_AVBOIT_PUSH_CONSTANT_BYTE_SIZE, NWB_AVBOIT_DRAW_PUSH_CONSTANT_BYTE_SIZE);
    BindingLayoutDesc sharedLayoutDesc(descArena);
    sharedLayoutDesc.setVisibility(ShaderType::All);
    sharedLayoutDesc.addItem(BindingLayoutItem::PushConstants(0u, NWB_AVBOIT_DRAW_PUSH_CONSTANT_BYTE_SIZE));
    auto sharedLayout = device.createBindingLayout(sharedLayoutDesc);
    ASSERT_NE(sharedLayout.get(), nullptr);
    ASSERT_TRUE(sharedLayout->isDescriptorBufferCompatible());
    EXPECT_EQ(sharedLayout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::None);
    EXPECT_EQ(sharedLayout->getDescriptorBufferSetSizeBytes(), 0u);
    EXPECT_TRUE(sharedLayout->getDescriptorBufferBindingOffsets().empty());

    EXPECT_TRUE(heap.getResourceLayout()->isDescriptorBufferCompatible());
    EXPECT_TRUE(heap.getSamplerLayout()->isDescriptorBufferCompatible());

    heap.free(transmittanceStorageHandle);
}


// CSG's clip, cap-fill, and interval dispatch inputs all live in the global descriptor heap. The clip/cap-fill leaf
// retains the shared 64-byte mesh push ABI, while each interval kernel uses its 48-byte dispatch selector ABI. Keep
// both local layouts descriptor-free and prove the corresponding UniformBuffer/StorageBuffer heap writes separately.
TEST_F(DescriptorBufferRoundTripTest, CsgMaterialTailShapesBuildAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/csg_material_tail_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    auto makeConstantBuffer = [&]() {
        return device.createBuffer(
            BufferDesc()
                .setByteSize(64u)
                .setIsConstantBuffer(true)
                .setInitialState(ResourceStates::ConstantBuffer)
                .setKeepInitialState(true)
        );
    };
    auto makeStructuredSrv = [&](const u32 stride) {
        return device.createBuffer(
            BufferDesc()
                .setByteSize(stride * 64u)
                .setStructStride(stride)
                .setCanHaveRawViews(true)
                .setInitialState(ResourceStates::ShaderResource)
                .setKeepInitialState(true)
        );
    };
    auto receiverRanges = makeStructuredSrv(96u);
    auto cutters = makeStructuredSrv(112u);
    auto materialTyped = makeStructuredSrv(4u);
    auto instances = makeStructuredSrv(64u);
    auto clipContextSlots = makeConstantBuffer();
    auto bindlessSlots = makeConstantBuffer();
    auto sampleState = makeConstantBuffer();
    auto meshView = makeConstantBuffer();
    ASSERT_TRUE(
        receiverRanges && cutters && materialTyped && instances && clipContextSlots && bindlessSlots && sampleState && meshView
    );

    const auto verifyPushOnlyLayout = [&](const char* name, const ShaderType::Mask visibility, const u32 pushConstantByteSize) {
        SCOPED_TRACE(name);
        BindingLayoutDesc layoutDesc(descArena);
        layoutDesc.setVisibility(visibility);
        layoutDesc.addItem(BindingLayoutItem::PushConstants(0u, pushConstantByteSize));

        auto layout = device.createBindingLayout(layoutDesc);
        ASSERT_NE(layout.get(), nullptr);
        ASSERT_TRUE(layout->isDescriptorBufferCompatible());
        EXPECT_EQ(layout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::None);
        EXPECT_EQ(layout->getDescriptorBufferSetSizeBytes(), 0u);
        EXPECT_TRUE(layout->getDescriptorBufferBindingOffsets().empty());
    };
    verifyPushOnlyLayout("clip/cap-fill", ShaderType::Mesh | ShaderType::Compute | ShaderType::Pixel, sizeof(u32) * 16u);
    verifyPushOnlyLayout("interval dispatch", ShaderType::Compute, sizeof(u32) * 12u);

    // The CSG shader aliases its uint/float Texture2DArray views onto the existing StorageImage heap binding. Prove
    // the live heap accepts a typed array UAV descriptor there; the graphics cook verifies each Slang alias emitted
    // against this same set-0 binding.
    auto& heap = device.getDescriptorHeap();
    ASSERT_TRUE(heap.isInitialized());
    const auto registerCsgUniformBuffer = [&](Core::Buffer* buffer) {
        const GpuDescriptorHandle handle = heap.allocate(GpuDescriptorClass::UniformBuffer);
        EXPECT_TRUE(handle.valid());
        if(handle.valid())
            EXPECT_TRUE(heap.write(handle, DescriptorWriteItem::ConstantBuffer(0u, buffer)));
        return handle;
    };
    const GpuDescriptorHandle clipContextHandle = registerCsgUniformBuffer(clipContextSlots.get());
    const GpuDescriptorHandle bindlessSlotsHandle = registerCsgUniformBuffer(bindlessSlots.get());
    const GpuDescriptorHandle sampleStateHandle = registerCsgUniformBuffer(sampleState.get());
    const GpuDescriptorHandle meshViewHandle = registerCsgUniformBuffer(meshView.get());
    auto csgStorageImage = device.createTexture(
        TextureDesc()
            .setWidth(16u)
            .setHeight(16u)
            .setArraySize(3u)
            .setDimension(TextureDimension::Texture2DArray)
            .setFormat(Format::RGBA32_UINT)
            .setInUAV(true)
            .setInitialState(ResourceStates::UnorderedAccess)
            .setKeepInitialState(true)
    );
    ASSERT_TRUE(csgStorageImage);
    const GpuDescriptorHandle csgStorageHandle = heap.allocate(GpuDescriptorClass::StorageImage);
    ASSERT_TRUE(csgStorageHandle.valid());
    EXPECT_TRUE(heap.write(
        csgStorageHandle,
        DescriptorWriteItem::Texture_UAV(
            0u,
            csgStorageImage.get(),
            Format::RGBA32_UINT,
            TextureSubresourceSet(0u, 1u, 0u, 3u),
            TextureDimension::Texture2DArray
        )
    ));

    // CSG's receiver/cutter buffers and the cap-fill material/instance inputs are each persistent StorageBuffer heap
    // entries. The local clip layout above deliberately has no SRVs for them, so prove all four descriptor writes use
    // the same global table instead.
    const auto registerCsgContextBuffer = [&](Core::Buffer* buffer) {
        const GpuDescriptorHandle handle = heap.allocate(GpuDescriptorClass::StorageBuffer);
        EXPECT_TRUE(handle.valid());
        if(handle.valid())
            EXPECT_TRUE(heap.write(handle, DescriptorWriteItem::StructuredBuffer_SRV(0u, buffer)));
        return handle;
    };
    const GpuDescriptorHandle receiverRangeHandle = registerCsgContextBuffer(receiverRanges.get());
    const GpuDescriptorHandle cutterHandle = registerCsgContextBuffer(cutters.get());
    const GpuDescriptorHandle materialTypedHandle = registerCsgContextBuffer(materialTyped.get());
    const GpuDescriptorHandle instanceHandle = registerCsgContextBuffer(instances.get());

    if(clipContextHandle.valid())
        heap.free(clipContextHandle);
    if(bindlessSlotsHandle.valid())
        heap.free(bindlessSlotsHandle);
    if(sampleStateHandle.valid())
        heap.free(sampleStateHandle);
    if(meshViewHandle.valid())
        heap.free(meshViewHandle);
    heap.free(csgStorageHandle);
    if(receiverRangeHandle.valid())
        heap.free(receiverRangeHandle);
    if(cutterHandle.valid())
        heap.free(cutterHandle);
    if(materialTypedHandle.valid())
        heap.free(materialTypedHandle);
    if(instanceHandle.valid())
        heap.free(instanceHandle);
}


// Caustic resolve selects every sampled input and its writable output from the global descriptor heap. Its local ABI
// is therefore only the 14-word selector push block; a future local image or buffer must make this proof fail.
TEST_F(DescriptorBufferRoundTripTest, CausticResolveShapeBuildsAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();

    // A short-lived arena for the layout desc (it copies its bindings into object-arena storage on creation).
    static constexpr Name kDescArenaName{"tests/descriptor_buffer/caustic_shape_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    // CausticResolvePushConstants is 14 scalar words in the C++/Slang ABI.
    BindingLayoutDesc layoutDesc(descArena);
    layoutDesc.setVisibility(ShaderType::Compute);
    layoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 14u));

    auto layout = device.createBindingLayout(layoutDesc);
    ASSERT_NE(layout.get(), nullptr);

    ASSERT_TRUE(layout->isDescriptorBufferCompatible())
        << "caustic resolve push-only shape is not compatible with descriptor-buffer pipelines";
    EXPECT_EQ(layout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::None);
    EXPECT_EQ(layout->getDescriptorBufferSetSizeBytes(), 0u);
    EXPECT_TRUE(layout->getDescriptorBufferBindingOffsets().empty());
}


// Caustic geometry downsample reads its G-buffer inputs and writes its half-res geometry cache through global heap
// slots. Its local ABI is only the eight-word selector push block.
TEST_F(DescriptorBufferRoundTripTest, CausticGeometryDownsampleShapeBuildsAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/caustic_geom_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    // CausticGeometryDownsamplePushConstants is eight scalar words in the C++/Slang ABI.
    BindingLayoutDesc layoutDesc(descArena);
    layoutDesc.setVisibility(ShaderType::Compute);
    layoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 8u));

    auto layout = device.createBindingLayout(layoutDesc);
    ASSERT_NE(layout.get(), nullptr);

    ASSERT_TRUE(layout->isDescriptorBufferCompatible())
        << "caustic geometry downsample push-only shape is not compatible with descriptor-buffer pipelines";
    EXPECT_EQ(layout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::None);
    EXPECT_EQ(layout->getDescriptorBufferSetSizeBytes(), 0u);
    EXPECT_TRUE(layout->getDescriptorBufferBindingOffsets().empty());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Caustic accumulator decay selects its writable R32_UINT Texture2DArray from the global StorageImage heap. Its local
// ABI is only the four-word push block.
TEST_F(DescriptorBufferRoundTripTest, CausticAccumulatorDecayShapeBuildsAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/caustic_decay_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    // CausticAccumulatorDecayPushConstants is four scalar words in the C++/Slang ABI.
    BindingLayoutDesc layoutDesc(descArena);
    layoutDesc.setVisibility(ShaderType::Compute);
    layoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 4u));

    auto layout = device.createBindingLayout(layoutDesc);
    ASSERT_NE(layout.get(), nullptr);

    ASSERT_TRUE(layout->isDescriptorBufferCompatible())
        << "caustic accumulator decay push-only shape is not compatible with descriptor-buffer pipelines";
    EXPECT_EQ(layout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::None);
    EXPECT_EQ(layout->getDescriptorBufferSetSizeBytes(), 0u);
    EXPECT_TRUE(layout->getDescriptorBufferBindingOffsets().empty());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Both caustic photon producers fetch every selector, input, and accumulator output from the global descriptor heap.
// Their identical local ABI is only the shared 15-word photon push block; the fixed TLAS remains in the global heap.
TEST_F(DescriptorBufferRoundTripTest, CausticPhotonProducerShapesBuildAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/caustic_photon_producer_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    // CausticPhotonPushConstants is 15 scalar words and byte-identical for the SW and HW producers.
    BindingLayoutDesc swLayoutDesc(descArena);
    swLayoutDesc.setVisibility(ShaderType::Compute);
    swLayoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 15u));

    auto swLayout = device.createBindingLayout(swLayoutDesc);
    ASSERT_NE(swLayout.get(), nullptr);
    ASSERT_TRUE(swLayout->isDescriptorBufferCompatible())
        << "caustic SW photon push-only shape is not compatible with descriptor-buffer pipelines";
    EXPECT_EQ(swLayout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::None);
    EXPECT_EQ(swLayout->getDescriptorBufferSetSizeBytes(), 0u);
    EXPECT_TRUE(swLayout->getDescriptorBufferBindingOffsets().empty());

    // Descriptor layout compatibility is independent of shader stage, so Compute makes this HW local-shape proof
    // runnable on descriptor-buffer-only test devices.
    BindingLayoutDesc hwLayoutDesc(descArena);
    hwLayoutDesc.setVisibility(ShaderType::Compute);
    hwLayoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 15u));

    auto hwLayout = device.createBindingLayout(hwLayoutDesc);
    ASSERT_NE(hwLayout.get(), nullptr);
    ASSERT_TRUE(hwLayout->isDescriptorBufferCompatible())
        << "caustic HW photon push-only shape is not compatible with descriptor-buffer pipelines";
    EXPECT_EQ(hwLayout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::None);
    EXPECT_EQ(hwLayout->getDescriptorBufferSetSizeBytes(), 0u);
    EXPECT_TRUE(hwLayout->getDescriptorBufferBindingOffsets().empty());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

