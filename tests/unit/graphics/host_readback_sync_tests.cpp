// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <core/graphics/vulkan/host_readback_sync.h>
#include <core/graphics/vulkan/state_tracking_detail.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_host_readback_sync_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core;
namespace HostSync = Core::GraphicsBackend::VulkanDetail;
namespace StateTracking = Core::GraphicsBackend::VulkanStateTrackingDetail;

inline constexpr Name s_HostReadbackTestArena("tests/graphics/host_readback_sync");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(HostReadbackSync, ClassifiesEveryResourceStateBit){
    struct StateCase{
        ResourceStates::Mask state;
        bool writesBuffer;
    };
    constexpr StateCase s_Cases[] = {
        { ResourceStates::Common, false },
        { ResourceStates::ConstantBuffer, false },
        { ResourceStates::VertexBuffer, false },
        { ResourceStates::IndexBuffer, false },
        { ResourceStates::IndirectArgument, false },
        { ResourceStates::ShaderResource, false },
        { ResourceStates::UnorderedAccess, true },
        { ResourceStates::RenderTarget, false },
        { ResourceStates::DepthWrite, false },
        { ResourceStates::DepthRead, false },
        { ResourceStates::StreamOut, true },
        { ResourceStates::CopyDest, true },
        { ResourceStates::CopySource, false },
        { ResourceStates::ResolveDest, true },
        { ResourceStates::ResolveSource, false },
        { ResourceStates::Present, false },
        { ResourceStates::AccelStructRead, false },
        { ResourceStates::AccelStructWrite, true },
        { ResourceStates::AccelStructBuildInput, false },
        { ResourceStates::AccelStructBuildBlas, true },
        { ResourceStates::ShadingRateSurface, false },
        { ResourceStates::OpacityMicromapWrite, true },
        { ResourceStates::OpacityMicromapBuildInput, false },
        { ResourceStates::ConvertCoopVecMatrixInput, false },
        { ResourceStates::ConvertCoopVecMatrixOutput, true },
    };

    EXPECT_FALSE(HostSync::HasBufferDeviceWriteState(ResourceStates::Unknown));
    for(const StateCase& stateCase : s_Cases)
        EXPECT_EQ(HostSync::HasBufferDeviceWriteState(stateCase.state), stateCase.writesBuffer);
    EXPECT_TRUE(HostSync::HasBufferDeviceWriteState(
        ResourceStates::ShaderResource | ResourceStates::CopyDest
    ));
}

