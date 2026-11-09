// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <core/graphics/vulkan/queue_barrier_scope_policy.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


namespace Tests{


TEST(QueueBarrierScopePolicy, TransferQueueRemovesUnsupportedShaderAccess){
    VkPipelineStageFlags2 stages = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_COPY_BIT;
    VkAccessFlags2 access = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT;
    Core::GraphicsBackend::QueueBarrierScopePolicy::Normalize(Core::GpuQueueCapability::Transfer, stages, access);
    EXPECT_EQ(stages, VK_PIPELINE_STAGE_2_COPY_BIT);
    EXPECT_EQ(access, VK_ACCESS_2_TRANSFER_READ_BIT);

    stages = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    access = VK_ACCESS_2_SHADER_READ_BIT;
    Core::GraphicsBackend::QueueBarrierScopePolicy::Normalize(Core::GpuQueueCapability::Transfer, stages, access);
    EXPECT_EQ(stages, VK_PIPELINE_STAGE_2_NONE);
    EXPECT_EQ(access, 0u);
}


};


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/graphics/vulkan/blas_opacity_micromap_policy.h>
#include <core/alloc/scratch.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_blas_opacity_micromap_policy_tests{

using namespace NWB;
using namespace NWB::Core;
using namespace NWB::Core::GraphicsBackend::VulkanDetail;

TEST(GpuBlasOpacityMicromapPolicyTests, AttachmentSkipsAbsentAndHandlesEmptyUsageCounts){
    Alloc::ScratchArena arena(NWB::Tests::s_TestArena);
    RayTracingGeometryDesc geometries[3];
    geometries[2].geometryData.triangles.numOmmUsageCounts = 2u;
    OpacityMicromapUsageOffsetVector offsets(arena);
    offsets.resize(3u);
    offsets[0] = Limit<usize>::s_Max;
    offsets[1] = offsets[2] = 0u;
    OpacityMicromapUsageVector usages(arena);
    usages.resize(2u);
    OpacityMicromapGeometryVector micromaps(arena);
    micromaps.resize(3u);
    BlasGeometryScratch scratch(arena);
    scratch.resizeForSizeQuery(3u);
    BlasOpacityMicromapPolicy::AttachBlasOpacityMicromapGeometries(geometries, 3u, offsets, usages, micromaps, scratch);
    EXPECT_EQ(scratch.geometries[0].geometry.triangles.pNext, nullptr);
    EXPECT_EQ(scratch.geometries[1].geometry.triangles.pNext, &micromaps[1]);
    EXPECT_EQ(micromaps[1].pUsageCounts, nullptr);
    EXPECT_EQ(scratch.geometries[2].geometry.triangles.pNext, &micromaps[2]);
    EXPECT_EQ(micromaps[2].pUsageCounts, usages.data());
    const auto implicit = BlasOpacityMicromapPolicy::ResolveOpacityMicromapIndexFormat(Format::UNKNOWN, {});
    ASSERT_TRUE(implicit);
    EXPECT_EQ(implicit->indexType, VK_INDEX_TYPE_NONE_KHR);
    EXPECT_EQ(implicit->indexStride, 0u);
}

}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_GRAPHICS_BACKEND_VULKAN) && NWB_GRAPHICS_BACKEND_VULKAN

#include <tests/common/test_context.h>

#include <gtest/gtest.h>

#include <core/graphics/vulkan/calibrated_timestamp_probe.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_calibrated_timestamp_probe_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static u32 s_CountCalls = 0u;
static u32 s_ListCalls = 0u;
static u32 s_TimestampCalls = 0u;
static b8 s_RetryFirstCount = false;
static b8 s_HasDeviceDomain = false;

