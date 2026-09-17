// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/optics/coincident_volumes.h>
#include <impl/ecs_render/optics/performance_profile.h>
#include <impl/ecs_render/optics/secondary_geometry_contract.h>

#include <core/alloc/general.h>
#include <core/alloc/scratch.h>

#include <global/limit.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_coincident_optical_volumes_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;

struct SelectionContext{
    Core::Alloc::GlobalArena arena{ Name("tests/coincident_optical_volumes/selection") };
    Core::Alloc::ScratchArena scratch{ Name("tests/coincident_optical_volumes/scratch") };
    RendererOpticalVolumeSelection selection{ arena };
};

[[nodiscard]] CoincidentOpticalVolumeCandidate MakeCandidate(const u32 entityIndex){
    CoincidentOpticalVolumeCandidate candidate;
    candidate.entity = Core::ECS::EntityID(entityIndex, 0u);
    candidate.mesh = Core::Assets::AssetRef<Mesh>("tests/coincident_optical_volumes/mesh");
    candidate.material = Core::Assets::AssetRef<Material>("tests/coincident_optical_volumes/glass");
    return candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// P9 decision gate: no proxy or light-space representation ships unqualified. Both descriptors fail closed until measured evidence plus visual approval qualify them.
TEST(CoincidentOpticalVolumes, SecondaryRepresentationsFailClosedUntilQualified){
    Impl::SecondaryEffectGeometryProxyDescriptor proxy{};
    EXPECT_FALSE(proxy.usable());
    proxy.qualified = true;
    EXPECT_FALSE(proxy.usable());
    Impl::LightSpaceTransmissionDescriptor field{};
    EXPECT_FALSE(field.usable());
    field.qualified = true;
    EXPECT_FALSE(field.usable());
    field.sliceCount = 16u;
    field.nearDepth = 1.0f;
    field.farDepth = 100.0f;
    EXPECT_TRUE(field.usable());
}


TEST(CoincidentOpticalVolumes, AutomaticSelectionComparesBytesFromSeparateAllocations){
    SelectionContext context;
    const u8 firstBytes[] = { 0u, 17u, 255u, 64u };
    const u8 secondBytes[] = { 0u, 17u, 255u, 64u };
    CoincidentOpticalVolumeCandidate candidates[] = { MakeCandidate(20u), MakeCandidate(10u), MakeCandidate(30u) };
    candidates[0].mutableTypedBytes = firstBytes;
    candidates[1].mutableTypedBytes = secondBytes;
    candidates[2].mutableTypedBytes = firstBytes;
    for(auto& candidate : candidates)
        candidate.mutableTypedByteCount = sizeof(firstBytes);

    context.selection.select(candidates, LengthOf(candidates), context.scratch);

    EXPECT_TRUE(context.selection.isSuppressed(candidates[0].entity));
    EXPECT_FALSE(context.selection.isSuppressed(candidates[1].entity));
    EXPECT_TRUE(context.selection.isSuppressed(candidates[2].entity));
    EXPECT_FALSE(context.selection.isSuppressed(Core::ECS::EntityID(99u, 0u)));
}

TEST(CoincidentOpticalVolumes, AutomaticSelectionKeepsDifferentByteValuesAndLengths){
    SelectionContext context;
    const u8 firstBytes[] = { 1u, 2u, 3u, 4u };
    const u8 changedBytes[] = { 1u, 2u, 3u, 5u };
    CoincidentOpticalVolumeCandidate candidates[] = {
        MakeCandidate(10u), MakeCandidate(20u), MakeCandidate(30u), MakeCandidate(40u)
    };
    candidates[0].mutableTypedBytes = firstBytes;
    candidates[0].mutableTypedByteCount = sizeof(firstBytes);
    candidates[1].mutableTypedBytes = changedBytes;
    candidates[1].mutableTypedByteCount = sizeof(changedBytes);
    candidates[2].mutableTypedBytes = firstBytes;
    candidates[2].mutableTypedByteCount = sizeof(firstBytes) - 1u;
    candidates[1].priority = 100;

    context.selection.select(candidates, LengthOf(candidates), context.scratch);

    for(const auto& candidate : candidates)
        EXPECT_FALSE(context.selection.isSuppressed(candidate.entity));
}

TEST(CoincidentOpticalVolumes, IdenticalMaterialGroupingKeepsDistinctMediumSemantics){
    SelectionContext context;
    CoincidentOpticalVolumeCandidate candidates[] = {
        MakeCandidate(10u), MakeCandidate(20u), MakeCandidate(30u), MakeCandidate(40u)
    };
    candidates[1].boundaryMode = 1u;
    candidates[2].boundaryMode = 2u;
    candidates[3].boundaryMode = 2u;
    candidates[3].mediumPriority = 50;
    context.selection.select(candidates, LengthOf(candidates), context.scratch);
    for(const auto& candidate : candidates)
        EXPECT_FALSE(context.selection.isSuppressed(candidate.entity));

    candidates[3].mediumPriority = 0;
    context.selection.select(candidates, LengthOf(candidates), context.scratch);
    EXPECT_FALSE(context.selection.isSuppressed(candidates[2].entity));
    EXPECT_TRUE(context.selection.isSuppressed(candidates[3].entity));
}

TEST(CoincidentOpticalVolumes, AuthoredGroupRepresentativeSuppliesItsCompleteMediumPolicy){
    SelectionContext context;
    CoincidentOpticalVolumeCandidate candidates[] = {MakeCandidate(10u), MakeCandidate(20u)};
    for(auto& candidate : candidates)
        candidate.group = Name("tests/coincident_optical_volumes/policy_override");
    candidates[0].boundaryMode = 1u;
    candidates[0].mediumPriority = 100;
    candidates[1].boundaryMode = 2u;
    candidates[1].mediumPriority = -10;
    candidates[1].priority = 20;
    context.selection.select(candidates, LengthOf(candidates), context.scratch);
    EXPECT_TRUE(context.selection.isSuppressed(candidates[0].entity));
    EXPECT_FALSE(context.selection.isSuppressed(candidates[1].entity));
}

TEST(CoincidentOpticalVolumes, ExplicitGroupUsesPriorityAcrossDifferentSurfaceBytes){
    SelectionContext context;
    const u8 firstBytes[] = { 7u, 8u, 9u, 10u };
    const u8 secondBytes[] = { 11u, 12u };
    CoincidentOpticalVolumeCandidate candidates[] = { MakeCandidate(10u), MakeCandidate(30u), MakeCandidate(20u) };
    for(auto& candidate : candidates)
        candidate.group = Name("tests/coincident_optical_volumes/authored_volume");
    candidates[0].mutableTypedBytes = firstBytes;
    candidates[0].mutableTypedByteCount = sizeof(firstBytes);
    candidates[0].priority = -10;
    candidates[1].mutableTypedBytes = secondBytes;
    candidates[1].mutableTypedByteCount = sizeof(secondBytes);
    candidates[1].priority = 5;
    candidates[2].priority = 4;

    context.selection.select(candidates, LengthOf(candidates), context.scratch);

    EXPECT_TRUE(context.selection.isSuppressed(candidates[0].entity));
    EXPECT_FALSE(context.selection.isSuppressed(candidates[1].entity));
    EXPECT_TRUE(context.selection.isSuppressed(candidates[2].entity));
}

TEST(CoincidentOpticalVolumes, PriorityTiesUseFullEntityIdRegardlessOfInputPermutation){
    SelectionContext context;
    CoincidentOpticalVolumeCandidate source[] = { MakeCandidate(200u), MakeCandidate(1u), MakeCandidate(300u) };
    source[1].entity = Core::ECS::EntityID(1u, 1u);
    for(auto& candidate : source){
        candidate.group = Name("tests/coincident_optical_volumes/tie");
        candidate.priority = 3;
    }
    const u32 permutations[][3u] = {
        { 0u, 1u, 2u }, { 0u, 2u, 1u }, { 1u, 0u, 2u },
        { 1u, 2u, 0u }, { 2u, 0u, 1u }, { 2u, 1u, 0u }
    };

    for(const auto& permutation : permutations){
        const CoincidentOpticalVolumeCandidate candidates[] = {
            source[permutation[0]], source[permutation[1]], source[permutation[2]]
        };
        context.selection.select(candidates, LengthOf(candidates), context.scratch);
        EXPECT_FALSE(context.selection.isSuppressed(source[0].entity));
        EXPECT_TRUE(context.selection.isSuppressed(source[1].entity));
        EXPECT_TRUE(context.selection.isSuppressed(source[2].entity));
    }
}

TEST(CoincidentOpticalVolumes, GroupsMeshAndMaterialRemainIndependentBoundaries){
    SelectionContext context;
    CoincidentOpticalVolumeCandidate candidates[] = {
        MakeCandidate(10u), MakeCandidate(20u), MakeCandidate(30u), MakeCandidate(40u), MakeCandidate(50u),
        MakeCandidate(60u), MakeCandidate(70u), MakeCandidate(80u), MakeCandidate(90u)
    };
    const Name firstGroup("tests/coincident_optical_volumes/first");
    const Name secondGroup("tests/coincident_optical_volumes/second");
    candidates[2].group = firstGroup;
    candidates[3].group = firstGroup;
    candidates[4].group = secondGroup;
    candidates[5].group = secondGroup;
    candidates[6].group = firstGroup;
    candidates[6].mesh = Core::Assets::AssetRef<Mesh>("tests/coincident_optical_volumes/other_mesh");
    candidates[7].group = firstGroup;
    candidates[7].material = Core::Assets::AssetRef<Material>("tests/coincident_optical_volumes/other_glass");
    candidates[8].group = Name("tests/coincident_optical_volumes/third");

    context.selection.select(candidates, LengthOf(candidates), context.scratch);

    for(usize index = 0u; index < LengthOf(candidates); ++index){
        const bool expectedSuppressed = index == 1u || index == 3u || index == 5u;
        EXPECT_EQ(context.selection.isSuppressed(candidates[index].entity), expectedSuppressed) << index;
    }
}

TEST(CoincidentOpticalVolumes, TinyTranslationDifferencesAreNotCoincidentEvenInExplicitGroup){
    SelectionContext context;
    CoincidentOpticalVolumeCandidate candidates[] = {
        MakeCandidate(10u), MakeCandidate(20u), MakeCandidate(30u), MakeCandidate(40u)
    };
    for(auto& candidate : candidates)
        candidate.group = Name("tests/coincident_optical_volumes/translation");
    candidates[1].position.x = 0.0000001f;
    candidates[2].position.y = 0.0000001f;
    candidates[3].position.z = 0.0000001f;

    context.selection.select(candidates, LengthOf(candidates), context.scratch);

    for(const auto& candidate : candidates)
        EXPECT_FALSE(context.selection.isSuppressed(candidate.entity));
}

TEST(CoincidentOpticalVolumes, RotationAndScaleDifferencesPreserveDistinctBoundaries){
    SelectionContext context;
    CoincidentOpticalVolumeCandidate candidates[] = {
        MakeCandidate(10u), MakeCandidate(20u), MakeCandidate(30u), MakeCandidate(40u),
        MakeCandidate(50u), MakeCandidate(60u), MakeCandidate(70u), MakeCandidate(80u)
    };
    for(auto& candidate : candidates)
        candidate.group = Name("tests/coincident_optical_volumes/transforms");
    candidates[1].rotation = Float4U(0.6f, 0.f, 0.f, 0.8f);
    candidates[2].rotation = Float4U(0.f, 0.6f, 0.f, 0.8f);
    candidates[3].rotation = Float4U(0.f, 0.f, 0.6f, 0.8f);
    candidates[4].rotation = Float4U(0.6f, 0.f, 0.f, -0.8f);
    candidates[5].scale.x = 1.000001f;
    candidates[6].scale.y = 1.000001f;
    candidates[7].scale.z = 1.000001f;

    context.selection.select(candidates, LengthOf(candidates), context.scratch);

    for(const auto& candidate : candidates)
        EXPECT_FALSE(context.selection.isSuppressed(candidate.entity));
}

TEST(CoincidentOpticalVolumes, SignedZeroTransformLanesHaveTheSameIdentity){
    SelectionContext context;
    CoincidentOpticalVolumeCandidate candidates[] = { MakeCandidate(20u), MakeCandidate(10u) };
    candidates[0].position = Float3U(-0.f, 0.f, -0.f);
    candidates[0].rotation = Float4U(-0.f, 0.f, -0.f, 1.f);
    candidates[1].position = Float3U(0.f, -0.f, 0.f);
    candidates[1].rotation = Float4U(0.f, -0.f, 0.f, 1.f);

    context.selection.select(candidates, LengthOf(candidates), context.scratch);

    EXPECT_TRUE(context.selection.isSuppressed(candidates[0].entity));
    EXPECT_FALSE(context.selection.isSuppressed(candidates[1].entity));
}

TEST(CoincidentOpticalVolumes, InvalidCandidatesCannotSuppressEachOtherOrValidGeometry){
    SelectionContext context;
    CoincidentOpticalVolumeCandidate invalidCases[13u];
    for(usize index = 0u; index < LengthOf(invalidCases); ++index){
        invalidCases[index] = MakeCandidate(static_cast<u32>(index) + 100u);
        invalidCases[index].priority = 100;
    }
    invalidCases[0].entity = Core::ECS::ENTITY_ID_INVALID;
    invalidCases[1].mesh.reset();
    invalidCases[2].material.reset();
    invalidCases[3].position.x = Limit<f32>::s_QuietNaN;
    invalidCases[4].position.z = Limit<f32>::s_Infinity;
    invalidCases[5].rotation.y = Limit<f32>::s_Infinity;
    invalidCases[6].scale.y = Limit<f32>::s_Infinity;
    invalidCases[7].scale.x = 0.f;
    invalidCases[8].scale.y = -0.f;
    invalidCases[9].scale.z = 0.f;
    invalidCases[10].rotation = Float4U(0.f, 0.f, 0.f, 0.f);
    invalidCases[11].mutableTypedByteCount = 4u;
    invalidCases[12].mutableTypedByteCount = 4u;
    invalidCases[12].group = Name("tests/coincident_optical_volumes/invalid_group_bytes");

    for(const auto& invalidCase : invalidCases){
        CoincidentOpticalVolumeCandidate candidates[] = { MakeCandidate(10u), invalidCase, invalidCase };
        if(candidates[2].entity.valid())
            candidates[2].entity = Core::ECS::EntityID(candidates[1].entity.index() + 100u, 0u);
        else
            candidates[2].entity = Core::ECS::EntityID(candidates[1].entity.index(), 0u);

        context.selection.select(candidates, LengthOf(candidates), context.scratch);

        for(const auto& candidate : candidates)
            EXPECT_FALSE(context.selection.isSuppressed(candidate.entity)) << candidate.entity.id;
    }
}

TEST(CoincidentOpticalVolumes, SelectionOwnsOnlyItsResultAfterBorrowedInputsChangeOrExpire){
    SelectionContext context;
    const Core::ECS::EntityID retained(10u, 0u);
    const Core::ECS::EntityID suppressed(20u, 0u);
    {
        u8 firstBytes[] = { 1u, 2u, 3u, 4u };
        u8 secondBytes[] = { 1u, 2u, 3u, 4u };
        CoincidentOpticalVolumeCandidate candidates[] = { MakeCandidate(10u), MakeCandidate(20u) };
        candidates[0].mutableTypedBytes = firstBytes;
        candidates[1].mutableTypedBytes = secondBytes;
        candidates[0].mutableTypedByteCount = sizeof(firstBytes);
        candidates[1].mutableTypedByteCount = sizeof(secondBytes);
        context.selection.select(candidates, LengthOf(candidates), context.scratch);

        secondBytes[0] = 99u;
        candidates[0].entity = Core::ECS::EntityID(100u, 0u);
        candidates[1].entity = Core::ECS::EntityID(200u, 0u);
        EXPECT_FALSE(context.selection.isSuppressed(retained));
        EXPECT_TRUE(context.selection.isSuppressed(suppressed));
        EXPECT_FALSE(context.selection.isSuppressed(candidates[1].entity));
    }

    EXPECT_FALSE(context.selection.isSuppressed(retained));
    EXPECT_TRUE(context.selection.isSuppressed(suppressed));

    const CoincidentOpticalVolumeCandidate nextFrame[] = { MakeCandidate(30u), MakeCandidate(40u) };
    context.selection.select(nextFrame, LengthOf(nextFrame), context.scratch);
    EXPECT_FALSE(context.selection.isSuppressed(suppressed));
    EXPECT_FALSE(context.selection.isSuppressed(nextFrame[0].entity));
    EXPECT_TRUE(context.selection.isSuppressed(nextFrame[1].entity));
}

TEST(CoincidentOpticalVolumes, NextFrameReevaluatesSurfaceBytesPriorityAndRemovedRepresentatives){
    SelectionContext context;
    u8 firstBytes[] = { 10u, 20u, 30u, 40u };
    u8 secondBytes[] = { 10u, 20u, 30u, 40u };
    CoincidentOpticalVolumeCandidate candidates[] = { MakeCandidate(10u), MakeCandidate(20u), MakeCandidate(30u) };
    for(auto& candidate : candidates){
        candidate.mutableTypedBytes = firstBytes;
        candidate.mutableTypedByteCount = sizeof(firstBytes);
    }
    candidates[1].mutableTypedBytes = secondBytes;
    context.selection.select(candidates, LengthOf(candidates), context.scratch);
    EXPECT_TRUE(context.selection.isSuppressed(candidates[1].entity));
    EXPECT_TRUE(context.selection.isSuppressed(candidates[2].entity));

    secondBytes[3] = 99u;
    context.selection.select(candidates, LengthOf(candidates), context.scratch);
    EXPECT_FALSE(context.selection.isSuppressed(candidates[1].entity));
    EXPECT_TRUE(context.selection.isSuppressed(candidates[2].entity));

    secondBytes[3] = firstBytes[3];
    candidates[2].priority = 1;
    context.selection.select(candidates, LengthOf(candidates), context.scratch);
    EXPECT_TRUE(context.selection.isSuppressed(candidates[0].entity));
    EXPECT_TRUE(context.selection.isSuppressed(candidates[1].entity));
    EXPECT_FALSE(context.selection.isSuppressed(candidates[2].entity));

    context.selection.select(candidates, 2u, context.scratch);
    EXPECT_FALSE(context.selection.isSuppressed(candidates[0].entity));
    EXPECT_TRUE(context.selection.isSuppressed(candidates[1].entity));
    EXPECT_FALSE(context.selection.isSuppressed(candidates[2].entity));
}

TEST(CoincidentOpticalVolumes, ResetEmptyAndSingleCandidateClearPriorSuppression){
    SelectionContext context;
    const CoincidentOpticalVolumeCandidate candidates[] = { MakeCandidate(10u), MakeCandidate(20u) };
    context.selection.select(candidates, LengthOf(candidates), context.scratch);
    ASSERT_TRUE(context.selection.isSuppressed(candidates[1].entity));
    context.selection.reset();
    EXPECT_FALSE(context.selection.isSuppressed(candidates[1].entity));

    context.selection.select(candidates, LengthOf(candidates), context.scratch);
    context.selection.select(nullptr, 0u, context.scratch);
    EXPECT_FALSE(context.selection.isSuppressed(candidates[1].entity));

    context.selection.select(candidates, LengthOf(candidates), context.scratch);
    context.selection.select(&candidates[1], 1u, context.scratch);
    EXPECT_FALSE(context.selection.isSuppressed(candidates[0].entity));
    EXPECT_FALSE(context.selection.isSuppressed(candidates[1].entity));
    EXPECT_FALSE(context.selection.isSuppressed(Core::ECS::ENTITY_ID_INVALID));
}

TEST(CoincidentOpticalVolumes, EmptyAndSingleCandidateFastPathsDoNotAllocate){
    SelectionContext context;
    const CoincidentOpticalVolumeCandidate candidate = MakeCandidate(10u);
    const ArenaMemoryStats ownerBefore = context.arena.memoryStats();
    const ArenaMemoryStats scratchBefore = context.scratch.memoryStats();

    context.selection.select(nullptr, 0u, context.scratch);
    context.selection.select(&candidate, 1u, context.scratch);
    context.selection.reset();

    const ArenaMemoryStats ownerAfter = context.arena.memoryStats();
    const ArenaMemoryStats scratchAfter = context.scratch.memoryStats();
    EXPECT_FALSE(context.selection.isSuppressed(candidate.entity));
    EXPECT_EQ(ownerAfter.allocationCount, ownerBefore.allocationCount);
    EXPECT_EQ(ownerAfter.reallocationCount, ownerBefore.reallocationCount);
    EXPECT_EQ(scratchAfter.allocationCount, scratchBefore.allocationCount);
    EXPECT_EQ(scratchAfter.reallocationCount, scratchBefore.reallocationCount);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// P10 reference gate: agreed scene, resolution, geometry, ray budgets, and optical model stay at
// full extent, and no approximation ships without measured qualification. Any workload reduction is
// a new definition, not a faster profile of the same workload.
TEST(CoincidentOpticalVolumes, ReferencePerformanceProfileKeepsFullExtentWithoutWorkloadChange){
    const Impl::PerformanceProfile reference = Impl::ReferencePerformanceProfile();
    EXPECT_TRUE(reference.isReference);
    EXPECT_TRUE(reference.retainsMeshCount);
    EXPECT_TRUE(reference.retainsTransparentObjects);
    EXPECT_TRUE(reference.retainsReflection);
    EXPECT_TRUE(reference.retainsRefraction);
    EXPECT_TRUE(reference.retainsCaustics);
    EXPECT_TRUE(reference.retainsSurfelGi);
    EXPECT_FALSE(reference.secondaryRayProxyEnabled);
    EXPECT_FALSE(reference.lightSpaceTransmissionEnabled);
    EXPECT_FALSE(reference.dynamicControlEnabled);
    EXPECT_EQ(Impl::ChangedPerformanceDimensionCount(reference), 0u);
    EXPECT_FALSE(Impl::RequiresNewWorkloadDefinition(reference));
    const Impl::SecondaryEffectGeometryProxyDescriptor proxy{};
    const Impl::LightSpaceTransmissionDescriptor field{};
    EXPECT_TRUE(Impl::ValidatePerformanceProfile(reference, proxy, field));
    EXPECT_FALSE(Impl::IsSingleDimensionCandidate(reference, proxy, field));
}


// One approved dimension at a time (15.2): 1.0x, 0.75x, or 0.5x only; a half-linear extent without
// reconstruction loses contacts and leaks light, so it fails closed.
TEST(CoincidentOpticalVolumes, ReducedShadowExtentRequiresReconstructionAndApprovedStep){
    Impl::PerformanceProfile candidate;
    candidate.transparentShadowExtent = 0.5f;
    const Impl::SecondaryEffectGeometryProxyDescriptor proxy{};
    const Impl::LightSpaceTransmissionDescriptor field{};
    EXPECT_FALSE(Impl::ValidatePerformanceProfile(candidate, proxy, field));
    candidate.extentReconstruction = true;
    EXPECT_TRUE(Impl::ValidatePerformanceProfile(candidate, proxy, field));
    EXPECT_TRUE(Impl::IsSingleDimensionCandidate(candidate, proxy, field));
    candidate.transparentShadowExtent = 0.6f;
    EXPECT_FALSE(Impl::ValidatePerformanceProfile(candidate, proxy, field));
}


// Two changed dimensions stop being a one-dimension experiment, even when each step is approved.
TEST(CoincidentOpticalVolumes, TwoChangedDimensionsAreNotASingleDimensionCandidate){
    Impl::PerformanceProfile candidate;
    candidate.transparentShadowExtent = 0.75f;
    candidate.causticBudget = 0.5f;
    candidate.extentReconstruction = true;
    const Impl::SecondaryEffectGeometryProxyDescriptor proxy{};
    const Impl::LightSpaceTransmissionDescriptor field{};
    EXPECT_TRUE(Impl::ValidatePerformanceProfile(candidate, proxy, field));
    EXPECT_EQ(Impl::ChangedPerformanceDimensionCount(candidate), 2u);
    EXPECT_FALSE(Impl::IsSingleDimensionCandidate(candidate, proxy, field));
}


// Surfel reduction without a maximum-age guarantee starves the field; unqualified P9 prototypes
// stay off until measured evidence plus visual approval qualify them (P9 decision gate).
TEST(CoincidentOpticalVolumes, SurfelBudgetAndProxyGatesEnforceAgeAndQualification){
    const Impl::SecondaryEffectGeometryProxyDescriptor unqualifiedProxy{};
    const Impl::LightSpaceTransmissionDescriptor unqualifiedField{};
    Impl::PerformanceProfile aged{};
    aged.surfelUpdateBudget = 0.5f;
    aged.surfelMaxAgeFrames = 0u;
    EXPECT_FALSE(Impl::ValidatePerformanceProfile(aged, unqualifiedProxy, unqualifiedField));
    Impl::PerformanceProfile proxied;
    proxied.secondaryRayProxyEnabled = true;
    EXPECT_FALSE(Impl::ValidatePerformanceProfile(proxied, unqualifiedProxy, unqualifiedField));
    Impl::PerformanceProfile lightSpace;
    lightSpace.lightSpaceTransmissionEnabled = true;
    EXPECT_FALSE(Impl::ValidatePerformanceProfile(lightSpace, unqualifiedProxy, unqualifiedField));
    EXPECT_FALSE(Impl::RequiresNewWorkloadDefinition(proxied));
}


// Removing meshes, transparent objects, or any retained effect changes the target workload
// definition (15.2); such a candidate must not report reference-workload results.
TEST(CoincidentOpticalVolumes, DroppedFeaturesRequireANewWorkloadDefinition){
    Impl::PerformanceProfile thinned;
    thinned.retainsMeshCount = false;
    const Impl::SecondaryEffectGeometryProxyDescriptor proxy{};
    const Impl::LightSpaceTransmissionDescriptor field{};
    EXPECT_TRUE(Impl::RequiresNewWorkloadDefinition(thinned));
    EXPECT_FALSE(Impl::ValidatePerformanceProfile(thinned, proxy, field));
    EXPECT_FALSE(Impl::IsSingleDimensionCandidate(thinned, proxy, field));
    Impl::PerformanceProfile noGi;
    noGi.retainsSurfelGi = false;
    EXPECT_TRUE(Impl::RequiresNewWorkloadDefinition(noGi));
    EXPECT_FALSE(Impl::ValidatePerformanceProfile(noGi, proxy, field));
}


// Dynamic per-frame resizing and backend flips stay off: oscillation, latency, memory pressure,
// and load-spike recovery are a later step with hysteresis and logged transitions (15.4).
TEST(CoincidentOpticalVolumes, DynamicControlIsRejectedUntilQualified){
    Impl::PerformanceProfile candidate;
    candidate.transparentShadowExtent = 0.75f;
    candidate.extentReconstruction = true;
    candidate.dynamicControlEnabled = true;
    const Impl::SecondaryEffectGeometryProxyDescriptor proxy{};
    const Impl::LightSpaceTransmissionDescriptor field{};
    EXPECT_FALSE(Impl::ValidatePerformanceProfile(candidate, proxy, field));
    candidate.dynamicControlEnabled = false;
    EXPECT_TRUE(Impl::IsSingleDimensionCandidate(candidate, proxy, field));
}


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

