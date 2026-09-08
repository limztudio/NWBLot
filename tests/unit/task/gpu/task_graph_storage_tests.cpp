// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>

#include <core/graphics/task_graph/task_graph.h>
#include <global/text_utils.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_storage_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestArena = ::NWB::Tests::TestArena<struct TaskGraphStorageTestsTag>;
inline constexpr Name s_StorageScratchArena("tests/graphics/task_graph_storage_scratch");


TEST(GpuTaskGraphStorage, LargeTaskChainPreservesDeclarationsAndReusesStorage){
    constexpr usize s_TaskCount = 4096u;
    TestArena testArena;
    Core::Alloc::ScratchArena scratchArena(s_StorageScratchArena);
    Vector<Name, Core::Alloc::ScratchArena> identities(scratchArena);
    identities.reserve(s_TaskCount);
    for(usize index = 0u; index < s_TaskCount; ++index){
        char indexText[32u] = {};
        identities.push_back(DeriveName(Name("tests/graph_storage/task/"), FormatDecimal(index, indexText)));
    }

    Core::GpuTaskGraph graph(testArena.arena);
    Core::GpuTaskId previousGenerationTask;
    for(usize pass = 0u; pass < 2u; ++pass){
        const Core::GpuGraphResourceId resource = graph.importResource(Core::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/graph_storage/input"))
            .setMarkerLabel("Storage Input")
            .setType(Core::GpuGraphResourceType::Buffer)
            .setInitialState(Core::ResourceStates::ShaderResource)
        );
        ASSERT_TRUE(resource.valid());
        const Core::GpuExternalCompletionId completion = graph.importExternalCompletion(Core::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/graph_storage/completion"))
            .setMarkerLabel("Storage Completion")
        );
        ASSERT_TRUE(completion.valid());
        const Core::GpuTaskResourceUse use{
            .resource = resource,
            .range = {},
            .requiredState = Core::ResourceStates::ShaderResource,
            .access = Core::GpuTaskResourceAccess::Read,
        };
        Core::GpuTaskId previousTask;
        bool accepted = true;
        const ArenaMemoryStats memoryBefore = testArena.arena.memoryStats();
        const Timer begin = TimerNow();
        for(usize index = 0u; index < s_TaskCount; ++index){
            Core::GpuTaskDesc desc;
            desc
                .setIdentity(identities[index])
                .setMarkerLabel("Storage Chain Task")
                .setDependencies(&previousTask, index == 0u ? 0u : 1u)
                .setExternalDependencies(&completion, 1u)
                .setResourceUses(&use, 1u)
            ;
            previousTask = graph.addTask(desc);
            accepted = accepted && previousTask.valid();
        }
        const u64 nanoseconds = DurationInNS<u64>(TimerNow(), begin);
        const ArenaMemoryStats memoryAfter = testArena.arena.memoryStats();
        const u64 allocations = memoryAfter.allocationCount - memoryBefore.allocationCount;
        char durationText[32u] = {};
        char allocationText[32u] = {};
        RecordProperty(pass == 0u ? "declaration_ns" : "reused_declaration_ns", FormatDecimal(nanoseconds, durationText).data());
        RecordProperty(pass == 0u ? "declaration_allocations" : "reused_declaration_allocations", FormatDecimal(allocations, allocationText).data());
        ASSERT_TRUE(accepted);
        if(pass != 0u){
            EXPECT_EQ(memoryAfter.allocationCount, memoryBefore.allocationCount);
            EXPECT_EQ(memoryAfter.usedBytes, memoryBefore.usedBytes);
            EXPECT_EQ(memoryAfter.reservedBytes, memoryBefore.reservedBytes);
            EXPECT_EQ(memoryAfter.reallocationCount, memoryBefore.reallocationCount);
        }
        {
            const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
            ASSERT_TRUE(declarations.valid());
            EXPECT_FALSE(declarations.validTask(previousGenerationTask));
            ASSERT_EQ(declarations.taskCount(), s_TaskCount);
            for(usize index = 0u; index < s_TaskCount; ++index){
                const Core::GpuTaskGraphTaskView task = declarations.taskAt(index);
                EXPECT_EQ(task.identity, identities[index]);
                EXPECT_EQ(task.markerLabel, AStringView("Storage Chain Task"));
                ASSERT_EQ(task.dependencyCount, index == 0u ? 0u : 1u);
                if(index != 0u)
                    EXPECT_EQ(task.dependencies[0u], declarations.taskAt(index - 1u).id);
                ASSERT_EQ(task.externalDependencyCount, 1u);
                EXPECT_EQ(task.externalDependencies[0u], completion);
                ASSERT_EQ(task.resourceUseCount, 1u);
                EXPECT_EQ(task.resourceUses[0u].resource, resource);
                EXPECT_EQ(task.resourceUses[0u].requiredState, use.requiredState);
            }
        }
        previousGenerationTask = previousTask;
        graph.reset();
    }
}

