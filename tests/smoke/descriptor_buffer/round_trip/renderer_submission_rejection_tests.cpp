// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_timing_packets_test_support.h"
#include "graph_resources_test_support.h"
#include "round_trip_fixture.h"
#include "timing_scopes_test_support.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Renderer-shaped rejection probes retain task lifecycle independently from command-list recording. Timing endpoints
// use the same split frame transaction as RendererSystem, so a rejected ordinary packet can be retired by the late
// accepted-frontier recovery packet without publishing a partial duration.
struct NativeRendererRejectionProbeTask{
    enum class TimingEndpoint : u8{
        None,
        Begin,
        End,
    };

    struct Payload{
        Device* device = nullptr;
        GpuTimingFrameTransaction* frameTransaction = nullptr;
        GpuTimingSubmissionTicket* timingTicket = nullptr;
        Buffer* buffer = nullptr;
        ResourceStates::Mask expectedBufferState = ResourceStates::Unknown;
        bool* recorded = nullptr;
        QueueSubmissionToken* acceptedToken = nullptr;
        u32* acceptedCount = nullptr;
        u32* discardedCount = nullptr;
        bool* timingAccepted = nullptr;
        TimingEndpoint timingEndpoint = TimingEndpoint::None;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        CommandList& commandList,
        const GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        bool ready = !payload.buffer || commandList.getBufferState(payload.buffer) == payload.expectedBufferState;
        if(payload.timingEndpoint != TimingEndpoint::None){
            if(!payload.device || !payload.frameTransaction || !payload.timingTicket)
                ready = false;
            else{
                GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
                ready = ready && (
                    payload.timingEndpoint == TimingEndpoint::Begin
                    ? payload.frameTransaction->begin(s_FrameTransactionScope, *payload.device, commandList)
                    : payload.frameTransaction->recordEnd(commandList)
                );
            }
        }
        if(payload.recorded)
            *payload.recorded = ready;
        return ready;
    }

    static void accepted(Payload& payload, const QueueSubmissionToken& token){
        if(payload.acceptedToken)
            *payload.acceptedToken = token;
        if(payload.acceptedCount)
            ++*payload.acceptedCount;

        bool timingAccepted = true;
        if(payload.frameTransaction && payload.timingEndpoint == TimingEndpoint::Begin)
            timingAccepted = payload.frameTransaction->confirmBeginSubmission(token);
        else if(payload.frameTransaction && payload.timingEndpoint == TimingEndpoint::End)
            timingAccepted = payload.frameTransaction->confirmEndSubmission(token, true);
        if(payload.timingAccepted)
            *payload.timingAccepted = timingAccepted;
        if(!timingAccepted && payload.frameTransaction)
            payload.frameTransaction->discard();
    }

    static void discarded(Payload& payload){
        if(payload.discardedCount)
            ++*payload.discardedCount;
    }
};


