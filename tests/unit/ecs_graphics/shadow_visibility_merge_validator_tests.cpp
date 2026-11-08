// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>
#include <tests/unit/ecs_graphics/name_cases.adhd.h>

#include <impl/ecs_render/shadow/shadow_visibility_merge_validator.h>

#include <core/task/gpu/compiler_internal.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_shadow_visibility_merge_validator_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ShadowPacketPlan{
    TestArena<> testArena;
    Core::GpuTaskGraph graph;
    Core::GpuCompiledGraph compiledGraph;
    Impl::PreparedShadowVisibilityTasks tasks;


public:
    ShadowPacketPlan()
        : graph(testArena.arena)
        , compiledGraph(testArena.arena)
    {}


public:
    [[nodiscard]] b8 build(const b8 combinedUpsample, const b8 temporalMerge, const u32 splitStage = Limit<u32>::s_Max, const b8 combinedWavelet = false, const b8 combinedTemporal = false, const b8 diagnostics = false){
        const Array<Name, 8> identities = {
            Name(NWB::Tests::EcsGraphicsCases::s_EcsCaseTestsShadowPacketOpaque), Name(NWB::Tests::EcsGraphicsCases::s_EcsCaseTestsShadowPacketOpaqueWavelet),
            Name(NWB::Tests::EcsGraphicsCases::s_EcsCaseTestsShadowPacketOpaqueUpsample), Name(NWB::Tests::EcsGraphicsCases::s_EcsCaseTestsShadowPacketTransparentTrace),
            Name(NWB::Tests::EcsGraphicsCases::s_EcsCaseTestsShadowPacketTransparentTemporal), Name(NWB::Tests::EcsGraphicsCases::s_EcsCaseTestsShadowPacketTransparentWavelet),
            Name(NWB::Tests::EcsGraphicsCases::s_EcsCaseTestsShadowPacketTerminal), Name(NWB::Tests::EcsGraphicsCases::s_EcsCaseTestsShadowPacketDiagnostics)
        };
        constexpr Array<AStringView, 8> s_Labels = { "Shadow Opaque", "Shadow Opaque Wavelet", "Shadow Opaque Upsample", "Shadow Transparent Trace", "Shadow Transparent Temporal", "Shadow Transparent Wavelet", "Shadow Terminal", "Shadow Diagnostics" };
        static_assert(s_Labels.size() == identities.size());
        Array<Core::GpuTaskId, 8> stages{};
        Core::GpuTaskId previous;
        for(u32 index = 0u; index < LengthOf(identities); ++index){
            if((index == 1u && combinedTemporal) || (index == 2u && combinedUpsample) || (index == 4u && !temporalMerge) || (index == 7u && !diagnostics))
                continue;
            Core::GpuTaskSchedulingHint scheduling;
            scheduling.mergeWithPrevious = previous.valid();
            scheduling.forceSubmissionBoundary = index == splitStage;
            const auto stageResult = graph.addTask(
                Core::GpuTaskDesc{}
                    .setIdentity(identities[index])
                    .setMarkerLabel(s_Labels[index])
                    .setScheduling(scheduling)
                    .setDependencies({ previous.valid() ? &previous : nullptr, previous.valid() ? 1u : 0u }),
                Core::GpuTaskCommandRequirements{ Core::GpuQueueCapability::Graphics });
            if(!stageResult){
                ADD_FAILURE() << "stage declaration=" << index;
                return false;
            }
            stages[index] = *stageResult;
            previous = stages[index];
        }
        tasks.terminal = stages[6];
        tasks.opaque = stages[0];
        tasks.opaqueFirstWavelet = stages[1];
        tasks.opaqueResolve = stages[2];
        tasks.transparentTrace = stages[3];
        tasks.transparentTemporalMerge = stages[4];
        tasks.transparentFirstWavelet = stages[5];
        tasks.diagnostics = stages[7];
        tasks.combinedUpsample = combinedUpsample;
        tasks.combinedWavelet = combinedWavelet;
        tasks.combinedTemporal = combinedTemporal;
        const Core::GpuPhysicalQueueInfo queue{
            .familyIndex = 0u,
            .queueIndex = 0u,
            .id = { .index = 0u, .deviceGeneration = 1u },
            .queueClass = Core::CommandQueue::Graphics,
            .capabilities = static_cast<Core::GpuQueueCapability::Mask>(Core::GpuQueueCapability::Graphics | Core::GpuQueueCapability::Compute | Core::GpuQueueCapability::Transfer),
        };
        const Core::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u };
        Core::GpuTaskGraphAnalysis analysis(testArena.arena);
        Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Core::Alloc::ScratchArena scratchArena{Name(NWB::Tests::EcsGraphicsCases::s_EcsCaseTestsShadowPacketScratch)};
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        const Core::GpuTaskGraphCompiler compiler;
        Core::GpuTaskGraphCompileOptions options;
        options.allowMetadataOnlyTasks = true;
        if(!compiler.compile(declarations, topology, options, scratchArena, analysis, assignments, compiledGraph)){
            ADD_FAILURE() << "analysis status=" << static_cast<u32>(analysis.diagnostic().status) << ", queue status=" << static_cast<u32>(assignments.diagnostic().status);
            return false;
        }
        return true;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(ShadowVisibilityMergeValidator, CombinedTemporalSelectsOpaqueEvenWhenUpsampleIsCombined){
    const Core::GpuTaskId opaque{ .generation = 1u, .index = 1u };
    const Core::GpuTaskId wavelet{ .generation = 1u, .index = 2u };
    const Core::GpuTaskId resolve{ .generation = 1u, .index = 3u };
    EXPECT_EQ(Impl::SelectOpaqueShadowCompletion(true, true, opaque, wavelet, resolve), opaque);
    EXPECT_EQ(Impl::SelectOpaqueShadowCompletion(false, true, opaque, wavelet, resolve), wavelet);
    EXPECT_EQ(Impl::SelectOpaqueShadowCompletion(false, false, opaque, wavelet, resolve), resolve);
    EXPECT_FALSE(Impl::SelectOpaqueShadowCompletion(true, false, {}, wavelet, resolve).valid());
}

