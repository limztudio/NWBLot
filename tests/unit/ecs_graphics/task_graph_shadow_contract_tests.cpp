// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_contract_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_task_graph_shadow_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_IMPL = "impl";
static constexpr AStringView s_ECS_RENDER = "ecs_render";
static constexpr AStringView s_RAYTRACE = "raytrace";
static constexpr AStringView s_RT_SHADOW_TASKS_H = "raytrace/rt_shadow_tasks.h";
static constexpr AStringView s_RT_SHADOW_MATERIAL_CONTEXT_CPP = "raytrace/rt_shadow_material_context.cpp";
static constexpr AStringView s_RT_SHADOW_VISIBILITY_TARGET_CPP = "raytrace/rt_shadow_visibility_target.cpp";
static constexpr AStringView s_RT_SHADOW_OPAQUE_CPP = "raytrace/rt_shadow_opaque.cpp";
static constexpr AStringView s_RT_SHADOW_TRANSPARENT_CPP = "raytrace/rt_shadow_transparent.cpp";
static constexpr AStringView s_RT_SHADOW_GPU_VISIBILITY_CPP = "raytrace/rt_shadow_gpu_visibility.cpp";
static constexpr AStringView s_RT_SHADOW_PIPELINES_CPP = "raytrace/rt_shadow_pipelines.cpp";
static constexpr AStringView s_RENDERER_FRAME_PIPELINE_GRAPH_SHADOW_VIS = "renderer_frame_pipeline_graph_shadow_visibility.cpp";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace EcsGraphicsTaskGraphContractTestDetail;
using EcsGraphicsTaskGraphContractTestDetail::AString;


