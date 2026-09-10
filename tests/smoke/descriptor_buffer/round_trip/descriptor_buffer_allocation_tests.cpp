// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Both global segments must report a non-zero device address; later cases depend on it.
TEST_F(DescriptorBufferRoundTripTest, ManagerEnabledAndSegmentsMapped){
    auto& mgr = manager();

    ASSERT_TRUE(mgr.isEnabled());
    EXPECT_NE(mgr.getResourceBindingInfo().address, 0u);
    EXPECT_NE(mgr.getSamplerBindingInfo().address, 0u);
    EXPECT_EQ(mgr.getResourceBufferIndex(), 0u);
    EXPECT_EQ(mgr.getSamplerBufferIndex(), 1u);
}


// Storage buffers are the most representative round trip.
TEST_F(DescriptorBufferAllocationTest, RoundTripsStorageBufferDescriptor){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& mgr = manager();

    auto storageBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(4096u)
            .setStructStride(16u)
            .setCanHaveRawViews(true)
            .setCanHaveUAVs(true)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    ASSERT_NE(storageBuffer.get(), nullptr);

    const u32 descriptorSize = mgr.getDescriptorSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    ASSERT_GT(descriptorSize, 0u);

    const auto segment = mgr.allocate(GraphicsBackend::DescriptorBufferSegmentKind::Resource, descriptorSize, mgr.getOffsetAlignmentBytes());
    ASSERT_TRUE(segment.valid());

    // writeDescriptor's return is the round-trip signal; segment bytes stay manager-private.
    const DescriptorWriteItem item = DescriptorWriteItem::RawBuffer_UAV(0u, storageBuffer.get());
    const bool wrote = mgr.writeDescriptor(item, segment, segment.offsetBytes, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    EXPECT_TRUE(wrote);

    mgr.free(segment);
}


// Uniform (constant) buffer descriptor: same carve/write/free shape, different VkDescriptorType arm. Ensures the
// manager routes UNIFORM_BUFFER through VkDescriptorAddressInfoEXT (the non-texel buffer-info path).
TEST_F(DescriptorBufferAllocationTest, RoundTripsUniformBufferDescriptor){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& mgr = manager();

    auto constantBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setIsConstantBuffer(true)
            .setInitialState(ResourceStates::ConstantBuffer)
            .setKeepInitialState(true)
    );
    ASSERT_NE(constantBuffer.get(), nullptr);

    const u32 descriptorSize = mgr.getDescriptorSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    ASSERT_GT(descriptorSize, 0u);

    const auto segment = mgr.allocate(GraphicsBackend::DescriptorBufferSegmentKind::Resource, descriptorSize, mgr.getOffsetAlignmentBytes());
    ASSERT_TRUE(segment.valid());

    const DescriptorWriteItem item = DescriptorWriteItem::ConstantBuffer(0u, constantBuffer.get());
    const bool wrote = mgr.writeDescriptor(item, segment, segment.offsetBytes, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    EXPECT_TRUE(wrote);

    mgr.free(segment);
}


// Sampled-image descriptor: the Texture_SRV path, which routes through VkDescriptorImageInfo (image view + layout).
// Together with storage/uniform buffers, this covers the descriptor classes the shadow/GI/caustics passes bind.
TEST_F(DescriptorBufferAllocationTest, RoundTripsSampledImageDescriptor){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& mgr = manager();

    auto texture = device.createTexture(
        TextureDesc()
            .setWidth(64u)
            .setHeight(64u)
            .setFormat(Format::RGBA8_UNORM)
            .setInitialState(ResourceStates::ShaderResource)
            .setKeepInitialState(true)
    );
    ASSERT_NE(texture.get(), nullptr);

    const u32 descriptorSize = mgr.getDescriptorSize(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    ASSERT_GT(descriptorSize, 0u);

    const auto segment = mgr.allocate(GraphicsBackend::DescriptorBufferSegmentKind::Resource, descriptorSize, mgr.getOffsetAlignmentBytes());
    ASSERT_TRUE(segment.valid());

    const DescriptorWriteItem item = DescriptorWriteItem::Texture_SRV(0u, texture.get());
    const bool wrote = mgr.writeDescriptor(item, segment, segment.offsetBytes, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    EXPECT_TRUE(wrote);

    mgr.free(segment);
}


// Sampler descriptor: the one class that lives in the separate SAMPLER segment (RADV requires samplers in their own
// descriptor buffer binding). Verifies the kind routing places the carve in the sampler segment, not the resource one.
TEST_F(DescriptorBufferAllocationTest, RoundTripsSamplerDescriptor){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& mgr = manager();

    auto sampler = device.createSampler(SamplerDesc().setAllFilters(true));
    ASSERT_NE(sampler.get(), nullptr);

    const u32 descriptorSize = mgr.getDescriptorSize(VK_DESCRIPTOR_TYPE_SAMPLER);
    ASSERT_GT(descriptorSize, 0u);

    const auto segment = mgr.allocate(GraphicsBackend::DescriptorBufferSegmentKind::Sampler, descriptorSize, mgr.getOffsetAlignmentBytes());
    ASSERT_TRUE(segment.valid());
    EXPECT_EQ(segment.kind, GraphicsBackend::DescriptorBufferSegmentKind::Sampler);

    const DescriptorWriteItem item = DescriptorWriteItem::Sampler(0u, sampler.get());
    const bool wrote = mgr.writeDescriptor(item, segment, segment.offsetBytes, VK_DESCRIPTOR_TYPE_SAMPLER);
    EXPECT_TRUE(wrote);

    mgr.free(segment);
}


// Free-list reuse: allocate a range, free it, allocate the same size again — the second carve must be satisfied from
// the free list and succeed. This proves the sub-allocator remains safe under live allocate/free churn across frames.
TEST_F(DescriptorBufferAllocationTest, FreeListReusesFreedRange){
    auto& mgr = manager();

    const u32 descriptorSize = mgr.getDescriptorSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    ASSERT_GT(descriptorSize, 0u);

    const auto first = mgr.allocate(GraphicsBackend::DescriptorBufferSegmentKind::Resource, descriptorSize, mgr.getOffsetAlignmentBytes());
    ASSERT_TRUE(first.valid());

    mgr.free(first);

    const auto second = mgr.allocate(GraphicsBackend::DescriptorBufferSegmentKind::Resource, descriptorSize, mgr.getOffsetAlignmentBytes());
    ASSERT_TRUE(second.valid());
    EXPECT_EQ(second.offsetBytes, first.offsetBytes);

    mgr.free(second);
}


// Descriptor array elements advance by the driver-reported descriptor size. Every type the conversion routes through
// writeDescriptor must report a non-zero size; a zero size would make allocation and writes reject the descriptor.
TEST_F(DescriptorBufferRoundTripTest, EveryDescriptorTypeReportsNonZeroSize){
    auto& mgr = manager();

    static constexpr VkDescriptorType kTypes[] = {
        VK_DESCRIPTOR_TYPE_SAMPLER,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    };

    for(const VkDescriptorType type : kTypes){
        EXPECT_GT(mgr.getDescriptorSize(type), 0u)
            << "descriptor type " << static_cast<u32>(type) << " reported a zero size";
    }
}


// Alignment: carve two adjacent ranges and confirm each block offset is descriptorBufferOffsetAlignment-aligned, and
// the second does not overlap the first. That is the invariant vkCmdSetDescriptorBufferOffsetsEXT must honor.
TEST_F(DescriptorBufferAllocationTest, AllocationsAreDescriptorBufferOffsetAligned){
    auto& mgr = manager();

    const u32 descriptorSize = mgr.getDescriptorSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    const u32 offsetAlignment = mgr.getOffsetAlignmentBytes();
    ASSERT_GT(descriptorSize, 0u);
    ASSERT_GT(offsetAlignment, 0u);

    const auto a = mgr.allocate(GraphicsBackend::DescriptorBufferSegmentKind::Resource, descriptorSize, offsetAlignment);
    const auto b = mgr.allocate(GraphicsBackend::DescriptorBufferSegmentKind::Resource, descriptorSize, offsetAlignment);
    ASSERT_TRUE(a.valid());
    ASSERT_TRUE(b.valid());

    EXPECT_EQ(a.offsetBytes % offsetAlignment, 0u);
    EXPECT_EQ(b.offsetBytes % offsetAlignment, 0u);
    EXPECT_GE(b.offsetBytes, a.offsetBytes + a.sizeBytes);

    mgr.free(a);
    mgr.free(b);
}


// The manager's public byte-write entry point must not reinterpret a resource payload through the wrong
// VkDescriptorDataEXT union arm, and allocation/free inputs must preserve the block-ownership invariant.
TEST_F(DescriptorBufferAllocationTest, ManagerRejectsMismatchedWritesAndInvalidBlocks){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& mgr = manager();

    auto storageBuffer = device.createBuffer(
        BufferDesc()
            .setByteSize(4096u)
            .setStructStride(16u)
            .setCanHaveRawViews(true)
            .setCanHaveUAVs(true)
            .setInitialState(ResourceStates::Common)
            .setKeepInitialState(true)
    );
    ASSERT_TRUE(storageBuffer);

    const u32 descriptorSize = mgr.getDescriptorSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    ASSERT_GT(descriptorSize, 0u);

    // Invalid public descriptor-buffer operations are diagnostic contract failures in developer builds: the error
    // logger deliberately raises a soft break after rejecting them. Exercise that policy in child processes so this
    // suite can validate it without stopping the parent test run; final builds still verify the ordinary false return.
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        EXPECT_FALSE(mgr.allocate(
            static_cast<GraphicsBackend::DescriptorBufferSegmentKind::Enum>(0xffu),
            descriptorSize,
            mgr.getOffsetAlignmentBytes()
        ).valid());
    }, "");
    EXPECT_DEATH_IF_SUPPORTED({
        EXPECT_FALSE(mgr.allocate(
            GraphicsBackend::DescriptorBufferSegmentKind::Resource,
            descriptorSize,
            0u
        ).valid());
    }, "");
#else
    EXPECT_FALSE(mgr.allocate(
        static_cast<GraphicsBackend::DescriptorBufferSegmentKind::Enum>(0xffu),
        descriptorSize,
        mgr.getOffsetAlignmentBytes()
    ).valid());
    EXPECT_FALSE(mgr.allocate(
        GraphicsBackend::DescriptorBufferSegmentKind::Resource,
        descriptorSize,
        0u
    ).valid());
#endif

    const auto segment = mgr.allocate(
        GraphicsBackend::DescriptorBufferSegmentKind::Resource,
        descriptorSize,
        mgr.getOffsetAlignmentBytes()
    );
    ASSERT_TRUE(segment.valid());

    const DescriptorWriteItem item = DescriptorWriteItem::RawBuffer_UAV(0u, storageBuffer.get());
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        EXPECT_FALSE(mgr.writeDescriptor(item, segment, segment.offsetBytes, VK_DESCRIPTOR_TYPE_SAMPLER));
    }, "");
