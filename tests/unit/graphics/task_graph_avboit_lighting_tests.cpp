// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_avboit_lighting_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, RoutesLaggedLightingAlongsideAvboit){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    const auto importTexture = [&](
        const Name& identity,
        const AStringView label,
        const Graphics::ResourceStates::Mask initialState,
        const Graphics::ResourceQueueSharing::Mask queueSharing = Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute
    ){
        Graphics::GpuGraphResourceDesc desc;
        desc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(initialState)
            .setQueueSharing(queueSharing)
        ;
        return graph.importResource(desc);
    };
    const auto importBuffer = [&](const Name& identity, const AStringView label, const Graphics::ResourceStates::Mask initialState){
        Graphics::GpuGraphResourceDesc desc;
        desc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setType(Graphics::GpuGraphResourceType::Buffer)
            .setInitialState(initialState)
            .setQueueSharing(Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute)
        ;
        return graph.importResource(desc);
    };
    const Graphics::GpuGraphResourceId sharedPrefixRead = importTexture(
        Name("tests/task_graph/lagged_shared_prefix_read"),
        "Shared Prefix Read",
        Graphics::ResourceStates::ShaderResource
    );
    const Graphics::GpuGraphResourceId currentBindlessSlots = importBuffer(
        Name("tests/task_graph/lagged_current_bindless_slots"),
        "Current Bindless Slots",
        Graphics::ResourceStates::Common
    );
    const Graphics::GpuGraphResourceId historyBindlessSlots = importBuffer(
        Name("tests/task_graph/lagged_history_bindless_slots"),
        "Lagged History Bindless Slots",
        Graphics::ResourceStates::Common
    );
    const Graphics::GpuGraphResourceId historyIrradiance = importTexture(
        Name("tests/task_graph/lagged_history_irradiance"),
        "Lagged Irradiance",
        Graphics::ResourceStates::ShaderResource,
        Graphics::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer
    );
    const Graphics::GpuGraphResourceId currentIrradiance = importTexture(
        Name("tests/task_graph/lagged_current_irradiance"),
        "Current Irradiance",
        Graphics::ResourceStates::CopySource,
        Graphics::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer
    );
    const Graphics::GpuGraphResourceId opaqueColor = importTexture(
        Name("tests/task_graph/lagged_opaque_color"),
        "Opaque Color",
        Graphics::ResourceStates::Common
    );
    const Graphics::GpuGraphResourceId avboitAccumulation = importTexture(
        Name("tests/task_graph/lagged_avboit_accumulation"),
        "AVBOIT Accumulation",
        Graphics::ResourceStates::Common
    );
    const Graphics::GpuGraphResourceId compositeColor = importTexture(
        Name("tests/task_graph/lagged_composite_color"),
        "Composite Color",
        Graphics::ResourceStates::Common
    );
    const Graphics::GpuGraphResourceId backbuffer = AddHazardDomain(
        graph,
        Name("tests/task_graph/lagged_backbuffer"),
        "Back Buffer"
    );
    ASSERT_TRUE(sharedPrefixRead.valid());
    ASSERT_TRUE(currentBindlessSlots.valid());
    ASSERT_TRUE(historyBindlessSlots.valid());
    ASSERT_TRUE(historyIrradiance.valid());
    ASSERT_TRUE(currentIrradiance.valid());
    ASSERT_TRUE(opaqueColor.valid());
    ASSERT_TRUE(avboitAccumulation.valid());
    ASSERT_TRUE(compositeColor.valid());
    ASSERT_TRUE(backbuffer.valid());

    const Graphics::GpuExternalCompletionId historyCompletion = graph.importExternalCompletion(
        Graphics::GpuExternalCompletionDesc{}
            .setIdentity(Name("tests/task_graph/lagged_history_complete"))
            .setMarkerLabel("Lagged History Complete")
    );
    ASSERT_TRUE(historyCompletion.valid());

    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Graphics::GpuTaskCostHint::Large;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    const Graphics::GpuQueueRequest computeRequest{
        Graphics::GpuQueueCapability::Compute,
        Graphics::GpuQueuePreference::Compute,
        true,
        true,
    };
    const Graphics::GpuQueueRequest computeUploadRequest{
        Graphics::GpuQueueCapability::Transfer,
        Graphics::GpuQueuePreference::Compute,
        true,
        false,
    };
    const Graphics::GpuQueueRequest transferRequest{
        Graphics::GpuQueueCapability::Transfer,
        Graphics::GpuQueuePreference::Transfer,
        true,
        true,
    };
    const Graphics::GpuTaskResourceUse shadowPrepareUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = currentBindlessSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    Graphics::GpuTaskDesc shadowPrepareDesc;
    shadowPrepareDesc
        .setIdentity(Name("tests/task_graph/lagged_shadow_prepare"))
        .setMarkerLabel("Shadow Prepare")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setResourceUses(shadowPrepareUses, LengthOf(shadowPrepareUses))
    ;
    const Graphics::GpuTaskId shadowPrepare = graph.addTask(shadowPrepareDesc);
    ASSERT_TRUE(shadowPrepare.valid());
    const Graphics::GpuTaskResourceUse prefixUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = sharedPrefixRead,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = currentBindlessSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskDesc prefixDesc;
    prefixDesc
        .setIdentity(Name("tests/task_graph/lagged_graphics_prefix"))
        .setMarkerLabel("Graphics Prefix")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setDependencies(&shadowPrepare, 1u)
        .setResourceUses(prefixUses, LengthOf(prefixUses))
    ;
    const Graphics::GpuTaskId prefix = graph.addTask(prefixDesc);
    ASSERT_TRUE(prefix.valid());
    const Graphics::GpuTaskResourceUse shadowVisibilityUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = sharedPrefixRead,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = currentBindlessSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskDesc shadowVisibilityDesc;
    shadowVisibilityDesc
        .setIdentity(Name("tests/task_graph/lagged_shadow_visibility"))
        .setMarkerLabel("Shadow Visibility")
        .setQueue(computeRequest)
        .setScheduling(scheduling)
        .setDependencies(&prefix, 1u)
        .setExternalDependencies(&historyCompletion, 1u)
        .setResourceUses(shadowVisibilityUses, LengthOf(shadowVisibilityUses))
    ;
    const Graphics::GpuTaskId shadowVisibility = graph.addTask(shadowVisibilityDesc);
    ASSERT_TRUE(shadowVisibility.valid());
    const Graphics::GpuTaskResourceUse surfelGiUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = sharedPrefixRead,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = currentIrradiance,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskId surfelGiDependencies[] = { shadowVisibility };
    Graphics::GpuTaskDesc surfelGiDesc;
    surfelGiDesc
        .setIdentity(Name("tests/task_graph/lagged_surfel_gi"))
        .setMarkerLabel("Surfel GI")
        .setQueue(computeRequest)
        .setScheduling(scheduling)
        .setDependencies(surfelGiDependencies, LengthOf(surfelGiDependencies))
        .setResourceUses(surfelGiUses, LengthOf(surfelGiUses))
    ;
    const Graphics::GpuTaskId surfelGi = graph.addTask(surfelGiDesc);
    ASSERT_TRUE(surfelGi.valid());
    const Graphics::GpuTaskResourceUse hardwareUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = sharedPrefixRead,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
            .hasIndependentStateSource = true,
        },
    };
    Graphics::GpuTaskDesc hardwareDesc;
    hardwareDesc
        .setIdentity(Name("tests/task_graph/lagged_hardware_caustics"))
        .setMarkerLabel("Hardware Caustics")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setDependencies(&prefix, 1u)
        .setExternalDependencies(&historyCompletion, 1u)
        .setResourceUses(hardwareUses, LengthOf(hardwareUses))
    ;
    const Graphics::GpuTaskId hardware = graph.addTask(hardwareDesc);
    ASSERT_TRUE(hardware.valid());

    const Graphics::GpuTaskResourceUse avboitPreUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = sharedPrefixRead,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
            .hasIndependentStateSource = true,
        },
        Graphics::GpuTaskResourceUse{
            .resource = avboitAccumulation,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc avboitPreDesc;
    avboitPreDesc
        .setIdentity(Name("tests/task_graph/lagged_avboit_pre"))
        .setMarkerLabel("AVBOIT Pre")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setDependencies(&prefix, 1u)
        .setResourceUses(avboitPreUses, LengthOf(avboitPreUses))
    ;
    const Graphics::GpuTaskId avboitPre = graph.addTask(avboitPreDesc);
    ASSERT_TRUE(avboitPre.valid());

    const Graphics::GpuTaskResourceUse laggedHistorySlotsUploadUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = historyBindlessSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::Common,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskSchedulingHint laggedHistorySlotsUploadScheduling;
    laggedHistorySlotsUploadScheduling.cost = Graphics::GpuTaskCostHint::Tiny;
    laggedHistorySlotsUploadScheduling.forceSubmissionBoundary = false;
    laggedHistorySlotsUploadScheduling.allowPacketMerge = true;
    Graphics::GpuTaskDesc laggedHistorySlotsUploadDesc;
    laggedHistorySlotsUploadDesc
        .setIdentity(Name("tests/task_graph/lagged_history_bindless_slots_upload"))
        .setMarkerLabel("Lagged Lighting Bindless Slots Upload")
        .setQueue(computeUploadRequest)
        .setScheduling(laggedHistorySlotsUploadScheduling)
        .setDependencies(&prefix, 1u)
        .setResourceUses(laggedHistorySlotsUploadUses, LengthOf(laggedHistorySlotsUploadUses))
    ;
    const Graphics::GpuTaskId laggedHistorySlotsUpload = graph.addTask(laggedHistorySlotsUploadDesc);
    ASSERT_TRUE(laggedHistorySlotsUpload.valid());

    const Graphics::GpuTaskResourceUse lightingUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = sharedPrefixRead,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
            .hasIndependentStateSource = true,
        },
        Graphics::GpuTaskResourceUse{
            .resource = historyIrradiance,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = historyBindlessSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = opaqueColor,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskSchedulingHint lightingScheduling;
    lightingScheduling.cost = Graphics::GpuTaskCostHint::Large;
    lightingScheduling.forceSubmissionBoundary = false;
    lightingScheduling.allowPacketMerge = true;
    lightingScheduling.mergeWithPrevious = true;
    const Graphics::GpuTaskId lightingDependencies[] = {
        prefix,
        laggedHistorySlotsUpload,
    };
    Graphics::GpuTaskDesc lightingDesc;
    lightingDesc
        .setIdentity(Name("tests/task_graph/lagged_deferred_lighting"))
        .setMarkerLabel("Deferred Lighting")
        .setQueue(computeRequest)
        .setScheduling(lightingScheduling)
        .setDependencies(lightingDependencies, LengthOf(lightingDependencies))
        .setExternalDependencies(&historyCompletion, 1u)
        .setResourceUses(lightingUses, LengthOf(lightingUses))
    ;
    const Graphics::GpuTaskId lighting = graph.addTask(lightingDesc);
    ASSERT_TRUE(lighting.valid());

    const Graphics::GpuTaskResourceUse compositeUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = opaqueColor,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = avboitAccumulation,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = compositeColor,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskId compositeTaskDependencies[] = {
        lighting,
        avboitPre,
    };
    Graphics::GpuTaskDesc compositeDesc;
    compositeDesc
        .setIdentity(Name("tests/task_graph/lagged_deferred_composite"))
        .setMarkerLabel("Deferred Composite")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setDependencies(compositeTaskDependencies, LengthOf(compositeTaskDependencies))
        .setResourceUses(compositeUses, LengthOf(compositeUses))
    ;
    const Graphics::GpuTaskId composite = graph.addTask(compositeDesc);
    ASSERT_TRUE(composite.valid());

    const Graphics::GpuTaskResourceUse presentUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = compositeColor,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = backbuffer,
            .range = {},
            .requiredState = Graphics::ResourceStates::Present,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskId presentDependencies[] = {
        composite,
        surfelGi,
    };
    Graphics::GpuTaskDesc presentDesc;
    presentDesc
        .setIdentity(Name("tests/task_graph/lagged_deferred_present"))
        .setMarkerLabel("Deferred Present")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setDependencies(presentDependencies, LengthOf(presentDependencies))
        .setResourceUses(presentUses, LengthOf(presentUses))
    ;
    const Graphics::GpuTaskId present = graph.addTask(presentDesc);
    ASSERT_TRUE(present.valid());

    const Graphics::GpuTaskResourceUse historyCopyUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = currentIrradiance,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopySource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = historyIrradiance,
            .range = {},
            .requiredState = Graphics::ResourceStates::CopyDest,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskId historyCopyDependencies[] = { present };
    Graphics::GpuTaskDesc historyCopyDesc;
    historyCopyDesc
        .setIdentity(Name("tests/task_graph/lagged_history_copy"))
        .setMarkerLabel("Lagged Lighting History Copy")
        .setQueue(transferRequest)
        .setScheduling(scheduling)
        .setDependencies(historyCopyDependencies, LengthOf(historyCopyDependencies))
        .setResourceUses(historyCopyUses, LengthOf(historyCopyUses))
    ;
    const Graphics::GpuTaskId historyCopy = graph.addTask(historyCopyDesc);
    ASSERT_TRUE(historyCopy.valid());

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
    Graphics::GpuCompiledGraph compiledGraphStorage(testArena.arena);
    Graphics::GpuTaskGraphCompileOptions frontierOptions;
    frontierOptions.packetizationPolicy = Graphics::GpuTaskGraphPacketizationPolicy::FrontierSafe;
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraphStorage, frontierOptions));

    {
        const GpuTaskGraphReadViews views(graph, compiledGraphStorage);
        ASSERT_TRUE(views.valid());
        const Graphics::GpuCompiledGraph::ReadView& compiledGraph = views.compiled;

        const Graphics::GpuTaskQueueAssignment* const shadowPrepareAssignment = assignments.find(shadowPrepare);
        const Graphics::GpuTaskQueueAssignment* const prefixAssignment = assignments.find(prefix);
        const Graphics::GpuTaskQueueAssignment* const shadowVisibilityAssignment = assignments.find(shadowVisibility);
        const Graphics::GpuTaskQueueAssignment* const surfelGiAssignment = assignments.find(surfelGi);
        const Graphics::GpuTaskQueueAssignment* const hardwareAssignment = assignments.find(hardware);
        const Graphics::GpuTaskQueueAssignment* const avboitPreAssignment = assignments.find(avboitPre);
        const Graphics::GpuTaskQueueAssignment* const laggedHistorySlotsUploadAssignment = assignments.find(laggedHistorySlotsUpload);
        const Graphics::GpuTaskQueueAssignment* const lightingAssignment = assignments.find(lighting);
        const Graphics::GpuTaskQueueAssignment* const compositeAssignment = assignments.find(composite);
        const Graphics::GpuTaskQueueAssignment* const presentAssignment = assignments.find(present);
        const Graphics::GpuTaskQueueAssignment* const historyCopyAssignment = assignments.find(historyCopy);
        ASSERT_NE(shadowPrepareAssignment, nullptr);
        ASSERT_NE(prefixAssignment, nullptr);
        ASSERT_NE(shadowVisibilityAssignment, nullptr);
        ASSERT_NE(surfelGiAssignment, nullptr);
        ASSERT_NE(hardwareAssignment, nullptr);
        ASSERT_NE(avboitPreAssignment, nullptr);
        ASSERT_NE(laggedHistorySlotsUploadAssignment, nullptr);
        ASSERT_NE(lightingAssignment, nullptr);
        ASSERT_NE(compositeAssignment, nullptr);
        ASSERT_NE(presentAssignment, nullptr);
        ASSERT_NE(historyCopyAssignment, nullptr);
        EXPECT_EQ(shadowPrepareAssignment->queueClass, Graphics::CommandQueue::Graphics);
        EXPECT_EQ(prefixAssignment->queueClass, Graphics::CommandQueue::Graphics);
        EXPECT_EQ(shadowVisibilityAssignment->queueClass, Graphics::CommandQueue::Compute);
        EXPECT_EQ(surfelGiAssignment->queueClass, Graphics::CommandQueue::Compute);
        EXPECT_EQ(hardwareAssignment->queueClass, Graphics::CommandQueue::Graphics);
        EXPECT_EQ(avboitPreAssignment->queueClass, Graphics::CommandQueue::Graphics);
        EXPECT_EQ(laggedHistorySlotsUploadAssignment->queueClass, Graphics::CommandQueue::Compute);
        EXPECT_EQ(lightingAssignment->queueClass, Graphics::CommandQueue::Compute);
        EXPECT_EQ(lightingAssignment->reason, Graphics::GpuTaskQueueAssignmentReason::DedicatedCompute);
        EXPECT_EQ(compositeAssignment->queueClass, Graphics::CommandQueue::Graphics);
        EXPECT_EQ(presentAssignment->queueClass, Graphics::CommandQueue::Graphics);
        EXPECT_EQ(historyCopyAssignment->queueClass, Graphics::CommandQueue::Transfer);
        EXPECT_EQ(historyCopyAssignment->reason, Graphics::GpuTaskQueueAssignmentReason::DedicatedTransfer);
        EXPECT_EQ(historyCopyAssignment->queue, queues[2u].id);

        const Graphics::GpuSubmissionPacketId shadowPreparePacket = compiledGraph.packetForTask(shadowPrepare);
        const Graphics::GpuSubmissionPacketId prefixPacket = compiledGraph.packetForTask(prefix);
        const Graphics::GpuSubmissionPacketId shadowVisibilityPacket = compiledGraph.packetForTask(shadowVisibility);
        const Graphics::GpuSubmissionPacketId surfelGiPacket = compiledGraph.packetForTask(surfelGi);
        const Graphics::GpuSubmissionPacketId hardwarePacket = compiledGraph.packetForTask(hardware);
        const Graphics::GpuSubmissionPacketId avboitPrePacket = compiledGraph.packetForTask(avboitPre);
        const Graphics::GpuSubmissionPacketId laggedHistorySlotsUploadPacket = compiledGraph.packetForTask(laggedHistorySlotsUpload);
        const Graphics::GpuSubmissionPacketId lightingPacket = compiledGraph.packetForTask(lighting);
        const Graphics::GpuSubmissionPacketId compositePacket = compiledGraph.packetForTask(composite);
        const Graphics::GpuSubmissionPacketId presentPacket = compiledGraph.packetForTask(present);
        const Graphics::GpuSubmissionPacketId historyCopyPacket = compiledGraph.packetForTask(historyCopy);
        ASSERT_TRUE(shadowPreparePacket.valid());
        ASSERT_TRUE(prefixPacket.valid());
        ASSERT_TRUE(shadowVisibilityPacket.valid());
        ASSERT_TRUE(surfelGiPacket.valid());
        ASSERT_TRUE(hardwarePacket.valid());
        ASSERT_TRUE(avboitPrePacket.valid());
        ASSERT_TRUE(laggedHistorySlotsUploadPacket.valid());
        ASSERT_TRUE(lightingPacket.valid());
        ASSERT_TRUE(compositePacket.valid());
        ASSERT_TRUE(presentPacket.valid());
        ASSERT_TRUE(historyCopyPacket.valid());
        ASSERT_EQ(compiledGraph.packetCount(), 10u);
        EXPECT_EQ(compiledGraph.packetIdAt(0u), shadowPreparePacket);
        EXPECT_EQ(compiledGraph.packetIdAt(1u), prefixPacket);
        EXPECT_EQ(compiledGraph.packetIdAt(2u), shadowVisibilityPacket);
        EXPECT_EQ(compiledGraph.packetIdAt(3u), surfelGiPacket);
        EXPECT_EQ(compiledGraph.packetIdAt(4u), hardwarePacket);
        EXPECT_EQ(compiledGraph.packetIdAt(5u), avboitPrePacket);
        EXPECT_EQ(compiledGraph.packetIdAt(6u), lightingPacket);
        EXPECT_EQ(compiledGraph.packetIdAt(7u), compositePacket);
        EXPECT_EQ(compiledGraph.packetIdAt(8u), presentPacket);
        EXPECT_EQ(compiledGraph.packetIdAt(9u), historyCopyPacket);
        EXPECT_EQ(laggedHistorySlotsUploadPacket, lightingPacket);
        EXPECT_EQ(compiledGraph.packet(lightingPacket).plan->taskCount, 2u);
        const Graphics::GpuCompiledTask* const compiledShadowPrepare = compiledGraph.findTask(shadowPrepare).plan;
        const Graphics::GpuCompiledTask* const compiledPrefix = compiledGraph.findTask(prefix).plan;
        ASSERT_NE(compiledShadowPrepare, nullptr);
        ASSERT_NE(compiledPrefix, nullptr);
        ASSERT_EQ(compiledShadowPrepare->prologueBarrierCount, 1u);
        const Graphics::GpuCompiledBarrier* const shadowPrepareBarrier = compiledGraph.findTask(shadowPrepare).prologueBarriers;
        ASSERT_NE(shadowPrepareBarrier, nullptr);
        EXPECT_EQ(shadowPrepareBarrier[0u].type, Graphics::GpuCompiledBarrierType::BufferTransition);
        EXPECT_EQ(shadowPrepareBarrier[0u].resource, currentBindlessSlots);
        EXPECT_EQ(shadowPrepareBarrier[0u].before, Graphics::ResourceStates::Common);
        EXPECT_EQ(shadowPrepareBarrier[0u].after, Graphics::ResourceStates::ConstantBuffer);
        ASSERT_EQ(compiledPrefix->prologueStateSeedCount, 1u);
        const Graphics::GpuPacketStateSeed* const prefixSeeds = compiledGraph.findTask(prefix).prologueStateSeeds;
        ASSERT_NE(prefixSeeds, nullptr);
        EXPECT_EQ(prefixSeeds[0u].resource, currentBindlessSlots);
        EXPECT_EQ(prefixSeeds[0u].sourcePacket, shadowPreparePacket);
        ASSERT_EQ(compiledGraph.packet(prefixPacket).plan->dependencyCount, 1u);
        const Graphics::GpuPacketDependency* const prefixPacketDependencies = compiledGraph.packet(prefixPacket).dependencies;
        ASSERT_NE(prefixPacketDependencies, nullptr);
        EXPECT_EQ(prefixPacketDependencies[0u].producer, shadowPreparePacket);
        EXPECT_EQ(FindEdge(analysis, avboitPre, lighting), nullptr);
        EXPECT_EQ(FindEdge(analysis, surfelGi, lighting), nullptr);
        const Graphics::GpuCompiledTask* const compiledLighting = compiledGraph.findTask(lighting).plan;
        ASSERT_NE(compiledLighting, nullptr);
        bool lightingTransitionsLaggedHistorySlots = false;
        const Graphics::GpuCompiledBarrier* const lightingBarriers = compiledGraph.findTask(lighting).prologueBarriers;
        for(usize index = 0u; index < compiledLighting->prologueBarrierCount; ++index){
            lightingTransitionsLaggedHistorySlots = lightingTransitionsLaggedHistorySlots
                || (
                    lightingBarriers[index].type == Graphics::GpuCompiledBarrierType::BufferTransition
                    && lightingBarriers[index].resource == historyBindlessSlots
                    && lightingBarriers[index].before == Graphics::ResourceStates::Common
                    && lightingBarriers[index].after == Graphics::ResourceStates::ConstantBuffer
                )
            ;
        }
        EXPECT_TRUE(lightingTransitionsLaggedHistorySlots);
        ASSERT_EQ(compiledGraph.packet(lightingPacket).plan->dependencyCount, 1u);
        const Graphics::GpuPacketDependency* const lightingPacketDependencies = compiledGraph.packet(lightingPacket).dependencies;
        ASSERT_NE(lightingPacketDependencies, nullptr);
        EXPECT_EQ(lightingPacketDependencies[0u].producer, prefixPacket);
        ASSERT_EQ(compiledGraph.packet(lightingPacket).plan->externalDependencyCount, 1u);
        const Graphics::GpuExternalCompletionId* const lightingExternalDependencies = compiledGraph.packet(
            lightingPacket
        ).externalDependencies;
        ASSERT_NE(lightingExternalDependencies, nullptr);
        EXPECT_EQ(lightingExternalDependencies[0u], historyCompletion);

        const Graphics::GpuCompiledTask* const compiledShadowVisibility = compiledGraph.findTask(shadowVisibility).plan;
        ASSERT_NE(compiledShadowVisibility, nullptr);
        ASSERT_GT(compiledShadowVisibility->prologueStateSeedCount, 0u);
        const Graphics::GpuPacketStateSeed* const shadowVisibilitySeeds = compiledGraph.findTask(
            shadowVisibility
        ).prologueStateSeeds;
        ASSERT_NE(shadowVisibilitySeeds, nullptr);
        bool shadowVisibilityImportsBindlessSlotsState = false;
        for(usize index = 0u; index < compiledShadowVisibility->prologueStateSeedCount; ++index){
            shadowVisibilityImportsBindlessSlotsState = shadowVisibilityImportsBindlessSlotsState
                || (
                    shadowVisibilitySeeds[index].resource == currentBindlessSlots
                    && shadowVisibilitySeeds[index].sourcePacket == prefixPacket
                )
            ;
        }
        EXPECT_TRUE(shadowVisibilityImportsBindlessSlotsState);
        ASSERT_EQ(compiledGraph.packet(shadowVisibilityPacket).plan->externalDependencyCount, 1u);
        const Graphics::GpuExternalCompletionId* const shadowVisibilityExternalDependencies = compiledGraph.packet(
            shadowVisibilityPacket
        ).externalDependencies;
        ASSERT_NE(shadowVisibilityExternalDependencies, nullptr);
        EXPECT_EQ(shadowVisibilityExternalDependencies[0u], historyCompletion);
        EXPECT_NE(FindEdge(analysis, shadowPrepare, shadowVisibility), nullptr);
        ASSERT_EQ(compiledGraph.packet(shadowVisibilityPacket).plan->dependencyCount, 1u);
        const Graphics::GpuPacketDependency* const shadowVisibilityPacketDependencies = compiledGraph.packet(
            shadowVisibilityPacket
        ).dependencies;
        ASSERT_NE(shadowVisibilityPacketDependencies, nullptr);
        EXPECT_EQ(shadowVisibilityPacketDependencies[0u].producer, prefixPacket);
        ASSERT_EQ(compiledGraph.packet(hardwarePacket).plan->externalDependencyCount, 1u);
        const Graphics::GpuExternalCompletionId* const hardwareExternalDependencies = compiledGraph.packet(
            hardwarePacket
        ).externalDependencies;
        ASSERT_NE(hardwareExternalDependencies, nullptr);
        EXPECT_EQ(hardwareExternalDependencies[0u], historyCompletion);
        ASSERT_EQ(compiledGraph.packet(surfelGiPacket).plan->externalDependencyCount, 0u);
        ASSERT_GE(compiledGraph.packet(surfelGiPacket).plan->dependencyCount, 1u);
        const Graphics::GpuPacketDependency* const surfelGiPacketDependencies = compiledGraph.packet(
            surfelGiPacket
        ).dependencies;
        ASSERT_NE(surfelGiPacketDependencies, nullptr);
        bool surfelGiWaitsForShadowVisibility = false;
        for(usize index = 0u; index < compiledGraph.packet(surfelGiPacket).plan->dependencyCount; ++index){
            surfelGiWaitsForShadowVisibility = surfelGiWaitsForShadowVisibility
                || surfelGiPacketDependencies[index].producer == shadowVisibilityPacket
            ;
        }
        EXPECT_TRUE(surfelGiWaitsForShadowVisibility);

        EXPECT_EQ(compiledGraph.packet(avboitPrePacket).plan->externalDependencyCount, 0u);

        ASSERT_EQ(compiledGraph.packet(compositePacket).plan->dependencyCount, 2u);
        const Graphics::GpuPacketDependency* const compositePacketDependencies = compiledGraph.packet(compositePacket).dependencies;
        ASSERT_NE(compositePacketDependencies, nullptr);
        bool compositeWaitsForLighting = false;
        bool compositeWaitsForAvboit = false;
        for(usize index = 0u; index < compiledGraph.packet(compositePacket).plan->dependencyCount; ++index){
            compositeWaitsForLighting = compositeWaitsForLighting
                || compositePacketDependencies[index].producer == lightingPacket
            ;
            compositeWaitsForAvboit = compositeWaitsForAvboit
                || compositePacketDependencies[index].producer == avboitPrePacket
            ;
        }
        EXPECT_TRUE(compositeWaitsForLighting);
        EXPECT_TRUE(compositeWaitsForAvboit);
        EXPECT_EQ(compiledGraph.packet(compositePacket).plan->externalDependencyCount, 0u);

        const Graphics::GpuCompiledTask* const compiledPresent = compiledGraph.findTask(present).plan;
        ASSERT_NE(compiledPresent, nullptr);
        ASSERT_EQ(compiledPresent->prologueStateSeedCount, 1u);
        const Graphics::GpuPacketStateSeed* const presentSeed = compiledGraph.findTask(present).prologueStateSeeds;
        ASSERT_NE(presentSeed, nullptr);
        EXPECT_EQ(presentSeed[0u].resource, compositeColor);
        EXPECT_EQ(presentSeed[0u].sourcePacket, compositePacket);
        ASSERT_EQ(compiledGraph.packet(presentPacket).plan->dependencyCount, 2u);
        const Graphics::GpuPacketDependency* const presentPacketDependencies = compiledGraph.packet(presentPacket).dependencies;
        ASSERT_NE(presentPacketDependencies, nullptr);
        bool presentWaitsForComposite = false;
        bool presentWaitsForSurfelGi = false;
        for(usize index = 0u; index < compiledGraph.packet(presentPacket).plan->dependencyCount; ++index){
            presentWaitsForComposite = presentWaitsForComposite
                || presentPacketDependencies[index].producer == compositePacket
            ;
            presentWaitsForSurfelGi = presentWaitsForSurfelGi
                || presentPacketDependencies[index].producer == surfelGiPacket
            ;
        }
        EXPECT_TRUE(presentWaitsForComposite);
        EXPECT_TRUE(presentWaitsForSurfelGi);
        EXPECT_EQ(compiledGraph.packet(presentPacket).plan->externalDependencyCount, 0u);

        const Graphics::GpuPacketDependency* const historyCopyPacketDependencies = compiledGraph.packet(
            historyCopyPacket
        ).dependencies;
        ASSERT_NE(historyCopyPacketDependencies, nullptr);
        bool historyCopyWaitsForPresent = false;
        for(usize index = 0u; index < compiledGraph.packet(historyCopyPacket).plan->dependencyCount; ++index){
            historyCopyWaitsForPresent = historyCopyWaitsForPresent
                || historyCopyPacketDependencies[index].producer == presentPacket
            ;
        }
        EXPECT_TRUE(historyCopyWaitsForPresent);
        EXPECT_EQ(compiledGraph.packet(historyCopyPacket).plan->externalDependencyCount, 0u);
        const Graphics::GpuCompiledTask* const compiledHistoryCopy = compiledGraph.findTask(historyCopy).plan;
        ASSERT_NE(compiledHistoryCopy, nullptr);
        const Graphics::GpuCompiledBarrier* const historyCopyBarriers = compiledGraph.findTask(historyCopy).prologueBarriers;
        ASSERT_NE(historyCopyBarriers, nullptr);
        bool historyCopyTransitionsCurrentIrradiance = false;
        bool historyCopyTransitionsHistoryIrradiance = false;
        for(usize index = 0u; index < compiledHistoryCopy->prologueBarrierCount; ++index){
            const Graphics::GpuCompiledBarrier& barrier = historyCopyBarriers[index];
            historyCopyTransitionsCurrentIrradiance = historyCopyTransitionsCurrentIrradiance
                || (
                    barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
                    && barrier.resource == currentIrradiance
                    && barrier.before == Graphics::ResourceStates::UnorderedAccess
                    && barrier.after == Graphics::ResourceStates::CopySource
                )
            ;
            historyCopyTransitionsHistoryIrradiance = historyCopyTransitionsHistoryIrradiance
                || (
                    barrier.type == Graphics::GpuCompiledBarrierType::TextureTransition
                    && barrier.resource == historyIrradiance
                    && barrier.before == Graphics::ResourceStates::ShaderResource
                    && barrier.after == Graphics::ResourceStates::CopyDest
                )
            ;
        }
        EXPECT_TRUE(historyCopyTransitionsCurrentIrradiance);
        EXPECT_TRUE(historyCopyTransitionsHistoryIrradiance);
    }

    const Graphics::GpuPhysicalQueueInfo graphicsOnlyQueue = GraphicsQueue();
    const Graphics::GpuTaskGraphQueueTopology graphicsOnlyTopology{
        .queues = &graphicsOnlyQueue,
        .queueCount = 1u,
    };
    Graphics::GpuTaskGraphAnalysis graphicsOnlyAnalysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments graphicsOnlyAssignments(testArena.arena);
    Graphics::GpuCompiledGraph graphicsOnlyCompiledGraphStorage(testArena.arena);
    ASSERT_TRUE(Compile(
        graph,
        graphicsOnlyAnalysis,
        graphicsOnlyTopology,
        graphicsOnlyAssignments,
        graphicsOnlyCompiledGraphStorage,
        frontierOptions
    ));
    const GpuTaskGraphReadViews graphicsOnlyViews(graph, graphicsOnlyCompiledGraphStorage);
    ASSERT_TRUE(graphicsOnlyViews.valid());
    const Graphics::GpuCompiledGraph::ReadView& graphicsOnlyCompiledGraph = graphicsOnlyViews.compiled;
    const Graphics::GpuTaskQueueAssignment* const graphicsUploadAssignment = graphicsOnlyAssignments.find(
        laggedHistorySlotsUpload
    );
    const Graphics::GpuTaskQueueAssignment* const graphicsLightingAssignment = graphicsOnlyAssignments.find(lighting);
    ASSERT_NE(graphicsUploadAssignment, nullptr);
    ASSERT_NE(graphicsLightingAssignment, nullptr);
    EXPECT_EQ(graphicsUploadAssignment->queue, graphicsOnlyQueue.id);
    EXPECT_EQ(graphicsUploadAssignment->queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(graphicsLightingAssignment->queue, graphicsOnlyQueue.id);
    EXPECT_EQ(graphicsLightingAssignment->queueClass, Graphics::CommandQueue::Graphics);
    const Graphics::GpuSubmissionPacketId graphicsUploadPacket = graphicsOnlyCompiledGraph.packetForTask(
        laggedHistorySlotsUpload
    );
    const Graphics::GpuSubmissionPacketId graphicsLightingPacket = graphicsOnlyCompiledGraph.packetForTask(lighting);
    ASSERT_TRUE(graphicsUploadPacket.valid());
    ASSERT_TRUE(graphicsLightingPacket.valid());
    EXPECT_EQ(graphicsUploadPacket, graphicsLightingPacket);
    EXPECT_EQ(graphicsOnlyCompiledGraph.packet(graphicsLightingPacket).plan->taskCount, 2u);
}

