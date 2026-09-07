// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_contract_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_task_graph_geometry_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace EcsGraphicsTaskGraphContractTestDetail;
using EcsGraphicsTaskGraphContractTestDetail::AString;


// The native submission gate may only publish already-preflighted storage. Retained invisible meshes consume the
// accepted prefix of that storage, while each newly frozen geometry buffer consumes at most one additional slot.
TEST(EcsGraphics, ShadowTraceGeometryAcceptancePreflightsUnionCapacityAndPublishesLinearly){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString rayTracingHeaderSource;
    AString rayTracingSource;
    AString freezeSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "shadow_trace_geometry.h", rayTracingHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.cpp", rayTracingSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "shadow_trace_geometry.cpp", freezeSource));

    const AStringView rayTracingHeader(rayTracingHeaderSource.data(), rayTracingHeaderSource.size());
    const AStringView rayTracing(rayTracingSource.data(), rayTracingSource.size());
    EXPECT_TRUE(ContainsText(rayTracingHeader, "bool normalizationPending = false;"));

    const usize freezeOffset = rayTracing.find(
        "bool RendererRayTracingSystem::freezePreparedShadowTraceGeometryBuffers("
    );
    const usize confirmOffset = rayTracing.find(
        "void RendererRayTracingSystem::confirmPreparedShadowTraceGeometryNormalization()noexcept",
        freezeOffset
    );
    const usize invalidateOffset = rayTracing.find(
        "void RendererRayTracingSystem::invalidatePreparedShadowTraceGeometryBuffers()noexcept",
        confirmOffset
    );
    ASSERT_NE(freezeOffset, AStringView::npos);
    ASSERT_NE(confirmOffset, AStringView::npos);
    ASSERT_NE(invalidateOffset, AStringView::npos);

    const AStringView freeze(freezeSource.data(), freezeSource.size());
    EXPECT_TRUE(ContainsText(rayTracing.substr(freezeOffset, confirmOffset - freezeOffset), "return FreezePreparedShadowTraceGeometryBuffers("));
    EXPECT_TRUE(ContainsText(freeze, "const bool normalizedByAcceptedPacket = record->accepted;"));
    EXPECT_TRUE(ContainsText(freeze, ".normalizationPending = !normalizedByAcceptedPacket,"));
    EXPECT_TRUE(ContainsText(freeze, "AddOverflows<usize>(acceptedBuffers.size(), outPrepared.size())"));
    EXPECT_TRUE(ContainsText(freeze, "const usize acceptedCapacity = acceptedBuffers.size() + outPrepared.size();"));
    EXPECT_TRUE(ContainsText(freeze, "acceptedBuffers.reserve(acceptedCapacity);"));

    const AStringView confirm = rayTracing.substr(confirmOffset, invalidateOffset - confirmOffset);
    EXPECT_TRUE(ContainsText(confirm, "if(resource.buffer && resource.normalizationPending)"));
    EXPECT_TRUE(ContainsText(confirm, "m_acceptedShadowTraceGeometryBuffers.size() < m_acceptedShadowTraceGeometryBuffers.capacity()"));
    EXPECT_TRUE(ContainsText(confirm, "resource.normalizationPending = false;"));
    EXPECT_EQ(CountText(confirm, "m_acceptedShadowTraceGeometryBuffers.push_back(resource.buffer);"), 1u);
    EXPECT_FALSE(ContainsText(confirm, "for(const Core::BufferHandle& acceptedBuffer"));
    EXPECT_FALSE(ContainsText(confirm, ".reserve("));
}