#else
    EXPECT_FALSE(mgr.writeDescriptor(item, segment, segment.offsetBytes, VK_DESCRIPTOR_TYPE_SAMPLER));
#endif
    EXPECT_TRUE(mgr.writeDescriptor(item, segment, segment.offsetBytes, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER));

    mgr.free(segment);
    // A duplicate free must be rejected instead of inserting a second copy of the range into the free list.
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        mgr.free(segment);
    }, "");
#else
    mgr.free(segment);
#endif

    // Reusing the manager after a free gives the new owner a different allocation serial. A stale segment copy must
    // not be allowed to write even if a future free-list allocation happens to recycle its byte range.
    const auto replacement = mgr.allocate(
        GraphicsBackend::DescriptorBufferSegmentKind::Resource,
        descriptorSize,
        mgr.getOffsetAlignmentBytes()
    );
    ASSERT_TRUE(replacement.valid());
    EXPECT_NE(replacement.allocationSerial, segment.allocationSerial);
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        EXPECT_FALSE(mgr.writeDescriptor(item, segment, segment.offsetBytes, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER));
    }, "");
#else
    EXPECT_FALSE(mgr.writeDescriptor(item, segment, segment.offsetBytes, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER));
#endif
    EXPECT_TRUE(mgr.writeDescriptor(item, replacement, replacement.offsetBytes, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER));
    mgr.free(replacement);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

