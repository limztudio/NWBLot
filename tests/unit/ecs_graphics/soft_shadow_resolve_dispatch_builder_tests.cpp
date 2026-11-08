// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/unit/ecs_graphics/name_cases.adhd.h>

#include <impl/ecs_render/shadow/soft_shadow_resolve_dispatch_builder.h>
#include <impl/ecs_render/deferred/deferred_frame_targets.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>

#include <core/alloc/general.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_soft_shadow_resolve_dispatch_builder_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(SoftShadowResolveDispatchBuilder, AlternatingHistoryUsesTheNewMomentsAndGraphOwnedBoundaries){
    Core::Alloc::GlobalArena arena(Tests::EcsGraphicsCases::s_EcsCaseTestsSoftShadowResolveDispatchBuilder);
    RendererRayTracingState state(arena);
    DeferredFrameTargets targets;
    targets.bindless.shadowHistB = Core::GpuDescriptorHandle(31u);
    targets.bindless.shadowMomentsB = Core::GpuDescriptorHandle(32u);
    targets.bindless.shadowSoftHalfA = Core::GpuDescriptorHandle(33u);
    targets.bindless.transparentHistA = Core::GpuDescriptorHandle(41u);
    targets.bindless.transparentMomentsA = Core::GpuDescriptorHandle(42u);
    OpaqueSoftShadowResolveOptions opaque;
    opaque.frontIsA = true;
    opaque.temporalActive = true;
    opaque.dispatchResolve = true;
    opaque.phase = SoftShadowOpaqueResolvePhase::WaveletOnly;
    const SoftShadowResolveDispatch first = SoftShadowResolveDispatchBuilder::Opaque(opaque, state, targets);
    EXPECT_EQ(first.firstWaveletResources.inputColor, 31u);
    EXPECT_EQ(first.firstWaveletResources.moments, 32u);
    EXPECT_TRUE(first.graphOwnsFirstWaveletInputState);
    EXPECT_TRUE(first.graphOwnsWaveletMomentsEntryState);
    EXPECT_TRUE(first.graphOwnsFirstWaveletOutputState);
    EXPECT_FALSE(first.firstWaveletWritesHalfA);
    EXPECT_EQ(first.fold, SoftShadowUpsampleFold::Overwrite);
    opaque.temporalActive = false;
    opaque.phase = SoftShadowOpaqueResolvePhase::TemporalAndWavelet;
    const SoftShadowResolveDispatch noHistory = SoftShadowResolveDispatchBuilder::Opaque(opaque, state, targets);
    EXPECT_EQ(noHistory.firstWaveletResources.inputColor, 33u);
    EXPECT_FALSE(noHistory.graphOwnsFirstWaveletInputState);

    TransparentSoftShadowResolveOptions transparent;
    transparent.frontIsA = false;
    transparent.temporalActive = true;
    transparent.dispatchResolve = true;
    transparent.splitResolve = true;
    const SoftShadowResolveDispatch second = SoftShadowResolveDispatchBuilder::Transparent(transparent, state, targets);
    EXPECT_EQ(second.firstWaveletResources.inputColor, 41u);
    EXPECT_EQ(second.firstWaveletResources.moments, 42u);
    EXPECT_TRUE(second.graphOwnsFirstWaveletInputState);
    EXPECT_TRUE(second.graphOwnsWaveletMomentsEntryState);
    EXPECT_TRUE(second.graphOwnsFirstWaveletOutputState);
    EXPECT_TRUE(second.firstWaveletWritesHalfA);
    EXPECT_EQ(second.fold, SoftShadowUpsampleFold::Multiply);
    transparent.dispatchTemporalMerge = true;
    const SoftShadowResolveDispatch ownMerge = SoftShadowResolveDispatchBuilder::Transparent(transparent, state, targets);
    EXPECT_FALSE(ownMerge.graphOwnsFirstWaveletInputState);
    EXPECT_FALSE(ownMerge.graphOwnsWaveletMomentsEntryState);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

