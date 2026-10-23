// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"
#include "task_graph_lifecycle_test_utils.h"

#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_payload_lifecycle_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


struct ResetDiscardFailure{};
struct ResetOriginalFailure{};

struct ThrowingResetTask{
    struct Payload{
        u32& destructionCount;
        u32& discardCount;
        bool owned = true;


    public:
        Payload(u32& destructions, u32& discards)noexcept
            : destructionCount(destructions)
            , discardCount(discards)
        {}
        Payload(Payload&& other)noexcept
            : destructionCount(other.destructionCount)
            , discardCount(other.discardCount)
            , owned(other.owned)
        { other.owned = false; }
        ~Payload()noexcept{
            if(owned)
                ++destructionCount;
        }
    };

    static void Discarded(Payload& payload){
        ++payload.discardCount;
        throw ResetDiscardFailure{};
    }
};


struct BorrowedPayloadLifetimeTask{
    struct Payload{
        bool& ownerAlive;
        u32& destructionCount;
        u32& discardCount;
        u32& lateUseCount;
        bool throwOnDiscard = false;
        bool owned = true;


    public:
        Payload(bool& alive, u32& destructions, u32& discards, u32& lateUses, const bool throws)noexcept
            : ownerAlive(alive)
            , destructionCount(destructions)
            , discardCount(discards)
            , lateUseCount(lateUses)
            , throwOnDiscard(throws)
        {}
        Payload(Payload&& other)noexcept
            : ownerAlive(other.ownerAlive)
            , destructionCount(other.destructionCount)
            , discardCount(other.discardCount)
            , lateUseCount(other.lateUseCount)
            , throwOnDiscard(other.throwOnDiscard)
            , owned(other.owned)
        { other.owned = false; }
        ~Payload()noexcept{
            if(!owned)
                return;
            if(!ownerAlive)
                ++lateUseCount;
            ++destructionCount;
        }
    };

    static void Discarded(Payload& payload){
        if(!payload.ownerAlive)
            ++payload.lateUseCount;
        ++payload.discardCount;
        if(payload.throwOnDiscard)
            throw ResetDiscardFailure{};
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTaskGraph, RejectsNonRecordableTasksDuringNativeCompilation){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    u32 discardedCount = 0u;
    Graphics::GpuTaskDesc desc;
    desc.setIdentity(Name("tests/task_graph/non_recordable")).setMarkerLabel("Non-recordable Task");
    const auto taskResult = graph.addTask<PacketLifecycleTask>(desc, PacketLifecycleTask::Payload{ .discardedCount = &discardedCount });
    ASSERT_TRUE(taskResult);
    const Graphics::GpuTaskId task = *taskResult;
    ASSERT_TRUE(task.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_TRUE(declarations.taskAt(task.index).hasPayload);
        EXPECT_FALSE(declarations.taskAt(task.index).hasRecordPayload);
    }

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u, };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    const Graphics::GpuTaskGraphCompiler compiler;

    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_FALSE(compiler.compile(analysis, assignments, compiledGraph, scratchArena, declarations, topology));
        EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::MissingTaskRecordPayload);
        EXPECT_EQ(analysis.diagnostic().task, task);
        EXPECT_FALSE(analysis.valid());
        EXPECT_FALSE(assignments.valid());
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
        EXPECT_FALSE(compiledPlan.valid());
    }

    Graphics::GpuTaskGraphCompileOptions metadataOptions;
    metadataOptions.allowMetadataOnlyTasks = true;
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_TRUE(compiler.compile(analysis, assignments, compiledGraph, scratchArena, declarations, topology, metadataOptions));
        EXPECT_TRUE(analysis.valid());
        EXPECT_TRUE(assignments.valid());
        const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
        EXPECT_TRUE(compiledPlan.validFor(declarations));
    }

    graph.reset();
    EXPECT_EQ(discardedCount, 1u);
}

