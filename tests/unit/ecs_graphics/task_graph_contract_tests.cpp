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


// Source contracts name only their implementation owners. Do not sweep every task-graph source here: an unrelated
// file split must not change a contract that has no dependency on it.
static bool ReadRendererSources(
    const TestPath& repoRoot,
    const InitializerList<StringView> sourcePaths,
    AString& outSource
){
    const TestPath rendererDirectory = repoRoot / "impl" / "ecs_render";
    outSource.clear();
    for(const StringView sourcePath : sourcePaths){
        AString source;
        if(!ReadTextFile(rendererDirectory / sourcePath.data(), source))
            return false;
        if(!outSource.empty())
            outSource += "\n\n";
        outSource.append(source.data(), source.size());
    }
    return true;
}


static bool ReadGraphicsModuleSources(const TestPath& repoRoot, AString& outSource){
    static constexpr StringView s_SourceNames[] = {
        "module.cpp",
        "module_graph_setup.cpp",
        "module_texture_upload.cpp",
        "module_setup.cpp",
    };

    const TestPath graphicsDirectory = repoRoot / "core" / "graphics";
    outSource.clear();
    for(const StringView sourceName : s_SourceNames){
        AString source;
        if(!ReadTextFile(graphicsDirectory / sourceName.data(), source))
            return false;
        if(!outSource.empty())
            outSource += "\n\n";
        outSource.append(source.data(), source.size());
    }
    return true;
}


static bool ReadRendererSystemSources(const TestPath& repoRoot, AString& outSource){
    static constexpr StringView s_SourceNames[] = {
        "system.cpp",
        "system_resources.cpp",
        "system_render.cpp",
    };

    const TestPath systemDirectory = repoRoot / "impl" / "ecs_render" / "kernel";
    outSource.clear();
    for(const StringView sourceName : s_SourceNames){
        AString source;
        if(!ReadTextFile(systemDirectory / sourceName.data(), source))
            return false;
        if(!outSource.empty())
            outSource += "\n\n";
        outSource.append(source.data(), source.size());
    }
    return true;
}


// Compile, recording, and accepted-submission statistics live with the immutable graph artifacts. Keep the renderer
// bridge by-value so debug tooling can inspect one coherent generation without reaching into private packet storage.
TEST(EcsGraphics, DeferredGraphExposesRuntimeTelemetryArtifacts){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemHeaderSource;
    AString systemSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "system.h", systemHeaderSource));
    ASSERT_TRUE(ReadRendererSystemSources(repoRoot, systemSource));
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
    ASSERT_TRUE(ReadRendererSystemSources(repoRoot, systemSource));
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
    EXPECT_TRUE(ContainsText(frameGraph, "\"Recording: packets={} tasks={} command lists={} barriers={} worker-routed={} overlapped={}\\n\""));
    EXPECT_TRUE(ContainsText(frameGraph, "\"Submission: accepted packets={} accepted tasks={} rejected packets={} rejected tasks={} submissions={} command lists={} waits={} failed submissions={}\\n\""));
    EXPECT_TRUE(ContainsText(frameGraph, "\"CPU: declaration={:.3f} ms compile={:.3f} ms native recording elapsed={:.3f} ms submit={:.3f} ms\\n\""));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.declarationSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "\"CPU compile phases: analysis={:.3f} ms queue assignment={:.3f} ms planning={:.3f} ms\\n\""));
    EXPECT_TRUE(ContainsText(frameGraph, "\"CPU analysis detail: validation={:.3f} ms dependencies={:.3f} ms hazards={:.3f} ms cycles/topology={:.3f} ms\\n\""));
    EXPECT_TRUE(ContainsText(frameGraph, "\"CPU planning detail: packetization={:.3f} ms resource states/barriers={:.3f} ms packet dependencies={:.3f} ms\\n\""));
    EXPECT_TRUE(ContainsText(frameGraph, "\"CPU recording summed spans: packet={:.3f} ms command-list acquisition={:.3f} ms graph barrier lowering={:.3f} ms task={:.3f} ms\\n\""));
    EXPECT_TRUE(ContainsText(frameGraph, "\"Ready-frontier recording: elapsed={:.3f} ms logical-worker busy={:.3f} ms logical-worker capacity={:.3f} ms utilization={:.1f}%\""));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.analysisSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.queueAssignmentSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.planningSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.validationSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.dependencyAnalysisSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.hazardAnalysisSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "compileStatistics.topologicalOrderSeconds * 1000.0,"));
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
    EXPECT_TRUE(ContainsText(frameGraph, "recordingStatistics.taskRecordSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "recordingStatistics.recordingElapsedSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "recordingStatistics.readyFrontierElapsedSeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "recordingStatistics.readyFrontierWorkerBusySeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "recordingStatistics.readyFrontierWorkerCapacitySeconds * 1000.0,"));
    EXPECT_TRUE(ContainsText(frameGraph, "recordingStatistics.readyFrontierWorkerUtilization() * 100.0"));
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
    ASSERT_TRUE(ReadRendererSources(repoRoot, { "deferred/task_graph_deferred_lighting.cpp" }, taskGraphSource));
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
    EXPECT_TRUE(ContainsText(commandHeader, "struct GpuCommandArenaStatistics{"));
    EXPECT_TRUE(ContainsText(commandHeader, "struct GpuCommandArenaWorkerStatistics{"));
    EXPECT_TRUE(ContainsText(commandHeader, "Direct recording is always queryable"));
    EXPECT_TRUE(ContainsText(commandHeader, "nativeHandleStorageLowerBoundBytes counts only the"));
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
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "m_graphics.getDevice().getCommandArenaStatistics(queueInfo.id)"
    ));
    EXPECT_TRUE(ContainsText(frameGraph, "if(!commandArenaStatistics.valid())\n                continue;"));
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
        "  Recording: packets={} tasks={} command lists={} barriers={} worker-routed={} overlapped={} CPU summed spans: command-list acquisition={:.3f} ms graph barrier lowering={:.3f} ms task={:.3f} ms packet={:.3f} ms"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "  Command arena: workers={} epochs={} pending epochs={} command buffers current/high-water={}/{} reusable={} leased={} pending={} growth={} resets={} native handle storage lower bound={} bytes"
    ));
    EXPECT_TRUE(ContainsText(frameGraph, "commandArenaStatistics.pendingCommandPoolEpochCount,"));
    EXPECT_TRUE(ContainsText(frameGraph, "commandArenaStatistics.nativeHandleStorageLowerBoundBytes"));
    EXPECT_TRUE(ContainsText(frameGraph, "__hidden_frame_graph_export::AppendCommandArenaWorkerStatistics("));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "m_graphics.getDevice().getCommandArenaWorkerStatistics(queueInfo.id, 0u, 0u)"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "m_deferredLightingCompiledGraph.packetIdAt(packetIndex)"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "previousRecordedPacket->recordingWorkerDomain == recordedPacket->recordingWorkerDomain"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "previousRecordedPacket->recordingWorkerIndex == recordedPacket->recordingWorkerIndex"
    ));
    EXPECT_TRUE(ContainsText(
        frameGraph,
        "Worker arena domain={} index={}: epochs={} pending epochs={} command buffers current/high-water={}/{}"
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
    EXPECT_TRUE(ContainsText(frameGraph, "queueRecordingStatistics.workerRoutedPacketCount,"));
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
    EXPECT_TRUE(ContainsText(skinning, "m_acceptedSkinningState.buildMergedBufferSubset("));
    EXPECT_TRUE(ContainsText(skinning, "const Core::GpuTaskGraphTaskAcceptedCallback acceptedCallback{"));
    EXPECT_TRUE(ContainsText(skinning, ".task = terminalTask,\n        .context = &skinningAcceptance,"));
    EXPECT_TRUE(ContainsText(skinning, "context->stateReady = context->cache->commit(*context->candidate);"));
    EXPECT_TRUE(ContainsText(skinning, "const Core::QueueSubmissionToken skinningToken = transaction.taskToken("));
    EXPECT_TRUE(ContainsText(skinning, "if(!skinningSubmitted || !skinningAcceptance.stateReady){"));
    EXPECT_FALSE(ContainsText(skinning, "mergeAcceptedSkinningState("));
}


// Caustics and Surfel GI choose a semantic producer task at graph declaration. Keep their normal-frame merge and
// presence validation task-based so a later packet split cannot leak compiler packet identities back into the
// renderer's effect policy.
TEST(EcsGraphics, EffectsTopologyUsesSemanticTaskAnchors){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    ASSERT_TRUE(ReadRendererSystemSources(repoRoot, systemSource));
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


// Prefix and shadow record spans are task-addressed. Shadow Preparation and Mesh View Setup keep distinct timing
// packets without requiring adjacency, while the later normal-range partition remains an exact coverage invariant.
TEST(EcsGraphics, PrefixAndShadowTopologyUsesSemanticTaskAnchors){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    ASSERT_TRUE(ReadRendererSystemSources(repoRoot, systemSource));
    const AStringView system(systemSource.data(), systemSource.size());

    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredShadowPrepareTask)"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_graphicsPrefixTask)"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_graphicsPrefixDeferredClearFirstTask)"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredShadowVisibilityTask)"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredSoftwareCausticsTask)"));
    EXPECT_TRUE(ContainsText(system, "tasksSharePacket(\n            m_graphicsPrefixDeferredClearFirstTask"));
    EXPECT_TRUE(ContainsText(
        system,
        "packetRangeForTasks(m_deferredShadowPrepareTask, m_graphicsPrefixTask)"
    ));
    EXPECT_TRUE(ContainsText(
        system,
        "const bool shadowPrepareAndMeshViewSetupTimingPacketsAreDistinct =\n"
        "        !m_deferredLightingCompiledGraph.tasksSharePacket(\n"
        "            m_deferredShadowPrepareTask,\n"
        "            m_graphicsPrefixMeshViewSetupTask"
    ));
    EXPECT_EQ(CountText(system, "shadowPrepareAndMeshViewSetupTimingPacketsAreDistinct"), 3u);
    EXPECT_TRUE(ContainsText(system, "|| !shadowPrepareAndMeshViewSetupTimingPacketsAreDistinct"));
    EXPECT_TRUE(ContainsText(system, "&& shadowPrepareAndMeshViewSetupTimingPacketsAreDistinct"));
    EXPECT_TRUE(ContainsText(system, "shadowPreparePrefixSubmitter.submitTaskRangeInCompileOrderFromTasks("));
    EXPECT_TRUE(ContainsText(system, "m_deferredShadowPrepareTask,\n                m_graphicsPrefixTask,"));

    EXPECT_FALSE(ContainsText(
        system,
        "shadowPrepareThroughPrefixPacketRange.packetCount\n"
        "            != shadowPreparePacketRange.packetCount + graphicsPrefixWorkPacketRange.packetCount"
    ));
    EXPECT_TRUE(ContainsText(
        system,
        "deferredNormalPacketRange.packetCount\n"
        "            != shadowPrepareThroughPrefixPacketRange.packetCount + effectsThroughPresentationPacketRange.packetCount"
    ));
    EXPECT_TRUE(ContainsText(system, "shadowPreparePrefixTimingTicketCount == 1u + graphicsPrefixUniquePacketCount"));

    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId shadowPreparePacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId graphicsPrefixPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId shadowVisibilityPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId softwareCausticsPacket"));
}


// Software visibility and caustics retain distinct timing tickets, but their semantic range may contain untimed
// compiler packets. Keep the hardware single-packet invariant while making the software boundary task-based.
TEST(EcsGraphics, SoftwareShadowEffectsTopologyUsesSemanticTaskAnchors){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    ASSERT_TRUE(ReadRendererSystemSources(repoRoot, systemSource));
    const AStringView system(systemSource.data(), systemSource.size());

    EXPECT_TRUE(ContainsText(
        system,
        "packetRangeForTasks(\n"
        "        m_deferredShadowVisibilityTask,\n"
        "        hardwareShadowSupported ? m_deferredShadowVisibilityTask : m_deferredSoftwareCausticsTask"
    ));
    EXPECT_TRUE(ContainsText(
        system,
        "const bool softwareShadowEffectsTimingPacketsAreDistinct =\n"
        "        !hardwareShadowSupported\n"
        "        && !m_deferredLightingCompiledGraph.tasksSharePacket("
    ));
    EXPECT_EQ(CountText(system, "softwareShadowEffectsTimingPacketsAreDistinct"), 3u);
    EXPECT_TRUE(ContainsText(
        system,
        "hardwareShadowSupported\n"
        "            ? shadowEffectsPacketRange.packetCount != RendererSystemRenderDetail::s_SinglePacketCount"
    ));
    EXPECT_TRUE(ContainsText(system, "shadowEffectsSubmitter.submitTaskRangeInCompileOrderFromTasks("));
    EXPECT_TRUE(ContainsText(
        system,
        "m_deferredShadowVisibilityTask,\n"
        "                hardwareShadowSupported ? m_deferredShadowVisibilityTask : m_deferredSoftwareCausticsTask,"
    ));
    EXPECT_TRUE(ContainsText(system, "const usize shadowEffectsTimingTicketCount = hardwareShadowSupported"));
    EXPECT_FALSE(ContainsText(system, "s_SoftwareShadowEffectsPacketCount"));
    EXPECT_FALSE(ContainsText(system, "shadowEffectsPacketRange.packetCount == shadowEffectsTimingTicketCount"));
}


