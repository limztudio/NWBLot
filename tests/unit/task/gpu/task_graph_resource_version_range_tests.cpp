// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_resource_version_test_utils.h"

#include <tests/unit/task/gpu/case_types.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TaskGraphResourceVersionTestUtils{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTaskGraphResourceVersion, RequiresWholeResourceRangesForAccelStructAndHazardDomain){
    const auto analyzeVersion = [](const Core::GpuGraphResourceType::Enum type, const Name& identity, const Core::GpuTaskResourceRange& range)->Expected<void, Core::GpuTaskGraphAnalysisStatus::Enum>{
        Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphResourceVersionTestUtils::s_ResourceVersionScratchArena);

        TestArena testArena;
        Core::GpuTaskGraph graph(testArena.arena);
        Core::GpuGraphResourceDesc resourceDesc;
        resourceDesc.setIdentity(identity).setMarkerLabel("Whole Resource Version").setType(type);
        if(type == Core::GpuGraphResourceType::AccelStruct)
            resourceDesc.setInitialState(Core::ResourceStates::Common);
        const auto resourceResult = graph.importResource(resourceDesc);
        if(!resourceResult)
            return MakeUnexpected(Core::GpuTaskGraphAnalysisStatus::NotAnalyzed);
        const Core::GpuGraphResourceId resource = *resourceResult;
        if(!resource.valid())
            return MakeUnexpected(Core::GpuTaskGraphAnalysisStatus::NotAnalyzed);
        const auto versionResult = AddVersion(graph, resource, Core::GpuGraphResourceVersionOrigin::ImportedRoot, range);
        if(!versionResult)
            return MakeUnexpected(Core::GpuTaskGraphAnalysisStatus::NotAnalyzed);
        const Core::GpuGraphResourceVersionId version = *versionResult;
        if(!version.valid())
            return MakeUnexpected(Core::GpuTaskGraphAnalysisStatus::NotAnalyzed);
        Core::GpuTaskGraphAnalysis analysis(testArena.arena);
        if(!TaskGraphTestUtils::Analyze(graph, operationScratchArena, analysis))
            return MakeUnexpected(analysis.diagnostic().status);
        return {};
    };

    Core::GpuTaskResourceRange partialRange;
    partialRange.bufferRange = Core::BufferRange(16u, 32u);
    const auto partialAccelStruct = analyzeVersion(Core::GpuGraphResourceType::AccelStruct, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionPartialAccelStruct), partialRange);
    ASSERT_FALSE(partialAccelStruct);
    EXPECT_EQ(partialAccelStruct.error(), Core::GpuTaskGraphAnalysisStatus::InvalidResourceVersion);
    const auto partialHazardDomain = analyzeVersion(Core::GpuGraphResourceType::HazardDomain, Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResourceVersionPartialHazardDomain), partialRange);
    ASSERT_FALSE(partialHazardDomain);
    EXPECT_EQ(partialHazardDomain.error(), Core::GpuTaskGraphAnalysisStatus::InvalidResourceVersion);
    EXPECT_TRUE(analyzeVersion(Core::GpuGraphResourceType::AccelStruct, Name(NWB::Tests::TaskGraphTestUtils::s_TaskGraphCaseTestsTaskGraphResourceVersionWholeAccelStruct), Core::GpuTaskResourceRange{}));
    EXPECT_TRUE(analyzeVersion(Core::GpuGraphResourceType::HazardDomain, Name(NWB::Tests::TaskGraphTestUtils::s_TaskGraphCaseTestsTaskGraphResourceVersionWholeHazardDomain), Core::GpuTaskResourceRange{}));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