// The renderer transaction has two distinct presentation boundaries: the deferred Present writer and the later
// timing/presentation endpoint. This matrix rejects both, plus every other design-mandated boundary, on a deterministic
// Graphics fallback topology. A final clean execution proves that rejection cleanup returned command/upload storage.
TEST_F(DescriptorBufferRoundTripTest, RendererGraphNativeRejectionMatrixPreservesLifecycleTimingAndReusableStorage){
    auto& graphics = s_scope->graphics();
    auto& device = DescriptorBufferRoundTripTest::device();
    auto& timing = graphics.gpuTiming();
    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_TRUE(graphicsQueue.valid());
    const VkQueue nativeGraphicsQueue = static_cast<VkQueue>(
        device.getNativeQueue(GraphicsBackend::ObjectTypes::VK_Queue, graphicsQueue).pointer()
    );
    ASSERT_NE(nativeGraphicsQueue, VK_NULL_HANDLE);

    enum class FailurePoint : u8{
        FirstPacket,
        FirstComputeFallback,
        GraphicsOverlap,
        Lighting,
        DeferredPresent,
        PresentationEndpoint,
        History,
        Recovery,
        Clean,
    };
    enum ProbeIndex : usize{
        FirstProbe,
        ComputeProbe,
        OverlapProbe,
        LightingProbe,
        DeferredPresentProbe,
        PresentationEndpointProbe,
        HistoryProbe,

        ProbeCount,
    };
    struct ProbeState{
        bool recorded = false;
        QueueSubmissionToken acceptedToken;
        u32 acceptedCount = 0u;
        u32 discardedCount = 0u;
        bool timingAccepted = false;
    };
    struct RejectionArm{
        VulkanTestQueueSubmit2Observer* observer = nullptr;
        VkQueue queue = VK_NULL_HANDLE;
        bool armed = false;
    };

    const bool timingActive = device.supportsComparableGpuTimestamps(graphicsQueue);
    s_scope->setGpuTimingEnabled(timingActive);
    const FailurePoint failurePoints[] = {
        FailurePoint::FirstPacket,
        FailurePoint::FirstComputeFallback,
        FailurePoint::GraphicsOverlap,
        FailurePoint::Lighting,
        FailurePoint::DeferredPresent,
        FailurePoint::PresentationEndpoint,
        FailurePoint::History,
        FailurePoint::Recovery,
        FailurePoint::Clean,
    };
    u64 timingFrameIndex = 700u;
    for(const FailurePoint failurePoint : failurePoints){
        SCOPED_TRACE(static_cast<u32>(failurePoint));
        timing.resetQueries();
        if(timingActive){
            ASSERT_TRUE(timing.prepareScopeQueries(s_FrameTransactionScope.identity, device, 1u));
            timing.beginFrame(timingFrameIndex);
        }

        const auto executeRow = [&](){
            constexpr u32 uploadWords[] = { 0x13579BDFu, 0x2468ACE0u, 0xC001C0DEu, 0x600DF00Du };
            auto setupBuffer = device.createBuffer(
                BufferDesc()
                    .setByteSize(256u)
                    .setCanHaveRawViews(true)
                    .setIsConstantBuffer(true)
                    .setInitialState(ResourceStates::Common)
                    .setQueueSharing(ResourceQueueSharing::GraphicsAndTransfer)
            );
            ASSERT_NE(setupBuffer.get(), nullptr);

            GpuTimingFrameTransaction frameTransaction(timing);
            GpuTimingSubmissionTicket beginTimingTicket(timing);
            GpuTimingSubmissionTicket endTimingTicket(timing);
            ProbeState probeStates[ProbeCount];
            bool recoveryArmed = false;
            bool recoveryRetiresTiming = false;
            bool recoveryRecorded = false;
            bool recoveryAccepted = false;
            bool recoveryDiscarded = false;
            QueueSubmissionToken uploadAcceptedToken;

            GpuTaskGraph graph(DescriptorBufferRoundTripTest::arena());
            const GpuGraphResourceId setupResource = graph.importBuffer(
                setupBuffer,
                GpuGraphResourceDesc{}
                    .setIdentity(Name("tests/descriptor_buffer/rejection_matrix_setup"))
                    .setMarkerLabel("Rejection Matrix Setup")
                    .setType(GpuGraphResourceType::Buffer)
                    .setInitialState(ResourceStates::Common)
                    .setQueueSharing(setupBuffer->getDescription().queueSharing)
            );
            const GpuUploadBlobId setupUpload = graph.copyUploadData(uploadWords, sizeof(uploadWords), alignof(u32));
            ASSERT_TRUE(setupResource.valid());
            ASSERT_TRUE(setupUpload.valid());

            const GpuQueueRequest graphicsRequest{
                GpuQueueCapability::Graphics,
                GpuQueuePreference::Graphics,
                false,
                false,
            };
            const GpuQueueRequest computeFallbackRequest{
                GpuQueueCapability::Compute,
                GpuQueuePreference::Compute,
                true,
                true,
            };
            const GpuQueueRequest uploadRequest{
                GpuQueueCapability::Transfer,
                GpuQueuePreference::Graphics,
                false,
                false,
            };
            GpuTaskSchedulingHint mergeScheduling;
            mergeScheduling.cost = GpuTaskCostHint::Tiny;
            mergeScheduling.overlapPreferred = false;
            mergeScheduling.allowPacketMerge = true;
            const GpuTaskId uploadTask = graph.addUploadBufferTask(
                GpuTaskDesc{}
                    .setIdentity(Name("tests/descriptor_buffer/rejection_matrix_upload"))
                    .setMarkerLabel("Rejection Matrix Upload")
                    .setQueue(uploadRequest)
                    .setScheduling(mergeScheduling),
                GpuUploadBufferTaskDesc{
                    .source = setupUpload,
                    .destination = setupResource,
                    .finalState = ResourceStates::ConstantBuffer,
                    .acceptedToken = &uploadAcceptedToken,
                }
            );
            ASSERT_TRUE(uploadTask.valid());

            GpuTaskSchedulingHint firstScheduling = mergeScheduling;
            firstScheduling.mergeWithPrevious = true;
            const GpuTaskResourceUse firstUses[] = {
                GpuTaskResourceUse{
                    .resource = setupResource,
                    .range = {},
                    .requiredState = ResourceStates::ConstantBuffer,
                    .access = GpuTaskResourceAccess::Read,
                },
            };
            const GpuTaskId firstTask = graph.addTask<NativeRendererRejectionProbeTask>(
                GpuTaskDesc{}
                    .setIdentity(Name("tests/descriptor_buffer/rejection_matrix_first"))
                    .setMarkerLabel("First Graphics Packet")
                    .setQueue(graphicsRequest)
                    .setScheduling(firstScheduling)
                    .setDependencies(&uploadTask, 1u)
                    .setResourceUses(firstUses, LengthOf(firstUses)),
                NativeRendererRejectionProbeTask::Payload{
                    .device = &device,
                    .frameTransaction = &frameTransaction,
                    .timingTicket = &beginTimingTicket,
                    .buffer = setupBuffer.get(),
                    .expectedBufferState = ResourceStates::ConstantBuffer,
                    .recorded = &probeStates[FirstProbe].recorded,
                    .acceptedToken = &probeStates[FirstProbe].acceptedToken,
                    .acceptedCount = &probeStates[FirstProbe].acceptedCount,
                    .discardedCount = &probeStates[FirstProbe].discardedCount,
                    .timingAccepted = &probeStates[FirstProbe].timingAccepted,
                    .timingEndpoint = NativeRendererRejectionProbeTask::TimingEndpoint::Begin,
                }
            );
            ASSERT_TRUE(firstTask.valid());

            GpuTaskSchedulingHint boundaryScheduling;
            boundaryScheduling.cost = GpuTaskCostHint::Large;
            boundaryScheduling.forceSubmissionBoundary = true;
            boundaryScheduling.allowPacketMerge = false;
            const auto addProbe = [&graph, &boundaryScheduling, &probeStates](
                const Name& identity,
                const AStringView label,
                const GpuQueueRequest& queueRequest,
                const GpuTaskId* const dependencies,
                const usize dependencyCount,
                const ProbeIndex probeIndex
            ){
                return graph.addTask<NativeRendererRejectionProbeTask>(
                    GpuTaskDesc{}
                        .setIdentity(identity)
                        .setMarkerLabel(label)
                        .setQueue(queueRequest)
                        .setScheduling(boundaryScheduling)
                        .setDependencies(dependencies, dependencyCount),
                    NativeRendererRejectionProbeTask::Payload{
                        .recorded = &probeStates[probeIndex].recorded,
                        .acceptedToken = &probeStates[probeIndex].acceptedToken,
                        .acceptedCount = &probeStates[probeIndex].acceptedCount,
                        .discardedCount = &probeStates[probeIndex].discardedCount,
                    }
                );
            };

            const GpuTaskId computeTask = addProbe(
                Name("tests/descriptor_buffer/rejection_matrix_compute"),
                "First Compute-Shaped Packet",
                computeFallbackRequest,
                &firstTask,
                1u,
                ComputeProbe
            );
            const GpuTaskId overlapTask = addProbe(
                Name("tests/descriptor_buffer/rejection_matrix_overlap"),
                "Graphics Overlap Packet",
                graphicsRequest,
                &firstTask,
                1u,
                OverlapProbe
            );
            ASSERT_TRUE(computeTask.valid());
            ASSERT_TRUE(overlapTask.valid());

            const GpuTaskId lightingDependencies[] = { computeTask, overlapTask };
            const GpuTaskId lightingTask = addProbe(
                Name("tests/descriptor_buffer/rejection_matrix_lighting"),
                "Deferred Lighting Packet",
                computeFallbackRequest,
                lightingDependencies,
                LengthOf(lightingDependencies),
                LightingProbe
            );
            ASSERT_TRUE(lightingTask.valid());

            const GpuTaskId deferredPresentTask = addProbe(
                Name("tests/descriptor_buffer/rejection_matrix_deferred_present"),
                "Deferred Present Writer",
                graphicsRequest,
                &lightingTask,
                1u,
                DeferredPresentProbe
            );
            ASSERT_TRUE(deferredPresentTask.valid());

            const GpuTaskId presentationEndpointTask = graph.addTask<NativeRendererRejectionProbeTask>(
                GpuTaskDesc{}
                    .setIdentity(Name("tests/descriptor_buffer/rejection_matrix_presentation_endpoint"))
                    .setMarkerLabel("Presentation Timing Endpoint")
                    .setQueue(graphicsRequest)
                    .setScheduling(boundaryScheduling)
                    .setDependencies(&deferredPresentTask, 1u),
                NativeRendererRejectionProbeTask::Payload{
                    .device = &device,
                    .frameTransaction = &frameTransaction,
                    .timingTicket = &endTimingTicket,
                    .recorded = &probeStates[PresentationEndpointProbe].recorded,
                    .acceptedToken = &probeStates[PresentationEndpointProbe].acceptedToken,
                    .acceptedCount = &probeStates[PresentationEndpointProbe].acceptedCount,
                    .discardedCount = &probeStates[PresentationEndpointProbe].discardedCount,
                    .timingAccepted = &probeStates[PresentationEndpointProbe].timingAccepted,
                    .timingEndpoint = NativeRendererRejectionProbeTask::TimingEndpoint::End,
                }
            );
            ASSERT_TRUE(presentationEndpointTask.valid());

            const GpuTaskId historyTask = addProbe(
                Name("tests/descriptor_buffer/rejection_matrix_history"),
                "History Copy Packet",
                computeFallbackRequest,
                &presentationEndpointTask,
                1u,
                HistoryProbe
            );
            ASSERT_TRUE(historyTask.valid());

            GpuTaskSchedulingHint recoveryScheduling = boundaryScheduling;
            recoveryScheduling.joinsAcceptedQueueFrontier = true;
            recoveryScheduling.isRecoverySubmission = true;
            const GpuTaskId recoveryTask = graph.addTask<NativeFrameRecoveryPacketTask>(
                GpuTaskDesc{}
                    .setIdentity(Name("tests/descriptor_buffer/rejection_matrix_recovery"))
                    .setMarkerLabel("Frame Recovery Packet")
                    .setQueue(graphicsRequest)
                    .setScheduling(recoveryScheduling),
                NativeFrameRecoveryPacketTask::Payload{
                    .transaction = &frameTransaction,
                    .armed = &recoveryArmed,
                    .retiresTiming = &recoveryRetiresTiming,
                    .recorded = &recoveryRecorded,
                    .accepted = &recoveryAccepted,
                    .discarded = &recoveryDiscarded,
                }
            );
            ASSERT_TRUE(recoveryTask.valid());

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
            Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/rejection_matrix_scratch"));
            const GpuTaskGraphCompiler compiler;
            GpuTaskGraphCompileOptions compileOptions;
            compileOptions.packetizationPolicy = GpuTaskGraphPacketizationPolicy::FrontierSafe;
            const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
            ASSERT_TRUE(compiler.compile(compilationDeclarations,
                analysis,
                topology,
                assignments,
                compiledGraph,
                scratchArena,
                compileOptions
            ));

            const GpuTaskId probeTasks[ProbeCount] = {
                firstTask,
                computeTask,
                overlapTask,
                lightingTask,
                deferredPresentTask,
                presentationEndpointTask,
                historyTask,
            };
            const GpuTaskGraphReadViews views(graph, compiledGraph);
            ASSERT_TRUE(views.valid());

            const GpuSubmissionPacketId recoveryPacket = views.compiled.packetForTask(recoveryTask);
            ASSERT_TRUE(recoveryPacket.valid());
            ASSERT_EQ(views.compiled.packetCount(), 8u);
            EXPECT_EQ(views.compiled.packetForTask(uploadTask), views.compiled.packetForTask(firstTask));
            for(usize probeIndex = 0u; probeIndex < ProbeCount; ++probeIndex){
                ASSERT_TRUE(views.compiled.packetForTask(probeTasks[probeIndex]).valid());
                EXPECT_EQ(views.compiled.packetIdAt(probeIndex), views.compiled.packetForTask(probeTasks[probeIndex]));
            }
            EXPECT_EQ(views.compiled.packetIdAt(7u), recoveryPacket);
            EXPECT_EQ(views.compiled.packet(recoveryPacket).plan->dependencyCount, 0u);
            EXPECT_EQ(views.compiled.packet(recoveryPacket).plan->externalDependencyCount, 0u);
            EXPECT_TRUE(views.compiled.packet(recoveryPacket).plan->joinsAcceptedQueueFrontier);
            EXPECT_TRUE(views.compiled.packet(recoveryPacket).plan->isRecoverySubmission);
            ASSERT_NE(views.compiled.queueInfoForTask(computeTask), nullptr);
            EXPECT_EQ(views.compiled.queueInfoForTask(computeTask)->queueClass, CommandQueue::Graphics);

            GpuTaskId rejectionTask;
            GpuTaskId rejectionArmTask;
            switch(failurePoint){
            case FailurePoint::FirstPacket:
                rejectionTask = firstTask;
                break;
            case FailurePoint::FirstComputeFallback:
                rejectionTask = computeTask;
                rejectionArmTask = firstTask;
                break;
            case FailurePoint::GraphicsOverlap:
                rejectionTask = overlapTask;
                rejectionArmTask = computeTask;
                break;
            case FailurePoint::Lighting:
            case FailurePoint::Recovery:
                rejectionTask = lightingTask;
                rejectionArmTask = overlapTask;
                break;
            case FailurePoint::DeferredPresent:
                rejectionTask = deferredPresentTask;
                rejectionArmTask = lightingTask;
                break;
            case FailurePoint::PresentationEndpoint:
                rejectionTask = presentationEndpointTask;
                rejectionArmTask = deferredPresentTask;
                break;
            case FailurePoint::History:
                rejectionTask = historyTask;
                break;
            case FailurePoint::Clean:
                break;
            }
            const GpuSubmissionPacketId rejectionPacket = views.compiled.packetForTask(rejectionTask);
            if(failurePoint != FailurePoint::Clean)
                ASSERT_TRUE(rejectionPacket.valid());

            VulkanTestQueueSubmit2Observer submissionObserver(device);
            ASSERT_TRUE(submissionObserver.valid());
            RejectionArm rejectionArm{
                .observer = &submissionObserver,
                .queue = nativeGraphicsQueue,
            };
            const GpuTaskGraphTaskAcceptedCallback acceptedCallback{
                .task = rejectionArmTask,
                .context = &rejectionArm,
                .invoke = [](void* const rawContext, const QueueSubmissionToken& token){
                    RejectionArm* const context = static_cast<RejectionArm*>(rawContext);
                    if(!context || !context->observer || context->queue == VK_NULL_HANDLE || !token.valid())
                        return false;
                    context->armed = context->observer->armSubmissionFailures(context->queue);
                    return context->armed;
                },
            };
            const GpuTaskGraphTaskTimingTicket taskTimingTickets[] = {
                GpuTaskGraphTaskTimingTicket{
                    .task = firstTask,
                    .timingTicket = &beginTimingTicket,
                },
                GpuTaskGraphTaskTimingTicket{
                    .task = presentationEndpointTask,
                    .timingTicket = &endTimingTicket,
                },
            };
            GpuTaskGraphNormalExecutionDesc normalExecution;
            normalExecution.terminalTask = presentationEndpointTask;
            normalExecution.taskTimingTickets = taskTimingTickets;
            normalExecution.taskTimingTicketCount = LengthOf(taskTimingTickets);
            if(rejectionArmTask.valid()){
                normalExecution.taskAcceptedCallbacks = &acceptedCallback;
                normalExecution.taskAcceptedCallbackCount = 1u;
            }

            if(failurePoint == FailurePoint::FirstPacket){
                const GpuPhysicalQueueInfo* const targetQueue = views.compiled.queueInfoForTask(firstTask);
                ASSERT_NE(targetQueue, nullptr);
                EXPECT_EQ(targetQueue->id, graphicsQueue);
                ASSERT_TRUE(submissionObserver.armSubmissionFailures(nativeGraphicsQueue));
            }

            GpuRecordedGraph recordedGraph(DescriptorBufferRoundTripTest::arena());
            GpuGraphSubmissionTransaction transaction(DescriptorBufferRoundTripTest::arena());
            transaction.reset(compiledGraph);
            const GpuNativePacketRecorder recorder(device);
            const GpuTaskScheduler submitter(device);
            GpuSubmissionPacketId failedPacket;
            const bool normalAccepted = submitter.submit(
                graph,
                compiledGraph,
                recorder,
                recordedGraph,
                normalExecution,
                transaction,
                scratchArena,
                &failedPacket
            );
            const bool normalShouldAccept = failurePoint == FailurePoint::History || failurePoint == FailurePoint::Clean;
            EXPECT_EQ(normalAccepted, normalShouldAccept);
            if(normalShouldAccept)
                EXPECT_FALSE(failedPacket.valid());
            else
                EXPECT_EQ(failedPacket, rejectionPacket);
            if(rejectionArmTask.valid())
                EXPECT_TRUE(rejectionArm.armed);

            if(failurePoint == FailurePoint::FirstPacket)
                frameTransaction.discard();
            else if(!normalShouldAccept){
                if(timingActive)
                    EXPECT_TRUE(frameTransaction.needsRetirement());
                ASSERT_TRUE(frameTransaction.prepareForRecovery());
                recoveryArmed = true;
                recoveryRetiresTiming = frameTransaction.needsRetirement();
                if(failurePoint == FailurePoint::Recovery){
                    const GpuPhysicalQueueInfo* const targetQueue = views.compiled.queueInfoForTask(recoveryTask);
                    ASSERT_NE(targetQueue, nullptr);
                    EXPECT_EQ(targetQueue->id, graphicsQueue);
                    ASSERT_TRUE(submissionObserver.armSubmissionFailures(nativeGraphicsQueue));
                }
                const bool recoverySubmitted = submitter.recordAndSubmitAcceptedFrontierTask(
                    graph,
                    compiledGraph,
                    recorder,
                    recordedGraph,
                    recoveryTask,
                    transaction,
                    scratchArena
                );
                EXPECT_EQ(recoverySubmitted, failurePoint != FailurePoint::Recovery);
            }
            else{
                if(failurePoint == FailurePoint::History){
                    const GpuPhysicalQueueInfo* const targetQueue = views.compiled.queueInfoForTask(historyTask);
                    ASSERT_NE(targetQueue, nullptr);
                    EXPECT_EQ(targetQueue->id, graphicsQueue);
                    ASSERT_TRUE(submissionObserver.armSubmissionFailures(nativeGraphicsQueue));
                }
                const bool historySubmitted = submitter.recordAndSubmitTask(
                    graph,
                    compiledGraph,
                    recorder,
                    recordedGraph,
                    historyTask,
                    nullptr,
                    transaction,
                    scratchArena
                );
                EXPECT_EQ(historySubmitted, failurePoint == FailurePoint::Clean);
            }

            if(failurePoint != FailurePoint::Clean){
                EXPECT_FALSE(transaction.packetToken(rejectionPacket).valid());
            }
            if(failurePoint == FailurePoint::Recovery){
                EXPECT_FALSE(transaction.packetToken(recoveryPacket).valid());
            }

            QueueSubmissionToken acceptedTokenSnapshots[8u];
            bool acceptedPacketSnapshots[8u]{};
            for(usize packetIndex = 0u; packetIndex < views.compiled.packetCount(); ++packetIndex){
                const GpuSubmissionPacketId packet = views.compiled.packetIdAt(packetIndex);
                acceptedTokenSnapshots[packetIndex] = transaction.packetToken(packet);
                acceptedPacketSnapshots[packetIndex] = acceptedTokenSnapshots[packetIndex].valid();
            }
            ASSERT_TRUE(transaction.discardUnaccepted(
                graph,
                compiledGraph,
                recordedGraph.recordingAttemptGeneration()
            ));

            for(usize packetIndex = 0u; packetIndex < views.compiled.packetCount(); ++packetIndex){
                const GpuSubmissionPacketId packet = views.compiled.packetIdAt(packetIndex);
                const QueueSubmissionToken currentToken = transaction.packetToken(packet);
                if(acceptedPacketSnapshots[packetIndex]){
                    EXPECT_TRUE(currentToken.valid());
                    EXPECT_EQ(currentToken.queue, acceptedTokenSnapshots[packetIndex].queue);
                    EXPECT_EQ(currentToken.value, acceptedTokenSnapshots[packetIndex].value);
                    EXPECT_EQ(currentToken.physicalQueueIndex, acceptedTokenSnapshots[packetIndex].physicalQueueIndex);
                    EXPECT_EQ(currentToken.deviceGeneration, acceptedTokenSnapshots[packetIndex].deviceGeneration);
                }
                else{
                    EXPECT_FALSE(currentToken.valid());
                }
            }
            for(usize probeIndex = 0u; probeIndex < ProbeCount; ++probeIndex){
                const GpuSubmissionPacketId packet = views.compiled.packetForTask(probeTasks[probeIndex]);
                const bool accepted = transaction.packetToken(packet).valid();
                EXPECT_EQ(probeStates[probeIndex].acceptedCount, accepted ? 1u : 0u);
                EXPECT_EQ(probeStates[probeIndex].discardedCount, accepted ? 0u : 1u);
                EXPECT_EQ(probeStates[probeIndex].acceptedToken.valid(), accepted);
            }

            const bool firstPacketAccepted = transaction.packetToken(views.compiled.packetForTask(firstTask)).valid();
            EXPECT_EQ(uploadAcceptedToken.valid(), firstPacketAccepted);
            const bool recoveryPacketAccepted = transaction.packetToken(recoveryPacket).valid();
            EXPECT_EQ(recoveryAccepted, recoveryPacketAccepted);
            EXPECT_EQ(recoveryDiscarded, !recoveryPacketAccepted);
            EXPECT_FALSE(frameTransaction.needsRetirement());

            const GpuTaskGraphSubmissionStatistics statistics = transaction.submissionStatistics();
            ASSERT_TRUE(statistics.valid());
            const usize expectedRejectedSubmissions = failurePoint == FailurePoint::Clean
                ? 0u
                : (failurePoint == FailurePoint::Recovery ? 2u : 1u)
            ;
            EXPECT_EQ(statistics.rejectedSubmissionCount, expectedRejectedSubmissions);
            EXPECT_FALSE(submissionObserver.overflowed());
            EXPECT_EQ(submissionObserver.injectedSubmissionFailureCount(), expectedRejectedSubmissions);
            EXPECT_EQ(submissionObserver.pendingSubmissionFailureCount(), 0u);
            const bool recoveryShouldAccept = failurePoint >= FailurePoint::FirstComputeFallback
                && failurePoint <= FailurePoint::PresentationEndpoint
            ;
            EXPECT_EQ(statistics.acceptedFrontierSubmissionCount, recoveryShouldAccept ? 1u : 0u);
            EXPECT_EQ(statistics.recoverySubmissionCount, recoveryShouldAccept ? 1u : 0u);
        };

        executeRow();
        ASSERT_TRUE(device.waitForIdle());
        if(timingActive)
            timing.collect(device, timingFrameIndex + 1u);
        timing.resetQueries();
        timingFrameIndex += 2u;
    }

    s_scope->setGpuTimingEnabled(false);
    timing.resetQueries();
}