// Snapshot Copy and the timed Surfel GI endpoint must retain distinct packets, but Preparation may alias/share
// Snapshot and compiler-owned untimed packets may appear inside the semantic task range.
TEST(EcsGraphics, SurfelGiTopologyUsesSemanticTaskAnchors){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    AString surfelGiSource;
    ASSERT_TRUE(ReadRendererSystemSources(repoRoot, systemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "task_graph_surfel_gi.cpp", surfelGiSource));
    const AStringView system(systemSource.data(), systemSource.size());
    const AStringView surfelGi(surfelGiSource.data(), surfelGiSource.size());

    EXPECT_TRUE(ContainsText(
        system,
        "packetRangeForTasks(surfelGiFirstTask, m_deferredSurfelGiTask)"
    ));
    EXPECT_TRUE(ContainsText(
        system,
        "const bool surfelGiSnapshotCopyAndTimingPacketsAreDistinct =\n"
        "        !m_deferredSurfelGiSnapshotCopyTask.valid()\n"
        "        || !m_deferredLightingCompiledGraph.tasksSharePacket(\n"
        "            m_deferredSurfelGiSnapshotCopyTask,\n"
        "            m_deferredSurfelGiTask"
    ));
    EXPECT_EQ(CountText(system, "surfelGiSnapshotCopyAndTimingPacketsAreDistinct"), 3u);
    EXPECT_TRUE(ContainsText(system, "|| !surfelGiSnapshotCopyAndTimingPacketsAreDistinct"));
    EXPECT_TRUE(ContainsText(system, "&& surfelGiSnapshotCopyAndTimingPacketsAreDistinct"));
    EXPECT_TRUE(ContainsText(system, "surfelGiSubmitter.submitTaskRangeInCompileOrderFromTasks("));
    EXPECT_TRUE(ContainsText(system, "surfelGiFirstTask,\n                m_deferredSurfelGiTask,"));

    EXPECT_TRUE(ContainsText(
        surfelGi,
        "if(!m_deferredSurfelGiPreparationTask.valid())\n"
        "            m_deferredSurfelGiPreparationTask = m_deferredSurfelGiSnapshotCopyTask;"
    ));
    EXPECT_TRUE(ContainsText(system, "|| !surfelGiPreparedPrefixMergedIntoGiPacket"));
    EXPECT_TRUE(ContainsText(system, "|| !surfelGiInitializationLifecycleMergedIntoPreparationPacket"));
    EXPECT_FALSE(ContainsText(
        system,
        "tasksSharePacket(\n"
        "            m_deferredSurfelGiPreparationTask,\n"
        "            m_deferredSurfelGiSnapshotCopyTask"
    ));
    EXPECT_FALSE(ContainsText(system, "s_SurfelGiMergedPreparationAndCopyPacketCount"));
    EXPECT_FALSE(ContainsText(system, "s_SurfelGiSeparatePreparationAndCopyPacketCount"));
    EXPECT_FALSE(ContainsText(system, "expectedSurfelGiPacketCount"));
    EXPECT_FALSE(ContainsText(system, "surfelGiPacketRange.packetCount =="));
    EXPECT_FALSE(ContainsText(system, "surfelGiPacketRange.packetCount !="));
}


// AVBOIT validation follows semantic stage anchors and accepts the compiler-owned packet range between them. It
// must not constrain that range to the currently generated one-packet or five-packet topology.
TEST(EcsGraphics, AvboitTopologyUsesSemanticTaskAnchors){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemHeaderSource;
    AString systemSource;
    AString avboitSystemHeaderSource;
    AString avboitStageHeaderSource;
    AString avboitValidationSource;
    AString avboitSubmissionSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "system.h", systemHeaderSource));
    ASSERT_TRUE(ReadRendererSystemSources(repoRoot, systemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "avboit" / "avboit_system.h", avboitSystemHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "avboit" / "task_graph_stage.h", avboitStageHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "avboit" / "task_graph_stage_validation.cpp", avboitValidationSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "avboit" / "task_graph_stage_submission.cpp", avboitSubmissionSource));
    const AStringView systemHeader(systemHeaderSource.data(), systemHeaderSource.size());
    const AStringView system(systemSource.data(), systemSource.size());
    const AStringView avboitSystemHeader(avboitSystemHeaderSource.data(), avboitSystemHeaderSource.size());
    const AStringView avboitStageHeader(avboitStageHeaderSource.data(), avboitStageHeaderSource.size());
    const AStringView avboitValidation(avboitValidationSource.data(), avboitValidationSource.size());
    const AStringView avboitSubmission(avboitSubmissionSource.data(), avboitSubmissionSource.size());

    EXPECT_TRUE(ContainsText(avboitSystemHeader, "RendererAvboitTaskGraphValidation validateTaskGraphStage("));
    EXPECT_TRUE(ContainsText(avboitSystemHeader, "RendererAvboitTaskGraphSubmission submitTaskGraphStage("));
    EXPECT_TRUE(ContainsText(avboitStageHeader, "struct RendererAvboitTaskGraphValidation"));
    EXPECT_TRUE(ContainsText(avboitStageHeader, "RendererTaskGraphTransparencyStage m_stage;"));
    EXPECT_TRUE(ContainsText(avboitStageHeader, "Core::GpuTaskId m_submissionCompletionTask;"));
    EXPECT_TRUE(ContainsText(avboitValidation, "taskIsCompiled(taskGraphStage.m_preTask)"));
    EXPECT_TRUE(ContainsText(avboitValidation, "taskIsCompiled(taskGraphStage.m_depthWarpTask)"));
    EXPECT_TRUE(ContainsText(avboitValidation, "taskIsCompiled(taskGraphStage.m_extinctionTask)"));
    EXPECT_TRUE(ContainsText(avboitValidation, "taskIsCompiled(taskGraphStage.m_integrationTask)"));
    EXPECT_TRUE(ContainsText(avboitValidation, "taskIsCompiled(taskGraphStage.m_accumulationTask)"));
    EXPECT_TRUE(ContainsText(avboitValidation, "compiledGraph.tasksSharePacket(\n            taskGraphStage.m_preTask"));
    EXPECT_TRUE(ContainsText(avboitValidation, "compiledGraph.packetRangeForTasks("));
    EXPECT_TRUE(ContainsText(avboitValidation, "compiledGraph.validPacketRange(packetRange)"));
    EXPECT_FALSE(ContainsText(avboitValidation, "s_AsyncComputePacketCount"));
    EXPECT_FALSE(ContainsText(avboitValidation, "s_SinglePacketCount"));
    EXPECT_FALSE(ContainsText(avboitValidation, "expectedPacketCount"));
    EXPECT_FALSE(ContainsText(avboitValidation, "packetRange.packetCount =="));
    EXPECT_TRUE(ContainsText(avboitSubmission, "submitTaskRangeInCompileOrderFromTasks("));
    EXPECT_TRUE(ContainsText(avboitSubmission, "validation.stage().firstTask,"));
    EXPECT_TRUE(ContainsText(avboitSubmission, "validation.submissionCompletionTask(),"));
    EXPECT_TRUE(ContainsText(system, "m_avboitSystem.validateTaskGraphStage("));
    EXPECT_TRUE(ContainsText(system, "m_avboitSystem.submitTaskGraphStage("));
    EXPECT_TRUE(ContainsText(
        system,
        "if(m_deferredLightingTaskGraphValid){\n"
        "            RendererAvboitTaskGraphSubmitContext avboitSubmitContext"
    ));
    EXPECT_FALSE(ContainsText(systemHeader, "m_deferredAvboit"));
    EXPECT_FALSE(ContainsText(system, "m_deferredAvboit"));
    EXPECT_FALSE(ContainsText(system, "s_AvboitAsyncComputePacketCount"));

    for(const AStringView avboitSource : { avboitValidation, avboitSubmission }){
        EXPECT_FALSE(ContainsText(avboitSource, "GpuSubmissionPacketId avboitPrePacket"));
        EXPECT_FALSE(ContainsText(avboitSource, "GpuSubmissionPacketId avboitDepthWarpPacket"));
        EXPECT_FALSE(ContainsText(avboitSource, "GpuSubmissionPacketId avboitExtinctionPacket"));
        EXPECT_FALSE(ContainsText(avboitSource, "GpuSubmissionPacketId avboitIntegrationPacket"));
        EXPECT_FALSE(ContainsText(avboitSource, "GpuSubmissionPacketId avboitAccumulationPacket"));
    }
}


// Lighting and Composite own distinct timing tickets, but their semantic range may contain untimed compiler
// packets. Preserve the non-merge requirement without constraining the inclusive range to exactly two packets.
TEST(EcsGraphics, DeferredLightingCompositeTopologyUsesSemanticTaskAnchors){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    ASSERT_TRUE(ReadRendererSystemSources(repoRoot, systemSource));
    const AStringView system(systemSource.data(), systemSource.size());

    EXPECT_TRUE(ContainsText(
        system,
        "packetRangeForTasks(m_deferredLightingTask, m_deferredCompositeTask)"
    ));
    EXPECT_TRUE(ContainsText(
        system,
        "const bool deferredLightingCompositeTimingPacketsAreDistinct = !m_deferredLightingCompiledGraph.tasksSharePacket("
    ));
    EXPECT_TRUE(ContainsText(system, "&& deferredLightingCompositeTimingPacketsAreDistinct"));
    EXPECT_TRUE(ContainsText(system, "deferredSubmitter.submitTaskRangeInCompileOrderFromTasks("));
    EXPECT_TRUE(ContainsText(system, "m_deferredLightingTask,\n                m_deferredCompositeTask,"));
    EXPECT_FALSE(ContainsText(system, "s_DeferredLightingCompositePacketCount"));
    EXPECT_FALSE(ContainsText(system, "deferredLightingCompositePacketRange.packetCount =="));
    EXPECT_FALSE(ContainsText(system, "deferredLightingCompositePacketRange.packetCount\n            !="));
}


// The exact terminal packet is retained solely in compiler-owned presentation metadata for the swap-chain binary
// signal. Every other normal renderer readiness check uses a declared task anchor or a semantic task range.
TEST(EcsGraphics, OnlyTerminalPresentationRetainsAPacketIdentity){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    ASSERT_TRUE(ReadRendererSystemSources(repoRoot, systemSource));
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
    ASSERT_TRUE(ReadRendererSystemSources(repoRoot, systemSource));
    ASSERT_TRUE(ReadRendererSources(
        repoRoot,
        {
            "kernel/task_graph_frame_recovery_task.h",
            "kernel/task_graph_frame_recovery_task.cpp",
            "raytrace/task_graph_shadow_prepare_tasks.h",
            "raytrace/task_graph_shadow_prepare_tasks.cpp",
            "mesh/task_graph_prefix_tasks.h",
            "mesh/task_graph_prefix_tasks.cpp",
            "deferred/task_graph_present_task.h",
            "deferred/task_graph_present_task.cpp",
            "kernel/task_graph_frame_timing_end_task.h",
            "kernel/task_graph_frame_timing_end_task.cpp",
            "deferred/task_graph_deferred_lighting.cpp",
        },
        taskGraphSource
    ));
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
    EXPECT_TRUE(ContainsText(recovery, "confirmEndSubmission(token, false)"));
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
    EXPECT_TRUE(ContainsText(shadowPrepareAcceptance, "context->frameTimingTransaction->confirmBeginSubmission(token)"));
    EXPECT_TRUE(ContainsText(shadowPrepareAcceptance, ".task = m_deferredShadowPrepareTask,"));
    EXPECT_TRUE(ContainsText(shadowPrepareAcceptance, ".invoke = acceptShadowPrepareTask,"));
    EXPECT_FALSE(ContainsText(system, "acceptGraphicsPrefixBeginTask"));
}


