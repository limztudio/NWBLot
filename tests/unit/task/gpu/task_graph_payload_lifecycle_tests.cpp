// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"
#include "task_graph_lifecycle_test_utils.h"

#include <tests/unit/task/gpu/case_types.adhd.h>

#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_payload_lifecycle_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


struct ResetDiscardFailure{};
struct ResetOriginalFailure{};

struct ThrowingResetTask{
public:
    struct Payload{
        u32& destructionCount;
        u32& discardCount;
        b8 owned = true;


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


public:
    static void Discarded(Payload& payload){
        ++payload.discardCount;
        throw ResetDiscardFailure{};
    }
};


struct BorrowedPayloadLifetimeTask{
public:
    struct Payload{
        b8& ownerAlive;
        u32& destructionCount;
        u32& discardCount;
        u32& lateUseCount;
        b8 throwOnDiscard = false;
        b8 owned = true;


    public:
        Payload(b8& alive, u32& destructions, u32& discards, u32& lateUses, const b8 throws)noexcept
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


public:
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
    Core::GpuTaskGraph graph(testArena.arena);

    u32 discardedCount = 0u;
    Core::GpuTaskDesc desc;
    desc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphNonRecordable)).setMarkerLabel("Non-recordable Task");
    const auto taskResult = graph.addTask<PacketLifecycleTask>(desc, PacketLifecycleTask::Payload{ .discardedCount = &discardedCount });
    ASSERT_TRUE(taskResult);
    const Core::GpuTaskId task = *taskResult;
    ASSERT_TRUE(task.valid());
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_TRUE(declarations.taskAt(task.index).hasPayload);
        EXPECT_FALSE(declarations.taskAt(task.index).hasRecordPayload);
    }

    const Core::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Core::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u, };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    Core::Alloc::ScratchArena scratchArena(s_TaskGraphScratchArena);
    const Core::GpuTaskGraphCompiler compiler;

    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_FALSE(compiler.compile(declarations, topology, Core::GpuTaskGraphCompileOptions{}, scratchArena, analysis, assignments, compiledGraph));
        EXPECT_EQ(analysis.diagnostic().status, Core::GpuTaskGraphAnalysisStatus::MissingTaskRecordPayload);
        EXPECT_EQ(analysis.diagnostic().task, task);
        EXPECT_FALSE(analysis.valid());
        EXPECT_FALSE(assignments.valid());
        const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
        EXPECT_FALSE(compiledPlan.valid());
    }

    Core::GpuTaskGraphCompileOptions metadataOptions;
    metadataOptions.allowMetadataOnlyTasks = true;
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_TRUE(compiler.compile(declarations, topology, metadataOptions, scratchArena, analysis, assignments, compiledGraph));
        EXPECT_TRUE(analysis.valid());
        EXPECT_TRUE(assignments.valid());
        const Core::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);
        EXPECT_TRUE(compiledPlan.validFor(declarations));
    }

    graph.reset();
    EXPECT_EQ(discardedCount, 1u);
}

TEST(GpuTaskGraph, RegistersOnlyExactTypedPayloadLifecycleSignatures){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    u32 recordCount = 0u;
    u32 acceptedCount = 0u;
    u32 discardedCount = 0u;
    Core::GpuTaskDesc desc;
    desc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphMalformedLifecycleSignatures)).setMarkerLabel("Malformed Lifecycle Signatures");
    const auto taskResult = graph.addTask<MalformedLifecycleTask>(desc, MalformedLifecycleTask::Payload{ .recordCount = &recordCount, .acceptedCount = &acceptedCount, .discardedCount = &discardedCount, });
    ASSERT_TRUE(taskResult);
    const Core::GpuTaskId task = *taskResult;
    ASSERT_TRUE(task.valid());
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_TRUE(declarations.taskAt(task.index).hasPayload);
        EXPECT_FALSE(declarations.taskAt(task.index).hasRecordPayload);
    }

    const Core::GpuPhysicalQueueInfo queue = GraphicsQueue();
    const Core::GpuPhysicalQueueTopology topology{ .queues = &queue, .queueCount = 1u, };
    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, topology));
    graph.reset();

    EXPECT_EQ(recordCount, 0u);
    EXPECT_EQ(acceptedCount, 0u);
    EXPECT_EQ(discardedCount, 0u);
}


