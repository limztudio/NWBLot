// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>
#include <gtest/gtest.h>

#include <global/filesystem/operations.h>
#include <global/filesystem/path.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_task_graph_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using AString = NWB::Tests::TestAString;
using TestPath = ::Path<NWB::Core::Alloc::GlobalArena>;

struct TaskGraphContractTestArenaTag{};
using TestArena = NWB::Tests::TestArena<TaskGraphContractTestArenaTag>;


static bool ContainsText(const AStringView text, const AStringView expected){
    AString normalized;
    normalized.reserve(text.size());
    for(const char ch : text){
        if(ch != '\r')
            normalized += ch;
    }
    return AStringView(normalized.data(), normalized.size()).find(expected) != AStringView::npos;
}

static usize CountText(const AStringView text, const AStringView expected){
    if(expected.empty())
        return 0u;
    usize count = 0u;
    usize offset = 0u;
    while(offset < text.size()){
        const usize found = text.find(expected, offset);
        if(found == AStringView::npos)
            break;
        ++count;
        offset = found + expected.size();
    }
    return count;
}

static TestPath RepoRoot(TestArena& testArena){
    return TestPath(testArena.arena, __FILE__).parent_path().parent_path().parent_path().parent_path().lexically_normal();
}


// Compile, recording, and accepted-submission statistics live with the immutable graph artifacts. Keep the renderer
// bridge by-value so debug tooling can inspect one coherent generation without reaching into private packet storage.
TEST(EcsGraphics, DeferredGraphExposesRuntimeTelemetryArtifacts){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemHeaderSource;
    AString systemSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "system.h", systemHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "system.cpp", systemSource));
    const AStringView systemHeader(systemHeaderSource.data(), systemHeaderSource.size());
    const AStringView system(systemSource.data(), systemSource.size());

    EXPECT_TRUE(ContainsText(systemHeader, "deferredTaskGraphRuntimeStatistics()const noexcept"));
    EXPECT_TRUE(ContainsText(system, "Core::GpuTaskGraphRuntimeStatistics RendererSystem::deferredTaskGraphRuntimeStatistics()const noexcept"));
    EXPECT_TRUE(ContainsText(system, "Core::CollectGpuTaskGraphRuntimeStatistics("));
    EXPECT_TRUE(ContainsText(system, "m_deferredLightingCompiledGraph,"));
    EXPECT_TRUE(ContainsText(system, "m_deferredLightingRecordedGraph,"));
    EXPECT_TRUE(ContainsText(system, "m_deferredLightingSubmissionTransaction"));
}


// FrameGraphBuilder retains labels by view until the capture is encoded. Keep the renderer's human-readable
// runtime snapshot in persistent renderer-owned storage, and reset the label when no coherent attempt exists.
TEST(EcsGraphics, DeferredGraphRuntimeTelemetryUsesPersistentFrameGraphLabel){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemHeaderSource;
    AString systemSource;
    AString frameGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "system.h", systemHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "system.cpp", systemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "frame_graph_export.cpp", frameGraphSource));
    const AStringView systemHeader(systemHeaderSource.data(), systemHeaderSource.size());
    const AStringView system(systemSource.data(), systemSource.size());
    const AStringView frameGraph(frameGraphSource.data(), frameGraphSource.size());

    EXPECT_TRUE(ContainsText(systemHeader, "AString<Core::Alloc::GlobalArena> m_frameGraphRendererLabel;"));
    EXPECT_TRUE(ContainsText(system, ", m_frameGraphRendererLabel(arena)"));
    EXPECT_TRUE(ContainsText(frameGraph, "const Core::GpuTaskGraphRuntimeStatistics deferredRuntimeStatistics = deferredTaskGraphRuntimeStatistics();"));
    EXPECT_TRUE(ContainsText(frameGraph, "if(deferredRuntimeStatistics.valid()){"));
    EXPECT_TRUE(ContainsText(frameGraph, "StringAppendFormat(\n            m_frameGraphRendererLabel,"));
    EXPECT_TRUE(ContainsText(frameGraph, "\"Task graph: tasks={} packets={} deps={} transitions={}\\n\""));
    EXPECT_TRUE(ContainsText(frameGraph, "\"Declarations: resource sets={} resource-set members={} direct uses={} declared set uses={} expanded set-member uses={} materialized uses={}\\n\""));
    EXPECT_TRUE(ContainsText(frameGraph, "\"Data: payload objects={} payload object bytes={} upload blobs={} upload blob bytes={}\\n\""));
    EXPECT_TRUE(ContainsText(frameGraph, "\"Recording: packets={} tasks={} command lists={} barriers={} parallel={}\\n\""));
    EXPECT_TRUE(ContainsText(frameGraph, "\"Submission: accepted packets={} accepted tasks={} rejected packets={} rejected tasks={} submissions={} command lists={} waits={} failed submissions={}\\n\""));
    EXPECT_TRUE(ContainsText(frameGraph, "\"CPU: declaration={:.3f} ms compile={:.3f} ms record={:.3f} ms submit={:.3f} ms\\n\""));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.declarationSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "\"CPU compile phases: analysis={:.3f} ms queue assignment={:.3f} ms planning={:.3f} ms\\n\""));
    EXPECT_TRUE(ContainsText(frameGraph, "\"CPU planning detail: packetization={:.3f} ms resource states={:.3f} ms packet dependencies={:.3f} ms\\n\""));
    EXPECT_TRUE(ContainsText(frameGraph, "\"CPU recording phases: command-list acquisition={:.3f} ms graph barrier lowering={:.3f} ms task recording={:.3f} ms\""));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.analysisSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.queueAssignmentSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.planningSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.packetizationSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.resourceStatePlanningSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.packetDependencyPlanningSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.resourceSetCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.resourceSetMemberCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.directResourceUseCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.declaredResourceSetUseCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.expandedResourceSetMemberUseCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.resourceUseCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.payloadObjectCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.payloadObjectBytes,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.uploadBlobCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.uploadBlobBytes,"));
    EXPECT_TRUE(ContainsText(frameGraph, "recordingStatistics.commandListAcquisitionSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "recordingStatistics.graphBarrierRecordingSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "recordingStatistics.taskRecordSeconds * 1000.0"));
    EXPECT_TRUE(ContainsText(frameGraph, "submissionStatistics.acceptedPacketCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "submissionStatistics.acceptedTaskCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "submissionStatistics.rejectedPacketCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "submissionStatistics.rejectedTaskCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "submissionStatistics.rejectedSubmissionCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "m_frameGraphRendererLabel += \"Renderer Frame\";"));
    EXPECT_TRUE(ContainsText(frameGraph, "AStringView(m_frameGraphRendererLabel.data(), m_frameGraphRendererLabel.size())"));
}


// Descriptor heap lifetime is owned by the Device rather than any deferred graph attempt or physical queue. Keep
// one by-value current snapshot on the persistent renderer label so no-graph frames retain this diagnostic context.
TEST(EcsGraphics, FrameGraphExportsDeviceWideDescriptorHeapLifecycle){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString frameGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "frame_graph_export.cpp", frameGraphSource));
    const AStringView frameGraph(frameGraphSource.data(), frameGraphSource.size());

    EXPECT_TRUE(ContainsText(
        frameGraph,
        "const Core::GpuDescriptorHeapLifecycleStatistics descriptorHeapLifecycleStatistics =\n"
        "        m_graphics.getDevice().getDescriptorHeap().lifecycleStatistics()\n"
        "    ;"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "\"\\nDescriptor heap lifecycle (device-wide current): initialized={} \"\n"
        "        \"resource live/capacity={}/{} sampler live/capacity={}/{} \"\n"
        "        \"acceleration structure live/capacity={}/{} pending retired slots={} \"\n"
        "        \"accepted heap uses={} unsubmitted heap uses={} abandoned heap uses={}\""
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "StringAppendFormat(\n"
        "        m_frameGraphRendererLabel,\n"
        "        \"\\nDescriptor heap lifecycle (device-wide current): initialized={} \"\n"
        "        \"resource live/capacity={}/{} sampler live/capacity={}/{} \"\n"
        "        \"acceleration structure live/capacity={}/{} pending retired slots={} \"\n"
        "        \"accepted heap uses={} unsubmitted heap uses={} abandoned heap uses={}\""
    ));

    const usize snapshotOffset = frameGraph.find("const Core::GpuDescriptorHeapLifecycleStatistics descriptorHeapLifecycleStatistics");
    const usize runtimeValidOffset = frameGraph.find("if(deferredRuntimeStatistics.valid()){");
    const usize queueLoopOffset = frameGraph.find("for(usize queueIndex = 0u; queueIndex < queueTopology.queueCount; ++queueIndex){");
    const usize fallbackOffset = frameGraph.find("m_frameGraphRendererLabel += \"Renderer Frame\";");
    const usize lifecycleLabelOffset = frameGraph.find("Descriptor heap lifecycle (device-wide current):");
    const usize rendererFrameOffset = frameGraph.find("const Handle rendererFrame = builder.addPass(");
    ASSERT_NE(snapshotOffset, AStringView::npos);
    ASSERT_NE(runtimeValidOffset, AStringView::npos);
    ASSERT_NE(queueLoopOffset, AStringView::npos);
    ASSERT_NE(fallbackOffset, AStringView::npos);
    ASSERT_NE(lifecycleLabelOffset, AStringView::npos);
    ASSERT_NE(rendererFrameOffset, AStringView::npos);
    EXPECT_LT(runtimeValidOffset, snapshotOffset);
    EXPECT_LT(queueLoopOffset, snapshotOffset);
    EXPECT_LT(fallbackOffset, snapshotOffset);
    EXPECT_LT(snapshotOffset, lifecycleLabelOffset);
    EXPECT_LT(fallbackOffset, lifecycleLabelOffset);
    EXPECT_LT(lifecycleLabelOffset, rendererFrameOffset);

    const AStringView lifecycleLabel = frameGraph.substr(lifecycleLabelOffset, rendererFrameOffset - lifecycleLabelOffset);
    EXPECT_TRUE(ContainsText(lifecycleLabel, "descriptorHeapLifecycleStatistics.initialized,"));
    EXPECT_TRUE(ContainsText(lifecycleLabel, "descriptorHeapLifecycleStatistics.resourceLiveSlotCount,"));
    EXPECT_TRUE(ContainsText(lifecycleLabel, "descriptorHeapLifecycleStatistics.resourceCapacity,"));
    EXPECT_TRUE(ContainsText(lifecycleLabel, "descriptorHeapLifecycleStatistics.samplerLiveSlotCount,"));
    EXPECT_TRUE(ContainsText(lifecycleLabel, "descriptorHeapLifecycleStatistics.samplerCapacity,"));
    EXPECT_TRUE(ContainsText(lifecycleLabel, "descriptorHeapLifecycleStatistics.accelStructLiveSlotCount,"));
    EXPECT_TRUE(ContainsText(lifecycleLabel, "descriptorHeapLifecycleStatistics.accelStructCapacity,"));
    EXPECT_TRUE(ContainsText(lifecycleLabel, "descriptorHeapLifecycleStatistics.pendingRetiredSlotCount,"));
    EXPECT_TRUE(ContainsText(lifecycleLabel, "descriptorHeapLifecycleStatistics.acceptedHeapUseCount,"));
    EXPECT_TRUE(ContainsText(lifecycleLabel, "descriptorHeapLifecycleStatistics.unsubmittedHeapUseCount,"));
    EXPECT_TRUE(ContainsText(lifecycleLabel, "descriptorHeapLifecycleStatistics.abandonedHeapUseCount"));
    EXPECT_FALSE(ContainsText(lifecycleLabel, "queueStatistics."));
    EXPECT_FALSE(ContainsText(lifecycleLabel, "queueInfo."));
}


// Renderer-side graph declaration is intentionally separate from core compilation, and a failed attempt must not
// publish its elapsed time because the compiler only publishes the supplied value with a completed immutable plan.
TEST(EcsGraphics, DeferredGraphMeasuresDeclarationAttemptBeforeCoreCompile){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString taskGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "task_graph.cpp", taskGraphSource));
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());

    const usize lightingOffset = taskGraph.find("void RendererSystem::buildDeferredLightingTaskGraph");
    const usize resetOffset = taskGraph.find(
        "m_deferredLightingSubmissionTransaction.reset(m_deferredLightingCompiledGraph);",
        lightingOffset
    );
    const usize declarationBeginOffset = taskGraph.find("const Timer declarationBegin = TimerNow();", resetOffset);
    const usize feedbackOffset = taskGraph.find(
        "m_deferredTaskTimingFeedback.configureCompileOptions(",
        declarationBeginOffset
    );
    const usize declarationEndOffset = taskGraph.find(
        "compileOptions.declarationSeconds = DurationInSeconds<f64>(TimerNow(), declarationBegin);",
        feedbackOffset
    );
    const usize compilerOffset = taskGraph.find("if(!compiler.compile(", declarationEndOffset);
    ASSERT_NE(lightingOffset, AStringView::npos);
    ASSERT_NE(resetOffset, AStringView::npos);
    ASSERT_NE(declarationBeginOffset, AStringView::npos);
    ASSERT_NE(feedbackOffset, AStringView::npos);
    ASSERT_NE(declarationEndOffset, AStringView::npos);
    ASSERT_NE(compilerOffset, AStringView::npos);
    EXPECT_TRUE(ContainsText(taskGraph, "#include <global/timer.h>"));
    EXPECT_LT(resetOffset, declarationBeginOffset);
    EXPECT_LT(declarationBeginOffset, feedbackOffset);
    EXPECT_LT(feedbackOffset, declarationEndOffset);
    EXPECT_LT(declarationEndOffset, compilerOffset);
}