// A strict Compute request proves the design's first-Compute rejection row against an actual dedicated queue when
// the adapter exposes one. The Graphics fallback matrix above remains authoritative on every supported topology.
TEST_F(DescriptorBufferRoundTripTest, RendererGraphRejectsDedicatedFirstComputeAndRetiresThroughGraphicsRecovery){
    HeadlessGraphicsScope asyncScope;
    ASSERT_TRUE(asyncScope.setAsyncComputeLaneEnabled(true));
    if(!asyncScope.initialize())
        GTEST_SKIP() << "Renderer rejection matrix: no usable dedicated-compute headless Vulkan device on this host.";

    auto& graphics = asyncScope.graphics();
    auto& device = graphics.getDevice();
    if(!HasDedicatedComputeQueue(device))
        GTEST_SKIP() << "Renderer rejection matrix: adapter has no dedicated compute-only queue family.";

    const GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_TRUE(graphicsQueue.valid());
    auto& timing = graphics.gpuTiming();
    const bool timingActive = device.supportsComparableGpuTimestamps(graphicsQueue);
    asyncScope.setGpuTimingEnabled(timingActive);
    timing.resetQueries();
    if(timingActive){
        ASSERT_TRUE(timing.prepareScopeQueries(s_FrameTransactionScope.identity, device, 1u));
        timing.beginFrame(810u);
    }

    struct ProbeState{
        bool recorded = false;
        QueueSubmissionToken acceptedToken;
        u32 acceptedCount = 0u;
        u32 discardedCount = 0u;
        bool timingAccepted = false;
    };
    struct RejectionArm{
        VulkanTestQueueSubmit2Observer* observer = nullptr;
        VkQueue queue = VK_NULL_HANDLE;
        bool armed = false;
    };

    GpuTimingFrameTransaction frameTransaction(timing);
    GpuTimingSubmissionTicket beginTimingTicket(timing);
    GpuTimingSubmissionTicket endTimingTicket(timing);
    ProbeState firstState;
    ProbeState computeState;
    ProbeState endpointState;
    bool recoveryArmed = false;
    bool recoveryRetiresTiming = false;
    bool recoveryRecorded = false;
    bool recoveryAccepted = false;
    bool recoveryDiscarded = false;

    GpuTaskGraph graph(asyncScope.arena());
    const GpuQueueRequest graphicsRequest{
        GpuQueueCapability::Graphics,
        GpuQueuePreference::Graphics,
        false,
        false,
    };
    const GpuQueueRequest strictComputeRequest{
        GpuQueueCapability::Compute,
        GpuQueuePreference::Compute,
        false,
        false,
    };
    GpuTaskSchedulingHint boundaryScheduling;
    boundaryScheduling.cost = GpuTaskCostHint::Large;
    boundaryScheduling.forceSubmissionBoundary = true;
    boundaryScheduling.allowPacketMerge = false;

    const GpuTaskId firstTask = graph.addTask<NativeRendererRejectionProbeTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/dedicated_rejection_first"))
            .setMarkerLabel("Dedicated Rejection First Graphics")
            .setQueue(graphicsRequest)
            .setScheduling(boundaryScheduling),
        NativeRendererRejectionProbeTask::Payload{
            .device = &device,
            .frameTransaction = &frameTransaction,
            .timingTicket = &beginTimingTicket,
            .recorded = &firstState.recorded,
            .acceptedToken = &firstState.acceptedToken,
            .acceptedCount = &firstState.acceptedCount,
            .discardedCount = &firstState.discardedCount,
            .timingAccepted = &firstState.timingAccepted,
            .timingEndpoint = NativeRendererRejectionProbeTask::TimingEndpoint::Begin,
        }
    );
    ASSERT_TRUE(firstTask.valid());

    const GpuTaskId computeTask = graph.addTask<NativeRendererRejectionProbeTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/dedicated_rejection_compute"))
            .setMarkerLabel("Dedicated First Compute")
            .setQueue(strictComputeRequest)
            .setScheduling(boundaryScheduling)
            .setDependencies(&firstTask, 1u),
        NativeRendererRejectionProbeTask::Payload{
            .recorded = &computeState.recorded,
            .acceptedToken = &computeState.acceptedToken,
            .acceptedCount = &computeState.acceptedCount,
            .discardedCount = &computeState.discardedCount,
        }
    );
    ASSERT_TRUE(computeTask.valid());

    const GpuTaskId endpointTask = graph.addTask<NativeRendererRejectionProbeTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/dedicated_rejection_endpoint"))
            .setMarkerLabel("Dedicated Rejection Presentation Endpoint")
            .setQueue(graphicsRequest)
            .setScheduling(boundaryScheduling)
            .setDependencies(&computeTask, 1u),
        NativeRendererRejectionProbeTask::Payload{
            .device = &device,
            .frameTransaction = &frameTransaction,
            .timingTicket = &endTimingTicket,
            .recorded = &endpointState.recorded,
            .acceptedToken = &endpointState.acceptedToken,
            .acceptedCount = &endpointState.acceptedCount,
            .discardedCount = &endpointState.discardedCount,
            .timingAccepted = &endpointState.timingAccepted,
            .timingEndpoint = NativeRendererRejectionProbeTask::TimingEndpoint::End,
        }
    );
    ASSERT_TRUE(endpointTask.valid());

    GpuTaskSchedulingHint recoveryScheduling = boundaryScheduling;
    recoveryScheduling.joinsAcceptedQueueFrontier = true;
    recoveryScheduling.isRecoverySubmission = true;
    const GpuTaskId recoveryTask = graph.addTask<NativeFrameRecoveryPacketTask>(
        GpuTaskDesc{}
            .setIdentity(Name("tests/descriptor_buffer/dedicated_rejection_recovery"))
            .setMarkerLabel("Dedicated Rejection Graphics Recovery")
            .setQueue(graphicsRequest)
            .setScheduling(recoveryScheduling),
        NativeFrameRecoveryPacketTask::Payload{
            .transaction = &frameTransaction,
            .armed = &recoveryArmed,
            .retiresTiming = &recoveryRetiresTiming,
            .recorded = &recoveryRecorded,
            .accepted = &recoveryAccepted,
            .discarded = &recoveryDiscarded,
        }
    );
    ASSERT_TRUE(recoveryTask.valid());

    const GpuPhysicalQueueTopology topology = device.getPhysicalQueueTopology();
    ASSERT_NE(topology.queues, nullptr);
    ASSERT_GT(topology.queueCount, 0u);
    GpuTaskGraphAnalysis analysis(asyncScope.arena());
    GpuTaskGraphQueueAssignments assignments(asyncScope.arena());
    GpuCompiledGraph compiledGraph(asyncScope.arena());
    Alloc::ScratchArena scratchArena(Name("tests/descriptor_buffer/dedicated_rejection_scratch"));
    const GpuTaskGraphCompiler compiler;
    GpuTaskGraphCompileOptions compileOptions;
    compileOptions.packetizationPolicy = GpuTaskGraphPacketizationPolicy::FrontierSafe;
    const GpuTaskGraph::DeclarationReadView compilationDeclarations(graph);
    ASSERT_TRUE(compiler.compile(compilationDeclarations,
        analysis,
        topology,
        assignments,
        compiledGraph,
        scratchArena,
        compileOptions
    ));
    const GpuTaskGraphReadViews views(graph, compiledGraph);
    ASSERT_TRUE(views.valid());

    ASSERT_EQ(views.compiled.packetCount(), 4u);
    const GpuSubmissionPacketId firstPacket = views.compiled.packetForTask(firstTask);
    const GpuSubmissionPacketId computePacket = views.compiled.packetForTask(computeTask);
    const GpuSubmissionPacketId endpointPacket = views.compiled.packetForTask(endpointTask);
    const GpuSubmissionPacketId recoveryPacket = views.compiled.packetForTask(recoveryTask);
    ASSERT_TRUE(firstPacket.valid());
    ASSERT_TRUE(computePacket.valid());
    ASSERT_TRUE(endpointPacket.valid());
    ASSERT_TRUE(recoveryPacket.valid());
    EXPECT_EQ(views.compiled.packetIdAt(0u), firstPacket);
    EXPECT_EQ(views.compiled.packetIdAt(1u), computePacket);
    EXPECT_EQ(views.compiled.packetIdAt(2u), endpointPacket);
    EXPECT_EQ(views.compiled.packetIdAt(3u), recoveryPacket);
    const GpuPhysicalQueueInfo* const computeQueue = views.compiled.queueInfoForTask(computeTask);
    ASSERT_NE(computeQueue, nullptr);
    EXPECT_EQ(computeQueue->queueClass, CommandQueue::Compute);
    EXPECT_TRUE(computeQueue->dedicated);
    ASSERT_TRUE(computeQueue->id.valid());
    const VkQueue nativeComputeQueue = static_cast<VkQueue>(
        device.getNativeQueue(GraphicsBackend::ObjectTypes::VK_Queue, computeQueue->id).pointer()
    );
    ASSERT_NE(nativeComputeQueue, VK_NULL_HANDLE);

    VulkanTestQueueSubmit2Observer submissionObserver(device);
    ASSERT_TRUE(submissionObserver.valid());
    RejectionArm rejectionArm{
        .observer = &submissionObserver,
        .queue = nativeComputeQueue,
    };
    const GpuTaskGraphTaskAcceptedCallback acceptedCallback{
        .task = firstTask,
        .context = &rejectionArm,
        .invoke = [](void* const rawContext, const QueueSubmissionToken& token){
            RejectionArm* const context = static_cast<RejectionArm*>(rawContext);
            if(!context || !context->observer || context->queue == VK_NULL_HANDLE || !token.valid())
                return false;
            context->armed = context->observer->armSubmissionFailures(context->queue);
            return context->armed;
        },
    };
    const GpuTaskGraphTaskTimingTicket timingTickets[] = {
        GpuTaskGraphTaskTimingTicket{
            .task = firstTask,
            .timingTicket = &beginTimingTicket,
        },
        GpuTaskGraphTaskTimingTicket{
            .task = endpointTask,
            .timingTicket = &endTimingTicket,
        },
    };
    GpuTaskGraphNormalExecutionDesc normalExecution;
    normalExecution.terminalTask = endpointTask;
    normalExecution.taskTimingTickets = timingTickets;
    normalExecution.taskTimingTicketCount = LengthOf(timingTickets);
    normalExecution.taskAcceptedCallbacks = &acceptedCallback;
    normalExecution.taskAcceptedCallbackCount = 1u;

    GpuRecordedGraph recordedGraph(asyncScope.arena());
    GpuGraphSubmissionTransaction transaction(asyncScope.arena());
    transaction.reset(compiledGraph);
    const GpuNativePacketRecorder recorder(device);
    const GpuTaskScheduler submitter(device);
    GpuSubmissionPacketId failedPacket;
    EXPECT_FALSE(submitter.submit(
        graph,
        compiledGraph,
        recorder,
        recordedGraph,
        normalExecution,
        transaction,
        scratchArena,
        &failedPacket
    ));
    EXPECT_EQ(failedPacket, computePacket);
    EXPECT_TRUE(rejectionArm.armed);
    EXPECT_EQ(submissionObserver.injectedSubmissionFailureCount(), 1u);
    EXPECT_EQ(submissionObserver.pendingSubmissionFailureCount(), 0u);
    EXPECT_FALSE(transaction.packetToken(computePacket).valid());
    ASSERT_TRUE(firstState.acceptedToken.valid());
    const QueueSubmissionToken firstTokenSnapshot = firstState.acceptedToken;
    if(timingActive)
        EXPECT_TRUE(frameTransaction.needsRetirement());

    ASSERT_TRUE(frameTransaction.prepareForRecovery());
    recoveryArmed = true;
    recoveryRetiresTiming = frameTransaction.needsRetirement();
    ASSERT_TRUE(submitter.recordAndSubmitAcceptedFrontierTask(
        graph,
        compiledGraph,
        recorder,
        recordedGraph,
        recoveryTask,
        transaction,
        scratchArena
    ));
    EXPECT_TRUE(recoveryRecorded);
    EXPECT_TRUE(recoveryAccepted);
    EXPECT_FALSE(recoveryDiscarded);
    EXPECT_FALSE(frameTransaction.needsRetirement());

    ASSERT_TRUE(transaction.discardUnaccepted(
        graph,
        compiledGraph,
        recordedGraph.recordingAttemptGeneration()
    ));
    EXPECT_EQ(firstState.acceptedCount, 1u);
    EXPECT_EQ(firstState.discardedCount, 0u);
    EXPECT_EQ(computeState.acceptedCount, 0u);
    EXPECT_EQ(computeState.discardedCount, 1u);
    EXPECT_EQ(endpointState.acceptedCount, 0u);
    EXPECT_EQ(endpointState.discardedCount, 1u);
    const QueueSubmissionToken firstToken = transaction.packetToken(firstPacket);
    EXPECT_EQ(firstToken.queue, firstTokenSnapshot.queue);
    EXPECT_EQ(firstToken.value, firstTokenSnapshot.value);
    EXPECT_EQ(firstToken.physicalQueueIndex, firstTokenSnapshot.physicalQueueIndex);
    EXPECT_EQ(firstToken.deviceGeneration, firstTokenSnapshot.deviceGeneration);
    const GpuTaskGraphSubmissionStatistics statistics = transaction.submissionStatistics();
    ASSERT_TRUE(statistics.valid());
    EXPECT_EQ(statistics.rejectedSubmissionCount, 1u);
    EXPECT_EQ(statistics.acceptedFrontierSubmissionCount, 1u);
    EXPECT_EQ(statistics.recoverySubmissionCount, 1u);

    ASSERT_TRUE(device.waitForIdle());
    if(timingActive)
        timing.collect(device, 811u);
    asyncScope.setGpuTimingEnabled(false);
    timing.resetQueries();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

