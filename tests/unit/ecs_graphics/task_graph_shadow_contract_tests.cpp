// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_contract_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_task_graph_shadow_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace EcsGraphicsTaskGraphContractTestDetail;
using EcsGraphicsTaskGraphContractTestDetail::AString;


// The retained monolithic soft-shadow route must clear all-lit visibility on the selected Compute packet. Its
// renderer-local callback retains typed command-IR capture while avoiding the generic clear helper's Graphics path.
TEST(EcsGraphics, ShadowVisibilityAllLitClearUsesComputeGraphCallback){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString allLitClearTaskSource;
    AString shadowVisibilityTaskGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "task_graph_shadow_visibility_tasks.cpp", allLitClearTaskSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_shadow_visibility.cpp", shadowVisibilityTaskGraphSource));
    const AStringView callback(allLitClearTaskSource.data(), allLitClearTaskSource.size());
    const AStringView shadowVisibility(shadowVisibilityTaskGraphSource.data(), shadowVisibilityTaskGraphSource.size());

    EXPECT_TRUE(ContainsText(callback, "context.declarations.textureForResource(payload.destination)"));
    EXPECT_TRUE(ContainsText(callback, "if(!destination || commandList.isRenderPassActive())"));
    EXPECT_FALSE(ContainsText(callback, "endRenderPass()"));
    EXPECT_TRUE(ContainsText(callback, "Core::GpuClearTextureTaskDesc clearDesc{"));
    EXPECT_TRUE(ContainsText(callback, ".destination = payload.destination,"));
    EXPECT_TRUE(ContainsText(callback, ".subresources = s_ShadowVisibilitySubresources,"));
    EXPECT_TRUE(ContainsText(callback, ".valueType = Core::GpuClearTextureTaskValueType::Float,"));
    EXPECT_TRUE(ContainsText(callback, ".floatValue = Core::Color(1.f, 1.f, 1.f, 1.f),"));
    EXPECT_TRUE(ContainsText(callback, "context.commandIrCapture"));
    EXPECT_TRUE(ContainsText(callback, "captureClearTexture("));
    EXPECT_TRUE(ContainsText(callback, "commandList.clearTextureFloat(*destination, clearDesc.subresources, clearDesc.floatValue);"));

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


// Shadow Visibility has both a fully split soft-transparent route and a retained monolithic compatibility route.
// Each graph-owned chain may choose an alternate Compute family, while its direct successors retain that physical
// queue and the explicit primary-Graphics presentation guard remains outside this effect.
TEST(EcsGraphics, ShadowVisibilityPermitsOptInCrossFamilyComputeRouting){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString shadowVisibilitySource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_shadow_visibility.cpp", shadowVisibilitySource));
    const AStringView shadowVisibility(shadowVisibilitySource.data(), shadowVisibilitySource.size());

    EXPECT_TRUE(ContainsText(shadowVisibility, "EnableCrossFamilyComputeEffectRouting(opaqueScheduling)"));
    EXPECT_TRUE(ContainsText(shadowVisibility, "EnableCrossFamilyComputeEffectRouting(tailScheduling)"));
    EXPECT_TRUE(ContainsText(shadowVisibility, "EnableCrossFamilyComputeEffectRouting(primitiveScheduling)"));
    EXPECT_TRUE(ContainsText(shadowVisibility, "EnableCrossFamilyComputeEffectRouting(allLitClearScheduling)"));
    EXPECT_TRUE(ContainsText(shadowVisibility, "EnableCrossFamilyComputeEffectRouting(scheduling)"));
    EXPECT_TRUE(ContainsText(shadowVisibility, "EnableCrossFamilyComputeEffectRouting(statsReadbackScheduling)"));
}


// Split timing scopes nest Async Shadow around Shadow Visibility, with Transparent Resolve innermost when active.
// Marker leases and ending timestamps must close in reverse order so the Vulkan marker stack and measured intervals
// retain that nesting.
TEST(EcsGraphics, SplitShadowVisibilityClosesNestedTimingMarkersInReverseOrder){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString shadowSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_shadow.cpp", shadowSource));
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


// Split shadow callbacks must declare only resources their native body touches. Fresh retained scratch stays
// Unknown until a graph writer publishes it, while an accepted temporal history remains a real sampled input.
TEST(EcsGraphics, SplitShadowVisibilityKeepsFreshScratchAsFirstWrites){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString shadowVisibilitySource;
    AString shadowSource;
    AString softShadowSource;
    AString rayTracingSystemSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_shadow_visibility.cpp", shadowVisibilitySource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_shadow.cpp", shadowSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_softshadow.cpp", softShadowSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.cpp", rayTracingSystemSource));
    const AStringView shadowVisibility(shadowVisibilitySource.data(), shadowVisibilitySource.size());
    const AStringView shadowSourceView(shadowSource.data(), shadowSource.size());
    const AStringView softShadowSourceView(softShadowSource.data(), softShadowSource.size());
    const AStringView rayTracingSystem(rayTracingSystemSource.data(), rayTracingSystemSource.size());

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

    EXPECT_TRUE(ContainsText(shadowVisibility, "const bool softShadowHistoryReadable = rayTracingPlan.softShadowHistoryReadable;"));
    EXPECT_TRUE(ContainsText(rayTracingSystem, "state.m_softShadowTemporalReady\n            && state.m_prevWorldToClipValid\n            && state.m_softShadowTemporalSeeded"));
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
    const usize adaptiveOffset = softwareVisibility.find("if(!softTransparentRan && m_rayTracingState.m_swShadowAdaptiveEnabled)");
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


// Both software-shadow recording routes publish the same production-owned one-shot diagnostic. The split
// transparent trace reports only after recording its dispatch, while the retained monolithic path shares it.
TEST(EcsGraphics, SoftwareShadowTraversalDiagnosticCoversSplitAndMonolithicRoutes){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString shadowSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_shadow.cpp", shadowSource));
    const AStringView shadow(shadowSource.data(), shadowSource.size());

    const usize splitTraceOffset = shadow.find("bool RendererRayTracingSystem::renderSoftTransparentShadowTrace(");
    const usize reportOffset = shadow.find("void RendererRayTracingSystem::reportSoftwareShadowTraversal(", splitTraceOffset);
    const usize temporalMergeOffset = shadow.find("bool RendererRayTracingSystem::renderSoftTransparentShadowTemporalMerge(", reportOffset);
    const usize monolithicOffset = shadow.find("bool RendererRayTracingSystem::renderGpuBvhShadowVisibility(", temporalMergeOffset);
    const usize opaqueOffset = shadow.find("bool RendererRayTracingSystem::renderGpuBvhShadowVisibilityOpaque(", monolithicOffset);
    ASSERT_NE(splitTraceOffset, AStringView::npos);
    ASSERT_NE(reportOffset, AStringView::npos);
    ASSERT_NE(temporalMergeOffset, AStringView::npos);
    ASSERT_NE(monolithicOffset, AStringView::npos);
    ASSERT_NE(opaqueOffset, AStringView::npos);
    ASSERT_LT(splitTraceOffset, reportOffset);
    ASSERT_LT(reportOffset, temporalMergeOffset);
    ASSERT_LT(temporalMergeOffset, monolithicOffset);
    ASSERT_LT(monolithicOffset, opaqueOffset);

    const AStringView splitTrace = shadow.substr(splitTraceOffset, reportOffset - splitTraceOffset);
    const AStringView report = shadow.substr(reportOffset, temporalMergeOffset - reportOffset);
    const AStringView monolithic = shadow.substr(monolithicOffset, opaqueOffset - monolithicOffset);
    EXPECT_TRUE(ContainsText(splitTrace, "dispatchSoftShadowDenoiseAndTransparentFold("));
    EXPECT_TRUE(ContainsText(splitTrace, "reportSoftwareShadowTraversal(targets);\n    return true;"));
    EXPECT_TRUE(ContainsText(report, "if(m_rayTracingState.m_swShadowDispatchLogged)"));
    EXPECT_TRUE(ContainsText(report, "m_rayTracingState.m_swShadowDispatchLogged = true;"));
    EXPECT_TRUE(ContainsText(report, "RendererSystem: dispatched software shadow traversal"));
    EXPECT_EQ(CountText(monolithic, "reportSoftwareShadowTraversal(targets);"), 2u);
    EXPECT_FALSE(ContainsText(monolithic, "const auto logSoftwareShadowTraversal"));
}


// The retained monolithic callback owns later native scratch transitions, but its graph entry must still reflect
// each fresh target's first write. Only an accepted temporal history may enter as a sampled input.
TEST(EcsGraphics, MonolithicShadowVisibilityKeepsFreshScratchAsFirstWrites){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString shadowVisibilitySource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_shadow_visibility.cpp", shadowVisibilitySource));
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


// Temporal soft-shadow scratch stays private, but its accepted native state must seed the next shadow packet on
// either the Graphics fallback or dedicated Compute route. The separate return-state cache stays Compute-only.
TEST(EcsGraphics, ShadowTemporalScratchRetainsAcceptedStateAcrossGraphicsRoute){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    AString shadowVisibilitySource;
    ASSERT_TRUE(ReadRendererFramePipelineRuntimeSources(repoRoot, systemSource));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_shadow_visibility.cpp",
        shadowVisibilitySource
    ));
    const AStringView system(systemSource.data(), systemSource.size());
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
    EXPECT_TRUE(ContainsText(acceptedShadow, "if(context->runsOnCompute){"));
    EXPECT_TRUE(ContainsText(acceptedShadow, "m_shadowVisibilityReturnState.buildFilteredResourceSubset("));
    EXPECT_TRUE(ContainsText(acceptedShadow, "context->renderer->m_shadowVisibilityReturnState.commit("));
    EXPECT_TRUE(ContainsText(acceptedShadow, "context->renderer->m_shadowComputePersistentState.commit("));
    EXPECT_TRUE(ContainsText(system, "finalizeSoftShadowTemporalHistory(*context->targets)"));
    EXPECT_TRUE(ContainsText(
        system,
        ".task = m_deferredShadowVisibilityTask,\n"
        "        .context = &shadowVisibilityStateLifecycle,\n"
        "        .invoke = prepareShadowVisibilityTask,"
    ));
    EXPECT_TRUE(ContainsText(
        system,
        ".task = m_deferredShadowVisibilityTask,\n"
        "        .context = &shadowVisibilityStateLifecycle,\n"
        "        .invoke = acceptShadowVisibilityTask,"
    ));
}