// Fusion consumes common channel/history outputs, so its preparation and frozen eligibility must not require RayQuery.
TEST(EcsGraphics, SoftwareSoftShadowsShareCombinedResolvePreparationAndGraphOwnership){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);
    AString systemSource;
    AString pipelineSource;
    AString frameSource;
    AString graphSource;
    AString recordSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ECS_RENDER / s_RAYTRACE / "raytracing_system.cpp", systemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ECS_RENDER / s_RAYTRACE / "rt_softshadow_pipelines.cpp", pipelineSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ECS_RENDER / s_RAYTRACE / "raytracing_frame_resources.cpp", frameSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ECS_RENDER / s_RENDERER_FRAME_PIPELINE_GRAPH_SHADOW_VIS, graphSource));
    ASSERT_TRUE(ReadRendererSources(
        repoRoot,
        {
            s_RT_SHADOW_TASKS_H,
            s_RT_SHADOW_MATERIAL_CONTEXT_CPP,
            s_RT_SHADOW_VISIBILITY_TARGET_CPP,
            s_RT_SHADOW_OPAQUE_CPP,
            s_RT_SHADOW_TRANSPARENT_CPP,
            s_RT_SHADOW_GPU_VISIBILITY_CPP,
            s_RT_SHADOW_PIPELINES_CPP,
        },
        recordSource
    ));
    const AStringView system(systemSource.data(), systemSource.size());
    const AStringView pipelines(pipelineSource.data(), pipelineSource.size());
    const AStringView frame(frameSource.data(), frameSource.size());
    const AStringView graph(graphSource.data(), graphSource.size());
    const AStringView record(recordSource.data(), recordSource.size());

    const usize prepare = system.find("bool RendererRayTracingSystem::preflightShadowVisibilityResources(");
    const usize hardware = system.find("if(m_shadowVisibilityHardwareSupported){", prepare);
    const usize software = system.find("// No hardware ray tracing:", hardware);
    const usize recording = system.find("bool RendererRayTracingSystem::recordPreflightShadowVisibilityResources(", software);
    ASSERT_NE(prepare, AStringView::npos);
    ASSERT_NE(hardware, AStringView::npos);
    ASSERT_NE(software, AStringView::npos);
    ASSERT_NE(recording, AStringView::npos);
    ASSERT_LT(prepare, hardware);
    ASSERT_LT(hardware, software);
    ASSERT_LT(software, recording);
    const AStringView backendPreflights[] = {
        system.substr(hardware, software - hardware), system.substr(software, recording - software)
    };
    for(const AStringView backend : backendPreflights){
        EXPECT_EQ(CountText(backend, "prepareSoftCombinedResolvePipelines();"), 1u);
        const usize ready = backend.find("m_rayTracingState.m_softTransparentTemporalReady =");
        const usize combined = backend.find("prepareSoftCombinedResolvePipelines();");
        ASSERT_NE(ready, AStringView::npos);
        ASSERT_NE(combined, AStringView::npos);
        EXPECT_LT(ready, combined);
    }
    const usize commonPrepare = pipelines.find("void RendererRayTracingSystem::prepareSoftCombinedResolvePipelines(){");
    const usize dispatch = pipelines.find("void RendererRayTracingSystem::dispatchSoftShadowResolve(", commonPrepare);
    ASSERT_NE(commonPrepare, AStringView::npos);
    ASSERT_NE(dispatch, AStringView::npos);
    ASSERT_LT(commonPrepare, dispatch);
    const AStringView common = pipelines.substr(commonPrepare, dispatch - commonPrepare);
    EXPECT_TRUE(ContainsText(common, "if(!m_rayTracingState.m_softShadowReady || !m_rayTracingState.m_softTransparentReady)"));
    EXPECT_TRUE(ContainsText(common, "if(!ensureSoftCombinedUpsamplePipeline())"));
    EXPECT_TRUE(ContainsText(common, "if(!ensureSoftCombinedWaveletPipeline())"));
    EXPECT_FALSE(ContainsText(common, "queryFeatureSupport"));
    EXPECT_FALSE(ContainsText(common, "m_shadowVisibilityHardwareSupported"));

    const usize upsample = frame.find(".combinedSoftUpsample =");
    const usize wavelet = frame.find(".combinedSoftWaveletReady =", upsample);
    ASSERT_NE(upsample, AStringView::npos);
    ASSERT_NE(wavelet, AStringView::npos);
    ASSERT_LT(upsample, wavelet);
    const AStringView selection = frame.substr(upsample, wavelet - upsample);
    EXPECT_TRUE(ContainsText(selection, ".combinedSoftUpsample = softTransparentFoldReady"));
    EXPECT_TRUE(ContainsText(selection, "state.m_softShadowResolve.m_combinedUpsample.m_pipeline"));
    EXPECT_TRUE(ContainsText(selection, "NWB_SHADOW_RESOLVE_PASS_COUNT == 1u && NWB_SHADOW_RESOLVE_TRANSPARENT_PASS_COUNT == 1u"));
    EXPECT_FALSE(ContainsText(selection, "hardwareTransparentTrace"));
    EXPECT_TRUE(ContainsText(graph, "const bool combinedSoftUpsample = splitSoftTransparentFold && rayTracingPlan.combinedSoftUpsample;"));
    EXPECT_TRUE(ContainsText(graph, "if(combinedSoftWavelet){"));
    EXPECT_TRUE(ContainsText(graph, "opaqueHistoryFrontIsA ? opaqueHistoryB : opaqueHistoryA"));
    EXPECT_TRUE(ContainsText(graph, "transparentFirstWaveletResourceUses.push_back(WriteUse(shadowSoftHalfB, Core::ResourceStates::UnorderedAccess));"));
    EXPECT_TRUE(ContainsText(graph, "transparentFoldResourceUses.push_back(ReadUse(shadowSoftHalfB, Core::ResourceStates::ShaderResource));"));
    EXPECT_TRUE(ContainsText(graph, "? WriteUse(shadowVisibility, Core::ResourceStates::UnorderedAccess)"));
    EXPECT_TRUE(ContainsText(record, "if(payload.combinedUpsample && !*payload.transparentTraceProduced){"));
}


// The renderer-local visibility clear retains typed command-IR capture after the graph-owned CopyDest transition.
// Its task contract permits Compute or Graphics so the scheduler can keep the clear with its consumer.
TEST(EcsGraphics, AllLitClearRejectsMissingDestinationAndActiveRenderPass){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString allLitClearTaskSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ECS_RENDER / s_RAYTRACE / "task_graph_shadow_visibility_tasks.cpp", allLitClearTaskSource));
    const AStringView callback(allLitClearTaskSource.data(), allLitClearTaskSource.size());

    EXPECT_TRUE(ContainsText(callback, "context.declarations.textureForResource(payload.destination)"));
    EXPECT_TRUE(ContainsText(callback, "if(!destination || commandList.isRenderPassActive())"));
    EXPECT_FALSE(ContainsText(callback, "endRenderPass()"));
}