TEST(GpuTaskGraphStorage, MixedDeclarationsAndUploadBytesSurviveStorageGrowth){
    constexpr usize s_RecordCount = 256u;
    TestArena testArena;
    Core::Alloc::ScratchArena scratchArena(s_StorageScratchArena);
    Vector<Name, Core::Alloc::ScratchArena> identities(scratchArena);
    identities.reserve(s_RecordCount);
    for(usize index = 0u; index < s_RecordCount; ++index){
        char indexText[32u] = {};
        identities.push_back(DeriveName(Name("tests/graph_storage/record/"), FormatDecimal(index, indexText)));
    }
    Core::GpuTaskGraph graph(testArena.arena);
    Vector<Core::GpuUploadBlobId, Core::Alloc::ScratchArena> uploads(scratchArena);
    uploads.reserve(s_RecordCount);
    bool accepted = true;
    const Timer begin = TimerNow();
    for(usize index = 0u; index < s_RecordCount; ++index){
        const Core::GpuGraphResourceId resource = graph.importResource(Core::GpuGraphResourceDesc{}
            .setIdentity(identities[index])
            .setMarkerLabel("Storage Resource")
            .setType(Core::GpuGraphResourceType::Buffer)
        );
        const Core::GpuGraphResourceSetId resourceSet = graph.importResourceSet(Core::GpuGraphResourceSetDesc{}
            .setIdentity(identities[index])
            .setMarkerLabel("Storage Resource Set")
            .setMembers(&resource, 1u)
        );
        const Core::GpuGraphPipelineId pipeline = graph.importPipeline(Core::GpuGraphPipelineDesc{}
            .setIdentity(identities[index])
            .setMarkerLabel("Storage Pipeline")
            .setType(Core::GpuGraphPipelineType::Compute)
        );
        const Core::GpuExternalCompletionId completion = graph.importExternalCompletion(Core::GpuExternalCompletionDesc{}
            .setIdentity(identities[index])
            .setMarkerLabel("Storage Completion")
        );
        const u64 bytes[] = { static_cast<u64>(index), ~static_cast<u64>(index) };
        const Core::GpuUploadBlobId upload = graph.copyUploadData(bytes, sizeof(bytes), alignof(u64));
        uploads.push_back(upload);
        accepted = accepted && resource.valid() && resourceSet.valid() && pipeline.valid() && completion.valid() && upload.valid();
    }
    const u64 nanoseconds = DurationInNS<u64>(TimerNow(), begin);
    char valueText[32u] = {};
    RecordProperty("mixed_declaration_ns", FormatDecimal(nanoseconds, valueText).data());
    ASSERT_TRUE(accepted);
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        ASSERT_EQ(declarations.resourceCount(), s_RecordCount);
        ASSERT_EQ(declarations.resourceSetCount(), s_RecordCount);
        ASSERT_EQ(declarations.pipelineCount(), s_RecordCount);
        ASSERT_EQ(declarations.externalCompletionCount(), s_RecordCount);
        ASSERT_EQ(declarations.uploadBlobCount(), s_RecordCount);
        for(usize index = 0u; index < s_RecordCount; ++index){
            const Core::GpuTaskGraphResourceView resource = declarations.resourceAt(index);
            const Core::GpuTaskGraphResourceSetView resourceSet = declarations.resourceSetAt(index);
            const Core::GpuTaskGraphPipelineView pipeline = declarations.pipelineAt(index);
            const Core::GpuTaskGraphExternalCompletionView completion = declarations.externalCompletionAt(index);
            EXPECT_EQ(resource.identity, identities[index]);
            EXPECT_EQ(resource.markerLabel, AStringView("Storage Resource"));
            EXPECT_EQ(resourceSet.identity, identities[index]);
            EXPECT_EQ(resourceSet.markerLabel, AStringView("Storage Resource Set"));
            ASSERT_EQ(resourceSet.memberCount, 1u);
            EXPECT_EQ(resourceSet.members[0u], resource.id);
            EXPECT_EQ(pipeline.identity, identities[index]);
            EXPECT_EQ(pipeline.markerLabel, AStringView("Storage Pipeline"));
            EXPECT_EQ(pipeline.type, Core::GpuGraphPipelineType::Compute);
            EXPECT_EQ(completion.identity, identities[index]);
            EXPECT_EQ(completion.markerLabel, AStringView("Storage Completion"));
            usize byteSize = 0u;
            const void* const storedBytes = declarations.uploadBlobData(uploads[index], byteSize);
            const u64 expectedBytes[] = { static_cast<u64>(index), ~static_cast<u64>(index) };
            ASSERT_NE(storedBytes, nullptr);
            ASSERT_EQ(byteSize, sizeof(expectedBytes));
            EXPECT_EQ(NWB_MEMCMP(storedBytes, expectedBytes, sizeof(expectedBytes)), 0);
        }
    }
    graph.reset();
    const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
    EXPECT_EQ(declarations.resourceCount(), 0u);
    EXPECT_EQ(declarations.resourceSetCount(), 0u);
    EXPECT_EQ(declarations.pipelineCount(), 0u);
    EXPECT_EQ(declarations.externalCompletionCount(), 0u);
    EXPECT_EQ(declarations.uploadBlobCount(), 0u);
    for(const Core::GpuUploadBlobId upload : uploads)
        EXPECT_FALSE(declarations.validUploadBlob(upload));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