TEST(GpuTaskGraph, RoutesLiveAvboitBeforeDeferredLighting){
    TestArena testArena;
    Graphics::GpuTaskGraph graph(testArena.arena);

    const auto importTexture = [&](const Name& identity, const AStringView label, const Graphics::ResourceStates::Mask initialState){
        Graphics::GpuGraphResourceDesc desc;
        desc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(initialState)
            .setQueueSharing(Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute)
        ;
        return graph.importResource(desc);
    };
    const auto importBuffer = [&](const Name& identity, const AStringView label, const Graphics::ResourceStates::Mask initialState){
        Graphics::GpuGraphResourceDesc desc;
        desc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setType(Graphics::GpuGraphResourceType::Buffer)
            .setInitialState(initialState)
            .setQueueSharing(Graphics::ResourceQueueSharing::GraphicsAndAsyncCompute)
        ;
        return graph.importResource(desc);
    };
    const Graphics::GpuGraphResourceId sharedPrefixRead = importTexture(
        Name("tests/task_graph/live_shared_prefix_read"),
        "Shared Prefix Read",
        Graphics::ResourceStates::ShaderResource
    );
    const Graphics::GpuGraphResourceId currentBindlessSlots = importBuffer(
        Name("tests/task_graph/live_current_bindless_slots"),
        "Current Bindless Slots",
        Graphics::ResourceStates::Common
    );
    const Graphics::GpuGraphResourceId currentIrradiance = importTexture(
        Name("tests/task_graph/live_current_irradiance"),
        "Current Surfel Irradiance",
        Graphics::ResourceStates::Common
    );
    const Graphics::GpuGraphResourceId currentShadowVisibility = importTexture(
        Name("tests/task_graph/live_current_shadow_visibility"),
        "Current Shadow Visibility",
        Graphics::ResourceStates::Common
    );
    const Graphics::GpuGraphResourceId currentCausticIrradiance = importTexture(
        Name("tests/task_graph/live_current_caustic_irradiance"),
        "Current Caustic Irradiance",
        Graphics::ResourceStates::Common
    );
    const Graphics::GpuGraphResourceId avboitWorking = importTexture(
        Name("tests/task_graph/live_avboit_working"),
        "AVBOIT Working",
        Graphics::ResourceStates::Common
    );
    const Graphics::GpuGraphResourceId avboitAccumulation = importTexture(
        Name("tests/task_graph/live_avboit_accumulation"),
        "AVBOIT Accumulation",
        Graphics::ResourceStates::Common
    );
    const Graphics::GpuGraphResourceId opaqueColor = importTexture(
        Name("tests/task_graph/live_opaque_color"),
        "Opaque Color",
        Graphics::ResourceStates::Common
    );
    const Graphics::GpuGraphResourceId compositeColor = importTexture(
        Name("tests/task_graph/live_composite_color"),
        "Composite Color",
        Graphics::ResourceStates::Common
    );
    const Graphics::GpuGraphResourceId backbuffer = AddHazardDomain(
        graph,
        Name("tests/task_graph/live_backbuffer"),
        "Back Buffer"
    );
    ASSERT_TRUE(sharedPrefixRead.valid());
    ASSERT_TRUE(currentBindlessSlots.valid());
    ASSERT_TRUE(currentIrradiance.valid());
    ASSERT_TRUE(currentShadowVisibility.valid());
    ASSERT_TRUE(currentCausticIrradiance.valid());
    ASSERT_TRUE(avboitWorking.valid());
    ASSERT_TRUE(avboitAccumulation.valid());
    ASSERT_TRUE(opaqueColor.valid());
    ASSERT_TRUE(compositeColor.valid());
    ASSERT_TRUE(backbuffer.valid());

    Graphics::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Graphics::GpuTaskCostHint::Large;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    const Graphics::GpuQueueRequest graphicsRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        false,
        false,
    };
    const Graphics::GpuQueueRequest computeRequest{
        Graphics::GpuQueueCapability::Compute,
        Graphics::GpuQueuePreference::Compute,
        true,
        false,
    };

    const Graphics::GpuTaskResourceUse shadowPrepareUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = currentBindlessSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    Graphics::GpuTaskDesc shadowPrepareDesc;
    shadowPrepareDesc
        .setIdentity(Name("tests/task_graph/live_shadow_prepare"))
        .setMarkerLabel("Shadow Prepare")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setResourceUses(shadowPrepareUses, LengthOf(shadowPrepareUses))
    ;
    const Graphics::GpuTaskId shadowPrepare = graph.addTask(shadowPrepareDesc);
    ASSERT_TRUE(shadowPrepare.valid());
    const Graphics::GpuTaskResourceUse prefixUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = sharedPrefixRead,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
        Graphics::GpuTaskResourceUse{
            .resource = currentBindlessSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
    };
    Graphics::GpuTaskDesc prefixDesc;
    prefixDesc
        .setIdentity(Name("tests/task_graph/live_graphics_prefix"))
        .setMarkerLabel("Graphics Prefix")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setDependencies(&shadowPrepare, 1u)
        .setResourceUses(prefixUses, LengthOf(prefixUses))
    ;
    const Graphics::GpuTaskId prefix = graph.addTask(prefixDesc);
    ASSERT_TRUE(prefix.valid());

    const Graphics::GpuTaskResourceUse shadowVisibilityUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = sharedPrefixRead,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = currentBindlessSlots,
            .range = {},
            .requiredState = Graphics::ResourceStates::ConstantBuffer,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = currentShadowVisibility,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    Graphics::GpuTaskDesc shadowVisibilityDesc;
    shadowVisibilityDesc
        .setIdentity(Name("tests/task_graph/live_shadow_visibility"))
        .setMarkerLabel("Shadow Visibility")
        .setQueue(computeRequest)
        .setScheduling(scheduling)
        .setDependencies(&prefix, 1u)
        .setResourceUses(shadowVisibilityUses, LengthOf(shadowVisibilityUses))
    ;
    const Graphics::GpuTaskId shadowVisibility = graph.addTask(shadowVisibilityDesc);
    ASSERT_TRUE(shadowVisibility.valid());

    const Graphics::GpuTaskResourceUse softwareCausticsUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = sharedPrefixRead,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = currentCausticIrradiance,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskId softwareCausticsDependencies[] = { shadowVisibility };
    Graphics::GpuTaskDesc softwareCausticsDesc;
    softwareCausticsDesc
        .setIdentity(Name("tests/task_graph/live_software_caustics"))
        .setMarkerLabel("Software Caustics")
        .setQueue(computeRequest)
        .setScheduling(scheduling)
        .setDependencies(softwareCausticsDependencies, LengthOf(softwareCausticsDependencies))
        .setResourceUses(softwareCausticsUses, LengthOf(softwareCausticsUses))
    ;
    const Graphics::GpuTaskId softwareCaustics = graph.addTask(softwareCausticsDesc);
    ASSERT_TRUE(softwareCaustics.valid());

    const Graphics::GpuTaskResourceUse surfelGiUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = sharedPrefixRead,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = currentIrradiance,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskId surfelGiDependencies[] = { softwareCaustics };
    Graphics::GpuTaskDesc surfelGiDesc;
    surfelGiDesc
        .setIdentity(Name("tests/task_graph/live_surfel_gi"))
        .setMarkerLabel("Surfel GI")
        .setQueue(computeRequest)
        .setScheduling(scheduling)
        .setDependencies(surfelGiDependencies, LengthOf(surfelGiDependencies))
        .setResourceUses(surfelGiUses, LengthOf(surfelGiUses))
    ;
    const Graphics::GpuTaskId surfelGi = graph.addTask(surfelGiDesc);
    ASSERT_TRUE(surfelGi.valid());

    const Graphics::GpuTaskResourceUse preUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = sharedPrefixRead,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
            .hasIndependentStateSource = true,
        },
        Graphics::GpuTaskResourceUse{
            .resource = avboitWorking,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    Graphics::GpuTaskDesc preDesc;
    preDesc
        .setIdentity(Name("tests/task_graph/live_avboit_pre"))
        .setMarkerLabel("AVBOIT Pre")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setDependencies(&prefix, 1u)
        .setResourceUses(preUses, LengthOf(preUses))
    ;
    const Graphics::GpuTaskId pre = graph.addTask(preDesc);
    ASSERT_TRUE(pre.valid());

    const Graphics::GpuTaskResourceUse depthWarpUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = avboitWorking,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    const Graphics::GpuTaskId preDependency[] = { pre };
    Graphics::GpuTaskDesc depthWarpDesc;
    depthWarpDesc
        .setIdentity(Name("tests/task_graph/live_avboit_depth_warp"))
        .setMarkerLabel("AVBOIT Depth Warp")
        .setQueue(computeRequest)
        .setScheduling(scheduling)
        .setDependencies(preDependency, LengthOf(preDependency))
        .setResourceUses(depthWarpUses, LengthOf(depthWarpUses))
    ;
    const Graphics::GpuTaskId depthWarp = graph.addTask(depthWarpDesc);
    ASSERT_TRUE(depthWarp.valid());

    const Graphics::GpuTaskResourceUse extinctionUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = avboitWorking,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    const Graphics::GpuTaskId depthWarpDependency[] = { depthWarp };
    Graphics::GpuTaskDesc extinctionDesc;
    extinctionDesc
        .setIdentity(Name("tests/task_graph/live_avboit_extinction"))
        .setMarkerLabel("AVBOIT Extinction")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setDependencies(depthWarpDependency, LengthOf(depthWarpDependency))
        .setResourceUses(extinctionUses, LengthOf(extinctionUses))
    ;
    const Graphics::GpuTaskId extinction = graph.addTask(extinctionDesc);
    ASSERT_TRUE(extinction.valid());

    const Graphics::GpuTaskResourceUse integrationUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = avboitWorking,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::ReadWrite,
        },
    };
    const Graphics::GpuTaskId extinctionDependency[] = { extinction };
    Graphics::GpuTaskDesc integrationDesc;
    integrationDesc
        .setIdentity(Name("tests/task_graph/live_avboit_integration"))
        .setMarkerLabel("AVBOIT Integration")
        .setQueue(computeRequest)
        .setScheduling(scheduling)
        .setDependencies(extinctionDependency, LengthOf(extinctionDependency))
        .setResourceUses(integrationUses, LengthOf(integrationUses))
    ;
    const Graphics::GpuTaskId integration = graph.addTask(integrationDesc);
    ASSERT_TRUE(integration.valid());

    const Graphics::GpuTaskResourceUse accumulationUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = sharedPrefixRead,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = avboitWorking,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = avboitAccumulation,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskId integrationDependency[] = { integration };
    Graphics::GpuTaskDesc accumulationDesc;
    accumulationDesc
        .setIdentity(Name("tests/task_graph/live_avboit_accumulation"))
        .setMarkerLabel("AVBOIT Accumulation")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setDependencies(integrationDependency, LengthOf(integrationDependency))
        .setResourceUses(accumulationUses, LengthOf(accumulationUses))
    ;
    const Graphics::GpuTaskId accumulation = graph.addTask(accumulationDesc);
    ASSERT_TRUE(accumulation.valid());

    const Graphics::GpuTaskResourceUse lightingUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = sharedPrefixRead,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = currentIrradiance,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = currentShadowVisibility,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = currentCausticIrradiance,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = opaqueColor,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskId lightingDependencies[] = {
        shadowVisibility,
        softwareCaustics,
        surfelGi,
        accumulation,
    };
    Graphics::GpuTaskDesc lightingDesc;
    lightingDesc
        .setIdentity(Name("tests/task_graph/live_deferred_lighting"))
        .setMarkerLabel("Deferred Lighting")
        .setQueue(computeRequest)
        .setScheduling(scheduling)
        .setDependencies(lightingDependencies, LengthOf(lightingDependencies))
        .setResourceUses(lightingUses, LengthOf(lightingUses))
    ;
    const Graphics::GpuTaskId lighting = graph.addTask(lightingDesc);
    ASSERT_TRUE(lighting.valid());

    const Graphics::GpuTaskResourceUse compositeUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = opaqueColor,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = avboitAccumulation,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = compositeColor,
            .range = {},
            .requiredState = Graphics::ResourceStates::UnorderedAccess,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskId compositeDependencies[] = {
        lighting,
        accumulation,
    };
    Graphics::GpuTaskDesc compositeDesc;
    compositeDesc
        .setIdentity(Name("tests/task_graph/live_deferred_composite"))
        .setMarkerLabel("Deferred Composite")
        .setQueue(computeRequest)
        .setScheduling(scheduling)
        .setDependencies(compositeDependencies, LengthOf(compositeDependencies))
        .setResourceUses(compositeUses, LengthOf(compositeUses))
    ;
    const Graphics::GpuTaskId composite = graph.addTask(compositeDesc);
    ASSERT_TRUE(composite.valid());

    const Graphics::GpuTaskResourceUse presentUses[] = {
        Graphics::GpuTaskResourceUse{
            .resource = compositeColor,
            .range = {},
            .requiredState = Graphics::ResourceStates::ShaderResource,
            .access = Graphics::GpuTaskResourceAccess::Read,
        },
        Graphics::GpuTaskResourceUse{
            .resource = backbuffer,
            .range = {},
            .requiredState = Graphics::ResourceStates::Present,
            .access = Graphics::GpuTaskResourceAccess::Write,
        },
    };
    const Graphics::GpuTaskId presentDependencies[] = { composite };
    Graphics::GpuTaskDesc presentDesc;
    presentDesc
        .setIdentity(Name("tests/task_graph/live_deferred_present"))
        .setMarkerLabel("Deferred Present")
        .setQueue(graphicsRequest)
        .setScheduling(scheduling)
        .setDependencies(presentDependencies, LengthOf(presentDependencies))
        .setResourceUses(presentUses, LengthOf(presentUses))
    ;
    const Graphics::GpuTaskId present = graph.addTask(presentDesc);
    ASSERT_TRUE(present.valid());

    const Graphics::GpuPhysicalQueueInfo queues[] = {
        GraphicsQueue(),
        DedicatedComputeQueue(),
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


    const Graphics::GpuTaskQueueAssignment* const shadowPrepareAssignment = assignments.find(shadowPrepare);
    const Graphics::GpuTaskQueueAssignment* const prefixAssignment = assignments.find(prefix);
    const Graphics::GpuTaskQueueAssignment* const shadowVisibilityAssignment = assignments.find(shadowVisibility);
    const Graphics::GpuTaskQueueAssignment* const softwareCausticsAssignment = assignments.find(softwareCaustics);
    const Graphics::GpuTaskQueueAssignment* const surfelGiAssignment = assignments.find(surfelGi);
    const Graphics::GpuTaskQueueAssignment* const preAssignment = assignments.find(pre);
    const Graphics::GpuTaskQueueAssignment* const depthWarpAssignment = assignments.find(depthWarp);
    const Graphics::GpuTaskQueueAssignment* const extinctionAssignment = assignments.find(extinction);
    const Graphics::GpuTaskQueueAssignment* const integrationAssignment = assignments.find(integration);
    const Graphics::GpuTaskQueueAssignment* const accumulationAssignment = assignments.find(accumulation);
    const Graphics::GpuTaskQueueAssignment* const lightingAssignment = assignments.find(lighting);
    const Graphics::GpuTaskQueueAssignment* const compositeAssignment = assignments.find(composite);
    const Graphics::GpuTaskQueueAssignment* const presentAssignment = assignments.find(present);
    ASSERT_NE(shadowPrepareAssignment, nullptr);
    ASSERT_NE(prefixAssignment, nullptr);
    ASSERT_NE(shadowVisibilityAssignment, nullptr);
    ASSERT_NE(softwareCausticsAssignment, nullptr);
    ASSERT_NE(surfelGiAssignment, nullptr);
    ASSERT_NE(preAssignment, nullptr);
    ASSERT_NE(depthWarpAssignment, nullptr);
    ASSERT_NE(extinctionAssignment, nullptr);
    ASSERT_NE(integrationAssignment, nullptr);
    ASSERT_NE(accumulationAssignment, nullptr);
    ASSERT_NE(lightingAssignment, nullptr);
    ASSERT_NE(compositeAssignment, nullptr);
    ASSERT_NE(presentAssignment, nullptr);
    EXPECT_EQ(shadowPrepareAssignment->queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(prefixAssignment->queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(shadowVisibilityAssignment->queueClass, Graphics::CommandQueue::Compute);
    EXPECT_EQ(softwareCausticsAssignment->queueClass, Graphics::CommandQueue::Compute);
    EXPECT_EQ(surfelGiAssignment->queueClass, Graphics::CommandQueue::Compute);
    EXPECT_EQ(preAssignment->queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(depthWarpAssignment->queueClass, Graphics::CommandQueue::Compute);
    EXPECT_EQ(extinctionAssignment->queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(integrationAssignment->queueClass, Graphics::CommandQueue::Compute);
    EXPECT_EQ(accumulationAssignment->queueClass, Graphics::CommandQueue::Graphics);
    EXPECT_EQ(lightingAssignment->queueClass, Graphics::CommandQueue::Compute);
    EXPECT_EQ(compositeAssignment->queueClass, Graphics::CommandQueue::Compute);
    EXPECT_EQ(presentAssignment->queueClass, Graphics::CommandQueue::Graphics);

    const Graphics::GpuSubmissionPacketId shadowPreparePacket = compiledPlan.packetForTask(shadowPrepare);
    const Graphics::GpuSubmissionPacketId prefixPacket = compiledPlan.packetForTask(prefix);
    const Graphics::GpuSubmissionPacketId shadowVisibilityPacket = compiledPlan.packetForTask(shadowVisibility);
    const Graphics::GpuSubmissionPacketId softwareCausticsPacket = compiledPlan.packetForTask(softwareCaustics);
    const Graphics::GpuSubmissionPacketId surfelGiPacket = compiledPlan.packetForTask(surfelGi);
    const Graphics::GpuSubmissionPacketId prePacket = compiledPlan.packetForTask(pre);
    const Graphics::GpuSubmissionPacketId depthWarpPacket = compiledPlan.packetForTask(depthWarp);
    const Graphics::GpuSubmissionPacketId extinctionPacket = compiledPlan.packetForTask(extinction);
    const Graphics::GpuSubmissionPacketId integrationPacket = compiledPlan.packetForTask(integration);
    const Graphics::GpuSubmissionPacketId accumulationPacket = compiledPlan.packetForTask(accumulation);
    const Graphics::GpuSubmissionPacketId lightingPacket = compiledPlan.packetForTask(lighting);
    const Graphics::GpuSubmissionPacketId compositePacket = compiledPlan.packetForTask(composite);
    const Graphics::GpuSubmissionPacketId presentPacket = compiledPlan.packetForTask(present);
    ASSERT_TRUE(shadowPreparePacket.valid());
    ASSERT_TRUE(prefixPacket.valid());
    ASSERT_TRUE(shadowVisibilityPacket.valid());
    ASSERT_TRUE(softwareCausticsPacket.valid());
    ASSERT_TRUE(surfelGiPacket.valid());
    ASSERT_TRUE(prePacket.valid());
    ASSERT_TRUE(depthWarpPacket.valid());
    ASSERT_TRUE(extinctionPacket.valid());
    ASSERT_TRUE(integrationPacket.valid());
    ASSERT_TRUE(accumulationPacket.valid());
    ASSERT_TRUE(lightingPacket.valid());
    ASSERT_TRUE(compositePacket.valid());
    ASSERT_TRUE(presentPacket.valid());
    ASSERT_EQ(compiledPlan.packetCount(), 13u);
    EXPECT_EQ(compiledPlan.packetIdAt(0u), shadowPreparePacket);
    EXPECT_EQ(compiledPlan.packetIdAt(1u), prefixPacket);
    EXPECT_EQ(compiledPlan.packetIdAt(2u), shadowVisibilityPacket);
    EXPECT_EQ(compiledPlan.packetIdAt(3u), softwareCausticsPacket);
    EXPECT_EQ(compiledPlan.packetIdAt(4u), surfelGiPacket);
    EXPECT_EQ(compiledPlan.packetIdAt(5u), prePacket);
    EXPECT_EQ(compiledPlan.packetIdAt(6u), depthWarpPacket);
    EXPECT_EQ(compiledPlan.packetIdAt(7u), extinctionPacket);
    EXPECT_EQ(compiledPlan.packetIdAt(8u), integrationPacket);
    EXPECT_EQ(compiledPlan.packetIdAt(9u), accumulationPacket);
    EXPECT_EQ(compiledPlan.packetIdAt(10u), lightingPacket);
    EXPECT_EQ(compiledPlan.packetIdAt(11u), compositePacket);
    EXPECT_EQ(compiledPlan.packetIdAt(12u), presentPacket);
    const Graphics::GpuCompiledTask* const compiledShadowPrepare = compiledPlan.findTask(shadowPrepare).plan;
    const Graphics::GpuCompiledTask* const compiledPrefix = compiledPlan.findTask(prefix).plan;
    ASSERT_NE(compiledShadowPrepare, nullptr);
    ASSERT_NE(compiledPrefix, nullptr);
    ASSERT_EQ(compiledShadowPrepare->prologueBarrierCount, 1u);
    const Graphics::GpuCompiledBarrier* const shadowPrepareBarrier = compiledPlan.findTask(shadowPrepare).prologueBarriers;
    ASSERT_NE(shadowPrepareBarrier, nullptr);
    EXPECT_EQ(shadowPrepareBarrier[0u].type, Graphics::GpuCompiledBarrierType::BufferTransition);
    EXPECT_EQ(shadowPrepareBarrier[0u].resource, currentBindlessSlots);
    EXPECT_EQ(shadowPrepareBarrier[0u].before, Graphics::ResourceStates::Common);
    EXPECT_EQ(shadowPrepareBarrier[0u].after, Graphics::ResourceStates::ConstantBuffer);
    ASSERT_EQ(compiledPrefix->prologueStateSeedCount, 1u);
    const Graphics::GpuPacketStateSeed* const prefixSeeds = compiledPlan.findTask(prefix).prologueStateSeeds;
    ASSERT_NE(prefixSeeds, nullptr);
    EXPECT_EQ(prefixSeeds[0u].resource, currentBindlessSlots);
    EXPECT_EQ(prefixSeeds[0u].sourcePacket, shadowPreparePacket);
    ASSERT_EQ(compiledPlan.packet(prefixPacket).plan->dependencyCount, 1u);
    const Graphics::GpuPacketDependency* const prefixPacketDependencies = compiledPlan.packet(prefixPacket).dependencies;
    ASSERT_NE(prefixPacketDependencies, nullptr);
    EXPECT_EQ(prefixPacketDependencies[0u].producer, shadowPreparePacket);

    EXPECT_NE(FindEdge(analysis, shadowVisibility, lighting), nullptr);
    EXPECT_NE(FindEdge(analysis, softwareCaustics, lighting), nullptr);
    EXPECT_NE(FindEdge(analysis, surfelGi, lighting), nullptr);
    EXPECT_NE(FindEdge(analysis, accumulation, lighting), nullptr);
    EXPECT_NE(FindEdge(analysis, prefix, lighting), nullptr);
    const Graphics::GpuCompiledTask* const compiledLighting = compiledPlan.findTask(lighting).plan;
    ASSERT_NE(compiledLighting, nullptr);
    ASSERT_GT(compiledLighting->prologueStateSeedCount, 0u);
    const Graphics::GpuPacketStateSeed* const lightingSeeds = compiledPlan.findTask(lighting).prologueStateSeeds;
    ASSERT_NE(lightingSeeds, nullptr);
    bool lightingImportsShadowVisibilityState = false;
    bool lightingImportsSoftwareCausticsState = false;
    bool lightingImportsSurfelGiState = false;
    bool lightingImportsAccumulationState = false;
    for(usize index = 0u; index < compiledLighting->prologueStateSeedCount; ++index){
        lightingImportsShadowVisibilityState = lightingImportsShadowVisibilityState
            || lightingSeeds[index].sourcePacket == shadowVisibilityPacket
        ;
        lightingImportsSoftwareCausticsState = lightingImportsSoftwareCausticsState
            || lightingSeeds[index].sourcePacket == softwareCausticsPacket
        ;
        lightingImportsSurfelGiState = lightingImportsSurfelGiState
            || lightingSeeds[index].sourcePacket == surfelGiPacket
        ;
        lightingImportsAccumulationState = lightingImportsAccumulationState
            || lightingSeeds[index].sourcePacket == accumulationPacket
        ;
    }
    EXPECT_TRUE(lightingImportsShadowVisibilityState);
    EXPECT_TRUE(lightingImportsSoftwareCausticsState);
    EXPECT_TRUE(lightingImportsSurfelGiState);
    EXPECT_TRUE(lightingImportsAccumulationState);
    ASSERT_EQ(compiledPlan.packet(lightingPacket).plan->dependencyCount, 4u);
    const Graphics::GpuPacketDependency* const lightingPacketDependencies = compiledPlan.packet(lightingPacket).dependencies;
    ASSERT_NE(lightingPacketDependencies, nullptr);
    bool lightingWaitsForPrefix = false;
    bool lightingWaitsForShadowVisibility = false;
    bool lightingWaitsForSoftwareCaustics = false;
    bool lightingWaitsForSurfelGi = false;
    bool lightingWaitsForAccumulation = false;
    for(usize index = 0u; index < compiledPlan.packet(lightingPacket).plan->dependencyCount; ++index){
        lightingWaitsForPrefix = lightingWaitsForPrefix || lightingPacketDependencies[index].producer == prefixPacket;
        lightingWaitsForShadowVisibility = lightingWaitsForShadowVisibility
            || lightingPacketDependencies[index].producer == shadowVisibilityPacket
        ;
        lightingWaitsForSoftwareCaustics = lightingWaitsForSoftwareCaustics
            || lightingPacketDependencies[index].producer == softwareCausticsPacket
        ;
        lightingWaitsForSurfelGi = lightingWaitsForSurfelGi
            || lightingPacketDependencies[index].producer == surfelGiPacket
        ;
        lightingWaitsForAccumulation = lightingWaitsForAccumulation
            || lightingPacketDependencies[index].producer == accumulationPacket
        ;
    }
    EXPECT_FALSE(lightingWaitsForPrefix);
    EXPECT_TRUE(lightingWaitsForShadowVisibility);
    EXPECT_TRUE(lightingWaitsForSoftwareCaustics);
    EXPECT_TRUE(lightingWaitsForSurfelGi);
    EXPECT_TRUE(lightingWaitsForAccumulation);
    EXPECT_EQ(compiledPlan.packet(lightingPacket).plan->externalDependencyCount, 0u);

    const Graphics::GpuCompiledTask* const compiledShadowVisibility = compiledPlan.findTask(shadowVisibility).plan;
    ASSERT_NE(compiledShadowVisibility, nullptr);
    ASSERT_GT(compiledShadowVisibility->prologueStateSeedCount, 0u);
    const Graphics::GpuPacketStateSeed* const shadowVisibilitySeeds = compiledPlan.findTask(
        shadowVisibility
    ).prologueStateSeeds;
    ASSERT_NE(shadowVisibilitySeeds, nullptr);
    bool shadowVisibilityImportsBindlessSlotsState = false;
    for(usize index = 0u; index < compiledShadowVisibility->prologueStateSeedCount; ++index){
        shadowVisibilityImportsBindlessSlotsState = shadowVisibilityImportsBindlessSlotsState
            || (
                shadowVisibilitySeeds[index].resource == currentBindlessSlots
                && shadowVisibilitySeeds[index].sourcePacket == prefixPacket
            )
        ;
    }
    EXPECT_TRUE(shadowVisibilityImportsBindlessSlotsState);
    EXPECT_EQ(compiledPlan.packet(shadowVisibilityPacket).plan->externalDependencyCount, 0u);
    EXPECT_NE(FindEdge(analysis, shadowPrepare, shadowVisibility), nullptr);
    ASSERT_EQ(compiledPlan.packet(shadowVisibilityPacket).plan->dependencyCount, 1u);
    const Graphics::GpuPacketDependency* const shadowVisibilityPacketDependencies = compiledPlan.packet(
        shadowVisibilityPacket
    ).dependencies;
    ASSERT_NE(shadowVisibilityPacketDependencies, nullptr);
    EXPECT_EQ(shadowVisibilityPacketDependencies[0u].producer, prefixPacket);
    ASSERT_EQ(compiledPlan.packet(softwareCausticsPacket).plan->externalDependencyCount, 0u);
    ASSERT_GE(compiledPlan.packet(softwareCausticsPacket).plan->dependencyCount, 1u);
    const Graphics::GpuPacketDependency* const softwareCausticsPacketDependencies = compiledPlan.packet(
        softwareCausticsPacket
    ).dependencies;
    ASSERT_NE(softwareCausticsPacketDependencies, nullptr);
    bool softwareCausticsWaitsForShadowVisibility = false;
    for(usize index = 0u; index < compiledPlan.packet(softwareCausticsPacket).plan->dependencyCount; ++index){
        softwareCausticsWaitsForShadowVisibility = softwareCausticsWaitsForShadowVisibility
            || softwareCausticsPacketDependencies[index].producer == shadowVisibilityPacket
        ;
    }
    EXPECT_TRUE(softwareCausticsWaitsForShadowVisibility);
    ASSERT_EQ(compiledPlan.packet(surfelGiPacket).plan->externalDependencyCount, 0u);
    ASSERT_GE(compiledPlan.packet(surfelGiPacket).plan->dependencyCount, 1u);
    const Graphics::GpuPacketDependency* const surfelGiPacketDependencies = compiledPlan.packet(
        surfelGiPacket
    ).dependencies;
    ASSERT_NE(surfelGiPacketDependencies, nullptr);
    bool surfelGiWaitsForSoftwareCaustics = false;
    for(usize index = 0u; index < compiledPlan.packet(surfelGiPacket).plan->dependencyCount; ++index){
        surfelGiWaitsForSoftwareCaustics = surfelGiWaitsForSoftwareCaustics
            || surfelGiPacketDependencies[index].producer == softwareCausticsPacket
        ;
    }
    EXPECT_TRUE(surfelGiWaitsForSoftwareCaustics);

    const Graphics::GpuPacketDependency* const compositePacketDependencies = compiledPlan.packet(compositePacket).dependencies;
    ASSERT_NE(compositePacketDependencies, nullptr);
    bool compositeWaitsForLighting = false;
    bool compositeWaitsForAccumulation = false;
    for(usize index = 0u; index < compiledPlan.packet(compositePacket).plan->dependencyCount; ++index){
        compositeWaitsForLighting = compositeWaitsForLighting
            || compositePacketDependencies[index].producer == lightingPacket
        ;
        compositeWaitsForAccumulation = compositeWaitsForAccumulation
            || compositePacketDependencies[index].producer == accumulationPacket
        ;
    }
    EXPECT_TRUE(compositeWaitsForLighting);
    EXPECT_TRUE(compositeWaitsForAccumulation);
    EXPECT_EQ(compiledPlan.packet(compositePacket).plan->externalDependencyCount, 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