// Split timing scopes nest Async Shadow around Shadow Visibility, with Transparent Resolve innermost when active.
// Marker leases and ending timestamps must close in reverse order so the Vulkan marker stack and measured intervals retain that nesting.
TEST(EcsGraphics, SplitShadowVisibilityClosesNestedTimingMarkersInReverseOrder){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString shadowSource;
    ASSERT_TRUE(ReadRendererSources(
        repoRoot,
        {
            s_RT_SHADOW_TASKS_H,
            s_RT_SHADOW_MATERIAL_CONTEXT_CPP,
            s_RT_SHADOW_VISIBILITY_TARGET_CPP,
            s_RT_SHADOW_OPAQUE_CPP,
            s_RT_SHADOW_TRANSPARENT_CPP,
            s_RT_SHADOW_GPU_VISIBILITY_CPP,
            s_RT_SHADOW_PIPELINES_CPP,
        },
        shadowSource
    ));
    const AStringView shadow(shadowSource.data(), shadowSource.size());

    const usize opaqueTaskOffset = shadow.find("struct ShadowVisibilityOpaqueGraphTask{");
    const usize firstWaveletTaskOffset = shadow.find("struct ShadowVisibilityOpaqueFirstWaveletGraphTask{", opaqueTaskOffset);
    ASSERT_NE(opaqueTaskOffset, AStringView::npos);
    ASSERT_NE(firstWaveletTaskOffset, AStringView::npos);
    ASSERT_LT(opaqueTaskOffset, firstWaveletTaskOffset);
    const AStringView opaqueTask = shadow.substr(opaqueTaskOffset, firstWaveletTaskOffset - opaqueTaskOffset);

    const usize asyncMarkerBeginOffset = opaqueTask.find("payload.asyncTiming->emplace(");
    const usize visibilityMarkerBeginOffset = opaqueTask.find("payload.shadowVisibilityTiming->emplace(", asyncMarkerBeginOffset);
    const usize visibilityMarkerFinishOffset = opaqueTask.find(
        "const bool visibilityMarkerFinished = Core::FinishSplitGpuTimingMarker(payload.shadowVisibilityTiming);",
        visibilityMarkerBeginOffset
    );
    const usize asyncMarkerFinishOffset = opaqueTask.find(
        "asyncMarkerFinished = Core::FinishSplitGpuTimingMarker(payload.asyncTiming);",
        visibilityMarkerFinishOffset
    );
    const usize fallbackOffset = opaqueTask.find("if(!opaqueRecorded){", visibilityMarkerBeginOffset);
    const usize fallbackVisibilityDiscardOffset = opaqueTask.find(
        "payload.shadowVisibilityTiming->value().discardTiming();",
        fallbackOffset
    );
    const usize fallbackAsyncDiscardOffset = opaqueTask.find(
        "payload.asyncTiming->value().discardTiming();",
        fallbackVisibilityDiscardOffset
    );
    const usize discardedObserverOffset = opaqueTask.find("static void discarded(Payload& payload){", asyncMarkerFinishOffset);
    const usize observerVisibilityDiscardOffset = opaqueTask.find(
        "Core::DiscardGpuTimingMeasure(payload.shadowVisibilityTiming);",
        discardedObserverOffset
    );
    const usize observerAsyncDiscardOffset = opaqueTask.find(
        "Core::DiscardGpuTimingMeasure(payload.asyncTiming);",
        observerVisibilityDiscardOffset
    );
    ASSERT_NE(asyncMarkerBeginOffset, AStringView::npos);
    ASSERT_NE(visibilityMarkerBeginOffset, AStringView::npos);
    ASSERT_NE(visibilityMarkerFinishOffset, AStringView::npos);
    ASSERT_NE(asyncMarkerFinishOffset, AStringView::npos);
    ASSERT_NE(fallbackOffset, AStringView::npos);
    ASSERT_NE(fallbackVisibilityDiscardOffset, AStringView::npos);
    ASSERT_NE(fallbackAsyncDiscardOffset, AStringView::npos);
    ASSERT_NE(discardedObserverOffset, AStringView::npos);
    ASSERT_NE(observerVisibilityDiscardOffset, AStringView::npos);
    ASSERT_NE(observerAsyncDiscardOffset, AStringView::npos);
    EXPECT_LT(asyncMarkerBeginOffset, visibilityMarkerBeginOffset);
    EXPECT_LT(visibilityMarkerBeginOffset, visibilityMarkerFinishOffset);
    EXPECT_LT(visibilityMarkerFinishOffset, asyncMarkerFinishOffset);
    EXPECT_LT(fallbackVisibilityDiscardOffset, fallbackAsyncDiscardOffset);
    EXPECT_LT(observerVisibilityDiscardOffset, observerAsyncDiscardOffset);

    const usize foldTaskOffset = shadow.find("struct ShadowTransparentSoftFoldGraphTask{");
    const usize foldTaskEndOffset = shadow.find("struct ShadowVisibilityGraphTask{", foldTaskOffset);
    ASSERT_NE(foldTaskOffset, AStringView::npos);
    ASSERT_NE(foldTaskEndOffset, AStringView::npos);
    ASSERT_LT(foldTaskOffset, foldTaskEndOffset);
    const AStringView foldTask = shadow.substr(foldTaskOffset, foldTaskEndOffset - foldTaskOffset);

    const usize transparentTimingFinishOffset = foldTask.find(
        "payload.transparentResolveTiming->value().finishTiming(commandList);"
    );
    const usize transparentTimingResetOffset = foldTask.find(
        "payload.transparentResolveTiming->reset();",
        transparentTimingFinishOffset
    );
    const usize visibilityTimingFinishOffset = foldTask.find(
        "payload.shadowVisibilityTiming->value().finishTiming(commandList);",
        transparentTimingResetOffset
    );
    const usize visibilityTimingResetOffset = foldTask.find(
        "payload.shadowVisibilityTiming->reset();",
        visibilityTimingFinishOffset
    );
    const usize asyncTimingFinishOffset = foldTask.find(
        "payload.asyncTiming->value().finishTiming(commandList);",
        visibilityTimingResetOffset
    );
    const usize asyncTimingResetOffset = foldTask.find(
        "payload.asyncTiming->reset();",
        asyncTimingFinishOffset
    );
    ASSERT_NE(transparentTimingFinishOffset, AStringView::npos);
    ASSERT_NE(transparentTimingResetOffset, AStringView::npos);
    ASSERT_NE(visibilityTimingFinishOffset, AStringView::npos);
    ASSERT_NE(visibilityTimingResetOffset, AStringView::npos);
    ASSERT_NE(asyncTimingFinishOffset, AStringView::npos);
    ASSERT_NE(asyncTimingResetOffset, AStringView::npos);
    EXPECT_LT(transparentTimingFinishOffset, transparentTimingResetOffset);
    EXPECT_LT(transparentTimingResetOffset, visibilityTimingFinishOffset);
    EXPECT_LT(visibilityTimingFinishOffset, visibilityTimingResetOffset);
    EXPECT_LT(visibilityTimingResetOffset, asyncTimingFinishOffset);
    EXPECT_LT(asyncTimingFinishOffset, asyncTimingResetOffset);
}


