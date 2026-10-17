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


[[nodiscard]] static Expected<Name> IndexedIdentity(const Name& prefix, const usize index){
    char text[32u] = {};
    return DeriveName(prefix, FormatDecimal(index, text));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTaskGraphImportScaling, PreservesCompletionUpgradeConflictsAndResetAcrossIndexBoundaries){
    using namespace __hidden_task_graph_import_scaling_tests;
    for(const usize prefixCount : { 0u, 32u, 33u }){
        SCOPED_TRACE(prefixCount);
        TaskGraphTestUtils::TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        for(usize index = 0u; index < prefixCount; ++index){
            {
                const auto derivedNameAt58Col127 = IndexedIdentity(Name("tests/import_scaling/completion_prefix/"), index);
                ASSERT_TRUE(derivedNameAt58Col127);
                const auto gpuAdmissionAt57 = graph.importExternalCompletion(Graphics::GpuExternalCompletionDesc{}.setIdentity(*derivedNameAt58Col127).setMarkerLabel("Completion Prefix"));
                ASSERT_TRUE(gpuAdmissionAt57);
                ASSERT_TRUE(gpuAdmissionAt57->valid());
            }
        }
        const Graphics::GpuExternalCompletionDesc metadata = Graphics::GpuExternalCompletionDesc{}.setIdentity(Name("tests/import_scaling/upgraded_completion")).setMarkerLabel("Original Completion");
        const auto idResult = graph.importExternalCompletion(metadata);
        ASSERT_TRUE(idResult);
        const Graphics::GpuExternalCompletionId id = *idResult;
        ASSERT_TRUE(id.valid());
        u64 metadataRevision = 0u;
        {
            const Graphics::GpuTaskGraph::DeclarationReadView view(graph);
            metadataRevision = view.declarationRevision();
        }
        Graphics::GpuExternalCompletionDesc upgraded = metadata;
        upgraded.markerLabel = "Token Upgrade Label";
        upgraded.token = { .value = 31u, .physicalQueueIndex = 0u, .deviceGeneration = 17u, .queue = Graphics::CommandQueue::Graphics, };
        {
            const auto gpuAdmissionAt70 = graph.importExternalCompletion(upgraded);
            EXPECT_TRUE(gpuAdmissionAt70);
            if(gpuAdmissionAt70)
                EXPECT_EQ(*gpuAdmissionAt70, id);
        }
        u64 upgradedRevision = 0u;
        {
            const Graphics::GpuTaskGraph::DeclarationReadView view(graph);
            upgradedRevision = view.declarationRevision();
            EXPECT_NE(upgradedRevision, metadataRevision);
            EXPECT_EQ(view.externalCompletionAt(id.index).markerLabel, metadata.markerLabel);
            const Graphics::QueueSubmissionToken* const token = view.externalCompletionToken(id);
            ASSERT_NE(token, nullptr);
            EXPECT_EQ(token->value, upgraded.token.value);
        }
        {
            const auto gpuAdmissionAt81 = graph.importExternalCompletion(metadata);
            EXPECT_TRUE(gpuAdmissionAt81);
            if(gpuAdmissionAt81)
                EXPECT_EQ(*gpuAdmissionAt81, id);
        }
        {
            const auto gpuAdmissionAt82 = graph.importExternalCompletion(upgraded);
            EXPECT_TRUE(gpuAdmissionAt82);
            if(gpuAdmissionAt82)
                EXPECT_EQ(*gpuAdmissionAt82, id);
        }
        Graphics::GpuExternalCompletionDesc conflict = upgraded;
        ++conflict.token.value;
        EXPECT_FALSE(graph.importExternalCompletion(conflict).has_value());
        Graphics::GpuExternalCompletionDesc differentGeneration = upgraded;
        differentGeneration.identity = Name("tests/import_scaling/different_generation_completion");
        ++differentGeneration.token.deviceGeneration;
        EXPECT_FALSE(graph.importExternalCompletion(differentGeneration).has_value());
        {
            const Graphics::GpuTaskGraph::DeclarationReadView view(graph);
            EXPECT_EQ(view.declarationRevision(), upgradedRevision);
            EXPECT_EQ(view.externalCompletionCount(), prefixCount + 1u);
            EXPECT_EQ(view.externalCompletionToken(id)->value, upgraded.token.value);
        }
        graph.reset();
        ++upgraded.token.deviceGeneration;
        const auto reboundResult = graph.importExternalCompletion(upgraded);
        ASSERT_TRUE(reboundResult);
        const Graphics::GpuExternalCompletionId rebound = *reboundResult;
        ASSERT_TRUE(rebound.valid());
        EXPECT_EQ(rebound.index, 0u);
        EXPECT_NE(rebound.generation, id.generation);
        const Graphics::GpuTaskGraph::DeclarationReadView view(graph);
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

