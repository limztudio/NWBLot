// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_contract_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_task_graph_effects_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_IMPL = "impl";
static constexpr AStringView s_ECS_RENDER = "ecs_render";
static constexpr AStringView s_RENDERER_FRAME_PIPELINE_GRAPH_SURFEL_GI_ = "renderer_frame_pipeline_graph_surfel_gi.cpp";
static constexpr AStringView s_RAYTRACE = "raytrace";
static constexpr AStringView s_RENDERER_FRAME_PIPELINE_GRAPH_CAUSTICS_CPP = "renderer_frame_pipeline_graph_caustics.cpp";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace EcsGraphicsTaskGraphContractTestDetail;
using EcsGraphicsTaskGraphContractTestDetail::AString;


// The persistent counter crosses the Compute GI packet and optional Transfer readback tail.  Its next-frame imported cache must therefore be concurrently shared by each actual transport rather than retaining a stale exclusive Transfer owner with no future release destination.
TEST(EcsGraphics, SurfelCounterSharesComputeAndTransferReadbackPath){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString surfelSource;
    AString surfelTaskGraphSource;
    AString readbackSource;
    AString readbackLifecycleSource;
    AString systemSource;
    AString rayTracingSystemSource;
    ASSERT_TRUE(ReadRendererSources(
        repoRoot,
        {
            "raytrace/rt_surfel_tasks.h",
            "raytrace/rt_surfel_tasks.cpp",
            "raytrace/rt_surfel_pipelines.cpp",
            "raytrace/rt_surfel_resources.cpp",
            "raytrace/rt_surfel_render.cpp",
        },
        surfelSource
    ));
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ECS_RENDER / s_RENDERER_FRAME_PIPELINE_GRAPH_SURFEL_GI_, surfelTaskGraphSource));
    ASSERT_TRUE(ReadRendererFramePipelineRuntimeSources(repoRoot, systemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ECS_RENDER / s_RAYTRACE / "raytracing_frame_resources.cpp", rayTracingSystemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ECS_RENDER / "renderer_frame_pipeline_graph_surfel_gi_readback.cpp", readbackSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ECS_RENDER / "execute/frame_execute_surfel_readback.cpp", readbackLifecycleSource));
    const AStringView readbackOwner(readbackSource.data(), readbackSource.size());
    const AStringView readbackLifecycle(readbackLifecycleSource.data(), readbackLifecycleSource.size());
    const AStringView surfel(surfelSource.data(), surfelSource.size());
    const AStringView surfelTaskGraph(surfelTaskGraphSource.data(), surfelTaskGraphSource.size());
    const AStringView system(systemSource.data(), systemSource.size());
    const AStringView rayTracingSystem(rayTracingSystemSource.data(), rayTracingSystemSource.size());

    const usize counterOffset = surfel.find("if(!m_rayTracingState.m_surfelCounterBuffer){");
    const usize traceArgsOffset = surfel.find("// Build-args rewrites the indirect dispatch buffer each frame.", counterOffset);
    ASSERT_NE(counterOffset, AStringView::npos);
    ASSERT_NE(traceArgsOffset, AStringView::npos);
    ASSERT_LT(counterOffset, traceArgsOffset);
    const AStringView counter = surfel.substr(counterOffset, traceArgsOffset - counterOffset);
    EXPECT_TRUE(ContainsText(counter, ".setCanHaveUAVs(true)"));
    EXPECT_TRUE(ContainsText(counter, ".setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)"));
    EXPECT_TRUE(ContainsText(counter, ".setDebugName(Name(\"surfel_counter\"))"));

    const usize readbackOffset = readbackOwner.find("void RendererFramePipeline::declareDeferredSurfelCountReadbackTask");
    ASSERT_NE(readbackOffset, AStringView::npos);
    const AStringView readback = readbackOwner.substr(readbackOffset);
    EXPECT_TRUE(ContainsText(readback, "rayTracingSurfelResources.counterBuffer"));
    EXPECT_TRUE(ContainsText(readback, ".source = counter,"));
    EXPECT_TRUE(ContainsText(readback, "addCopyBufferTask("));
    EXPECT_FALSE(ContainsText(readback, ".acceptedToken ="));

    EXPECT_TRUE(ContainsText(surfelTaskGraph, ".states = m_surfelGiCounterPersistentState.source(),"));
    EXPECT_TRUE(ContainsText(readbackLifecycle, "m_surfelGiCounterPersistentState.buildFilteredBufferSubset("));
    const usize retainedStateCommit = readbackLifecycle.find("context->acceptedStateReady = context->renderer->m_surfelGiCounterPersistentState.commit(");
    const usize acceptedStateGuard = readbackLifecycle.find("if(context->acceptedStateReady)", retainedStateCommit);
    const usize readbackPublication = readbackLifecycle.find("context->renderer->m_raytracingSystem.confirmSurfelCountReadbackSubmission(token);", acceptedStateGuard);
    ASSERT_NE(retainedStateCommit, AStringView::npos);
    ASSERT_NE(acceptedStateGuard, AStringView::npos);
    ASSERT_NE(readbackPublication, AStringView::npos);
    EXPECT_LT(retainedStateCommit, acceptedStateGuard);
    EXPECT_LT(acceptedStateGuard, readbackPublication);
    EXPECT_TRUE(ContainsText(system, ".task = m_deferredSurfelGiCounterReadbackTask,"));
    EXPECT_TRUE(ContainsText(system, "scratchArena,\n                nullptr,\n                &readbackAcceptedCallback"));
    EXPECT_TRUE(ContainsText(rayTracingSystem, "m_rayTracingState.m_surfelCountReadbackSubmissionToken = submissionToken;"));
    const usize readbackExecutionOffset = system.find("const bool readbackAccepted = scheduler.executeTask(");
    const usize readbackTokenOffset = system.find(
        "const Core::QueueSubmissionToken readbackSubmissionToken =",
        readbackExecutionOffset
    );
    ASSERT_NE(readbackExecutionOffset, AStringView::npos);
    ASSERT_NE(readbackTokenOffset, AStringView::npos);
    EXPECT_LT(readbackExecutionOffset, readbackTokenOffset);
    EXPECT_TRUE(ContainsText(system, "else if(!readbackAccepted || !readbackContext.acceptedStateReady){"));
}