// Split shadow callbacks must declare only resources their native body touches.
// Fresh retained scratch stays Unknown until a graph writer publishes it, while an accepted temporal history remains a real sampled input.
TEST(EcsGraphics, SplitShadowVisibilityKeepsFreshScratchAsFirstWrites){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString shadowVisibilitySource;
    AString shadowSource;
    AString softShadowSource;
    AString frameResourcesSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ECS_RENDER / s_RENDERER_FRAME_PIPELINE_GRAPH_SHADOW_VIS, shadowVisibilitySource));
    ASSERT_TRUE(ReadRendererSources(
        repoRoot,
        {
            s_RT_SHADOW_TASKS_H,
            s_RT_SHADOW_MATERIAL_CONTEXT_CPP,
            s_RT_SHADOW_VISIBILITY_TARGET_CPP,
            s_RT_SHADOW_OPAQUE_CPP,
            s_RT_SHADOW_TRANSPARENT_CPP,
            s_RT_SHADOW_GPU_VISIBILITY_CPP,
            s_RT_SHADOW_PIPELINES_CPP,
        },
        shadowSource
    ));
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ECS_RENDER / s_RAYTRACE / "rt_softshadow_dispatch.cpp", softShadowSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ECS_RENDER / s_RAYTRACE / "raytracing_frame_resources.cpp", frameResourcesSource));
    const AStringView shadowVisibility(shadowVisibilitySource.data(), shadowVisibilitySource.size());
    const AStringView shadowSourceView(shadowSource.data(), shadowSource.size());
    const AStringView softShadowSourceView(softShadowSource.data(), softShadowSource.size());
    const AStringView frameResources(frameResourcesSource.data(), frameResourcesSource.size());

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
    EXPECT_TRUE(ContainsText(opaqueDesc, "!hardwareShadowSupported && traceGeometryStatesGraphOwned"));

    EXPECT_TRUE(ContainsText(transparentTraceUses, "WriteUse(transparentSoftHalf, Core::ResourceStates::UnorderedAccess)"));
    EXPECT_FALSE(ContainsText(transparentTraceUses, "ReadWriteUse(shadowVisibility"));
    EXPECT_FALSE(ContainsText(transparentTraceUses, "shadowCoarseTransmittance"));
    EXPECT_FALSE(ContainsText(transparentTraceUses, "ReadWriteUse(shadowSoftHalfA"));
    EXPECT_FALSE(ContainsText(transparentTraceUses, "ReadWriteUse(shadowSoftHalfB"));

    EXPECT_TRUE(ContainsText(shadowVisibility, "const bool softShadowHistoryReadable = rayTracingPlan.softShadowHistoryReadable;"));
    EXPECT_TRUE(ContainsText(frameResources, "state.m_softShadowTemporalReady\n            && state.m_prevWorldToClipValid\n            && state.m_softShadowTemporalSeeded"));
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
    const usize adaptiveOffset = softwareVisibility.find("if(!softTransparentRan)");
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


