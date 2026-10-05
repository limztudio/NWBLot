// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

#include <global/timer.h>


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
        TypedPipeline,
        ExternalCompletion,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Name IndexedIdentity(const Name& prefix, const usize index){
    char text[32u] = {};
    return DeriveName(prefix, FormatDecimal(index, text));
}

[[nodiscard]] static Graphics::ComputePipelineHandle MakeComputePipeline(
    Graphics::GraphicsArena& arena,
    const Graphics::GraphicsBackend::VulkanContext& context){
    Graphics::ComputePipeline* const pipeline = NewArenaObject<Graphics::ComputePipeline>(arena, context);
    if(!pipeline)
        return {};
    return Graphics::ComputePipelineHandle(pipeline, Graphics::ComputePipelineHandle::deleter_type(&arena), s_AdoptRef);
}

static void CheckImportScaling(const usize importCount, const ImportScenario::Enum scenario){
    TaskGraphTestUtils::TestArena testArena;
    Graphics::GraphicsAllocator allocator(testArena.arena);
    Core::CpuTaskScheduler scheduler(0u);
    const Graphics::GraphicsBackend::VulkanContext context(allocator, scheduler, 17u);
    Core::Alloc::ScratchArena inputScratch(Name("tests/import_scaling/inputs"));
    Vector<Graphics::GpuGraphPipelineDesc, Core::Alloc::ScratchArena> pipelineDescriptions(inputScratch);
    Vector<Graphics::ComputePipelineHandle, Core::Alloc::ScratchArena> pipelines(inputScratch);
    Vector<Graphics::GpuExternalCompletionDesc, Core::Alloc::ScratchArena> completionDescriptions(inputScratch);
    Vector<Graphics::GpuGraphPipelineId, Core::Alloc::ScratchArena> pipelineIDs(importCount, inputScratch);
    Vector<Graphics::GpuExternalCompletionId, Core::Alloc::ScratchArena> completionIDs(importCount, inputScratch);
    if(scenario == ImportScenario::ExternalCompletion)
        completionDescriptions.reserve(importCount);
    else
        pipelineDescriptions.reserve(importCount);
    if(scenario == ImportScenario::TypedPipeline)
        pipelines.reserve(importCount);
    for(usize index = 0u; index < importCount; ++index){
        const Name identity = IndexedIdentity(Name("tests/import_scaling/import/"), index);
        if(scenario == ImportScenario::ExternalCompletion){
            completionDescriptions.push_back(Graphics::GpuExternalCompletionDesc{}
                .setIdentity(identity)
                .setMarkerLabel("Indexed Completion")
                .setToken({
                    .value = index + 1u,
                    .physicalQueueIndex = 0u,
                    .deviceGeneration = 17u,
                    .queue = Graphics::CommandQueue::Graphics,
                })
            );
        }
        else{
            pipelineDescriptions.push_back(Graphics::GpuGraphPipelineDesc{}
                .setIdentity(identity)
                .setMarkerLabel("Indexed Pipeline")
                .setType(Graphics::GpuGraphPipelineType::Compute)
            );
            if(scenario == ImportScenario::TypedPipeline){
                pipelines.push_back(MakeComputePipeline(testArena.arena, context));
                ASSERT_TRUE(pipelines.back());
            }
        }
    }

    Graphics::GpuTaskGraph graph(testArena.arena);
    u64 minimumAppendNanoseconds = Limit<u64>::s_Max;
    u64 minimumReimportNanoseconds = Limit<u64>::s_Max;
    for(usize iteration = 0u; iteration < 4u; ++iteration){
        graph.reset();
        bool imported = true;
        const Timer appendBegin = TimerNow();
        for(usize index = 0u; index < importCount; ++index){
            if(scenario == ImportScenario::ExternalCompletion){
                completionIDs[index] = graph.importExternalCompletion(completionDescriptions[index]);
                imported = imported && completionIDs[index].valid();
            }
            else{
                pipelineIDs[index] = scenario == ImportScenario::TypedPipeline
                    ? graph.importComputePipeline(pipelines[index], pipelineDescriptions[index])
                    : graph.importPipeline(pipelineDescriptions[index])
                ;
                imported = imported && pipelineIDs[index].valid();
            }
        }
        const u64 appendNanoseconds = DurationInNS<u64>(TimerNow(), appendBegin);
        ASSERT_TRUE(imported);
        const Timer reimportBegin = TimerNow();
        for(usize remaining = importCount; remaining != 0u; --remaining){
            const usize index = remaining - 1u;
            if(scenario == ImportScenario::ExternalCompletion)
                imported = imported && graph.importExternalCompletion(completionDescriptions[index]) == completionIDs[index];
            else{
                const Graphics::GpuGraphPipelineId id = scenario == ImportScenario::TypedPipeline
                    ? graph.importComputePipeline(pipelines[index], pipelineDescriptions[index])
                    : graph.importPipeline(pipelineDescriptions[index])
                ;
                imported = imported && id == pipelineIDs[index];
            }
        }
        const u64 reimportNanoseconds = DurationInNS<u64>(TimerNow(), reimportBegin);
        ASSERT_TRUE(imported);
        if(iteration != 0u){
            minimumAppendNanoseconds = Min(minimumAppendNanoseconds, appendNanoseconds);
            minimumReimportNanoseconds = Min(minimumReimportNanoseconds, reimportNanoseconds);
        }
    }
    const Graphics::GpuTaskGraph::DeclarationReadView view(graph);
    ASSERT_TRUE(view.valid());
    EXPECT_EQ(view.pipelineCount(), scenario == ImportScenario::ExternalCompletion ? 0u : importCount);
    EXPECT_EQ(view.externalCompletionCount(), scenario == ImportScenario::ExternalCompletion ? importCount : 0u);
    for(usize index = 0u; index < importCount; ++index){
        if(scenario == ImportScenario::ExternalCompletion){
            EXPECT_EQ(completionIDs[index].index, index);
            const Graphics::QueueSubmissionToken* const token = view.externalCompletionToken(completionIDs[index]);
            ASSERT_NE(token, nullptr);
            EXPECT_EQ(token->value, completionDescriptions[index].token.value);
        }
        else{
            EXPECT_EQ(pipelineIDs[index].index, index);
            if(scenario == ImportScenario::TypedPipeline){
                EXPECT_EQ(view.computePipelineFor(pipelineIDs[index]), pipelines[index].get());
                EXPECT_EQ(pipelines[index]->getReferenceCount(), 2u);
            }
        }
    }
    RecordUnsignedTestProperty("import_count", importCount);
    RecordUnsignedTestProperty("append_ns", minimumAppendNanoseconds);
    RecordUnsignedTestProperty("reimport_ns", minimumReimportNanoseconds);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTaskGraphImportScaling, PreservesPipelineConflictsAndReferencesAcrossIndexBoundaries){
    using namespace __hidden_task_graph_import_scaling_tests;
    for(const usize prefixCount : { 0u, 32u, 33u }){
        SCOPED_TRACE(prefixCount);
        TaskGraphTestUtils::TestArena testArena;
        Graphics::GraphicsAllocator allocator(testArena.arena);
        Core::CpuTaskScheduler scheduler(0u);
        const Graphics::GraphicsBackend::VulkanContext context(allocator, scheduler, 17u);
        const Graphics::ComputePipelineHandle first = MakeComputePipeline(testArena.arena, context);
        const Graphics::ComputePipelineHandle second = MakeComputePipeline(testArena.arena, context);
        ASSERT_TRUE(first);
        ASSERT_TRUE(second);
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphPipelineDesc firstDescription = Graphics::GpuGraphPipelineDesc{}
            .setIdentity(Name("tests/import_scaling/typed_first"))
            .setMarkerLabel("First Pipeline")
            .setType(Graphics::GpuGraphPipelineType::Compute)
        ;
        const Graphics::GpuGraphPipelineId firstID = graph.importComputePipeline(first, firstDescription);
        ASSERT_TRUE(firstID.valid());
        for(usize index = 0u; index < prefixCount; ++index){
            ASSERT_TRUE(graph.importPipeline(Graphics::GpuGraphPipelineDesc{}
                .setIdentity(IndexedIdentity(Name("tests/import_scaling/pipeline_prefix/"), index))
                .setMarkerLabel("Pipeline Prefix")
                .setType(Graphics::GpuGraphPipelineType::Compute)
            ).valid());
        }
        const Graphics::GpuGraphPipelineDesc metadataDescription = Graphics::GpuGraphPipelineDesc{}
            .setIdentity(Name("tests/import_scaling/metadata_only"))
            .setMarkerLabel("Metadata Pipeline")
            .setType(Graphics::GpuGraphPipelineType::Compute)
        ;
        const Graphics::GpuGraphPipelineId metadataID = graph.importPipeline(metadataDescription);
        ASSERT_TRUE(metadataID.valid());
        u64 revision = 0u;
        {
            const Graphics::GpuTaskGraph::DeclarationReadView view(graph);
            revision = view.declarationRevision();
        }
        EXPECT_FALSE(graph.importComputePipeline(second, metadataDescription).valid());
        EXPECT_FALSE(graph.importComputePipeline(second, firstDescription).valid());
        Graphics::GpuGraphPipelineDesc alias = firstDescription;
        alias.identity = Name("tests/import_scaling/typed_alias");
        EXPECT_FALSE(graph.importComputePipeline(first, alias).valid());
        Graphics::GpuGraphPipelineDesc differentKind = firstDescription;
        differentKind.type = Graphics::GpuGraphPipelineType::Graphics;
        EXPECT_FALSE(graph.importPipeline(differentKind).valid());
        Graphics::GpuGraphPipelineDesc changedLabel = firstDescription;
        changedLabel.markerLabel = "Replacement Pipeline Label";
        EXPECT_EQ(graph.importComputePipeline(first, changedLabel), firstID);
        EXPECT_EQ(graph.importPipeline(changedLabel), firstID);
        {
            const Graphics::GpuTaskGraph::DeclarationReadView view(graph);
            EXPECT_EQ(view.declarationRevision(), revision);
            EXPECT_EQ(view.pipelineCount(), prefixCount + 2u);
            EXPECT_EQ(view.pipelineAt(firstID.index).markerLabel, firstDescription.markerLabel);
            EXPECT_EQ(view.computePipelineFor(firstID), first.get());
            EXPECT_EQ(view.computePipelineFor(metadataID), nullptr);
        }
        EXPECT_EQ(first->getReferenceCount(), 2u);
        EXPECT_EQ(second->getReferenceCount(), 1u);
        graph.reset();
        EXPECT_EQ(first->getReferenceCount(), 1u);
        const Graphics::GpuGraphPipelineId rebound = graph.importComputePipeline(first, firstDescription);
        ASSERT_TRUE(rebound.valid());
        EXPECT_EQ(rebound.index, 0u);
        EXPECT_NE(rebound.generation, firstID.generation);
        const Graphics::GpuTaskGraph::DeclarationReadView view(graph);
        EXPECT_FALSE(view.validPipeline(firstID));
        EXPECT_EQ(view.computePipelineFor(rebound), first.get());
    }
}

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