VKAPI_ATTR VkResult VKAPI_CALL EnumerateDomains([[maybe_unused]] VkPhysicalDevice physicalDevice, u32* count, VkTimeDomainKHR* domains){
    if(!domains){
        ++s_CountCalls;
        *count = 1u;
        return s_RetryFirstCount && s_CountCalls == 1u ? VK_INCOMPLETE : VK_SUCCESS;
    }
    ++s_ListCalls;
    *count = 1u;
    domains[0] = s_HasDeviceDomain ? VK_TIME_DOMAIN_DEVICE_KHR : VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL GetTimestamps([[maybe_unused]] VkDevice device, [[maybe_unused]] u32 count, [[maybe_unused]] const VkCalibratedTimestampInfoKHR* infos, u64* timestamps, u64* maxDeviation){
    ++s_TimestampCalls;
    *timestamps = 1u;
    *maxDeviation = 0u;
    return VK_SUCCESS;
}

TEST(CalibratedTimestampProbe, RetriesIncompleteEnumerationBeforeDeviceQuery){
    s_CountCalls = 0u;
    s_ListCalls = 0u;
    s_TimestampCalls = 0u;
    s_RetryFirstCount = true;
    s_HasDeviceDomain = true;
    Alloc::ScratchArena scratch(NWB::Tests::s_TestArena);
    EXPECT_TRUE(VulkanDetail::CalibratedTimestampProbe::Query(VK_NULL_HANDLE, VK_NULL_HANDLE, &EnumerateDomains, &GetTimestamps, scratch));
    EXPECT_EQ(s_CountCalls, 2u);
    EXPECT_EQ(s_ListCalls, 1u);
    EXPECT_EQ(s_TimestampCalls, 1u);
}

TEST(CalibratedTimestampProbe, MissingDeviceDomainDoesNotQueryTimestamps){
    s_CountCalls = 0u;
    s_ListCalls = 0u;
    s_TimestampCalls = 0u;
    s_RetryFirstCount = false;
    s_HasDeviceDomain = false;
    Alloc::ScratchArena scratch(NWB::Tests::s_TestArena);
    EXPECT_FALSE(VulkanDetail::CalibratedTimestampProbe::Query(VK_NULL_HANDLE, VK_NULL_HANDLE, &EnumerateDomains, &GetTimestamps, scratch));
    EXPECT_EQ(s_CountCalls, 1u);
    EXPECT_EQ(s_ListCalls, 1u);
    EXPECT_EQ(s_TimestampCalls, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_GRAPHICS_BACKEND_VULKAN) && NWB_GRAPHICS_BACKEND_VULKAN

#include <core/graphics/vulkan/cluster_operation_input.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GraphicsBackend{
namespace __hidden_gpu_cluster_operation_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuClusterOperation, NativeInputRebindsUnionPointersAfterCopy){
    for(const auto type : {
        RayTracingClusterOperationType::Move,
        RayTracingClusterOperationType::ClasBuild,
        RayTracingClusterOperationType::ClasBuildTemplates,
        RayTracingClusterOperationType::ClasInstantiateTemplates,
        RayTracingClusterOperationType::BlasBuild
    }){
        RayTracingClusterOperationParams params;
        params.type = type;
        params.maxArgCount = 7u;
        const auto built = VulkanDetail::ClusterOperationInput::Build(params, NWB_TEXT("test cluster input"));
        ASSERT_TRUE(built);
        auto copied = *built;
        const auto native = copied.nativeInfo();
        EXPECT_EQ(native.maxAccelerationStructureCount, 7u);
        if(type == RayTracingClusterOperationType::Move)
            EXPECT_EQ(native.opInput.pMoveObjects, &copied.move);
        else if(type == RayTracingClusterOperationType::BlasBuild)
            EXPECT_EQ(native.opInput.pClustersBottomLevel, &copied.blas);
        else
            EXPECT_EQ(native.opInput.pTriangleClusters, &copied.cluster);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


#include <tests/common/test_context.h>

#include <gtest/gtest.h>

#include <core/graphics/rhi/gpu_crash_diagnostics.h>

#include <global/sync.h>
#include <global/thread.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_crash_marker_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_RetainedMarkerLabel = "Retained marker view";
constexpr AStringView s_DuplicateRootLabel = "Duplicate root";
constexpr AStringView s_DuplicateLeafLabel = "Duplicate leaf";

using TestArena = ::NWB::Tests::TestArena<struct GpuCrashMarkerTestsTag>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuCrashMarkerTracker, CopiesExactNonTerminatedViewAndEmbeddedNullBytes){
    TestArena testArena;
    Core::GpuCrashTracker crashTracker(testArena.arena);
    Core::GpuCrashMarkerTracker commandListTracker(crashTracker, testArena.arena);
    Array<char, 6u> label = { 'O', 'u', 't', 'e', 'r', 'X' };
    const usize outerHash = commandListTracker.pushEvent(AStringView(label.data(), 5u));
    label[0] = 'x';

    const Array<char, 3u> nestedLabel = { 'I', '\0', 'n' };
    const usize nestedHash = commandListTracker.pushEvent(AStringView(nestedLabel.data(), nestedLabel.size()));
    const Core::ResolvedMarker outer = crashTracker.resolveMarker(outerHash);
    const Core::ResolvedMarker nested = crashTracker.resolveMarker(nestedHash);
    ASSERT_TRUE(outer.first());
    ASSERT_TRUE(nested.first());
    EXPECT_EQ(outer.second(), AStringView("Outer"));
    constexpr Array<char, 9u> s_Expected = { 'O', 'u', 't', 'e', 'r', '/', 'I', '\0', 'n' };
    EXPECT_EQ(nested.second(), AStringView(s_Expected.data(), s_Expected.size()));
}


TEST(GpuCrashMarkerTracker, SharedDeviceTrackerPreservesHistoryAcrossCommandListTrackerReset){
    TestArena testArena;
    Core::GpuCrashTracker crashTracker(testArena.arena);
    Core::GpuCrashMarkerTracker firstCommandListTracker(crashTracker, testArena.arena);
    Core::GpuCrashMarkerTracker secondCommandListTracker(crashTracker, testArena.arena);

    const usize outerHash = firstCommandListTracker.pushEvent("Outer");
    const usize nestedHash = firstCommandListTracker.pushEvent("Inner");
    const Core::ResolvedMarker nested = crashTracker.resolveMarker(nestedHash);
    ASSERT_TRUE(nested.first());
    EXPECT_EQ(nested.second(), AStringView("Outer/Inner"));

    firstCommandListTracker.resetEventStack();

    const usize freshHash = secondCommandListTracker.pushEvent("Fresh");
    const Core::ResolvedMarker fresh = crashTracker.resolveMarker(freshHash);
    ASSERT_TRUE(fresh.first());
    EXPECT_EQ(fresh.second(), AStringView("Fresh"));

    const Core::ResolvedMarker historicalOuter = crashTracker.resolveMarker(outerHash);
    const Core::ResolvedMarker historicalNested = crashTracker.resolveMarker(nestedHash);
    ASSERT_TRUE(historicalOuter.first());
    ASSERT_TRUE(historicalNested.first());
    EXPECT_EQ(historicalOuter.second(), AStringView("Outer"));
    EXPECT_EQ(historicalNested.second(), AStringView("Outer/Inner"));

    secondCommandListTracker.popEvent();
    const usize finalHash = firstCommandListTracker.pushEvent("Final");
    const Core::ResolvedMarker final = crashTracker.resolveMarker(finalHash);
    ASSERT_TRUE(final.first());
    EXPECT_EQ(final.second(), AStringView("Final"));
}


TEST(GpuCrashMarkerTracker, DestroyedCommandListTrackersLeaveAllDeviceHistoryResolvable){
    TestArena testArena;
    Core::GpuCrashTracker crashTracker(testArena.arena);
    constexpr Array<AStringView, 6u> s_MarkerNames = { "Destroyed command list 0", "Destroyed command list 1", "Destroyed command list 2", "Destroyed command list 3", "Destroyed command list 4", "Destroyed command list 5", };
    Array<usize, 6u> markerHashes = {};

    usize markerIndex = 0u;
    for(const AStringView markerName : s_MarkerNames){
        Core::GpuCrashMarkerTracker commandListTracker(crashTracker, testArena.arena);
        markerHashes[markerIndex] = commandListTracker.pushEvent(markerName);
        ++markerIndex;
    }

    markerIndex = 0u;
    for(const AStringView markerName : s_MarkerNames){
        const Core::ResolvedMarker resolved = crashTracker.resolveMarker(markerHashes[markerIndex]);
        ASSERT_TRUE(resolved.first());
        EXPECT_EQ(resolved.second(), markerName);
        ++markerIndex;
    }
}


TEST(GpuCrashMarkerTracker, RetainedResolvedViewSurvivesConcurrentHistoryGrowthAndTrackerDestruction){
    TestArena testArena;
    Core::GpuCrashTracker crashTracker(testArena.arena);
    usize retainedHash = 0u;
    {
        Core::GpuCrashMarkerTracker commandListTracker(crashTracker, testArena.arena);
        retainedHash = commandListTracker.pushEvent(s_RetainedMarkerLabel);
    }
    const Core::ResolvedMarker retained = crashTracker.resolveMarker(retainedHash);
    ASSERT_TRUE(retained.first());
    ASSERT_EQ(retained.second(), AStringView("Retained marker view"));

    constexpr Array<AStringView, 16> s_FirstWriterMarkerNames = {
        "Concurrent writer A marker 00", "Concurrent writer A marker 01",
        "Concurrent writer A marker 02", "Concurrent writer A marker 03",
        "Concurrent writer A marker 04", "Concurrent writer A marker 05",
        "Concurrent writer A marker 06", "Concurrent writer A marker 07",
        "Concurrent writer A marker 08", "Concurrent writer A marker 09",
        "Concurrent writer A marker 10", "Concurrent writer A marker 11",
        "Concurrent writer A marker 12", "Concurrent writer A marker 13",
        "Concurrent writer A marker 14", "Concurrent writer A marker 15",
    };
    constexpr Array<AStringView, 16> s_SecondWriterMarkerNames = {
        "Concurrent writer B marker 00", "Concurrent writer B marker 01",
        "Concurrent writer B marker 02", "Concurrent writer B marker 03",
        "Concurrent writer B marker 04", "Concurrent writer B marker 05",
        "Concurrent writer B marker 06", "Concurrent writer B marker 07",
        "Concurrent writer B marker 08", "Concurrent writer B marker 09",
        "Concurrent writer B marker 10", "Concurrent writer B marker 11",
        "Concurrent writer B marker 12", "Concurrent writer B marker 13",
        "Concurrent writer B marker 14", "Concurrent writer B marker 15",
    };
    Latch growthStart(3u);
    Latch growthMidpoint(3u);
    Atomic<u32> activeWriters{ 2u };
    Atomic<b8> invalidObservation{ false };
    const auto insertMarkers = [&](const Span<const AStringView> markerNames){
        {
            Core::GpuCrashMarkerTracker commandListTracker(crashTracker, testArena.arena);
            growthStart.arrive_and_wait();
            for(usize markerIndex = 0u; markerIndex < markerNames.size(); ++markerIndex){
                if(markerIndex == markerNames.size() / 2u)
                    growthMidpoint.arrive_and_wait();

                commandListTracker.resetEventStack();
                const usize insertedHash = commandListTracker.pushEvent(markerNames[markerIndex]);
                const Core::ResolvedMarker inserted = crashTracker.resolveMarker(insertedHash);
                if(!inserted.first() || inserted.second() != markerNames[markerIndex])
                    invalidObservation.store(true, MemoryOrder::release);
            }
        }
        const u32 previousActiveWriters = activeWriters.fetch_sub(1u, MemoryOrder::acq_rel);
        if(previousActiveWriters == 0u || previousActiveWriters > 2u)
            invalidObservation.store(true, MemoryOrder::release);
    };
    Thread firstWriter([&](){ insertMarkers(s_FirstWriterMarkerNames); });
    Thread secondWriter([&](){ insertMarkers(s_SecondWriterMarkerNames); });

    const auto retainedViewIsValid = [&]()noexcept{
        const Core::ResolvedMarker resolved = crashTracker.resolveMarker(retainedHash);
        return retained.second() == AStringView(s_RetainedMarkerLabel) && resolved.first() && resolved.second() == AStringView(s_RetainedMarkerLabel);
    };
    growthStart.arrive_and_wait();
    if(!retainedViewIsValid())
        invalidObservation.store(true, MemoryOrder::release);
    growthMidpoint.arrive_and_wait();
    do{
        if(!retainedViewIsValid())
            invalidObservation.store(true, MemoryOrder::release);
        YieldThread();
    }while(activeWriters.load(MemoryOrder::acquire) != 0u);

    firstWriter.join();
    secondWriter.join();

    EXPECT_FALSE(invalidObservation.load(MemoryOrder::acquire));
    EXPECT_EQ(retained.second(), AStringView("Retained marker view"));
    const Core::ResolvedMarker resolvedAgain = crashTracker.resolveMarker(retainedHash);
    ASSERT_TRUE(resolvedAgain.first());
    EXPECT_EQ(resolvedAgain.second(), retained.second());
}


TEST(GpuCrashMarkerTracker, ExactDuplicatePathsReuseIdsAndDistinctPathsResolveIndependently){
    TestArena testArena;
    Core::GpuCrashTracker crashTracker(testArena.arena);
    Core::GpuCrashMarkerTracker commandListTracker(crashTracker, testArena.arena);

    const usize firstRootHash = commandListTracker.pushEvent(s_DuplicateRootLabel);
    const usize firstDuplicateHash = commandListTracker.pushEvent(s_DuplicateLeafLabel);
    commandListTracker.resetEventStack();
    const usize secondRootHash = commandListTracker.pushEvent(s_DuplicateRootLabel);
    const usize secondDuplicateHash = commandListTracker.pushEvent(s_DuplicateLeafLabel);
    commandListTracker.resetEventStack();
    const usize distinctHash = commandListTracker.pushEvent("Distinct path");

    EXPECT_EQ(firstRootHash, secondRootHash);
    EXPECT_EQ(firstDuplicateHash, secondDuplicateHash);
    EXPECT_NE(firstDuplicateHash, distinctHash);
    const Core::ResolvedMarker duplicate = crashTracker.resolveMarker(firstDuplicateHash);
    const Core::ResolvedMarker distinct = crashTracker.resolveMarker(distinctHash);
    ASSERT_TRUE(duplicate.first());
    ASSERT_TRUE(distinct.first());
    EXPECT_EQ(duplicate.second(), AStringView("Duplicate root/Duplicate leaf"));
    EXPECT_EQ(distinct.second(), AStringView("Distinct path"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/graphics/vulkan/descriptor_buffer_allocation_state.h>
#include <tests/common/test_context.h>
#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_descriptor_buffer_allocation_tests{
using namespace NWB;

TEST(GpuDescriptorBufferAllocation, CoalescingReusesRangeWithoutRevivingStaleIdentity){
    Tests::TestArena<> arena;
    Core::GraphicsBackend::DescriptorBufferAllocationState state(arena.arena);
    Array<u8, 64u> bytes{};
    using Kind = Core::GraphicsBackend::DescriptorBufferSegmentKind::Enum;
    constexpr Kind kind = Core::GraphicsBackend::DescriptorBufferSegmentKind::Resource;
    const auto first = state.allocate(kind, 16u, 8u, 41u, static_cast<u32>(bytes.size()), bytes.data());
    const auto middle = state.allocate(kind, 16u, 8u, 41u, static_cast<u32>(bytes.size()), bytes.data());
    const auto last = state.allocate(kind, 16u, 8u, 41u, static_cast<u32>(bytes.size()), bytes.data());
    ASSERT_TRUE(first && middle && last);
    state.free(*middle, 41u, static_cast<u32>(bytes.size()));
    state.free(*first, 41u, static_cast<u32>(bytes.size()));
    state.free(*last, 41u, static_cast<u32>(bytes.size()));
    const auto merged = state.allocate(kind, 48u, 8u, 41u, static_cast<u32>(bytes.size()), bytes.data());
    ASSERT_TRUE(merged);
    EXPECT_EQ(merged->offsetBytes, 0u);
    EXPECT_NE(merged->allocationSerial, first->allocationSerial);
    EXPECT_FALSE(state.isLive(*first, kind, 41u, static_cast<u32>(bytes.size())));
    EXPECT_FALSE(state.ownsAt(*first, first->offsetBytes));
    EXPECT_TRUE(state.isLive(*merged, kind, 41u, static_cast<u32>(bytes.size())));
    EXPECT_TRUE(state.ownsAt(*merged, merged->offsetBytes + 8u));
    state.resetOffset();
    state.clearAllocations();
    const auto afterReset = state.allocate(kind, 16u, 8u, 41u, static_cast<u32>(bytes.size()), bytes.data());
    ASSERT_TRUE(afterReset);
    EXPECT_NE(afterReset->allocationSerial, first->allocationSerial);
    EXPECT_FALSE(state.isLive(*first, kind, 41u, static_cast<u32>(bytes.size())));
}

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/graphics/vulkan/descriptor_class_policy.h>
#include <core/graphics/vulkan/descriptor_image_policy.h>
#include <tests/common/capturing_logger.h>
#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_descriptor_class_policy_tests{
using namespace NWB;

TEST(GpuDescriptorClassPolicy, StorageAliasesAndSampledImageDimensionsStayDistinct){
    using Policy = Core::GraphicsBackend::DescriptorClassPolicy;
    EXPECT_EQ(Policy::ClassToResourceType(Core::GpuDescriptorClass::StorageBuffer), Core::ResourceType::StructuredBuffer_UAV);
    EXPECT_TRUE(Policy::IsResourceTypeCompatible(Core::GpuDescriptorClass::StorageBuffer, Core::ResourceType::StructuredBuffer_SRV));
    EXPECT_TRUE(Policy::IsResourceTypeCompatible(Core::GpuDescriptorClass::StorageBuffer, Core::ResourceType::RawBuffer_UAV));
    EXPECT_FALSE(Policy::IsResourceTypeCompatible(Core::GpuDescriptorClass::UniformBuffer, Core::ResourceType::RawBuffer_UAV));
    EXPECT_TRUE(Policy::IsSampledImageClass(Core::GpuDescriptorClass::SampledImage2DArrayUint));
    EXPECT_EQ(Policy::GetSampledImageDimension(Core::GpuDescriptorClass::SampledImage2DArrayUint), Core::TextureDimension::Texture2DArray);
    EXPECT_EQ(Policy::GetSampledImageDimension(Core::GpuDescriptorClass::SampledImageCube), Core::TextureDimension::TextureCube);
    EXPECT_FALSE(Policy::IsSampledImageClass(Core::GpuDescriptorClass::StorageImage));
    EXPECT_EQ(Policy::GetSampledImageDimension(Core::GpuDescriptorClass::StorageImage), Core::TextureDimension::Unknown);
}


TEST(GpuDescriptorImagePolicy, Storage3dRejectsIntegerFormatWhileTwoDimensionalStorageAcceptsIt){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard guard(logger, Core::Common::LoggerBreakPolicy::ReportOnly);
    Core::TextureDesc texture;
    texture.dimension = Core::TextureDimension::Texture3D;
    texture.format = Core::Format::R32_UINT;
    auto item = Core::DescriptorWriteItem::None();
    using Policy = Core::GraphicsBackend::DescriptorImagePolicy;
    EXPECT_FALSE(Policy::Validate(Core::GpuDescriptorClass::StorageImage, item, texture));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("rejected format")));
    item.dimension = Core::TextureDimension::Texture2D;
    EXPECT_TRUE(Policy::Validate(Core::GpuDescriptorClass::StorageImage, item, texture));
    item.dimension = Core::TextureDimension::Unknown;
    item.format = Core::Format::R32_FLOAT;
    EXPECT_TRUE(Policy::Validate(Core::GpuDescriptorClass::StorageImage, item, texture));
}

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/graphics/rhi/ownership.adhd.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(ResourceStateOwnership, MatchesReleaseDestinationOnlyForTheExpectedQueue){
    const Core::GpuPhysicalQueueId owner{ .index = 1u, .deviceGeneration = 7u };
    const Core::GpuPhysicalQueueId other{ .index = 2u, .deviceGeneration = 7u };
    const Core::GpuPhysicalQueueId oldGeneration{ .index = 2u, .deviceGeneration = 6u };
    const Core::GpuPhysicalQueueId invalid{};
    EXPECT_TRUE(Core::MatchesResourceStateOwnership(owner, invalid, owner, owner));
    EXPECT_FALSE(Core::MatchesResourceStateOwnership(owner, other, owner, owner));
    EXPECT_TRUE(Core::MatchesResourceStateOwnership(owner, other, owner, other));
    EXPECT_FALSE(Core::MatchesResourceStateOwnership(owner, invalid, owner, other));
    EXPECT_FALSE(Core::MatchesResourceStateOwnership(owner, oldGeneration, owner, other));
    EXPECT_FALSE(Core::MatchesResourceStateOwnership(other, invalid, owner, owner));
    EXPECT_TRUE(Core::MatchesResourceStateOwnership(invalid, invalid, invalid, invalid));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <core/graphics/runtime/runtime_setup_upload_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_setup_upload_graph_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(SetupUploadGraph, LargeThresholdKeepsQueueRoutingAndSubmissionBoundary){
    using Core::GraphicsModuleDetail::SetupUploadGraph;
    const auto below = SetupUploadGraph::Scheduling(SetupUploadGraph::s_LargeMinimumBytes - 1u, true, true);
    const auto at = SetupUploadGraph::Scheduling(SetupUploadGraph::s_LargeMinimumBytes, true, true);
    EXPECT_EQ(below.cost, Core::GpuTaskCostHint::Tiny);
    EXPECT_EQ(at.cost, Core::GpuTaskCostHint::Large);
    for(const auto& hint : { below, at }){
        EXPECT_TRUE(hint.forceSubmissionBoundary);
        EXPECT_FALSE(hint.allowPacketMerge);
        EXPECT_TRUE(hint.allowSameClassQueueRouting);
        EXPECT_TRUE(hint.preferNonPrimarySameClassQueue);
        EXPECT_TRUE(hint.allowCrossFamilySameClassQueueRouting);
    }
}

TEST(SetupUploadGraph, UnknownStateAndCrossFamilySharingKeepUploadContract){
    using Core::GraphicsModuleDetail::SetupUploadGraph;
    EXPECT_EQ(SetupUploadGraph::FinalState(Core::ResourceStates::Unknown), Core::ResourceStates::CopyDest);
    EXPECT_EQ(SetupUploadGraph::FinalState(Core::ResourceStates::ShaderResource), Core::ResourceStates::ShaderResource);
    EXPECT_EQ(SetupUploadGraph::ResolveConsumerSharing(Core::ResourceQueueSharing::Exclusive, Core::CommandQueue::Compute, false), Core::ResourceQueueSharing::GraphicsAndAsyncCompute);
    EXPECT_EQ(SetupUploadGraph::ResolveConsumerSharing(Core::ResourceQueueSharing::Exclusive, Core::CommandQueue::Compute, true), Core::ResourceQueueSharing::AsyncCompute);
    EXPECT_EQ(SetupUploadGraph::ResolveConsumerSharing(Core::ResourceQueueSharing::Graphics, Core::CommandQueue::Transfer, false), Core::ResourceQueueSharing::GraphicsAndTransfer);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>

#include <gtest/gtest.h>

#include <core/graphics/runtime/runtime_standalone_graph.h>

#include <global/exception.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_standalone_workspace_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(StandaloneGraphWorkspacePool, OverlappingLeasesKeepDistinctWorkspacesAndReuseReleasedStorage){
    TestArena<> arena;
    Core::GraphicsModuleDetail::StandaloneGraphWorkspacePool pool(arena.arena);
    Core::GraphicsModuleDetail::StandaloneGraphWorkspace* firstWorkspace = nullptr;
    {
        const Core::GraphicsModuleDetail::StandaloneGraphWorkspacePool::Lease first(pool);
        firstWorkspace = first.workspace();
        ASSERT_NE(firstWorkspace, nullptr);
        {
            const Core::GraphicsModuleDetail::StandaloneGraphWorkspacePool::Lease second(pool);
            ASSERT_NE(second.workspace(), nullptr);
            EXPECT_NE(firstWorkspace, second.workspace());
            EXPECT_FALSE(pool.releaseCommandListCaches());
        }
        EXPECT_FALSE(pool.releaseCommandListCaches());
    }
    EXPECT_TRUE(pool.releaseCommandListCaches());
    {
        const Core::GraphicsModuleDetail::StandaloneGraphWorkspacePool::Lease reused(pool);
        EXPECT_EQ(reused.workspace(), firstWorkspace);
    }
    EXPECT_TRUE(pool.releaseCommandListCaches());
}

TEST(StandaloneGraphWorkspacePool, ExceptionDiscardsLeaseWithoutBlockingNextSubmission){
    TestArena<> arena;
    Core::GraphicsModuleDetail::StandaloneGraphWorkspacePool pool(arena.arena);
    EXPECT_THROW({
        const Core::GraphicsModuleDetail::StandaloneGraphWorkspacePool::Lease lease(pool);
        ASSERT_NE(lease.workspace(), nullptr);
        throw RuntimeException("expected workspace unwind");
    }, RuntimeException);
    EXPECT_TRUE(pool.releaseCommandListCaches());
    {
        const Core::GraphicsModuleDetail::StandaloneGraphWorkspacePool::Lease next(pool);
        ASSERT_NE(next.workspace(), nullptr);
        EXPECT_FALSE(pool.releaseCommandListCaches());
    }
    EXPECT_TRUE(pool.releaseCommandListCaches());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/graphics/vulkan/texture_image_policy.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_texture_image_policy_tests{

using namespace NWB::Core;
using namespace NWB::Core::GraphicsBackend::VulkanTextureDetail;

TEST(GpuTextureImagePolicyTests, ImageSharingRequiresCompleteQueueFamilyProvenance){
    TextureDesc desc;
    desc.format = Format::RGBA8_UNORM;
    const auto metadata = TextureImagePolicy::ValidateTextureCreateDesc(desc, {}, false);
    ASSERT_TRUE(metadata);
    VkImageCreateInfo info = TextureImagePolicy::BuildTextureImageCreateInfo(desc, *metadata);
    EXPECT_TRUE(TextureImagePolicy::IsTextureImageInfoConsistent(desc, info));
    info.sharingMode = VK_SHARING_MODE_CONCURRENT;
    info.queueFamilyIndexCount = 1u;
    EXPECT_FALSE(TextureImagePolicy::IsTextureImageInfoConsistent(desc, info));
    const uint32_t families[] = { 0u, 1u };
    info.queueFamilyIndexCount = 2u;
    info.pQueueFamilyIndices = families;
    EXPECT_TRUE(TextureImagePolicy::IsTextureImageInfoConsistent(desc, info));
    info.flags |= VK_IMAGE_CREATE_PROTECTED_BIT;
    EXPECT_FALSE(TextureImagePolicy::IsTextureImageInfoConsistent(desc, info));
}

}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/graphics/vulkan/texture_shape_policy.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_texture_shape_policy_tests{

using namespace NWB::Core;
using NWB::Core::GraphicsBackend::VulkanTextureDetail::TextureShapePolicy;

TEST(GpuTextureShapePolicyTests, CubeArraysRequireWholeCubesAndSquareFaces){
    TextureDesc desc;
    desc.dimension = TextureDimension::TextureCubeArray;
    desc.width = desc.height = 16u;
    desc.arraySize = 12u;
    EXPECT_TRUE(TextureShapePolicy::IsValid(desc));
    desc.arraySize = 13u;
    EXPECT_FALSE(TextureShapePolicy::IsValid(desc));
    desc.arraySize = 12u;
    desc.height = 8u;
    EXPECT_FALSE(TextureShapePolicy::IsValid(desc));
}

TEST(GpuTextureShapePolicyTests, ThreeDimensionalDepthDeterminesMipLimit){
    TextureDesc desc;
    desc.dimension = TextureDimension::Texture3D;
    desc.depth = 16u;
    desc.mipLevels = 5u;
    EXPECT_EQ(TextureShapePolicy::MaxMipLevels(desc), 5u);
    EXPECT_TRUE(TextureShapePolicy::IsValid(desc));
    desc.mipLevels = 6u;
    EXPECT_FALSE(TextureShapePolicy::IsValid(desc));
}

}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/graphics/vulkan/texture_transfer_layout.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_texture_transfer_layout_tests{

using namespace NWB::Core::GraphicsBackend::VulkanDetail;

TEST(GpuTextureTransferLayoutTests, CompressedPaddedSlicesPreserveTouchedByteBoundary){
    const VkExtent3D extent{ 5u, 5u, 2u };
    const TextureFormatBlockLayout block{ 4u, 4u, 16u };
    const auto touched = TextureTransferLayout::BuildBufferImageCopyLayout(extent, block, 48u, 144u, BufferImageCopyRequiredSize::TouchedBytes, BufferImageCopyPitchFields::EmitExplicit);
    const auto padded = TextureTransferLayout::BuildBufferImageCopyLayout(extent, block, 48u, 144u, BufferImageCopyRequiredSize::PaddedSlices, BufferImageCopyPitchFields::EmitExplicit);
    ASSERT_TRUE(touched);
    ASSERT_TRUE(padded);
    EXPECT_EQ(touched->requiredSize, 224u);
    EXPECT_EQ(padded->requiredSize, 240u);
    EXPECT_EQ(touched->bufferRowLength, 12u);
    EXPECT_EQ(touched->bufferImageHeight, 12u);
    EXPECT_FALSE(TextureTransferLayout::BuildBufferImageCopyLayout(extent, block, 17u, 0u, BufferImageCopyRequiredSize::TouchedBytes, BufferImageCopyPitchFields::OmitImplicit));
}

}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>
#include <tests/unit/graphics/gpu_timing_test_names.adhd.h>

#include <core/graphics/gpu_timing_metric_correlator.h>
#include <core/perf/timing.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_timing_metric_correlator_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = ::NWB::Tests::TestArena<struct GpuTimingMetricCorrelatorTestsTag>;
inline constexpr Name s_MetricScratchArena = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingMetricCorrelatorScratch;


struct RegistrationSentinel{};

class ThrowOnceTimingSink final : public Core::Perf::TimingSink, NoCopy{
public:
    explicit ThrowOnceTimingSink(Core::Perf::TimingRecorder& timing)noexcept
        : m_timing(timing)
    {}


public:
    [[nodiscard]] virtual b8 enabled()const noexcept override{ return true; }

    [[nodiscard]] virtual Expected<Core::Perf::TimingScopeId> registerScope(const Name& scopeName)override{
        ++m_registrationAttempt;
        if(m_registrationAttempt == 3u)
            throw RegistrationSentinel{};
        return m_timing.registerScope(scopeName);
    }

    virtual void recordSample(const Core::Perf::TimingScopeId scope, const f64 seconds, const u64 sampleFrameIndex)noexcept override{
        m_timing.recordSample(scope, seconds, sampleFrameIndex);
    }

    virtual void publishFrame(const u64 publishFrameIndex)noexcept override{ m_timing.publishFrame(publishFrameIndex); }


private:
    Core::Perf::TimingRecorder& m_timing;
    usize m_registrationAttempt = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTimingPacketEnvelopeMetrics, CorrelatesLargeOutOfOrderFrame){
    constexpr usize s_ScopeCount = 2048u;
    constexpr usize s_PairCount = s_ScopeCount / 2u;
    constexpr u64 s_FrameIndex = 91u;
    TestArena testArena;
    Core::Perf::TimingRecorder timing(testArena.arena);
    timing.setEnabled(true);
    Core::GpuTimingMetricCorrelator correlator(testArena.arena, timing);
    Core::Alloc::ScratchArena scratchArena(s_MetricScratchArena);
    Vector<Core::GpuPacketEnvelopeMetricScope, Core::Alloc::ScratchArena> scopes{ scratchArena };
    scopes.reserve(s_ScopeCount);
    for(usize scopeIndex = 0u; scopeIndex < s_ScopeCount; ++scopeIndex){
        Array<char, 32u> scopeIndexBuffer = {};
        const auto scopeNameResult = DeriveName(Name(GraphicsTimingCases::s_GpuTimingCaseLargeEnvelope), FormatDecimal(scopeIndex, scopeIndexBuffer));
        ASSERT_TRUE(scopeNameResult);
        scopes.push_back(Core::GpuPacketEnvelopeMetricScope{ .scopeName = *scopeNameResult, .physicalQueue = { .index = static_cast<u16>(scopeIndex % 2u), .deviceGeneration = 7u }, });
    }
    const Name overlapScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingLargeEnvelopeOverlap;
    const Array<Core::GpuPacketEnvelopeMetricQueueOutput, 2u> outputs = {
        Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = { .index = 0u, .deviceGeneration = 7u }, .internalIdleScopeName = Name(GraphicsTimingCases::s_GpuTimingCaseLargeIdle0) },
        Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = { .index = 1u, .deviceGeneration = 7u }, .internalIdleScopeName = Name(GraphicsTimingCases::s_GpuTimingCaseLargeIdle1) },
    };
    const b8 prepared = correlator.preparePacketEnvelopeMetrics(s_FrameIndex, scopes, overlapScope, outputs);

    ASSERT_TRUE(prepared);

    Core::GpuTimingSinkSampleVector samples{ scratchArena };
    samples.reserve(1u + LengthOf(outputs));
    b8 completedEarly = false;
    for(usize arrivalIndex = 0u; arrivalIndex < s_ScopeCount; ++arrivalIndex){
        // An odd stride permutes this power-of-two input count and separates arrival order from queue order.
        const usize scopeIndex = (arrivalIndex * 773u) % s_ScopeCount;
        const Core::GpuPacketEnvelopeMetricScope& scope = scopes[scopeIndex];
        const u64 beginTicks = static_cast<u64>(scopeIndex / 2u) * 20u + static_cast<u64>(scopeIndex % 2u) * 5u;
        correlator.recordTimestampRange(scope.scopeName, s_FrameIndex, Core::GpuComparableTimestampRange{ .beginTicks = beginTicks, .endTicks = beginTicks + 10u, .secondsPerTick = 0.25, .physicalQueue = scope.physicalQueue, }, scratchArena, samples);
        if(arrivalIndex + 1u < s_ScopeCount)
            completedEarly = completedEarly || !samples.empty();
    }


    EXPECT_FALSE(completedEarly);
    ASSERT_EQ(samples.size(), 1u + LengthOf(outputs));
    for(const Core::GpuTimingSinkSample& sample : samples)
        timing.recordSample(sample.scope, sample.durationSeconds, sample.sourceFrameIndex);
    timing.publishFrame(s_FrameIndex + 1u);

    const Core::Perf::TimingStats& overlap = timing.stats(overlapScope);
    ASSERT_EQ(overlap.sampleCount, 1u);
    EXPECT_DOUBLE_EQ(overlap.seconds, static_cast<f64>(s_PairCount * 5u) * 0.25);
    EXPECT_EQ(overlap.firstSampleFrameIndex, s_FrameIndex);
    for(const Core::GpuPacketEnvelopeMetricQueueOutput& output : outputs){
        const Core::Perf::TimingStats& idle = timing.stats(output.internalIdleScopeName);
        ASSERT_EQ(idle.sampleCount, 1u);
        EXPECT_DOUBLE_EQ(idle.seconds, static_cast<f64>((s_PairCount - 1u) * 10u) * 0.25);
        EXPECT_EQ(idle.firstSampleFrameIndex, s_FrameIndex);
    }
}

TEST(GpuTimingPacketEnvelopeMetrics, ReplacesDuplicateRangesWithoutCompletingMissingScopes){
    TestArena testArena;
    Core::Perf::TimingRecorder timing(testArena.arena);
    timing.setEnabled(true);
    Core::GpuTimingMetricCorrelator correlator(testArena.arena, timing);
    Core::Alloc::ScratchArena scratchArena(s_MetricScratchArena);
    const Core::GpuPhysicalQueueId firstQueue{ .index = 0u, .deviceGeneration = 9u };
    const Core::GpuPhysicalQueueId secondQueue{ .index = 1u, .deviceGeneration = 9u };
    const Name overlapScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingDuplicateEnvelopeOverlap;
    const Array<Core::GpuPacketEnvelopeMetricScope, 2u> scopes = {
        Core::GpuPacketEnvelopeMetricScope{ .scopeName = Name(GraphicsTimingCases::s_GpuTimingCaseDuplicateEnvelopeFirst), .physicalQueue = firstQueue },
        Core::GpuPacketEnvelopeMetricScope{ .scopeName = Name(GraphicsTimingCases::s_GpuTimingCaseDuplicateEnvelopeSecond), .physicalQueue = secondQueue },
    };
    const Array<Core::GpuPacketEnvelopeMetricQueueOutput, 2u> outputs = {
        Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = secondQueue, .internalIdleScopeName = Name(GraphicsTimingCases::s_GpuTimingCaseDuplicateIdle1) },
        Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = firstQueue, .internalIdleScopeName = Name(GraphicsTimingCases::s_GpuTimingCaseDuplicateIdle0) },
    };
    const auto prepare = [&](){
        return correlator.preparePacketEnvelopeMetrics(73u, scopes, overlapScope, outputs);
    };
    ASSERT_TRUE(prepare());
    Core::GpuTimingSinkSampleVector samples{ scratchArena };
    samples.reserve(1u + LengthOf(outputs));
    Core::GpuComparableTimestampRange range{ .beginTicks = 0u, .endTicks = 100u, .secondsPerTick = 0.5, .physicalQueue = firstQueue, };
    correlator.recordTimestampRange(scopes[0u].scopeName, 73u, range, scratchArena, samples);
    ASSERT_TRUE(samples.empty());
    ASSERT_TRUE(prepare());
    range.beginTicks = 10u;
    range.endTicks = 20u;
    correlator.recordTimestampRange(scopes[0u].scopeName, 73u, range, scratchArena, samples);
    ASSERT_TRUE(samples.empty());

    correlator.recordTimestampRange(scopes[1u].scopeName, 73u, range, scratchArena, samples);
    correlator.recordTimestampRange(Name(GraphicsTimingCases::s_GpuTimingCaseUnrelatedEnvelope), 73u, range, scratchArena, samples);
    ASSERT_TRUE(samples.empty());
    range.beginTicks = 15u;
    range.endTicks = 25u;
    range.physicalQueue = secondQueue;
    correlator.recordTimestampRange(scopes[1u].scopeName, 73u, range, scratchArena, samples);
    ASSERT_EQ(samples.size(), 3u);
    for(const Core::GpuTimingSinkSample& sample : samples)
        timing.recordSample(sample.scope, sample.durationSeconds, sample.sourceFrameIndex);
    timing.publishFrame(74u);
    EXPECT_DOUBLE_EQ(timing.stats(overlapScope).seconds, 2.5);
    EXPECT_DOUBLE_EQ(timing.stats(outputs[0u].internalIdleScopeName).seconds, 0.0);
    EXPECT_DOUBLE_EQ(timing.stats(outputs[1u].internalIdleScopeName).seconds, 0.0);

    samples.clear();
    correlator.recordTimestampRange(scopes[1u].scopeName, 73u, range, scratchArena, samples);
    EXPECT_TRUE(samples.empty());
}

TEST(GpuTimingPacketEnvelopeMetrics, PreservesPartialFramesAcrossGrowthRejectedPreparesAndOutOfOrderRetirement){
    constexpr usize s_FrameCount = 8u;
    TestArena testArena;
    Core::Perf::TimingRecorder timing(testArena.arena);
    timing.setEnabled(true);
    Core::GpuTimingMetricCorrelator correlator(testArena.arena, timing);
    Core::Alloc::ScratchArena scratchArena(s_MetricScratchArena);
    const Core::GpuPhysicalQueueId firstQueue{ .index = 0u, .deviceGeneration = 9u };
    const Core::GpuPhysicalQueueId secondQueue{ .index = 1u, .deviceGeneration = 9u };
    const Name overlapScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingGrowingEnvelopeOverlap;
    const Array<Core::GpuPacketEnvelopeMetricScope, 2> scopes = {
        Core::GpuPacketEnvelopeMetricScope{ .scopeName = Name(GraphicsTimingCases::s_GpuTimingCaseGrowingEnvelopeFirst), .physicalQueue = firstQueue },
        Core::GpuPacketEnvelopeMetricScope{ .scopeName = Name(GraphicsTimingCases::s_GpuTimingCaseGrowingEnvelopeSecond), .physicalQueue = secondQueue },
    };
    const Array<Core::GpuPacketEnvelopeMetricScope, 2> reversedScopes = { scopes[1u], scopes[0u] };
    const Array<Core::GpuPacketEnvelopeMetricScope, 2> duplicateScopes = { scopes[0u], Core::GpuPacketEnvelopeMetricScope{ .scopeName = scopes[0u].scopeName, .physicalQueue = secondQueue }, };
    const Array<Core::GpuPacketEnvelopeMetricQueueOutput, 2> outputs = {
        Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = firstQueue, .internalIdleScopeName = Name(GraphicsTimingCases::s_GpuTimingCaseGrowingIdle0) },
        Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = secondQueue, .internalIdleScopeName = Name(GraphicsTimingCases::s_GpuTimingCaseGrowingIdle1) },
    };
    const auto prepare = [&](const u64 frameIndex, const Span<const Core::GpuPacketEnvelopeMetricScope> scopeInputs){
        return correlator.preparePacketEnvelopeMetrics(frameIndex, scopeInputs, overlapScope, outputs);
    };
    EXPECT_FALSE(prepare(100u, duplicateScopes));
    EXPECT_FALSE(correlator.hasOutputRole(overlapScope));
    const Core::GpuComparableTimestampRange firstRange{ .beginTicks = 0u, .endTicks = 10u, .secondsPerTick = 0.5, .physicalQueue = firstQueue, };
    const Core::GpuComparableTimestampRange secondRange{ .beginTicks = 5u, .endTicks = 15u, .secondsPerTick = 0.5, .physicalQueue = secondQueue, };
    Core::GpuTimingSinkSampleVector samples{ scratchArena };
    samples.reserve(3u);
    for(usize frameOffset = 0u; frameOffset < s_FrameCount; ++frameOffset){
        const u64 frameIndex = 100u + frameOffset;
        const b8 reverseOrder = frameOffset % 2u != 0u;
        ASSERT_TRUE(prepare(frameIndex, reverseOrder ? reversedScopes : scopes));
        correlator.recordTimestampRange(scopes[0u].scopeName, frameIndex, firstRange, scratchArena, samples);
        ASSERT_TRUE(samples.empty());
        EXPECT_FALSE(prepare(frameIndex, duplicateScopes));
        EXPECT_FALSE(prepare(frameIndex, reverseOrder ? scopes : reversedScopes));
        ASSERT_TRUE(prepare(frameIndex, reverseOrder ? reversedScopes : scopes));
    }
    for(usize arrivalIndex = 0u; arrivalIndex < s_FrameCount; ++arrivalIndex){
        const u64 frameIndex = 100u + (arrivalIndex * 5u) % s_FrameCount;
        correlator.recordTimestampRange(scopes[0u].scopeName, frameIndex, firstRange, scratchArena, samples);
        ASSERT_TRUE(samples.empty());
        correlator.recordTimestampRange(scopes[1u].scopeName, frameIndex, secondRange, scratchArena, samples);
        ASSERT_EQ(samples.size(), 3u);
        EXPECT_DOUBLE_EQ(samples[0u].durationSeconds, 2.5);
        EXPECT_DOUBLE_EQ(samples[1u].durationSeconds, 0.0);
        EXPECT_DOUBLE_EQ(samples[2u].durationSeconds, 0.0);
        for(const Core::GpuTimingSinkSample& sample : samples)
            EXPECT_EQ(sample.sourceFrameIndex, frameIndex);
        samples.clear();
    }
}

