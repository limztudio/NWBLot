// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_contract_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_task_graph_hybrid_fallback_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace EcsGraphicsTaskGraphContractTestDetail;
using EcsGraphicsTaskGraphContractTestDetail::AString;


// A healthy hybrid tail requires both a fresh software triple and a complete frozen hardware restore triple. A tail
// miss restores only declared blobs; invalid snapshots reject the merged packet for a fresh preflight.
TEST(EcsGraphics, HybridHardwareFallbackRequiresCompleteGraphOwnedBlobs){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString taskGraphSource;
    AString rayTracingHeaderSource;
    AString rayTracingSource;
    AString swBvhSource;
    AString kernelSystemHeaderSource;
    AString kernelSystemSource;
    AString smokeCmakeSource;
    AString stressTestProjectSource;
    AString transparentMultiProjectSource;
    ASSERT_TRUE(ReadRendererSources(
        repoRoot,
        {
            "raytrace/task_graph_shadow_prepare_tasks.h",
            "raytrace/task_graph_shadow_prepare_tasks.cpp",
            "renderer_frame_pipeline_graph_shadow_prepare.cpp",
        },
        taskGraphSource
    ));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.h", rayTracingHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.cpp", rayTracingSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_swbvh.cpp", swBvhSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline.h", kernelSystemHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline.cpp", kernelSystemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / "CMakeLists.txt", smokeCmakeSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / "stress_test_project.cpp", stressTestProjectSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "tests" / "smoke" / "transparent_multi_project.cpp", transparentMultiProjectSource));

    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());
    const AStringView rayTracingHeader(rayTracingHeaderSource.data(), rayTracingHeaderSource.size());
    const AStringView rayTracing(rayTracingSource.data(), rayTracingSource.size());
    const AStringView swBvh(swBvhSource.data(), swBvhSource.size());
    const AStringView kernelSystemHeader(kernelSystemHeaderSource.data(), kernelSystemHeaderSource.size());
    const AStringView kernelSystem(kernelSystemSource.data(), kernelSystemSource.size());
    const AStringView smokeCmake(smokeCmakeSource.data(), smokeCmakeSource.size());
    const AStringView stressTestProject(stressTestProjectSource.data(), stressTestProjectSource.size());
    const AStringView transparentMultiProject(transparentMultiProjectSource.data(), transparentMultiProjectSource.size());

    EXPECT_TRUE(ContainsText(rayTracingHeader, "retainPreparedHybridHardwareMaterialContextFallbackUploads"));
    EXPECT_FALSE(ContainsText(rayTracingHeader, "hybridHardwareFallbackUploadsGraphOwned"));
    EXPECT_EQ(CountText(rayTracingHeader, "recordPreparedHybridHardwareMaterialContextFallback("), 1u);
    EXPECT_FALSE(ContainsText(rayTracingHeader, "recordPreparedHybridHardwareMaterialContextFallback(Core::CommandList& commandList);"));
    const usize retainFallbackOffset = rayTracing.find(
        "bool RendererRayTracingSystem::retainPreparedHybridHardwareMaterialContextFallbackUploads("
    );
    const usize retainFallbackEndOffset = rayTracing.find(
        "void RendererRayTracingSystem::confirmPreparedShadowMaterialContextUploads()noexcept",
        retainFallbackOffset
    );
    ASSERT_NE(retainFallbackOffset, AStringView::npos);
    ASSERT_NE(retainFallbackEndOffset, AStringView::npos);
    const AStringView retainFallback = rayTracing.substr(
        retainFallbackOffset,
        retainFallbackEndOffset - retainFallbackOffset
    );
    EXPECT_TRUE(ContainsText(retainFallback, "outInstanceMaterialBlob = {};"));
    EXPECT_TRUE(ContainsText(retainFallback, "outInstanceBlob = {};"));
    EXPECT_TRUE(ContainsText(retainFallback, "outMaterialTypedBlob = {};"));
    EXPECT_EQ(CountText(retainFallback, "graph.copyUploadData("), 3u);
    EXPECT_EQ(CountText(retainFallback, "outInstanceMaterialBlob = graph.copyUploadData("), 1u);
    EXPECT_EQ(CountText(retainFallback, "outInstanceBlob = graph.copyUploadData("), 1u);
    EXPECT_EQ(CountText(retainFallback, "outMaterialTypedBlob = graph.copyUploadData("), 1u);
    EXPECT_EQ(CountText(
        retainFallback,
        "outInstanceMaterialBlob = graph.copyUploadData(\n"
        "        bytes,\n"
        "        instanceMaterialByteCount,\n"
        "        alignof(NwbRtInstanceMaterialGpu)\n"
        "    );"
    ), 1u);
    EXPECT_EQ(CountText(
        retainFallback,
        "outInstanceBlob = graph.copyUploadData(\n"
        "        bytes + instanceMaterialByteCount,\n"
        "        instanceByteCount,\n"
        "        alignof(InstanceGpuData)\n"
        "    );"
    ), 1u);
    EXPECT_EQ(CountText(
        retainFallback,
        "outMaterialTypedBlob = graph.copyUploadData(\n"
        "        bytes + instanceMaterialByteCount + instanceByteCount,\n"
        "        materialTypedByteCount,\n"
        "        alignof(u32)\n"
        "    );"
    ), 1u);
    EXPECT_TRUE(ContainsText(
        retainFallback,
        "return outInstanceMaterialBlob.valid() && outInstanceBlob.valid() && outMaterialTypedBlob.valid();"
    ));

    const usize hybridTailRecordOffset = taskGraph.find("bool ShadowPrepareHybridSoftwareTailGraphTask::record(");
    const usize hybridTailRecordEndOffset = taskGraph.find(
        "void ShadowPrepareHybridSoftwareTailGraphTask::discarded(",
        hybridTailRecordOffset
    );
    ASSERT_NE(hybridTailRecordOffset, AStringView::npos);
    ASSERT_NE(hybridTailRecordEndOffset, AStringView::npos);
    const AStringView hybridTailRecord = taskGraph.substr(
        hybridTailRecordOffset,
        hybridTailRecordEndOffset - hybridTailRecordOffset
    );
    const usize hybridTailRecordValidationEndOffset = hybridTailRecord.find(
        "// The tail shares the accepting packet's timing ticket for SW-BVH scopes."
    );
    ASSERT_NE(hybridTailRecordValidationEndOffset, AStringView::npos);
    const AStringView hybridTailRecordValidation = hybridTailRecord.substr(0u, hybridTailRecordValidationEndOffset);
    EXPECT_EQ(CountText(hybridTailRecordValidation, "context.declarations.uploadBlobData("), 3u);
    EXPECT_EQ(CountText(
        hybridTailRecordValidation,
        "hybridHardwareFallbackInstanceMaterialData = context.declarations.uploadBlobData(\n"
        "        payload.hybridHardwareFallbackInstanceMaterialBlob,\n"
        "        hybridHardwareFallbackInstanceMaterialByteCount\n"
        "    );"
    ), 1u);
    EXPECT_EQ(CountText(
        hybridTailRecordValidation,
        "hybridHardwareFallbackInstanceData = context.declarations.uploadBlobData(\n"
        "        payload.hybridHardwareFallbackInstanceBlob,\n"
        "        hybridHardwareFallbackInstanceByteCount\n"
        "    );"
    ), 1u);
    EXPECT_EQ(CountText(
        hybridTailRecordValidation,
        "hybridHardwareFallbackMaterialTypedData = context.declarations.uploadBlobData(\n"
        "        payload.hybridHardwareFallbackMaterialTypedBlob,\n"
        "        hybridHardwareFallbackMaterialTypedByteCount\n"
        "    );"
    ), 1u);
    EXPECT_EQ(CountText(
        hybridTailRecordValidation,
        "    if(\n"
        "        !hybridHardwareFallbackInstanceMaterialData\n"
        "        || !hybridHardwareFallbackInstanceData\n"
        "        || !hybridHardwareFallbackMaterialTypedData\n"
        "        || hybridHardwareFallbackInstanceMaterialByteCount == 0u\n"
        "        || hybridHardwareFallbackInstanceByteCount == 0u\n"
        "        || hybridHardwareFallbackMaterialTypedByteCount == 0u\n"
        "    )\n"
        "        return false;"
    ), 1u);
    EXPECT_EQ(CountText(
        hybridTailRecord,
        "        hybridHardwareFallbackInstanceMaterialData,\n"
        "        hybridHardwareFallbackInstanceMaterialByteCount,\n"
        "        hybridHardwareFallbackInstanceData,\n"
        "        hybridHardwareFallbackInstanceByteCount,\n"
        "        hybridHardwareFallbackMaterialTypedData,\n"
        "        hybridHardwareFallbackMaterialTypedByteCount\n"
        "    );"
    ), 1u);
    EXPECT_FALSE(ContainsText(taskGraph, "hybridHardwareFallbackUploadsGraphOwned"));
    const usize hybridFallbackRetentionOffset = taskGraph.find(
        "    if(hybridSoftwareTailGraphOwned){",
        taskGraph.find("Core::GpuUploadBlobId hybridHardwareFallbackInstanceMaterialBlob;")
    );
    const usize hybridFallbackRetentionEndOffset = taskGraph.find(
        "// A fully frozen hybrid packet has a separate software-tail callback",
        hybridFallbackRetentionOffset
    );
    ASSERT_NE(hybridFallbackRetentionOffset, AStringView::npos);
    ASSERT_NE(hybridFallbackRetentionEndOffset, AStringView::npos);
    const AStringView hybridFallbackRetention = taskGraph.substr(
        hybridFallbackRetentionOffset,
        hybridFallbackRetentionEndOffset - hybridFallbackRetentionOffset
    );
    EXPECT_TRUE(ContainsText(hybridFallbackRetention, "retainPreparedHybridHardwareMaterialContextFallbackUploads("));
    EXPECT_TRUE(ContainsText(hybridFallbackRetention, "hybridHardwareFallbackInstanceMaterialBlob.valid()"));
    EXPECT_TRUE(ContainsText(hybridFallbackRetention, "hybridHardwareFallbackInstanceBlob.valid()"));
    EXPECT_TRUE(ContainsText(hybridFallbackRetention, "hybridHardwareFallbackMaterialTypedBlob.valid()"));
    EXPECT_TRUE(ContainsText(hybridFallbackRetention, "!shadowInstanceMaterials.valid()"));
    EXPECT_TRUE(ContainsText(hybridFallbackRetention, "!shadowInstances.valid()"));
    EXPECT_TRUE(ContainsText(hybridFallbackRetention, "!shadowMaterialTyped.valid()"));
    EXPECT_TRUE(ContainsText(
        hybridFallbackRetention,
        "healthy hybrid tail requires a complete graph-owned hardware material fallback"
    ));
    EXPECT_TRUE(ContainsText(hybridFallbackRetention, "return false;"));

    const usize hybridTailUsesOffset = taskGraph.find(
        "    if(hybridSoftwareTailGraphOwned){",
        taskGraph.find("hybridSoftwareTailResourceUses.reserve(")
    );
    const usize hybridTailUsesEndOffset = taskGraph.find("    bool resourcesImported = true;", hybridTailUsesOffset);
    ASSERT_NE(hybridTailUsesOffset, AStringView::npos);
    ASSERT_NE(hybridTailUsesEndOffset, AStringView::npos);
    const AStringView hybridTailUses = taskGraph.substr(hybridTailUsesOffset, hybridTailUsesEndOffset - hybridTailUsesOffset);
    EXPECT_TRUE(ContainsText(hybridTailUses, "if(hybridSoftwareTailGraphOwned)"));
    EXPECT_EQ(CountText(hybridTailUses, "hybridSoftwareTailResourceUses.push_back("), 3u);
    EXPECT_EQ(CountText(hybridTailUses, "WriteUse(shadowInstanceMaterials, Core::ResourceStates::ShaderResource)"), 1u);
    EXPECT_EQ(CountText(hybridTailUses, "WriteUse(shadowInstances, Core::ResourceStates::ShaderResource)"), 1u);
    EXPECT_EQ(CountText(hybridTailUses, "WriteUse(shadowMaterialTyped, Core::ResourceStates::ShaderResource)"), 1u);
    EXPECT_TRUE(ContainsText(swBvh, "const void* const instanceMaterialData"));
    EXPECT_TRUE(ContainsText(swBvh, "graph-owned hybrid hardware fallback bytes differ from preflight"));
    EXPECT_TRUE(ContainsText(swBvh, "tryWriteBuffer(*instanceMaterialBuffer, instanceMaterialData"));
    EXPECT_FALSE(ContainsText(swBvh, "bool RendererRayTracingSystem::recordPreparedHybridHardwareMaterialContextFallback(Core::CommandList& commandList){"));
    const usize restoreOffset = swBvh.find("bool RendererRayTracingSystem::recordPreparedHybridHardwareMaterialContextFallback(");
    const usize restoreEndOffset = swBvh.find("bool RendererRayTracingSystem::buildSceneSwBvhImpl(", restoreOffset);
    ASSERT_NE(restoreOffset, AStringView::npos);
    ASSERT_NE(restoreEndOffset, AStringView::npos);
    const AStringView restore = swBvh.substr(restoreOffset, restoreEndOffset - restoreOffset);
    EXPECT_EQ(CountText(restore, "NWB_MEMCMP("), 3u);
    EXPECT_EQ(CountText(restore, "commandList.tryWriteBuffer("), 3u);
    const usize restoreMismatchOffset = restore.find("graph-owned hybrid hardware fallback bytes differ from preflight");
    const usize firstRestoreTransitionOffset = restore.find("commandList.setBufferState(");
    ASSERT_NE(restoreMismatchOffset, AStringView::npos);
    ASSERT_NE(firstRestoreTransitionOffset, AStringView::npos);
    EXPECT_LT(restoreMismatchOffset, firstRestoreTransitionOffset);
    const AStringView restoreValidation = restore.substr(0u, firstRestoreTransitionOffset);
    EXPECT_EQ(CountText(restoreValidation, "NWB_MEMCMP("), 3u);
    EXPECT_EQ(CountText(restoreValidation, "commandList.tryWriteBuffer("), 0u);
    EXPECT_EQ(CountText(restoreValidation, "commandList.setBufferState("), 0u);
    EXPECT_TRUE(ContainsText(restoreValidation, "graph-owned hybrid hardware fallback bytes differ from preflight"));
    EXPECT_EQ(CountText(
        restoreValidation,
        "m_preparedHybridHardwareFallbackBytes.data(),\n"
        "            instanceMaterialData,\n"
        "            instanceMaterialByteCount"
    ), 1u);
    EXPECT_EQ(CountText(
        restoreValidation,
        "m_preparedHybridHardwareFallbackBytes.data() + instanceMaterialByteCount,\n"
        "            instanceData,\n"
        "            instanceByteCount"
    ), 1u);
    EXPECT_EQ(CountText(
        restoreValidation,
        "m_preparedHybridHardwareFallbackBytes.data() + instanceMaterialByteCount + instanceByteCount,\n"
        "            materialTypedData,\n"
        "            materialTypedByteCount"
    ), 1u);
    const usize restoreValidationEndOffset = restoreValidation.find("Core::Buffer* const instanceMaterialBuffer");
    ASSERT_NE(restoreValidationEndOffset, AStringView::npos);
    EXPECT_TRUE(ContainsText(
        restoreValidation.substr(restoreMismatchOffset, restoreValidationEndOffset - restoreMismatchOffset),
        "return false;"
    ));
    EXPECT_EQ(CountText(restore, "commandList.setBufferState("), 6u);
    const usize firstRestoreCommitOffset = restore.find("commandList.commitBarriers();", firstRestoreTransitionOffset);
    ASSERT_NE(firstRestoreCommitOffset, AStringView::npos);
    const AStringView restoreCopyDestStates = restore.substr(
        firstRestoreTransitionOffset,
        firstRestoreCommitOffset - firstRestoreTransitionOffset
    );
    EXPECT_EQ(CountText(restoreCopyDestStates, "Core::ResourceStates::CopyDest"), 3u);
    EXPECT_EQ(CountText(restoreCopyDestStates, "Core::ResourceStates::ShaderResource"), 0u);
    EXPECT_EQ(CountText(
        restoreCopyDestStates,
        "commandList.setBufferState(instanceMaterialBuffer, Core::ResourceStates::CopyDest);"
    ), 1u);
    EXPECT_EQ(CountText(
        restoreCopyDestStates,
        "commandList.setBufferState(instanceBuffer, Core::ResourceStates::CopyDest);"
    ), 1u);
    EXPECT_EQ(CountText(
        restoreCopyDestStates,
        "commandList.setBufferState(materialTypedBuffer, Core::ResourceStates::CopyDest);"
    ), 1u);
    const usize firstRestoreWriteOffset = restore.find("commandList.tryWriteBuffer(", firstRestoreCommitOffset);
    const usize finalRestoreStateOffset = restore.find(
        "commandList.setBufferState(instanceMaterialBuffer, Core::ResourceStates::ShaderResource);",
        firstRestoreWriteOffset
    );
    ASSERT_NE(firstRestoreWriteOffset, AStringView::npos);
    ASSERT_NE(finalRestoreStateOffset, AStringView::npos);
    const AStringView restoreWrites = restore.substr(firstRestoreWriteOffset, finalRestoreStateOffset - firstRestoreWriteOffset);
    EXPECT_EQ(CountText(restoreWrites, "commandList.tryWriteBuffer("), 3u);
    EXPECT_EQ(CountText(
        restoreWrites,
        "commandList.tryWriteBuffer(*instanceMaterialBuffer, instanceMaterialData, instanceMaterialByteCount)"
    ), 1u);
    EXPECT_EQ(CountText(
        restoreWrites,
        "commandList.tryWriteBuffer(*instanceBuffer, instanceData, instanceByteCount)"
    ), 1u);
    EXPECT_EQ(CountText(
        restoreWrites,
        "commandList.tryWriteBuffer(*materialTypedBuffer, materialTypedData, materialTypedByteCount)"
    ), 1u);
    const usize finalRestoreCommitOffset = restore.find("commandList.commitBarriers();", finalRestoreStateOffset);
    ASSERT_NE(finalRestoreCommitOffset, AStringView::npos);
    const AStringView finalRestoreStates = restore.substr(
        finalRestoreStateOffset,
        finalRestoreCommitOffset - finalRestoreStateOffset
    );
    EXPECT_EQ(CountText(finalRestoreStates, "Core::ResourceStates::ShaderResource"), 3u);
    EXPECT_EQ(CountText(
        finalRestoreStates,
        "commandList.setBufferState(instanceMaterialBuffer, Core::ResourceStates::ShaderResource);"
    ), 1u);
    EXPECT_EQ(CountText(
        finalRestoreStates,
        "commandList.setBufferState(instanceBuffer, Core::ResourceStates::ShaderResource);"
    ), 1u);
    EXPECT_EQ(CountText(
        finalRestoreStates,
        "commandList.setBufferState(materialTypedBuffer, Core::ResourceStates::ShaderResource);"
    ), 1u);
    const usize restorePublicationOffset = restore.find("m_preparedHybridHardwareFallbackRecorded = true;");
    ASSERT_NE(restorePublicationOffset, AStringView::npos);
    EXPECT_LT(finalRestoreStateOffset, finalRestoreCommitOffset);
    EXPECT_LT(finalRestoreCommitOffset, restorePublicationOffset);
    EXPECT_TRUE(ContainsText(
        swBvh,
        "const bool canReuseSwMaterialContext =\n"
        "        !hybridSoftwareMaterialContextCaptureRequired\n"
        "        && staticScene"
    ));
    const usize hybridFallbackCaptureOffset = swBvh.find("|| !capturePreparedHybridHardwareMaterialContextFallback()");
    const usize softwareMaterialCaptureOffset = swBvh.find("if(!capturePreparedShadowMaterialContext(", hybridFallbackCaptureOffset);
    ASSERT_NE(hybridFallbackCaptureOffset, AStringView::npos);
    ASSERT_NE(softwareMaterialCaptureOffset, AStringView::npos);
    EXPECT_TRUE(ContainsText(
        swBvh.substr(hybridFallbackCaptureOffset, softwareMaterialCaptureOffset - hybridFallbackCaptureOffset),
        "return false;"
    ));

    const usize swPipelineOffset = rayTracing.find("const bool swPipelineReady = meshResourcesReady && ensureSwShadowPipeline();");
    const usize swGatherOffset = rayTracing.find("&& prepareSceneSwBvhResources(scratchArena)");
    ASSERT_NE(swPipelineOffset, AStringView::npos);
    ASSERT_NE(swGatherOffset, AStringView::npos);
    EXPECT_LT(swPipelineOffset, swGatherOffset);
    EXPECT_FALSE(ContainsText(rayTracing, "m_shadowVisibilityHybridResourcesPreflighted && !m_shadowVisibilityHybridPipelinePreflighted"));
    const usize hybridPreflightOffset = rayTracing.find("bool RendererRayTracingSystem::recordPreflightHybridSoftwareTail(");
    const usize hybridPreflightEndOffset = rayTracing.find("// A graph-owned hybrid plan may exist", hybridPreflightOffset);
    ASSERT_NE(hybridPreflightOffset, AStringView::npos);
    ASSERT_NE(hybridPreflightEndOffset, AStringView::npos);
    const AStringView hybridPreflight = rayTracing.substr(
        hybridPreflightOffset,
        hybridPreflightEndOffset - hybridPreflightOffset
    );
    const usize restoreFailureOffset = hybridPreflight.find("if(!recordPreparedHybridHardwareMaterialContextFallback(");
    ASSERT_NE(restoreFailureOffset, AStringView::npos);
    const AStringView restoreFailureGuard = hybridPreflight.substr(restoreFailureOffset);
    EXPECT_TRUE(ContainsText(
        restoreFailureGuard,
        "frozen hybrid hardware material-context restore failed; rejecting shadow preparation packet"
    ));
    EXPECT_EQ(CountText(restoreFailureGuard, "return false;"), 1u);
    EXPECT_FALSE(ContainsText(rayTracing, "buildSceneTlas(commandList, scratchArena, false)"));
    EXPECT_FALSE(ContainsText(rayTracingHeader, "forceHybridHardwareFallbackSnapshotStaleForTesting"));
    EXPECT_FALSE(ContainsText(rayTracingHeader, "forceHybridSceneTraversalFallbackForTesting"));
    EXPECT_FALSE(ContainsText(rayTracingHeader, "forceHybridSceneTraversalFallbackEveryFrameForTesting"));
    EXPECT_FALSE(ContainsText(rayTracingHeader, "m_forceHybridSceneTraversalFallbackForTesting"));
    EXPECT_FALSE(ContainsText(rayTracingHeader, "m_forceHybridSceneTraversalFallbackEveryFrameForTesting"));
    EXPECT_FALSE(ContainsText(rayTracingHeader, "m_expectHybridSceneTraversalRecoveryForTesting"));
    EXPECT_FALSE(ContainsText(rayTracingHeader, "m_reportedHybridSceneTraversalFallbackLoopForTesting"));
    EXPECT_FALSE(ContainsText(rayTracingHeader, "m_reportedHybridSceneTraversalFallbackLoopFailureForTesting"));
    EXPECT_FALSE(ContainsText(rayTracingHeader, "m_reportedHybridHardwareFallbackRestoreLoopForTesting"));
    EXPECT_FALSE(ContainsText(rayTracing, "forceHybridSceneTraversalFallback"));
    EXPECT_FALSE(ContainsText(rayTracing, "test forced hybrid software traversal fallback"));
    EXPECT_FALSE(ContainsText(rayTracing, "test hybrid software traversal recovered"));
    EXPECT_FALSE(ContainsText(swBvh, "m_reportedHybridHardwareFallbackRestoreLoopForTesting"));
    EXPECT_FALSE(ContainsText(rayTracing, "retried directly"));
    EXPECT_FALSE(ContainsText(kernelSystemHeader, "forceHybridHardwareFallbackSnapshotStaleForTesting"));
    EXPECT_FALSE(ContainsText(kernelSystemHeader, "forceHybridSceneTraversalFallback"));
    EXPECT_FALSE(ContainsText(kernelSystem, "forceHybridHardwareFallbackSnapshotStaleForTesting"));
    EXPECT_FALSE(ContainsText(kernelSystem, "forceHybridSceneTraversalFallback"));
    EXPECT_FALSE(ContainsText(smokeCmake, "NWB_TRANSPARENT_MULTI_FORCE_HYBRID_HARDWARE_FALLBACK_STALE"));
    EXPECT_FALSE(ContainsText(smokeCmake, "NWB_TRANSPARENT_MULTI_FORCE_HYBRID_TRAVERSAL_FALLBACK"));
    EXPECT_FALSE(ContainsText(smokeCmake, "nwb_transparent_multi_hybrid_fallback_smoke"));
    EXPECT_FALSE(ContainsText(smokeCmake, "nwb_transparent_multi_hybrid_fallback_capture_smoke"));
    EXPECT_FALSE(ContainsText(smokeCmake, "NWB_HYBRID_SHADOW_BOUNDARY_FALLBACK_BENCHMARK"));
    EXPECT_FALSE(ContainsText(smokeCmake, "nwb_hybrid_shadow_boundary_fallback_benchmark"));
    EXPECT_FALSE(ContainsText(smokeCmake, "nwb_hybrid_shadow_boundary_fallback_capture_smoke"));
    EXPECT_FALSE(ContainsText(smokeCmake, "nwb_transparent_multi_hybrid_fallback_stale_smoke"));
    const usize healthyCaptureOffset = smokeCmake.find("            nwb_hybrid_shadow_boundary_healthy_capture_smoke");
    const usize opaqueCaptureOffset = smokeCmake.find(
        "            nwb_hybrid_shadow_boundary_opaque_capture_smoke",
        healthyCaptureOffset
    );
    const usize opaqueCaptureEndOffset = smokeCmake.find(
        "        nwb_declare_executable(nwb_async_shadow_m4_sync_benchmark)",
        opaqueCaptureOffset
    );
    ASSERT_NE(healthyCaptureOffset, AStringView::npos);
    ASSERT_NE(opaqueCaptureOffset, AStringView::npos);
    ASSERT_NE(opaqueCaptureEndOffset, AStringView::npos);
    ASSERT_LT(healthyCaptureOffset, opaqueCaptureOffset);
    ASSERT_LT(opaqueCaptureOffset, opaqueCaptureEndOffset);
    const AStringView healthyCapture = smokeCmake.substr(
        healthyCaptureOffset,
        opaqueCaptureOffset - healthyCaptureOffset
    );
    const AStringView opaqueCapture = smokeCmake.substr(
        opaqueCaptureOffset,
        opaqueCaptureEndOffset - opaqueCaptureOffset
    );
    EXPECT_TRUE(ContainsText(healthyCapture, "$<TARGET_FILE:nwb_hybrid_shadow_boundary_healthy_benchmark>"));
    EXPECT_TRUE(ContainsText(
        healthyCapture,
        "\"--expect-log-message\" \"StressTestSmokeProject: enabled healthy hybrid transparent-shadow benchmark\""
    ));
    EXPECT_TRUE(ContainsText(
        healthyCapture,
        "\"--expect-log-message\" \"StressTestSmokeProject: RayQuery-capable hybrid shadow hardware available\""
    ));
    EXPECT_TRUE(ContainsText(
        healthyCapture,
        "\"--expect-log-message\" \"RendererSystem: dispatched software shadow traversal\""
    ));
    EXPECT_TRUE(ContainsText(
        healthyCapture,
        "\"--skip-log-message\" \"StressTestSmokeProject: hybrid shadow boundary skipped because RayQuery-capable hardware is unavailable\""
    ));
    EXPECT_TRUE(ContainsText(healthyCapture, "\"--skip-blocking-log-message\" \"[ERROR]\""));
    EXPECT_TRUE(ContainsText(healthyCapture, "\"--skip-blocking-log-message\" \"failed to resolve shader\""));
    EXPECT_TRUE(ContainsText(
        healthyCapture,
        "\"--reject-log-message\" \"RendererSystem: split opaque soft-shadow producer failed\\; retaining all-lit visibility\""
    ));
    EXPECT_TRUE(ContainsText(
        healthyCapture,
        "\"--reject-log-message\" \"RendererSystem: frozen hybrid hardware material-context restore failed\\; rejecting shadow preparation packet\""
    ));
    EXPECT_TRUE(ContainsText(
        healthyCapture,
        "\"--reject-log-message\" \"RendererSystem: restored frozen hybrid hardware material context\""
    ));
    EXPECT_TRUE(ContainsText(healthyCapture, "NWB_HYBRID_SHADOW_BOUNDARY_OPAQUE_BASELINE=0"));
    EXPECT_TRUE(ContainsText(
        healthyCapture,
        "CONFIGURATIONS dbg opt"
    ));
    EXPECT_FALSE(ContainsText(healthyCapture, "enabled natural opaque hardware-shadow baseline"));
    EXPECT_FALSE(ContainsText(healthyCapture, "RendererSystem: created RayQuery shadow compute pipeline"));

    EXPECT_TRUE(ContainsText(opaqueCapture, "$<TARGET_FILE:nwb_hybrid_shadow_boundary_healthy_benchmark>"));
    EXPECT_TRUE(ContainsText(
        opaqueCapture,
        "\"--expect-log-message\" \"StressTestSmokeProject: enabled natural opaque hardware-shadow baseline\""
    ));
    EXPECT_TRUE(ContainsText(
        opaqueCapture,
        "\"--expect-log-message\" \"StressTestSmokeProject: RayQuery-capable hybrid shadow hardware available\""
    ));
    EXPECT_TRUE(ContainsText(
        opaqueCapture,
        "\"--skip-log-message\" \"StressTestSmokeProject: hybrid shadow boundary skipped because RayQuery-capable hardware is unavailable\""
    ));
    EXPECT_TRUE(ContainsText(opaqueCapture, "\"--skip-blocking-log-message\" \"[ERROR]\""));
    EXPECT_TRUE(ContainsText(opaqueCapture, "\"--skip-blocking-log-message\" \"failed to resolve shader\""));
    EXPECT_TRUE(ContainsText(
        opaqueCapture,
        "\"--reject-log-message\" \"RendererSystem: dispatched software shadow traversal\""
    ));
    EXPECT_TRUE(ContainsText(
        opaqueCapture,
        "\"--reject-log-message\" \"RendererSystem: split opaque soft-shadow producer failed\\; retaining all-lit visibility\""
    ));
    EXPECT_TRUE(ContainsText(
        opaqueCapture,
        "\"--reject-log-message\" \"RendererSystem: frozen hybrid hardware material-context restore failed\\; rejecting shadow preparation packet\""
    ));
    EXPECT_TRUE(ContainsText(
        opaqueCapture,
        "\"--reject-log-message\" \"RendererSystem: restored frozen hybrid hardware material context\""
    ));
    EXPECT_TRUE(ContainsText(opaqueCapture, "NWB_HYBRID_SHADOW_BOUNDARY_OPAQUE_BASELINE=1"));
    EXPECT_TRUE(ContainsText(
        opaqueCapture,
        "CONFIGURATIONS dbg opt"
    ));
    EXPECT_FALSE(ContainsText(opaqueCapture, "enabled healthy hybrid transparent-shadow benchmark"));
    EXPECT_FALSE(ContainsText(opaqueCapture, "RendererSystem: created RayQuery shadow compute pipeline"));

    const usize transparentCaptureOffset = smokeCmake.find("            nwb_transparent_multi_capture_smoke");
    const usize transparentCaptureEndOffset = smokeCmake.find(
        "            nwb_transparent_multi_sw_capture_smoke",
        transparentCaptureOffset
    );
    ASSERT_NE(transparentCaptureOffset, AStringView::npos);
    ASSERT_NE(transparentCaptureEndOffset, AStringView::npos);
    ASSERT_LT(transparentCaptureOffset, transparentCaptureEndOffset);
    const AStringView transparentCapture = smokeCmake.substr(
        transparentCaptureOffset,
        transparentCaptureEndOffset - transparentCaptureOffset
    );
    EXPECT_TRUE(ContainsText(
        transparentCapture,
        "\"--expect-log-message\" \"RendererSystem: dispatched software shadow traversal\""
    ));
    EXPECT_TRUE(ContainsText(
        transparentCapture,
        "\"--reject-log-message\" \"RendererSystem: frozen hybrid hardware material-context restore failed\\; rejecting shadow preparation packet\""
    ));
    EXPECT_TRUE(ContainsText(
        transparentCapture,
        "ENVIRONMENT \"NWB_TRANSPARENT_MULTI_SPIN_ANGLE=0.6;NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME=6\""
    ));
    EXPECT_FALSE(ContainsText(transparentCapture, "RendererSystem: created RayQuery shadow compute pipeline"));

    const usize opaqueBaselineHelperOffset = stressTestProject.find(
        "[[nodiscard]] static bool hybridShadowOpaqueBaseline()"
    );
    const usize opaqueBaselineHelperEndOffset = stressTestProject.find(
        "static NotNullUniquePtr<NWB::Core::ECS::World> createWorldOrDie(",
        opaqueBaselineHelperOffset
    );
    ASSERT_NE(opaqueBaselineHelperOffset, AStringView::npos);
    ASSERT_NE(opaqueBaselineHelperEndOffset, AStringView::npos);
    const AStringView opaqueBaselineHelper = stressTestProject.substr(
        opaqueBaselineHelperOffset,
        opaqueBaselineHelperEndOffset - opaqueBaselineHelperOffset
    );
    EXPECT_TRUE(ContainsText(opaqueBaselineHelper, "#if defined(NWB_HYBRID_SHADOW_BOUNDARY_BENCHMARK)"));
    EXPECT_TRUE(ContainsText(
        opaqueBaselineHelper,
        "ReadSmokeEnvironmentFlag(\"NWB_HYBRID_SHADOW_BOUNDARY_OPAQUE_BASELINE\")"
    ));
    EXPECT_TRUE(ContainsText(opaqueBaselineHelper, "return s_enabled;"));

    const usize createCharacterOffset = stressTestProject.find(
        "[[nodiscard]] NWB::Core::ECS::EntityID createCharacter(const u32 index)"
    );
    const usize createCharacterEndOffset = stressTestProject.find(
        "[[nodiscard]] NWB::Core::ECS::EntityID createWall(",
        createCharacterOffset
    );
    ASSERT_NE(createCharacterOffset, AStringView::npos);
    ASSERT_NE(createCharacterEndOffset, AStringView::npos);
    const AStringView createCharacter = stressTestProject.substr(
        createCharacterOffset,
        createCharacterEndOffset - createCharacterOffset
    );
    EXPECT_TRUE(ContainsText(createCharacter, "const bool transparentMaterialClass = (index % 2u) == 0u;"));
    EXPECT_TRUE(ContainsText(
        createCharacter,
        "const bool transparent = !hybridShadowOpaqueBaseline() && transparentMaterialClass;"
    ));
    EXPECT_TRUE(ContainsText(createCharacter, "const f32 z = transparentMaterialClass ? s_TransparentRowZ : s_OpaqueRowZ;"));

    const usize hybridBenchmarkStartupOffset = stressTestProject.find(
        "#if defined(NWB_HYBRID_SHADOW_BOUNDARY_BENCHMARK)",
        createCharacterEndOffset
    );
    const usize hybridBenchmarkStartupEndOffset = stressTestProject.find(
        "const u32 transparentCharacterCount",
        hybridBenchmarkStartupOffset
    );
    ASSERT_NE(hybridBenchmarkStartupOffset, AStringView::npos);
    ASSERT_NE(hybridBenchmarkStartupEndOffset, AStringView::npos);
    const AStringView hybridBenchmarkStartup = stressTestProject.substr(
        hybridBenchmarkStartupOffset,
        hybridBenchmarkStartupEndOffset - hybridBenchmarkStartupOffset
    );
    EXPECT_TRUE(ContainsText(
        hybridBenchmarkStartup,
        "queryFeatureSupport(NWB::Core::Feature::RayTracingAccelStruct)"
    ));
    EXPECT_TRUE(ContainsText(hybridBenchmarkStartup, "queryFeatureSupport(NWB::Core::Feature::RayQuery)"));
    EXPECT_TRUE(ContainsText(hybridBenchmarkStartup, "if(!rayQueryCapable)"));
    EXPECT_TRUE(ContainsText(
        hybridBenchmarkStartup,
        "hybrid shadow boundary skipped because RayQuery-capable hardware is unavailable"
    ));
    EXPECT_TRUE(ContainsText(hybridBenchmarkStartup, "if(hybridShadowOpaqueBaseline())"));
    EXPECT_TRUE(ContainsText(hybridBenchmarkStartup, "enabled natural opaque hardware-shadow baseline"));
    EXPECT_TRUE(ContainsText(hybridBenchmarkStartup, "enabled healthy hybrid transparent-shadow benchmark"));
    EXPECT_FALSE(ContainsText(hybridBenchmarkStartup, "NWB_FATAL_ASSERT_MSG("));
    EXPECT_FALSE(ContainsText(stressTestProject, "forceHybridSceneTraversalFallback"));
    EXPECT_FALSE(ContainsText(stressTestProject, "NWB_HYBRID_SHADOW_BOUNDARY_FALLBACK_BENCHMARK"));
    EXPECT_FALSE(ContainsText(transparentMultiProject, "forceHybridHardwareFallbackSnapshotStaleForTesting"));
    EXPECT_FALSE(ContainsText(transparentMultiProject, "forceHybridSceneTraversalFallback"));
    EXPECT_FALSE(ContainsText(transparentMultiProject, "NWB_TRANSPARENT_MULTI_FORCE_HYBRID_TRAVERSAL_FALLBACK"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

