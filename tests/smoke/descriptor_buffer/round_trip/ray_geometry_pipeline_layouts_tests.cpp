// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// BVH bitonic-sort parity: its keys and payload are global StorageBuffer-heap entries selected by the push block.
// The pipeline-local leaf therefore has no resource entries or descriptor object; it must stay descriptor-buffer-compatible
// alongside the heap's persistent resource/sampler layouts.
TEST_F(DescriptorBufferRoundTripTest, BvhSortPushOnlyHeapLayoutBuildsAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& heap = device.getDescriptorHeap();
    ASSERT_TRUE(heap.isInitialized());

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/bvh_sort_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    BindingLayoutDesc layoutDesc(descArena);
    layoutDesc.setVisibility(ShaderType::Compute);
    layoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 8u));

    auto layout = device.createBindingLayout(layoutDesc);
    ASSERT_NE(layout.get(), nullptr);

    ASSERT_TRUE(layout->isDescriptorBufferCompatible())
        << "bvh sort push-only layout is not compatible with descriptor-buffer pipelines";
    EXPECT_TRUE(layout->getDescriptorBufferBindingOffsets().empty());
    EXPECT_TRUE(heap.getResourceLayout()->isDescriptorBufferCompatible());
    EXPECT_TRUE(heap.getSamplerLayout()->isDescriptorBufferCompatible());
}


// BVH LBVH-build parity: all five scratch/work buffers are heap registrations. The local leaf only
// carries the expanded push constants; heap writes retain each concrete buffer until deferred free retires its slot.
TEST_F(DescriptorBufferRoundTripTest, BvhBuildPushOnlyHeapLayoutRegistersScratchBuffers){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& heap = device.getDescriptorHeap();
    ASSERT_TRUE(heap.isInitialized());

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/bvh_build_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    auto makeStructuredUav = [&](const u32 stride) {
        return device.createBuffer(
            BufferDesc()
                .setByteSize(stride * 4096u)
                .setStructStride(stride)
                .setCanHaveUAVs(true)
                .setInitialState(ResourceStates::Common)
                .setKeepInitialState(true)
        );
    };

    auto buildKeys = makeStructuredUav(4u);
    auto buildPayload = makeStructuredUav(4u);
    auto nodes = makeStructuredUav(64u);
    auto parent = makeStructuredUav(4u);
    auto visitCounter = makeStructuredUav(4u);
    ASSERT_TRUE(buildKeys && buildPayload && nodes && parent && visitCounter);

    BindingLayoutDesc layoutDesc(descArena);
    layoutDesc.setVisibility(ShaderType::Compute);
    layoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 12u + sizeof(Float4) * 2u));

    auto layout = device.createBindingLayout(layoutDesc);
    ASSERT_NE(layout.get(), nullptr);

    ASSERT_TRUE(layout->isDescriptorBufferCompatible())
        << "bvh build push-only layout is not compatible with descriptor-buffer pipelines";
    EXPECT_TRUE(layout->getDescriptorBufferBindingOffsets().empty());

    Core::Buffer* buffers[] = {
        buildKeys.get(),
        buildPayload.get(),
        nodes.get(),
        parent.get(),
        visitCounter.get(),
    };
    Vector<GpuDescriptorHandle, Alloc::GlobalArena> handles(descArena);
    for(Core::Buffer* buffer : buffers){
        const GpuDescriptorHandle handle = heap.allocate(GpuDescriptorClass::StorageBuffer);
        ASSERT_TRUE(handle.valid());
        ASSERT_TRUE(heap.write(handle, DescriptorWriteItem::StructuredBuffer_UAV(0u, buffer)));
        EXPECT_EQ(handle.descriptorClass(), GpuDescriptorClass::StorageBuffer);
        handles.push_back(handle);
    }
    for(const GpuDescriptorHandle handle : handles)
        heap.free(handle);
}