TEST(GpuTaskGraph, DiscardsTypedPayloadLifecycleOnlyOnceWhenGraphIsAbandoned){
    u32 acceptedCount = 0u;
    u32 discardedCount = 0u;

    {
        TestArena testArena;
        Core::GpuTaskGraph graph(testArena.arena);
        Core::GpuTaskDesc desc;
        desc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphLifecycleDiscarded)).setMarkerLabel("Discarded Lifecycle Task");
        const auto taskResult = graph.addTask<PacketLifecycleTask>(desc, PacketLifecycleTask::Payload{ .acceptedCount = &acceptedCount, .discardedCount = &discardedCount, });
        ASSERT_TRUE(taskResult);
        const Core::GpuTaskId task = *taskResult;
        ASSERT_TRUE(task.valid());

        graph.reset();
        graph.reset();

        EXPECT_EQ(acceptedCount, 0u);
        EXPECT_EQ(discardedCount, 1u);
    }

    {
        TestArena testArena;
        Core::GpuTaskGraph graph(testArena.arena);
        Core::GpuTaskDesc desc;
        desc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphLifecycleUnresolved)).setMarkerLabel("Unresolved Lifecycle Task");
        const auto taskResult = graph.addTask<PacketLifecycleTask>(desc, PacketLifecycleTask::Payload{ .acceptedCount = &acceptedCount, .discardedCount = &discardedCount, });
        ASSERT_TRUE(taskResult);
        const Core::GpuTaskId task = *taskResult;
        ASSERT_TRUE(task.valid());
    }

    EXPECT_EQ(acceptedCount, 0u);
    EXPECT_EQ(discardedCount, 2u);
}

TEST(GpuTaskGraph, ResetRetiresPayloadsAndPublishesReusableStateWhenDiscardObserverThrows){
    TestArena testArena;
    u32 destructionCount = 0u;
    u32 discardCount = 0u;
    Core::GpuTaskGraph graph(testArena.arena);
    Core::GpuTaskDesc desc;
    desc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResetThrowingObserver)).setMarkerLabel("Throwing Reset Observer");
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
    Core::GpuTaskGraph graph(testArena.arena);
    Core::GpuTaskDesc desc;
    desc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphResetActiveUnwind)).setMarkerLabel("Reset During Active Unwind");
    const auto task = graph.addTask<ThrowingResetTask>(desc, ThrowingResetTask::Payload(destructionCount, discardCount));
    ASSERT_TRUE(task);

    EXPECT_THROW(([&](){
        ScopeExit resetOnUnwind([&]()noexcept{ graph.reset(); });
        throw ResetOriginalFailure{};
    }()), ResetOriginalFailure
    );
    EXPECT_EQ(discardCount, 0u);
    EXPECT_EQ(destructionCount, 1u);

    const auto nextTask = graph.addTask(Core::GpuTaskDesc{}.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphAfterResetUnwind)).setMarkerLabel("After Reset Unwind"));
    ASSERT_TRUE(nextTask);
    graph.reset();
    EXPECT_EQ(discardCount, 0u);
    EXPECT_EQ(destructionCount, 1u);
}

