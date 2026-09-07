// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_mesh/skinning/graph_resource_uses.h>

#include <tests/common/capturing_logger.h>
#include <global/text_utils.h>
#include <global/timer.h>
#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skinning_graph_resource_uses_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;

[[nodiscard]] MeshSkinningGraphDispatchPlan Plan(const u32 offset = 0u, const u64 generation = 1u){
    MeshSkinningGraphDispatchPlan plan;
    plan.bindlessResourceSlotsResource = { offset + 1u, generation };
    plan.restPositionResource = { offset + 2u, generation };
    plan.restNormalResource = { offset + 3u, generation };
    plan.restTangentResource = { offset + 4u, generation };
    plan.skinnedPositionResource = { offset + 5u, generation };
    plan.skinnedNormalResource = { offset + 6u, generation };
    plan.skinnedTangentResource = { offset + 7u, generation };
    plan.meshletDescResource = { offset + 8u, generation };
    plan.meshletBoundsResource = { offset + 9u, generation };
    plan.meshletPositionRefDeltaResource = { offset + 10u, generation };
    plan.meshletAttributeRefDeltaResource = { offset + 11u, generation };
    plan.meshletLocalVertexRefResource = { offset + 12u, generation };
    plan.meshletPrimitiveIndexResource = { offset + 13u, generation };
    plan.attributeSkinResource = { offset + 14u, generation };
    plan.skinResource = { offset + 15u, generation };
    plan.jointPaletteResource = { offset + 16u, generation };
    plan.attributeResource = { offset + 17u, generation };
    plan.hasActiveSkin = true;
    plan.copiedRestStreams = true;
    plan.updatesMeshletBounds = true;
    plan.repacksNormals = true;
    return plan;
}

void VerifyUse(
    const Core::GpuTaskResourceUse& use,
    const Core::GpuGraphResourceId resource,
    const Core::ResourceStates::Mask state,
    const Core::GpuTaskResourceAccess::Enum access){
    EXPECT_EQ(use.resource, resource);
    EXPECT_EQ(use.requiredState, state);
    EXPECT_EQ(use.access, access);
    EXPECT_FALSE(use.hasIndependentStateSource);
    EXPECT_EQ(use.range.bufferRange.byteOffset, 0u);
    EXPECT_EQ(use.range.bufferRange.byteSize, Core::s_EntireBuffer.byteSize);
}

TEST(SkinningGraphResourceUses, PreservesOrderedRolesAndIndependentPhaseStates){
    Core::Alloc::ScratchArena scratch(Name("tests/skinning_graph_uses/roles"));
    MeshSkinningGraphResourceUses uses(scratch);
    const MeshSkinningGraphDispatchPlan plan = Plan();
    ASSERT_TRUE(BuildMeshSkinningGraphResourceUses(&plan, 1u, scratch, uses));
    constexpr u32 deformation[] = { 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 10u, 11u, 14u, 15u, 16u };
    constexpr u32 postDispatch[] = { 1u, 5u, 8u, 10u, 12u, 13u, 9u, 6u, 11u, 17u };
    constexpr u32 finalizer[] = { 5u, 6u, 7u, 9u, 17u };
    ASSERT_EQ(uses.deformation.size(), LengthOf(deformation));
    ASSERT_EQ(uses.postDispatch.size(), LengthOf(postDispatch));
    ASSERT_EQ(uses.finalizer.size(), LengthOf(finalizer));
    for(usize index = 0u; index < LengthOf(deformation); ++index){
        const bool writes = index >= 4u && index <= 6u;
        const auto state = index == 0u ? Core::ResourceStates::ConstantBuffer
            : writes ? Core::ResourceStates::UnorderedAccess : Core::ResourceStates::ShaderResource;
        VerifyUse(uses.deformation[index], { deformation[index], 1u }, state,
            writes ? Core::GpuTaskResourceAccess::Write : Core::GpuTaskResourceAccess::Read);
    }
    for(usize index = 0u; index < LengthOf(postDispatch); ++index){
        const bool writes = index == 6u || index == 9u;
        const auto state = index == 0u ? Core::ResourceStates::ConstantBuffer
            : writes ? Core::ResourceStates::UnorderedAccess : Core::ResourceStates::ShaderResource;
        VerifyUse(uses.postDispatch[index], { postDispatch[index], 1u }, state,
            writes ? Core::GpuTaskResourceAccess::Write : Core::GpuTaskResourceAccess::Read);
    }
    for(usize index = 0u; index < LengthOf(finalizer); ++index)
        VerifyUse(uses.finalizer[index], { finalizer[index], 1u }, Core::ResourceStates::ShaderResource, Core::GpuTaskResourceAccess::Read);
}