TEST(GpuTaskGraph, RegistersOnlyExactTypedPayloadLifecycleSignatures){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    u32 recordCount = 0u;
    u32 acceptedCount = 0u;
    u32 discardedCount = 0u;
    Graphics::GpuTaskDesc desc;
    desc.setIdentity(Name("tests/task_graph/malformed_lifecycle_signatures")).setMarkerLabel("Malformed Lifecycle Signatures");
    const auto taskResult = graph.addTask<MalformedLifecycleTask>(desc, MalformedLifecycleTask::Payload{ .recordCount = &recordCount, .acceptedCount = &acceptedCount, .discardedCount = &discardedCount, });
    ASSERT_TRUE(taskResult);
    const Graphics::GpuTaskId task = *taskResult;
    ASSERT_TRUE(task.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_TRUE(declarations.taskAt(task.index).hasPayload);
        EXPECT_FALSE(declarations.taskAt(task.index).hasRecordPayload);
    }

    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u, };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    graph.reset();

    EXPECT_EQ(recordCount, 0u);
    EXPECT_EQ(acceptedCount, 0u);
    EXPECT_EQ(discardedCount, 0u);
}


TEST(GpuTaskGraph, RejectsMetadataResourceUsesBeforeNativeRecording){
    TestArena testArena;
    const Graphics::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u, };
    const Graphics::GpuTaskGraphCompiler compiler;

    const auto expectMissingTypedImport = [&](const Name& identity, const AStringView label, const Graphics::GpuGraphResourceType::Enum type, const Graphics::ResourceStates::Mask requiredState){
        Graphics::GpuTaskGraph graph(testArena.arena);
        const auto resourceResult = graph.importResource(Graphics::GpuGraphResourceDesc{}.setIdentity(identity).setMarkerLabel(label).setType(type).setInitialState(Graphics::ResourceStates::Common));
        ASSERT_TRUE(resourceResult);
        const Graphics::GpuGraphResourceId resource = *resourceResult;
        ASSERT_TRUE(resource.valid());

        const Graphics::GpuTaskResourceUse use{ .resource = resource, .range = {}, .requiredState = requiredState, .access = Graphics::GpuTaskResourceAccess::Read, };
        Graphics::GpuTaskDesc desc;
        desc.setIdentity(identity).setMarkerLabel(label).setResourceUses(&use, 1u);
        u32 recordCount = 0u;
        const auto taskResult = graph.addTask<NativeRecordProbeTask>(desc, NativeRecordProbeTask::Payload{ .recordCount = &recordCount });
        ASSERT_TRUE(taskResult);
        const Graphics::GpuTaskId task = *taskResult;
        ASSERT_TRUE(task.valid());

        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
        Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_TRUE(declarations.taskAt(task.index).hasPayload);
        EXPECT_TRUE(declarations.taskAt(task.index).hasRecordPayload);
        EXPECT_FALSE(compiler.compile(analysis, assignments, compiledGraph, scratchArena, declarations, topology));
        EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::InvalidResourceUse);
        EXPECT_EQ(analysis.diagnostic().task, task);
        EXPECT_EQ(analysis.diagnostic().resource, resource);
        EXPECT_FALSE(analysis.valid());
        EXPECT_FALSE(assignments.valid());
        {
            const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
            EXPECT_FALSE(compiledPlan.valid());
        }
        EXPECT_EQ(recordCount, 0u);

        Graphics::GpuTaskGraphCompileOptions metadataOptions;
        metadataOptions.allowMetadataOnlyTasks = true;
        EXPECT_TRUE(compiler.compile(analysis, assignments, compiledGraph, scratchArena, declarations, topology, metadataOptions));
        EXPECT_TRUE(analysis.valid());
        EXPECT_TRUE(assignments.valid());
        {
            const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
            EXPECT_TRUE(compiledPlan.validFor(declarations));
        }
        EXPECT_EQ(recordCount, 0u);
    };

    expectMissingTypedImport(Name("tests/task_graph/native_metadata_texture"), "Native Metadata Texture", Graphics::GpuGraphResourceType::Texture, Graphics::ResourceStates::ShaderResource);
    expectMissingTypedImport(Name("tests/task_graph/native_metadata_buffer"), "Native Metadata Buffer", Graphics::GpuGraphResourceType::Buffer, Graphics::ResourceStates::ShaderResource);
    expectMissingTypedImport(Name("tests/task_graph/native_metadata_accel_struct"), "Native Metadata Accel Struct", Graphics::GpuGraphResourceType::AccelStruct, Graphics::ResourceStates::AccelStructRead);

    {
        Graphics::GpuTaskGraph graph(testArena.arena);
        const auto unusedTextureResult = AddTextureMetadata(graph, Name("tests/task_graph/native_unused_metadata_texture"), "Native Unused Metadata Texture");
        ASSERT_TRUE(unusedTextureResult);
        const Graphics::GpuGraphResourceId unusedTexture = *unusedTextureResult;
        const auto hazardDomainResult = AddHazardDomain(graph, Name("tests/task_graph/native_hazard_domain"), "Native Hazard Domain");
        ASSERT_TRUE(hazardDomainResult);
        const Graphics::GpuGraphResourceId hazardDomain = *hazardDomainResult;
        ASSERT_TRUE(unusedTexture.valid());
        ASSERT_TRUE(hazardDomain.valid());

        const Graphics::GpuTaskResourceUse use{ .resource = hazardDomain, .range = {}, .requiredState = Graphics::ResourceStates::Common, .access = Graphics::GpuTaskResourceAccess::Write, };
        Graphics::GpuTaskDesc desc;
        desc.setIdentity(Name("tests/task_graph/native_hazard_domain_task")).setMarkerLabel("Native Hazard Domain Task").setResourceUses(&use, 1u);
        u32 recordCount = 0u;
        const auto taskResult = graph.addTask<NativeRecordProbeTask>(desc, NativeRecordProbeTask::Payload{ .recordCount = &recordCount });
        ASSERT_TRUE(taskResult);
        const Graphics::GpuTaskId task = *taskResult;
        ASSERT_TRUE(task.valid());

        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
        Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
        {
            const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
            EXPECT_TRUE(compiler.compile(analysis, assignments, compiledGraph, scratchArena, declarations, topology));
            EXPECT_TRUE(analysis.valid());
            EXPECT_TRUE(assignments.valid());
            const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
            EXPECT_TRUE(compiledPlan.validFor(declarations));
            EXPECT_EQ(recordCount, 0u);
        }

        const Graphics::GpuTaskResourceUse invalidTypedUse{ .resource = unusedTexture, .range = {}, .requiredState = Graphics::ResourceStates::Unknown, .access = Graphics::GpuTaskResourceAccess::Read, };
        Graphics::GpuTaskDesc invalidTypedDesc;
        invalidTypedDesc.setIdentity(Name("tests/task_graph/native_unknown_typed_state_task")).setMarkerLabel("Native Unknown Typed State Task").setResourceUses(&invalidTypedUse, 1u);
        const auto invalidTypedTaskResult = graph.addTask<NativeRecordProbeTask>(invalidTypedDesc, NativeRecordProbeTask::Payload{ .recordCount = &recordCount });
        ASSERT_TRUE(invalidTypedTaskResult);
        const Graphics::GpuTaskId invalidTypedTask = *invalidTypedTaskResult;
        ASSERT_TRUE(invalidTypedTask.valid());

        Graphics::GpuTaskGraphCompileOptions metadataOptions;
        metadataOptions.allowMetadataOnlyTasks = true;
        {
            const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
            EXPECT_FALSE(compiler.compile(analysis, assignments, compiledGraph, scratchArena, declarations, topology, metadataOptions));
            EXPECT_EQ(analysis.diagnostic().status, Graphics::GpuTaskGraphAnalysisStatus::InvalidResourceUse);
            EXPECT_EQ(analysis.diagnostic().task, invalidTypedTask);
            EXPECT_EQ(analysis.diagnostic().resource, unusedTexture);
            EXPECT_EQ(recordCount, 0u);
        }
    }
}

