// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>
#include <tests/unit/ecs_graphics/name_cases.adhd.h>

#include <impl/ecs_render/raytrace/prepared_software_bvh_graph_resources.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_prepared_software_bvh_graph_resources_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using TestArena = Tests::TestArena<struct PreparedSoftwareBvhGraphResourcesTestsTag>;

struct BuildContext{
    TestArena testArena;
    Core::GpuTaskGraph graph{ testArena.arena };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(PreparedSoftwareBvhGraphResources, MissingImportedBuildBuffersCannotPublishResources){
    BuildContext context;
    Core::Alloc::ScratchArena scratch{Name(NWB::Tests::EcsGraphicsCases::s_EcsCaseTestsPreparedSwBvhMissing)};
    const auto emptyResources = ResolvePreparedSoftwareBvhGraphResources(context.graph, {}, scratch);
    ASSERT_TRUE(emptyResources);
    EXPECT_TRUE(emptyResources->empty());
    const PreparedMeshSwBvhBuild missingBuild{};
    EXPECT_FALSE(ResolvePreparedSoftwareBvhGraphResources(context.graph, Span<const PreparedMeshSwBvhBuild>(&missingBuild, 1u), scratch));
    const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
    ASSERT_TRUE(view.valid());
    EXPECT_EQ(view.resourceCount(), 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