// The renderer label must enumerate the immutable compiled plan, not the live Device registry: compile, transaction,
// and recording telemetry are generation-bound to that plan. Keep only terminal-work queues so idle topology entries
// do not turn the persistent FrameGraph label into zero-only noise.
TEST(EcsGraphics, DeferredGraphFrameTelemetryUsesCompiledPhysicalQueueSnapshots){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString commandHeaderSource;
    AString compiledGraphHeaderSource;
    AString compiledGraphSource;
    AString frameGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "rhi" / "command.h", commandHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "task_graph" / "compiled_graph.h", compiledGraphHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "task_graph" / "compiled_graph.cpp", compiledGraphSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "frame_graph_export.cpp", frameGraphSource));
    const AStringView commandHeader(commandHeaderSource.data(), commandHeaderSource.size());
    const AStringView compiledGraphHeader(compiledGraphHeaderSource.data(), compiledGraphHeaderSource.size());
    const AStringView compiledGraph(compiledGraphSource.data(), compiledGraphSource.size());
    const AStringView frameGraph(frameGraphSource.data(), frameGraphSource.size());

    EXPECT_TRUE(ContainsText(commandHeader, "Borrowed immutable topology view; its producer owns the storage."));
    EXPECT_TRUE(ContainsText(compiledGraphHeader, "GpuPhysicalQueueTopology queueTopology()const noexcept;"));
    EXPECT_TRUE(ContainsText(compiledGraphHeader, "Borrowed immutable-plan topology view."));
    EXPECT_TRUE(ContainsText(compiledGraphHeader, "serialize access\n    // with reset/recompile"));
    EXPECT_TRUE(ContainsText(
        compiledGraphHeader,
        "GpuTaskGraphPhysicalQueueCompileStatistics physicalQueueCompileStatistics("
    ));
    EXPECT_TRUE(ContainsText(
        compiledGraphHeader,
        "    usize epilogueBarrierCount = 0u;\n"
        "    usize ownershipReleaseBarrierCount = 0u;\n"
        "    usize ownershipAcquireBarrierCount = 0u;\n\n"
        "    [[nodiscard]] bool valid()const noexcept{"
    ));
    EXPECT_TRUE(ContainsText(compiledGraph, "if(!valid())\n        return {};"));
    EXPECT_TRUE(ContainsText(compiledGraph, ".queues = m_queueTopology.empty() ? nullptr : m_queueTopology.data(),"));
    EXPECT_TRUE(ContainsText(compiledGraph, ".queueCount = m_queueTopology.size(),"));

    EXPECT_TRUE(ContainsText(frameGraph, "if(deferredRuntimeStatistics.valid()){"));
    EXPECT_TRUE(ContainsText(frameGraph, "m_deferredLightingCompiledGraph.queueTopology()"));
    EXPECT_FALSE(ContainsText(frameGraph, "getPhysicalQueueTopology()"));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "const Core::GpuPhysicalQueueInfo& queueInfo = queueTopology.queues[queueIndex];"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "m_deferredLightingSubmissionTransaction.physicalQueueSubmissionStatistics(\n"
        "                    m_deferredLightingCompiledGraph,\n"
        "                    queueInfo.id\n"
        "                )"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "m_deferredLightingCompiledGraph.physicalQueueCompileStatistics(queueInfo.id)"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "m_deferredLightingRecordedGraph.physicalQueueRecordingStatistics(\n"
        "                    m_deferredLightingCompiledGraph,\n"
        "                    queueInfo.id\n"
        "                )"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "if(\n"
        "                !queueStatistics.valid()\n"
        "                || (queueStatistics.acceptedPacketCount == 0u && queueStatistics.rejectedPacketCount == 0u)\n"
        "            )"
    ));
    EXPECT_TRUE(ContainsText(frameGraph, "if(!queueCompileStatistics.valid())\n                continue;"));
    EXPECT_TRUE(ContainsText(frameGraph, "if(!queueRecordingStatistics.valid())\n                continue;"));
    const usize terminalSubmissionGateOffset = frameGraph.find(
        "queueStatistics.acceptedPacketCount == 0u && queueStatistics.rejectedPacketCount == 0u"
    );
    const usize queueCompileQueryOffset = frameGraph.find(
        "m_deferredLightingCompiledGraph.physicalQueueCompileStatistics(queueInfo.id)"
    );
    const usize queueCompileGateOffset = frameGraph.find("queueCompileStatistics.valid()");
    const usize queueRecordingQueryOffset = frameGraph.find(
        "m_deferredLightingRecordedGraph.physicalQueueRecordingStatistics("
    );
    const usize queueRecordingGateOffset = frameGraph.find("queueRecordingStatistics.valid()");
    const usize queueFamilyIndexOffset = frameGraph.find("queueInfo.familyIndex,");
    const usize queueNativeIndexOffset = frameGraph.find("queueInfo.queueIndex,");
    const usize queueDedicatedOffset = frameGraph.find("queueInfo.dedicated,");
    ASSERT_NE(terminalSubmissionGateOffset, AStringView::npos);
    ASSERT_NE(queueCompileQueryOffset, AStringView::npos);
    ASSERT_NE(queueCompileGateOffset, AStringView::npos);
    ASSERT_NE(queueRecordingQueryOffset, AStringView::npos);
    ASSERT_NE(queueRecordingGateOffset, AStringView::npos);
    ASSERT_NE(queueFamilyIndexOffset, AStringView::npos);
    ASSERT_NE(queueNativeIndexOffset, AStringView::npos);
    ASSERT_NE(queueDedicatedOffset, AStringView::npos);
    EXPECT_LT(terminalSubmissionGateOffset, queueCompileQueryOffset);
    EXPECT_LT(queueCompileQueryOffset, queueCompileGateOffset);
    EXPECT_LT(queueCompileGateOffset, queueRecordingQueryOffset);
    EXPECT_LT(queueRecordingQueryOffset, queueRecordingGateOffset);
    EXPECT_LT(queueRecordingGateOffset, queueFamilyIndexOffset);
    EXPECT_LT(queueFamilyIndexOffset, queueNativeIndexOffset);
    EXPECT_LT(queueNativeIndexOffset, queueDedicatedOffset);
    EXPECT_FALSE(ContainsText(frameGraph, "queueCompileStatistics.taskCount == 0u"));
    EXPECT_FALSE(ContainsText(frameGraph, "queueCompileStatistics.packetCount == 0u"));
    EXPECT_FALSE(ContainsText(frameGraph, "queueRecordingStatistics.packetCount == 0u"));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "Physical queue index={} generation={} class={} family index={} native queue index={} dedicated={}:"
    ));
    EXPECT_TRUE(ContainsText(frameGraph, "accepted packets={} accepted tasks={} rejected packets={} rejected tasks={}"));
    EXPECT_TRUE(ContainsText(frameGraph, "native submissions={} rejected submit paths={} command lists={}"));
    EXPECT_TRUE(ContainsText(frameGraph, "planned waits={} same-queue elisions={} timeline waits={} merged timeline waits={}"));
    EXPECT_TRUE(ContainsText(frameGraph, "accepted frontier={} CPU={:.3f} ms"));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "  Compile plan: tasks={} packets={} merged tasks={} prologue barriers={} epilogue barriers={} ownership release barriers (subset)={} ownership acquire barriers (subset)={}"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "  Recording: packets={} tasks={} command lists={} barriers={} parallel={} CPU command-list acquisition={:.3f} ms graph barrier lowering={:.3f} ms task recording={:.3f} ms total={:.3f} ms"
    ));
    EXPECT_TRUE(ContainsText(frameGraph, "queueStatistics.queue.index,"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueStatistics.queue.deviceGeneration,"));
    EXPECT_TRUE(ContainsText(frameGraph, "__hidden_frame_graph_export::PhysicalQueueClassLabel(queueStatistics.queueClass),"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueInfo.familyIndex,"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueInfo.queueIndex,"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueInfo.dedicated,"));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "__hidden_frame_graph_export::PhysicalQueueClassLabel(queueStatistics.queueClass),\n"
        "                queueInfo.familyIndex,\n"
        "                queueInfo.queueIndex,\n"
        "                queueInfo.dedicated,\n"
        "                queueStatistics.acceptedPacketCount,"
    ));
    EXPECT_TRUE(ContainsText(frameGraph, "queueStatistics.rejectedSubmissionCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueStatistics.submissionSeconds * 1000.0"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueCompileStatistics.taskCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueCompileStatistics.packetCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueCompileStatistics.mergedTaskCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueCompileStatistics.prologueBarrierCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueCompileStatistics.epilogueBarrierCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueCompileStatistics.ownershipReleaseBarrierCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueCompileStatistics.ownershipAcquireBarrierCount,"));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "queueCompileStatistics.epilogueBarrierCount,\n"
        "                queueCompileStatistics.ownershipReleaseBarrierCount,\n"
        "                queueCompileStatistics.ownershipAcquireBarrierCount,\n"
        "                queueRecordingStatistics.packetCount,"
    ));
    EXPECT_TRUE(ContainsText(frameGraph, "queueRecordingStatistics.packetCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueRecordingStatistics.taskCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueRecordingStatistics.commandListCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueRecordingStatistics.barrierCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueRecordingStatistics.parallelPacketCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueRecordingStatistics.commandListAcquisitionSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueRecordingStatistics.graphBarrierRecordingSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueRecordingStatistics.taskRecordSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "queueRecordingStatistics.recordingSeconds * 1000.0"));
}


// MeshSkinning has a complete primary-Graphics serial chain with one terminal timing submission. It can therefore
// let FrontierScored coalesce its cheap immediate successors without reinstating per-task merge hints.
TEST(EcsGraphics, MeshSkinningUsesFrontierScoredSerialPacketization){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString skinningSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_mesh" / "skinning" / "system.cpp", skinningSource));
    const AStringView skinning(skinningSource.data(), skinningSource.size());

    EXPECT_TRUE(ContainsText(skinning, "Core::GpuTaskGraphCompileOptions compileOptions;"));
    EXPECT_TRUE(ContainsText(skinning, "compileOptions.packetizationPolicy = Core::GpuTaskGraphPacketizationPolicy::FrontierScored;"));
    EXPECT_TRUE(ContainsText(skinning, "compiler.compile(graph, analysis, topology, assignments, compiledGraph, scratchArena, compileOptions)"));
    EXPECT_EQ(CountText(skinning, "mergeWithPrevious"), 0u);
    EXPECT_EQ(CountText(skinning, "scheduling.frontierScoredMergeDomain = Name(\"mesh_skinning.serial\");"), 2u);
    EXPECT_EQ(CountText(skinning, "setDependencies(&terminalTask, 1u);"), 4u);
    EXPECT_TRUE(ContainsText(skinning, "if(compiledGraph.packetCount() != 1u)"));
    EXPECT_TRUE(ContainsText(skinning, "const Core::GpuPhysicalQueueId graphicsQueue = device.getPrimaryPhysicalQueue(Core::CommandQueue::Graphics);"));
    EXPECT_TRUE(ContainsText(skinning, "terminalQueue->id != graphicsQueue"));
    EXPECT_EQ(CountText(skinning, "Core::GpuTaskGraphTaskTimingTicket{"), 1u);
    EXPECT_TRUE(ContainsText(skinning, ".task = terminalTask,\n            .timingTicket = &timingTicket,"));
}


// Caustics and Surfel GI choose a semantic producer task at graph declaration. Keep their normal-frame merge and
// presence validation task-based so a later packet split cannot leak compiler packet identities back into the
// renderer's effect policy.
TEST(EcsGraphics, EffectsTopologyUsesSemanticTaskAnchors){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "system.cpp", systemSource));
    const AStringView system(systemSource.data(), systemSource.size());

    EXPECT_TRUE(ContainsText(system, "const Core::GpuTaskId causticsTask"));
    EXPECT_TRUE(ContainsText(system, "m_deferredCausticPhotonTask,\n            causticsTask"));
    EXPECT_TRUE(ContainsText(system, "m_deferredCausticResolveUpsampleTask,\n            causticsTask"));
    EXPECT_TRUE(ContainsText(system, "m_deferredSurfelGiIrradianceClearTask,\n            m_deferredSurfelGiTask"));
    EXPECT_TRUE(ContainsText(system, "m_deferredSurfelGiResolveTask,\n                m_deferredSurfelGiTask"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredSurfelGiTask)"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredHardwareCausticsTask)"));

    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId hardwareCausticsPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId causticPhotonPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId surfelGiPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId causticsPacket"));
}


// Prefix and shadow record spans are task-addressed. The renderer can still query the compiler for the exact
// terminal presentation packet elsewhere, but ordinary readiness and merge validation must not mirror packet IDs.
TEST(EcsGraphics, PrefixAndShadowTopologyUsesSemanticTaskAnchors){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "system.cpp", systemSource));
    const AStringView system(systemSource.data(), systemSource.size());

    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredShadowPrepareTask)"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_graphicsPrefixTask)"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_graphicsPrefixDeferredClearFirstTask)"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredShadowVisibilityTask)"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredSoftwareCausticsTask)"));
    EXPECT_TRUE(ContainsText(system, "tasksSharePacket(\n            m_graphicsPrefixDeferredClearFirstTask"));

    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId shadowPreparePacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId graphicsPrefixPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId shadowVisibilityPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId softwareCausticsPacket"));
}


// The AVBOIT routing choice can still produce one or five submissions, but validation must ask whether semantic
// stages compiled and share their declared packet rather than duplicate packet IDs for each stage.
TEST(EcsGraphics, AvboitTopologyUsesSemanticTaskAnchors){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "system.cpp", systemSource));
    const AStringView system(systemSource.data(), systemSource.size());

    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredAvboitPreTask)"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredAvboitDepthWarpTask)"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredAvboitExtinctionTask)"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredAvboitIntegrationTask)"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredAvboitAccumulationTask)"));
    EXPECT_TRUE(ContainsText(system, "tasksSharePacket(\n            m_deferredAvboitPreTask"));

    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId avboitPrePacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId avboitDepthWarpPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId avboitExtinctionPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId avboitIntegrationPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId avboitAccumulationPacket"));
}


// The exact terminal packet is retained solely in compiler-owned presentation metadata for the swap-chain binary
// signal. Every other normal renderer readiness check uses a declared task anchor or a semantic task range.
TEST(EcsGraphics, OnlyTerminalPresentationRetainsAPacketIdentity){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "system.cpp", systemSource));
    const AStringView system(systemSource.data(), systemSource.size());

    EXPECT_EQ(CountText(system, "packetForTask("), 0u);
    EXPECT_TRUE(ContainsText(system, "GpuCompiledPresentEndpoint* const presentationEndpoint"));
    EXPECT_TRUE(ContainsText(system, "m_deferredLightingCompiledGraph.presentEndpoint()"));
    EXPECT_TRUE(ContainsText(system, "presentationEndpoint->packet"));
    EXPECT_TRUE(ContainsText(system, "terminalPresentationPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId deferredLightingPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId deferredCompositePacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId deferredPresentPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId deferredLaggedLightingHistoryPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId deferredFrameRecoveryPacket"));
}


// The frame timing query must record its published endpoint after the optional presentation contributor. A rejected
// endpoint remains recoverable through the separate non-publishing recovery task instead of silently publishing a
// partial frame duration.
TEST(EcsGraphics, FrameTimingUsesGraphOwnedTerminalPresentationEndpoint){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    AString taskGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "system.cpp", systemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "task_graph.cpp", taskGraphSource));
    const AStringView system(systemSource.data(), systemSource.size());
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());

    const usize deferredPresentOffset = taskGraph.find("struct DeferredPresentGraphTask");
    const usize frameTimingEndOffset = taskGraph.find("struct FrameTimingEndGraphTask", deferredPresentOffset);
    const usize recoveryOffset = taskGraph.find("struct FrameRecoveryGraphTask");
    const usize shadowPrepareOffset = taskGraph.find("struct ShadowPrepareGraphTask");
    const usize meshViewSetupOffset = taskGraph.find("struct MeshViewSetupGraphTask", shadowPrepareOffset);
    ASSERT_NE(deferredPresentOffset, AStringView::npos);
    ASSERT_NE(frameTimingEndOffset, AStringView::npos);
    ASSERT_NE(recoveryOffset, AStringView::npos);
    ASSERT_NE(shadowPrepareOffset, AStringView::npos);
    ASSERT_NE(meshViewSetupOffset, AStringView::npos);
    ASSERT_LT(deferredPresentOffset, frameTimingEndOffset);
    ASSERT_LT(shadowPrepareOffset, meshViewSetupOffset);

    const AStringView deferredPresent = taskGraph.substr(deferredPresentOffset, frameTimingEndOffset - deferredPresentOffset);
    EXPECT_FALSE(ContainsText(deferredPresent, "frameTimingTransaction"));
    EXPECT_FALSE(ContainsText(deferredPresent, "recordEnd(commandList)"));
    EXPECT_TRUE(ContainsText(taskGraph, "render.frame_timing_end"));
    EXPECT_TRUE(ContainsText(taskGraph, "setDependencies(&frameTimingEndDependency, 1u)"));
    EXPECT_TRUE(ContainsText(taskGraph, "frameTimingTransaction->recordEnd(commandList)"));
    EXPECT_TRUE(ContainsText(taskGraph, "declarePresentEndpoint(Core::GpuPresentEndpoint{"));
    EXPECT_TRUE(ContainsText(taskGraph, ".producer = m_deferredFrameTimingEndTask,"));
    EXPECT_TRUE(ContainsText(taskGraph, ".backBuffer = backbuffer,"));

    const AStringView shadowPrepare = taskGraph.substr(shadowPrepareOffset, meshViewSetupOffset - shadowPrepareOffset);
    EXPECT_TRUE(ContainsText(shadowPrepare, "frameTimingTransaction->begin("));
    const AStringView meshViewSetup = taskGraph.substr(meshViewSetupOffset, deferredPresentOffset - meshViewSetupOffset);
    EXPECT_FALSE(ContainsText(meshViewSetup, "frameTimingTransaction->begin("));

    const AStringView recovery = taskGraph.substr(recoveryOffset, deferredPresentOffset - recoveryOffset);
    EXPECT_TRUE(ContainsText(recovery, "frameTimingTransaction->recordEnd(commandList)"));
    EXPECT_TRUE(ContainsText(recovery, "confirmEndSubmission(false)"));
    EXPECT_TRUE(ContainsText(system, "presentationEndpoint->producer"));
    EXPECT_TRUE(ContainsText(system, "presentationEndpoint->queue != primaryGraphicsQueue"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredFrameTimingEndTask)"));

    const usize shadowPrepareAcceptanceOffset = system.find("const auto acceptShadowPrepareTask = [](");
    ASSERT_NE(shadowPrepareAcceptanceOffset, AStringView::npos);
    const usize shadowPreparePrefixAcceptedOffset = system.find(
        "const bool shadowPreparePrefixAccepted =",
        shadowPrepareAcceptanceOffset
    );
    ASSERT_NE(shadowPreparePrefixAcceptedOffset, AStringView::npos);
    const AStringView shadowPrepareAcceptance = system.substr(
        shadowPrepareAcceptanceOffset,
        shadowPreparePrefixAcceptedOffset - shadowPrepareAcceptanceOffset
    );
    EXPECT_TRUE(ContainsText(shadowPrepareAcceptance, "context->frameTimingTransaction->confirmBeginSubmission()"));
    EXPECT_TRUE(ContainsText(shadowPrepareAcceptance, ".task = m_deferredShadowPrepareTask,"));
    EXPECT_TRUE(ContainsText(shadowPrepareAcceptance, ".invoke = acceptShadowPrepareTask,"));
    EXPECT_FALSE(ContainsText(system, "acceptGraphicsPrefixBeginTask"));
}


// Late recovery, readback, and history tasks own their record/submit/reject sequencing in the generic runtime.
// Keep the renderer limited to payload validation, timing arming, and device-recreation policy rather than
// reconstructing compiler packet ranges around every late tail.
TEST(EcsGraphics, LateGraphTailsUseRuntimeHelpers){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "system.cpp", systemSource));
    const AStringView system(systemSource.data(), systemSource.size());

    EXPECT_TRUE(ContainsText(system, "recordAndSubmitAcceptedFrontierTask("));
    EXPECT_TRUE(ContainsText(system, "deferredRecorder,\n            m_deferredLightingRecordedGraph,\n            m_deferredFrameRecoveryTask"));
    EXPECT_FALSE(ContainsText(system, "deferredFrameRecoveryPacketRange"));
    EXPECT_FALSE(ContainsText(system, "surfelGiCounterReadbackPacketRange"));
    EXPECT_FALSE(ContainsText(system, "deferredLaggedLightingHistoryPacketRange"));
    EXPECT_EQ(CountText(system, "recordAndSubmitTask("), 2u);
    EXPECT_FALSE(ContainsText(system, "recordTaskRangeInCompileOrder("));
    EXPECT_FALSE(ContainsText(system, "submitTaskRangeInCompileOrder("));
    EXPECT_FALSE(ContainsText(system, "const auto discardFrameRecovery"));
    EXPECT_FALSE(ContainsText(system, "failed to late-record deferred frame recovery packet"));
    EXPECT_FALSE(ContainsText(system, "deferred frame recovery submission was rejected"));
}