TEST(GpuTimingPacketEnvelopeMetrics, PublishesNoPartialFrameWhenOutputRegistrationThrows){
    TestArena testArena;
    Core::Perf::TimingRecorder timing(testArena.arena);
    timing.setEnabled(true);
    ThrowOnceTimingSink sink(timing);
    Core::GpuTimingMetricCorrelator correlator(testArena.arena, sink);
    Core::Alloc::ScratchArena scratchArena(s_MetricScratchArena);
    const Core::GpuPhysicalQueueId firstQueue{ .index = 0u, .deviceGeneration = 13u };
    const Core::GpuPhysicalQueueId secondQueue{ .index = 1u, .deviceGeneration = 13u };
    const Name overlapScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingThrowingEnvelopeOverlap;
    const Array<Core::GpuPacketEnvelopeMetricScope, 2u> scopes = {
        Core::GpuPacketEnvelopeMetricScope{ .scopeName = Name(GraphicsTimingCases::s_GpuTimingCaseThrowingEnvelopeFirst), .physicalQueue = firstQueue },
        Core::GpuPacketEnvelopeMetricScope{ .scopeName = Name(GraphicsTimingCases::s_GpuTimingCaseThrowingEnvelopeSecond), .physicalQueue = secondQueue },
    };
    const Array<Core::GpuPacketEnvelopeMetricQueueOutput, 2u> outputs = {
        Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = firstQueue, .internalIdleScopeName = Name(GraphicsTimingCases::s_GpuTimingCaseThrowingIdle0) },
        Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = secondQueue, .internalIdleScopeName = Name(GraphicsTimingCases::s_GpuTimingCaseThrowingIdle1) },
    };
    const auto prepare = [&](){
        return correlator.preparePacketEnvelopeMetrics(73u, scopes, overlapScope, outputs);
    };
    EXPECT_THROW(EXPECT_TRUE(prepare()), RegistrationSentinel);
    EXPECT_TRUE(correlator.hasOutputRole(overlapScope));
    EXPECT_TRUE(correlator.hasOutputRole(outputs[0u].internalIdleScopeName));
    EXPECT_FALSE(correlator.hasOutputRole(outputs[1u].internalIdleScopeName));
    ASSERT_TRUE(prepare());

    Core::GpuTimingSinkSampleVector samples{ scratchArena };
    samples.reserve(3u);
    const Core::GpuComparableTimestampRange firstRange{ .beginTicks = 2u, .endTicks = 8u, .secondsPerTick = 0.5, .physicalQueue = firstQueue, };
    const Core::GpuComparableTimestampRange secondRange{ .beginTicks = 4u, .endTicks = 10u, .secondsPerTick = 0.5, .physicalQueue = secondQueue, };
    correlator.recordTimestampRange(scopes[0u].scopeName, 73u, firstRange, scratchArena, samples);
    EXPECT_TRUE(samples.empty());
    correlator.recordTimestampRange(scopes[1u].scopeName, 73u, secondRange, scratchArena, samples);
    ASSERT_EQ(samples.size(), 3u);
    EXPECT_DOUBLE_EQ(samples[0u].durationSeconds, 2.0);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/unit/graphics/gpu_timing_test_names.adhd.h>