TEST(GpuTaskGraphImportScaling, DISABLED_MetadataPipelineBenchmark8Imports){
    using namespace __hidden_task_graph_import_scaling_tests;
    CheckImportScaling(8u, ImportScenario::MetadataPipeline);
}

TEST(GpuTaskGraphImportScaling, DISABLED_MetadataPipelineBenchmark1024Imports){
    using namespace __hidden_task_graph_import_scaling_tests;
    CheckImportScaling(1024u, ImportScenario::MetadataPipeline);
}

TEST(GpuTaskGraphImportScaling, DISABLED_MetadataPipelineBenchmark4096Imports){
    using namespace __hidden_task_graph_import_scaling_tests;
    CheckImportScaling(4096u, ImportScenario::MetadataPipeline);
}

TEST(GpuTaskGraphImportScaling, DISABLED_TypedPipelineBenchmark8Imports){
    using namespace __hidden_task_graph_import_scaling_tests;
    CheckImportScaling(8u, ImportScenario::TypedPipeline);
}

TEST(GpuTaskGraphImportScaling, DISABLED_TypedPipelineBenchmark1024Imports){
    using namespace __hidden_task_graph_import_scaling_tests;
    CheckImportScaling(1024u, ImportScenario::TypedPipeline);
}

TEST(GpuTaskGraphImportScaling, DISABLED_TypedPipelineBenchmark4096Imports){
    using namespace __hidden_task_graph_import_scaling_tests;
    CheckImportScaling(4096u, ImportScenario::TypedPipeline);
}

TEST(GpuTaskGraphImportScaling, DISABLED_ExternalCompletionBenchmark8Imports){
    using namespace __hidden_task_graph_import_scaling_tests;
    CheckImportScaling(8u, ImportScenario::ExternalCompletion);
}

TEST(GpuTaskGraphImportScaling, DISABLED_ExternalCompletionBenchmark1024Imports){
    using namespace __hidden_task_graph_import_scaling_tests;
    CheckImportScaling(1024u, ImportScenario::ExternalCompletion);
}

TEST(GpuTaskGraphImportScaling, DISABLED_ExternalCompletionBenchmark4096Imports){
    using namespace __hidden_task_graph_import_scaling_tests;
    CheckImportScaling(4096u, ImportScenario::ExternalCompletion);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

