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

[[nodiscard]] static Core::GpuTaskSchedulingHint PreludeScheduling()noexcept{
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Tiny;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    return scheduling;
}

[[nodiscard]] static Core::GpuTaskCommandRequirements PrimaryGraphicsRequirements()noexcept{
    Core::GpuTaskCommandRequirements commands;
    commands.requiredCapabilities = Core::GpuQueueCapability::Graphics;
    commands.requiresPrimaryGraphicsQueue = true;
    return commands;
}

[[nodiscard]] static Expected<Core::GpuTaskId> AddPrelude(Core::GpuTaskGraph& graph){
    return AddTaskWithCommands(Name("tests/task_graph/prelude"), "Normal Execution Prelude", PrimaryGraphicsRequirements(), PreludeScheduling(), { .policy = Core::GpuTaskTimingPolicy::PacketOnly }, {}, graph);
}

[[nodiscard]] static b8 HasPacketDependency(const Core::GpuCompiledPacketView& consumer, const Core::GpuSubmissionPacketId producer)noexcept{
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
    Core::GpuTaskGraph graph(testArena.arena);
    const Array<Core::GpuPhysicalQueueInfo, 2u> queues = { GraphicsQueue(), DedicatedTransferQueue() };
    const Core::GpuPhysicalQueueTopology topology{ .queues = queues.data(), .queueCount = LengthOf(queues) };

    const auto preludeResult = AddPrelude(graph);
    ASSERT_TRUE(preludeResult);
    const Core::GpuTaskId prelude = *preludeResult;
    ASSERT_TRUE(prelude.valid());
    ASSERT_TRUE(graph.setNormalExecutionPrelude(prelude));
    const auto sceneRootResult = AddTaskWithCommands(Name("tests/task_graph/prelude_scene_root"), "Scene Root", { .requiredCapabilities = Core::GpuQueueCapability::Graphics }, {}, {}, {}, graph);
    ASSERT_TRUE(sceneRootResult);
    const Core::GpuTaskId sceneRoot = *sceneRootResult;
    const auto uiRootResult = AddTaskWithCommands(Name("tests/task_graph/prelude_ui_root"), "UI Upload Root", { .requiredCapabilities = Core::GpuQueueCapability::Transfer, .externalQueue = queues[1u].id }, {}, {}, {}, graph);
    ASSERT_TRUE(uiRootResult);
    const Core::GpuTaskId uiRoot = *uiRootResult;
    ASSERT_TRUE(sceneRoot.valid());
    ASSERT_TRUE(uiRoot.valid());
    const auto sceneTailResult = AddTaskWithCommands(Name("tests/task_graph/prelude_scene_tail"), "Scene Tail", { .requiredCapabilities = Core::GpuQueueCapability::Graphics }, {}, {}, Span(&sceneRoot, 1u), graph);
    ASSERT_TRUE(sceneTailResult);
    const Core::GpuTaskId sceneTail = *sceneTailResult;
    const auto uiTailResult = AddTaskWithCommands(Name("tests/task_graph/prelude_ui_tail"), "UI Tail", { .requiredCapabilities = Core::GpuQueueCapability::Transfer, .externalQueue = queues[1u].id }, {}, {}, Span(&uiRoot, 1u), graph);
    ASSERT_TRUE(uiTailResult);
    const Core::GpuTaskId uiTail = *uiTailResult;
    ASSERT_TRUE(sceneTail.valid());
    ASSERT_TRUE(uiTail.valid());
    const Array<Core::GpuTaskId, 2u> terminalDependencies = { sceneTail, uiTail };
    const auto terminalResult = AddTaskWithCommands(Name("tests/task_graph/prelude_terminal"), "Presentation Terminal", PrimaryGraphicsRequirements(), {}, {}, Span(terminalDependencies), graph);
    ASSERT_TRUE(terminalResult);
    const Core::GpuTaskId terminal = *terminalResult;
    ASSERT_TRUE(terminal.valid());
    Core::GpuTaskSchedulingHint recoveryScheduling = PreludeScheduling();
    recoveryScheduling.joinsAcceptedQueueFrontier = true;
    recoveryScheduling.isRecoverySubmission = true;
    const auto recoveryResult = AddTaskWithCommands(Name("tests/task_graph/prelude_recovery"), "Recovery Frontier", PrimaryGraphicsRequirements(), recoveryScheduling, {}, {}, graph);
    ASSERT_TRUE(recoveryResult);
    const Core::GpuTaskId recovery = *recoveryResult;
    ASSERT_TRUE(recovery.valid());

    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        ASSERT_TRUE(declarations.valid());
        const Core::GpuTaskGraphTaskView scene = declarations.taskAt(sceneRoot.index);
        const Core::GpuTaskGraphTaskView ui = declarations.taskAt(uiRoot.index);
        const Core::GpuTaskGraphTaskView sceneDependent = declarations.taskAt(sceneTail.index);
        const Core::GpuTaskGraphTaskView uiDependent = declarations.taskAt(uiTail.index);
        const Core::GpuTaskGraphTaskView lateRecovery = declarations.taskAt(recovery.index);
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

    Core::GpuTaskGraphAnalysis analysis(testArena.arena);
    Core::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Core::GpuCompiledGraph compiledGraph(testArena.arena);
    Core::GpuTaskGraphCompileOptions options;
    options.packetizationPolicy = Core::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    options.packetTimingEnvelope = { .firstTask = prelude, .lastTask = terminal };
    ASSERT_TRUE(Compile(analysis, assignments, compiledGraph, operationScratchArena, graph, topology, options));
    const Core::GpuCompiledGraph::ReadView plan(compiledGraph);
    ASSERT_TRUE(plan.valid());
    const Core::GpuSubmissionPacketId preludePacket = plan.packetForTask(prelude);
    const Core::GpuSubmissionPacketId scenePacket = plan.packetForTask(sceneRoot);
    const Core::GpuSubmissionPacketId uiPacket = plan.packetForTask(uiRoot);
    const Core::GpuSubmissionPacketId recoveryPacket = plan.packetForTask(recovery);
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
    const Core::GpuCompiledPacketView recoveryPlan = plan.packet(recoveryPacket);
    ASSERT_TRUE(recoveryPlan.valid());
    EXPECT_EQ(recoveryPlan.plan->dependencyCount, 0u);
    EXPECT_TRUE(recoveryPlan.plan->joinsAcceptedQueueFrontier);
}