// Shadow geometry-downsample reads its G-buffer inputs, scene payload, and output through the global heap. The local
// leaf is therefore only the ten-word selector push ABI.
TEST_F(DescriptorBufferRoundTripTest, ShadowGeometryDownsampleShapeBuildsAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/shadow_geom_downsample_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    BindingLayoutDesc layoutDesc(descArena);
    layoutDesc.setVisibility(ShaderType::Compute);
    layoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 10u));

    auto layout = device.createBindingLayout(layoutDesc);
    ASSERT_NE(layout.get(), nullptr);

    ASSERT_TRUE(layout->isDescriptorBufferCompatible())
        << "shadow geometry downsample push-only shape is not compatible with descriptor-buffer pipelines";
    EXPECT_EQ(layout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::None);
    EXPECT_EQ(layout->getDescriptorBufferSetSizeBytes(), 0u);
    EXPECT_TRUE(layout->getDescriptorBufferBindingOffsets().empty());
}


// The SVGF a-trous resolve (and its RGB variant) selects every input, output, and scene payload from the global heap.
// Its local leaf is the 21-word selector push ABI.
TEST_F(DescriptorBufferRoundTripTest, ShadowResolveShapeBuildsAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/shadow_resolve_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    BindingLayoutDesc layoutDesc(descArena);
    layoutDesc.setVisibility(ShaderType::Compute);
    layoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 21u));

    auto layout = device.createBindingLayout(layoutDesc);
    ASSERT_NE(layout.get(), nullptr);

    ASSERT_TRUE(layout->isDescriptorBufferCompatible())
        << "shadow resolve push-only shape is not compatible with descriptor-buffer pipelines";
    EXPECT_EQ(layout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::None);
    EXPECT_EQ(layout->getDescriptorBufferSetSizeBytes(), 0u);
    EXPECT_TRUE(layout->getDescriptorBufferBindingOffsets().empty());
}


// Shadow reproject-merge selects its temporal images and geometry from the global heap. Its local leaf retains the
// 128-byte matrix-plus-selector push ABI only.
TEST_F(DescriptorBufferRoundTripTest, ShadowReprojectMergeShapeBuildsAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/shadow_reproject_merge_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    BindingLayoutDesc layoutDesc(descArena);
    layoutDesc.setVisibility(ShaderType::Compute);
    layoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 32u));

    auto layout = device.createBindingLayout(layoutDesc);
    ASSERT_NE(layout.get(), nullptr);

    ASSERT_TRUE(layout->isDescriptorBufferCompatible())
        << "shadow reproject-merge push-only shape is not compatible with descriptor-buffer pipelines";
    EXPECT_EQ(layout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::None);
    EXPECT_EQ(layout->getDescriptorBufferSetSizeBytes(), 0u);
    EXPECT_TRUE(layout->getDescriptorBufferBindingOffsets().empty());
}


// The HW hard and soft inline-RayQuery passes select their TLAS, G-buffer inputs, and visibility outputs from the
// global heap. They differ only in their six-word hard and nine-word soft push selector ABIs.
TEST_F(DescriptorBufferRoundTripTest, ShadowRtTraceShapeBuildsAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/shadow_rt_trace_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    struct RayQueryShape{
        const char* name = nullptr;
        u32 pushConstantWordCount = 0u;
    };
    const RayQueryShape shapes[] = {
        { "hard", 6u },
        { "soft", 9u },
    };
    for(const RayQueryShape& shape : shapes){
        SCOPED_TRACE(shape.name);
        BindingLayoutDesc layoutDesc(descArena);
        layoutDesc.setVisibility(ShaderType::Compute);
        layoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * shape.pushConstantWordCount));

        auto layout = device.createBindingLayout(layoutDesc);
        ASSERT_NE(layout.get(), nullptr);
        ASSERT_TRUE(layout->isDescriptorBufferCompatible())
            << "shadow RT " << shape.name << " push-only shape is not compatible with descriptor-buffer pipelines";
        EXPECT_EQ(layout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::None);
        EXPECT_EQ(layout->getDescriptorBufferSetSizeBytes(), 0u);
        EXPECT_TRUE(layout->getDescriptorBufferBindingOffsets().empty());
    }
}


