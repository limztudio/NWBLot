// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/optical_scene_resources.h>
#include <impl/ecs_render/components.h>

#include <core/alloc/general.h>

#include <gtest/gtest.h>

#include <global/floating_point_environment.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_optical_scene_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;

struct OpticalSceneContext{
    Core::Alloc::GlobalArena arena{ Name("tests/optical_scene/upload") };
    Core::Alloc::ScratchArena scratch{ Name("tests/optical_scene/gather") };
    RayTracingOpticalSceneGather gather{ scratch, 4u };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(OpticalScene, OnlyEmittedTransparentPoliciesRequireClosedMedia){
    OpticalSceneContext context;
    RendererComponent renderer;
    renderer.opticalBoundaryMode = OpticalBoundaryMode::ClosedNested;
    context.gather.append(Core::ECS::EntityID(1u, 0u), renderer, false, {}, {}, false);
    renderer.opticalBoundaryMode = static_cast<OpticalBoundaryMode::Enum>(Limit<u8>::s_Max);
    context.gather.append(Core::ECS::EntityID(s_ExpectedDualCount, 0u), renderer, false, {}, {}, false);
    EXPECT_TRUE(context.gather.unspecifiedBoundariesOnly);
    renderer.opticalBoundaryMode = OpticalBoundaryMode::Unspecified;
    context.gather.append(Core::ECS::EntityID(3u, 0u), renderer, true, {}, {1.f, 1.f, 1.f}, true);
    EXPECT_TRUE(context.gather.unspecifiedBoundariesOnly);
    EXPECT_EQ(context.gather.header.flags, NWB_RT_OPTICAL_SCENE_FLAG_BOUNDS_VALID);
}

TEST(OpticalScene, ClosedAndUnknownTransparentPoliciesRequireGeneralTransport){
    const OpticalBoundaryMode::Enum modes[] = { OpticalBoundaryMode::ClosedNested, OpticalBoundaryMode::ClosedPriority, static_cast<OpticalBoundaryMode::Enum>(Limit<u8>::s_Max), };
    for(const auto mode : modes){
        for(const bool boundsValid : {false, true}){
            OpticalSceneContext context;
            RendererComponent renderer;
            context.gather.append(Core::ECS::EntityID(1u, 0u), renderer, true, {}, {1.f, 1.f, 1.f}, true);
            renderer.opticalBoundaryMode = mode;
            context.gather.append(Core::ECS::EntityID(s_ExpectedDualCount, 0u), renderer, true, {}, {1.f, 1.f, 1.f}, boundsValid);
            renderer.opticalBoundaryMode = OpticalBoundaryMode::Unspecified;
            context.gather.append(Core::ECS::EntityID(3u, 0u), renderer, true, {}, {1.f, 1.f, 1.f}, true);
            EXPECT_FALSE(context.gather.unspecifiedBoundariesOnly);
            EXPECT_EQ(context.gather.header.flags, boundsValid ? NWB_RT_OPTICAL_SCENE_FLAG_BOUNDS_VALID : 0u);
        }
    }
}

TEST(OpticalScene, IncompleteBoundsRemainRejectedWithUnspecifiedPolicySpecialization){
    OpticalSceneContext context;
    RendererComponent renderer;
    context.gather.markIncomplete();
    EXPECT_TRUE(context.gather.unspecifiedBoundariesOnly);
    context.gather.append(Core::ECS::EntityID(1u, 0u), renderer, true, {}, {1.f, 1.f, 1.f}, false);
    EXPECT_TRUE(context.gather.unspecifiedBoundariesOnly);
    EXPECT_EQ(context.gather.header.flags, 0u);
    EXPECT_EQ(context.gather.instances[0].flags, NWB_RT_OPTICAL_INSTANCE_FLAG_TRANSPARENT);
}

TEST(OpticalScene, OpaqueInstancesPreserveOrderWithoutExpandingOpticalBounds){
    OpticalSceneContext context;
    RendererComponent renderer;
    context.gather.append(Core::ECS::EntityID(10u, 0u), renderer, false, { -100.f, -100.f, -100.f }, {}, false);
    context.gather.append(Core::ECS::EntityID(20u, 0u), renderer, true, { -1.f, -2.f, -3.f }, { 1.f, 2.f, 3.f }, true);

    ASSERT_EQ(context.gather.instances.size(), s_ExpectedDualCount);
    EXPECT_EQ(context.gather.instances[0].entityId, Core::ECS::EntityID(10u, 0u).id);
    EXPECT_EQ(context.gather.instances[0].flags, 0u);
    EXPECT_EQ(context.gather.header.transparentCount, 1u);
    EXPECT_EQ(context.gather.header.boundsMin.x, -1.f);
    EXPECT_EQ(context.gather.header.flags, NWB_RT_OPTICAL_SCENE_FLAG_BOUNDS_VALID);
}


TEST(OpticalScene, UntrustedTransparentBoundsCannotBecomeAnAirShortcut){
    OpticalSceneContext context;
    RendererComponent renderer;
    context.gather.append(Core::ECS::EntityID(1u, 0u), renderer, true, { -1.f, -1.f, -1.f }, { 1.f, 1.f, 1.f }, false);
    context.gather.append(Core::ECS::EntityID(s_ExpectedDualCount, 0u), renderer, true, { -2.f, -2.f, -2.f }, { 2.f, 2.f, 2.f }, true);

    EXPECT_EQ(context.gather.header.transparentCount, s_ExpectedDualCount);
    EXPECT_EQ(context.gather.header.flags, 0u);
    EXPECT_EQ(context.gather.instances[0].flags, NWB_RT_OPTICAL_INSTANCE_FLAG_TRANSPARENT);
}

TEST(OpticalScene, NonfiniteAndInvertedBoundsRejectTheProof){
    OpticalSceneContext context;
    RendererComponent renderer;
    const Float3U invalidMins[] = { { Limit<f32>::s_Infinity, 0.f, 0.f }, { 2.f, 0.f, 0.f }, };
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

TEST(OpticalScene, IncompleteBoundsHaveDistinctContentIdentity){
    OpticalSceneContext context;
    RendererComponent renderer;
    context.gather.append(Core::ECS::EntityID(1u, 0u), renderer, true, {}, { 1.f, 1.f, 1.f }, true);
    const u64 initial = context.gather.contentHash();
    context.gather.markIncomplete();
    EXPECT_NE(initial, context.gather.contentHash());
}

TEST(OpticalScene, FrozenUploadOwnsBytesAfterGatherChanges){
    OpticalSceneContext context;
    RendererComponent renderer;
    context.gather.append(Core::ECS::EntityID(7u, s_ExpectedDualCount), renderer, true, {}, { 1.f, 2.f, 3.f }, true);
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
    EXPECT_EQ(storedEntity, Core::ECS::EntityID(7u, s_ExpectedDualCount).id);
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
    const auto bounds = ComputeOpticalWorldBounds(LoadFloat(transform), LoadFloat(localMin), LoadFloat(localMax));
    ASSERT_TRUE(bounds);
    Float3U minimum{};
    Float3U maximum{};
    StoreFloat(bounds->minimum, minimum);
    StoreFloat(bounds->maximum, maximum);
    for(u32 corner = 0u; corner < 8u; ++corner){
        const f64 coordinates[] = { (corner & 1u) ? localMax.x : localMin.x, (corner & s_ExpectedDualCount) ? localMax.y : localMin.y, (corner & 4u) ? localMax.z : localMin.z, };
        for(usize row = 0u; row < 3u; ++row){
            f64 exact = transform.m[row][3u];
            for(usize column = 0u; column < 3u; ++column)
                exact += static_cast<f64>(transform.m[row][column]) * coordinates[column];
            EXPECT_LT(static_cast<f64>(minimum.raw[row]), exact);
            EXPECT_GT(static_cast<f64>(maximum.raw[row]), exact);
        }
    }
}

TEST(OpticalScene, CancellationRetainsOrderedAffineEnclosure){
    volatile f32 inputs[] = { -0x1p+24f, 0x1.000002p+24f, 0x1p+24f, 1.0f, 0x1.fffffep-1f, };
    Float34U transform{};
    transform._11 = inputs[0u];
    transform._12 = inputs[1u];
    transform._21 = inputs[0u];
    transform._22 = inputs[2u];
    transform._23 = inputs[3u];
    transform._31 = inputs[2u];
    transform._32 = inputs[0u];
    transform._33 = inputs[2u];
    transform._34 = inputs[0u];
    const Float3U point(inputs[3u], inputs[4u], inputs[3u]);
    const auto bounds = ComputeOpticalWorldBounds(LoadFloat(transform), LoadFloat(point), LoadFloat(point));
    ASSERT_TRUE(bounds);
    Float3U minimum{};
    Float3U maximum{};
    StoreFloat(bounds->minimum, minimum);
    StoreFloat(bounds->maximum, maximum);

    // X distinguishes fused cancellation, while Z loses its intermediate unit before the final translation; reassociation changes the enclosed point.
#if defined(NWB_HAS_FMA3) || defined(NWB_HAS_NEON64)
    constexpr f64 s_ExpectedX = 0x1.fffffcp-1;
#else
    constexpr f64 s_ExpectedX = 0.0;
#endif
    EXPECT_LT(static_cast<f64>(minimum.x), s_ExpectedX);
    EXPECT_GT(static_cast<f64>(maximum.x), s_ExpectedX);
    for(usize lane = 1u; lane < 3u; ++lane){
        EXPECT_LT(minimum.raw[lane], 0.0f);
        EXPECT_GT(maximum.raw[lane], 0.0f);
        EXPECT_LT(Abs(minimum.raw[lane]), 0x1p-16f);
        EXPECT_LT(Abs(maximum.raw[lane]), 0x1p-16f);
    }
    EXPECT_LT(maximum.x - minimum.x, 0x1p-16f);
}

TEST(OpticalScene, InactiveSignalingNaNLanesPreserveAffineEnclosureAndExceptionFlags){
#pragma STDC FENV_ACCESS ON
    const auto originalEnvironment = HoldFloatingPointExceptions();
    ASSERT_TRUE(originalEnvironment);
    ScopeExit restoreEnvironment([&]()noexcept{
        if(!SetFloatingPointEnvironment(*originalEnvironment))
            NWB_ASSERT(false);
    });

    Float34U transform{};
    transform._11 = -2.f;
    transform._12 = 0.25f;
    transform._14 = 100000.f;
    transform._22 = 3.f;
    transform._24 = -40000.f;
    transform._33 = 0.5f;
    SIMDMatrix objectToWorld = LoadFloat(transform);
    const SIMDVectorSingle localMin = VectorSet(-3.f, -2.f, -1.f, 0.f);
    const SIMDVectorSingle localMax = VectorSet(1.f, 4.f, 5.f, 0.f);
    volatile u32 inactiveBits[]{ 0x7f800123u, 0xff800456u };
    const SIMDVectorSingle poisonedMin = VectorSelect(localMin, VectorReplicateInt(inactiveBits[0]), s_SIMDMaskW);
    const SIMDVectorSingle poisonedMax = VectorSelect(localMax, VectorReplicateInt(inactiveBits[1]), s_SIMDMaskW);
    objectToWorld.v[3] = VectorReplicateInt(inactiveBits[0]);
    for(const i32 roundingMode : { FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO }){
        ASSERT_TRUE(SetFloatingPointRoundingMode(roundingMode));
        ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
        const auto reference = ComputeOpticalWorldBounds(objectToWorld, localMin, localMax);
        const i32 referenceFlags = TestFloatingPointExceptions(FE_ALL_EXCEPT);
        ASSERT_TRUE(reference);
        ASSERT_TRUE(ClearFloatingPointExceptions(FE_ALL_EXCEPT));
        const auto bounds = ComputeOpticalWorldBounds(objectToWorld, poisonedMin, poisonedMax);
        const i32 actualFlags = TestFloatingPointExceptions(FE_ALL_EXCEPT);
        ASSERT_TRUE(bounds);
        EXPECT_EQ(actualFlags, referenceFlags);
        EXPECT_EQ(actualFlags & FE_INVALID, 0);
        EXPECT_EQ(VectorGetIntW(bounds->minimum), 0u);
        EXPECT_EQ(VectorGetIntW(bounds->maximum), 0u);
        EXPECT_TRUE(Vector4EqualInt(bounds->minimum, reference->minimum));
        EXPECT_TRUE(Vector4EqualInt(bounds->maximum, reference->maximum));
    }
}

TEST(OpticalScene, NonfiniteAffineBoundsNeverPublishAPartialResult){
    Float34U transform{};
    transform._11 = 1.f;
    transform._22 = 1.f;
    transform._33 = 1.f;
    transform._34 = Limit<f32>::s_Infinity;
    const auto bounds = ComputeOpticalWorldBounds(LoadFloat(transform), VectorZero(), VectorSet(1.f, 1.f, 1.f, 0.f));
    EXPECT_FALSE(bounds);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

