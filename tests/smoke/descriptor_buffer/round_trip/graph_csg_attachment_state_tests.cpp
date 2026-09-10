// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "csg_probes_test_support.h"
#include "graph_resources_test_support.h"
#include "packet_recording_test_support.h"
#include "packet_retry_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Opaque CSG now lowers G-buffer event production, receiver-span build, interval combine, and material/cap sampling
// as graph tasks. Record the whole same-UAV chain on a real Graphics packet so Span, Combine, and Sample cannot rely
// on a native state bridge.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedOpaqueCsgReceiverSpanCombineAndSampleStatesRecordWithoutNativeBridge){
    auto& device = DescriptorBufferRoundTripTest::device();
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
    auto materialGeometry = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setInitialState(ResourceStates::Common)
    );
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
    ASSERT_NE(materialGeometry.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId capBackNormalResource = graph.importTexture(
        capBackNormal,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/opaque_csg_combine_cap_back_normal"))
            .setMarkerLabel("Opaque CSG Combine Cap Back Normal")
            .setType(GpuGraphResourceType::Texture)
    );
    const GpuGraphResourceId intervalDepthResource = graph.importTexture(
        intervalDepth,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/opaque_csg_combine_interval_depth"))
            .setMarkerLabel("Opaque CSG Combine Interval Depth")
            .setType(GpuGraphResourceType::Texture)
    );
    const GpuGraphResourceId intervalIdResource = graph.importTexture(
        intervalId,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/opaque_csg_combine_interval_id"))
            .setMarkerLabel("Opaque CSG Combine Interval Id")
            .setType(GpuGraphResourceType::Texture)
    );
    const GpuGraphResourceId receiverEventDataResource = graph.importTexture(
        receiverEventData,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/opaque_csg_span_receiver_event_data"))
            .setMarkerLabel("Opaque CSG Span Receiver Event Data")
            .setType(GpuGraphResourceType::Texture)
    );
    const GpuGraphResourceId receiverEventCountResource = graph.importTexture(
        receiverEventCount,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/opaque_csg_span_receiver_event_count"))
            .setMarkerLabel("Opaque CSG Span Receiver Event Count")
            .setType(GpuGraphResourceType::Texture)
    );
    const GpuGraphResourceId receiverSpanDataResource = graph.importTexture(
        receiverSpanData,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/opaque_csg_combine_receiver_span_data"))
            .setMarkerLabel("Opaque CSG Combine Receiver Span Data")
            .setType(GpuGraphResourceType::Texture)
    );
    const GpuGraphResourceId receiverSpanCountResource = graph.importTexture(
        receiverSpanCount,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/opaque_csg_combine_receiver_span_count"))
            .setMarkerLabel("Opaque CSG Combine Receiver Span Count")
            .setType(GpuGraphResourceType::Texture)
    );
    const GpuGraphResourceId removedIntervalDepthResource = graph.importTexture(
        removedIntervalDepth,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/opaque_csg_combine_removed_interval_depth"))
            .setMarkerLabel("Opaque CSG Combine Removed Interval Depth")
            .setType(GpuGraphResourceType::Texture)
    );
    const GpuGraphResourceId removedIntervalCapNormalResource = graph.importTexture(
        removedIntervalCapNormal,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/opaque_csg_combine_removed_interval_cap_normal"))
            .setMarkerLabel("Opaque CSG Combine Removed Interval Cap Normal")
            .setType(GpuGraphResourceType::Texture)
    );
    const GpuGraphResourceId removedIntervalDataResource = graph.importTexture(
        removedIntervalData,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/opaque_csg_combine_removed_interval_data"))
            .setMarkerLabel("Opaque CSG Combine Removed Interval Data")
            .setType(GpuGraphResourceType::Texture)
    );
    const GpuGraphResourceId removedIntervalCountResource = graph.importTexture(
        removedIntervalCount,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/opaque_csg_combine_removed_interval_count"))
            .setMarkerLabel("Opaque CSG Combine Removed Interval Count")
            .setType(GpuGraphResourceType::Texture)
    );
    const GpuGraphResourceId materialGeometryResource = graph.importBuffer(
        materialGeometry,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/opaque_csg_sample_material_geometry"))
            .setMarkerLabel("Opaque CSG Material Geometry")
            .setType(GpuGraphResourceType::Buffer)
    );
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

    const TextureSubresourceSet peelRange(0u, 1u, 0u, 4u);
    const TextureSubresourceSet receiverEventRange(0u, 1u, 0u, 32u);
    const TextureSubresourceSet receiverEventCountRange(0u, 1u, 0u, 1u);
    const TextureSubresourceSet receiverSpanRange(0u, 1u, 0u, 16u);
    const TextureSubresourceSet receiverSpanCountRange(0u, 1u, 0u, 1u);
    const TextureSubresourceSet removedIntervalRange(0u, 1u, 0u, 16u);
    const TextureSubresourceSet removedIntervalCountRange(0u, 1u, 0u, 1u);
    const GpuTaskResourceUse producerUses[] = {
        GpuTaskResourceUse{
            .resource = capBackNormalResource,
            .range = GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
        GpuTaskResourceUse{
            .resource = intervalDepthResource,
            .range = GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
        GpuTaskResourceUse{
            .resource = intervalIdResource,
            .range = GpuTaskResourceRange{ .textureSubresources = peelRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
        GpuTaskResourceUse{
            .resource = receiverEventDataResource,
            .range = GpuTaskResourceRange{ .textureSubresources = receiverEventRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
        },
        GpuTaskResourceUse{
            .resource = receiverEventCountResource,
            .range = GpuTaskResourceRange{ .textureSubresources = receiverEventCountRange },
            .requiredState = ResourceStates::UnorderedAccess,
            .access = GpuTaskResourceAccess::Write,
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
    const GpuTaskResourceUse sampleUses[] = {
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
    const GpuGraphResourceSetId sampleMaterialGeometrySet = graph.importResourceSet(
        GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/descriptor_buffer/opaque_csg_sample_material_geometry_set"))
            .setMarkerLabel("Opaque CSG Material Geometry")
            .setMembers(&materialGeometryResource, 1u)
    );
    ASSERT_TRUE(sampleMaterialGeometrySet.valid());
    const GpuTaskResourceSetUse sampleMaterialGeometrySetUses[] = {
        GpuTaskResourceSetUse{
            .resourceSet = sampleMaterialGeometrySet,
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
    GpuTaskSchedulingHint producerScheduling;
    producerScheduling.cost = GpuTaskCostHint::Medium;
    producerScheduling.forceSubmissionBoundary = false;
    producerScheduling.allowPacketMerge = true;
    GpuTaskDesc producerDesc;
    producerDesc
        .setIdentity(Name("tests/descriptor_buffer/opaque_csg_interval_producer"))
        .setMarkerLabel("Opaque CSG Interval Producer")
        .setQueue(graphicsQueue)
        .setScheduling(producerScheduling)
        .setResourceUses(producerUses, LengthOf(producerUses))
    ;
    bool producerShouldRecord = true;
    bool producerAttempted = false;
    const GpuTaskId producerTask = graph.addTask<NativePacketCaptureRetryTask>(
        producerDesc,
        NativePacketCaptureRetryTask::Payload{
            .shouldRecord = &producerShouldRecord,
            .attempted = &producerAttempted,
        }
    );
    ASSERT_TRUE(producerTask.valid());

    GpuTaskSchedulingHint spanScheduling = producerScheduling;
    spanScheduling.mergeWithPrevious = true;
    GpuTaskDesc spanDesc;
    spanDesc
        .setIdentity(Name("tests/descriptor_buffer/opaque_csg_receiver_span"))
        .setMarkerLabel("Opaque CSG Receiver Span")
        .setQueue(graphicsQueue)
        .setScheduling(spanScheduling)
        .setDependencies(&producerTask, 1u)
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

    GpuTaskSchedulingHint combineScheduling = producerScheduling;
    combineScheduling.mergeWithPrevious = true;
    GpuTaskDesc combineDesc;
    combineDesc
        .setIdentity(Name("tests/descriptor_buffer/opaque_csg_interval_combine"))
        .setMarkerLabel("Opaque CSG Interval Combine")
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

    GpuTaskSchedulingHint sampleScheduling = combineScheduling;
    GpuTaskDesc sampleDesc;
    sampleDesc
        .setIdentity(Name("tests/descriptor_buffer/opaque_csg_interval_sample"))
        .setMarkerLabel("Opaque CSG Interval Sample")
        .setQueue(graphicsQueue)
        .setScheduling(sampleScheduling)
        .setDependencies(&combineTask, 1u)
        .setResourceUses(sampleUses, LengthOf(sampleUses))
        .setResourceSetUses(sampleMaterialGeometrySetUses, LengthOf(sampleMaterialGeometrySetUses))
    ;
    bool sampleRecorded = false;
    const GpuTaskId sampleTask = graph.addTask<NativePacketCsgIntervalSampleProbeTask>(
        sampleDesc,
        NativePacketCsgIntervalSampleProbeTask::Payload{
            .materialGeometry = materialGeometry.get(),
            .removedIntervalDepth = removedIntervalDepth.get(),
            .removedIntervalCapNormal = removedIntervalCapNormal.get(),
            .removedIntervalData = removedIntervalData.get(),
            .removedIntervalCount = removedIntervalCount.get(),
            .recorded = &sampleRecorded,
        }
    );
    ASSERT_TRUE(sampleTask.valid());

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
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/opaque_csg_combine_state_scratch"));
    const GpuTaskGraphCompiler compiler;
    GpuTaskGraphCompileOptions compileOptions;
    compileOptions.packetizationPolicy = GpuTaskGraphPacketizationPolicy::FrontierSafe;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena, compileOptions));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 1u);
    const GpuSubmissionPacketId packet = views.compiled.packetForTask(producerTask);
    ASSERT_TRUE(packet.valid());
    EXPECT_EQ(views.compiled.packetForTask(spanTask), packet);
    EXPECT_EQ(views.compiled.packetForTask(combineTask), packet);
    EXPECT_EQ(views.compiled.packetForTask(sampleTask), packet);
    const GpuCompiledPacketView packetPlan = views.compiled.packet(packet);
    ASSERT_TRUE(packetPlan.valid());
    ASSERT_EQ(packetPlan.plan->taskCount, 4u);
    ASSERT_NE(views.compiled.packet(packet).tasks, nullptr);
    EXPECT_EQ(views.compiled.packet(packet).tasks[0u], producerTask);
    EXPECT_EQ(views.compiled.packet(packet).tasks[1u], spanTask);
    EXPECT_EQ(views.compiled.packet(packet).tasks[2u], combineTask);
    EXPECT_EQ(views.compiled.packet(packet).tasks[3u], sampleTask);

    const GpuCompiledTaskView compiledProducer = views.compiled.findTask(producerTask);
    const GpuCompiledTaskView compiledSpan = views.compiled.findTask(spanTask);
    const GpuCompiledTaskView compiledCombine = views.compiled.findTask(combineTask);
    const GpuCompiledTaskView compiledSample = views.compiled.findTask(sampleTask);
    ASSERT_TRUE(compiledProducer.valid());
    ASSERT_TRUE(compiledSpan.valid());
    ASSERT_TRUE(compiledCombine.valid());
    ASSERT_TRUE(compiledSample.valid());
    EXPECT_EQ(compiledProducer.plan->prologueStateSeedCount, 0u);
    EXPECT_EQ(compiledSpan.plan->prologueStateSeedCount, 0u);
    EXPECT_EQ(compiledCombine.plan->prologueStateSeedCount, 0u);
    EXPECT_EQ(compiledSample.plan->prologueStateSeedCount, 0u);
    EXPECT_EQ(compiledProducer.plan->prologueBarrierCount, 5u);
    EXPECT_EQ(compiledSpan.plan->prologueBarrierCount, 4u);
    EXPECT_EQ(compiledCombine.plan->prologueBarrierCount, 9u);
    EXPECT_EQ(compiledSample.plan->prologueBarrierCount, 5u);
    const GpuCompiledBarrier* const sampleBarriers = views.compiled.findTask(sampleTask).prologueBarriers;
    ASSERT_NE(sampleBarriers, nullptr);
    bool materialGeometryTransition = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledSample.plan->prologueBarrierCount; ++barrierIndex){
        const GpuCompiledBarrier& barrier = sampleBarriers[barrierIndex];
        if(
            barrier.type == GpuCompiledBarrierType::BufferTransition
            && barrier.resource == materialGeometryResource
            && barrier.before == ResourceStates::Common
            && barrier.after == ResourceStates::ShaderResource
        )
            materialGeometryTransition = true;
    }
    EXPECT_TRUE(materialGeometryTransition);

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
    EXPECT_TRUE(producerAttempted);
    EXPECT_TRUE(spanRecorded);
    EXPECT_TRUE(combineRecorded);
    EXPECT_TRUE(sampleRecorded);
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


// Accumulation finishes Graphics raster work, but Deferred Composite may be a separate Compute packet. The normal
// path uses a no-op Graphics finalizer whose declared ShaderResource reads lower its color-attachment and read-only
// depth transitions before that packet boundary; the finalizer itself deliberately performs no native state work.
TEST_F(DescriptorBufferRoundTripTest, GraphOwnedAvboitAccumulationAttachmentStatesRecordWithoutNativeBridge){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto stateProbe = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setIsConstantBuffer(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(stateProbe.get(), nullptr);
    const TextureDesc accumulationTextureDesc = TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setFormat(Format::RGBA8_UNORM)
        .setInRenderTarget(true)
        .setInitialState(ResourceStates::Common)
    ;
    auto accumColor = device.createTexture(accumulationTextureDesc);
    auto accumExtinction = device.createTexture(accumulationTextureDesc);
    const TextureDesc depthTextureDesc = TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setFormat(Format::D32S8)
        .setInRenderTarget(true)
        .setInitialState(ResourceStates::Common)
    ;
    auto deferredDepth = device.createTexture(depthTextureDesc);
    ASSERT_NE(accumColor.get(), nullptr);
    ASSERT_NE(accumExtinction.get(), nullptr);
    ASSERT_NE(deferredDepth.get(), nullptr);
    Texture* const initialTextures[] = {
        accumColor.get(),
        accumExtinction.get(),
        deferredDepth.get(),
    };
    ASSERT_TRUE(PrimeTextureStatesForGraph(
        device,
        initialTextures,
        LengthOf(initialTextures),
        ResourceStates::Common
    ));

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId stateProbeResource = graph.importBuffer(
        stateProbe,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/avboit_accumulation_state_probe"))
            .setMarkerLabel("AVBOIT Accumulation State Probe")
            .setType(GpuGraphResourceType::Buffer)
    );
    const GpuGraphResourceId accumColorResource = graph.importTexture(
        accumColor,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/avboit_accumulation_color"))
            .setMarkerLabel("AVBOIT Accumulation Color")
            .setType(GpuGraphResourceType::Texture)
    );
    const GpuGraphResourceId accumExtinctionResource = graph.importTexture(
        accumExtinction,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/avboit_accumulation_extinction"))
            .setMarkerLabel("AVBOIT Accumulation Extinction")
            .setType(GpuGraphResourceType::Texture)
    );
    const GpuGraphResourceId deferredDepthResource = graph.importTexture(
        deferredDepth,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/avboit_deferred_depth"))
            .setMarkerLabel("Deferred Depth")
            .setType(GpuGraphResourceType::Texture)
    );
    ASSERT_TRUE(stateProbeResource.valid());
    ASSERT_TRUE(accumColorResource.valid());
    ASSERT_TRUE(accumExtinctionResource.valid());
    ASSERT_TRUE(deferredDepthResource.valid());

    const GpuQueueRequest graphicsQueue{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    const GpuQueueRequest computeQueue{
        GpuQueueCapability::Compute,
        GpuQueuePreference::Compute,
        true,
        true,
    };
    GpuTaskSchedulingHint accumulationScheduling;
    accumulationScheduling.cost = GpuTaskCostHint::Large;
    accumulationScheduling.forceSubmissionBoundary = false;
    accumulationScheduling.allowPacketMerge = true;
    GpuTaskSchedulingHint finalizeScheduling;
    finalizeScheduling.cost = GpuTaskCostHint::Tiny;
    finalizeScheduling.forceSubmissionBoundary = false;
    finalizeScheduling.allowPacketMerge = true;
    finalizeScheduling.mergeWithPrevious = true;
    GpuTaskSchedulingHint compositeScheduling;
    compositeScheduling.cost = GpuTaskCostHint::Medium;
    compositeScheduling.forceSubmissionBoundary = true;
    compositeScheduling.allowPacketMerge = false;

    const GpuTaskResourceUse accumulationUses[] = {
        GpuTaskResourceUse{
            .resource = stateProbeResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = accumColorResource,
            .range = {},
            .requiredState = ResourceStates::RenderTarget,
            .access = GpuTaskResourceAccess::Write,
        },
        GpuTaskResourceUse{
            .resource = accumExtinctionResource,
            .range = {},
            .requiredState = ResourceStates::RenderTarget,
            .access = GpuTaskResourceAccess::Write,
        },
        GpuTaskResourceUse{
            .resource = deferredDepthResource,
            .range = {},
            .requiredState = ResourceStates::DepthRead,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    GpuTaskDesc accumulationDesc;
    accumulationDesc
        .setIdentity(Name("tests/descriptor_buffer/avboit_accumulation"))
        .setMarkerLabel("AVBOIT Accumulation")
        .setQueue(graphicsQueue)
        .setScheduling(accumulationScheduling)
        .setResourceUses(accumulationUses, LengthOf(accumulationUses))
    ;
    bool accumulationRecorded = false;
    const GpuTaskId accumulationTask = graph.addTask<NativePacketPrefixTask>(
        accumulationDesc,
        NativePacketPrefixTask::Payload{
            .buffer = stateProbe.get(),
            .expectedState = ResourceStates::ConstantBuffer,
            .texture = accumColor.get(),
            .expectedTextureState = ResourceStates::RenderTarget,
            .additionalTexture = accumExtinction.get(),
            .expectedAdditionalTextureState = ResourceStates::RenderTarget,
            .thirdTexture = deferredDepth.get(),
            .expectedThirdTextureState = ResourceStates::DepthRead,
            .recorded = &accumulationRecorded,
        }
    );
    ASSERT_TRUE(accumulationTask.valid());

    const GpuTaskResourceUse finalizeUses[] = {
        GpuTaskResourceUse{
            .resource = stateProbeResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = accumColorResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = accumExtinctionResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = deferredDepthResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    GpuTaskDesc finalizeDesc;
    finalizeDesc
        .setIdentity(Name("tests/descriptor_buffer/avboit_accumulation_finalize"))
        .setMarkerLabel("AVBOIT Accumulation Finalize")
        .setQueue(graphicsQueue)
        .setScheduling(finalizeScheduling)
        .setDependencies(&accumulationTask, 1u)
        .setResourceUses(finalizeUses, LengthOf(finalizeUses))
    ;
    bool finalizerRecorded = false;
    const GpuTaskId finalizerTask = graph.addTask<NativePacketPrefixTask>(
        finalizeDesc,
        NativePacketPrefixTask::Payload{
            // This probe deliberately only observes graph-established attachment states.
            .buffer = stateProbe.get(),
            .expectedState = ResourceStates::ConstantBuffer,
            .texture = accumColor.get(),
            .expectedTextureState = ResourceStates::ShaderResource,
            .additionalTexture = accumExtinction.get(),
            .expectedAdditionalTextureState = ResourceStates::ShaderResource,
            .thirdTexture = deferredDepth.get(),
            .expectedThirdTextureState = ResourceStates::ShaderResource,
            .recorded = &finalizerRecorded,
        }
    );
    ASSERT_TRUE(finalizerTask.valid());

    const GpuTaskResourceUse compositeUses[] = {
        GpuTaskResourceUse{
            .resource = stateProbeResource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = accumColorResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = accumExtinctionResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
    };

    GpuTaskDesc compositeDesc;
    compositeDesc
        .setIdentity(Name("tests/descriptor_buffer/avboit_deferred_composite"))
        .setMarkerLabel("Deferred Composite")
        .setQueue(computeQueue)
        .setScheduling(compositeScheduling)
        .setDependencies(&finalizerTask, 1u)
        .setResourceUses(compositeUses, LengthOf(compositeUses))
    ;
    bool compositeRecorded = false;
    const GpuTaskId compositeTask = graph.addTask<NativePacketPrefixTask>(
        compositeDesc,
        NativePacketPrefixTask::Payload{
            .buffer = stateProbe.get(),
            .expectedState = ResourceStates::ConstantBuffer,
            .texture = accumColor.get(),
            .expectedTextureState = ResourceStates::ShaderResource,
            .additionalTexture = accumExtinction.get(),
            .expectedAdditionalTextureState = ResourceStates::ShaderResource,
            .recorded = &compositeRecorded,
        }
    );
    ASSERT_TRUE(compositeTask.valid());

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

    ASSERT_EQ(views.compiled.packetCount(), 2u);

    const GpuSubmissionPacketId accumulationPacket = views.compiled.packetForTask(accumulationTask);
    const GpuSubmissionPacketId finalizerPacket = views.compiled.packetForTask(finalizerTask);
    const GpuSubmissionPacketId compositePacket = views.compiled.packetForTask(compositeTask);
    ASSERT_TRUE(accumulationPacket.valid());
    ASSERT_TRUE(finalizerPacket.valid());
    ASSERT_TRUE(compositePacket.valid());
    EXPECT_EQ(finalizerPacket, accumulationPacket);
    EXPECT_NE(compositePacket, finalizerPacket);

    const GpuCompiledTaskView compiledFinalizer = views.compiled.findTask(finalizerTask);
    const GpuCompiledTaskView compiledComposite = views.compiled.findTask(compositeTask);
    ASSERT_TRUE(compiledFinalizer.valid());
    ASSERT_TRUE(compiledComposite.valid());
    ASSERT_EQ(compiledFinalizer.plan->prologueBarrierCount, 3u);
    const GpuCompiledBarrier* const finalizerBarriers = views.compiled.findTask(finalizerTask).prologueBarriers;
    ASSERT_NE(finalizerBarriers, nullptr);
    bool finalizesAccumColor = false;
    bool finalizesAccumExtinction = false;
    bool finalizesDeferredDepth = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledFinalizer.plan->prologueBarrierCount; ++barrierIndex){
        const GpuCompiledBarrier& barrier = finalizerBarriers[barrierIndex];
        const bool isAttachmentFinalization = barrier.type == GpuCompiledBarrierType::TextureTransition
            && barrier.before == ResourceStates::RenderTarget
            && barrier.after == ResourceStates::ShaderResource
        ;
        finalizesAccumColor = finalizesAccumColor || (isAttachmentFinalization && barrier.resource == accumColorResource);
        finalizesAccumExtinction = finalizesAccumExtinction
            || (isAttachmentFinalization && barrier.resource == accumExtinctionResource);
        finalizesDeferredDepth = finalizesDeferredDepth || (
            barrier.type == GpuCompiledBarrierType::TextureTransition
            && barrier.resource == deferredDepthResource
            && barrier.before == ResourceStates::DepthRead
            && barrier.after == ResourceStates::ShaderResource
        );
    }
    EXPECT_TRUE(finalizesAccumColor);
    EXPECT_TRUE(finalizesAccumExtinction);
    EXPECT_TRUE(finalizesDeferredDepth);
    const GpuCompiledBarrier* const compositeBarriers = views.compiled.findTask(compositeTask).prologueBarriers;
    for(u32 barrierIndex = 0u; barrierIndex < compiledComposite.plan->prologueBarrierCount; ++barrierIndex){
        const GpuCompiledBarrier& barrier = compositeBarriers[barrierIndex];
        EXPECT_FALSE(
            barrier.type == GpuCompiledBarrierType::TextureTransition
            && (
                barrier.before == ResourceStates::RenderTarget
                || barrier.before == ResourceStates::DepthRead
            )
        );
    }

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
    EXPECT_TRUE(finalizerRecorded);
    EXPECT_TRUE(compositeRecorded);

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
    EXPECT_TRUE(transaction.packetToken(finalizerPacket).valid());
    EXPECT_TRUE(transaction.packetToken(compositePacket).valid());
    EXPECT_TRUE(device.waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

