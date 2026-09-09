// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/optical_scene_resources.h>
#include <impl/ecs_render/components.h>

#include <core/alloc/general.h>
#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_optical_scene_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;

struct OpticalSceneContext{
    Core::Alloc::GlobalArena arena{ Name("tests/optical_scene/upload") };
    Core::Alloc::ScratchArena scratch{ Name("tests/optical_scene/gather") };
    RayTracingOpticalSceneGather gather{ scratch, 4u };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(OpticalScene, UnspecifiedTransparentBoundsParticipateInAirProof){
    OpticalSceneContext context;
    RendererComponent renderer;
    context.gather.append(Core::ECS::EntityID(1u, 0u), renderer, true, { -3.f, -2.f, -1.f }, { 4.f, 5.f, 6.f }, true);

    EXPECT_EQ(context.gather.header.transparentCount, 1u);
    EXPECT_EQ(context.gather.header.flags, NWB_RT_OPTICAL_SCENE_FLAG_BOUNDS_VALID);
    EXPECT_EQ(context.gather.header.boundsMin.x, -3.f);
    EXPECT_EQ(context.gather.header.boundsMax.z, 6.f);
    ASSERT_EQ(context.gather.instances.size(), 1u);
    EXPECT_EQ(context.gather.instances[0].boundaryMode, NWB_RT_OPTICAL_BOUNDARY_UNSPECIFIED);
    EXPECT_EQ(context.gather.instances[0].flags,
        NWB_RT_OPTICAL_INSTANCE_FLAG_TRANSPARENT | NWB_RT_OPTICAL_INSTANCE_FLAG_BOUNDS_VALID);
}

TEST(OpticalScene, OpaqueInstancesPreserveOrderWithoutExpandingOpticalBounds){
    OpticalSceneContext context;
    RendererComponent renderer;
    context.gather.append(Core::ECS::EntityID(10u, 0u), renderer, false, { -100.f, -100.f, -100.f }, {}, false);
    context.gather.append(Core::ECS::EntityID(20u, 0u), renderer, true, { -1.f, -2.f, -3.f }, { 1.f, 2.f, 3.f }, true);

    ASSERT_EQ(context.gather.instances.size(), 2u);
    EXPECT_EQ(context.gather.instances[0].entityId, Core::ECS::EntityID(10u, 0u).id);
    EXPECT_EQ(context.gather.instances[0].flags, 0u);
    EXPECT_EQ(context.gather.header.transparentCount, 1u);
    EXPECT_EQ(context.gather.header.boundsMin.x, -1.f);
    EXPECT_EQ(context.gather.header.flags, NWB_RT_OPTICAL_SCENE_FLAG_BOUNDS_VALID);
}

TEST(OpticalScene, UnionIncludesDisjointNestedAndPriorityParticipants){
    OpticalSceneContext context;
    RendererComponent renderer;
    renderer.opticalBoundaryMode = OpticalBoundaryMode::ClosedNested;
    context.gather.append(Core::ECS::EntityID(1u, 0u), renderer, true, { -5.f, -1.f, -2.f }, { -2.f, 2.f, 3.f }, true);
    renderer.opticalBoundaryMode = OpticalBoundaryMode::ClosedPriority;
    renderer.opticalMediumPriority = -7;
    renderer.opticalVolumePriority = 100;
    context.gather.append(Core::ECS::EntityID(2u, 1u), renderer, true, { 1.f, -3.f, -1.f }, { 6.f, 1.f, 7.f }, true);

    EXPECT_EQ(context.gather.header.transparentCount, 2u);
    EXPECT_EQ(context.gather.header.boundsMin.x, -5.f);
    EXPECT_EQ(context.gather.header.boundsMin.y, -3.f);
    EXPECT_EQ(context.gather.header.boundsMax.x, 6.f);
    EXPECT_EQ(context.gather.header.boundsMax.z, 7.f);
    EXPECT_EQ(context.gather.instances[1].mediumPriority, -7);
    EXPECT_EQ(context.gather.instances[1].boundaryMode, NWB_RT_OPTICAL_BOUNDARY_CLOSED_PRIORITY);
    EXPECT_EQ(context.gather.instances[1].entityId, Core::ECS::EntityID(2u, 1u).id);
}

TEST(OpticalScene, UntrustedTransparentBoundsCannotBecomeAnAirShortcut){
    OpticalSceneContext context;
    RendererComponent renderer;
    context.gather.append(Core::ECS::EntityID(1u, 0u), renderer, true, { -1.f, -1.f, -1.f }, { 1.f, 1.f, 1.f }, false);
    context.gather.append(Core::ECS::EntityID(2u, 0u), renderer, true, { -2.f, -2.f, -2.f }, { 2.f, 2.f, 2.f }, true);

    EXPECT_EQ(context.gather.header.transparentCount, 2u);
    EXPECT_EQ(context.gather.header.flags, 0u);
    EXPECT_EQ(context.gather.instances[0].flags, NWB_RT_OPTICAL_INSTANCE_FLAG_TRANSPARENT);
}

TEST(OpticalScene, NonfiniteAndInvertedBoundsRejectTheProof){
    OpticalSceneContext context;
    RendererComponent renderer;
    const Float3U invalidMins[] = {
        { Limit<f32>::s_Infinity, 0.f, 0.f }, { 2.f, 0.f, 0.f },
    };
    for(const auto& boundsMin : invalidMins){
        RayTracingOpticalSceneGather gather(context.scratch, 1u);
        gather.append(Core::ECS::EntityID(1u, 0u), renderer, true, boundsMin, { 1.f, 1.f, 1.f }, true);
        EXPECT_EQ(gather.header.flags, 0u);
        EXPECT_EQ(gather.instances[0].flags, NWB_RT_OPTICAL_INSTANCE_FLAG_TRANSPARENT);
    }
}

TEST(OpticalScene, MissingGeometryInvalidatesEvenAnOtherwiseEmptyScene){
    OpticalSceneContext context;
    EXPECT_EQ(context.gather.header.flags, NWB_RT_OPTICAL_SCENE_FLAG_BOUNDS_VALID);
    context.gather.markIncomplete();
    EXPECT_EQ(context.gather.header.transparentCount, 0u);
    EXPECT_EQ(context.gather.header.flags, 0u);
}

TEST(OpticalScene, PolicyAndIncompleteBoundsChangeSemanticContentIdentity){
    OpticalSceneContext context;
    RendererComponent renderer;
    context.gather.append(Core::ECS::EntityID(1u, 0u), renderer, true, {}, { 1.f, 1.f, 1.f }, true);
    const u64 initial = context.gather.contentHash();
    context.gather.instances[0].mediumPriority = 4;
    const u64 changedPriority = context.gather.contentHash();
    EXPECT_NE(initial, changedPriority);
    context.gather.instances[0].boundaryMode = NWB_RT_OPTICAL_BOUNDARY_CLOSED_PRIORITY;
    const u64 changedMode = context.gather.contentHash();
    EXPECT_NE(changedPriority, changedMode);
    context.gather.markIncomplete();
    EXPECT_NE(changedMode, context.gather.contentHash());
}

TEST(OpticalScene, FrozenUploadOwnsBytesAfterGatherChanges){
    OpticalSceneContext context;
    RendererComponent renderer;
    context.gather.append(Core::ECS::EntityID(7u, 2u), renderer, true, {}, { 1.f, 2.f, 3.f }, true);
    const RayTracingOpticalSceneUpload upload(context.arena, context.gather);
    context.gather.instances.clear();
    context.gather.header.transparentCount = 0u;

    ASSERT_EQ(upload.bytes.size(), NWB_RT_OPTICAL_SCENE_HEADER_BYTES + NWB_RT_OPTICAL_INSTANCE_BYTES);
    EXPECT_EQ(upload.instanceCount, 1u);
    u32 storedCount = 0u;
    u32 storedEntity = 0u;
    NWB_MEMCPY(&storedCount, sizeof(storedCount), upload.bytes.data() + NWB_RT_OPTICAL_SCENE_TRANSPARENT_COUNT_OFFSET, sizeof(u32));
    NWB_MEMCPY(&storedEntity, sizeof(storedEntity), upload.bytes.data() + NWB_RT_OPTICAL_SCENE_HEADER_BYTES, sizeof(u32));
    EXPECT_EQ(storedCount, 1u);
    EXPECT_EQ(storedEntity, Core::ECS::EntityID(7u, 2u).id);
}

TEST(OpticalScene, HybridOrderComparisonUsesFullEntityGenerationAndLength){
    RayTracingOpticalInstanceGpu first[] = {
        { Core::ECS::EntityID(1u, 0u).id }, { Core::ECS::EntityID(2u, 3u).id },
    };
    RayTracingOpticalInstanceGpu second[] = { first[0], first[1] };
    const auto* const bytes = reinterpret_cast<const u8*>(first);
    EXPECT_TRUE(OpticalInstanceOrderMatches(bytes, LengthOf(first), second, LengthOf(second)));
    second[1].entityId = Core::ECS::EntityID(2u, 4u).id;
    EXPECT_FALSE(OpticalInstanceOrderMatches(bytes, LengthOf(first), second, LengthOf(second)));
    second[1] = first[1];
    Swap(second[0], second[1]);
    EXPECT_FALSE(OpticalInstanceOrderMatches(bytes, LengthOf(first), second, LengthOf(second)));
    EXPECT_FALSE(OpticalInstanceOrderMatches(bytes, LengthOf(first), second, 1u));
    EXPECT_FALSE(OpticalInstanceOrderMatches(nullptr, 1u, second, 1u));
    EXPECT_TRUE(OpticalInstanceOrderMatches(nullptr, 0u, nullptr, 0u));
}

TEST(OpticalScene, AffineBoundsEncloseMirroringNonuniformScaleAndLargeTranslation){
    Float34U transform{};
    transform._11 = -2.f;
    transform._12 = 0.25f;
    transform._14 = 100000.f;
    transform._22 = 3.f;
    transform._24 = -40000.f;
    transform._33 = 0.5f;
    const Float3U localMin(-3.f, -2.f, -1.f);
    const Float3U localMax(1.f, 4.f, 5.f);
    Float3U minimum{};
    Float3U maximum{};
    ASSERT_TRUE(ComputeOpticalWorldBounds(transform, localMin, localMax, minimum, maximum));
    for(u32 corner = 0u; corner < 8u; ++corner){
        const f64 coordinates[] = {
            (corner & 1u) ? localMax.x : localMin.x,
            (corner & 2u) ? localMax.y : localMin.y,
            (corner & 4u) ? localMax.z : localMin.z,
        };
        for(usize row = 0u; row < 3u; ++row){
            f64 exact = transform.m[row][3u];
            for(usize column = 0u; column < 3u; ++column)
                exact += static_cast<f64>(transform.m[row][column]) * coordinates[column];
            EXPECT_LT(static_cast<f64>(minimum.raw[row]), exact);
            EXPECT_GT(static_cast<f64>(maximum.raw[row]), exact);
        }
    }
}

TEST(OpticalScene, NonfiniteAffineBoundsNeverPublishAPartialResult){
    Float34U transform{};
    transform._11 = 1.f;
    transform._22 = 1.f;
    transform._33 = 1.f;
    transform._34 = Limit<f32>::s_Infinity;
    Float3U minimum(7.f, 8.f, 9.f);
    Float3U maximum(10.f, 11.f, 12.f);
    EXPECT_FALSE(ComputeOpticalWorldBounds(transform, {}, { 1.f, 1.f, 1.f }, minimum, maximum));
    EXPECT_EQ(minimum.x, 7.f);
    EXPECT_EQ(maximum.z, 12.f);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

