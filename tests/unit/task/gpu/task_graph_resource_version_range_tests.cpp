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
    const auto analyzeVersion = [](const Graphics::GpuGraphResourceType::Enum type, const Name& identity, const Graphics::GpuTaskResourceRange& range)->Expected<void, Graphics::GpuTaskGraphAnalysisStatus::Enum>{
        Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphResourceVersionTestUtils::s_ResourceVersionScratchArena);

        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        Graphics::GpuGraphResourceDesc resourceDesc;
        resourceDesc.setIdentity(identity).setMarkerLabel("Whole Resource Version").setType(type);
        if(type == Graphics::GpuGraphResourceType::AccelStruct)
            resourceDesc.setInitialState(Graphics::ResourceStates::Common);
        const auto resourceResult = graph.importResource(resourceDesc);
        if(!resourceResult)
            return MakeUnexpected(Graphics::GpuTaskGraphAnalysisStatus::NotAnalyzed);
        const Graphics::GpuGraphResourceId resource = *resourceResult;
        if(!resource.valid())
            return MakeUnexpected(Graphics::GpuTaskGraphAnalysisStatus::NotAnalyzed);
        const auto versionResult = AddVersion(graph, resource, Graphics::GpuGraphResourceVersionOrigin::ImportedRoot, range);
        if(!versionResult)
            return MakeUnexpected(Graphics::GpuTaskGraphAnalysisStatus::NotAnalyzed);
        const Graphics::GpuGraphResourceVersionId version = *versionResult;
        if(!version.valid())
            return MakeUnexpected(Graphics::GpuTaskGraphAnalysisStatus::NotAnalyzed);
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        if(!Analyze(operationScratchArena, graph, analysis))
            return MakeUnexpected(analysis.diagnostic().status);
        return {};
    };

    Graphics::GpuTaskResourceRange partialRange;
    partialRange.bufferRange = Graphics::BufferRange(16u, 32u);
    const auto partialAccelStruct = analyzeVersion(Graphics::GpuGraphResourceType::AccelStruct, Name("tests/task_graph_resource_version/partial_accel_struct"), partialRange);
    ASSERT_FALSE(partialAccelStruct);
    EXPECT_EQ(partialAccelStruct.error(), Graphics::GpuTaskGraphAnalysisStatus::InvalidResourceVersion);
    const auto partialHazardDomain = analyzeVersion(Graphics::GpuGraphResourceType::HazardDomain, Name("tests/task_graph_resource_version/partial_hazard_domain"), partialRange);
    ASSERT_FALSE(partialHazardDomain);
    EXPECT_EQ(partialHazardDomain.error(), Graphics::GpuTaskGraphAnalysisStatus::InvalidResourceVersion);
    EXPECT_TRUE(analyzeVersion(Graphics::GpuGraphResourceType::AccelStruct, Name("tests/task_graph_resource_version/whole_accel_struct"), Graphics::GpuTaskResourceRange{}));
    EXPECT_TRUE(analyzeVersion(Graphics::GpuGraphResourceType::HazardDomain, Name("tests/task_graph_resource_version/whole_hazard_domain"), Graphics::GpuTaskResourceRange{}));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