// All nine software-shadow kernels share one 21-word selector push ABI. Their G-buffer, context, output, scratch,
// and indirect resources are global-heap entries, leaving no local descriptor bindings.
TEST_F(DescriptorBufferRoundTripTest, SwShadowTraceShapeBuildsAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/sw_shadow_trace_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    BindingLayoutDesc layoutDesc(descArena);
    layoutDesc.setVisibility(ShaderType::Compute);
    layoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 21u));

    auto layout = device.createBindingLayout(layoutDesc);
    ASSERT_NE(layout.get(), nullptr);
    ASSERT_TRUE(layout->isDescriptorBufferCompatible())
        << "SW shadow push-only shape is not compatible with descriptor-buffer pipelines";
    EXPECT_EQ(layout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::None);
    EXPECT_EQ(layout->getDescriptorBufferSetSizeBytes(), 0u);
    EXPECT_TRUE(layout->getDescriptorBufferBindingOffsets().empty());
}


// Skinned-mesh compute resolves every persistent stream and its per-runtime selector payload through the global heap.
// Its three local leaves therefore retain only their dispatch push blocks. Verify those push-only layouts and their
// composition with the global resource/sampler heap layouts.
TEST_F(DescriptorBufferRoundTripTest, SkinnedMeshComputeShapesBuildAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& heap = device.getDescriptorHeap();
    ASSERT_TRUE(heap.isInitialized());

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/skinned_mesh_compute_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    struct ComputeShape{
        const char* name = nullptr;
        u32 pushConstantByteSize = 0u;
    };
    const ComputeShape shapes[] = {
        { "skinning", NWB_SKINNED_MESH_PUSH_CONSTANT_BYTE_SIZE },
        { "bounds", NWB_SKINNED_MESH_BOUNDS_PUSH_CONSTANT_BYTE_SIZE },
        { "repack", NWB_SKINNED_MESH_REPACK_PUSH_CONSTANT_BYTE_SIZE },
    };

    for(const ComputeShape& shape : shapes){
        SCOPED_TRACE(shape.name);

        BindingLayoutDesc layoutDesc(descArena);
        layoutDesc
            .setVisibility(ShaderType::Compute)
        ;
        layoutDesc.addItem(BindingLayoutItem::PushConstants(0u, shape.pushConstantByteSize));

        auto layout = device.createBindingLayout(layoutDesc);
        ASSERT_NE(layout.get(), nullptr);
        ASSERT_TRUE(layout->isDescriptorBufferCompatible())
            << shape.name << " push-only layout is not compatible with descriptor-buffer pipelines";
        EXPECT_EQ(layout->getDescriptorBufferSegmentKind(), GraphicsBackend::DescriptorBufferSegmentKind::None);
        EXPECT_EQ(layout->getDescriptorBufferSetSizeBytes(), 0u);
        EXPECT_TRUE(layout->getDescriptorBufferBindingOffsets().empty());

        // This is the exact three-layout composition used by each production skinned compute pipeline: its local
        // slot-CB set followed by the fixed resource/sampler heap sets. No shader is needed to validate descriptor
        // layout composition, and creating this descriptor keeps the test headless and independent of cooked assets.
        ComputePipelineDesc pipelineDesc;
        pipelineDesc
            .addBindingLayout(layout)
            .addBindingLayout(heap.getResourceLayout())
            .addBindingLayout(heap.getSamplerLayout())
        ;
        ASSERT_EQ(pipelineDesc.bindingLayouts.size(), 3u);
        EXPECT_EQ(pipelineDesc.bindingLayouts[0].get(), layout.get());
        EXPECT_EQ(pipelineDesc.bindingLayouts[1].get(), heap.getResourceLayout().get());
        EXPECT_EQ(pipelineDesc.bindingLayouts[2].get(), heap.getSamplerLayout().get());
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