// One typed CPU snapshot binds an acquired WSI image to its exact owning framebuffer. Graphics publishes it only
// after attachment identity validation, clears it on every lifecycle boundary, and never asks mutable backend
// current-image state which framebuffer should render.
TEST(EcsGraphics, PresentationAcquisitionPublishesOneValidatedSnapshot){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString graphicsHeaderSource;
    AString graphicsSource;
    AString backendContractSource;
    AString backendOrchestrationSource;
    AString rendererResourcesSource;
    AString uiSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "module.h", graphicsHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "module.cpp", graphicsSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "backend_contract.h", backendContractSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "vulkan" / "backend_context_orchestration.cpp", backendOrchestrationSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "system_resources.cpp", rendererResourcesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "system.cpp", uiSource));
    const AStringView graphicsHeader(graphicsHeaderSource.data(), graphicsHeaderSource.size());
    const AStringView graphics(graphicsSource.data(), graphicsSource.size());
    const AStringView backendContract(backendContractSource.data(), backendContractSource.size());
    const AStringView backendOrchestration(backendOrchestrationSource.data(), backendOrchestrationSource.size());
    const AStringView rendererResources(rendererResourcesSource.data(), rendererResourcesSource.size());
    const AStringView ui(uiSource.data(), uiSource.size());

    EXPECT_TRUE(ContainsText(graphicsHeader, "const AcquiredPresentationFrame& acquiredPresentationFrame()const noexcept"));
    EXPECT_TRUE(ContainsText(graphicsHeader, "AcquiredPresentationFrame m_acquiredPresentationFrame;"));
    EXPECT_FALSE(ContainsText(graphicsHeader, "getCurrentBackBuffer"));
    EXPECT_FALSE(ContainsText(graphicsHeader, "getCurrentBackBufferIndex"));
    EXPECT_FALSE(ContainsText(graphicsHeader, "getCurrentFramebuffer"));
    EXPECT_TRUE(ContainsText(backendContract, "{ backend.abandonAcquiredFrame() }->SameAs<bool>;"));

    const usize abandonmentOffset = backendOrchestration.find("bool BackendContext::abandonAcquiredFrame()noexcept{");
    const usize presentDefinitionOffset = backendOrchestration.find("bool BackendContext::present(){", abandonmentOffset);
    ASSERT_NE(abandonmentOffset, AStringView::npos);
    ASSERT_NE(presentDefinitionOffset, AStringView::npos);
    const AStringView abandonment = backendOrchestration.substr(
        abandonmentOffset,
        presentDefinitionOffset - abandonmentOffset
    );
    const usize quarantineOffset = abandonment.find("m_swapChainIndex = Limit<u32>::s_Max;");
    const usize signalIdleOffset = abandonment.find("presentationSignalNeedsIdle && !m_rhiDevice->waitForIdle()");
    const usize signalResetOffset = abandonment.find("resetFramePresentationSignal();", signalIdleOffset);
    const usize forceSubmitOffset = abandonment.find("drainSubmitDesc.forceNativeSubmission = true;");
    const usize drainSubmitOffset = abandonment.find("m_rhiDevice->executeCommandLists(nullptr, 0u, primaryGraphicsQueue, drainSubmitDesc)");
    const usize idleOffset = abandonment.find("m_rhiDevice->waitForIdle()", drainSubmitOffset);
    ASSERT_NE(quarantineOffset, AStringView::npos);
    ASSERT_NE(signalIdleOffset, AStringView::npos);
    ASSERT_NE(signalResetOffset, AStringView::npos);
    ASSERT_NE(forceSubmitOffset, AStringView::npos);
    ASSERT_NE(drainSubmitOffset, AStringView::npos);
    ASSERT_NE(idleOffset, AStringView::npos);
    EXPECT_LT(quarantineOffset, signalIdleOffset);
    EXPECT_LT(signalIdleOffset, signalResetOffset);
    EXPECT_LT(signalResetOffset, forceSubmitOffset);
    EXPECT_LT(forceSubmitOffset, drainSubmitOffset);
    EXPECT_LT(drainSubmitOffset, idleOffset);
    EXPECT_TRUE(ContainsText(abandonment, "if(!m_frameAcquired)\n        return true;"));
    EXPECT_TRUE(ContainsText(abandonment, "if(m_frameAbandonmentComplete){"));
    EXPECT_TRUE(ContainsText(abandonment, "replaceFramePresentationSemaphoreAfterIdle()"));
    EXPECT_TRUE(ContainsText(abandonment, "m_rhiDevice->captureGpuCrash(\"acquired-frame abandonment drain\")"));
    EXPECT_TRUE(ContainsText(abandonment, "m_rhiDevice->captureGpuCrash(\"acquired-frame abandonment drain idle\")"));
    EXPECT_TRUE(ContainsText(abandonment, "m_frameAbandonmentComplete = true;"));
    EXPECT_FALSE(ContainsText(abandonment, "m_frameAcquired = false"));

    const AStringView present = backendOrchestration.substr(presentDefinitionOffset);
    const usize nonConsumedOffset = present.find(
        "presentWaitDisposition != VulkanDetail::QueuePresentWaitDisposition::Consumed"
    );
    const usize deviceLostOffset = present.find(
        "presentWaitDisposition == VulkanDetail::QueuePresentWaitDisposition::DeviceLost",
        nonConsumedOffset
    );
    const usize unconsumedIdleOffset = present.find("if(!m_rhiDevice->waitForIdle())", deviceLostOffset);
    const usize idleFailureCaptureOffset = present.find("captureGpuCrash(\"present semaphore idle\")", unconsumedIdleOffset);
    const usize replacementOffset = present.find("if(!replaceFramePresentationSemaphoreAfterIdle())", idleFailureCaptureOffset);
    const usize replacementFailureCaptureOffset = present.find(
        "captureGpuCrash(\"present semaphore replacement\")",
        replacementOffset
    );
    const usize unconsumedResetOffset = present.find("resetFramePresentationSignal();", replacementFailureCaptureOffset);
    ASSERT_NE(nonConsumedOffset, AStringView::npos);
    ASSERT_NE(deviceLostOffset, AStringView::npos);
    ASSERT_NE(unconsumedIdleOffset, AStringView::npos);
    ASSERT_NE(idleFailureCaptureOffset, AStringView::npos);
    ASSERT_NE(replacementOffset, AStringView::npos);
    ASSERT_NE(replacementFailureCaptureOffset, AStringView::npos);
    ASSERT_NE(unconsumedResetOffset, AStringView::npos);
    EXPECT_LT(nonConsumedOffset, deviceLostOffset);
    EXPECT_LT(deviceLostOffset, unconsumedIdleOffset);
    EXPECT_LT(unconsumedIdleOffset, idleFailureCaptureOffset);
    EXPECT_LT(idleFailureCaptureOffset, replacementOffset);
    EXPECT_LT(replacementOffset, replacementFailureCaptureOffset);
    EXPECT_LT(replacementFailureCaptureOffset, unconsumedResetOffset);
    EXPECT_TRUE(ContainsText(present, "if(!frameSignalAccepted || !m_rhiDevice){"));
    EXPECT_FALSE(ContainsText(
        present.substr(nonConsumedOffset, replacementFailureCaptureOffset - nonConsumedOffset),
        "resetFramePresentationSignal();"
    ));

    const usize renderOffset = graphics.find("void Graphics::render(){");
    const usize averageOffset = graphics.find("void Graphics::updateAverageFrameTime", renderOffset);
    ASSERT_NE(renderOffset, AStringView::npos);
    ASSERT_NE(averageOffset, AStringView::npos);
    const AStringView render = graphics.substr(renderOffset, averageOffset - renderOffset);
    EXPECT_TRUE(ContainsText(render, "Framebuffer* const framebuffer = m_acquiredPresentationFrame.framebuffer.get();"));
    EXPECT_FALSE(ContainsText(render, "getCurrent"));

    const usize animateOffset = graphics.find("bool Graphics::animateRenderPresentInternal");
    ASSERT_NE(animateOffset, AStringView::npos);
    const AStringView animate = graphics.substr(animateOffset);
    const usize entryClearOffset = animate.find("m_acquiredPresentationFrame = {};");
    const usize acquireOffset = animate.find("AcquiredBackBuffer acquiredBackBuffer = m_backend->beginFrame(resizeCallbacks);");
    const usize identityOffset = animate.find("acquiredFramebufferDesc.colorAttachments[0].texture != acquiredBackBuffer.texture.get()");
    const usize publishOffset = animate.find("m_acquiredPresentationFrame = {", acquireOffset);
    const usize resetOffset = animate.find("ScopedAcquiredPresentationFrameReset acquiredFrameReset", publishOffset);
    const usize preambleOffset = animate.find("prepareFramePreamble()", resetOffset);
    ASSERT_NE(entryClearOffset, AStringView::npos);
    ASSERT_NE(acquireOffset, AStringView::npos);
    ASSERT_NE(identityOffset, AStringView::npos);
    ASSERT_NE(publishOffset, AStringView::npos);
    ASSERT_NE(resetOffset, AStringView::npos);
    ASSERT_NE(preambleOffset, AStringView::npos);
    EXPECT_LT(entryClearOffset, acquireOffset);
    EXPECT_LT(acquireOffset, identityOffset);
    EXPECT_LT(identityOffset, publishOffset);
    EXPECT_LT(publishOffset, resetOffset);
    EXPECT_LT(resetOffset, preambleOffset);
    EXPECT_TRUE(ContainsText(animate, "acquiredFramebufferDesc.colorAttachments.size() != 1u"));
    const usize missingFramebufferWarningOffset = animate.find("acquired swap-chain image has no matching framebuffer");
    const usize missingFramebufferAbandonOffset = animate.find("m_backend->abandonAcquiredFrame()", missingFramebufferWarningOffset);
    const usize missingFramebufferRecreationOffset = animate.find("requestDeviceRecreation()", missingFramebufferAbandonOffset);
    const usize attachmentWarningOffset = animate.find("acquired swap-chain image mismatches its framebuffer attachment");
    const usize attachmentAbandonOffset = animate.find("m_backend->abandonAcquiredFrame()", attachmentWarningOffset);
    const usize attachmentRecreationOffset = animate.find("requestDeviceRecreation()", attachmentAbandonOffset);
    ASSERT_NE(missingFramebufferWarningOffset, AStringView::npos);
    ASSERT_NE(missingFramebufferAbandonOffset, AStringView::npos);
    ASSERT_NE(missingFramebufferRecreationOffset, AStringView::npos);
    ASSERT_NE(attachmentWarningOffset, AStringView::npos);
    ASSERT_NE(attachmentAbandonOffset, AStringView::npos);
    ASSERT_NE(attachmentRecreationOffset, AStringView::npos);
    EXPECT_LT(missingFramebufferWarningOffset, missingFramebufferAbandonOffset);
    EXPECT_LT(missingFramebufferAbandonOffset, missingFramebufferRecreationOffset);
    EXPECT_LT(missingFramebufferRecreationOffset, attachmentWarningOffset);
    EXPECT_LT(attachmentWarningOffset, attachmentAbandonOffset);
    EXPECT_LT(attachmentAbandonOffset, attachmentRecreationOffset);
    EXPECT_LT(attachmentRecreationOffset, publishOffset);
    const usize renderCallOffset = animate.find("render();", preambleOffset);
    const usize postRenderExitOffset = animate.find("if(m_deviceRecreationRequested || device.isDeviceLost()){", renderCallOffset);
    const usize postRenderAbandonOffset = animate.find("else if(!m_backend->abandonAcquiredFrame())", postRenderExitOffset);
    const usize presentCallOffset = animate.find("const bool presented = m_backend->present();", postRenderAbandonOffset);
    const usize presentFailureOffset = animate.find("if(!presented){", presentCallOffset);
    const usize presentAbandonOffset = animate.find("!device.isDeviceLost() && !m_backend->abandonAcquiredFrame()", presentFailureOffset);
    ASSERT_NE(renderCallOffset, AStringView::npos);
    ASSERT_NE(postRenderExitOffset, AStringView::npos);
    ASSERT_NE(postRenderAbandonOffset, AStringView::npos);
    ASSERT_NE(presentCallOffset, AStringView::npos);
    ASSERT_NE(presentFailureOffset, AStringView::npos);
    ASSERT_NE(presentAbandonOffset, AStringView::npos);
    EXPECT_LT(renderCallOffset, postRenderExitOffset);
    EXPECT_LT(postRenderExitOffset, postRenderAbandonOffset);
    EXPECT_LT(postRenderAbandonOffset, presentCallOffset);
    EXPECT_LT(presentCallOffset, presentFailureOffset);
    EXPECT_LT(presentFailureOffset, presentAbandonOffset);
    EXPECT_EQ(CountText(animate, "m_backend->abandonAcquiredFrame()"), 4u);
    EXPECT_TRUE(ContainsText(animate, "prepareFramePreamble() returns false only after terminal device loss"));
    EXPECT_TRUE(ContainsText(animate, "required device teardown owns the unresolved acquired image and synchronization"));
    EXPECT_TRUE(ContainsText(graphics, "~ScopedAcquiredPresentationFrameReset(){ m_frame = {}; }"));
    EXPECT_TRUE(ContainsText(graphics, "bool Graphics::init(const Common::FrameData& data){\n    m_acquiredPresentationFrame = {};"));
    EXPECT_TRUE(ContainsText(graphics, "bool Graphics::createHeadlessDevice(){\n    m_acquiredPresentationFrame = {};"));
    EXPECT_TRUE(ContainsText(graphics, "void Graphics::destroy(){\n    m_acquiredPresentationFrame = {};"));
    EXPECT_TRUE(ContainsText(graphics, "void Graphics::backBufferResizing(){\n    m_acquiredPresentationFrame = {};"));

    for(const AStringView setupSource : { rendererResources, ui }){
        EXPECT_TRUE(ContainsText(setupSource, "Pipeline compatibility setup uses the stable framebuffer-zero prototype"));
        EXPECT_TRUE(ContainsText(setupSource, "m_graphics.getFramebuffer(0u)"));
        EXPECT_FALSE(ContainsText(setupSource, "getCurrentFramebuffer"));
    }
}


// Renderer presentation must import the exact acquired swap-chain texture, preserve its captured native origin,
// and own the RenderTarget-to-Present state closure. The late record callback revalidates both graph identity and
// framebuffer attachment identity before touching the image.
TEST(EcsGraphics, RendererPresentationGraphBindsExactAcquiredTexture){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString contributorHeaderSource;
    AString rendererHeaderSource;
    AString rendererResourcesSource;
    AString rendererSource;
    AString presentationBuildSource;
    AString presentationTaskHeaderSource;
    AString presentationTaskSource;
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "task_graph" / "presentation_contributor.h",
        contributorHeaderSource
    ));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "system.h", rendererHeaderSource));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / "kernel" / "system_resources.cpp",
        rendererResourcesSource
    ));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "system_render.cpp", rendererSource));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / "deferred" / "task_graph_deferred_lighting.cpp",
        presentationBuildSource
    ));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / "deferred" / "task_graph_present_task.h",
        presentationTaskHeaderSource
    ));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / "deferred" / "task_graph_present_task.cpp",
        presentationTaskSource
    ));
    const AStringView contributorHeader(contributorHeaderSource.data(), contributorHeaderSource.size());
    const AStringView rendererHeader(rendererHeaderSource.data(), rendererHeaderSource.size());
    const AStringView rendererResources(rendererResourcesSource.data(), rendererResourcesSource.size());
    const AStringView renderer(rendererSource.data(), rendererSource.size());
    const AStringView presentationBuild(presentationBuildSource.data(), presentationBuildSource.size());
    const AStringView presentationTaskHeader(presentationTaskHeaderSource.data(), presentationTaskHeaderSource.size());
    const AStringView presentationTask(presentationTaskSource.data(), presentationTaskSource.size());

    EXPECT_TRUE(ContainsText(
        contributorHeader,
        "prepareTaskGraphPresentation(const AcquiredPresentationFrame& frame)"
    ));
    EXPECT_TRUE(ContainsText(
        contributorHeader,
        "const AcquiredPresentationFrame& frame,\n"
        "        GpuGraphResourceId backbuffer,"
    ));
    EXPECT_TRUE(ContainsText(contributorHeader, "exact typed acquired texture imported by"));
    EXPECT_FALSE(ContainsText(contributorHeader, "presentation hazard domain"));
    EXPECT_TRUE(ContainsText(rendererHeader, "const Core::AcquiredPresentationFrame& presentationFrame,"));
    EXPECT_TRUE(ContainsText(
        rendererResources,
        "else if(m_graphics.isDeviceRecreationRequested()){\n"
        "            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT(\"RendererSystem: presentation contributor requested recreation during preparation\"));\n"
        "            return false;"
    ));

    EXPECT_FALSE(ContainsText(
        presentationBuild,
        "HazardDomainDesc(Name(\"render.deferred_present.backbuffer\")"
    ));
    EXPECT_TRUE(ContainsText(
        presentationBuild,
        ".setInitialState(presentationFrame.backBuffer.nativeInitialState)\n"
        "        .setExternalFinalState(Core::ResourceStates::Present)"
    ));
    EXPECT_TRUE(ContainsText(
        presentationBuild,
        "const Core::GpuGraphResourceId backbuffer = m_deferredLightingTaskGraph.importTexture(\n"
        "        presentationFrame.backBuffer.texture,"
    ));
    EXPECT_TRUE(ContainsText(
        presentationBuild,
        "WriteTextureUse(\n"
        "            backbuffer,\n"
        "            presentationFramebufferDesc.colorAttachments[0].subresources,\n"
        "            Core::ResourceStates::RenderTarget"
    ));
    EXPECT_TRUE(ContainsText(presentationBuild, ".presentationFrame = presentationFrame,"));
    EXPECT_TRUE(ContainsText(presentationBuild, ".backBuffer = backbuffer,"));
    EXPECT_TRUE(ContainsText(
        presentationBuild,
        "declarePresentEndpoint(Core::GpuPresentEndpoint{\n"
        "        .producer = m_deferredFrameTimingEndTask,\n"
        "        .backBuffer = backbuffer,"
    ));

    EXPECT_TRUE(ContainsText(presentationTaskHeader, "Core::AcquiredPresentationFrame presentationFrame;"));
    EXPECT_TRUE(ContainsText(presentationTaskHeader, "Core::GpuGraphResourceId backBuffer;"));
    EXPECT_TRUE(ContainsText(presentationTask, "!payload.presentationFrame.valid()"));
    EXPECT_TRUE(ContainsText(
        presentationTask,
        "presentationFramebufferDesc.colorAttachments[0].texture != payload.presentationFrame.backBuffer.texture.get()"
    ));
    EXPECT_TRUE(ContainsText(
        presentationTask,
        "context.taskGraph.textureForResource(payload.backBuffer) != payload.presentationFrame.backBuffer.texture.get()"
    ));
    EXPECT_TRUE(ContainsText(
        presentationTask,
        "payload.deferredSystem->renderDeferredPresent(\n"
        "        commandList,\n"
        "        *payload.targets,\n"
        "        payload.presentationFrame"
    ));

    EXPECT_TRUE(ContainsText(
        renderer,
        "const Core::AcquiredPresentationFrame presentationFrame = m_graphics.acquiredPresentationFrame();"
    ));
    EXPECT_TRUE(ContainsText(renderer, "presentationFrame.framebuffer.get() != framebuffer"));
    EXPECT_TRUE(ContainsText(
        renderer,
        "presentationFramebufferDesc.colorAttachments[0].texture != presentationFrame.backBuffer.texture.get()"
    ));

    // Once the exact back-buffer writer accepted, generic suffix recovery cannot make the acquired image reusable.
    // The renderer must escalate that partial-acceptance edge to Graphics' recreation/abandonment path.
    const usize partialAcceptanceOffset = renderer.find(
        "const bool presentationBackBufferWriteAccepted = m_deferredLightingSubmissionTransaction.taskToken("
    );
    const usize acceptedWriterOffset = renderer.find("m_deferredPresentTask", partialAcceptanceOffset);
    const usize recreationOffset = renderer.find("if(presentationBackBufferWriteAccepted){", acceptedWriterOffset);
    const usize recoveryFailureOffset = renderer.find("failFrameRenderRecovery();", recreationOffset);
    ASSERT_NE(partialAcceptanceOffset, AStringView::npos);
    ASSERT_NE(acceptedWriterOffset, AStringView::npos);
    ASSERT_NE(recreationOffset, AStringView::npos);
    ASSERT_NE(recoveryFailureOffset, AStringView::npos);
    EXPECT_LT(partialAcceptanceOffset, acceptedWriterOffset);
    EXPECT_LT(acceptedWriterOffset, recreationOffset);
    EXPECT_LT(recreationOffset, recoveryFailureOffset);
    EXPECT_TRUE(ContainsText(
        renderer.substr(recreationOffset, recoveryFailureOffset - recreationOffset),
        "acquired back buffer was written before presentation suffix rejection; requesting recreation"
    ));
}


