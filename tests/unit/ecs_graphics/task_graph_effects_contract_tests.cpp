// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_contract_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_task_graph_effects_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace EcsGraphicsTaskGraphContractTestDetail;
using EcsGraphicsTaskGraphContractTestDetail::AString;


// Every surfel stage consumes the same packed [0,1] G-buffer normal contract. Keep the decode in one shader include so
// spawn cannot silently store a different normal space from resolve and upsample.
TEST(EcsGraphics, SurfelGbufferNormalsSharePackedDecodeContract){
    TestArena testArena;
    const TestPath surfelDirectory = RepoRoot(testArena) / "impl" / "assets" / "graphics" / "gi" / "surfel";

    AString gbufferSource;
    AString spawnSource;
    AString resolveSource;
    AString upsampleSource;
    ASSERT_TRUE(ReadTextFile(surfelDirectory / "surfel_gbuffer.slangi", gbufferSource));
    ASSERT_TRUE(ReadTextFile(surfelDirectory / "surfel_spawn_cs.slang", spawnSource));
    ASSERT_TRUE(ReadTextFile(surfelDirectory / "surfel_resolve_cs.slang", resolveSource));
    ASSERT_TRUE(ReadTextFile(surfelDirectory / "surfel_upsample_cs.slang", upsampleSource));
    const AStringView gbuffer(gbufferSource.data(), gbufferSource.size());
    const AStringView spawn(spawnSource.data(), spawnSource.size());
    const AStringView resolve(resolveSource.data(), resolveSource.size());
    const AStringView upsample(upsampleSource.data(), upsampleSource.size());

    EXPECT_TRUE(ContainsText(gbuffer, "return normalize(packedNormal * 2.0 - 1.0);"));
    EXPECT_TRUE(ContainsText(spawn, "#include \"surfel_gbuffer.slangi\""));
    EXPECT_TRUE(ContainsText(resolve, "#include \"surfel_gbuffer.slangi\""));
    EXPECT_TRUE(ContainsText(upsample, "#include \"surfel_gbuffer.slangi\""));
    EXPECT_TRUE(ContainsText(spawn, "const float3 worldNormal = nwbSurfelDecodeGbufferNormal(rawNormal);"));
    EXPECT_TRUE(ContainsText(resolve, "const float3 normal = nwbSurfelDecodeGbufferNormal(rawNormal);"));
    EXPECT_TRUE(ContainsText(upsample, "const float3 centerNormal = nwbSurfelDecodeGbufferNormal(rawNormal);"));
    EXPECT_TRUE(ContainsText(upsample, "const float3 tapNormal = nwbSurfelDecodeGbufferNormal(tapRawNormal);"));
    EXPECT_FALSE(ContainsText(spawn, "normalize(rawNormal);"));
    EXPECT_FALSE(ContainsText(spawn, "normalize(rawNormal * 2.0 - 1.0)"));
    EXPECT_FALSE(ContainsText(resolve, "normalize(rawNormal * 2.0 - 1.0)"));
    EXPECT_FALSE(ContainsText(upsample, "normalize(rawNormal * 2.0 - 1.0)"));
}