// The graph-owned ImGui terminal task must record from declaration-time data. Re-reading ImGui's mutable command
// arrays after the task declares its sampled textures would allow an undeclared bindless access into the packet.
TEST(EcsGraphics, UiPresentationSnapshotsLateRecordInputs){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString uiSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "system.cpp", uiSource));
    const AStringView ui(uiSource.data(), uiSource.size());

    AString uiHeaderSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "system.h", uiHeaderSource));
    const AStringView uiHeader(uiHeaderSource.data(), uiHeaderSource.size());

    EXPECT_TRUE(ContainsText(uiHeader, "struct TaskGraphDrawCommand"));
    EXPECT_TRUE(ContainsText(ui, "m_taskGraphDrawCommands"));
    EXPECT_TRUE(ContainsText(ui, "recordTaskGraphDrawSnapshot"));
    EXPECT_TRUE(ContainsText(ui, "graph-owned ImGui overlay cannot safely record a custom draw callback"));
    EXPECT_TRUE(ContainsText(ui, "appendDrawTextureUse(drawCommand.texture)"));

    const usize recordOffset = ui.find("bool UiSystem::recordTaskGraphPresentation");
    const usize opaqueRecordOffset = ui.find("bool UiSystem::recordStandaloneLegacyTaskGraphPresentation", recordOffset);
    ASSERT_NE(recordOffset, AStringView::npos);
    ASSERT_NE(opaqueRecordOffset, AStringView::npos);
    ASSERT_LT(recordOffset, opaqueRecordOffset);
    const AStringView recordBody = ui.substr(recordOffset, opaqueRecordOffset - recordOffset);
    EXPECT_TRUE(ContainsText(recordBody, "recordTaskGraphDrawSnapshot(commandList, framebuffer)"));
    EXPECT_FALSE(ContainsText(recordBody, "ImGui::GetDrawData()"));
    EXPECT_FALSE(ContainsText(recordBody, "renderDrawData(commandList, framebuffer"));

    // The separately named opaque fallback is intentionally the sole graph task allowed to touch live callback
    // storage, and it must guard that synchronous boundary against a changed ImGui frame.
    const usize completionOffset = ui.find("bool UiSystem::recordTaskGraphUploadCompletion", opaqueRecordOffset);
    ASSERT_NE(completionOffset, AStringView::npos);
    const AStringView opaqueRecord = ui.substr(opaqueRecordOffset, completionOffset - opaqueRecordOffset);
    EXPECT_TRUE(ContainsText(opaqueRecord, "ImGui::GetDrawData() != drawData"));
    EXPECT_TRUE(ContainsText(opaqueRecord, "frameGeneration != m_frameGeneration"));
    EXPECT_TRUE(ContainsText(opaqueRecord, "renderDrawData(commandList, framebuffer, *drawData)"));
}


// Large immutable UI uploads may already select Transfer/Compute. Their persistent buffers and textures must be
// created with the matching graph-sharing contract before a same-class auxiliary queue can legally record them.
TEST(EcsGraphics, UiGraphUploadsDeclareConcurrentProducerFamilies){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString uiSystemSource;
    AString uiTextureSource;
    AString uiGraphicsResourceSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "system.cpp", uiSystemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "texture_resources.cpp", uiTextureSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "graphics_resources.cpp", uiGraphicsResourceSource));
    const AStringView uiSystem(uiSystemSource.data(), uiSystemSource.size());
    const AStringView uiTextures(uiTextureSource.data(), uiTextureSource.size());
    const AStringView uiGraphicsResources(uiGraphicsResourceSource.data(), uiGraphicsResourceSource.size());

    EXPECT_TRUE(ContainsText(uiSystem, "allowSameClassQueueRouting = preferDedicatedTransport"));
    EXPECT_TRUE(ContainsText(uiSystem, "allowCrossFamilySameClassQueueRouting = preferDedicatedTransport"));
    EXPECT_TRUE(ContainsText(uiTextures, "allowSameClassQueueRouting = preferDedicatedTransport"));
    EXPECT_TRUE(ContainsText(uiTextures, "allowCrossFamilySameClassQueueRouting = preferDedicatedTransport"));
    EXPECT_TRUE(ContainsText(uiTextures, "ResourceQueueSharing::GraphicsAsyncComputeAndTransfer"));
    EXPECT_TRUE(ContainsText(uiGraphicsResources, "ResourceQueueSharing::GraphicsAsyncComputeAndTransfer"));
}


// The exceptional non-renderer/custom-callback UI route must keep its texture uploads and ordinary rasterization
// graph-owned. An arbitrary callback is explicitly opaque and serial, but submitStandaloneTaskGraph() records it
// synchronously; native direct rendering remains only as the last availability fallback after that graph rejects.
TEST(EcsGraphics, UiLegacyFallbackUsesStandaloneGraphs){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString graphicsHeaderSource;
    AString graphicsSource;
    AString uiSource;
    AString uiTextureSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "module.h", graphicsHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "module.cpp", graphicsSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "system.cpp", uiSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "texture_resources.cpp", uiTextureSource));
    const AStringView graphicsHeader(graphicsHeaderSource.data(), graphicsHeaderSource.size());
    const AStringView graphics(graphicsSource.data(), graphicsSource.size());
    const AStringView ui(uiSource.data(), uiSource.size());
    const AStringView uiTextures(uiTextureSource.data(), uiTextureSource.size());

    EXPECT_TRUE(ContainsText(graphicsHeader, "StandaloneTaskGraphDeclaration"));
    EXPECT_TRUE(ContainsText(graphicsHeader, "submitStandaloneTaskGraph"));
    EXPECT_TRUE(ContainsText(graphics, "Graphics::submitStandaloneTaskGraph"));
    EXPECT_TRUE(ContainsText(ui, "StandaloneTextureUploadCompletionTask"));
    EXPECT_TRUE(ContainsText(ui, "declareStandaloneTextureUploadGraph"));
    EXPECT_TRUE(ContainsText(ui, "submitStandaloneTaskGraphPresentation"));
    EXPECT_TRUE(ContainsText(ui, "StandaloneLegacyPresentationTask"));
    EXPECT_TRUE(ContainsText(ui, "declareStandaloneLegacyTaskGraphPresentation"));
    EXPECT_TRUE(ContainsText(ui, "submitStandaloneLegacyTaskGraphPresentation"));
    EXPECT_TRUE(ContainsText(ui, "OpaquePresentationQueueRequest"));
    EXPECT_TRUE(ContainsText(ui, "Standalone ImGui Presentation Back Buffer"));
    EXPECT_TRUE(ContainsText(ui, "ImGui Opaque Callback Domain"));
    EXPECT_TRUE(ContainsText(ui, "if(prepareTaskGraphPresentation(framebuffer))"));

    const usize opaquePresentationOffset = ui.find("Core::GpuTaskId UiSystem::declareStandaloneLegacyTaskGraphPresentation");
    const usize legacySubmitOffset = ui.find("bool UiSystem::submitPreparedLegacyTextureUploads");
    ASSERT_NE(opaquePresentationOffset, AStringView::npos);
    ASSERT_NE(legacySubmitOffset, AStringView::npos);
    ASSERT_LT(opaquePresentationOffset, legacySubmitOffset);
    const AStringView opaquePresentation = ui.substr(opaquePresentationOffset, legacySubmitOffset - opaquePresentationOffset);
    EXPECT_TRUE(ContainsText(opaquePresentation, "m_graphics.submitStandaloneTaskGraph"));
    EXPECT_TRUE(ContainsText(opaquePresentation, "importTaskGraphTexture(graph, *textureResource)"));
    EXPECT_TRUE(ContainsText(opaquePresentation, "m_frameGeneration"));
    EXPECT_TRUE(ContainsText(opaquePresentation, "setQueue(__hidden_ui::OpaquePresentationQueueRequest())"));
    EXPECT_TRUE(ContainsText(ui, "recordStandaloneLegacyTaskGraphPresentation"));
    EXPECT_FALSE(ContainsText(opaquePresentation, "executeCommandLists"));
    EXPECT_FALSE(ContainsText(opaquePresentation, "createCommandList"));

    const usize renderOffset = ui.find("void UiSystem::render", legacySubmitOffset);
    ASSERT_NE(renderOffset, AStringView::npos);
    const AStringView legacySubmit = ui.substr(legacySubmitOffset, renderOffset - legacySubmitOffset);
    EXPECT_TRUE(ContainsText(legacySubmit, "m_graphics.submitStandaloneTaskGraph"));
    EXPECT_TRUE(ContainsText(legacySubmit, "getPrimaryPhysicalQueue(Core::CommandQueue::Graphics)"));
    EXPECT_TRUE(ContainsText(legacySubmit, "submissionToken,\n        graphicsQueue"));
    EXPECT_FALSE(ContainsText(legacySubmit, "executeCommandLists"));
    EXPECT_FALSE(ContainsText(legacySubmit, "createCommandList"));
    EXPECT_FALSE(ContainsText(legacySubmit, "prepareTextureRequests"));
    EXPECT_FALSE(ContainsText(ui, "m_prepareCommandList"));
    EXPECT_FALSE(ContainsText(uiTextures, "recordTextureUpload"));
    EXPECT_TRUE(ContainsText(uiTextures, "if(previousTask.valid())"));

    const usize presentationDeclareOffset = ui.find("Core::GpuTaskId UiSystem::declareTaskGraphPresentation");
    const usize standaloneTextureOffset = ui.find("Core::GpuTaskId UiSystem::declareStandaloneTextureUploadGraph");
    ASSERT_NE(presentationDeclareOffset, AStringView::npos);
    ASSERT_NE(standaloneTextureOffset, AStringView::npos);
    const AStringView presentationDeclare = ui.substr(
        presentationDeclareOffset,
        standaloneTextureOffset - presentationDeclareOffset
    );
    EXPECT_FALSE(ContainsText(presentationDeclare, "|| !previousTask.valid()"));
    EXPECT_TRUE(ContainsText(presentationDeclare, "if(previousTask.valid())"));

    const AStringView presentationRenderBody = ui.substr(renderOffset);
    const usize standalonePresentationOffset = presentationRenderBody.find("submitStandaloneTaskGraphPresentation(framebuffer)");
    const usize opaquePresentationFallbackOffset = presentationRenderBody.find("submitStandaloneLegacyTaskGraphPresentation(framebuffer)");
    const usize directTextureFallbackOffset = presentationRenderBody.find("submitPreparedLegacyTextureUploads(*drawData)");
    ASSERT_NE(standalonePresentationOffset, AStringView::npos);
    ASSERT_NE(opaquePresentationFallbackOffset, AStringView::npos);
    ASSERT_NE(directTextureFallbackOffset, AStringView::npos);
    EXPECT_LT(standalonePresentationOffset, directTextureFallbackOffset);
    EXPECT_LT(standalonePresentationOffset, opaquePresentationFallbackOffset);
    EXPECT_LT(opaquePresentationFallbackOffset, directTextureFallbackOffset);

    const usize prepareFrameOffset = ui.find("bool UiSystem::prepareFrameResources");
    const usize snapshotClearOffset = ui.find("void UiSystem::clearTaskGraphDrawSnapshot", prepareFrameOffset);
    const usize resizeOffset = ui.find("void UiSystem::backBufferResizing", renderOffset);
    ASSERT_NE(prepareFrameOffset, AStringView::npos);
    ASSERT_NE(snapshotClearOffset, AStringView::npos);
    ASSERT_NE(resizeOffset, AStringView::npos);
    const AStringView prepareFrame = ui.substr(prepareFrameOffset, snapshotClearOffset - prepareFrameOffset);
    const AStringView directRenderBody = ui.substr(renderOffset, resizeOffset - renderOffset);
    EXPECT_TRUE(ContainsText(prepareFrame, "ensureRenderCommandList()"));
    EXPECT_FALSE(ContainsText(directRenderBody, "ensureRenderCommandList()"));
    EXPECT_FALSE(ContainsText(directRenderBody, "prepareTextureRequests"));
    EXPECT_TRUE(ContainsText(directRenderBody, "standalone legacy ImGui graph presentation failed; retaining direct raster fallback"));
    EXPECT_TRUE(ContainsText(directRenderBody, "direct ImGui fallback submission was rejected; retaining frame for retry"));

    const usize directTextureSubmitOffset = directRenderBody.find("submitPreparedLegacyTextureUploads(*drawData)");
    const usize directExecuteOffset = directRenderBody.find("device.executeCommandLists(commandLists, 1, Core::CommandQueue::Graphics, &submitted)");
    const usize directRejectedSubmitOffset = directRenderBody.find("if(!submitted)", directExecuteOffset);
    const usize directFrameResetOffset = directRenderBody.find("m_frameStarted = false", directExecuteOffset);
    ASSERT_NE(directTextureSubmitOffset, AStringView::npos);
    ASSERT_NE(directExecuteOffset, AStringView::npos);
    ASSERT_NE(directRejectedSubmitOffset, AStringView::npos);
    ASSERT_NE(directFrameResetOffset, AStringView::npos);
    EXPECT_LT(directTextureSubmitOffset, directExecuteOffset);
    EXPECT_LT(directExecuteOffset, directRejectedSubmitOffset);
    EXPECT_LT(directRejectedSubmitOffset, directFrameResetOffset);
}


// Public setup uploads return only a resource handle, so a Transfer/Compute producer must still establish queue
// order for later legacy consumers. Keep those readiness packets inside the same graph transaction instead of
// issuing an opaque direct zero-command submission after graph acceptance.
TEST(EcsGraphics, SetupUploadReadinessBridgeRemainsGraphOwned){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString graphicsSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "module.cpp", graphicsSource));
    const AStringView graphics(graphicsSource.data(), graphicsSource.size());

    EXPECT_TRUE(ContainsText(graphics, "SetupUploadReadinessBridgeGraphTask"));
    EXPECT_TRUE(ContainsText(graphics, "DeclareSetupUploadReadinessBridgeTasks"));
    EXPECT_TRUE(ContainsText(graphics, "graphics.setup_upload.readiness_bridge"));
    EXPECT_FALSE(ContainsText(graphics, "BridgeSetupUploadToConsumerQueues"));

    const usize setupUploadOffset = graphics.find("static bool SubmitGraphOwnedSetupUpload");
    const usize timingResetOffset = graphics.find("struct FrameTimingResetGraphTask", setupUploadOffset);
    ASSERT_NE(setupUploadOffset, AStringView::npos);
    ASSERT_NE(timingResetOffset, AStringView::npos);
    const AStringView setupUpload = graphics.substr(setupUploadOffset, timingResetOffset - setupUploadOffset);
    EXPECT_TRUE(ContainsText(setupUpload, "DeclareSetupUploadReadinessBridgeTasks"));
    EXPECT_TRUE(ContainsText(setupUpload, "bridgePrimaryUploadQueue"));
    EXPECT_TRUE(ContainsText(setupUpload, "requiredTerminalQueue"));
    EXPECT_FALSE(ContainsText(setupUpload, "executeCommandLists"));
    EXPECT_TRUE(ContainsText(graphics, "ResolveSetupUploadSameClassRouting"));
    EXPECT_TRUE(ContainsText(graphics, "preferNonPrimarySameClassQueue"));
    EXPECT_TRUE(ContainsText(graphics, "sameClassRouting.enabled ? sameClassRouting.primaryQueue"));

    const usize textureBatchOffset = graphics.find("bool Graphics::uploadTextureBatch");
    const usize meshSetupOffset = graphics.find("Graphics::MeshResource Graphics::setupMesh", textureBatchOffset);
    ASSERT_NE(textureBatchOffset, AStringView::npos);
    ASSERT_NE(meshSetupOffset, AStringView::npos);
    const AStringView textureBatch = graphics.substr(textureBatchOffset, meshSetupOffset - textureBatchOffset);
    EXPECT_TRUE(ContainsText(textureBatch, "preserveSameClassQueueWithDirectDependency"));
    EXPECT_TRUE(ContainsText(textureBatch, "sameClassRouting.crossesQueueFamily"));
}