TEST(GpuTaskGraph, DiscardsTypedPayloadLifecycleOnlyOnceWhenGraphIsAbandoned){
    u32 acceptedCount = 0u;
    u32 discardedCount = 0u;

    {
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        Graphics::GpuTaskDesc desc;
        desc.setIdentity(Name("tests/task_graph/lifecycle_discarded")).setMarkerLabel("Discarded Lifecycle Task");
        const auto taskResult = graph.addTask<PacketLifecycleTask>(desc, PacketLifecycleTask::Payload{ .acceptedCount = &acceptedCount, .discardedCount = &discardedCount, });
        ASSERT_TRUE(taskResult);
        const Graphics::GpuTaskId task = *taskResult;
        ASSERT_TRUE(task.valid());

        graph.reset();
        graph.reset();

        EXPECT_EQ(acceptedCount, 0u);
        EXPECT_EQ(discardedCount, 1u);
    }

    {
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        Graphics::GpuTaskDesc desc;
        desc.setIdentity(Name("tests/task_graph/lifecycle_unresolved")).setMarkerLabel("Unresolved Lifecycle Task");
        const auto taskResult = graph.addTask<PacketLifecycleTask>(desc, PacketLifecycleTask::Payload{ .acceptedCount = &acceptedCount, .discardedCount = &discardedCount, });
        ASSERT_TRUE(taskResult);
        const Graphics::GpuTaskId task = *taskResult;
        ASSERT_TRUE(task.valid());
    }

    EXPECT_EQ(acceptedCount, 0u);
    EXPECT_EQ(discardedCount, s_ExpectedDualCount);
}

