// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_import_scaling_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTaskGraphImportScaling, PreservesCompletionUpgradeConflictsAndResetAcrossIndexBoundaries){
    using namespace __hidden_task_graph_import_scaling_tests;
    for(const usize prefixCount : { 0u, 32u, 33u }){
        SCOPED_TRACE(prefixCount);
        TaskGraphTestUtils::TestArena testArena;
        Core::GpuTaskGraph graph(testArena.arena);
        for(usize index = 0u; index < prefixCount; ++index){
            {
                const auto prefixNameResult = IndexedName(Name("tests/import_scaling/completion_prefix/"), index);
                ASSERT_TRUE(prefixNameResult);
                const auto gpuTaskAdmission = graph.importExternalCompletion(Core::GpuExternalCompletionDesc{}.setIdentity(*prefixNameResult).setMarkerLabel("Completion Prefix"));
                ASSERT_TRUE(gpuTaskAdmission);
                ASSERT_TRUE(gpuTaskAdmission->valid());
            }
        }
        const Core::GpuExternalCompletionDesc metadata = Core::GpuExternalCompletionDesc{}.setIdentity(Name("tests/import_scaling/upgraded_completion")).setMarkerLabel("Original Completion");
        const auto idResult = graph.importExternalCompletion(metadata);
        ASSERT_TRUE(idResult);
        const Core::GpuExternalCompletionId id = *idResult;
        ASSERT_TRUE(id.valid());
        u64 metadataRevision = 0u;
        {
            const Core::GpuTaskGraph::DeclarationReadView view(graph);
            metadataRevision = view.declarationRevision();
        }
        Core::GpuExternalCompletionDesc upgraded = metadata;
        upgraded.markerLabel = "Token Upgrade Label";
        upgraded.token = { .value = 31u, .physicalQueueIndex = 0u, .deviceGeneration = 17u, .queue = Core::CommandQueue::Graphics, };
        {
            const auto secondGpuTaskAdmission = graph.importExternalCompletion(upgraded);
            EXPECT_TRUE(secondGpuTaskAdmission);
            if(secondGpuTaskAdmission)
                EXPECT_EQ(*secondGpuTaskAdmission, id);
        }
        u64 upgradedRevision = 0u;
        {
            const Core::GpuTaskGraph::DeclarationReadView view(graph);
            upgradedRevision = view.declarationRevision();
            EXPECT_NE(upgradedRevision, metadataRevision);
            EXPECT_EQ(view.externalCompletionAt(id.index).markerLabel, metadata.markerLabel);
            const Core::QueueSubmissionToken* const token = view.externalCompletionToken(id);
            ASSERT_NE(token, nullptr);
            EXPECT_EQ(token->value, upgraded.token.value);
        }
        {
            const auto thirdGpuTaskAdmission = graph.importExternalCompletion(metadata);
            EXPECT_TRUE(thirdGpuTaskAdmission);
            if(thirdGpuTaskAdmission)
                EXPECT_EQ(*thirdGpuTaskAdmission, id);
        }
        {
            const auto fourthGpuTaskAdmission = graph.importExternalCompletion(upgraded);
            EXPECT_TRUE(fourthGpuTaskAdmission);
            if(fourthGpuTaskAdmission)
                EXPECT_EQ(*fourthGpuTaskAdmission, id);
        }
        Core::GpuExternalCompletionDesc conflict = upgraded;
        ++conflict.token.value;
        EXPECT_FALSE(graph.importExternalCompletion(conflict).has_value());
        Core::GpuExternalCompletionDesc differentGeneration = upgraded;
        differentGeneration.identity = Name("tests/import_scaling/different_generation_completion");
        ++differentGeneration.token.deviceGeneration;
        EXPECT_FALSE(graph.importExternalCompletion(differentGeneration).has_value());
        {
            const Core::GpuTaskGraph::DeclarationReadView view(graph);
            EXPECT_EQ(view.declarationRevision(), upgradedRevision);
            EXPECT_EQ(view.externalCompletionCount(), prefixCount + 1u);
            EXPECT_EQ(view.externalCompletionToken(id)->value, upgraded.token.value);
        }
        graph.reset();
        ++upgraded.token.deviceGeneration;
        const auto reboundResult = graph.importExternalCompletion(upgraded);
        ASSERT_TRUE(reboundResult);
        const Core::GpuExternalCompletionId rebound = *reboundResult;
        ASSERT_TRUE(rebound.valid());
        EXPECT_EQ(rebound.index, 0u);
        EXPECT_NE(rebound.generation, id.generation);
        const Core::GpuTaskGraph::DeclarationReadView view(graph);
        EXPECT_FALSE(view.validExternalCompletion(id));
        EXPECT_EQ(view.externalCompletionToken(rebound)->value, upgraded.token.value);
        EXPECT_EQ(view.externalCompletionToken(rebound)->deviceGeneration, upgraded.token.deviceGeneration);
        EXPECT_TRUE(view.validForDeviceGeneration(upgraded.token.deviceGeneration));
        EXPECT_FALSE(view.validForDeviceGeneration(static_cast<u16>(upgraded.token.deviceGeneration - 1u)));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

