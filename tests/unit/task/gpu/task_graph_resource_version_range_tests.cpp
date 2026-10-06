// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_resource_version_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TaskGraphResourceVersionTestUtils{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTaskGraphResourceVersion, RequiresWholeResourceRangesForAccelStructAndHazardDomain){
    const auto analyzeVersion = [](
        const Graphics::GpuGraphResourceType::Enum type,
        const Name& identity,
        const Graphics::GpuTaskResourceRange& range,
        Graphics::GpuTaskGraphAnalysisStatus::Enum& outStatus
    ){
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        Graphics::GpuGraphResourceDesc resourceDesc;
        resourceDesc
            .setIdentity(identity)
            .setMarkerLabel("Whole Resource Version")
            .setType(type)
        ;
        if(type == Graphics::GpuGraphResourceType::AccelStruct)
            resourceDesc.setInitialState(Graphics::ResourceStates::Common);
        const Graphics::GpuGraphResourceId resource = graph.importResource(resourceDesc);
        if(!resource.valid())
            return false;
        const Graphics::GpuGraphResourceVersionId version = AddVersion(
            graph,
            resource,
            Graphics::GpuGraphResourceVersionOrigin::ImportedRoot,
            range
        );
        if(!version.valid())
            return false;
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        const bool valid = Analyze(graph, analysis);
        outStatus = analysis.diagnostic().status;
        return valid;
    };

    Graphics::GpuTaskResourceRange partialRange;
    partialRange.bufferRange = Graphics::BufferRange(16u, 32u);
    Graphics::GpuTaskGraphAnalysisStatus::Enum status = Graphics::GpuTaskGraphAnalysisStatus::Success;
    EXPECT_FALSE(analyzeVersion(
        Graphics::GpuGraphResourceType::AccelStruct,
        Name("tests/task_graph_resource_version/partial_accel_struct"),
        partialRange,
        status
    ));
    EXPECT_EQ(status, Graphics::GpuTaskGraphAnalysisStatus::InvalidResourceVersion);
    EXPECT_FALSE(analyzeVersion(
        Graphics::GpuGraphResourceType::HazardDomain,
        Name("tests/task_graph_resource_version/partial_hazard_domain"),
        partialRange,
        status
    ));
    EXPECT_EQ(status, Graphics::GpuTaskGraphAnalysisStatus::InvalidResourceVersion);
    EXPECT_TRUE(analyzeVersion(
        Graphics::GpuGraphResourceType::AccelStruct,
        Name("tests/task_graph_resource_version/whole_accel_struct"),
        Graphics::GpuTaskResourceRange{},
        status
    ));
    EXPECT_TRUE(analyzeVersion(
        Graphics::GpuGraphResourceType::HazardDomain,
        Name("tests/task_graph_resource_version/whole_hazard_domain"),
        Graphics::GpuTaskResourceRange{},
        status
    ));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