// The parity baseline must pin temporal AVBOIT in test code, not introduce a renderer/core feature toggle. The
// runner waits for the smoke marker only after that project has suspended its next submission.
TEST(EcsGraphics, TransparentAvboitBaselineCaptureIsFrameLockedAndTestOwned){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString profileSource;
    AString runnerSource;
    AString smokeSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "ab" / "renderer_baseline" / "profiles.py", profileSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "ab" / "renderer_baseline" / "run.py", runnerSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / "transparent_multi_project.cpp", smokeSource));
    const AStringView profiles(profileSource.data(), profileSource.size());
    const AStringView runner(runnerSource.data(), runnerSource.size());
    const AStringView smoke(smokeSource.data(), smokeSource.size());

    EXPECT_TRUE(ContainsText(profiles, "capture_freeze_frame=96"));
    EXPECT_TRUE(ContainsText(profiles, "capture_ready_log=\"TransparentMultiSmokeProject: renderer baseline capture ready after\""));
    EXPECT_TRUE(ContainsText(profiles, "fixed_delta_seconds=1.0 / 60.0"));
    EXPECT_TRUE(ContainsText(runner, "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME"));
    EXPECT_TRUE(ContainsText(runner, "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS"));
    EXPECT_TRUE(ContainsText(runner, "wait_for_log_message("));
    EXPECT_TRUE(ContainsText(smoke, "rendererBaselineCaptureFreezeFrame"));
    EXPECT_TRUE(ContainsText(smoke, "rendererBaselineFixedDelta"));
    EXPECT_TRUE(ContainsText(smoke, "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS"));
    EXPECT_TRUE(ContainsText(smoke, "m_context.graphics.setFrameSubmissionSuspended(true)"));
    EXPECT_TRUE(ContainsText(smoke, "renderer baseline capture ready after {} rendered frames; render submission suspended"));
    EXPECT_TRUE(ContainsText(smoke, "m_context.graphics.setFrameSubmissionSuspended(false)"));
}


// Skinned CSG animates both the receiver and the cutter. It must use the same test-only freeze contract before its
// baseline can act as a runtime-skinning/CSG parity reference.
TEST(EcsGraphics, SkinnedCsgBaselineCaptureIsFrameLockedAndTestOwned){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString profileSource;
    AString smokeSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "ab" / "renderer_baseline" / "profiles.py", profileSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / "csg_skinned_visible_project.cpp", smokeSource));
    const AStringView profiles(profileSource.data(), profileSource.size());
    const AStringView smoke(smokeSource.data(), smokeSource.size());

    EXPECT_TRUE(ContainsText(profiles, "window_title=\"NWB Skinned CSG Smoke\""));
    EXPECT_TRUE(ContainsText(profiles, "capture_ready_log=\"CsgSkinnedVisibleSmokeProject: renderer baseline capture ready after\""));
    EXPECT_TRUE(ContainsText(profiles, "fixed_delta_seconds=1.0 / 60.0"));
    EXPECT_TRUE(ContainsText(smoke, "rendererBaselineCaptureFreezeFrame"));
    EXPECT_TRUE(ContainsText(smoke, "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME"));
    EXPECT_TRUE(ContainsText(smoke, "rendererBaselineFixedDelta"));
    EXPECT_TRUE(ContainsText(smoke, "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS"));
    EXPECT_TRUE(ContainsText(smoke, "m_context.graphics.setFrameSubmissionSuspended(true)"));
    EXPECT_TRUE(ContainsText(smoke, "CsgSkinnedVisibleSmokeProject: renderer baseline capture ready after {} rendered frames; render submission suspended"));
    EXPECT_TRUE(ContainsText(smoke, "m_context.graphics.setFrameSubmissionSuspended(false)"));
}


// Caustic accumulation is temporal even with a static refractor. Keep its baseline warm-up frame-counted and on the
// same test-only fixed clock rather than allowing host throughput to choose an arbitrary convergence point.
TEST(EcsGraphics, CausticBaselineUsesFixedTemporalWarmup){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString profileSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "ab" / "renderer_baseline" / "profiles.py", profileSource));
    const AStringView profiles(profileSource.data(), profileSource.size());
    const usize causticsOffset = profiles.find("\"caustics\": BaselineProfile(");
    const usize surfelOffset = profiles.find("\"surfel-gi\": BaselineProfile(", causticsOffset);
    ASSERT_NE(causticsOffset, AStringView::npos);
    ASSERT_NE(surfelOffset, AStringView::npos);
    ASSERT_LT(causticsOffset, surfelOffset);
    const AStringView caustics = profiles.substr(causticsOffset, surfelOffset - causticsOffset);

    EXPECT_TRUE(ContainsText(caustics, "settle_seconds=0.75"));
    EXPECT_TRUE(ContainsText(caustics, "capture_freeze_frame=360"));
    EXPECT_TRUE(ContainsText(caustics, "capture_ready_log=\"TransparentMultiSmokeProject: renderer baseline capture ready after\""));
    EXPECT_TRUE(ContainsText(caustics, "fixed_delta_seconds=1.0 / 60.0"));
}


// Surfel GI has its own temporal producer/resolve sequence. Its baseline therefore needs a project-local, fixed
// frame boundary rather than inheriting wall-clock capture timing from an unrelated smoke scene.
TEST(EcsGraphics, SurfelGiBaselineUsesFixedTemporalWarmup){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString profileSource;
    AString smokeSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "ab" / "renderer_baseline" / "profiles.py", profileSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / "gi_test_project.cpp", smokeSource));
    const AStringView profiles(profileSource.data(), profileSource.size());
    const AStringView smoke(smokeSource.data(), smokeSource.size());
    const usize surfelOffset = profiles.find("\"surfel-gi\": BaselineProfile(");
    const usize stressOffset = profiles.find("\"stress\": BaselineProfile(", surfelOffset);
    ASSERT_NE(surfelOffset, AStringView::npos);
    ASSERT_NE(stressOffset, AStringView::npos);
    ASSERT_LT(surfelOffset, stressOffset);
    const AStringView surfel = profiles.substr(surfelOffset, stressOffset - surfelOffset);

    EXPECT_TRUE(ContainsText(surfel, "settle_seconds=0.75"));
    EXPECT_TRUE(ContainsText(surfel, "capture_freeze_frame=360"));
    EXPECT_TRUE(ContainsText(surfel, "capture_ready_log=\"GiTestSmokeProject: renderer baseline capture ready after\""));
    EXPECT_TRUE(ContainsText(surfel, "fixed_delta_seconds=1.0 / 60.0"));
    EXPECT_TRUE(ContainsText(smoke, "rendererBaselineCaptureFreezeFrame"));
    EXPECT_TRUE(ContainsText(smoke, "rendererBaselineFixedDelta"));
    EXPECT_TRUE(ContainsText(smoke, "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME"));
    EXPECT_TRUE(ContainsText(smoke, "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS"));
    EXPECT_TRUE(ContainsText(smoke, "GiTestSmokeProject: renderer baseline capture ready after {} rendered frames; render submission suspended"));
}


// Soft shadows retain temporal history even when the camera/yaw are frozen. Capture the same accepted history phase
// through the smoke-only fixed clock before using this scene as a parity reference.
TEST(EcsGraphics, SoftShadowBaselineUsesFixedTemporalWarmup){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString profileSource;
    AString smokeSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "ab" / "renderer_baseline" / "profiles.py", profileSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / "soft_shadow_test_project.cpp", smokeSource));
    const AStringView profiles(profileSource.data(), profileSource.size());
    const AStringView smoke(smokeSource.data(), smokeSource.size());
    const usize softShadowOffset = profiles.find("\"soft-shadows\": BaselineProfile(");
    const usize causticsOffset = profiles.find("\"caustics\": BaselineProfile(", softShadowOffset);
    ASSERT_NE(softShadowOffset, AStringView::npos);
    ASSERT_NE(causticsOffset, AStringView::npos);
    ASSERT_LT(softShadowOffset, causticsOffset);
    const AStringView softShadows = profiles.substr(softShadowOffset, causticsOffset - softShadowOffset);

    EXPECT_TRUE(ContainsText(softShadows, "settle_seconds=0.75"));
    EXPECT_TRUE(ContainsText(softShadows, "capture_freeze_frame=360"));
    EXPECT_TRUE(ContainsText(softShadows, "capture_ready_log=\"SoftShadowTestSmokeProject: renderer baseline capture ready after\""));
    EXPECT_TRUE(ContainsText(softShadows, "fixed_delta_seconds=1.0 / 60.0"));
    EXPECT_TRUE(ContainsText(smoke, "rendererBaselineCaptureFreezeFrame"));
    EXPECT_TRUE(ContainsText(smoke, "rendererBaselineFixedDelta"));
    EXPECT_TRUE(ContainsText(smoke, "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME"));
    EXPECT_TRUE(ContainsText(smoke, "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS"));
    EXPECT_TRUE(ContainsText(smoke, "SoftShadowTestSmokeProject: renderer baseline capture ready after {} rendered frames; render submission suspended"));
}


// The stress scene already has a dedicated M4 capture contract. Its baseline path must remain separate, while still
// pinning the dense skinned transparent/opaque workload to a reproducible frame and simulation clock.
TEST(EcsGraphics, StressBaselineUsesSeparateFixedTemporalWarmup){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString profileSource;
    AString smokeSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "ab" / "renderer_baseline" / "profiles.py", profileSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / "stress_test_project.cpp", smokeSource));
    const AStringView profiles(profileSource.data(), profileSource.size());
    const AStringView smoke(smokeSource.data(), smokeSource.size());
    const usize stressOffset = profiles.find("\"stress\": BaselineProfile(");
    ASSERT_NE(stressOffset, AStringView::npos);
    const AStringView stress = profiles.substr(stressOffset);

    EXPECT_TRUE(ContainsText(stress, "settle_seconds=0.75"));
    EXPECT_TRUE(ContainsText(stress, "capture_freeze_frame=96"));
    EXPECT_TRUE(ContainsText(stress, "capture_ready_log=\"StressTestSmokeProject: renderer baseline capture ready after\""));
    EXPECT_TRUE(ContainsText(stress, "fixed_delta_seconds=1.0 / 60.0"));
    EXPECT_TRUE(ContainsText(smoke, "m4PixelCaptureFreezeFrame"));
    EXPECT_TRUE(ContainsText(smoke, "rendererBaselineCaptureFreezeFrame"));
    EXPECT_TRUE(ContainsText(smoke, "rendererBaselineFixedDelta"));
    EXPECT_TRUE(ContainsText(smoke, "StressTestSmokeProject: M4 pixel capture ready after {} rendered frames; render submission suspended"));
    EXPECT_TRUE(ContainsText(smoke, "StressTestSmokeProject: renderer baseline capture ready after {} rendered frames; render submission suspended"));
    EXPECT_TRUE(ContainsText(smoke, "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS"));
}


// Surfel GI is an explicitly promoted Compute adopter. It can select an alternate Compute family only for the
// graph-owned output-clear/compute chain; the compiler remains responsible for rejecting an undeclared resource
// sharing contract or lowering the required exclusive ownership transfer.
TEST(EcsGraphics, SurfelGiPermitsOptInCrossFamilyComputeRouting){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString taskGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "task_graph.cpp", taskGraphSource));
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());

    const usize surfelGiOffset = taskGraph.find("bool RendererSystem::declareDeferredSurfelGiTask");
    const usize readbackOffset = taskGraph.find("void RendererSystem::declareDeferredSurfelCountReadbackTask", surfelGiOffset);
    ASSERT_NE(surfelGiOffset, AStringView::npos);
    ASSERT_NE(readbackOffset, AStringView::npos);
    ASSERT_LT(surfelGiOffset, readbackOffset);
    const AStringView surfelGi = taskGraph.substr(surfelGiOffset, readbackOffset - surfelGiOffset);

    EXPECT_TRUE(ContainsText(surfelGi, "EnableSameFamilyComputeEffectRouting(surfelIrradianceClearScheduling, false)"));
    EXPECT_TRUE(ContainsText(surfelGi, "EnableCrossFamilyComputeEffectRouting(surfelIrradianceClearScheduling)"));
    EXPECT_TRUE(ContainsText(surfelGi, "EnableSameFamilyComputeEffectRouting(surfelGiScheduling)"));
    EXPECT_TRUE(ContainsText(surfelGi, "EnableCrossFamilyComputeEffectRouting(surfelGiScheduling)"));
}


// The full irradiance clear is deliberately renderer-local: the generic helper conservatively declares Graphics
// for render-pass lowering, while this native clear is constrained to the direct Compute GI packet and captures the
// same typed command-IR record after the graph-owned CopyDest transition.
TEST(EcsGraphics, SurfelIrradianceClearUsesComputeGraphCallback){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString taskGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "task_graph.cpp", taskGraphSource));
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());

    const usize callbackOffset = taskGraph.find("struct SurfelIrradianceClearGraphTask");
    const usize shadowPrepareOffset = taskGraph.find("struct ShadowPrepareGraphTask", callbackOffset);
    const usize surfelGiOffset = taskGraph.find("bool RendererSystem::declareDeferredSurfelGiTask", shadowPrepareOffset);
    const usize readbackOffset = taskGraph.find("void RendererSystem::declareDeferredSurfelCountReadbackTask", surfelGiOffset);
    ASSERT_NE(callbackOffset, AStringView::npos);
    ASSERT_NE(shadowPrepareOffset, AStringView::npos);
    ASSERT_NE(surfelGiOffset, AStringView::npos);
    ASSERT_NE(readbackOffset, AStringView::npos);
    ASSERT_LT(callbackOffset, shadowPrepareOffset);
    ASSERT_LT(shadowPrepareOffset, surfelGiOffset);
    ASSERT_LT(surfelGiOffset, readbackOffset);
    const AStringView callback = taskGraph.substr(callbackOffset, shadowPrepareOffset - callbackOffset);
    const AStringView surfelGi = taskGraph.substr(surfelGiOffset, readbackOffset - surfelGiOffset);

    EXPECT_TRUE(ContainsText(callback, "context.taskGraph.textureForResource(payload.destination)"));
    EXPECT_TRUE(ContainsText(callback, "if(!destination || commandList.isRenderPassActive())"));
    EXPECT_FALSE(ContainsText(callback, "endRenderPass()"));
    EXPECT_TRUE(ContainsText(callback, "Core::GpuClearTextureTaskDesc clearDesc{"));
    EXPECT_TRUE(ContainsText(callback, ".destination = payload.destination,"));
    EXPECT_TRUE(ContainsText(callback, ".subresources = s_FramebufferSubresources,"));
    EXPECT_TRUE(ContainsText(callback, ".valueType = Core::GpuClearTextureTaskValueType::Float,"));
    EXPECT_TRUE(ContainsText(callback, ".floatValue = Core::Color(0.f, 0.f, 0.f, 0.f),"));
    EXPECT_TRUE(ContainsText(callback, "context.commandIrCapture"));
    EXPECT_TRUE(ContainsText(callback, "captureClearTexture("));
    EXPECT_TRUE(ContainsText(callback, "commandList.clearTextureFloat(destination, clearDesc.subresources, clearDesc.floatValue);"));

    const usize resourceUseOffset = surfelGi.find("const Core::GpuTaskResourceUse surfelIrradianceClearResourceUse");
    const usize giSchedulingOffset = surfelGi.find("Core::GpuTaskSchedulingHint surfelGiScheduling", resourceUseOffset);
    ASSERT_NE(resourceUseOffset, AStringView::npos);
    ASSERT_NE(giSchedulingOffset, AStringView::npos);
    ASSERT_LT(resourceUseOffset, giSchedulingOffset);
    const AStringView irradianceClear = surfelGi.substr(resourceUseOffset, giSchedulingOffset - resourceUseOffset);

    EXPECT_TRUE(ContainsText(irradianceClear, "WriteTextureUse(\n        surfelIrradiance,\n        ECSRenderDetail::s_FramebufferSubresources,\n        Core::ResourceStates::CopyDest\n    )"));
    EXPECT_TRUE(ContainsText(irradianceClear, ".setQueue(ComputePacketQueueRequest())"));
    EXPECT_TRUE(ContainsText(irradianceClear, ".setResourceUses(&surfelIrradianceClearResourceUse, 1u)"));
    EXPECT_TRUE(ContainsText(irradianceClear, "addTask<ECSRenderDetail::SurfelIrradianceClearGraphTask>("));
    EXPECT_FALSE(ContainsText(irradianceClear, "addClearTextureTask("));
}


// Hardware Caustics is a separate Graphics-capable effect chain. Its clear and every independently created
// temporal-accumulator prefix must carry the explicit cross-family opt-in so copied photon/resolve schedules
// retain one selected physical Graphics queue without making a windowed present eligible for that queue.
TEST(EcsGraphics, HardwareCausticsPermitsOptInCrossFamilyGraphicsRouting){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString taskGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "task_graph.cpp", taskGraphSource));
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());

    const usize lightingOffset = taskGraph.find("void RendererSystem::buildDeferredLightingTaskGraph");
    ASSERT_NE(lightingOffset, AStringView::npos);
    const AStringView lighting = taskGraph.substr(lightingOffset);

    EXPECT_TRUE(ContainsText(lighting, "EnableCrossFamilyComputeEffectRouting(hardwareScheduling)"));
    EXPECT_TRUE(ContainsText(lighting, "EnableCrossFamilyComputeEffectRouting(irradianceClearScheduling)"));
    EXPECT_TRUE(ContainsText(lighting, "EnableCrossFamilyComputeEffectRouting(accumulatorBootstrapClearScheduling)"));
    EXPECT_TRUE(ContainsText(lighting, "EnableCrossFamilyComputeEffectRouting(accumulatorDecayScheduling)"));
}