// The split software-BVH task records only compute commands after graph-owned clears. The compatibility Shadow
// Preparation endpoint can still record both transfer clears and compute dispatches, while preferring Graphics.
TEST(EcsGraphics, ShadowPreparationQueueCapabilitiesMatchNativeCommands){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString taskGraphSource;
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_shadow_prepare.cpp",
        taskGraphSource
    ));

    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());
    const usize softwareBuildOffset = taskGraph.find(".setMarkerLabel(\"Shadow Prepare SW-BVH Build\")");
    const usize softwareBuildEndOffset = taskGraph.find("m_deferredLightingTaskGraph.addTask<", softwareBuildOffset);
    ASSERT_NE(softwareBuildOffset, AStringView::npos);
    ASSERT_NE(softwareBuildEndOffset, AStringView::npos);
    const AStringView softwareBuild = taskGraph.substr(
        softwareBuildOffset,
        softwareBuildEndOffset - softwareBuildOffset
    );
    EXPECT_TRUE(ContainsText(softwareBuild, ".setQueue(GraphicsPreferredComputeQueueRequest())"));
    EXPECT_FALSE(ContainsText(softwareBuild, ".setQueue(GraphicsComputeQueueRequest())"));

    const usize shadowPrepareOffset = taskGraph.find(".setMarkerLabel(\"Shadow Preparation\")", softwareBuildEndOffset);
    const usize shadowPrepareEndOffset = taskGraph.find(
        "m_deferredLightingTaskGraph.addTask<ECSRenderDetail::ShadowPrepareGraphTask>",
        shadowPrepareOffset
    );
    ASSERT_NE(shadowPrepareOffset, AStringView::npos);
    ASSERT_NE(shadowPrepareEndOffset, AStringView::npos);
    const AStringView shadowPrepare = taskGraph.substr(
        shadowPrepareOffset,
        shadowPrepareEndOffset - shadowPrepareOffset
    );
    EXPECT_TRUE(ContainsText(shadowPrepare, ".setQueue(GraphicsComputeUploadQueueRequest())"));
    EXPECT_FALSE(ContainsText(shadowPrepare, ".setQueue(GraphicsPreferredComputeQueueRequest())"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