TEST(ShadowVisibilityMergeValidator, FusionOmitsOnlyOpaqueUpsampleAndKeepsOptionalTemporalMerge){
    for(const b8 temporalMerge : { false, true }){
        ShadowPacketPlan fixture;
        ASSERT_TRUE(fixture.build(true, temporalMerge));
        const Core::GpuCompiledGraph::ReadView plan(fixture.compiledGraph);
        EXPECT_FALSE(fixture.tasks.opaqueResolve.valid());
        EXPECT_EQ(fixture.tasks.transparentTemporalMerge.valid(), temporalMerge);
        EXPECT_TRUE(Impl::PreparedShadowVisibilityTasksSharePacket(plan, fixture.tasks));

        Impl::PreparedShadowVisibilityTasks ordinary = fixture.tasks;
        ordinary.combinedUpsample = false;
        EXPECT_FALSE(Impl::PreparedShadowVisibilityTasksSharePacket(plan, ordinary));
    }
}

TEST(ShadowVisibilityMergeValidator, SeparateRouteRequiresItsOpaqueUpsample){
    for(const b8 temporalMerge : { false, true }){
        ShadowPacketPlan fixture;
        ASSERT_TRUE(fixture.build(false, temporalMerge));
        const Core::GpuCompiledGraph::ReadView plan(fixture.compiledGraph);
        EXPECT_TRUE(Impl::PreparedShadowVisibilityTasksSharePacket(plan, fixture.tasks));

        Impl::PreparedShadowVisibilityTasks unexpectedTail = fixture.tasks;
        unexpectedTail.combinedUpsample = true;
        EXPECT_FALSE(Impl::PreparedShadowVisibilityTasksSharePacket(plan, unexpectedTail));
        Impl::PreparedShadowVisibilityTasks missingTail = fixture.tasks;
        missingTail.opaqueResolve = {};
        EXPECT_FALSE(Impl::PreparedShadowVisibilityTasksSharePacket(plan, missingTail));
    }
}