// Caustic resolve targets start Unknown after recreation. Geometry downsample and prepare must therefore publish
// their first results as writes, while a warm hardware accumulator imports only accepted Graphics packet state.
TEST(EcsGraphics, CausticGraphScratchUsesFirstWritesAndHardwareRetainsAcceptedAccumulatorState){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString taskGraphSource;
    AString systemSource;
    AString systemHeaderSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "task_graph.cpp", taskGraphSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "system.cpp", systemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "system.h", systemHeaderSource));
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());
    const AStringView system(systemSource.data(), systemSource.size());
    const AStringView systemHeader(systemHeaderSource.data(), systemHeaderSource.size());

    const usize softwareCausticsOffset = taskGraph.find("bool RendererSystem::declareDeferredSoftwareCausticsTask");
    const usize surfelGiOffset = taskGraph.find("bool RendererSystem::declareDeferredSurfelGiTask", softwareCausticsOffset);
    const usize deferredLightingOffset = taskGraph.find("void RendererSystem::buildDeferredLightingTaskGraph", surfelGiOffset);
    const usize hardwareCausticsOffset = taskGraph.find("if(declaresHardwareCaustics){", deferredLightingOffset);
    const usize avboitOffset = taskGraph.find("AvboitPreGraphTask::Payload", hardwareCausticsOffset);
    ASSERT_NE(softwareCausticsOffset, AStringView::npos);
    ASSERT_NE(surfelGiOffset, AStringView::npos);
    ASSERT_NE(deferredLightingOffset, AStringView::npos);
    ASSERT_NE(hardwareCausticsOffset, AStringView::npos);
    ASSERT_NE(avboitOffset, AStringView::npos);
    ASSERT_LT(softwareCausticsOffset, surfelGiOffset);
    ASSERT_LT(surfelGiOffset, deferredLightingOffset);
    ASSERT_LT(deferredLightingOffset, hardwareCausticsOffset);
    ASSERT_LT(hardwareCausticsOffset, avboitOffset);
    const AStringView softwareCaustics = taskGraph.substr(softwareCausticsOffset, surfelGiOffset - softwareCausticsOffset);
    const AStringView hardwareCaustics = taskGraph.substr(hardwareCausticsOffset, avboitOffset - hardwareCausticsOffset);

    const usize softwareGeometryOffset = softwareCaustics.find("geometryResourceUses.push_back(ReadTextureUse(");
    const usize softwarePrepareOffset = softwareCaustics.find("constexpr bool s_CausticResolvePrepareWritesHalf", softwareGeometryOffset);
    const usize softwareUpsampleOffset = softwareCaustics.find("resolveUpsampleResourceUses.push_back(", softwarePrepareOffset);
    const usize hardwareGeometryOffset = hardwareCaustics.find("hardwareGeometryResourceUses.push_back(ReadTextureUse(");
    const usize hardwarePrepareOffset = hardwareCaustics.find("constexpr bool s_HardwareCausticResolvePrepareWritesHalf", hardwareGeometryOffset);
    const usize hardwareUpsampleOffset = hardwareCaustics.find("hardwareResolveUpsampleResourceUses.push_back(", hardwarePrepareOffset);
    ASSERT_NE(softwareGeometryOffset, AStringView::npos);
    ASSERT_NE(softwarePrepareOffset, AStringView::npos);
    ASSERT_NE(softwareUpsampleOffset, AStringView::npos);
    ASSERT_NE(hardwareGeometryOffset, AStringView::npos);
    ASSERT_NE(hardwarePrepareOffset, AStringView::npos);
    ASSERT_NE(hardwareUpsampleOffset, AStringView::npos);
    ASSERT_LT(softwareGeometryOffset, softwarePrepareOffset);
    ASSERT_LT(softwarePrepareOffset, softwareUpsampleOffset);
    ASSERT_LT(hardwareGeometryOffset, hardwarePrepareOffset);
    ASSERT_LT(hardwarePrepareOffset, hardwareUpsampleOffset);
    const AStringView softwareGeometry = softwareCaustics.substr(
        softwareGeometryOffset,
        softwarePrepareOffset - softwareGeometryOffset
    );
    const AStringView softwarePrepare = softwareCaustics.substr(
        softwarePrepareOffset,
        softwareUpsampleOffset - softwarePrepareOffset
    );
    const AStringView hardwareGeometry = hardwareCaustics.substr(
        hardwareGeometryOffset,
        hardwarePrepareOffset - hardwareGeometryOffset
    );
    const AStringView hardwarePrepare = hardwareCaustics.substr(
        hardwarePrepareOffset,
        hardwareUpsampleOffset - hardwarePrepareOffset
    );

    EXPECT_TRUE(ContainsText(softwareGeometry, "geometryResourceUses.push_back(WriteTextureUse(\n        causticResolveGeometry,"));
    EXPECT_FALSE(ContainsText(softwareGeometry, "geometryResourceUses.push_back(ReadWriteTextureUse("));
    EXPECT_EQ(CountText(softwarePrepare, "resolvePrepareResourceUses.push_back(ReadTextureUse("), 2u);
    EXPECT_FALSE(ContainsText(softwarePrepare, "resolvePrepareResourceUses.push_back(ReadWriteTextureUse("));
    EXPECT_EQ(CountText(softwarePrepare, "resolvePrepareResourceUses.push_back(WriteTextureUse("), 2u);
    EXPECT_TRUE(ContainsText(hardwareGeometry, "hardwareGeometryResourceUses.push_back(WriteTextureUse(\n            causticResolveGeometry,"));
    EXPECT_FALSE(ContainsText(hardwareGeometry, "hardwareGeometryResourceUses.push_back(ReadWriteTextureUse("));
    EXPECT_EQ(CountText(hardwarePrepare, "hardwareResolvePrepareResourceUses.push_back(ReadTextureUse("), 2u);
    EXPECT_FALSE(ContainsText(hardwarePrepare, "hardwareResolvePrepareResourceUses.push_back(ReadWriteTextureUse("));
    EXPECT_EQ(CountText(hardwarePrepare, "hardwareResolvePrepareResourceUses.push_back(WriteTextureUse("), 2u);

    const usize hardwareStateSourcesOffset = system.find("Core::GpuExternalPacketStateSource hardwareCausticsStateSources[");
    const usize deferredCompositeStateSourcesOffset = system.find(
        "Core::GpuExternalPacketStateSource deferredCompositeStateSources[",
        hardwareStateSourcesOffset
    );
    const usize hardwareAcceptanceOffset = system.find("struct HardwareCausticsAcceptanceContext{");
    const usize hardwareFailureOffset = system.find("if(!hardwareCausticsStateReady){", hardwareAcceptanceOffset);
    ASSERT_NE(hardwareStateSourcesOffset, AStringView::npos);
    ASSERT_NE(deferredCompositeStateSourcesOffset, AStringView::npos);
    ASSERT_NE(hardwareAcceptanceOffset, AStringView::npos);
    ASSERT_NE(hardwareFailureOffset, AStringView::npos);
    ASSERT_LT(hardwareStateSourcesOffset, deferredCompositeStateSourcesOffset);
    ASSERT_LT(hardwareAcceptanceOffset, hardwareFailureOffset);
    const AStringView hardwareStateSources = system.substr(
        hardwareStateSourcesOffset,
        deferredCompositeStateSourcesOffset - hardwareStateSourcesOffset
    );
    const AStringView hardwareAcceptance = system.substr(
        hardwareAcceptanceOffset,
        hardwareFailureOffset - hardwareAcceptanceOffset
    );

    EXPECT_TRUE(ContainsText(system, "s_HardwareCausticsStateSourceCapacity = 2u;"));
    EXPECT_TRUE(ContainsText(system, "m_hardwareCausticAccumulatorPersistentState(arena)"));
    EXPECT_TRUE(ContainsText(system, "m_hardwareCausticAccumulatorPersistentState.reset();"));
    EXPECT_EQ(CountText(system, "m_hardwareCausticAccumulatorPersistentState.reset();"), 1u);
    EXPECT_TRUE(ContainsText(systemHeader, "Core::GpuPersistentResourceStateCache m_hardwareCausticAccumulatorPersistentState;"));
    EXPECT_TRUE(ContainsText(hardwareStateSources, "__hidden_renderer_system::s_HardwareCausticsStateSourceCapacity"));
    EXPECT_TRUE(ContainsText(hardwareStateSources, "m_hardwareCausticAccumulatorPersistentState.valid()"));
    EXPECT_TRUE(ContainsText(hardwareStateSources, "m_hardwareCausticAccumulatorPersistentState.source()"));
    EXPECT_TRUE(ContainsText(hardwareAcceptance, "m_hardwareCausticAccumulatorPersistentState.replaceTextureSubset("));
    EXPECT_TRUE(ContainsText(hardwareAcceptance, "context->targets->causticAccumulator"));
    EXPECT_TRUE(ContainsText(hardwareAcceptance, "const Core::GpuTaskGraphTaskAcceptedCallback hardwareCausticsAcceptedCallback{"));
    EXPECT_TRUE(ContainsText(hardwareAcceptance, "if(context->stateReady && context->usesLaggedHistory){"));
    EXPECT_TRUE(ContainsText(system, "a rejected record must preserve its prior warm-decay source."));
    EXPECT_TRUE(ContainsText(system, "if(!hardwareCausticsWasAccepted)\n                    restoreCausticsCpuState();"));
}


// FrontierSafe normally closes a packet at a cross-queue consumer. These direct serial effect chains instead own
// one timing/acceptance packet, so every accumulator alternative and semantic tail must opt in explicitly.
TEST(EcsGraphics, FrontierSafeEffectChainsRetainTheirSemanticPackets){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString taskGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "task_graph.cpp", taskGraphSource));
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());

    const usize softwareCausticsOffset = taskGraph.find("bool RendererSystem::declareDeferredSoftwareCausticsTask");
    const usize surfelGiOffset = taskGraph.find("bool RendererSystem::declareDeferredSurfelGiTask", softwareCausticsOffset);
    const usize surfelReadbackOffset = taskGraph.find("void RendererSystem::declareDeferredSurfelCountReadbackTask", surfelGiOffset);
    const usize deferredLightingOffset = taskGraph.find("void RendererSystem::buildDeferredLightingTaskGraph", surfelReadbackOffset);
    const usize hardwareCausticsOffset = taskGraph.find("if(declaresHardwareCaustics){", deferredLightingOffset);
    const usize avboitOffset = taskGraph.find("AvboitPreGraphTask::Payload", hardwareCausticsOffset);
    ASSERT_NE(softwareCausticsOffset, AStringView::npos);
    ASSERT_NE(surfelGiOffset, AStringView::npos);
    ASSERT_NE(surfelReadbackOffset, AStringView::npos);
    ASSERT_NE(deferredLightingOffset, AStringView::npos);
    ASSERT_NE(hardwareCausticsOffset, AStringView::npos);
    ASSERT_NE(avboitOffset, AStringView::npos);
    ASSERT_LT(softwareCausticsOffset, surfelGiOffset);
    ASSERT_LT(surfelGiOffset, surfelReadbackOffset);
    ASSERT_LT(surfelReadbackOffset, deferredLightingOffset);
    ASSERT_LT(deferredLightingOffset, hardwareCausticsOffset);
    ASSERT_LT(hardwareCausticsOffset, avboitOffset);
    const AStringView softwareCaustics = taskGraph.substr(softwareCausticsOffset, surfelGiOffset - softwareCausticsOffset);
    const AStringView surfelGi = taskGraph.substr(surfelGiOffset, surfelReadbackOffset - surfelGiOffset);
    const AStringView hardwareCaustics = taskGraph.substr(hardwareCausticsOffset, avboitOffset - hardwareCausticsOffset);
    const AStringView avboit = taskGraph.substr(avboitOffset);

    EXPECT_TRUE(ContainsText(softwareCaustics, "accumulatorNonTemporalClearScheduling.allowMergeAcrossConsumerFrontier = true;"));
    EXPECT_TRUE(ContainsText(softwareCaustics, "accumulatorBootstrapClearScheduling.allowMergeAcrossConsumerFrontier = true;"));
    EXPECT_TRUE(ContainsText(softwareCaustics, "accumulatorDecayScheduling.allowMergeAcrossConsumerFrontier = true;"));
    EXPECT_TRUE(ContainsText(softwareCaustics, "causticsScheduling.allowMergeAcrossConsumerFrontier = true;"));
    EXPECT_TRUE(ContainsText(softwareCaustics, ".setDependencies(&causticsDependency, 1u)"));
    EXPECT_TRUE(ContainsText(softwareCaustics, "render.software_caustics.resolve_timing_close"));

    EXPECT_TRUE(ContainsText(surfelGi, "surfelGiScheduling.allowMergeAcrossConsumerFrontier = true;"));
    EXPECT_TRUE(ContainsText(surfelGi, ".setDependencies(&surfelGiDependency, 1u)"));
    EXPECT_TRUE(ContainsText(surfelGi, "render.surfel_gi"));

    EXPECT_TRUE(ContainsText(hardwareCaustics, "accumulatorNonTemporalClearScheduling.allowMergeAcrossConsumerFrontier = true;"));
    EXPECT_TRUE(ContainsText(hardwareCaustics, "accumulatorBootstrapClearScheduling.allowMergeAcrossConsumerFrontier = true;"));
    EXPECT_TRUE(ContainsText(hardwareCaustics, "accumulatorDecayScheduling.allowMergeAcrossConsumerFrontier = true;"));
    EXPECT_TRUE(ContainsText(hardwareCaustics, "hardwareCausticsScheduling.allowMergeAcrossConsumerFrontier = true;"));
    EXPECT_TRUE(ContainsText(hardwareCaustics, ".setDependencies(&causticsDependency, 1u)"));
    EXPECT_TRUE(ContainsText(hardwareCaustics, "render.hardware_caustics.resolve_timing_close"));

    EXPECT_TRUE(ContainsText(avboit, "avboitClearScheduling.allowMergeAcrossConsumerFrontier = true;"));
    EXPECT_TRUE(ContainsText(avboit, "avboitOccupancyScheduling.allowMergeAcrossConsumerFrontier = true;"));
    EXPECT_TRUE(ContainsText(avboit, "accumulationFinalizeScheduling.allowMergeAcrossConsumerFrontier = true;"));
    EXPECT_TRUE(ContainsText(avboit, ".setDependencies(&occupancyDependency, 1u)"));
    EXPECT_TRUE(ContainsText(avboit, ".setDependencies(&m_deferredAvboitAccumulationTask, 1u)"));
}


// Shadow Visibility has both a fully split soft-transparent route and a retained monolithic compatibility route.
// Each graph-owned chain may choose an alternate Compute family, while its direct successors retain that physical
// queue and the explicit primary-Graphics presentation guard remains outside this effect.
TEST(EcsGraphics, ShadowVisibilityPermitsOptInCrossFamilyComputeRouting){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString taskGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "task_graph.cpp", taskGraphSource));
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());

    const usize shadowOffset = taskGraph.find("bool RendererSystem::declareDeferredShadowVisibilityTask");
    const usize softwareCausticsOffset = taskGraph.find("bool RendererSystem::declareDeferredSoftwareCausticsTask", shadowOffset);
    ASSERT_NE(shadowOffset, AStringView::npos);
    ASSERT_NE(softwareCausticsOffset, AStringView::npos);
    ASSERT_LT(shadowOffset, softwareCausticsOffset);
    const AStringView shadowVisibility = taskGraph.substr(shadowOffset, softwareCausticsOffset - shadowOffset);

    EXPECT_TRUE(ContainsText(shadowVisibility, "EnableCrossFamilyComputeEffectRouting(opaqueScheduling)"));
    EXPECT_TRUE(ContainsText(shadowVisibility, "EnableCrossFamilyComputeEffectRouting(tailScheduling)"));
    EXPECT_TRUE(ContainsText(shadowVisibility, "EnableCrossFamilyComputeEffectRouting(primitiveScheduling)"));
    EXPECT_TRUE(ContainsText(shadowVisibility, "EnableCrossFamilyComputeEffectRouting(allLitClearScheduling)"));
    EXPECT_TRUE(ContainsText(shadowVisibility, "EnableCrossFamilyComputeEffectRouting(scheduling)"));
    EXPECT_TRUE(ContainsText(shadowVisibility, "EnableCrossFamilyComputeEffectRouting(statsReadbackScheduling)"));
}