TEST(GpuTaskGraph, ResetRetiresPayloadsAndPublishesReusableStateWhenDiscardObserverThrows){
    TestArena testArena;
    u32 destructionCount = 0u;
    u32 discardCount = 0u;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuTaskDesc desc;
    desc.setIdentity(Name("tests/task_graph/reset_throwing_observer")).setMarkerLabel("Throwing Reset Observer");
    const auto task = graph.addTask<ThrowingResetTask>(desc, ThrowingResetTask::Payload(destructionCount, discardCount));
    ASSERT_TRUE(task);

    EXPECT_THROW(graph.reset(), ResetDiscardFailure);
    EXPECT_EQ(discardCount, 1u);
    EXPECT_EQ(destructionCount, 1u);
    EXPECT_TRUE(graph.tryReset());
    EXPECT_EQ(discardCount, 1u);
    EXPECT_EQ(destructionCount, 1u);
}

TEST(GpuTaskGraph, ResetDuringUnwindPreservesOriginalFailureWithoutCallingDiscardObserver){
    TestArena testArena;
    u32 destructionCount = 0u;
    u32 discardCount = 0u;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuTaskDesc desc;
    desc.setIdentity(Name("tests/task_graph/reset_active_unwind")).setMarkerLabel("Reset During Active Unwind");
    const auto task = graph.addTask<ThrowingResetTask>(desc, ThrowingResetTask::Payload(destructionCount, discardCount));
    ASSERT_TRUE(task);

    EXPECT_THROW(([&](){
        ScopeExit resetOnUnwind([&]()noexcept{ graph.reset(); });
        throw ResetOriginalFailure{};
    }()), ResetOriginalFailure
    );
    EXPECT_EQ(discardCount, 0u);
    EXPECT_EQ(destructionCount, 1u);

    const auto nextTask = graph.addTask(Graphics::GpuTaskDesc{}.setIdentity(Name("tests/task_graph/after_reset_unwind")).setMarkerLabel("After Reset Unwind"));
    ASSERT_TRUE(nextTask);
    graph.reset();
    EXPECT_EQ(discardCount, 0u);
    EXPECT_EQ(destructionCount, 1u);
}

