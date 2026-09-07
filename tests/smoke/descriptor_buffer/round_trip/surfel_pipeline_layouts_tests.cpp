// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Both surfel-GI trace backends select their target-generation, material-context, surfel, and scene data through the
// global descriptor heap. Their pipeline-local layout is therefore only the shared 14-u32 selector push range.
TEST_F(DescriptorBufferRoundTripTest, SurfelTraceShapesBuildAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/surfel_trace_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    auto makeConstantBuffer = [&]() {
        return device.createBuffer(
            BufferDesc()
                .setByteSize(256u)
                .setIsConstantBuffer(true)
                .setInitialState(ResourceStates::ConstantBuffer)
                .setKeepInitialState(true)
        );
    };
    auto makeStructuredSrv = [&](const u32 stride) {
        return device.createBuffer(
            BufferDesc()
                .setByteSize(stride * 256u)
                .setStructStride(stride)
                .setCanHaveRawViews(true)
                .setInitialState(ResourceStates::ShaderResource)
                .setKeepInitialState(true)
        );
    };
    auto makeStructuredUav = [&](const u32 stride) {
        return device.createBuffer(
            BufferDesc()
                .setByteSize(stride * 256u)
                .setStructStride(stride)
                .setCanHaveRawViews(true)
                .setInitialState(ResourceStates::UnorderedAccess)
                .setKeepInitialState(true)
        );
    };

    auto bindlessResources = makeConstantBuffer();
    auto materialContextSlots = makeConstantBuffer();
    auto surfelConstants = makeConstantBuffer();
    auto pool = makeStructuredUav(96u);
    auto snapshotPool = makeStructuredSrv(96u);
    auto snapshotCellHead = makeStructuredSrv(4u);
    ASSERT_TRUE(
        bindlessResources && materialContextSlots && surfelConstants && pool && snapshotPool && snapshotCellHead
    );

    // SW trace carries no local CBV/SRV/UAV entries.
    BindingLayoutDesc swLayoutDesc(descArena);
    swLayoutDesc.setVisibility(ShaderType::Compute);
    swLayoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 14u));

    auto swLayout = device.createBindingLayout(swLayoutDesc);
    ASSERT_NE(swLayout.get(), nullptr);
    ASSERT_TRUE(swLayout->isDescriptorBufferCompatible())
        << "surfel SW trace shape is not compatible with descriptor-buffer pipelines";
    EXPECT_TRUE(swLayout->getDescriptorBufferBindingOffsets().empty());

    // HW trace differs only by its global TLAS heap layout; its local leaf is identical.
    BindingLayoutDesc hwLayoutDesc(descArena);
    hwLayoutDesc.setVisibility(ShaderType::Compute);
    hwLayoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 14u));

    auto hwLayout = device.createBindingLayout(hwLayoutDesc);
    ASSERT_NE(hwLayout.get(), nullptr);
    ASSERT_TRUE(hwLayout->isDescriptorBufferCompatible())
        << "surfel HW trace shape is not compatible with descriptor-buffer pipelines";
    EXPECT_TRUE(hwLayout->getDescriptorBufferBindingOffsets().empty());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Surfel upsample selects its half irradiance, G-buffer inputs, and storage output through the descriptor heap. Its
// local layout must remain a 14-u32 push-only selector block.
TEST_F(DescriptorBufferRoundTripTest, SurfelUpsampleShapeBuildsAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/surfel_upsample_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    auto makeUavTexture = [&](const u32 w, const u32 h) {
        return device.createTexture(
            TextureDesc()
                .setWidth(w).setHeight(h)
                .setFormat(Format::RGBA16_FLOAT)
                .setInUAV(true)
                .setInitialState(ResourceStates::UnorderedAccess)
                .setKeepInitialState(true)
        );
    };

    auto output = makeUavTexture(32u, 32u);
    ASSERT_TRUE(output);

    // The local ABI carries no UAVs; the output uses the storage-image heap.
    BindingLayoutDesc layoutDesc(descArena);
    layoutDesc.setVisibility(ShaderType::Compute);
    layoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 14u));

    auto layout = device.createBindingLayout(layoutDesc);
    ASSERT_NE(layout.get(), nullptr);

    ASSERT_TRUE(layout->isDescriptorBufferCompatible())
        << "surfel upsample shape is not compatible with descriptor-buffer pipelines";
    EXPECT_TRUE(layout->getDescriptorBufferBindingOffsets().empty());
}