// Surfel GI is an explicitly promoted Compute adopter. It can select an alternate Compute family only for the
// graph-owned output-clear/compute chain; the compiler remains responsible for rejecting an undeclared resource
// sharing contract or lowering the required exclusive ownership transfer.
TEST(EcsGraphics, SurfelGiPermitsOptInCrossFamilyComputeRouting){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString surfelGiSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_surfel_gi.cpp", surfelGiSource));
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
    AString rayTracingSystemSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_surfel_gi.cpp", surfelSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_surfel_gi.cpp", surfelTaskGraphSource));
    ASSERT_TRUE(ReadRendererFramePipelineRuntimeSources(repoRoot, systemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.cpp", rayTracingSystemSource));
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

    const usize readbackOffset = surfelTaskGraph.find("void RendererFramePipeline::declareDeferredSurfelCountReadbackTask");
    ASSERT_NE(readbackOffset, AStringView::npos);
    const AStringView readback = surfelTaskGraph.substr(readbackOffset);
    EXPECT_TRUE(ContainsText(readback, "rayTracingSurfelResources.counterBuffer"));
    EXPECT_TRUE(ContainsText(readback, ".source = counter,"));
    EXPECT_TRUE(ContainsText(readback, ".setQueue(TransferQueueRequest())"));
    EXPECT_FALSE(ContainsText(readback, ".acceptedToken ="));

    EXPECT_TRUE(ContainsText(surfelTaskGraph, ".states = m_surfelGiCounterPersistentState.source(),"));
    EXPECT_TRUE(ContainsText(system, "m_surfelGiCounterPersistentState.buildFilteredBufferSubset("));
    EXPECT_TRUE(ContainsText(system, "m_surfelGiCounterPersistentState.commit(\n                    *context->candidate"));
    EXPECT_TRUE(ContainsText(system, ".task = m_deferredSurfelGiCounterReadbackTask,"));
    EXPECT_TRUE(ContainsText(system, "scratchArena,\n                nullptr,\n                &readbackAcceptedCallback"));
    EXPECT_TRUE(ContainsText(
        system,
        "if(context->acceptedStateReady)\n"
        "                    context->renderer->m_raytracingSystem.confirmSurfelCountReadbackSubmission(token);"
    ));
    EXPECT_TRUE(ContainsText(rayTracingSystem, "m_rayTracingState.m_surfelCountReadbackSubmissionToken = submissionToken;"));
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
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_surfel_gi.cpp", surfelGiSource));
    const AStringView callback(surfelTasksSource.data(), surfelTasksSource.size());
    const AStringView surfelGi(surfelGiSource.data(), surfelGiSource.size());

    EXPECT_TRUE(ContainsText(callback, "context.declarations.textureForResource(payload.destination)"));
    EXPECT_TRUE(ContainsText(callback, "if(!destination || commandList.isRenderPassActive())"));
    EXPECT_FALSE(ContainsText(callback, "endRenderPass()"));
    EXPECT_TRUE(ContainsText(callback, "Core::GpuClearTextureTaskDesc clearDesc{"));
    EXPECT_TRUE(ContainsText(callback, ".destination = payload.destination,"));
    EXPECT_TRUE(ContainsText(callback, ".subresources = s_FramebufferSubresources,"));
    EXPECT_TRUE(ContainsText(callback, ".valueType = Core::GpuClearTextureTaskValueType::Float,"));
    EXPECT_TRUE(ContainsText(callback, ".floatValue = Core::Color(0.f, 0.f, 0.f, 0.f),"));
    EXPECT_TRUE(ContainsText(callback, "context.commandIrCapture"));
    EXPECT_TRUE(ContainsText(callback, "captureClearTexture("));
    EXPECT_TRUE(ContainsText(callback, "commandList.clearTextureFloat(*destination, clearDesc.subresources, clearDesc.floatValue);"));

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
    ASSERT_TRUE(ReadRendererSources(repoRoot, { "renderer_frame_pipeline_graph.cpp" }, taskGraphSource));
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());

    const usize lightingOffset = taskGraph.find("void RendererFramePipeline::buildDeferredLightingTaskGraph");
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
            "renderer_frame_pipeline_graph_caustics.cpp",
            "renderer_frame_pipeline_graph_surfel_gi.cpp",
            "renderer_frame_pipeline_graph.cpp",
        },
        taskGraphSource
    ));
    ASSERT_TRUE(ReadRendererFramePipelineRuntimeSources(repoRoot, systemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline.h", systemHeaderSource));
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());
    const AStringView system(systemSource.data(), systemSource.size());
    const AStringView systemHeader(systemHeaderSource.data(), systemHeaderSource.size());

    const usize softwareCausticsOffset = taskGraph.find("bool RendererFramePipeline::declareDeferredSoftwareCausticsTask");
    const usize surfelGiOffset = taskGraph.find("bool RendererFramePipeline::declareDeferredSurfelGiTask", softwareCausticsOffset);
    const usize deferredLightingOffset = taskGraph.find("void RendererFramePipeline::buildDeferredLightingTaskGraph", surfelGiOffset);
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
    EXPECT_TRUE(ContainsText(hardwareCaustics, ".states = m_hardwareCausticAccumulatorPersistentState.source(),"));
    EXPECT_TRUE(ContainsText(hardwareCaustics, "m_hardwareCausticAccumulatorPersistentState.valid()"));
    EXPECT_EQ(
        CountText(
            hardwareCaustics,
            ".setExternalStateSources(accumulatorStateSources, accumulatorStateSourceCount)"
        ),
        3u
    );
    EXPECT_FALSE(ContainsText(system, "deferredStateBindings"));
    EXPECT_FALSE(ContainsText(system, "m_causticIrradianceLightingState"));
    EXPECT_TRUE(ContainsText(hardwareLifecycle, "prepareHardwareCausticsTask"));
    EXPECT_TRUE(ContainsText(hardwareLifecycle, "m_hardwareCausticAccumulatorPersistentState.buildFilteredResourceSubset("));
    EXPECT_TRUE(ContainsText(hardwareLifecycle, "acceptHardwareCausticsTask"));
    EXPECT_TRUE(ContainsText(hardwareLifecycle, "m_hardwareCausticAccumulatorPersistentState.commit("));
    EXPECT_FALSE(ContainsText(hardwareLifecycle, "replaceTextureSubset("));
    EXPECT_TRUE(ContainsText(
        system,
        ".task = m_deferredHardwareCausticsTask,\n"
        "            .context = &hardwareCausticsStateLifecycle,\n"
        "            .invoke = prepareHardwareCausticsTask,"
    ));
    EXPECT_TRUE(ContainsText(
        system,
        ".task = m_deferredHardwareCausticsTask,\n"
        "            .context = &hardwareCausticsStateLifecycle,\n"
        "            .invoke = acceptHardwareCausticsTask,"
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


// FrontierSafe normally closes a packet at a cross-queue consumer. These direct serial effect chains instead own
// one timing/acceptance packet, so every accumulator alternative and semantic tail must opt in explicitly.
TEST(EcsGraphics, FrontierSafeEffectChainsRetainTheirSemanticPackets){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString softwareCausticsSource;
    AString surfelGiSource;
    AString deferredLightingSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_caustics.cpp", softwareCausticsSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_surfel_gi.cpp", surfelGiSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph.cpp", deferredLightingSource));
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


// A fully prepared soft-transparent frame selects the split graph route from production state alone. The retained
// monolithic callback remains a natural compatibility fallback, not a behavior-selectable benchmark arm.
TEST(EcsGraphics, SoftTransparentFoldHasNoProductionTestControl){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemHeaderSource;
    AString systemSource;
    AString shadowVisibilitySource;
    AString smokeCmakeSource;
    AString stressTestProjectSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline.h", systemHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline.cpp", systemSource));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_shadow_visibility.cpp",
        shadowVisibilitySource
    ));
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / "CMakeLists.txt", smokeCmakeSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / "stress_test_project.cpp", stressTestProjectSource));
    const AStringView systemHeader(systemHeaderSource.data(), systemHeaderSource.size());
    const AStringView system(systemSource.data(), systemSource.size());
    const AStringView shadowVisibility(shadowVisibilitySource.data(), shadowVisibilitySource.size());
    const AStringView smokeCmake(smokeCmakeSource.data(), smokeCmakeSource.size());
    const AStringView stressTestProject(stressTestProjectSource.data(), stressTestProjectSource.size());

    EXPECT_TRUE(ContainsText(
        shadowVisibility,
        "const bool splitSoftTransparentFold = preparedSoftTransparentFoldCandidate;"
    ));
    EXPECT_TRUE(ContainsText(shadowVisibility, "if(splitSoftTransparentFold){"));
    EXPECT_TRUE(ContainsText(shadowVisibility, "Core::GpuTaskId shadowVisibilityDependency = prefixTask;"));
    EXPECT_FALSE(ContainsText(shadowVisibility, "graphOwnedSoftTransparentFoldEnabled"));
    EXPECT_FALSE(ContainsText(shadowVisibility, "SoftTransparentShadowFoldEnabledForTesting"));
    EXPECT_FALSE(ContainsText(shadowVisibility, "soft-transparent shadow-fold benchmark path active"));
    EXPECT_FALSE(ContainsText(systemHeader, "setGraphOwnedSoftTransparentShadowFoldEnabledForTesting"));
    EXPECT_FALSE(ContainsText(systemHeader, "m_graphOwnedSoftTransparentShadowFoldEnabledForTesting"));
    EXPECT_FALSE(ContainsText(systemHeader, "m_graphOwnedSoftTransparentShadowFoldBenchmarkForTesting"));
    EXPECT_FALSE(ContainsText(systemHeader, "m_reportedGraphOwnedSoftTransparentShadowFoldBenchmarkForTesting"));
    EXPECT_FALSE(ContainsText(system, "setGraphOwnedSoftTransparentShadowFoldEnabledForTesting"));
    EXPECT_FALSE(ContainsText(smokeCmake, "nwb_soft_transparent_shadow_fold_graph_benchmark"));
    EXPECT_FALSE(ContainsText(smokeCmake, "nwb_soft_transparent_shadow_fold_monolithic_benchmark"));
    EXPECT_FALSE(ContainsText(smokeCmake, "NWB_SOFT_TRANSPARENT_SHADOW_FOLD_BENCHMARK"));
    EXPECT_FALSE(ContainsText(smokeCmake, "NWB_SOFT_TRANSPARENT_SHADOW_FOLD_MONOLITHIC_BENCHMARK"));
    EXPECT_FALSE(ContainsText(stressTestProject, "NWB_SOFT_TRANSPARENT_SHADOW_FOLD_BENCHMARK"));
    EXPECT_FALSE(ContainsText(stressTestProject, "NWB_SOFT_TRANSPARENT_SHADOW_FOLD_MONOLITHIC_BENCHMARK"));
    EXPECT_FALSE(ContainsText(
        stressTestProject,
        "StressTestSmokeProject: enabled graph-owned soft-transparent shadow-fold benchmark"
    ));
    EXPECT_FALSE(ContainsText(
        stressTestProject,
        "StressTestSmokeProject: enabled retained monolithic soft-transparent shadow-fold benchmark"
    ));
    EXPECT_FALSE(ContainsText(stressTestProject, "NWB Soft Transparent Shadow Fold Benchmark"));
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
            "renderer_frame_pipeline_graphics_prefix.cpp",
            "renderer_frame_pipeline_graph.cpp",
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


