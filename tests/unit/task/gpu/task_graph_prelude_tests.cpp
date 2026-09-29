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

[[nodiscard]] static Graphics::GpuTaskSchedulingHint PreludeScheduling(){
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    return scheduling;
}

[[nodiscard]] static Graphics::GpuTaskCommandRequirements PrimaryGraphicsRequirements(){
    Graphics::GpuTaskCommandRequirements commands;
    commands.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;
    commands.requiresPrimaryGraphicsQueue = true;
    return commands;
}

[[nodiscard]] static Graphics::GpuTaskId AddPrelude(Graphics::GpuTaskGraph& graph){
    return AddTaskWithCommands(
        graph,
        Name("tests/task_graph/prelude"),
        "Normal Execution Prelude",
        PrimaryGraphicsRequirements(),
        PreludeScheduling(),
        { .policy = Graphics::GpuTaskTimingPolicy::PacketOnly }
    );
}

[[nodiscard]] static bool HasPacketDependency(
    const Graphics::GpuCompiledPacketView& consumer,
    const Graphics::GpuSubmissionPacketId producer
){
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
    TaskGraphTestUtils::TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedTransferQueue() };
    const Graphics::GpuPhysicalQueueTopology topology{ .queues = queues, .queueCount = LengthOf(queues) };

    const Graphics::GpuTaskId prelude = AddPrelude(graph);
    ASSERT_TRUE(prelude.valid());
    ASSERT_TRUE(graph.setNormalExecutionPrelude(prelude));
    const Graphics::GpuTaskId sceneRoot = AddTaskWithCommands(
        graph, Name("tests/task_graph/prelude_scene_root"), "Scene Root",
        { .requiredCapabilities = Graphics::GpuQueueCapability::Graphics }
    );
    const Graphics::GpuTaskId uiRoot = AddTaskWithCommands(
        graph, Name("tests/task_graph/prelude_ui_root"), "UI Upload Root",
        { .requiredCapabilities = Graphics::GpuQueueCapability::Transfer, .externalQueue = queues[1u].id }
    );
    ASSERT_TRUE(sceneRoot.valid());
    ASSERT_TRUE(uiRoot.valid());
    const Graphics::GpuTaskId sceneTail = AddTaskWithCommands(
        graph, Name("tests/task_graph/prelude_scene_tail"), "Scene Tail",
        { .requiredCapabilities = Graphics::GpuQueueCapability::Graphics }, {}, {}, &sceneRoot, 1u
    );
    const Graphics::GpuTaskId uiTail = AddTaskWithCommands(
        graph, Name("tests/task_graph/prelude_ui_tail"), "UI Tail",
        { .requiredCapabilities = Graphics::GpuQueueCapability::Transfer, .externalQueue = queues[1u].id },
        {}, {}, &uiRoot, 1u
    );
    ASSERT_TRUE(sceneTail.valid());
    ASSERT_TRUE(uiTail.valid());
    const Graphics::GpuTaskId terminalDependencies[] = { sceneTail, uiTail };
    const Graphics::GpuTaskId terminal = AddTaskWithCommands(
        graph, Name("tests/task_graph/prelude_terminal"), "Presentation Terminal",
        PrimaryGraphicsRequirements(), {}, {}, terminalDependencies, LengthOf(terminalDependencies)
    );
    ASSERT_TRUE(terminal.valid());
    Graphics::GpuTaskSchedulingHint recoveryScheduling = PreludeScheduling();
    recoveryScheduling.joinsAcceptedQueueFrontier = true;
    recoveryScheduling.isRecoverySubmission = true;
    const Graphics::GpuTaskId recovery = AddTaskWithCommands(
        graph, Name("tests/task_graph/prelude_recovery"), "Recovery Frontier",
        PrimaryGraphicsRequirements(), recoveryScheduling
    );
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
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph, options));
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
    const Graphics::GpuTaskId foreignPrelude = AddPrelude(otherGraph);
    ASSERT_TRUE(foreignPrelude.valid());
    EXPECT_FALSE(graph.setNormalExecutionPrelude({}));
    EXPECT_FALSE(graph.setNormalExecutionPrelude(foreignPrelude));

    const Graphics::GpuTaskId prelude = AddPrelude(graph);
    ASSERT_TRUE(prelude.valid());
    ASSERT_TRUE(graph.setNormalExecutionPrelude(prelude));
    EXPECT_FALSE(graph.setNormalExecutionPrelude(prelude));
    const Graphics::GpuTaskId root = AddTask(
        graph, Name("tests/task_graph/prelude_root_before_reset"), "Root Before Reset"
    );
    ASSERT_TRUE(root.valid());
    graph.reset();
    EXPECT_FALSE(graph.setNormalExecutionPrelude(prelude));
    const Graphics::GpuTaskId unguardedRoot = AddTask(
        graph, Name("tests/task_graph/prelude_root_after_reset"), "Root After Reset"
    );
    ASSERT_TRUE(unguardedRoot.valid());
    const Graphics::GpuTaskId latePrelude = AddPrelude(graph);
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
        const Graphics::GpuTaskId task = AddTaskWithCommands(
            graph, Name("tests/task_graph/prelude_transfer_only"), "Transfer Only Prelude",
            transferOnly, PreludeScheduling()
        );
        ASSERT_TRUE(task.valid());
        EXPECT_FALSE(graph.setNormalExecutionPrelude(task));
    }
    {
        Graphics::GpuTaskGraph graph(testArena.arena);
        Graphics::GpuTaskCommandRequirements external = PrimaryGraphicsRequirements();
        external.externalQueue = GraphicsQueue().id;
        const Graphics::GpuTaskId task = AddTaskWithCommands(
            graph, Name("tests/task_graph/prelude_external"), "External Queue Prelude",
            external, PreludeScheduling()
        );
        ASSERT_TRUE(task.valid());
        EXPECT_FALSE(graph.setNormalExecutionPrelude(task));
    }
    {
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuTaskId task = AddTaskWithCommands(
            graph, Name("tests/task_graph/prelude_mergeable"), "Mergeable Prelude",
            PrimaryGraphicsRequirements()
        );
        ASSERT_TRUE(task.valid());
        EXPECT_FALSE(graph.setNormalExecutionPrelude(task));
    }
    {
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId domain = AddHazardDomain(
            graph, Name("tests/task_graph/prelude_resource"), "Prelude Resource"
        );
        ASSERT_TRUE(domain.valid());
        const Graphics::GpuTaskResourceUse use{
            .resource = domain,
            .range = {},
            .requiredState = Graphics::ResourceStates::Common,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        };
        Graphics::GpuTaskDesc desc;
        desc.setIdentity(Name("tests/task_graph/prelude_resource_use"))
            .setMarkerLabel("Resource Using Prelude")
            .setScheduling(PreludeScheduling())
            .setResourceUses(&use, 1u)
        ;
        const Graphics::GpuTaskId task = graph.addTask(desc, PrimaryGraphicsRequirements());
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

