// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"
#include "task_graph_lifecycle_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_queue_assignment_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, AssignsOnlyCompatiblePhysicalQueuesAndFallsBackToGraphics){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuQueueRequest graphicsRequest;
    graphicsRequest.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;
    graphicsRequest.preferredQueue = Graphics::GpuQueuePreference::Graphics;
    graphicsRequest.allowFallback = false;
    graphicsRequest.compilerMayOverridePreference = false;
    Graphics::GpuQueueRequest computeRequest;
    computeRequest.requiredCapabilities = Graphics::GpuQueueCapability::Compute;
    computeRequest.preferredQueue = Graphics::GpuQueuePreference::Compute;
    computeRequest.allowFallback = true;
    computeRequest.compilerMayOverridePreference = true;

    const Graphics::GpuTaskId graphicsTask = AddTaskWithQueue(
        graph,
        Name("tests/task_graph/queue_graphics"),
        "Queue Graphics",
        graphicsRequest
    );
    const Graphics::GpuTaskId computeTask = AddTaskWithQueue(
        graph,
        Name("tests/task_graph/queue_compute"),
        "Queue Compute",
        computeRequest
    );
    ASSERT_TRUE(graphicsTask.valid());
    ASSERT_TRUE(computeTask.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    const Graphics::GpuPhysicalQueueInfo queues[] = {
        DedicatedComputeQueue(),
        GraphicsQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Assign(graph, analysis, topology, assignments));
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

        ASSERT_TRUE(assignments.validFor(declarations));
    }

    const Graphics::GpuTaskQueueAssignment* const graphicsAssignment = assignments.find(graphicsTask);
    const Graphics::GpuTaskQueueAssignment* const computeAssignment = assignments.find(computeTask);
    ASSERT_NE(graphicsAssignment, nullptr);
    ASSERT_NE(computeAssignment, nullptr);
    EXPECT_EQ(graphicsAssignment->queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(graphicsAssignment->reason, Graphics::GpuTaskQueueAssignmentReason::RequiredGraphics);
    EXPECT_EQ(computeAssignment->queueClass, Graphics::CommandQueue::Compute);
    EXPECT_TRUE(computeAssignment->dedicated);
    EXPECT_EQ(computeAssignment->reason, Graphics::GpuTaskQueueAssignmentReason::DedicatedCompute);

    const Graphics::GpuPhysicalQueueInfo graphicsOnly[] = { GraphicsQueue() };
    const Graphics::GpuTaskGraphQueueTopology graphicsOnlyTopology{
        .queues = graphicsOnly,
        .queueCount = LengthOf(graphicsOnly),
    };
    Graphics::GpuTaskGraphQueueAssignments graphicsFallbackAssignments(testArena.arena);
    ASSERT_TRUE(Assign(graph, analysis, graphicsOnlyTopology, graphicsFallbackAssignments));
    const Graphics::GpuTaskQueueAssignment* const fallbackAssignment = graphicsFallbackAssignments.find(computeTask);
    ASSERT_NE(fallbackAssignment, nullptr);
    EXPECT_EQ(fallbackAssignment->queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(fallbackAssignment->reason, Graphics::GpuTaskQueueAssignmentReason::Fallback);
}

TEST(GpuTaskGraph, RetainsTinyAndNonOverlappingComputeTasksOnGraphics){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuQueueRequest computeRequest;
    computeRequest.requiredCapabilities = Graphics::GpuQueueCapability::Compute;
    computeRequest.preferredQueue = Graphics::GpuQueuePreference::Compute;
    computeRequest.allowFallback = true;
    computeRequest.compilerMayOverridePreference = true;

    Graphics::GpuTaskSchedulingHint tinyScheduling;
    tinyScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    Graphics::GpuTaskSchedulingHint noOverlapScheduling;
    noOverlapScheduling.overlapPreferred = false;
    Graphics::GpuQueueRequest strictComputeRequest = computeRequest;
    strictComputeRequest.compilerMayOverridePreference = false;
    Graphics::GpuQueueRequest noFallbackComputeRequest = computeRequest;
    noFallbackComputeRequest.allowFallback = false;

    const Graphics::GpuTaskId tinyTask = AddTaskWithQueue(
        graph,
        Name("tests/task_graph/queue_tiny"),
        "Queue Tiny",
        computeRequest,
        tinyScheduling
    );
    const Graphics::GpuTaskId noOverlapTask = AddTaskWithQueue(
        graph,
        Name("tests/task_graph/queue_no_overlap"),
        "Queue No Overlap",
        computeRequest,
        noOverlapScheduling
    );
    const Graphics::GpuTaskId strictTask = AddTaskWithQueue(
        graph,
        Name("tests/task_graph/queue_strict_compute"),
        "Queue Strict Compute",
        strictComputeRequest,
        tinyScheduling
    );
    const Graphics::GpuTaskId noFallbackTask = AddTaskWithQueue(
        graph,
        Name("tests/task_graph/queue_no_fallback_compute"),
        "Queue No Fallback Compute",
        noFallbackComputeRequest,
        tinyScheduling
    );
    ASSERT_TRUE(tinyTask.valid());
    ASSERT_TRUE(noOverlapTask.valid());
    ASSERT_TRUE(strictTask.valid());
    ASSERT_TRUE(noFallbackTask.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Assign(graph, analysis, topology, assignments));

    const Graphics::GpuTaskQueueAssignment* const tinyAssignment = assignments.find(tinyTask);
    const Graphics::GpuTaskQueueAssignment* const noOverlapAssignment = assignments.find(noOverlapTask);
    const Graphics::GpuTaskQueueAssignment* const strictAssignment = assignments.find(strictTask);
    const Graphics::GpuTaskQueueAssignment* const noFallbackAssignment = assignments.find(noFallbackTask);
    ASSERT_NE(tinyAssignment, nullptr);
    ASSERT_NE(noOverlapAssignment, nullptr);
    ASSERT_NE(strictAssignment, nullptr);
    ASSERT_NE(noFallbackAssignment, nullptr);
    EXPECT_EQ(tinyAssignment->queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(tinyAssignment->reason, Graphics::GpuTaskQueueAssignmentReason::CompilerOverride);
    EXPECT_EQ(tinyAssignment->initialQueue, tinyAssignment->queue);
    EXPECT_EQ(noOverlapAssignment->queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(noOverlapAssignment->reason, Graphics::GpuTaskQueueAssignmentReason::CompilerOverride);
    EXPECT_EQ(strictAssignment->queueClass, Graphics::CommandQueue::Compute);
    EXPECT_EQ(strictAssignment->reason, Graphics::GpuTaskQueueAssignmentReason::DedicatedCompute);
    EXPECT_EQ(noFallbackAssignment->queueClass, Graphics::CommandQueue::Compute);
    EXPECT_EQ(noFallbackAssignment->reason, Graphics::GpuTaskQueueAssignmentReason::DedicatedCompute);
}

TEST(GpuTaskGraph, FallsBackOnlyWhenConcretePreferenceIsUnavailableAndFallbackIsAllowed){
    const auto runCase = [](const bool compilerMayOverridePreference, const bool allowFallback, const bool expectedResult){
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        Graphics::GpuQueueRequest computeRequest;
        computeRequest.requiredCapabilities = Graphics::GpuQueueCapability::Compute;
        computeRequest.preferredQueue = Graphics::GpuQueuePreference::Compute;
        computeRequest.compilerMayOverridePreference = compilerMayOverridePreference;
        computeRequest.allowFallback = allowFallback;
        const Graphics::GpuTaskId task = AddTaskWithQueue(
            graph,
            Name("tests/task_graph/unavailable_strict_compute"),
            "Unavailable Strict Compute",
            computeRequest
        );
        ASSERT_TRUE(task.valid());

        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        ASSERT_TRUE(Analyze(graph, analysis));
        const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue() };
        const Graphics::GpuTaskGraphQueueTopology topology{
            .queues = queues,
            .queueCount = LengthOf(queues),
        };
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        EXPECT_EQ(Assign(graph, analysis, topology, assignments), expectedResult);
        if(expectedResult){
            const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(task);
            ASSERT_NE(assignment, nullptr);
            EXPECT_EQ(assignment->queueClass, Graphics::CommandQueue::Graphics);
            EXPECT_EQ(assignment->reason, Graphics::GpuTaskQueueAssignmentReason::Fallback);
        }
        else{
            EXPECT_EQ(assignments.diagnostic().status, Graphics::GpuTaskGraphQueueAssignmentStatus::NoCompatibleQueue);
        }
    };

    runCase(false, true, true);
    runCase(true, false, false);
    runCase(true, true, true);
}

TEST(GpuTaskGraph, PreservesConcretePreferenceSemanticsForGraphicsRequiredTasks){
    const auto runCase = [](
        const bool allowFallback,
        const bool exposeGraphicsCapableCompute,
        const bool expectedResult,
        const Graphics::CommandQueue::Enum expectedQueueClass,
        const Graphics::GpuTaskQueueAssignmentReason::Enum expectedReason
    ){
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        Graphics::GpuQueueRequest queueRequest;
        queueRequest.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;
        queueRequest.preferredQueue = Graphics::GpuQueuePreference::Compute;
        queueRequest.allowFallback = allowFallback;
        queueRequest.compilerMayOverridePreference = false;
        const Graphics::GpuTaskId task = AddTaskWithQueue(
            graph,
            Name("tests/task_graph/graphics_required_compute_preference"),
            "Graphics Required Compute Preference",
            queueRequest
        );
        ASSERT_TRUE(task.valid());

        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        ASSERT_TRUE(Analyze(graph, analysis));
        Graphics::GpuPhysicalQueueInfo graphicsCapableCompute = DedicatedComputeQueue();
        graphicsCapableCompute.capabilities = QueueCapabilities(
            Graphics::GpuQueueCapability::Graphics,
            Graphics::GpuQueueCapability::Compute,
            Graphics::GpuQueueCapability::Transfer
        );
        graphicsCapableCompute.dedicated = false;
        const Graphics::GpuPhysicalQueueInfo queues[] = {
            GraphicsQueue(),
            graphicsCapableCompute,
        };
        const Graphics::GpuTaskGraphQueueTopology topology{
            .queues = queues,
            .queueCount = exposeGraphicsCapableCompute ? LengthOf(queues) : 1u,
        };
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        const bool result = Assign(graph, analysis, topology, assignments);
        if(!expectedResult){
            EXPECT_FALSE(result);
            EXPECT_EQ(assignments.diagnostic().status, Graphics::GpuTaskGraphQueueAssignmentStatus::NoCompatibleQueue);
            EXPECT_EQ(assignments.diagnostic().task, task);
            EXPECT_EQ(assignments.diagnostic().requiredCapabilities, Graphics::GpuQueueCapability::Graphics);
            return;
        }

        ASSERT_TRUE(result);
        const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(task);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->queueClass, expectedQueueClass);
        EXPECT_EQ(assignment->reason, expectedReason);
        EXPECT_EQ(
            assignment->queue,
            exposeGraphicsCapableCompute ? graphicsCapableCompute.id : GraphicsQueue().id
        );
    };

    runCase(
        false,
        false,
        false,
        Graphics::CommandQueue::kCount,
        Graphics::GpuTaskQueueAssignmentReason::Unknown
    );
    runCase(
        true,
        false,
        true,
        Graphics::CommandQueue::Graphics,
        Graphics::GpuTaskQueueAssignmentReason::Fallback
    );
    runCase(
        false,
        true,
        true,
        Graphics::CommandQueue::Compute,
        Graphics::GpuTaskQueueAssignmentReason::PreferredQueue
    );
}

TEST(GpuTaskGraph, ScoresAnyAcrossQueueClassesDeterministicallyWithoutPhysicalRoutingOptIn){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    Graphics::GpuQueueRequest graphicsRequest;
    graphicsRequest.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;
    graphicsRequest.preferredQueue = Graphics::GpuQueuePreference::Graphics;
    graphicsRequest.allowFallback = false;
    graphicsRequest.compilerMayOverridePreference = false;
    Graphics::GpuTaskSchedulingHint graphicsScheduling;
    graphicsScheduling.cost = Graphics::GpuTaskCostHint::Large;
    const Graphics::GpuTaskId graphicsTask = AddTaskWithQueue(
        graph,
        Name("tests/task_graph/scored_any_graphics"),
        "Scored Any Graphics",
        graphicsRequest,
        graphicsScheduling
    );

    Graphics::GpuQueueRequest anyRequest;
    anyRequest.requiredCapabilities = Graphics::GpuQueueCapability::Transfer;
    anyRequest.preferredQueue = Graphics::GpuQueuePreference::Any;
    const Graphics::GpuTaskId anyTask = AddTaskWithQueue(
        graph,
        Name("tests/task_graph/scored_any"),
        "Scored Any",
        anyRequest
    );
    ASSERT_TRUE(graphicsTask.valid());
    ASSERT_TRUE(anyTask.valid());

    Graphics::GpuPhysicalQueueInfo auxiliaryGraphics = GraphicsQueue(3u);
    auxiliaryGraphics.queueIndex = 1u;
    const Graphics::GpuPhysicalQueueInfo firstQueues[] = {
        auxiliaryGraphics,
        DedicatedTransferQueue(),
        DedicatedComputeQueue(),
        GraphicsQueue(),
    };
    const Graphics::GpuPhysicalQueueInfo secondQueues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
        auxiliaryGraphics,
        DedicatedTransferQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology firstTopology{
        .queues = firstQueues,
        .queueCount = LengthOf(firstQueues),
    };
    const Graphics::GpuTaskGraphQueueTopology secondTopology{
        .queues = secondQueues,
        .queueCount = LengthOf(secondQueues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    Graphics::GpuTaskGraphQueueAssignments firstAssignments(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments secondAssignments(testArena.arena);
    ASSERT_TRUE(Assign(graph, analysis, firstTopology, firstAssignments));
    ASSERT_TRUE(Assign(graph, analysis, secondTopology, secondAssignments));

    const Graphics::GpuTaskQueueAssignment* const firstAssignment = firstAssignments.find(anyTask);
    const Graphics::GpuTaskQueueAssignment* const secondAssignment = secondAssignments.find(anyTask);
    ASSERT_NE(firstAssignment, nullptr);
    ASSERT_NE(secondAssignment, nullptr);
    EXPECT_EQ(firstAssignment->queue, DedicatedComputeQueue().id);
    EXPECT_EQ(secondAssignment->queue, firstAssignment->queue);
    EXPECT_NE(firstAssignment->queue, auxiliaryGraphics.id);
    EXPECT_EQ(firstAssignment->reason, Graphics::GpuTaskQueueAssignmentReason::ScoredAny);
    EXPECT_EQ(firstAssignment->initialQueue, firstAssignment->queue);
    EXPECT_EQ(firstAssignment->modifiers, Graphics::GpuTaskQueueAssignmentModifier::None);
    EXPECT_EQ(firstAssignment->score.overlap, 8);
    EXPECT_EQ(firstAssignment->score.queueLoad, 0);
    EXPECT_EQ(firstAssignment->score.incomingCrossings, 0);
    EXPECT_EQ(firstAssignment->score.outgoingCrossings, 0);
    EXPECT_EQ(firstAssignment->score.ownershipTransfers, 0);
}

TEST(GpuTaskGraph, ScoresComputeIndependenceOwnershipAndStrictTiePolicy){
    const auto runCase = [](
        const Graphics::ResourceQueueSharing::Mask queueSharing,
        const bool compilerMayOverridePreference,
        const bool addIndependentGraphics,
        const Graphics::CommandQueue::Enum expectedQueue,
        const Graphics::GpuTaskQueueAssignmentReason::Enum expectedReason,
        const i32 expectedOwnershipTransfers
    ){
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Name resourceNames[] = {
            Name("tests/task_graph/scored_compute_resource_a"),
            Name("tests/task_graph/scored_compute_resource_b"),
            Name("tests/task_graph/scored_compute_resource_c"),
        };
        Graphics::GpuGraphResourceId resources[LengthOf(resourceNames)] = {};
        for(usize resourceIndex = 0u; resourceIndex < LengthOf(resources); ++resourceIndex){
            resources[resourceIndex] = AddBufferMetadata(
                graph,
                resourceNames[resourceIndex],
                "Scored Compute Resource",
                Graphics::ResourceStates::Common,
                queueSharing
            );
            ASSERT_TRUE(resources[resourceIndex].valid());
        }

        Graphics::GpuQueueRequest graphicsRequest;
        graphicsRequest.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;
        graphicsRequest.preferredQueue = Graphics::GpuQueuePreference::Graphics;
        graphicsRequest.allowFallback = false;
        graphicsRequest.compilerMayOverridePreference = false;
        Graphics::GpuTaskSchedulingHint producerScheduling;
        producerScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
        const Name producerNames[] = {
            Name("tests/task_graph/scored_compute_producer_a"),
            Name("tests/task_graph/scored_compute_producer_b"),
            Name("tests/task_graph/scored_compute_producer_c"),
        };
        for(usize producerIndex = 0u; producerIndex < LengthOf(producerNames); ++producerIndex){
            const Graphics::GpuTaskResourceUse producerUse{
                .resource = resources[producerIndex],
                .range = {},
                .requiredState = Graphics::ResourceStates::UnorderedAccess,
                .access = Graphics::GpuTaskResourceAccess::Write,
            };
            Graphics::GpuTaskDesc producerDesc;
            producerDesc
                .setIdentity(producerNames[producerIndex])
                .setMarkerLabel("Scored Compute Producer")
                .setQueue(graphicsRequest)
                .setScheduling(producerScheduling)
                .setResourceUses(&producerUse, 1u)
            ;
            ASSERT_TRUE(graph.addTask(producerDesc).valid());
        }

        if(addIndependentGraphics){
            const Graphics::GpuTaskId independentGraphics = AddTaskWithQueue(
                graph,
                Name("tests/task_graph/scored_compute_independent_graphics"),
                "Scored Compute Independent Graphics",
                graphicsRequest,
                producerScheduling
            );
            ASSERT_TRUE(independentGraphics.valid());
        }

        Graphics::GpuQueueRequest computeRequest;
        computeRequest.requiredCapabilities = Graphics::GpuQueueCapability::Compute;
        computeRequest.preferredQueue = Graphics::GpuQueuePreference::Compute;
        computeRequest.allowFallback = true;
        computeRequest.compilerMayOverridePreference = compilerMayOverridePreference;
        Graphics::GpuTaskSchedulingHint computeScheduling;
        computeScheduling.cost = Graphics::GpuTaskCostHint::Small;
        const Graphics::GpuTaskResourceUse computeUses[] = {
            { .resource = resources[0u], .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
            { .resource = resources[1u], .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
            { .resource = resources[2u], .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
        };
        Graphics::GpuTaskDesc computeDesc;
        computeDesc
            .setIdentity(Name("tests/task_graph/scored_compute_consumer"))
            .setMarkerLabel("Scored Compute Consumer")
            .setQueue(computeRequest)
            .setScheduling(computeScheduling)
            .setResourceUses(computeUses, LengthOf(computeUses))
        ;
        const Graphics::GpuTaskId computeTask = graph.addTask(computeDesc);
        ASSERT_TRUE(computeTask.valid());

        const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue() };
        const Graphics::GpuTaskGraphQueueTopology topology{
            .queues = queues,
            .queueCount = LengthOf(queues),
        };
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        ASSERT_TRUE(Analyze(graph, analysis));
        ASSERT_EQ(analysis.inferredEdges().size(), 3u);
        ASSERT_EQ(analysis.schedulingEdges().size(), 3u);
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        ASSERT_TRUE(Assign(graph, analysis, topology, assignments));
        const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(computeTask);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->queueClass, expectedQueue);
        EXPECT_EQ(assignment->reason, expectedReason);
        EXPECT_EQ(assignment->initialQueue, assignment->queue);
        EXPECT_EQ(assignment->score.incomingCrossings, expectedQueue == Graphics::CommandQueue::Compute ? 3 : 0);
        EXPECT_EQ(assignment->score.ownershipTransfers, expectedOwnershipTransfers);
    };

    runCase(
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute,
        true,
        false,
        Graphics::CommandQueue::Graphics,
        Graphics::GpuTaskQueueAssignmentReason::CompilerOverride,
        0
    );
    runCase(
        Graphics::ResourceQueueSharing::Exclusive,
        true,
        true,
        Graphics::CommandQueue::Graphics,
        Graphics::GpuTaskQueueAssignmentReason::CompilerOverride,
        0
    );
    runCase(
        Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute,
        true,
        true,
        Graphics::CommandQueue::Compute,
        Graphics::GpuTaskQueueAssignmentReason::DedicatedCompute,
        0
    );
    runCase(
        Graphics::ResourceQueueSharing::Exclusive,
        false,
        true,
        Graphics::CommandQueue::Compute,
        Graphics::GpuTaskQueueAssignmentReason::DedicatedCompute,
        3
    );
}

TEST(GpuTaskGraph, QueueScoreUsesOnlyReducedOutgoingDependencies){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuQueueRequest computeRequest;
    computeRequest.requiredCapabilities = Graphics::GpuQueueCapability::Compute;
    computeRequest.preferredQueue = Graphics::GpuQueuePreference::Compute;
    computeRequest.allowFallback = false;
    computeRequest.compilerMayOverridePreference = false;
    Graphics::GpuQueueRequest graphicsRequest;
    graphicsRequest.requiredCapabilities = Graphics::GpuQueueCapability::Graphics;
    graphicsRequest.preferredQueue = Graphics::GpuQueuePreference::Graphics;
    graphicsRequest.allowFallback = false;
    graphicsRequest.compilerMayOverridePreference = false;

    const Graphics::GpuTaskId producer = AddTaskWithQueue(
        graph,
        Name("tests/task_graph/reduced_queue_score_producer"),
        "Reduced Queue Score Producer",
        computeRequest
    );
    const Graphics::GpuTaskId middle = AddTaskWithQueue(
        graph,
        Name("tests/task_graph/reduced_queue_score_middle"),
        "Reduced Queue Score Middle",
        graphicsRequest,
        {},
        {},
        &producer,
        1u
    );
    const Graphics::GpuTaskId finalDependencies[] = { producer, middle };
    const Graphics::GpuTaskId finalTask = AddTaskWithQueue(
        graph,
        Name("tests/task_graph/reduced_queue_score_final"),
        "Reduced Queue Score Final",
        graphicsRequest,
        {},
        {},
        finalDependencies,
        LengthOf(finalDependencies)
    );
    ASSERT_TRUE(producer.valid());
    ASSERT_TRUE(middle.valid());
    ASSERT_TRUE(finalTask.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    ASSERT_EQ(analysis.edges().size(), 3u);
    ASSERT_EQ(analysis.schedulingEdges().size(), 2u);
    const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    ASSERT_TRUE(Assign(graph, analysis, topology, assignments));
    const Graphics::GpuTaskQueueAssignment* const producerAssignment = assignments.find(producer);
    ASSERT_NE(producerAssignment, nullptr);
    EXPECT_EQ(producerAssignment->queueClass, Graphics::CommandQueue::Compute);
    EXPECT_EQ(producerAssignment->score.outgoingCrossings, 1);
    EXPECT_EQ(producerAssignment->score.incomingCrossings, 0);
    EXPECT_EQ(producerAssignment->score.ownershipTransfers, 0);
}

TEST(GpuTaskGraph, DeduplicatesRawOwnershipScoreAndIgnoresSameFamilyQueueCrossings){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId texture = AddTextureMetadata(
        graph,
        Name("tests/task_graph/ownership_score_texture"),
        "Ownership Score Texture"
    );
    ASSERT_TRUE(texture.valid());
    const Graphics::GpuGraphResourceId secondTexture = AddTextureMetadata(
        graph,
        Name("tests/task_graph/ownership_score_second_texture"),
        "Ownership Score Second Texture"
    );
    ASSERT_TRUE(secondTexture.valid());

    const Graphics::GpuTaskResourceUse producerUse{
        .resource = texture,
        .range = {},
        .requiredState = Graphics::ResourceStates::UnorderedAccess,
        .access = Graphics::GpuTaskResourceAccess::Write,
    };
    const Graphics::GpuTaskResourceUse consumerUse{
        .resource = texture,
        .range = {},
        .requiredState = Graphics::ResourceStates::UnorderedAccess,
        .access = Graphics::GpuTaskResourceAccess::ReadWrite,
    };
    Graphics::GpuTaskResourceUse producerUses[] = { producerUse, producerUse };
    producerUses[1u].resource = secondTexture;
    Graphics::GpuTaskResourceUse consumerUses[] = { consumerUse, consumerUse };
    consumerUses[1u].resource = secondTexture;
    Graphics::GpuTaskResourceUse finalUses[] = { consumerUses[0u], consumerUses[1u] };
    for(Graphics::GpuTaskResourceUse& use : finalUses)
        use.access = Graphics::GpuTaskResourceAccess::Read;
    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    const Graphics::GpuQueueRequest computeRequest{
        Graphics::GpuQueueCapability::Compute,
        Graphics::GpuQueuePreference::Compute,
        false,
        false,
    };
    Graphics::GpuTaskDesc producerDesc;
    producerDesc
        .setIdentity(Name("tests/task_graph/ownership_score_producer"))
        .setMarkerLabel("Ownership Score Producer")
        .setQueue(graphicsRequest)
        .setResourceUses(producerUses, LengthOf(producerUses))
    ;
    const Graphics::GpuTaskId producer = graph.addTask(producerDesc);
    Graphics::GpuTaskDesc consumerDesc;
    consumerDesc
        .setIdentity(Name("tests/task_graph/ownership_score_consumer"))
        .setMarkerLabel("Ownership Score Consumer")
        .setQueue(computeRequest)
        .setResourceUses(consumerUses, LengthOf(consumerUses))
    ;
    const Graphics::GpuTaskId consumer = graph.addTask(consumerDesc);
    ASSERT_TRUE(producer.valid());
    ASSERT_TRUE(consumer.valid());
    Graphics::GpuTaskDesc finalDesc;
    finalDesc
        .setIdentity(Name("tests/task_graph/ownership_score_final_consumer"))
        .setMarkerLabel("Ownership Score Final Consumer")
        .setQueue(graphicsRequest)
        .setResourceUses(finalUses, LengthOf(finalUses))
    ;
    const Graphics::GpuTaskId finalConsumer = graph.addTask(finalDesc);
    ASSERT_TRUE(finalConsumer.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    ASSERT_EQ(analysis.inferredEdges().size(), 6u);
    ASSERT_EQ(analysis.schedulingEdges().size(), 2u);

    const Graphics::GpuPhysicalQueueInfo separateFamilyQueues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology separateFamilyTopology{
        .queues = separateFamilyQueues,
        .queueCount = LengthOf(separateFamilyQueues),
    };
    Graphics::GpuTaskGraphQueueAssignments separateFamilyAssignments(testArena.arena);
    ASSERT_TRUE(Assign(graph, analysis, separateFamilyTopology, separateFamilyAssignments));
    const Graphics::GpuTaskQueueAssignment* const separateFamilyAssignment = separateFamilyAssignments.find(consumer);
    ASSERT_NE(separateFamilyAssignment, nullptr);
    EXPECT_EQ(separateFamilyAssignment->score.incomingCrossings, 1);
    EXPECT_EQ(separateFamilyAssignment->score.outgoingCrossings, 1);
    EXPECT_EQ(separateFamilyAssignment->score.ownershipTransfers, 4);
    const Graphics::GpuTaskQueueAssignment* const producerAssignment = separateFamilyAssignments.find(producer);
    const Graphics::GpuTaskQueueAssignment* const finalAssignment = separateFamilyAssignments.find(finalConsumer);
    ASSERT_NE(producerAssignment, nullptr);
    ASSERT_NE(finalAssignment, nullptr);
    EXPECT_EQ(producerAssignment->score.ownershipTransfers, 2);
    EXPECT_EQ(finalAssignment->score.ownershipTransfers, 2);

    Graphics::GpuPhysicalQueueInfo sameFamilyCompute = DedicatedComputeQueue();
    sameFamilyCompute.familyIndex = GraphicsQueue().familyIndex;
    sameFamilyCompute.queueIndex = 1u;
    const Graphics::GpuPhysicalQueueInfo sameFamilyQueues[] = {
        GraphicsQueue(),
        sameFamilyCompute,
    };
    const Graphics::GpuTaskGraphQueueTopology sameFamilyTopology{
        .queues = sameFamilyQueues,
        .queueCount = LengthOf(sameFamilyQueues),
    };
    Graphics::GpuTaskGraphQueueAssignments sameFamilyAssignments(testArena.arena);
    ASSERT_TRUE(Assign(graph, analysis, sameFamilyTopology, sameFamilyAssignments));
    const Graphics::GpuTaskQueueAssignment* const sameFamilyAssignment = sameFamilyAssignments.find(consumer);
    ASSERT_NE(sameFamilyAssignment, nullptr);
    EXPECT_EQ(sameFamilyAssignment->score.incomingCrossings, 1);
    EXPECT_EQ(sameFamilyAssignment->score.ownershipTransfers, 0);
    const Graphics::GpuTaskQueueAssignment* const sameFamilyProducer = sameFamilyAssignments.find(producer);
    const Graphics::GpuTaskQueueAssignment* const sameFamilyFinal = sameFamilyAssignments.find(finalConsumer);
    ASSERT_NE(sameFamilyProducer, nullptr);
    ASSERT_NE(sameFamilyFinal, nullptr);
    EXPECT_EQ(sameFamilyProducer->score.ownershipTransfers, 0);
    EXPECT_EQ(sameFamilyFinal->score.ownershipTransfers, 0);
}

TEST(GpuTaskGraph, UsesCompleteProvisionalRoutesWhenScoringFutureConsumers){
    const auto runCase = [](const bool addFutureConsumer, const Graphics::CommandQueue::Enum expectedQueue){
        TestArena testArena;
        Graphics::GpuTaskGraph graph(testArena.arena);
        const Graphics::GpuGraphResourceId incoming = AddBufferMetadata(
            graph,
            Name("tests/task_graph/provisional_score_incoming"),
            "Provisional Score Incoming"
        );
        const Graphics::GpuGraphResourceId outgoing = AddBufferMetadata(
            graph,
            Name("tests/task_graph/provisional_score_outgoing"),
            "Provisional Score Outgoing"
        );
        ASSERT_TRUE(incoming.valid());
        ASSERT_TRUE(outgoing.valid());

        const Graphics::GpuQueueRequest graphicsRequest{
            Graphics::GpuQueueCapability::Graphics,
            Graphics::GpuQueuePreference::Graphics,
            false,
            false,
        };
        const Graphics::GpuQueueRequest strictComputeRequest{
            Graphics::GpuQueueCapability::Compute,
            Graphics::GpuQueuePreference::Compute,
            false,
            false,
        };
        const Graphics::GpuQueueRequest movableComputeRequest{
            Graphics::GpuQueueCapability::Compute,
            Graphics::GpuQueuePreference::Compute,
            true,
            true,
        };
        Graphics::GpuTaskSchedulingHint tinyScheduling;
        tinyScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
        Graphics::GpuTaskSchedulingHint movableScheduling;
        movableScheduling.cost = Graphics::GpuTaskCostHint::Small;

        const Graphics::GpuTaskResourceUse producerUse{
            .resource = incoming,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        };
        Graphics::GpuTaskDesc producerDesc;
        producerDesc
            .setIdentity(Name("tests/task_graph/provisional_score_producer"))
            .setMarkerLabel("Provisional Score Producer")
            .setQueue(graphicsRequest)
            .setScheduling(tinyScheduling)
            .setResourceUses(&producerUse, 1u)
        ;
        const Graphics::GpuTaskId producer = graph.addTask(producerDesc);
        ASSERT_TRUE(producer.valid());

        const Graphics::GpuTaskResourceUse movableUses[] = {
            { .resource = incoming, .range = {}, .requiredState = Graphics::ResourceStates::ShaderResource, .access = Graphics::GpuTaskResourceAccess::Read },
            { .resource = outgoing, .range = {}, .requiredState = Graphics::ResourceStates::UnorderedAccess, .access = Graphics::GpuTaskResourceAccess::Write },
        };
        Graphics::GpuTaskDesc movableDesc;
        movableDesc
            .setIdentity(Name("tests/task_graph/provisional_score_movable"))
            .setMarkerLabel("Provisional Score Movable")
            .setQueue(movableComputeRequest)
            .setScheduling(movableScheduling)
            .setResourceUses(movableUses, LengthOf(movableUses))
        ;
        const Graphics::GpuTaskId movable = graph.addTask(movableDesc);
        ASSERT_TRUE(movable.valid());

        ASSERT_TRUE(AddTaskWithQueue(
            graph,
            Name("tests/task_graph/provisional_score_compute_load"),
            "Provisional Score Compute Load",
            strictComputeRequest,
            tinyScheduling
        ).valid());
        ASSERT_TRUE(AddTaskWithQueue(
            graph,
            Name("tests/task_graph/provisional_score_independent_graphics"),
            "Provisional Score Independent Graphics",
            graphicsRequest,
            tinyScheduling
        ).valid());

        Graphics::GpuTaskId futureConsumer;
        if(addFutureConsumer){
            const Graphics::GpuTaskResourceUse consumerUse{
                .resource = outgoing,
                .range = {},
                .requiredState = Graphics::ResourceStates::ShaderResource,
                .access = Graphics::GpuTaskResourceAccess::Read,
            };
            Graphics::GpuTaskDesc consumerDesc;
            consumerDesc
                .setIdentity(Name("tests/task_graph/provisional_score_future_consumer"))
                .setMarkerLabel("Provisional Score Future Consumer")
                .setQueue(strictComputeRequest)
                .setScheduling(tinyScheduling)
                .setResourceUses(&consumerUse, 1u)
            ;
            futureConsumer = graph.addTask(consumerDesc);
            ASSERT_TRUE(futureConsumer.valid());
        }

        const Graphics::GpuPhysicalQueueInfo queues[] = { GraphicsQueue(), DedicatedComputeQueue() };
        const Graphics::GpuTaskGraphQueueTopology topology{
            .queues = queues,
            .queueCount = LengthOf(queues),
        };
        Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
        ASSERT_TRUE(Analyze(graph, analysis));
        if(addFutureConsumer){
            const Graphics::GpuTaskDependencyEdge* const edge = FindEdge(analysis, movable, futureConsumer);
            ASSERT_NE(edge, nullptr);
        }
        Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
        ASSERT_TRUE(Assign(graph, analysis, topology, assignments));
        const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(movable);
        ASSERT_NE(assignment, nullptr);
        EXPECT_EQ(assignment->queueClass, expectedQueue);
        EXPECT_EQ(
            assignment->reason,
            expectedQueue == Graphics::CommandQueue::Compute
                ? Graphics::GpuTaskQueueAssignmentReason::DedicatedCompute
                : Graphics::GpuTaskQueueAssignmentReason::CompilerOverride
        );
        if(expectedQueue == Graphics::CommandQueue::Compute){
            EXPECT_EQ(assignment->score.preference, 1);
            EXPECT_EQ(assignment->score.overlap, 1);
            EXPECT_EQ(assignment->score.queueLoad, 2);
            EXPECT_EQ(assignment->score.incomingCrossings, 1);
            EXPECT_EQ(assignment->score.outgoingCrossings, 0);
            EXPECT_EQ(assignment->score.ownershipTransfers, 1);
            EXPECT_EQ(assignment->score.total(), -2);
        }
        else{
            EXPECT_EQ(assignment->score.preference, 0);
            EXPECT_EQ(assignment->score.overlap, 1);
            EXPECT_EQ(assignment->score.queueLoad, 2);
            EXPECT_EQ(assignment->score.incomingCrossings, 0);
            EXPECT_EQ(assignment->score.outgoingCrossings, 0);
            EXPECT_EQ(assignment->score.ownershipTransfers, 0);
            EXPECT_EQ(assignment->score.total(), -1);
        }
    };

    runCase(false, Graphics::CommandQueue::Graphics);
    runCase(true, Graphics::CommandQueue::Compute);
}

TEST(GpuTaskGraph, RejectsInvalidAndIncompatibleQueueTopologiesDeterministically){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuQueueRequest computeRequest;
    computeRequest.requiredCapabilities = Graphics::GpuQueueCapability::Compute;
    computeRequest.preferredQueue = Graphics::GpuQueuePreference::Compute;
    const Graphics::GpuTaskId task = AddTaskWithQueue(
        graph,
        Name("tests/task_graph/queue_diagnostic"),
        "Queue Diagnostic",
        computeRequest
    );
    ASSERT_TRUE(task.valid());

    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    ASSERT_TRUE(Analyze(graph, analysis));
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    EXPECT_FALSE(Assign(graph, analysis, Graphics::GpuTaskGraphQueueTopology{}, assignments));
    EXPECT_EQ(
        assignments.diagnostic().status,
        Graphics::GpuTaskGraphQueueAssignmentStatus::InvalidQueueTopology
    );

    const Graphics::GpuPhysicalQueueInfo graphicsOnly[] = {
        GraphicsQueue(0u, Graphics::GpuQueueCapability::Graphics),
    };
    const Graphics::GpuTaskGraphQueueTopology graphicsOnlyTopology{
        .queues = graphicsOnly,
        .queueCount = LengthOf(graphicsOnly),
    };
    EXPECT_FALSE(Assign(graph, analysis, graphicsOnlyTopology, assignments));
    EXPECT_EQ(
        assignments.diagnostic().status,
        Graphics::GpuTaskGraphQueueAssignmentStatus::NoCompatibleQueue
    );
    EXPECT_EQ(assignments.diagnostic().task, task);
    EXPECT_EQ(assignments.diagnostic().requiredCapabilities, Graphics::GpuQueueCapability::Compute);

    Graphics::GpuPhysicalQueueInfo invalidTransferQueue = DedicatedTransferQueue();
    invalidTransferQueue.capabilities = Graphics::GpuQueueCapability::Compute;
    const Graphics::GpuTaskGraphQueueTopology invalidTransferTopology{
        .queues = &invalidTransferQueue,
        .queueCount = 1u,
    };
    EXPECT_FALSE(Assign(graph, analysis, invalidTransferTopology, assignments));
    EXPECT_EQ(
        assignments.diagnostic().status,
        Graphics::GpuTaskGraphQueueAssignmentStatus::InvalidQueueTopology
    );

    Graphics::GpuPhysicalQueueInfo duplicateNativeQueue = GraphicsQueue(3u);
    // Different graph IDs must not alias the same Vulkan family/index transport.
    duplicateNativeQueue.queueIndex = GraphicsQueue().queueIndex;
    const Graphics::GpuPhysicalQueueInfo duplicateNativeQueues[] = {
        GraphicsQueue(),
        duplicateNativeQueue,
    };
    const Graphics::GpuTaskGraphQueueTopology duplicateNativeTopology{
        .queues = duplicateNativeQueues,
        .queueCount = LengthOf(duplicateNativeQueues),
    };
    EXPECT_FALSE(Assign(graph, analysis, duplicateNativeTopology, assignments));
    EXPECT_EQ(
        assignments.diagnostic().status,
        Graphics::GpuTaskGraphQueueAssignmentStatus::InvalidQueueTopology
    );

    const Graphics::GpuPhysicalQueueInfo topologyA[] = { DedicatedComputeQueue(), GraphicsQueue() };
    const Graphics::GpuPhysicalQueueInfo topologyB[] = { GraphicsQueue(), DedicatedComputeQueue() };
    const Graphics::GpuTaskGraphQueueTopology firstTopology{
        .queues = topologyA,
        .queueCount = LengthOf(topologyA),
    };
    const Graphics::GpuTaskGraphQueueTopology secondTopology{
        .queues = topologyB,
        .queueCount = LengthOf(topologyB),
    };
    Graphics::GpuTaskGraphQueueAssignments firstAssignments(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments secondAssignments(testArena.arena);
    ASSERT_TRUE(Assign(graph, analysis, firstTopology, firstAssignments));
    ASSERT_TRUE(Assign(graph, analysis, secondTopology, secondAssignments));
    const Graphics::GpuTaskQueueAssignment* const firstAssignment = firstAssignments.find(task);
    const Graphics::GpuTaskQueueAssignment* const secondAssignment = secondAssignments.find(task);
    ASSERT_NE(firstAssignment, nullptr);
    ASSERT_NE(secondAssignment, nullptr);
    EXPECT_EQ(firstAssignment->queue, secondAssignment->queue);
    EXPECT_EQ(firstAssignment->queueClass, secondAssignment->queueClass);
    EXPECT_EQ(firstAssignment->reason, secondAssignment->reason);
}

TEST(GpuTaskGraph, CompilesTransferPreferenceToGraphicsFallbackWithoutARendererPath){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    u32 acceptedCount = 0u;
    u32 discardedCount = 0u;
    Graphics::QueueSubmissionToken acceptedToken;
    Graphics::GpuTaskDesc desc;
    desc
        .setIdentity(Name("tests/task_graph/transfer_fallback"))
        .setMarkerLabel("Transfer Fallback")
        .setQueue(Graphics::GpuQueueRequest{
            Graphics::GpuQueueCapability::Transfer,
            Graphics::GpuQueuePreference::Transfer,
            true,
            true,
        })
    ;
    const Graphics::GpuTaskId copyTask = graph.addTask<PacketLifecycleTask>(
        desc,
        PacketLifecycleTask::Payload{ &acceptedCount, &discardedCount, &acceptedToken }
    );
    ASSERT_TRUE(copyTask.valid());

    const Graphics::GpuPhysicalQueueInfo graphicsOnly[] = { GraphicsQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = graphicsOnly,
        .queueCount = LengthOf(graphicsOnly),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(copyTask);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(assignment->reason, Graphics::GpuTaskQueueAssignmentReason::Fallback);

    const Graphics::GpuSubmissionPacketId packet = compiledPlan.packetForTask(copyTask);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(compiledPlan.packet(packet).plan->queue, assignment->queue);
    const Graphics::GpuPhysicalQueueInfo* const packetQueue = compiledPlan.queueInfo(compiledPlan.packet(packet).plan->queue);
    ASSERT_NE(packetQueue, nullptr);
    EXPECT_EQ(packetQueue->queueClass, Graphics::CommandQueue::Graphics);
}

TEST(GpuTaskGraph, CompilesEligibleTransferPreferenceToDedicatedTransferQueue){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Graphics::GpuTaskCostHint::Medium;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    Graphics::GpuTaskDesc desc;
    desc
        .setIdentity(Name("tests/task_graph/dedicated_transfer"))
        .setMarkerLabel("Dedicated Transfer")
        .setQueue(Graphics::GpuQueueRequest{
            Graphics::GpuQueueCapability::Transfer,
            Graphics::GpuQueuePreference::Transfer,
            true,
            true,
        })
        .setScheduling(scheduling)
    ;
    const Graphics::GpuTaskId copyTask = graph.addTask<PacketLifecycleTask>(
        desc,
        PacketLifecycleTask::Payload{}
    );
    ASSERT_TRUE(copyTask.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
        DedicatedTransferQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(copyTask);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->queueClass, Graphics::CommandQueue::Transfer);
    EXPECT_TRUE(assignment->dedicated);
    EXPECT_EQ(assignment->reason, Graphics::GpuTaskQueueAssignmentReason::DedicatedTransfer);

    const Graphics::GpuSubmissionPacketId packet = compiledPlan.packetForTask(copyTask);
    ASSERT_TRUE(packet.valid());
    const Graphics::GpuPhysicalQueueInfo* const packetQueue = compiledPlan.queueInfo(compiledPlan.packet(packet).plan->queue);
    ASSERT_NE(packetQueue, nullptr);
    EXPECT_EQ(packetQueue->queueClass, Graphics::CommandQueue::Transfer);
    EXPECT_EQ(packetQueue->capabilities, Graphics::GpuQueueCapability::Transfer);
}

TEST(GpuTaskGraph, RetainsTinyTransferTasksOnGraphics){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);
    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    Graphics::GpuTaskDesc desc;
    desc
        .setIdentity(Name("tests/task_graph/tiny_transfer"))
        .setMarkerLabel("Tiny Transfer")
        .setQueue(Graphics::GpuQueueRequest{
            Graphics::GpuQueueCapability::Transfer,
            Graphics::GpuQueuePreference::Transfer,
            true,
            true,
        })
        .setScheduling(scheduling)
    ;
    const Graphics::GpuTaskId copyTask = graph.addTask<PacketLifecycleTask>(
        desc,
        PacketLifecycleTask::Payload{}
    );
    ASSERT_TRUE(copyTask.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedTransferQueue(),
    };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));

    const Graphics::GpuTaskQueueAssignment* const assignment = assignments.find(copyTask);
    ASSERT_NE(assignment, nullptr);
    EXPECT_EQ(assignment->queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(assignment->reason, Graphics::GpuTaskQueueAssignmentReason::CompilerOverride);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