#include <core/graphics/gpu_timing_metrics.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_timing_metrics_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuPacketEnvelopeMetrics, UnionsSameQueueRangesAndOrdersEveryQueue){
    Core::Alloc::ScratchArena scratchArena{Name(GraphicsTimingCases::s_GpuTimingCasePacketMetricsUnion)};
    Core::GpuQueuePacketEnvelopeMetricsVector queueMetrics{scratchArena};
    const Array<Core::GpuComparableTimestampRange, 8u> ranges{{
        { .beginTicks = 40u, .endTicks = 50u, .secondsPerTick = 0.25, .physicalQueue = { .index = 2u, .deviceGeneration = 7u }, },
        { .beginTicks = 100u, .endTicks = 100u, .secondsPerTick = 0.25, .physicalQueue = { .index = 1u, .deviceGeneration = 7u }, },
        { .beginTicks = 8u, .endTicks = 20u, .secondsPerTick = 0.25, .physicalQueue = { .index = 2u, .deviceGeneration = 7u }, },
        { .beginTicks = 5u, .endTicks = 15u, .secondsPerTick = 0.25, .physicalQueue = { .index = 0u, .deviceGeneration = 7u }, },
        { .beginTicks = 0u, .endTicks = 10u, .secondsPerTick = 0.25, .physicalQueue = { .index = 2u, .deviceGeneration = 7u }, },
        { .beginTicks = 20u, .endTicks = 30u, .secondsPerTick = 0.25, .physicalQueue = { .index = 2u, .deviceGeneration = 7u }, },
        { .beginTicks = 42u, .endTicks = 45u, .secondsPerTick = 0.25, .physicalQueue = { .index = 2u, .deviceGeneration = 7u }, },
        { .beginTicks = 0u, .endTicks = 10u, .secondsPerTick = 0.25, .physicalQueue = { .index = 2u, .deviceGeneration = 7u }, },
    }};
    Expected<Core::GpuPacketEnvelopeMetrics> metrics = Core::TryAggregateGpuPacketEnvelopeMetrics(ranges, scratchArena, queueMetrics);
    ASSERT_TRUE(metrics);
    EXPECT_DOUBLE_EQ(metrics->secondsPerTick, 0.25);
    EXPECT_EQ(metrics->queueOverlapTicks, 10u);
    ASSERT_EQ(queueMetrics.size(), 3u);
    EXPECT_EQ(queueMetrics[0u].physicalQueue.index, 0u);
    EXPECT_EQ(queueMetrics[0u].internalIdleTicks, 0u);
    EXPECT_EQ(queueMetrics[1u].physicalQueue.index, 1u);
    EXPECT_EQ(queueMetrics[1u].internalIdleTicks, 0u);
    EXPECT_EQ(queueMetrics[2u].physicalQueue.index, 2u);
    EXPECT_EQ(queueMetrics[2u].internalIdleTicks, 10u);
}

