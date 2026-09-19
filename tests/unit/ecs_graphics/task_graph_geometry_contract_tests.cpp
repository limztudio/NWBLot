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
// storage identities and traversal table during preflight. Software recording consumes that snapshot or rejects
// the packet without regathering ECS/material data after graph declaration.
TEST(EcsGraphics, SoftwareStaticSceneCacheFreezesTraversalWithoutRecordingTimeRegather){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString rayTracingHeaderSource;
    AString rayTracingSource;
    AString swBvhSource;
    AString materialContextSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.h", rayTracingHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.cpp", rayTracingSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_swbvh.cpp", swBvhSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_shadow_material_context.cpp", materialContextSource));

    const AStringView materialContext(materialContextSource.data(), materialContextSource.size());
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

    const usize materialCacheCaptureOffset = materialContext.find(
        "bool RendererRayTracingSystem::capturePreparedShadowMaterialContextCacheReuse("
    );
    const usize materialCacheCaptureEndOffset = materialContext.find(
        "bool RendererRayTracingSystem::matchesPreparedShadowMaterialContext(",
        materialCacheCaptureOffset
    );
    ASSERT_NE(materialCacheCaptureOffset, AStringView::npos);
    ASSERT_NE(materialCacheCaptureEndOffset, AStringView::npos);
    const AStringView materialCacheCapture = materialContext.substr(
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

    const usize materialRetainOffset = materialContext.find(
        "bool RendererRayTracingSystem::retainPreparedShadowMaterialContextUploads("
    );
    const usize materialRetainEndOffset = materialContext.find(
        "void RendererRayTracingSystem::confirmPreparedShadowMaterialContextUploads()",
        materialRetainOffset
    );
    ASSERT_NE(materialRetainOffset, AStringView::npos);
    ASSERT_NE(materialRetainEndOffset, AStringView::npos);
    const AStringView materialRetain = materialContext.substr(
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

    const usize materialConfirmOffset = materialContext.find(
        "void RendererRayTracingSystem::confirmPreparedShadowMaterialContextUploads()"
    );
    const usize materialConfirmEndOffset = materialContext.find(
        "NWB_IMPL_END",
        materialConfirmOffset
    );
    ASSERT_NE(materialConfirmOffset, AStringView::npos);
    ASSERT_NE(materialConfirmEndOffset, AStringView::npos);
    const AStringView materialConfirm = materialContext.substr(
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
        "NWB_IMPL_END",
        pureRecordOffset
    );
    ASSERT_NE(pureRecordOffset, AStringView::npos);
    ASSERT_NE(pureRecordEndOffset, AStringView::npos);
    const AStringView pureRecord = rayTracing.substr(pureRecordOffset, pureRecordEndOffset - pureRecordOffset);
    EXPECT_TRUE(ContainsText(pureRecord, "if(!m_preparedSceneSwBvhReady)"));
    EXPECT_TRUE(ContainsText(pureRecord, "if(!recordPreparedSceneSwBvhTraversal())"));
    EXPECT_FALSE(ContainsText(pureRecord, "buildSceneSwBvh("));
}


// Hardware shadows share the TLAS route; only devices without ray queries prepare and record software BVHs.
TEST(EcsGraphics, HardwareShadowPreparationKeepsSoftwareBvhsExclusiveToSoftwareDevices){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString rayTracingSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.cpp", rayTracingSource));
    const AStringView rayTracing(rayTracingSource.data(), rayTracingSource.size());
    const usize preflightOffset = rayTracing.find("bool RendererRayTracingSystem::preflightShadowVisibilityResources(");
    const usize recordOffset = rayTracing.find("bool RendererRayTracingSystem::recordPreflightShadowVisibilityResources(");
    const usize recordEndOffset = rayTracing.find("NWB_IMPL_END", recordOffset);
    ASSERT_NE(preflightOffset, AStringView::npos);
    ASSERT_NE(recordOffset, AStringView::npos);
    ASSERT_NE(recordEndOffset, AStringView::npos);

    const AStringView preflight = rayTracing.substr(preflightOffset, recordOffset - preflightOffset);
    const usize hardwarePreflightOffset = preflight.find("if(m_shadowVisibilityHardwareSupported){");
    const usize softwarePreflightOffset = preflight.find("// No hardware ray tracing:", hardwarePreflightOffset);
    ASSERT_NE(hardwarePreflightOffset, AStringView::npos);
    ASSERT_NE(softwarePreflightOffset, AStringView::npos);
    const AStringView hardwarePreflight = preflight.substr(hardwarePreflightOffset, softwarePreflightOffset - hardwarePreflightOffset);
    EXPECT_TRUE(ContainsText(hardwarePreflight, "preparePendingMeshBlasResources(scratchArena)"));
    EXPECT_TRUE(ContainsText(hardwarePreflight, "prepareSceneTlasResources(scratchArena)"));
    EXPECT_TRUE(ContainsText(hardwarePreflight, "prepareHardwareTransparentShadowResources(targets)"));
    EXPECT_FALSE(ContainsText(hardwarePreflight, "preparePendingMeshSwBvhResources("));
    EXPECT_FALSE(ContainsText(hardwarePreflight, "prepareSceneSwBvhResources("));
    EXPECT_FALSE(ContainsText(hardwarePreflight, "capturePreparedMeshSwBvhBuilds("));
    const AStringView softwarePreflight = preflight.substr(softwarePreflightOffset);
    EXPECT_TRUE(ContainsText(softwarePreflight, "preparePendingMeshSwBvhResources(scratchArena)"));
    EXPECT_TRUE(ContainsText(softwarePreflight, "prepareSceneSwBvhResources(scratchArena)"));
    EXPECT_TRUE(ContainsText(softwarePreflight, "capturePreparedMeshSwBvhBuilds(scratchArena)"));

    const AStringView record = rayTracing.substr(recordOffset, recordEndOffset - recordOffset);
    const usize hardwareRecordOffset = record.find("if(m_shadowVisibilityHardwareSupported){");
    const usize softwareRecordOffset = record.find("const bool meshSwBvhReady =", hardwareRecordOffset);
    ASSERT_NE(hardwareRecordOffset, AStringView::npos);
    ASSERT_NE(softwareRecordOffset, AStringView::npos);
    const AStringView hardwareRecord = record.substr(hardwareRecordOffset, softwareRecordOffset - hardwareRecordOffset);
    EXPECT_TRUE(ContainsText(hardwareRecord, "recordPreparedMeshBlasBuilds("));
    EXPECT_TRUE(ContainsText(hardwareRecord, "recordPreparedSceneTlasBuild("));
    EXPECT_FALSE(ContainsText(hardwareRecord, "buildPendingMeshSwBvh("));
    EXPECT_FALSE(ContainsText(hardwareRecord, "recordPreparedMeshSwBvhBuilds("));
    const AStringView softwareRecord = record.substr(softwareRecordOffset);
    EXPECT_TRUE(ContainsText(softwareRecord, "buildPendingMeshSwBvh(commandList, scratchArena)"));
    EXPECT_TRUE(ContainsText(softwareRecord, "recordPreparedMeshSwBvhBuilds("));
    EXPECT_TRUE(ContainsText(softwareRecord, "recordPreparedSceneSwBvhTraversal()"));
}


// Hardware material reconstruction and software traversal both read static/skinned geometry through raw heap views.
TEST(EcsGraphics, RayTracingMaterialAndSoftwareInputsExposeRawViews){
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
        "MakeNotNull(NWB_TEXT(\"position\")),\n"
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
    AString sceneGraphSource;
    AString hardwareCausticsSource;
    AString preparedBuildsSource;
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
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl/ecs_render/raytrace/task_graph_scene_resources.cpp", sceneGraphSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.h", rayTracingHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.cpp", rayTracingSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_swbvh.cpp", swBvhSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "mesh" / "mesh_raytracing_handoff.cpp", meshResourcesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "mesh" / "renderer_mesh_types.h", meshTypesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "renderer_raytracing_state.h", rendererStateHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "renderer_raytracing_state.cpp", rendererStateSource));
    ASSERT_TRUE(ReadRendererFramePipelineRuntimeSources(repoRoot, systemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "hardware_caustics_stage_builder.cpp", hardwareCausticsSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "prepared_builds.h", preparedBuildsSource));
    const AStringView hardwareCaustics(hardwareCausticsSource.data(), hardwareCausticsSource.size());
    const AStringView preparedBuilds(preparedBuildsSource.data(), preparedBuildsSource.size());
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());
    const AStringView sceneGraph(sceneGraphSource.data(), sceneGraphSource.size());
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
    EXPECT_TRUE(ContainsText(preparedBuilds, "bool backingFresh = false;"));
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
    // Refraction and reflection use the neutral importer, forwarding the same generation-aware initial state as
    // the direct shadow, GI, and caustic imports. Include the extracted caustic owner in the generation checks.
    static constexpr AStringView s_SceneTlasImport = "AccelStructResourceDesc(Name(\"render.deferred_effects.tlas\"), \"Scene TLAS\")";
    EXPECT_EQ(
        CountText(taskGraph, s_SceneTlasImport) + CountText(sceneGraph, s_SceneTlasImport)
            + CountText(hardwareCaustics, s_SceneTlasImport),
        5u
    );
    // Hardware shadows import early; reflection/refraction import only when that shared read set is still absent.
    EXPECT_EQ(CountText(taskGraph, "sceneReads = ImportRayTracingSceneGraphReads("), 2u);
    EXPECT_EQ(
        CountText(taskGraph, "sceneTlasBackingInitialState()")
            + CountText(hardwareCaustics, "sceneTlasBackingInitialState()"),
        6u
    );
    EXPECT_TRUE(ContainsText(
        taskGraph,
        "ImportRayTracingSceneGraphReads(\n"
        "            m_deferredLightingTaskGraph, sceneResources, m_raytracingSystem.sceneTlasBackingInitialState()"
    ));
    EXPECT_EQ(CountText(sceneGraph, ".setInitialState(tlasInitialState)"), 1u);
    EXPECT_EQ(
        CountText(taskGraph, ".setInitialState(m_raytracingSystem.sceneTlasBackingInitialState())")
            + CountText(sceneGraph, ".setInitialState(tlasInitialState)")
            + CountText(hardwareCaustics, ".setInitialState(m_raytracingSystem.sceneTlasBackingInitialState())"),
        4u
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
    const usize discardMeshBuildStateEndOffset = meshResources.find(
        "NWB_IMPL_END",
        discardMeshBuildStateOffset
    );
    ASSERT_NE(discardMeshBuildStateOffset, AStringView::npos);
    ASSERT_NE(discardMeshBuildStateEndOffset, AStringView::npos);
    const AStringView discardMeshBuildState = meshResources.substr(
        discardMeshBuildStateOffset,
        discardMeshBuildStateEndOffset - discardMeshBuildStateOffset
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

