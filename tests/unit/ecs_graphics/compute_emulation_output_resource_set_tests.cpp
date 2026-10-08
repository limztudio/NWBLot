// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/material/task_graph_opaque_compute_emulation_plan.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>

#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_compute_emulation_output_resource_set_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using TestArena = Tests::TestArena<struct ComputeEmulationOutputResourceSetTestsTag>;
inline constexpr Name s_SetIdentity("tests/compute_output_set/resources");
inline constexpr AStringView s_SetLabel = "Compute Emulation Outputs";

struct OutputContext{
    TestArena testArena;
    Core::GpuTaskGraph graph{ testArena.arena };
    ECSRenderDetail::OpaqueRegularComputeEmulationGraphPlan plan{ testArena.arena };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(ComputeEmulationOutputResourceSet, EmptyPlansFailWithoutImporting){
    OutputContext context;
    Core::Alloc::ScratchArena scratch(Name("tests/compute_output_set/empty"));
    EXPECT_FALSE(RendererTaskGraphDetail::GatherImportedOutputBufferResourceSet(
        context.graph, context.plan, scratch, s_SetIdentity, s_SetLabel
    ));
    context.plan.captured = true;
    EXPECT_FALSE(RendererTaskGraphDetail::GatherImportedOutputBufferResourceSet(
        context.graph, context.plan, scratch, s_SetIdentity, s_SetLabel
    ));
    const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
    ASSERT_TRUE(view.valid());
    EXPECT_EQ(view.resourceCount(), 0u);
    EXPECT_EQ(view.resourceSetCount(), 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