// Hash-build's constants, pool, and cell-head are all persistent heap descriptors. Its local layout contains only the
// shared selector push range.
TEST_F(DescriptorBufferRoundTripTest, SurfelHashBuildShapeBuildsAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/surfel_hash_build_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    auto makeConstantBuffer = [&]() {
        return device.createBuffer(
            BufferDesc()
                .setByteSize(256u)
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
                .setCanHaveRawViews(true)
                .setInitialState(ResourceStates::Common)
                .setKeepInitialState(true)
        );
    };

    auto constants = makeConstantBuffer();
    auto pool = makeStructuredUav(16u);
    auto cellHead = makeStructuredUav(4u);
    ASSERT_TRUE(constants && pool && cellHead);

    BindingLayoutDesc layoutDesc(descArena);
    layoutDesc.setVisibility(ShaderType::Compute);
    layoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 14u));

    auto layout = device.createBindingLayout(layoutDesc);
    ASSERT_NE(layout.get(), nullptr);

    ASSERT_TRUE(layout->isDescriptorBufferCompatible())
        << "surfel hash-build shape is not compatible with descriptor-buffer pipelines";
    EXPECT_TRUE(layout->getDescriptorBufferBindingOffsets().empty());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
TEST_F(DescriptorBufferRoundTripTest, SurfelAgeFreeShapeBuildsAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/surfel_age_free_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    auto makeConstantBuffer = [&]() {
        return device.createBuffer(
            BufferDesc()
                .setByteSize(256u)
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
                .setCanHaveRawViews(true)
                .setInitialState(ResourceStates::Common)
                .setKeepInitialState(true)
        );
    };

    auto constants = makeConstantBuffer();
    auto pool = makeStructuredUav(16u);
    auto counter = makeStructuredUav(4u);
    auto freeList = makeStructuredUav(4u);
    ASSERT_TRUE(constants && pool && counter && freeList);

    // Constants, pool, counter, and free-list are selected by the shared heap-slot block.
    BindingLayoutDesc layoutDesc(descArena);
    layoutDesc.setVisibility(ShaderType::Compute);
    layoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 14u));

    auto layout = device.createBindingLayout(layoutDesc);
    ASSERT_NE(layout.get(), nullptr);

    ASSERT_TRUE(layout->isDescriptorBufferCompatible())
        << "surfel age-free shape is not compatible with descriptor-buffer pipelines";
    EXPECT_TRUE(layout->getDescriptorBufferBindingOffsets().empty());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
TEST_F(DescriptorBufferRoundTripTest, SurfelTraceBuildArgsShapeBuildsAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/surfel_trace_buildargs_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    auto makeConstantBuffer = [&]() {
        return device.createBuffer(
            BufferDesc()
                .setByteSize(256u)
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
                .setCanHaveRawViews(true)
                .setInitialState(ResourceStates::Common)
                .setKeepInitialState(true)
        );
    };

    auto constants = makeConstantBuffer();
    auto counter = makeStructuredUav(4u);
    auto args = makeStructuredUav(4u);
    ASSERT_TRUE(constants && counter && args);

    // Constants, counter, and indirect-argument output are heap descriptors.
    BindingLayoutDesc layoutDesc(descArena);
    layoutDesc.setVisibility(ShaderType::Compute);
    layoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 14u));

    auto layout = device.createBindingLayout(layoutDesc);
    ASSERT_NE(layout.get(), nullptr);

    ASSERT_TRUE(layout->isDescriptorBufferCompatible())
        << "surfel trace build-args shape is not compatible with descriptor-buffer pipelines";
    EXPECT_TRUE(layout->getDescriptorBufferBindingOffsets().empty());
}