// Split shadow callbacks must declare only resources their native body touches. Fresh retained scratch stays
// Unknown until a graph writer publishes it, while an accepted temporal history remains a real sampled input.
TEST(EcsGraphics, SplitShadowVisibilityKeepsFreshScratchAsFirstWrites){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString taskGraphSource;
    AString shadowSource;
    AString softShadowSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "task_graph.cpp", taskGraphSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_shadow.cpp", shadowSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_softshadow.cpp", softShadowSource));
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());
    const AStringView shadowSourceView(shadowSource.data(), shadowSource.size());
    const AStringView softShadowSourceView(softShadowSource.data(), softShadowSource.size());

    const usize shadowOffset = taskGraph.find("bool RendererSystem::declareDeferredShadowVisibilityTask");
    const usize softwareCausticsOffset = taskGraph.find("bool RendererSystem::declareDeferredSoftwareCausticsTask", shadowOffset);
    ASSERT_NE(shadowOffset, AStringView::npos);
    ASSERT_NE(softwareCausticsOffset, AStringView::npos);
    ASSERT_LT(shadowOffset, softwareCausticsOffset);
    const AStringView shadowVisibility = taskGraph.substr(shadowOffset, softwareCausticsOffset - shadowOffset);

    const usize opaqueResourcesOffset = shadowVisibility.find("opaqueResourceUses.reserve(");
    const usize opaqueFirstWaveletOffset = shadowVisibility.find("opaqueFirstWaveletResourceUses.reserve(", opaqueResourcesOffset);
    const usize opaqueDescOffset = shadowVisibility.find("Core::GpuTaskDesc opaqueDesc;", opaqueFirstWaveletOffset);
    const usize traceUsesOffset = shadowVisibility.find("transparentTraceResourceUses.reserve(", opaqueFirstWaveletOffset);
    const usize transparentHistoryOffset = shadowVisibility.find("graphOwnsTransparentTemporalMergeEntryStates =", traceUsesOffset);
    ASSERT_NE(opaqueResourcesOffset, AStringView::npos);
    ASSERT_NE(opaqueFirstWaveletOffset, AStringView::npos);
    ASSERT_NE(opaqueDescOffset, AStringView::npos);
    ASSERT_NE(traceUsesOffset, AStringView::npos);
    ASSERT_NE(transparentHistoryOffset, AStringView::npos);
    ASSERT_LT(opaqueResourcesOffset, opaqueFirstWaveletOffset);
    ASSERT_LT(opaqueFirstWaveletOffset, opaqueDescOffset);
    ASSERT_LT(traceUsesOffset, transparentHistoryOffset);
    const AStringView opaqueResources = shadowVisibility.substr(
        opaqueResourcesOffset,
        opaqueFirstWaveletOffset - opaqueResourcesOffset
    );
    const AStringView opaqueDesc = shadowVisibility.substr(opaqueDescOffset, traceUsesOffset - opaqueDescOffset);
    const AStringView transparentTraceUses = shadowVisibility.substr(
        traceUsesOffset,
        transparentHistoryOffset - traceUsesOffset
    );

    EXPECT_TRUE(ContainsText(opaqueResources, "WriteUse(shadowVisibility, Core::ResourceStates::UnorderedAccess)"));
    EXPECT_TRUE(ContainsText(opaqueResources, "WriteUse(shadowSoftHalfA, Core::ResourceStates::UnorderedAccess)"));
    EXPECT_TRUE(ContainsText(opaqueResources, "WriteUse(shadowSoftGeometry, Core::ResourceStates::UnorderedAccess)"));
    EXPECT_TRUE(ContainsText(opaqueResources, "if(hardwareShadowSupported){"));
    EXPECT_TRUE(ContainsText(opaqueResources, "ReadUse(sceneTlas, Core::ResourceStates::AccelStructRead)"));
    EXPECT_TRUE(ContainsText(opaqueResources, "}else{\n            opaqueResourceUses.push_back(ReadUse(sceneBvhNodes"));
    EXPECT_FALSE(ContainsText(opaqueResources, "shadowCoarseTransmittance"));
    EXPECT_FALSE(ContainsText(opaqueResources, "shadowSoftHalfB"));
    EXPECT_FALSE(ContainsText(opaqueResources, "shadowHistA"));
    EXPECT_FALSE(ContainsText(opaqueResources, "transparentSoftHalf"));
    EXPECT_TRUE(ContainsText(opaqueDesc, ".setResourceUses(opaqueResourceUses.data(), opaqueResourceUses.size())"));
    EXPECT_TRUE(ContainsText(opaqueDesc, "!hardwareShadowSupported && softwareTraceGeometryStatesGraphOwned"));

    EXPECT_TRUE(ContainsText(transparentTraceUses, "WriteUse(transparentSoftHalf, Core::ResourceStates::UnorderedAccess)"));
    EXPECT_FALSE(ContainsText(transparentTraceUses, "ReadWriteUse(shadowVisibility"));
    EXPECT_FALSE(ContainsText(transparentTraceUses, "shadowCoarseTransmittance"));
    EXPECT_FALSE(ContainsText(transparentTraceUses, "ReadWriteUse(shadowSoftHalfA"));
    EXPECT_FALSE(ContainsText(transparentTraceUses, "ReadWriteUse(shadowSoftHalfB"));

    EXPECT_TRUE(ContainsText(shadowVisibility, "const bool softShadowHistoryReadable ="));
    EXPECT_TRUE(ContainsText(shadowVisibility, "m_softShadowTemporalReady"));
    EXPECT_TRUE(ContainsText(shadowVisibility, "m_prevWorldToClipValid"));
    EXPECT_TRUE(ContainsText(shadowVisibility, "m_softShadowTemporalSeeded"));
    EXPECT_TRUE(ContainsText(shadowVisibility, "if(softShadowHistoryReadable){\n                opaqueFirstWaveletResourceUses.push_back(ReadUse(shadowSoftGeometryPrevious"));
    EXPECT_TRUE(ContainsText(shadowVisibility, "if(softShadowHistoryReadable){\n                transparentTemporalMergeResourceUses.push_back(ReadUse(shadowSoftGeometryPrevious"));
    EXPECT_TRUE(ContainsText(shadowVisibility, "WriteUse(transparentHistoryOut, Core::ResourceStates::UnorderedAccess)"));
    EXPECT_TRUE(ContainsText(shadowVisibility, "splitSoftTransparentFold\n            || appendOptionalWriteTexture("));

    const usize softwareVisibilityOffset = shadowSourceView.find("bool RendererRayTracingSystem::renderGpuBvhShadowVisibility(");
    const usize softwareOpaqueOffset = shadowSourceView.find("bool RendererRayTracingSystem::renderGpuBvhShadowVisibilityOpaque(", softwareVisibilityOffset);
    ASSERT_NE(softwareVisibilityOffset, AStringView::npos);
    ASSERT_NE(softwareOpaqueOffset, AStringView::npos);
    ASSERT_LT(softwareVisibilityOffset, softwareOpaqueOffset);
    const AStringView softwareVisibility = shadowSourceView.substr(softwareVisibilityOffset, softwareOpaqueOffset - softwareVisibilityOffset);
    const usize adaptiveOffset = softwareVisibility.find("if(!softTransparentRan && rayTracingState().m_swShadowAdaptiveEnabled)");
    ASSERT_NE(adaptiveOffset, AStringView::npos);
    const AStringView preAdaptive = softwareVisibility.substr(0u, adaptiveOffset);
    const AStringView adaptive = softwareVisibility.substr(adaptiveOffset);
    EXPECT_FALSE(ContainsText(preAdaptive, "targets.shadowCoarseTransmittance.get()"));
    EXPECT_TRUE(ContainsText(adaptive, "commandList.setEnableUavBarriersForTexture(targets.shadowCoarseTransmittance.get(), true)"));
    EXPECT_TRUE(ContainsText(adaptive, "commandList.setTextureState(targets.shadowCoarseTransmittance.get(), ECSRenderDetail::s_ShadowVisibilitySubresources, Core::ResourceStates::UnorderedAccess)"));

    const usize softDispatchOffset = softShadowSourceView.find("void RendererRayTracingSystem::dispatchSoftShadowDenoiseAndTransparentFold(");
    ASSERT_NE(softDispatchOffset, AStringView::npos);
    const AStringView softDispatch = softShadowSourceView.substr(softDispatchOffset);
    EXPECT_TRUE(ContainsText(softDispatch, "const bool temporalHistoryReadable = softShadowTemporalHistoryUsable();"));
    EXPECT_TRUE(ContainsText(softDispatch, "if(temporalHistoryReadable){\n                commandList.setTextureState(resources.historyIn"));
    EXPECT_TRUE(ContainsText(softDispatch, "if(temporalHistoryReadable)\n                commandList.setTextureState(targets.shadowSoftGeometryPrev"));
}


// AVBOIT's Graphics raster packets deliberately retain their established primary route, but its split Depth Warp
// and Integration tasks are pure Compute packets with complete graph-declared handoffs. Keep their auxiliary
// routing opt-in explicit rather than inferring it from the broader AVBOIT effect name.
TEST(EcsGraphics, SplitAvboitComputePacketsPermitCrossFamilyRouting){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString taskGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "task_graph.cpp", taskGraphSource));
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());

    const usize schedulingOffset = taskGraph.find("Core::GpuTaskSchedulingHint avboitComputeScheduling");
    const usize accumulationOffset = taskGraph.find("AvboitAccumulationGraphTask::Payload", schedulingOffset);
    ASSERT_NE(schedulingOffset, AStringView::npos);
    ASSERT_NE(accumulationOffset, AStringView::npos);
    ASSERT_LT(schedulingOffset, accumulationOffset);
    const AStringView splitCompute = taskGraph.substr(schedulingOffset, accumulationOffset - schedulingOffset);

    EXPECT_TRUE(ContainsText(splitCompute, "EnableSameFamilyComputeEffectRouting(avboitComputeScheduling, false)"));
    EXPECT_TRUE(ContainsText(splitCompute, "EnableCrossFamilyComputeEffectRouting(avboitComputeScheduling)"));
    EXPECT_TRUE(ContainsText(splitCompute, ".setQueue(ComputeQueueRequest())"));
    EXPECT_TRUE(ContainsText(splitCompute, "render.avboit.depth_warp"));
    EXPECT_TRUE(ContainsText(splitCompute, "render.avboit.integration"));
}


// Queue timing feedback is deliberately opt-in, but the two graph-owned AVBOIT Compute tasks must route accepted
// timestamp samples back into the next immutable compiler snapshot. Keep this source-level contract focused on the
// renderer boundary rather than coupling it to one physical queue topology.
TEST(EcsGraphics, DeferredGraphWiresAcceptedTaskTimingFeedback){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemHeaderSource;
    AString timingFeedbackHeaderSource;
    AString taskGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "system.h", systemHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "task_timing_feedback.h", timingFeedbackHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "task_graph.cpp", taskGraphSource));
    const AStringView systemHeader(systemHeaderSource.data(), systemHeaderSource.size());
    const AStringView timingFeedbackHeader(timingFeedbackHeaderSource.data(), timingFeedbackHeaderSource.size());
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());

    EXPECT_TRUE(ContainsText(timingFeedbackHeader, "class RendererTaskTimingFeedback final"));
    EXPECT_TRUE(ContainsText(systemHeader, "RendererTaskTimingFeedback m_deferredTaskTimingFeedback"));

    const usize lightingOffset = taskGraph.find("void RendererSystem::buildDeferredLightingTaskGraph");
    const usize compilerOffset = taskGraph.find("if(!compiler.compile(", lightingOffset);
    const usize feedbackOffset = taskGraph.find("m_deferredTaskTimingFeedback.configureCompileOptions(", lightingOffset);
    ASSERT_NE(lightingOffset, AStringView::npos);
    ASSERT_NE(compilerOffset, AStringView::npos);
    ASSERT_NE(feedbackOffset, AStringView::npos);
    EXPECT_LT(feedbackOffset, compilerOffset);

    const usize depthWarpOffset = taskGraph.find("struct AvboitDepthWarpGraphTask");
    const usize extinctionComputeEmulationOffset = taskGraph.find(
        "struct AvboitExtinctionComputeEmulationGraphTask",
        depthWarpOffset
    );
    const usize integrationOffset = taskGraph.find("struct AvboitIntegrationGraphTask", extinctionComputeEmulationOffset);
    const usize accumulationOffset = taskGraph.find("struct AvboitAccumulationGraphTask", integrationOffset);
    ASSERT_NE(depthWarpOffset, AStringView::npos);
    ASSERT_NE(extinctionComputeEmulationOffset, AStringView::npos);
    ASSERT_NE(integrationOffset, AStringView::npos);
    ASSERT_NE(accumulationOffset, AStringView::npos);
    ASSERT_LT(depthWarpOffset, extinctionComputeEmulationOffset);
    ASSERT_LT(extinctionComputeEmulationOffset, integrationOffset);
    ASSERT_LT(integrationOffset, accumulationOffset);
    const AStringView depthWarp = taskGraph.substr(depthWarpOffset, extinctionComputeEmulationOffset - depthWarpOffset);
    const AStringView integration = taskGraph.substr(integrationOffset, accumulationOffset - integrationOffset);

    for(const AStringView task : { depthWarp, integration }){
        EXPECT_TRUE(ContainsText(task, ".timingFeedback"));
        EXPECT_TRUE(ContainsText(task, ".timingScope"));
        EXPECT_TRUE(ContainsText(task, "beginSample("));
        EXPECT_TRUE(ContainsText(task, "static void accepted("));
        EXPECT_TRUE(ContainsText(task, "acceptSubmission("));
        EXPECT_TRUE(ContainsText(task, "static void discarded("));
        EXPECT_TRUE(ContainsText(task, "discardRecording("));
    }

    const AStringView lighting = taskGraph.substr(lightingOffset);
    EXPECT_TRUE(ContainsText(lighting, "allowTimingFeedbackRouting = true"));
    EXPECT_TRUE(ContainsText(lighting, ".timingFeedback = &m_deferredTaskTimingFeedback"));
    EXPECT_TRUE(ContainsText(lighting, ".timingFeedback = splitAvboitStages ? &m_deferredTaskTimingFeedback : nullptr"));
    EXPECT_TRUE(ContainsText(lighting, ".setTimingMetadata(avboitIntegrationTiming)"));
}


// Scene-light and shading constants are prepared before graph declaration and published from accepted graph work.
// The previous direct compatibility writer had no callers, so keep it retired instead of letting an unreachable
// native state/submit bridge silently return to the deferred subsystem.
TEST(EcsGraphics, DeferredSceneShadingUploadsHaveNoNativeCompatibilityDispatcher){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString deferredHeaderSource;
    AString deferredLightingSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "deferred" / "deferred_system.h", deferredHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "deferred" / "deferred_lighting.cpp", deferredLightingSource));
    const AStringView deferredHeader(deferredHeaderSource.data(), deferredHeaderSource.size());
    const AStringView deferredLighting(deferredLightingSource.data(), deferredLightingSource.size());

    EXPECT_TRUE(ContainsText(deferredHeader, "prepareSceneShadingBufferUploads"));
    EXPECT_TRUE(ContainsText(deferredHeader, "confirmSceneShadingBufferUploads"));
    EXPECT_FALSE(ContainsText(deferredHeader, "updateSceneShadingBuffer"));
    EXPECT_FALSE(ContainsText(deferredLighting, "updateSceneShadingBuffer"));
    EXPECT_FALSE(ContainsText(deferredLighting, "commandList.writeBuffer("));
}


