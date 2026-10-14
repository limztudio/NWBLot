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
    const auto reads = NWB::Impl::ImportRayTracingSceneGraphReads(graph, resources, NWB::Core::ResourceStates::Common, scratchArena);

    EXPECT_FALSE(reads.valid());
    EXPECT_TRUE(reads.csgUses.empty());
    EXPECT_EQ(reads.csgUses.get_allocator().arenaPtr(), &scratchArena);
    const NWB::Core::GpuTaskGraph::DeclarationReadView view(graph);
    ASSERT_TRUE(view.valid());
    EXPECT_EQ(view.resourceCount(), 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