// Accepted static scene-BVH and software-material cache hits have no upload bytes, but they still freeze the exact
// storage identities and traversal table during preflight. Pure software consumes that snapshot or rejects the
// packet; hybrid consumes it or restores HW. Neither route regathers ECS/material data after graph declaration.
TEST(EcsGraphics, SoftwareStaticSceneCacheFreezesTraversalWithoutRecordingTimeRegather){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString rayTracingHeaderSource;
    AString rayTracingSource;
    AString swBvhSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.h", rayTracingHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.cpp", rayTracingSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_swbvh.cpp", swBvhSource));

    const AStringView rayTracingHeader(rayTracingHeaderSource.data(), rayTracingHeaderSource.size());
    const AStringView rayTracing(rayTracingSource.data(), rayTracingSource.size());
    const AStringView swBvh(swBvhSource.data(), swBvhSource.size());

    EXPECT_TRUE(ContainsText(rayTracingHeader, "capturePreparedSceneBvhCacheReuse"));
    EXPECT_TRUE(ContainsText(rayTracingHeader, "capturePreparedShadowMaterialContextCacheReuse"));
    EXPECT_TRUE(ContainsText(rayTracingHeader, "bool m_preparedSceneBvhUploadRequired = false;"));
    EXPECT_TRUE(ContainsText(rayTracingHeader, "bool m_preparedShadowMaterialContextUploadRequired = false;"));
    EXPECT_TRUE(ContainsText(swBvh, "&& !capturePreparedSceneBvhCacheReuse(sceneStaticHash, instanceCount)"));
    EXPECT_TRUE(ContainsText(swBvh, "&& !capturePreparedShadowMaterialContextCacheReuse("));

    const usize cacheCaptureOffset = rayTracing.find("bool RendererRayTracingSystem::capturePreparedSceneBvhCacheReuse(");
    const usize cacheCaptureEndOffset = rayTracing.find("bool RendererRayTracingSystem::matchesPreparedSceneBvh(", cacheCaptureOffset);
    ASSERT_NE(cacheCaptureOffset, AStringView::npos);
    ASSERT_NE(cacheCaptureEndOffset, AStringView::npos);
    const AStringView cacheCapture = rayTracing.substr(cacheCaptureOffset, cacheCaptureEndOffset - cacheCaptureOffset);
    EXPECT_TRUE(ContainsText(cacheCapture, "state.m_sceneSwBvhStaticSceneHashValid"));
    EXPECT_TRUE(ContainsText(cacheCapture, "m_preparedSceneBvhReady = true;"));
    EXPECT_TRUE(ContainsText(cacheCapture, "m_preparedSceneBvhUploadRequired = false;"));
    EXPECT_FALSE(ContainsText(cacheCapture, "m_preparedSceneBvhNodeBytes.resize"));
    EXPECT_FALSE(ContainsText(cacheCapture, "m_preparedSceneBvhInstanceBytes.resize"));

    const usize materialCacheCaptureOffset = rayTracing.find(
        "bool RendererRayTracingSystem::capturePreparedShadowMaterialContextCacheReuse("
    );
    const usize materialCacheCaptureEndOffset = rayTracing.find(
        "bool RendererRayTracingSystem::matchesPreparedShadowMaterialContext(",
        materialCacheCaptureOffset
    );
    ASSERT_NE(materialCacheCaptureOffset, AStringView::npos);
    ASSERT_NE(materialCacheCaptureEndOffset, AStringView::npos);
    const AStringView materialCacheCapture = rayTracing.substr(
        materialCacheCaptureOffset,
        materialCacheCaptureEndOffset - materialCacheCaptureOffset
    );
    EXPECT_TRUE(ContainsText(materialCacheCapture, "state.m_swShadowMaterialContextHashValid"));
    EXPECT_TRUE(ContainsText(materialCacheCapture, "m_preparedShadowMaterialContextReady = true;"));
    EXPECT_TRUE(ContainsText(materialCacheCapture, "m_preparedShadowMaterialContextUploadRequired = false;"));
    EXPECT_FALSE(ContainsText(materialCacheCapture, "m_preparedShadowInstanceMaterialBytes.resize"));
    EXPECT_FALSE(ContainsText(materialCacheCapture, "m_preparedShadowInstanceBytes.resize"));
    EXPECT_FALSE(ContainsText(materialCacheCapture, "m_preparedShadowMaterialTypedBytes.resize"));

    const usize retainOffset = rayTracing.find("bool RendererRayTracingSystem::retainPreparedSceneBvhUploads(");
    const usize retainEndOffset = rayTracing.find("void RendererRayTracingSystem::confirmPreparedSceneBvhUploads()", retainOffset);
    ASSERT_NE(retainOffset, AStringView::npos);
    ASSERT_NE(retainEndOffset, AStringView::npos);
    const AStringView retain = rayTracing.substr(retainOffset, retainEndOffset - retainOffset);
    EXPECT_TRUE(ContainsText(retain, "outNodeBlob = {};"));
    EXPECT_TRUE(ContainsText(retain, "outInstanceBlob = {};"));
    EXPECT_TRUE(ContainsText(retain, "if(!m_preparedSceneBvhUploadRequired)"));
    EXPECT_TRUE(ContainsText(retain, "state.m_sceneSwBvhStaticSceneHash != m_preparedSceneBvhStaticSceneHash"));
    EXPECT_TRUE(ContainsText(rayTracing, "if(!m_preparedSceneBvhReady || !m_preparedSceneBvhUploadRequired)"));

    const usize materialRetainOffset = rayTracing.find(
        "bool RendererRayTracingSystem::retainPreparedShadowMaterialContextUploads("
    );
    const usize materialRetainEndOffset = rayTracing.find(
        "bool RendererRayTracingSystem::retainPreparedHybridHardwareMaterialContextFallbackUploads(",
        materialRetainOffset
    );
    ASSERT_NE(materialRetainOffset, AStringView::npos);
    ASSERT_NE(materialRetainEndOffset, AStringView::npos);
    const AStringView materialRetain = rayTracing.substr(
        materialRetainOffset,
        materialRetainEndOffset - materialRetainOffset
    );
    const usize materialCacheRetainOffset = materialRetain.find(
        "if(!m_preparedShadowMaterialContextUploadRequired)"
    );
    const usize materialUploadCopyOffset = materialRetain.find("outInstanceMaterialBlob = graph.copyUploadData(");
    ASSERT_NE(materialCacheRetainOffset, AStringView::npos);
    ASSERT_NE(materialUploadCopyOffset, AStringView::npos);
    ASSERT_LT(materialCacheRetainOffset, materialUploadCopyOffset);
    const AStringView materialCacheRetain = materialRetain.substr(
        materialCacheRetainOffset,
        materialUploadCopyOffset - materialCacheRetainOffset
    );
    EXPECT_TRUE(ContainsText(materialCacheRetain, "state.m_swShadowMaterialContextHash != m_preparedShadowMaterialContextHash"));
    EXPECT_TRUE(ContainsText(materialCacheRetain, "return true;"));

    const usize materialConfirmOffset = rayTracing.find(
        "void RendererRayTracingSystem::confirmPreparedShadowMaterialContextUploads()"
    );
    const usize materialConfirmEndOffset = rayTracing.find(
        "void RendererRayTracingSystem::clearPreparedSceneBvh()",
        materialConfirmOffset
    );
    ASSERT_NE(materialConfirmOffset, AStringView::npos);
    ASSERT_NE(materialConfirmEndOffset, AStringView::npos);
    const AStringView materialConfirm = rayTracing.substr(
        materialConfirmOffset,
        materialConfirmEndOffset - materialConfirmOffset
    );
    EXPECT_TRUE(ContainsText(
        materialConfirm,
        "if(m_preparedShadowMaterialContextReady && m_preparedShadowMaterialContextUploadRequired)"
    ));
    EXPECT_EQ(CountText(materialConfirm, "clearPreparedShadowMaterialContext();"), 1u);

    const usize materialHashOffset = swBvh.find("[[nodiscard]] u64 ComputeShadowMaterialContextHash(");
    const usize materialHashEndOffset = swBvh.find("// Cross-frame cache pins raw keys", materialHashOffset);
    ASSERT_NE(materialHashOffset, AStringView::npos);
    ASSERT_NE(materialHashEndOffset, AStringView::npos);
    const AStringView materialHash = swBvh.substr(materialHashOffset, materialHashEndOffset - materialHashOffset);
    const usize instanceMaterialsHashOffset = materialHash.find("reinterpret_cast<const u8*>(instanceMaterials.data())");
    const usize instanceDataHashOffset = materialHash.find("reinterpret_cast<const u8*>(instanceData.data())");
    const usize typedBytesHashOffset = materialHash.find("materialTypedBytes.data()");
    ASSERT_NE(instanceMaterialsHashOffset, AStringView::npos);
    ASSERT_NE(instanceDataHashOffset, AStringView::npos);
    ASSERT_NE(typedBytesHashOffset, AStringView::npos);
    EXPECT_LT(instanceMaterialsHashOffset, instanceDataHashOffset);
    EXPECT_LT(instanceDataHashOffset, typedBytesHashOffset);

    const usize traversalOffset = rayTracing.find("bool RendererRayTracingSystem::recordPreparedSceneSwBvhTraversal()");
    const usize traversalEndOffset = rayTracing.find("bool RendererRayTracingSystem::retainPreparedSceneBvhUploads(", traversalOffset);
    ASSERT_NE(traversalOffset, AStringView::npos);
    ASSERT_NE(traversalEndOffset, AStringView::npos);
    const AStringView traversal = rayTracing.substr(traversalOffset, traversalEndOffset - traversalOffset);
    EXPECT_TRUE(ContainsText(traversal, "const bool sceneSnapshotValid ="));
    EXPECT_TRUE(ContainsText(traversal, "const bool materialSnapshotValid ="));
    const usize rejectionOffset = traversal.find("const auto rejectPreparedTraversal = [&]()");
    const usize rejectionEndOffset = traversal.find("    };", rejectionOffset);
    ASSERT_NE(rejectionOffset, AStringView::npos);
    ASSERT_NE(rejectionEndOffset, AStringView::npos);
    const AStringView rejection = traversal.substr(rejectionOffset, rejectionEndOffset - rejectionOffset);
    EXPECT_TRUE(ContainsText(
        rejection,
        "if(!m_preparedSceneBvhUploadRequired)\n"
        "            state.m_sceneSwBvhStaticSceneHashValid = false;"
    ));
    EXPECT_TRUE(ContainsText(
        rejection,
        "if(!m_preparedShadowMaterialContextUploadRequired)\n"
        "            state.m_swShadowMaterialContextHashValid = false;"
    ));
    const AStringView traversalValidation = traversal.substr(rejectionEndOffset + 6u);
    EXPECT_EQ(CountText(traversalValidation, "return false;"), 0u);
    EXPECT_EQ(CountText(traversalValidation, "return rejectPreparedTraversal();"), 5u);
    EXPECT_TRUE(ContainsText(traversal, "const bool tablesMatch ="));
    EXPECT_FALSE(ContainsText(traversal, "restoreMutableTables"));
    EXPECT_EQ(CountText(traversal, ".clear();"), 0u);
    EXPECT_EQ(CountText(traversal, ".push_back("), 0u);
    EXPECT_FALSE(ContainsText(traversal, "seenThisFrame"));

    const usize pureRecordOffset = rayTracing.find(
        "bool RendererRayTracingSystem::recordPreflightShadowVisibilityResources("
    );
    const usize pureRecordEndOffset = rayTracing.find(
        "bool RendererRayTracingSystem::recordPreflightHybridSoftwareTail(",
        pureRecordOffset
    );
    ASSERT_NE(pureRecordOffset, AStringView::npos);
    ASSERT_NE(pureRecordEndOffset, AStringView::npos);
    const AStringView pureRecord = rayTracing.substr(pureRecordOffset, pureRecordEndOffset - pureRecordOffset);
    EXPECT_TRUE(ContainsText(pureRecord, "if(!m_preparedSceneSwBvhReady)"));
    EXPECT_TRUE(ContainsText(pureRecord, "if(!recordPreparedSceneSwBvhTraversal())"));
    EXPECT_FALSE(ContainsText(pureRecord, "buildSceneSwBvh("));

    const usize hybridTailOffset = rayTracing.find("bool RendererRayTracingSystem::recordPreflightHybridSoftwareTail(");
    const usize hybridTailEndOffset = rayTracing.find("// A graph-owned hybrid plan may exist", hybridTailOffset);
    ASSERT_NE(hybridTailOffset, AStringView::npos);
    ASSERT_NE(hybridTailEndOffset, AStringView::npos);
    const AStringView hybridTail = rayTracing.substr(hybridTailOffset, hybridTailEndOffset - hybridTailOffset);
    EXPECT_TRUE(ContainsText(hybridTail, "const bool hybridSceneTraversalFrozen ="));
    EXPECT_TRUE(ContainsText(hybridTail, "&& m_preparedSceneSwBvhReady"));
    EXPECT_TRUE(ContainsText(hybridTail, "&& recordPreparedSceneSwBvhTraversal()"));
    EXPECT_TRUE(ContainsText(hybridTail, "recordPreparedHybridHardwareMaterialContextFallback("));
    EXPECT_FALSE(ContainsText(hybridTail, "buildSceneSwBvh("));
    EXPECT_FALSE(ContainsText(hybridTail, "Core::Alloc::ScratchArena"));
}