// Every UI presentation route consumes the same acquired-frame identity. Normal overlay work reuses the renderer's
// typed resource, standalone raster paths import that exact TextureHandle, and upload-only standalone work skips the
// external-final texture import entirely so it cannot manufacture an unwritten presentation endpoint.
TEST(EcsGraphics, UiPresentationGraphsBindExactAcquiredTexture){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString uiHeaderSource;
    AString uiSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "system.h", uiHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "system.cpp", uiSource));
    const AStringView uiHeader(uiHeaderSource.data(), uiHeaderSource.size());
    const AStringView ui(uiSource.data(), uiSource.size());

    EXPECT_TRUE(ContainsText(
        uiHeader,
        "virtual bool prepareTaskGraphPresentation(const Core::AcquiredPresentationFrame& frame)override;"
    ));
    EXPECT_TRUE(ContainsText(
        uiHeader,
        "const Core::AcquiredPresentationFrame& frame,\n"
        "        Core::GpuGraphResourceId backbuffer,"
    ));
    EXPECT_TRUE(ContainsText(uiHeader, "Core::AcquiredPresentationFrame m_taskGraphPresentationFrame;"));
    EXPECT_TRUE(ContainsText(
        ui,
        "framebufferDesc.colorAttachments[0].texture == frame.backBuffer.texture.get()"
    ));
    EXPECT_TRUE(ContainsText(
        ui,
        "graph.textureForResource(backbuffer) == frame.backBuffer.texture.get()"
    ));
    EXPECT_TRUE(ContainsText(ui, "PresentationBackBufferResourceDesc("));
    EXPECT_TRUE(ContainsText(ui, ".setType(Core::GpuGraphResourceType::Texture)"));
    EXPECT_TRUE(ContainsText(ui, ".setInitialState(frame.backBuffer.nativeInitialState)"));
    EXPECT_TRUE(ContainsText(ui, ".setExternalFinalState(Core::ResourceStates::Present)"));

    const usize renderTaskOffset = ui.find("struct UiSystem::TaskGraphRenderTask{");
    const usize uploadCompletionOffset = ui.find("struct UiSystem::TaskGraphUploadCompletionTask{", renderTaskOffset);
    const usize legacyTaskOffset = ui.find("struct UiSystem::StandaloneLegacyPresentationTask{");
    const usize nextTaskOffset = ui.find("UiSystem::UiSystem(", legacyTaskOffset);
    ASSERT_NE(renderTaskOffset, AStringView::npos);
    ASSERT_NE(uploadCompletionOffset, AStringView::npos);
    ASSERT_NE(legacyTaskOffset, AStringView::npos);
    ASSERT_NE(nextTaskOffset, AStringView::npos);
    const AStringView renderTask = ui.substr(renderTaskOffset, uploadCompletionOffset - renderTaskOffset);
    const AStringView legacyTask = ui.substr(legacyTaskOffset, nextTaskOffset - legacyTaskOffset);
    for(const AStringView task : { renderTask, legacyTask }){
        EXPECT_TRUE(ContainsText(task, "Core::AcquiredPresentationFrame frame;"));
        EXPECT_TRUE(ContainsText(task, "Core::GpuGraphResourceId backbuffer;"));
    }
    EXPECT_TRUE(ContainsText(renderTask, "payload.backbuffer,\n            context"));
    EXPECT_TRUE(ContainsText(legacyTask, "payload.backbuffer,"));
    EXPECT_TRUE(ContainsText(legacyTask, "context"));

    const usize declarationOffset = ui.find("Core::GpuTaskId UiSystem::declareTaskGraphPresentation");
    const usize standaloneOffset = ui.find("bool UiSystem::submitStandaloneTaskGraphPresentation", declarationOffset);
    const usize legacyDeclarationOffset = ui.find(
        "Core::GpuTaskId UiSystem::declareStandaloneLegacyTaskGraphPresentation",
        standaloneOffset
    );
    const usize legacySubmitOffset = ui.find(
        "bool UiSystem::submitStandaloneLegacyTaskGraphPresentation",
        legacyDeclarationOffset
    );
    const usize uploadGraphOffset = ui.find(
        "Core::GpuTaskId UiSystem::declareStandaloneTextureUploadGraph",
        legacySubmitOffset
    );
    ASSERT_NE(declarationOffset, AStringView::npos);
    ASSERT_NE(standaloneOffset, AStringView::npos);
    ASSERT_NE(legacyDeclarationOffset, AStringView::npos);
    ASSERT_NE(legacySubmitOffset, AStringView::npos);
    ASSERT_NE(uploadGraphOffset, AStringView::npos);
    const AStringView declaration = ui.substr(declarationOffset, standaloneOffset - declarationOffset);
    const AStringView standalone = ui.substr(standaloneOffset, legacyDeclarationOffset - standaloneOffset);
    const AStringView legacyDeclaration = ui.substr(
        legacyDeclarationOffset,
        legacySubmitOffset - legacyDeclarationOffset
    );
    const AStringView legacySubmit = ui.substr(legacySubmitOffset, uploadGraphOffset - legacySubmitOffset);

    EXPECT_TRUE(ContainsText(
        declaration,
        "GraphBindsAcquiredPresentationTexture(graph, frame, backbuffer)"
    ));
    EXPECT_TRUE(ContainsText(
        declaration,
        ".resource = backbuffer,\n"
        "            .range = {},\n"
        "            // Rasterization writes the exact renderer-owned presentation texture."
    ));
    EXPECT_TRUE(ContainsText(declaration, ".requiredState = Core::ResourceStates::RenderTarget,"));
    EXPECT_TRUE(ContainsText(declaration, ".frame = frame,"));
    EXPECT_TRUE(ContainsText(declaration, ".backbuffer = backbuffer,"));

    const usize drawBranchOffset = standalone.find("context->ui->m_taskGraphDrawUploadsPrepared");
    const usize standaloneImportOffset = standalone.find("backbuffer = graph.importTexture(", drawBranchOffset);
    const usize standaloneDeclareOffset = standalone.find(
        "return context->ui->declareTaskGraphPresentation(",
        standaloneImportOffset
    );
    ASSERT_NE(drawBranchOffset, AStringView::npos);
    ASSERT_NE(standaloneImportOffset, AStringView::npos);
    ASSERT_NE(standaloneDeclareOffset, AStringView::npos);
    EXPECT_LT(drawBranchOffset, standaloneImportOffset);
    EXPECT_LT(standaloneImportOffset, standaloneDeclareOffset);
    EXPECT_TRUE(ContainsText(standalone, "Core::GpuGraphResourceId backbuffer;"));
    EXPECT_TRUE(ContainsText(standalone, "context->frame.backBuffer.texture,"));
    EXPECT_TRUE(ContainsText(standalone, "PresentationBackBufferResourceDesc("));
    EXPECT_FALSE(ContainsText(standalone, "importHazardDomain"));

    EXPECT_TRUE(ContainsText(
        legacyDeclaration,
        "const Core::GpuGraphResourceId backbuffer = graph.importTexture(\n"
        "        frame.backBuffer.texture,"
    ));
    EXPECT_TRUE(ContainsText(legacyDeclaration, "PresentationBackBufferResourceDesc("));
    EXPECT_TRUE(ContainsText(legacyDeclaration, ".requiredState = Core::ResourceStates::RenderTarget,"));
    EXPECT_TRUE(ContainsText(
        legacyDeclaration,
        "GraphBindsAcquiredPresentationTexture(graph, frame, backbuffer)"
    ));
    EXPECT_EQ(CountText(legacyDeclaration, "importHazardDomain("), 1u);
    EXPECT_TRUE(ContainsText(legacyDeclaration, "ui.imgui_standalone_legacy_presentation.callback"));
    EXPECT_EQ(CountText(ui, ".setType(Core::GpuGraphResourceType::HazardDomain)"), 1u);

    EXPECT_TRUE(ContainsText(
        ui,
        "GraphBindsAcquiredPresentationTexture(context.taskGraph, frame, backbuffer)"
    ));
    EXPECT_EQ(
        CountText(ui, "failed after its terminal packet was accepted; requesting recreation"),
        2u
    );
    for(const AStringView submit : { standalone, legacySubmit }){
        const usize acceptedFailureOffset = submit.find("if(!m_frameFinished){");
        const usize recreationRequestOffset = submit.find("m_graphics.requestDeviceRecreation();", acceptedFailureOffset);
        ASSERT_NE(acceptedFailureOffset, AStringView::npos);
        ASSERT_NE(recreationRequestOffset, AStringView::npos);
        EXPECT_LT(acceptedFailureOffset, recreationRequestOffset);
    }
}


