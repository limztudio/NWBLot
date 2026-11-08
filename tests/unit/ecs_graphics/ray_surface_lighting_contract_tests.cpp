// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>
#include <tests/unit/ecs_graphics/name_cases.adhd.h>

#include <impl/ecs_render/raytrace/task_graph_scene_resources.h>

#include <gtest/gtest.h>


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
    NWB::Core::Alloc::ScratchArena scratchArena{Name(NWB::Tests::EcsGraphicsCases::s_EcsCaseTestsRaySceneGraphReadsScratch)};
    NWB::Impl::RayTracingSceneGraphResources resources;
    const auto scratchBefore = scratchArena.memoryStats();
    const auto reads = NWB::Impl::ImportRayTracingSceneGraphReads(resources, NWB::Core::ResourceStates::Common, {}, graph, scratchArena);
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