// Opaque hardware shadows never prepare the optional software traversal resources. Keep the direct compatibility
// builder behind the frozen hybrid-resource gate without removing the real pure-software fallback.
TEST(EcsGraphics, HardwareOpaqueShadowPreparationDoesNotEagerlyBuildSoftwareBvhs){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString rayTracingSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.cpp", rayTracingSource));

    const AStringView rayTracing(rayTracingSource.data(), rayTracingSource.size());
    const usize recordOffset = rayTracing.find(
        "bool RendererRayTracingSystem::recordPreflightShadowVisibilityResources("
    );
    const usize recordEndOffset = rayTracing.find(
        "bool RendererRayTracingSystem::recordPreflightHybridSoftwareTail(",
        recordOffset
    );
    ASSERT_NE(recordOffset, AStringView::npos);
    ASSERT_NE(recordEndOffset, AStringView::npos);
    const AStringView record = rayTracing.substr(recordOffset, recordEndOffset - recordOffset);

    const usize hardwareBranchOffset = record.find("if(m_shadowVisibilityHardwareSupported){");
    const usize softwareBranchOffset = record.find("const bool meshSwBvhReady =", hardwareBranchOffset);
    ASSERT_NE(hardwareBranchOffset, AStringView::npos);
    ASSERT_NE(softwareBranchOffset, AStringView::npos);
    const AStringView hardwareBranch = record.substr(
        hardwareBranchOffset,
        softwareBranchOffset - hardwareBranchOffset
    );
    const usize directBuildAssignmentOffset = hardwareBranch.find("const bool directMeshSwBvhBuildReady =");
    const usize hybridTailCallOffset = hardwareBranch.find("return recordPreflightHybridSoftwareTail(");
    const usize directBuildCallOffset = hardwareBranch.find("buildPendingMeshSwBvh(commandList, scratchArena)");
    ASSERT_NE(directBuildAssignmentOffset, AStringView::npos);
    ASSERT_NE(hybridTailCallOffset, AStringView::npos);
    ASSERT_NE(directBuildCallOffset, AStringView::npos);
    EXPECT_TRUE(ContainsText(
        hardwareBranch,
        "const bool directMeshSwBvhBuildReady = !m_shadowVisibilityHybridResourcesPreflighted\n"
        "            || meshSwBvhBuildsGraphOwned\n"
        "            || buildPendingMeshSwBvh(commandList, scratchArena)"
    ));
    EXPECT_EQ(CountText(hardwareBranch, "buildPendingMeshSwBvh(commandList, scratchArena)"), 1u);
    EXPECT_LT(directBuildAssignmentOffset, directBuildCallOffset);
    EXPECT_LT(directBuildCallOffset, hybridTailCallOffset);

    const AStringView softwareBranch = record.substr(softwareBranchOffset);
    EXPECT_TRUE(ContainsText(softwareBranch, ": buildPendingMeshSwBvh(commandList, scratchArena)"));
}