// Late recovery, readback, and history tasks own their record/submit/reject sequencing in the generic runtime.
// Keep the renderer limited to payload validation, timing arming, and device-recreation policy rather than
// reconstructing compiler packet ranges around every late tail.
TEST(EcsGraphics, LateGraphTailsUseRuntimeHelpers){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    ASSERT_TRUE(ReadRendererSystemSources(repoRoot, systemSource));
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


// The late history-copy packet prepares all three filtered return candidates while recording and commits them only
// from its exact accepted callback. Its task token remains the source of truth when that callback reports failure.
TEST(EcsGraphics, LaggedHistoryReturnCachesPublishOnlyOnTaskAcceptance){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    ASSERT_TRUE(ReadRendererSystemSources(repoRoot, systemSource));
    const AStringView system(systemSource.data(), systemSource.size());

    const usize acceptanceOffset = system.find("struct HistoryCopyAcceptanceContext{");
    const usize submitOffset = system.find("const bool historyCopyAccepted = submitter.recordAndSubmitTask(", acceptanceOffset);
    const usize tokenOffset = system.find("const Core::QueueSubmissionToken historyCopySubmissionToken =", submitOffset);
    ASSERT_NE(acceptanceOffset, AStringView::npos);
    ASSERT_NE(submitOffset, AStringView::npos);
    ASSERT_NE(tokenOffset, AStringView::npos);
    ASSERT_LT(acceptanceOffset, submitOffset);
    ASSERT_LT(submitOffset, tokenOffset);
    const AStringView acceptance = system.substr(acceptanceOffset, submitOffset - acceptanceOffset);
    EXPECT_EQ(CountText(acceptance, "buildFilteredResourceSubset("), 3u);
    EXPECT_EQ(CountText(acceptance, ".commit("), 3u);
    EXPECT_TRUE(ContainsText(acceptance, ".task = m_deferredLaggedLightingHistoryTask,"));
    EXPECT_TRUE(ContainsText(acceptance, ".context = &historyCopyAcceptance,"));
    EXPECT_TRUE(ContainsText(acceptance, ".invoke = acceptHistoryCopyFinalState,"));
    EXPECT_FALSE(ContainsText(acceptance, ".replaceTextureSubset("));

    const usize failureOffset = system.find(
        "if(historyCopySubmissionToken.valid() && (!historyCopyAccepted || !historyCopyAcceptance.acceptedStateReady))",
        tokenOffset
    );
    ASSERT_NE(failureOffset, AStringView::npos);
    EXPECT_TRUE(ContainsText(
        system.substr(submitOffset, failureOffset - submitOffset),
        "scratchArena,\n                nullptr,\n                &historyCopyAcceptedCallback"
    ));
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
    EXPECT_TRUE(ContainsText(ui, "appendDrawTextureUse(drawCommand)"));

    const usize recordOffset = ui.find("bool UiSystem::recordTaskGraphPresentation");
    const usize opaqueRecordOffset = ui.find("bool UiSystem::recordStandaloneLegacyTaskGraphPresentation", recordOffset);
    ASSERT_NE(recordOffset, AStringView::npos);
    ASSERT_NE(opaqueRecordOffset, AStringView::npos);
    ASSERT_LT(recordOffset, opaqueRecordOffset);
    const AStringView recordBody = ui.substr(recordOffset, opaqueRecordOffset - recordOffset);
    EXPECT_TRUE(ContainsText(recordBody, "recordTaskGraphDrawSnapshot(commandList, frame, backbuffer, context)"));
    EXPECT_FALSE(ContainsText(recordBody, "ImGui::GetDrawData()"));
    EXPECT_FALSE(ContainsText(recordBody, "renderDrawData(commandList, frame.framebuffer.get()"));

    // The separately named opaque fallback is intentionally the sole graph task allowed to touch live callback
    // storage, and it must guard that synchronous boundary against a changed ImGui frame.
    const usize completionOffset = ui.find("bool UiSystem::recordTaskGraphUploadCompletion", opaqueRecordOffset);
    ASSERT_NE(completionOffset, AStringView::npos);
    const AStringView opaqueRecord = ui.substr(opaqueRecordOffset, completionOffset - opaqueRecordOffset);
    EXPECT_TRUE(ContainsText(opaqueRecord, "ImGui::GetDrawData() != drawData"));
    EXPECT_TRUE(ContainsText(opaqueRecord, "frameGeneration != m_frameGeneration"));
    EXPECT_TRUE(ContainsText(opaqueRecord, "renderDrawData(commandList, frame.framebuffer.get(), *drawData)"));
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


// A newly-created retained ImGui texture is natively Unknown until its upload accepts. The graph must preserve that
// origin for the first write, then use the descriptor ShaderResource state only after the accepted batch publishes it.
TEST(EcsGraphics, UiFreshTextureImportsPreserveNativeOrigins){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString uiHeaderSource;
    AString uiSource;
    AString uiTextureSource;
    AString uiSubmissionSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "system.h", uiHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "system.cpp", uiSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "texture_resources.cpp", uiTextureSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "texture_submission.h", uiSubmissionSource));
    const AStringView uiHeader(uiHeaderSource.data(), uiHeaderSource.size());
    const AStringView ui(uiSource.data(), uiSource.size());
    const AStringView uiTextures(uiTextureSource.data(), uiTextureSource.size());
    const AStringView uiSubmission(uiSubmissionSource.data(), uiSubmissionSource.size());

    EXPECT_TRUE(ContainsText(uiHeader, "bool initialUploadAccepted = false;"));
    EXPECT_TRUE(ContainsText(uiHeader, "bool textureInitialUploadAccepted = false;"));
    EXPECT_TRUE(ContainsText(uiSubmission, "bool* initialUploadAccepted = nullptr;"));
    EXPECT_TRUE(ContainsText(uiSubmission, "void add(ImTextureData& textureData, bool* const initialUploadAccepted = nullptr)"));

    const usize createStatusOffset = uiSubmission.find("case ImTextureStatus_WantCreate:");
    const usize updateStatusOffset = uiSubmission.find("case ImTextureStatus_WantUpdates:", createStatusOffset);
    const usize okStatusOffset = uiSubmission.find("case ImTextureStatus_OK:", updateStatusOffset);
    ASSERT_NE(createStatusOffset, AStringView::npos);
    ASSERT_NE(updateStatusOffset, AStringView::npos);
    ASSERT_NE(okStatusOffset, AStringView::npos);
    ASSERT_LT(createStatusOffset, updateStatusOffset);
    ASSERT_LT(updateStatusOffset, okStatusOffset);
    const AStringView createStatus = uiSubmission.substr(createStatusOffset, updateStatusOffset - createStatusOffset);
    const AStringView updateStatus = uiSubmission.substr(updateStatusOffset, okStatusOffset - updateStatusOffset);
    EXPECT_TRUE(ContainsText(
        createStatus,
        "if(request.initialUploadAccepted)\n                        *request.initialUploadAccepted = true;"
    ));
    EXPECT_FALSE(ContainsText(updateStatus, "initialUploadAccepted"));

    for(const AStringView source : { uiTextures, ui }){
        EXPECT_TRUE(ContainsText(
            source,
            "TextureResourceDesc(\n    const Core::TextureDesc& textureDesc,\n    const bool initialUploadAccepted\n)"
        ));
        EXPECT_TRUE(ContainsText(
            source,
            ".setInitialState(initialUploadAccepted ? textureDesc.initialState : Core::ResourceStates::Unknown)"
        ));
    }
    EXPECT_TRUE(ContainsText(
        uiTextures,
        "__hidden_ui::TextureResourceDesc(resource.texture->getCreationDescription(), resource.initialUploadAccepted)"
    ));
    EXPECT_TRUE(ContainsText(uiTextures, "m_textureUploadBatch.add(*textureData, &resource->initialUploadAccepted)"));
    EXPECT_TRUE(ContainsText(ui, ".textureInitialUploadAccepted = textureResource->initialUploadAccepted,"));
    EXPECT_TRUE(ContainsText(ui, "const auto appendDrawTextureUse = [&](const TaskGraphDrawCommand& drawCommand){"));
    EXPECT_TRUE(ContainsText(
        ui,
        "__hidden_ui::TextureResourceDesc(\n"
        "                    drawCommand.texture->getCreationDescription(),\n"
        "                    drawCommand.textureInitialUploadAccepted\n"
        "                )"
    ));
    EXPECT_TRUE(ContainsText(ui, "appendDrawTextureUse(drawCommand)"));
    EXPECT_TRUE(ContainsText(ui, "m_textureUploadBatch.complete(true);"));
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
    ASSERT_TRUE(ReadGraphicsModuleSources(repoRoot, graphicsSource));
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
    EXPECT_TRUE(ContainsText(ui, "if(prepareTaskGraphPresentation(frame))"));

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
    const usize standalonePresentationOffset = presentationRenderBody.find("submitStandaloneTaskGraphPresentation(frame)");
    const usize opaquePresentationFallbackOffset = presentationRenderBody.find("submitStandaloneLegacyTaskGraphPresentation(frame)");
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
    EXPECT_FALSE(ContainsText(prepareFrame, "ensureRenderCommandList()"));
    EXPECT_TRUE(ContainsText(directRenderBody, "if(!ensureRenderCommandList())"));
    EXPECT_FALSE(ContainsText(directRenderBody, "prepareTextureRequests"));
    EXPECT_TRUE(ContainsText(directRenderBody, "standalone legacy ImGui graph presentation failed; retaining direct raster fallback"));
    EXPECT_TRUE(ContainsText(directRenderBody, "direct ImGui fallback submission was rejected; retaining frame for retry"));

    const usize directTextureSubmitOffset = directRenderBody.find("submitPreparedLegacyTextureUploads(*drawData)");
    const usize directCommandListOffset = directRenderBody.find("if(!ensureRenderCommandList())");
    const usize directExecuteOffset = directRenderBody.find("device.executeCommandLists(commandLists, 1, Core::CommandQueue::Graphics, &submitted)");
    const usize directRejectedSubmitOffset = directRenderBody.find("if(!submitted)", directExecuteOffset);
    const usize directFrameResetOffset = directRenderBody.find("m_frameStarted = false", directExecuteOffset);
    ASSERT_NE(directTextureSubmitOffset, AStringView::npos);
    ASSERT_NE(directCommandListOffset, AStringView::npos);
    ASSERT_NE(directExecuteOffset, AStringView::npos);
    ASSERT_NE(directRejectedSubmitOffset, AStringView::npos);
    ASSERT_NE(directFrameResetOffset, AStringView::npos);
    EXPECT_LT(directTextureSubmitOffset, directExecuteOffset);
    EXPECT_LT(directTextureSubmitOffset, directCommandListOffset);
    EXPECT_LT(directCommandListOffset, directExecuteOffset);
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
    AString textureUploadSource;
    ASSERT_TRUE(ReadGraphicsModuleSources(repoRoot, graphicsSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "module_texture_upload.cpp", textureUploadSource));
    const AStringView graphics(graphicsSource.data(), graphicsSource.size());
    const AStringView textureUpload(textureUploadSource.data(), textureUploadSource.size());

    EXPECT_TRUE(ContainsText(graphics, "SetupUploadReadinessBridgeGraphTask"));
    EXPECT_TRUE(ContainsText(graphics, "DeclareSetupUploadReadinessBridgeTasks"));
    EXPECT_TRUE(ContainsText(graphics, "graphics.setup_upload.readiness_bridge"));
    EXPECT_FALSE(ContainsText(graphics, "BridgeSetupUploadToConsumerQueues"));

    const usize setupGraphOffset = graphics.find("GpuTaskId DeclareSetupUploadGraph");
    const usize timingResetOffset = graphics.find("struct FrameTimingResetGraphTask", setupGraphOffset);
    const usize setupUploadOffset = graphics.find("bool SubmitGraphOwnedSetupUpload");
    const usize standaloneGraphOffset = graphics.find("bool Graphics::submitStandaloneTaskGraph", setupUploadOffset);
    ASSERT_NE(setupGraphOffset, AStringView::npos);
    ASSERT_NE(timingResetOffset, AStringView::npos);
    ASSERT_NE(setupUploadOffset, AStringView::npos);
    ASSERT_NE(standaloneGraphOffset, AStringView::npos);
    const AStringView setupGraph = graphics.substr(setupGraphOffset, timingResetOffset - setupGraphOffset);
    const AStringView setupUpload = graphics.substr(setupUploadOffset, standaloneGraphOffset - setupUploadOffset);
    EXPECT_TRUE(ContainsText(setupGraph, "DeclareSetupUploadReadinessBridgeTasks"));
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
    EXPECT_TRUE(ContainsText(textureUpload, "preserveSameClassQueueWithDirectDependency"));
    EXPECT_TRUE(ContainsText(textureBatch, "sameClassRouting.crossesQueueFamily"));
}


// Windows may deny foreground activation to the parent smoke harness after bootstrap. Keep the opt-in local to the
// frame-lagged executable so it bypasses only its focus throttle, uses Graphics' normal render-pass extension, and
// unregisters before the renderer/world can be destroyed.
TEST(EcsGraphics, FrameLaggedSmokeRendersWhenUnfocused){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString smokeSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / "transparent_multi_project.cpp", smokeSource));
    const AStringView smoke(smokeSource.data(), smokeSource.size());

    EXPECT_TRUE(ContainsText(smoke, "class FrameLaggedAsyncLightingUnfocusedPass final : public NWB::Core::IRenderPass"));
    EXPECT_TRUE(ContainsText(smoke, "virtual bool shouldRenderUnfocused()override{ return true; }"));
    EXPECT_TRUE(ContainsText(smoke, "m_context.graphics.addRenderPassToBack(m_frameLaggedAsyncLightingUnfocusedPass);"));
    EXPECT_TRUE(ContainsText(smoke, "m_context.graphics.removeRenderPass(m_frameLaggedAsyncLightingUnfocusedPass);"));

    const usize shutdownOffset = smoke.find("virtual void onShutdown()override");
    const usize updateOffset = smoke.find("virtual bool onUpdate", shutdownOffset);
    ASSERT_NE(shutdownOffset, AStringView::npos);
    ASSERT_NE(updateOffset, AStringView::npos);
    const AStringView shutdown = smoke.substr(shutdownOffset, updateOffset - shutdownOffset);
    const usize removeOffset = shutdown.find("removeFrameLaggedAsyncLightingUnfocusedPass();");
    const usize destroyOffset = shutdown.find("destroyWorld();");
    ASSERT_NE(removeOffset, AStringView::npos);
    ASSERT_NE(destroyOffset, AStringView::npos);
    EXPECT_LT(removeOffset, destroyOffset);
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

    AString surfelGiSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "task_graph_surfel_gi.cpp", surfelGiSource));
    const AStringView surfelGi(surfelGiSource.data(), surfelGiSource.size());

    EXPECT_TRUE(ContainsText(surfelGi, "#include <impl/ecs_render/raytrace/task_graph_surfel_tasks.h>"));
    EXPECT_TRUE(ContainsText(surfelGi, "EnableSameFamilyComputeEffectRouting(surfelIrradianceClearScheduling, false)"));
    EXPECT_TRUE(ContainsText(surfelGi, "EnableCrossFamilyComputeEffectRouting(surfelIrradianceClearScheduling)"));
    EXPECT_TRUE(ContainsText(surfelGi, "EnableSameFamilyComputeEffectRouting(surfelGiScheduling)"));
    EXPECT_TRUE(ContainsText(surfelGi, "EnableCrossFamilyComputeEffectRouting(surfelGiScheduling)"));
}


// The persistent counter crosses the Compute GI packet and optional Transfer readback tail.  Its next-frame
// imported cache must therefore be concurrently shared by each actual transport rather than retaining a stale
// exclusive Transfer owner with no future release destination.
TEST(EcsGraphics, SurfelCounterSharesComputeAndTransferReadbackPath){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString surfelSource;
    AString surfelTaskGraphSource;
    AString systemSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_surfel_gi.cpp", surfelSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "task_graph_surfel_gi.cpp", surfelTaskGraphSource));
    ASSERT_TRUE(ReadRendererSystemSources(repoRoot, systemSource));
    const AStringView surfel(surfelSource.data(), surfelSource.size());
    const AStringView surfelTaskGraph(surfelTaskGraphSource.data(), surfelTaskGraphSource.size());
    const AStringView system(systemSource.data(), systemSource.size());

    const usize counterOffset = surfel.find("if(!rayTracingState().m_surfelCounterBuffer){");
    const usize traceArgsOffset = surfel.find("// Build-args rewrites the indirect dispatch buffer each frame.", counterOffset);
    ASSERT_NE(counterOffset, AStringView::npos);
    ASSERT_NE(traceArgsOffset, AStringView::npos);
    ASSERT_LT(counterOffset, traceArgsOffset);
    const AStringView counter = surfel.substr(counterOffset, traceArgsOffset - counterOffset);
    EXPECT_TRUE(ContainsText(counter, ".setCanHaveUAVs(true)"));
    EXPECT_TRUE(ContainsText(counter, ".setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)"));
    EXPECT_TRUE(ContainsText(counter, ".setDebugName(Name(\"surfel_counter\"))"));

    const usize readbackOffset = surfelTaskGraph.find("void RendererSystem::declareDeferredSurfelCountReadbackTask");
    ASSERT_NE(readbackOffset, AStringView::npos);
    const AStringView readback = surfelTaskGraph.substr(readbackOffset);
    EXPECT_TRUE(ContainsText(readback, "m_rayTracingState.m_surfelCounterBuffer"));
    EXPECT_TRUE(ContainsText(readback, ".source = counter,"));
    EXPECT_TRUE(ContainsText(readback, ".setQueue(TransferQueueRequest())"));

    EXPECT_TRUE(ContainsText(system, "if(m_surfelGiCounterPersistentState.valid())"));
    EXPECT_TRUE(ContainsText(system, "m_surfelGiCounterPersistentState.buildFilteredBufferSubset("));
    EXPECT_TRUE(ContainsText(system, "m_surfelGiCounterPersistentState.commit(\n                    *context->candidate"));
    EXPECT_TRUE(ContainsText(system, ".task = m_deferredSurfelGiCounterReadbackTask,"));
    EXPECT_TRUE(ContainsText(system, "scratchArena,\n                nullptr,\n                &readbackAcceptedCallback"));
    const usize readbackSubmitOffset = system.find("const bool readbackAccepted = submitter.recordAndSubmitTask(");
    const usize readbackTokenOffset = system.find(
        "const Core::QueueSubmissionToken readbackSubmissionToken =",
        readbackSubmitOffset
    );
    ASSERT_NE(readbackSubmitOffset, AStringView::npos);
    ASSERT_NE(readbackTokenOffset, AStringView::npos);
    EXPECT_LT(readbackSubmitOffset, readbackTokenOffset);
    EXPECT_TRUE(ContainsText(system, "else if(!readbackAccepted || !readbackContext.acceptedStateReady){"));
}