TEST(GpuPacketEnvelopeMetrics, CountsConcurrentQueueUnionOnceAtTripleOverlap){
    Core::Alloc::ScratchArena scratchArena{Name(GraphicsTimingCases::s_GpuTimingCasePacketMetricsTriple)};
    Core::GpuQueuePacketEnvelopeMetricsVector queueMetrics{scratchArena};
    const Array<Core::GpuComparableTimestampRange, 3u> ranges{{
        { .beginTicks = 0u, .endTicks = 100u, .secondsPerTick = 1.0, .physicalQueue = { .index = 0u, .deviceGeneration = 2u }, },
        { .beginTicks = 10u, .endTicks = 90u, .secondsPerTick = 1.0, .physicalQueue = { .index = 1u, .deviceGeneration = 2u }, },
        { .beginTicks = 20u, .endTicks = 80u, .secondsPerTick = 1.0, .physicalQueue = { .index = 2u, .deviceGeneration = 2u }, },
    }};
    Expected<Core::GpuPacketEnvelopeMetrics> metrics = Core::TryAggregateGpuPacketEnvelopeMetrics(ranges, scratchArena, queueMetrics);
    ASSERT_TRUE(metrics);
    EXPECT_EQ(metrics->queueOverlapTicks, 80u);
    ASSERT_EQ(queueMetrics.size(), 3u);
    EXPECT_EQ(queueMetrics[0u].internalIdleTicks, 0u);
    EXPECT_EQ(queueMetrics[1u].internalIdleTicks, 0u);
    EXPECT_EQ(queueMetrics[2u].internalIdleTicks, 0u);
}

TEST(GpuPacketEnvelopeMetrics, PreservesLargeAndNearLimitTickPrecision){
    Core::Alloc::ScratchArena scratchArena{Name(GraphicsTimingCases::s_GpuTimingCasePacketMetricsPrecision)};
    Core::GpuQueuePacketEnvelopeMetricsVector queueMetrics{scratchArena};
    static constexpr u64 s_LargeTick = 9007199254740993u;
    const Array<Core::GpuComparableTimestampRange, 3u> largeRanges{{
        { .beginTicks = s_LargeTick, .endTicks = s_LargeTick + 20u, .secondsPerTick = 0.5, .physicalQueue = { .index = 5u, .deviceGeneration = 9u }, },
        { .beginTicks = s_LargeTick + 30u, .endTicks = s_LargeTick + 40u, .secondsPerTick = 0.5, .physicalQueue = { .index = 5u, .deviceGeneration = 9u }, },
        { .beginTicks = s_LargeTick + 5u, .endTicks = s_LargeTick + 35u, .secondsPerTick = 0.5, .physicalQueue = { .index = 6u, .deviceGeneration = 9u }, },
    }};
    Expected<Core::GpuPacketEnvelopeMetrics> metrics = Core::TryAggregateGpuPacketEnvelopeMetrics(largeRanges, scratchArena, queueMetrics);
    ASSERT_TRUE(metrics);
    EXPECT_EQ(metrics->queueOverlapTicks, 20u);
    ASSERT_EQ(queueMetrics.size(), 2u);
    EXPECT_EQ(queueMetrics[0u].internalIdleTicks, 10u);

    const Array<Core::GpuComparableTimestampRange, 2u> nearLimitRanges{{
        { .beginTicks = Limit<u64>::s_Max - 20u, .endTicks = Limit<u64>::s_Max - 10u, .secondsPerTick = 1.0, .physicalQueue = { .index = 1u, .deviceGeneration = 4u }, },
        { .beginTicks = Limit<u64>::s_Max - 15u, .endTicks = Limit<u64>::s_Max - 5u, .secondsPerTick = 1.0, .physicalQueue = { .index = 2u, .deviceGeneration = 4u }, },
    }};
    ASSERT_TRUE((metrics = Core::TryAggregateGpuPacketEnvelopeMetrics(nearLimitRanges, scratchArena, queueMetrics)));
    EXPECT_EQ(metrics->queueOverlapTicks, 5u);
}

TEST(GpuPacketEnvelopeMetrics, AcceptsCompleteZeroOverlapInputs){
    Core::Alloc::ScratchArena scratchArena{Name(GraphicsTimingCases::s_GpuTimingCasePacketMetricsZero)};
    Core::GpuQueuePacketEnvelopeMetricsVector queueMetrics{scratchArena};
    const Array<Core::GpuComparableTimestampRange, 2u> touchingRanges{{
        { .beginTicks = 0u, .endTicks = 10u, .secondsPerTick = 1.0, .physicalQueue = { .index = 0u, .deviceGeneration = 6u }, },
        { .beginTicks = 10u, .endTicks = 20u, .secondsPerTick = 1.0, .physicalQueue = { .index = 1u, .deviceGeneration = 6u }, },
    }};
    Expected<Core::GpuPacketEnvelopeMetrics> metrics = Core::TryAggregateGpuPacketEnvelopeMetrics(touchingRanges, scratchArena, queueMetrics);
    ASSERT_TRUE(metrics);
    EXPECT_EQ(metrics->queueOverlapTicks, 0u);
    ASSERT_EQ(queueMetrics.size(), 2u);

    const Array<Core::GpuComparableTimestampRange, 2u> zeroLengthRanges{{
        { .beginTicks = 25u, .endTicks = 25u, .secondsPerTick = 1.0, .physicalQueue = { .index = 3u, .deviceGeneration = 6u }, },
        { .beginTicks = 75u, .endTicks = 75u, .secondsPerTick = 1.0, .physicalQueue = { .index = 1u, .deviceGeneration = 6u }, },
    }};
    ASSERT_TRUE((metrics = Core::TryAggregateGpuPacketEnvelopeMetrics(zeroLengthRanges, scratchArena, queueMetrics)));
    EXPECT_EQ(metrics->queueOverlapTicks, 0u);
    ASSERT_EQ(queueMetrics.size(), 2u);
    EXPECT_EQ(queueMetrics[0u].physicalQueue.index, 1u);
    EXPECT_EQ(queueMetrics[1u].physicalQueue.index, 3u);
    EXPECT_EQ(queueMetrics[0u].internalIdleTicks, 0u);
    EXPECT_EQ(queueMetrics[1u].internalIdleTicks, 0u);
}