TEST(GpuTaskGraph, RetiresOperationPayloadsBeforeBorrowedStateExpiresAndPreservesDeclarations){
    TestArena testArena;
    bool ownerAlive = true;
    u32 destructionCount = 0u;
    u32 discardCount = 0u;
    u32 lateUseCount = 0u;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    Graphics::GpuGraphSubmissionTransaction transaction(testArena.arena);
    Graphics::GpuTaskId task;
    u64 generation = 0u;
    u64 declarationRevision = 0u;
    usize payloadObjectSize = 0u;
    {
        Graphics::GpuGraphSubmissionTransaction::PayloadLifetimeScope payloadLifetime(transaction, graph, compiledGraph);
        Graphics::GpuTaskDesc desc;
        desc.setIdentity(Name("tests/task_graph/operation_payload")).setMarkerLabel("Operation Payload");
        const auto added = graph.addTask<BorrowedPayloadLifetimeTask>(desc, BorrowedPayloadLifetimeTask::Payload(ownerAlive, destructionCount, discardCount, lateUseCount, false));
        ASSERT_TRUE(added);
        task = *added;
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        generation = declarations.generation();
        declarationRevision = declarations.declarationRevision();
        payloadObjectSize = declarations.taskAt(task.index).payloadObjectSize;
        EXPECT_TRUE(declarations.taskAt(task.index).hasPayload);
    }
    EXPECT_EQ(discardCount, 1u);
    EXPECT_EQ(destructionCount, 1u);
    ownerAlive = false;
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_EQ(declarations.generation(), generation);
        EXPECT_EQ(declarations.declarationRevision(), declarationRevision);
        EXPECT_EQ(declarations.taskCount(), 1u);
        EXPECT_TRUE(declarations.validTask(task));
        const Graphics::GpuTaskGraphTaskView view = declarations.taskAt(task.index);
        EXPECT_EQ(view.identity, Name("tests/task_graph/operation_payload"));
        EXPECT_EQ(view.markerLabel, AStringView("Operation Payload"));
        EXPECT_EQ(view.payloadObjectSize, payloadObjectSize);
        EXPECT_FALSE(view.hasPayload);
    }
    graph.reset();
    EXPECT_EQ(discardCount, 1u);
    EXPECT_EQ(destructionCount, 1u);
    EXPECT_EQ(lateUseCount, 0u);
}

TEST(GpuTaskGraph, OperationPayloadRetirementPreservesObserverFailureAndFinishesAllDestruction){
    TestArena testArena;
    bool ownerAlive = true;
    u32 destructionCount = 0u;
    u32 discardCount = 0u;
    u32 lateUseCount = 0u;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    Graphics::GpuGraphSubmissionTransaction transaction(testArena.arena);
    EXPECT_THROW(([&](){
        Graphics::GpuGraphSubmissionTransaction::PayloadLifetimeScope payloadLifetime(transaction, graph, compiledGraph);
        Graphics::GpuTaskDesc firstDesc;
        firstDesc.setIdentity(Name("tests/task_graph/retire_throw_first")).setMarkerLabel("Retire Throw First");
        const auto first = graph.addTask<BorrowedPayloadLifetimeTask>(firstDesc, BorrowedPayloadLifetimeTask::Payload(ownerAlive, destructionCount, discardCount, lateUseCount, true));
        ASSERT_TRUE(first);
        Graphics::GpuTaskDesc secondDesc;
        secondDesc.setIdentity(Name("tests/task_graph/retire_throw_second")).setMarkerLabel("Retire Throw Second");
        const auto second = graph.addTask<BorrowedPayloadLifetimeTask>(secondDesc, BorrowedPayloadLifetimeTask::Payload(ownerAlive, destructionCount, discardCount, lateUseCount, false));
        ASSERT_TRUE(second);
    }()), ResetDiscardFailure
    );
    EXPECT_EQ(discardCount, 1u);
    EXPECT_EQ(destructionCount, s_ExpectedDualCount);
    ownerAlive = false;
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_EQ(declarations.taskCount(), s_ExpectedDualCount);
        EXPECT_FALSE(declarations.taskAt(0u).hasPayload);
        EXPECT_FALSE(declarations.taskAt(1u).hasPayload);
    }
    graph.reset();
    EXPECT_EQ(discardCount, 1u);
    EXPECT_EQ(destructionCount, s_ExpectedDualCount);
    EXPECT_EQ(lateUseCount, 0u);
}

TEST(GpuTaskGraph, OperationPayloadRetirementDuringUnwindPreservesOriginalFailureWithoutObservers){
    TestArena testArena;
    bool ownerAlive = true;
    u32 destructionCount = 0u;
    u32 discardCount = 0u;
    u32 lateUseCount = 0u;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    Graphics::GpuGraphSubmissionTransaction transaction(testArena.arena);
    EXPECT_THROW(([&](){
        Graphics::GpuGraphSubmissionTransaction::PayloadLifetimeScope payloadLifetime(transaction, graph, compiledGraph);
        Graphics::GpuTaskDesc desc;
        desc.setIdentity(Name("tests/task_graph/retirement_unwind")).setMarkerLabel("Retirement Unwind");
        const auto task = graph.addTask<BorrowedPayloadLifetimeTask>(desc, BorrowedPayloadLifetimeTask::Payload(ownerAlive, destructionCount, discardCount, lateUseCount, true));
        ASSERT_TRUE(task);
        throw ResetOriginalFailure{};
    }()), ResetOriginalFailure
    );
    EXPECT_EQ(discardCount, 0u);
    EXPECT_EQ(destructionCount, 1u);
    ownerAlive = false;
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_EQ(declarations.taskCount(), 1u);
        EXPECT_FALSE(declarations.taskAt(0u).hasPayload);
    }
    graph.reset();
    EXPECT_EQ(discardCount, 0u);
    EXPECT_EQ(destructionCount, 1u);
    EXPECT_EQ(lateUseCount, 0u);
}

