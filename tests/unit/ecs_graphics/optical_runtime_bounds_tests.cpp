// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/unit/ecs_graphics/name_cases.adhd.h>

#include <impl/ecs_render/components.adhd.h>
#include <impl/ecs_render/optics/graph_types.adhd.h>
#include <impl/ecs_render/optics/optical_scene_upload.h>

#include <core/alloc/general.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_optical_runtime_bounds_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(OpticalRuntimeBounds, UploadByteSizeRejectsEmptyMisalignedAndOutOfCapacity){
    EXPECT_FALSE(OpticalUploadByteSizeFits(0u, 4u, sizeof(u32)));
    EXPECT_FALSE(OpticalUploadByteSizeFits(4u, 3u, sizeof(u32)));
    EXPECT_FALSE(OpticalUploadByteSizeFits(3u, 4u, sizeof(u32)));
    EXPECT_FALSE(OpticalUploadByteSizeFits(4u, 4u, 0u));
    EXPECT_TRUE(OpticalUploadByteSizeFits(4u, 4u, sizeof(u32)));
}

TEST(OpticalRuntimeBounds, FinalizeCountsRespectShaderAddressLimits){
    constexpr usize s_MaxInputs = Limit<u32>::s_Max / sizeof(RayTracingOpticalRuntimeInputGpu);
    constexpr usize s_MaxInstances = (Limit<u32>::s_Max - NWB_RT_OPTICAL_SCENE_HEADER_BYTES) / NWB_RT_OPTICAL_INSTANCE_BYTES;
    EXPECT_FALSE(OpticalRuntimeInputCountFits(0u, 0u));
    EXPECT_TRUE(OpticalRuntimeInputCountFits(s_MaxInputs, sizeof(RayTracingOpticalRuntimeInputGpu)));
    EXPECT_FALSE(OpticalRuntimeInputCountFits(s_MaxInputs + 1u, sizeof(RayTracingOpticalRuntimeInputGpu)));
    EXPECT_TRUE(OpticalSceneInstanceCountFits(s_MaxInstances));
    EXPECT_FALSE(OpticalSceneInstanceCountFits(s_MaxInstances + 1u));
}

TEST(OpticalRuntimeBounds, GraphBufferRequiresResourceAndAcceptedUploadOrReuse){
    RayTracingOpticalSceneGraphBuffer result;
    EXPECT_FALSE(result.valid());
    result.resource = { .generation = 1u, .index = 0u };
    EXPECT_FALSE(result.valid());
    result.reused = true;
    EXPECT_TRUE(result.valid());
    result.reused = false;
    result.uploadTask = { .generation = 1u, .index = 0u };
    EXPECT_TRUE(result.valid());
    result.resource.generation = 0u;
    EXPECT_FALSE(result.valid());
}

TEST(OpticalRuntimeBounds, MissingRuntimeBoundsPoisonsUnionWithoutDroppingInstance){
    Core::Alloc::ScratchArena scratch{ Name(NWB::Tests::EcsGraphicsCases::s_EcsCaseTestsOpticalRuntimeBoundsGather) };
    RayTracingOpticalSceneGather gather{ scratch, 8u };
    RendererComponent renderer;
    const auto descriptor = Core::GpuDescriptorHandle::Make(Core::GpuDescriptorClass::StorageBuffer, 5u);
    gather.appendRuntime(Core::ECS::EntityID(1u, 0u), renderer, Core::BufferHandle{}, descriptor, {});
    EXPECT_EQ(gather.instances.size(), 1u);
    EXPECT_EQ(gather.header.transparentCount, 1u);
    EXPECT_TRUE(gather.runtimeBounds.empty());
    EXPECT_FALSE(gather.boundsCompleteExceptRuntime);
    EXPECT_EQ(gather.header.flags & NWB_RT_OPTICAL_SCENE_FLAG_BOUNDS_VALID, 0u);
}

TEST(OpticalRuntimeBounds, RejectedUploadControlDoesNotAllocateOrPublish){
    Core::Alloc::GlobalArena arena{ Name(NWB::Tests::EcsGraphicsCases::s_EcsCaseTestsOpticalRuntimeBoundsRejectedUploadControl) };
    const auto before = arena.memoryStats();
    const auto rejected = CreateRayTracingOpticalUploadControl(arena, Core::BufferHandle{}, Core::GpuPhysicalQueueId{ .index = 0u, .deviceGeneration = 1u });
    ASSERT_FALSE(rejected.has_value());
    const auto after = arena.memoryStats();
    EXPECT_EQ(after.reservedBytes, before.reservedBytes);
    EXPECT_EQ(after.usedBytes, before.usedBytes);
    EXPECT_EQ(after.peakUsedBytes, before.peakUsedBytes);
    EXPECT_EQ(after.allocationCount, before.allocationCount);
    EXPECT_EQ(after.reallocationCount, before.reallocationCount);
    EXPECT_EQ(after.deallocationCount, before.deallocationCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