TEST(GpuTaskGraph, RetiresOperationPayloadsBeforeBorrowedStateExpiresAndPreservesDeclarations){
    TestArena testArena;
    b8 ownerAlive = true;
    u32 destructionCount = 0u;
    u32 discardCount = 0u;
    u32 lateUseCount = 0u;
    Core::GpuTaskGraph graph(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    Core::GpuGraphSubmissionTransaction transaction(testArena.arena);
    Core::GpuTaskId task;
    u64 generation = 0u;
    u64 declarationRevision = 0u;
    usize payloadObjectSize = 0u;
    {
        Core::GpuGraphSubmissionTransaction::PayloadLifetimeScope payloadLifetime(transaction, graph, compiledGraph);
        Core::GpuTaskDesc desc;
        desc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphOperationPayload)).setMarkerLabel("Operation Payload");
        const auto added = graph.addTask<BorrowedPayloadLifetimeTask>(desc, BorrowedPayloadLifetimeTask::Payload(ownerAlive, destructionCount, discardCount, lateUseCount, false));
        ASSERT_TRUE(added);
        task = *added;
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        generation = declarations.generation();
        declarationRevision = declarations.declarationRevision();
        payloadObjectSize = declarations.taskAt(task.index).payloadObjectSize;
        EXPECT_TRUE(declarations.taskAt(task.index).hasPayload);
    }
    EXPECT_EQ(discardCount, 1u);
    EXPECT_EQ(destructionCount, 1u);
    ownerAlive = false;
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_EQ(declarations.generation(), generation);
        EXPECT_EQ(declarations.declarationRevision(), declarationRevision);
        EXPECT_EQ(declarations.taskCount(), 1u);
        EXPECT_TRUE(declarations.validTask(task));
        const Core::GpuTaskGraphTaskView view = declarations.taskAt(task.index);
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
    b8 ownerAlive = true;
    u32 destructionCount = 0u;
    u32 discardCount = 0u;
    u32 lateUseCount = 0u;
    Core::GpuTaskGraph graph(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    Core::GpuGraphSubmissionTransaction transaction(testArena.arena);
    EXPECT_THROW(([&](){
        Core::GpuGraphSubmissionTransaction::PayloadLifetimeScope payloadLifetime(transaction, graph, compiledGraph);
        Core::GpuTaskDesc firstDesc;
        firstDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphRetireThrowFirst)).setMarkerLabel("Retire Throw First");
        const auto first = graph.addTask<BorrowedPayloadLifetimeTask>(firstDesc, BorrowedPayloadLifetimeTask::Payload(ownerAlive, destructionCount, discardCount, lateUseCount, true));
        ASSERT_TRUE(first);
        Core::GpuTaskDesc secondDesc;
        secondDesc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphRetireThrowSecond)).setMarkerLabel("Retire Throw Second");
        const auto second = graph.addTask<BorrowedPayloadLifetimeTask>(secondDesc, BorrowedPayloadLifetimeTask::Payload(ownerAlive, destructionCount, discardCount, lateUseCount, false));
        ASSERT_TRUE(second);
    }()), ResetDiscardFailure
    );
    EXPECT_EQ(discardCount, 1u);
    EXPECT_EQ(destructionCount, 2u);
    ownerAlive = false;
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        EXPECT_EQ(declarations.taskCount(), 2u);
        EXPECT_FALSE(declarations.taskAt(0u).hasPayload);
        EXPECT_FALSE(declarations.taskAt(1u).hasPayload);
    }
    graph.reset();
    EXPECT_EQ(discardCount, 1u);
    EXPECT_EQ(destructionCount, 2u);
    EXPECT_EQ(lateUseCount, 0u);
}