TEST(GpuTaskGraph, NormalExecutionPreludeRejectsInvalidRegistrationAndClearsOnReset){
    TaskGraphTestUtils::TestArena testArena;
    Core::GpuTaskGraph graph(testArena.arena);
    Core::GpuTaskGraph otherGraph(testArena.arena);
    const auto foreignPreludeResult = AddPrelude(otherGraph);
    ASSERT_TRUE(foreignPreludeResult);
    const Core::GpuTaskId foreignPrelude = *foreignPreludeResult;
    ASSERT_TRUE(foreignPrelude.valid());
    EXPECT_FALSE(graph.setNormalExecutionPrelude({}));
    EXPECT_FALSE(graph.setNormalExecutionPrelude(foreignPrelude));

    const auto preludeResult = AddPrelude(graph);
    ASSERT_TRUE(preludeResult);
    const Core::GpuTaskId prelude = *preludeResult;
    ASSERT_TRUE(prelude.valid());
    ASSERT_TRUE(graph.setNormalExecutionPrelude(prelude));
    EXPECT_FALSE(graph.setNormalExecutionPrelude(prelude));
    const auto rootResult = AddTask(Name("tests/task_graph/prelude_root_before_reset"), "Root Before Reset", {}, {}, {}, {}, {}, graph);
    ASSERT_TRUE(rootResult);
    const Core::GpuTaskId root = *rootResult;
    ASSERT_TRUE(root.valid());
    graph.reset();
    EXPECT_FALSE(graph.setNormalExecutionPrelude(prelude));
    const auto unguardedRootResult = AddTask(Name("tests/task_graph/prelude_root_after_reset"), "Root After Reset", {}, {}, {}, {}, {}, graph);
    ASSERT_TRUE(unguardedRootResult);
    const Core::GpuTaskId unguardedRoot = *unguardedRootResult;
    ASSERT_TRUE(unguardedRoot.valid());
    const auto latePreludeResult = AddPrelude(graph);
    ASSERT_TRUE(latePreludeResult);
    const Core::GpuTaskId latePrelude = *latePreludeResult;
    ASSERT_TRUE(latePrelude.valid());
    EXPECT_FALSE(graph.setNormalExecutionPrelude(latePrelude));
    const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
    ASSERT_TRUE(declarations.valid());
    EXPECT_EQ(declarations.taskAt(unguardedRoot.index).dependencyCount, 0u);
}

