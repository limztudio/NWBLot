// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/optical_scene_upload.h>
#include <impl/ecs_render/components.h>
#include <impl/assets/graphics/mesh/runtime_bounds_constants.h>

#include <tests/common/graphics_metadata_test_objects.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_optical_runtime_bounds_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using TestArena = Tests::TestArena<struct OpticalRuntimeBoundsTestsTag>;

struct Context{
    TestArena testArena;
    Core::Alloc::ScratchArena scratch{ Name("tests/optical_runtime_bounds/gather") };
    Core::GraphicsAllocator graphicsAllocator{ testArena.arena };
    Core::CpuTaskScheduler cpuScheduler{ 0u };
    Core::GraphicsBackend::VulkanContext context;
    Core::GraphicsBackend::VulkanAllocator allocator;
    RayTracingOpticalSceneGather gather{ scratch, 8u };

    Context()
        : context(graphicsAllocator, cpuScheduler, 1u)
        , allocator(context)
    {}

    [[nodiscard]] Core::BufferHandle makeBounds(){
        Core::BufferDesc desc;
        desc
            .setByteSize(NWB_RUNTIME_MESH_BOUNDS_BYTE_SIZE)
            .setCanHaveRawViews(true)
            .setCanHaveUAVs(true)
            .setDebugName(Name("tests.optical_runtime_bounds.buffer"))
            .enableAutomaticStateTracking(Core::ResourceStates::ShaderResource)
        ;
        Core::Buffer* const buffer = Tests::NewMetadataOnlyBuffer(testArena.arena, context, allocator, desc, true);
        return Core::BufferHandle(buffer, Core::BufferHandle::deleter_type(&testArena.arena), AdoptRef);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(OpticalRuntimeBounds, CurrentPoseBindingPreservesStaticUnionButRequiresGpuValidation){
    Context context;
    RendererComponent renderer;
    const auto buffer = context.makeBounds();
    const auto descriptor = Core::GpuDescriptorHandle::make(Core::GpuDescriptorClass::StorageBuffer, 4u);
    context.gather.append(Core::ECS::EntityID(1u, 0u), renderer, true, {-8.f, -3.f, -2.f}, {-5.f, 2.f, 7.f}, true);
    context.gather.appendRuntime(Core::ECS::EntityID(2u, 0u), renderer, buffer, descriptor, {});

    EXPECT_EQ(context.gather.header.transparentCount, 2u);
    EXPECT_EQ(context.gather.header.flags & NWB_RT_OPTICAL_SCENE_FLAG_BOUNDS_VALID, 0u);
    EXPECT_EQ(context.gather.header.boundsMin.x, -8.f);
    EXPECT_EQ(context.gather.header.boundsMax.z, 7.f);
    EXPECT_TRUE(context.gather.boundsCompleteExceptRuntime);
    ASSERT_EQ(context.gather.runtimeBounds.size(), 1u);
    EXPECT_EQ(context.gather.runtimeBounds[0].instanceIndex, 1u);
    EXPECT_EQ(context.gather.instances[1].flags, NWB_RT_OPTICAL_INSTANCE_FLAG_TRANSPARENT);
}

TEST(OpticalRuntimeBounds, FrozenInputsRetainBufferTransformPolicyAndExactEmittedOrder){
    Context context;
    RendererComponent renderer;
    renderer.opticalBoundaryMode = OpticalBoundaryMode::ClosedPriority;
    renderer.opticalMediumPriority = -11;
    auto buffer = context.makeBounds();
    auto* const identity = buffer.get();
    const auto descriptor = Core::GpuDescriptorHandle::make(Core::GpuDescriptorClass::StorageBuffer, 7u);
    Float34U transform{};
    transform.m[0][0] = -2.f;
    transform.m[1][1] = 0.5f;
    transform.m[2][2] = 3.f;
    transform.m[0][3] = 12.f;
    const auto entity = Core::ECS::EntityID(4u, 1u);
    context.gather.append(Core::ECS::EntityID(3u, 1u), renderer, false, {}, {}, false);
    context.gather.appendRuntime(entity, renderer, buffer, descriptor, transform);
    transform.m[0][3] = -18.f;
    context.gather.appendRuntime(entity, renderer, buffer, descriptor, transform);
    buffer.reset();
    transform.m[0][3] = 999.f;
    renderer.opticalMediumPriority = 22;

    ASSERT_EQ(context.gather.runtimeBounds.size(), 2u);
    EXPECT_EQ(context.gather.runtimeBounds[0].buffer.get(), identity);
    EXPECT_EQ(context.gather.runtimeBounds[1].buffer.get(), identity);
    EXPECT_EQ(context.gather.runtimeBounds[0].instanceIndex, 1u);
    EXPECT_EQ(context.gather.runtimeBounds[1].instanceIndex, 2u);
    EXPECT_EQ(context.gather.runtimeBounds[0].objectToWorld.m[0][3], 12.f);
    EXPECT_EQ(context.gather.runtimeBounds[1].objectToWorld.m[0][3], -18.f);
    EXPECT_EQ(context.gather.instances[1].entityId, entity.id);
    EXPECT_EQ(context.gather.instances[2].entityId, entity.id);
    EXPECT_EQ(context.gather.instances[1].mediumPriority, -11);
    EXPECT_EQ(context.gather.instances[2].boundaryMode, NWB_RT_OPTICAL_BOUNDARY_CLOSED_PRIORITY);
}

TEST(OpticalRuntimeBounds, InvalidContributorBeforeOrAfterRuntimeCannotRecoverCompleteness){
    for(const bool invalidFirst : {false, true}){
        Context context;
        RendererComponent renderer;
        const auto buffer = context.makeBounds();
        const auto descriptor = Core::GpuDescriptorHandle::make(Core::GpuDescriptorClass::StorageBuffer, 3u);
        if(invalidFirst)
            context.gather.markIncomplete();
        context.gather.appendRuntime(Core::ECS::EntityID(1u, 0u), renderer, buffer, descriptor, {});
        if(!invalidFirst)
            context.gather.append(Core::ECS::EntityID(2u, 0u), renderer, true, {}, {}, false);
        context.gather.appendRuntime(Core::ECS::EntityID(3u, 0u), renderer, buffer, descriptor, {});
        EXPECT_FALSE(context.gather.boundsCompleteExceptRuntime);
        EXPECT_EQ(context.gather.header.flags & NWB_RT_OPTICAL_SCENE_FLAG_BOUNDS_VALID, 0u);
        EXPECT_EQ(context.gather.runtimeBounds.size(), 2u);
    }
}

TEST(OpticalRuntimeBounds, MissingOrWrongDescriptorPoisonsUnionWithoutDroppingInstance){
    for(u32 invalidKind = 0u; invalidKind < 3u; ++invalidKind){
        Context context;
        RendererComponent renderer;
        const auto buffer = invalidKind == 0u ? Core::BufferHandle{} : context.makeBounds();
        const auto descriptor = invalidKind == 1u ? Core::GpuDescriptorHandle::invalid()
            : Core::GpuDescriptorHandle::make(invalidKind == 2u ? Core::GpuDescriptorClass::UniformBuffer : Core::GpuDescriptorClass::StorageBuffer, 5u);
        context.gather.appendRuntime(Core::ECS::EntityID(1u, 0u), renderer, buffer, descriptor, {});
        EXPECT_EQ(context.gather.instances.size(), 1u);
        EXPECT_EQ(context.gather.header.transparentCount, 1u);
        EXPECT_TRUE(context.gather.runtimeBounds.empty());
        EXPECT_FALSE(context.gather.boundsCompleteExceptRuntime);
        EXPECT_EQ(context.gather.header.flags & NWB_RT_OPTICAL_SCENE_FLAG_BOUNDS_VALID, 0u);
    }
}

TEST(OpticalRuntimeBounds, CpuUploadNeverPublishesUnvalidatedRuntimeBounds){
    Context context;
    RendererComponent renderer;
    const auto buffer = context.makeBounds();
    const auto descriptor = Core::GpuDescriptorHandle::make(Core::GpuDescriptorClass::StorageBuffer, 2u);
    context.gather.appendRuntime(Core::ECS::EntityID(1u, 0u), renderer, buffer, descriptor, {});
    RayTracingOpticalSceneUpload upload(context.testArena.arena, context.gather);
    u32 headerFlags = Limit<u32>::s_Max;
    u32 instanceFlags = Limit<u32>::s_Max;
    NWB_MEMCPY(&headerFlags, sizeof(headerFlags), upload.bytes.data() + NWB_RT_OPTICAL_SCENE_FLAGS_OFFSET, sizeof(u32));
    NWB_MEMCPY(&instanceFlags, sizeof(instanceFlags), upload.bytes.data() + NWB_RT_OPTICAL_SCENE_HEADER_BYTES + NWB_RT_OPTICAL_INSTANCE_FLAGS_OFFSET, sizeof(u32));
    EXPECT_EQ(headerFlags & NWB_RT_OPTICAL_SCENE_FLAG_BOUNDS_VALID, 0u);
    EXPECT_EQ(instanceFlags, NWB_RT_OPTICAL_INSTANCE_FLAG_TRANSPARENT);
    const auto before = context.gather.contentHash();
    context.gather.runtimeBounds[0].objectToWorld.m[0][3] = 7.f;
    EXPECT_NE(context.gather.contentHash(), before);
    EXPECT_EQ(upload.instanceCount, 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