TEST(ShadowVisibilityMergeValidator, MissingStagesAndForeignGraphIdentitiesCannotProveOneAcceptancePacket){
    for(const b8 combined : { false, true }){
        ShadowPacketPlan fixture;
        ShadowPacketPlan foreign;
        ASSERT_TRUE(fixture.build(combined, true));
        ASSERT_TRUE(foreign.build(combined, true));
        const Core::GpuCompiledGraph::ReadView plan(fixture.compiledGraph);
        const Array<Core::GpuTaskId Impl::PreparedShadowVisibilityTasks::*, 4> required = {
            &Impl::PreparedShadowVisibilityTasks::terminal,
            &Impl::PreparedShadowVisibilityTasks::opaqueFirstWavelet,
            &Impl::PreparedShadowVisibilityTasks::transparentTrace,
            &Impl::PreparedShadowVisibilityTasks::transparentFirstWavelet,
        };
        for(const auto member : required){
            Impl::PreparedShadowVisibilityTasks missing = fixture.tasks;
            missing.*member = {};
            EXPECT_FALSE(Impl::PreparedShadowVisibilityTasksSharePacket(plan, missing));
            Impl::PreparedShadowVisibilityTasks stale = fixture.tasks;
            stale.*member = foreign.tasks.*member;
            EXPECT_FALSE(Impl::PreparedShadowVisibilityTasksSharePacket(plan, stale));
        }
        Impl::PreparedShadowVisibilityTasks staleMerge = fixture.tasks;
        staleMerge.transparentTemporalMerge = foreign.tasks.transparentTemporalMerge;
        EXPECT_FALSE(Impl::PreparedShadowVisibilityTasksSharePacket(plan, staleMerge));
    }
}

TEST(ShadowVisibilityMergeValidator, EverySeparateSubmissionBoundaryRejectsThePreparedChain){
    for(const b8 combined : { false, true }){
        for(u32 splitStage = 1u; splitStage < 7u; ++splitStage){
            if(combined && splitStage == 2u)
                continue;
            SCOPED_TRACE(combined);
            SCOPED_TRACE(splitStage);
            ShadowPacketPlan fixture;
            ASSERT_TRUE(fixture.build(combined, true, splitStage));
            const Core::GpuCompiledGraph::ReadView plan(fixture.compiledGraph);
            EXPECT_FALSE(Impl::PreparedShadowVisibilityTasksSharePacket(plan, fixture.tasks));
        }
    }
}

TEST(ShadowVisibilityMergeValidator, TrailingDiagnosticsCannotExportScratchStateFromAnotherAcceptancePacket){
    for(const b8 combined : { false, true }){
        {
            ShadowPacketPlan fixture;
            ASSERT_TRUE(fixture.build(combined, true, 7u, false, false, true));
            const Core::GpuCompiledGraph::ReadView plan(fixture.compiledGraph);
            Impl::PreparedShadowVisibilityTasks withoutDiagnostics = fixture.tasks;
            withoutDiagnostics.diagnostics = {};
            ASSERT_TRUE(Impl::PreparedShadowVisibilityTasksSharePacket(plan, withoutDiagnostics));
            EXPECT_FALSE(Impl::PreparedShadowVisibilityTasksSharePacket(plan, fixture.tasks));
        }
        ShadowPacketPlan merged;
        ShadowPacketPlan foreign;
        ASSERT_TRUE(merged.build(combined, true, Limit<u32>::s_Max, false, false, true));
        ASSERT_TRUE(foreign.build(combined, true, Limit<u32>::s_Max, false, false, true));
        const Core::GpuCompiledGraph::ReadView plan(merged.compiledGraph);
        ASSERT_TRUE(Impl::PreparedShadowVisibilityTasksSharePacket(plan, merged.tasks));
        Impl::PreparedShadowVisibilityTasks stale = merged.tasks;
        stale.diagnostics = foreign.tasks.diagnostics;
        EXPECT_FALSE(Impl::PreparedShadowVisibilityTasksSharePacket(plan, stale));
    }
}

