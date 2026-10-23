// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_prelude_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;

[[nodiscard]] static Graphics::GpuTaskSchedulingHint PreludeScheduling()noexcept{
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    return scheduling;
}

[[nodiscard]] static Graphics::GpuTaskCommandRequirements PrimaryGraphicsRequirements()noexcept{
    Graphics::GpuTaskCommandRequirements commands;
    commands.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;
    commands.requiresPrimaryGraphicsQueue = true;
    return commands;
}

[[nodiscard]] static Expected<Graphics::GpuTaskId> AddPrelude(Graphics::GpuTaskGraph& graph){
    return AddTaskWithCommands(graph, Name("tests/task_graph/prelude"), "Normal Execution Prelude", PrimaryGraphicsRequirements(), PreludeScheduling(), { .policy = Graphics::GpuTaskTimingPolicy::PacketOnly });
}

[[nodiscard]] static bool HasPacketDependency(const Graphics::GpuCompiledPacketView& consumer, const Graphics::GpuSubmissionPacketId producer)noexcept{
    if(!consumer.valid())
        return false;
    for(u32 index = 0u; index < consumer.plan->dependencyCount; ++index){
        if(consumer.dependencies[index].producer == producer)
            return true;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GpuTaskGraph, NormalExecutionPreludeGatesIndependentBranchesWithoutOrderingThem){
    Core::Alloc::ScratchArena operationScratchArena(::NWB::Tests::TaskGraphTestUtils::s_TaskGraphScratchArena);

    TaskGraphTestUtils::TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedTransferQueue() };
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues) };

    const auto preludeResult = AddPrelude(graph);
    ASSERT_TRUE(preludeResult);
    const Graphics::GpuTaskId prelude = *preludeResult;
    ASSERT_TRUE(prelude.valid());
    ASSERT_TRUE(graph.setNormalExecutionPrelude(prelude));
    const auto sceneRootResult = AddTaskWithCommands(graph, Name("tests/task_graph/prelude_scene_root"), "Scene Root", { .requiredCapabilities = Graphics::GpuQueueCapability::Graphics });
    ASSERT_TRUE(sceneRootResult);
    const Graphics::GpuTaskId sceneRoot = *sceneRootResult;
    const auto uiRootResult = AddTaskWithCommands(graph, Name("tests/task_graph/prelude_ui_root"), "UI Upload Root", { .requiredCapabilities = Graphics::GpuQueueCapability::Transfer, .externalQueue = queues[1u].id });
    ASSERT_TRUE(uiRootResult);
    const Graphics::GpuTaskId uiRoot = *uiRootResult;
    ASSERT_TRUE(sceneRoot.valid());
    ASSERT_TRUE(uiRoot.valid());
    const auto sceneTailResult = AddTaskWithCommands(graph, Name("tests/task_graph/prelude_scene_tail"), "Scene Tail", { .requiredCapabilities = Graphics::GpuQueueCapability::Graphics }, {}, {}, &sceneRoot, 1u);
    ASSERT_TRUE(sceneTailResult);
    const Graphics::GpuTaskId sceneTail = *sceneTailResult;
    const auto uiTailResult = AddTaskWithCommands(graph, Name("tests/task_graph/prelude_ui_tail"), "UI Tail", { .requiredCapabilities = Graphics::GpuQueueCapability::Transfer, .externalQueue = queues[1u].id }, {}, {}, &uiRoot, 1u);
    ASSERT_TRUE(uiTailResult);
    const Graphics::GpuTaskId uiTail = *uiTailResult;
    ASSERT_TRUE(sceneTail.valid());
    ASSERT_TRUE(uiTail.valid());
    const Graphics::GpuTaskId terminalDependencies[] = { sceneTail, uiTail };
    const auto terminalResult = AddTaskWithCommands(graph, Name("tests/task_graph/prelude_terminal"), "Presentation Terminal", PrimaryGraphicsRequirements(), {}, {}, terminalDependencies, LengthOf(terminalDependencies));
    ASSERT_TRUE(terminalResult);
    const Graphics::GpuTaskId terminal = *terminalResult;
    ASSERT_TRUE(terminal.valid());
    Graphics::GpuTaskSchedulingHint recoveryScheduling = PreludeScheduling();
    recoveryScheduling.joinsAcceptedQueueFrontier = true;
    recoveryScheduling.isRecoverySubmission = true;
    const auto recoveryResult = AddTaskWithCommands(graph, Name("tests/task_graph/prelude_recovery"), "Recovery Frontier", PrimaryGraphicsRequirements(), recoveryScheduling);
    ASSERT_TRUE(recoveryResult);
    const Graphics::GpuTaskId recovery = *recoveryResult;
    ASSERT_TRUE(recovery.valid());

    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        const Graphics::GpuTaskGraphTaskView scene = declarations.taskAt(sceneRoot.index);
        const Graphics::GpuTaskGraphTaskView ui = declarations.taskAt(uiRoot.index);
        const Graphics::GpuTaskGraphTaskView sceneDependent = declarations.taskAt(sceneTail.index);
        const Graphics::GpuTaskGraphTaskView uiDependent = declarations.taskAt(uiTail.index);
        const Graphics::GpuTaskGraphTaskView lateRecovery = declarations.taskAt(recovery.index);
        ASSERT_EQ(scene.dependencyCount, 1u);
        ASSERT_EQ(ui.dependencyCount, 1u);
        ASSERT_EQ(sceneDependent.dependencyCount, 1u);
        ASSERT_EQ(uiDependent.dependencyCount, 1u);
        EXPECT_EQ(scene.dependencies[0u], prelude);
        EXPECT_EQ(ui.dependencies[0u], prelude);
        EXPECT_EQ(sceneDependent.dependencies[0u], sceneRoot);
        EXPECT_EQ(uiDependent.dependencies[0u], uiRoot);
        EXPECT_EQ(lateRecovery.dependencyCount, 0u);
    }

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    Graphics::GpuTaskGraphCompileOptions options;
    options.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    options.packetTimingEnvelope = { .firstTask = prelude, .lastTask = terminal };
    ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, topology, options));
    const Graphics::GpuCompiledGraph::ReadView plan(compiledGraph);
    ASSERT_TRUE(plan.valid());
    const Graphics::GpuSubmissionPacketId preludePacket = plan.packetForTask(prelude);
    const Graphics::GpuSubmissionPacketId scenePacket = plan.packetForTask(sceneRoot);
    const Graphics::GpuSubmissionPacketId uiPacket = plan.packetForTask(uiRoot);
    const Graphics::GpuSubmissionPacketId recoveryPacket = plan.packetForTask(recovery);
    ASSERT_TRUE(preludePacket.valid());
    ASSERT_TRUE(scenePacket.valid());
    ASSERT_TRUE(uiPacket.valid());
    ASSERT_TRUE(recoveryPacket.valid());
    EXPECT_EQ(preludePacket.index, 0u);
    EXPECT_LT(preludePacket.index, scenePacket.index);
    EXPECT_LT(preludePacket.index, uiPacket.index);
    EXPECT_EQ(plan.packet(preludePacket).plan->queue, queues[0u].id);
    EXPECT_EQ(plan.packet(uiPacket).plan->queue, queues[1u].id);
    EXPECT_TRUE(HasPacketDependency(plan.packet(scenePacket), preludePacket));
    EXPECT_TRUE(HasPacketDependency(plan.packet(uiPacket), preludePacket));
    EXPECT_FALSE(HasPacketDependency(plan.packet(scenePacket), uiPacket));
    EXPECT_FALSE(HasPacketDependency(plan.packet(uiPacket), scenePacket));
    const Graphics::GpuCompiledPacketView recoveryPlan = plan.packet(recoveryPacket);
    ASSERT_TRUE(recoveryPlan.valid());
    EXPECT_EQ(recoveryPlan.plan->dependencyCount, 0u);
    EXPECT_TRUE(recoveryPlan.plan->joinsAcceptedQueueFrontier);
}