// AVBOIT's normal path rejects an uncaptured transparent phase before recording. Its last aggregate native
// dispatcher therefore had no caller and only kept mutable mesh/material/CSG writes reachable in dead code.
// Keep that bridge retired so every supported transparent phase starts from its declaration-time graph snapshot.
TEST(EcsGraphics, AvboitMaterialUploadsHaveNoNativeCompatibilityDispatcher){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString avboitHeaderSource;
    AString avboitSource;
    AString materialHeaderSource;
    AString materialPassSource;
    AString materialResourcesSource;
    AString meshHeaderSource;
    AString meshViewSource;
    AString csgHeaderSource;
    AString csgResourcesSource;
    AString csgIntervalSource;
    AString taskGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "avboit" / "avboit_system.h", avboitHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "avboit" / "avboit_pass.cpp", avboitSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_system.h", materialHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_pass.cpp", materialPassSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_pass_resources.cpp", materialResourcesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "mesh" / "mesh_system.h", meshHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "mesh" / "mesh_view.cpp", meshViewSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "csg" / "csg_system.h", csgHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "csg" / "csg_resources.cpp", csgResourcesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "csg" / "csg_interval_peel.cpp", csgIntervalSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "task_graph.cpp", taskGraphSource));

    const AStringView avboitHeader(avboitHeaderSource.data(), avboitHeaderSource.size());
    const AStringView avboit(avboitSource.data(), avboitSource.size());
    const AStringView materialHeader(materialHeaderSource.data(), materialHeaderSource.size());
    const AStringView materialPass(materialPassSource.data(), materialPassSource.size());
    const AStringView materialResources(materialResourcesSource.data(), materialResourcesSource.size());
    const AStringView meshHeader(meshHeaderSource.data(), meshHeaderSource.size());
    const AStringView meshView(meshViewSource.data(), meshViewSource.size());
    const AStringView csgHeader(csgHeaderSource.data(), csgHeaderSource.size());
    const AStringView csgResources(csgResourcesSource.data(), csgResourcesSource.size());
    const AStringView csgInterval(csgIntervalSource.data(), csgIntervalSource.size());
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());

    EXPECT_FALSE(ContainsText(avboitHeader, "renderAvboitPasses"));
    EXPECT_FALSE(ContainsText(avboitHeader, "renderAvboitPreDepthWarpPasses"));
    EXPECT_FALSE(ContainsText(avboitHeader, "renderAvboitPostOccupancyPasses"));
    EXPECT_FALSE(ContainsText(avboitHeader, "buildTransparentCsgIntervals"));
    EXPECT_FALSE(ContainsText(avboitHeader, "clearAvboitTargets"));
    EXPECT_FALSE(ContainsText(avboit, "renderAvboitPasses"));
    EXPECT_FALSE(ContainsText(avboit, "renderAvboitPreDepthWarpPasses"));
    EXPECT_FALSE(ContainsText(avboit, "renderAvboitPostOccupancyPasses"));
    EXPECT_FALSE(ContainsText(avboit, "buildTransparentCsgIntervals"));
    EXPECT_FALSE(ContainsText(avboit, "clearAvboitTargets"));
    EXPECT_FALSE(ContainsText(avboit, "ClearAvboitTargetValues"));

    EXPECT_FALSE(ContainsText(materialHeader, "renderMaterialPass("));
    EXPECT_FALSE(ContainsText(materialHeader, "uploadMaterialPassDrawBuffers"));
    EXPECT_FALSE(ContainsText(materialPass, "renderMaterialPass("));
    EXPECT_FALSE(ContainsText(materialResources, "commandList.writeBuffer("));
    EXPECT_FALSE(ContainsText(meshHeader, "updateMeshViewBuffer"));
    EXPECT_FALSE(ContainsText(meshView, "commandList.writeBuffer("));
    EXPECT_FALSE(ContainsText(csgHeader, "uploadCsgFrameBuffers"));
    EXPECT_FALSE(ContainsText(csgHeader, "uploadCsgIntervalSampleState"));
    EXPECT_FALSE(ContainsText(csgResources, "commandList.writeBuffer("));
    EXPECT_FALSE(ContainsText(csgInterval, "commandList.writeBuffer("));

    EXPECT_TRUE(ContainsText(taskGraph, "TransparentMaterialPassGraphSnapshot"));
    EXPECT_TRUE(ContainsText(taskGraph, "if(payload.hasTransparentRenderers && (!payload.occupancyPhasePrepared || !payload.occupancySnapshot.captured))"));
    EXPECT_TRUE(ContainsText(taskGraph, "if(payload.hasTransparentRenderers && (!payload.extinctionPhasePrepared || !payload.extinctionSnapshot.captured))"));
    EXPECT_TRUE(ContainsText(taskGraph, "if(payload.hasTransparentRenderers && (!payload.accumulationPhasePrepared || !payload.accumulationSnapshot.captured))"));
    EXPECT_TRUE(ContainsText(taskGraph, "addUploadBufferTask("));

    const usize avboitPreOffset = taskGraph.find("struct AvboitPreGraphTask");
    const usize avboitOccupancyOffset = taskGraph.find("struct AvboitOccupancyComputeEmulationGraphTask", avboitPreOffset);
    ASSERT_NE(avboitPreOffset, AStringView::npos);
    ASSERT_NE(avboitOccupancyOffset, AStringView::npos);
    ASSERT_LT(avboitPreOffset, avboitOccupancyOffset);
    const AStringView avboitPre = taskGraph.substr(avboitPreOffset, avboitOccupancyOffset - avboitPreOffset);
    EXPECT_TRUE(ContainsText(avboitPre, "if(payload.transparentCsgStreamsUploaded != payload.transparentCsgSnapshot.captured)"));
    EXPECT_FALSE(ContainsText(avboitPre, "CsgFrameState"));

    const usize transparentCsgCaptureOffset = taskGraph.find("avboitPrePayload.transparentCsgSnapshot.capture(");
    const usize transparentCsgSpanCaptureOffset = taskGraph.find("avboitCsgReceiverSpanPayload.transparentCsgSnapshot.capture(");
    const usize transparentCsgCombineCaptureOffset = taskGraph.find("avboitCsgIntervalCombinePayload.transparentCsgSnapshot.capture(");
    const usize transparentCsgUploadedOffset = taskGraph.find("avboitPrePayload.transparentCsgStreamsUploaded = true");
    ASSERT_NE(transparentCsgCaptureOffset, AStringView::npos);
    ASSERT_NE(transparentCsgSpanCaptureOffset, AStringView::npos);
    ASSERT_NE(transparentCsgCombineCaptureOffset, AStringView::npos);
    ASSERT_NE(transparentCsgUploadedOffset, AStringView::npos);
    EXPECT_LT(transparentCsgCaptureOffset, transparentCsgUploadedOffset);
    EXPECT_LT(transparentCsgSpanCaptureOffset, transparentCsgUploadedOffset);
    EXPECT_LT(transparentCsgCombineCaptureOffset, transparentCsgUploadedOffset);
}


// Shadow Prepare consumes the immutable material-context selector uploaded before graph recording. Do not retain
// a native writer that can re-read descriptor slots after that declaration boundary.
TEST(EcsGraphics, RayTraceMaterialContextSelectorHasNoNativeCompatibilityDispatcher){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString rayTracingHeaderSource;
    AString shadowSource;
    AString taskGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.h", rayTracingHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_shadow.cpp", shadowSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "task_graph.cpp", taskGraphSource));
    const AStringView rayTracingHeader(rayTracingHeaderSource.data(), rayTracingHeaderSource.size());
    const AStringView shadow(shadowSource.data(), shadowSource.size());
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());

    EXPECT_TRUE(ContainsText(rayTracingHeader, "snapshotRayTraceMaterialContextSlots"));
    EXPECT_FALSE(ContainsText(rayTracingHeader, "uploadRayTraceMaterialContextSlots"));
    EXPECT_FALSE(ContainsText(shadow, "uploadRayTraceMaterialContextSlots"));
    EXPECT_TRUE(ContainsText(taskGraph, "render.raytrace.material_context_slots_upload"));
    EXPECT_TRUE(ContainsText(taskGraph, "m_rayTraceMaterialContextSlotsUploadTask"));
    EXPECT_FALSE(ContainsText(taskGraph, "rayTraceMaterialContextSlotsGraphOwned"));
}


// A frozen caustic-emission stream is either represented by a graph upload blob or absent. Recording must not
// re-upload it natively after the packet has declared the target buffer.
TEST(EcsGraphics, CausticEmissionTargetsHaveNoNativeCompatibilityDispatcher){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString rayTracingHeaderSource;
    AString causticsSource;
    AString taskGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.h", rayTracingHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_caustics.cpp", causticsSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "task_graph.cpp", taskGraphSource));
    const AStringView rayTracingHeader(rayTracingHeaderSource.data(), rayTracingHeaderSource.size());
    const AStringView caustics(causticsSource.data(), causticsSource.size());
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());

    EXPECT_TRUE(ContainsText(rayTracingHeader, "retainPreparedCausticEmissionTargetUpload"));
    EXPECT_FALSE(ContainsText(rayTracingHeader, "recordPreparedCausticEmissionTargets"));
    EXPECT_FALSE(ContainsText(caustics, "recordPreparedCausticEmissionTargets"));
    EXPECT_FALSE(ContainsText(caustics, "commandList.writeBuffer("));
    EXPECT_TRUE(ContainsText(taskGraph, "render.raytrace.caustic_emission_targets_upload"));
    EXPECT_TRUE(ContainsText(taskGraph, "m_causticEmissionTargetsUploadTask"));
    EXPECT_FALSE(ContainsText(taskGraph, "causticEmissionTargetsGraphOwned"));
}


// Surfel frame constants are frozen while the graph is declared. The Shadow Prepare and optional hybrid-tail
// callbacks must consume that upload rather than recomputing and writing a later mutable constant buffer.
TEST(EcsGraphics, SurfelFrameConstantsHaveNoNativeCompatibilityDispatcher){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString rayTracingHeaderSource;
    AString surfelSource;
    AString taskGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.h", rayTracingHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_surfel_gi.cpp", surfelSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "task_graph.cpp", taskGraphSource));
    const AStringView rayTracingHeader(rayTracingHeaderSource.data(), rayTracingHeaderSource.size());
    const AStringView surfel(surfelSource.data(), surfelSource.size());
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());

    EXPECT_TRUE(ContainsText(rayTracingHeader, "retainPreparedSurfelFrameConstantsUpload"));
    EXPECT_FALSE(ContainsText(rayTracingHeader, "recordPreparedSurfelFrameConstants"));
    EXPECT_FALSE(ContainsText(surfel, "recordPreparedSurfelFrameConstants"));
    EXPECT_FALSE(ContainsText(surfel, "commandList.writeBuffer("));
    EXPECT_TRUE(ContainsText(taskGraph, "render.surfel_gi.constants_upload"));
    EXPECT_TRUE(ContainsText(taskGraph, "m_surfelFrameConstantsUploadTask"));
    EXPECT_FALSE(ContainsText(taskGraph, "surfelFrameConstantsGraphOwned"));
}


// The current deferred bindless selector has a mandatory graph upload before any Shadow/Lighting/Composite/Present
// consumer. Those render callbacks must not retain a native writer for a target-generation selector.
TEST(EcsGraphics, DeferredBindlessSelectorHasNoNativeCompatibilityDispatcher){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString deferredHeaderSource;
    AString deferredTargetsSource;
    AString deferredLightingSource;
    AString deferredCompositeSource;
    AString taskGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "deferred" / "deferred_system.h", deferredHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "deferred" / "deferred_targets.cpp", deferredTargetsSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "deferred" / "deferred_lighting.cpp", deferredLightingSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "deferred" / "deferred_composite.cpp", deferredCompositeSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "task_graph.cpp", taskGraphSource));
    const AStringView deferredHeader(deferredHeaderSource.data(), deferredHeaderSource.size());
    const AStringView deferredTargets(deferredTargetsSource.data(), deferredTargetsSource.size());
    const AStringView deferredLighting(deferredLightingSource.data(), deferredLightingSource.size());
    const AStringView deferredComposite(deferredCompositeSource.data(), deferredCompositeSource.size());
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());

    EXPECT_FALSE(ContainsText(deferredHeader, "uploadDeferredBindlessFrameResources"));
    EXPECT_FALSE(ContainsText(deferredTargets, "uploadDeferredBindlessFrameResources"));
    EXPECT_FALSE(ContainsText(deferredTargets, "bindless.slotsBuffer.get(), &bindless.slots"));
    EXPECT_FALSE(ContainsText(deferredLighting, "currentBindlessSlotsGraphOwned"));
    EXPECT_FALSE(ContainsText(deferredComposite, "currentBindlessSlotsGraphOwned"));
    EXPECT_TRUE(ContainsText(taskGraph, "render.deferred.bindless_slots_upload"));
    EXPECT_TRUE(ContainsText(taskGraph, "m_deferredBindlessSlotsUploadTask"));
}


// The optional lagged-history selector is either already accepted or uploaded as a declared dependency of Lighting.
// Keep the render callback free of a native writer so rejected graph work cannot make that selector look resident.
TEST(EcsGraphics, LaggedLightingSelectorHasNoNativeCompatibilityDispatcher){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString deferredHeaderSource;
    AString deferredTargetsSource;
    AString deferredLightingSource;
    AString taskGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "deferred" / "deferred_system.h", deferredHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "deferred" / "deferred_targets.cpp", deferredTargetsSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "deferred" / "deferred_lighting.cpp", deferredLightingSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "task_graph.cpp", taskGraphSource));
    const AStringView deferredHeader(deferredHeaderSource.data(), deferredHeaderSource.size());
    const AStringView deferredTargets(deferredTargetsSource.data(), deferredTargetsSource.size());
    const AStringView deferredLighting(deferredLightingSource.data(), deferredLightingSource.size());
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());

    EXPECT_FALSE(ContainsText(deferredHeader, "uploadLaggedLightingHistoryResources"));
    EXPECT_FALSE(ContainsText(deferredTargets, "uploadLaggedLightingHistoryResources"));
    EXPECT_FALSE(ContainsText(deferredTargets, "history.slotsBuffer.get(), &history.slots"));
    EXPECT_FALSE(ContainsText(deferredLighting, "laggedBindlessSlotsGraphOwned"));
    EXPECT_TRUE(ContainsText(taskGraph, "render.lagged_lighting.bindless_slots_upload"));
    EXPECT_TRUE(ContainsText(taskGraph, "laggedBindlessSlotsGraphOwned"));
}


// The legacy bulk shadow-context uploader has no caller: frozen graph batches and the explicit hybrid restore own
// those two supported paths. Keep the dead mutable writer out of the ray-tracing subsystem.
TEST(EcsGraphics, ShadowMaterialContextHasNoDeadNativeBulkUploader){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString rayTracingHeaderSource;
    AString shadowSource;
    AString swBvhSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.h", rayTracingHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_shadow.cpp", shadowSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_swbvh.cpp", swBvhSource));
    const AStringView rayTracingHeader(rayTracingHeaderSource.data(), rayTracingHeaderSource.size());
    const AStringView shadow(shadowSource.data(), shadowSource.size());
    const AStringView swBvh(swBvhSource.data(), swBvhSource.size());

    EXPECT_FALSE(ContainsText(rayTracingHeader, "uploadShadowMaterialContextBuffers"));
    EXPECT_FALSE(ContainsText(shadow, "uploadShadowMaterialContextBuffers"));
    EXPECT_TRUE(ContainsText(rayTracingHeader, "recordPreparedHybridHardwareMaterialContextFallback"));
    EXPECT_TRUE(ContainsText(swBvh, "UploadPreparedShadowMaterialContextBuffers"));
}


// The remaining renderer native writes are intentional compatibility boundaries, not normal prepared-frame
// uploads. Keep their small, named surface fixed so future work must either graph-own a new writer or document it.
TEST(EcsGraphics, NativeRendererWritesRemainExplicitCompatibilityBoundaries){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString swBvhSource;
    AString shadowSource;
    AString uiSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_swbvh.cpp", swBvhSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_shadow.cpp", shadowSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "system.cpp", uiSource));
    const AStringView swBvh(swBvhSource.data(), swBvhSource.size());
    const AStringView shadow(shadowSource.data(), shadowSource.size());
    const AStringView ui(uiSource.data(), uiSource.size());

    // Frozen hybrid plans normally use graph blobs. These six writes exist only behind the retained stale-plan
    // rebuild/restore route, whose direct retry disables later material consumers when it cannot remain tracked.
    EXPECT_EQ(CountText(swBvh, "writeBuffer("), 6u);
    EXPECT_TRUE(ContainsText(swBvh, "if(shadowMaterialContextBatchGraphOwned){"));
    EXPECT_TRUE(ContainsText(swBvh, "graph-owned HW shadow material context unexpectedly reused a native cache"));
    EXPECT_TRUE(ContainsText(swBvh, "graph-owned SW shadow material context unexpectedly reused a native cache"));
    EXPECT_TRUE(ContainsText(swBvh, "recordPreparedHybridHardwareMaterialContextFallback"));

    // Adaptive statistics use graph primitives on the normal prepared route. The one direct copy remains only for
    // a compatibility invocation that did not supply that frozen plan.
    EXPECT_EQ(CountText(shadow, "copyBuffer("), 1u);
    EXPECT_TRUE(ContainsText(shadow, "if(snapshot && !graphOwnsAdaptivePrimitives)"));

    // UI’s one direct submission is the documented availability fallback after both standalone graph attempts
    // reject; its retained live draw bytes are never a normal renderer-owned overlay update.
    EXPECT_EQ(CountText(ui, "executeCommandLists("), 1u);
    EXPECT_TRUE(ContainsText(ui, "standalone legacy ImGui graph presentation failed; retaining direct raster fallback"));
    EXPECT_TRUE(ContainsText(ui, "direct ImGui fallback submission was rejected; retaining frame for retry"));
}


// The current renderer has exactly two runtime-selected sampled-image domains: material Texture2D assets (shared
// by raster and ray-trace surface dispatch) and ImGui textures.  A new domain must not silently rely on a global
// descriptor slot: keep the supported domain small and require each one to retain handles before graph declaration.
TEST(EcsGraphics, DynamicBindlessSampledImagesHaveFrozenGraphDeclarationOwners){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString materialAssetHeaderSource;
    AString materialSurfaceSource;
    AString taskGraphSource;
    AString uiHeaderSource;
    AString uiSource;
    AString uiTextureSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "assets_material" / "asset.h", materialAssetHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_surface.cpp", materialSurfaceSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "task_graph.cpp", taskGraphSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "system.h", uiHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "system.cpp", uiSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "texture_resources.cpp", uiTextureSource));

    const AStringView materialAssetHeader(materialAssetHeaderSource.data(), materialAssetHeaderSource.size());
    const AStringView materialSurface(materialSurfaceSource.data(), materialSurfaceSource.size());
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());
    const AStringView uiHeader(uiHeaderSource.data(), uiHeaderSource.size());
    const AStringView ui(uiSource.data(), uiSource.size());
    const AStringView uiTextures(uiTextureSource.data(), uiTextureSource.size());

    // Material resource validation supports only a Texture2D asset and a sampler. The prepared collector must
    // resolve the former to a retained texture handle, while samplers deliberately have no resource state to track.
    EXPECT_TRUE(ContainsText(materialAssetHeader, "SampledImage2D = 1"));
    EXPECT_TRUE(ContainsText(materialAssetHeader, "Sampler = 2"));
    EXPECT_TRUE(ContainsText(materialAssetHeader, "return resourceKind == MaterialResourceKind::SampledImage2D || resourceKind == MaterialResourceKind::Sampler"));
    EXPECT_TRUE(ContainsText(materialSurface, "appendPreparedMaterialSurfaceSampledTextures"));
    EXPECT_TRUE(ContainsText(materialSurface, "inOutTextures.push_back(textureResource.texture)"));
    EXPECT_TRUE(ContainsText(materialSurface, "default:\n            return false;"));

    // Raster and trace consumers share the frozen material collection. The named sets make a future dynamic
    // bindless consumer visible to the audit rather than allowing it to hide behind the descriptor heap.
    EXPECT_TRUE(ContainsText(taskGraph, "GatherPreparedMaterialSampledTextureResourceSet"));
    EXPECT_TRUE(ContainsText(taskGraph, "render.graphics_prefix.gbuffer.material_sampled_textures"));
    EXPECT_TRUE(ContainsText(taskGraph, "render.graphics_prefix.csg_interval_sample.material_sampled_textures"));
    EXPECT_TRUE(ContainsText(taskGraph, "render.avboit.intervals.transparent_csg_material_sampled_textures"));
    EXPECT_TRUE(ContainsText(taskGraph, "render.avboit.occupancy.material_sampled_textures"));
    EXPECT_TRUE(ContainsText(taskGraph, "render.avboit.extinction.material_sampled_textures"));
    EXPECT_TRUE(ContainsText(taskGraph, "render.avboit.accumulation.material_sampled_textures"));
    EXPECT_TRUE(ContainsText(taskGraph, "render.trace_material_sampled_textures"));
    EXPECT_TRUE(ContainsText(taskGraph, "traceMaterialSampledTextureSetUse"));

    // ImGui is the other dynamic domain. Its draw command retains the selected texture and heap slot, its upload
    // path imports the exact destination, and the terminal task declares that frozen texture rather than reading
    // the mutable ImGui command list.
    EXPECT_TRUE(ContainsText(uiHeader, "Core::TextureHandle texture;"));
    EXPECT_TRUE(ContainsText(uiHeader, "Core::GpuDescriptorHandle textureHeapHandle"));
    EXPECT_TRUE(ContainsText(uiTextures, "heap.allocate(Core::GpuDescriptorClass::SampledImage)"));
    EXPECT_TRUE(ContainsText(uiTextures, "importTaskGraphTexture(graph, *resource)"));
    EXPECT_TRUE(ContainsText(uiTextures, "graph.addUploadTextureTask("));
    EXPECT_TRUE(ContainsText(ui, "appendDrawTextureUse(drawCommand.texture)"));
    EXPECT_TRUE(ContainsText(ui, "m_taskGraphDrawCommands.push_back(TaskGraphDrawCommand{"));
    EXPECT_TRUE(ContainsText(ui, "graph-owned ImGui overlay cannot safely record a custom draw callback"));
}


