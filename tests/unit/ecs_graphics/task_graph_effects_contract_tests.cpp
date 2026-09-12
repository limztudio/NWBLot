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
    AString readbackSource;
    AString systemSource;
    AString rayTracingSystemSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_surfel_gi.cpp", surfelSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_surfel_gi.cpp", surfelTaskGraphSource));
    ASSERT_TRUE(ReadRendererFramePipelineRuntimeSources(repoRoot, systemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_frame_resources.cpp", rayTracingSystemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_surfel_gi_readback.cpp", readbackSource));
    const AStringView readbackOwner(readbackSource.data(), readbackSource.size());
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
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "hardware_caustics_stage_builder.cpp", taskGraphSource));
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());

    const usize lightingOffset = taskGraph.find("bool HardwareCausticsStageBuilder::declare(");
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

    AString softwareSource;
    AString hardwareSource;
    AString callerSource;
    AString systemSource;
    AString systemHeaderSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_caustics.cpp", softwareSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "hardware_caustics_stage_builder.cpp", hardwareSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph.cpp", callerSource));
    ASSERT_TRUE(ReadRendererFramePipelineRuntimeSources(repoRoot, systemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline.h", systemHeaderSource));
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
    AString hardwareCausticsSource;
    AString softwareResolveSource;
    AString hardwareResolveSource;
    AString avboitClearSource;
    AString avboitOccupancySource;
    AString avboitAccumulationSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_caustics.cpp", softwareCausticsSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_surfel_gi.cpp", surfelGiSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace/hardware_caustics_stage_builder.cpp", hardwareCausticsSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace/software_caustics_resolve_chain.cpp", softwareResolveSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace/hardware_caustics_resolve_chain.cpp", hardwareResolveSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "avboit/clear_chain_builder.cpp", avboitClearSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "avboit/occupancy_record_builder.cpp", avboitOccupancySource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "avboit/accumulation_record_builder.cpp", avboitAccumulationSource));
    const AStringView softwareCaustics(softwareCausticsSource.data(), softwareCausticsSource.size());
    const AStringView surfelGi(surfelGiSource.data(), surfelGiSource.size());
    const AStringView hardwareCaustics(hardwareCausticsSource.data(), hardwareCausticsSource.size());
    const AStringView softwareResolve(softwareResolveSource.data(), softwareResolveSource.size());
    const AStringView hardwareResolve(hardwareResolveSource.data(), hardwareResolveSource.size());
    const AStringView avboitClear(avboitClearSource.data(), avboitClearSource.size());
    const AStringView avboitOccupancy(avboitOccupancySource.data(), avboitOccupancySource.size());
    const AStringView avboitAccumulation(avboitAccumulationSource.data(), avboitAccumulationSource.size());

    EXPECT_TRUE(ContainsText(softwareCaustics, "accumulatorNonTemporalClearScheduling.allowMergeAcrossConsumerFrontier = true;"));
    EXPECT_TRUE(ContainsText(softwareCaustics, "accumulatorBootstrapClearScheduling.allowMergeAcrossConsumerFrontier = true;"));
    EXPECT_TRUE(ContainsText(softwareCaustics, "accumulatorDecayScheduling.allowMergeAcrossConsumerFrontier = true;"));
    EXPECT_TRUE(ContainsText(softwareCaustics, "causticsScheduling.allowMergeAcrossConsumerFrontier = true;"));
    EXPECT_TRUE(ContainsText(softwareCaustics, ".setDependencies(&causticsDependency, 1u)"));
    EXPECT_TRUE(ContainsText(softwareCaustics, "resolveChainInputs.baseScheduling = geometryScheduling;"));
    EXPECT_TRUE(ContainsText(softwareResolve, "Core::GpuTaskSchedulingHint resolvePrepareScheduling = inputs.baseScheduling;"));
    EXPECT_TRUE(ContainsText(softwareResolve, "render.software_caustics.resolve_timing_close"));

    EXPECT_TRUE(ContainsText(surfelGi, "surfelGiScheduling.allowMergeAcrossConsumerFrontier = true;"));
    EXPECT_TRUE(ContainsText(surfelGi, ".setDependencies(&surfelGiDependency, 1u)"));
    EXPECT_TRUE(ContainsText(surfelGi, "render.surfel_gi"));

    EXPECT_TRUE(ContainsText(hardwareCaustics, "accumulatorNonTemporalClearScheduling.allowMergeAcrossConsumerFrontier = true;"));
    EXPECT_TRUE(ContainsText(hardwareCaustics, "accumulatorBootstrapClearScheduling.allowMergeAcrossConsumerFrontier = true;"));
    EXPECT_TRUE(ContainsText(hardwareCaustics, "accumulatorDecayScheduling.allowMergeAcrossConsumerFrontier = true;"));
    EXPECT_TRUE(ContainsText(hardwareCaustics, "hardwareCausticsScheduling.allowMergeAcrossConsumerFrontier = true;"));
    EXPECT_TRUE(ContainsText(hardwareCaustics, ".setDependencies(&causticsDependency, 1u)"));
    EXPECT_TRUE(ContainsText(hardwareCaustics, "resolveChainInputs.baseScheduling = hardwareGeometryScheduling;"));
    EXPECT_TRUE(ContainsText(hardwareResolve, "Core::GpuTaskSchedulingHint hardwareResolvePrepareScheduling = inputs.baseScheduling;"));
    EXPECT_TRUE(ContainsText(hardwareResolve, "render.hardware_caustics.resolve_timing_close"));

    EXPECT_TRUE(ContainsText(avboitClear, "avboitClearScheduling.allowMergeAcrossConsumerFrontier = true;"));
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
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "shared" / "task_graph_stage.h", sharedTaskGraphStageSource));
    const AStringView sharedTaskGraphStage(sharedTaskGraphStageSource.data(), sharedTaskGraphStageSource.size());

    EXPECT_TRUE(ContainsText(sharedTaskGraphStage, "s_SharedComputeEmulationMaximumDrawCount = 5u;"));
    EXPECT_TRUE(ContainsText(
        sharedTaskGraphStage,
        "s_SharedComputeEmulationMaximumPhaseCount =\n"
        "    s_SharedComputeEmulationMaximumDrawCount * s_SharedComputeEmulationPhasesPerDraw;"
    ));

    struct ExpectedFifthDraw{
        AStringView path;
        AStringView generateIdentity;
        AStringView rasterIdentity;
        AStringView generateMarker;
        AStringView rasterMarker;
    };
    const ExpectedFifthDraw draws[] = {
        {
            "renderer_frame_pipeline_graphics_prefix.cpp",
            "render.graphics_prefix.opaque_shared_compute_emulation_generate_e",
            "render.graphics_prefix.opaque_shared_compute_emulation_raster_e",
            "Opaque Shared Compute Emulation Generate E",
            "Opaque Shared Compute Emulation Raster E",
        },
        {
            "avboit/occupancy_record_builder.cpp",
            "render.avboit.occupancy.shared_compute_emulation_generate_e",
            "render.avboit.occupancy.shared_compute_emulation_raster_e",
            "AVBOIT Occupancy Shared Compute Emulation Generate E",
            "AVBOIT Occupancy Shared Compute Emulation Raster E",
        },
        {
            "avboit/extinction_record_builder.cpp",
            "render.avboit.extinction.shared_compute_emulation_generate_e",
            "render.avboit.extinction.shared_compute_emulation_raster_e",
            "AVBOIT Extinction Shared Compute Emulation Generate E",
            "AVBOIT Extinction Shared Compute Emulation Raster E",
        },
        {
            "avboit/accumulation_record_builder.cpp",
            "render.avboit.accumulation.shared_compute_emulation_generate_e",
            "render.avboit.accumulation.shared_compute_emulation_raster_e",
            "AVBOIT Accumulation Shared Compute Emulation Generate E",
            "AVBOIT Accumulation Shared Compute Emulation Raster E",
        },
    };
    for(const ExpectedFifthDraw& draw : draws){
        SCOPED_TRACE(draw.path.data());
        AString source;
        ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / draw.path.data(), source));
        const AStringView owner(source.data(), source.size());
        EXPECT_TRUE(ContainsText(owner, draw.generateIdentity));
        EXPECT_TRUE(ContainsText(owner, draw.rasterIdentity));
        EXPECT_TRUE(ContainsText(owner, draw.generateMarker));
        EXPECT_TRUE(ContainsText(owner, draw.rasterMarker));
    }
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

    AString builderSource;
    AString callerSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "avboit" / "compute_effect_chain_builder.cpp", builderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph.cpp", callerSource));
    const AStringView builder(builderSource.data(), builderSource.size());
    const AStringView caller(callerSource.data(), callerSource.size());
    struct StageContract{
        AStringView function;
        AStringView description;
        AStringView validGuard;
        AStringView identity;
        AStringView callerGuard;
    };
    const StageContract stages[] = {
        {
            "bool AvboitComputeEffectChainBuilder::declareDepthWarp(",
            "Core::GpuTaskDesc depthWarpDesc;",
            "if(!m_avboitSystem.taskGraphStage().m_depthWarpTask.valid())",
            "render.avboit.depth_warp",
            "if(!avboitComputeEffectChainBuilder.declareDepthWarp(",
        },
        {
            "bool AvboitComputeEffectChainBuilder::declareIntegration(",
            "Core::GpuTaskDesc integrationDesc;",
            "if(!m_avboitSystem.taskGraphStage().m_integrationTask.valid())",
            "render.avboit.integration",
            "if(!avboitComputeEffectChainBuilder.declareIntegration(",
        },
    };
    for(const StageContract& stage : stages){
        SCOPED_TRACE(stage.function.data());
        const usize begin = builder.find(stage.function);
        ASSERT_NE(begin, AStringView::npos);
        const usize end = builder.find("\n}", begin);
        ASSERT_NE(end, AStringView::npos);
        const AStringView body = builder.substr(begin, end - begin);
        EXPECT_EQ(CountText(body, "Core::GpuTaskSchedulingHint avboitComputeScheduling;"), 1u);
        EXPECT_TRUE(ContainsText(body, "avboitComputeScheduling.forceSubmissionBoundary = false"));
        EXPECT_TRUE(ContainsText(body, "avboitComputeScheduling.allowPacketMerge = true"));
        EXPECT_TRUE(ContainsText(body, "avboitComputeScheduling.mergeWithPrevious = true"));
        EXPECT_TRUE(ContainsText(body, "avboitComputeScheduling.allowMergeAcrossConsumerFrontier = true"));
        EXPECT_TRUE(ContainsText(body, "EnableSameFamilyComputeEffectRouting(avboitComputeScheduling);"));
        EXPECT_FALSE(ContainsText(body, "EnableSameFamilyComputeEffectRouting(avboitComputeScheduling, false)"));
        EXPECT_TRUE(ContainsText(body, "EnableCrossFamilyComputeEffectRouting(avboitComputeScheduling)"));
        EXPECT_TRUE(ContainsText(body, "avboitComputeScheduling.allowTimingFeedbackRouting = true"));
        EXPECT_TRUE(ContainsText(body, "avboitComputeScheduling.allowCrossClassTimingFeedbackRouting = true"));
        EXPECT_TRUE(ContainsText(body, "const Core::GpuTaskTimingMetadata avboitComputeStageTiming ="));
        EXPECT_TRUE(ContainsText(body, "AvboitComputeStageTimingMetadata(*inputs.targets)"));
        EXPECT_FALSE(ContainsText(body, "AvboitIntegrationTimingMetadata"));
        const usize desc = body.find(stage.description);
        const usize guard = body.find(stage.validGuard, desc);
        ASSERT_NE(desc, AStringView::npos);
        ASSERT_NE(guard, AStringView::npos);
        ASSERT_LT(desc, guard);
        const AStringView declaration = body.substr(desc, guard - desc);
        EXPECT_TRUE(ContainsText(declaration, ".setQueue(RendererTaskGraphDetail::ComputeQueueRequest())"));
        EXPECT_TRUE(ContainsText(declaration, ".setScheduling(avboitComputeScheduling)"));
        EXPECT_TRUE(ContainsText(declaration, ".setTimingMetadata(avboitComputeStageTiming)"));
        EXPECT_FALSE(ContainsText(declaration, "GraphicsComputeQueueRequest()"));
        EXPECT_EQ(CountText(declaration, stage.identity), 1u);
        EXPECT_TRUE(ContainsText(body.substr(guard), "return false;"));
        const usize callerBegin = caller.find(stage.callerGuard);
        ASSERT_NE(callerBegin, AStringView::npos);
        const usize callerEnd = caller.find("\n    }", callerBegin);
        ASSERT_NE(callerEnd, AStringView::npos);
        const AStringView call = caller.substr(callerBegin, callerEnd - callerBegin);
        EXPECT_TRUE(ContainsText(call, ".targets = &deferredTargets.avboit,"));
        EXPECT_TRUE(ContainsText(call, "return;"));
    }
    EXPECT_FALSE(ContainsText(builder, "splitAvboitStages"));
    EXPECT_FALSE(ContainsText(caller, "splitAvboitStages"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