// Hybrid transparent shadows build a software BVH on ray-tracing hardware, so both static and skinned trace inputs
// must expose the raw views consumed by their global descriptor-heap slots.
TEST(EcsGraphics, HybridSoftwareBvhInputsExposeRawViewsOnRayTracingHardware){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString meshResourcesSource;
    AString skinningRuntimeCacheSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "mesh" / "mesh_resources.cpp", meshResourcesSource));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_mesh" / "skinning" / "runtime_cache_resources.cpp",
        skinningRuntimeCacheSource
    ));

    const AStringView meshResources(meshResourcesSource.data(), meshResourcesSource.size());
    const AStringView skinningRuntimeCache(skinningRuntimeCacheSource.data(), skinningRuntimeCacheSource.size());
    EXPECT_TRUE(ContainsText(
        meshResources,
        "NWB_TEXT(\"position\"),\n"
        "        true,\n"
        "        rtSupported"
    ));
    EXPECT_TRUE(ContainsText(meshResources, "indexFlags.canHaveRawViews = true;"));
    EXPECT_TRUE(ContainsText(
        skinningRuntimeCache,
        "NWB_TEXT(\"skinned position\"),\n"
        "        true,\n"
        "        rtSupported"
    ));
    EXPECT_TRUE(ContainsText(
        skinningRuntimeCache,
        "NWB_TEXT(\"rt triangle index\"),\n"
        "            true,\n"
        "            rtSupported"
    ));
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
    AString meshResourcesSource;
    AString meshTypesSource;
    AString rendererStateHeaderSource;
    AString rendererStateSource;
    AString systemSource;
    ASSERT_TRUE(ReadRendererSources(
        repoRoot,
        {
            "renderer_frame_pipeline_graph_shadow_prepare.cpp",
            "renderer_frame_pipeline_graph_shadow_visibility.cpp",
            "renderer_frame_pipeline_graph_surfel_gi.cpp",
            "renderer_frame_pipeline_graph.cpp",
        },
        taskGraphSource
    ));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.h", rayTracingHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.cpp", rayTracingSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_swbvh.cpp", swBvhSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "mesh" / "mesh_resources.cpp", meshResourcesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "mesh" / "renderer_mesh_types.h", meshTypesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "renderer_raytracing_state.h", rendererStateHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "renderer_raytracing_state.cpp", rendererStateSource));
    ASSERT_TRUE(ReadRendererFramePipelineRuntimeSources(repoRoot, systemSource));
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());
    const AStringView rayTracingHeader(rayTracingHeaderSource.data(), rayTracingHeaderSource.size());
    const AStringView rayTracing(rayTracingSource.data(), rayTracingSource.size());
    const AStringView swBvh(swBvhSource.data(), swBvhSource.size());
    const AStringView meshResources(meshResourcesSource.data(), meshResourcesSource.size());
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
        "const Core::ResourceStates::Mask blasInitialState = state.backingFresh\n"
        "            ? Core::ResourceStates::Common\n"
        "            : Core::ResourceStates::Unknown\n"
        "        ;"
    ));
    EXPECT_TRUE(ContainsText(taskGraph, "AccelStructResourceDesc(blasIdentity, \"Mesh BLAS\").setInitialState(blasInitialState)"));

    EXPECT_TRUE(ContainsText(rendererStateHeader, "bool m_tlasBackingFresh = false;"));
    EXPECT_TRUE(ContainsText(rendererStateHeader, "bool m_tlasBackingStateHandoffPending = false;"));
    EXPECT_TRUE(ContainsText(rendererState, "m_tlasBackingFresh = false;"));
    EXPECT_TRUE(ContainsText(rendererState, "m_tlasBackingStateHandoffPending = false;"));
    EXPECT_TRUE(ContainsText(swBvh, "m_rayTracingState.m_tlasBackingFresh = true;"));
    EXPECT_TRUE(ContainsText(
        swBvh,
        "if(m_rayTracingState.m_tlasBackingFresh)\n"
        "            m_rayTracingState.m_tlasBackingStateHandoffPending = true;"
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
        "m_meshSystem.confirmAcceptedRayTracingStateHandoffs();"
    ));
    EXPECT_TRUE(ContainsText(
        meshResources,
        "if(mesh.blasBackingFresh && mesh.blasBackingStateHandoffPending){\n"
        "            mesh.blasBackingFresh = false;\n"
        "            mesh.blasBackingStateHandoffPending = false;"
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
        "m_meshSystem.discardRayTracingBuildState();"
    ));
    EXPECT_FALSE(ContainsText(
        rayTracing.substr(discardPreflightOffset, preflightOffset - discardPreflightOffset),
        "m_tlasBackingFresh = false;"
    ));
    EXPECT_FALSE(ContainsText(
        rayTracing.substr(discardPreflightOffset, preflightOffset - discardPreflightOffset),
        "meshResources.blasBackingFresh = false;"
    ));
    const usize discardMeshBuildStateOffset = meshResources.find("void RendererMeshSystem::discardRayTracingBuildState()noexcept");
    const usize collectMeshBuildStateOffset = meshResources.find(
        "bool RendererMeshSystem::collectSoftwareBvhParentBuildStates(",
        discardMeshBuildStateOffset
    );
    ASSERT_NE(discardMeshBuildStateOffset, AStringView::npos);
    ASSERT_NE(collectMeshBuildStateOffset, AStringView::npos);
    const AStringView discardMeshBuildState = meshResources.substr(
        discardMeshBuildStateOffset,
        collectMeshBuildStateOffset - discardMeshBuildStateOffset
    );
    EXPECT_TRUE(ContainsText(discardMeshBuildState, "mesh.blasBackingStateHandoffPending = false;"));
    EXPECT_TRUE(ContainsText(discardMeshBuildState, "mesh.blasBuildPending = true;"));
    EXPECT_TRUE(ContainsText(discardMeshBuildState, "mesh.swBvhBuildPending = true;"));
    EXPECT_FALSE(ContainsText(discardMeshBuildState, "mesh.blasBackingFresh = false;"));
    // Candidate creation happens after recording but before native acceptance. A failed prepare callback therefore
    // rejects the graph without publishing state; a failed accepted callback is surfaced by the unified state guard.
    EXPECT_TRUE(ContainsText(
        system,
        "context->stateReady = false;\n"
        "            renderer.m_raytracingSystem.discardPreflightShadowVisibilityResources();\n"
        "            return false;"
    ));
    EXPECT_TRUE(ContainsText(
        system,
        "context->stateReady = renderer.m_shadowPreparePersistentState.commit(*context->stateCandidate);"
    ));
    EXPECT_TRUE(ContainsText(
        system,
        "if(!context->stateReady){\n"
        "            renderer.m_raytracingSystem.discardPreflightShadowVisibilityResources();"
    ));
    EXPECT_TRUE(ContainsText(system, "const bool acceptedStateLost ="));
    EXPECT_TRUE(ContainsText(
        system,
        "(shadowPrepareSubmissionToken.valid() && !shadowPrepareStateLifecycle.stateReady)"
    ));
    EXPECT_TRUE(ContainsText(
        system,
        "if(!recovered || acceptedStateLost || (!presentationSignalReady && finalPresentationSubmissionToken.valid()))"
    ));
    const usize stateReadyFalseOffset = system.find("context->stateReady = false;");
    const usize acceptedStateLostOffset = system.find("const bool acceptedStateLost =");
    const usize stateLossRecoveryGuardOffset = system.find(
        "if(!recovered || acceptedStateLost || (!presentationSignalReady && finalPresentationSubmissionToken.valid()))"
    );
    const usize stateLossRecoveryOffset = system.find("failFrameRenderRecovery();", stateLossRecoveryGuardOffset);
    ASSERT_NE(stateReadyFalseOffset, AStringView::npos);
    ASSERT_NE(acceptedStateLostOffset, AStringView::npos);
    ASSERT_NE(stateLossRecoveryGuardOffset, AStringView::npos);
    ASSERT_NE(stateLossRecoveryOffset, AStringView::npos);
    EXPECT_LT(commitPersistentStateOffset, confirmSceneTlasOffset);
    EXPECT_LT(confirmSceneTlasOffset, confirmMeshBlasOffset);
    EXPECT_LT(confirmMeshBlasOffset, confirmDirectHandoffOffset);
    EXPECT_LT(stateReadyFalseOffset, acceptedStateLostOffset);
    EXPECT_LT(commitPersistentStateOffset, acceptedStateLostOffset);
    EXPECT_LT(acceptedStateLostOffset, stateLossRecoveryGuardOffset);
    EXPECT_LT(stateLossRecoveryGuardOffset, stateLossRecoveryOffset);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

