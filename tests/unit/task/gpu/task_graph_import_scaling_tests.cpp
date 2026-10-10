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

namespace ImportScenario{
    enum Enum : u8{
        MetadataPipeline,
        ExternalCompletion,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Name IndexedIdentity(const Name& prefix, const usize index){
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
            ASSERT_TRUE(graph.importExternalCompletion(Graphics::GpuExternalCompletionDesc{}
                .setIdentity(IndexedIdentity(Name("tests/import_scaling/completion_prefix/"), index))
                .setMarkerLabel("Completion Prefix")
            ).valid());
        }
        const Graphics::GpuExternalCompletionDesc metadata = Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/import_scaling/upgraded_completion"))
            .setMarkerLabel("Original Completion")
        ;
        const Graphics::GpuExternalCompletionId id = graph.importExternalCompletion(metadata);
        ASSERT_TRUE(id.valid());
        u64 metadataRevision = 0u;
        {
            const Graphics::GpuTaskGraph::DeclarationReadView view(graph);
            metadataRevision = view.declarationRevision();
        }
        Graphics::GpuExternalCompletionDesc upgraded = metadata;
        upgraded.markerLabel = "Token Upgrade Label";
        upgraded.token = {
            .value = 31u,
            .physicalQueueIndex = 0u,
            .deviceGeneration = 17u,
            .queue = Graphics::CommandQueue::Graphics,
        };
        EXPECT_EQ(graph.importExternalCompletion(upgraded), id);
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
        EXPECT_EQ(graph.importExternalCompletion(metadata), id);
        EXPECT_EQ(graph.importExternalCompletion(upgraded), id);
        Graphics::GpuExternalCompletionDesc conflict = upgraded;
        ++conflict.token.value;
        EXPECT_FALSE(graph.importExternalCompletion(conflict).valid());
        Graphics::GpuExternalCompletionDesc differentGeneration = upgraded;
        differentGeneration.identity = Name("tests/import_scaling/different_generation_completion");
        ++differentGeneration.token.deviceGeneration;
        EXPECT_FALSE(graph.importExternalCompletion(differentGeneration).valid());
        {
            const Graphics::GpuTaskGraph::DeclarationReadView view(graph);
            EXPECT_EQ(view.declarationRevision(), upgradedRevision);
            EXPECT_EQ(view.externalCompletionCount(), prefixCount + 1u);
            EXPECT_EQ(view.externalCompletionToken(id)->value, upgraded.token.value);
        }
        graph.reset();
        ++upgraded.token.deviceGeneration;
        const Graphics::GpuExternalCompletionId rebound = graph.importExternalCompletion(upgraded);
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