// Software-caustics scratch is private on both the dedicated Compute route and its legal Graphics fallback. Only
// the cross-queue irradiance return cache is route-conditional; all retained state publishes from the exact task.
TEST(EcsGraphics, SoftwareCausticsScratchRetainsAcceptedStateAcrossGraphicsRoute){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    AString causticsSource;
    ASSERT_TRUE(ReadRendererFramePipelineRuntimeSources(repoRoot, systemSource));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_caustics.cpp",
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
    const AStringView acceptedCaustics = system.substr(candidatesOffset, callbacksOffset - candidatesOffset);
    EXPECT_FALSE(ContainsText(system, "m_causticIrradianceLightingState"));
    EXPECT_TRUE(ContainsText(acceptedCaustics, "if(context->runsOnCompute){"));
    EXPECT_TRUE(ContainsText(acceptedCaustics, "m_causticIrradianceReturnState.buildFilteredResourceSubset("));
    EXPECT_TRUE(ContainsText(acceptedCaustics, "m_causticsComputePersistentState.buildFilteredResourceSubset("));
    EXPECT_TRUE(ContainsText(acceptedCaustics, "context->renderer->m_causticIrradianceReturnState.commit("));
    EXPECT_TRUE(ContainsText(acceptedCaustics, "context->renderer->m_causticsComputePersistentState.commit("));
    EXPECT_TRUE(ContainsText(
        system,
        ".task = m_deferredSoftwareCausticsTask,\n"
        "            .context = &softwareCausticsStateLifecycle,\n"
        "            .invoke = prepareSoftwareCausticsTask,"
    ));
    EXPECT_TRUE(ContainsText(
        system,
        ".task = m_deferredSoftwareCausticsTask,\n"
        "            .context = &softwareCausticsStateLifecycle,\n"
        "            .invoke = acceptSoftwareCausticsTask,"
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


// Depth Warp and Integration are each declared once as merge-capable, Compute-preferred semantic tasks. The
// compiler may independently retain or collapse either stage, while any retained Compute route keeps the explicit
// same-family and cross-family auxiliary-transport opt-ins.
TEST(EcsGraphics, NaturalAvboitComputeStagesPermitCompilerOwnedRouting){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString taskGraphSource;
    ASSERT_TRUE(ReadRendererSources(repoRoot, { "renderer_frame_pipeline_graph.cpp" }, taskGraphSource));
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());

    const usize schedulingOffset = taskGraph.find("Core::GpuTaskSchedulingHint avboitComputeScheduling");
    const usize accumulationOffset = taskGraph.find("AvboitAccumulationGraphTask::Payload", schedulingOffset);
    ASSERT_NE(schedulingOffset, AStringView::npos);
    ASSERT_NE(accumulationOffset, AStringView::npos);
    ASSERT_LT(schedulingOffset, accumulationOffset);
    const AStringView naturalComputeStages = taskGraph.substr(schedulingOffset, accumulationOffset - schedulingOffset);

    EXPECT_TRUE(ContainsText(naturalComputeStages, "avboitComputeScheduling.forceSubmissionBoundary = false"));
    EXPECT_TRUE(ContainsText(naturalComputeStages, "avboitComputeScheduling.allowPacketMerge = true"));
    EXPECT_TRUE(ContainsText(naturalComputeStages, "avboitComputeScheduling.mergeWithPrevious = true"));
    EXPECT_TRUE(ContainsText(naturalComputeStages, "avboitComputeScheduling.allowMergeAcrossConsumerFrontier = true"));
    EXPECT_TRUE(ContainsText(naturalComputeStages, "EnableSameFamilyComputeEffectRouting(avboitComputeScheduling);"));
    EXPECT_FALSE(ContainsText(naturalComputeStages, "EnableSameFamilyComputeEffectRouting(avboitComputeScheduling, false)"));
    EXPECT_TRUE(ContainsText(naturalComputeStages, "EnableCrossFamilyComputeEffectRouting(avboitComputeScheduling)"));
    EXPECT_TRUE(ContainsText(naturalComputeStages, "avboitComputeScheduling.allowTimingFeedbackRouting = true"));
    EXPECT_TRUE(ContainsText(naturalComputeStages, "avboitComputeScheduling.allowCrossClassTimingFeedbackRouting = true"));
    const usize depthWarpDescOffset = naturalComputeStages.find("Core::GpuTaskDesc depthWarpDesc;");
    const usize depthWarpFailureOffset = naturalComputeStages.find(
        "if(!m_avboitSystem.taskGraphStage().m_depthWarpTask.valid())",
        depthWarpDescOffset
    );
    const usize integrationDescOffset = naturalComputeStages.find("Core::GpuTaskDesc integrationDesc;", depthWarpFailureOffset);
    const usize integrationFailureOffset = naturalComputeStages.find(
        "if(!m_avboitSystem.taskGraphStage().m_integrationTask.valid())",
        integrationDescOffset
    );
    ASSERT_NE(depthWarpDescOffset, AStringView::npos);
    ASSERT_NE(depthWarpFailureOffset, AStringView::npos);
    ASSERT_NE(integrationDescOffset, AStringView::npos);
    ASSERT_NE(integrationFailureOffset, AStringView::npos);
    ASSERT_LT(depthWarpDescOffset, depthWarpFailureOffset);
    ASSERT_LT(depthWarpFailureOffset, integrationDescOffset);
    ASSERT_LT(integrationDescOffset, integrationFailureOffset);
    const AStringView depthWarpStage = naturalComputeStages.substr(
        depthWarpDescOffset,
        depthWarpFailureOffset - depthWarpDescOffset
    );
    const AStringView integrationStage = naturalComputeStages.substr(
        integrationDescOffset,
        integrationFailureOffset - integrationDescOffset
    );
    for(const AStringView computeStage : { depthWarpStage, integrationStage }){
        EXPECT_TRUE(ContainsText(computeStage, ".setQueue(ComputeQueueRequest())"));
        EXPECT_TRUE(ContainsText(computeStage, ".setScheduling(avboitComputeScheduling)"));
        EXPECT_TRUE(ContainsText(computeStage, ".setTimingMetadata(avboitComputeStageTiming)"));
        EXPECT_FALSE(ContainsText(computeStage, "GraphicsComputeQueueRequest()"));
    }
    EXPECT_TRUE(ContainsText(
        naturalComputeStages,
        "const Core::GpuTaskTimingMetadata avboitComputeStageTiming ="
    ));
    EXPECT_TRUE(ContainsText(
        naturalComputeStages,
        "AvboitComputeStageTimingMetadata(deferredTargets.avboit)"
    ));
    EXPECT_FALSE(ContainsText(naturalComputeStages, "AvboitIntegrationTimingMetadata"));
    EXPECT_TRUE(ContainsText(depthWarpStage, "render.avboit.depth_warp"));
    EXPECT_TRUE(ContainsText(integrationStage, "render.avboit.integration"));
    EXPECT_FALSE(ContainsText(taskGraph, "splitAvboitStages"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