// The renderer-local irradiance clear captures the typed command-IR record after the graph-owned CopyDest transition and permits either Compute or Graphics transport.
TEST(EcsGraphics, SurfelClearRejectsMissingDestinationAndActiveRenderPass){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString surfelTasksSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ECS_RENDER / s_RAYTRACE / "task_graph_surfel_tasks.cpp", surfelTasksSource));
    const AStringView callback(surfelTasksSource.data(), surfelTasksSource.size());

    EXPECT_TRUE(ContainsText(callback, "context.declarations.textureForResource(payload.destination)"));
    EXPECT_TRUE(ContainsText(callback, "if(!destination || commandList.isRenderPassActive())"));
    EXPECT_FALSE(ContainsText(callback, "endRenderPass()"));
}


// Caustic resolve targets start Unknown after recreation. Geometry downsample and prepare must therefore publish their first results as writes,
// a warm hardware accumulator imports only accepted Graphics packet state.
TEST(EcsGraphics, CausticGraphScratchUsesFirstWritesAndHardwareRetainsAcceptedAccumulatorState){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString softwareSource;
    AString hardwareSource;
    AString callerSource;
    AString systemSource;
    AString systemHeaderSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ECS_RENDER / s_RENDERER_FRAME_PIPELINE_GRAPH_CAUSTICS_CPP, softwareSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ECS_RENDER / s_RAYTRACE / "hardware_caustics_stage_builder.cpp", hardwareSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ECS_RENDER / "renderer_frame_pipeline_graph.cpp", callerSource));
    ASSERT_TRUE(ReadRendererFramePipelineRuntimeSources(repoRoot, systemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ECS_RENDER / "renderer_frame_pipeline.h", systemHeaderSource));
    const AStringView softwareCaustics(softwareSource.data(), softwareSource.size());
    const AStringView hardwareCaustics(hardwareSource.data(), hardwareSource.size());
    const AStringView caller(callerSource.data(), callerSource.size());
    const AStringView system(systemSource.data(), systemSource.size());
    const AStringView systemHeader(systemHeaderSource.data(), systemHeaderSource.size());
    EXPECT_TRUE(ContainsText(caller, ".accumulatorPersistentState = &m_hardwareCausticAccumulatorPersistentState,"));
    EXPECT_TRUE(ContainsText(caller, "if(!hardwareCausticsStageBuilder.declare("));

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
    EXPECT_EQ(CountText(softwarePrepare, "resolvePrepareResourceUses.push_back(ReadTextureUse("), s_ExpectedDualCount);
    EXPECT_FALSE(ContainsText(softwarePrepare, "resolvePrepareResourceUses.push_back(ReadWriteTextureUse("));
    EXPECT_EQ(CountText(softwarePrepare, "resolvePrepareResourceUses.push_back(WriteTextureUse("), s_ExpectedDualCount);
    EXPECT_TRUE(ContainsText(hardwareGeometry, "hardwareGeometryResourceUses.push_back(WriteTextureUse(\n            causticResolveGeometry,"));
    EXPECT_FALSE(ContainsText(hardwareGeometry, "hardwareGeometryResourceUses.push_back(ReadWriteTextureUse("));
    EXPECT_EQ(CountText(hardwarePrepare, "hardwareResolvePrepareResourceUses.push_back(ReadTextureUse("), s_ExpectedDualCount);
    EXPECT_FALSE(ContainsText(hardwarePrepare, "hardwareResolvePrepareResourceUses.push_back(ReadWriteTextureUse("));
    EXPECT_EQ(CountText(hardwarePrepare, "hardwareResolvePrepareResourceUses.push_back(WriteTextureUse("), s_ExpectedDualCount);

    const usize hardwareLifecycleOffset = system.find("struct HardwareCausticsStateLifecycleContext{");
    const usize deferredLightingLifecycleOffset = system.find(
        "const Core::TextureHandle deferredLightingShadowReturnTextures[]",
        hardwareLifecycleOffset
    );
    ASSERT_NE(hardwareLifecycleOffset, AStringView::npos);
    ASSERT_NE(deferredLightingLifecycleOffset, AStringView::npos);
    ASSERT_LT(hardwareLifecycleOffset, deferredLightingLifecycleOffset);
    const AStringView hardwareLifecycle = system.substr(
        hardwareLifecycleOffset,
        deferredLightingLifecycleOffset - hardwareLifecycleOffset
    );

    EXPECT_TRUE(ContainsText(system, "m_hardwareCausticAccumulatorPersistentState(arena)"));
    EXPECT_TRUE(ContainsText(system, "m_hardwareCausticAccumulatorPersistentState.reset();"));
    EXPECT_EQ(CountText(system, "m_hardwareCausticAccumulatorPersistentState.reset();"), 1u);
    EXPECT_TRUE(ContainsText(systemHeader, "Core::GpuPersistentResourceStateCache m_hardwareCausticAccumulatorPersistentState;"));
    EXPECT_TRUE(ContainsText(hardwareCaustics, "const Core::GpuTaskExternalStateSource accumulatorStateSources[]"));
    EXPECT_TRUE(ContainsText(hardwareCaustics, ".states = (*inputs.accumulatorPersistentState).source(),"));
    EXPECT_TRUE(ContainsText(hardwareCaustics, "(*inputs.accumulatorPersistentState).valid()"));
    EXPECT_EQ(
        CountText(
            hardwareCaustics,
            ".setExternalStateSources(accumulatorStateSources, accumulatorStateSourceCount)"
        ),
        3u
    );
    EXPECT_FALSE(ContainsText(system, "deferredStateBindings"));
    EXPECT_FALSE(ContainsText(system, "m_causticIrradianceLightingState"));
    // Prepare/accept bodies now live in the FrameExecuteLifecycle class; the concatenated system source
    // still owns them, so assert on the whole system rather than the execute.cpp-only accepted range.
    EXPECT_TRUE(ContainsText(system, "FrameExecuteLifecycle::PrepareHardwareCausticsTask("));
    EXPECT_TRUE(ContainsText(system, "FrameExecuteLifecycle::AcceptHardwareCausticsTask("));
    EXPECT_TRUE(ContainsText(hardwareLifecycle, "m_hardwareCausticAccumulatorPersistentState.buildFilteredResourceSubset("));
    EXPECT_TRUE(ContainsText(hardwareLifecycle, "m_hardwareCausticAccumulatorPersistentState.commit("));
    EXPECT_FALSE(ContainsText(hardwareLifecycle, "replaceTextureSubset("));
    EXPECT_TRUE(ContainsText(
        system,
        ".task = m_deferredHardwareCausticsTask,\n"
        "            .context = &hardwareCausticsStateLifecycle,\n"
        "            .invoke = FrameExecuteLifecycle::PrepareHardwareCausticsTask,"
    ));
    EXPECT_TRUE(ContainsText(
        system,
        ".task = m_deferredHardwareCausticsTask,\n"
        "            .context = &hardwareCausticsStateLifecycle,\n"
        "            .invoke = FrameExecuteLifecycle::AcceptHardwareCausticsTask,"
    ));
    EXPECT_TRUE(ContainsText(
        system,
        "(selectedCausticsSubmissionToken.valid() && !selectedCausticsStateReady)"
    ));
    EXPECT_TRUE(ContainsText(
        system,
        "if(!selectedCausticsSubmissionToken.valid())\n"
        "                    restoreCausticsCpuState();"
    ));
}