// The full irradiance clear is deliberately renderer-local: the generic helper conservatively declares Graphics
// for render-pass lowering, while this native clear is constrained to the direct Compute GI packet and captures the
// same typed command-IR record after the graph-owned CopyDest transition.
TEST(EcsGraphics, SurfelIrradianceClearUsesComputeGraphCallback){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString surfelTasksSource;
    AString surfelGiSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "task_graph_surfel_tasks.cpp", surfelTasksSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "task_graph_surfel_gi.cpp", surfelGiSource));
    const AStringView callback(surfelTasksSource.data(), surfelTasksSource.size());
    const AStringView surfelGi(surfelGiSource.data(), surfelGiSource.size());

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


// The retained monolithic soft-shadow route must clear all-lit visibility on the selected Compute packet. Its
// renderer-local callback retains typed command-IR capture while avoiding the generic clear helper's Graphics path.
TEST(EcsGraphics, ShadowVisibilityAllLitClearUsesComputeGraphCallback){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString allLitClearTaskSource;
    AString shadowVisibilityTaskGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "task_graph_shadow_visibility_tasks.cpp", allLitClearTaskSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "task_graph_shadow_visibility.cpp", shadowVisibilityTaskGraphSource));
    const AStringView callback(allLitClearTaskSource.data(), allLitClearTaskSource.size());
    const AStringView shadowVisibility(shadowVisibilityTaskGraphSource.data(), shadowVisibilityTaskGraphSource.size());

    EXPECT_TRUE(ContainsText(callback, "context.taskGraph.textureForResource(payload.destination)"));
    EXPECT_TRUE(ContainsText(callback, "if(!destination || commandList.isRenderPassActive())"));
    EXPECT_FALSE(ContainsText(callback, "endRenderPass()"));
    EXPECT_TRUE(ContainsText(callback, "Core::GpuClearTextureTaskDesc clearDesc{"));
    EXPECT_TRUE(ContainsText(callback, ".destination = payload.destination,"));
    EXPECT_TRUE(ContainsText(callback, ".subresources = s_ShadowVisibilitySubresources,"));
    EXPECT_TRUE(ContainsText(callback, ".valueType = Core::GpuClearTextureTaskValueType::Float,"));
    EXPECT_TRUE(ContainsText(callback, ".floatValue = Core::Color(1.f, 1.f, 1.f, 1.f),"));
    EXPECT_TRUE(ContainsText(callback, "context.commandIrCapture"));
    EXPECT_TRUE(ContainsText(callback, "captureClearTexture("));
    EXPECT_TRUE(ContainsText(callback, "commandList.clearTextureFloat(destination, clearDesc.subresources, clearDesc.floatValue);"));

    const usize resourceUseOffset = shadowVisibility.find("const Core::GpuTaskResourceUse allLitClearResourceUse");
    const usize shadowSchedulingOffset = shadowVisibility.find("Core::GpuTaskSchedulingHint scheduling;", resourceUseOffset);
    ASSERT_NE(resourceUseOffset, AStringView::npos);
    ASSERT_NE(shadowSchedulingOffset, AStringView::npos);
    ASSERT_LT(resourceUseOffset, shadowSchedulingOffset);
    const AStringView allLitClear = shadowVisibility.substr(resourceUseOffset, shadowSchedulingOffset - resourceUseOffset);

    EXPECT_TRUE(ContainsText(allLitClear, "WriteTextureUse(\n        shadowVisibility,\n        ECSRenderDetail::s_ShadowVisibilitySubresources,\n        Core::ResourceStates::CopyDest\n    )"));
    EXPECT_TRUE(ContainsText(allLitClear, ".setQueue(ComputePacketQueueRequest())"));
    EXPECT_TRUE(ContainsText(allLitClear, ".setResourceUses(&allLitClearResourceUse, 1u)"));
    EXPECT_TRUE(ContainsText(allLitClear, "m_deferredLightingTaskGraph.addTask<"));
    EXPECT_TRUE(ContainsText(allLitClear, "ECSRenderDetail::ShadowVisibilityAllLitClearGraphTask"));
    EXPECT_FALSE(ContainsText(allLitClear, "addClearTextureTask("));
}


// Hardware Caustics is a separate Graphics-capable effect chain. Its clear and every independently created
// temporal-accumulator prefix must carry the explicit cross-family opt-in so copied photon/resolve schedules
// retain one selected physical Graphics queue without making a windowed present eligible for that queue.
TEST(EcsGraphics, HardwareCausticsPermitsOptInCrossFamilyGraphicsRouting){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString taskGraphSource;
    ASSERT_TRUE(ReadRendererSources(repoRoot, { "deferred/task_graph_deferred_lighting.cpp" }, taskGraphSource));
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
    ASSERT_TRUE(ReadRendererSources(
        repoRoot,
        {
            "raytrace/task_graph_caustics.cpp",
            "raytrace/task_graph_surfel_gi.cpp",
            "deferred/task_graph_deferred_lighting.cpp",
        },
        taskGraphSource
    ));
    ASSERT_TRUE(ReadRendererSystemSources(repoRoot, systemSource));
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
    EXPECT_TRUE(ContainsText(hardwareStateSources, "RendererSystemRenderDetail::s_HardwareCausticsStateSourceCapacity"));
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

    AString softwareCausticsSource;
    AString surfelGiSource;
    AString deferredLightingSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "task_graph_caustics.cpp", softwareCausticsSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "task_graph_surfel_gi.cpp", surfelGiSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "deferred" / "task_graph_deferred_lighting.cpp", deferredLightingSource));
    const AStringView softwareCaustics(softwareCausticsSource.data(), softwareCausticsSource.size());
    const AStringView surfelGi(surfelGiSource.data(), surfelGiSource.size());
    const AStringView hardwareCaustics(deferredLightingSource.data(), deferredLightingSource.size());
    const AStringView avboitOccupancy(deferredLightingSource.data(), deferredLightingSource.size());
    const AStringView avboitAccumulation(deferredLightingSource.data(), deferredLightingSource.size());

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

    EXPECT_TRUE(ContainsText(avboitOccupancy, "avboitClearScheduling.allowMergeAcrossConsumerFrontier = true;"));
    EXPECT_TRUE(ContainsText(avboitOccupancy, "avboitOccupancyScheduling.allowMergeAcrossConsumerFrontier = true;"));
    EXPECT_TRUE(ContainsText(avboitOccupancy, ".setDependencies(&occupancyDependency, 1u)"));
    EXPECT_TRUE(ContainsText(avboitAccumulation, "accumulationFinalizeScheduling.allowMergeAcrossConsumerFrontier = true;"));
    EXPECT_TRUE(ContainsText(
        avboitAccumulation,
        ".setDependencies(&m_avboitSystem.taskGraphStage().m_accumulationTask, 1u)"
    ));
}


// A retained generated-vertex output needs an explicit graph phase for every producer/raster handoff. Keep the
// narrow fifth regular draw visible rather than allowing it to fall through to a callback-local compatibility path.
TEST(EcsGraphics, SharedComputeEmulationRetainsFiveRegularDraws){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString sharedTaskGraphStageSource;
    AString taskGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "shared" / "task_graph_stage.h", sharedTaskGraphStageSource));
    ASSERT_TRUE(ReadRendererSources(
        repoRoot,
        {
            "deferred/task_graph_graphics_prefix.cpp",
            "deferred/task_graph_deferred_lighting.cpp",
        },
        taskGraphSource
    ));
    const AStringView sharedTaskGraphStage(sharedTaskGraphStageSource.data(), sharedTaskGraphStageSource.size());
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());

    EXPECT_TRUE(ContainsText(sharedTaskGraphStage, "s_SharedComputeEmulationMaximumDrawCount = 5u;"));
    EXPECT_TRUE(ContainsText(
        sharedTaskGraphStage,
        "s_SharedComputeEmulationMaximumPhaseCount =\n"
        "    s_SharedComputeEmulationMaximumDrawCount * s_SharedComputeEmulationPhasesPerDraw;"
    ));

    const AStringView fifthPhaseIdentities[] = {
        "render.graphics_prefix.opaque_shared_compute_emulation_generate_e",
        "render.graphics_prefix.opaque_shared_compute_emulation_raster_e",
        "render.avboit.occupancy.shared_compute_emulation_generate_e",
        "render.avboit.occupancy.shared_compute_emulation_raster_e",
        "render.avboit.extinction.shared_compute_emulation_generate_e",
        "render.avboit.extinction.shared_compute_emulation_raster_e",
        "render.avboit.accumulation.shared_compute_emulation_generate_e",
        "render.avboit.accumulation.shared_compute_emulation_raster_e",
    };
    for(const AStringView identity : fifthPhaseIdentities)
        EXPECT_TRUE(ContainsText(taskGraph, identity));

    const AStringView fifthPhaseMarkers[] = {
        "Opaque Shared Compute Emulation Generate E",
        "Opaque Shared Compute Emulation Raster E",
        "AVBOIT Occupancy Shared Compute Emulation Generate E",
        "AVBOIT Occupancy Shared Compute Emulation Raster E",
        "AVBOIT Extinction Shared Compute Emulation Generate E",
        "AVBOIT Extinction Shared Compute Emulation Raster E",
        "AVBOIT Accumulation Shared Compute Emulation Generate E",
        "AVBOIT Accumulation Shared Compute Emulation Raster E",
    };
    for(const AStringView marker : fifthPhaseMarkers)
        EXPECT_TRUE(ContainsText(taskGraph, marker));
}


// Shadow Visibility has both a fully split soft-transparent route and a retained monolithic compatibility route.
// Each graph-owned chain may choose an alternate Compute family, while its direct successors retain that physical
// queue and the explicit primary-Graphics presentation guard remains outside this effect.
TEST(EcsGraphics, ShadowVisibilityPermitsOptInCrossFamilyComputeRouting){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString shadowVisibilitySource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "task_graph_shadow_visibility.cpp", shadowVisibilitySource));
    const AStringView shadowVisibility(shadowVisibilitySource.data(), shadowVisibilitySource.size());

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

    AString shadowVisibilitySource;
    AString shadowSource;
    AString softShadowSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "task_graph_shadow_visibility.cpp", shadowVisibilitySource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_shadow.cpp", shadowSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_softshadow.cpp", softShadowSource));
    const AStringView shadowVisibility(shadowVisibilitySource.data(), shadowVisibilitySource.size());
    const AStringView shadowSourceView(shadowSource.data(), shadowSource.size());
    const AStringView softShadowSourceView(softShadowSource.data(), softShadowSource.size());

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


// The retained monolithic callback owns later native scratch transitions, but its graph entry must still reflect
// each fresh target's first write. Only an accepted temporal history may enter as a sampled input.
TEST(EcsGraphics, MonolithicShadowVisibilityKeepsFreshScratchAsFirstWrites){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString shadowVisibilitySource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "task_graph_shadow_visibility.cpp", shadowVisibilitySource));
    const AStringView shadowVisibility(shadowVisibilitySource.data(), shadowVisibilitySource.size());

    const usize opaqueHistoryHelperOffset = shadowVisibility.find("const auto appendOptionalOpaqueTemporalHistoryTexture");
    const usize transparentHistoryHelperOffset = shadowVisibility.find(
        "const auto appendOptionalTransparentTemporalHistoryTexture",
        opaqueHistoryHelperOffset
    );
    const usize optionalImportsOffset = shadowVisibility.find("bool optionalResourcesImported =", transparentHistoryHelperOffset);
    const usize sceneTlasOffset = shadowVisibility.find("Core::GpuGraphResourceId sceneTlas;", optionalImportsOffset);
    ASSERT_NE(opaqueHistoryHelperOffset, AStringView::npos);
    ASSERT_NE(transparentHistoryHelperOffset, AStringView::npos);
    ASSERT_NE(optionalImportsOffset, AStringView::npos);
    ASSERT_NE(sceneTlasOffset, AStringView::npos);
    ASSERT_LT(opaqueHistoryHelperOffset, transparentHistoryHelperOffset);
    ASSERT_LT(transparentHistoryHelperOffset, optionalImportsOffset);
    ASSERT_LT(optionalImportsOffset, sceneTlasOffset);
    const AStringView opaqueHistoryHelper = shadowVisibility.substr(
        opaqueHistoryHelperOffset,
        transparentHistoryHelperOffset - opaqueHistoryHelperOffset
    );
    const AStringView transparentHistoryHelper = shadowVisibility.substr(
        transparentHistoryHelperOffset,
        optionalImportsOffset - transparentHistoryHelperOffset
    );
    const AStringView optionalImports = shadowVisibility.substr(optionalImportsOffset, sceneTlasOffset - optionalImportsOffset);

    EXPECT_TRUE(ContainsText(opaqueHistoryHelper, "else if(!splitSoftTransparentFold){"));
    EXPECT_TRUE(ContainsText(opaqueHistoryHelper, "if(softShadowHistoryReadable)\n                    resourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));"));
    EXPECT_TRUE(ContainsText(opaqueHistoryHelper, "else if(m_rayTracingState.m_softShadowTemporalReady)\n                resourceUses.push_back(WriteUse(resource, Core::ResourceStates::UnorderedAccess));"));
    EXPECT_TRUE(ContainsText(transparentHistoryHelper, "if(m_rayTracingState.m_softTransparentTemporalReady){"));
    EXPECT_TRUE(ContainsText(transparentHistoryHelper, "if(softShadowHistoryReadable)\n                    resourceUses.push_back(ReadUse(resource, Core::ResourceStates::ShaderResource));"));
    EXPECT_TRUE(ContainsText(transparentHistoryHelper, "resourceUses.push_back(WriteUse(resource, Core::ResourceStates::UnorderedAccess));"));

    EXPECT_TRUE(ContainsText(optionalImports, "appendOptionalWriteTexture(\n            deferredTargets.shadowSoftHalfA"));
    EXPECT_TRUE(ContainsText(optionalImports, "appendOptionalWriteTexture(\n            deferredTargets.shadowSoftHalfB"));
    EXPECT_TRUE(ContainsText(optionalImports, "appendOptionalWriteTexture(\n            deferredTargets.shadowSoftGeometry"));
    EXPECT_TRUE(ContainsText(optionalImports, "appendOptionalWriteTexture(\n            deferredTargets.transparentSoftHalf"));
    EXPECT_TRUE(ContainsText(optionalImports, "appendOptionalTransparentTemporalHistoryTexture(\n            deferredTargets.transparentHistA"));
    EXPECT_TRUE(ContainsText(optionalImports, "appendOptionalTransparentTemporalHistoryTexture(\n            deferredTargets.transparentHistB"));
    EXPECT_TRUE(ContainsText(optionalImports, "appendOptionalTransparentTemporalHistoryTexture(\n            deferredTargets.transparentMomentsA"));
    EXPECT_TRUE(ContainsText(optionalImports, "appendOptionalTransparentTemporalHistoryTexture(\n            deferredTargets.transparentMomentsB"));
    EXPECT_FALSE(ContainsText(optionalImports, "ReadWriteUse("));
}


