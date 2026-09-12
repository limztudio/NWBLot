// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/csg/renderer_csg_types.h>
#include <impl/ecs_render/deferred/opaque_csg_interval_clear_builder.h>

#include <core/alloc/scratch.h>
#include <core/task/gpu/task_graph.h>

#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_opaque_csg_interval_clear_builder_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Core = NWB::Core;
namespace Impl = NWB::Impl;
using TestArena = NWB::Tests::TestArena<>;


TEST(OpaqueCsgIntervalClearBuilder, NoOpaqueWorkDoesNotPublishDependencyAsClear){
    TestArena testArena;
    Core::Alloc::ScratchArena scratchArena(Name("tests/opaque_csg_interval_clear/scratch"));
    Core::GpuTaskGraph graph(testArena.arena);
    Core::GpuTaskDesc dependencyDesc;
    dependencyDesc.setIdentity(Name("tests/opaque_csg_interval_clear/dependency")).setMarkerLabel("Incoming CSG Dependency");
    const Core::GpuTaskId dependency = graph.addTask(dependencyDesc);
    ASSERT_TRUE(dependency.valid());

    usize initialTaskCount = 0u;
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        initialTaskCount = declarations.taskCount();
        ASSERT_EQ(initialTaskCount, 1u);
        ASSERT_TRUE(declarations.validTask(dependency));
    }

    Impl::DeferredFrameTargets targets;
    Impl::CsgFrameGpuData csgFrameData(scratchArena);
    Impl::GraphClearTimingRecordState clearTimingState;
    Impl::OpaqueCsgIntervalClearResult result{ .clearTask = dependency, .clearFirstTask = dependency };
    const Impl::OpaqueCsgIntervalClearInputs inputs{
        .targets = &targets,
        .csgFrameData = &csgFrameData,
        .csgIntervalId = {},
        .csgReceiverEventCount = {},
        .csgPeelSubresources = {},
        .csgReceiverEventCountSubresources = {},
        .dependencyTask = dependency,
        .hasOpaqueCsgFrameWork = false,
    };
    Impl::OpaqueCsgIntervalClearBuilder builder(graph);
    ASSERT_TRUE(builder.declare(inputs, clearTimingState, result));
    EXPECT_FALSE(result.clearTask.valid());
    EXPECT_FALSE(result.clearFirstTask.valid());

    const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
    ASSERT_EQ(declarations.taskCount(), initialTaskCount);
    EXPECT_TRUE(declarations.validTask(dependency));
    EXPECT_EQ(declarations.taskAt(0u).id, dependency);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