TEST(GpuPacketEnvelopeMetrics, RejectsInvalidOrIncomparableInputAndClearsOutputs){
    Core::Alloc::ScratchArena scratchArena{Name(GraphicsTimingCases::s_GpuTimingCasePacketMetricsRejection)};
    Core::GpuQueuePacketEnvelopeMetricsVector queueMetrics{scratchArena};
    const Array<Core::GpuComparableTimestampRange, 2u> validRanges{{
        { .beginTicks = 0u, .endTicks = 10u, .secondsPerTick = 1.0, .physicalQueue = { .index = 0u, .deviceGeneration = 3u }, },
        { .beginTicks = 5u, .endTicks = 15u, .secondsPerTick = 1.0, .physicalQueue = { .index = 1u, .deviceGeneration = 3u }, },
    }};
    const auto expectRejected = [&](const Span<const Core::GpuComparableTimestampRange> ranges){
        queueMetrics.push_back(Core::GpuQueuePacketEnvelopeMetrics{ .physicalQueue = { .index = 4u, .deviceGeneration = 8u }, .internalIdleTicks = 13u, });
        EXPECT_FALSE(Core::TryAggregateGpuPacketEnvelopeMetrics(ranges, scratchArena, queueMetrics));
        EXPECT_TRUE(queueMetrics.empty());
    };

    expectRejected(Span<const Core::GpuComparableTimestampRange>{});
    expectRejected(Span<const Core::GpuComparableTimestampRange>(validRanges.data(), 0u));
    Array<Core::GpuComparableTimestampRange, 2u> invalidRanges{};

    invalidRanges[0u] = validRanges[0u];
    invalidRanges[1u] = validRanges[1u];
    invalidRanges[0u].beginTicks = 11u;
    expectRejected(invalidRanges);

    invalidRanges[0u] = validRanges[0u];
    invalidRanges[0u].physicalQueue = {};
    expectRejected(invalidRanges);

    invalidRanges[0u] = validRanges[0u];
    invalidRanges[0u].secondsPerTick = Limit<f64>::s_Infinity;
    expectRejected(invalidRanges);

    invalidRanges[0u].secondsPerTick = Limit<f64>::s_QuietNaN;
    expectRejected(invalidRanges);

    invalidRanges[0u].secondsPerTick = 0.0;
    expectRejected(invalidRanges);

    invalidRanges[0u] = validRanges[0u];
    invalidRanges[1u].secondsPerTick = 0.5;
    expectRejected(invalidRanges);

    invalidRanges[1u] = validRanges[1u];
    invalidRanges[1u].physicalQueue.deviceGeneration = 4u;
    expectRejected(invalidRanges);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>
#include <tests/unit/graphics/gpu_timing_test_names.adhd.h>

#include <gtest/gtest.h>

#include <core/graphics/gpu_timing.h>
#include <core/perf/timing.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_timing_recorder_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = ::NWB::Tests::TestArena<struct GpuTimingRecorderTestsTag>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(IsNothrowDestructible_V<Core::GpuTimingFrameTransaction>, "GPU timing frame transactions must unwind without throwing");
static_assert(IsNothrowDestructible_V<Core::GpuTimingMeasure>, "GPU timing measures stored in Optional must unwind without throwing");


struct GpuTimingSubscriptionCapture{
    u32 sampleCount = 0u;


public:
    static void Invoke(void* const context, const Core::GpuTimingSample&)noexcept{
        GpuTimingSubscriptionCapture* const capture = static_cast<GpuTimingSubscriptionCapture*>(context);
        if(!capture)
            return;

        ++capture->sampleCount;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTimingSampleSubscriptions, ScopedDemandsReplaceAtomicallyAndRemainSubscriptionIsolated){
    TestArena testArena;
    Core::Perf::TimingRecorder timingSink(testArena.arena);
    Core::GpuTimingRecorder recorder(testArena.arena, timingSink);
    GpuTimingSubscriptionCapture firstCapture;
    GpuTimingSubscriptionCapture secondCapture;
    const Name firstScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsGpuTimingFeedbackFirst;
    const Name sharedScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsGpuTimingFeedbackShared;
    const Name secondScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsGpuTimingFeedbackSecond;
    const Name metricFirstInput = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsGpuTimingFeedbackMetricFirstInput;
    const Name metricSecondInput = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsGpuTimingFeedbackMetricSecondInput;
    const Name metricAlternateFirstInput = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsGpuTimingFeedbackMetricAlternateFirstInput;
    const Name metricAlternateSecondInput = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsGpuTimingFeedbackMetricAlternateSecondInput;
    const Name metricOutput = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsGpuTimingFeedbackMetricOutput;
    const Array<Name, 2u> firstScopes = { firstScope, sharedScope };
    const Array<Name, 2u> secondScopes = { sharedScope, secondScope };
    const Array<Name, 2u> invalidScopes = { firstScope, s_NameNone };
    const Array<Name, 2u> duplicateScopes = { firstScope, firstScope };
    const auto firstResult = recorder.subscribeSampleListener(Core::GpuTimingSampleListener{ .context = &firstCapture, .invoke = &GpuTimingSubscriptionCapture::Invoke, });
    ASSERT_TRUE(firstResult);
    const Core::GpuTimingSampleSubscription first = *firstResult;
    const auto secondResult = recorder.subscribeSampleListener(Core::GpuTimingSampleListener{ .context = &secondCapture, .invoke = &GpuTimingSubscriptionCapture::Invoke, });
    ASSERT_TRUE(secondResult);
    const Core::GpuTimingSampleSubscription second = *secondResult;

    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    static_assert(noexcept(recorder.unsubscribeSampleListener(first)));
    EXPECT_NE(first, second);
    EXPECT_FALSE(recorder.collectionActive());
    ASSERT_TRUE(recorder.setFeedbackCollectionScopes(first, firstScopes));
    ASSERT_TRUE(recorder.setFeedbackCollectionScopes(first, firstScopes));
    EXPECT_TRUE(recorder.collectionActive());
    EXPECT_FALSE(recorder.setFeedbackCollectionScopes(first, invalidScopes));
    EXPECT_TRUE(recorder.collectionActive());
    EXPECT_FALSE(recorder.setFeedbackCollectionScopes(first, duplicateScopes));
    EXPECT_TRUE(recorder.collectionActive());
    EXPECT_FALSE(recorder.prepareOverlapMetric(metricFirstInput, metricSecondInput, firstScope));
    ASSERT_TRUE(recorder.prepareOverlapMetric(metricFirstInput, metricSecondInput, metricOutput));
    EXPECT_FALSE(recorder.setFeedbackCollectionScopes(first, Span<const Name>(&metricOutput, 1u)));
    EXPECT_TRUE(recorder.collectionActive());
    EXPECT_FALSE(recorder.prepareOverlapMetric(metricAlternateFirstInput, metricAlternateSecondInput, firstScope));
    ASSERT_TRUE(recorder.setFeedbackCollectionScopes(second, secondScopes));
    ASSERT_TRUE(recorder.setFeedbackCollectionScopes(first, Span<const Name>(&firstScope, 1u)));
    ASSERT_TRUE(recorder.clearFeedbackCollectionScopes(first));
    EXPECT_TRUE(recorder.collectionActive());

    ASSERT_TRUE(recorder.setFeedbackCollectionScopes(first, Span<const Name>(&firstScope, 1u)));
    recorder.unsubscribeSampleListener(second);
    EXPECT_TRUE(recorder.collectionActive());
    EXPECT_FALSE(recorder.setFeedbackCollectionScopes(second, Span<const Name>(&secondScope, 1u)));
    EXPECT_FALSE(recorder.clearFeedbackCollectionScopes(second));

    ASSERT_TRUE(recorder.clearFeedbackCollectionScopes(first));
    EXPECT_FALSE(recorder.collectionActive());
    EXPECT_TRUE(recorder.clearFeedbackCollectionScopes(first));

    recorder.unsubscribeSampleListener(first);
    EXPECT_FALSE(recorder.setFeedbackCollectionScopes(first, Span<const Name>(&firstScope, 1u)));
    EXPECT_FALSE(recorder.clearFeedbackCollectionScopes(first));
}

TEST(GpuTimingSampleAttribution, IssuesUniqueProcessIdentitiesAcrossRecorderResetAndRecreation){
    TestArena testArena;
    Core::Perf::TimingRecorder firstSink(testArena.arena);
    Core::Perf::TimingRecorder secondSink(testArena.arena);
    Core::GpuTimingRecorder firstRecorder(testArena.arena, firstSink);
    Core::GpuTimingRecorder secondRecorder(testArena.arena, secondSink);

    const auto firstResult = firstRecorder.allocateSampleAttribution();
    ASSERT_TRUE(firstResult);
    const Core::GpuTimingSampleAttribution first = *firstResult;
    const auto secondResult = firstRecorder.allocateSampleAttribution();
    ASSERT_TRUE(secondResult);
    const Core::GpuTimingSampleAttribution second = *secondResult;
    const auto recreatedResult = secondRecorder.allocateSampleAttribution();
    ASSERT_TRUE(recreatedResult);
    const Core::GpuTimingSampleAttribution recreated = *recreatedResult;
    firstRecorder.resetQueries();
    const auto afterResetResult = firstRecorder.allocateSampleAttribution();
    ASSERT_TRUE(afterResetResult);
    const Core::GpuTimingSampleAttribution afterReset = *afterResetResult;

    ASSERT_TRUE(first.valid());
    ASSERT_TRUE(second.valid());
    ASSERT_TRUE(recreated.valid());
    ASSERT_TRUE(afterReset.valid());
    EXPECT_NE(first, second);
    EXPECT_NE(first, recreated);
    EXPECT_NE(first, afterReset);
    EXPECT_NE(second, recreated);
    EXPECT_NE(second, afterReset);
    EXPECT_NE(recreated, afterReset);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(TimerQueryResult, MasksHighBitsAndWrapsPartialWidth){
    const Core::TimerQueryResult result{ .beginTicks = 0x1fau, .endTicks = 0x105u, .secondsPerTick = 0.5, .physicalQueue = { .index = 1u, .deviceGeneration = 3u }, .timestampValidBits = 8u, .comparableAcrossSubmissions = true, };

    ASSERT_TRUE(result.valid());
    EXPECT_EQ(result.timestampMask(), 0xffu);
    EXPECT_EQ(result.maskedBeginTicks(), 0xfau);
    EXPECT_EQ(result.durationTicks(), 11u);
    EXPECT_DOUBLE_EQ(result.durationSeconds(), 5.5);
    EXPECT_FALSE(result.hasComparableRange());
}

TEST(TimerQueryResult, WrapsFullWidthWithoutShiftingBySixtyFour){
    const Core::TimerQueryResult result{ .beginTicks = Limit<u64>::s_Max - 3u, .endTicks = 5u, .secondsPerTick = 1.0, .physicalQueue = { .index = 1u, .deviceGeneration = 3u }, .timestampValidBits = 64u, .comparableAcrossSubmissions = true, };

    ASSERT_TRUE(result.valid());
    EXPECT_EQ(result.durationTicks(), 9u);
    EXPECT_DOUBLE_EQ(result.durationSeconds(), 9.0);
    EXPECT_FALSE(result.hasComparableRange());
}

TEST(TimerQueryResult, RetainsNearFullWidthDurationWithoutPublishingAbsoluteRange){
    const Core::TimerQueryResult result{ .beginTicks = (u64{ 1u } << 63u) - 3u, .endTicks = 2u, .secondsPerTick = 0.5, .physicalQueue = { .index = 1u, .deviceGeneration = 3u }, .timestampValidBits = 63u, .comparableAcrossSubmissions = true, };

    ASSERT_TRUE(result.valid());
    EXPECT_EQ(result.durationTicks(), 5u);
    EXPECT_DOUBLE_EQ(result.durationSeconds(), 2.5);
    EXPECT_FALSE(result.hasComparableRange());
}

TEST(TimerQueryResult, ComparableRangeRequiresCapabilityAndPhysicalQueue){
    Core::TimerQueryResult result{ .beginTicks = 10u, .endTicks = 20u, .secondsPerTick = 1.0, .physicalQueue = { .index = 1u, .deviceGeneration = 3u }, .timestampValidBits = 64u, .comparableAcrossSubmissions = false, };

    ASSERT_TRUE(result.valid());
    EXPECT_FALSE(result.hasComparableRange());

    result.comparableAcrossSubmissions = true;
    result.physicalQueue = {};
    EXPECT_FALSE(result.hasComparableRange());

    result.physicalQueue = { .index = 1u, .deviceGeneration = 3u };
    EXPECT_TRUE(result.hasComparableRange());
}

TEST(TimerQueryResult, RejectsUnknownOrOutOfRangeWidths){
    Core::TimerQueryResult result{ .beginTicks = 10u, .endTicks = 20u, .secondsPerTick = 1.0, .physicalQueue = {}, .timestampValidBits = 0u, .comparableAcrossSubmissions = false, };

    EXPECT_FALSE(result.valid());
    EXPECT_EQ(result.timestampMask(), 0u);
    EXPECT_EQ(result.durationTicks(), 0u);
    EXPECT_DOUBLE_EQ(result.durationSeconds(), 0.0);

    result.timestampValidBits = 65u;
    EXPECT_FALSE(result.valid());
    EXPECT_EQ(result.timestampMask(), 0u);

    result.timestampValidBits = 64u;
    result.secondsPerTick = 0.0;
    EXPECT_FALSE(result.valid());
    EXPECT_FALSE(result.hasComparableRange());
}

TEST(GpuComparableTimestampRange, IntersectsRawTicksBeforeFloatingPointConversion){
    static constexpr u64 s_LargeTick = GraphicsTimingCases::s_GpuTimingLargeTick;
    const Core::GpuComparableTimestampRange first{ .beginTicks = s_LargeTick, .endTicks = s_LargeTick + 5u, .secondsPerTick = 0.25, .physicalQueue = { .index = 0u, .deviceGeneration = 7u }, };
    const Core::GpuComparableTimestampRange second{ .beginTicks = s_LargeTick + 4u, .endTicks = s_LargeTick + 8u, .secondsPerTick = 0.25, .physicalQueue = { .index = 3u, .deviceGeneration = 7u }, };

    const auto overlapTicks = Core::TryComputeGpuTimestampOverlap(first, second);
    ASSERT_TRUE(overlapTicks);
    EXPECT_EQ(*overlapTicks, 1u);

    Core::GpuComparableTimestampRange disjoint = second;
    disjoint.beginTicks = first.endTicks;
    disjoint.endTicks = first.endTicks + 2u;
    const auto disjointOverlap = Core::TryComputeGpuTimestampOverlap(first, disjoint);
    ASSERT_TRUE(disjointOverlap);
    EXPECT_EQ(*disjointOverlap, 0u);

    Core::GpuComparableTimestampRange recreatedDevice = second;
    recreatedDevice.physicalQueue.deviceGeneration = 8u;
    EXPECT_FALSE(Core::TryComputeGpuTimestampOverlap(first, recreatedDevice));

    Core::GpuComparableTimestampRange mismatchedPeriod = second;
    mismatchedPeriod.secondsPerTick = 0.5;
    EXPECT_FALSE(Core::TryComputeGpuTimestampOverlap(first, mismatchedPeriod));

    Core::GpuComparableTimestampRange infinitePeriod = second;
    infinitePeriod.secondsPerTick = Limit<f64>::s_Infinity;
    EXPECT_FALSE(infinitePeriod.valid());
    EXPECT_FALSE(Core::TryComputeGpuTimestampOverlap(first, infinitePeriod));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>
#include <tests/unit/graphics/gpu_timing_test_names.adhd.h>

#include <core/graphics/gpu_timing.h>
#include <core/graphics/gpu_timing_metric_correlator.h>
#include <core/perf/timing.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_timing_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = ::NWB::Tests::TestArena<struct GpuTimingTestsTag>;
inline constexpr Name s_GpuTimingMetricScratchArena = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsGpuTimingMetricScratch;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class FailOnceTimingSink final : public Core::Perf::TimingSink, NoCopy{
public:
    explicit FailOnceTimingSink(const usize failureAttempt)noexcept
        : m_failureAttempt(failureAttempt)
    {}


public:
    [[nodiscard]] virtual b8 enabled()const noexcept override{ return true; }

    [[nodiscard]] virtual Expected<Core::Perf::TimingScopeId> registerScope([[maybe_unused]] const Name& scopeName)noexcept override{
        const usize attempt = m_registrationAttempt;
        ++m_registrationAttempt;
        if(attempt == m_failureAttempt)
            return MakeUnexpected(Failure{});

        const u32 index = m_nextScopeIndex;
        ++m_nextScopeIndex;
        return Core::Perf::TimingScopeId{ .index = index, .generation = 1u };
    }

    virtual void recordSample([[maybe_unused]] const Core::Perf::TimingScopeId scope, [[maybe_unused]] const f64 seconds, [[maybe_unused]] const u64 sampleFrameIndex)noexcept override{}

    virtual void publishFrame([[maybe_unused]] const u64 publishFrameIndex)noexcept override{}


private:
    usize m_failureAttempt = 0u;
    usize m_registrationAttempt = 0u;
    u32 m_nextScopeIndex = 0u;
};


void RecordAndPublishTimestampRange(const Name& scopeName, const u64 frameIndex, const Core::GpuComparableTimestampRange& range, Core::GpuTimingMetricCorrelator& correlator, Core::Perf::TimingSink& timing, Core::Alloc::ScratchArena& scratchArena){
    Core::GpuTimingSinkSampleVector performanceSamples{ scratchArena };
    correlator.recordTimestampRange(scopeName, frameIndex, range, scratchArena, performanceSamples);
    for(const Core::GpuTimingSinkSample& sample : performanceSamples)
        timing.recordSample(sample.scope, sample.durationSeconds, sample.sourceFrameIndex);
}


TEST(GpuTimingOverlapRegistration, IsCanonicalIdempotentAndRejectsMetricRoleConflicts){
    TestArena testArena;
    Core::Perf::TimingRecorder timing(testArena.arena);
    Core::GpuTimingMetricCorrelator correlator(testArena.arena, timing);
    const Name firstScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingOverlapFirst;
    const Name secondScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingOverlapSecond;
    const Name thirdScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingOverlapThird;
    const Name fourthScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingOverlapFourth;
    const Name outputScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingOverlapOutput;
    const Name alternateOutputScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingOverlapAlternateOutput;

    EXPECT_FALSE(correlator.prepareOverlapMetric(s_NameNone, secondScope, outputScope));
    EXPECT_FALSE(correlator.prepareOverlapMetric(firstScope, s_NameNone, outputScope));
    EXPECT_FALSE(correlator.prepareOverlapMetric(firstScope, secondScope, s_NameNone));
    EXPECT_FALSE(correlator.prepareOverlapMetric(firstScope, firstScope, outputScope));
    EXPECT_FALSE(correlator.prepareOverlapMetric(firstScope, secondScope, firstScope));
    EXPECT_FALSE(correlator.prepareOverlapMetric(firstScope, secondScope, secondScope));
    EXPECT_EQ(timing.scopeCount(), 0u);

    ASSERT_TRUE(correlator.prepareOverlapMetric(firstScope, secondScope, outputScope));
    EXPECT_EQ(timing.scopeCount(), 1u);
    EXPECT_TRUE(correlator.prepareOverlapMetric(secondScope, firstScope, outputScope));
    EXPECT_EQ(timing.scopeCount(), 1u);

    EXPECT_FALSE(correlator.prepareOverlapMetric(firstScope, secondScope, alternateOutputScope));
    EXPECT_FALSE(correlator.prepareOverlapMetric(secondScope, firstScope, alternateOutputScope));
    EXPECT_FALSE(correlator.prepareOverlapMetric(thirdScope, fourthScope, outputScope));
    EXPECT_FALSE(correlator.prepareOverlapMetric(outputScope, thirdScope, alternateOutputScope));
    EXPECT_FALSE(correlator.prepareOverlapMetric(thirdScope, fourthScope, firstScope));
    EXPECT_EQ(timing.scopeCount(), 1u);

    EXPECT_TRUE(correlator.prepareOverlapMetric(firstScope, thirdScope, alternateOutputScope));
    EXPECT_EQ(timing.scopeCount(), 2u);
}

TEST(GpuTimingOverlapRegistration, ReversedDuplicatePublishesOneSample){
    TestArena testArena;
    Core::Perf::TimingRecorder timing(testArena.arena);
    timing.setEnabled(true);
    Core::GpuTimingMetricCorrelator correlator(testArena.arena, timing);
    const Name firstScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingOverlapSampleFirst;
    const Name secondScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingOverlapSampleSecond;
    const Name outputScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingOverlapSampleOutput;
    Core::Alloc::ScratchArena scratchArena(s_GpuTimingMetricScratchArena);
    const Core::GpuComparableTimestampRange firstRange{ .beginTicks = 10u, .endTicks = 20u, .secondsPerTick = 0.25, .physicalQueue = { .index = 0u, .deviceGeneration = 3u }, };
    const Core::GpuComparableTimestampRange secondRange{ .beginTicks = 18u, .endTicks = 30u, .secondsPerTick = 0.25, .physicalQueue = { .index = 1u, .deviceGeneration = 3u }, };
    Core::GpuTimingSinkSampleVector performanceSamples{ scratchArena };

    ASSERT_TRUE(correlator.prepareOverlapMetric(firstScope, secondScope, outputScope));
    ASSERT_TRUE(correlator.prepareOverlapMetric(secondScope, firstScope, outputScope));
    correlator.recordTimestampRange(firstScope, 41u, firstRange, scratchArena, performanceSamples);
    EXPECT_TRUE(performanceSamples.empty());
    correlator.recordTimestampRange(secondScope, 41u, secondRange, scratchArena, performanceSamples);
    ASSERT_EQ(performanceSamples.size(), 1u);
    const Core::GpuTimingSinkSample publishedSample = performanceSamples.front();
    const Core::Perf::TimingScopeId outputScopeId = timing.scopeAt(0u);
    EXPECT_EQ(publishedSample.scope.index, outputScopeId.index);
    EXPECT_EQ(publishedSample.scope.generation, outputScopeId.generation);
    EXPECT_DOUBLE_EQ(publishedSample.durationSeconds, 0.5);
    EXPECT_EQ(publishedSample.sourceFrameIndex, 41u);

    performanceSamples.clear();
    correlator.recordTimestampRange(secondScope, 41u, secondRange, scratchArena, performanceSamples);
    EXPECT_TRUE(performanceSamples.empty());
    timing.recordSample(publishedSample.scope, publishedSample.durationSeconds, publishedSample.sourceFrameIndex);
    timing.publishFrame(42u);

    const Core::Perf::TimingStats& stats = timing.stats(outputScope);
    ASSERT_EQ(stats.sampleCount, 1u);
    EXPECT_DOUBLE_EQ(stats.seconds, 0.5);
    EXPECT_EQ(stats.firstSampleFrameIndex, 41u);
    EXPECT_EQ(stats.lastSampleFrameIndex, 41u);
}

TEST(GpuTimingOverlapRegistration, CatchUpBatchBoundsSourceFramesAndPreservesLastArrivalDuration){
    TestArena testArena;
    Core::Perf::TimingRecorder timing(testArena.arena);
    timing.setEnabled(true);
    Core::GpuTimingMetricCorrelator correlator(testArena.arena, timing);
    Core::Alloc::ScratchArena scratchArena(s_GpuTimingMetricScratchArena);
    const Name firstScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingOverlapCatchUpFirst;
    const Name secondScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingOverlapCatchUpSecond;
    const Name outputScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingOverlapCatchUpOutput;
    const Core::GpuComparableTimestampRange firstRange{ .beginTicks = 10u, .endTicks = 30u, .secondsPerTick = 0.25, .physicalQueue = { .index = 0u, .deviceGeneration = 3u }, };
    const Core::GpuComparableTimestampRange newerRange{ .beginTicks = 18u, .endTicks = 34u, .secondsPerTick = 0.25, .physicalQueue = { .index = 1u, .deviceGeneration = 3u }, };
    Core::GpuComparableTimestampRange olderRange = newerRange;
    olderRange.beginTicks = 26u;
    Core::GpuTimingSinkSampleVector samples{ scratchArena };
    ASSERT_TRUE(correlator.prepareOverlapMetric(firstScope, secondScope, outputScope));
    correlator.recordTimestampRange(firstScope, 50u, firstRange, scratchArena, samples);
    correlator.recordTimestampRange(firstScope, 51u, firstRange, scratchArena, samples);
    correlator.recordTimestampRange(secondScope, 51u, newerRange, scratchArena, samples);
    correlator.recordTimestampRange(secondScope, 50u, olderRange, scratchArena, samples);
    ASSERT_EQ(samples.size(), 2u);
    EXPECT_EQ(samples[0u].sourceFrameIndex, 51u);
    EXPECT_EQ(samples[1u].sourceFrameIndex, 50u);
    EXPECT_DOUBLE_EQ(samples[0u].durationSeconds, 3.0);
    EXPECT_DOUBLE_EQ(samples[1u].durationSeconds, 1.0);
    for(const Core::GpuTimingSinkSample& sample : samples)
        timing.recordSample(sample.scope, sample.durationSeconds, sample.sourceFrameIndex);
    timing.publishFrame(52u);

    const Core::Perf::TimingStats& stats = timing.stats(outputScope);
    ASSERT_EQ(stats.sampleCount, 2u);
    EXPECT_EQ(stats.publishFrameIndex, 52u);
    EXPECT_EQ(stats.firstSampleFrameIndex, 50u);
    EXPECT_EQ(stats.lastSampleFrameIndex, 51u);
    EXPECT_DOUBLE_EQ(stats.seconds, 4.0);
    EXPECT_DOUBLE_EQ(stats.minSeconds, 1.0);
    EXPECT_DOUBLE_EQ(stats.maxSeconds, 3.0);
    EXPECT_DOUBLE_EQ(stats.lastSeconds, 1.0);
}

TEST(GpuTimingOverlapRegistration, DiscardPreservesRegistrationAndResetClearsIt){
    TestArena testArena;
    Core::Perf::TimingRecorder timing(testArena.arena);
    timing.setEnabled(true);
    Core::GpuTimingMetricCorrelator correlator(testArena.arena, timing);
    Core::Alloc::ScratchArena scratchArena(s_GpuTimingMetricScratchArena);
    const Name firstScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingOverlapLifecycleFirst;
    const Name secondScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingOverlapLifecycleSecond;
    const Name resetFirstScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingOverlapLifecycleResetFirst;
    const Name resetSecondScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingOverlapLifecycleResetSecond;
    const Name outputScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingOverlapLifecycleOutput;
    const Core::GpuComparableTimestampRange firstRange{ .beginTicks = 10u, .endTicks = 20u, .secondsPerTick = 0.25, .physicalQueue = { .index = 0u, .deviceGeneration = 3u }, };
    const Core::GpuComparableTimestampRange secondRange{ .beginTicks = 18u, .endTicks = 30u, .secondsPerTick = 0.25, .physicalQueue = { .index = 1u, .deviceGeneration = 3u }, };

    ASSERT_TRUE(correlator.prepareOverlapMetric(firstScope, secondScope, outputScope));
    RecordAndPublishTimestampRange(firstScope, 51u, firstRange, correlator, timing, scratchArena);
    correlator.discardPendingRanges();
    RecordAndPublishTimestampRange(secondScope, 51u, secondRange, correlator, timing, scratchArena);
    timing.publishFrame(52u);
    EXPECT_FALSE(timing.stats(outputScope).valid());

    RecordAndPublishTimestampRange(firstScope, 53u, firstRange, correlator, timing, scratchArena);
    RecordAndPublishTimestampRange(secondScope, 53u, secondRange, correlator, timing, scratchArena);
    timing.publishFrame(54u);
    const Core::Perf::TimingStats& afterDiscard = timing.stats(outputScope);
    ASSERT_EQ(afterDiscard.sampleCount, 1u);
    EXPECT_DOUBLE_EQ(afterDiscard.seconds, 0.5);
    EXPECT_EQ(afterDiscard.firstSampleFrameIndex, 53u);
    EXPECT_EQ(afterDiscard.lastSampleFrameIndex, 53u);

    RecordAndPublishTimestampRange(firstScope, 55u, firstRange, correlator, timing, scratchArena);
    correlator.reset();
    RecordAndPublishTimestampRange(secondScope, 55u, secondRange, correlator, timing, scratchArena);
    timing.publishFrame(56u);
    EXPECT_FALSE(timing.stats(outputScope).valid());

    ASSERT_TRUE(correlator.prepareOverlapMetric(resetFirstScope, resetSecondScope, outputScope));
    RecordAndPublishTimestampRange(resetFirstScope, 57u, firstRange, correlator, timing, scratchArena);
    RecordAndPublishTimestampRange(resetSecondScope, 57u, secondRange, correlator, timing, scratchArena);
    timing.publishFrame(58u);
    const Core::Perf::TimingStats& afterReset = timing.stats(outputScope);
    ASSERT_EQ(afterReset.sampleCount, 1u);
    EXPECT_DOUBLE_EQ(afterReset.seconds, 0.5);
    EXPECT_EQ(afterReset.firstSampleFrameIndex, 57u);
    EXPECT_EQ(afterReset.lastSampleFrameIndex, 57u);
}

TEST(GpuTimingPacketEnvelopeMetrics, PublishesCompleteOutOfOrderEnvelopeByPhysicalQueue){
    TestArena testArena;
    Core::Perf::TimingRecorder timing(testArena.arena);
    timing.setEnabled(true);
    Core::GpuTimingMetricCorrelator correlator(testArena.arena, timing);
    Core::Alloc::ScratchArena scratchArena(s_GpuTimingMetricScratchArena);
    const Name firstScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeFirst;
    const Name secondScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeSecond;
    const Name thirdScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeThird;
    const Name overlapScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeQueueOverlap;
    const Name firstQueueIdleScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeQueue0InternalIdle;
    const Name secondQueueIdleScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeQueue1InternalIdle;
    const Core::GpuPhysicalQueueId firstQueue{ .index = 0u, .deviceGeneration = 7u };
    const Core::GpuPhysicalQueueId secondQueue{ .index = 1u, .deviceGeneration = 7u };
    const Array<Core::GpuPacketEnvelopeMetricScope, 3u> scopes = {
        Core::GpuPacketEnvelopeMetricScope{ .scopeName = firstScope, .physicalQueue = firstQueue },
        Core::GpuPacketEnvelopeMetricScope{ .scopeName = secondScope, .physicalQueue = secondQueue },
        Core::GpuPacketEnvelopeMetricScope{ .scopeName = thirdScope, .physicalQueue = firstQueue },
    };
    const Array<Core::GpuPacketEnvelopeMetricQueueOutput, 2u> outputs = {
        Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = firstQueue, .internalIdleScopeName = firstQueueIdleScope },
        Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = secondQueue, .internalIdleScopeName = secondQueueIdleScope },
    };

    ASSERT_TRUE(correlator.preparePacketEnvelopeMetrics(73u, scopes, overlapScope, outputs));
    EXPECT_TRUE(correlator.preparePacketEnvelopeMetrics(73u, scopes, overlapScope, outputs));
    RecordAndPublishTimestampRange(thirdScope, 73u, Core::GpuComparableTimestampRange{ .beginTicks = 15u, .endTicks = 20u, .secondsPerTick = 0.25, .physicalQueue = firstQueue, }, correlator, timing, scratchArena);
    RecordAndPublishTimestampRange(firstScope, 73u, Core::GpuComparableTimestampRange{ .beginTicks = 0u, .endTicks = 10u, .secondsPerTick = 0.25, .physicalQueue = firstQueue, }, correlator, timing, scratchArena);
    timing.publishFrame(74u);
    EXPECT_FALSE(timing.stats(overlapScope).valid());

    RecordAndPublishTimestampRange(secondScope, 73u, Core::GpuComparableTimestampRange{ .beginTicks = 5u, .endTicks = 18u, .secondsPerTick = 0.25, .physicalQueue = secondQueue, }, correlator, timing, scratchArena);
    timing.publishFrame(75u);

    const Core::Perf::TimingStats& overlap = timing.stats(overlapScope);
    const Core::Perf::TimingStats& firstQueueIdle = timing.stats(firstQueueIdleScope);
    const Core::Perf::TimingStats& secondQueueIdle = timing.stats(secondQueueIdleScope);
    ASSERT_EQ(overlap.sampleCount, 1u);
    ASSERT_EQ(firstQueueIdle.sampleCount, 1u);
    ASSERT_EQ(secondQueueIdle.sampleCount, 1u);
    EXPECT_DOUBLE_EQ(overlap.seconds, 2.0);
    EXPECT_DOUBLE_EQ(firstQueueIdle.seconds, 1.25);
    EXPECT_DOUBLE_EQ(secondQueueIdle.seconds, 0.0);
    EXPECT_EQ(overlap.firstSampleFrameIndex, 73u);
    EXPECT_EQ(overlap.lastSampleFrameIndex, 73u);

    RecordAndPublishTimestampRange(thirdScope, 73u, Core::GpuComparableTimestampRange{ .beginTicks = 15u, .endTicks = 20u, .secondsPerTick = 0.25, .physicalQueue = firstQueue, }, correlator, timing, scratchArena);
    RecordAndPublishTimestampRange(firstScope, 73u, Core::GpuComparableTimestampRange{ .beginTicks = 0u, .endTicks = 10u, .secondsPerTick = 0.25, .physicalQueue = firstQueue, }, correlator, timing, scratchArena);
    RecordAndPublishTimestampRange(secondScope, 73u, Core::GpuComparableTimestampRange{ .beginTicks = 5u, .endTicks = 18u, .secondsPerTick = 0.25, .physicalQueue = secondQueue, }, correlator, timing, scratchArena);
    timing.publishFrame(76u);
    EXPECT_FALSE(timing.stats(overlapScope).valid());
    EXPECT_FALSE(timing.stats(firstQueueIdleScope).valid());
    EXPECT_FALSE(timing.stats(secondQueueIdleScope).valid());
}

TEST(GpuTimingPacketEnvelopeMetrics, RejectsOutputRoleAndPhysicalQueueCollisions){
    TestArena testArena;
    Core::Perf::TimingRecorder timing(testArena.arena);
    Core::GpuTimingMetricCorrelator correlator(testArena.arena, timing);
    const Name firstScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeRoleFirst;
    const Name secondScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeRoleSecond;
    const Name overlapScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeRoleOverlap;
    const Name alternateOverlapScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeRoleAlternateOverlap;
    const Name firstQueueIdleScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeRoleQueue0Idle;
    const Name secondQueueIdleScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeRoleQueue1Idle;
    const Name alternateFirstQueueIdleScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeRoleAlternateQueue0Idle;
    const Name alternateSecondQueueIdleScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeRoleAlternateQueue1Idle;
    const Core::GpuPhysicalQueueId firstQueue{ .index = 0u, .deviceGeneration = 9u };
    const Core::GpuPhysicalQueueId secondQueue{ .index = 1u, .deviceGeneration = 9u };
    const Array<Core::GpuPacketEnvelopeMetricScope, 2u> scopes = {
        Core::GpuPacketEnvelopeMetricScope{ .scopeName = firstScope, .physicalQueue = firstQueue },
        Core::GpuPacketEnvelopeMetricScope{ .scopeName = secondScope, .physicalQueue = secondQueue },
    };
    const Array<Core::GpuPacketEnvelopeMetricQueueOutput, 2u> outputs = {
        Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = firstQueue, .internalIdleScopeName = firstQueueIdleScope },
        Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = secondQueue, .internalIdleScopeName = secondQueueIdleScope },
    };
    ASSERT_TRUE(correlator.preparePacketEnvelopeMetrics(80u, scopes, overlapScope, outputs));

    const Array<Core::GpuPacketEnvelopeMetricQueueOutput, 2u> overlapAsIdleOutputs = {
        Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = firstQueue, .internalIdleScopeName = overlapScope },
        Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = secondQueue, .internalIdleScopeName = alternateSecondQueueIdleScope },
    };
    EXPECT_FALSE(correlator.preparePacketEnvelopeMetrics(81u, scopes, alternateOverlapScope, overlapAsIdleOutputs));

    const Array<Core::GpuPacketEnvelopeMetricQueueOutput, 2u> alternateOutputs = {
        Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = firstQueue, .internalIdleScopeName = alternateFirstQueueIdleScope },
        Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = secondQueue, .internalIdleScopeName = alternateSecondQueueIdleScope },
    };
    EXPECT_FALSE(correlator.preparePacketEnvelopeMetrics(81u, scopes, firstQueueIdleScope, alternateOutputs));

    const Array<Core::GpuPacketEnvelopeMetricQueueOutput, 2u> remappedOutputs = {
        Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = firstQueue, .internalIdleScopeName = alternateFirstQueueIdleScope },
        Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = secondQueue, .internalIdleScopeName = firstQueueIdleScope },
    };
    EXPECT_FALSE(correlator.preparePacketEnvelopeMetrics(81u, scopes, alternateOverlapScope, remappedOutputs));
    EXPECT_EQ(timing.scopeCount(), 3u);
}