// Temporal soft-shadow scratch stays private, but its accepted native state must seed the next shadow packet on
// either the Graphics fallback or dedicated Compute route. The separate return-state cache stays Compute-only.
TEST(EcsGraphics, ShadowTemporalScratchRetainsAcceptedStateAcrossGraphicsRoute){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    ASSERT_TRUE(ReadRendererSystemSources(repoRoot, systemSource));
    const AStringView system(systemSource.data(), systemSource.size());

    const usize stateSourcesOffset = system.find("Core::GpuExternalPacketStateSource shadowVisibilityStateSources[");
    const usize softwareCausticsSourcesOffset = system.find(
        "Core::GpuExternalPacketStateSource softwareCausticsStateSources[",
        stateSourcesOffset
    );
    ASSERT_NE(stateSourcesOffset, AStringView::npos);
    ASSERT_NE(softwareCausticsSourcesOffset, AStringView::npos);
    ASSERT_LT(stateSourcesOffset, softwareCausticsSourcesOffset);
    const AStringView shadowVisibilityStateSources = system.substr(
        stateSourcesOffset,
        softwareCausticsSourcesOffset - stateSourcesOffset
    );
    EXPECT_TRUE(ContainsText(shadowVisibilityStateSources, "if(m_shadowComputePersistentState.valid())"));
    EXPECT_TRUE(ContainsText(shadowVisibilityStateSources, "m_shadowComputePersistentState.source()"));
    EXPECT_FALSE(ContainsText(
        shadowVisibilityStateSources,
        "shadowVisibilityRunsOnCompute && m_shadowComputePersistentState.valid()"
    ));
    EXPECT_TRUE(ContainsText(
        shadowVisibilityStateSources,
        "if(shadowVisibilityRunsOnCompute && m_shadowVisibilityReturnState.valid())"
    ));

    const usize acceptedShadowOffset = system.find("const Core::TextureHandle shadowVisibilityReturnTextures[]");
    const usize scratchStateOffset = system.find("m_shadowComputePersistentState.buildFilteredResourceSubset(", acceptedShadowOffset);
    const usize acceptedCallbackOffset = system.find("const auto acceptShadowVisibilityTask = [](", scratchStateOffset);
    const usize returnCommitOffset = system.find("m_shadowVisibilityReturnState.commit(", acceptedCallbackOffset);
    const usize scratchCommitOffset = system.find("m_shadowComputePersistentState.commit(", returnCommitOffset);
    const usize temporalFinalizeOffset = system.find(
        "m_raytracingSystem.finalizeSoftShadowTemporalHistory(*context->targets);",
        scratchCommitOffset
    );
    ASSERT_NE(acceptedShadowOffset, AStringView::npos);
    ASSERT_NE(scratchStateOffset, AStringView::npos);
    ASSERT_NE(acceptedCallbackOffset, AStringView::npos);
    ASSERT_NE(returnCommitOffset, AStringView::npos);
    ASSERT_NE(scratchCommitOffset, AStringView::npos);
    ASSERT_NE(temporalFinalizeOffset, AStringView::npos);
    EXPECT_LT(returnCommitOffset, scratchCommitOffset);
    EXPECT_LT(scratchCommitOffset, temporalFinalizeOffset);
    const AStringView acceptedShadow = system.substr(acceptedShadowOffset, temporalFinalizeOffset - acceptedShadowOffset);
    EXPECT_TRUE(ContainsText(acceptedShadow, "deferredTargets.shadowSoftGeometry,"));
    EXPECT_TRUE(ContainsText(acceptedShadow, "deferredTargets.shadowSoftGeometryPrev,"));
    EXPECT_TRUE(ContainsText(acceptedShadow, "m_shadowComputePersistentState.buildFilteredResourceSubset("));
    EXPECT_TRUE(ContainsText(acceptedShadow, "if(shadowVisibilityRunsOnCompute){"));
    EXPECT_TRUE(ContainsText(acceptedShadow, "m_shadowVisibilityReturnState.buildFilteredResourceSubset("));
    EXPECT_TRUE(ContainsText(acceptedShadow, "context->renderer->m_shadowVisibilityReturnState.commit("));
    EXPECT_TRUE(ContainsText(acceptedShadow, "context->renderer->m_shadowComputePersistentState.commit("));
    EXPECT_TRUE(ContainsText(system, "finalizeSoftShadowTemporalHistory(*context->targets)"));
    EXPECT_TRUE(ContainsText(
        system,
        ".task = m_deferredShadowVisibilityTask,\n"
        "                .context = &shadowVisibilityAcceptance,\n"
        "                .invoke = acceptShadowVisibilityTask,"
    ));
}


// Software-caustics scratch is private on both the dedicated Compute route and its legal Graphics fallback. Only
// the cross-queue irradiance return cache is route-conditional; all retained state publishes from the exact task.
TEST(EcsGraphics, SoftwareCausticsScratchRetainsAcceptedStateAcrossGraphicsRoute){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    ASSERT_TRUE(ReadRendererSystemSources(repoRoot, systemSource));
    const AStringView system(systemSource.data(), systemSource.size());

    const usize stateSourcesOffset = system.find("Core::GpuExternalPacketStateSource softwareCausticsStateSources[");
    const usize surfelSourcesOffset = system.find("Core::GpuExternalPacketStateSource surfelGiStateSources[", stateSourcesOffset);
    ASSERT_NE(stateSourcesOffset, AStringView::npos);
    ASSERT_NE(surfelSourcesOffset, AStringView::npos);
    ASSERT_LT(stateSourcesOffset, surfelSourcesOffset);
    const AStringView stateSources = system.substr(stateSourcesOffset, surfelSourcesOffset - stateSourcesOffset);
    EXPECT_TRUE(ContainsText(stateSources, "if(m_causticsComputePersistentState.valid())"));
    EXPECT_FALSE(ContainsText(
        stateSources,
        "softwareCausticsRunsOnCompute && m_causticsComputePersistentState.valid()"
    ));
    EXPECT_TRUE(ContainsText(
        stateSources,
        "if(softwareCausticsRunsOnCompute && m_causticIrradianceReturnState.valid())"
    ));

    const usize candidatesOffset = system.find("const Core::TextureHandle causticsComputeScratchTextures[]");
    const usize callbacksOffset = system.find("const Core::GpuTaskGraphTaskAcceptedCallback shadowEffectsAcceptedCallbacks[]", candidatesOffset);
    ASSERT_NE(candidatesOffset, AStringView::npos);
    ASSERT_NE(callbacksOffset, AStringView::npos);
    ASSERT_LT(candidatesOffset, callbacksOffset);
    const AStringView acceptedCaustics = system.substr(candidatesOffset, callbacksOffset - candidatesOffset);
    EXPECT_TRUE(ContainsText(acceptedCaustics, "if(laggedAsyncLightingSchedule){"));
    EXPECT_TRUE(ContainsText(acceptedCaustics, "m_causticIrradianceLightingState.buildFilteredResourceSubset("));
    EXPECT_TRUE(ContainsText(acceptedCaustics, "if(softwareCausticsRunsOnCompute){"));
    EXPECT_TRUE(ContainsText(acceptedCaustics, "m_causticIrradianceReturnState.buildFilteredResourceSubset("));
    EXPECT_TRUE(ContainsText(acceptedCaustics, "m_causticsComputePersistentState.buildFilteredResourceSubset("));
    EXPECT_TRUE(ContainsText(acceptedCaustics, "context->renderer->m_causticIrradianceLightingState.commit("));
    EXPECT_TRUE(ContainsText(acceptedCaustics, "context->renderer->m_causticIrradianceReturnState.commit("));
    EXPECT_TRUE(ContainsText(acceptedCaustics, "context->renderer->m_causticsComputePersistentState.commit("));
    EXPECT_TRUE(ContainsText(system, ".task = m_deferredSoftwareCausticsTask,"));
    EXPECT_TRUE(ContainsText(system, "shadowEffectsSubmitted && softwareCausticsSubmissionToken.valid()"));
    const usize stateFailureOffset = system.find(
        "if(!hardwareShadowSupported && softwareCausticsSubmissionToken.valid() && !softwareCausticsAcceptance.stateReady)"
    );
    const usize surfelSubmitOffset = system.find("if(!submitDeferredSurfelGi())", stateFailureOffset);
    ASSERT_NE(stateFailureOffset, AStringView::npos);
    ASSERT_NE(surfelSubmitOffset, AStringView::npos);
    ASSERT_LT(stateFailureOffset, surfelSubmitOffset);
    const AStringView stateFailure = system.substr(stateFailureOffset, surfelSubmitOffset - stateFailureOffset);
    EXPECT_TRUE(ContainsText(stateFailure, "restoreUnacceptedShadowEffectsCpuState();"));
    EXPECT_FALSE(ContainsText(stateFailure, "restoreAvboitCpuState();"));
}


// AVBOIT's Graphics raster packets deliberately retain their established primary route, but its split Depth Warp
// and Integration tasks are pure Compute packets with complete graph-declared handoffs. Keep their auxiliary
// routing opt-in explicit rather than inferring it from the broader AVBOIT effect name.
TEST(EcsGraphics, SplitAvboitComputePacketsPermitCrossFamilyRouting){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString taskGraphSource;
    ASSERT_TRUE(ReadRendererSources(repoRoot, { "deferred/task_graph_deferred_lighting.cpp" }, taskGraphSource));
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
    ASSERT_TRUE(ReadRendererSources(
        repoRoot,
        {
            "avboit/task_graph_occupancy_tasks.h",
            "avboit/task_graph_occupancy_tasks.cpp",
            "avboit/task_graph_extinction_integration_tasks.h",
            "avboit/task_graph_extinction_integration_tasks.cpp",
            "avboit/task_graph_accumulation_tasks.h",
            "avboit/task_graph_accumulation_tasks.cpp",
            "deferred/task_graph_deferred_lighting.cpp",
        },
        taskGraphSource
    ));
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
    AString avboitOccupancyTasksSource;
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
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "avboit" / "task_graph_occupancy_tasks.cpp", avboitOccupancyTasksSource));
    ASSERT_TRUE(ReadRendererSources(
        repoRoot,
        {
            "shared/task_graph_draw_snapshots.h",
            "avboit/task_graph_occupancy_tasks.h",
            "avboit/task_graph_occupancy_tasks.cpp",
            "avboit/task_graph_extinction_integration_tasks.h",
            "avboit/task_graph_extinction_integration_tasks.cpp",
            "avboit/task_graph_accumulation_tasks.h",
            "avboit/task_graph_accumulation_tasks.cpp",
            "deferred/task_graph_deferred_lighting.cpp",
        },
        taskGraphSource
    ));

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
    const AStringView avboitOccupancyTasks(avboitOccupancyTasksSource.data(), avboitOccupancyTasksSource.size());
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

    EXPECT_TRUE(ContainsText(avboitOccupancyTasks, "if(payload.transparentCsgStreamsUploaded != payload.transparentCsgSnapshot.captured)"));
    EXPECT_FALSE(ContainsText(avboitOccupancyTasks, "CsgFrameState"));

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
    ASSERT_TRUE(ReadRendererSources(repoRoot, { "raytrace/task_graph_shadow_prepare.cpp" }, taskGraphSource));
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
    ASSERT_TRUE(ReadRendererSources(repoRoot, { "raytrace/task_graph_shadow_prepare.cpp" }, taskGraphSource));
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
    ASSERT_TRUE(ReadRendererSources(repoRoot, { "raytrace/task_graph_shadow_prepare.cpp" }, taskGraphSource));
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
    ASSERT_TRUE(ReadRendererSources(repoRoot, { "raytrace/task_graph_shadow_prepare.cpp" }, taskGraphSource));
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
    ASSERT_TRUE(ReadRendererSources(repoRoot, { "deferred/task_graph_deferred_lighting.cpp" }, taskGraphSource));
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