TEST(SkinningGraphResourceUses, DeduplicatesRepeatedAndPermutedInputsWithoutChangingFirstOccurrence){
    Core::Alloc::ScratchArena scratch(Name("tests/skinning_graph_uses/repeated"));
    MeshSkinningGraphResourceUses uses(scratch);
    MeshSkinningGraphDispatchPlan plans[] = { Plan(), Plan(), Plan(100u) };
    Swap(plans[1u].restPositionResource, plans[1u].restNormalResource);
    plans[2u].meshletDescResource = plans[0u].meshletDescResource;
    ASSERT_TRUE(BuildMeshSkinningGraphResourceUses(plans, LengthOf(plans), scratch, uses));
    ASSERT_EQ(uses.deformation.size(), 25u);
    ASSERT_EQ(uses.postDispatch.size(), 19u);
    ASSERT_EQ(uses.finalizer.size(), 10u);
    EXPECT_EQ(uses.deformation[1u].resource, plans[0u].restPositionResource);
    EXPECT_EQ(uses.deformation[2u].resource, plans[0u].restNormalResource);
    EXPECT_EQ(uses.deformation[13u].resource, plans[2u].bindlessResourceSlotsResource);
    EXPECT_EQ(uses.postDispatch[10u].resource, plans[2u].bindlessResourceSlotsResource);
    EXPECT_EQ(uses.finalizer[5u].resource, plans[2u].skinnedPositionResource);
}

TEST(SkinningGraphResourceUses, PreservesFullResourceGenerationAndRebuildsChangedFlags){
    Core::Alloc::ScratchArena scratch(Name("tests/skinning_graph_uses/generation"));
    MeshSkinningGraphResourceUses uses(scratch);
    MeshSkinningGraphDispatchPlan plans[] = { Plan(), Plan(0u, 1ull << 40u) };
    ASSERT_TRUE(BuildMeshSkinningGraphResourceUses(plans, LengthOf(plans), scratch, uses));
    ASSERT_EQ(uses.deformation.size(), 26u);
    ASSERT_EQ(uses.postDispatch.size(), 20u);
    ASSERT_EQ(uses.finalizer.size(), 10u);
    EXPECT_EQ(uses.deformation[13u].resource, plans[1u].bindlessResourceSlotsResource);
    EXPECT_EQ(uses.finalizer[5u].resource, plans[1u].skinnedPositionResource);

    plans[0u].hasActiveSkin = false;
    plans[0u].repacksNormals = false;
    plans[0u].updatesMeshletBounds = false;
    ASSERT_TRUE(BuildMeshSkinningGraphResourceUses(plans, 1u, scratch, uses));
    EXPECT_TRUE(uses.deformation.empty());
    EXPECT_EQ(uses.postDispatch.size(), 7u);
    EXPECT_EQ(uses.finalizer.size(), 3u);
    plans[0u].copiedRestStreams = false;
    plans[0u].updatesMeshletBounds = true;
    ASSERT_TRUE(BuildMeshSkinningGraphResourceUses(plans, 1u, scratch, uses));
    ASSERT_EQ(uses.finalizer.size(), 1u);
    EXPECT_EQ(uses.finalizer[0u].resource, plans[0u].meshletBoundsResource);
    ASSERT_TRUE(BuildMeshSkinningGraphResourceUses(nullptr, 0u, scratch, uses));
    EXPECT_TRUE(uses.deformation.empty());
    EXPECT_TRUE(uses.postDispatch.empty());
    EXPECT_TRUE(uses.finalizer.empty());
}