// Software-caustics scratch is private on both the dedicated Compute route and its legal Graphics fallback.
// Only the cross-queue irradiance return cache is route-conditional; all retained state publishes from the exact task.
TEST(EcsGraphics, SoftwareCausticsScratchRetainsAcceptedStateAcrossGraphicsRoute){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    AString causticsSource;
    ASSERT_TRUE(ReadRendererFramePipelineRuntimeSources(repoRoot, systemSource));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / s_RENDERER_FRAME_PIPELINE_GRAPH_CAUSTICS_CPP,
        causticsSource
    ));
    const AStringView system(systemSource.data(), systemSource.size());
    const AStringView caustics(causticsSource.data(), causticsSource.size());

    EXPECT_TRUE(ContainsText(caustics, ".states = m_causticsComputePersistentState.source(),"));
    EXPECT_TRUE(ContainsText(caustics, ".states = m_causticIrradianceReturnState.source(),"));
    EXPECT_TRUE(ContainsText(
        caustics,
        ".applicableConsumerQueueClass = Core::CommandQueue::Compute,"
    ));
    EXPECT_FALSE(ContainsText(caustics, "softwareCausticsRunsOnCompute"));

    const usize candidatesOffset = system.find("const Core::TextureHandle causticsComputeScratchTextures[]");
    const usize callbacksOffset = system.find(
        "Core::GpuTaskGraphTaskRecordedCallback normalRecordedCallbacks[",
        candidatesOffset
    );
    ASSERT_NE(candidatesOffset, AStringView::npos);
    ASSERT_NE(callbacksOffset, AStringView::npos);
    ASSERT_LT(candidatesOffset, callbacksOffset);
    EXPECT_FALSE(ContainsText(system, "m_causticIrradianceLightingState"));
    // Prepare/accept bodies now live in the FrameExecuteLifecycle class; assert lifecycle-owned strings
    // on the whole concatenated system source rather than the execute.cpp-only accepted range.
    EXPECT_TRUE(ContainsText(system, "FrameExecuteLifecycle::PrepareSoftwareCausticsTask("));
    EXPECT_TRUE(ContainsText(system, "FrameExecuteLifecycle::AcceptSoftwareCausticsTask("));
    EXPECT_TRUE(ContainsText(system, "if(context->runsOnCompute){"));
    EXPECT_TRUE(ContainsText(system, "m_causticIrradianceReturnState.buildFilteredResourceSubset("));
    EXPECT_TRUE(ContainsText(system, "m_causticsComputePersistentState.buildFilteredResourceSubset("));
    EXPECT_TRUE(ContainsText(system, "context->renderer->m_causticIrradianceReturnState.commit("));
    EXPECT_TRUE(ContainsText(system, "context->renderer->m_causticsComputePersistentState.commit("));
    EXPECT_TRUE(ContainsText(
        system,
        ".task = m_deferredSoftwareCausticsTask,\n"
        "            .context = &softwareCausticsStateLifecycle,\n"
        "            .invoke = FrameExecuteLifecycle::PrepareSoftwareCausticsTask,"
    ));
    EXPECT_TRUE(ContainsText(
        system,
        ".task = m_deferredSoftwareCausticsTask,\n"
        "            .context = &softwareCausticsStateLifecycle,\n"
        "            .invoke = FrameExecuteLifecycle::AcceptSoftwareCausticsTask,"
    ));
    EXPECT_TRUE(ContainsText(
        system,
        "(selectedCausticsSubmissionToken.valid() && !selectedCausticsStateReady)"
    ));
    EXPECT_TRUE(ContainsText(
        system,
        "if(!selectedCausticsSubmissionToken.valid())\n"
        "                    restoreCausticsCpuState();"
    ));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

