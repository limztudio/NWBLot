// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/optical_scene_upload.h>
#include <impl/ecs_render/components.h>
#include <impl/assets/graphics/mesh/runtime_bounds_constants.h>

#include <core/alloc/general.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_optical_runtime_bounds_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(OpticalRuntimeBounds, MissingRuntimeBoundsPoisonsUnionWithoutDroppingInstance){
    Core::Alloc::ScratchArena scratch{ Name("tests/optical_runtime_bounds/gather") };
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
    Core::Alloc::GlobalArena arena{ Name("tests/optical_runtime_bounds/rejected_upload_control") };
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

