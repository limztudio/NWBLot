// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_contract_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_task_graph_geometry_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_IMPL = "impl";
static constexpr AStringView s_ECS_RENDER = "ecs_render";
static constexpr AStringView s_RAYTRACE = "raytrace";
static constexpr AStringView s_RAYTRACING_SYSTEM_CPP = "raytracing_system.cpp";
static constexpr AStringView s_MESH = "mesh";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


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
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ECS_RENDER / s_RAYTRACE / "shadow_trace_geometry.h", rayTracingHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ECS_RENDER / s_RAYTRACE / s_RAYTRACING_SYSTEM_CPP, rayTracingSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ECS_RENDER / s_RAYTRACE / "shadow_trace_geometry.cpp", freezeSource));

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


// A fresh acceleration-structure backing allocation has the device descriptor's Common state; a retained backing
// instead needs the exact accepted Shadow Preparation handoff. Keep freshness tied to the physical generation so a
// discarded plan retries Common while an accepted packet never fabricates AccelStructRead.
TEST(EcsGraphics, PreparedAccelStructInitialStatesTrackBackingGenerationHandoffs){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString taskGraphSource;
    AString sceneGraphSource;
    AString hardwareCausticsSource;
    AString rayTracingHeaderSource;
    AString rayTracingSource;
    AString swBvhSource;
    AString meshResourcesSource;
    AString rendererStateSource;
    AString systemSource;
    ASSERT_TRUE(ReadRendererSources(
        repoRoot,
        {
            "renderer_frame_pipeline_graph_shadow_prepare.cpp",
            "renderer_frame_pipeline_graph_shadow_visibility.cpp",
            "renderer_frame_pipeline_graph_surfel_gi.cpp",
            "renderer_frame_pipeline_graph.cpp",
            "graph/frame_graph_reflection_resolve.cpp",
        },
        taskGraphSource
    ));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl/ecs_render/raytrace/task_graph_scene_resources.cpp", sceneGraphSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ECS_RENDER / s_RAYTRACE / "raytracing_system.h", rayTracingHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ECS_RENDER / s_RAYTRACE / s_RAYTRACING_SYSTEM_CPP, rayTracingSource));
    ASSERT_TRUE(ReadRendererSources(
        repoRoot,
        {
            "raytrace/rt_swbvh_helpers.h",
            "raytrace/rt_swbvh_mesh_blas.cpp",
            "raytrace/rt_swbvh_mesh_swbvh_prep.cpp",
            "raytrace/rt_swbvh_mesh_build.cpp",
            "raytrace/rt_swbvh_scene_tlas.cpp",
            "raytrace/rt_swbvh_scene_swbvh.cpp",
            "raytrace/rt_swbvh_bvh_infra.cpp",
        },
        swBvhSource
    ));
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ECS_RENDER / s_MESH / "mesh_raytracing_handoff.cpp", meshResourcesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ECS_RENDER / s_RAYTRACE / "renderer_raytracing_state.cpp", rendererStateSource));
    ASSERT_TRUE(ReadRendererFramePipelineRuntimeSources(repoRoot, systemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / s_IMPL / s_ECS_RENDER / s_RAYTRACE / "hardware_caustics_stage_builder.cpp", hardwareCausticsSource));
    const AStringView hardwareCaustics(hardwareCausticsSource.data(), hardwareCausticsSource.size());
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());
    const AStringView sceneGraph(sceneGraphSource.data(), sceneGraphSource.size());
    const AStringView rayTracingHeader(rayTracingHeaderSource.data(), rayTracingHeaderSource.size());
    const AStringView rayTracing(rayTracingSource.data(), rayTracingSource.size());
    const AStringView swBvh(swBvhSource.data(), swBvhSource.size());
    const AStringView meshResources(meshResourcesSource.data(), meshResourcesSource.size());
    const AStringView rendererState(rendererStateSource.data(), rendererStateSource.size());
    const AStringView system(systemSource.data(), systemSource.size());

    EXPECT_TRUE(ContainsText(swBvh, "outBuild.backingFresh = meshResources.blasBackingFresh;"));
    EXPECT_TRUE(ContainsText(swBvh, "meshResources.blasBackingFresh != build.backingFresh"));
    EXPECT_EQ(CountText(swBvh, "meshResources.blasBackingFresh = true;"), 1u);
    EXPECT_TRUE(ContainsText(swBvh, "meshResources.blasBackingFresh = false;"));
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

    EXPECT_TRUE(ContainsText(rendererState, "m_tlasBackingFresh = false;"));
    EXPECT_TRUE(ContainsText(swBvh, "m_rayTracingState.m_tlasBackingFresh = true;"));
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
        "        state.m_tlasBackingFresh = false;"
    ));
    EXPECT_TRUE(ContainsText(
        taskGraph,
        "const Core::ResourceStates::Mask sceneTlasInitialState = m_raytracingSystem.sceneTlasBackingInitialState();"
    ));
    EXPECT_TRUE(ContainsText(taskGraph, "AccelStructResourceDesc(Name(RendererFramePipelineDetail::s_SceneTlasResourceName), RendererFramePipelineDetail::s_SceneTlasResourceLabel).setInitialState(sceneTlasInitialState)"));
    // Refraction and reflection use the neutral importer, forwarding the same generation-aware initial state as
    // the direct shadow, GI, and caustic imports. Include the extracted caustic owner in the generation checks.
    static constexpr AStringView s_SceneTlasImportA = "AccelStructResourceDesc(Name(RendererFramePipelineDetail::s_SceneTlasResourceName), RendererFramePipelineDetail::s_SceneTlasResourceLabel)";
    static constexpr AStringView s_SceneTlasImportB = "RendererTaskGraphDetail::AccelStructResourceDesc(Name(RendererFramePipelineDetail::s_SceneTlasResourceName), RendererFramePipelineDetail::s_SceneTlasResourceLabel)";
    EXPECT_EQ(
        CountText(taskGraph, s_SceneTlasImportA) + CountText(sceneGraph, s_SceneTlasImportB)
            + CountText(hardwareCaustics, s_SceneTlasImportA),
        5u
    );
    // Hardware shadows import early; reflection/refraction import only when that shared read set is still absent.
    EXPECT_EQ(CountText(taskGraph, "sceneReads = ImportRayTracingSceneGraphReads("), s_ExpectedDualCount);
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
    ASSERT_NE(clearPreparedSceneTlasOffset, AStringView::npos);
    ASSERT_NE(capturePreparedSceneTlasOffset, AStringView::npos);
    ASSERT_NE(discardPreflightOffset, AStringView::npos);
    ASSERT_NE(preflightOffset, AStringView::npos);
    ASSERT_NE(commitPersistentStateOffset, AStringView::npos);
    ASSERT_NE(confirmSceneTlasOffset, AStringView::npos);
    ASSERT_NE(confirmMeshBlasOffset, AStringView::npos);
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
    EXPECT_TRUE(ContainsText(discardMeshBuildState, "mesh.blasBuildPending = true;"));
    EXPECT_TRUE(ContainsText(discardMeshBuildState, "mesh.swBvhBuildPending = true;"));
    EXPECT_FALSE(ContainsText(discardMeshBuildState, "mesh.blasBackingFresh = false;"));
    // Candidate creation happens after recording but before native acceptance. A failed prepare callback therefore
    // rejects the graph without publishing state; a failed accepted callback is surfaced by the unified state guard.
    EXPECT_TRUE(ContainsText(
        system,
        "context->stateReady = false;\n"
        "        renderer.m_raytracingSystem.discardPreflightShadowVisibilityResources();\n"
        "        return false;"
    ));
    EXPECT_TRUE(ContainsText(
        system,
        "context->stateReady = renderer.m_shadowPreparePersistentState.commit(*context->stateCandidate);"
    ));
    EXPECT_TRUE(ContainsText(
        system,
        "if(!context->stateReady){\n"
        "        renderer.m_raytracingSystem.discardPreflightShadowVisibilityResources();"
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
    EXPECT_LT(stateReadyFalseOffset, acceptedStateLostOffset);
    EXPECT_LT(commitPersistentStateOffset, acceptedStateLostOffset);
    EXPECT_LT(acceptedStateLostOffset, stateLossRecoveryGuardOffset);
    EXPECT_LT(stateLossRecoveryGuardOffset, stateLossRecoveryOffset);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

