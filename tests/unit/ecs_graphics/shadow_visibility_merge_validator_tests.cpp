// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>

#include <impl/ecs_render/execute/shadow_visibility_merge_validator.h>
#include <impl/ecs_render/kernel/task_graph_queue_requests.h>

#include <core/task/gpu/compiler.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{
namespace __hidden_shadow_visibility_merge_validator_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ShadowPacketPlan{
    TestArena<> testArena;
    Core::GpuTaskGraph graph;
    Core::GpuCompiledGraph compiledGraph;
    Impl::PreparedShadowVisibilityTasks tasks;

    ShadowPacketPlan()
        : graph(testArena.arena)
        , compiledGraph(testArena.arena){
    }

    [[nodiscard]] bool build(const bool combinedUpsample, const bool temporalMerge, const u32 splitStage = Limit<u32>::s_Max, const bool combinedWavelet = false){
        const Name identities[] = {
            Name("tests/shadow_packet/opaque"), Name("tests/shadow_packet/opaque_wavelet"),
            Name("tests/shadow_packet/opaque_upsample"), Name("tests/shadow_packet/transparent_trace"),
            Name("tests/shadow_packet/transparent_temporal"), Name("tests/shadow_packet/transparent_wavelet"),
            Name("tests/shadow_packet/terminal")
        };
        constexpr AStringView labels[] = {
            "Shadow Opaque", "Shadow Opaque Wavelet", "Shadow Opaque Upsample", "Shadow Transparent Trace",
            "Shadow Transparent Temporal", "Shadow Transparent Wavelet", "Shadow Terminal"
        };
        static_assert(LengthOf(labels) == LengthOf(identities));
        Core::GpuTaskId stages[LengthOf(identities)]{};
        Core::GpuTaskId previous;
        for(u32 index = 0u; index < LengthOf(identities); ++index){
            if((index == 2u && combinedUpsample) || (index == 4u && !temporalMerge))
                continue;
            Core::GpuTaskSchedulingHint scheduling;
            scheduling.mergeWithPrevious = previous.valid();
            scheduling.forceSubmissionBoundary = index == splitStage;
            stages[index] = graph.addTask(
                Core::GpuTaskDesc{}
                    .setIdentity(identities[index])
                    .setMarkerLabel(labels[index])
                    .setQueue(Impl::RendererTaskGraphDetail::GraphicsQueueRequest())
                    .setScheduling(scheduling)
                    .setDependencies(previous.valid() ? &previous : nullptr, previous.valid() ? 1u : 0u)
            );
            if(!stages[index].valid()){
                ADD_FAILURE() << "stage declaration=" << index;
                return false;
            }
            previous = stages[index];
        }
        tasks = {
            .terminal = stages[6], .opaque = stages[0], .opaqueFirstWavelet = stages[1], .opaqueResolve = stages[2],
            .transparentTrace = stages[3], .transparentTemporalMerge = stages[4], .transparentFirstWavelet = stages[5],
            .combinedUpsample = combinedUpsample,
            .combinedWavelet = combinedWavelet,
        };
        const Core::GpuPhysicalQueueInfo queue{
            .familyIndex = 0u,
            .queueIndex = 0u,
            .id = { .index = 0u, .deviceGeneration = 1u },
            .queueClass = Core::CommandQueue::Graphics,
            .capabilities = static_cast<Core::GpuQueueCapability::Mask>(
                Core::GpuQueueCapability::Graphics | Core::GpuQueueCapability::Compute | Core::GpuQueueCapability::Transfer
            ),
        };
        const Core::GpuTaskGraphQueueTopology topology{ .queues = &queue, .queueCount = 1u };
        Core::GpuTaskGraphAnalysis analysis(testArena.arena);
        Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Core::Alloc::ScratchArena scratchArena(Name("tests/shadow_packet/scratch"));
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        const Core::GpuTaskGraphCompiler compiler;
        Core::GpuTaskGraphCompileOptions options;
        options.allowMetadataOnlyTasks = true;
        if(!compiler.compile(declarations, analysis, topology, assignments, compiledGraph, scratchArena, options)){
            ADD_FAILURE() << "analysis status=" << static_cast<u32>(analysis.diagnostic().status)
                << ", queue status=" << static_cast<u32>(assignments.diagnostic().status);
            return false;
        }
        return true;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(ShadowVisibilityMergeValidator, FusionOmitsOnlyOpaqueUpsampleAndKeepsOptionalTemporalMerge){
    for(const bool temporalMerge : { false, true }){
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
    for(const bool temporalMerge : { false, true }){
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
    for(const bool combined : { false, true }){
        ShadowPacketPlan fixture;
        ShadowPacketPlan foreign;
        ASSERT_TRUE(fixture.build(combined, true));
        ASSERT_TRUE(foreign.build(combined, true));
        const Core::GpuCompiledGraph::ReadView plan(fixture.compiledGraph);
        Core::GpuTaskId Impl::PreparedShadowVisibilityTasks::* const required[] = {
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
    for(const bool combined : { false, true }){
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