TEST(GpuTaskGraph, DiscardsTypedPayloadWhenDeclarationIsRejected){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    u32 discardedCount = 0u;
    Graphics::GpuTaskDesc invalidDesc;
    const auto task = graph.addTask<PacketLifecycleTask>(invalidDesc, PacketLifecycleTask::Payload{ .discardedCount = &discardedCount });

    EXPECT_FALSE(task.has_value());
    EXPECT_EQ(discardedCount, 1u);

    graph.reset();
    EXPECT_EQ(discardedCount, 1u);
}

TEST(GpuTaskGraph, ClearsPrimitiveAcceptedTokensWhenDeclarationFails){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuTaskDesc desc;
    Graphics::QueueSubmissionToken token{ .value = 1u, .physicalQueueIndex = 0u, .deviceGeneration = 1u, .queue = Graphics::CommandQueue::Graphics, };

    Graphics::GpuCopyBufferTaskDesc copyBuffer;
    copyBuffer.acceptedToken = &token;
    EXPECT_FALSE(graph.addCopyBufferTask(desc, copyBuffer).has_value());
    EXPECT_FALSE(token.valid());

    token = Graphics::QueueSubmissionToken{ .value = 1u, .physicalQueueIndex = 0u, .deviceGeneration = 1u, .queue = Graphics::CommandQueue::Graphics, };
    Graphics::GpuCopyTextureTaskDesc copyTexture;
    copyTexture.acceptedToken = &token;
    EXPECT_FALSE(graph.addCopyTextureTask(desc, copyTexture).has_value());
    EXPECT_FALSE(token.valid());

    token = Graphics::QueueSubmissionToken{ .value = 1u, .physicalQueueIndex = 0u, .deviceGeneration = 1u, .queue = Graphics::CommandQueue::Graphics, };
    Graphics::GpuResolveTextureTaskDesc resolveTexture;
    resolveTexture.acceptedToken = &token;
    EXPECT_FALSE(graph.addResolveTextureTask(desc, resolveTexture).has_value());
    EXPECT_FALSE(token.valid());

    token = Graphics::QueueSubmissionToken{ .value = 1u, .physicalQueueIndex = 0u, .deviceGeneration = 1u, .queue = Graphics::CommandQueue::Graphics, };
    Graphics::GpuClearBufferTaskDesc clearBuffer;
    clearBuffer.acceptedToken = &token;
    EXPECT_FALSE(graph.addClearBufferTask(desc, clearBuffer).has_value());
    EXPECT_FALSE(token.valid());

    token = Graphics::QueueSubmissionToken{ .value = 1u, .physicalQueueIndex = 0u, .deviceGeneration = 1u, .queue = Graphics::CommandQueue::Graphics, };
    Graphics::GpuClearTextureTaskDesc clearTexture;
    clearTexture.acceptedToken = &token;
    EXPECT_FALSE(graph.addClearTextureTask(desc, clearTexture).has_value());
    EXPECT_FALSE(token.valid());

    token = Graphics::QueueSubmissionToken{ .value = 1u, .physicalQueueIndex = 0u, .deviceGeneration = 1u, .queue = Graphics::CommandQueue::Graphics, };
    Graphics::GpuClearTextureRectUIntTaskDesc clearTextureRect;
    clearTextureRect.acceptedToken = &token;
    EXPECT_FALSE(graph.addClearTextureRectUIntTask(desc, clearTextureRect).has_value());
    EXPECT_FALSE(token.valid());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