TEST(HostReadbackSync, BuildsExactWholeBufferHostDependency){
    const VkBuffer buffer = reinterpret_cast<VkBuffer>(static_cast<usize>(0x1234u));
    const VkBufferMemoryBarrier2 barrier = HostSync::BuildHostReadBufferBarrier(buffer);

    EXPECT_EQ(barrier.sType, VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2);
    EXPECT_EQ(barrier.pNext, nullptr);
    EXPECT_EQ(barrier.srcStageMask, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
    EXPECT_EQ(barrier.srcAccessMask, VK_ACCESS_2_MEMORY_WRITE_BIT);
    EXPECT_EQ(barrier.dstStageMask, VK_PIPELINE_STAGE_2_HOST_BIT);
    EXPECT_EQ(barrier.dstAccessMask, VK_ACCESS_2_HOST_READ_BIT);
    EXPECT_EQ(barrier.srcQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED);
    EXPECT_EQ(barrier.dstQueueFamilyIndex, VK_QUEUE_FAMILY_IGNORED);
    EXPECT_EQ(barrier.buffer, buffer);
    EXPECT_EQ(barrier.offset, 0u);
    EXPECT_EQ(barrier.size, VK_WHOLE_SIZE);
}

TEST(HostReadbackSync, CollectsEveryExactQueueFamilyWithoutLegacyLaneGatesOrFixedCapacity){
    constexpr GpuPhysicalQueueInfo s_Queues[] = {
        { .id = {}, .queueClass = CommandQueue::Graphics, .capabilities = GpuQueueCapability::Graphics,
            .familyIndex = 2u },
        { .id = {}, .queueClass = CommandQueue::Graphics, .capabilities = GpuQueueCapability::Graphics,
            .familyIndex = 4u },
        { .id = {}, .queueClass = CommandQueue::Compute, .capabilities = GpuQueueCapability::Compute,
            .familyIndex = 2u },
        { .id = {}, .queueClass = CommandQueue::Compute, .capabilities = GpuQueueCapability::Compute,
            .familyIndex = 6u },
        { .id = {}, .queueClass = CommandQueue::Transfer, .capabilities = GpuQueueCapability::Transfer,
            .familyIndex = 8u },
        { .id = {}, .queueClass = CommandQueue::Transfer, .capabilities = GpuQueueCapability::Transfer,
            .familyIndex = 10u },
        { .id = {}, .queueClass = CommandQueue::Graphics, .capabilities = GpuQueueCapability::Graphics,
            .familyIndex = 12u },
        { .id = {}, .queueClass = CommandQueue::Compute, .capabilities = GpuQueueCapability::Compute,
            .familyIndex = 14u },
        { .id = {}, .queueClass = CommandQueue::Transfer, .capabilities = GpuQueueCapability::Transfer,
            .familyIndex = 16u },
        { .id = {}, .queueClass = CommandQueue::Graphics, .capabilities = GpuQueueCapability::Graphics,
            .familyIndex = 18u },
        { .id = {}, .familyIndex = VK_QUEUE_FAMILY_IGNORED },
    };
    constexpr u32 s_ExpectedFamilies[] = { 2u, 4u, 6u, 8u, 10u, 12u, 14u, 16u, 18u };
    Alloc::ScratchArena scratchArena(s_HostReadbackTestArena);
    Vector<u32, Alloc::ScratchArena> familyIndices(scratchArena);

    HostSync::CollectUniquePhysicalQueueFamilyIndices(GpuPhysicalQueueTopology{}, familyIndices);
    EXPECT_TRUE(familyIndices.empty());
    HostSync::CollectUniquePhysicalQueueFamilyIndices(
        GpuPhysicalQueueTopology{ s_Queues, LengthOf(s_Queues) },
        familyIndices
    );
    ASSERT_EQ(familyIndices.size(), LengthOf(s_ExpectedFamilies));
    for(usize familyIndex = 0u; familyIndex < familyIndices.size(); ++familyIndex)
        EXPECT_EQ(familyIndices[familyIndex], s_ExpectedFamilies[familyIndex]);
}

TEST(HostReadbackSync, BuildsCheckedPerPhysicalQueueBreadcrumbLayout){
    Array<GpuPhysicalQueueInfo, 9u> queues{};
    for(usize queueIndex = 0u; queueIndex < queues.size(); ++queueIndex){
        queues[queueIndex].id.index = static_cast<u16>(queueIndex);
        queues[queueIndex].id.deviceGeneration = 17u;
        // Distinct exact queues may intentionally share one Vulkan family.
        queues[queueIndex].familyIndex = 4u;
    }
    const GpuPhysicalQueueTopology topology{ queues.data(), queues.size() };
    HostSync::AmdBreadcrumbRingLayout layout;

    EXPECT_FALSE(HostSync::TryBuildAmdBreadcrumbRingLayout({}, 256u, layout));
    EXPECT_FALSE(HostSync::TryBuildAmdBreadcrumbRingLayout(topology, 0u, layout));
    EXPECT_FALSE(HostSync::TryBuildAmdBreadcrumbRingLayout(
        GpuPhysicalQueueTopology{ queues.data(), 2u },
        Limit<usize>::s_Max,
        layout
    ));
    EXPECT_FALSE(HostSync::TryBuildAmdBreadcrumbRingLayout(
        GpuPhysicalQueueTopology{ queues.data(), 1u },
        Limit<usize>::s_Max / sizeof(u32) + 1u,
        layout
    ));

    ASSERT_TRUE(HostSync::TryBuildAmdBreadcrumbRingLayout(topology, 256u, layout));
    EXPECT_EQ(layout.deviceGeneration, 17u);
    EXPECT_EQ(layout.physicalQueueCount, queues.size());
    EXPECT_EQ(layout.slotsPerQueue, 256u);
    EXPECT_EQ(layout.totalSlotCount, queues.size() * 256u);
    EXPECT_EQ(layout.totalByteSize, queues.size() * 256u * sizeof(u32));

    queues[4u].id.index = 8u;
    EXPECT_FALSE(HostSync::TryBuildAmdBreadcrumbRingLayout(topology, 256u, layout));
    queues[4u].id.index = 4u;
    queues[4u].id.deviceGeneration = 18u;
    EXPECT_FALSE(HostSync::TryBuildAmdBreadcrumbRingLayout(topology, 256u, layout));
}

TEST(HostReadbackSync, MapsQueueLocalBreadcrumbSlotsIntoDisjointExactQueueRegions){
    Array<GpuPhysicalQueueInfo, 9u> queues{};
    for(usize queueIndex = 0u; queueIndex < queues.size(); ++queueIndex){
        queues[queueIndex].id.index = static_cast<u16>(queueIndex);
        queues[queueIndex].id.deviceGeneration = 23u;
        queues[queueIndex].familyIndex = 6u;
    }
    const GpuPhysicalQueueTopology topology{ queues.data(), queues.size() };
    HostSync::AmdBreadcrumbRingLayout layout;
    ASSERT_TRUE(HostSync::TryBuildAmdBreadcrumbRingLayout(topology, 256u, layout));

    usize previousLastSlot = 0u;
    for(usize queueIndex = 0u; queueIndex < queues.size(); ++queueIndex){
        usize firstSlot = 0u;
        usize lastSlot = 0u;
        VkDeviceSize byteOffset = 0u;
        ASSERT_TRUE(HostSync::TryResolveAmdBreadcrumbRingSlot(
            layout,
            queues[queueIndex].id,
            0u,
            firstSlot,
            byteOffset
        ));
        EXPECT_EQ(firstSlot, queueIndex * 256u);
        EXPECT_EQ(byteOffset, firstSlot * sizeof(u32));
        ASSERT_TRUE(HostSync::TryResolveAmdBreadcrumbRingSlot(
            layout,
            queues[queueIndex].id,
            255u,
            lastSlot,
            byteOffset
        ));
        EXPECT_EQ(lastSlot, queueIndex * 256u + 255u);
        if(queueIndex != 0u)
            EXPECT_GT(firstSlot, previousLastSlot);
        previousLastSlot = lastSlot;
    }

    usize flatSlot = 0u;
    VkDeviceSize byteOffset = 0u;
    GpuPhysicalQueueId invalidQueue;
    EXPECT_FALSE(HostSync::TryResolveAmdBreadcrumbRingSlot(layout, invalidQueue, 0u, flatSlot, byteOffset));
    invalidQueue = queues[0u].id;
    invalidQueue.deviceGeneration = 24u;
    EXPECT_FALSE(HostSync::TryResolveAmdBreadcrumbRingSlot(layout, invalidQueue, 0u, flatSlot, byteOffset));
    invalidQueue = queues[0u].id;
    invalidQueue.index = static_cast<u16>(queues.size());
    EXPECT_FALSE(HostSync::TryResolveAmdBreadcrumbRingSlot(layout, invalidQueue, 0u, flatSlot, byteOffset));
    EXPECT_FALSE(HostSync::TryResolveAmdBreadcrumbRingSlot(layout, queues[0u].id, 256u, flatSlot, byteOffset));
}

TEST(HostReadbackSync, BuildsPairUniqueBreadcrumbReservationsUntilHonestTerminalBoundary){
    constexpr usize s_SlotsPerQueue = 256u;
    constexpr u64 s_MaximumSerial = static_cast<u64>(s_SlotsPerQueue) * Limit<u32>::s_Max;
    HostSync::AmdBreadcrumbReservation reservation;

    ASSERT_TRUE(HostSync::TryBuildNextAmdBreadcrumbReservation(0u, s_SlotsPerQueue, reservation));
    EXPECT_EQ(reservation.serial, 1u);
    EXPECT_EQ(reservation.marker, 1u);
    EXPECT_EQ(reservation.localSlot, 0u);

    ASSERT_TRUE(HostSync::TryBuildNextAmdBreadcrumbReservation(255u, s_SlotsPerQueue, reservation));
    EXPECT_EQ(reservation.serial, 256u);
    EXPECT_EQ(reservation.marker, 1u);
    EXPECT_EQ(reservation.localSlot, 255u);

    ASSERT_TRUE(HostSync::TryBuildNextAmdBreadcrumbReservation(256u, s_SlotsPerQueue, reservation));
    EXPECT_EQ(reservation.serial, 257u);
    EXPECT_EQ(reservation.marker, 2u);
    EXPECT_EQ(reservation.localSlot, 0u);

    ASSERT_TRUE(HostSync::TryBuildNextAmdBreadcrumbReservation(
        s_MaximumSerial - 1u,
        s_SlotsPerQueue,
        reservation
    ));
    EXPECT_EQ(reservation.serial, s_MaximumSerial);
    EXPECT_EQ(reservation.marker, Limit<u32>::s_Max);
    EXPECT_EQ(reservation.localSlot, 255u);

    EXPECT_FALSE(HostSync::TryBuildNextAmdBreadcrumbReservation(
        s_MaximumSerial,
        s_SlotsPerQueue,
        reservation
    ));
    EXPECT_EQ(reservation.serial, 0u);
    EXPECT_EQ(reservation.marker, 0u);
    EXPECT_EQ(reservation.localSlot, 0u);
    EXPECT_FALSE(HostSync::TryBuildNextAmdBreadcrumbReservation(0u, 0u, reservation));
}

TEST(HostReadbackSync, RejectsStaleBreadcrumbObservationBeforeReusedSlotWrite){
    HostSync::AmdBreadcrumbReservation firstReservation;
    HostSync::AmdBreadcrumbReservation reusedReservation;
    ASSERT_TRUE(HostSync::TryBuildNextAmdBreadcrumbReservation(0u, 256u, firstReservation));
    ASSERT_TRUE(HostSync::TryBuildNextAmdBreadcrumbReservation(256u, 256u, reusedReservation));
    ASSERT_EQ(firstReservation.localSlot, reusedReservation.localSlot);
    ASSERT_NE(firstReservation.marker, reusedReservation.marker);

    EXPECT_TRUE(HostSync::MatchesAmdBreadcrumbObservation(firstReservation.marker, firstReservation.marker));
    EXPECT_FALSE(HostSync::MatchesAmdBreadcrumbObservation(firstReservation.marker, reusedReservation.marker));
    EXPECT_FALSE(HostSync::MatchesAmdBreadcrumbObservation(0u, reusedReservation.marker));
}

TEST(HostReadbackSync, DeduplicatesNativeBuffersAndAppendsOneBarrierEach){
    Alloc::GlobalArena arena(s_HostReadbackTestArena);
    HostSync::HostReadbackBarrierTracker tracker(arena);
    Vector<VkBufferMemoryBarrier2, Alloc::GlobalArena> barriers(arena);
    const VkBuffer first = reinterpret_cast<VkBuffer>(static_cast<usize>(0x1000u));
    const VkBuffer second = reinterpret_cast<VkBuffer>(static_cast<usize>(0x2000u));

    EXPECT_FALSE(tracker.registerBuffer(VK_NULL_HANDLE));
    EXPECT_TRUE(tracker.registerBuffer(first));
    EXPECT_FALSE(tracker.registerBuffer(first));
    EXPECT_TRUE(tracker.registerBuffer(second));
    EXPECT_EQ(tracker.size(), 2u);
#if !defined(NWB_FINAL)
    HostSync::ResetHostReadbackBarrierAppendCountForTesting();
#endif
    tracker.appendBarriers(barriers);
    ASSERT_EQ(barriers.size(), 2u);
    EXPECT_EQ(barriers[0u].buffer, first);
    EXPECT_EQ(barriers[1u].buffer, second);
#if !defined(NWB_FINAL)
    EXPECT_EQ(HostSync::GetHostReadbackBarrierAppendCountForTesting(), 2u);
#endif

    tracker.clear();
    EXPECT_EQ(tracker.size(), 0u);
    tracker.registerDeviceOwnedBuffer(first);
    tracker.registerDeviceOwnedBuffer(first);
    EXPECT_EQ(tracker.size(), 1u);
}

TEST(HostReadbackSync, UniversalHostScopesSurviveEveryExactQueueClass){
    constexpr GpuQueueCapability::Mask s_Capabilities[] = {
        GpuQueueCapability::Graphics,
        GpuQueueCapability::Compute,
        GpuQueueCapability::Transfer,
    };

    for(const GpuQueueCapability::Mask capabilities : s_Capabilities){
        VkPipelineStageFlags2 sourceStage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        VkAccessFlags2 sourceAccess = VK_ACCESS_2_MEMORY_WRITE_BIT;
        StateTracking::NormalizeBarrierScopeForQueueCapabilities(capabilities, sourceStage, sourceAccess);
        EXPECT_EQ(sourceStage, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
        EXPECT_EQ(sourceAccess, VK_ACCESS_2_MEMORY_WRITE_BIT);

        VkPipelineStageFlags2 destinationStage = VK_PIPELINE_STAGE_2_HOST_BIT;
        VkAccessFlags2 destinationAccess = VK_ACCESS_2_HOST_READ_BIT;
        StateTracking::NormalizeBarrierScopeForQueueCapabilities(
            capabilities,
            destinationStage,
            destinationAccess
        );
        EXPECT_EQ(destinationStage, VK_PIPELINE_STAGE_2_HOST_BIT);
        EXPECT_EQ(destinationAccess, VK_ACCESS_2_HOST_READ_BIT);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