TEST(GpuTimingPacketEnvelopeMetrics, RetainsSuccessfulOutputRolesWhenSinkRegistrationFails){
    TestArena testArena;
    FailOnceTimingSink timing(2u);
    Core::GpuTimingMetricCorrelator correlator(testArena.arena, timing);
    const Name firstScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeFailureFirst;
    const Name secondScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeFailureSecond;
    const Name retryFirstScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeFailureRetryFirst;
    const Name retrySecondScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeFailureRetrySecond;
    const Name overlapScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeFailureOverlap;
    const Name firstQueueIdleScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeFailureQueue0Idle;
    const Name secondQueueIdleScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeFailureQueue1Idle;
    const Name alternateSecondQueueIdleScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeFailureAlternateQueue1Idle;
    const Core::GpuPhysicalQueueId firstQueue{ .index = 0u, .deviceGeneration = 13u };
    const Core::GpuPhysicalQueueId secondQueue{ .index = 1u, .deviceGeneration = 13u };
    const Array<Core::GpuPacketEnvelopeMetricScope, 2u> scopes = {
        Core::GpuPacketEnvelopeMetricScope{ .scopeName = firstScope, .physicalQueue = firstQueue },
        Core::GpuPacketEnvelopeMetricScope{ .scopeName = secondScope, .physicalQueue = secondQueue },
    };
    const Array<Core::GpuPacketEnvelopeMetricQueueOutput, 2u> outputs = {
        Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = firstQueue, .internalIdleScopeName = firstQueueIdleScope },
        Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = secondQueue, .internalIdleScopeName = secondQueueIdleScope },
    };
    const Array<Core::GpuPacketEnvelopeMetricQueueOutput, 2u> retryOutputs = {
        Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = firstQueue, .internalIdleScopeName = firstQueueIdleScope },
        Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = secondQueue, .internalIdleScopeName = alternateSecondQueueIdleScope },
    };

    EXPECT_FALSE(correlator.preparePacketEnvelopeMetrics(90u, scopes, overlapScope, outputs));
    EXPECT_FALSE(correlator.prepareOverlapMetric(retryFirstScope, retrySecondScope, overlapScope));
    EXPECT_FALSE(correlator.prepareOverlapMetric(retryFirstScope, retrySecondScope, firstQueueIdleScope));
    ASSERT_TRUE(correlator.prepareOverlapMetric(retryFirstScope, retrySecondScope, secondQueueIdleScope));
    EXPECT_TRUE(correlator.preparePacketEnvelopeMetrics(90u, scopes, overlapScope, retryOutputs));
}