// The hybrid HW-to-SW tail may fail after the software material context has replaced the opaque-HW context. Its
// successful frozen restore must use declaration-time graph blobs; only a stale snapshot may retain the existing
// direct re-gather/retry boundary, which disables later consumers before they can observe undeclared resources.
TEST(EcsGraphics, HybridHardwareFallbackRestoreUsesGraphOwnedBlobsWhenFrozen){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString taskGraphSource;
    AString rayTracingHeaderSource;
    AString rayTracingSource;
    AString swBvhSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "task_graph.cpp", taskGraphSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.h", rayTracingHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.cpp", rayTracingSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_swbvh.cpp", swBvhSource));

    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());
    const AStringView rayTracingHeader(rayTracingHeaderSource.data(), rayTracingHeaderSource.size());
    const AStringView rayTracing(rayTracingSource.data(), rayTracingSource.size());
    const AStringView swBvh(swBvhSource.data(), swBvhSource.size());

    EXPECT_TRUE(ContainsText(rayTracingHeader, "retainPreparedHybridHardwareMaterialContextFallbackUploads"));
    EXPECT_TRUE(ContainsText(rayTracingHeader, "hybridHardwareFallbackUploadsGraphOwned"));
    EXPECT_TRUE(ContainsText(rayTracing, "retainPreparedHybridHardwareMaterialContextFallbackUploads"));
    EXPECT_TRUE(ContainsText(rayTracing, "graph.copyUploadData("));
    EXPECT_TRUE(ContainsText(taskGraph, "hybridHardwareFallbackInstanceMaterialBlob"));
    EXPECT_TRUE(ContainsText(taskGraph, "hybridHardwareFallbackUploadsGraphOwned"));
    EXPECT_TRUE(ContainsText(taskGraph, "context.taskGraph.uploadBlobData("));
    EXPECT_TRUE(ContainsText(taskGraph, "frozen hybrid hardware material fallback cannot use graph-owned upload blobs"));
    EXPECT_TRUE(ContainsText(swBvh, "const void* const instanceMaterialData"));
    EXPECT_TRUE(ContainsText(swBvh, "graph-owned hybrid hardware fallback bytes differ from preflight"));
    EXPECT_TRUE(ContainsText(swBvh, "tryWriteBuffer(instanceMaterialBuffer, instanceMaterialData"));

    // Keep the stale-snapshot direct retry explicitly narrow. It remains the compatibility boundary only after the
    // immutable graph bytes fail validation, and it disables material consumers for this compiled frame.
    EXPECT_TRUE(ContainsText(rayTracing, "!restoredFrozenHardwareContext\n                    && buildSceneTlas(commandList, scratchArena, false)"));
    EXPECT_TRUE(ContainsText(rayTracing, "disableHybridMaterialConsumers();"));
}


// A fresh acceleration-structure backing allocation has the device descriptor's Common state; a retained backing
// instead needs the exact accepted Shadow Preparation handoff. Keep freshness tied to the physical generation so a
// discarded plan retries Common while an accepted packet never fabricates AccelStructRead.
TEST(EcsGraphics, PreparedAccelStructInitialStatesTrackBackingGenerationHandoffs){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString taskGraphSource;
    AString rayTracingHeaderSource;
    AString rayTracingSource;
    AString swBvhSource;
    AString meshTypesSource;
    AString rendererStateHeaderSource;
    AString rendererStateSource;
    AString systemSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "task_graph.cpp", taskGraphSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.h", rayTracingHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.cpp", rayTracingSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_swbvh.cpp", swBvhSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "mesh" / "renderer_mesh_types.h", meshTypesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "shared" / "renderer_state.h", rendererStateHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "shared" / "renderer_state.cpp", rendererStateSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "system.cpp", systemSource));
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());
    const AStringView rayTracingHeader(rayTracingHeaderSource.data(), rayTracingHeaderSource.size());
    const AStringView rayTracing(rayTracingSource.data(), rayTracingSource.size());
    const AStringView swBvh(swBvhSource.data(), swBvhSource.size());
    const AStringView meshTypes(meshTypesSource.data(), meshTypesSource.size());
    const AStringView rendererStateHeader(rendererStateHeaderSource.data(), rendererStateHeaderSource.size());
    const AStringView rendererState(rendererStateSource.data(), rendererStateSource.size());
    const AStringView system(systemSource.data(), systemSource.size());

    EXPECT_TRUE(ContainsText(meshTypes, "bool blasBackingFresh = false;"));
    EXPECT_TRUE(ContainsText(meshTypes, "bool blasBackingStateHandoffPending = false;"));
    EXPECT_TRUE(ContainsText(rayTracingHeader, "bool backingFresh = false;"));
    EXPECT_TRUE(ContainsText(swBvh, "outBuild.backingFresh = meshResources.blasBackingFresh;"));
    EXPECT_TRUE(ContainsText(swBvh, "meshResources.blasBackingFresh != build.backingFresh"));
    EXPECT_EQ(CountText(swBvh, "meshResources.blasBackingFresh = true;"), 1u);
    EXPECT_TRUE(ContainsText(
        swBvh,
        "if(meshResources.blasBackingFresh)\n"
        "        meshResources.blasBackingStateHandoffPending = true;"
    ));
    EXPECT_TRUE(ContainsText(
        swBvh,
        "meshResources.blasBackingFresh = false;\n"
        "        meshResources.blasBackingStateHandoffPending = false;"
    ));
    EXPECT_TRUE(ContainsText(
        taskGraph,
        "const Core::ResourceStates::Mask blasInitialState = build.backingFresh\n"
        "                ? Core::ResourceStates::Common\n"
        "                : Core::ResourceStates::Unknown\n"
        "            ;"
    ));
    EXPECT_TRUE(ContainsText(taskGraph, "AccelStructResourceDesc(blasIdentity, \"Prepared Mesh BLAS\").setInitialState(blasInitialState)"));
    EXPECT_TRUE(ContainsText(
        taskGraph,
        "const Core::ResourceStates::Mask blasInitialState = mesh.blasBackingFresh\n"
        "            ? Core::ResourceStates::Common\n"
        "            : Core::ResourceStates::Unknown\n"
        "        ;"
    ));
    EXPECT_TRUE(ContainsText(taskGraph, "AccelStructResourceDesc(blasIdentity, \"Mesh BLAS\").setInitialState(blasInitialState)"));

    EXPECT_TRUE(ContainsText(rendererStateHeader, "bool m_tlasBackingFresh = false;"));
    EXPECT_TRUE(ContainsText(rendererStateHeader, "bool m_tlasBackingStateHandoffPending = false;"));
    EXPECT_TRUE(ContainsText(rendererState, "m_tlasBackingFresh = false;"));
    EXPECT_TRUE(ContainsText(rendererState, "m_tlasBackingStateHandoffPending = false;"));
    EXPECT_TRUE(ContainsText(swBvh, "rayTracingState().m_tlasBackingFresh = true;"));
    EXPECT_TRUE(ContainsText(
        swBvh,
        "if(rayTracingState().m_tlasBackingFresh)\n"
        "            rayTracingState().m_tlasBackingStateHandoffPending = true;"
    ));
    EXPECT_TRUE(ContainsText(rayTracingHeader, "sceneTlasBackingInitialState()const noexcept"));
    EXPECT_FALSE(ContainsText(rayTracingHeader, "preparedSceneTlasBuildInitialState()const noexcept"));
    EXPECT_TRUE(ContainsText(
        rayTracing,
        "return state.m_tlasBackingFresh\n"
        "        ? Core::ResourceStates::Common\n"
        "        : Core::ResourceStates::Unknown\n"
        "    ;"
    ));
    EXPECT_TRUE(ContainsText(
        rayTracing,
        "if(preparedTlasMatchesCurrent){\n"
        "        state.m_tlasBackingFresh = false;\n"
        "        state.m_tlasBackingStateHandoffPending = false;"
    ));
    EXPECT_TRUE(ContainsText(
        rayTracing,
        "if(state.m_tlasBackingFresh && state.m_tlasBackingStateHandoffPending){\n"
        "        state.m_tlasBackingFresh = false;\n"
        "        state.m_tlasBackingStateHandoffPending = false;"
    ));
    EXPECT_TRUE(ContainsText(
        rayTracing,
        "if(meshResources.blasBackingFresh && meshResources.blasBackingStateHandoffPending){\n"
        "            meshResources.blasBackingFresh = false;\n"
        "            meshResources.blasBackingStateHandoffPending = false;"
    ));
    EXPECT_TRUE(ContainsText(
        taskGraph,
        "const Core::ResourceStates::Mask sceneTlasInitialState = m_raytracingSystem.sceneTlasBackingInitialState();"
    ));
    EXPECT_TRUE(ContainsText(taskGraph, "AccelStructResourceDesc(Name(\"render.deferred_effects.tlas\"), \"Scene TLAS\").setInitialState(sceneTlasInitialState)"));
    EXPECT_EQ(CountText(taskGraph, "AccelStructResourceDesc(Name(\"render.deferred_effects.tlas\"), \"Scene TLAS\")"), 4u);
    EXPECT_EQ(CountText(taskGraph, "sceneTlasBackingInitialState()"), 4u);
    EXPECT_EQ(
        CountText(taskGraph, ".setInitialState(m_raytracingSystem.sceneTlasBackingInitialState())"),
        3u
    );

    const usize clearPreparedSceneTlasOffset = rayTracing.find("void RendererRayTracingSystem::clearPreparedSceneTlasBuild()noexcept");
    const usize capturePreparedSceneTlasOffset = rayTracing.find(
        "bool RendererRayTracingSystem::capturePreparedSceneTlasBuild(",
        clearPreparedSceneTlasOffset
    );
    const usize discardPreflightOffset = rayTracing.find("void RendererRayTracingSystem::discardPreflightShadowVisibilityResources()noexcept");
    const usize preflightOffset = rayTracing.find(
        "bool RendererRayTracingSystem::preflightShadowVisibilityResources(",
        discardPreflightOffset
    );
    const usize commitPersistentStateOffset = system.find("renderer.m_shadowPreparePersistentState.commit(");
    const usize confirmSceneTlasOffset = system.find("renderer.m_raytracingSystem.confirmPreparedSceneTlasBuild();");
    const usize confirmMeshBlasOffset = system.find("renderer.m_raytracingSystem.confirmPreparedMeshBlasBuilds();");
    const usize confirmDirectHandoffOffset = system.find(
        "renderer.m_raytracingSystem.confirmAcceptedShadowPrepareAccelStructStateHandoffs();"
    );
    ASSERT_NE(clearPreparedSceneTlasOffset, AStringView::npos);
    ASSERT_NE(capturePreparedSceneTlasOffset, AStringView::npos);
    ASSERT_NE(discardPreflightOffset, AStringView::npos);
    ASSERT_NE(preflightOffset, AStringView::npos);
    ASSERT_NE(commitPersistentStateOffset, AStringView::npos);
    ASSERT_NE(confirmSceneTlasOffset, AStringView::npos);
    ASSERT_NE(confirmMeshBlasOffset, AStringView::npos);
    ASSERT_NE(confirmDirectHandoffOffset, AStringView::npos);
    EXPECT_FALSE(ContainsText(
        rayTracing.substr(clearPreparedSceneTlasOffset, capturePreparedSceneTlasOffset - clearPreparedSceneTlasOffset),
        "m_tlasBackingFresh"
    ));
    EXPECT_TRUE(ContainsText(
        rayTracing.substr(discardPreflightOffset, preflightOffset - discardPreflightOffset),
        "clearPreparedSceneTlasBuild();"
    ));
    EXPECT_TRUE(ContainsText(
        rayTracing.substr(discardPreflightOffset, preflightOffset - discardPreflightOffset),
        "m_tlasBackingStateHandoffPending = false;"
    ));
    EXPECT_TRUE(ContainsText(
        rayTracing.substr(discardPreflightOffset, preflightOffset - discardPreflightOffset),
        "meshResources.blasBackingStateHandoffPending = false;"
    ));
    EXPECT_FALSE(ContainsText(
        rayTracing.substr(discardPreflightOffset, preflightOffset - discardPreflightOffset),
        "m_tlasBackingFresh = false;"
    ));
    EXPECT_FALSE(ContainsText(
        rayTracing.substr(discardPreflightOffset, preflightOffset - discardPreflightOffset),
        "meshResources.blasBackingFresh = false;"
    ));
    // A missing or rejected candidate occurs after native packet acceptance. Retain plan cleanup, but force the
    // established device-recreation recovery path before any fresh backing can be retried with Common.
    EXPECT_TRUE(ContainsText(system, "bool shadowPrepareStateLostAfterAcceptance = false;"));
    EXPECT_EQ(CountText(system, "context->shadowPrepareStateLostAfterAcceptance = true;"), 2u);
    EXPECT_TRUE(ContainsText(
        system,
        "context->shadowPrepareStateLostAfterAcceptance = true;\n"
        "                renderer.m_raytracingSystem.discardPreflightShadowVisibilityResources();\n"
        "                context->shadowPrepareStateReady = false;\n"
        "                return false;"
    ));
    EXPECT_TRUE(ContainsText(
        system,
        "context->shadowPrepareStateLostAfterAcceptance = true;\n"
        "                    renderer.m_raytracingSystem.discardPreflightShadowVisibilityResources();\n"
        "                    context->shadowPrepareStateReady = false;\n"
        "                    return false;"
    ));
    EXPECT_TRUE(ContainsText(
        system,
        "if(!recovered || prefixTimingAcceptance.shadowPrepareStateLostAfterAcceptance)\n"
        "                failFrameRenderRecovery();"
    ));
    const usize stateLossAfterAcceptanceOffset = system.find("context->shadowPrepareStateLostAfterAcceptance = true;");
    const usize commitStateLossOffset = system.find(
        "context->shadowPrepareStateLostAfterAcceptance = true;",
        commitPersistentStateOffset
    );
    const usize stateLossRecoveryGuardOffset = system.find(
        "if(!recovered || prefixTimingAcceptance.shadowPrepareStateLostAfterAcceptance)"
    );
    const usize stateLossRecoveryOffset = system.find("failFrameRenderRecovery();", stateLossRecoveryGuardOffset);
    ASSERT_NE(stateLossAfterAcceptanceOffset, AStringView::npos);
    ASSERT_NE(commitStateLossOffset, AStringView::npos);
    ASSERT_NE(stateLossRecoveryGuardOffset, AStringView::npos);
    ASSERT_NE(stateLossRecoveryOffset, AStringView::npos);
    EXPECT_LT(commitPersistentStateOffset, confirmSceneTlasOffset);
    EXPECT_LT(confirmSceneTlasOffset, confirmMeshBlasOffset);
    EXPECT_LT(confirmMeshBlasOffset, confirmDirectHandoffOffset);
    EXPECT_LT(stateLossAfterAcceptanceOffset, stateLossRecoveryGuardOffset);
    EXPECT_LT(commitPersistentStateOffset, commitStateLossOffset);
    EXPECT_LT(commitStateLossOffset, stateLossRecoveryGuardOffset);
    EXPECT_LT(stateLossRecoveryGuardOffset, stateLossRecoveryOffset);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

