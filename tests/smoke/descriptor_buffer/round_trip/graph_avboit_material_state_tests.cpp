// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "csg_probes_test_support.h"
#include "graph_resources_test_support.h"
#include "material_probes_test_support.h"
#include "packet_recording_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Prepared transparent CSG Pre writes peel and receiver-event StorageImage aliases, then graph-owned Span builds
// receiver spans before Combine writes four removed-interval aliases. Span, Combine, Clear, and Occupancy must all
// see graph-lowered state without reissuing native setup; check the first and last slices on a real Vulkan packet.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedAvboitOccupancyCsgIntervalSampleStateRecordsWithoutNativeBridge){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto coverage = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setCanHaveUAVs(true)
            .setInitialState(ResourceStates::Common)
    );
    auto materialGeometry = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
    );
    auto occupancyMaterialGeometry = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(coverage.get(), nullptr);
    ASSERT_NE(materialGeometry.get(), nullptr);
    ASSERT_NE(occupancyMaterialGeometry.get(), nullptr);
    const auto makeStorageArray = [&device](const u32 arraySize){
        return device.createTexture(
            TextureDesc()
                .setWidth(4u)
                .setHeight(4u)
                .setArraySize(arraySize)
                .setDimension(TextureDimension::Texture2DArray)
                .setFormat(Format::RGBA32_UINT)
                .setInUAV(true)
                .setInitialState(ResourceStates::Common)
        );
    };
    auto capBackNormal = makeStorageArray(4u);
    auto intervalDepth = makeStorageArray(4u);
    auto intervalId = makeStorageArray(4u);
    auto receiverEventData = makeStorageArray(32u);
    auto receiverEventCount = makeStorageArray(1u);
    auto receiverSpanData = makeStorageArray(16u);
    auto receiverSpanCount = makeStorageArray(1u);
    auto removedIntervalDepth = makeStorageArray(16u);
    auto removedIntervalCapNormal = makeStorageArray(16u);
    auto removedIntervalData = makeStorageArray(16u);
    auto removedIntervalCount = makeStorageArray(1u);
    ASSERT_NE(capBackNormal.get(), nullptr);
    ASSERT_NE(intervalDepth.get(), nullptr);
    ASSERT_NE(intervalId.get(), nullptr);
    ASSERT_NE(receiverEventData.get(), nullptr);
    ASSERT_NE(receiverEventCount.get(), nullptr);
    ASSERT_NE(receiverSpanData.get(), nullptr);
    ASSERT_NE(receiverSpanCount.get(), nullptr);
    ASSERT_NE(removedIntervalDepth.get(), nullptr);
    ASSERT_NE(removedIntervalCapNormal.get(), nullptr);
    ASSERT_NE(removedIntervalData.get(), nullptr);
    ASSERT_NE(removedIntervalCount.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId coverageResource = graph.importBuffer(
        coverage,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/avboit_csg_coverage"))
            .setMarkerLabel("AVBOIT CSG Coverage")
            .setType(GpuGraphResourceType::Buffer)
    );
    const GpuGraphResourceId materialGeometryResource = graph.importBuffer(
        materialGeometry,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/avboit_csg_material_geometry"))
            .setMarkerLabel("Transparent CSG Material Geometry")
            .setType(GpuGraphResourceType::Buffer)
    );
    const GpuGraphResourceId occupancyMaterialGeometryResource = graph.importBuffer(
        occupancyMaterialGeometry,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/avboit_occupancy_material_geometry"))
            .setMarkerLabel("AVBOIT Occupancy Material Geometry")
            .setType(GpuGraphResourceType::Buffer)
    );
    const GpuGraphResourceId capBackNormalResource = graph.importTexture(
        capBackNormal,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/avboit_csg_cap_back_normal"))
            .setMarkerLabel("AVBOIT CSG Cap Back Normal")
            .setType(GpuGraphResourceType::Texture)
    );
    const GpuGraphResourceId intervalDepthResource = graph.importTexture(
        intervalDepth,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/avboit_csg_interval_depth"))
            .setMarkerLabel("AVBOIT CSG Interval Depth")
            .setType(GpuGraphResourceType::Texture)
    );
    const GpuGraphResourceId intervalIdResource = graph.importTexture(
        intervalId,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/avboit_csg_interval_id"))
            .setMarkerLabel("AVBOIT CSG Interval Id")
            .setType(GpuGraphResourceType::Texture)
    );
    const GpuGraphResourceId receiverEventDataResource = graph.importTexture(
        receiverEventData,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/avboit_csg_receiver_event_data"))
            .setMarkerLabel("AVBOIT CSG Receiver Event Data")
            .setType(GpuGraphResourceType::Texture)
    );
    const GpuGraphResourceId receiverEventCountResource = graph.importTexture(
        receiverEventCount,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/avboit_csg_receiver_event_count"))
            .setMarkerLabel("AVBOIT CSG Receiver Event Count")
            .setType(GpuGraphResourceType::Texture)
    );
    const GpuGraphResourceId receiverSpanDataResource = graph.importTexture(
        receiverSpanData,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/avboit_csg_receiver_span_data"))
            .setMarkerLabel("AVBOIT CSG Receiver Span Data")
            .setType(GpuGraphResourceType::Texture)
    );
    const GpuGraphResourceId receiverSpanCountResource = graph.importTexture(
        receiverSpanCount,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/avboit_csg_receiver_span_count"))
            .setMarkerLabel("AVBOIT CSG Receiver Span Count")
            .setType(GpuGraphResourceType::Texture)
    );
    const GpuGraphResourceId removedIntervalDepthResource = graph.importTexture(
        removedIntervalDepth,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/avboit_csg_removed_interval_depth"))
            .setMarkerLabel("AVBOIT CSG Removed Interval Depth")
            .setType(GpuGraphResourceType::Texture)
    );
    const GpuGraphResourceId removedIntervalCapNormalResource = graph.importTexture(
        removedIntervalCapNormal,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/avboit_csg_removed_interval_cap_normal"))
            .setMarkerLabel("AVBOIT CSG Removed Interval Cap Normal")
            .setType(GpuGraphResourceType::Texture)
    );
    const GpuGraphResourceId removedIntervalDataResource = graph.importTexture(
        removedIntervalData,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/avboit_csg_removed_interval_data"))
            .setMarkerLabel("AVBOIT CSG Removed Interval Data")
            .setType(GpuGraphResourceType::Texture)
    );
    const GpuGraphResourceId removedIntervalCountResource = graph.importTexture(
        removedIntervalCount,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/avboit_csg_removed_interval_count"))
            .setMarkerLabel("AVBOIT CSG Removed Interval Count")
            .setType(GpuGraphResourceType::Texture)
    );
    ASSERT_TRUE(coverageResource.valid());
    ASSERT_TRUE(capBackNormalResource.valid());
    ASSERT_TRUE(intervalDepthResource.valid());
    ASSERT_TRUE(intervalIdResource.valid());
    ASSERT_TRUE(receiverEventDataResource.valid());
    ASSERT_TRUE(receiverEventCountResource.valid());
    ASSERT_TRUE(receiverSpanDataResource.valid());
    ASSERT_TRUE(receiverSpanCountResource.valid());
    ASSERT_TRUE(removedIntervalDepthResource.valid());
    ASSERT_TRUE(removedIntervalCapNormalResource.valid());
    ASSERT_TRUE(removedIntervalDataResource.valid());
    ASSERT_TRUE(removedIntervalCountResource.valid());
    ASSERT_TRUE(materialGeometryResource.valid());
    ASSERT_TRUE(occupancyMaterialGeometryResource.valid());

    const TextureSubresourceSet peelRange(0u, 1u, 0u, 4u);
    const TextureSubresourceSet receiverEventRange(0u, 1u, 0u, 32u);
    const TextureSubresourceSet receiverEventCountRange(0u, 1u, 0u, 1u);
    const TextureSubresourceSet receiverSpanRange(0u, 1u, 0u, 16u);
    const TextureSubresourceSet receiverSpanCountRange(0u, 1u, 0u, 1u);
    const TextureSubresourceSet removedIntervalRange(0u, 1u, 0u, 16u);
    const TextureSubresourceSet removedIntervalCountRange(0u, 1u, 0u, 1u);
    const GpuTaskResourceUse preUses[] = {
        GpuTaskResourceUse{
            .resource = capBackNormalResource,
            .range = GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
        GpuTaskResourceUse{
            .resource = intervalDepthResource,
            .range = GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
        GpuTaskResourceUse{
            .resource = intervalIdResource,
            .range = GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
        GpuTaskResourceUse{
            .resource = receiverEventDataResource,
            .range = GpuTaskResourceRange{ .textureSubresources = receiverEventRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
        GpuTaskResourceUse{
            .resource = receiverEventCountResource,
            .range = GpuTaskResourceRange{ .textureSubresources = receiverEventCountRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
    };
    const GpuGraphResourceSetId preMaterialGeometrySet = graph.importResourceSet(
        GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/descriptor_buffer/avboit_csg_material_geometry_set"))
            .setMarkerLabel("Transparent CSG Material Geometry")
            .setMembers(&materialGeometryResource, 1u)
    );
    ASSERT_TRUE(preMaterialGeometrySet.valid());
    const GpuTaskResourceSetUse preMaterialGeometrySetUses[] = {
        GpuTaskResourceSetUse{
            .resourceSet = preMaterialGeometrySet,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    const GpuTaskResourceUse spanUses[] = {
        GpuTaskResourceUse{
            .resource = receiverEventDataResource,
            .range = GpuTaskResourceRange{ .textureSubresources = receiverEventRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = receiverEventCountResource,
            .range = GpuTaskResourceRange{ .textureSubresources = receiverEventCountRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = receiverSpanDataResource,
            .range = GpuTaskResourceRange{ .textureSubresources = receiverSpanRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
        GpuTaskResourceUse{
            .resource = receiverSpanCountResource,
            .range = GpuTaskResourceRange{ .textureSubresources = receiverSpanCountRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    const GpuTaskResourceUse combineUses[] = {
        GpuTaskResourceUse{
            .resource = capBackNormalResource,
            .range = GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = intervalDepthResource,
            .range = GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = intervalIdResource,
            .range = GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = receiverSpanDataResource,
            .range = GpuTaskResourceRange{ .textureSubresources = receiverSpanRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = receiverSpanCountResource,
            .range = GpuTaskResourceRange{ .textureSubresources = receiverSpanCountRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = removedIntervalDepthResource,
            .range = GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
        GpuTaskResourceUse{
            .resource = removedIntervalCapNormalResource,
            .range = GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
        GpuTaskResourceUse{
            .resource = removedIntervalDataResource,
            .range = GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
        GpuTaskResourceUse{
            .resource = removedIntervalCountResource,
            .range = GpuTaskResourceRange{ .textureSubresources = removedIntervalCountRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    const GpuTaskResourceUse clearUses[] = {
        GpuTaskResourceUse{
            .resource = coverageResource,
            .range = {},
            .requiredState = ResourceStates::CopyDest,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    const GpuTaskResourceUse occupancyUses[] = {
        GpuTaskResourceUse{
            .resource = coverageResource,
            .range = {},
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::ReadWrite,
        },
        GpuTaskResourceUse{
            .resource = removedIntervalDepthResource,
            .range = GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = removedIntervalCapNormalResource,
            .range = GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = removedIntervalDataResource,
            .range = GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = removedIntervalCountResource,
            .range = GpuTaskResourceRange{ .textureSubresources = removedIntervalCountRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    const GpuGraphResourceSetId occupancyMaterialGeometrySet = graph.importResourceSet(
        GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/descriptor_buffer/avboit_occupancy_material_geometry_set"))
            .setMarkerLabel("AVBOIT Occupancy Material Geometry")
            .setMembers(&occupancyMaterialGeometryResource, 1u)
    );
    ASSERT_TRUE(occupancyMaterialGeometrySet.valid());
    const GpuTaskResourceSetUse occupancyMaterialGeometrySetUses[] = {
        GpuTaskResourceSetUse{
            .resourceSet = occupancyMaterialGeometrySet,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    const GpuQueueRequest graphicsQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint preScheduling;
    preScheduling.cost = GpuTaskCostHint::Large;
    preScheduling.forceSubmissionBoundary = false;
    preScheduling.allowPacketMerge = true;
    GpuTaskDesc preDesc;
    preDesc
        .setIdentity(Name("tests/descriptor_buffer/avboit_csg_pre"))
        .setMarkerLabel("Transparent CSG Pre")
        .setQueue(graphicsQueue)
        .setScheduling(preScheduling)
        .setResourceUses(preUses, LengthOf(preUses))
        .setResourceSetUses(preMaterialGeometrySetUses, LengthOf(preMaterialGeometrySetUses))
    ;
    bool preRecorded = false;
    const GpuTaskId preTask = graph.addTask<NativePacketMaterialGeometryEntryProbeTask>(
        preDesc,
        NativePacketMaterialGeometryEntryProbeTask::Payload{
            .geometry = materialGeometry.get(),
            .recorded = &preRecorded,
        }
    );
    ASSERT_TRUE(preTask.valid());

    GpuTaskSchedulingHint spanScheduling = preScheduling;
    spanScheduling.cost = GpuTaskCostHint::Medium;
    spanScheduling.mergeWithPrevious = true;
    GpuTaskDesc spanDesc;
    spanDesc
        .setIdentity(Name("tests/descriptor_buffer/avboit_csg_receiver_span"))
        .setMarkerLabel("Transparent CSG Receiver Span")
        .setQueue(graphicsQueue)
        .setScheduling(spanScheduling)
        .setDependencies(&preTask, 1u)
        .setResourceUses(spanUses, LengthOf(spanUses))
    ;
    bool spanRecorded = false;
    const GpuTaskId spanTask = graph.addTask<NativePacketCsgReceiverSpanProbeTask>(
        spanDesc,
        NativePacketCsgReceiverSpanProbeTask::Payload{
            .receiverEventData = receiverEventData.get(),
            .receiverEventCount = receiverEventCount.get(),
            .receiverSpanData = receiverSpanData.get(),
            .receiverSpanCount = receiverSpanCount.get(),
            .recorded = &spanRecorded,
        }
    );
    ASSERT_TRUE(spanTask.valid());

    GpuTaskSchedulingHint combineScheduling = preScheduling;
    combineScheduling.cost = GpuTaskCostHint::Medium;
    combineScheduling.mergeWithPrevious = true;
    GpuTaskDesc combineDesc;
    combineDesc
        .setIdentity(Name("tests/descriptor_buffer/avboit_csg_interval_combine"))
        .setMarkerLabel("Transparent CSG Interval Combine")
        .setQueue(graphicsQueue)
        .setScheduling(combineScheduling)
        .setDependencies(&spanTask, 1u)
        .setResourceUses(combineUses, LengthOf(combineUses))
    ;
    bool combineRecorded = false;
    const GpuTaskId combineTask = graph.addTask<NativePacketCsgIntervalCombineProbeTask>(
        combineDesc,
        NativePacketCsgIntervalCombineProbeTask::Payload{
            .capBackNormal = capBackNormal.get(),
            .intervalDepth = intervalDepth.get(),
            .intervalId = intervalId.get(),
            .receiverSpanData = receiverSpanData.get(),
            .receiverSpanCount = receiverSpanCount.get(),
            .removedIntervalDepth = removedIntervalDepth.get(),
            .removedIntervalCapNormal = removedIntervalCapNormal.get(),
            .removedIntervalData = removedIntervalData.get(),
            .removedIntervalCount = removedIntervalCount.get(),
            .recorded = &combineRecorded,
        }
    );
    ASSERT_TRUE(combineTask.valid());

    GpuTaskSchedulingHint clearScheduling;
    clearScheduling.cost = GpuTaskCostHint::Tiny;
    clearScheduling.forceSubmissionBoundary = false;
    clearScheduling.allowPacketMerge = true;
    clearScheduling.mergeWithPrevious = true;
    GpuTaskDesc clearDesc;
    clearDesc
        .setIdentity(Name("tests/descriptor_buffer/avboit_csg_clear"))
        .setMarkerLabel("AVBOIT Clear")
        .setQueue(graphicsQueue)
        .setScheduling(clearScheduling)
        .setDependencies(&combineTask, 1u)
        .setResourceUses(clearUses, LengthOf(clearUses))
    ;
    bool clearRecorded = false;
    const GpuTaskId clearTask = graph.addTask<NativePacketPrefixTask>(
        clearDesc,
        NativePacketPrefixTask::Payload{
            .buffer = coverage.get(),
            .expectedState = ResourceStates::CopyDest,
            .recorded = &clearRecorded,
        }
    );
    ASSERT_TRUE(clearTask.valid());

    GpuTaskSchedulingHint occupancyScheduling;
    occupancyScheduling.cost = GpuTaskCostHint::Large;
    occupancyScheduling.forceSubmissionBoundary = false;
    occupancyScheduling.allowPacketMerge = true;
    occupancyScheduling.mergeWithPrevious = true;
    GpuTaskDesc occupancyDesc;
    occupancyDesc
        .setIdentity(Name("tests/descriptor_buffer/avboit_csg_occupancy"))
        .setMarkerLabel("AVBOIT CSG Occupancy")
        .setQueue(graphicsQueue)
        .setScheduling(occupancyScheduling)
        .setDependencies(&clearTask, 1u)
        .setResourceUses(occupancyUses, LengthOf(occupancyUses))
        .setResourceSetUses(occupancyMaterialGeometrySetUses, LengthOf(occupancyMaterialGeometrySetUses))
    ;
    bool occupancyRecorded = false;
    const GpuTaskId occupancyTask = graph.addTask<NativePacketCsgIntervalSampleProbeTask>(
        occupancyDesc,
        NativePacketCsgIntervalSampleProbeTask::Payload{
            .coverage = coverage.get(),
            .materialGeometry = occupancyMaterialGeometry.get(),
            .removedIntervalDepth = removedIntervalDepth.get(),
            .removedIntervalCapNormal = removedIntervalCapNormal.get(),
            .removedIntervalData = removedIntervalData.get(),
            .removedIntervalCount = removedIntervalCount.get(),
            .recorded = &occupancyRecorded,
        }
    );
    ASSERT_TRUE(occupancyTask.valid());

    const GpuPhysicalQueueInfo queue{
        .familyIndex = device.getQueueFamilyIndex(CommandQueue::Graphics),
        .queueIndex = 0u,
        .id = BackendQueueId(device, CommandQueue::Graphics),
        .queueClass = CommandQueue::Graphics,
        .capabilities = static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
            | static_cast<u8>(GpuQueueCapability::Transfer)
        ),
        .dedicated = false,
    };
    const GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/avboit_csg_occupancy_state_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 1u);
    const GpuSubmissionPacketId packet = views.compiled.packetForTask(preTask);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(views.compiled.packetForTask(spanTask), packet);
    EXPECT_EQ(views.compiled.packetForTask(combineTask), packet);
    EXPECT_EQ(views.compiled.packetForTask(clearTask), packet);
    EXPECT_EQ(views.compiled.packetForTask(occupancyTask), packet);
    const GpuCompiledPacketView packetPlan = views.compiled.packet(packet);
    ASSERT_TRUE(packetPlan.valid());
    ASSERT_EQ(packetPlan.plan->taskCount, 5u);
    ASSERT_NE(views.compiled.packet(packet).tasks, nullptr);
    EXPECT_EQ(views.compiled.packet(packet).tasks[0u], preTask);
    EXPECT_EQ(views.compiled.packet(packet).tasks[1u], spanTask);
    EXPECT_EQ(views.compiled.packet(packet).tasks[2u], combineTask);
    EXPECT_EQ(views.compiled.packet(packet).tasks[3u], clearTask);
    EXPECT_EQ(views.compiled.packet(packet).tasks[4u], occupancyTask);

    const GpuCompiledTaskView compiledPre = views.compiled.findTask(preTask);
    const GpuCompiledTaskView compiledSpan = views.compiled.findTask(spanTask);
    const GpuCompiledTaskView compiledCombine = views.compiled.findTask(combineTask);
    const GpuCompiledTaskView compiledClear = views.compiled.findTask(clearTask);
    const GpuCompiledTaskView compiledOccupancy = views.compiled.findTask(occupancyTask);
    ASSERT_TRUE(compiledPre.valid());
    ASSERT_TRUE(compiledSpan.valid());
    ASSERT_TRUE(compiledCombine.valid());
    ASSERT_TRUE(compiledClear.valid());
    ASSERT_TRUE(compiledOccupancy.valid());
    ASSERT_EQ(compiledPre.plan->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledSpan.plan->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledCombine.plan->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledClear.plan->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledOccupancy.plan->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledPre.plan->prologueBarrierCount, 6u);
    ASSERT_EQ(compiledSpan.plan->prologueBarrierCount, 4u);
    ASSERT_EQ(compiledCombine.plan->prologueBarrierCount, 9u);
    ASSERT_EQ(compiledClear.plan->prologueBarrierCount, 1u);
    ASSERT_EQ(compiledOccupancy.plan->prologueBarrierCount, 6u);
    const GpuCompiledBarrier* const preBarriers = views.compiled.findTask(preTask).prologueBarriers;
    const GpuCompiledBarrier* const spanBarriers = views.compiled.findTask(spanTask).prologueBarriers;
    const GpuCompiledBarrier* const combineBarriers = views.compiled.findTask(combineTask).prologueBarriers;
    const GpuCompiledBarrier* const clearBarriers = views.compiled.findTask(clearTask).prologueBarriers;
    const GpuCompiledBarrier* const occupancyBarriers = views.compiled.findTask(occupancyTask).prologueBarriers;
    ASSERT_NE(preBarriers, nullptr);
    ASSERT_NE(spanBarriers, nullptr);
    ASSERT_NE(combineBarriers, nullptr);
    ASSERT_NE(clearBarriers, nullptr);
    ASSERT_NE(occupancyBarriers, nullptr);
    const auto hasTextureTransition = [](const GpuCompiledBarrier* barriers, const u32 barrierCount, const GpuGraphResourceId resource, const TextureSubresourceSet& range, const ResourceStates::Mask before, const ResourceStates::Mask after){
        for(u32 barrierIndex = 0u; barrierIndex < barrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == GpuCompiledBarrierType::TextureTransition
                && barrier.resource == resource
                && barrier.range.textureSubresources == range
                && barrier.before == before
                && barrier.after == after
            )
                return true;
        }
        return false;
    };
    const auto hasTextureUav = [](const GpuCompiledBarrier* barriers, const u32 barrierCount, const GpuGraphResourceId resource, const TextureSubresourceSet& range){
        for(u32 barrierIndex = 0u; barrierIndex < barrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == GpuCompiledBarrierType::TextureUav
                && barrier.resource == resource
                && barrier.range.textureSubresources == range
                && barrier.before == ResourceStates::UnorderedAccess
                && barrier.after == ResourceStates::UnorderedAccess
            )
                return true;
        }
        return false;
    };
    EXPECT_TRUE(hasTextureTransition(preBarriers, compiledPre.plan->prologueBarrierCount, capBackNormalResource, peelRange, ResourceStates::Common, ResourceStates::UnorderedAccess));
    EXPECT_TRUE(hasTextureTransition(preBarriers, compiledPre.plan->prologueBarrierCount, intervalDepthResource, peelRange, ResourceStates::Common, ResourceStates::UnorderedAccess));
    EXPECT_TRUE(hasTextureTransition(preBarriers, compiledPre.plan->prologueBarrierCount, intervalIdResource, peelRange, ResourceStates::Common, ResourceStates::UnorderedAccess));
    EXPECT_TRUE(hasTextureTransition(preBarriers, compiledPre.plan->prologueBarrierCount, receiverEventDataResource, receiverEventRange, ResourceStates::Common, ResourceStates::UnorderedAccess));
    EXPECT_TRUE(hasTextureTransition(preBarriers, compiledPre.plan->prologueBarrierCount, receiverEventCountResource, receiverEventCountRange, ResourceStates::Common, ResourceStates::UnorderedAccess));
    bool materialGeometryTransition = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledPre.plan->prologueBarrierCount; ++barrierIndex){
        const GpuCompiledBarrier& barrier = preBarriers[barrierIndex];
        if(
            barrier.type == GpuCompiledBarrierType::BufferTransition
            && barrier.resource == materialGeometryResource
            && barrier.before == ResourceStates::Common
            && barrier.after == ResourceStates::ShaderResource
        )
            materialGeometryTransition = true;
    }
    EXPECT_TRUE(materialGeometryTransition);
    EXPECT_TRUE(hasTextureUav(spanBarriers, compiledSpan.plan->prologueBarrierCount, receiverEventDataResource, receiverEventRange));
    EXPECT_TRUE(hasTextureUav(spanBarriers, compiledSpan.plan->prologueBarrierCount, receiverEventCountResource, receiverEventCountRange));
    EXPECT_TRUE(hasTextureTransition(spanBarriers, compiledSpan.plan->prologueBarrierCount, receiverSpanDataResource, receiverSpanRange, ResourceStates::Common, ResourceStates::UnorderedAccess));
    EXPECT_TRUE(hasTextureTransition(spanBarriers, compiledSpan.plan->prologueBarrierCount, receiverSpanCountResource, receiverSpanCountRange, ResourceStates::Common, ResourceStates::UnorderedAccess));
    EXPECT_TRUE(hasTextureUav(combineBarriers, compiledCombine.plan->prologueBarrierCount, capBackNormalResource, peelRange));
    EXPECT_TRUE(hasTextureUav(combineBarriers, compiledCombine.plan->prologueBarrierCount, intervalDepthResource, peelRange));
    EXPECT_TRUE(hasTextureUav(combineBarriers, compiledCombine.plan->prologueBarrierCount, intervalIdResource, peelRange));
    EXPECT_TRUE(hasTextureUav(combineBarriers, compiledCombine.plan->prologueBarrierCount, receiverSpanDataResource, receiverSpanRange));
    EXPECT_TRUE(hasTextureUav(combineBarriers, compiledCombine.plan->prologueBarrierCount, receiverSpanCountResource, receiverSpanCountRange));
    EXPECT_TRUE(hasTextureTransition(combineBarriers, compiledCombine.plan->prologueBarrierCount, removedIntervalDepthResource, removedIntervalRange, ResourceStates::Common, ResourceStates::UnorderedAccess));
    EXPECT_TRUE(hasTextureTransition(combineBarriers, compiledCombine.plan->prologueBarrierCount, removedIntervalCapNormalResource, removedIntervalRange, ResourceStates::Common, ResourceStates::UnorderedAccess));
    EXPECT_TRUE(hasTextureTransition(combineBarriers, compiledCombine.plan->prologueBarrierCount, removedIntervalDataResource, removedIntervalRange, ResourceStates::Common, ResourceStates::UnorderedAccess));
    EXPECT_TRUE(hasTextureTransition(combineBarriers, compiledCombine.plan->prologueBarrierCount, removedIntervalCountResource, removedIntervalCountRange, ResourceStates::Common, ResourceStates::UnorderedAccess));
    bool coverageClearTransition = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledClear.plan->prologueBarrierCount; ++barrierIndex){
        const GpuCompiledBarrier& barrier = clearBarriers[barrierIndex];
        if(
            barrier.type == GpuCompiledBarrierType::BufferTransition
            && barrier.resource == coverageResource
            && barrier.before == ResourceStates::Common
            && barrier.after == ResourceStates::CopyDest
        )
            coverageClearTransition = true;
    }
    EXPECT_TRUE(coverageClearTransition);
    bool coverageTransition = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledOccupancy.plan->prologueBarrierCount; ++barrierIndex){
        const GpuCompiledBarrier& barrier = occupancyBarriers[barrierIndex];
        if(
            barrier.type == GpuCompiledBarrierType::BufferTransition
            && barrier.resource == coverageResource
            && barrier.before == ResourceStates::CopyDest
            && barrier.after == ResourceStates::UnorderedAccess
        )
            coverageTransition = true;
    }
    bool occupancyMaterialGeometryTransition = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledOccupancy.plan->prologueBarrierCount; ++barrierIndex){
        const GpuCompiledBarrier& barrier = occupancyBarriers[barrierIndex];
        if(
            barrier.type == GpuCompiledBarrierType::BufferTransition
            && barrier.resource == occupancyMaterialGeometryResource
            && barrier.before == ResourceStates::Common
            && barrier.after == ResourceStates::ShaderResource
        )
            occupancyMaterialGeometryTransition = true;
    }
    EXPECT_TRUE(hasTextureUav(occupancyBarriers, compiledOccupancy.plan->prologueBarrierCount, removedIntervalDepthResource, removedIntervalRange));
    EXPECT_TRUE(hasTextureUav(occupancyBarriers, compiledOccupancy.plan->prologueBarrierCount, removedIntervalCapNormalResource, removedIntervalRange));
    EXPECT_TRUE(hasTextureUav(occupancyBarriers, compiledOccupancy.plan->prologueBarrierCount, removedIntervalDataResource, removedIntervalRange));
    EXPECT_TRUE(hasTextureUav(occupancyBarriers, compiledOccupancy.plan->prologueBarrierCount, removedIntervalCountResource, removedIntervalCountRange));
    EXPECT_TRUE(coverageTransition);
    EXPECT_TRUE(occupancyMaterialGeometryTransition);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    GpuSubmissionPacketId failedPacket;
    const bool packetRecorded = recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        views.compiled.allPacketRange(),
        recordedGraph,
        &failedPacket
    );
    EXPECT_TRUE(preRecorded);
    EXPECT_TRUE(spanRecorded);
    EXPECT_TRUE(combineRecorded);
    EXPECT_TRUE(clearRecorded);
    EXPECT_TRUE(occupancyRecorded);
    ASSERT_TRUE(packetRecorded) << "failed packet " << failedPacket.index;

    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuTaskScheduler submitter(device);
    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        views.compiled.allPacketRange(),
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    EXPECT_TRUE(transaction.packetToken(packet).valid());
    EXPECT_TRUE(device.waitForIdle());
}


// AVBOIT Extinction consumes the transparent CSG interval aliases after its optional depth-warp gap. Its exact
// selected mesh buffers must reach ShaderResource through the immutable graph set before the native callback runs.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedAvboitExtinctionMaterialGeometryStateRecordsWithoutNativeBridge){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto materialGeometry = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
    );
    const auto makeStorageArray = [&device](const u32 arraySize){
        return device.createTexture(
            TextureDesc()
                .setWidth(4u)
                .setHeight(4u)
                .setArraySize(arraySize)
                .setDimension(TextureDimension::Texture2DArray)
                .setFormat(Format::RGBA32_UINT)
                .setInUAV(true)
                .setInitialState(ResourceStates::Common)
        );
    };
    auto removedIntervalDepth = makeStorageArray(16u);
    auto removedIntervalCapNormal = makeStorageArray(16u);
    auto removedIntervalData = makeStorageArray(16u);
    auto removedIntervalCount = makeStorageArray(1u);
    ASSERT_NE(materialGeometry.get(), nullptr);
    ASSERT_NE(removedIntervalDepth.get(), nullptr);
    ASSERT_NE(removedIntervalCapNormal.get(), nullptr);
    ASSERT_NE(removedIntervalData.get(), nullptr);
    ASSERT_NE(removedIntervalCount.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId materialGeometryResource = graph.importBuffer(
        materialGeometry,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/avboit_extinction_material_geometry"))
            .setMarkerLabel("AVBOIT Extinction Material Geometry")
            .setType(GpuGraphResourceType::Buffer)
    );
    const GpuGraphResourceId removedIntervalDepthResource = graph.importTexture(
        removedIntervalDepth,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/avboit_extinction_removed_interval_depth"))
            .setMarkerLabel("AVBOIT Extinction Removed Interval Depth")
            .setType(GpuGraphResourceType::Texture)
    );
    const GpuGraphResourceId removedIntervalCapNormalResource = graph.importTexture(
        removedIntervalCapNormal,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/avboit_extinction_removed_interval_cap_normal"))
            .setMarkerLabel("AVBOIT Extinction Removed Interval Cap Normal")
            .setType(GpuGraphResourceType::Texture)
    );
    const GpuGraphResourceId removedIntervalDataResource = graph.importTexture(
        removedIntervalData,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/avboit_extinction_removed_interval_data"))
            .setMarkerLabel("AVBOIT Extinction Removed Interval Data")
            .setType(GpuGraphResourceType::Texture)
    );
    const GpuGraphResourceId removedIntervalCountResource = graph.importTexture(
        removedIntervalCount,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/avboit_extinction_removed_interval_count"))
            .setMarkerLabel("AVBOIT Extinction Removed Interval Count")
            .setType(GpuGraphResourceType::Texture)
    );
    ASSERT_TRUE(materialGeometryResource.valid());
    ASSERT_TRUE(removedIntervalDepthResource.valid());
    ASSERT_TRUE(removedIntervalCapNormalResource.valid());
    ASSERT_TRUE(removedIntervalDataResource.valid());
    ASSERT_TRUE(removedIntervalCountResource.valid());

    const TextureSubresourceSet removedIntervalRange(0u, 1u, 0u, 16u);
    const TextureSubresourceSet removedIntervalCountRange(0u, 1u, 0u, 1u);
    const GpuTaskResourceUse extinctionUses[] = {
        GpuTaskResourceUse{
            .resource = removedIntervalDepthResource,
            .range = GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = removedIntervalCapNormalResource,
            .range = GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = removedIntervalDataResource,
            .range = GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = removedIntervalCountResource,
            .range = GpuTaskResourceRange{ .textureSubresources = removedIntervalCountRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    const GpuGraphResourceSetId materialGeometrySet = graph.importResourceSet(
        GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/descriptor_buffer/avboit_extinction_material_geometry_set"))
            .setMarkerLabel("AVBOIT Extinction Material Geometry")
            .setMembers(&materialGeometryResource, 1u)
    );
    ASSERT_TRUE(materialGeometrySet.valid());
    const GpuTaskResourceSetUse materialGeometrySetUses[] = {
        GpuTaskResourceSetUse{
            .resourceSet = materialGeometrySet,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    const GpuQueueRequest graphicsQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint extinctionScheduling;
    extinctionScheduling.cost = GpuTaskCostHint::Large;
    extinctionScheduling.forceSubmissionBoundary = false;
    extinctionScheduling.allowPacketMerge = true;
    GpuTaskDesc extinctionDesc;
    extinctionDesc
        .setIdentity(Name("tests/descriptor_buffer/avboit_extinction_csg_sample"))
        .setMarkerLabel("AVBOIT Extinction")
        .setQueue(graphicsQueue)
        .setScheduling(extinctionScheduling)
        .setResourceUses(extinctionUses, LengthOf(extinctionUses))
        .setResourceSetUses(materialGeometrySetUses, LengthOf(materialGeometrySetUses))
    ;
    bool extinctionRecorded = false;
    const GpuTaskId extinctionTask = graph.addTask<NativePacketCsgIntervalSampleProbeTask>(
        extinctionDesc,
        NativePacketCsgIntervalSampleProbeTask::Payload{
            .materialGeometry = materialGeometry.get(),
            .removedIntervalDepth = removedIntervalDepth.get(),
            .removedIntervalCapNormal = removedIntervalCapNormal.get(),
            .removedIntervalData = removedIntervalData.get(),
            .removedIntervalCount = removedIntervalCount.get(),
            .recorded = &extinctionRecorded,
        }
    );
    ASSERT_TRUE(extinctionTask.valid());

    const GpuPhysicalQueueInfo queue{
        .familyIndex = device.getQueueFamilyIndex(CommandQueue::Graphics),
        .queueIndex = 0u,
        .id = BackendQueueId(device, CommandQueue::Graphics),
        .queueClass = CommandQueue::Graphics,
        .capabilities = static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
            | static_cast<u8>(GpuQueueCapability::Transfer)
        ),
        .dedicated = false,
    };
    const GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/avboit_extinction_state_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 1u);
    const GpuSubmissionPacketId packet = views.compiled.packetForTask(extinctionTask);
    ASSERT_TRUE(packet.valid());
    const GpuCompiledTaskView compiledExtinction = views.compiled.findTask(extinctionTask);
    ASSERT_TRUE(compiledExtinction.valid());
    ASSERT_EQ(compiledExtinction.plan->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledExtinction.plan->prologueBarrierCount, 5u);
    const GpuCompiledBarrier* const extinctionBarriers = views.compiled.findTask(extinctionTask).prologueBarriers;
    ASSERT_NE(extinctionBarriers, nullptr);
    bool materialGeometryTransition = false;
    const auto hasTextureTransition = [](const GpuGraphResourceId resource, const TextureSubresourceSet& range, const GpuCompiledBarrier* barriers, const u32 barrierCount){
        for(u32 barrierIndex = 0u; barrierIndex < barrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == GpuCompiledBarrierType::TextureTransition
                && barrier.resource == resource
                && barrier.range.textureSubresources == range
                && barrier.before == ResourceStates::Common
                && barrier.after == ResourceStates::UnorderedAccess
            )
                return true;
        }
        return false;
    };
    for(u32 barrierIndex = 0u; barrierIndex < compiledExtinction.plan->prologueBarrierCount; ++barrierIndex){
        const GpuCompiledBarrier& barrier = extinctionBarriers[barrierIndex];
        if(
            barrier.type == GpuCompiledBarrierType::BufferTransition
            && barrier.resource == materialGeometryResource
            && barrier.before == ResourceStates::Common
            && barrier.after == ResourceStates::ShaderResource
        )
            materialGeometryTransition = true;
    }
    EXPECT_TRUE(hasTextureTransition(removedIntervalDepthResource, removedIntervalRange, extinctionBarriers, compiledExtinction.plan->prologueBarrierCount));
    EXPECT_TRUE(hasTextureTransition(removedIntervalCapNormalResource, removedIntervalRange, extinctionBarriers, compiledExtinction.plan->prologueBarrierCount));
    EXPECT_TRUE(hasTextureTransition(removedIntervalDataResource, removedIntervalRange, extinctionBarriers, compiledExtinction.plan->prologueBarrierCount));
    EXPECT_TRUE(hasTextureTransition(removedIntervalCountResource, removedIntervalCountRange, extinctionBarriers, compiledExtinction.plan->prologueBarrierCount));
    EXPECT_TRUE(materialGeometryTransition);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    GpuSubmissionPacketId failedPacket;
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        views.compiled.allPacketRange(),
        recordedGraph,
        &failedPacket
    )) << "failed packet " << failedPacket.index;
    EXPECT_TRUE(extinctionRecorded);

    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuTaskScheduler submitter(device);
    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        views.compiled.allPacketRange(),
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    EXPECT_TRUE(transaction.packetToken(packet).valid());
    EXPECT_TRUE(device.waitForIdle());
}


// AVBOIT Accumulation consumes the transparent CSG interval aliases after the extinction/integration sequence. Its
// exact selected mesh buffers must reach ShaderResource through the immutable graph set before the native callback.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedAvboitAccumulationMaterialGeometryStateRecordsWithoutNativeBridge){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto materialGeometry = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
    );
    const auto makeStorageArray = [&device](const u32 arraySize){
        return device.createTexture(
            TextureDesc()
                .setWidth(4u)
                .setHeight(4u)
                .setArraySize(arraySize)
                .setDimension(TextureDimension::Texture2DArray)
                .setFormat(Format::RGBA32_UINT)
                .setInUAV(true)
                .setInitialState(ResourceStates::Common)
        );
    };
    auto removedIntervalDepth = makeStorageArray(16u);
    auto removedIntervalCapNormal = makeStorageArray(16u);
    auto removedIntervalData = makeStorageArray(16u);
    auto removedIntervalCount = makeStorageArray(1u);
    ASSERT_NE(materialGeometry.get(), nullptr);
    ASSERT_NE(removedIntervalDepth.get(), nullptr);
    ASSERT_NE(removedIntervalCapNormal.get(), nullptr);
    ASSERT_NE(removedIntervalData.get(), nullptr);
    ASSERT_NE(removedIntervalCount.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId materialGeometryResource = graph.importBuffer(
        materialGeometry,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/avboit_accumulation_material_geometry"))
            .setMarkerLabel("AVBOIT Accumulation Material Geometry")
            .setType(GpuGraphResourceType::Buffer)
    );
    const GpuGraphResourceId removedIntervalDepthResource = graph.importTexture(
        removedIntervalDepth,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/avboit_accumulation_removed_interval_depth"))
            .setMarkerLabel("AVBOIT Accumulation Removed Interval Depth")
            .setType(GpuGraphResourceType::Texture)
    );
    const GpuGraphResourceId removedIntervalCapNormalResource = graph.importTexture(
        removedIntervalCapNormal,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/avboit_accumulation_removed_interval_cap_normal"))
            .setMarkerLabel("AVBOIT Accumulation Removed Interval Cap Normal")
            .setType(GpuGraphResourceType::Texture)
    );
    const GpuGraphResourceId removedIntervalDataResource = graph.importTexture(
        removedIntervalData,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/avboit_accumulation_removed_interval_data"))
            .setMarkerLabel("AVBOIT Accumulation Removed Interval Data")
            .setType(GpuGraphResourceType::Texture)
    );
    const GpuGraphResourceId removedIntervalCountResource = graph.importTexture(
        removedIntervalCount,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/avboit_accumulation_removed_interval_count"))
            .setMarkerLabel("AVBOIT Accumulation Removed Interval Count")
            .setType(GpuGraphResourceType::Texture)
    );
    ASSERT_TRUE(materialGeometryResource.valid());
    ASSERT_TRUE(removedIntervalDepthResource.valid());
    ASSERT_TRUE(removedIntervalCapNormalResource.valid());
    ASSERT_TRUE(removedIntervalDataResource.valid());
    ASSERT_TRUE(removedIntervalCountResource.valid());

    const TextureSubresourceSet removedIntervalRange(0u, 1u, 0u, 16u);
    const TextureSubresourceSet removedIntervalCountRange(0u, 1u, 0u, 1u);
    const GpuTaskResourceUse accumulationUses[] = {
        GpuTaskResourceUse{
            .resource = removedIntervalDepthResource,
            .range = GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = removedIntervalCapNormalResource,
            .range = GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = removedIntervalDataResource,
            .range = GpuTaskResourceRange{ .textureSubresources = removedIntervalRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = removedIntervalCountResource,
            .range = GpuTaskResourceRange{ .textureSubresources = removedIntervalCountRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    const GpuGraphResourceSetId materialGeometrySet = graph.importResourceSet(
        GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/descriptor_buffer/avboit_accumulation_material_geometry_set"))
            .setMarkerLabel("AVBOIT Accumulation Material Geometry")
            .setMembers(&materialGeometryResource, 1u)
    );
    ASSERT_TRUE(materialGeometrySet.valid());
    const GpuTaskResourceSetUse materialGeometrySetUses[] = {
        GpuTaskResourceSetUse{
            .resourceSet = materialGeometrySet,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    const GpuQueueRequest graphicsQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    GpuTaskSchedulingHint accumulationScheduling;
    accumulationScheduling.cost = GpuTaskCostHint::Large;
    accumulationScheduling.forceSubmissionBoundary = false;
    accumulationScheduling.allowPacketMerge = true;
    GpuTaskDesc accumulationDesc;
    accumulationDesc
        .setIdentity(Name("tests/descriptor_buffer/avboit_accumulation_csg_sample"))
        .setMarkerLabel("AVBOIT Accumulation")
        .setQueue(graphicsQueue)
        .setScheduling(accumulationScheduling)
        .setResourceUses(accumulationUses, LengthOf(accumulationUses))
        .setResourceSetUses(materialGeometrySetUses, LengthOf(materialGeometrySetUses))
    ;
    bool accumulationRecorded = false;
    const GpuTaskId accumulationTask = graph.addTask<NativePacketCsgIntervalSampleProbeTask>(
        accumulationDesc,
        NativePacketCsgIntervalSampleProbeTask::Payload{
            .materialGeometry = materialGeometry.get(),
            .removedIntervalDepth = removedIntervalDepth.get(),
            .removedIntervalCapNormal = removedIntervalCapNormal.get(),
            .removedIntervalData = removedIntervalData.get(),
            .removedIntervalCount = removedIntervalCount.get(),
            .recorded = &accumulationRecorded,
        }
    );
    ASSERT_TRUE(accumulationTask.valid());

    const GpuPhysicalQueueInfo queue{
        .familyIndex = device.getQueueFamilyIndex(CommandQueue::Graphics),
        .queueIndex = 0u,
        .id = BackendQueueId(device, CommandQueue::Graphics),
        .queueClass = CommandQueue::Graphics,
        .capabilities = static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
            | static_cast<u8>(GpuQueueCapability::Transfer)
        ),
        .dedicated = false,
    };
    const GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/avboit_accumulation_state_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 1u);
    const GpuSubmissionPacketId packet = views.compiled.packetForTask(accumulationTask);
    ASSERT_TRUE(packet.valid());
    const GpuCompiledTaskView compiledAccumulation = views.compiled.findTask(accumulationTask);
    ASSERT_TRUE(compiledAccumulation.valid());
    ASSERT_EQ(compiledAccumulation.plan->prologueStateSeedCount, 0u);
    ASSERT_EQ(compiledAccumulation.plan->prologueBarrierCount, 5u);
    const GpuCompiledBarrier* const accumulationBarriers = views.compiled.findTask(accumulationTask).prologueBarriers;
    ASSERT_NE(accumulationBarriers, nullptr);
    bool materialGeometryTransition = false;
    const auto hasTextureTransition = [](const GpuGraphResourceId resource, const TextureSubresourceSet& range, const GpuCompiledBarrier* barriers, const u32 barrierCount){
        for(u32 barrierIndex = 0u; barrierIndex < barrierCount; ++barrierIndex){
            const GpuCompiledBarrier& barrier = barriers[barrierIndex];
            if(
                barrier.type == GpuCompiledBarrierType::TextureTransition
                && barrier.resource == resource
                && barrier.range.textureSubresources == range
                && barrier.before == ResourceStates::Common
                && barrier.after == ResourceStates::UnorderedAccess
            )
                return true;
        }
        return false;
    };
    for(u32 barrierIndex = 0u; barrierIndex < compiledAccumulation.plan->prologueBarrierCount; ++barrierIndex){
        const GpuCompiledBarrier& barrier = accumulationBarriers[barrierIndex];
        if(
            barrier.type == GpuCompiledBarrierType::BufferTransition
            && barrier.resource == materialGeometryResource
            && barrier.before == ResourceStates::Common
            && barrier.after == ResourceStates::ShaderResource
        )
            materialGeometryTransition = true;
    }
    EXPECT_TRUE(hasTextureTransition(removedIntervalDepthResource, removedIntervalRange, accumulationBarriers, compiledAccumulation.plan->prologueBarrierCount));
    EXPECT_TRUE(hasTextureTransition(removedIntervalCapNormalResource, removedIntervalRange, accumulationBarriers, compiledAccumulation.plan->prologueBarrierCount));
    EXPECT_TRUE(hasTextureTransition(removedIntervalDataResource, removedIntervalRange, accumulationBarriers, compiledAccumulation.plan->prologueBarrierCount));
    EXPECT_TRUE(hasTextureTransition(removedIntervalCountResource, removedIntervalCountRange, accumulationBarriers, compiledAccumulation.plan->prologueBarrierCount));
    EXPECT_TRUE(materialGeometryTransition);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    const GpuNativePacketRecorder recorder(device);
    GpuSubmissionPacketId failedPacket;
    ASSERT_TRUE(recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        views.compiled.allPacketRange(),
        recordedGraph,
        &failedPacket
    )) << "failed packet " << failedPacket.index;
    EXPECT_TRUE(accumulationRecorded);

    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuTaskScheduler submitter(device);
    ASSERT_TRUE(submitter.submitPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        views.compiled.allPacketRange(),
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena
    ));
    EXPECT_TRUE(transaction.packetToken(packet).valid());
    EXPECT_TRUE(device.waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