TEST(GpuTimingPacketEnvelopeMetrics, IsolatesFramesRejectsWrongQueuesPrunesIncompleteFramesAndResetsState){
    TestArena testArena;
    Core::Perf::TimingRecorder timing(testArena.arena);
    timing.setEnabled(true);
    Core::GpuTimingMetricCorrelator correlator(testArena.arena, timing);
    Core::Alloc::ScratchArena scratchArena(s_GpuTimingMetricScratchArena);
    const Name firstScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeIsolationFirst;
    const Name secondScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeIsolationSecond;
    const Name overlapScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeIsolationOverlap;
    const Name firstQueueIdleScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeIsolationQueue0Idle;
    const Name secondQueueIdleScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeIsolationQueue1Idle;
    const Name resetFirstOutputScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeIsolationResetFirstOutput;
    const Name resetSecondInputScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeIsolationResetSecondInput;
    const Name resetSecondOutputScope = NWB::Tests::GraphicsTimingCases::s_GpuTimingNameTestsTimingEnvelopeIsolationResetSecondOutput;
    const Core::GpuPhysicalQueueId firstQueue{ .index = 0u, .deviceGeneration = 11u };
    const Core::GpuPhysicalQueueId secondQueue{ .index = 1u, .deviceGeneration = 11u };
    const Array<Core::GpuPacketEnvelopeMetricScope, 2u> scopes = {
        Core::GpuPacketEnvelopeMetricScope{ .scopeName = firstScope, .physicalQueue = firstQueue },
        Core::GpuPacketEnvelopeMetricScope{ .scopeName = secondScope, .physicalQueue = secondQueue },
    };
    const Array<Core::GpuPacketEnvelopeMetricQueueOutput, 2u> outputs = {
        Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = firstQueue, .internalIdleScopeName = firstQueueIdleScope },
        Core::GpuPacketEnvelopeMetricQueueOutput{ .physicalQueue = secondQueue, .internalIdleScopeName = secondQueueIdleScope },
    };
    ASSERT_TRUE(correlator.preparePacketEnvelopeMetrics(50u, scopes, overlapScope, outputs));
    ASSERT_TRUE(correlator.preparePacketEnvelopeMetrics(51u, scopes, overlapScope, outputs));

    const Core::GpuComparableTimestampRange firstRange{ .beginTicks = 2u, .endTicks = 8u, .secondsPerTick = 0.5, .physicalQueue = firstQueue, };
    const Core::GpuComparableTimestampRange secondRange{ .beginTicks = 4u, .endTicks = 10u, .secondsPerTick = 0.5, .physicalQueue = secondQueue, };
    RecordAndPublishTimestampRange(firstScope, 50u, firstRange, correlator, timing, scratchArena);
    RecordAndPublishTimestampRange(secondScope, 51u, secondRange, correlator, timing, scratchArena);
    RecordAndPublishTimestampRange(firstScope, 51u, secondRange, correlator, timing, scratchArena);
    timing.publishFrame(52u);
    EXPECT_FALSE(timing.stats(overlapScope).valid());

    RecordAndPublishTimestampRange(firstScope, 51u, firstRange, correlator, timing, scratchArena);
    timing.publishFrame(53u);
    const Core::Perf::TimingStats& frameFiftyOne = timing.stats(overlapScope);
    ASSERT_EQ(frameFiftyOne.sampleCount, 1u);
    EXPECT_DOUBLE_EQ(frameFiftyOne.seconds, 2.0);
    EXPECT_EQ(frameFiftyOne.firstSampleFrameIndex, 51u);

    RecordAndPublishTimestampRange(secondScope, 50u, secondRange, correlator, timing, scratchArena);
    timing.publishFrame(54u);
    const Core::Perf::TimingStats& frameFifty = timing.stats(overlapScope);
    ASSERT_EQ(frameFifty.sampleCount, 1u);
    EXPECT_DOUBLE_EQ(frameFifty.seconds, 2.0);
    EXPECT_EQ(frameFifty.firstSampleFrameIndex, 50u);
    EXPECT_EQ(frameFifty.lastSampleFrameIndex, 50u);

    ASSERT_TRUE(correlator.preparePacketEnvelopeMetrics(55u, scopes, overlapScope, outputs));
    RecordAndPublishTimestampRange(firstScope, 55u, firstRange, correlator, timing, scratchArena);
    correlator.discardPendingRanges();
    RecordAndPublishTimestampRange(secondScope, 55u, secondRange, correlator, timing, scratchArena);
    timing.publishFrame(56u);
    EXPECT_FALSE(timing.stats(overlapScope).valid());
    EXPECT_FALSE(timing.stats(firstQueueIdleScope).valid());
    EXPECT_FALSE(timing.stats(secondQueueIdleScope).valid());
    EXPECT_FALSE(correlator.prepareOverlapMetric(resetFirstOutputScope, resetSecondInputScope, overlapScope));
    EXPECT_FALSE(correlator.prepareOverlapMetric(resetFirstOutputScope, resetSecondInputScope, firstQueueIdleScope));
    EXPECT_FALSE(correlator.prepareOverlapMetric(resetFirstOutputScope, resetSecondInputScope, secondQueueIdleScope));

    ASSERT_TRUE(correlator.preparePacketEnvelopeMetrics(57u, scopes, overlapScope, outputs));
    RecordAndPublishTimestampRange(firstScope, 57u, firstRange, correlator, timing, scratchArena);
    RecordAndPublishTimestampRange(secondScope, 57u, secondRange, correlator, timing, scratchArena);
    timing.publishFrame(58u);
    const Core::Perf::TimingStats& afterDiscard = timing.stats(overlapScope);
    ASSERT_EQ(afterDiscard.sampleCount, 1u);
    EXPECT_EQ(afterDiscard.firstSampleFrameIndex, 57u);
    EXPECT_EQ(afterDiscard.lastSampleFrameIndex, 57u);

    ASSERT_TRUE(correlator.preparePacketEnvelopeMetrics(60u, scopes, overlapScope, outputs));
    RecordAndPublishTimestampRange(firstScope, 60u, firstRange, correlator, timing, scratchArena);
    ASSERT_TRUE(correlator.preparePacketEnvelopeMetrics(1000u, scopes, overlapScope, outputs));
    RecordAndPublishTimestampRange(secondScope, 60u, secondRange, correlator, timing, scratchArena);
    timing.publishFrame(1001u);
    EXPECT_FALSE(timing.stats(overlapScope).valid());
    EXPECT_FALSE(timing.stats(firstQueueIdleScope).valid());
    EXPECT_FALSE(timing.stats(secondQueueIdleScope).valid());

    RecordAndPublishTimestampRange(firstScope, 1000u, firstRange, correlator, timing, scratchArena);
    correlator.reset();
    RecordAndPublishTimestampRange(secondScope, 1000u, secondRange, correlator, timing, scratchArena);
    timing.publishFrame(1002u);
    EXPECT_FALSE(timing.stats(overlapScope).valid());
    EXPECT_FALSE(timing.stats(firstQueueIdleScope).valid());
    EXPECT_FALSE(timing.stats(secondQueueIdleScope).valid());

    ASSERT_TRUE(correlator.prepareOverlapMetric(overlapScope, firstQueueIdleScope, resetFirstOutputScope));
    ASSERT_TRUE(correlator.prepareOverlapMetric(secondQueueIdleScope, resetSecondInputScope, resetSecondOutputScope));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <core/graphics/vulkan/vulkan_viewport_policy.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


namespace Tests{


TEST(VulkanViewportPolicy, FractionalBoundsAreRoundedOutAndNegativeScissorsAreRejected){
    using Policy = Core::GraphicsBackend::VulkanViewportPolicy;
    const Core::Viewport viewport(1.2f, 10.2f, 2.1f, 8.7f, 0.f, 1.f);
    const auto rect = Policy::BuildImplicitScissor(viewport);
    ASSERT_TRUE(rect);
    EXPECT_EQ(rect->offset.x, 1);
    EXPECT_EQ(rect->offset.y, 2);
    EXPECT_EQ(rect->extent.width, 10u);
    EXPECT_EQ(rect->extent.height, 7u);
    EXPECT_FALSE(Policy::BuildImplicitScissor(Core::Viewport(-0.1f, 10.f, 0.f, 8.f, 0.f, 1.f)));
}


};


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <core/graphics/rhi/resource.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_resource_queue_sharing_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(!ResourceQueueSharing::IsValid(static_cast<ResourceQueueSharing::Mask>(1u << 7u)));


TEST(ResourceQueueSharing, RejectsEveryUnknownSharingBit){
    constexpr u32 s_MaxValidValue = static_cast<u32>(ResourceQueueSharing::GraphicsAsyncComputeAndTransfer);
    for(u32 rawValue = s_MaxValidValue; rawValue <= Limit<u8>::s_Max; ++rawValue){
        SCOPED_TRACE(rawValue);
        const ResourceQueueSharing::Mask sharing = static_cast<ResourceQueueSharing::Mask>(static_cast<u8>(rawValue));
        EXPECT_EQ(ResourceQueueSharing::IsValid(sharing), rawValue <= s_MaxValidValue);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