TEST(GpuTaskGraph, NormalExecutionPreludeRejectsInvalidRegistrationAndClearsOnReset){
    TaskGraphTestUtils::TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuTaskGraph otherGraph(testArena.arena);
    const auto foreignPreludeResult = AddPrelude(otherGraph);
    ASSERT_TRUE(foreignPreludeResult);
    const Graphics::GpuTaskId foreignPrelude = *foreignPreludeResult;
    ASSERT_TRUE(foreignPrelude.valid());
    EXPECT_FALSE(graph.setNormalExecutionPrelude({}));
    EXPECT_FALSE(graph.setNormalExecutionPrelude(foreignPrelude));

    const auto preludeResult = AddPrelude(graph);
    ASSERT_TRUE(preludeResult);
    const Graphics::GpuTaskId prelude = *preludeResult;
    ASSERT_TRUE(prelude.valid());
    ASSERT_TRUE(graph.setNormalExecutionPrelude(prelude));
    EXPECT_FALSE(graph.setNormalExecutionPrelude(prelude));
    const auto rootResult = AddTask(graph, Name("tests/task_graph/prelude_root_before_reset"), "Root Before Reset");
    ASSERT_TRUE(rootResult);
    const Graphics::GpuTaskId root = *rootResult;
    ASSERT_TRUE(root.valid());
    graph.reset();
    EXPECT_FALSE(graph.setNormalExecutionPrelude(prelude));
    const auto unguardedRootResult = AddTask(graph, Name("tests/task_graph/prelude_root_after_reset"), "Root After Reset");
    ASSERT_TRUE(unguardedRootResult);
    const Graphics::GpuTaskId unguardedRoot = *unguardedRootResult;
    ASSERT_TRUE(unguardedRoot.valid());
    const auto latePreludeResult = AddPrelude(graph);
    ASSERT_TRUE(latePreludeResult);
    const Graphics::GpuTaskId latePrelude = *latePreludeResult;
    ASSERT_TRUE(latePrelude.valid());
    EXPECT_FALSE(graph.setNormalExecutionPrelude(latePrelude));
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
    ASSERT_TRUE(declarations.valid());
    EXPECT_EQ(declarations.taskAt(unguardedRoot.index).dependencyCount, 0u);
}