// The retained monolithic callback owns later native scratch transitions, but its graph entry must still reflect each fresh target's first write.
// Only an accepted temporal history may enter as a sampled input.
TEST(EcsGraphics, MonolithicShadowVisibilityKeepsFreshScratchAsFirstWrites){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString shadowVisibilitySource;
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ECS_RENDER / s_RENDERER_FRAME_PIPELINE_GRAPH_SHADOW_VIS, shadowVisibilitySource));
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
    EXPECT_TRUE(ContainsText(opaqueHistoryHelper, "else if(rayTracingPlan.opaqueTemporalMergeReady)\n                resourceUses.push_back(WriteUse(resource, Core::ResourceStates::UnorderedAccess));"));
    EXPECT_TRUE(ContainsText(transparentHistoryHelper, "if(rayTracingPlan.transparentTemporalMergeReady){"));
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


// Temporal soft-shadow scratch stays private, but its accepted native state must seed the next shadow packet on either the Graphics fallback or dedicated Compute route. The separate return-state cache stays Compute-only.
TEST(EcsGraphics, ShadowTemporalScratchRetainsAcceptedStateAcrossGraphicsRoute){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    AString shadowVisibilitySource;
    AString shadowLifecycleSource;
    ASSERT_TRUE(ReadRendererFramePipelineRuntimeSources(repoRoot, systemSource));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_shadow_visibility.cpp",
        shadowVisibilitySource
    ));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / "execute" / "frame_execute_lifecycle.cpp",
        shadowLifecycleSource
    ));
    const AStringView system(systemSource.data(), systemSource.size());
    const AStringView shadowLifecycle(shadowLifecycleSource.data(), shadowLifecycleSource.size());
    const AStringView shadowVisibility(shadowVisibilitySource.data(), shadowVisibilitySource.size());

    EXPECT_TRUE(ContainsText(
        shadowVisibility,
        "const auto* const shadowScratchStates = m_shadowComputePersistentState.source();"
    ));
    EXPECT_TRUE(ContainsText(
        shadowVisibility,
        "const auto* const shadowReturnStates = m_shadowVisibilityReturnState.source();"
    ));
    EXPECT_TRUE(ContainsText(shadowVisibility, ".states = shadowScratchStates,"));
    EXPECT_TRUE(ContainsText(shadowVisibility, ".states = shadowReturnStates,"));
    EXPECT_TRUE(ContainsText(
        shadowVisibility,
        ".applicableConsumerQueueClass = Core::CommandQueue::Compute,"
    ));
    EXPECT_FALSE(ContainsText(shadowVisibility, "shadowVisibilityRunsOnCompute"));

    const usize acceptedShadowOffset = system.find("const Core::TextureHandle shadowVisibilityReturnTextures[]");
    const usize acceptedShadowEndOffset = system.find(
        "FrameExecuteLifecycle::ShadowVisibilityStateLifecycleContext shadowVisibilityStateLifecycle{",
        acceptedShadowOffset
    );
    const usize scratchStateOffset = shadowLifecycle.find("FrameExecuteLifecycle::PrepareShadowVisibilityTask(");
    const usize acceptedCallbackOffset = shadowLifecycle.find("FrameExecuteLifecycle::AcceptShadowVisibilityTask(", scratchStateOffset);
    const usize returnCommitOffset = shadowLifecycle.find("m_shadowVisibilityReturnState.commit(", acceptedCallbackOffset);
    const usize scratchCommitOffset = shadowLifecycle.find("m_shadowComputePersistentState.commit(", returnCommitOffset);
    const usize temporalFinalizeOffset = shadowLifecycle.find(
        "m_raytracingSystem.finalizeSoftShadowTemporalHistory(*context->targets);",
        scratchCommitOffset
    );
    ASSERT_NE(acceptedShadowOffset, AStringView::npos);
    ASSERT_NE(acceptedShadowEndOffset, AStringView::npos);
    ASSERT_NE(scratchStateOffset, AStringView::npos);
    ASSERT_NE(acceptedCallbackOffset, AStringView::npos);
    ASSERT_NE(returnCommitOffset, AStringView::npos);
    ASSERT_NE(scratchCommitOffset, AStringView::npos);
    ASSERT_NE(temporalFinalizeOffset, AStringView::npos);
    ASSERT_LT(acceptedShadowOffset, acceptedShadowEndOffset);
    ASSERT_LT(scratchStateOffset, acceptedCallbackOffset);
    EXPECT_LT(returnCommitOffset, scratchCommitOffset);
    EXPECT_LT(scratchCommitOffset, temporalFinalizeOffset);
    const AStringView acceptedShadow = system.substr(acceptedShadowOffset, acceptedShadowEndOffset - acceptedShadowOffset);
    const AStringView preparedShadow = shadowLifecycle.substr(scratchStateOffset, acceptedCallbackOffset - scratchStateOffset);
    EXPECT_TRUE(ContainsText(acceptedShadow, "deferredTargets.shadowSoftGeometry,"));
    EXPECT_TRUE(ContainsText(acceptedShadow, "deferredTargets.shadowSoftGeometryPrev,"));
    EXPECT_TRUE(ContainsText(acceptedShadow, "rayTracingGraphResources.hardwareTransparentCrossingsBuffer,"));
    EXPECT_TRUE(ContainsText(acceptedShadow, "lightSpaceShadowResources.events,"));
    EXPECT_TRUE(ContainsText(acceptedShadow, "rayTracingGraphResources.hardwareTransparentOverflowListBuffer,"));
    EXPECT_TRUE(ContainsText(acceptedShadow, "rayTracingGraphResources.hardwareTransparentOverflowArgsBuffer,"));
    EXPECT_TRUE(ContainsText(preparedShadow, "m_shadowComputePersistentState.buildMergedResourceSubset("));
    EXPECT_TRUE(ContainsText(preparedShadow, "if(context->runsOnCompute){"));
    EXPECT_TRUE(ContainsText(preparedShadow, "m_shadowVisibilityReturnState.buildFilteredResourceSubset("));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

