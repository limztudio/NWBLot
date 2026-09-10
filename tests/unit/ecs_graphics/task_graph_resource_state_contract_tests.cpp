// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_contract_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_task_graph_resource_state_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace EcsGraphicsTaskGraphContractTestDetail;
using EcsGraphicsTaskGraphContractTestDetail::AString;


// The late history-copy packet prepares all three filtered return candidates while recording and commits them only
// from its exact accepted callback. Its task token remains the source of truth when that callback reports failure.
TEST(EcsGraphics, LaggedHistoryReturnCachesPublishOnlyOnTaskAcceptance){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    ASSERT_TRUE(ReadRendererFramePipelineRuntimeSources(repoRoot, systemSource));
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
    EXPECT_FALSE(ContainsText(system, "historyCopyStateBindings"));
    EXPECT_FALSE(ContainsText(system, "GpuTaskPacketStateBinding"));
    EXPECT_TRUE(ContainsText(
        system.substr(submitOffset, tokenOffset - submitOffset),
        "m_deferredLaggedLightingHistoryTask,\n"
        "                &historyCopyRecordedCallback,"
    ));

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
    ASSERT_TRUE(ReadRendererSources(repoRoot, { "deferred/task_graph_suffix_builder.cpp", "renderer_frame_pipeline_graph.cpp" }, taskGraphSource));
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
    ASSERT_TRUE(ReadRendererFramePipelineRuntimeSources(repoRoot, systemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline.h", systemHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_shadow_visibility.cpp", shadowVisibilityTaskGraphSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph.cpp", deferredLightingTaskGraphSource));
    const AStringView system(systemSource.data(), systemSource.size());
    const AStringView systemHeader(systemHeaderSource.data(), systemHeaderSource.size());
    const AStringView shadowVisibility(shadowVisibilityTaskGraphSource.data(), shadowVisibilityTaskGraphSource.size());
    const AStringView lighting(deferredLightingTaskGraphSource.data(), deferredLightingTaskGraphSource.size());
    const usize renderOffset = system.find("void RendererFramePipeline::render(");
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
        "laggedLightingHistoryWriterWaitPending && !m_deferredLightingHistoryWriterDrainCompletion.valid()"
    ), 1u);
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

    const usize readTrackingOffset = system.find("void RendererFramePipeline::resetLaggedLightingHistoryReadTracking");
    const usize fullTrackingOffset = system.find("void RendererFramePipeline::resetLaggedLightingHistoryTracking", readTrackingOffset);
    const usize targetResetOffset = system.find("void RendererFramePipeline::resetTargetGenerationStateHandoffs", fullTrackingOffset);
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
    AString avboitTargetsSource;
    ASSERT_TRUE(ReadRendererSources(repoRoot, { "deferred/task_graph_suffix_builder.cpp", "renderer_frame_pipeline_graph.cpp" }, taskGraphSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "avboit" / "avboit_targets.cpp", avboitTargetsSource));
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());
    const AStringView avboitTargets(avboitTargetsSource.data(), avboitTargetsSource.size());
    const usize lightingOffset = taskGraph.find("void RendererFramePipeline::buildDeferredLightingTaskGraph");
    const usize compileOffset = taskGraph.find("if(!compiler.compile(", lightingOffset);
    ASSERT_NE(lightingOffset, AStringView::npos);
    ASSERT_NE(compileOffset, AStringView::npos);
    const AStringView deferredLighting = taskGraph.substr(lightingOffset, compileOffset - lightingOffset);
    const AStringView deferredSuffix = taskGraph;

    EXPECT_TRUE(ContainsText(
        deferredLighting,
        "const auto importFirstWriteTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){\n"
        "        Core::GpuGraphResourceDesc desc = TextureResourceDesc(identity, label);\n"
        "        desc.setInitialState(Core::ResourceStates::Unknown);\n"
        "        return m_deferredLightingTaskGraph.importTexture(texture, desc);\n"
        "    };"
    ));
    EXPECT_EQ(CountText(deferredLighting, "importFirstWriteTexture("), 10u);
    EXPECT_EQ(CountText(deferredSuffix, "importFirstWriteTexture("), 11u);
    EXPECT_TRUE(ContainsText(
        deferredLighting,
        "const auto importAvboitTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){\n"
        "        return clearAvboitTargets\n"
        "            ? importFirstWriteTexture(texture, identity, label)\n"
        "            : importTexture(texture, identity, label)\n"
        "        ;\n"
        "    };"
    ));
    EXPECT_EQ(CountText(deferredLighting, " = importAvboitTexture("), 12u);
    for(const AStringView resource : {
        AStringView("refractionDepth"), AStringView("refractionNormalIor"), AStringView("refractionTintCoverage"),
        AStringView("refractionInstance"), AStringView("refractionSpecularRoughness"), AStringView("refractionResolve"),
        AStringView("avboitForegroundColor"), AStringView("avboitForegroundExtinction"),
    }){
        AString declaration(resource.data(), resource.size());
        declaration += " = importAvboitTexture(";
        EXPECT_TRUE(ContainsText(deferredLighting, AStringView(declaration.data(), declaration.size())));
    }
    EXPECT_EQ(CountText(avboitTargets, ".setInitialState(Core::ResourceStates::Common)"), 4u);
    EXPECT_EQ(CountText(avboitTargets, ".setKeepInitialState(true)"), 4u);
    EXPECT_EQ(CountText(
        avboitTargets,
        ".enableAutomaticStateTracking(Core::ResourceStates::Common)"
    ), 1u);
    EXPECT_TRUE(ContainsText(
        deferredLighting,
        "const Core::GpuGraphResourceId albedo = importFirstWriteTexture(\n"
        "        deferredTargets.albedo,"
    ));
    EXPECT_TRUE(ContainsText(
        deferredLighting,
        "const Core::GpuGraphResourceId normal = importFirstWriteTexture(\n"
        "        deferredTargets.normal,"
    ));
    EXPECT_TRUE(ContainsText(
        deferredLighting,
        "const Core::GpuGraphResourceId worldPosition = importFirstWriteTexture(\n"
        "        deferredTargets.worldPosition,"
    ));
    EXPECT_TRUE(ContainsText(
        deferredLighting,
        "const Core::GpuGraphResourceId depth = importFirstWriteTexture(\n"
        "        deferredTargets.depth,"
    ));
    EXPECT_TRUE(ContainsText(
        deferredLighting,
        "const Core::GpuGraphResourceId opaqueColor = importFirstWriteTexture(\n"
        "        deferredTargets.opaqueColor,"
    ));
    EXPECT_TRUE(ContainsText(
        deferredSuffix,
        "const Core::GpuGraphResourceId compositeColor = importFirstWriteTexture(\n"
        "        targets.compositeColor,"
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


// G-buffer initialization belongs to the first attachment pass on tile GPUs. Follow-up raster tasks must declare
// preservation explicitly because automatic barrier recovery resumes dynamic rendering with LOAD operations.
TEST(EcsGraphics, GbufferClearsUseFirstTilePassAndContinuationLoads){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString gbufferTaskSource;
    AString prefixSource;
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / "deferred" / "task_graph_gbuffer_task.cpp",
        gbufferTaskSource
    ));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graphics_prefix.cpp",
        prefixSource
    ));
    const AStringView gbufferTask(gbufferTaskSource.data(), gbufferTaskSource.size());
    const AStringView prefix(prefixSource.data(), prefixSource.size());

    const usize peelDispatch = gbufferTask.find("csgSystem.dispatchCsgIntervalPeels(");
    const usize beginRenderPass = gbufferTask.find(
        "commandList.beginRenderPass(*deferredTargets.framebuffer, renderPassParameters);"
    );
    ASSERT_NE(peelDispatch, AStringView::npos);
    ASSERT_NE(beginRenderPass, AStringView::npos);
    EXPECT_LT(peelDispatch, beginRenderPass);
    EXPECT_TRUE(ContainsText(
        gbufferTask,
        "renderPassParameters.colorClearValues[NWB_MESH_GBUFFER_BASE_COLOR_LOCATION] = "
        "ECSRenderDetail::s_ClearColor;"
    ));
    EXPECT_TRUE(ContainsText(
        gbufferTask,
        "renderPassParameters.colorClearValues[NWB_MESH_GBUFFER_NORMAL_LOCATION] =\n"
        "        ECSRenderDetail::s_GBufferNormalClearColor;"
    ));
    EXPECT_TRUE(ContainsText(
        gbufferTask,
        "renderPassParameters.colorClearValues[NWB_MESH_GBUFFER_WORLD_POSITION_LOCATION] =\n"
        "        ECSRenderDetail::s_GBufferWorldPositionClearColor;"
    ));
    EXPECT_TRUE(ContainsText(
        gbufferTask,
        "renderPassParameters.colorAttachmentActions[attachmentIndex].loadAction = "
        "Core::RenderPassLoadAction::Clear;"
    ));
    EXPECT_TRUE(ContainsText(
        gbufferTask,
        "renderPassParameters.depthAttachmentActions.loadAction = Core::RenderPassLoadAction::Clear;"
    ));

    EXPECT_FALSE(ContainsText(prefix, "render.graphics_prefix.deferred_clear_albedo"));
    EXPECT_FALSE(ContainsText(prefix, "render.graphics_prefix.deferred_clear_normal"));
    EXPECT_FALSE(ContainsText(prefix, "render.graphics_prefix.deferred_clear_world_position"));
    EXPECT_FALSE(ContainsText(prefix, "render.graphics_prefix.deferred_clear_depth"));
    EXPECT_TRUE(ContainsText(prefix, "render.graphics_prefix.deferred_clear_opaque_color"));
    EXPECT_FALSE(ContainsText(prefix, "m_graphicsPrefixDeferredClearFirstTask"));
    EXPECT_TRUE(ContainsText(prefix, ".beforeClear = &BeginGraphClearTimingRecord,"));
    EXPECT_TRUE(ContainsText(prefix, ".afterClear = &EndGraphClearTimingRecord,"));
    EXPECT_TRUE(ContainsText(prefix, "G-buffer timing owns that fused work."));
    EXPECT_TRUE(ContainsText(
        prefix,
        "gbufferResourceUses.push_back(WriteUse(albedo, Core::ResourceStates::RenderTarget));"
    ));
    EXPECT_TRUE(ContainsText(
        prefix,
        "gbufferResourceUses.push_back(WriteUse(normal, Core::ResourceStates::RenderTarget));"
    ));
    EXPECT_TRUE(ContainsText(
        prefix,
        "gbufferResourceUses.push_back(WriteUse(worldPosition, Core::ResourceStates::RenderTarget));"
    ));
    EXPECT_TRUE(ContainsText(
        prefix,
        "gbufferResourceUses.push_back(WriteUse(depth, Core::ResourceStates::DepthWrite));"
    ));

    EXPECT_TRUE(ContainsText(
        prefix,
        "opaqueSharedComputeEmulationRasterResourceUses.push_back(\n"
        "            ReadWriteUse(albedo, Core::ResourceStates::RenderTarget)"
    ));
    EXPECT_TRUE(ContainsText(
        prefix,
        "opaqueSharedComputeEmulationRasterResourceUses.push_back(\n"
        "            ReadWriteUse(depth, Core::ResourceStates::DepthWrite)"
    ));
    EXPECT_TRUE(ContainsText(
        prefix,
        "csgIntervalSampleResourceUses.push_back(ReadWriteUse(albedo, Core::ResourceStates::RenderTarget));"
    ));
    EXPECT_TRUE(ContainsText(
        prefix,
        "csgIntervalSampleResourceUses.push_back(ReadWriteUse(depth, Core::ResourceStates::DepthWrite));"
    ));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