TEST(GpuTaskGraph, NormalExecutionPreludeRejectsNonPrimaryAndDependentRoots){
    TaskGraphTestUtils::TestArena testArena;
    {
        Graphics::GpuTaskGraph graph(testArena.arena);
        Graphics::GpuTaskCommandRequirements transferOnly = PrimaryGraphicsRequirements();
        transferOnly.requiredCapabilities = Graphics::GpuQueueCapability::Transfer;
        const auto taskResult = AddTaskWithCommands(graph, Name("tests/task_graph/prelude_transfer_only"), "Transfer Only Prelude", transferOnly, PreludeScheduling());
        ASSERT_TRUE(taskResult);
        const Graphics::GpuTaskId task = *taskResult;
        ASSERT_TRUE(task.valid());
        EXPECT_FALSE(graph.setNormalExecutionPrelude(task));
    }
    {
        Graphics::GpuTaskGraph graph(testArena.arena);
        Graphics::GpuTaskCommandRequirements external = PrimaryGraphicsRequirements();
        external.externalQueue = GraphicsQueue().id;
        const auto taskResult = AddTaskWithCommands(graph, Name("tests/task_graph/prelude_external"), "External Queue Prelude", external, PreludeScheduling());
        ASSERT_TRUE(taskResult);
        const Graphics::GpuTaskId task = *taskResult;
        ASSERT_TRUE(task.valid());
        EXPECT_FALSE(graph.setNormalExecutionPrelude(task));
    }
    {
        Graphics::GpuTaskGraph graph(testArena.arena);
        const auto taskResult = AddTaskWithCommands(graph, Name("tests/task_graph/prelude_mergeable"), "Mergeable Prelude", PrimaryGraphicsRequirements());
        ASSERT_TRUE(taskResult);
        const Graphics::GpuTaskId task = *taskResult;
        ASSERT_TRUE(task.valid());
        EXPECT_FALSE(graph.setNormalExecutionPrelude(task));
    }
    {
        Graphics::GpuTaskGraph graph(testArena.arena);
        const auto domainResult = AddHazardDomain(graph, Name("tests/task_graph/prelude_resource"), "Prelude Resource");
        ASSERT_TRUE(domainResult);
        const Graphics::GpuGraphResourceId domain = *domainResult;
        ASSERT_TRUE(domain.valid());
        const Graphics::GpuTaskResourceUse use{ .resource = domain, .range = {}, .requiredState = Graphics::ResourceStates::Common, .access = Graphics::GpuTaskResourceAccess::ReadWrite, };
        Graphics::GpuTaskDesc desc;
        desc.setIdentity(Name("tests/task_graph/prelude_resource_use")).setMarkerLabel("Resource Using Prelude").setScheduling(PreludeScheduling()).setResourceUses(&use, 1u);
        const auto taskResult = graph.addTask(desc, PrimaryGraphicsRequirements());
        ASSERT_TRUE(taskResult);
        const Graphics::GpuTaskId task = *taskResult;
        ASSERT_TRUE(task.valid());
        EXPECT_FALSE(graph.setNormalExecutionPrelude(task));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

