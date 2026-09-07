// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "acceptance_observers_test_support.h"
#include "graph_resources_test_support.h"
#include "packet_recording_test_support.h"
#include "packet_retry_test_support.h"
#include "recording_capability_test_support.h"
#include "round_trip_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(DescriptorBufferRoundTripTest, NativePacketRecordsPrefixSequenceAndExportsFinalState){
    auto& device = DescriptorBufferRoundTripTest::device();
    auto buffer = device.createBuffer(
        BufferDesc()
            .setByteSize(256u)
            .setCanHaveRawViews(true)
            .setIsConstantBuffer(true)
            .setInitialState(ResourceStates::Common)
    );
    ASSERT_NE(buffer.get(), nullptr);
    auto texture = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::RGBA8_UNORM)
            .setInRenderTarget(true)
            .setInitialState(ResourceStates::Unknown)
    );
    ASSERT_NE(texture.get(), nullptr);
    auto additionalTexture = device.createTexture(
        TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::RGBA8_UNORM)
            .setInUAV(true)
            .setInitialState(ResourceStates::Unknown)
    );
    ASSERT_NE(additionalTexture.get(), nullptr);

    GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
    const GpuGraphResourceId resource = graph.importBuffer(
        buffer,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/merged_packet_buffer"))
            .setMarkerLabel("Merged Packet Buffer")
            .setType(GpuGraphResourceType::Buffer)
    );
    ASSERT_TRUE(resource.valid());
    const GpuGraphResourceId textureResource = graph.importTexture(
        texture,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/merged_packet_texture"))
            .setMarkerLabel("Merged Packet Texture")
            .setType(GpuGraphResourceType::Texture)
    );
    ASSERT_TRUE(textureResource.valid());
    const GpuGraphResourceId additionalTextureResource = graph.importTexture(
        additionalTexture,
        GpuGraphResourceDesc{}
            .setIdentity(Name("tests/descriptor_buffer/merged_packet_additional_texture"))
            .setMarkerLabel("Merged Packet Additional Texture")
            .setType(GpuGraphResourceType::Texture)
    );
    ASSERT_TRUE(additionalTextureResource.valid());

    const GpuTaskResourceUse meshViewSetupUses[] = {
        GpuTaskResourceUse{
            .resource = resource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    GpuTaskSchedulingHint meshViewSetupScheduling;
    meshViewSetupScheduling.allowPacketMerge = true;
    GpuTaskDesc meshViewSetupDesc;
    meshViewSetupDesc
        .setIdentity(Name("tests/descriptor_buffer/native_packet_mesh_view_setup"))
        .setMarkerLabel("Native Packet Mesh View Setup")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Graphics,
            GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setScheduling(meshViewSetupScheduling)
        .setResourceUses(meshViewSetupUses, LengthOf(meshViewSetupUses))
    ;
    bool nativeMeshViewSetupRecorded = false;
    const GpuTaskId meshViewSetupTask = graph.addTask<NativePacketPrefixTask>(
        meshViewSetupDesc,
        NativePacketPrefixTask::Payload{
            .buffer = buffer.get(),
            .expectedState = ResourceStates::ConstantBuffer,
            .recorded = &nativeMeshViewSetupRecorded,
        }
    );
    ASSERT_TRUE(meshViewSetupTask.valid());

    const GpuTaskResourceUse sceneSetupUses[] = {
        GpuTaskResourceUse{
            .resource = resource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    GpuTaskSchedulingHint sceneSetupScheduling;
    sceneSetupScheduling.allowPacketMerge = true;
    sceneSetupScheduling.mergeWithPrevious = true;
    GpuTaskDesc sceneSetupDesc;
    sceneSetupDesc
        .setIdentity(Name("tests/descriptor_buffer/merged_packet_scene_setup"))
        .setMarkerLabel("Merged Packet Scene Setup")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Graphics,
            GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setScheduling(sceneSetupScheduling)
        .setDependencies(&meshViewSetupTask, 1u)
        .setResourceUses(sceneSetupUses, LengthOf(sceneSetupUses))
    ;
    bool nativeSceneSetupRecorded = false;
    const GpuTaskId sceneSetupTask = graph.addTask<NativePacketPrefixTask>(
        sceneSetupDesc,
        NativePacketPrefixTask::Payload{
            .buffer = buffer.get(),
            .expectedState = ResourceStates::ConstantBuffer,
            .recorded = &nativeSceneSetupRecorded,
        }
    );
    ASSERT_TRUE(sceneSetupTask.valid());

    const GpuTaskResourceUse clearUses[] = {
        GpuTaskResourceUse{
            .resource = resource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = textureResource,
            .range = {},
            .requiredState = ResourceStates::CopyDest,
            .access = GpuTaskResourceAccess::Write,
        },
        GpuTaskResourceUse{
            .resource = additionalTextureResource,
            .range = {},
            .requiredState = ResourceStates::CopyDest,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    GpuTaskSchedulingHint clearScheduling;
    clearScheduling.allowPacketMerge = true;
    clearScheduling.mergeWithPrevious = true;
    GpuTaskDesc clearDesc;
    clearDesc
        .setIdentity(Name("tests/descriptor_buffer/merged_packet_clear"))
        .setMarkerLabel("Merged Packet Clear")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Graphics,
            GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setScheduling(clearScheduling)
        .setDependencies(&sceneSetupTask, 1u)
        .setResourceUses(clearUses, LengthOf(clearUses))
    ;
    bool nativeClearRecorded = false;
    const GpuTaskId clearTask = graph.addTask<NativePacketPrefixTask>(
        clearDesc,
        NativePacketPrefixTask::Payload{
            .buffer = buffer.get(),
            .expectedState = ResourceStates::ConstantBuffer,
            .texture = texture.get(),
            .expectedTextureState = ResourceStates::CopyDest,
            .additionalTexture = additionalTexture.get(),
            .expectedAdditionalTextureState = ResourceStates::CopyDest,
            .recorded = &nativeClearRecorded,
        }
    );
    ASSERT_TRUE(clearTask.valid());

    const GpuTaskResourceUse gbufferUses[] = {
        GpuTaskResourceUse{
            .resource = resource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = textureResource,
            .range = {},
            .requiredState = ResourceStates::RenderTarget,
            .access = GpuTaskResourceAccess::Write,
        },
    };
    GpuTaskSchedulingHint gbufferScheduling;
    gbufferScheduling.allowPacketMerge = true;
    gbufferScheduling.mergeWithPrevious = true;
    GpuTaskDesc gbufferDesc;
    gbufferDesc
        .setIdentity(Name("tests/descriptor_buffer/merged_packet_gbuffer"))
        .setMarkerLabel("Merged Packet G-Buffer")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Graphics,
            GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setScheduling(gbufferScheduling)
        .setDependencies(&clearTask, 1u)
        .setResourceUses(gbufferUses, LengthOf(gbufferUses))
    ;
    bool nativeGbufferRecorded = false;
    const GpuTaskId gbufferTask = graph.addTask<NativePacketPrefixTask>(
        gbufferDesc,
        NativePacketPrefixTask::Payload{
            .buffer = buffer.get(),
            .expectedState = ResourceStates::ConstantBuffer,
            .texture = texture.get(),
            .expectedTextureState = ResourceStates::RenderTarget,
            .additionalTexture = additionalTexture.get(),
            .expectedAdditionalTextureState = ResourceStates::CopyDest,
            .recorded = &nativeGbufferRecorded,
        }
    );
    ASSERT_TRUE(gbufferTask.valid());

    const GpuTaskResourceUse normalizeUses[] = {
        GpuTaskResourceUse{
            .resource = resource,
            .range = {},
            .requiredState = ResourceStates::ConstantBuffer,
            .access = GpuTaskResourceAccess::Read,
        },
        GpuTaskResourceUse{
            .resource = textureResource,
            .range = {},
            .requiredState = ResourceStates::ShaderResource,
            .access = GpuTaskResourceAccess::Read,
        },
    };
    GpuTaskSchedulingHint normalizeScheduling;
    normalizeScheduling.allowPacketMerge = true;
    normalizeScheduling.mergeWithPrevious = true;
    GpuTaskDesc normalizeDesc;
    normalizeDesc
        .setIdentity(Name("tests/descriptor_buffer/merged_packet_normalize"))
        .setMarkerLabel("Merged Packet Normalize")
        .setQueue(GpuQueueRequest{
            GpuQueueCapability::Graphics,
            GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setScheduling(normalizeScheduling)
        .setDependencies(&gbufferTask, 1u)
        .setResourceUses(normalizeUses, LengthOf(normalizeUses))
    ;
    bool nativeNormalizeRecorded = false;
    const GpuTaskId normalizeTask = graph.addTask<NativePacketPrefixTask>(
        normalizeDesc,
        NativePacketPrefixTask::Payload{
            .buffer = buffer.get(),
            .expectedState = ResourceStates::ConstantBuffer,
            .texture = texture.get(),
            .expectedTextureState = ResourceStates::ShaderResource,
            .additionalTexture = additionalTexture.get(),
            .expectedAdditionalTextureState = ResourceStates::CopyDest,
            .recorded = &nativeNormalizeRecorded,
        }
    );
    ASSERT_TRUE(normalizeTask.valid());

    const GpuPhysicalQueueInfo queue{
        .id = BackendQueueId(device, CommandQueue::Graphics),
        .queueClass = CommandQueue::Graphics,
        .capabilities = static_cast<GpuQueueCapability::Mask>(
            static_cast<u8>(GpuQueueCapability::Graphics)
            | static_cast<u8>(GpuQueueCapability::Compute)
            | static_cast<u8>(GpuQueueCapability::Transfer)
        ),
        .familyIndex = 0u,
        .queueIndex = 0u,
        .dedicated = false,
    };
    const GpuTaskGraphQueueTopology topology{
        .queues = &queue,
        .queueCount = 1u,
    };
    GpuTaskGraphAnalysis analysis(DescriptorBufferRoundTripTest::arena());
    GpuTaskGraphQueueAssignments assignments(DescriptorBufferRoundTripTest::arena());
    GpuCompiledGraph compiledGraph(DescriptorBufferRoundTripTest::arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/merged_packet_scratch"));
    const GpuTaskGraphCompiler compiler;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations, analysis, topology, assignments, compiledGraph, scratchArena));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 1u);
    const GpuSubmissionPacketId packet = views.compiled.packetForTask(normalizeTask);
    const GpuSubmissionPacketRange packetRange = views.compiled.allPacketRange();
    ASSERT_TRUE(packet.valid());
    ASSERT_TRUE(packetRange.valid());
    ASSERT_EQ(packetRange.packetCount, 1u);
    EXPECT_EQ(packet, views.compiled.packetForTask(meshViewSetupTask));
    EXPECT_EQ(packet, views.compiled.packetForTask(sceneSetupTask));
    EXPECT_EQ(packet, views.compiled.packetForTask(clearTask));
    EXPECT_EQ(packet, views.compiled.packetForTask(gbufferTask));
    const GpuCompiledPacketView packetPlan = views.compiled.packet(packet);
    ASSERT_TRUE(packetPlan.valid());
    ASSERT_EQ(packetPlan.plan->taskCount, 5u);
    ASSERT_NE(views.compiled.packet(packet).tasks, nullptr);
    EXPECT_EQ(views.compiled.packet(packet).tasks[0u], meshViewSetupTask);
    EXPECT_EQ(views.compiled.packet(packet).tasks[1u], sceneSetupTask);
    EXPECT_EQ(views.compiled.packet(packet).tasks[2u], clearTask);
    EXPECT_EQ(views.compiled.packet(packet).tasks[3u], gbufferTask);
    EXPECT_EQ(views.compiled.packet(packet).tasks[4u], normalizeTask);
    EXPECT_TRUE(views.declarations.taskAt(meshViewSetupTask.index).hasPayload);
    EXPECT_TRUE(views.declarations.taskAt(sceneSetupTask.index).hasPayload);
    EXPECT_TRUE(views.declarations.taskAt(clearTask.index).hasPayload);
    EXPECT_TRUE(views.declarations.taskAt(gbufferTask.index).hasPayload);
    EXPECT_TRUE(views.declarations.taskAt(normalizeTask.index).hasPayload);
    ASSERT_TRUE(views.compiled.findTask(sceneSetupTask).valid());
    ASSERT_TRUE(views.compiled.findTask(clearTask).valid());
    ASSERT_TRUE(views.compiled.findTask(gbufferTask).valid());
    const GpuCompiledTaskView compiledNormalize = views.compiled.findTask(normalizeTask);
    ASSERT_TRUE(compiledNormalize.valid());
    const GpuCompiledBarrier* const normalizeBarriers = views.compiled.findTask(normalizeTask).prologueBarriers;
    ASSERT_NE(normalizeBarriers, nullptr);
    // The normalizer probe emits no native state change. The graph must lower the G-buffer attachment transition
    // before it records, just as the renderer's Post-G-Buffer Normalize task does for its declared resources.
    bool graphOwnsNormalizeTransition = false;
    for(u32 barrierIndex = 0u; barrierIndex < compiledNormalize.plan->prologueBarrierCount; ++barrierIndex){
        const GpuCompiledBarrier& barrier = normalizeBarriers[barrierIndex];
        graphOwnsNormalizeTransition = graphOwnsNormalizeTransition
            || (
                barrier.type == GpuCompiledBarrierType::TextureTransition
                && barrier.resource == textureResource
                && barrier.before == ResourceStates::RenderTarget
                && barrier.after == ResourceStates::ShaderResource
            )
        ;
    }
    EXPECT_TRUE(graphOwnsNormalizeTransition);

    GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
    GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    GpuSubmissionPacketId failedPacket;
    const bool recorded = recorder.recordPacketRangeInCompileOrder(
        graph,
        compiledGraph,
        packetRange,
        recordedGraph,
        &failedPacket
    );
    ASSERT_TRUE(recorded) << "failed packet " << failedPacket.index;
    ASSERT_TRUE(nativeMeshViewSetupRecorded);
    ASSERT_TRUE(nativeSceneSetupRecorded);
    ASSERT_TRUE(nativeClearRecorded);
    ASSERT_TRUE(nativeGbufferRecorded);
    ASSERT_TRUE(nativeNormalizeRecorded);
    const Optional<GpuRecordedPacket> recordedPacket = recordedGraph.packetSnapshot(packet);
    ASSERT_TRUE(recordedPacket.has_value());
    EXPECT_EQ(recordedPacket->commandListCount, 1u);
    CommandListResourceStateHandoff finalStateStorage(DescriptorBufferRoundTripTest::arena());
    ASSERT_TRUE(recordedGraph.copyTaskFinalStateSeed(compiledGraph, views.compiled, normalizeTask, finalStateStorage));
    const CommandListResourceStateHandoff* const finalState = &finalStateStorage;

    auto stateProbe = device.createCommandList();
    ASSERT_NE(stateProbe.get(), nullptr);
    stateProbe->open(finalState);
    EXPECT_EQ(stateProbe->getBufferState(buffer.get()), ResourceStates::ConstantBuffer);
    EXPECT_EQ(stateProbe->getTextureSubresourceState(texture.get(), 0u, 0u), ResourceStates::ShaderResource);
    EXPECT_EQ(stateProbe->getTextureSubresourceState(additionalTexture.get(), 0u, 0u), ResourceStates::CopyDest);
    stateProbe->close();

    const GpuTaskGraphSubmitter submitter(device);
    NativePacketSubmissionHookObserver hookObserver;
    const QueueSubmissionPreSubmitHook rejectedHook{
        .context = &hookObserver,
        .invoke = RejectNativePacketSubmissionHook,
    };
    const GpuTaskGraphTaskSubmissionHook mergedTaskSubmissionHooks[] = {
        GpuTaskGraphTaskSubmissionHook{
            .task = meshViewSetupTask,
            .hook = rejectedHook,
        },
        GpuTaskGraphTaskSubmissionHook{
            .task = normalizeTask,
            .hook = rejectedHook,
        },
    };
    EXPECT_FALSE(submitter.submitTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        meshViewSetupTask,
        normalizeTask,
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena,
        nullptr,
        nullptr,
        0u,
        mergedTaskSubmissionHooks,
        LengthOf(mergedTaskSubmissionHooks)
    ));
    EXPECT_EQ(hookObserver.invocationCount, 0u);
    EXPECT_FALSE(transaction.hasAcceptedPackets());

    NativeTaskAcceptanceOrder taskAcceptanceOrder;
    NativeTaskAcceptanceObserver meshViewSetupAcceptance{
        .lastToken = {},
        .continueSubmission = false,
        .order = &taskAcceptanceOrder,
        .orderMarker = 1u,
    };
    NativeTaskAcceptanceObserver normalizeAcceptance{
        .lastToken = {},
        .order = &taskAcceptanceOrder,
        .orderMarker = 2u,
    };
    const GpuTaskGraphTaskAcceptedCallback taskAcceptedCallbacks[] = {
        GpuTaskGraphTaskAcceptedCallback{
            .task = normalizeTask,
            .context = &normalizeAcceptance,
            .invoke = ObserveNativeTaskAcceptance,
        },
        GpuTaskGraphTaskAcceptedCallback{
            .task = meshViewSetupTask,
            .context = &meshViewSetupAcceptance,
            .invoke = ObserveNativeTaskAcceptance,
        },
    };
    GpuSubmissionPacketId failedSubmissionPacket;
    EXPECT_FALSE(submitter.submitTaskRangeInCompileOrder(
        graph,
        compiledGraph,
        recordedGraph,
        meshViewSetupTask,
        normalizeTask,
        nullptr,
        0u,
        nullptr,
        0u,
        transaction,
        scratchArena,
        &failedSubmissionPacket,
        taskAcceptedCallbacks,
        LengthOf(taskAcceptedCallbacks)
    ));
    const QueueSubmissionToken packetToken = transaction.packetToken(packet);
    EXPECT_TRUE(packetToken.valid());
    EXPECT_EQ(failedSubmissionPacket, packet);
    EXPECT_EQ(meshViewSetupAcceptance.acceptedCount, 1u);
    EXPECT_EQ(normalizeAcceptance.acceptedCount, 1u);
    EXPECT_EQ(meshViewSetupAcceptance.lastToken.value, packetToken.value);
    EXPECT_EQ(normalizeAcceptance.lastToken.value, packetToken.value);
    EXPECT_EQ(taskAcceptanceOrder.invocationCount, 2u);
    EXPECT_EQ(taskAcceptanceOrder.markers[0u], 1u);
    EXPECT_EQ(taskAcceptanceOrder.markers[1u], 2u);
    EXPECT_TRUE(device.waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