TEST(GpuTaskGraph, NormalExecutionPreludeRejectsNonPrimaryAndDependentRoots){
    TaskGraphTestUtils::TestArena testArena;
    {
        Core::GpuTaskGraph graph(testArena.arena);
        Core::GpuTaskCommandRequirements transferOnly = PrimaryGraphicsRequirements();
        transferOnly.requiredCapabilities = Core::GpuQueueCapability::Transfer;
        const auto taskResult = AddTaskWithCommands(Name("tests/task_graph/prelude_transfer_only"), "Transfer Only Prelude", transferOnly, PreludeScheduling(), {}, {}, graph);
        ASSERT_TRUE(taskResult);
        const Core::GpuTaskId task = *taskResult;
        ASSERT_TRUE(task.valid());
        EXPECT_FALSE(graph.setNormalExecutionPrelude(task));
    }
    {
        Core::GpuTaskGraph graph(testArena.arena);
        Core::GpuTaskCommandRequirements external = PrimaryGraphicsRequirements();
        external.externalQueue = GraphicsQueue().id;
        const auto taskResult = AddTaskWithCommands(Name("tests/task_graph/prelude_external"), "External Queue Prelude", external, PreludeScheduling(), {}, {}, graph);
        ASSERT_TRUE(taskResult);
        const Core::GpuTaskId task = *taskResult;
        ASSERT_TRUE(task.valid());
        EXPECT_FALSE(graph.setNormalExecutionPrelude(task));
    }
    {
        Core::GpuTaskGraph graph(testArena.arena);
        const auto taskResult = AddTaskWithCommands(Name("tests/task_graph/prelude_mergeable"), "Mergeable Prelude", PrimaryGraphicsRequirements(), {}, {}, {}, graph);
        ASSERT_TRUE(taskResult);
        const Core::GpuTaskId task = *taskResult;
        ASSERT_TRUE(task.valid());
        EXPECT_FALSE(graph.setNormalExecutionPrelude(task));
    }
    {
        Core::GpuTaskGraph graph(testArena.arena);
        const auto domainResult = AddHazardDomain(graph, Name("tests/task_graph/prelude_resource"), "Prelude Resource");
        ASSERT_TRUE(domainResult);
        const Core::GpuGraphResourceId domain = *domainResult;
        ASSERT_TRUE(domain.valid());
        const Core::GpuTaskResourceUse use{ .resource = domain, .range = {}, .requiredState = Core::ResourceStates::Common, .access = Core::GpuTaskResourceAccess::ReadWrite, };
        Core::GpuTaskDesc desc;
        desc.setIdentity(Name("tests/task_graph/prelude_resource_use")).setMarkerLabel("Resource Using Prelude").setScheduling(PreludeScheduling()).setResourceUses(Span(&use, 1u));
        const auto taskResult = graph.addTask(desc, PrimaryGraphicsRequirements());
        ASSERT_TRUE(taskResult);
        const Core::GpuTaskId task = *taskResult;
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

