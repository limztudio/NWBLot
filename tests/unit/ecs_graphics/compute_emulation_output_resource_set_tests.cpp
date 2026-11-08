// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>
#include <tests/unit/ecs_graphics/name_cases.adhd.h>

#include <impl/ecs_render/material/task_graph_opaque_compute_emulation_plan.h>
#include <impl/ecs_render/avboit/avboit_compute_emulation_validator.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_compute_emulation_output_resource_set_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using TestArena = Tests::TestArena<struct ComputeEmulationOutputResourceSetTestsTag>;
inline constexpr Name s_SetIdentity = NWB::Tests::EcsGraphicsCases::s_EcsGraphicsNameTestsComputeOutputSetResources;
inline constexpr AStringView s_SetLabel = "Compute Emulation Outputs";

struct OutputContext{
    TestArena testArena;
    Core::GpuTaskGraph graph{ testArena.arena };
    ECSRenderDetail::OpaqueRegularComputeEmulationGraphPlan plan{ testArena.arena };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(ComputeEmulationOutputResourceSet, EmptyPlansFailWithoutImporting){
    OutputContext context;
    Core::Alloc::ScratchArena scratch{Name(NWB::Tests::EcsGraphicsCases::s_EcsCaseTestsComputeOutputSetEmpty)};
    EXPECT_FALSE(RendererTaskGraphDetail::GatherImportedOutputBufferResourceSet(context.graph, context.plan, scratch, s_SetIdentity, s_SetLabel));
    context.plan.captured = true;
    EXPECT_FALSE(RendererTaskGraphDetail::GatherImportedOutputBufferResourceSet(context.graph, context.plan, scratch, s_SetIdentity, s_SetLabel));
    const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
    ASSERT_TRUE(view.valid());
    EXPECT_EQ(view.resourceCount(), 0u);
    EXPECT_EQ(view.resourceSetCount(), 0u);
}


TEST(AvboitComputeEmulationValidator, RejectsStrayAndOversizedSharedPhases){
    TestArena testArena;
    Core::GpuCompiledGraph graph(testArena.arena);
    const Core::GpuCompiledGraph::ReadView plan(graph);
    const AvboitComputeEmulationValidator validator(plan);
    Array<Core::GpuTaskId, s_SharedComputeEmulationMaximumPhaseCount> tasks = {};
    EXPECT_TRUE(validator.sharedComputeEmulationChainValid(tasks, 0u, {}, {}, {}, nullptr, nullptr));
    tasks[0u] = Core::GpuTaskId{ .generation = 1u, .index = 0u };
    EXPECT_FALSE(validator.sharedComputeEmulationChainValid(tasks, 0u, {}, {}, {}, nullptr, nullptr));
    EXPECT_FALSE(validator.sharedComputeEmulationChainValid(tasks, tasks.size() + 1u, {}, {}, {}, nullptr, nullptr));
    Core::GpuPhysicalQueueInfo graphicsQueue{};
    graphicsQueue.queueClass = Core::CommandQueue::Graphics;
    const Array<Core::GpuTaskId, 1> shortTasks = { tasks[0u] };
    const usize supportedPhaseCount = s_SharedComputeEmulationMinimumDrawCount * s_SharedComputeEmulationPhasesPerDraw;
    EXPECT_FALSE(validator.sharedComputeEmulationChainValid(shortTasks, supportedPhaseCount, {}, tasks[0u], tasks[0u], &graphicsQueue, &graphicsQueue));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