TEST(ShadowVisibilityMergeValidator, CombinedWaveletRequiresBothTemporalPhasesAndCombinedTerminal){
    ShadowPacketPlan fixture;
    ASSERT_TRUE(fixture.build(true, true, Limit<u32>::s_Max, true));
    const Core::GpuCompiledGraph::ReadView plan(fixture.compiledGraph);
    EXPECT_TRUE(Impl::PreparedShadowVisibilityTasksSharePacket(plan, fixture.tasks));
    Impl::PreparedShadowVisibilityTasks missingTemporal = fixture.tasks;
    missingTemporal.transparentTemporalMerge = {};
    EXPECT_FALSE(Impl::PreparedShadowVisibilityTasksSharePacket(plan, missingTemporal));
    Impl::PreparedShadowVisibilityTasks separateTerminal = fixture.tasks;
    separateTerminal.combinedUpsample = false;
    EXPECT_FALSE(Impl::PreparedShadowVisibilityTasksSharePacket(plan, separateTerminal));
}

TEST(ShadowVisibilityMergeValidator, CombinedWaveletRejectsEveryProducerOrTerminalPacketSplit){
    for(const u32 stage : { 1u, 3u, 4u, 5u, 6u }){
        ShadowPacketPlan fixture;
        ASSERT_TRUE(fixture.build(true, true, stage, true));
        const Core::GpuCompiledGraph::ReadView plan(fixture.compiledGraph);
        EXPECT_FALSE(Impl::PreparedShadowVisibilityTasksSharePacket(plan, fixture.tasks)) << stage;
    }
}

TEST(ShadowVisibilityMergeValidator, CombinedTemporalOmitsOnlyTheIndependentOpaqueMerge){
    ShadowPacketPlan fixture;
    ASSERT_TRUE(fixture.build(true, true, Limit<u32>::s_Max, true, true));
    const Core::GpuCompiledGraph::ReadView plan(fixture.compiledGraph);
    EXPECT_FALSE(fixture.tasks.opaqueFirstWavelet.valid());
    EXPECT_TRUE(Impl::PreparedShadowVisibilityTasksSharePacket(plan, fixture.tasks));

    Impl::PreparedShadowVisibilityTasks separateTemporal = fixture.tasks;
    separateTemporal.combinedTemporal = false;
    EXPECT_FALSE(Impl::PreparedShadowVisibilityTasksSharePacket(plan, separateTemporal));
    Impl::PreparedShadowVisibilityTasks missingMerge = fixture.tasks;
    missingMerge.transparentTemporalMerge = {};
    EXPECT_FALSE(Impl::PreparedShadowVisibilityTasksSharePacket(plan, missingMerge));
    Impl::PreparedShadowVisibilityTasks missingCombinedWavelet = fixture.tasks;
    missingCombinedWavelet.combinedWavelet = false;
    EXPECT_FALSE(Impl::PreparedShadowVisibilityTasksSharePacket(plan, missingCombinedWavelet));
    Impl::PreparedShadowVisibilityTasks duplicateMerge = fixture.tasks;
    duplicateMerge.opaqueFirstWavelet = fixture.tasks.transparentTemporalMerge;
    EXPECT_FALSE(Impl::PreparedShadowVisibilityTasksSharePacket(plan, duplicateMerge));
}

TEST(ShadowVisibilityMergeValidator, CombinedTemporalRejectsEveryRemainingPacketSplit){
    for(const u32 stage : { 3u, 4u, 5u, 6u }){
        ShadowPacketPlan fixture;
        ASSERT_TRUE(fixture.build(true, true, stage, true, true));
        const Core::GpuCompiledGraph::ReadView plan(fixture.compiledGraph);
        EXPECT_FALSE(Impl::PreparedShadowVisibilityTasksSharePacket(plan, fixture.tasks)) << stage;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