// The next graph declaration clears its history-tail output token. Lighting owns a read-ready completion for the
// immutable prior snapshot, while Shadow and Hardware Caustics own a distinct writer-drain completion before their
// first live writes. Even when both carry one native token, no semantic graph ID can be rebound at submission time.
TEST(EcsGraphics, LaggedLightingHistoryConsumersOwnSemanticPriorTokens){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    AString systemHeaderSource;
    AString shadowVisibilityTaskGraphSource;
    AString deferredLightingTaskGraphSource;
    ASSERT_TRUE(ReadRendererSystemSources(repoRoot, systemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "system.h", systemHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "task_graph_shadow_visibility.cpp", shadowVisibilityTaskGraphSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "deferred" / "task_graph_deferred_lighting.cpp", deferredLightingTaskGraphSource));
    const AStringView system(systemSource.data(), systemSource.size());
    const AStringView systemHeader(systemHeaderSource.data(), systemHeaderSource.size());
    const AStringView shadowVisibility(shadowVisibilityTaskGraphSource.data(), shadowVisibilityTaskGraphSource.size());
    const AStringView lighting(deferredLightingTaskGraphSource.data(), deferredLightingTaskGraphSource.size());
    const usize renderOffset = system.find("void RendererSystem::render(");
    ASSERT_NE(renderOffset, AStringView::npos);
    const AStringView render = system.substr(renderOffset);
    const usize priorReadReadyOffset = render.find(
        "const Core::QueueSubmissionToken priorLaggedLightingHistoryReadReadyToken"
    );
    const usize priorDrainOffset = render.find("const Core::QueueSubmissionToken priorLaggedLightingHistoryWriterDrainToken");
    const usize armDrainOffset = render.find(
        "m_laggedLightingHistoryWriterDrainToken = laggedLightingHistorySubmissionToken"
    );
    const usize graphBuildOffset = render.find("buildDeferredLightingTaskGraph(");
    ASSERT_NE(priorReadReadyOffset, AStringView::npos);
    ASSERT_NE(priorDrainOffset, AStringView::npos);
    ASSERT_NE(armDrainOffset, AStringView::npos);
    ASSERT_NE(graphBuildOffset, AStringView::npos);

    EXPECT_LT(priorReadReadyOffset, graphBuildOffset);
    EXPECT_LT(priorDrainOffset, graphBuildOffset);
    EXPECT_LT(armDrainOffset, graphBuildOffset);
    EXPECT_TRUE(ContainsText(render, "&& priorLaggedLightingHistoryReadReadyToken.valid()"));
    EXPECT_TRUE(ContainsText(render, "const auto laggedLightingHistoryTokenPending ="));
    EXPECT_TRUE(ContainsText(render, "tokenTargetGeneration == laggedLightingHistoryTargetGeneration"));
    EXPECT_TRUE(ContainsText(render, "const bool laggedLightingHistorySubmissionPending = laggedLightingHistoryTokenPending("));
    EXPECT_TRUE(ContainsText(render, "if(laggedLightingHistorySubmissionPending){"));
    EXPECT_TRUE(ContainsText(render, "else if(\n        m_laggedLightingHistoryWriterDrainToken.valid()"));
    EXPECT_TRUE(ContainsText(render, ") < token.value"));
    EXPECT_TRUE(ContainsText(render, "laggedLightingHistoryWriterWaitPending"));
    EXPECT_TRUE(ContainsText(
        render,
        "const bool laggedLightingHistoryWriterWaitPending = priorLaggedLightingHistoryWriterDrainToken.valid();"
    ));
    EXPECT_TRUE(ContainsText(
        render,
        ".laggedLightingHistoryReadReady = priorLaggedLightingHistoryReadReadyToken.valid()"
    ));
    EXPECT_TRUE(ContainsText(render, ".laggedLightingHistoryWriterWaitPending = laggedLightingHistoryWriterWaitPending"));
    EXPECT_EQ(CountText(render, "priorLaggedLightingHistoryReadReadyToken,"), 2u);
    EXPECT_EQ(CountText(render, "priorLaggedLightingHistoryWriterDrainToken,"), 2u);
    EXPECT_FALSE(ContainsText(render, "GpuTaskGraphExternalCompletionToken"));
    EXPECT_FALSE(ContainsText(render, "deferredLightingCompletionTokens"));
    EXPECT_FALSE(ContainsText(render, "shadowEffectsCompletionTokens"));
    EXPECT_FALSE(ContainsText(render, "hardwareCausticsCompletionTokens"));
    EXPECT_FALSE(ContainsText(render, ".token = m_laggedLightingHistorySubmissionToken"));
    EXPECT_TRUE(ContainsText(
        render,
        "laggedAsyncLightingSchedule && !m_deferredLightingHistoryReadReadyCompletion.valid()"
    ));
    EXPECT_TRUE(ContainsText(
        render,
        "laggedLightingHistoryWriterWaitPending && !m_deferredLightingHistoryWriterDrainCompletion.valid()"
    ));
    EXPECT_EQ(CountText(
        render,
        "!laggedLightingHistoryWriterWaitPending || m_deferredLightingHistoryWriterDrainCompletion.valid()"
    ), 3u);
    EXPECT_TRUE(ContainsText(render, "device.queueGetCompletedInstance("));
    EXPECT_FALSE(ContainsText(render, "consumeLaggedLightingHistoryWriterDrain"));
    EXPECT_TRUE(ContainsText(lighting, ".acceptedToken = &m_laggedLightingHistorySubmissionToken"));
    EXPECT_TRUE(ContainsText(lighting, "if(useLaggedLightingHistory){"));
    EXPECT_TRUE(ContainsText(lighting, "if(features.laggedLightingHistoryWriterWaitPending){"));
    EXPECT_EQ(CountText(lighting, ".setToken(laggedLightingHistoryReadReadyToken)"), 1u);
    EXPECT_EQ(CountText(lighting, ".setToken(laggedLightingHistoryWriterDrainToken)"), 1u);
    EXPECT_TRUE(ContainsText(lighting, "render.deferred_lighting.lagged_history_read_ready"));
    EXPECT_TRUE(ContainsText(lighting, "render.deferred_lighting.lagged_history_writer_drain"));
    EXPECT_TRUE(ContainsText(
        lighting,
        "const Core::GpuExternalCompletionId laggedLightingExternalDependencies[] = {\n"
        "        m_deferredLightingHistoryReadReadyCompletion,"
    ));
    EXPECT_TRUE(ContainsText(
        lighting,
        "features.laggedLightingHistoryWriterWaitPending\n"
        "            ? m_deferredLightingHistoryWriterDrainCompletion\n"
        "            : Core::GpuExternalCompletionId{}"
    ));
    EXPECT_TRUE(ContainsText(
        lighting,
        "hardwareExternalDependencies = features.laggedLightingHistoryWriterWaitPending\n"
        "            ? &m_deferredLightingHistoryWriterDrainCompletion"
    ));
    EXPECT_FALSE(ContainsText(lighting, "m_deferredLightingHistoryCompletion"));
    EXPECT_EQ(CountText(system, "m_deferredLightingHistoryReadReadyCompletion = {};"), 2u);
    EXPECT_EQ(CountText(system, "m_deferredLightingHistoryWriterDrainCompletion = {};"), 2u);
    EXPECT_EQ(CountText(lighting, "m_deferredLightingHistoryReadReadyCompletion = {};"), 1u);
    EXPECT_EQ(CountText(lighting, "m_deferredLightingHistoryWriterDrainCompletion = {};"), 1u);

    EXPECT_TRUE(ContainsText(shadowVisibility, "laggedLightingHistoryWriterDrainDependencies"));
    EXPECT_TRUE(ContainsText(
        shadowVisibility,
        "Core::GpuExternalCompletionId laggedLightingHistoryWriterDrainCompletion"
    ));
    EXPECT_TRUE(ContainsText(
        systemHeader,
        "Core::GpuExternalCompletionId laggedLightingHistoryWriterDrainCompletion"
    ));
    EXPECT_TRUE(ContainsText(shadowVisibility, ".setExternalDependencies(\n                laggedLightingHistoryWriterDrainDependencies,"));
    EXPECT_TRUE(ContainsText(shadowVisibility, ".setExternalDependencies(\n            laggedLightingHistoryWriterDrainDependencies,"));
    EXPECT_TRUE(ContainsText(
        systemHeader,
        "const Core::QueueSubmissionToken& laggedLightingHistoryReadReadyToken"
    ));
    EXPECT_TRUE(ContainsText(
        systemHeader,
        "const Core::QueueSubmissionToken& laggedLightingHistoryWriterDrainToken"
    ));
    EXPECT_TRUE(ContainsText(systemHeader, "m_deferredLightingHistoryReadReadyCompletion;"));
    EXPECT_TRUE(ContainsText(systemHeader, "m_deferredLightingHistoryWriterDrainCompletion;"));
    EXPECT_FALSE(ContainsText(systemHeader, "m_deferredLightingHistoryCompletion;"));

    const usize setterOffset = systemHeader.find("void setFrameLaggedAsyncLightingEnabled(");
    const usize nextSetterOffset = systemHeader.find("[[nodiscard]] bool frameLaggedAsyncLightingEnabled", setterOffset);
    ASSERT_NE(setterOffset, AStringView::npos);
    ASSERT_NE(nextSetterOffset, AStringView::npos);
    const AStringView setter = systemHeader.substr(setterOffset, nextSetterOffset - setterOffset);
    EXPECT_TRUE(ContainsText(setter, "m_laggedLightingHistoryWriterDrainToken = m_laggedLightingHistorySubmissionToken"));
    EXPECT_TRUE(ContainsText(setter, "m_laggedLightingHistoryWriterDrainGeneration = m_laggedLightingHistoryGeneration"));
    EXPECT_TRUE(ContainsText(setter, "resetLaggedLightingHistoryReadTracking();"));
    EXPECT_FALSE(ContainsText(setter, "resetLaggedLightingHistoryTracking();"));
    EXPECT_FALSE(ContainsText(systemHeader, "NWB_ASSERT(!m_laggedLightingHistoryWriterDrainToken.valid())"));

    const usize readTrackingOffset = system.find("void RendererSystem::resetLaggedLightingHistoryReadTracking");
    const usize fullTrackingOffset = system.find("void RendererSystem::resetLaggedLightingHistoryTracking", readTrackingOffset);
    const usize targetResetOffset = system.find("void RendererSystem::resetTargetGenerationStateHandoffs", fullTrackingOffset);
    ASSERT_NE(readTrackingOffset, AStringView::npos);
    ASSERT_NE(fullTrackingOffset, AStringView::npos);
    ASSERT_NE(targetResetOffset, AStringView::npos);
    const AStringView readTracking = system.substr(readTrackingOffset, fullTrackingOffset - readTrackingOffset);
    const AStringView fullTracking = system.substr(fullTrackingOffset, targetResetOffset - fullTrackingOffset);
    EXPECT_FALSE(ContainsText(readTracking, "invalidateLaggedLightingHistoryWriterDrain"));
    EXPECT_TRUE(ContainsText(fullTracking, "invalidateLaggedLightingHistoryWriterDrain();"));
    EXPECT_EQ(CountText(system, "invalidateLaggedLightingHistoryWriterDrain();"), 2u);
}


// Fresh deferred outputs must lower their first graph write from the native image origin. Active lagged history
// remains a generic import because Lighting reads its accepted descriptor-state handoff.
TEST(EcsGraphics, DeferredFirstWriteTextureImportsPreserveNativeOrigins){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString taskGraphSource;
    ASSERT_TRUE(ReadRendererSources(repoRoot, { "deferred/task_graph_deferred_lighting.cpp" }, taskGraphSource));
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());
    const usize lightingOffset = taskGraph.find("void RendererSystem::buildDeferredLightingTaskGraph");
    const usize compileOffset = taskGraph.find("if(!compiler.compile(", lightingOffset);
    ASSERT_NE(lightingOffset, AStringView::npos);
    ASSERT_NE(compileOffset, AStringView::npos);
    const AStringView deferredLighting = taskGraph.substr(lightingOffset, compileOffset - lightingOffset);

    EXPECT_TRUE(ContainsText(
        deferredLighting,
        "const auto importFirstWriteTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){\n"
        "        Core::GpuGraphResourceDesc desc = TextureResourceDesc(identity, label);\n"
        "        desc.setInitialState(Core::ResourceStates::Unknown);\n"
        "        return m_deferredLightingTaskGraph.importTexture(texture, desc);\n"
        "    };"
    ));
    EXPECT_EQ(CountText(deferredLighting, "importFirstWriteTexture("), 5u);
    EXPECT_TRUE(ContainsText(
        deferredLighting,
        "const Core::GpuGraphResourceId opaqueColor = importFirstWriteTexture(\n"
        "        deferredTargets.opaqueColor,"
    ));
    EXPECT_TRUE(ContainsText(
        deferredLighting,
        "const Core::GpuGraphResourceId compositeColor = importFirstWriteTexture(\n"
        "        deferredTargets.compositeColor,"
    ));
    EXPECT_TRUE(ContainsText(
        deferredLighting,
        "historyCopyDestinationShadowVisibility = history\n"
        "            ? shadowVisibility\n"
        "            : importFirstWriteTexture("
    ));
    EXPECT_TRUE(ContainsText(
        deferredLighting,
        "historyCopyDestinationCausticIrradiance = history\n"
        "            ? causticIrradiance\n"
        "            : importFirstWriteTexture("
    ));
    EXPECT_TRUE(ContainsText(
        deferredLighting,
        "historyCopyDestinationSurfelIrradiance = history\n"
        "            ? surfelIrradiance\n"
        "            : importFirstWriteTexture("
    ));
    EXPECT_TRUE(ContainsText(
        deferredLighting,
        "const Core::GpuGraphResourceId shadowVisibility = importTexture(\n"
        "        history ? history->shadowVisibility : deferredTargets.shadowVisibility,"
    ));
    EXPECT_TRUE(ContainsText(
        deferredLighting,
        "const Core::GpuGraphResourceId causticIrradiance = importTexture(\n"
        "        history ? history->causticIrradiance : deferredTargets.causticIrradiance,"
    ));
    EXPECT_TRUE(ContainsText(
        deferredLighting,
        "const Core::GpuGraphResourceId surfelIrradiance = importTexture(\n"
        "        history ? history->surfelIrradiance : deferredTargets.surfelIrradiance,"
    ));
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
    ASSERT_TRUE(ReadRendererSources(
        repoRoot,
        {
            "material/task_graph_resource_sets.h",
            "deferred/task_graph_graphics_prefix.cpp",
            "csg/task_graph_transparent_interval_tasks.cpp",
            "avboit/task_graph_occupancy_tasks.cpp",
            "avboit/task_graph_extinction_integration_tasks.cpp",
            "avboit/task_graph_accumulation_tasks.cpp",
            "raytrace/task_graph_shadow_prepare.cpp",
            "deferred/task_graph_deferred_lighting.cpp",
        },
        taskGraphSource
    ));
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
    EXPECT_TRUE(ContainsText(ui, "appendDrawTextureUse(drawCommand)"));
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
    ASSERT_TRUE(ReadRendererSources(
        repoRoot,
        {
            "raytrace/task_graph_shadow_prepare_tasks.h",
            "raytrace/task_graph_shadow_prepare_tasks.cpp",
            "raytrace/task_graph_shadow_prepare.cpp",
        },
        taskGraphSource
    ));
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
    ASSERT_TRUE(ReadRendererSources(
        repoRoot,
        {
            "raytrace/task_graph_shadow_prepare.cpp",
            "raytrace/task_graph_shadow_visibility.cpp",
            "raytrace/task_graph_surfel_gi.cpp",
            "deferred/task_graph_deferred_lighting.cpp",
        },
        taskGraphSource
    ));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.h", rayTracingHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.cpp", rayTracingSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_swbvh.cpp", swBvhSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "mesh" / "renderer_mesh_types.h", meshTypesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "shared" / "renderer_state.h", rendererStateHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "shared" / "renderer_state.cpp", rendererStateSource));
    ASSERT_TRUE(ReadRendererSystemSources(repoRoot, systemSource));
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