TEST(SkinningGraphResourceUses, RejectsEveryPhaseConflictOrMissingResourceAndRecovers){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard registration(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    Core::Alloc::ScratchArena scratch(Name("tests/skinning_graph_uses/rejections"));
    MeshSkinningGraphResourceUses uses(scratch);
    for(u32 failure = 0u; failure < 6u; ++failure){
        MeshSkinningGraphDispatchPlan plan = Plan();
        switch(failure){
        case 0u:
            plan.restPositionResource = plan.skinnedPositionResource;
            break;
        case 1u:
            plan.skinResource = {};
            break;
        case 2u:
            plan.hasActiveSkin = false;
            plan.skinnedPositionResource = plan.meshletBoundsResource;
            break;
        case 3u:
            plan.meshletPrimitiveIndexResource = {};
            break;
        case 4u:
            plan.hasActiveSkin = false;
            plan.repacksNormals = false;
            plan.skinnedNormalResource = {};
            break;
        case 5u:
            plan.hasActiveSkin = false;
            plan.copiedRestStreams = false;
            plan.repacksNormals = false;
            plan.updatesMeshletBounds = false;
            break;
        }
        EXPECT_FALSE(BuildMeshSkinningGraphResourceUses(&plan, 1u, scratch, uses));
        EXPECT_TRUE(uses.deformation.empty());
        EXPECT_TRUE(uses.postDispatch.empty());
        EXPECT_TRUE(uses.finalizer.empty());
        plan = Plan();
        ASSERT_TRUE(BuildMeshSkinningGraphResourceUses(&plan, 1u, scratch, uses));
        EXPECT_EQ(uses.deformation.size(), 13u);
        EXPECT_EQ(uses.postDispatch.size(), 10u);
        EXPECT_EQ(uses.finalizer.size(), 5u);
    }
    EXPECT_EQ(logger.errorCount(), 6u);
}

TEST(SkinningGraphResourceUses, PromotedCollectionsRejectLaterPhaseConflictsAndRebuildAfterFailure){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard registration(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    Core::Alloc::ScratchArena scratch(Name("tests/skinning_graph_uses/promoted_failures"));
    MeshSkinningGraphResourceUses uses(scratch);
    for(u32 failure = 0u; failure < 3u; ++failure){
        MeshSkinningGraphDispatchPlan plans[] = { Plan(), Plan(100u), Plan(200u) };
        ASSERT_TRUE(BuildMeshSkinningGraphResourceUses(plans, LengthOf(plans), scratch, uses));
        if(failure == 0u)
            plans[2u].restPositionResource = plans[1u].skinnedPositionResource;
        else if(failure == 1u)
            plans[2u].meshletDescResource = plans[0u].meshletBoundsResource;
        else{
            plans[2u].hasActiveSkin = false;
            plans[2u].repacksNormals = false;
            plans[2u].skinnedNormalResource = {};
        }
        EXPECT_FALSE(BuildMeshSkinningGraphResourceUses(plans, LengthOf(plans), scratch, uses));
        EXPECT_TRUE(uses.deformation.empty());
        EXPECT_TRUE(uses.postDispatch.empty());
        EXPECT_TRUE(uses.finalizer.empty());
        plans[2u] = Plan(200u);
        ASSERT_TRUE(BuildMeshSkinningGraphResourceUses(plans, LengthOf(plans), scratch, uses));
        EXPECT_EQ(uses.deformation.size(), 39u);
        EXPECT_EQ(uses.postDispatch.size(), 30u);
        EXPECT_EQ(uses.finalizer.size(), 15u);
    }
    EXPECT_EQ(logger.errorCount(), 3u);
}

TEST(SkinningGraphResourceUses, RepeatedPlansUseTheSameScratchCapacityAsOnePlan){
    Core::Alloc::ScratchArena singleScratch(Name("tests/skinning_graph_uses/single_storage"));
    Core::Alloc::ScratchArena repeatedScratch(Name("tests/skinning_graph_uses/repeated_storage"));
    MeshSkinningGraphResourceUses singleUses(singleScratch);
    MeshSkinningGraphResourceUses repeatedUses(repeatedScratch);
    MeshSkinningGraphDispatchPlan plans[64u];
    for(MeshSkinningGraphDispatchPlan& plan : plans)
        plan = Plan();
    ASSERT_TRUE(BuildMeshSkinningGraphResourceUses(plans, 1u, singleScratch, singleUses));
    ASSERT_TRUE(BuildMeshSkinningGraphResourceUses(plans, LengthOf(plans), repeatedScratch, repeatedUses));
    const ArenaMemoryStats single = singleScratch.memoryStats();
    const ArenaMemoryStats repeated = repeatedScratch.memoryStats();
    EXPECT_EQ(repeated.allocationCount, single.allocationCount);
    EXPECT_EQ(repeated.peakUsedBytes, single.peakUsedBytes);
    EXPECT_EQ(repeated.reservedBytes, single.reservedBytes);
}

void RecordProperty(const NotNull<const char*> key, const u64 value){
    char text[32u] = {};
    testing::Test::RecordProperty(key.get(), FormatDecimal(value, text).data());
}

void BenchmarkGather(const u32 planCount, const u32 iterations, const bool shared){
    Core::Alloc::GlobalArena inputArena(Name("tests/skinning_graph_uses/input"));
    Vector<MeshSkinningGraphDispatchPlan, Core::Alloc::GlobalArena> plans(inputArena);
    plans.reserve(planCount);
    for(u32 index = 0u; index < planCount; ++index)
        plans.push_back(Plan(shared ? 0u : index * 32u));
    u64 elapsed = 0u;
    u64 outputCount = 0u;
    u64 peakBytes = 0u;
    u64 reservedBytes = 0u;
    u64 allocationCount = 0u;
    bool succeeded = true;
    for(u32 iteration = 0u; iteration < iterations; ++iteration){
        const Timer begin = TimerNow();
        {
            Core::Alloc::ScratchArena scratch(Name("tests/skinning_graph_uses/benchmark"));
            {
                MeshSkinningGraphResourceUses uses(scratch);
                succeeded = BuildMeshSkinningGraphResourceUses(plans.data(), plans.size(), scratch, uses) && succeeded;
                outputCount += uses.deformation.size() + uses.postDispatch.size() + uses.finalizer.size();
            }
            const ArenaMemoryStats stats = scratch.memoryStats();
            peakBytes = Max(peakBytes, stats.peakUsedBytes);
            reservedBytes = Max(reservedBytes, stats.reservedBytes);
            allocationCount += stats.allocationCount;
        }
        elapsed += DurationInNS<u64>(TimerNow(), begin);
    }
    EXPECT_TRUE(succeeded);
    EXPECT_EQ(outputCount, static_cast<u64>(iterations) * (shared ? 1u : planCount) * 28u);
    RecordProperty(MakeNotNull("skinning_uses_ns"), elapsed);
    RecordProperty(MakeNotNull("skinning_uses_iterations"), iterations);
    RecordProperty(MakeNotNull("skinning_uses_plan_count"), planCount);
    RecordProperty(MakeNotNull("skinning_uses_output_count"), outputCount);
    RecordProperty(MakeNotNull("skinning_uses_peak_bytes"), peakBytes);
    RecordProperty(MakeNotNull("skinning_uses_reserved_bytes"), reservedBytes);
    RecordProperty(MakeNotNull("skinning_uses_allocations"), allocationCount);
}

TEST(SkinningGraphResourceUsesBenchmark, DISABLED_SingleDispatchPlan){
    BenchmarkGather(1u, 4096u, false);
}

TEST(SkinningGraphResourceUsesBenchmark, DISABLED_UniqueDispatchPlans){
    BenchmarkGather(1024u, 1u, false);
}

TEST(SkinningGraphResourceUsesBenchmark, DISABLED_SharedDispatchPlans){
    BenchmarkGather(1024u, 8u, true);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