TEST(GpuTaskGraph, OperationPayloadRetirementDuringUnwindPreservesOriginalFailureWithoutObservers){
    TestArena testArena;
    b8 ownerAlive = true;
    u32 destructionCount = 0u;
    u32 discardCount = 0u;
    u32 lateUseCount = 0u;
    Core::GpuTaskGraph graph(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    Core::GpuGraphSubmissionTransaction transaction(testArena.arena);
    EXPECT_THROW(([&](){
        Core::GpuGraphSubmissionTransaction::PayloadLifetimeScope payloadLifetime(transaction, graph, compiledGraph);
        Core::GpuTaskDesc desc;
        desc.setIdentity(Name(TaskGraphTestUtils::s_GpuCaseTaskGraphRetirementUnwind)).setMarkerLabel("Retirement Unwind");
        const auto task = graph.addTask<BorrowedPayloadLifetimeTask>(desc, BorrowedPayloadLifetimeTask::Payload(ownerAlive, destructionCount, discardCount, lateUseCount, true));
        ASSERT_TRUE(task);
        throw ResetOriginalFailure{};
    }()), ResetOriginalFailure
    );
    EXPECT_EQ(discardCount, 0u);
    EXPECT_EQ(destructionCount, 1u);
    ownerAlive = false;
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
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
    Core::GpuTaskGraph graph(testArena.arena);

    u32 discardedCount = 0u;
    Core::GpuTaskDesc invalidDesc;
    const auto task = graph.addTask<PacketLifecycleTask>(invalidDesc, PacketLifecycleTask::Payload{ .discardedCount = &discardedCount });

    EXPECT_FALSE(task.has_value());
    EXPECT_EQ(discardedCount, 1u);

    graph.reset();
    EXPECT_EQ(discardedCount, 1u);
}

TEST(GpuTaskGraph, ClearsPrimitiveAcceptedTokensWhenDeclarationFails){
    TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    Core::GpuTaskDesc desc;
    Core::QueueSubmissionToken token{ .value = 1u, .physicalQueueIndex = 0u, .deviceGeneration = 1u, .queue = Core::CommandQueue::Graphics, };

    Core::GpuCopyBufferTaskDesc copyBuffer;
    copyBuffer.acceptedToken = &token;
    EXPECT_FALSE(graph.addCopyBufferTask(desc, copyBuffer).has_value());
    EXPECT_FALSE(token.valid());

    token = Core::QueueSubmissionToken{ .value = 1u, .physicalQueueIndex = 0u, .deviceGeneration = 1u, .queue = Core::CommandQueue::Graphics, };
    Core::GpuCopyTextureTaskDesc copyTexture;
    copyTexture.acceptedToken = &token;
    EXPECT_FALSE(graph.addCopyTextureTask(desc, copyTexture).has_value());
    EXPECT_FALSE(token.valid());

    token = Core::QueueSubmissionToken{ .value = 1u, .physicalQueueIndex = 0u, .deviceGeneration = 1u, .queue = Core::CommandQueue::Graphics, };
    Core::GpuResolveTextureTaskDesc resolveTexture;
    resolveTexture.acceptedToken = &token;
    EXPECT_FALSE(graph.addResolveTextureTask(desc, resolveTexture).has_value());
    EXPECT_FALSE(token.valid());

    token = Core::QueueSubmissionToken{ .value = 1u, .physicalQueueIndex = 0u, .deviceGeneration = 1u, .queue = Core::CommandQueue::Graphics, };
    Core::GpuClearBufferTaskDesc clearBuffer;
    clearBuffer.acceptedToken = &token;
    EXPECT_FALSE(graph.addClearBufferTask(desc, clearBuffer).has_value());
    EXPECT_FALSE(token.valid());

    token = Core::QueueSubmissionToken{ .value = 1u, .physicalQueueIndex = 0u, .deviceGeneration = 1u, .queue = Core::CommandQueue::Graphics, };
    Core::GpuClearTextureTaskDesc clearTexture;
    clearTexture.acceptedToken = &token;
    EXPECT_FALSE(graph.addClearTextureTask(desc, clearTexture).has_value());
    EXPECT_FALSE(token.valid());

    token = Core::QueueSubmissionToken{ .value = 1u, .physicalQueueIndex = 0u, .deviceGeneration = 1u, .queue = Core::CommandQueue::Graphics, };
    Core::GpuClearTextureRectUIntTaskDesc clearTextureRect;
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

