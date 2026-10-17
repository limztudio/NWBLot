// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <impl/ecs_render/raytrace/scene_resources.h>
#include <impl/ecs_render/raytrace/task_graph_scene_resources.h>

#include <tests/common/test_context.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ray_surface_lighting_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(EcsGraphics, RaySceneSnapshotDoesNotTreatMissingResourcesAsHardwareAvailability){
    NWB::Impl::RayTracingSceneGraphResources resources;
    resources.hardwareAvailable = true;
    EXPECT_FALSE(resources.valid());
}

TEST(EcsGraphics, RaySceneGraphReadsMissingResourcesRetainScratchArena){
    NWB::Tests::TestArena<struct RaySceneGraphReadsTestsTag> testArena;
    NWB::Core::GpuTaskGraph graph(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena(Name("tests/ray_scene_graph_reads/scratch"));
    NWB::Impl::RayTracingSceneGraphResources resources;
    const auto scratchBefore = scratchArena.memoryStats();
    const auto reads = NWB::Impl::ImportRayTracingSceneGraphReads(graph, resources, NWB::Core::ResourceStates::Common, scratchArena);
    const auto scratchAfter = scratchArena.memoryStats();

    EXPECT_FALSE(reads.has_value());
    EXPECT_EQ(scratchAfter.usedBytes, scratchBefore.usedBytes);
    EXPECT_EQ(scratchAfter.reservedBytes, scratchBefore.reservedBytes);
    EXPECT_EQ(scratchAfter.allocationCount, scratchBefore.allocationCount);
    EXPECT_EQ(scratchAfter.reallocationCount, scratchBefore.reallocationCount);
    EXPECT_EQ(scratchAfter.deallocationCount, scratchBefore.deallocationCount);
    const NWB::Core::GpuTaskGraph::DeclarationReadView view(graph);
    ASSERT_TRUE(view.valid());
    EXPECT_EQ(view.resourceCount(), 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