// Surfel spawn selects both G-buffer inputs and all persistent surfel buffers through the descriptor heap. The local
// layout is only the common selector block.
TEST_F(DescriptorBufferRoundTripTest, SurfelSpawnShapeBuildsAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/surfel_spawn_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    auto makeConstantBuffer = [&]() {
        return device.createBuffer(
            BufferDesc()
                .setByteSize(256u)
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
                .setCanHaveRawViews(true)
                .setInitialState(ResourceStates::Common)
                .setKeepInitialState(true)
        );
    };
    auto constants = makeConstantBuffer();
    auto pool = makeStructuredUav(16u);
    auto cellHead = makeStructuredUav(4u);
    auto counter = makeStructuredUav(4u);
    auto freeList = makeStructuredUav(4u);
    ASSERT_TRUE(constants && pool && cellHead && counter && freeList);

    // No local persistent-buffer descriptors remain.
    BindingLayoutDesc layoutDesc(descArena);
    layoutDesc.setVisibility(ShaderType::Compute);
    layoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 14u));

    auto layout = device.createBindingLayout(layoutDesc);
    ASSERT_NE(layout.get(), nullptr);

    ASSERT_TRUE(layout->isDescriptorBufferCompatible())
        << "surfel spawn shape is not compatible with descriptor-buffer pipelines";
    EXPECT_TRUE(layout->getDescriptorBufferBindingOffsets().empty());
}


// Surfel resolve gathers persistent heap buffers and writes its half-resolution output through the storage-image heap.
// Its local layout is the same 14-u32 selector block as every other surfel pass.
TEST_F(DescriptorBufferRoundTripTest, SurfelResolveShapeBuildsAsDescriptorBuffer){
    auto& device = DescriptorBufferRoundTripTest::device();

    static constexpr Name kDescArenaName{"tests/descriptor_buffer/surfel_resolve_desc_arena"};
    Alloc::GlobalArena descArena{kDescArenaName};

    auto makeConstantBuffer = [&]() {
        return device.createBuffer(
            BufferDesc()
                .setByteSize(256u)
                .setIsConstantBuffer(true)
                .setInitialState(ResourceStates::ConstantBuffer)
                .setKeepInitialState(true)
        );
    };
    auto makeStructuredSrv = [&](const u32 stride) {
        return device.createBuffer(
            BufferDesc()
                .setByteSize(stride * 4096u)
                .setStructStride(stride)
                .setCanHaveRawViews(true)
                .setInitialState(ResourceStates::ShaderResource)
                .setKeepInitialState(true)
        );
    };
    auto makeUavTexture = [&](const u32 w, const u32 h) {
        return device.createTexture(
            TextureDesc()
                .setWidth(w).setHeight(h)
                .setFormat(Format::RGBA16_FLOAT)
                .setInUAV(true)
                .setInitialState(ResourceStates::UnorderedAccess)
                .setKeepInitialState(true)
        );
    };

    auto constants = makeConstantBuffer();
    auto pool = makeStructuredSrv(16u);
    auto cellHead = makeStructuredSrv(4u);
    auto output = makeUavTexture(32u, 32u);
    ASSERT_TRUE(constants && pool && cellHead && output);

    // No local CBV/SRV/UAV entries remain.
    BindingLayoutDesc layoutDesc(descArena);
    layoutDesc.setVisibility(ShaderType::Compute);
    layoutDesc.addItem(BindingLayoutItem::PushConstants(0u, sizeof(u32) * 14u));

    auto layout = device.createBindingLayout(layoutDesc);
    ASSERT_NE(layout.get(), nullptr);

    ASSERT_TRUE(layout->isDescriptorBufferCompatible())
        << "surfel resolve shape is not compatible with descriptor-buffer pipelines";
    EXPECT_TRUE(layout->getDescriptorBufferBindingOffsets().empty());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

